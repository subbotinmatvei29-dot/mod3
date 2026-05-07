#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
//./main abc 12 3.5 test
void process_arg(const char *s) {
    char *endptr;
    // Пытаемся конвертировать строку в double
    double x = strtod(s, &endptr);

    // Проверка: конвертация прошла успешно (endptr сдвинулся) 
    // и достигнут конец строки ('\0'), значит мусора нет.
    if (endptr != s && *endptr == '\0') {
        printf("%g %g\n", x, x * 2);
    } else {
        printf("%s\n", s);
    }
}

/* Обрабатывает диапазон аргументов argv от индекса l до r включительно */
void process_range(char *argv[], int l, int r) {
    for (int i = l; i <= r; i++) {
        process_arg(argv[i]);
    }
}

int main(int argc, char *argv[]) {
    // Если нет аргументов , выходим
    if (argc < 2) {
        printf("no arguments\n");
        return 0;
    }

    int n = argc - 1;      // Количество полезных аргументов
    int mid = n / 2;       // Точка разделения на две половины , родитель
    
    pid_t pid = fork();    

    if (pid == -1) {
        perror("fork failed");
        return 1;
    } 
    else if (pid == 0) {
        // Обрабатывает ВТОРУЮ половину аргументов
        process_range(argv, mid + 1, argc - 1);
    } 
    else {

        // Обрабатывает ПЕРВУЮ половину аргументов
        process_range(argv, 1, mid);
        
        // Ждем завершения ребенка, чтобы избежать зомби-процесса
        wait(NULL);
    }

    return 0;
}