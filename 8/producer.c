#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char *argv[]){
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Использование: %s <имя_файла> [количество_строк]\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    const char *filename = argv[1]; //сохраняет имя файла
    long limit = -1;
    // Если задан третий аргумент, читаем его как лимит количества строк.
    if (argc == 3) {
        char *endptr;
        limit = strtol(argv[2], &endptr, 10); // Преобразуем строку в число.
        if (*endptr != '\0' || limit <= 0) { //Проверяем, что аргумент действительно является положительным числом.
            fprintf(stderr, "Ошибка: количество строк должно быть положительным числом\n");
            return EXIT_FAILURE;
        }
    }
    //Берём текущее время и PID, чтобы разные процессы не получили одинаковую последовательность случайных чисел.
    srand((unsigned int)(time(NULL) ^ getpid()));
    // получаем семафор для файла
    int semid = get_file_semaphore(filename, NULL);
    if (semid == -1) {
        return EXIT_FAILURE;
    }

    long produced = 0;

    while (limit == -1 || produced < limit) {
        char line[MAX_LINE_LEN];
        generate_record_line(line, sizeof(line));

        lock_sem(semid);

        FILE *fp = fopen(filename, "a");
        if (fp == NULL) {
            perror("fopen");
            unlock_sem(semid);
            return EXIT_FAILURE;
        }

        fputs(line, fp);
        fflush(fp);
        fclose(fp);

        unlock_sem(semid);

        printf("[producer %d] записал: %s", getpid(), line);

        produced++;
        sleep((unsigned int)(rand() % 3 + 1));
    }

    return EXIT_SUCCESS;
}
