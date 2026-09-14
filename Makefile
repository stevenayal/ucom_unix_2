CC=gcc
CFLAGS=-Wall -Wextra -Wpedantic -std=c11 -g

SERVER=server
CLIENT=client

SERVER_SRC=src/server.c
CLIENT_SRC=src/client.c

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_SRC)
	$(CC) $(CFLAGS) $(SERVER_SRC) -o $(SERVER)

$(CLIENT): $(CLIENT_SRC)
	$(CC) $(CFLAGS) $(CLIENT_SRC) -o $(CLIENT)

run-server: $(SERVER)
	./$(SERVER)

run-client: $(CLIENT)
	./$(CLIENT)

test: all
	bash tests/integration_test.sh

clean:
	rm -f $(SERVER) $(CLIENT)

.PHONY: all run-server run-client test clean
