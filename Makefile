CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic

all: server client

server: server.c
	$(CC) $(CFLAGS) server.c -o s -pthread

client: client.c
	$(CC) $(CFLAGS) client.c -o c

clean:
	rm -f s c

