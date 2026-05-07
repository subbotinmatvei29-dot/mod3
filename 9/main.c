#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#define FILENAME "data.txt"
#define SEM_NAME "/file_sem_example"
#define BUFFER_SIZE 256
#define TOTAL_LINES 10
#define MAX_NUMBERS_IN_LINE 5
// формирует одну строку, в которой содержится случайное количество случайных чисел
void generate_random_line(char *buffer, size_t size) {
    int count = rand() % MAX_NUMBERS_IN_LINE + 1;
    int written = 0;

    buffer[0] = '\0';

    for (int i = 0; i < count; i++) {
        int value = rand() % 101 - 50;
        int n;

        if (i == 0) {
            n = snprintf(buffer + written, size - written, "%d", value);
        } else {
            n = snprintf(buffer + written, size - written, " %d", value);
        }

        if (n < 0 || (size_t)n >= size - written) {
            break;
        }

        written += n;
    }

    if ((size_t)written + 2 < size) {
        buffer[written++] = '\n';
        buffer[written] = '\0';
    }
}
// обработка строки
void process_line(const char *line, int line_number) {
    char copy[BUFFER_SIZE];
    char *token;
    int min = 0;
    int max = 0;
    int first = 1;

    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    token = strtok(copy, " \n");
    while (token != NULL) {
        int value = atoi(token);

        if (first) {
            min = value;
            max = value;
            first = 0;
        } else {
            if (value < min) {
                min = value;
            }
            if (value > max) {
                max = value;
            }
        }

        token = strtok(NULL, " \n");
    }

    if (!first) {
        printf("[CHILD ] строка %d: %s", line_number, line);
        printf("[CHILD ] min = %d, max = %d\n", min, max);
    }
}

int main(void) {
    sem_t *sem; // семафоры
    pid_t pid;

    setvbuf(stdout, NULL, _IONBF, 0); // отключение буфера вывода
    srand((unsigned int)time(NULL)); //Инициализация генератора случайных чисел
    //очистка файла
    FILE *clear_file = fopen(FILENAME, "w");
    if (clear_file == NULL) {
        perror("fopen");
        return EXIT_FAILURE;
    }
    fclose(clear_file);
    // удаляем старый семафор(после старого запуска)
    if (sem_unlink(SEM_NAME) == -1 && errno != ENOENT) {
        perror("sem_unlink");
        return EXIT_FAILURE;
    }
    //создаем семафор
    sem = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        return EXIT_FAILURE;
    }

    pid = fork();
    // создание дочернего процесса
    if (pid < 0) {
        perror("fork");
        sem_close(sem);
        sem_unlink(SEM_NAME);
        return EXIT_FAILURE;
    }
    // ветка дочернего
    if (pid == 0) {
        long last_pos = 0; //Хранит позицию в файле, до которой ребёнок уже дочитал
        int processed_lines = 0; //Хранит количество строк, которые ребёнок уже обработал
        // пока ребенок не обработал все строки, продолжает
        while (processed_lines < TOTAL_LINES) {
            if (sem_wait(sem) == -1) {
                perror("sem_wait");
                sem_close(sem);
                exit(EXIT_FAILURE);
            }

            FILE *fp = fopen(FILENAME, "r");
            if (fp == NULL) {
                perror("fopen");
                sem_post(sem);
                sem_close(sem);
                exit(EXIT_FAILURE);
            }
            //Переход к месту последнего чтения
            if (fseek(fp, last_pos, SEEK_SET) != 0) {
                perror("fseek");
                fclose(fp);
                sem_post(sem);
                sem_close(sem);
                exit(EXIT_FAILURE);
            }

            char buffer[BUFFER_SIZE];  // в этот массив по очереди читаются строки из файла
            // чтение строк
            while (processed_lines < TOTAL_LINES &&
                   fgets(buffer, sizeof(buffer), fp) != NULL) {
                process_line(buffer, processed_lines + 1);
                processed_lines++;
            }

            last_pos = ftell(fp); // возвращает текущую позицию в файле
            fclose(fp);

            if (sem_post(sem) == -1) {
                perror("sem_post");
                sem_close(sem);
                exit(EXIT_FAILURE);
            }

            usleep(200000); //Делается короткая пауза, чтобы дочерний процесс не крутился слишком быстро и не грузил процессор постоянной проверкой файла.
        }
        //Когда все строки обработаны, ребёнок закрывает семафор и завершает работу.
        if (sem_close(sem) == -1) {
            perror("sem_close");
            exit(EXIT_FAILURE);
        }

        exit(EXIT_SUCCESS);
    } else {
        // ветка родителя
        for (int i = 0; i < TOTAL_LINES; i++) {
            char line[BUFFER_SIZE];

            generate_random_line(line, sizeof(line));
            // Родитель тоже обязан захватывать семафор перед работой с файлом, иначе он может начать писать одновременно с ребёнком.
            if (sem_wait(sem) == -1) {
                perror("sem_wait");
                sem_close(sem);
                sem_unlink(SEM_NAME);
                wait(NULL);
                return EXIT_FAILURE;
            }
            // родитель не должен стирать старые строки.Он должен именно добавлять новые строки в конец файла.
            FILE *fp = fopen(FILENAME, "a");
            if (fp == NULL) {
                perror("fopen");
                sem_post(sem);
                sem_close(sem);
                sem_unlink(SEM_NAME);
                wait(NULL);
                return EXIT_FAILURE;
            }

            fprintf(fp, "%s", line);
            fclose(fp);

            printf("[PARENT] записал строку %d: %s", i + 1, line);
            //После завершения записи родитель освобождает файл.
            if (sem_post(sem) == -1) {
                perror("sem_post");
                sem_close(sem);
                sem_unlink(SEM_NAME);
                wait(NULL);
                return EXIT_FAILURE;
            }

            sleep(1);
        }

        wait(NULL);

        if (sem_close(sem) == -1) {
            perror("sem_close");
            sem_unlink(SEM_NAME);
            return EXIT_FAILURE;
        }

        if (sem_unlink(SEM_NAME) == -1) {
            perror("sem_unlink");
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
