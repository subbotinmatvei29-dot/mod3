#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Использование: %s <имя_файла> [количество_обработок]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *filename = argv[1];
    long limit = -1;

    if (argc == 3) {
        char *endptr;
        limit = strtol(argv[2], &endptr, 10); //Преобразуем аргумент в число.
        if (*endptr != '\0' || limit <= 0) {
            fprintf(stderr, "Ошибка: количество обработок должно быть положительным числом\n");
            return EXIT_FAILURE;
        }
    }

    srand((unsigned int)(time(NULL) ^ getpid()));

    int semid = get_file_semaphore(filename, NULL);
    if (semid == -1) {
        return EXIT_FAILURE;
    }

    long processed = 0;

    while (limit == -1 || processed < limit) {
        int found = 0;
        char line[MAX_LINE_LEN];
        char numbers_part[MAX_LINE_LEN];

        lock_sem(semid);

        FILE *fp = fopen(filename, "r+");
        if (fp == NULL) {
            perror("fopen");
            unlock_sem(semid);
            return EXIT_FAILURE;
        }

        while (1) {
            long pos = ftell(fp);

            if (fgets(line, sizeof(line), fp) == NULL) {
                break;
            }

            if (line[0] == '0' && isspace((unsigned char)line[1])) {
                if (fseek(fp, pos, SEEK_SET) != 0) {
                    perror("fseek");
                    fclose(fp);
                    unlock_sem(semid);
                    return EXIT_FAILURE;
                }

                if (fputc('1', fp) == EOF) {
                    perror("fputc");
                    fclose(fp);
                    unlock_sem(semid);
                    return EXIT_FAILURE;
                }

                fflush(fp);

                strncpy(numbers_part, line + 2, sizeof(numbers_part) - 1);
                numbers_part[sizeof(numbers_part) - 1] = '\0';

                found = 1;
                break;
            }
        }

        fclose(fp);
        unlock_sem(semid);

        if (found) {
            int min_value, max_value;

            if (parse_min_max(numbers_part, &min_value, &max_value) == 0) {
                printf("[consumer %d] строка: %s", getpid(), numbers_part);
                printf("[consumer %d] min = %d, max = %d\n", getpid(), min_value, max_value);
                processed++;
            } else {
                fprintf(stderr, "[consumer %d] ошибка разбора строки\n", getpid());
            }

            sleep((unsigned int)(rand() % 2 + 1));
        } else {
            sleep(1);
        }
    }

    return EXIT_SUCCESS;
}