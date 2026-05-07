#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
// глобальные флаги
volatile sig_atomic_t sigint_count = 0; // счетчик 
volatile sig_atomic_t pending_sigint = 0; // Флаг: есть ли необработанные SIGINT
volatile sig_atomic_t pending_sigquit = 0;
volatile sig_atomic_t need_exit = 0;//  Флаг принудительного выхода

void handler(int sig) {
    if (sig == SIGINT) {
        sigint_count++;
        pending_sigint++; // // Сообщаем главному циклу о событии

        if (sigint_count >= 3) {
            need_exit = 1;
        }
    } else if (sig == SIGQUIT)  {
        pending_sigquit++; // Сообщаем главному циклу о SIGQUIT
    }
}

int main(void) {
    // Открываем файл для записи логов
    FILE *file = fopen("output.txt", "w");
    if (file == NULL) {
        perror("Не удалось открыть файл");
        return 1;
    }
    //// Регистрируем наш обработчик для сигналов
    if (signal(SIGINT, handler) == SIG_ERR) {
        perror("Ошибка установки обработчика SIGINT");
        fclose(file);
        return 1;
    }

    if (signal(SIGQUIT, handler) == SIG_ERR) {
        perror("Ошибка установки обработчика SIGQUIT");
        fclose(file);
        return 1;
    }

    int counter = 1;

    while (1) {
        // Обрабатываем накопленные сигналы SIGINT
        // Используем while, чтобы обработать все пришедшие сигналы подряд
        while (pending_sigint > 0) {
            fprintf(file, "Получен и обработан сигнал SIGINT\n");
            fflush(file);
            pending_sigint--;
        }
        // Обрабатываем накопленные сигналы SIGQUIT
        while (pending_sigquit > 0) {
            fprintf(file, "Получен и обработан сигнал SIGQUIT\n");
            fflush(file);
            pending_sigquit--;
        }
        // Проверка условия выхода (после 3-х SIGINT)
        if (need_exit) {
            break;
        }
        // Основная работа программы: пишем счетчик
        fprintf(file, "%d\n", counter);
        fflush(file);
        counter++;

        sleep(1); //Пауза, чтобы не грузить процессор
    }

    fclose(file);
    return 0;
}