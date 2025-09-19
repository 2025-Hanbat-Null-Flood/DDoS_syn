#include "server.h"
#include <ctype.h>

int parse_ip_port(const char *s, uint32_t *ip_be, uint16_t *port_be){
    /* 형태: a.b.c.d[:port]  (port 생략 불가권장) */
    char buf[64];
    strncpy(buf, s, sizeof(buf)-1); buf[sizeof(buf)-1]=0;
    char *p = strchr(buf, ':');
    int port = 0;
    if(p){
        *p = 0;
        p++;
        for(char *q=p; *q; q++) if(!isdigit((unsigned char)*q)) return -1;
        port = atoi(p);
        if(port<=0 || port>65535) return -1;
    } else {
        return -1; // 포트 필수로 요구
    }

    struct in_addr a;
    if(inet_pton(AF_INET, buf, &a) != 1) return -1;
    *ip_be = a.s_addr;
    *port_be = htons((uint16_t)port);
    return 0;
}

/* 내부 유틸: fd 파싱. "all" 또는 숫자 */
static int parse_fd_arg(const char *s){
    if(!s) return -2; /* missing -> means all (we will treat as -1) */
    if(!strcmp(s, "all")) return -1;
    for(const char *p=s; *p; p++) if(!isdigit((unsigned char)*p)) return -1000;
    return atoi(s);
}

static void on_send_fail(Server *S, int fd){
    if(++S->C[fd].errcnt >= MAX_ERR){
        FD_CLR(fd, &S->master);
        close(fd);
        memset(&S->C[fd], 0, sizeof(S->C[fd]));
    }
}

void send_command(Server *S, int fd, uint32_t ip_be, uint16_t port_be, uint8_t state){
    if(fd<0 || fd> S->maxfd || !S->C[fd].active) return;

    command cmd = {
        .target_ip   = ip_be,
        .target_port = port_be,
        .state       = state ? 1 : 0,
        .padding     = 0
    };

    long long dl = now_ms() + ECHO_DEADLINE_MS;
    ssize_t w = send(fd, &cmd, sizeof(cmd), 0);
    if(w == (ssize_t)sizeof(cmd)){
        S->C[fd].awaiting    = 1;
        S->C[fd].last_cmd    = cmd.state;
        S->C[fd].deadline_ms = dl;
    } else {
        on_send_fail(S, fd);
    }
}

void broadcast_command(Server *S, uint32_t ip_be, uint16_t port_be, uint8_t state){
    for(int fd=0; fd<=S->maxfd; fd++){
        if(!S->C[fd].active) continue;
        send_command(S, fd, ip_be, port_be, state);
    }
}

void handle_command(Server *S, char *line){
    while(*line==' '||*line=='\t') line++;
    size_t n=strlen(line);
    while(n>0 && (line[n-1]=='\n'||line[n-1]=='\r'||line[n-1]==' '||line[n-1]=='\t')) line[--n]=0;
    if(!*line) return;

    /* 정확히 세 커맨드만 허용:
       set <ip>:<port>
       start [<fd>]    (기본 all)
       stop  [<fd>]    (기본 all)
    */
    char cmd[16], arg1[64];
    int cnt = sscanf(line, "%15s %63s", cmd, arg1);

    if(!strcmp(cmd, "quit")){
        S->running = 0;
        return;
    }

    if(!strcmp(cmd, "set")){
        if(cnt < 2){ /* 사용법 무시 */ return; }
        uint32_t ip_be; uint16_t port_be;
        if(parse_ip_port(arg1, &ip_be, &port_be)==0){
            S->cur_target_ip = ip_be;
            S->cur_target_port = port_be;
        }
        return;
    }

    if(!strcmp(cmd, "start") || !strcmp(cmd, "stop")){
        int fd = -1; /* -1 == all */
        if(cnt >= 2){
            int tmp = parse_fd_arg(arg1);
            if(tmp == -1000) return; /* invalid */
            if(tmp == -2) fd = -1;
            else fd = tmp;
        } else {
            fd = -1; /* default all */
        }

        /* 타겟이 설정되어 있지 않으면 무시 */
        if(S->cur_target_ip == 0 || S->cur_target_port == 0) return;

        uint8_t state = (!strcmp(cmd,"start")) ? 1 : 0;
        if(fd == -1){
            /* broadcast to all */
            broadcast_command(S, S->cur_target_ip, S->cur_target_port, state);
        } else {
            if(fd >= 0 && fd <= S->maxfd && S->C[fd].active)
                send_command(S, fd, S->cur_target_ip, S->cur_target_port, state);
        }
        return;
    }

    /* 그 외 명령은 무시 */
    return;
}
