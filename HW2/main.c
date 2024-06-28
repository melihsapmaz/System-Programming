#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#define FIFO1_PATH "fifo1"
#define FIFO2_PATH "fifo2"

volatile sig_atomic_t counter = 0;
pid_t first_child_pid;
pid_t second_child_pid;
int array_size = 0;

void GenerateRandomNumbers(int* numbers, int count) {
	for (int i = 0; i < count; i++) {
		numbers[i] = rand() % 10;
	}
}

void SendDataToFifo(const char *fifo_path, const int *numbers, int count, const char *command) {
	int fd = open(fifo_path, O_WRONLY);
	if (fd == -1) {
		perror("Error opening FIFO");
		exit(EXIT_FAILURE);
	}
	if(write(fd, numbers, count * sizeof(int)) == -1) {
		perror("Error writing to FIFO");
		exit(EXIT_FAILURE);
	}

	if (strlen(command) == 0) {
		close(fd);
		return;
	}

	if(write(fd, command, strlen(command) + 1) == -1) {
		perror("Error writing to FIFO");
		exit(EXIT_FAILURE);
	}

	close(fd);
}

void FirstChildProcess(int count) {
	int sum = 0;
	int fd;

	fd = open(FIFO1_PATH, O_RDONLY);
	if (fd == -1) {
		perror("Error opening FIFO");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < count; i++) {
		int number;
		if (read(fd, &number, sizeof(int)) == -1) {
			perror("Error reading from FIFO");
			exit(EXIT_FAILURE);
		}
		sum += number;
	}

	close(fd);

	fd = open(FIFO2_PATH, O_WRONLY);
	if (fd == -1) {
		perror("Error opening FIFO");
		exit(EXIT_FAILURE);
	}
	if(write(fd, &sum, sizeof(int)) == -1) {
		perror("Error writing to FIFO");
		exit(EXIT_FAILURE);
	}
	close(fd);
	printf("Sum of numbers: %d\n", sum);
	exit(EXIT_SUCCESS);
}

void SecondChildProcess(int count) {
	char command[20] = "asdf";
	int *numbers = (int*)malloc(count * sizeof(int));

	if (numbers == NULL) {
		fprintf(stderr, "Failed to allocate memory\n");
		exit(EXIT_FAILURE);
	}

	int product = 1;
	int fd;
	int num_read;
	int sum = 0;
	// Open FIFO for reading
	fd = open(FIFO2_PATH, O_RDONLY);
	if (fd == -1) {
		perror("Error opening FIFO");
		exit(EXIT_FAILURE);
	}

	// Read numbers from FIFO
	num_read = read(fd, numbers, count * sizeof(int));
	if (num_read == -1) {
		perror("Error reading from FIFO");
		exit(EXIT_FAILURE);
	}

	// Read command from FIFO
	num_read = read(fd, command, sizeof(command));
	if (num_read == -1) {
		perror("Error reading from FIFO");
		exit(EXIT_FAILURE);
	}


	num_read = read(fd, &sum, sizeof(int));
	if (num_read == -1) {
		perror("Error reading from FIFO");
		exit(EXIT_FAILURE);
	}

	// Close FIFO
	close(fd);

	// Check if command is "multiply"
	if (strcmp(command, "multiply") == 0) {
		// Calculate product of all numbers
		for (int i = 0; i < count; i++) {
			product *= numbers[i];
		}
		printf("Total result: %d\n", product + sum);
	} else {
		printf("Invalid command received.\n");
	}

	free(numbers);
	exit(EXIT_SUCCESS);
}

void SigchldHandler() {
	int status;
	pid_t pid;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		counter++;
		if (WIFEXITED(status)) {
			printf("Child process %d exited with status %d\n", pid, WEXITSTATUS(status));
		}
		else {
			printf("Child process %d exited abnormally\n", pid);
		}
	}
}

int main() {

	printf("Enter the size of the array: ");
	scanf("%d", &array_size);
	int num_children = 2;
	// Generate random numbers
	int *numbers = (int*)malloc(array_size * sizeof(int));
	if (numbers == NULL) {
		fprintf(stderr, "Failed to allocate memory\n");
		exit(EXIT_FAILURE);
	}
	for (int i = 0; i < array_size; ++i) {
		numbers[i] = 0;
	}

	// Set signal handler for SIGCHLD
	struct sigaction sa;
	sa.sa_handler = SigchldHandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("Error setting signal handler for SIGCHLD");
		exit(EXIT_FAILURE);
	}

	// Create FIFOs
	for(int i = 0; i < num_children; i++) {
		pid_t pid;
		if(i == 0) {
			first_child_pid = fork();
			pid = first_child_pid;
		} else {
			second_child_pid = fork();
			pid = second_child_pid;
		}
		if (pid == -1) {
			perror("Error forking child process");
			exit(EXIT_FAILURE);
		} else if (pid == 0) {
			if (i == 0) {
				sleep(2);
				FirstChildProcess(array_size);
			}
			else {
				sleep(2);
				SecondChildProcess(array_size);
			}
		}
	}

	// Seed the random number generator
	srand(time(NULL));
	GenerateRandomNumbers(numbers, array_size);
	// Create FIFOs if they don't exist
	if (mkfifo(FIFO1_PATH, 0666) == -1 && errno != EEXIST) {
		perror("Error creating FIFO1");
		exit(EXIT_FAILURE);
	}

	if (mkfifo(FIFO2_PATH, 0666) == -1 && errno != EEXIST) {
		perror("Error creating FIFO2");
		exit(EXIT_FAILURE);
	}

	// Wait for child processes to exit
	while (counter < num_children) {
		printf("Proceeding...\n");
		sleep(2);
	}
	// Remove FIFOs
	if (unlink(FIFO1_PATH) == -1) {
		perror("Error removing FIFO1");
		exit(EXIT_FAILURE);
	}

	if (unlink(FIFO2_PATH) == -1) {
		perror("Error removing FIFO2");
		exit(EXIT_FAILURE);
	}
	free(numbers);
	return 0;
}
