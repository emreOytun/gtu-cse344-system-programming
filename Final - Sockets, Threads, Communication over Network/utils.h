#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#define BUFFER_SIZE 1024
#define DATA_SIZE_MAX_CHAR_NUM 6

#define ACK_MSG "ACK"

#define PID_MAX_CHAR_SIZE 20
#define COORDINATE_MAX_CHAR_SIZE 20
#define NUM_CLIENTS_MAX_CHAR_SIZE 20

void int_to_str(int num, char *str);
int is_errno_eintr_or_eagain();
int write_file_until_size(int fd, const char* buf, size_t size);
int read_file_until_size(int fd, char* buf, size_t size);
int read_from_socket(int socket, char* buf);
int write_to_socket(int socket, char* buf, size_t size);
int write_log_until_size(int fifo_fd, const char* buf, size_t size);
