# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -pthread

# Source files
SERVER_SOURCES = Server.c file_operations.c
CLIENT_SOURCES = Client.c file_operations.c

# Output files
SERVER_OUTPUT = server
CLIENT_OUTPUT = client

all: $(SERVER_OUTPUT) $(CLIENT_OUTPUT)

$(SERVER_OUTPUT): $(SERVER_SOURCES)
	$(CC) $(CFLAGS) -o $(SERVER_OUTPUT) $(SERVER_SOURCES)

$(CLIENT_OUTPUT): $(CLIENT_SOURCES)
	$(CC) $(CFLAGS) -o $(CLIENT_OUTPUT) $(CLIENT_SOURCES)

clean:
	rm -f $(SERVER_OUTPUT) $(CLIENT_OUTPUT)