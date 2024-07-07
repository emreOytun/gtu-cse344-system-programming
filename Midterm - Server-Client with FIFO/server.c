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

#define WRITE_TEMP_FILE_NAME "write_temp.temp"
#define LIST_FILE_NAME "list_res.txt"

int con_fifo_fd = -1;
int con_fifo_write_fd = -1;
int client_req_fifo_fd = -1;
int client_res_fifo_fd = -1;

int lock_fd = -1;
int temp_file_fd = -1;
int list_res_fd = -1;

int parent_pid = -1;
int fork_pid = -1;
int fork_exec_pid = -1;

pid_t child_pids[BUF_INIT_SIZE];
int con_counter = 0;
int max_con_num = -1;

int client_idx = 1;

char con_fifo_path[FIFO_PATH_SIZE];
char client_req_fifo_path[FIFO_PATH_SIZE];
char client_res_fifo_path[FIFO_PATH_SIZE];

void init_child_pids_arr() {
	int i = 0;
	for (i = 0; i < BUF_INIT_SIZE; ++i) {
		child_pids[i] = -1;
	}
}

void unlink_fifos() {
	unlink_file_if_possible(&con_fifo_fd, con_fifo_path);
	unlink_file_if_possible(&con_fifo_write_fd, con_fifo_path);
	unlink_file_if_possible(&client_req_fifo_fd, client_req_fifo_path);
	unlink_file_if_possible(&client_res_fifo_fd, client_res_fifo_path);
}

void unlink_fifos_in_child_process() {
	unlink_file_if_possible(&client_req_fifo_fd, client_req_fifo_path);
	unlink_file_if_possible(&client_res_fifo_fd, client_res_fifo_path);
}

void close_fds() {
	close_file_if_possible(&con_fifo_fd);
	close_file_if_possible(&con_fifo_write_fd);
	close_file_if_possible(&client_req_fifo_fd);
	close_file_if_possible(&client_res_fifo_fd);
		
	close_file_if_possible(&lock_fd);
	close_file_if_possible(&temp_file_fd);
	close_file_if_possible(&list_res_fd);
}

void cleanup() {
	send_sigterm_if_possible(fork_exec_pid);

	signal_and_wait_all_children(child_pids, con_counter);
	unlink_fifos();
	close_fds();
	remove(LIST_FILE_NAME);
}

void cleanup_in_child_process() {
	int arr[1];
	arr[0] = fork_exec_pid;
	signal_and_wait_all_children(arr, 1);

	unlink_fifos_in_child_process();
	close_fds();
}

void close_client_fifos() {
	close_file_if_possible(&client_req_fifo_fd);
	close_file_if_possible(&client_res_fifo_fd);
}

void exit_child_process(int exit_status) {
	printf("client%d is disconnected.. \n", client_idx);
	_exit(exit_status);
}

void handle_sigterm_sigint(int signal) {
	if (fork_pid == 0) {
		cleanup_in_child_process();
		exit_child_process(EXIT_SUCCESS);		
	}
	else {
		// parent process
		cleanup();
	}
	_exit(EXIT_SUCCESS);
}

int set_sigterm_sigint_handler() {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &handle_sigterm_sigint;
	sa.sa_flags = 0;
	if (sigemptyset(&sa.sa_mask) == -1) {
		perror("Error in emptying the signal handler struct");
		return -1;
	}
	if (sigaction(SIGTERM, &sa, NULL) == -1) {
		perror("Error in setting signal handler for SIGTERM");
		return -1;
	}
	if (sigaction(SIGINT, &sa, NULL) == -1) {
		perror("Error in setting signal handler for SIGINT");
		return -1;
	}
	return 0;
}

void find_and_remove_child_pid(int pid) {
	// Search pid
	int i = 0;
	int is_found = 0;
	int pid_idx = -1;
	for (i = 0; i < con_counter && !is_found; ++i) {
		if (child_pids[i] == pid) {
			is_found = 1;
			pid_idx = i;
		}
	}

	if (is_found) {
		for (i = pid_idx + 1; i < con_counter; ++i) {
			child_pids[i - 1] = child_pids[i];
		}
		--con_counter;
		child_pids[con_counter] = -1; 
	}
}

void handle_sigchld(int signal) {
	// One SIGCHLD can come if several child processes terminate at the same time.
	// So, using waitpid with WNOHANG flag is required to handle all of them.

	// Also, SIGCHLD can come if a child process is stopped.
	// So, using waitpid with WUNTRACED flag is required.
	
	int status;
	int is_done = 0;
	while (!is_done) {
		int pid = waitpid(-1, &status, WUNTRACED | WNOHANG);
		if (pid < 0) {
			if (errno == ECHILD) {
				is_done = 1;
			}
			else if (errno != EINTR) {
				perror("Error in waitpid");
				is_done = 1;
			}
		}
		else if (pid == 0) {
			is_done = 1;
		}
		else {
			// If it is stopped it may be checked by WIFSTOPPED(status)
			if (WIFEXITED(status) || WIFSIGNALED(status)) {
				// A child has died
				find_and_remove_child_pid(pid);
			}
		}
	}	
}

int mask_sigchld(sigset_t* mask_signals_set) {
	if (sigemptyset(mask_signals_set) == -1) {
		perror("Error emptying signal set");
		return -1;
	}

	if (sigaddset(mask_signals_set, SIGCHLD) == -1) {
		perror("Error adding signal to signal set");
		return -1;	
	}
	
	if (sigprocmask(SIG_BLOCK, mask_signals_set, NULL) == -1) {
		perror("Error in blocking signals");
		return -1;
	}
	
	return 0;	
}

int unmask_signals(sigset_t* mask_signals_set) {
	if (sigprocmask(SIG_UNBLOCK, mask_signals_set, NULL) == -1) {
		perror("Error in unblocking signals");
		return -1;
	}
	return 0;	
}

int set_sigchld_handler() {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &handle_sigchld;
	sa.sa_flags = 0;
	if (sigemptyset(&sa.sa_mask) == -1) {
		perror("Error in emptying the signal handler struct");
		return -1;
	}
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("Error in setting signal handler for SIGCHLD");
		return -1;
	}
	return 0;
}

void read_user_input(int argc, char* argv[], char* dir_name) {
	if (argc != 4) {
		fprintf(stderr, "There must be exactly 3 arguments \n");
		exit(EXIT_FAILURE);
	}

	if (strcmp(argv[1], "neHosServer") != 0) {
		fprintf(stderr, "Error in user input server name. Program is closing... \n");
		exit(EXIT_FAILURE);
	}
   	
   	// Check the entered size of dir name
   	if (strlen(argv[2]) >= BUF_INIT_SIZE - 100) {
   		fprintf(stderr, "Error in user dir name input. It size should be max [%d] characters. Program is closing... \n", BUF_INIT_SIZE - 100);
   		exit(EXIT_FAILURE);
   	}
   	
   	convert_str_to_int(argv[3], &max_con_num);
   	if (max_con_num <= 0) {
   		fprintf(stderr, "Error in user input client number. Program is closing... \n");
   		exit(EXIT_FAILURE);
   	}
   	   	
   	// Create directory if it does not exist
   	strcat(dir_name, argv[2]);
   	if (mkdir(dir_name, ALL_PERMISSIONS) == -1) {
   		if (errno != EEXIST) {
	   		perror("Error in creating directory. Program is closing... ");
	   		exit(EXIT_FAILURE);
   		}
   	}
}

int send_fail_message_response(const char* response) {
	if (send_metadata_and_write_until_size(client_res_fifo_fd, response, strlen(response), COMPLETED) == -1) {
		perror(response);
		return -1;
	}
	return 0;
}

void write_log(const char* log_msg) {
	const char* log_file_name = "server_log.txt";
	
	struct flock lock;
	int log_lock_fd = lock_file(log_file_name, &lock, O_RDWR | O_CREAT | O_APPEND);
	if (log_lock_fd == -1) {
		perror("Problem in getting log file lock");
		return ;
	}
	
	if (write_file_until_size(log_lock_fd, log_msg, strlen(log_msg)) == -1) {
		perror("Problem in writing to log file");
		return ;
	}
	
	if (unlock_file(&log_lock_fd, &lock) == -1) {
		perror("Error in unlocking log file lock");
	}
} 

// Write command to response fifo, return 0 if it's successfully done. Otherwise, return -1 with printing the error message.
int handle_help(int help_command) {
	char* response = NULL;
	if (help_command == UNKNOWN || help_command == HELP) {
		response = "Available comments are: \nhelp, list, readF, writeT, upload, download, archServer, quit, killServer \n";
	}
	else if (help_command == LIST) {
		response = "list: sends a request to display the list of files in Servers directory (also displays the list received from the Server) \n";	
	}
	else if (help_command == READF) {
		response = "readF <file> <line #>: display the #th line of the <file>, returns with an error if <file> does not exists \n";	
	}
	else if (help_command == WRITET) {
		response = "writeT <file> <line #> <string>: request to write the content of “string” to the #th line the <file>, if the line # is not given writes to the end of file. If the file does not exists in Servers directory creates and edits the file at the same time \n";
	}
	else if (help_command == UPLOAD) {
		response = "upload <file>: uploads the file from the current working directory of client to the Servers directory (beware of the cases no file in clients current working directory and file with the same name on Servers side) \n";	
	}
	else if (help_command == DOWNLOAD) {
		response = "download <file>: request to receive <file> from Servers directory to client side \n";
	}
	else if (help_command == ARCHSERVER) {
		response = "archServer <fileName>.tar: Using fork, exec and tar utilities create a child process that will collect all the files currently available on the the Server side and store them in the <filename>.tar archive \n";
	}
	else if (help_command == KILLSERVER) {
		response = "killServer: Sends a kill request to the Server \n";
	}
	else if (help_command == QUIT) {
		response = "quit: Send write request to Server side log file and quits \n";
	}
	else {
		response = "Invalid command \n";
	}
	
	if (send_metadata_and_write_until_size(client_res_fifo_fd, response, strlen(response), COMPLETED) == -1) {
		perror("Error in writing server response fifo. Program is closing");
		return -1;
	}
	return 0;
}

int handle_list(char* dir_name) {
	char* list_res_file_name = LIST_FILE_NAME;
	char command[BUF_INIT_SIZE + 30];
	sprintf(command, "ls > %s %s", list_res_file_name, dir_name);
	
	// Execute ls command
	
	if (system(command) == -1) {
		const char* response = "Error in listing files in the directory \n";
		return send_fail_message_response(response);
	}
	
	list_res_fd = open(list_res_file_name, O_RDONLY);
	if (list_res_fd == -1) {
		const char* response = "Error reading files in the directory \n";
		return send_fail_message_response(response);
	}
	
	int is_done = 0;
	while (!is_done) {
		char ch;
		int read_bytes = read_file_until_size(list_res_fd, &ch, sizeof(char));	
		if (read_bytes == -1) {
			const char* response = "Error reading files in the directory \n";
			close(list_res_fd);
			remove(list_res_file_name);
			return send_fail_message_response(response);
		}
		else if (read_bytes == 0) {
			if (send_metadata_and_write_until_size(client_res_fifo_fd, &ch, 0, COMPLETED) == -1) {
				perror("Error in writing server response fifo. Connection lost. Program is closing");
				close(list_res_fd);
				remove(list_res_file_name);
				return -1;
			}
			is_done = 1;
		}
		else {
			if (send_metadata_and_write_until_size(client_res_fifo_fd, &ch, sizeof(char), NOT_COMPLETED_YET) == -1) {
				perror("Error in writing server response fifo. Program is closing");
				close(list_res_fd);
				remove(list_res_file_name);
				return -1;
			}
		}
	}
	
	close(list_res_fd);
	remove(list_res_file_name);
	
	return 0;
}

int handle_upload(char* dir_name, char* request_file_name) {
	struct flock lock;
	lock_fd = -1;
		
	char file_name[BUF_INIT_SIZE + COMMAND_MAX_SIZE];
	sprintf(file_name, "%s/%s", dir_name, request_file_name);

	// Check if file exists already
	int file_exists = check_if_file_exists(file_name);	
	if (file_exists == -1) {
		perror("Error in checking if file exists");	
		return -1;
	}
	else if (file_exists == 1) {
		int res = ERR_FILE_EXISTS;
		if (write_fifo(client_res_fifo_fd, (void*) &res, sizeof(int)) == -1) {
			perror("Error in writing server response fifo. Program is closing");
			return -1;
		}
		return 0;
	}
	else {
		int res = TRANSMISSION_STARTED;
		if (write_fifo(client_res_fifo_fd, (void*) &res, sizeof(int)) == -1) {
			perror("Error in writing server response fifo. Program is closing");
			return -1;
		}
	}
	
	// Create file and get lock
	lock_fd = lock_file(file_name, &lock, O_RDWR | O_CREAT | O_TRUNC);
	if (lock_fd == -1) {
		perror("Error in opening file for upload request");
		return -1;
	}

	// Read from fifo char-by-char and write to file
	char buf[5];
	int is_done = 0;
	struct metadata_t metadata;
	while (!is_done) {
		int read_res = read_fifo(client_req_fifo_fd, (void*) &metadata, sizeof(struct metadata_t));
		if (read_res == 0 || read_res == -1) {
			perror("Error in reading client fifo. Connection lost. Program is closing...");
			if (unlock_file(&lock_fd, &lock) == -1) {
				perror("Error in unlocking file lock \n");
			}
			remove(file_name);
			return -1;
		}
		
		int total_bytes_read_for_packet = 0;
		while (total_bytes_read_for_packet != metadata.len) {
			int read_bytes = read(client_req_fifo_fd, (void*) buf, sizeof(char));
			if (read_bytes == -1) {
				if (!is_errno_eintr_or_eagain()) {
					perror("Error in reading client request fifo");
					if (unlock_file(&lock_fd, &lock) == -1) {
						perror("Error in unlocking file lock \n");
						return -1;
					}
					remove(file_name);
					return -1;
				}
			}
			else if (read_bytes == 0) {
				perror("Error in reading client request fifo. Connection lost. Program is closing...");
				if (unlock_file(&lock_fd, &lock) == -1) {
					perror("Error in unlocking file lock \n");
					return -1;
				}
				remove(file_name);
				return -1;
			}
			else {
				if (write_file_until_size(lock_fd, buf, sizeof(char)) == -1) {
					perror("Error in writing to file in upload request");
					if (unlock_file(&lock_fd, &lock) == -1) {
						perror("Error in unlocking file lock \n");
						return -1;
					}			
					remove(file_name);
				}
				total_bytes_read_for_packet += read_bytes;		
			}
		}
		
		if (metadata.is_sent_completely) is_done = 1;
	}
	
	const char* response = "Given file is uploaded \n";
	if (send_metadata_and_write_until_size(client_res_fifo_fd, response, sizeof(char) * strlen(response), COMPLETED) == -1) {
		perror("Error in writing server response fifo. Program is closing");
		if (unlock_file(&lock_fd, &lock) == -1) {
			perror("Error in unlocking file lock \n");
			return -1;
		}
		return -1;
	}
	
	// Unlock file	
	if (unlock_file(&lock_fd, &lock) == -1) {
		perror("Error in unlocking file lock \n");
		return -1;
	}
	
	return 0;
}

int handle_download(char* dir_name, char* request_file_name, int is_for_arch) {
	struct flock lock;
	lock_fd = -1;
		
	char file_name[BUF_INIT_SIZE + COMMAND_MAX_SIZE];
	sprintf(file_name	, "%s/%s", dir_name, request_file_name);
	
	// Check if file exists already
	int file_exists = check_if_file_exists(file_name);	
	if (file_exists == -1) {
		perror("Error in checking if file exists");	
		return -1;
	}
	else if (file_exists == 0) {
		int res = ERR_FILE_NOT_EXISTS;
		if (write_fifo(client_res_fifo_fd, (void*) &res, sizeof(int)) == -1) {
			perror("Error in writing server response fifo. Connection lost. Program is closing");
			return -1;
		}
		return 0;
	}
	else {
		int res = TRANSMISSION_STARTED;
		if (write_fifo(client_res_fifo_fd, (void*) &res, sizeof(int)) == -1) {
			perror("Error in writing server response fifo. Program is closing");
			return -1;
		}
	}
	
	// Create file and get lock
	lock_fd = lock_file(file_name, &lock, O_RDWR);
	if (lock_fd == -1) {
		perror("Error in opening file for upload request");
		return -1;
	}

	// Read file and write to fifo char-by-char
	int is_done = 0;
	while (!is_done) {
		char ch;
		int read_bytes = read_file_until_size(lock_fd, &ch, sizeof(char));	
		if (read_bytes == -1) {
			perror("Error in reading server file for downloading");
			if (unlock_file(&lock_fd, &lock) == -1) {
				perror("Error in unlocking file lock \n");
			}
			return -1;
		}
		else if (read_bytes == 0) {
			if (send_metadata_and_write_until_size(client_res_fifo_fd, &ch, 0, COMPLETED) == -1) {
				perror("Error in writing to server fifo. Connection lost. Program is closing...");
				if (unlock_file(&lock_fd, &lock) == -1) {
					perror("Error in unlocking file lock \n");
					return -1;
				}
				return -1;
			}
			is_done = 1;
		}
		else {
			if (send_metadata_and_write_until_size(client_res_fifo_fd, &ch, sizeof(char), NOT_COMPLETED_YET) == -1) {
				perror("Error in writing to server fifo");
				if (unlock_file(&lock_fd, &lock) == -1) {
					perror("Error in unlocking file lock \n");
					return -1;
				}
				return -1;
			}
		}
	}
	
	if (!is_for_arch) {
		const char* response = "Given file is downloaded \n";
		if (send_metadata_and_write_until_size(client_res_fifo_fd, response, sizeof(char) * strlen(response), COMPLETED) == -1) {
			perror("Error in writing server response fifo. Program is closing");
			if (unlock_file(&lock_fd, &lock) == -1) {
				perror("Error in unlocking file lock \n");
				return -1;
			}
			return -1;
		}
	}
	
	// Unlock file	
	if (unlock_file(&lock_fd, &lock) == -1) {
		perror("Error in unlocking file lock \n");
		return -1;
	}
	
	return 0;
}

int handle_archserver(char* dir_name) {
	const char* list_res_file_name = LIST_FILE_NAME;
	char command[BUF_INIT_SIZE + 30];
	sprintf(command, "ls > %s %s", list_res_file_name, dir_name);
	
	if (system(command) == -1) {
		const char* response = "Error in getting file names in the directory \n";
		return send_fail_message_response(response);		
	}
	
	list_res_fd = open(list_res_file_name, O_RDONLY);
	if (list_res_fd == -1) {
		const char* response = "Error reading files in the directory \n";
		return send_fail_message_response(response);
	}
	
	char file_name[COMMAND_MAX_SIZE];
	int is_done = 0;
	int chars_read_in_line = 0;
	while (!is_done) {
		char ch;
		int read_bytes = read_file_until_size(list_res_fd, &ch, sizeof(char));	
		if (read_bytes == -1) {
			const char* response = "Error reading files in the directory \n";
			close(list_res_fd);
			remove(list_res_file_name);
			return send_fail_message_response(response);
		}
		else if (read_bytes == 0) {
			if (chars_read_in_line != 0) {
				// there is a file name in result
				file_name[chars_read_in_line] = '\0';
				++chars_read_in_line; // means size for file name
			
				if (send_metadata_and_write_until_size(client_res_fifo_fd, file_name, chars_read_in_line, NOT_COMPLETED_YET) == -1) {
					perror("Error in writing server response fifo. Program is closing");
					remove(list_res_file_name);
					close(list_res_fd);
					return -1;
				}
				
				if (handle_download(dir_name, file_name, 1) == -1) {
					remove(list_res_file_name);
					close(list_res_fd);
					return -1;
				}
			}	
			
			if (send_metadata_and_write_until_size(client_res_fifo_fd, &ch, 0, COMPLETED) == -1) {
				perror("Error in writing server response fifo. Program is closing");
				remove(list_res_file_name);
				close(list_res_fd);
				return -1;
			}
			
			is_done = 1;
		}
		else {
			if (ch == '\n') {
				file_name[chars_read_in_line] = '\0';
				++chars_read_in_line;
				
				if (send_metadata_and_write_until_size(client_res_fifo_fd, file_name, chars_read_in_line, NOT_COMPLETED_YET) == -1) {
					perror("Error in writing server response fifo. Program is closing");
					remove(list_res_file_name);
					close(list_res_fd);
					return -1;
				}
				
				if (handle_download(dir_name, file_name, 1) == -1) {
					remove(list_res_file_name);
					close(list_res_fd);
					return -1;
				}

				chars_read_in_line = 0;
			}
			else {
				file_name[chars_read_in_line] = ch;
				++chars_read_in_line;
			}
		}
	}
	
	remove(list_res_file_name);
	close(list_res_fd);
	
	return 0;
}

int handle_readF(char* dir_name, struct user_request_t* userrequest) {
	struct flock lock;
	lock_fd = -1;
	
	char file_name[BUF_INIT_SIZE + COMMAND_MAX_SIZE];
	sprintf(file_name	, "%s/%s", dir_name, userrequest->file_name);
	
	// Open the original file in appending mode
	lock_fd = lock_file(file_name, &lock, O_RDWR);
	if (lock_fd == -1) {
		char* response = "Error opening file or getting lock \n";
		if (errno == ENOENT) {
			response = "There is no such file \n";
		}
		return send_fail_message_response(response);
	}

	// If line is not given
	if (userrequest->line == -1) {		
		int is_done = 0;
		while (!is_done) {
			char ch;
			int read_bytes = read_file_until_size(lock_fd, &ch, sizeof(char));	
			if (read_bytes == -1) {
				const char* response = "Error reading files in the directory \n";
				if (unlock_file(&lock_fd, &lock) == -1) {
					perror("Error in unlocking file lock \n");
					return -1;
				}
				return send_fail_message_response(response);
			}
			else if (read_bytes == 0) {
				char ch = '\n';
				if (send_metadata_and_write_until_size(client_res_fifo_fd, &ch, sizeof(char), COMPLETED) == -1) {
					perror("Error in writing server response fifo. Connection lost.. Program is closing");
					if (unlock_file(&lock_fd, &lock) == -1) {
						perror("Error in unlocking file lock \n");
						return -1;
					}
					return -1;
				}
				is_done = 1;
			}
			else {
				if (send_metadata_and_write_until_size(client_res_fifo_fd, &ch, sizeof(char), NOT_COMPLETED_YET) == -1) {
					perror("Error in writing server response fifo. Program is closing");
					if (unlock_file(&lock_fd, &lock) == -1) {
						perror("Error in unlocking file lock \n");
						return -1;
					}
					return -1;
				}
			}
		}
	}
	
	// If line is given
	else {
		int processed_line = 0;
		int is_done = 0;
		while (!is_done && processed_line < userrequest->line - 1) {
			int is_line_processing_done = 0;
			while (!is_line_processing_done) {
				char ch;
				int read_bytes = read_file_until_size(lock_fd, &ch, sizeof(char)); 
				if (read_bytes == -1) {
					const char* response = "Error reading from file \n";
					if (unlock_file(&lock_fd, &lock) == -1) {
						perror("Error in unlocking file lock \n");
						return -1;
					}
					return send_fail_message_response(response);
				}
				else if (read_bytes == 0) {
					is_line_processing_done = 1;
					is_done = 1;
				}
				else {
					if (ch == '\n') {
						++processed_line;
						is_line_processing_done = 1; // If it's \n, then line is processed
					}
				}
			}
		}
	
		if (processed_line != userrequest->line - 1) {
			const char* response = "Given line is not found in the file \n";
			if (send_metadata_and_write_until_size(client_res_fifo_fd, response, strlen(response), COMPLETED) == -1) {
				perror("Error in writing server response fifo. Program is closing");
				if (unlock_file(&lock_fd, &lock) == -1) {
					perror("Error in unlocking file lock \n");
					return -1;
				}
				return -1;
			}
		}
		else {
			// Read the line and print into fifo
			is_done = 0;
			while (!is_done) {
				char ch;
				int read_bytes = read_file_until_size(lock_fd, &ch, sizeof(char));	
				if (read_bytes == -1) {
					const char* response = "Error reading files in the directory \n";
					if (unlock_file(&lock_fd, &lock) == -1) {
						perror("Error in unlocking file lock \n");
						return -1;
					}
					return send_fail_message_response(response);
				}
				else if (read_bytes == 0) {
					// Put '\n' to temp file after writing the user given string
					char ch = '\n';
					if (send_metadata_and_write_until_size(client_res_fifo_fd, &ch, sizeof(char), COMPLETED) == -1) {
						perror("Error in writing server response fifo. Connection lost. Program is closing");
						if (unlock_file(&lock_fd, &lock) == -1) {
							perror("Error in unlocking file lock \n");
							return -1;
						}
						return -1;
					}
					
					is_done = 1;
				}
				else {
					if (ch != '\n') {
						if (send_metadata_and_write_until_size(client_res_fifo_fd, &ch, sizeof(char), NOT_COMPLETED_YET) == -1) {
							perror("Error in writing server response fifo. Program is closing");
							if (unlock_file(&lock_fd, &lock) == -1) {
								perror("Error in unlocking file lock \n");
								return -1;
							}
							return -1;
						}
					}
					else {
						// Put '\n' to temp file after writing the user given string
						char ch = '\n';
						if (send_metadata_and_write_until_size(client_res_fifo_fd, &ch, sizeof(char), COMPLETED) == -1) {
							perror("Error in writing server response fifo. Program is closing");
							if (unlock_file(&lock_fd, &lock) == -1) {
								perror("Error in unlocking file lock \n");
								return -1;
							}
							return -1;
						}

						is_done = 1;
					}
				}
			}
		}
	}
	
	if (unlock_file(&lock_fd, &lock) == -1) {
		perror("Error in unlocking file lock \n");
		return -1;
	}
	
	return 0;
}

int handle_writeT(char* dir_name, struct user_request_t* userrequest) {
	struct flock lock;
	lock_fd = -1;
	
	char file_name[BUF_INIT_SIZE + COMMAND_MAX_SIZE];
	sprintf(file_name	, "%s/%s", dir_name, userrequest->file_name);
	
	// If line is not given
	if (userrequest->line == -1) {
		// Open the original file in appending mode
		lock_fd = lock_file(file_name, &lock, O_RDWR | O_CREAT | O_APPEND);
		if (lock_fd == -1) {
			const char* response = "Error opening file or getting lock \n";
			return send_fail_message_response(response);
		}
	
		// Append the given string to the opened file
		int i;
		for (i = 0; i < strlen(userrequest->str); ++i) {
			char ch = (userrequest->str)[i];
			if (write_file_until_size(lock_fd, &ch, sizeof(char)) == -1) {
				const char* response = "Error in writing to file \n";
				if (unlock_file(&lock_fd, &lock) == -1) {
					perror("Error in unlocking file lock \n");
					return -1;
				}
				return send_fail_message_response(response);
			}
		}
		
		const char* response = "Given string is written to file \n";
		if (send_metadata_and_write_until_size(client_res_fifo_fd, response, sizeof(char) * strlen(response), COMPLETED) == -1) {
			perror("Error in writing server response fifo. Program is closing");
			if (unlock_file(&lock_fd, &lock) == -1) {
				perror("Error in unlocking file lock \n");
				return -1;
			}
			return -1;
		}
		
		if (unlock_file(&lock_fd, &lock) == -1) {
			perror("Error in unlocking file lock \n");
			return -1;
		}
	}
	
	// If line is given
	else {
		// Open the original file
		lock_fd = lock_file(file_name, &lock, O_RDWR | O_CREAT);
		if (lock_fd == -1) {
			const char* response = "Error opening file or getting lock \n";
			return send_fail_message_response(response);
		}
		
		// Construct temp file path, and create & lock it.
		char write_temp_file[COMMAND_MAX_SIZE + BUF_INIT_SIZE + 50];
		sprintf(write_temp_file, "%s_temp", file_name);
		
		// Open write_temp_file and lock
		struct flock temp_lock;
		temp_file_fd = lock_file(write_temp_file, &temp_lock, O_RDWR | O_CREAT | O_TRUNC);
		if (temp_file_fd == -1) {
			const char* response = "Error opening tmp file \n";
			if (unlock_file(&lock_fd, &lock) == -1) {
				perror("Error in unlocking file lock \n");
				return -1;
			}
			return send_fail_message_response(response);
		}
		
		// Write lines until the given line to temp file.
		int processed_line = 0;
		for (processed_line = 0; processed_line < userrequest->line - 1; ++processed_line) {
			int is_line_processing_done = 0;
			while (!is_line_processing_done) {
				char ch;
				int read_bytes = read_file_until_size(lock_fd, &ch, sizeof(char)); 
				if (read_bytes == -1) {
					const char* response = "Error in reading from file \n";
					if (unlock_file(&temp_file_fd, &lock) == -1) {
						perror("Error in unlocking file lock \n");
						return -1;
					}
					if (unlock_file(&lock_fd, &lock) == -1) {
						perror("Error in unlocking file lock \n");
						return -1;
					}
					remove(write_temp_file);
					return send_fail_message_response(response);
				}
				else if (read_bytes == 0) {
					// Write \n, does not matter if it's the last line or no such line. Put enter to go next line
					ch = '\n';
					if (write_file_until_size(temp_file_fd, &ch, sizeof(char)) == -1) {
						const char* response = "Error in writing to temp file \n";
						if (unlock_file(&temp_file_fd, &lock) == -1) {
							perror("Error in unlocking file lock \n");
							return -1;
						}
						if (unlock_file(&lock_fd, &lock) == -1) {
							perror("Error in unlocking file lock \n");
							return -1;
						}
						remove(write_temp_file);
						return send_fail_message_response(response);
					}			
					is_line_processing_done = 1;
				}
				else {
					if (write_file_until_size(temp_file_fd, &ch, sizeof(char)) == -1) {
						const char* response = "Error in writing to temp file \n";
						if (unlock_file(&temp_file_fd, &lock) == -1) {
							perror("Error in unlocking file lock \n");
							return -1;
						}
						if (unlock_file(&lock_fd, &lock) == -1) {
							perror("Error in unlocking file lock \n");
							return -1;
						}
						remove(write_temp_file);
						return send_fail_message_response(response);
					}
					if (ch == '\n') is_line_processing_done = 1; // If it's \n, then line is processed
				}
			}
		}
		
		// Append the given string to the opened file
		int i;
		for (i = 0; i < strlen(userrequest->str); ++i) {
			char ch = (userrequest->str)[i];
			if (write_file_until_size(temp_file_fd, &ch, sizeof(char)) == -1) {
				const char* response = "Error in writing to file \n";
				if (unlock_file(&temp_file_fd, &lock) == -1) {
					perror("Error in unlocking file lock \n");
					return -1;
				}
				if (unlock_file(&lock_fd, &lock) == -1) {
					perror("Error in unlocking file lock \n");
					return -1;
				}
				remove(write_temp_file);
				return send_fail_message_response(response);
			}
		}
		
		// Put '\n' to temp file after writing the user given string
		char ch = '\n';
		if (write_file_until_size(temp_file_fd, &ch, sizeof(char)) == -1) {
			const char* response = "Error in writing to file \n";
			if (unlock_file(&temp_file_fd, &lock) == -1) {
				perror("Error in unlocking file lock \n");
				return -1;
			}
			if (unlock_file(&lock_fd, &lock) == -1) {
				perror("Error in unlocking file lock \n");
				return -1;
			}
			remove(write_temp_file);
			return send_fail_message_response(response);
		}
		
		// Write the remaining part to temp file
		int first_write_after_string_insert = 1;
		int is_done = 0;
		while (!is_done) {
			char ch;
			int read_bytes = read_file_until_size(lock_fd, &ch, sizeof(char)); 
			if (read_bytes == -1) {
				const char* response = "Error in reading from file \n";
				if (unlock_file(&temp_file_fd, &lock) == -1) {
					perror("Error in unlocking file lock \n");
					return -1;
				}
				if (unlock_file(&lock_fd, &lock) == -1) {
					perror("Error in unlocking file lock \n");
					return -1;
				}
				remove(write_temp_file);
				return send_fail_message_response(response);
			}
			else if (read_bytes == 0) is_done = 1;
			else {
				if (!(first_write_after_string_insert && ch == '\n')) {
					if (write_file_until_size(temp_file_fd, &ch, sizeof(char)) == -1) {
						const char* response = "Error in writing to temp file \n";
						if (unlock_file(&temp_file_fd, &lock) == -1) {
							perror("Error in unlocking file lock \n");
							return -1;
						}
						if (unlock_file(&lock_fd, &lock) == -1) {
							perror("Error in unlocking file lock \n");
							return -1;
						}
						remove(write_temp_file);
						return send_fail_message_response(response);
					}
				}
				
				first_write_after_string_insert = 0;
			}
		}
		
		if (lseek(temp_file_fd, 0, SEEK_SET) == -1 || lseek(lock_fd, 0, SEEK_SET) == -1) {
			const char* response = "Error in lseek for file \n";
			if (unlock_file(&temp_file_fd, &lock) == -1) {
				perror("Error in unlocking file lock \n");
				return -1;
			}
			if (unlock_file(&lock_fd, &lock) == -1) {
				perror("Error in unlocking file lock \n");
				return -1;
			}
			remove(write_temp_file);
			return send_fail_message_response(response);
		}
		
		is_done = 0;
		while (!is_done) {
			char ch;
			int read_bytes = read_file_until_size(temp_file_fd, &ch, sizeof(char)); 
			if (read_bytes == -1) {
				const char* response = "Error in reading from temp file \n";
				if (unlock_file(&temp_file_fd, &lock) == -1) {
					perror("Error in unlocking file lock \n");
					return -1;
				}
				if (unlock_file(&lock_fd, &lock) == -1) {
					perror("Error in unlocking file lock \n");
					return -1;
				}
				remove(write_temp_file);
				return send_fail_message_response(response);
			}
			else if (read_bytes == 0) is_done = 1;
			else {
				if (write_file_until_size(lock_fd, &ch, sizeof(char)) == -1) {
					const char* response = "Error in writing to file \n";
					if (unlock_file(&temp_file_fd, &lock) == -1) {
						perror("Error in unlocking file lock \n");
						return -1;
					}
					if (unlock_file(&lock_fd, &lock) == -1) {
						perror("Error in unlocking file lock \n");
						return -1;
					}
					remove(write_temp_file);
					return send_fail_message_response(response);
				}	
			}
		}
		
		if (remove(write_temp_file) == -1) {
			perror("Error in removing temp file \n");
			return -1;
		}
		if (unlock_file(&temp_file_fd, &lock) == -1) {
			perror("Error in unlocking temp file lock \n");
			return -1;
		}
		
		if (unlock_file(&lock_fd, &lock) == -1) {
			perror("Error in unlocking file lock \n");
			return -1;
		}
			
		const char* response = "Given string is written to file \n";
		if (send_metadata_and_write_until_size(client_res_fifo_fd, response, sizeof(char) * strlen(response), COMPLETED) == -1) {
			perror("Error in writing server response fifo. Program is closing");
			return -1;
		}
	}
	
	return 0;
} 

int handle_quit(int client_pid) {
	const char* response = "Sending write request to server log file \nWaiting for logfile... \n";
	if (send_metadata_and_write_until_size(client_res_fifo_fd, response, sizeof(char) * strlen(response), NOT_COMPLETED_YET) == -1) {
		perror("Error in writing server response fifo. Program is closing");
		return -1;
	}
	
	char log_msg[1000];
	sprintf(log_msg, "Client PID %d as client%d sent quit request. Worker Server Process is closing \n", client_pid, client_idx);
	write_log(log_msg);
	
	const char* response2 = "Logfile write request granted \n";
	if (send_metadata_and_write_until_size(client_res_fifo_fd, response2, sizeof(char) * strlen(response2), COMPLETED) == -1) {
		perror("Error in writing server response fifo. Program is closing");
		return -1;
	}
	
	// Wait for client ack
	int client_ack = -1;
	read_fifo(client_req_fifo_fd, (void*) &client_ack, sizeof(int));
	
	cleanup_in_child_process();
	exit_child_process(EXIT_SUCCESS);	
	
	return 0;
}

int handle_killserver(int client_pid) {
	const char* response = "Sending write request to server log file \nWaiting for logfile... \n";
	if (send_metadata_and_write_until_size(client_res_fifo_fd, response, sizeof(char) * strlen(response), NOT_COMPLETED_YET) == -1) {
		perror("Error in writing server response fifo. Program is closing");
		return -1;
	}
	
	char log_msg[1000];
	sprintf(log_msg, "Client PID %d as client%d sent kill request. Server is closing \n", client_pid, client_idx);
	write_log(log_msg);
	
	const char* response2 = "Logfile write request granted \n";
	if (send_metadata_and_write_until_size(client_res_fifo_fd, response2, sizeof(char) * strlen(response2), COMPLETED) == -1) {
		perror("Error in writing server response fifo. Program is closing");
		return -1;
	}
	
	// Wait for client ack
	int client_ack = -1;
	read_fifo(client_req_fifo_fd, (void*) &client_ack, sizeof(int));
	
	kill(parent_pid, SIGINT);
	cleanup_in_child_process();
	exit_child_process(EXIT_SUCCESS);	
	
	return 0;
}	


void worker_process(char* dir_name, int client_pid) {
	char log_msg[1000];
	sprintf(log_msg, "Client PID %d connected as 'client%d' \n", client_pid, client_idx);
	write_log(log_msg);
	printf("Client PID %d connected as 'client%d' \n", client_pid, client_idx);

	int is_done = 0;
	while (!is_done) {	
		// Read metadata
		char command[COMMAND_MAX_SIZE];
		if (read_metadata_and_read_until_size(client_req_fifo_fd, command) == -1) {
			fprintf(stderr, "Error in reading client request. Connection is lost. Program is closing... \n");
			cleanup_in_child_process();
			exit_child_process(EXIT_FAILURE);
		}
		
		struct user_request_t userrequest;
		if (parse_user_request(command, &userrequest) == -1) {
			const char* msg = "Invalid command. Enter 'help' to see the command \n";
			if (send_metadata_and_write_until_size(client_res_fifo_fd, msg, sizeof(char) * strlen(msg), COMPLETED) == -1) {
				fprintf(stderr, "Error in writing response to client. Connection is lost. Program is closing... \n");
				cleanup_in_child_process();
				exit_child_process(EXIT_FAILURE);
			}
		}	
		else {
			// Valid command
			if (userrequest.command == HELP) {
				char log_msg[1000];
				sprintf(log_msg, "Client PID %d as client%d request HELP command \n", client_pid, client_idx);
				write_log(log_msg);
				
				if (handle_help(userrequest.help_command) == -1) {
					sprintf(log_msg, "ERROR in client PID %d as client%d request processing \n", client_pid, client_idx);
					write_log(log_msg); 
					
					cleanup_in_child_process();
					exit_child_process(EXIT_FAILURE);	
				}
				
				sprintf(log_msg, "Client PID %d as client%d request HELP processed successfully \n", client_pid, client_idx);
				write_log(log_msg); 
			}
			else if (userrequest.command == LIST) {
				char log_msg[1000];
				sprintf(log_msg, "Client PID %d as client%d request LIST command \n", client_pid, client_idx);
				write_log(log_msg);
				
				if (handle_list(dir_name) == -1) {
					sprintf(log_msg, "ERROR in client PID %d as client%d request processing \n", client_pid, client_idx);
					write_log(log_msg);
				
					cleanup_in_child_process();
					exit_child_process(EXIT_FAILURE);	
				}
				
				sprintf(log_msg, "Client PID %d as client%d request LIST processed successfully \n", client_pid, client_idx);
				write_log(log_msg);
			}
			else if (userrequest.command == WRITET) {
				char log_msg[1000];
				sprintf(log_msg, "Client PID %d as client%d request WRITET command \n", client_pid, client_idx);
				write_log(log_msg);
				
				if (handle_writeT(dir_name, &userrequest) == -1) {
					sprintf(log_msg, "ERROR in client PID %d as client%d request processing \n", client_pid, client_idx);
					write_log(log_msg);
				
					cleanup_in_child_process();
					exit_child_process(EXIT_FAILURE);	
				}
				
				sprintf(log_msg, "Client PID %d as client%d request WRITET processed successfully \n", client_pid, client_idx);
				write_log(log_msg);
			}
			else if (userrequest.command == READF) {
				char log_msg[1000];
				sprintf(log_msg, "Client PID %d as client%d request READF command \n", client_pid, client_idx);
				write_log(log_msg);
				
				if (handle_readF(dir_name, &userrequest) == -1) {
					sprintf(log_msg, "ERROR in client PID %d as client%d request processing \n", client_pid, client_idx);
					write_log(log_msg);
				
					cleanup_in_child_process();
					exit_child_process(EXIT_FAILURE);	
				}
				
				sprintf(log_msg, "Client PID %d as client%d request READF processed successfully \n", client_pid, client_idx);
				write_log(log_msg);
			}
			else if (userrequest.command == UPLOAD) {
				char log_msg[1000];
				sprintf(log_msg, "Client PID %d as client%d request UPLOAD command \n", client_pid, client_idx);
				write_log(log_msg);
				
				if (handle_upload(dir_name, userrequest.file_name) == -1) {
					sprintf(log_msg, "ERROR in client PID %d as client%d request processing \n", client_pid, client_idx);
					write_log(log_msg);
				
					cleanup_in_child_process();
					exit_child_process(EXIT_FAILURE);	
				}
				
				sprintf(log_msg, "Client PID %d as client%d request UPLOAD processed successfully \n", client_pid, client_idx);
				write_log(log_msg);
			}
			else if (userrequest.command == DOWNLOAD) {
				char log_msg[1000];
				sprintf(log_msg, "Client PID %d as client%d request DOWNLOAD command \n", client_pid, client_idx);
				write_log(log_msg);
				
				if (handle_download(dir_name, userrequest.file_name, 0) == -1) {
					sprintf(log_msg, "ERROR in client PID %d as client%d request processing \n", client_pid, client_idx);
					write_log(log_msg);
				
					cleanup_in_child_process();
					exit_child_process(EXIT_FAILURE);	
				}
				
				sprintf(log_msg, "Client PID %d as client%d request DOWNLOAD processed successfully \n", client_pid, client_idx);
				write_log(log_msg);
			}
			else if (userrequest.command == ARCHSERVER) {
				char log_msg[1000];
				sprintf(log_msg, "Client PID %d as client%d request ARCHSERVER command \n", client_pid, client_idx);
				write_log(log_msg);
				
				if (handle_archserver(dir_name) == -1) {
					sprintf(log_msg, "ERROR in client PID %d as client%d request processing \n", client_pid, client_idx);
					write_log(log_msg);
				
					cleanup_in_child_process();
					exit_child_process(EXIT_FAILURE);	
				}
				
				sprintf(log_msg, "Client PID %d as client%d request ARCHSERVER processed successfully \n", client_pid, client_idx);
				write_log(log_msg);
			}
			else if (userrequest.command == QUIT) {
				char log_msg[1000];
				sprintf(log_msg, "Client PID %d as client%d request QUIT command \n", client_pid, client_idx);
				write_log(log_msg);
				
				if (handle_quit(client_pid) == -1) {
					sprintf(log_msg, "ERROR in client PID %d as client%d request processing \n", client_pid, client_idx);
					write_log(log_msg);
				
					cleanup_in_child_process();
					exit_child_process(EXIT_FAILURE);	
				}
				
				sprintf(log_msg, "Client PID %d as client%d request QUIT processed successfully \n", client_pid, client_idx);
				write_log(log_msg);
			}
			else if (userrequest.command == KILLSERVER) {
				char log_msg[1000];
				sprintf(log_msg, "Client PID %d as client%d request KILLSERVER command \n", client_pid, client_idx);
				write_log(log_msg);
				
				if (handle_killserver(client_pid) == -1) {
					sprintf(log_msg, "ERROR in client PID %d as client%d request processing \n", client_pid, client_idx);
					write_log(log_msg);
				
					cleanup_in_child_process();
					exit_child_process(EXIT_FAILURE);	
				}
				
				sprintf(log_msg, "Client PID %d as client%d request KILLSERVER processed successfully \n", client_pid, client_idx);
				write_log(log_msg);
			}
		}
	}
	
	cleanup_in_child_process();
	exit_child_process(EXIT_SUCCESS);
}



int main(int argc, char* argv[]) {
	parent_pid = getpid();
	
	printf("Server started with PID: %d \n", getpid());
	fflush(stdout);

	// Set signal handlers
	if (set_sigpipe_to_ignore() == -1) exit(EXIT_FAILURE);
	
	if (set_sigterm_sigint_handler() == -1) {
		perror("Error in setting signal handlers. Program is closing...");
		exit(EXIT_FAILURE);
	}
	if (set_sigchld_handler() == -1) {
		perror("Error in setting signal handlers. Program is closing...");
		exit(EXIT_FAILURE);
	}
	
	// Get user input for server creation
	int is_done = 0;
	char dir_name[BUF_INIT_SIZE] = "/tmp/";
 	read_user_input(argc, argv, dir_name);
 	
	// Convert pid to str
	char pid_str[PID_STR_SIZE];
	int pid = getpid();
	sprintf(pid_str, "%d", pid);
	
	// Init con_fifo_path
	strcpy(con_fifo_path, SERVER_BASE_PATH);
	strcat(con_fifo_path, pid_str);

	// Create fifo
	if (mkfifo(con_fifo_path, ALL_PERMISSIONS) == -1) {
		if (errno != EEXIST) {
			perror("Error in creating server fifo. Program is closing...");
			exit(EXIT_FAILURE);
		}
	}
	
	init_child_pids_arr();

	con_fifo_fd = open_fifo(con_fifo_path, O_RDONLY);
	if (con_fifo_fd == -1) {
		perror("Error in opening connection fifo. Program is closing...");
		cleanup();
		exit(EXIT_FAILURE);
	}
	
	con_fifo_write_fd = open_fifo(con_fifo_path, O_WRONLY);
	if (con_fifo_write_fd == -1) {
		perror("Error in opening connection fifo. Program is closing...");
		cleanup();
		exit(EXIT_FAILURE);
	}
	
	is_done = 0;
	sigset_t mask_signals_set;
	memset(&mask_signals_set, 0, sizeof(sigset_t));
	
	
	struct connect_request_t connect_request;
	memset(&connect_request, 0, sizeof(struct connect_request_t));
	
	while (!is_done) {	
		
		// Get child pid
		int read_res = read_fifo_retry_if_eof(con_fifo_fd, (void*) &connect_request, sizeof(struct connect_request_t));
		if (read_res == -1) {
			perror("Error in opening connection fifo. Program is closing...");
			cleanup();
			exit(EXIT_FAILURE);
		}
		
		// Open child fifo		
		sprintf(pid_str, "%d", connect_request.client_pid);
		strcpy(client_res_fifo_path, CLIENT_RES_BASE_PATH);
		strcat(client_res_fifo_path, pid_str);
		
		client_res_fifo_fd = open_fifo(client_res_fifo_path, O_WRONLY);
		if (client_res_fifo_fd == -1) {
			perror("Error in opening client response fifo. Client is discarding...");	
			continue;
		}
		
		strcpy(client_req_fifo_path, CLIENT_REQ_BASE_PATH);
		strcat(client_req_fifo_path, pid_str);
		
		client_req_fifo_fd = open_fifo(client_req_fifo_path, O_RDONLY);
		if (client_req_fifo_fd == -1) {	
			perror("Error in opening client request fifo. Client is discarding...");	
			close_client_fifos();
			continue;
		}
		
		// Mask SIGCHLD
		if (mask_sigchld(&mask_signals_set) == -1) {
			perror("Program is closing");
			cleanup();
			exit(EXIT_FAILURE);
		}
		
		int is_client_discarded = 0;
		int is_client_accepted = 0;		
		if (con_counter < max_con_num) is_client_accepted = 1;
		else if (connect_request.connect_type == TRY_CONNECT) {
			char log_msg[1000];
			sprintf(log_msg, "Connection request PID %d... Que FULL \n", connect_request.client_pid);
			write_log(log_msg);
			
			printf("Connection request PID %d... Que FULL \n", connect_request.client_pid);
		}
		else if (connect_request.connect_type == CONNECT) {
			char log_msg[1000];
			sprintf(log_msg, "Connection request PID %d... Que FULL \n", connect_request.client_pid);
			write_log(log_msg);
			
			printf("Connection request PID %d... Que FULL \n", connect_request.client_pid);
		
			// Unmask SIGCHLD
			if (unmask_signals(&mask_signals_set) == -1) {
				perror("Error in unmasking signal. Program is closing");
				cleanup();
				exit(EXIT_FAILURE);
			 }
		
			// Wait until there is a spot in queue
			int connection_waiting_response = QUEUE_WAITING;
			int write_res = -1;
			int healthcheck_counter = 0;
			while (!is_client_discarded && con_counter >= max_con_num) {
				if (healthcheck_counter >= 5) {
					healthcheck_counter = 0;
				
					// Send client a message to tell, it's waiting for queue
					write_res = write_fifo(client_res_fifo_fd, (void*) &connection_waiting_response, sizeof(int));
					if (write_res == -1) {
						if (errno == EPIPE) {
							perror("Error in writing to client result fifo. It is not open for read. Client is discarding...");
							close_client_fifos();
							is_client_discarded = 1;
						}
						else {
							perror("Error in writing to client result fifo. Program is closing...");
							cleanup();
							exit(EXIT_FAILURE);
						}
					}
				}
				
				// Call handle_sigchld to ensure any terminated child is not missed in any case
				handle_sigchld(SIGCHLD);
				++healthcheck_counter;
				
				// Sleep 10ms and check the spot again
				sleep(0.01); 
			}
			is_client_accepted = 1;
			
			// Mask SIGCHLD
			if (mask_sigchld(&mask_signals_set) == -1) {
				perror("Program is closing");
				cleanup();
				exit(EXIT_FAILURE);
			}
		}
		
		if (is_client_discarded) {
			// Unmask SIGCHLD
			if (unmask_signals(&mask_signals_set) == -1) {
				perror("Error in unmasking signal. Program is closing");
				cleanup();
				exit(EXIT_FAILURE);
			 }
			 handle_sigchld(SIGCHLD);
			 continue;
		}	
			
		// Send the result to the child fifo
		// Fork if it's accepted
		
		is_client_discarded = 0;	
		int connection_response = QUEUE_FULL;
		if (is_client_accepted) connection_response = CLIENT_CONNECTED;
		int write_res = write_fifo(client_res_fifo_fd, (void*) &connection_response, sizeof(int));
		if (write_res == -1) {
			if (errno == EPIPE) {
				perror("Error in writing to client result fifo. It is not open for read. Client is discarding...");
				close_client_fifos();
				is_client_discarded = 1;
			}
			else {
				perror("Error in writing to client result fifo. Program is closing...");
				cleanup();
				exit(EXIT_FAILURE);
			}
		}
		
		if (is_client_discarded) {
			// Unmask SIGCHLD
			if (unmask_signals(&mask_signals_set) == -1) {
				perror("Program is closing");
				cleanup();
				exit(EXIT_FAILURE);
			 }
			 handle_sigchld(SIGCHLD);
			 continue;
		}
		
		if (!is_client_accepted) {
			char log_msg[1000];
			sprintf(log_msg, "Client is not accepted with PID %d \n", connect_request.client_pid);
			write_log(log_msg);
			
			// Wait for ack from client
			int client_ack = -1;
			read_fifo(client_req_fifo_fd, (void*) &client_ack, sizeof(int));
	
			// Close client fifos
			close_client_fifos();
	
			// Unmask SIGCHLD and call the handler for once to be sure a signal is not missed
			if (unmask_signals(&mask_signals_set) == -1) {
				perror("Program is closing");
				cleanup();
				exit(EXIT_FAILURE);
			 }
			handle_sigchld(SIGCHLD);
		}
		else {
			// Client accepted
			fork_pid = fork();
			if (fork_pid == -1) {
				perror("Error in fork for worker process. Program is closing...");
				cleanup();
				exit(EXIT_FAILURE);	
			}	
			else if (fork_pid == 0) {
				// Child process
				worker_process(dir_name, connect_request.client_pid);
			}
			else {
				// Parent process
				++client_idx;
					
				// Close client fifos
				close_client_fifos();
				
				// Add child pid to child_pids array
				child_pids[con_counter] = fork_pid;
				++con_counter;	
			
				// Unmask SIGCHLD and call the handler for once to be sure a signal is not missed
				if (unmask_signals(&mask_signals_set) == -1) {
					perror("Program is closing");
					cleanup();
					exit(EXIT_FAILURE);
				 }
				handle_sigchld(SIGCHLD);
			}
		}
	}

	cleanup();
	exit(EXIT_SUCCESS);		
} 


