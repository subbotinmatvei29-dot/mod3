#ifndef CHAT_H
#define CHAT_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define SERVER_TYPE 10
#define MAX_TEXT 256
#define MAX_CLIENTS 100

typedef struct {
    long mtype;          // кому адресовано сообщение
    long sender;         // кто отправил сообщение
    char text[MAX_TEXT]; // текст сообщения
} Message;

#define MSG_SIZE (sizeof(Message) - sizeof(long))

#endif
