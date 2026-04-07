CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11
TARGET = main
OBJ = main.o

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

clean:
	rm -f $(OBJ) $(TARGET)