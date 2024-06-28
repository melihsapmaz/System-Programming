#include "file_operations.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>

void handle_help(int, char **);
void handle_list(int , char **);
void handle_readF(int , char **);
void handle_writeT(int , char **);
void handle_upload(int , char **);
void handle_download(int , char **);
void handle_archServer(int , char **);
void handle_killServer(int , char **);
void handle_quit(int , char **);
void send_command(int , const char *);
void command_handler(int , const char *);
void try_connect(int );
void connect_and_wait(int );

int sc;

void sigint_handler() {
	/* Signal handler code.
	   This function will be called when Ctrl+C is pressed. */
	send(sc, "quit", strlen("quit"), 0);
	printf("\nExiting...\n");
	// Perform cleanup here such as closing open sockets
	exit(0);
}

void sigusr1_handler() {
	/* Signal handler code.
	   This function will be called when the server sends a SIGUSR1 signal. */
	printf("\nServer is full. Exiting...\n");
	exit(0);
}

typedef struct {
	char* command;
	void (*handler)(int, char**);
	int arg_count;
} Command;

typedef struct {
	char* command;
	char* description;
} CommandHelp;

Command commands[] = {
		{"help", handle_help, 2},
		{"list", handle_list, 1},
		{"readF", handle_readF, 3},
		{"writeT", handle_writeT, 4},
		{"upload", handle_upload, 2},
		{"download", handle_download, 2},
		{"archServer", handle_archServer, 2},
		{"killServer", handle_killServer, 1},
		{"quit", handle_quit, 1}
};

CommandHelp commandsHelp[] = {
		{"help", "Available commands are :\nhelp, list, readF, writeT, upload, download, archServer, quit, killServer"},
		{"help list", "list\nsends a request to display the list of files in Servers directory\n(also displays the list received from the Server)"},
		{"help readF", "readF <file> <line #>\n"
		               "requests to display the # line of the <file>, if no line number is given\n"
		               "the whole contents of the file is requested (and displayed on the client side) "},
		{"help writeT", "writeT <file> <line #> <string> :\n"
		                "request to write the content of “string” to the #th line the <file>, if the line # is not given\n"
		                "writes to the end of file. If the file does not exists in Servers directory creates and edits the\n"
		                "file at the same time"},
		{"help upload", "upload <file>\n"
		                "uploads the file from the current working directory of client to the Servers directory\n"
		                "(beware of the cases no file in clients current working directory and file with the same\n"
		                " name on Servers side)"},
		{"help download", "download <file>\n"
		                  "request to receive <file> from Servers directory to client side"},
		{"help archServer", "archServer <fileName>.tar\n"
		                    "Using fork, exec and tar utilities create a child process that will collect all the files currently\n"
		                    "available on the the Server side and store them in the <filename>.tar archive"},
		{"help killServer", "killServer\n"
		                    " Sends a kill request to the Server"},
		{"help quit", "quit\n"
		              "Send write request to Server side log file and quits "}
};

int num_commands = sizeof(commands) / sizeof(Command);
int num_commandsHelp = sizeof(commandsHelp) / sizeof(CommandHelp);

int main(int argc, char *argv[]) {
	if (argc != 3 || (strcmp(argv[1], "Connect") != 0 && strcmp(argv[1], "tryConnect") != 0)) {
		fprintf(stderr, "Usage: %s <Connect | tryConnect> <server_port>\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	signal(SIGINT, sigint_handler);
	signal(SIGUSR1, sigusr1_handler);
	int server_port = atoi(argv[2]);

	if (strcmp(argv[1], "Connect") == 0) {
		connect_and_wait(server_port);
	} else if (strcmp(argv[1], "tryConnect") == 0) {
		try_connect(server_port);
	} else {
		fprintf(stderr, "Invalid option: %s\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	return 0;
}

void handle_help(int client_socket, char **args) {
	send_command(client_socket, "help");
	if (args[1] == NULL) {
		for (int i = 0; i < num_commandsHelp; i++) {
			if (strcmp("help", commandsHelp[i].command) == 0) {
				printf("%s\n", commandsHelp[i].description);
				break;
			}
		}
	} else {
		char newCommand[20];
		strcpy(newCommand, args[0]);
		strcat(newCommand, " ");
		strcat(newCommand, args[1]);
		for (int i = 0; i < num_commandsHelp; i++) {
			if (strcmp(newCommand, commandsHelp[i].command) == 0) {
				printf("%s\n", commandsHelp[i].description);
				break;
			}
		}
	}
}

void handle_list(int client_socket, char **args) {
	if (args[1] == NULL) {
		// Send request to server to list files
		// display the list received from the Server
		send_command(client_socket, "list");
	} else {
		printf("Invalid number of arguments\n");
	}
}

void handle_readF(int client_socket, char **args) {
	int line_number = -1;
	if (args[1] != NULL) {
		char command[1024];
		strcpy(command, "readF ");
		strcat(command, args[1]);
		if (args[2] != NULL) {
			line_number = atoi(args[2]);
		}
		char str[10];
		sprintf(str, "%9d", line_number);
		strcat(command, " ");
		strcat(command, str);
		send_command(client_socket, command);
	} else {
		printf("Invalid number of arguments\n");
	}
}

void handle_writeT(int client_socket, char **args) {
	if(args[1] != NULL && args[2] != NULL){
		char command[1024];
		strcpy(command, "writeT ");
		strcat(command, args[1]);   //filename
		strcat(command, " ");

		if(args[3] == NULL){
			//args[2] is the string to write
			strcat(command, args[2]);   //content
			strcat(command, " ");
			strcat(command, "-1");      //line number
		}
		else{
			//args[2] is the line number
			//args[3] is the string to write
			strcat(command, args[3]);   //content
			strcat(command, " ");
			strcat(command, args[2]);   //line number
		}
		send_command(client_socket, command);
	}
	else{
		printf("Invalid number of arguments\n");
	}
}

void handle_upload(int client_socket, char **args) {
	if (args[1] != NULL) {
		char* filename = args[1];
		FILE *file = fopen(filename, "r");
		if (file == NULL) {
			printf("File does not exist\n");
			return;
		}
		fseek(file, 0, SEEK_END);
		long file_size = ftell(file);
		rewind(file);

		char *file_content = malloc(file_size + 1);
		fread(file_content, file_size, 1, file);
		file_content[file_size] = '\0';

		char *message = malloc(strlen("upload ") + strlen(filename) + file_size + 2);
		sprintf(message, "upload %s %s", filename, file_content);
		send_command(client_socket, message);

		free(file_content);
		free(message);
		fclose(file);
	} else {
		printf("Invalid number of arguments\n");
	}
}

void handle_download(int client_socket, char **args) {
	if (args[1] != NULL) {
		char* filename = args[1];
		char command[1024];
		sprintf(command, "download %s", filename);
		send_command(client_socket, command);
	} else {
		printf("Invalid number of arguments\n");
	}
}

void handle_archServer(int client_socket, char **args) {
	if (args[1] != NULL) {
		// Send request to server to archive files
		char command[1024];
		strcpy(command, "archServer ");
		strcat(command, args[1]);
		send_command(client_socket, command);
	} else {
		printf("Invalid number of arguments\n");
	}
}

void handle_killServer(int client_socket, char **args) {
	if (args[1] == NULL) {
		// Send kill request to server
		send_command(client_socket, "killServer");
	} else {
		printf("Invalid number of arguments\n");
	}
}

void handle_quit(int client_socket, char **args) {
	if (args[1] == NULL) {
		// Send write request to Server side log file and quit
		send_command(client_socket, "quit");
	} else {
		printf("Invalid number of arguments\n");
	}
}

void send_command(int client_socket, const char *command) {
	send(client_socket, command, strlen(command), 0);
}

void command_handler(int client_socket, const char *command) {
	//take the command and create a string array, delimiting by space
	char *input = strdup(command);
	char *token = strtok(input, " ");
	char *args[4];
	int i = 0;
	while (token != NULL) {
		args[i++] = token;
		token = strtok(NULL, " ");
	}
	args[i] = NULL;

	for(int j = 0; j < num_commands; j++){
		if(strcmp(args[0], commands[j].command) == 0){
			commands[j].handler(client_socket, args);
			return;
		}
	}
	printf("Invalid command\n");
}

void try_connect(int server_port) {
	char buffer[1024] = {0};
	int bytes_received = 0;
	while (1) {
		// Create socket
		int client_socket = socket(AF_INET, SOCK_STREAM, 0);
		sc = client_socket;
		if (client_socket == -1) {
			perror("Socket creation failed");
			exit(EXIT_FAILURE);
		}
		// Set up server address
		struct sockaddr_in server_addr;
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
		server_addr.sin_port = htons(server_port); // Using specified port number

		// Try to connect to server
		if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0) {
			// Send client PID to server
			int client_pid = getpid();
			send(client_socket, &client_pid, sizeof(client_pid), 0);
			// Enter command loop
			char input[1024];
			printf(">> Waiting for Queue...\n");
			while (1) {
				printf(">> Enter command : ");
				scanf("%[^\n]", input);
				getchar(); // Clear input buffer
				// Send command to server
				command_handler(client_socket, input);
				// Receive and display response from server
				bzero(buffer, sizeof(buffer));
				while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
					buffer[bytes_received] = '\0';
					char *com = strtok(buffer, " ");
					char *filename = strtok(NULL, " ");
					char *file_content = strtok(NULL, " ");
					if(strcmp("download", com) == 0){
						write_text_file(filename, file_content, -1, 1);
						break;
					}
					else if(strcmp("quit", com) == 0){
						printf("\nExiting...\n");
						input[0] = '\0';
						// change input to quit
						strcpy(input, "quit");
						break;
					}
					else if(strcmp("kill", com) == 0){
						input[0] = '\0';
						// change input to quit
						strcpy(input, "quit");
						break;
					}
					else if(strcmp("full", com) == 0){
						printf("Server is full...");
						input[0] = '\0';
						// change input to quit
						strcpy(input, "quit");
						break;
					}
					printf("%s", buffer);
					break; // Exit loop after receiving response
				}
				if(strcmp(input, "quit") == 0){
					printf("\n");
					break;
				}
				printf("\n");
				buffer[0] = '\0';
			}
			// Close connection
			close(client_socket);
			break; // Exit loop
		}
		// Close socket
		close(client_socket);
		printf("Exiting...\n");
		exit(0);
	}
}

void connect_and_wait(int server_port) {
	int flag = 0;
	char buffer[1024] = {0};
	int bytes_received = 0;
	while (1) {
		// Create socket
		int client_socket = socket(AF_INET, SOCK_STREAM, 0);
		sc = client_socket;
		if (client_socket == -1) {
			perror("Socket creation failed");
			exit(EXIT_FAILURE);
		}
		// Set up server address
		struct sockaddr_in server_addr;
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
		server_addr.sin_port = htons(server_port); // Using specified port number

		// Try to connect to server
		if (connect(client_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) == 0) {
			// Send client PID to server
			int client_pid = getpid();
			send(client_socket, &client_pid, sizeof(client_pid), 0);
			// Enter command loop
			char input[1024];
			printf(">> Waiting for Queue:\n");
			while (1) {
				printf(">> Enter command : ");
				scanf("%[^\n]", input);
				getchar(); // Clear input buffer
				// Send command to server
				command_handler(client_socket, input);
				// Receive and display response from server
				bzero(buffer, sizeof(buffer));
				while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
					buffer[bytes_received] = '\0';
					char *com = strtok(buffer, " ");
					char *filename = strtok(NULL, " ");
					char *file_content = strtok(NULL, " ");
					if (strcmp("download", com) == 0) {
						write_text_file(filename, file_content, -1, 1);
						break;
					}
					else if (strcmp("quit", com) == 0) {
						printf("\nExiting...\n");
						input[0] = '\0';
						// change input to quit
						strcpy(input, "quit");
						break;
					}
					else if (strcmp("kill", com) == 0) {
						printf("\nExiting...\n");
						input[0] = '\0';
						// change input to quit
						strcpy(input, "quit");
						break;
					}
					else if (strcmp("full", com) == 0) {
						printf("Server is full...");
						input[0] = '\0';
						// change input to quit
						strcpy(input, "full");
						break;
					}
					printf("%s", buffer);
					break; // Exit loop after receiving response
				}
				if (strcmp(input, "quit") == 0) {
					printf("\n");
					flag = 0;
					break;
				}
				else if (strcmp(input, "full") == 0) {
					printf("\n");
					flag = 1;
					break;
				}
				printf("\n");
				buffer[0] = '\0';
			}
			if (flag == 1) {
				printf("Trying to connect...\n");
				sleep(1);
				continue;
			}
			// Close connection
			close(client_socket);
			exit(0);
		}
		// Close socket
		close(client_socket);
		exit(0);
	}
}


