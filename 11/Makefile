CC=gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11
TARGET = shm
SRC = main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) 

clean:
	rm -f $(TARGET) 

re: clean all