CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11

TARGETS = producer consumer

all: $(TARGETS)

producer: producer.o common.o
	$(CC) $(CFLAGS) -o producer producer.o common.o

consumer: consumer.o common.o
	$(CC) $(CFLAGS) -o consumer consumer.o common.o

producer.o: producer.c common.h
	$(CC) $(CFLAGS) -c producer.c

consumer.o: consumer.c common.h
	$(CC) $(CFLAGS) -c consumer.c

common.o: common.c common.h
	$(CC) $(CFLAGS) -c common.c

clean:
	rm -f *.o $(TARGETS)

.PHONY: all clean