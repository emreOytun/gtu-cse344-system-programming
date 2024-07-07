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

#define FIFO_PERM 0666

char* fifo1 = "fifo1";
char* fifo2 = "fifo2";

int fd_fifo1_read = -1;
int fd_fifo2_read = -1;

int fd_fifo1_write = -1;
int fd_fifo2_write = -1;

int fd_fifo1_child_read = -1;
int fd_fifo2_child_read = -1;
int fd_fifo2_child_write = -1;

int pid1 = -1;
int pid2 = -1;

volatile int counter = 0;
volatile int child_terminated_abnormally = 0;

// Convert string to integer
int convert_str_to_int(const char* str, int* num) {
    char* end;
    const long l = strtol(str, &end, 10);
    if (end == str) {
        return -1;  // not a decimal number
    }
    else if (*end != '\0') {
        return -1;  // there are some extra characters after number
    }
    else if (l > INT_MAX) {
        return -1;
    }
    else if (l < INT_MIN) {
        return -1;
    }
    *num = (int) l;
    return 0;
}

void unlink_fifos(char* fifo1, char* fifo2) {
	unlink(fifo1);
	unlink(fifo2);
}

void close_fd_fifos() {
	if (fd_fifo1_write != -1) {
		close(fd_fifo1_write);
	}
	
	if (fd_fifo2_write != -1) {
		close(fd_fifo2_write);
	}
	
	if (fd_fifo2_child_write != -1) {
		close(fd_fifo2_child_write);
	}
	
	if (fd_fifo1_read != -1) {
		close(fd_fifo1_read);
	}
	
	if (fd_fifo2_read != -1) {
		close(fd_fifo2_read);
	}
	
	if (fd_fifo1_child_read != -1) {
		close(fd_fifo1_child_read);
	}
	
	if (fd_fifo2_child_read != -1) {
		close(fd_fifo2_child_read);
	}
}

int parent_exit(int exit_status) {
	printf("Parent with id: %d exited with exit status: %d \n", getpid(), exit_status);
	fflush(stdout);
	exit(exit_status);
}

void send_sigterm_if_possible(int pid) {
	if (pid != -1) {
		kill(pid, SIGTERM);
	}
}

void signal_and_wait_all_children_to_terminate_on_error() {
	send_sigterm_if_possible(pid1);
	send_sigterm_if_possible(pid2);

	int is_done = 0;
	int status;
	while (!is_done) {
		printf("An error detected. Waiting for running children to terminate... \n");
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
		else if (pid > 0) {
			// If it is stopped it may be checked by WIFSTOPPED(status)
			if (WIFEXITED(status) || WIFSIGNALED(status)) {
				// A child has died
				if (WIFEXITED(status)) {
					printf("Child has been terminated. Child id: %d Exit status: %d \n", pid, WEXITSTATUS(status));
				}
				else {
					printf("Child has been terminated by signal. Child id: %d Signal: %d \n", pid, WTERMSIG(status));
				}
				fflush(stdout);
				++counter;
			}
		}
		sleep(2); // sleep 2sn
	}
}

void handle_sigterm_sigint(int signal) {
	if (pid1 == 0 || pid2 == 0) {
		// child process	
		close_fd_fifos();
		_exit(EXIT_FAILURE);
	}
	else {
		// parent process
		printf("Process received killing signal. Killing child processes if any and cleaning file descriptors \n");
		
		signal_and_wait_all_children_to_terminate_on_error();
		close_fd_fifos();
		unlink_fifos(fifo1, fifo2);
		
		printf("Parent with id: %d exited with exit status: %d \n", getpid(), EXIT_FAILURE);
		fflush(stdout);
		_exit(EXIT_FAILURE);
	}
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

void child1(int* result, int n) {
	sleep(10);
	
	// Close unnecessary file descriptors
	close_fd_fifos();
	
	fd_fifo1_child_read = open(fifo1, O_RDONLY);
	if (fd_fifo1_child_read == -1) {
		perror("Error in opening fifo1 in child1");
		close_fd_fifos();
		_exit(EXIT_FAILURE);
	}
	
	fd_fifo2_child_write = open(fifo2, O_WRONLY);
	if (fd_fifo2_child_write == -1) {
		perror("Error in opening fifo2 in child1");
		close_fd_fifos();
		_exit(EXIT_FAILURE);
	}
	
	// Get array elements and sum meanwhile
	int bytes_read = 0;
	int num;
	int nums_read = 0;
	while (nums_read < n) {
		bytes_read = read(fd_fifo1_child_read, (void*) &num, sizeof(int));
		if (bytes_read > 0) {
			++nums_read;	
			*result = *result + num;	
		}
		else if (!( (bytes_read < 0 && (errno == EINTR || errno == EAGAIN)) || bytes_read == 0 )) {
			perror("Error in reading from fifo1 in child1");	
			close_fd_fifos();
			_exit(EXIT_FAILURE);
		}
		else {
			sleep(1);
		}
	}
	
	// Send the result to fifo2
	if (write(fd_fifo2_child_write, (void*) result, sizeof(int)) < 0) {
		perror("Error in writing to fifo2 from child1");
		close_fd_fifos();
		_exit(EXIT_FAILURE);
	}
	
	close_fd_fifos();
	_exit(EXIT_SUCCESS);
}

void child2(int* result, int n) {
	sleep(10);
	
	// Close unnecessary file descriptors
	close_fd_fifos();
	
	fd_fifo2_child_read = open(fifo2, O_RDONLY);
	if (fd_fifo2_child_read == -1) {
		perror("Error in opening fifo2 in child2");
		close_fd_fifos();
		_exit(EXIT_FAILURE);
	}
	
	// Get random numbers from fifo2
	int mult = 1;
	int bytes_read;
	int num;
	int nums_read = 0;
	while (nums_read < n) {
		bytes_read = read(fd_fifo2_child_read, (void*) &num, sizeof(int));
		if (bytes_read > 0) {
			++nums_read;
			mult = mult * num;
		}
		else if (!( (bytes_read < 0 && (errno == EINTR || errno == EAGAIN)) || bytes_read == 0 )) {
			perror("Error in reading from fifo2 in child2");	
			close_fd_fifos();
			_exit(EXIT_FAILURE);
		}
		else {
			sleep(1);
		}
	}
	*result = mult;
	
	// Get the command
	char command[11];
	int is_done = 0;
	while (!is_done) {
		bytes_read = read(fd_fifo2_child_read, (void*) &command, sizeof(char) * 8);
		if (bytes_read > 0) {
			is_done = 1;
		}
		else if (!( (bytes_read < 0 && (errno == EINTR || errno == EAGAIN)) || bytes_read == 0 )) {
			perror("Error in reading from fifo2 in child2");	
			close_fd_fifos();
			_exit(EXIT_FAILURE);
		}
		else {
			sleep(1);
		}
	}
	command[bytes_read] = '\0';
	
	// Get the result calculated in child1
	int result_child1;
	is_done = 0;
	while (!is_done) {
		bytes_read = read(fd_fifo2_child_read, (void*) &result_child1, sizeof(int));
		if (bytes_read > 0) {
			is_done = 1;
		}
		else if (!( (bytes_read < 0 && (errno == EINTR || errno == EAGAIN)) || bytes_read == 0 )) {
			perror("Error in reading from fifo2 in child2");	
			close_fd_fifos();
			_exit(EXIT_FAILURE);
		}
		else {
			sleep(1);
		}
	}
	
	// Calculate the result
	int i;
	
	if (strcmp("multiply", command) != 0) {
		// handle wrong command as error
		fprintf(stderr, "Wrong command sent from parent process to fifo2 \n");
		close_fd_fifos();
		_exit(EXIT_FAILURE);
	}
	
	// Print the result
	int general_result = result_child1 + (*result);
	printf("Result: %d \n", general_result);
	fflush(stdout);
	
	close_fd_fifos();
	_exit(EXIT_SUCCESS);
}

void handle_sigchld(int signal) {
	// One SIGCHLD can come if several child processes terminate at the same time.
	// So, using waitpid with WNOHANG flag is required to handle all of them.

	// Also, SIGCHLD can come if a child process is stopped.
	// So, using waitpid with WUNTRACED flag is required.
	
	int status;
	struct flock lock;
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
				int kill_others = 0;
				if (WIFEXITED(status)) {	
					printf("Child has been terminated. Child id: %d Exit status: %d \n", pid, WEXITSTATUS(status));
					if (WEXITSTATUS(status) == EXIT_FAILURE) {
						kill_others = 1;
					}
				}
				else {
					printf("Child has been terminated by signal. Child id: %d Signal: %d \n", pid, WTERMSIG(status));
					kill_others = 1;
				}
				fflush(stdout);
				++counter;
				if (kill_others == 1) {
					child_terminated_abnormally = 1;
					if (pid == pid1) {
						pid1 = -1;
					}
					else if (pid == pid2) {
						pid2 = -1;
					}
					send_sigterm_if_possible(pid1);
					send_sigterm_if_possible(pid2);
				}
			}
		}
	}	
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

int main(int argc, char* argv[]) {
	int result = 0;

	// Take integer number argument
	if (argc != 2) {
		fprintf(stderr, "Integer argument is missing \n");
		parent_exit(EXIT_FAILURE);
	}
	
	int n = 0;
	if (convert_str_to_int(argv[1], &n) == -1) {
		fprintf(stderr, "Given argument is not an integer \n");
		parent_exit(EXIT_FAILURE);
	}
	
	// Set signal handler for SIGTERM and SIGINT
	if (set_sigterm_sigint_handler() == -1) {
		parent_exit(EXIT_FAILURE);
	}
	
	// Create fifos
	if (mkfifo(fifo1, FIFO_PERM) == -1) {
		if (errno != EEXIST) {
			perror("There is a problem with opening fifo1");
			parent_exit(EXIT_FAILURE);
		}
	}
	
	if (mkfifo(fifo2, FIFO_PERM) == -1) {
		if (errno != EEXIST) {
			perror("There is a problem with opening fifo2");
			unlink(fifo1);
			parent_exit(EXIT_FAILURE);
		}
	}

	// Open fifos. Send array of random numbers to fifo1 and command to fifo2
	fd_fifo1_read = open(fifo1, O_RDONLY | O_NONBLOCK);
	if (fd_fifo1_read == -1) {
		perror("Error in opening fifo1");
		unlink_fifos(fifo1, fifo2);
		parent_exit(EXIT_FAILURE);
	}
	
	fd_fifo1_write = open(fifo1, O_WRONLY);
	if (fd_fifo1_write == -1) {
		perror("Error in opening fifo1");
		close_fd_fifos();
		unlink_fifos(fifo1, fifo2);
		parent_exit(EXIT_FAILURE);
	}
	
	fd_fifo2_read = open(fifo2, O_RDONLY | O_NONBLOCK);
	if (fd_fifo2_read == -1) {
		perror("Error in opening fifo2");
		close_fd_fifos();
		unlink_fifos(fifo1, fifo2);
		parent_exit(EXIT_FAILURE);
	}
	
	fd_fifo2_write = open(fifo2, O_WRONLY);
	if (fd_fifo2_write == -1) {
		perror("Error in opening fifo2");
		close_fd_fifos();
		unlink_fifos(fifo1, fifo2);
		parent_exit(EXIT_FAILURE);
	}

	srand(time(NULL));		
	int numbers_sent = 0;
	printf("Generated numbers in parent: ");
	while (numbers_sent < n) {
		int random_number = rand() % 10;
		printf("%d ", random_number);
	
		// Write the generated items in the array to the fifo1
		if (write(fd_fifo1_write, (void*) &random_number, sizeof(int)) < 0) {
			perror("Error in writing to fifo1");
			close_fd_fifos();
			unlink_fifos(fifo1, fifo2);
			parent_exit(EXIT_FAILURE);
		}
		
		// Write the generated items in the array to the fifo2
		if (write(fd_fifo2_write, (void*) &random_number, sizeof(int)) < 0) {
			perror("Error in writing to fifo2");
			close_fd_fifos();
			unlink_fifos(fifo1, fifo2);
			parent_exit(EXIT_FAILURE);
		}
	
		++numbers_sent;
	}
	printf("\n");
	
	// Write sum command to the fifo2
	char* sum_command = "multiply";
	if (write(fd_fifo2_write, (void*) sum_command, sizeof(char) * 8) < 0) {
		perror("Error in writing to fifo2");
		close_fd_fifos();
		unlink_fifos(fifo1, fifo2);
		parent_exit(EXIT_FAILURE);
	}
	
	// Fork to create child processes
	pid1 = fork();
	if (pid1 == -1) {
		perror("Error in fork1");
		close_fd_fifos();
		unlink_fifos(fifo1, fifo2);
		parent_exit(EXIT_FAILURE);
	}
	if (pid1 == 0) {
		// Child1 process
		child1(&result, n);
	}
	else {
		// Parent process
		pid2 = fork();
		if (pid2 == -1) {
			perror("Error in fork2");
			sleep(1);
			signal_and_wait_all_children_to_terminate_on_error();
			close_fd_fifos();
			unlink_fifos(fifo1, fifo2);
			parent_exit(EXIT_FAILURE);
		}
		if (pid2 == 0) {
			// Child2 process
			child2(&result, n);
		}
		else {
			// Parent process
			if (set_sigchld_handler() == -1) {
				perror("Error in setting SIGCHLD signal handler in parent process");
				sleep(1);
				signal_and_wait_all_children_to_terminate_on_error();
				close_fd_fifos();
				unlink_fifos(fifo1, fifo2);
				parent_exit(EXIT_FAILURE);
			}
			// Wait for counter in spin lock
			int is_done = 0;
			while (!is_done) {
				if (child_terminated_abnormally) {
					printf("An error detected. Waiting for running children to terminate... \n");
				}
				else {
					printf("proceeding \n");
				}
				
				// Check counter
				if (counter == 2) {
					is_done = 1;
				}
			
				// Sleep 2 seconds
				sleep(2);
			}
		}
	}
	
	close_fd_fifos();
	unlink_fifos(fifo1, fifo2);
	
	if (child_terminated_abnormally) {
		parent_exit(EXIT_FAILURE);
	}
	parent_exit(EXIT_SUCCESS);
}
