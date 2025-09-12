// server_select_welcome.c
// 기능: 다중 접속 처리(select), 접속 즉시 환영 메시지 전송, 에코(optional)
// 빌드: gcc server_select_welcome.c -o server
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define PORT 8080           // 수신 포트
#define BUF  1024           // I/O 버퍼
#define BACKLOG 128         // listen 대기열

static int send_all(int fd, const void *p, size_t n) {
    // 부분 전송 대비 안전한 송신 루프
    const char *buf = (const char*)p;
    size_t off = 0;
    while (off < n) {
        ssize_t w = send(fd, buf + off, n - off, 0);
        if (w < 0) {
            if (errno == EINTR) continue; // 신호 중단 시 재시도
            return -1;                    // 실제 에러
        }
        off += (size_t)w;
    }
    return 0;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN); // 끊긴 소켓에 send 시 프로세스 종료 방지

    // 1) 수신 소켓 생성
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) { perror("socket"); return 1; }

    // 2) 재바인드 허용
    int yes = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("setsockopt"); close(listen_fd); return 1;
    }

    // 3) 바인드 주소 구성: 0.0.0.0:PORT
    struct sockaddr_in sa; memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(PORT);
    inet_pton(AF_INET, "0.0.0.0", &sa.sin_addr);

    if (bind(listen_fd, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
        perror("bind"); close(listen_fd); return 1;
    }
    if (listen(listen_fd, BACKLOG) == -1) {
        perror("listen"); close(listen_fd); return 1;
    }

    // 4) select용 FD 집합 준비
    fd_set master, ready;
    FD_ZERO(&master);
    FD_SET(listen_fd, &master);
    int max_fd = listen_fd;

    printf("Server listening on %d\n", PORT);

    // 5) 이벤트 루프
    char buf[BUF];
    for (;;) {
        ready = master; // select가 집합을 소모하므로 매번 복사
        int nready = select(max_fd + 1, &ready, NULL, NULL, NULL);
        if (nready == -1) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // 준비된 FD 순회
        for (int fd = 0; fd <= max_fd && nready > 0; ++fd) {
            if (!FD_ISSET(fd, &ready)) continue;
            --nready;

            if (fd == listen_fd) {
                // 5-1) 새 연결 수락
                struct sockaddr_in cli; socklen_t clen = sizeof(cli);
                int cfd = accept(listen_fd, (struct sockaddr*)&cli, &clen);
                if (cfd == -1) { perror("accept"); continue; }

                // 새 FD 감시 추가
                FD_SET(cfd, &master);
                if (cfd > max_fd) max_fd = cfd;

                // 접속 로그
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
                printf("[+] connected %s:%d (fd=%d)\n", ip, ntohs(cli.sin_port), cfd);

                // 접속 즉시 환영 메시지 전송
                const char *welcome = "Hello from server\n";
                if (send_all(cfd, welcome, strlen(welcome)) == -1) {
                    perror("send(welcome)");
                    FD_CLR(cfd, &master);
                    close(cfd);
                }
            } else {
                // 5-2) 기존 클라이언트 데이터 수신
                ssize_t n = recv(fd, buf, sizeof(buf), 0);
                if (n <= 0) {
                    // 종료 또는 에러
                    if (n < 0) perror("recv");
                    printf("[-] close fd=%d\n", fd);
                    FD_CLR(fd, &master);
                    close(fd);
                } else {
                    // 서버 콘솔에 수신 내용 출력
                    fwrite(buf, 1, (size_t)n, stdout);
                    if (buf[n-1] != '\n') putchar('\n');

                    // 에코가 필요하면 아래 주석 해제
                    // if (send_all(fd, buf, (size_t)n) == -1) {
                    //     perror("send(echo)");
                    //     FD_CLR(fd, &master);
                    //     close(fd);
                    // }
                }
            }
        }
    }

    // 6) 정리
    for (int fd = 0; fd <= max_fd; ++fd) if (FD_ISSET(fd, &master)) close(fd);
    return 0;
}
