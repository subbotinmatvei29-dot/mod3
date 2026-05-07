#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "chat.h"
// структура клиента
typedef struct {
    long id;
    int active;
} Client;

int msqid = -1; //идент очереди сообщения
Client clients[MAX_CLIENTS]; // массив клиентов
int client_count = 0; // текущее колл-во клиентов

void cleanup(int sig) {
    (void)sig;

    if (msqid != -1) {
        msgctl(msqid, IPC_RMID, NULL); 
        printf("\nСервер: очередь удалена.\n");
    }

    exit(EXIT_SUCCESS);
}
// ищет клиента по его идентификатору 
int find_client(long id) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i].id == id) {
            return i;
        }
    }
    return -1;
}
// добавляет клиента в список, если его еще нет
void add_client(long id) {
    int index = find_client(id);

    if (index != -1) {
        if (clients[index].active == 0) {
            clients[index].active = 1;
            printf("Сервер: клиент %ld снова активен.\n", id);
        }
        return;
    }

    if (client_count < MAX_CLIENTS) {
        clients[client_count].id = id;
        clients[client_count].active = 1;
        client_count++;
        printf("Сервер: клиент %ld подключен.\n", id);
    } else {
        printf("Сервер: список клиентов переполнен.\n");
    }
}
// Находит клиента и делает его неактивным
void deactivate_client(long id) {
    int index = find_client(id);

    if (index != -1) {
        clients[index].active = 0;
        printf("Сервер: клиент %ld отключен.\n", id);
    }
}

int main(void) {
    key_t key; // создаст или найдет очередб
    Message msg; // то что пришло
    Message out; // то что отправляется
    // создаст ключ
    key = ftok(".", 'A');
    if (key == -1) {
        perror("ftok");
        exit(EXIT_FAILURE);
    }

    msqid = msgget(key, IPC_CREAT | 0666); //Создает очередь сообщений или получает доступ к уже существующей.
    if (msqid == -1) {
        perror("msgget");
        exit(EXIT_FAILURE);
    }
    // установка обработчика сигнала
    signal(SIGINT, cleanup);

    printf("Сервер запущен. msqid = %d\n", msqid);

    while (1) {
        // получает сообщения из очереди
        if (msgrcv(msqid, &msg, MSG_SIZE, SERVER_TYPE, 0) == -1) {
            perror("msgrcv");
            continue;
        }

        add_client(msg.sender);

        if (strcmp(msg.text, "shutdown") == 0) {
            deactivate_client(msg.sender);
            continue;
        }

        printf("Сервер получил от %ld: %s\n", msg.sender, msg.text);
        // рассылка сообщения другим клиентам
        for (int i = 0; i < client_count; i++) {
            if (clients[i].active == 1 && clients[i].id != msg.sender) {
                out.mtype = clients[i].id;
                out.sender = msg.sender;
                strncpy(out.text, msg.text, MAX_TEXT - 1);
                out.text[MAX_TEXT - 1] = '\0';

                if (msgsnd(msqid, &out, MSG_SIZE, 0) == -1) {
                    perror("msgsnd");
                }
            }
        }
    }

    return 0;
}