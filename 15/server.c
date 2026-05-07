#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 4096
#define LINE_SIZE 1024
// перечисление возможных состояний клиента.
typedef enum {
    STATE_WAIT_HEADER = 0,
    STATE_RECV_FILE = 1
} ClientState;
//Структура клиента
typedef struct {
    int fd;
    ClientState state; // на этапе кклиент
    char linebuf[LINE_SIZE];
    size_t line_len;
    char filename[256];
    long file_size;
    long received;
    FILE *fp;
} Client;

static void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}
//может отправить не все данные за один раз.
static int send_all(int sock, const void *buffer, size_t length) {
    const char *ptr = (const char *)buffer;
    size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t sent_now = send(sock, ptr + total_sent, length - total_sent, 0);
        if (sent_now <= 0) {
            return -1;
        }
        total_sent += (size_t)sent_now;
    }

    return 0;
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
//Разбирает строку запроса CALC и формирует строку ответа
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
//Сбрасывает запись клиента в исходное состояние
static void client_reset(Client *cl) {
    cl->fd = -1;
    cl->state = STATE_WAIT_HEADER;
    cl->line_len = 0;
    cl->linebuf[0] = '\0';
    cl->filename[0] = '\0';
    cl->file_size = 0;
    cl->received = 0;
    cl->fp = NULL;
}
//Проходит по массиву клиентов и вызывает client_reset для каждого
static void clients_init(Client clients[], int size) {
    int i;
    for (i = 0; i < size; i++) {
        client_reset(&clients[i]);
    }
}
//Аккуратно удаляет клиента:
static void remove_client(int fd, Client clients[], fd_set *master) {
    if (fd < 0) {
        return;
    }

    if (clients[fd].fp != NULL) {
        fclose(clients[fd].fp);
        clients[fd].fp = NULL;
    }

    close(fd);
    FD_CLR(fd, master);
    client_reset(&clients[fd]);
}

static int safe_filename(const char *name) {
    if (name[0] == '\0') {
        return 0;
    }
    if (strstr(name, "..") != NULL) {
        return 0;
    }
    if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
        return 0;
    }
    return 1;
}

static int finish_with_response(int fd, Client clients[], fd_set *master, const char *response) {
    if (send_all(fd, response, strlen(response)) != 0) {
        perror("ERROR sending response");
    }
    remove_client(fd, clients, master);
    return 0;
}
//Обрабатывает кусок данных, который относится к телу файла. 
static int process_file_bytes(int fd,
                              Client clients[],
                              fd_set *master,
                              const char *data,
                              size_t len) {
    Client *cl = &clients[fd];
    long remaining;
    size_t to_write;

    if (cl->state != STATE_RECV_FILE) {
        return 0;
    }

    if (cl->fp == NULL) {
        return finish_with_response(fd, clients, master, "ERROR internal file state\n");
    }

    remaining = cl->file_size - cl->received;
    if (remaining < 0) {
        return finish_with_response(fd, clients, master, "ERROR invalid file state\n");
    }

    to_write = len;
    if ((long)to_write > remaining) {
        to_write = (size_t)remaining;
    }

    if (to_write > 0) {
        if (fwrite(data, 1, to_write, cl->fp) != to_write) {
            return finish_with_response(fd, clients, master, "ERROR writing file\n");
        }
        cl->received += (long)to_write;
    }

    if (cl->received == cl->file_size) {
        if (fclose(cl->fp) != 0) {
            cl->fp = NULL;
            return finish_with_response(fd, clients, master, "ERROR closing output file\n");
        }
        cl->fp = NULL;
        return finish_with_response(fd, clients, master, "OK FILE RECEIVED\n");
    }

    return 0;
}
//центральная функция разбора первой строки команды
static int handle_header_line(int fd,
                              Client clients[],
                              fd_set *master,
                              const char *line,
                              const char *extra_data,
                              size_t extra_len) {
    char cmd[16];
    char response[LINE_SIZE];
    Client *cl = &clients[fd];

    if (sscanf(line, "%15s", cmd) != 1) {
        return finish_with_response(fd, clients, master, "ERROR empty command\n");
    }

    if (strcmp(cmd, "CALC") == 0) {
        handle_calc_command(line, response, sizeof(response));
        return finish_with_response(fd, clients, master, response);
    }

    if (strcmp(cmd, "FILE") == 0) {
        char parsed_cmd[16];
        char filename[256];
        long file_size;
        int parsed;

        parsed = sscanf(line, "%15s %255s %ld", parsed_cmd, filename, &file_size);
        if (parsed != 3) {
            return finish_with_response(fd, clients, master, "ERROR invalid file header\n");
        }
        if (strcmp(parsed_cmd, "FILE") != 0) {
            return finish_with_response(fd, clients, master, "ERROR unknown command\n");
        }
        if (file_size < 0) {
            return finish_with_response(fd, clients, master, "ERROR invalid file size\n");
        }
        if (!safe_filename(filename)) {
            return finish_with_response(fd, clients, master, "ERROR invalid file name\n");
        }

        strncpy(cl->filename, filename, sizeof(cl->filename) - 1);
        cl->filename[sizeof(cl->filename) - 1] = '\0';
        cl->file_size = file_size;
        cl->received = 0;
        cl->state = STATE_RECV_FILE;

        cl->fp = fopen(cl->filename, "wb");
        if (cl->fp == NULL) {
            return finish_with_response(fd, clients, master, "ERROR cannot open output file\n");
        }

        if (cl->file_size == 0) {
            if (fclose(cl->fp) != 0) {
                cl->fp = NULL;
                return finish_with_response(fd, clients, master, "ERROR closing output file\n");
            }
            cl->fp = NULL;
            return finish_with_response(fd, clients, master, "OK FILE RECEIVED\n");
        }

        if (extra_len > 0) {
            return process_file_bytes(fd, clients, master, extra_data, extra_len);
        }
        return 0;
    }

    if (strcmp(cmd, "QUIT") == 0) {
        return finish_with_response(fd, clients, master, "OK BYE\n");
    }

    return finish_with_response(fd, clients, master, "ERROR unknown command\n");
}
// обработки клиента, когда select сказал, что на его сокете есть данные
static void handle_ready_client(int fd, Client clients[], fd_set *master) {
    char buffer[BUFFER_SIZE];
    ssize_t n;
    Client *cl = &clients[fd];

    n = recv(fd, buffer, sizeof(buffer), 0);
    if (n < 0) {
        perror("ERROR reading from client");
        remove_client(fd, clients, master);
        return;
    }
    if (n == 0) {
        printf("Client on fd %d disconnected\n", fd);
        remove_client(fd, clients, master);
        return;
    }

    if (cl->state == STATE_RECV_FILE) {
        process_file_bytes(fd, clients, master, buffer, (size_t)n);
        return;
    }

    if (cl->state == STATE_WAIT_HEADER) {
        size_t pos = 0;
        while (pos < (size_t)n) {
            if (cl->line_len >= sizeof(cl->linebuf) - 1) {
                finish_with_response(fd, clients, master, "ERROR header too long\n");
                return;
            }

            cl->linebuf[cl->line_len++] = buffer[pos];
            if (buffer[pos] == '\n') {
                size_t extra_start = pos + 1;
                size_t extra_len = (size_t)n - extra_start;

                cl->linebuf[cl->line_len] = '\0';
                handle_header_line(fd, clients, master, cl->linebuf, buffer + extra_start, extra_len);
                return;
            }
            pos++;
        }
    }
}

int main(int argc, char *argv[]) {
    int listener; // след сокет сервера
    int newfd;// сокет нового клиента после accept
    int portno;
    int fd_max;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    fd_set master; // набор активных сокетов
    fd_set read_fds; // копия мастера
    Client clients[FD_SETSIZE];

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    portno = atoi(argv[1]);
    if (portno <= 0) {
        fprintf(stderr, "ERROR: invalid port\n");
        exit(EXIT_FAILURE);
    }

    clients_init(clients, FD_SETSIZE);
    // создание сокета
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        error("ERROR opening socket");
    }

    { //Позволяет быстрее повторно использовать адрес и порт после перезапуска
        int opt = 1;
        if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            close(listener);
            error("ERROR setting SO_REUSEADDR");
        }
    }
    // подготовка адреса сервера
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons((unsigned short)portno);
    // привязка сокета
    if (bind(listener, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(listener);
        error("ERROR on binding");
    }
    //сокет ждёт входящие подключения
    if (listen(listener, 5) < 0) {
        close(listener);
        error("ERROR on listen");
    }
    // очистка селекта
    FD_ZERO(&master);
    FD_ZERO(&read_fds);
    //сервер следит хотя бы за слушающим сокетом.
    FD_SET(listener, &master);
    fd_max = listener;

    printf("TCP multiplexing server is listening on port %d\n", portno);

    while (1) {
        int i;
        // копирование мн-ва из-за селекта
        read_fds = master;

        if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("ERROR on select");
            break;
        }
        // перебор дескрип
        for (i = 0; i <= fd_max; i++) {
            if (!FD_ISSET(i, &read_fds)) {
                continue;
            }
            // пришел новый клиент
            if (i == listener) {
                char client_ip[INET_ADDRSTRLEN];
                clilen = sizeof(cli_addr);
                newfd = accept(listener, (struct sockaddr *)&cli_addr, &clilen);
                if (newfd < 0) {
                    perror("ERROR on accept");
                    continue;
                }

                if (newfd >= FD_SETSIZE) {
                    const char *msg = "ERROR too many clients\n";
                    send_all(newfd, msg, strlen(msg));
                    close(newfd);
                    continue;
                }

                FD_SET(newfd, &master);
                if (newfd > fd_max) {
                    fd_max = newfd;
                }
                client_reset(&clients[newfd]);
                clients[newfd].fd = newfd;

                if (inet_ntop(AF_INET, &cli_addr.sin_addr, client_ip, sizeof(client_ip)) != NULL) {
                    printf("New connection from %s:%d, fd=%d\n",
                           client_ip, ntohs(cli_addr.sin_port), newfd);
                } else {
                    printf("New connection from unknown client, fd=%d\n", newfd);
                }
            } else {
                handle_ready_client(i, clients, &master);
            }
        }
    }

    close(listener);
    return 0;
}
