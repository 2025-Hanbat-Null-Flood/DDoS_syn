#ifndef SERVER_H
#define SERVER_H

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
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
    unsigned char last_cmd;   // 마지막 state 값 저장
    long long deadline_ms;
    state_t state;
    long long last_seen_ms;
} Client;

typedef struct {
    fd_set master;
    int listen_fd, maxfd;
    Client C[MAXFD];
    volatile sig_atomic_t running;
    char cmdline[256];
    int  cmdlen;
    uint32_t cur_target_ip;
    uint16_t cur_target_port;
} Server;

// 네트워크 전송용 8바이트 커맨드
typedef struct {
    uint32_t target_ip;   // network byte order
    uint16_t target_port; // network byte order
    uint8_t  state;       // 0=off, 1=on
    uint8_t  padding;     // 항상 0
} command;

// time
long long now_ms(void);

// ui
void ui_init(void);
void ui_shutdown(void);
void render_ui(const Server *S, int listen_port);

// control
int  parse_ip_port(const char *s, uint32_t *ip_be, uint16_t *port_be);
void send_command(Server *S, int fd, uint32_t ip_be, uint16_t port_be, uint8_t state);
void broadcast_command(Server *S, uint32_t ip_be, uint16_t port_be, uint8_t state);
void handle_command(Server *S, char *line);

#endif
