CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11

TARGET = sniffer
SRC = sniffer.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

run: $(TARGET)
	sudo ./$(TARGET) 5001

clean:
	rm -f $(TARGET)