#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Нет аргументов\n");
        return 0;
    }

    int max_index = 1;
    size_t max_len = strlen(argv[1]);

    for (int i = 2; i < argc; i++) {
        size_t len = strlen(argv[i]);
        if (len > max_len) {
            max_len = len;
            max_index = i;
        }
    }

    printf("%s\n", argv[max_index]);
    return 0;
}