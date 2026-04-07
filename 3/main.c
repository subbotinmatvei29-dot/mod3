#include <stdio.h>
#include <stdlib.h>
#include "contacts.h"

int main(void) {
    int count = 0;
    int capacity = 2;
    struct contact *book = malloc(capacity * sizeof(struct contact));
    book = load_contacts(book, &count, &capacity);
    
    if (book == NULL) {
        printf("Ошибка памяти!\n");
        return 1;
    }

    int choice;

    while (1) {
        printf("\n1. Добавить  2. Редактировать  3. Удалить  4. Показать все  0. Выход\nВыбор: ");
        
        // Читаем выбор. Если ввели не число, завершаем или обрабатываем
        if (scanf("%d", &choice) != 1) break; 
        
        // ВАЖНО: очищаем буфер от '\n' после ввода числа
        while(getchar() != '\n'); 

        if (choice == 0) break;
        else if (choice == 1) book = add_contact(book, &count, &capacity);
        else if (choice == 2) edit_contact(book, count);
        else if (choice == 3) delete_contact(book, &count);
        else if (choice == 4) print_contacts(book, count);
        else printf("Неверный ввод!\n");
    }
    save_contacts(book, count);
    free(book);
    printf("Завершение...\n");
    return 0;
}