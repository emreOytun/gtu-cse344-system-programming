#include "utils.h"


void int_to_str(int num, char *str) {
    int i = 0;
    int is_negative = 0;

    // Handle 0 explicitly
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    // Handle negative numbers
    if (num < 0) {
        is_negative = 1;
        num = -num;
    }

    // Process each digit
    while (num != 0) {
        int rem = num % 10;
        str[i++] = rem + '0';
        num = num / 10;
    }

    // If the number is negative, append '-'
    if (is_negative) {
        str[i++] = '-';
    }

    str[i] = '\0'; // Null-terminate the string

    // Reverse the string
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

int is_errno_eintr_or_eagain() {
	if (errno == EINTR || errno == EAGAIN) {
		return 1;
	}
	return 0;
}

int write_file_until_size(int fd, const char* buf, size_t size) {
	if (size > 0) {
		// Write until all data is written
		int total_bytes_written = 0;
		while(total_bytes_written != (int) size) {
			int write_bytes = write(fd, (void*) &buf[total_bytes_written], size - total_bytes_written);
			if (write_bytes == -1) {
				if (!is_errno_eintr_or_eagain()) return -1;		
				errno = 0;	
			} 
			else total_bytes_written += write_bytes;
		}
		sleep(0.15);
		return total_bytes_written;
	}
	return 0;
}

int read_file_until_size(int fd, char* buf, size_t size) {
	int total_bytes_read = 0;
	while (total_bytes_read != (int) size) {
		int read_bytes = read(fd, (void*) &buf[total_bytes_read], size - total_bytes_read);
		if (read_bytes == -1) {
			if (!is_errno_eintr_or_eagain()) return -1;
			errno = 0;
		}
		else if (read_bytes == 0) {
			return -1;
		}
		else total_bytes_read += read_bytes;
	}
	return total_bytes_read;
}

int read_from_socket(int socket, char* buf) {
	int data_size;
	
	char size[DATA_SIZE_MAX_CHAR_NUM];
	int is_done = 0;
	while (!is_done) {
		int read_bytes = read_file_until_size(socket, size, DATA_SIZE_MAX_CHAR_NUM);
		if (read_bytes == 0) return -1;
		if (read_bytes == -1) {
			if (!is_errno_eintr_or_eagain()) return -1;	
			errno = 0;
		}
		else {
			data_size = atoi(size);	
			is_done = 1;
		}
	}
	
	int res = read_file_until_size(socket, buf, data_size);
	if (res > 0) buf[res] = '\0';
	return res;
}

int write_to_socket(int socket, char* buf, size_t size) {	
	if (size > 0) {
		char data_size[DATA_SIZE_MAX_CHAR_NUM];
		int_to_str(size, data_size);
		
		for (int i = strlen(data_size); i < DATA_SIZE_MAX_CHAR_NUM; i++) {
        	data_size[i] = '\n';
    	}
			
		int is_done = 0;
		while (!is_done) {
			int written_bytes = write_file_until_size(socket, data_size, DATA_SIZE_MAX_CHAR_NUM); // +1 for sending EOF 
			if (written_bytes == 0) return -1;
			if (written_bytes == -1) {
				if (!is_errno_eintr_or_eagain()) return -1;	
				errno = 0;
			}
			else is_done = 1;
		}
		
		return write_file_until_size(socket, buf, size);
	}
	return 0;
}

int write_log_until_size(int fifo_fd, const char* buf, size_t size) {
	if (size > 0) {
		// Write until all data is written
		int total_bytes_written = 0;
		while(total_bytes_written != (int) size) {
			int write_bytes = write(fifo_fd, (void*) &buf[total_bytes_written], size - total_bytes_written);
			if (write_bytes == -1) {
				if (!is_errno_eintr_or_eagain()) return -1;			
			} 
			else total_bytes_written += write_bytes;
		}
	}
	return 0;
}
