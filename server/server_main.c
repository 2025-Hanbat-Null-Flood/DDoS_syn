#include "server.h"

static void die(const char*msg){ perror(msg); exit(1); }
static void sigint_handler(int s){ (void)s; } // running은 루프 내에서 체크

long long now_ms(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec*1000 + ts.tv_nsec/1000000;
}

static void set_sock_timeouts(int fd, int sec){
    struct timeval tv = {.tv_sec=sec,.tv_usec=0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

int main(void){
    Server S;
    memset(&S, 0, sizeof(S));
    S.running = 1;

    signal(SIGINT, sigint_handler);

    ui_init();

    // 리스닝 소켓
    S.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(S.listen_fd<0) die("socket");
    int on=1; setsockopt(S.listen_fd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));

    struct sockaddr_in srv={0};
    srv.sin_family=AF_INET; srv.sin_addr.s_addr=INADDR_ANY; srv.sin_port=htons(PORT);
    if(bind(S.listen_fd,(struct sockaddr*)&srv,sizeof(srv))<0) die("bind");
    if(listen(S.listen_fd,BACKLOG)<0) die("listen");

    // select 집합
    FD_ZERO(&S.master);
    FD_SET(S.listen_fd,&S.master);
    FD_SET(STDIN_FILENO,&S.master);
    S.maxfd = (S.listen_fd>STDIN_FILENO)?S.listen_fd:STDIN_FILENO;

    render_ui(&S, PORT);

    while(S.running){
        fd_set readfds = S.master;
        struct timeval tv = {.tv_sec=0,.tv_usec=200*1000};
        int nready = select(S.maxfd+1, &readfds, NULL, NULL, &tv);
        if(nready<0){
            if(errno==EINTR) continue;
            break;
        }

        // 새 연결
        if(FD_ISSET(S.listen_fd,&readfds)){
            struct sockaddr_in cli; socklen_t len=sizeof(cli);
            int cfd = accept(S.listen_fd,(struct sockaddr*)&cli,&len);
            if(cfd>=0){
                set_sock_timeouts(cfd,5);
                FD_SET(cfd,&S.master);
                if(cfd>S.maxfd) S.maxfd=cfd;
                memset(&S.C[cfd],0,sizeof(S.C[cfd]));
                S.C[cfd].active=1; S.C[cfd].fd=cfd; S.C[cfd].peer=cli; S.C[cfd].state=S_UNKNOWN;
            }
        }

        // STDIN raw 입력
        if(FD_ISSET(STDIN_FILENO,&readfds)){
            unsigned char ch; ssize_t r;
            while((r = read(STDIN_FILENO, &ch, 1)) == 1){
                if(ch=='\r' || ch=='\n'){
                    S.cmdline[S.cmdlen]=0;
                    handle_command(&S, S.cmdline);
                    S.cmdlen=0;
                } else if(ch==0x7f || ch==0x08){
                    if(S.cmdlen>0) S.cmdlen--;
                } else if(ch>=0x20 && ch<0x7f){
                    if(S.cmdlen < (int)sizeof(S.cmdline)-1) S.cmdline[S.cmdlen++] = ch;
                }
            }
        }

        /* 클라이언트 수신 */
        for (int fd = 0; fd <= S.maxfd; fd++) {
            if (!S.C[fd].active) continue;
            if (!FD_ISSET(fd, &readfds)) continue;
        
            uint8_t ack;
            ssize_t n = recv(fd, &ack, 1, 0);      // 1바이트만 받음
            if (n == 1) {
                S.C[fd].last_seen_ms = now_ms();
            
                /* 데드라인 내 ack 수신 → 에러 카운트 증가 안 함 */
                if (S.C[fd].awaiting) {
                    S.C[fd].awaiting = 0;
                    S.C[fd].errcnt   = 0;
                }
            
                /* 선택: 상태 갱신(클라가 0/1을 보내는 경우) */
                if (ack == 0) S.C[fd].state = S_OFF;
                else if (ack == 1) S.C[fd].state = S_ON;
            
            } else if (n == 0) {
                FD_CLR(fd, &S.master);
                close(fd);
                memset(&S.C[fd], 0, sizeof(S.C[fd]));
            } else {
                if (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK) {
                    /* ignore */
                } else {
                    if (++S.C[fd].errcnt >= MAX_ERR) {
                        FD_CLR(fd, &S.master);
                        close(fd);
                        memset(&S.C[fd], 0, sizeof(S.C[fd]));
                    }
                }
            }
        }

        /* echo 타임아웃 */
        long long t = now_ms();
        for (int fd = 0; fd <= S.maxfd; fd++) {
            if (!S.C[fd].active) continue;
            if (S.C[fd].awaiting && t > S.C[fd].deadline_ms) {
                S.C[fd].awaiting = 0;
                if (++S.C[fd].errcnt >= MAX_ERR) {
                    FD_CLR(fd, &S.master);
                    close(fd);
                    memset(&S.C[fd], 0, sizeof(S.C[fd]));
                }
            }
        }

        render_ui(&S, PORT);
    }

    ui_shutdown();

    for(int fd=0; fd<=S.maxfd; fd++){
        if(S.C[fd].active){
            FD_CLR(fd,&S.master);
            close(fd);
        }
    }
    close(S.listen_fd);
    return 0;
}
