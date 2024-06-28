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
void sigint_handler(int signum) {
    termination_flag = 1;
    printf("\nSIGINT received. Exiting...\n");
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

    struct timeval start, end;        // Variables to hold start and end times
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
    return EXIT_SUCCESS;                 // Exit the program
}

// Initialize the buffer
void init_buffer(buffer_t *buffer, int capacity) {
    buffer->buffer = (file_info_t *)malloc(sizeof(file_info_t) * capacity); // Allocate memory for the buffer
    buffer->capacity = capacity;        // Set the buffer capacity
    buffer->count = 0;                // Initialize the buffer count
    buffer->in = 0;                 // Initialize the buffer in index
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
    pthread_mutex_lock(&buffer->mutex);  // Lock the buffer mutex
    while (buffer->count == 0 && !buffer->done) {   // Wait while the buffer is empty and manager is not done
        pthread_cond_wait(&buffer->not_empty, &buffer->mutex);   // Wait on the not empty condition variable
    }
    if (buffer->count == 0 && buffer->done) {   // Check if buffer is empty and manager is done
        pthread_mutex_unlock(&buffer->mutex);   // Unlock the buffer mutex
        return 0;   // Return 0 indicating no more items to process
    }
    *file_info = buffer->buffer[buffer->out];   // Remove the file information from the buffer
    buffer->out = (buffer->out + 1) % buffer->capacity;   // Update the out index
    buffer->count--;    // Decrement the buffer count
    pthread_cond_signal(&buffer->not_full);    // Signal that the buffer is not full
    pthread_mutex_unlock(&buffer->mutex);   // Unlock the buffer mutex
    return 1;   // Return 1 indicating an item was removed
}

// Manager thread function
void *manager_thread(void *arg) {
    thread_params_t *params = (thread_params_t *)arg;   // Cast argument to thread parameters
    traverse_directory(params->src_dir, params->dst_dir, params->buffer, params);   // Traverse the source directory
    pthread_mutex_lock(&params->buffer->mutex);    // Lock the buffer mutex
    params->buffer->done = 1;    // Set the done flag indicating manager is done producing
    pthread_cond_broadcast(&params->buffer->not_empty);   // Signal all waiting threads that buffer is not empty
    pthread_mutex_unlock(&params->buffer->mutex);   // Unlock the buffer mutex
    return NULL;    // Exit the manager thread
}

// Worker thread function
void *worker_thread(void *arg) {
    thread_params_t *params = (thread_params_t *)arg;   // Cast argument to thread parameters
    file_info_t file_info;    // Variable to hold file information

    while (!termination_flag) {
        if (buffer_remove(params->buffer, &file_info)) {    // Remove file information from the buffer
            copy_file(file_info.src, file_info.dst, params->buffer_size);   // Copy the file
            atomic_fetch_add(&params->total_files, 1);  // Increment total files copied
        }
    }

    // Signal that worker thread is exiting
    pthread_mutex_lock(&params->output_mutex);   // Lock the output mutex
    printf("Worker thread %ld exiting.\n", pthread_self());   // Print message indicating worker thread is exiting
    pthread_mutex_unlock(&params->output_mutex);   // Unlock the output mutex
    return NULL;    // Exit the worker thread
}

// Copy file from source to destination
void copy_file(const char *src, const char *dst, int buffer_size) {
    int src_fd = open(src, O_RDONLY);    // Open source file for reading
    if (src_fd == -1) {
        perror("open src");
        return;
    }
    int dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);   // Open destination file for writing
    if (dst_fd == -1) {
        perror("open dst");
        close(src_fd);
        return;
    }

    char buffer[buffer_size];    // Buffer for copying file contents
    ssize_t bytes_read, bytes_written;
    while ((bytes_read = read(src_fd, buffer, buffer_size)) > 0) {    // Read from source file
        bytes_written = write(dst_fd, buffer, bytes_read);    // Write to destination file
        if (bytes_written == -1) {
            perror("write");
            close(src_fd);
            close(dst_fd);
            return;
        }
    }
    if (bytes_read == -1) {
        perror("read");
    }
    close(src_fd);    // Close source file
    close(dst_fd);    // Close destination file
}

// Traverse the source directory and add files to the buffer
void traverse_directory(const char *src_dir, const char *dst_dir, buffer_t *buffer, thread_params_t *params) {
    DIR *dir = opendir(src_dir);    // Open source directory
    if (dir == NULL) {
        perror("opendir");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (termination_flag) {
            break;  // If termination flag is set, exit the loop
        }

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;   // Skip current and parent directory entries
        }

        char src_path[MAX_PATH], dst_path[MAX_PATH];
        snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, entry->d_name);    // Construct source path
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, entry->d_name);    // Construct destination path

        struct stat statbuf;
        if (stat(src_path, &statbuf) == -1) {
            perror("stat");
            continue;
        }

        if (S_ISDIR(statbuf.st_mode)) {    // Check if it is a directory
            pthread_mutex_lock(&params->output_mutex);    // Lock the output mutex
            printf("Copying directory: %s -> %s\n", src_path, dst_path);    // Print message indicating directory copy
            pthread_mutex_unlock(&params->output_mutex);    // Unlock the output mutex
            if (mkdir(dst_path, statbuf.st_mode) == -1) {
                perror("mkdir");
                continue;
            }
            atomic_fetch_add(&params->directories, 1);    // Increment directory count
            traverse_directory(src_path, dst_path, buffer, params);    // Traverse the directory recursively
        } else if (S_ISREG(statbuf.st_mode)) {    // Check if it is a regular file
            file_info_t file_info;    // Variable to hold file information
            strncpy(file_info.src, src_path, MAX_PATH);    // Copy source path to file information
            strncpy(file_info.dst, dst_path, MAX_PATH);    // Copy destination path to file information
            buffer_add(buffer, &file_info);    // Add file information to the buffer
            atomic_fetch_add(&params->regular_files, 1);    // Increment regular file count
        } else if (S_ISFIFO(statbuf.st_mode)) {    // Check if it is a FIFO file
            pthread_mutex_lock(&params->output_mutex);    // Lock the output mutex
            printf("FIFO file: %s -> %s\n", src_path, dst_path);    // Print message indicating FIFO file copy
            pthread_mutex_unlock(&params->output_mutex);    // Unlock the output mutex
            if (mkfifo(dst_path, statbuf.st_mode) == -1) {
                perror("mkfifo");
                continue;
            }
            atomic_fetch_add(&params->fifo_files, 1);    // Increment FIFO file count
        }
    }
    closedir(dir);    // Close source directory
}
