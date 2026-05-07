#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define Q1_NAME "/q1"
#define Q2_NAME "/q2"

#define MSG_SIZE 256
#define NORMAL_PRIO 1
#define EXIT_PRIO 10

int main() {
    mqd_t q1, q2;
    char buffer[MSG_SIZE];
    unsigned prio;

    q1 = mq_open(Q1_NAME, O_RDWR);
    q2 = mq_open(Q2_NAME, O_RDWR);

    if (q1 == (mqd_t)-1 || q2 == (mqd_t)-1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }

    while (1) {
        // получение сообщеня от первой программы
        if (mq_receive(q1, buffer, MSG_SIZE, &prio) == -1) {
            perror("mq_receive");
            break;
        }
        // проверка на завершение
        if (prio == EXIT_PRIO) {
            printf("prog2: received exit signal\n");
            break;
        }

        printf("prog1 says: %s\n", buffer);

        printf("prog2> ");
        if (fgets(buffer, MSG_SIZE, stdin) == NULL) {
            break;
        }

        buffer[strcspn(buffer, "\n")] = '\0';

        if (strcmp(buffer, "exit") == 0) {
            if (mq_send(q2, buffer, strlen(buffer) + 1, EXIT_PRIO) == -1) {
                perror("mq_send");
                break;
            }
            printf("prog2: exit message sent\n");
            break;
        }

        if (mq_send(q2, buffer, strlen(buffer) + 1, NORMAL_PRIO) == -1) {
            perror("mq_send");
            break;
        }
    }

    mq_close(q1);
    mq_close(q2);

    return 0;
}