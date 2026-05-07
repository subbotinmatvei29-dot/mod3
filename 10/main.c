#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>

#define MAX_NUMBERS 20

#define PARENT_TO_CHILD 0
#define CHILD_TO_PARENT 1
// структура разд памяти
typedef struct {
    int count;
    int numbers[MAX_NUMBERS];
    int min;
    int max;
    int stop;
} SharedData;

volatile sig_atomic_t stop_flag = 0;  //специальная переменная, которую меняет обработчик сигнала.
// ctrl+C
void handle_sigint(int sig) {
    (void)sig;
    stop_flag = 1;
}

int main(void) {
    int shmid, semid;
    SharedData *data;
    pid_t pid;
    int processed_sets = 0;
    struct sembuf op;
    union semun arg; //Используется для semctl() при установке значения семафора.
    //если придёт сигнал SIGINT, нужно вызвать функцию handle_sigint
    signal(SIGINT, handle_sigint);
    //Создание разделяемой памяти
    shmid = shmget(IPC_PRIVATE, sizeof(SharedData), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }
    //подключает созданный сегмент памяти к адресному пространству процесса.
    data = (SharedData *)shmat(shmid, NULL, 0);
    if (data == (void *) -1) {
        perror("shmat");
        shmctl(shmid, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }

    data->count = 0;
    data->min = 0;
    data->max = 0;
    data->stop = 0;
    //Создаётся набор из двух семафоров.
    semid = semget(IPC_PRIVATE, 2, IPC_CREAT | 0666);
    if (semid == -1) {
        perror("semget");
        shmdt(data);
        shmctl(shmid, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }

    arg.val = 0; //Это значение, которым мы инициализируем семафоры. Оба семафора стартуют с нуля.
    //Инициализация семафоров
    if (semctl(semid, PARENT_TO_CHILD, SETVAL, arg) == -1) {
        perror("semctl");
        shmdt(data);
        shmctl(shmid, IPC_RMID, NULL);
        semctl(semid, 0, IPC_RMID);
        exit(EXIT_FAILURE);
    }

    if (semctl(semid, CHILD_TO_PARENT, SETVAL, arg) == -1) {
        perror("semctl");
        shmdt(data);
        shmctl(shmid, IPC_RMID, NULL);
        semctl(semid, 0, IPC_RMID);
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid == -1) {
        perror("fork");
        shmdt(data);
        shmctl(shmid, IPC_RMID, NULL);
        semctl(semid, 0, IPC_RMID);
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        //Ребёнок игнорирует SIGIN
        signal(SIGINT, SIG_IGN);

        while (1) {
            op.sem_num = PARENT_TO_CHILD;
            op.sem_op = -1;
            op.sem_flg = 0;
            semop(semid, &op, 1);

            if (data->stop) {
                break;
            }

            data->min = data->numbers[0];
            data->max = data->numbers[0];

            for (int i = 1; i < data->count; i++) {
                if (data->numbers[i] < data->min) {
                    data->min = data->numbers[i];
                }
                if (data->numbers[i] > data->max) {
                    data->max = data->numbers[i];
                }
            }
            //Подготовка операции wait:
            op.sem_num = CHILD_TO_PARENT;
            op.sem_op = 1;
            op.sem_flg = 0;
            semop(semid, &op, 1);
        }

        shmdt(data);
        exit(EXIT_SUCCESS);
    }

    srand((unsigned int)(time(NULL) ^ getpid()));

    while (!stop_flag) {
        data->count = rand() % MAX_NUMBERS + 1;

        printf("Сгенерирован набор: ");
        for (int i = 0; i < data->count; i++) {
            data->numbers[i] = rand() % 100;
            printf("%d ", data->numbers[i]);
        }
        printf("\n");
        //Родитель делает post семафору “родитель → ребёнок”.
        op.sem_num = PARENT_TO_CHILD;
        op.sem_op = 1;
        op.sem_flg = 0;
        semop(semid, &op, 1);
        //Теперь родитель блокируется и ждёт, пока ребёнок не скажет, что результат готов.
        op.sem_num = CHILD_TO_PARENT;
        op.sem_op = -1;
        op.sem_flg = 0;
        semop(semid, &op, 1);

        printf("Минимум: %d, максимум: %d\n\n", data->min, data->max);
        processed_sets++;
    }

    data->stop = 1;
    //ребёнок может в этот момент спать на semop() и вообще не знать, что нужно завершаться.
    op.sem_num = PARENT_TO_CHILD;
    op.sem_op = 1;
    op.sem_flg = 0;
    semop(semid, &op, 1);

    waitpid(pid, NULL, 0);

    printf("SIGINT получен. Обработано наборов: %d\n", processed_sets);
    //Очистка ресурсов
    shmdt(data);
    shmctl(shmid, IPC_RMID, NULL); //Удалить сегмент shared memory из системы.
    semctl(semid, 0, IPC_RMID);  //Удалить набор семафоров.

    return 0;
}
