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

#define ARCH_DIR_NAME "ARCH_DIR"
#define ARCH_LOCK_NAME "ARHC_LOCK.txt"
#define TAR_COMMAND_BASE "tar -cvf "

int lock_fd = -1;
int con_fifo_fd = -1;
int client_req_fifo_fd = -1;
int client_res_fifo_fd = -1;
int arch_fork_pid = -1;
int arch_lock_fd = -1;
int fd = -1;

char con_fifo_path[FIFO_PATH_SIZE];
char client_req_fifo_path[FIFO_PATH_SIZE];
char client_res_fifo_path[FIFO_PATH_SIZE];

void unlink_fifos() {
	unlink_file_if_possible(&client_req_fifo_fd, client_req_fifo_path);
	unlink_file_if_possible(&client_res_fifo_fd, client_res_fifo_path);
	unlink_file_if_possible(&arch_lock_fd, ARCH_LOCK_NAME);
}

void close_fds() {
	close_file_if_possible(&con_fifo_fd);
	close_file_if_possible(&client_req_fifo_fd);
	close_file_if_possible(&client_res_fifo_fd);
	close_file_if_possible(&fd);
	close_file_if_possible(&lock_fd);
	close_file_if_possible(&arch_lock_fd);
}

void cleanup() {
	int arr[1];
	arr[0] = arch_fork_pid;
	signal_and_wait_all_children(arr, 1);
				
	unlink_fifos();	
	close_fds();
}

void handle_sigterm_sigint(int signal) {
	cleanup();
	
	fprintf(stderr, "Client is closing... \n");
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

void read_user_input(int argc, char* argv[], int* connect_type, int* server_pid) {
	if (argc != 4) {
		fprintf(stderr, "There must be exactly 3 arguments \n");
		exit(EXIT_FAILURE);
	}
	     	
	if (strcmp(argv[1], "neHosClient") != 0) {
		fprintf(stderr, "Error in user input server name. Program is closing... \n");
		exit(EXIT_FAILURE);
	}
   	
   	if (strcmp(argv[2], "Connect") == 0) {
   		*connect_type = CONNECT;
   	}
   	else if (strcmp(argv[2], "tryConnect") == 0) {
   		*connect_type = TRY_CONNECT;
   	}
   	else {
   		fprintf(stderr, "Error in user input connect type. Program is closing... \n");
   		exit(EXIT_FAILURE);
   	}
   	
   	convert_str_to_int(argv[3], server_pid);
   	if (server_pid < 0) {
   		fprintf(stderr, "Error in user input client number. Program is closing... \n");
   		exit(EXIT_FAILURE);
   	}
}

int read_and_print_until_transmission_completed(int fifo_fd) {
	char temp_buf[BUF_INIT_SIZE];
	char buf[BUF_INIT_SIZE];
	int buf_size = 0;
	int total_bytes_printed = 0;
	int total_bytes_read_for_all = 0;
	int is_done = 0;
	struct metadata_t metadata;
	while (!is_done) {
		int read_res = read_fifo(fifo_fd, (void*) &metadata, sizeof(struct metadata_t));
		if (read_res == 0 || read_res == -1) {
			fprintf(stderr, "Error in reading from server response fifo. Connection lost. Program is closing...\n");
			return -1;
		}
		
		int total_bytes_read_for_packet = 0;
		while (total_bytes_read_for_packet != metadata.len) {
			int max_char_to_read = metadata.len - total_bytes_read_for_packet;	
			if (BUF_INIT_SIZE < max_char_to_read) max_char_to_read = BUF_INIT_SIZE;
		
			int read_bytes = read(client_res_fifo_fd, (void*) temp_buf, max_char_to_read);
			if (read_bytes == -1) {
				if (!is_errno_eintr_or_eagain()) return -1;
			}
			else if (read_bytes == 0) {	
				fprintf(stderr, "Error in reading from server response fifo. Connection lost. Program is closing... \n");
				return -1;
			}
			else {
				total_bytes_read_for_packet += read_bytes;
				total_bytes_read_for_all += read_bytes;
			
				if (read_bytes + buf_size > BUF_INIT_SIZE) {
					write(STDOUT_FD, (void*) buf, sizeof(char) * buf_size);
					fflush(stdout);
					total_bytes_printed += buf_size;
					buf_size = 0;
					
					if (read_bytes >= BUF_INIT_SIZE) {
						write(STDOUT_FD, (void*) temp_buf, sizeof(char) * read_bytes);
						fflush(stdout);
						total_bytes_printed += read_bytes;
					}
					else {
						strncpy(buf, temp_buf, read_bytes);
						buf_size += read_bytes;	
					}	
				}
				else if (read_bytes + buf_size == BUF_INIT_SIZE) {
					strncpy(&buf[buf_size], temp_buf, read_bytes);
					write(STDOUT_FD, (void*) buf, sizeof(char) * BUF_INIT_SIZE);
					fflush(stdout);
					
					total_bytes_printed += BUF_INIT_SIZE;
					buf_size = 0;
				}
				else {
					strncpy(&buf[buf_size], temp_buf, read_bytes);
					buf_size += read_bytes;
				}
			}
		}
		
		if (metadata.is_sent_completely) is_done = 1;
	}
	if (total_bytes_printed != total_bytes_read_for_all) {
		write(STDOUT_FD, (void*) buf, sizeof(char) * buf_size);
	}
	return 0;
}

int handle_upload(char* request_file_name) {
	// Open file
	struct flock lock;
	lock_fd = -1;
	
	lock_fd = lock_file(request_file_name, &lock, O_RDWR);
	if (lock_fd == -1) {
		perror("Error in opening file for uploading");
		return -1;
	}
	
	// Read handshaking response from server
	int transmission_response = -1;
	int read_bytes = read_fifo(client_res_fifo_fd, (void*) &transmission_response, sizeof(int));
	if (read_bytes == -1 || read_bytes == 0) {
		perror("Error in opening client response fifo. Connection lost. Program is closing...");
		if (unlock_file(&lock_fd, &lock) == -1) {
			perror("Error in unlocking file lock \n");
			return -1;
		}
		return -1;
	}
	
	if (transmission_response == ERR_FILE_EXISTS) {
		printf("File already exists in server side. Upload is ended. \n");
		if (unlock_file(&lock_fd, &lock) == -1) {
			perror("Error in unlocking file lock \n");
			return -1;
		}
		return 0;
	}
	
	printf("Uploading file... Please wait... It can take a few minutes if it is a large file...\n");
	
	// Read file and write to fifo char-by-char
	int is_done = 0;
	while (!is_done) {
		char ch;
		int read_bytes = read_file_until_size(lock_fd, &ch, sizeof(char));	
		if (read_bytes == -1) {
			perror("Error in reading file for uploading");
			if (unlock_file(&lock_fd, &lock) == -1) {
				perror("Error in unlocking file lock \n");
				return -1;
			}
			return -1;
		}
		else if (read_bytes == 0) {
			if (send_metadata_and_write_until_size(client_req_fifo_fd, &ch, 0, COMPLETED) == -1) {
				perror("Error in writing to client request fifo. Connection lost. Program is closing...");
				if (unlock_file(&lock_fd, &lock) == -1) {
					perror("Error in unlocking file lock \n");
					return -1;
				}
				return -1;
			}
			is_done = 1;
		}
		else {
			if (send_metadata_and_write_until_size(client_req_fifo_fd, &ch, sizeof(char), NOT_COMPLETED_YET) == -1) {
				perror("Error in writing to client request fifo");
				if (unlock_file(&lock_fd, &lock) == -1) {
					perror("Error in unlocking file lock \n");
					return -1;
				}
				return -1;
			}
		}
	}
	
	// Close file
	if (unlock_file(&lock_fd, &lock) == -1) {
		perror("Error in unlocking file lock \n");
		return -1;
	}
	
	if (read_and_print_until_transmission_completed(client_res_fifo_fd) == -1) {
		perror("Error in reading response from response fifo. Program is closing...");
		return -1;
	}
	
	return 0;
}

int handle_download(char* request_file_name, int is_for_arch) {
	// Open file
	struct flock lock;
	lock_fd = -1;
	
	lock_fd = lock_file(request_file_name, &lock, O_WRONLY | O_CREAT);
	if (lock_fd == -1) {
		perror("Error in creating file for downloading");
		return -1;
	}
	
	// Read handshaking response from server
	int transmission_response = -1;
	int read_bytes = read_fifo(client_res_fifo_fd, (void*) &transmission_response, sizeof(int));
	if (read_bytes == -1 || read_bytes == 0) {
		perror("Error in opening client response fifo. Connection lost. Program is closing...");
		if (unlock_file(&lock_fd, &lock) == -1) {
			perror("Error in unlocking file lock \n");
			return -1;
		}
		return -1;
	}
		
	if (transmission_response == ERR_FILE_NOT_EXISTS) {
		printf("File already exists in server side. Upload is ended. \n");
		if (unlock_file(&lock_fd, &lock) == -1) {
			perror("Error in unlocking file lock \n");
			return -1;
		}
		return 0;
	}
	
	printf("Downloading file. Please wait... It can take a few minutes if it is a large file... \n");
	
	// Read from fifo char-by-char and write to file
	char buf[5];
	int is_done = 0;
	struct metadata_t metadata;
	memset(&metadata, 0, sizeof(struct metadata_t));
	while (!is_done) {
		int read_res = read_fifo(client_res_fifo_fd, (void*) &metadata, sizeof(struct metadata_t));
		if (read_res == 0 || read_res == -1) {
			perror("Error in reading fifo. Server connection is lost.");
			remove(request_file_name);
			if (unlock_file(&lock_fd, &lock) == -1) {
				perror("Error in unlocking file lock \n");
				return -1;
			}
			return -1;
		}
		
		int total_bytes_read_for_packet = 0;
		while (total_bytes_read_for_packet != metadata.len) {
			int read_bytes = read(client_res_fifo_fd, (void*) buf, sizeof(char));
			if (read_bytes == -1) {
				if (!is_errno_eintr_or_eagain()) {
					perror("Error in reading server response fifo");
					remove(request_file_name);
					if (unlock_file(&lock_fd, &lock) == -1) {
						perror("Error in unlocking file lock \n");
						return -1;
					}
					return -1;
				}
			}
			else if (read_bytes == 0) {
				perror("Error in reading server response fifo. Connection lost. Program is closing...");
				remove(request_file_name);	
				if (unlock_file(&lock_fd, &lock) == -1) {
					perror("Error in unlocking file lock \n");
					return -1;
				}
				return -1;
			}
			else {
				if (write_file_until_size(lock_fd, buf, sizeof(char)) == -1) {
					perror("Error in writing to file in downloading");
					remove(request_file_name);	
					if (unlock_file(&lock_fd, &lock) == -1) {
						perror("Error in unlocking file lock \n");
						return -1;
					}
					return -1;
				}
				total_bytes_read_for_packet += read_bytes;		
			}
		}
		
		if (metadata.is_sent_completely) is_done = 1;
	}
	

	// Close file
	if (unlock_file(&lock_fd, &lock) == -1) {
		perror("Error in unlocking file lock \n");
		return -1;
	}
	
	if (!is_for_arch) {
		if (read_and_print_until_transmission_completed(client_res_fifo_fd) == -1) {
			perror("Error in reading response from response fifo. Program is closing...");
			return -1;
		}
	}
	
	return 0;
}

int remove_arch_dir() {
	char command[100];
	command[0] = '\0';
	strcat(command, "rm -r ");
	strcat(command, ARCH_DIR_NAME);
	return system(command);	
}

int handle_archserver(struct user_request_t* userrequest) {
	// Get arch lock
	// Open file
	struct flock lock;
	arch_lock_fd = -1;
	
	arch_lock_fd = lock_file(ARCH_LOCK_NAME, &lock, O_RDWR | O_CREAT);
	if (arch_lock_fd == -1) {
		return -1;
	}

	// Create arch directory 
	if (mkdir(ARCH_DIR_NAME, ALL_PERMISSIONS) == -1) {	
		if (errno == EEXIST) {
			if (remove_arch_dir() == -1) {
   				perror("Error in creating directory. Program is closing... ");
   				return -1;
			}
			if (mkdir(ARCH_DIR_NAME, ALL_PERMISSIONS) == -1) {
				perror("Error in creating directory. Program is closing... ");
				return -1;	
			}
		}
		else {
			perror("Error in creating directory. Program is closing... ");
			return -1;
		}
   	}
   	
   	char file_name[COMMAND_MAX_SIZE + 30];
   	char read_file_name[COMMAND_MAX_SIZE];
   	int is_done = 0;
   	struct metadata_t metadata;
	memset(&metadata, 0, sizeof(struct metadata_t));
   	while (!is_done) {
   	
   		int read_res = read_fifo(client_res_fifo_fd, (void*) &metadata, sizeof(struct metadata_t));
		if (read_res == 0 || read_res == -1) {
			perror("Error in reading fifo. Server connection is lost.");
			remove_arch_dir();
			return -1;
		}		
		
   		if (metadata.is_sent_completely == COMPLETED) is_done = 1;
   		else {
   			// means there is a file to download
   			
   			// first fetch the file name
   			int read_bytes = read_fifo_until_size(client_res_fifo_fd, read_file_name, metadata.len);
   			if (read_bytes == -1) {
   				perror("Error in reading fifo. Server connection is lost.");
				remove_arch_dir();
				return -1;
   			}
   			
   			// Add arch directory name to the beginning of the file name as path
   			strcpy(file_name, ARCH_DIR_NAME);
   			file_name[strlen(ARCH_DIR_NAME)] = '\0';
   			
   			strcat(file_name, "/");
   			strcat(file_name, read_file_name);
   		
   			// download the file
   			if (handle_download(file_name, 1) == -1) {
				remove_arch_dir();
				return -1;
			}
   		}
   	}
   	
 	
   	// Fork+exec to execute tar utility on the arch directory
   	arch_fork_pid = fork();
   	if (arch_fork_pid == -1) {
   		perror("Error in reading fifo. Server connection is lost.");
		remove_arch_dir();
		return -1;
   	}
   	else if (arch_fork_pid == 0) {
   		// child
   		close_fds();

		char* const tar_argv[] = {"tar", "-cf", userrequest->file_name, "ARCH_DIR/", NULL};
   		execvp("tar", tar_argv);
   	}
   	else {
   		// parent
   		is_done = 0;
   		int status = -1;
   		while (!is_done) {
	   		int wait_res = waitpid(-1, &status, 0	);
	   		if (wait_res == -1) {
	   			if (!is_errno_eintr_or_eagain()) {
	   				perror("Error in waiting child.");
					remove_arch_dir();
					return -1;
	   			}
	   		}
   			else if (wait_res >= 0) is_done = 1;
   		}
   	}	
   	
   	// Remove arch directory
	remove_arch_dir();
	
	// Remove arch lock and close
	remove(ARCH_LOCK_NAME);
	if (unlock_file(&arch_lock_fd, &lock) == -1) {
		perror("Error in unlocking file lock \n");
		return -1;
	}
	
	return 0;
}

int handle_quit_or_killserver() {
	
	// Read server response
	if (read_and_print_until_transmission_completed(client_res_fifo_fd) == -1) {
		perror("Error in reading response from response fifo. Program is closing...");
		cleanup();
		exit(EXIT_FAILURE);
	}
	
	// Kapanis icin ack gonder	
	int pid = -1;
	int write_bytes = write_fifo(client_req_fifo_fd, (void*) &pid, sizeof(int));
	if (write_bytes == -1) {
		cleanup();
		exit(EXIT_FAILURE);
	}
	
	printf("Client is closing... \n");
	cleanup();
	exit(EXIT_SUCCESS);
	return 0;
}

void client_loop() {
	int is_done = 0;
	char command_str[COMMAND_MAX_SIZE];
	while (!is_done) {
		printf(">");
		fflush(stdout);
	
		int read_bytes = read(STDIN_FD, command_str, sizeof(char) * (COMMAND_MAX_SIZE - 1));	
		if (read_bytes < 0) {
			if (errno != EINTR && errno != EAGAIN) {
				perror("Error in getting user input. Program is closing...");
				fflush(stderr);
				cleanup();
				exit(EXIT_FAILURE);
			}
		}
		else if (read_bytes == 0) continue;
		
		// Put EOF at the end
		command_str[read_bytes] = '\0';
		
		// Copy command_str to parse
		char copy_command_str[COMMAND_MAX_SIZE];
		strcpy(copy_command_str, command_str);
		copy_command_str[strlen(command_str)] = '\0';
		
		int sent_server = 1;
		
		struct user_request_t userrequest;
		int parse_result = parse_user_request(copy_command_str, &userrequest);
		if (parse_result != -1) {
			if (userrequest.command == UPLOAD) {
				// Check if there is such file
				int file_exists = check_if_file_exists(userrequest.file_name);
				if (file_exists == 0) {
					printf("There is no such file in client side \n");				
					sent_server = 0;
				}
			}
			if (userrequest.command == DOWNLOAD) {
				// Check if there is such file
				int file_exists = check_if_file_exists(userrequest.file_name);
				if (file_exists == 1) { 
					printf("File already exists in client side \n");				
					sent_server = 0;
				}
			}
		}
		
		if (sent_server) {
			// Send metadata
			if (send_metadata_and_write_until_size(client_req_fifo_fd, command_str, sizeof(char) * (read_bytes + 1), COMPLETED) == -1) {
				perror("Error in writing to client request fifo. Program is closing...");
				cleanup();
				exit(EXIT_FAILURE);
			}
			
			int res = -1;
			int is_command_processed = 0;
			if (parse_result != -1) {
				if (userrequest.command == UPLOAD) {
					res = handle_upload(userrequest.file_name);
					if (res == -1) {
						cleanup();
						exit(EXIT_FAILURE);
					}
					is_command_processed = 1;
				}
				else if (userrequest.command == DOWNLOAD) {
					res = handle_download(userrequest.file_name, 0);
					if (res == -1) {
						cleanup();
						exit(EXIT_FAILURE);
					}
					is_command_processed = 1;
				}
				else if (userrequest.command == ARCHSERVER) {
					res = handle_archserver(&userrequest);
					if (res == -1) {
						cleanup();
						exit(EXIT_FAILURE);
					}
					is_command_processed = 1;
				}
				
				else if (userrequest.command == QUIT) {
					res = handle_quit_or_killserver(&userrequest);
					if (res == -1) {
						cleanup();
						exit(EXIT_FAILURE);
					}
					is_command_processed = 1;
				}
				else if (userrequest.command == KILLSERVER) {
					res = handle_quit_or_killserver(&userrequest);
					if (res == -1) {
						cleanup();
						exit(EXIT_FAILURE);
					}
					is_command_processed = 1;
				}
			}
			
			if (!is_command_processed) {
				if (read_and_print_until_transmission_completed(client_res_fifo_fd) == -1) {
					perror("Error in reading response from response fifo. Program is closing...");
					cleanup();
					exit(EXIT_FAILURE);
				}
			}
		}
	}
}

int main(int argc, char* argv[]) {
	// Set signal handlers
	if (set_sigpipe_to_ignore() == -1) exit(EXIT_FAILURE);
	
	if (set_sigterm_sigint_handler() == -1) {
		perror("Error in setting signal handlers. Program is closing...");
		exit(EXIT_FAILURE);
	}
	
	// Get user input for server creation
	int connect_type = -1;
	int server_pid = -1;
	read_user_input(argc, argv, &connect_type, &server_pid);
	
	/* Mkfifo for child req res fifos */
	
	// Convert pid to str
	char pid_str[PID_STR_SIZE];
	int pid = getpid();
	sprintf(pid_str, "%d", pid);
	
	// Init client_req_fifo_path and fifo
	strcpy(client_req_fifo_path, CLIENT_REQ_BASE_PATH);
	strcat(client_req_fifo_path, pid_str);

	// Create fifo
	if (mkfifo(client_req_fifo_path, ALL_PERMISSIONS) == -1) {
		if (errno != EEXIST) {
			perror("Error in creating client request fifo. Program is closing...");
			exit(EXIT_FAILURE);
		}
	}
	
	// Init client_res_fifo_path and fifo
	strcpy(client_res_fifo_path, CLIENT_RES_BASE_PATH);
	strcat(client_res_fifo_path, pid_str);

	// Create fifo
	if (mkfifo(client_res_fifo_path, ALL_PERMISSIONS) == -1) {
		if (errno != EEXIST) {
			perror("Error in creating client response fifo. Program is closing...");
			close_file_if_possible(&client_req_fifo_fd);
			exit(EXIT_FAILURE);
		}
	}
	
	/* Init con_fifo_path */
	
	// Convert pid to str
	sprintf(pid_str, "%d", server_pid);
	
	strcpy(con_fifo_path, SERVER_BASE_PATH);
	strcat(con_fifo_path, pid_str);
	
	con_fifo_fd = open_fifo(con_fifo_path, O_WRONLY);
	if (con_fifo_fd == -1) {
		perror("Error in opening server connection fifo. Program is closing...");
		cleanup();
		exit(EXIT_FAILURE);
	}
	
	// Send connect request
	
	struct connect_request_t connect_request;
	memset(&connect_request, 0, sizeof(struct connect_request_t));
	connect_request.connect_type = connect_type;
	connect_request.client_pid = pid;

	int write_bytes = write_fifo(con_fifo_fd, (void*) &connect_request, sizeof(struct connect_request_t));
	if (write_bytes == -1) {
		perror("Error in writing to server connection fifo. Program is closing...");
		cleanup();
		exit(EXIT_FAILURE);
	}
		

	client_res_fifo_fd = open_fifo(client_res_fifo_path, O_RDONLY);
	if (client_res_fifo_fd == -1) {
		perror("Error in opening client response fifo. Program is closing...");
		cleanup();
		exit(EXIT_FAILURE);
	}

	client_req_fifo_fd = open_fifo(client_req_fifo_path, O_WRONLY);
	if (client_req_fifo_fd == -1) {
		perror("Error in opening client request fifo. Program is closing...");
		cleanup();
		exit(EXIT_FAILURE);
	}
	
	int connection_response = QUEUE_WAITING;
	int read_bytes = -1;
	while (connection_response == QUEUE_WAITING) {
		read_bytes = read_fifo(client_res_fifo_fd, (void*) &connection_response, sizeof(int));
		if (read_bytes == -1 || read_bytes == 0) {
			perror("Error in opening client response fifo. Connection lost. Program is closing...");
			cleanup();
			exit(EXIT_FAILURE);
		} 
	}

	if (connection_response == QUEUE_FULL) {	
		printf("Server response: QUEUE FULL. Client is closing... \n");
	
		// Kapanis icin ack gonder	
		int write_bytes = write_fifo(client_req_fifo_fd, (void*) &pid, sizeof(int));
		if (write_bytes == -1) {
			cleanup();
			exit(EXIT_FAILURE);
		}
	}
	else {
		client_loop();
	}

	cleanup();
	exit(EXIT_SUCCESS);
}
