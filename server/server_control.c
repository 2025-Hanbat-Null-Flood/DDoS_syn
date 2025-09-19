#include "server.h"

int parse_ip_be(const char *s, uint32_t *out_be){
    struct in_addr a;
    if(inet_pton(AF_INET, s, &a) != 1) return -1;
    *out_be = a.s_addr; // network byte order
    return 0;
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

    if(!strcmp(line,"quit")) { S->running=0; return; }
    if(!strcmp(line,"help") || !strcmp(line,"list")) return;

    // all <ip> <port> on|off
    if(!strncmp(line,"all ",4)){
        char ip[64]={0}, op[8]={0}; int port=0;
        if(sscanf(line+4, "%63s %d %7s", ip, &port, op)==3){
            uint32_t ip_be; uint16_t port_be = htons(port);
            if(parse_ip_be(ip,&ip_be)==0){
                if(!strcmp(op,"on"))      broadcast_command(S, ip_be, port_be, 1);
                else if(!strcmp(op,"off")) broadcast_command(S, ip_be, port_be, 0);
            }
        }
        return;
    }

    // fd <n> <ip> <port> on|off
    if(!strncmp(line,"fd ",3)){
        int fd=-1, port=0; char ip[64]={0}, op[8]={0};
        if(sscanf(line+3, "%d %63s %d %7s", &fd, ip, &port, op)==4){
            uint32_t ip_be; uint16_t port_be = htons(port);
            if(fd>=0 && fd<=S->maxfd && S->C[fd].active && parse_ip_be(ip,&ip_be)==0){
                if(!strcmp(op,"on"))      send_command(S, fd, ip_be, port_be, 1);
                else if(!strcmp(op,"off")) send_command(S, fd, ip_be, port_be, 0);
            }
        }
        return;
    }

    // kick <n>
    if(!strncmp(line,"kick ",5)){
        int fd=-1;
        if(sscanf(line+5, "%d", &fd)==1){
            if(fd>=0 && fd<=S->maxfd && S->C[fd].active){
                FD_CLR(fd, &S->master);
                close(fd);
                memset(&S->C[fd], 0, sizeof(S->C[fd]));
            }
        }
        return;
    }
}
