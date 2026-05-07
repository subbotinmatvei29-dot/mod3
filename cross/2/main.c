#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <stdint.h>
#include <time.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/timerfd.h>
#include <sys/wait.h>

#define MAX_DRIVERS 128
#define INPUT_SIZE 256

// Команды от CLI к driver
enum {
    CMD_SEND_TASK = 1
    CMD_GET_STATUS = 2
};

// Ответы от driver к CLI
enum {
    RESP_TASK_ACCEPTED = 1,
    RESP_STATUS = 2,
    RESP_BUSY_ERROR = 3
};

// Состояние driver
enum {
    STATUS_AVAILABLE = 0,
    STATUS_BUSY = 1
};

// Сообщение-команда от CLI к driver
typedef struct {
    long mtype;        // PID driver
    int cmd;
    pid_t cli_pid;
    int task_timer;
} command_msg_t;

// Сообщение-ответ от driver к CLI
typedef struct {
    long mtype;        // PID CLI
    pid_t driver_pid;
    int resp_type;
    int status;
    int remaining;
} response_msg_t;

// Информация о driver в родительском процессе
typedef struct {
    pid_t pid;
    int alive;
} driver_info_t;

// Размер сообщений без поля mtype
#define COMMAND_MSG_SIZE  (sizeof(command_msg_t) - sizeof(long))
#define RESPONSE_MSG_SIZE (sizeof(response_msg_t) - sizeof(long))

static driver_info_t drivers[MAX_DRIVERS];
static int driver_count = 0;

static int cmd_qid = -1;
static int resp_qid = -1;

static pid_t parent_pid = 0;

static volatile sig_atomic_t stop_flag = 0;
static volatile sig_atomic_t wake_flag = 0;

// Обработка завершения
static void handle_stop_signal(int signo) {
    (void)signo;
    stop_flag = 1;
}

// Обработка SIGUSR1 — просто будим driver
static void handle_wakeup_signal(int signo) {
    (void)signo;
    wake_flag = 1;
}

// Установка обработчиков SIGINT / SIGTERM
static void install_stop_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = handle_stop_signal;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction(SIGINT)");
        exit(EXIT_FAILURE);
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction(SIGTERM)");
        exit(EXIT_FAILURE);
    }
}

// Установка обработчика SIGUSR1 для driver
static void install_wakeup_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = handle_wakeup_signal;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction(SIGUSR1)");
        _exit(EXIT_FAILURE);
    }
}

// Блокируем SIGUSR1 до fork(), чтобы driver не умер от сигнала до установки обработчика
static void block_sigusr1(void) {
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);

    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) {
        perror("sigprocmask(SIG_BLOCK)");
        exit(EXIT_FAILURE);
    }
}

// Разблокируем SIGUSR1 уже после установки обработчика в driver
static void unblock_sigusr1(void) {
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);

    if (sigprocmask(SIG_UNBLOCK, &set, NULL) == -1) {
        perror("sigprocmask(SIG_UNBLOCK)");
        _exit(EXIT_FAILURE);
    }
}

// Текущее монотонное время
static time_t now_sec(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
        perror("clock_gettime");
        return 0;
    }

    return ts.tv_sec;
}

// Сколько секунд осталось до конца задачи
static int remaining_sec(time_t busy_until) {
    time_t now = now_sec();

    if (busy_until <= now) {
        return 0;
    }

    return (int)(busy_until - now);
}

// Очистка timerfd от накопившихся срабатываний
static void drain_timerfd(int timer_fd) {
    uint64_t expirations;

    while (1) {
        ssize_t n = read(timer_fd, &expirations, sizeof(expirations));

        if (n == (ssize_t)sizeof(expirations)) {
            continue;
        }

        if (n == -1 && errno == EINTR) {
            continue;
        }

        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }

        break;
    }
}

// Проверяем, не закончилась ли задача по времени
static void normalize_busy(int *busy, time_t *busy_until, int timer_fd) {
    if (*busy && remaining_sec(*busy_until) == 0) {
        *busy = 0;
        *busy_until = 0;
        drain_timerfd(timer_fd);
    }
}

// Поиск driver по PID
static int find_driver_index(pid_t pid) {
    for (int i = 0; i < driver_count; i++) {
        if (drivers[i].pid == pid) {
            return i;
        }
    }

    return -1;
}

// Пометить driver как завершённый
static void mark_driver_dead(pid_t pid) {
    int idx = find_driver_index(pid);

    if (idx >= 0) {
        drivers[idx].alive = 0;
    }
}

// Убрать zombie-процессы
static void reap_children(void) {
    while (1) {
        pid_t pid = waitpid(-1, NULL, WNOHANG);

        if (pid <= 0) {
            break;
        }

        mark_driver_dead(pid);
    }
}

// Отправка ответа от driver к CLI
static void send_response(pid_t cli_pid,
                          pid_t driver_pid,
                          int resp_type,
                          int status,
                          int remaining) {
    response_msg_t resp;

    resp.mtype = (long)cli_pid;
    resp.driver_pid = driver_pid;
    resp.resp_type = resp_type;
    resp.status = status;
    resp.remaining = remaining;

    if (msgsnd(resp_qid, &resp, RESPONSE_MSG_SIZE, 0) == -1) {
        perror("msgsnd(response)");
    }
}

// Обработка всех команд, которые пришли конкретному driver
static void process_driver_commands(pid_t my_pid,
                                    int timer_fd,
                                    int *busy,
                                    time_t *busy_until) {
    while (1) {
        command_msg_t cmd;

        ssize_t n = msgrcv(
            cmd_qid,
            &cmd,
            COMMAND_MSG_SIZE,
            (long)my_pid,
            IPC_NOWAIT
        );

        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == ENOMSG) {
                break;
            }

            perror("msgrcv(driver)");
            break;
        }

        normalize_busy(busy, busy_until, timer_fd);

        if (cmd.cmd == CMD_SEND_TASK) {
            int rem = *busy ? remaining_sec(*busy_until) : 0;

            if (*busy) {
                send_response(
                    cmd.cli_pid,
                    my_pid,
                    RESP_BUSY_ERROR,
                    STATUS_BUSY,
                    rem
                );
                continue;
            }

            struct itimerspec spec;
            memset(&spec, 0, sizeof(spec));

            spec.it_value.tv_sec = cmd.task_timer;
            spec.it_value.tv_nsec = 0;

            if (timerfd_settime(timer_fd, 0, &spec, NULL) == -1) {
                perror("timerfd_settime");
                continue;
            }

            *busy = 1;
            *busy_until = now_sec() + cmd.task_timer;

            send_response(
                cmd.cli_pid,
                my_pid,
                RESP_TASK_ACCEPTED,
                STATUS_BUSY,
                cmd.task_timer
            );
        }
        else if (cmd.cmd == CMD_GET_STATUS) {
            normalize_busy(busy, busy_until, timer_fd);

            if (*busy) {
                send_response(
                    cmd.cli_pid,
                    my_pid,
                    RESP_STATUS,
                    STATUS_BUSY,
                    remaining_sec(*busy_until)
                );
            } else {
                send_response(
                    cmd.cli_pid,
                    my_pid,
                    RESP_STATUS,
                    STATUS_AVAILABLE,
                    0
                );
            }
        }
    }
}

// Процесс driver
static void driver_process(void) {
    install_stop_handlers();
    install_wakeup_handler();

    // Теперь SIGUSR1 безопасен
    unblock_sigusr1();

    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);

    if (timer_fd == -1) {
        perror("timerfd_create");
        _exit(EXIT_FAILURE);
    }

    int busy = 0;
    time_t busy_until = 0;
    pid_t my_pid = getpid();

    struct pollfd fds[1];

    fds[0].fd = timer_fd;
    fds[0].events = POLLIN;

    while (!stop_flag) {
        // Если driver был разбужен сигналом, сначала читаем команды
        if (wake_flag) {
            wake_flag = 0;
            process_driver_commands(my_pid, timer_fd, &busy, &busy_until);
            continue;
        }

        int rc = poll(fds, 1, -1);

        if (rc == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("poll(driver)");
            break;
        }

        // Сработал timerfd — задача завершена
        if (fds[0].revents & POLLIN) {
            drain_timerfd(timer_fd);

            busy = 0;
            busy_until = 0;
        }

        // На всякий случай читаем команды после пробуждения poll()
        process_driver_commands(my_pid, timer_fd, &busy, &busy_until);
    }

    close(timer_fd);
    _exit(EXIT_SUCCESS);
}

// Создание нового driver через fork
static pid_t create_driver_process(void) {
    if (driver_count >= MAX_DRIVERS) {
        fprintf(stderr, "Maximum number of drivers reached\n");
        return -1;
    }

    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        driver_process();
    }

    drivers[driver_count].pid = pid;
    drivers[driver_count].alive = 1;
    driver_count++;

    return pid;
}

// Отправка команды driver через System V очередь
static int send_command_to_driver(pid_t driver_pid, int cmd, int task_timer) {
    int idx = find_driver_index(driver_pid);

    if (idx < 0 || !drivers[idx].alive) {
        fprintf(stderr, "Driver %d not found\n", (int)driver_pid);
        return -1;
    }

    command_msg_t msg;

    msg.mtype = (long)driver_pid;
    msg.cmd = cmd;
    msg.cli_pid = parent_pid;
    msg.task_timer = task_timer;

    if (msgsnd(cmd_qid, &msg, COMMAND_MSG_SIZE, 0) == -1) {
        perror("msgsnd(command)");
        return -1;
    }

    // Будим driver сигналом.
    // Данные передаются НЕ через сигнал, а через System V очередь.
    if (kill(driver_pid, SIGUSR1) == -1) {
        if (errno == ESRCH) {
            mark_driver_dead(driver_pid);
            fprintf(stderr, "Driver %d is dead\n", (int)driver_pid);
            return -1;
        }

        perror("kill(SIGUSR1)");
        return -1;
    }

    return 0;
}

// Ожидание ответа от конкретного driver
static int wait_for_response(pid_t expected_driver_pid, response_msg_t *resp) {
    while (!stop_flag) {
        ssize_t n = msgrcv(
            resp_qid,
            resp,
            RESPONSE_MSG_SIZE,
            (long)parent_pid,
            0
        );

        if (n >= 0) {
            if (resp->driver_pid == expected_driver_pid) {
                return 0;
            }

            continue;
        }

        if (errno == EINTR) {
            continue;
        }

        perror("msgrcv(parent)");
        return -1;
    }

    return -1;
}

// Печать статуса одного driver
static void print_one_status(pid_t driver_pid) {
    response_msg_t resp;

    if (send_command_to_driver(driver_pid, CMD_GET_STATUS, 0) == -1) {
        return;
    }

    if (wait_for_response(driver_pid, &resp) == -1) {
        return;
    }

    if (resp.status == STATUS_BUSY) {
        printf("%d Busy %d\n", (int)driver_pid, resp.remaining);
    } else {
        printf("%d Available\n", (int)driver_pid);
    }
}

// Печать всех drivers
static void print_all_drivers(void) {
    int found = 0;

    reap_children();

    for (int i = 0; i < driver_count; i++) {
        if (!drivers[i].alive) {
            continue;
        }

        found = 1;
        print_one_status(drivers[i].pid);
    }

    if (!found) {
        printf("No drivers\n");
    }
}

// Очистка ресурсов
static void cleanup(void) {
    for (int i = 0; i < driver_count; i++) {
        if (drivers[i].alive) {
            kill(drivers[i].pid, SIGTERM);
        }
    }

    for (int i = 0; i < driver_count; i++) {
        if (drivers[i].alive) {
            while (waitpid(drivers[i].pid, NULL, 0) == -1) {
                if (errno == EINTR) {
                    continue;
                }

                break;
            }

            drivers[i].alive = 0;
        }
    }

    if (cmd_qid != -1) {
        msgctl(cmd_qid, IPC_RMID, NULL);
        cmd_qid = -1;
    }

    if (resp_qid != -1) {
        msgctl(resp_qid, IPC_RMID, NULL);
        resp_qid = -1;
    }
}

// Справка
static void print_help(void) {
    printf("Commands:\n");
    printf("  create_driver\n");
    printf("  send_task <pid> <task_timer>\n");
    printf("  get_status <pid>\n");
    printf("  get_drivers\n");
    printf("  help\n");
    printf("  exit\n");
}

int main(void) {
    parent_pid = getpid();

    /*
     * SIGUSR1 блокируем до fork().
     * Это нужно, чтобы новый driver не получил SIGUSR1 до установки обработчика.
     */
    block_sigusr1();

    install_stop_handlers();
    signal(SIGPIPE, SIG_IGN);

    cmd_qid = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    resp_qid = msgget(IPC_PRIVATE, IPC_CREAT | 0600);

    if (cmd_qid == -1 || resp_qid == -1) {
        perror("msgget");

        if (cmd_qid != -1) {
            msgctl(cmd_qid, IPC_RMID, NULL);
        }

        if (resp_qid != -1) {
            msgctl(resp_qid, IPC_RMID, NULL);
        }

        exit(EXIT_FAILURE);
    }

    print_help();

    while (!stop_flag) {
        char line[INPUT_SIZE];
        char *cmd;

        reap_children();

        printf("taxi> ");
        fflush(stdout);

        struct pollfd pfd;

        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int rc = poll(&pfd, 1, -1);

        if (rc == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("poll(stdin)");
            break;
        }

        if (!(pfd.revents & POLLIN)) {
            continue;
        }

        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        line[strcspn(line, "\n")] = '\0';

        cmd = line;

        while (isspace((unsigned char)*cmd)) {
            cmd++;
        }

        if (*cmd == '\0') {
            continue;
        }

        if (strcmp(cmd, "create_driver") == 0) {
            pid_t pid = create_driver_process();

            if (pid != -1) {
                printf("Driver created: %d\n", (int)pid);
            }
        }
        else if (strncmp(cmd, "send_task", 9) == 0) {
            int pid_value;
            int task_timer;
            response_msg_t resp;

            if (sscanf(cmd, "send_task %d %d", &pid_value, &task_timer) != 2) {
                printf("Usage: send_task <pid> <task_timer>\n");
                continue;
            }

            if (task_timer <= 0) {
                printf("task_timer must be > 0\n");
                continue;
            }

            if (send_command_to_driver((pid_t)pid_value, CMD_SEND_TASK, task_timer) == -1) {
                continue;
            }

            if (wait_for_response((pid_t)pid_value, &resp) == -1) {
                continue;
            }

            if (resp.resp_type == RESP_TASK_ACCEPTED) {
                printf("Driver %d: task accepted, Busy %d\n",
                       pid_value,
                       resp.remaining);
            } else {
                printf("Driver %d: Busy %d\n",
                       pid_value,
                       resp.remaining);
            }
        }
        else if (strncmp(cmd, "get_status", 10) == 0) {
            int pid_value;

            if (sscanf(cmd, "get_status %d", &pid_value) != 1) {
                printf("Usage: get_status <pid>\n");
                continue;
            }

            print_one_status((pid_t)pid_value);
        }
        else if (strcmp(cmd, "get_drivers") == 0) {
            print_all_drivers();
        }
        else if (strcmp(cmd, "help") == 0) {
            print_help();
        }
        else if (strcmp(cmd, "exit") == 0) {
            break;
        }
        else {
            printf("Unknown command. Type 'help'\n");
        }
    }

    cleanup();
    return 0;
}