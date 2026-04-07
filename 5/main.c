#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

volatile sig_atomic_t sigint_count = 0;
volatile sig_atomic_t pending_sigint = 0;
volatile sig_atomic_t pending_sigquit = 0;
volatile sig_atomic_t need_exit = 0;

void handler(int sig) {
    if (sig == SIGINT) {
        sigint_count++;
        pending_sigint++;

        if (sigint_count >= 3) {
            need_exit = 1;
        }
    } else if (sig == SIGQUIT) {
        pending_sigquit++;
    }
}

int main(void) {
    FILE *file = fopen("output.txt", "w");
    if (file == NULL) {
        perror("Не удалось открыть файл");
        return 1;
    }

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
        while (pending_sigint > 0) {
            fprintf(file, "Получен и обработан сигнал SIGINT\n");
            fflush(file);
            pending_sigint--;
        }

        while (pending_sigquit > 0) {
            fprintf(file, "Получен и обработан сигнал SIGQUIT\n");
            fflush(file);
            pending_sigquit--;
        }

        if (need_exit) {
            break;
        }

        fprintf(file, "%d\n", counter);
        fflush(file);
        counter++;

        sleep(1);
    }

    fclose(file);
    return 0;
}