// server_tui_select_raw.c
// 빌드: gcc -O2 -Wall server_tui_select_raw.c -o server
// 기능: select 기반 TUI 서버 + raw 입력
//  - 화면 상단: 연결/상태 테이블 실시간 갱신
//  - 하단 프롬프트: 바이트 단위 입력 처리(백스페이스, 엔터)
//  - 명령: 'all on/off', 'fd <n> on/off', 'kick <n>', 'list', 'help', 'quit'

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define PORT 8080
#define BACKLOG 128
#define MAXFD FD_SETSIZE
#define ECHO_DEADLINE_MS 2000
#define MAX_ERR 3
#define PROMPT "> "

typedef enum { S_UNKNOWN=0, S_OFF=1, S_ON=2 } state_t;

typedef struct {
    int active, fd;
    struct sockaddr_in peer;
    int errcnt;
    int awaiting;
    unsigned char last_cmd;
    long long deadline_ms;
    state_t state;
    long long last_seen_ms;
} Client;

static fd_set master;
static int listen_fd, maxfd;
static Client C[MAXFD];
static volatile sig_atomic_t running = 1;

// 입력 라인 버퍼(프롬프트에 다시 그리기용)
static char cmdline[256];
static int  cmdlen = 0;

// 터미널 모드
static struct termios tio_orig;

static void tty_raw(void){
    struct termios t;
    tcgetattr(STDIN_FILENO, &tio_orig);
    t = tio_orig;
    t.c_lflag &= ~(ICANON | ECHO);  // 라인버퍼/에코 끔
    t.c_cc[VMIN]  = 0;              // non-blocking read
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}
static void tty_restore(void){ tcsetattr(STDIN_FILENO, TCSANOW, &tio_orig); }

static long long now_ms(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec*1000 + ts.tv_nsec/1000000;
}
static void die(const char*msg){ perror(msg); exit(1); }
static void sigint(int s){ (void)s; running=0; }

static void ui_move_home(void){ printf("\x1b[H"); }
static void ui_clear(void){ printf("\x1b[2J\x1b[H"); }
static void ui_hide_cursor(void){ printf("\x1b[?25l"); }
static void ui_show_cursor(void){ printf("\x1b[?25h"); }
static void ui_printf(const char*fmt, ...){
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
}

static void close_client(Client *c){
    if(!c->active) return;
    FD_CLR(c->fd, &master);
    close(c->fd);
    memset(c, 0, sizeof(*c));
}

static void render_ui(void){
    ui_move_home();
    ui_printf("[*] select/TUI server on %d  |  commands: 'all on/off', 'fd <n> on/off', 'kick <n>', 'list', 'help', 'quit'\n", PORT);
    ui_printf("--------------------------------------------------------------------------------------------------------------------------------\n");
    ui_printf("%-5s %-21s %-6s %-7s %-7s %-9s %-8s\n", "FD", "Peer", "Err", "Wait", "State", "LastSeen", "Deadline");
    ui_printf("--------------------------------------------------------------------------------------------------------------------------------\n");

    long long t = now_ms();
    for(int fd=0; fd<=maxfd; fd++){
        if(!C[fd].active) continue;
        char ip[INET_ADDRSTRLEN]; inet_ntop(AF_INET, &C[fd].peer.sin_addr, ip, sizeof(ip));
        int port = ntohs(C[fd].peer.sin_port);
        const char *st = (C[fd].state==S_ON)?"ON":(C[fd].state==S_OFF)?"OFF":"?";
        long long last = C[fd].last_seen_ms? (t - C[fd].last_seen_ms) : -1;
        long long dl   = C[fd].awaiting? (C[fd].deadline_ms - t) : 0;

        ui_printf("%-5d %-21s %-6d %-7s %-7s %-9s %-8s\n",
            fd,
            ip,                     // Peer(간단히 IP만 표시; 필요시 "%s:%d")
            C[fd].errcnt,
            C[fd].awaiting?"yes":"no",
            st,
            (last>=0)? "0~" : "-",
            C[fd].awaiting? ((dl>0)?"0~":"expired") : "-");
    }

    ui_printf("--------------------------------------------------------------------------------------------------------------------------------\n");
    // 프롬프트 + 입력중인 버퍼 재렌더링
    ui_printf("%s%.*s\x1b[K", PROMPT, cmdlen, cmdline); // 커서부터 줄 끝 삭제
    fflush(stdout);
}

static void set_sock_timeouts(int fd, int sec){
    struct timeval tv = {.tv_sec=sec,.tv_usec=0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static void broadcast_cmd(unsigned char cmd){
    long long dl = now_ms() + ECHO_DEADLINE_MS;
    for(int fd=0; fd<=maxfd; fd++){
        if(!C[fd].active) continue;
        ssize_t w = send(fd, &cmd, 1, 0);
        if(w==1){ C[fd].awaiting=1; C[fd].last_cmd=cmd; C[fd].deadline_ms=dl; }
        else { if(++C[fd].errcnt>=MAX_ERR) close_client(&C[fd]); }
    }
}

static void send_cmd_fd(int fd, unsigned char cmd){
    if(fd<0 || fd>maxfd || !C[fd].active) return;
    long long dl = now_ms() + ECHO_DEADLINE_MS;
    ssize_t w = send(fd, &cmd, 1, 0);
    if(w==1){ C[fd].awaiting=1; C[fd].last_cmd=cmd; C[fd].deadline_ms=dl; }
    else { if(++C[fd].errcnt>=MAX_ERR) close_client(&C[fd]); }
}

static void handle_command(char *line){
    // 앞뒤 공백 제거
    while(*line==' '||*line=='\t') line++;
    size_t n=strlen(line);
    while(n>0 && (line[n-1]=='\n'||line[n-1]=='\r'||line[n-1]==' '||line[n-1]=='\t')) line[--n]=0;
    if(!*line) return;

    if(!strcmp(line,"quit")) { running=0; return; }
    if(!strcmp(line,"help") || !strcmp(line,"list")) return;

    if(!strncmp(line,"all ",4)){
        if(strstr(line+4,"on"))  broadcast_cmd('1');
        else if(strstr(line+4,"off")) broadcast_cmd('0');
        return;
    }
    if(!strncmp(line,"fd ",3)){
        int fd=-1; char op[8]={0};
        if(sscanf(line+3, "%d %7s", &fd, op)==2){
            if(!strcmp(op,"on")) send_cmd_fd(fd,'1');
            else if(!strcmp(op,"off")) send_cmd_fd(fd,'0');
        }
        return;
    }
    if(!strncmp(line,"kick ",5)){
        int fd=-1;
        if(sscanf(line+5, "%d", &fd)==1){
            if(fd>=0 && fd<=maxfd && C[fd].active) close_client(&C[fd]);
        }
        return;
    }
}

int main(void){
    signal(SIGINT, sigint);

    // 초기 화면 세팅
    ui_clear(); ui_hide_cursor();
    tty_raw();                         // raw 입력 시작
    atexit(tty_restore);               // 비정상 종료 대비 복구

    // 리스닝 소켓
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_fd<0) die("socket");
    int on=1; setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));

    struct sockaddr_in srv={0};
    srv.sin_family=AF_INET; srv.sin_addr.s_addr=INADDR_ANY; srv.sin_port=htons(PORT);
    if(bind(listen_fd,(struct sockaddr*)&srv,sizeof(srv))<0) die("bind");
    if(listen(listen_fd,BACKLOG)<0) die("listen");

    // select 집합 초기화
    FD_ZERO(&master);
    FD_SET(listen_fd,&master);
    FD_SET(STDIN_FILENO,&master);
    maxfd = (listen_fd>STDIN_FILENO)?listen_fd:STDIN_FILENO;

    render_ui();

    while(running){
        fd_set readfds = master;
        struct timeval tv = {.tv_sec=0,.tv_usec=200*1000}; // 200ms 틱
        int nready = select(maxfd+1, &readfds, NULL, NULL, &tv);
        if(nready<0){
            if(errno==EINTR) continue;
            break;
        }

        // 새 연결
        if(FD_ISSET(listen_fd,&readfds)){
            struct sockaddr_in cli; socklen_t len=sizeof(cli);
            int cfd = accept(listen_fd,(struct sockaddr*)&cli,&len);
            if(cfd>=0){
                set_sock_timeouts(cfd,5);
                FD_SET(cfd,&master);
                if(cfd>maxfd) maxfd=cfd;
                memset(&C[cfd],0,sizeof(C[cfd]));
                C[cfd].active=1; C[cfd].fd=cfd; C[cfd].peer=cli; C[cfd].state=S_UNKNOWN;
            }
        }

        // STDIN: raw 모드 바이트 단위 입력 처리
        if(FD_ISSET(STDIN_FILENO,&readfds)){
            unsigned char ch;
            ssize_t r;
            while((r = read(STDIN_FILENO, &ch, 1)) == 1){
                if(ch=='\r' || ch=='\n'){
                    cmdline[cmdlen]=0;
                    handle_command(cmdline);
                    cmdlen=0; // 입력 버퍼 리셋
                } else if(ch==0x7f || ch==0x08){
                    if(cmdlen>0) cmdlen--;     // 백스페이스
                } else if(ch>=0x20 && ch<0x7f){
                    if(cmdlen < (int)sizeof(cmdline)-1)
                        cmdline[cmdlen++] = ch; // 출력 가능 ASCII
                }
                // 그 외 제어문자 무시
            }
        }

        // 클라이언트 수신
        for(int fd=0; fd<=maxfd; fd++){
            if(!C[fd].active) continue;
            if(!FD_ISSET(fd,&readfds)) continue;

            unsigned char b;
            ssize_t n = recv(fd,&b,1,0);
            if(n>0){
                C[fd].last_seen_ms = now_ms();
                if(C[fd].awaiting && b==C[fd].last_cmd){
                    C[fd].awaiting=0; C[fd].errcnt=0;
                    C[fd].state = (b=='1')?S_ON:S_OFF;
                } else {
                    if(b=='1') C[fd].state=S_ON;
                    else if(b=='0') C[fd].state=S_OFF;
                }
            } else if(n==0){
                close_client(&C[fd]);
            } else {
                if(errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK) { /* ignore */ }
                else { if(++C[fd].errcnt>=MAX_ERR) close_client(&C[fd]); }
            }
        }

        // echo 타임아웃
        long long t = now_ms();
        for(int fd=0; fd<=maxfd; fd++){
            if(!C[fd].active) continue;
            if(C[fd].awaiting && t > C[fd].deadline_ms){
                C[fd].awaiting=0;
                if(++C[fd].errcnt>=MAX_ERR) close_client(&C[fd]);
            }
        }

        // 화면 갱신(입력 줄 포함)
        render_ui();
    }

    // 종료 정리
    ui_show_cursor();
    tty_restore();
    ui_move_home();
    printf("\nbye\n");
    for(int fd=0; fd<=maxfd; fd++) if(C[fd].active) close_client(&C[fd]);
    close(listen_fd);
    return 0;
}
