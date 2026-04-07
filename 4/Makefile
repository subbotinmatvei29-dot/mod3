CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11

all: shell concat sum maxlen

shell: shell.c
	$(CC) $(CFLAGS) -o shell shell.c

concat: concat.c
	$(CC) $(CFLAGS) -o concat concat.c

sum: sum.c
	$(CC) $(CFLAGS) -o sum sum.c

maxlen: maxlen.c
	$(CC) $(CFLAGS) -o maxlen maxlen.c

clean:
	rm -f shell concat sum maxlen