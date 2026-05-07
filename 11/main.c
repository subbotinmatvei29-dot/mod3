#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <semaphore.h>
#include<fcntl.h>
#include<sys/mman.h>
#include<sys/stat.h>
#include<errno.h>
#include<string.h>

#define MAX_NUMBERS 20
#define SHM_NAME "/my_shm"
#define SEM_PARENT_TO_CHILD "/sem_parent_to_child"
#define SEM_CHILD_TO_PARENT "/sem_child_to_parent"
#define SEM_MUTEX "/sem_mutex"

typedef struct {
    int count;
    int numbers[MAX_NUMBERS];
    int min;
    int max;
    int stop;
} SharedData;

volatile sig_atomic_t stop_flag = 0;

void handle_sigint(int sig) {
    (void)sig;
    stop_flag = 1;
}

int main(void) {
    SharedData *data=MAP_FAILED;
    pid_t pid;
    int processed_sets = 0;
    int shm_fd;
    sem_t *parent_to_child=SEM_FAILED;
    sem_t *child_to_parent=SEM_FAILED;
    sem_t *mutex=SEM_FAILED; // дверь с ключом, взаимное исключение))

    //Удаляем старые объекты на случай предыдущего некорректного завершения
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_PARENT_TO_CHILD);
    sem_unlink(SEM_CHILD_TO_PARENT);
    sem_unlink(SEM_MUTEX);

    signal(SIGINT, handle_sigint);
    // создаем shared memory
    shm_fd = shm_open(SHM_NAME,  O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }
    // Задаём размер shared memory под структуру SharedData
    if (ftruncate(shm_fd, sizeof(SharedData))==-1){
        perror("ftruncate");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        exit(EXIT_FAILURE);
    }
    // Отображаем shared memory в адресное пространство процесса
    data = mmap(NULL, sizeof(SharedData),
            PROT_READ | PROT_WRITE,
            MAP_SHARED, shm_fd, 0);
    if (data == (MAP_FAILED)) {
        perror("mmap");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        exit(EXIT_FAILURE);
    }
    memset(data, 0, sizeof(SharedData));
     // Семафор: родитель сообщает ребёнку, что данные готовы
    parent_to_child = sem_open(SEM_PARENT_TO_CHILD, O_CREAT | O_EXCL, 0666, 0);
    if (parent_to_child == SEM_FAILED) {
        perror("sem_open parent_to_child");
        munmap(data, sizeof(SharedData));
        close(shm_fd);
        shm_unlink(SHM_NAME);
        exit(EXIT_FAILURE);
    }
    // Семафор: ребёнок сообщает родителю, что результат готов
    child_to_parent = sem_open(SEM_CHILD_TO_PARENT, O_CREAT | O_EXCL, 0666, 0);
    if (child_to_parent == SEM_FAILED) {
        perror("sem_open child_to_parent");
        sem_close(parent_to_child);
        sem_unlink(SEM_PARENT_TO_CHILD);
        munmap(data, sizeof(SharedData));
        close(shm_fd);
        shm_unlink(SHM_NAME);
        exit(EXIT_FAILURE);
    }
    // Семафор-мьютекс: блокирует доступ к shared memory, пока другой процесс с ней работает
    mutex = sem_open(SEM_MUTEX, O_CREAT | O_EXCL, 0666, 1);
    if (mutex == SEM_FAILED) {
        perror("sem_open mutex");
        sem_close(parent_to_child);
        sem_close(child_to_parent);
        sem_unlink(SEM_PARENT_TO_CHILD);
        sem_unlink(SEM_CHILD_TO_PARENT);
        munmap(data, sizeof(SharedData));
        close(shm_fd);
        shm_unlink(SHM_NAME);
        exit(EXIT_FAILURE);
    }
    pid = fork();
    if (pid == -1) {
        perror("fork");
        sem_close(parent_to_child);
        sem_close(child_to_parent);
        sem_close(mutex);
        sem_unlink(SEM_PARENT_TO_CHILD);
        sem_unlink(SEM_CHILD_TO_PARENT);
        sem_unlink(SEM_MUTEX);
        munmap(data, sizeof(SharedData));
        close(shm_fd);
        shm_unlink(SHM_NAME);
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        signal(SIGINT, SIG_IGN);

        while (1) {
            // Ждём, пока родитель запишет новый набор
            while (sem_wait(parent_to_child) == -1) {
                if (errno == EINTR) {
                    continue;
                }
                perror("sem_wait (child)");
                sem_close(parent_to_child);
                sem_close(child_to_parent);
                sem_close(mutex);
                munmap(data, sizeof(SharedData));
                close(shm_fd);
                exit(EXIT_FAILURE);
            }

            // Захватываем shared memory перед чтением и записью
            while (sem_wait(mutex) == -1) {
                if (errno == EINTR) {
                    continue;
                }
                perror("sem_wait mutex (child)");
                sem_close(parent_to_child);
                sem_close(child_to_parent);
                sem_close(mutex);
                munmap(data, sizeof(SharedData));
                close(shm_fd);
                exit(EXIT_FAILURE);
            }

            if (data->stop) {
                if (sem_post(mutex) == -1) {
                    perror("sem_post mutex (child stop)");
                }
                break;
            }
            // Ищем минимум и максимум в полученном наборе
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

            // Освобождаем shared memory после работы с ней
            if (sem_post(mutex) == -1) {
                perror("sem_post mutex (child)");
                sem_close(parent_to_child);
                sem_close(child_to_parent);
                sem_close(mutex);
                munmap(data, sizeof(SharedData));
                close(shm_fd);
                exit(EXIT_FAILURE);
            }

            // Сообщаем родителю, что результат уже в shared memory
            if (sem_post(child_to_parent) == -1) {
                perror("sem_post (child)");
                sem_close(parent_to_child);
                sem_close(child_to_parent);
                sem_close(mutex);
                munmap(data, sizeof(SharedData));
                close(shm_fd);
                exit(EXIT_FAILURE);
            }
        }

        sem_close(parent_to_child);
        sem_close(child_to_parent);
        sem_close(mutex);
        munmap(data, sizeof(SharedData));
        close(shm_fd);
        exit(EXIT_SUCCESS);
    }

    srand((unsigned int)(time(NULL) ^ getpid()));

    while (!stop_flag) {
        int locked_for_write = 0;

        // Перед записью родитель тоже захватывает shared memory
        while (!locked_for_write) {
            if (sem_wait(mutex) == 0) {
                locked_for_write = 1;
                break;
            }
            if (errno == EINTR) {
                if (stop_flag) {
                    break;
                }
                continue;
            }
            perror("sem_wait mutex (parent write)");
            stop_flag = 1;
            break;
        }

        if (stop_flag) {
            break;
        }

        data->count = rand() % MAX_NUMBERS + 1;

        printf("Сгенерирован набор: ");
        for (int i = 0; i < data->count; i++) {
            data->numbers[i] = rand() % 100;
            printf("%d ", data->numbers[i]);
        }
        printf("\n");

        // После записи родитель освобождает shared memory
        if (sem_post(mutex) == -1) {
            perror("sem_post mutex (parent write)");
            break;
        }

        // Будим ребёнка: данные готовы 
        if (sem_post(parent_to_child) == -1) {
            perror("sem_post (parent)");
            break;
        }
        // Ждём, пока ребёнок закончит вычисления
        while (sem_wait(child_to_parent) == -1) {
            if (errno == EINTR) {
                if (stop_flag) {
                    break;
                }
                continue;
            }
            perror("sem_wait (parent)");
            stop_flag = 1;
            break;
        }

        if (stop_flag) {
            break;
        }

        int locked_for_read = 0;

        // Перед чтением результата родитель снова захватывает shared memory
        while (!locked_for_read) {
            if (sem_wait(mutex) == 0) {
                locked_for_read = 1;
                break;
            }
            if (errno == EINTR) {
                if (stop_flag) {
                    break;
                }
                continue;
            }
            perror("sem_wait mutex (parent read)");
            stop_flag = 1;
            break;
        }

        if (stop_flag) {
            break;
        }

        printf("Минимум: %d, максимум: %d\n\n", data->min, data->max);
        processed_sets++;

        // После чтения результата shared memory снова освобождается
        if (sem_post(mutex) == -1) {
            perror("sem_post mutex (parent read)");
            break;
        }
    }
    //локирует ребёнка до готовности данных, а child_to_parent блокирует родителя до готовности результата
    //для защиты критической секции, то есть shared memory.
    // Сообщаем ребёнку, что пора завершатьс
    // Перед установкой stop тоже блокируем shared memory
    int locked_for_stop = 0;
    while (!locked_for_stop) {
        if (sem_wait(mutex) == 0) {
            locked_for_stop = 1;
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        perror("sem_wait mutex (stop)");
        break;
    }

    if (locked_for_stop) {
        data->stop = 1;

        if (sem_post(mutex) == -1) {
            perror("sem_post mutex (stop)");
        }
    }

    if (sem_post(parent_to_child) == -1) {
        perror("sem_post (parent stop)");
    }

    waitpid(pid, NULL, 0);

    printf("SIGINT получен. Обработано наборов: %d\n", processed_sets);

    sem_close(parent_to_child);
    sem_close(child_to_parent);
    sem_close(mutex);
    sem_unlink(SEM_PARENT_TO_CHILD);
    sem_unlink(SEM_CHILD_TO_PARENT);
    sem_unlink(SEM_MUTEX);

    munmap(data, sizeof(SharedData));
    close(shm_fd);
    shm_unlink(SHM_NAME);

    return 0;
}