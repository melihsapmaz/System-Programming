#include "file_operations.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <glob.h>


#define MAX_PATH_LEN 256
#define MAX_CLIENTS_DEFAULT 5
#define MAX_DIR_ENTRIES 32
#define MAX_FILE_NAME_LEN 32

int server_socket;
pthread_t *threads;
int *client_sockets;
int *client_pids;
int client_count = 0;
int max_clients = 0;


void send_message(int , const char *);
void *handle_client(void *);
void signal_handler(int);
char* get_directory_list();
void handle_download(int, char **);
void archive_files(char *);

int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <directory> [max_clients]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	char *directory = argv[1];
	struct stat st = {0};
	if (stat(directory, &st) == -1) {
		if(mkdir(directory, 0700) == -1) {
			perror("Unable to create directory");
			exit(EXIT_FAILURE);
		}
	}
	if (chdir(directory) == -1) {
		perror("Failed to change directory");
		exit(EXIT_FAILURE);
	}

	max_clients = (argc > 2) ? atoi(argv[2]) : MAX_CLIENTS_DEFAULT;
	if (max_clients <= 0) {
		fprintf(stderr, "Invalid max_clients value. Setting to default: %d\n", MAX_CLIENTS_DEFAULT);
		max_clients = MAX_CLIENTS_DEFAULT;
	}

	//allocate memory for client sockets and pids and threads
	client_sockets = (int *)malloc(max_clients * sizeof(int));
	client_pids = (int *)malloc(max_clients * sizeof(int));
	threads = (void *) malloc(max_clients * sizeof(pthread_t));
	void init_mutex();

	for (int i = 0; i < max_clients; i++) {
		client_sockets[i] = -1;
	}
	// Set up signal handler
	signal(SIGINT, signal_handler);

	// Create socket
	server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (server_socket == -1) {
		perror("Socket creation failed");
		exit(EXIT_FAILURE);
	}

	// Bind socket
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(getpid()); // Using PID as port number

	if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
		perror("Bind failed");
		exit(EXIT_FAILURE);
	}

	// Listen
	if (listen(server_socket, max_clients) == -1) {
		perror("Listen failed");
		exit(EXIT_FAILURE);
	}

	printf("Server listening on port %d...\n", getpid());

	while (1) {
		struct sockaddr_in client_addr;
		socklen_t addr_len = sizeof(client_addr);
		// Accept connection
		int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);
		if (client_socket == -1) {
			perror("Accept failed");
			exit(EXIT_FAILURE);
		}

		// Create thread to handle client
		pthread_create(&threads[client_count], NULL, handle_client, &client_socket);

		// Increment client count
		client_count++;
	}

	close(server_socket);
	return 0;
}

void send_message(int client_socket, const char *message) {
	send(client_socket, message, strlen(message), 0);
}

void *handle_client(void *arg) {
	int client_socket = *((int *)arg);
	char buffer[1024];
	int bytes_received;

	// Receive client PID
	int client_pid;
	recv(client_socket, &client_pid, sizeof(client_pid), 0);

	if (client_count > max_clients) {
		printf("Client PID %d tried to connect but the server is full...\n", client_pid);
		//send(client_socket, "full", strlen("full"), 0);
		kill(client_pid, SIGUSR1);
		//close(client_socket);
		return NULL;
	}
	// Store client PID
	client_pids[client_count] = client_pid;
	client_sockets[client_count] = client_socket;
	// Assign client identifier
	char client_identifier[15];
	snprintf(client_identifier, sizeof(client_identifier), "client%02d", client_count);

	printf("Client PID %d connected as \"%s\"\n", client_pid, client_identifier);

	while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
		buffer[bytes_received] = '\0';
		//printf("%s: %s", client_identifier, buffer);

		char *command = strtok(buffer, " ");
		if(strcmp(command, "help") == 0) {
			send_message(client_socket, "\n");
		}
		else if (strcmp(buffer, "list") == 0) {
			// Get list of elements in directory
			char *directory_list = get_directory_list();
			if(strlen(directory_list) == 0) {
				send_message(client_socket, "Directory is empty");
				continue;
			}
			// Send list to client
			send_message(client_socket, directory_list);
			// Free memory allocated for directory list
			free(directory_list);
		}
		else if (strcmp(command, "readF") == 0) {
			char *filename = strtok(NULL, " ");
			char *line_str = strtok(NULL, " ");
			int line = atoi(line_str);
			char *file_content = read_text_file(filename, line); // Call the read_text_file function
			send_message(client_socket, file_content);
			free(file_content);
		}
		else if (strcmp(command, "writeT") == 0) {
			char *filename = strtok(NULL, " ");
			char *content = strtok(NULL, " ");
			char *line_str = strtok(NULL, " ");
			int line = atoi(line_str);
			write_text_file(filename, content, line, 0); // Call the write_text_file function
			send_message(client_socket, "File written successfully");
		}
		else if(strcmp(command, "upload") == 0) {
			char *filename = strtok(NULL, " ");
			char *file_content = strtok(NULL, " ");
			write_text_file(filename, file_content, -1, 1); // Call the write_text_file function
			send_message(client_socket, "File uploaded successfully");
		}
		else if(strcmp(command, "download") == 0) {
			char *arguments[2];
			arguments[0] = "download";
			arguments[1] = strtok(NULL, " ");
			handle_download(client_socket, arguments);
		}
		else if(strcmp(command, "archServer") == 0) {
			char *filename = strtok(NULL, " ");
			//deleter the .tar extension
			filename = strtok(filename, ".");
			archive_files(filename);
			send_message(client_socket, "Files archived successfully");
		}
		else if(strcmp(command, "killServer") == 0) {
			printf("kill signal from client %s...terminating...\n", client_identifier);
			send_message(client_socket, "kill");
			close(client_socket);
			exit(EXIT_SUCCESS);
		}
		else if(strcmp(command, "quit") == 0) {
			send_message(client_socket, "quit");
			printf("Client %s disconnected\n", client_identifier);
		}
		else {
			send_message(client_socket, "Invalid command");
		}
	}

	close(client_socket);
	pthread_exit(NULL);
}

void signal_handler(int sig) {
	if (sig == SIGINT) {
		destroy_mutex();
		printf("Server received SIGINT signal. Terminating...\n");
		for (int i = 0; i < client_count; i++) {
			if (client_sockets[i+1] != -1) {
				kill(client_pids[i+1], SIGINT);
			}
			pthread_cancel(threads[i]);
			pthread_join(threads[i], NULL);
		}
		close(server_socket);
		exit(EXIT_SUCCESS);
	}
}

char* get_directory_list() {
	DIR *dir;
	struct dirent *entry;
	char *directory_list = (char *)malloc(sizeof (char) * MAX_DIR_ENTRIES * MAX_FILE_NAME_LEN);
	directory_list[0] = '\0'; // Initialize directory list

	dir = opendir("."); // Open current directory
	if (dir == NULL) {
		perror("Unable to open directory");
		return directory_list; // Return empty string instead of NULL
	}

	while ((entry = readdir(dir)) != NULL) {
		// Skip "." and ".." entries
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		// Check if entry is a directory
		struct stat entry_stat;
		stat(entry->d_name, &entry_stat);
		if (S_ISDIR(entry_stat.st_mode)) {
			// Append directory sign and directory entry to list
			strcat(directory_list, "/");
			strcat(directory_list, entry->d_name);
		} else {
			// Append directory entry to list
			strcat(directory_list, entry->d_name);
		}
		strcat(directory_list, "\n"); // Append newline character
	}

	closedir(dir); // Close directory

	return directory_list; // Return directory list
}

void handle_download(int client_socket, char **args) {
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

		char *message = malloc(strlen("download ") + strlen(filename) + file_size + 2);
		sprintf(message, "download %s %s", filename, file_content);
		send(client_socket, message, strlen(message), 0);

		free(file_content);
		free(message);
		fclose(file);
	} else {
		printf("Invalid number of arguments\n");
	}
}

void archive_files(char *filename){
	pid_t pid = fork();

	if (pid < 0) {
		// Fork failed
		perror("fork failed");
		return;
	}

	if (pid == 0) {
		// Child process
		// Execute tar command to archive files
		//add the .gz extension to the filename
		strcat(filename, ".tar.gz");
		glob_t glob_result;
		glob("*", GLOB_TILDE, NULL, &glob_result);
		char **argv = malloc((glob_result.gl_pathc + 4) * sizeof(char *));
		argv[0] = "tar";
		argv[1] = "czvf";
		argv[2] = filename;
		for(size_t i = 0; i < glob_result.gl_pathc; i++) {
			argv[i+3] = glob_result.gl_pathv[i];
		}
		argv[glob_result.gl_pathc + 3] = NULL;
		execvp("tar", argv);
		perror("execvp failed");
		// Free glob memory
		globfree(&glob_result);
		_exit(EXIT_FAILURE);
	} else {
		// Parent process
		// Wait for child process to finish
		int status;
		waitpid(pid, &status, 0);
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
			printf("Files archived successfully\n");
		} else {
			printf("Failed to archive files\n");
		}
	}
}

