#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/time.h>
#include <stdatomic.h>
#include <signal.h>
#include <errno.h>

// Maximum path length for file and directory names
#define MAX_PATH 1024
#define HASH_TABLE_SIZE 100  // Define size for the hash table

// Global flag to indicate program termination
volatile sig_atomic_t termination_flag = 0;

// Signal handler function for SIGINT
void sigint_handler() {
	// Cancel the copy operation, if any file is copied, undo the operation
	printf("\nSIGINT received. Exiting...\n");
	termination_flag = 1;
}

// Structure to hold source and destination file paths
typedef struct {
	char src[MAX_PATH];
	char dst[MAX_PATH];
} file_info_t;


// Buffer structure for storing file information to be processed by workers
typedef struct {
	file_info_t *buffer;        // Array of file information
	int capacity;               // Maximum number of items in the buffer
	int count;                  // Current number of items in the buffer
	int in;                     // Index for the next write operation
	int out;                    // Index for the next read operation
	int done;                   // Flag indicating if the manager is done producing
	pthread_mutex_t mutex;      // Mutex for synchronizing access to the buffer
	pthread_cond_t not_empty;   // Condition variable for buffer not empty
	pthread_cond_t not_full;    // Condition variable for buffer not full
} buffer_t;

typedef struct {
	buffer_t *buffer;           // Shared buffer between manager and workers
	int buffer_size;            // Size of the buffer
	int num_workers;            // Number of worker threads
	char src_dir[MAX_PATH];     // Source directory path
	char dst_dir[MAX_PATH];     // Destination directory path
	atomic_int total_files;     // Total number of files copied
	atomic_int regular_files;   // Number of regular files copied
	atomic_int fifo_files;      // Number of FIFO files copied
	atomic_int directories;     // Number of directories copied
	atomic_llong total_bytes;   // Total bytes copied
	pthread_mutex_t output_mutex;  // Mutex for synchronizing output
	pthread_barrier_t barrier;  // Barrier for synchronizing worker threads
} thread_params_t;

void *manager_thread(void *arg);
void *worker_thread(void *arg);
void init_buffer(buffer_t *buffer, int capacity);
void destroy_buffer(buffer_t *buffer);
void buffer_add(buffer_t *buffer, file_info_t *file_info);
int buffer_remove(buffer_t *buffer, file_info_t *file_info);
void copy_file(const char *src, const char *dst, int buffer_size);
void traverse_directory(const char *src_dir, const char *dst_dir, buffer_t *buffer, thread_params_t *params);

int main(int argc, char *argv[]) {
	// Install signal handler for SIGINT
	if (signal(SIGINT, sigint_handler) == SIG_ERR) {
		perror("signal");
		return EXIT_FAILURE;
	}

	if (argc != 5) {
		fprintf(stderr, "Usage: %s <buffer_size> <num_workers> <src_dir> <dst_dir>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	int buffer_size = atoi(argv[1]);      // Buffer size
	int num_workers = atoi(argv[2]);      // Number of worker threads
	char *src_dir = argv[3];              // Source directory
	char *dst_dir = argv[4];              // Destination directory

	buffer_t buffer;
	init_buffer(&buffer, buffer_size);    // Initialize the buffer

	pthread_t manager;
	pthread_t workers[num_workers];       // Array to hold worker thread IDs

	thread_params_t params = {          // Thread parameters
			.buffer = &buffer,
			.buffer_size = buffer_size,
			.num_workers = num_workers,
			.total_files = ATOMIC_VAR_INIT(0),
			.regular_files = ATOMIC_VAR_INIT(0),
			.fifo_files = ATOMIC_VAR_INIT(0),
			.directories = ATOMIC_VAR_INIT(0),
			.total_bytes = ATOMIC_VAR_INIT(0),
	};

	strncpy(params.src_dir, src_dir, MAX_PATH); // Copy source and destination directory paths
	strncpy(params.dst_dir, dst_dir, MAX_PATH); // to the thread parameters

	pthread_mutex_init(&params.output_mutex, NULL); // Initialize output mutex
	pthread_barrier_init(&params.barrier, NULL, num_workers + 1); // Initialize barrier

	struct timeval start, end;  		// Variables to hold start and end times
	gettimeofday(&start, NULL);           // Start timing the operation

	pthread_create(&manager, NULL, manager_thread, &params);  // Create the manager thread

	for (int i = 0; i < num_workers; i++) {
		pthread_create(&workers[i], NULL, worker_thread, &params);  // Create worker threads
	}

	pthread_join(manager, NULL);          // Wait for manager thread to finish

	for (int i = 0; i < num_workers; i++) {
		pthread_join(workers[i], NULL);   // Wait for all worker threads to finish
	}

	gettimeofday(&end, NULL);             // End timing the operation
	long seconds = end.tv_sec - start.tv_sec;   // Calculate elapsed time in seconds
	long microseconds = end.tv_usec - start.tv_usec;    //microseconds
	long elapsed_microseconds = (seconds * 1000000) + microseconds; // Total elapsed time in microseconds
	long minutes = elapsed_microseconds / (1000000 * 60);   // Calculate minutes
	seconds = (elapsed_microseconds % (1000000 * 60)) / 1000000;    // Calculate seconds
	long milliseconds = (elapsed_microseconds % 1000000) / 1000;    // Calculate milliseconds

	printf("\n---------------STATISTICS--------------------\n");
	printf("Consumers: %d - Buffer Size: %d\n", num_workers, buffer_size);
	printf("Number of Regular Files: %d\n", params.regular_files);
	printf("Number of FIFO Files: %d\n", params.fifo_files);
	printf("Number of Directories: %d\n", params.directories);
	printf("TOTAL BYTES COPIED: %lld\n", params.total_bytes);
	printf("TOTAL TIME: %02ld:%02ld.%03ld (min:sec.mili)\n", minutes, seconds, milliseconds);

	destroy_buffer(&buffer);              // Destroy the buffer and free resources
	pthread_mutex_destroy(&params.output_mutex);    // Destroy the output mutex
	pthread_barrier_destroy(&params.barrier); // Destroy the barrier
	return EXIT_SUCCESS;    		  // Exit the program
}

// Initialize the buffer
void init_buffer(buffer_t *buffer, int capacity) {
	buffer->buffer = (file_info_t *)malloc(sizeof(file_info_t) * capacity); // Allocate memory for the buffer
	buffer->capacity = capacity;        // Set the buffer capacity
	buffer->count = 0;  			  // Initialize the buffer count
	buffer->in = 0; 			  // Initialize the buffer in index
	buffer->out = 0;    // Initialize the buffer out index
	buffer->done = 0;   // Initialize the done flag
	pthread_mutex_init(&buffer->mutex, NULL);   // Initialize the buffer mutex
	pthread_cond_init(&buffer->not_empty, NULL);    // Initialize the not empty condition variable
	pthread_cond_init(&buffer->not_full, NULL);    // Initialize the not full condition variable
}

// Destroy the buffer
void destroy_buffer(buffer_t *buffer) {
	free(buffer->buffer);   // Free the buffer memory
	pthread_mutex_destroy(&buffer->mutex);  // Destroy the buffer mutex
	pthread_cond_destroy(&buffer->not_empty);   // Destroy the not empty condition variable
	pthread_cond_destroy(&buffer->not_full);    // Destroy the not full condition variable
}

// Add file information to the buffer
void buffer_add(buffer_t *buffer, file_info_t *file_info) {
	pthread_mutex_lock(&buffer->mutex);  // Lock the buffer mutex
	while (buffer->count == buffer->capacity) { // Wait while the buffer is full
		pthread_cond_wait(&buffer->not_full, &buffer->mutex);   // Wait on the not full condition variable
	}
	buffer->buffer[buffer->in] = *file_info;    // Add the file information to the buffer
	buffer->in = (buffer->in + 1) % buffer->capacity;   // Update the in index
	buffer->count++;    // Increment the buffer count
	pthread_cond_signal(&buffer->not_empty);    // Signal that the buffer is not empty
	pthread_mutex_unlock(&buffer->mutex);   // Unlock the buffer mutex
}

// Remove file information from the buffer
int buffer_remove(buffer_t *buffer, file_info_t *file_info) {
	pthread_mutex_lock(&buffer->mutex); // Lock the buffer mutex
	while (buffer->count == 0 && !buffer->done) {   // Wait while the buffer is empty and not done
		pthread_cond_wait(&buffer->not_empty, &buffer->mutex);  // Wait on the not empty condition variable
	}
	if (buffer->count == 0 && buffer->done) {   // If the buffer is empty and done
		pthread_mutex_unlock(&buffer->mutex);   // Unlock the buffer mutex
		return 0;
	}
	*file_info = buffer->buffer[buffer->out];   // Remove the file information from the buffer
	buffer->out = (buffer->out + 1) % buffer->capacity; // Update the out index
	buffer->count--;   // Decrement the buffer count
	pthread_cond_signal(&buffer->not_full);    // Signal that the buffer is not full
	pthread_mutex_unlock(&buffer->mutex);   // Unlock the buffer mutex
	return 1;
}

// Manager thread function
void *manager_thread(void *arg) {
	thread_params_t *params = (thread_params_t *)arg;    // Get the thread parameters
	traverse_directory(params->src_dir, params->dst_dir, params->buffer, params);    // Traverse the source directory
	pthread_mutex_lock(&params->buffer->mutex);   // Lock the buffer mutex
	params->buffer->done = 1;    // Set the done flag
	pthread_cond_broadcast(&params->buffer->not_empty);  // Signal all waiting threads that the buffer is not empty
	pthread_mutex_unlock(&params->buffer->mutex);    // Unlock the buffer mutex
	pthread_barrier_wait(&params->barrier);  // Wait at the barrier
	return NULL;
}

// Worker thread function
void *worker_thread(void *arg) {
	thread_params_t *params = (thread_params_t *)arg;    // Get the thread parameters
	file_info_t file_info;   // File information
	while (buffer_remove(params->buffer, &file_info)) {  // Remove file information from the buffer
		copy_file(file_info.src, file_info.dst, params->buffer_size);   // Copy the file
	}
	pthread_barrier_wait(&params->barrier);  // Wait at the barrier
	return NULL;
}

// Function to copy a file from src to dst with a specified buffer size
void copy_file(const char *src, const char *dst, int buffer_size) {
	int src_fd, dst_fd;    // Source and destination file descriptors
	char *buffer = (char *)malloc(buffer_size);  // Allocate memory for the buffer
	ssize_t bytes_read, bytes_written;   // Variables to hold bytes read and written
	src_fd = open(src, O_RDONLY);    // Open the source file
	if (src_fd == -1) {
		perror("open src");
		free(buffer);
		return;
	}
	dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);    // Open the destination file
	if (dst_fd == -1) {
		perror("open dst");
		close(src_fd);
		free(buffer);
		return;
	}
	while ((bytes_read = read(src_fd, buffer, buffer_size)) > 0) {  // Read from the source file
		bytes_written = write(dst_fd, buffer, bytes_read);  // Write to the destination file
		if (bytes_written != bytes_read) {
			perror("write");
			break;
		}
	}
	if (bytes_read == -1) {
		perror("read");
	}
	close(src_fd);    // Close the source file
	close(dst_fd);    // Close the destination file
	free(buffer);    // Free the buffer memory
}

// Function to traverse the source directory and add files to the buffer
void traverse_directory(const char *src_dir, const char *dst_dir, buffer_t *buffer, thread_params_t *params) {
	DIR *dir = opendir(src_dir);    // Open the source directory
	if (!dir) {
		perror("opendir");
		return;
	}
	struct dirent *entry;    // Directory entry
	while ((entry = readdir(dir)) != NULL) {   // Read directory entries
		if (termination_flag) break;   // Check for termination flag
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {  // Skip . and ..
			continue;
		}
		char src_path[MAX_PATH];    // Source path
		char dst_path[MAX_PATH];    // Destination path
		snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, entry->d_name);   // Create source path
		snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, entry->d_name);   // Create destination path
		if (entry->d_type == DT_DIR) {    // If the entry is a directory
			pthread_mutex_lock(&params->output_mutex);  // Lock the output mutex
			printf("Creating directory: %s\n", dst_path);  // Print the directory creation message
			pthread_mutex_unlock(&params->output_mutex);  // Unlock the output mutex
			mkdir(dst_path, 0755);  // Create the directory
			atomic_fetch_add(&params->directories, 1);    // Increment the directory count
			traverse_directory(src_path, dst_path, buffer, params);   // Recursively traverse the directory
		} else if (entry->d_type == DT_REG) {  // If the entry is a regular file
			pthread_mutex_lock(&params->output_mutex);  // Lock the output mutex
			printf("Adding file to buffer: %s\n", src_path);    // Print the file addition message
			pthread_mutex_unlock(&params->output_mutex);  // Unlock the output mutex
			file_info_t file_info = {.src = "", .dst = ""};   // Initialize file information
			strncpy(file_info.src, src_path, MAX_PATH);   // Copy source path to file information
			strncpy(file_info.dst, dst_path, MAX_PATH);   // Copy destination path to file information
			buffer_add(buffer, &file_info);    // Add the file information to the buffer
			atomic_fetch_add(&params->regular_files, 1);    // Increment the regular file count
		} else if (entry->d_type == DT_FIFO) { // If the entry is a FIFO file
			pthread_mutex_lock(&params->output_mutex);  // Lock the output mutex
			printf("Adding FIFO to buffer: %s\n", src_path);    // Print the FIFO addition message
			pthread_mutex_unlock(&params->output_mutex);  // Unlock the output mutex
			file_info_t file_info = {.src = "", .dst = ""};   // Initialize file information
			strncpy(file_info.src, src_path, MAX_PATH);   // Copy source path to file information
			strncpy(file_info.dst, dst_path, MAX_PATH);   // Copy destination path to file information
			buffer_add(buffer, &file_info);    // Add the file information to the buffer
			atomic_fetch_add(&params->fifo_files, 1);    // Increment the FIFO file count
		}
	}
	closedir(dir);   // Close the directory
}
