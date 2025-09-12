#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUF_SIZE 1024

int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    char buffer[BUF_SIZE];
    socklen_t addr_len = sizeof(address);

    // IPv4, TCP socket으로 소켓 열고 확인하기
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        exit(1);
    }

    // IPv4, 모든 NIC, 8080 포트 설정
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    // address.sin_addr.s_addr = INADDR_ANY; // 구식 방법
    if (inet_pton(AF_INET, "0.0.0.0", &address.sin_addr) <= 0) {
        perror("inet_pton");
        close(server_fd);
        exit(1);
    }

    // 설정값으로 소켓 바인딩(연결)하고 확인하기
    if (-1 == bind(server_fd, (struct sockaddr*)&address, sizeof(address))) {
        perror("bind");
        close(server_fd);
        exit(1);
    }

    // 연결 요청 대기 상태로 만들고 확인하기
    if (-1 == listen(server_fd, 5)) {
        perror("listen");
        close(server_fd);
        exit(1);
    }

    printf("Server listening on port %d...\n", PORT);

    // 연결 요청시 수락하고 클라이언트 소켓과 연결하고 확인하기
    client_fd = accept(server_fd, (struct sockaddr*)&address, &addr_len);
    if (client_fd == -1) {
        perror("accept");
        close(server_fd);
        exit(1);
    }

    // ㄴ 연결 완료
    // 데이터 주고 받기

    // 데이터 읽고 출력하기
    int n = read(client_fd, buffer, BUF_SIZE); // 읽고, 받은 크기 반환
    buffer[n] = '\0'; // 문자열 끝 표시
    printf("Received: %s\n", buffer); // 출력

    char *msg = "Hello from server";
    write(client_fd, msg, strlen(msg)); // 데이터 보내기

    close(client_fd);
    close(server_fd);
    return 0;
}
