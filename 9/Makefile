CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11
LDFLAGS = -pthread
TARGET = sem_posix
SRC = main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET) data.txt

re: clean all