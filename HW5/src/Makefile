CC = gcc
CFLAGS = -Wall -Wextra -pthread

.PHONY: all clean

all: main

220104004130_main: main.o
	$(CC) $(CFLAGS) -o main main.o

220104004130_main.o: main.c
	$(CC) $(CFLAGS) -c main.c

clean:
	rm -f main main.o