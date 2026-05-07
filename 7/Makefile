CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11
LDFLAGS = -lrt

TARGETS = prog1 prog2

all: $(TARGETS)

prog1: prog1.c
	$(CC) $(CFLAGS) prog1.c -o prog1 $(LDFLAGS)

prog2: prog2.c
	$(CC) $(CFLAGS) prog2.c -o prog2 $(LDFLAGS)

clean:
	rm -f $(TARGETS)

rebuild: clean all