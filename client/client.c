#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUF 1024
static _Atomic int running = 1;

void* recv_thread(void* arg){
    int sock = *(int*)arg;
    char buf[BUF];
    while (running) {
        int n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) { running = 0; break; }
        if (n >= 4 && !memcmp(buf,"PING",4)) {
            // 이벤트 처리. 큐에 넣거나 즉시 실행.
            puts("[PING] 작업 실행");
        }
    }
    return NULL;
}

int main(){
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = { .sin_family=AF_INET, .sin_port=htons(8080) };
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
    connect(sock, (struct sockaddr*)&sa, sizeof(sa));

    pthread_t th; pthread_create(&th, NULL, recv_thread, &sock);

    // 메인 스레드: 다른 일 수행. 종료 감시.
    puts("q 입력 시 종료");
    for (;;) {
        int c = getchar();
        if (c=='q') { running = 0; break; }
        if (c==EOF) { running = 0; break; }
        // 기타 작업 처리 가능
    }
    pthread_join(th, NULL);
    close(sock);
}
