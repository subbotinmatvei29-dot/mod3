#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include "chat.h"

int main(int argc, char *argv[]) {
    key_t key;
    int msqid;
    long my_id;
    pid_t pid;
    Message msg;
    char buffer[MAX_TEXT];

    if (argc != 2) {
        fprintf(stderr, "Использование: %s <номер_клиента>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    my_id = atol(argv[1]);

    if (my_id < 20 || my_id % 10 != 0) {
        fprintf(stderr, "Номер клиента должен быть 20, 30, 40, ...\n");
        exit(EXIT_FAILURE);
    }
    // подключение к очереди
    key = ftok(".", 'A');
    if (key == -1) {
        perror("ftok");
        exit(EXIT_FAILURE);
    }

    msqid = msgget(key, 0666); // подключ к существ очереди
    if (msqid == -1) {
        perror("msgget");
        fprintf(stderr, "Сначала запусти сервер.\n");
        exit(EXIT_FAILURE);
    }

    printf("Клиент %ld подключен к очереди.\n", my_id);

    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        while (1) {
            if (msgrcv(msqid, &msg, MSG_SIZE, my_id, 0) == -1) {
                perror("msgrcv");
                exit(EXIT_FAILURE);
            }

            printf("\n[От клиента %ld]: %s\n> ", msg.sender, msg.text);
            fflush(stdout);
        }
    } else {
        while (1) {
            printf("> ");
            fflush(stdout);

            if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
                perror("fgets");
                kill(pid, SIGTERM);
                wait(NULL);
                break;
            }

            buffer[strcspn(buffer, "\n")] = '\0';

            msg.mtype = SERVER_TYPE;
            msg.sender = my_id;
            strncpy(msg.text, buffer, MAX_TEXT - 1);
            msg.text[MAX_TEXT - 1] = '\0';

            if (msgsnd(msqid, &msg, MSG_SIZE, 0) == -1) {
                perror("msgsnd");
                continue;
            }

            if (strcmp(buffer, "shutdown") == 0) {
                kill(pid, SIGTERM);
                wait(NULL);
                break;
            }
        }

        printf("Клиент %ld завершил работу.\n", my_id);
    }

    return 0;
}