#include "server.h"

static void ui_move_home(void){ printf("\x1b[H"); }
static void ui_clear(void){ printf("\x1b[2J\x1b[H"); }
static void ui_hide_cursor(void){ printf("\x1b[?25l"); }
static void ui_show_cursor(void){ printf("\x1b[?25h"); }

static struct termios tio_orig;
static int tty_ready = 0;

static void tty_raw(void){
    struct termios t;
    tcgetattr(STDIN_FILENO, &tio_orig);
    t = tio_orig;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN]  = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    tty_ready = 1;
}
static void tty_restore(void){
    if(tty_ready){
        tcsetattr(STDIN_FILENO, TCSANOW, &tio_orig);
        tty_ready = 0;
    }
}

void ui_init(void){
    ui_clear();
    ui_hide_cursor();
    tty_raw();
}

void ui_shutdown(void){
    ui_show_cursor();
    tty_restore();
    printf("\n");
}

void render_ui(const Server *S, int listen_port){
    ui_move_home();
    printf("[*] select/TUI server on %d  |  commands: 'set <ip>:<port>', 'start [<fd>]', 'stop [<fd>]' (default all)\n", listen_port);
    printf("--------------------------------------------------------------------------------------------------------------------------------\n");
    printf("%-5s %-21s %-6s %-7s %-7s %-9s %-8s\n", "FD", "Peer", "Err", "Wait", "State", "LastSeen", "Deadline");
    printf("--------------------------------------------------------------------------------------------------------------------------------\n");

    long long t = now_ms();
    for(int fd=0; fd<=S->maxfd; fd++){
        if(!S->C[fd].active) continue;
        char ip[INET_ADDRSTRLEN]; inet_ntop(AF_INET, &S->C[fd].peer.sin_addr, ip, sizeof(ip));
        const char *st = (S->C[fd].state==S_ON)?"ON":(S->C[fd].state==S_OFF)?"OFF":"?";
        long long last = S->C[fd].last_seen_ms? (t - S->C[fd].last_seen_ms) : -1;
        long long dl   = S->C[fd].awaiting? (S->C[fd].deadline_ms - t) : 0;

        printf("%-5d %-21s %-6d %-7s %-7s %-9s %-8s\n",
               fd,
               ip,
               S->C[fd].errcnt,
               S->C[fd].awaiting?"yes":"no",
               st,
               (last>=0)? "0~" : "-",
               S->C[fd].awaiting? ((dl>0)?"0~":"expired") : "-");
    }

    printf("--------------------------------------------------------------------------------------------------------------------------------\n");
    printf("%s%.*s\x1b[K", PROMPT, S->cmdlen, S->cmdline);
    fflush(stdout);
}
