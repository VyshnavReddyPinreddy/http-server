CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic

all: server 

server: server.c
	$(CC) $(CFLAGS) server.c -o s -pthread

clean:
	rm -f s 

