#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 4096
#define LINE_SIZE 1024

static void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}
// отправляем данные в совет полностью
static int send_all(int sock, const void *buffer, size_t length) {
    const char *ptr = (const char *)buffer;
    size_t total_sent = 0;
    // пока не отправили все, продолжаем
    while (total_sent < length) {
        ssize_t sent_now = send(sock, ptr + total_sent, length - total_sent, 0);
        if (sent_now <= 0) {
            return -1;
        }
        total_sent += (size_t)sent_now;
    }

    return 0;
}
// читает данные из сокета до символа \n
static int recv_line(int sock, char *buffer, size_t maxlen) {
    size_t i = 0;

    while (i < maxlen - 1) {
        char c;
        ssize_t n = recv(sock, &c, 1, 0);

        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            break;
        }

        buffer[i++] = c;

        if (c == '\n') {
            break;
        }
    }

    buffer[i] = '\0';
    return (int)i;
}

static int calculate(double a, double b, char op, double *result) {
    switch (op) {
        case '+':
            *result = a + b;
            return 0;

        case '-':
            *result = a - b;
            return 0;

        case '*':
            *result = a * b;
            return 0;

        case '/':
            if (b == 0.0) {
                return 1;
            }
            *result = a / b;
            return 0;

        default:
            return 2;
    }
}
// обработчик команды самой
static int handle_calc_command(const char *buffer, char *response, size_t response_size) {
    char cmd[16];
    char op;
    double a, b, result;
    int parsed;
    int status;

    parsed = sscanf(buffer, "%15s %c %lf %lf", cmd, &op, &a, &b);
    if (parsed != 4) {
        snprintf(response, response_size, "ERROR invalid format\n");
        return -1;
    }

    if (strcmp(cmd, "CALC") != 0) {
        snprintf(response, response_size, "ERROR unknown command\n");
        return -1;
    }

    status = calculate(a, b, op, &result);

    if (status == 1) {
        snprintf(response, response_size, "ERROR division by zero\n");
        return -1;
    }

    if (status == 2) {
        snprintf(response, response_size, "ERROR unknown operation\n");
        return -1;
    }

    snprintf(response, response_size, "RESULT %.6f\n", result);
    return 0;
}
// обработчик команды 
static int handle_file_command(int sock, const char *header, char *response, size_t response_size) {
    char cmd[16];
    char dest_path[LINE_SIZE];
    char buffer[BUFFER_SIZE];
    long file_size;
    long total_received = 0;
    int parsed;
    FILE *fp = NULL;

    parsed = sscanf(header, "%15s %ld", cmd, &file_size);
    if (parsed != 2) {
        snprintf(response, response_size, "ERROR invalid file header\n");
        return -1;
    }

    if (strcmp(cmd, "FILE") != 0) {
        snprintf(response, response_size, "ERROR unknown command\n");
        return -1;
    }

    if (file_size < 0) {
        snprintf(response, response_size, "ERROR invalid file size\n");
        return -1;
    }

    if (recv_line(sock, dest_path, sizeof(dest_path)) <= 0) {
        snprintf(response, response_size, "ERROR reading destination path\n");
        return -1;
    }

    dest_path[strcspn(dest_path, "\r\n")] = '\0';

    if (dest_path[0] == '\0') {
        snprintf(response, response_size, "ERROR empty destination path\n");
        return -1;
    }

    fp = fopen(dest_path, "wb");
    if (fp == NULL) {
        snprintf(response, response_size, "ERROR cannot open destination file\n");
        return -1;
    }

    while (total_received < file_size) {
        long remaining = file_size - total_received;
        size_t to_read = (remaining < (long)sizeof(buffer))
                             ? (size_t)remaining
                             : sizeof(buffer);

        ssize_t bytes_recv = recv(sock, buffer, to_read, 0);
        if (bytes_recv < 0) {
            fclose(fp);
            snprintf(response, response_size, "ERROR receiving file data\n");
            return -1;
        }

        if (bytes_recv == 0) {
            fclose(fp);
            snprintf(response, response_size,
                     "ERROR connection closed during file transfer\n");
            return -1;
        }

        if (fwrite(buffer, 1, (size_t)bytes_recv, fp) != (size_t)bytes_recv) {
            fclose(fp);
            snprintf(response, response_size, "ERROR writing file\n");
            return -1;
        }

        total_received += bytes_recv;
    }

    if (fclose(fp) != 0) {
        snprintf(response, response_size, "ERROR closing destination file\n");
        return -1;
    }

    snprintf(response, response_size, "OK FILE RECEIVED\n");
    return 0;
}
// главная функция обработки одного клиента
static void handle_client(int sock) {
    char line[LINE_SIZE];
    char response[LINE_SIZE];
    char cmd[16];
    int n;

    n = recv_line(sock, line, sizeof(line)); // читает данные из сокета
    if (n < 0) {
        perror("ERROR reading command");
        return;
    }

    if (n == 0) {
        printf("Client disconnected before sending a command\n");
        return;
    }

    if (sscanf(line, "%15s", cmd) != 1) {
        snprintf(response, sizeof(response), "ERROR empty command\n");
        send_all(sock, response, strlen(response));
        return;
    }

    if (strcmp(cmd, "CALC") == 0) {
        handle_calc_command(line, response, sizeof(response));
        if (send_all(sock, response, strlen(response)) != 0) {
            perror("ERROR sending calc response");
        }
    } else if (strcmp(cmd, "FILE") == 0) {
        handle_file_command(sock, line, response, sizeof(response));
        if (send_all(sock, response, strlen(response)) != 0) {
            perror("ERROR sending file response");
        }
    } else if (strcmp(cmd, "QUIT") == 0) {
        snprintf(response, sizeof(response), "OK BYE\n");
        if (send_all(sock, response, strlen(response)) != 0) {
            perror("ERROR sending quit response");
        }
    } else {
        snprintf(response, sizeof(response), "ERROR unknown command\n");
        if (send_all(sock, response, strlen(response)) != 0) {
            perror("ERROR sending error response");
        }
    }
}

int main(int argc, char *argv[]) {
    int sockfd, newsockfd;
    int portno;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    signal(SIGCHLD, SIG_IGN);

    portno = atoi(argv[1]);
    if (portno <= 0) {
        fprintf(stderr, "ERROR: invalid port\n");
        exit(EXIT_FAILURE);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0); 
    if (sockfd < 0) {
        error("ERROR opening socket");
    }

    {
        int opt = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            close(sockfd);
            error("ERROR setting SO_REUSEADDR");
        }
    }
    // обнуляет всю структуру
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY; //Слушаем все сетевые карты
    serv_addr.sin_port = htons((uint16_t)portno);
    // связывается с адресом и портом
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sockfd);
        error("ERROR on binding");
    }
    // Переходим в режим ожидания. "5" — это размер очереди входящих запросов.
    if (listen(sockfd, 5) < 0) {
        close(sockfd);
        error("ERROR on listen");
    }

    printf("TCP server is listening on port %d\n", portno);

    clilen = sizeof(cli_addr);

    while (1) {
        pid_t pid;
        char client_ip[INET_ADDRSTRLEN];
        //  Программа спит на ней, пока не придет клиент. 
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0) {
            perror("ERROR on accept");
            continue;
        }

        if (inet_ntop(AF_INET, &cli_addr.sin_addr, client_ip, sizeof(client_ip)) != NULL) {
            printf("New connection from %s:%d\n",
                   client_ip, ntohs(cli_addr.sin_port));
        } else {
            printf("New connection from unknown client\n");
        }

        pid = fork();
        if (pid < 0) {
            perror("ERROR on fork");
            close(newsockfd);
            continue;
        }

        if (pid == 0) {
            close(sockfd);
            handle_client(newsockfd);
            close(newsockfd);
            _exit(0);
        }

        close(newsockfd);
    }

    close(sockfd);
    return 0;
}