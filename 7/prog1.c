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
    struct mq_attr attr;
    char buffer[MSG_SIZE];
    unsigned prio;

    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MSG_SIZE;
    attr.mq_curmsgs = 0;
    // создание открытие очередеей
    q1 = mq_open(Q1_NAME, O_CREAT | O_RDWR, 0666, &attr);
    q2 = mq_open(Q2_NAME, O_CREAT | O_RDWR, 0666, &attr);

    if (q1 == (mqd_t)-1 || q2 == (mqd_t)-1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }

    while (1) {
        printf("prog1> ");
        if (fgets(buffer, MSG_SIZE, stdin) == NULL) {
            break;
        }

        buffer[strcspn(buffer, "\n")] = '\0';

        if (strcmp(buffer, "exit") == 0) {
            if (mq_send(q1, buffer, strlen(buffer) + 1, EXIT_PRIO) == -1) {
                perror("mq_send");
                break;
            }
            printf("prog1: exit message sent\n");
            break;
        }
        // отправка завершения 
        if (mq_send(q1, buffer, strlen(buffer) + 1, NORMAL_PRIO) == -1) {
            perror("mq_send");
            break;
        }
        // получения ответа
        if (mq_receive(q2, buffer, MSG_SIZE, &prio) == -1) {
            perror("mq_receive");
            break;
        }
        // проверка , не пришел ли сигнал завершения
        if (prio == EXIT_PRIO) {
            printf("prog1: received exit signal\n");
            break;
        }

        printf("prog1 received: %s (prio=%u)\n", buffer, prio);
    }

    mq_close(q1);
    mq_close(q2);
    mq_unlink(Q1_NAME);
    mq_unlink(Q2_NAME);

    return 0;
}