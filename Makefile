CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic

all: server
	./s

server: server.c
	$(CC) $(CFLAGS) server.c -o s -pthread

clean:
	rm -f s

.PHONY: all clean