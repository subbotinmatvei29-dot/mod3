#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/types.h>
// создает новый семафор для файла либо получает уже сущест
int get_file_semaphore(const char *filename, int *is_creator){
    int fd=open(filename, O_CREAT | O_RDWR, 0666);
    if (fd==-1){
        perror("open");
        return -1;
    }
    close(fd);
    // ключ формируется
    key_t key=ftok(filename, 'S');
    if (key==-1){
        perror("ftok");
        return -1;
    }

    int semid=semget(key, 1, 0666 | IPC_CREAT | IPC_EXCL);
    if (semid!=-1){
        union semum arg;
        arg.val=1;

        if (semctl(semid,0,SETVAL, arg)==-1){
            perror("semctl SETVAl");
            return -1;
        }
        if (is_creator!=NULL){
            *is_creator=1;
        }
        return semid;
    }
    if (errno!=EEXIST){
        perror("semget");
        return -1;
    }
    semid =semget(key,1,0666);
    if (semid==-1){
        perror("semget existing");
        return -1;
    }
    if (is_creator!=NULL){
        *is_creator=0;
    }
    return semid;
}
// усеньшает значения семафора на 1
void lock_sem(int semid){
    struct sembuf op[2]={{0,0, 0}, {0,1,0}};
    if (semop(semid, op,2)==-1){
        perror("semop lock");
        exit(EXIT_FAILURE);
    }
}


// увеличивает на 1
void unlock_sem(int semid){
    struct sembuf op={0,-1,0};
    if (semop(semid,&op,1)==-1){
        perror("semop unlock");
        exit(EXIT_FAILURE); 
    }
}
// создает строку ( для производителя)
void generate_record_line(char *buf, size_t size){
    int count=rand()%8 +3;
    int len = snprintf(buf,size, "0");
    for(int i=0; i<count && len<(int)size;i++){
        int value = rand() % 201 - 100;   // от -100 до 100
        int written = snprintf(buf + len, size - (size_t)len, " %d", value);

        if (written < 0 || written >= (int)(size - (size_t)len)) {
            break;
        }
        len+=written;
    }
    if (len < (int)size-1){
        buf[len++]='\n';
        buf[len]='\0';
    } else {
        buf[size-2]='\n';
        buf[size-1]='\0';
    }
}
// получает строку (использ потребитель)
int parse_min_max(const char *str, int *min_value, int *max_value) {
    char *endptr;
    long value;

    while (isspace((unsigned char)*str)) {
        str++;
    }

    if (*str == '\0') {
        return -1;
    }

    value = strtol(str, &endptr, 10);
    if (str == endptr) {
        return -1;
    }

    *min_value = (int)value;
    *max_value = (int)value;
    str = endptr;

    while (*str != '\0') {
        while (isspace((unsigned char)*str)) {
            str++;
        }

        if (*str == '\0') {
            break;
        }

        value = strtol(str, &endptr, 10);
        if (str == endptr) {
            return -1;
        }

        if ((int)value < *min_value) {
            *min_value = (int)value;
        }
        if ((int)value > *max_value) {
            *max_value = (int)value;
        }

        str = endptr;
    }

    return 0;
}