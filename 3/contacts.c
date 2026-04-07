#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contacts.h"
#include <fcntl.h>
#include <unistd.h>

void clear_input_buffer(void) {
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {
    }
}


struct contact* load_contacts(struct contact *book, int *count, int *capacity){
    int fd=open("contacts.bin", O_RDONLY);
    if (fd==-1){
        return book;
    }
    struct contact temp;
    ssize_t bytes_read;

    while ((bytes_read = read(fd, &temp, sizeof(struct contact))) >0){
        if(*count>=*capacity){
            *capacity *=2;
            struct contact *new_book=realloc(book, (*capacity)* sizeof(struct contact));
            if (new_book == NULL){
                printf("ошибка выделения памяти\n");
                close(fd);
                return book;
            }
            book = new_book;
        }
        book[*count]=temp;
        (*count)++;
    }
    if (bytes_read==-1){
        printf("ошибка чтения файла\n");
    }
    close(fd);
    return book;
}

void save_contacts(struct contact *book, int count){
    int fd=open("contacts.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd==-1){
        perror("ошибка открытия файла");
        return;
    }
    for(int i=0;i<count;i++){
        ssize_t bytes_written = write(fd, &book[i],sizeof(struct contact));
        if (bytes_written!= sizeof(struct contact)){
            perror("Ошибка записи файла");
            close(fd);
            return;
        }
        
    }
    close(fd);
}


// Безопасное чтение строки с пробелами
void read_line(char *buffer, int size) {
    if (fgets(buffer, size, stdin) == NULL) {
        buffer[0] = '\0';
        return;
    }

    size_t len = strlen(buffer);

    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    } else {
        clear_input_buffer();
    }
}

// Функция добавления контакта
struct contact* add_contact(struct contact *book, int *count, int *capacity) {
    if (*count >= *capacity) {
        *capacity *= 2;

        struct contact *temp = realloc(book, (*capacity) * sizeof(struct contact));
        if (temp == NULL) {
            printf("Ошибка выделения памяти!\n");
            *capacity /= 2;
            return book;
        }

        book = temp;
    }

    printf("\n--- Новая запись ---\n");

    do {
        printf("Фамилия (обязательно): ");
        read_line(book[*count].surname, 100);
        if (strlen(book[*count].surname) == 0) {
            printf("Фамилия не может быть пустой!\n");
        }
    } while (strlen(book[*count].surname) == 0);

    do {
        printf("Имя (обязательно): ");
        read_line(book[*count].name, 100);
        if (strlen(book[*count].name) == 0) {
            printf("Имя не может быть пустым!\n");
        }
    } while (strlen(book[*count].name) == 0);

    printf("(Для пропуска жмите Enter)\n");

    printf("Отчество: ");
    read_line(book[*count].patronymic, 100);

    printf("Место работы: ");
    read_line(book[*count].workplace, 100);

    printf("Должность: ");
    read_line(book[*count].job, 100);

    printf("Телефон: ");
    read_line(book[*count].phone, 100);

    printf("Email: ");
    read_line(book[*count].mail, 100);

    printf("Соцсети: ");
    read_line(book[*count].social, 100);

    (*count)++;
    printf("Контакт добавлен!\n");
    return book;
}

// Функция редактирования
void edit_contact(struct contact *book, int count) {
    if (count == 0) {
        printf("Книга пуста!\n");
        return;
    }

    int edit_num;
    printf("Введите номер контакта для редактирования (1-%d): ", count);

    if (scanf("%d", &edit_num) != 1) {
        printf("Ошибка ввода!\n");
        clear_input_buffer();
        return;
    }
    clear_input_buffer();

    if (edit_num < 1 || edit_num > count) {
        printf("Такого контакта нет!\n");
        return;
    }

    int index = edit_num - 1;
    char temp[100];

    printf("\nОставь строку пустой и нажми Enter, если не хочешь менять поле.\n");

    printf("Новая фамилия: ");
    read_line(temp, 100);
    if (strlen(temp) > 0) {
        strcpy(book[index].surname, temp);
    }

    printf("Новое имя: ");
    read_line(temp, 100);
    if (strlen(temp) > 0) {
        strcpy(book[index].name, temp);
    }

    printf("Отчество: ");
    read_line(temp, 100);
    if (strlen(temp) > 0) {
        strcpy(book[index].patronymic, temp);
    }

    printf("Место работы: ");
    read_line(temp, 100);
    if (strlen(temp) > 0) {
        strcpy(book[index].workplace, temp);
    }

    printf("Должность: ");
    read_line(temp, 100);
    if (strlen(temp) > 0) {
        strcpy(book[index].job, temp);
    }

    printf("Телефон: ");
    read_line(temp, 100);
    if (strlen(temp) > 0) {
        strcpy(book[index].phone, temp);
    }

    printf("Почта: ");
    read_line(temp, 100);
    if (strlen(temp) > 0) {
        strcpy(book[index].mail, temp);
    }

    printf("Соцсети: ");
    read_line(temp, 100);
    if (strlen(temp) > 0) {
        strcpy(book[index].social, temp);
    }

    printf("Контакт обновлен!\n");
}

// Функция удаления
void delete_contact(struct contact *book, int *count) {
    if (*count == 0) {
        printf("Книга пуста!\n");
        return;
    }

    int del_num;
    printf("Введите номер для удаления (1-%d): ", *count);

    if (scanf("%d", &del_num) != 1) {
        printf("Ошибка ввода!\n");
        clear_input_buffer();
        return;
    }
    clear_input_buffer();

    if (del_num < 1 || del_num > *count) {
        printf("Такого контакта нет!\n");
        return;
    }

    int index = del_num - 1;

    for (int i = index; i < *count - 1; i++) {
        book[i] = book[i + 1];
    }

    (*count)--;
    printf("Контакт удален!\n");
}

// Вывод контактов
void print_contacts(const struct contact *book, int count) {
    if (count == 0) {
        printf("\nКнига пуста.\n");
        return;
    }

    printf("\n=== СПИСОК КОНТАКТОВ ===\n");

    for (int i = 0; i < count; i++) {
        printf("ID: %d | %s %s", i + 1, book[i].surname, book[i].name);

        if (strlen(book[i].patronymic) > 0) {
            printf(" %s", book[i].patronymic);
        }
        printf("\n");

        if (strlen(book[i].workplace) > 0 || strlen(book[i].job) > 0) {
            printf("  Работа: %s", book[i].workplace);

            if (strlen(book[i].job) > 0) {
                printf(", %s", book[i].job);
            }
            printf("\n");
        }

        if (strlen(book[i].phone) > 0) {
            printf("  Тел: %s\n", book[i].phone);
        }

        if (strlen(book[i].mail) > 0) {
            printf("  Почта: %s\n", book[i].mail);
        }

        if (strlen(book[i].social) > 0) {
            printf("  Соцсети: %s\n", book[i].social);
        }

        printf("------------------------\n");
    }
}