CC = gcc
CFLAGS = -Wall -Wextra -std=c11

TARGET = phonebook

all: $(TARGET)

$(TARGET): main.o contacts.o
	$(CC) $(CFLAGS) -o $(TARGET) main.o contacts.o

main.o: main.c contacts.h
	$(CC) $(CFLAGS) -c main.c

contacts.o: contacts.c contacts.h
	$(CC) $(CFLAGS) -c contacts.c

clean:
	rm -f *.o $(TARGET)