CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11

TARGETS = server client

all: $(TARGETS)

server: server.c chat.h
	$(CC) $(CFLAGS) server.c -o server

client: client.c chat.h
	$(CC) $(CFLAGS) client.c -o client

clean:
	rm -f $(TARGETS)

re: clean all