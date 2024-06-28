#ifndef UNTITLED_FILE_OPERATIONS_H
#define UNTITLED_FILE_OPERATIONS_H

#include <stdlib.h>

char* read_text_file(const char *filename, int line_number);
void write_text_file(const char *filename, const char *content, int line_number, int upload_flag);
char* read_binary_file(const char *filename, size_t *size);
void write_binary_file(const char *filename, const void *content, size_t size);
void destroy_mutex();
void init_mutex();

#endif //UNTITLED_FILE_OPERATIONS_H
