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
// Client *cl
#define BUF_SIZE 65535
#define TEXT_SIZE 1024
#define CLOSE_MSG "__close__"
//Структура клиента
typedef struct Client {
    struct in_addr ip;//Хранит IP-адрес клиента
    unsigned short port;
    int count;
    struct Client *next;//Указатель на следующий элемент списка
} Client;

volatile sig_atomic_t stop_flag = 0;

void handle_signal(int sig)
{
    (void)sig;
    stop_flag = 1;
}
//Считает контрольную сумму для IP-заголовка.
unsigned short checksum(void *data, int len)
{
    unsigned short *ptr = data;
    unsigned int sum = 0;
    //Берём данные по 2 байта и суммируем.
    while (len > 1) {
        sum += *ptr;
        ptr++;
        len -= 2;
    }

    if (len == 1) {
        sum += *(unsigned char *)ptr;
    }
    //переносим старшие биты вниз
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (unsigned short)(~sum);
}

Client *find_client(Client *head, struct in_addr ip, unsigned short port)
{
    while (head != NULL) {
        if (head->ip.s_addr == ip.s_addr && head->port == port) {
            return head;
        }
        head = head->next;
    }
    return NULL;
}
//Создаёт нового клиента и добавляет его в начало списка.
Client *add_client(Client **head, struct in_addr ip, unsigned short port)
{
    Client *node = malloc(sizeof(Client));
    if (node == NULL) {
        return NULL;
    }

    node->ip = ip;
    node->port = port;
    node->count = 0;
    node->next = *head;
    *head = node;
    return node;
}
//Удаляет клиента из списка по ip:port
void remove_client(Client **head, struct in_addr ip, unsigned short port)
{
    Client *cur = *head;
    Client *prev = NULL;

    while (cur != NULL) {
        if (cur->ip.s_addr == ip.s_addr && cur->port == port) {
            if (prev == NULL) {
                *head = cur->next;
            } else {
                prev->next = cur->next;
            }
            free(cur);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}
//Освобождает весь список клиентов перед завершением сервера.
void free_list(Client *head)
{
    Client *tmp;
    while (head != NULL) {
        tmp = head;
        head = head->next;
        free(tmp);
    }
}
//Формирует и отправляет UDP-пакет вручную через raw socket.
int send_raw_udp(int sockfd,
                 struct in_addr src_ip,
                 unsigned short src_port,
                 struct in_addr dst_ip,
                 unsigned short dst_port,
                 const char *text)
{
    //мы вручную собираем полный пакет в памяти, а потом отправляем его одним вызовом sendto()
    char packet[sizeof(struct iphdr) + sizeof(struct udphdr) + TEXT_SIZE]; // локальные переменные
    struct iphdr *ip = (struct iphdr *)packet;
    struct udphdr *udp = (struct udphdr *)(packet + sizeof(struct iphdr));
    char *data = packet + sizeof(struct iphdr) + sizeof(struct udphdr);
    struct sockaddr_in addr;
    int data_len = (int)strlen(text) + 1;
    int packet_len = (int)sizeof(struct iphdr) + (int)sizeof(struct udphdr) + data_len;
    // очищаем буфер, чтобы не было мусора в заголовках
    memset(packet, 0, sizeof(packet));
    memcpy(data, text, data_len);
    // Заполнение IP-заголовка
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
    //Заполнение UDP-заголовка
    udp->source = htons(src_port); // сетевой порядок байтов
    udp->dest = htons(dst_port);
    udp->len = htons(sizeof(struct udphdr) + data_len);
    udp->check = 0;
    //Подготовка адреса для sendto
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr = dst_ip;
    addr.sin_port = htons(dst_port);

    if (sendto(sockfd, packet, packet_len, 0, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int sockfd;
    int one = 1;
    unsigned short server_port;
    Client *clients = NULL;
    unsigned char buffer[BUF_SIZE];
    struct sigaction sa;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_port>\n", argv[0]);
        return 1;
    }

    server_port = (unsigned short)atoi(argv[1]);
    if (server_port == 0) {
        fprintf(stderr, "Wrong port\n");
        return 1;
    }
    //Настройка сигналов
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (sockfd == -1) {
        perror("socket");
        return 1;
    }
    // IP-заголовок уже включён в пакет, я сам его заполнил. (“IP-заголовок я уже формирую сам, не добавляй его автоматически)
    if (setsockopt(sockfd, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) == -1) {
        perror("setsockopt");
        close(sockfd);
        return 1;
    }

    printf("Server started on port %u\n", server_port);

    while (!stop_flag) {
        ssize_t n;
        struct iphdr *ip;
        struct udphdr *udp;
        int ip_len;
        int data_len;
        char text[TEXT_SIZE];
        struct in_addr client_ip;
        struct in_addr server_ip;
        unsigned short client_port;
        unsigned short dst_port;
        char client_ip_str[INET_ADDRSTRLEN];

        n = recvfrom(sockfd, buffer, sizeof(buffer), 0, NULL, NULL);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("recvfrom");
            break;
        }

        if (n < (ssize_t)(sizeof(struct iphdr) + sizeof(struct udphdr))) {
            continue;
        }

        ip = (struct iphdr *)buffer;
        if (ip->version != 4 || ip->protocol != IPPROTO_UDP) {
            continue;
        }
        //Длина IP-заголовка
        ip_len = ip->ihl * 4;
        if (n < ip_len + (ssize_t)sizeof(struct udphdr)) {
            continue;
        }
        //После IP header сразу идёт UDP header.
        udp = (struct udphdr *)(buffer + ip_len);
        // проверка порта назначения
        dst_port = ntohs(udp->dest);
        if (dst_port != server_port) {
            continue;
        }
        // получаем адреса и порты
        client_ip.s_addr = ip->saddr;
        server_ip.s_addr = ip->daddr;
        client_port = ntohs(udp->source);
        //длина данных
        data_len = (int)n - ip_len - (int)sizeof(struct udphdr);
        if (data_len <= 0) {
            continue;
        }
        if (data_len >= TEXT_SIZE) {
            data_len = TEXT_SIZE - 1;
        }

        memcpy(text, buffer + ip_len + sizeof(struct udphdr), data_len);
        text[data_len] = '\0';

        inet_ntop(AF_INET, &client_ip, client_ip_str, sizeof(client_ip_str));

        if (strcmp(text, CLOSE_MSG) == 0) {
            remove_client(&clients, client_ip, client_port);
            printf("Client %s:%u disconnected, counter reset\n", client_ip_str, client_port);
            continue;
        }
        //Ищем клиента по ip:port
        Client *cl = find_client(clients, client_ip, client_port);
        char reply[TEXT_SIZE];
        //Если клиента ещё нет — создаём
        if (cl == NULL) {
            cl = add_client(&clients, client_ip, client_port);
            if (cl == NULL) {
                fprintf(stderr, "malloc error\n");
                continue;
            }
        }

        cl->count++;
        snprintf(reply, sizeof(reply), "%s", text);
        snprintf(reply + strlen(reply), sizeof(reply) - strlen(reply), " %d", cl->count);

        if (send_raw_udp(sockfd, server_ip, server_port, client_ip, client_port, reply) == -1) {
            perror("sendto");
            continue;
        }

        printf("From %s:%u -> %s\n", client_ip_str, client_port, reply);
    }

    free_list(clients);
    close(sockfd);
    printf("Server stopped\n");
    return 0;
}
