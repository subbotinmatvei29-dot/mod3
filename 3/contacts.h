#ifndef CONTACT_H
#define CONTACT_H

// Описание структуры
struct contact {
    char surname[100];
    char name[100];
    char patronymic[100];
    char workplace[100];
    char job[100];
    char phone[100];
    char mail[100];
    char social[100];
};

// Прототипы функций
void read_line(char *buffer, int size);
struct contact* add_contact(struct contact *book, int *count, int *capacity);
void edit_contact(struct contact *book, int count);
void delete_contact(struct contact *book, int *count);
void print_contacts(const struct contact *book, int count);
struct contact *load_contacts(struct contact *book, int *count, int *capacity);
void save_contacts(struct contact *book, int count);

#endif // CONTACT_H