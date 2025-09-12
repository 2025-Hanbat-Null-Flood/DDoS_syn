// client_recv_only.c
// 빌드: gcc client_recv_only.c -o client -lpthread
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#define BUF 1024
#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 8080

static _Atomic int running = 1;

// 수신 스레드: 서버 메시지 출력
static void* recv_thread(void* arg) {
    int sock = *(int*)arg;
    char buf[BUF];

    for (;;) {
        // running이 0이면 정리
        if (!running) break;

        ssize_t n = recv(sock, buf, sizeof(buf)-1, 0);
        if (n <= 0) {
            // shutdown() 또는 서버 종료로 깨어남
            if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
                // 타임아웃/신호면 루프 계속
                continue;
            }
            // 그 외 에러 또는 정상 종료
            running = 0;
            break;
        }
        buf[n] = '\0';
        printf("[SERVER] %s", buf);
        if (buf[n-1] != '\n') putchar('\n');
    }
    return NULL;
}

int main(void) {
    // 1) 소켓 생성
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) { perror("socket"); return 1; }

    // 2) 서버 주소 설정
    struct sockaddr_in sa; memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &sa.sin_addr) <= 0) {
        perror("inet_pton"); close(sock); return 1;
    }

    // 3) 연결
    if (connect(sock, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
        perror("connect"); close(sock); return 1;
    }

    // 4) recv 타임아웃(선택): 200ms
    struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    puts("서버에 연결됨. 'q' 입력 시 종료.");
    pthread_t th;
    if (pthread_create(&th, NULL, recv_thread, &sock) != 0) {
        perror("pthread_create"); close(sock); return 1;
    }

    // 5) 메인 스레드: 종료 키 감시
    for (;;) {
        int c = getchar();
        if (c == 'q' || c == EOF) {
            running = 0;
            // 핵심: 블로킹 recv를 깨우기 위해 반이중 종료
            shutdown(sock, SHUT_RDWR);  // 읽기/쓰기 모두 종료 -> recv()가 0 또는 에러로 반환
            break;
        }
    }

    // 6) 종료 처리
    pthread_join(th, NULL);
    close(sock);
    return 0;
}
