#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#include "utility.h"

// Convert string to integer
int convert_str_to_int(const char* str, int* num) {
    char* end;
    const long l = strtol(str, &end, 10);
    if (end == str) {
    	*num = -1;
        return -1;  // not a decimal number
    }
    else if (l > INT_MAX) {
        *num = -1;
        return -1;
    }
    else if (l < INT_MIN) {
        *num = -1;
        return -1;
    }
    *num = (int) l;
    return 0;
}

int convert_str_to_int_no_extra_char_check(const char* str, int* num) {
    char* end;
    const long l = strtol(str, &end, 10);
    if (end == str) {
    	*num = -1;
        return -1;  // not a decimal number
    }
     else if (*end != '\0' && *end != '\n' && *end != ' ' && *end != '\t') {
        *num = -1;
        return -1;  // there are some extra characters after number
    }
    else if (l > INT_MAX) {
        *num = -1;
        return -1;
    }
    else if (l < INT_MIN) {
        *num = -1;
        return -1;
    }
    *num = (int) l;
    return 0;
}

int get_command_from_str(const char* token) {
	if (strcmp(token, "help") == 0) return HELP;
	if (strcmp(token, "list") == 0) return LIST;
	if (strcmp(token, "readF") == 0) return READF;
	if (strcmp(token, "writeT") == 0) return WRITET;
	if (strcmp(token, "upload") == 0) return UPLOAD;
	if (strcmp(token, "download") == 0) return DOWNLOAD;
	if (strcmp(token, "archServer") == 0) return ARCHSERVER;
	if (strcmp(token, "killServer") == 0) return KILLSERVER;
	if (strcmp(token, "quit") == 0) return QUIT;
	return UNKNOWN;
}

int is_command_without_file(int command) {
	if (command == HELP || command == LIST || command == KILLSERVER || command == QUIT) return 1;
	return 0;
}

int is_command_without_line(int command) {
	if (command == UPLOAD || command == DOWNLOAD) return 1;
	return 0;
}

// flags: It must consist O_WRONLY or O_RDWR because it must be opened for writing.
int lock_file(const char* file_name, struct flock* lock, int flags) {
	int fd = open_file(file_name, flags, 0777);	
	if (fd == -1) {
		perror("Error in opening lock file");
		return -1;
	}
	
	// Get lock
	memset(lock, 0, sizeof(*lock));
	lock->l_type = F_WRLCK;
	int is_done = 0;
	while (!is_done) {
		int lock_res = fcntl(fd, F_SETLKW, lock);
		if (lock_res == -1) {
			if (!is_errno_eintr_or_eagain()) {
				perror("Error in getting lock");
				return -1;
			}
		}
		else is_done = 1;
	}
	return fd;
}

int unlock_file(int* fd_lock_file, struct flock* lock) {
	// Release lock
	lock->l_type = F_UNLCK;
	int is_done = 0;
	while (!is_done) {
		int lock_res = fcntl(*fd_lock_file, F_SETLKW, lock);
		if (lock_res == -1) {
			if (!is_errno_eintr_or_eagain()) {
				perror("Error in unlocking lock");
				return -1;
			}
		}
		else is_done = 1;
	}
	close_file_if_possible(fd_lock_file);
	return 0;
}

int parse_user_request(char* command, struct user_request_t* userrequest) {
	const char* del = " \t\n";
	
	// Save remaining str
	char copy_str[COMMAND_MAX_SIZE];
	strcpy(copy_str, command);
	copy_str[strlen(command)] = '\0';
	
	char* remaining_str = copy_str;
	
	// Get command
	char* token = strtok(command, del);
	if (token == NULL) return -1;
	userrequest->command = get_command_from_str(token);
	
	// Check if command is known
	if (userrequest->command == UNKNOWN) return -1;
	
	// Check if command does not require any file or something
	if (is_command_without_file(userrequest->command)) {
		if (userrequest->command == HELP) {
			token = strtok(NULL, del);
			if (token != NULL) {
				userrequest->help_command = get_command_from_str(token);		
				if (userrequest->help_command == UNKNOWN) return -1;
				token = strtok(NULL, del);
				if (token != NULL) return -1; // If something is still in the command, return -1
			}	
			else {
				userrequest->help_command = UNKNOWN;
			}
		}
	
		token = strtok(NULL, del);
		if (token != NULL) return -1; // If something is still in the command, return -1
		return 0;
	} 
	
	
	// Get file name
	token = strtok(NULL, del);
	if (token == NULL) return -1;
		
	// Check if file name length is at least 1 character
	int len = strlen(token);	
	if (len <= 0) return -1;
	
	// Check if command is ARCHSERVER
	if (userrequest->command == ARCHSERVER) {
		if (len < 5) return -1;
		
		strncpy(userrequest->file_name, token, len); 
		(userrequest->file_name)[len] = '\0';
		
		token = strtok(NULL, del);
		if (token != NULL) return -1; // If something is still in the command, return -1
		return 0;
	}
	
	strcpy(userrequest->file_name, token);
	(userrequest->file_name)[len] = '\0';
	
	if (is_command_without_line(userrequest->command)) {
		token = strtok(NULL, del);
		if (token != NULL) return -1; // If something is still in the command, return -1
		return 0;
	}
	
	
	// Get line number	
	token = strtok(NULL, " \t\n");
	
	if (userrequest->command == READF) {
		if (token == NULL) {
			userrequest->line = -1; // Indicate no line given
			return 0; // If line is not given, then it's okay
		}
		
		int convert_res = convert_str_to_int(token, &userrequest->line);
		if (convert_res == -1) return -1; // If something is there, then it should be a number indicating line
		if (userrequest->line <= 0) return -1; // Check if line is positive, otherwise return -1
		
		token = strtok(NULL, del);
		if (token != NULL) return -1; // If something is still in the command, return -1
		return 0;
	}
	
	// Only WRITET left here since this part
	if (token == NULL) return -1; // Because there should be at least string, even if there is no line given
	
	// If line is given, it's written into line part. If it's not given, then -1 is written.
	int convert_res = convert_str_to_int_no_extra_char_check(token, &userrequest->line);
		
	// If line is not given, then string is written into str part. If it's not given, then it'll be overridden below.
	strcpy(userrequest->str, token);
	(userrequest->str)[strlen(token)] = '\0';	
			
	// Get string
	token = strtok(NULL, "\n");
	
	// If line is not given
	if (token == NULL) {
		// Check if str len is at least 1
		if (strlen(userrequest->str) == 0) return -1;
		return 0;
	}
	
	// If line is given, then str is now read and line number is checked.
	if (convert_res == -1) {
		// Az once okunan tokenle birlikte geri kalan hepsi str'a ait
		strtok(remaining_str, del);
		strtok(NULL, del);
		
		token = strtok(NULL, "\n");
		strcpy(userrequest->str, token);
		
		// Check if str len is at least 1
		if (strlen(userrequest->str) == 0) return -1;
	}
	
	else {
		if (userrequest->line <= 0) return -1; // Line should be greater than 0.
	
		// Az once okunan line number'di. Bundan sonrakiler str'a ait
		strtok(remaining_str, del);
		strtok(NULL, del);
		strtok(NULL, del);
		
		token = strtok(NULL, "\n");
		strcpy(userrequest->str, token);
		
		// Check if str len is at least 1
		if (strlen(userrequest->str) == 0) return -1;
	}
	
	return 0;
}

int is_errno_eintr_or_eagain() {
	if (errno == EINTR || errno == EAGAIN) {
		return 1;
	}
	return 0;
}

void unlink_file_if_possible(int* fd, const char* file_path) {
	if (*fd != -1) {
		close(*fd);
		*fd = -1;
		unlink(file_path);
	}
}

void close_file_if_possible(int* fd) {
	if (*fd != -1) {
		close(*fd);
		*fd = -1;
	}
}

void send_sigterm_if_possible(int pid) {
	if (pid != -1) {
		kill(pid, SIGTERM);
	}
}

void sigpipe_handler(int unused) { }

int set_sigpipe_to_ignore() {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &sigpipe_handler;
	sa.sa_flags = 0;
	if (sigemptyset(&sa.sa_mask) == -1) {
		perror("Error in emptying the signal handler struct");
		return -1;
	}
	if (sigaction(SIGPIPE, &sa, NULL) == -1) {
		perror("Error in setting signal handler for SIGPIPE");
		return -1;
	}
	return 0;
}

void signal_and_wait_all_children(pid_t* child_pids, int child_counter) {
	int i;
	for (i = 0; i < child_counter; ++i) {
		send_sigterm_if_possible(child_pids[i]);
	}

	int is_done = 0;
	int status;
	while (!is_done) {
		int pid = waitpid(-1, &status, WUNTRACED | WNOHANG);
		if (pid < 0) {
			if (errno == ECHILD) {
				is_done = 1;
			}
			else if (errno != EINTR) {	// it can continue to wait if it is EINTR, otherwise it means wait failed for some reason
				perror("Error in waitpid");
				is_done = 1;
			}
		}
		sleep(0.01);
	}
}

int open_file(const char* file_path, int flags, int mode) {
	int is_done = 0;
	int fd = -1;
	while (!is_done) {
		fd = open(file_path, flags, mode);
		if (fd == -1 && is_errno_eintr_or_eagain()) is_done = 0;
		else if (fd == -1) {
			fd = -1;
			is_done = 1;
		}
		else is_done = 1;
	}
	return fd;	
}

int open_fifo(const char* fifo_path, int flags) {
	int is_done = 0;
	int fd = -1;
	while (!is_done) {
		fd = open(fifo_path, flags);
		if (fd == -1 && is_errno_eintr_or_eagain()) is_done = 0;
		else if (fd == -1) {
			fd = -1;
			is_done = 1;
		}
		else is_done = 1;
	}
	return fd;
}

int check_if_file_exists(const char* file_path) {
	// Check if there is such file
	int file_exists = -1;
	int fd = open_file(file_path, O_WRONLY, 0666);
	if (fd == -1) {
		if (errno == ENOENT) file_exists = 0;
	}
	else {
		close(fd);
		file_exists = 1;
	}
	return file_exists;
}

int read_fifo(int fifo_fd, void* buf, size_t size) {
	int is_read_done = 0;
	while (!is_read_done) {
		int read_bytes = read(fifo_fd, buf, size);
		if (read_bytes == -1) {
			if (!is_errno_eintr_or_eagain()) return -1;
		}
		else if (read_bytes == 0) return 0;
		else if (read_bytes > 0) return read_bytes;
	}
	return 0;
}

int read_fifo_retry_if_eof(int fifo_fd, void* buf, size_t size) {
	int is_read_done = 0;
	while (!is_read_done) {
		int read_bytes = read(fifo_fd, buf, size);
		if (read_bytes == -1) {
			if (!is_errno_eintr_or_eagain()) return -1;
		}
		else if (read_bytes == 0) {
			sleep(0.01);
		}
		else if (read_bytes > 0) return read_bytes;
	}
	return 0;
}

int write_file(int fifo_fd, const void* buf, size_t size) {
	if (size > 0) {
		int is_write_done = 0;
		while(!is_write_done) {
			int write_bytes = write(fifo_fd, buf, size);
			if (write_bytes == -1) {
				if (!is_errno_eintr_or_eagain()) return -1;
			}
			else return 0;
		}
	}
	return 0;
}

int write_fifo(int fifo_fd, const void* buf, size_t size) {
	if (size > 0) {
		int is_write_done = 0;
		while(!is_write_done) {
			int write_bytes = write(fifo_fd, buf, size);
			if (write_bytes == -1) {
				if (!is_errno_eintr_or_eagain()) return -1;
			}
			else return 0;
		}
	}
	return 0;
}

int create_metadata_and_sent_to_fifo(int len, int is_sent_compeletely, int fifo_fd) {
	struct metadata_t metadata;
	metadata.len = len;
	metadata.is_sent_completely = is_sent_compeletely;
	return write_fifo(fifo_fd, &metadata, sizeof(struct metadata_t));
}

int write_file_until_size(int fifo_fd, const char* buf, size_t size) {
	if (size > 0) {
		// Write until all data is written
		int total_bytes_written = 0;
		while(total_bytes_written != size) {
			int write_bytes = write(fifo_fd, (void*) &buf[total_bytes_written], size - total_bytes_written);
			if (write_bytes == -1) {
				if (!is_errno_eintr_or_eagain()) return -1;			
			} 
			else total_bytes_written += write_bytes;
		}
	}
	return 0;
}

int write_fifo_until_size(int fifo_fd, const char* buf, size_t size) {
	if (size > 0) {
		// Write until all data is written
		int total_bytes_written = 0;
		while(total_bytes_written != size) {
			int write_bytes = write(fifo_fd, (void*) &buf[total_bytes_written], size - total_bytes_written);
			if (write_bytes == -1) {
				if (!is_errno_eintr_or_eagain()) return -1;			
			} 
			else total_bytes_written += write_bytes;
		}
	}
	return 0;
}

int read_file_until_size(int fifo_fd, char* buf, size_t size) {
	int total_bytes_read = 0;
	while (total_bytes_read != size) {
		int read_bytes = read(fifo_fd, (void*) &buf[total_bytes_read], size - total_bytes_read);
		if (read_bytes == -1) {
			if (!is_errno_eintr_or_eagain()) return -1;
		}
		else if (read_bytes == 0) {
			return 0; // difference between fifo read
		}
		else total_bytes_read += read_bytes;
	}
	return total_bytes_read;
}

int read_fifo_until_size(int fifo_fd, char* buf, size_t size) {
	int total_bytes_read = 0;
	while (total_bytes_read != size) {
		int read_bytes = read(fifo_fd, (void*) &buf[total_bytes_read], size - total_bytes_read);
		if (read_bytes == -1) {
			if (!is_errno_eintr_or_eagain()) return -1;
		}
		else if (read_bytes == 0) {
			return -1;
		}
		else total_bytes_read += read_bytes;
	}
	return total_bytes_read;
}

int send_metadata_and_write_until_size(int fifo_fd, const char* buf, size_t size, int completed) {
	int res = create_metadata_and_sent_to_fifo(size, completed, fifo_fd);
	if (res == -1) return -1;
	
	if (size > 0) {
		res = write_fifo_until_size(fifo_fd, buf, size);
		if (res == -1) return -1;
	}
	return 0;
}

int read_metadata_and_read_until_size(int fifo_fd, char* buf) {
	struct metadata_t metadata;
	int read_res = read_fifo(fifo_fd, (void*) &metadata, sizeof(struct metadata_t));
	if (read_res == 0 || read_res == -1) return -1;
	
	read_res = read_fifo_until_size(fifo_fd, (void*) buf, metadata.len);
	if (read_res == 0 || read_res == -1) return -1;
	return 0;
}
