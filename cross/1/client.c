#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#define BUF_SIZE 65535
#define TEXT_SIZE 1024
#define CLOSE_MSG "__close__"

volatile sig_atomic_t stop_flag = 0;

void handle_signal(int sig)
{
    (void)sig;
    stop_flag = 1;
}

unsigned short checksum(void *data, int len)
{
    unsigned short *ptr = data;
    unsigned int sum = 0;

    while (len > 1) {
        sum += *ptr;
        ptr++;
        len -= 2;
    }

    if (len == 1) {
        sum += *(unsigned char *)ptr;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (unsigned short)(~sum);
}

int send_raw_udp(int sockfd,
                 struct in_addr src_ip,
                 unsigned short src_port,
                 struct in_addr dst_ip,
                 unsigned short dst_port,
                 const char *text)
{
    char packet[sizeof(struct iphdr) + sizeof(struct udphdr) + TEXT_SIZE];
    struct iphdr *ip = (struct iphdr *)packet;
    struct udphdr *udp = (struct udphdr *)(packet + sizeof(struct iphdr));
    char *data = packet + sizeof(struct iphdr) + sizeof(struct udphdr);
    struct sockaddr_in addr;
    int data_len = (int)strlen(text) + 1;
    int packet_len = (int)sizeof(struct iphdr) + (int)sizeof(struct udphdr) + data_len;

    memset(packet, 0, sizeof(packet));
    memcpy(data, text, data_len);

    ip->ihl = 5;
    ip->version = 4;
    ip->tos = 0;
    ip->tot_len = htons(packet_len);
    ip->id = htons(12345);
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = IPPROTO_UDP;
    ip->saddr = src_ip.s_addr;
    ip->daddr = dst_ip.s_addr;
    ip->check = 0;
    ip->check = checksum(ip, sizeof(struct iphdr));

    udp->source = htons(src_port);
    udp->dest = htons(dst_port);
    udp->len = htons(sizeof(struct udphdr) + data_len);
    udp->check = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr = dst_ip;
    addr.sin_port = htons(dst_port);

    if (sendto(sockfd, packet, packet_len, 0, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        return -1;
    }

    return 0;
}
//Ждёт ответ от сервера и копирует текст ответа в буфер reply
int wait_reply(int sockfd,
               struct in_addr server_ip,
               unsigned short server_port,
               unsigned short client_port,
               char *reply)
{
    unsigned char buffer[BUF_SIZE];

    while (!stop_flag) {
        ssize_t n;
        struct iphdr *ip;
        struct udphdr *udp;
        int ip_len;
        int data_len;
        struct in_addr src_ip;
        unsigned short src_port;
        unsigned short dst_port;
        //читаем пакет
        n = recvfrom(sockfd, buffer, sizeof(buffer), 0, NULL, NULL);
        if (n == -1) {
            //если системный вызов прервался сигналом, просто продолжаем.
            if (errno == EINTR) {
                continue;
            }
            perror("recvfrom");
            return -1;
        }

        if (n < (ssize_t)(sizeof(struct iphdr) + sizeof(struct udphdr))) {
            continue;
        }
        //Разбор IP и UDP заголовков
        ip = (struct iphdr *)buffer;
        if (ip->version != 4 || ip->protocol != IPPROTO_UDP) {
            continue;
        }

        ip_len = ip->ihl * 4;
        if (n < ip_len + (ssize_t)sizeof(struct udphdr)) {
            continue;
        }

        udp = (struct udphdr *)(buffer + ip_len);
        //Получаем адрес и порты
        src_ip.s_addr = ip->saddr;
        src_port = ntohs(udp->source);
        dst_port = ntohs(udp->dest);
        //Клиент считает ответом сервера только тот пакет, у которого:источник — нужный IP сервера; порт источника — порт сервера; порт назначения — наш клиентский порт.
        if (src_ip.s_addr != server_ip.s_addr) {
            continue;
        }
        if (src_port != server_port) {
            continue;
        }
        if (dst_port != client_port) {
            continue;
        }

        data_len = (int)n - ip_len - (int)sizeof(struct udphdr);
        if (data_len <= 0) {
            continue;
        }
        if (data_len >= TEXT_SIZE) {
            data_len = TEXT_SIZE - 1;
        }

        memcpy(reply, buffer + ip_len + sizeof(struct udphdr), data_len);
        reply[data_len] = '\0';
        return 0;
    }

    return 1;
}

int main(int argc, char *argv[])
{
    int sockfd;
    int one = 1;
    struct in_addr client_ip;
    struct in_addr server_ip;
    unsigned short client_port;
    unsigned short server_port;
    struct sigaction sa;
    char text[TEXT_SIZE];

    if (argc != 5) {
        fprintf(stderr, "Usage: %s <client_ip> <client_port> <server_ip> <server_port>\n", argv[0]);
        return 1;
    }

    if (inet_pton(AF_INET, argv[1], &client_ip) != 1) {
        fprintf(stderr, "Wrong client ip\n");
        return 1;
    }

    client_port = (unsigned short)atoi(argv[2]);
    if (client_port == 0) {
        fprintf(stderr, "Wrong client port\n");
        return 1;
    }

    if (inet_pton(AF_INET, argv[3], &server_ip) != 1) {
        fprintf(stderr, "Wrong server ip\n");
        return 1;
    }

    server_port = (unsigned short)atoi(argv[4]);
    if (server_port == 0) {
        fprintf(stderr, "Wrong server port\n");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (sockfd == -1) {
        perror("socket");
        return 1;
    }

    if (setsockopt(sockfd, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) == -1) {
        perror("setsockopt");
        close(sockfd);
        return 1;
    }

    printf("Client started\n");

    while (!stop_flag) {
        char reply[TEXT_SIZE];

        printf("> ");
        fflush(stdout);

        if (fgets(text, sizeof(text), stdin) == NULL) {
            break;
        }

        text[strcspn(text, "\n")] = '\0';

        if (send_raw_udp(sockfd, client_ip, client_port, server_ip, server_port, text) == -1) {
            perror("sendto");
            break;
        }

        if (wait_reply(sockfd, server_ip, server_port, client_port, reply) == -1) {
            break;
        }

        if (stop_flag) {
            break;
        }

        printf("Server: %s\n", reply);
    }

    if (send_raw_udp(sockfd, client_ip, client_port, server_ip, server_port, CLOSE_MSG) == -1) {
        perror("sendto close");
    } else {
        printf("Close message sent\n");
    }

    close(sockfd);
    printf("Client stopped\n");
    return 0;
}
