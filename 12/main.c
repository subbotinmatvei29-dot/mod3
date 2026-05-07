#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/wait.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    int sockfd;
    int my_port, peer_port;
    pid_t pid;

    char buffer[BUFFER_SIZE];

    struct sockaddr_in my_addr;
    struct sockaddr_in peer_addr;
    struct sockaddr_in sender_addr;
    socklen_t sender_len;

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <my_port> <peer_ip> <peer_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    my_port = atoi(argv[1]);
    peer_port = atoi(argv[3]);
    // Создаём UDP-сокет
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    // Настраиваем локальный адрес и привязываем сокет к своему порту
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(my_port);
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
     // Настраиваем адрес собеседника, куда будем отправлять сообщения
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(peer_port);

    if (inet_pton(AF_INET, argv[2], &peer_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid peer IP address: %s\n", argv[2]);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    // дочерний принимает сообщения, родительский отправляет
    pid = fork();
    if (pid < 0) {
        perror("fork");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        printf("Receiver process started. Waiting for messages...\n");

        while (1) {
            sender_len = sizeof(sender_addr);

            ssize_t n = recvfrom(sockfd,
                                 buffer,
                                 BUFFER_SIZE - 1,
                                 0,
                                 (struct sockaddr *)&sender_addr,
                                 &sender_len);

            if (n < 0) {
                perror("recvfrom");
                close(sockfd);
                exit(EXIT_FAILURE);
            }
            // Делаем принятые данные корректной строкой
            buffer[n] = '\0';
            printf("\nFriend: %s", buffer);
            fflush(stdout);
        }
    } else {
        printf("Sender process started. You can type messages:\n");

        while (1) {
            printf("You: ");
            fflush(stdout);

            if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
                break;
            }

            if (sendto(sockfd,
                       buffer,
                       strlen(buffer),
                       0,
                       (struct sockaddr *)&peer_addr,
                       sizeof(peer_addr)) < 0) {
                perror("sendto");
                kill(pid, SIGTERM);
                close(sockfd);
                wait(NULL);
                exit(EXIT_FAILURE);
            }
        }
        // При завершении ввода останавливаем процесс-приёмник
        kill(pid, SIGTERM);
        wait(NULL);
        close(sockfd);
    }

    return 0;
}