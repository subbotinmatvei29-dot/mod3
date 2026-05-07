#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#define BUFFER_SIZE 65536

static volatile sig_atomic_t keep_running = 1;

static void handle_sigint(int signo) {
    (void)signo;
    keep_running = 0;
}

int main(int argc, char *argv[]) {
    int sockfd;
    int target_port;
    unsigned char buffer[BUFFER_SIZE];

    struct sockaddr_in src_addr;
    socklen_t src_len = sizeof(src_addr);

    char src_ip_str[INET_ADDRSTRLEN];
    char dst_ip_str[INET_ADDRSTRLEN];

    char *endptr = NULL;
    long port_value;

    if (argc != 2) {
        fprintf(stderr, "Usage: sudo %s <target_port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    port_value = strtol(argv[1], &endptr, 10);
    if (*argv[1] == '\0' || *endptr != '\0' || port_value < 1 || port_value > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    target_port = (int)port_value;

    if (signal(SIGINT, handle_sigint) == SIG_ERR) {
        perror("signal");
        return EXIT_FAILURE;
    }

    sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    printf("RAW socket created successfully.\n");
    printf("Waiting for UDP packets to destination port %d...\n", target_port);
    printf("Press Ctrl+C to stop.\n");

    while (keep_running) {
        src_len = sizeof(src_addr);
        // получение пакета (читаем пакет из сокета)
        ssize_t packet_len = recvfrom(sockfd,
                                      buffer,
                                      sizeof(buffer),
                                      0,
                                      (struct sockaddr *)&src_addr,
                                      &src_len);

        if (packet_len < 0) {
            perror("recvfrom");
            close(sockfd);
            return EXIT_FAILURE;
        }

        if (packet_len < (ssize_t)sizeof(struct iphdr)) {
            continue;
        }

        struct iphdr *ip = (struct iphdr *)buffer;

        if (ip->version != 4) {
            continue;
        }

        if (ip->protocol != IPPROTO_UDP) {
            continue;
        }

        int ip_header_len = ip->ihl * 4;
        if (ip_header_len < (int)sizeof(struct iphdr)) {
            continue;
        }

        if (packet_len < ip_header_len + (ssize_t)sizeof(struct udphdr)) {
            continue;
        }

        struct udphdr *udp = (struct udphdr *)(buffer + ip_header_len);

        int src_port = ntohs(udp->source);
        int dst_port = ntohs(udp->dest);
        int udp_len = ntohs(udp->len);

        if (dst_port != target_port) {
            continue;
        }

        if (udp_len < (int)sizeof(struct udphdr)) {
            continue;
        }
        //Вычисление положения и длины payload
        int payload_offset = ip_header_len + (int)sizeof(struct udphdr);
        int payload_len = udp_len - (int)sizeof(struct udphdr);
        int max_payload_len = (int)packet_len - payload_offset;

        if (max_payload_len < 0) {
            continue;
        }

        if (payload_len > max_payload_len) {
            payload_len = max_payload_len;
        }

        if (payload_len < 0) {
            continue;
        }

        unsigned char *payload = buffer + payload_offset;

        struct in_addr src_ip_addr;
        struct in_addr dst_ip_addr;
        src_ip_addr.s_addr = ip->saddr;
        dst_ip_addr.s_addr = ip->daddr;

        if (inet_ntop(AF_INET, &src_ip_addr, src_ip_str, sizeof(src_ip_str)) == NULL) {
            strcpy(src_ip_str, "unknown");
        }

        if (inet_ntop(AF_INET, &dst_ip_addr, dst_ip_str, sizeof(dst_ip_str)) == NULL) {
            strcpy(dst_ip_str, "unknown");
        }

        printf("\n=== Target UDP packet detected ===\n");
        printf("Total packet size: %zd bytes\n", packet_len);
        printf("IP header length: %d bytes\n", ip_header_len);
        printf("Source IP: %s\n", src_ip_str);
        printf("Destination IP: %s\n", dst_ip_str);
        printf("Source port: %d\n", src_port);
        printf("Destination port: %d\n", dst_port);
        printf("UDP length: %d bytes\n", udp_len);
        printf("Payload length: %d bytes\n", payload_len);
        printf("Message: ");

        if (payload_len > 0) {
            fwrite(payload, 1, (size_t)payload_len, stdout);
        } else {
            printf("<empty>");
        }
        printf("\n");

        fflush(stdout);
    }

    close(sockfd);
    printf("\nSniffer stopped.\n");
    return EXIT_SUCCESS;
}