#include "file_operations.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>

pthread_mutex_t file_mutex;

// Function to read a text file
char* read_text_file(const char *filename, int line_number) {
	pthread_mutex_lock(&file_mutex);
	FILE *file = fopen(filename, "r");
	if (file == NULL) {
		perror("Unable to open file");
		return NULL;
	}

	char *content = malloc(1024); // Allocate memory for content
	content[0] = '\0'; // Initialize content

	char buffer[1024];
	int current_line = 1;
	while (fgets(buffer, sizeof(buffer), file) != NULL) {
		if (line_number == -1 || line_number == current_line) {
			if (strlen(content) + strlen(buffer) + 1 > sizeof(content)) {
				content = realloc(content, strlen(content) + strlen(buffer) + 1);
			}
			strcat(content, buffer);
		}
		if (line_number == current_line) {
			break;
		}
		current_line++;
	}

	fclose(file); // Close file
	pthread_mutex_unlock(&file_mutex);
	return content; // Return content
}

// Function to write to a text file
void write_text_file(const char *filename, const char *content, int line_number, int upload_flag) {
	pthread_mutex_lock(&file_mutex);
	if(upload_flag == 1) {
		FILE *file = fopen(filename, "w");
		if (file == NULL) {
			perror("Unable to open file");
			return;
		}
		fputs(content, file); // Write content to file
		fclose(file); // Close file
		return;
	}
	FILE *file = fopen(filename, "r");
	if (file == NULL) {
		perror("Unable to open file");
		return;
	}

	char buffer[1024];
	char *file_content = malloc(1024 * 1024); // Allocate memory for file content
	file_content[0] = '\0'; // Initialize file content

	int current_line = 1;
	while (fgets(buffer, sizeof(buffer), file) != NULL) {
		if (line_number == current_line) {
			strcat(file_content, content);
			strcat(file_content, "\n"); // Add newline character
		} else {
			strcat(file_content, buffer);
		}
		current_line++;
	}

	// If line_number is -1, append content at the end of the file
	if (line_number == -1) {
		//reallocate memory if needed
		if (strlen(file_content) + strlen(content) + 1 > sizeof(file_content)) {
			file_content = realloc(file_content, strlen(file_content) + strlen(content) + 1);
		}
		strcat(file_content, "\n"); // Add newline character
		strcat(file_content, content);
	}

	fclose(file); // Close file

	file = fopen(filename, "w"); // Open file in write mode
	if (file == NULL) {
		perror("Unable to open file");
		return;
	}

	fputs(file_content, file); // Write the new content to the file

	free(file_content); // Free the allocated memory

	pthread_mutex_unlock(&file_mutex);
	fclose(file); // Close file
}

// Function to read a binary file
char* read_binary_file(const char *filename, size_t *size) {
	FILE *file = fopen(filename, "rb");
	if (file == NULL) {
		perror("Unable to open file");
		return NULL;
	}

	fseek(file, 0, SEEK_END);
	*size = ftell(file);
	rewind(file);

	char *content = malloc(*size);
	fread(content, *size, 1, file);
	fclose(file);

	return content;
}

// Function to write to a binary file
void write_binary_file(const char *filename, const void *content, size_t size) {
	FILE *file = fopen(filename, "wb");
	if (file == NULL) {
		perror("Unable to open file");
		return;
	}
	fwrite(content, size, 1, file); // Write content to file
	fclose(file); // Close file
}

void init_mutex(){
	pthread_mutex_init(&file_mutex, NULL);
}

void destroy_mutex(){
	pthread_mutex_destroy(&file_mutex);
}