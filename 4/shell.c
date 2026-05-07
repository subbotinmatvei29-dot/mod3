#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

struct command {
    char *argv[32];       // аргументы команды для exec
    char *input_file;     // файл для перенаправления ввода через <
    char *output_file;    // файл для перенаправления вывода через >
};

int main(void) {
    char line[256];           // исходная строка, которую ввёл пользователь
    char prepared[512];       // строка после вставки пробелов вокруг | < >
    char *tokens[64];         // токены после разбиения строки
    struct command cmds[16];  // массив команд конвейера

    while (1) {
        printf(">>> ");
        fflush(stdout);  

        
        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;  // конец ввода, выходим из shell
        }

        // убираем символ перевода строки в конце
        line[strcspn(line, "\n")] = '\0';

        // пустую строку просто игнорируем
        if (line[0] == '\0') {
            continue;
        }

        // встроенная команда выхода из shell
        if (strcmp(line, "exit") == 0) {
            break;
        }

        int j = 0;
        int too_long = 0;

        // подготавливаем строку:
        // вокруг спецсимволов | < > ставим пробелы,
        // чтобы потом strtok корректно разбил всё на токены
        for (int i = 0; line[i] != '\0'; i++) {
            if (j >= (int)sizeof(prepared) - 4) {
                too_long = 1;
                break;
            }

            if (line[i] == '|' || line[i] == '<' || line[i] == '>') {
                prepared[j++] = ' ';
                prepared[j++] = line[i];
                prepared[j++] = ' ';
            } else {
                prepared[j++] = line[i];
            }
        }
        prepared[j] = '\0';

        if (too_long) {
            fprintf(stderr, "Слишком длинная команда\n");
            continue;
        }

        int token_count = 0;

        // разбиваем подготовленную строку на токены по пробелам и табам
        char *token = strtok(prepared, " \t");
        while (token != NULL && token_count < 63) {
            tokens[token_count++] = token;
            token = strtok(NULL, " \t");
        }
        tokens[token_count] = NULL;

        if (token_count == 0) {
            continue;
        }

        // обнуляем все команды перед новым разбором
        memset(cmds, 0, sizeof(cmds));

        int cmd_index = 0;  // номер текущей команды
        int arg_index = 0;  // номер аргумента внутри текущей команды
        int error = 0;

        // разбираем токены:
        // обычные слова идут в argv,
        // < задаёт input_file,
        // > задаёт output_file,
        // | завершает текущую команду и начинает следующую
        for (int i = 0; i < token_count; i++) {
            if (strcmp(tokens[i], "|") == 0) {
                // нельзя завершить пустую команду
                if (arg_index == 0) {
                    fprintf(stderr, "Ошибка: пустая команда\n");
                    error = 1;
                    break;
                }

                cmds[cmd_index].argv[arg_index] = NULL;  // argv должен заканчиваться NULL
                cmd_index++;
                arg_index = 0;

                if (cmd_index >= 16) {
                    fprintf(stderr, "Слишком много команд\n");
                    error = 1;
                    break;
                }
            } else if (strcmp(tokens[i], "<") == 0) {
                // после < обязательно должно быть имя файла(чтение данных из файла)
                if (i + 1 >= token_count) {
                    fprintf(stderr, "Ошибка: нет файла после <\n");
                    error = 1;
                    break;
                }
                cmds[cmd_index].input_file = tokens[++i];
            } else if (strcmp(tokens[i], ">") == 0) { // вывод файла
                // после > обязательно должно быть имя файла
                if (i + 1 >= token_count) {
                    fprintf(stderr, "Ошибка: нет файла после >\n");
                    error = 1;
                    break;
                }
                cmds[cmd_index].output_file = tokens[++i];
            } else {
                // обычный токен добавляем в список аргументов команды
                if (arg_index >= 31) {
                    fprintf(stderr, "Слишком много аргументов\n");
                    error = 1;
                    break;
                }
                cmds[cmd_index].argv[arg_index++] = tokens[i];
            }
        }

        if (error) {
            continue;
        }

        // если после разбора последняя команда пустая — это ошибка
        if (arg_index == 0) {
            fprintf(stderr, "Ошибка: пустая последняя команда\n");
            continue;
        }

        // завершаем argv последней команды
        cmds[cmd_index].argv[arg_index] = NULL;
        int cmd_count = cmd_index + 1;  // сколько всего команд в конвейере

        int prev_fd = -1;   // чтение из предыдущего pipe
        pid_t pids[16];     // pid всех запущенных дочерних процессов
        int started = 0;    // сколько процессов реально успели создать

        // (несколько процессов подряд)
        for (int i = 0; i < cmd_count; i++) {
            int fd[2] = {-1, -1};  // pipe для связи текущей команды со следующей

            // pipe нужен только если команда не последняя(вывод одного процесса на ввод другого)
            if (i < cmd_count - 1) {
                if (pipe(fd) == -1) {
                    perror("pipe");
                    error = 1;
                    break;
                }
            }

            pid_t pid = fork();

            if (pid == -1) {
                perror("fork");
                error = 1;
                if (fd[0] != -1) close(fd[0]);
                if (fd[1] != -1) close(fd[1]);
                break;
            } else if (pid == 0) {
                // ===== дочерний процесс: настраивает ввод/вывод и запускает команду =====

                // если указан файл после <, то stdin берём из него
                if (cmds[i].input_file != NULL) {
                    int in = open(cmds[i].input_file, O_RDONLY);
                    if (in == -1) {
                        perror(cmds[i].input_file);
                        exit(1);
                    }
                    dup2(in, 0);   // перенаправляем stdin на файл
                    close(in);
                } else if (prev_fd != -1) {
                    // Направление ввода следующей команды из предыдущего pipe:
                    dup2(prev_fd, 0);
                }

                // если указан файл после >, то stdout направляем в него(запуск процесаа , программа пишет в файл)
                if (cmds[i].output_file != NULL) {
                    int out = open(cmds[i].output_file,
                                   O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (out == -1) {
                        perror(cmds[i].output_file);
                        exit(1);
                    }
                    dup2(out, 1);  // перенаправляем stdout в файл
                    close(out);
                } else if (i < cmd_count - 1) {
                    // иначе, если команда не последняя, stdout направляем в pipe
                    dup2(fd[1], 1);
                }

                // лишние дескрипторы ребёнку больше не нужны
                if (prev_fd != -1) {
                    close(prev_fd);
                }
                if (fd[0] != -1) close(fd[0]);
                if (fd[1] != -1) close(fd[1]);

                // сначала пытаемся запустить локальную программу из текущей папки
                // например ./sum, ./concat, ./maxlen
                if (strchr(cmds[i].argv[0], '/') == NULL) {
                    char local_path[300];
                    snprintf(local_path, sizeof(local_path), "./%s", cmds[i].argv[0]);

                    if (access(local_path, X_OK) == 0) {
                        execv(local_path, cmds[i].argv);
                    }
                }

                // если локальной программы нет, пробуем обычную команду через PATH
                execvp(cmds[i].argv[0], cmds[i].argv);

                // если дошли сюда, значит запуск не удался
                fprintf(stderr, "Команда не найдена или не запускается: %s\n",
                        cmds[i].argv[0]);
                exit(1);

            } else {
                //  родительский процесс

                pids[started++] = pid;  // запоминаем pid для waitpid

                // старый входной конец pipe больше не нужен
                if (prev_fd != -1) {
                    close(prev_fd);
                }

                if (i < cmd_count - 1) {
                    // родителю не нужно писать в pipe,
                    // он только сохраняет чтение для следующей команды
                    close(fd[1]);
                    prev_fd = fd[0];
                } else {
                    prev_fd = -1;
                }
            }
        }

        // на всякий случай закрываем оставшийся дескриптор
        if (prev_fd != -1) {
            close(prev_fd);
        }

        // ждём завершения всех запущенных процессов
        for (int i = 0; i < started; i++) {
            waitpid(pids[i], NULL, 0);
        }
    }

    return 0;
}