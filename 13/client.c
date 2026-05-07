#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define LINE_SIZE 1024
#define BUFFER_SIZE 4096

static void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}
// корректно отправлять данные полностью
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
// убираем перевод строки
static void trim_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
    }
}

static int read_line_stdin(const char *prompt, char *buffer, size_t size) {
    printf("%s", prompt);

    if (fgets(buffer, (int)size, stdin) == NULL) {
        return -1;
    }

    trim_newline(buffer);
    return 0;
}
// читает ответ сервера из сокета и печатает его на экран
static void print_server_response(int sock) {
    char response[LINE_SIZE];
    // чтения данных из сокета
    ssize_t bytes_recv = recv(sock, response, sizeof(response) - 1, 0);

    if (bytes_recv < 0) {
        perror("ERROR reading from socket");
        return;
    }

    if (bytes_recv == 0) {
        printf("Server closed connection\n");
        return;
    }

    response[bytes_recv] = '\0';
    printf("Server response: %s", response);
}

static void run_calc_client(int sock) {
    char line[LINE_SIZE];
    char op_line[LINE_SIZE];
    char request[LINE_SIZE];
    double a, b;
    char op;

    if (read_line_stdin("Enter operation (+, -, *, /): ", op_line, sizeof(op_line)) != 0) {
        fprintf(stderr, "ERROR reading operation\n");
        return;
    }

    if (strlen(op_line) != 1) {
        fprintf(stderr, "ERROR: operation must be one character\n");
        return;
    }
    op = op_line[0];

    if (read_line_stdin("Enter first number: ", line, sizeof(line)) != 0) {
        fprintf(stderr, "ERROR reading first number\n");
        return;
    }
    if (sscanf(line, "%lf", &a) != 1) {
        fprintf(stderr, "ERROR: invalid first number\n");
        return;
    }

    if (read_line_stdin("Enter second number: ", line, sizeof(line)) != 0) {
        fprintf(stderr, "ERROR reading second number\n");
        return;
    }
    if (sscanf(line, "%lf", &b) != 1) {
        fprintf(stderr, "ERROR: invalid second number\n");
        return;
    }

    snprintf(request, sizeof(request), "CALC %c %.6f %.6f\n", op, a, b);

    if (send_all(sock, request, strlen(request)) != 0) {
        perror("ERROR sending calculation request");
        return;
    }

    print_server_response(sock);
}
// из полного пути к файлу выделяет только имя файла
static const char *extract_filename(const char *path) {
    const char *slash1 = strrchr(path, '/');
    const char *slash2 = strrchr(path, '\\');
    const char *base = path;

    if (slash1 != NULL && slash2 != NULL) {
        base = (slash1 > slash2) ? slash1 + 1 : slash2 + 1;
    } else if (slash1 != NULL) {
        base = slash1 + 1;
    } else if (slash2 != NULL) {
        base = slash2 + 1;
    }

    return base;
}
// режим передачи файла
static void run_file_client(int sock) {
    char source_path[LINE_SIZE];
    char dest_path[LINE_SIZE];
    char header[LINE_SIZE];
    char buffer[BUFFER_SIZE];
    FILE *fp;
    long file_size;
    size_t bytes_read;

    if (read_line_stdin("Enter source file path: ", source_path, sizeof(source_path)) != 0) {
        fprintf(stderr, "ERROR reading source file path\n");
        return;
    }

    if (source_path[0] == '\0') {
        fprintf(stderr, "ERROR: empty source file path\n");
        return;
    }

    if (read_line_stdin("Enter destination path on server: ", dest_path, sizeof(dest_path)) != 0) {
        fprintf(stderr, "ERROR reading destination path\n");
        return;
    }

    if (dest_path[0] == '\0') {
        fprintf(stderr, "ERROR: empty destination path\n");
        return;
    }

    fp = fopen(source_path, "rb");
    if (fp == NULL) {
        perror("ERROR opening source file");
        return;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("ERROR seeking file");
        fclose(fp);
        return;
    }

    file_size = ftell(fp);
    if (file_size < 0) {
        perror("ERROR getting file size");
        fclose(fp);
        return;
    }

    rewind(fp);

    snprintf(header, sizeof(header), "FILE %ld\n", file_size);

    if (send_all(sock, header, strlen(header)) != 0) {
        perror("ERROR sending file header");
        fclose(fp);
        return;
    }

    {
        char dest_line[LINE_SIZE + 2];
        snprintf(dest_line, sizeof(dest_line), "%s\n", dest_path);

        if (send_all(sock, dest_line, strlen(dest_line)) != 0) {
            perror("ERROR sending destination path");
            fclose(fp);
            return;
        }
    }

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (send_all(sock, buffer, bytes_read) != 0) {
            perror("ERROR sending file data");
            fclose(fp);
            return;
        }
    }

    if (ferror(fp)) {
        perror("ERROR reading source file");
        fclose(fp);
        return;
    }

    fclose(fp);
    print_server_response(sock);
}
// меню)
static void run_client_menu(int sock) {
    char line[LINE_SIZE];
    int choice;

    printf("Choose mode:\n");
    printf("1 - Calculation\n");
    printf("2 - Send file\n");

    if (read_line_stdin("Your choice: ", line, sizeof(line)) != 0) {
        fprintf(stderr, "ERROR reading menu choice\n");
        return;
    }

    if (sscanf(line, "%d", &choice) != 1) {
        fprintf(stderr, "ERROR: invalid menu choice\n");
        return;
    }

    if (choice == 1) {
        run_calc_client(sock);
    } else if (choice == 2) {
        run_file_client(sock);
    } else {
        printf("Invalid choice\n");
    }
}

int main(int argc, char *argv[]) {
    int my_sock, portno;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <hostname> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    portno = atoi(argv[2]);
    if (portno <= 0) {
        fprintf(stderr, "ERROR: invalid port\n");
        exit(EXIT_FAILURE);
    }

    my_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (my_sock < 0) {
        error("ERROR opening socket");
    }

    server = gethostbyname(argv[1]);
    if (server == NULL) {
        close(my_sock);
        fprintf(stderr, "ERROR, no such host\n");
        exit(EXIT_FAILURE);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, (size_t)server->h_length);
    serv_addr.sin_port = htons((uint16_t)portno);

    if (connect(my_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(my_sock);
        error("ERROR connecting");
    }

    run_client_menu(my_sock);

    close(my_sock);
    return 0;
}