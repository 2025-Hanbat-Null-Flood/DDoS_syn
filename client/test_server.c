// server_for_attack_test.c
// gcc server_for_attack_test.c -o server -lpthread

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

#define PORT 8080
#define MAX_CLIENTS 10

int client_socks[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// 클라이언트 수신 핸들러 (지금은 단순 로그용)
void* handle_client(void* arg) {
    int sock = *(int*)arg;
    char buffer[1024];
    while (1) {
        ssize_t len = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (len <= 0) break;
        buffer[len] = '\0';
        printf("[CLIENT MSG] %s", buffer);
    }

    close(sock);
    pthread_mutex_lock(&lock);
    for (int i = 0; i < client_count; ++i) {
        if (client_socks[i] == sock) {
            client_socks[i] = client_socks[--client_count];
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    printf("[-] 클라이언트 연결 종료\n");
    return NULL;
}

int main() {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) { perror("socket"); exit(1); }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }

    if (listen(server_sock, MAX_CLIENTS) < 0) {
        perror("listen"); exit(1);
    }

    printf("[*] 서버 실행 중. 클라이언트 연결 대기...\n");

    pthread_t client_threads[MAX_CLIENTS];

    // 클라이언트 수락 스레드
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
        if (client_sock < 0) {
            perror("accept");
            continue;
        }

        pthread_mutex_lock(&lock);
        if (client_count < MAX_CLIENTS) {
            client_socks[client_count++] = client_sock;
            pthread_create(&client_threads[client_count], NULL, handle_client, &client_socks[client_count-1]);
            printf("[+] 클라이언트 연결됨: %s\n", inet_ntoa(client_addr.sin_addr));

            // 연결 후 인사 메시지 전송
            char welcome[] = "환영합니다! attack 명령을 기다립니다.\n";
            send(client_sock, welcome, strlen(welcome), 0);
        } else {
            char msg[] = "서버 접속자 수 초과\n";
            send(client_sock, msg, strlen(msg), 0);
            close(client_sock);
        }
        pthread_mutex_unlock(&lock);

        // 명령어 입력 루프
        printf(">> 클라이언트에게 보낼 명령을 입력하세요 (예: attack 127.0.0.1 80 / q):\n");
        char cmd[256];
        if (fgets(cmd, sizeof(cmd), stdin)) {
            if (cmd[0] == 'q') break;
            pthread_mutex_lock(&lock);
            for (int i = 0; i < client_count; ++i) {
                send(client_socks[i], cmd, strlen(cmd), 0);
            }
            pthread_mutex_unlock(&lock);
        }
    }

    // 종료 처리
    for (int i = 0; i < client_count; ++i) close(client_socks[i]);
    close(server_sock);
    printf("[*] 서버 종료\n");
    return 0;
}

