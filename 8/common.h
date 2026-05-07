#ifndef COMMON_H
#define COMMON_H

#include<stddef.h>
#define MAX_LINE_LEN 1024

union semum{
    int val;
    struct semid_ds *buf;
    unsigned short *array;
#ifdef __linux__
    struct seminfo *__buf;
#endif

};

int get_file_semaphore(const char *filename, int *is_creator);
void lock_sem(int semid);
void unlock_sem(int semid);
void generate_record_line(char *buf, size_t size);
int parse_min_max(const char *str, int *min_value, int *max_value);
#endif
