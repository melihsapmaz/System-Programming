# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -lpthread

# Source files
SERVER_SOURCES = Server.c
CLIENT_SOURCES = Client.c

# Output files
SERVER_OUTPUT = server
CLIENT_OUTPUT = client

all: $(SERVER_OUTPUT) $(CLIENT_OUTPUT)

$(SERVER_OUTPUT): $(SERVER_SOURCES)
	$(CC) $(CFLAGS) -o $(SERVER_OUTPUT) $(SERVER_SOURCES) -lm

$(CLIENT_OUTPUT): $(CLIENT_SOURCES)
	$(CC) $(CFLAGS) -o $(CLIENT_OUTPUT) $(CLIENT_SOURCES)

clean:
	rm -f $(SERVER_OUTPUT) $(CLIENT_OUTPUT)