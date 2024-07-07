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

#define GTU_STUDENT_GRADES 89
#define GTU_STUDENT_GRADES_WITH_TEXT 90
#define ADD_STUDENT_GRADE 91
#define SEARCH_STUDENT 92
#define SORT_ALL 93
#define SHOW_ALL 94
#define LIST_GRADES 95
#define LIST_SOME 96

#define BUFFER_SIZE_LIMIT 5001
#define COMMAND_SIZE_LIMIT 20
#define NAME_SIZE_LIMIT 2000
#define GRADE_SIZE_LIMIT 10
#define SORT_BY_SIZE_LIMIT 5
#define SORT_ORDER_SIZE_LIMIT 4
#define NUM_ENTRIES_SIZE_LIMIT 10
#define PAGE_NUMBER_SIZE_LIMIT 10

#define DESC 100
#define ASC 101
#define NAME 102
#define GRADE 103

#define LOG_FILE_NAME "logs.txt"

static const unsigned int retry_limit = 5000;

// Macro to retry the interrupted system calls
#define HANDLE_EINTR_EAGAIN(x) ({            \
  unsigned int _retries = 0;                 \
                                             \
  typeof(x) _result;                         \
                                             \
  do                                         \
  {                                          \
    _result = (x);                           \
  }                                          \
  while (_result == -1                       \
         && (errno == EINTR ||               \
             errno == EAGAIN ||              \
             errno == EWOULDBLOCK)           \
         && _retries++ < retry_limit);       \
                                             \
  _result;                                   \
})

struct commandinfo_t {
    int command;
    char file_name[NAME_SIZE_LIMIT + 1]; // file name
    char name[NAME_SIZE_LIMIT + 1]; // student name
    char grade[GRADE_SIZE_LIMIT + 1]; // letter grade
    int sort_by;
    int sort_order;
    int num_entries;
    int page_number;
};

char* trim(char *string);
void my_strncpy(char * dest, const char* source, int n);
int find_command(const char* buf, int* i, struct commandinfo_t* command_info);
int find_next_quote(const char* buf, const int* i);
int find_next_two_quote(const char* buf, int* i, int* first_quote, int* second_quote);
int check_end_of_input(const char* buf, const int* i);
int convert_str_to_int(const char* str, int* num);

int fill_command_info(const char* buf, int* i, struct commandinfo_t* command_info);
int parse(char* buf, struct commandinfo_t* command_info);

void close_file(int fd);
void log_to_file(const char* msg);
void log_from_command(const char* buf, int is_success);

struct commandinfo_t parse_record(const char* record, int record_len);
int add_dynamically(struct commandinfo_t* info, struct commandinfo_t** infos, int* len, int* size);
void swap(struct commandinfo_t* infos, int idx1, int idx2);
int read_from_file(int fd, struct commandinfo_t** infos, int* len, const struct commandinfo_t* command_info);
void handle_command(const struct commandinfo_t* command_info, const char* buf);

void handle_gtu_student_grades_command(const char* buf);
void print_greeding_message();
void print_newline();

int main(int argc, char* argv[]) {
    char buf[BUFFER_SIZE_LIMIT];

    int pid = 1;
    int bytes_read = 0;
    int is_done = 0;
    print_greeding_message();
    while (!is_done) {
        
        // Read command
        print_newline();
        bytes_read = HANDLE_EINTR_EAGAIN(read(0, (void*) buf, sizeof(char) * 5000)); 
        
        if (bytes_read < 0) {
            	char* err_msg = strerror(errno);
		int err_msg_len = strlen(err_msg);
		char msg[err_msg_len + 50];
		int bytes = sprintf(msg, "Error on getting input. Reason: %s \n", err_msg);
		msg[bytes] = '\0';
		HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
        }
        else {
            if (bytes_read > 0) {
            	buf[bytes_read - 1] = '\0'; // put '\0' instead '\n'
            }
            else {
            	buf[0] = '\0';
            }
        }
        
        if (strcmp("exit", buf) == 0) {
	   	char* msg = "Program is closing. Goodbye...\n"; 
		HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));	
	   	is_done = 1;
   	}
        else {
            // Parse command
            struct commandinfo_t command_info;
            memset(&command_info, 0, sizeof(command_info));
            if (parse(buf, &command_info) == -1) {
                char msg[BUFFER_SIZE_LIMIT + 100];
                int sprintf_writen = sprintf(msg, "Invalid command: %s \n", buf);
                msg[sprintf_writen] = '\0';
                HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
                log_to_file(msg);
            } 
            else {
            	if (command_info.command == GTU_STUDENT_GRADES) {
            		handle_gtu_student_grades_command(buf);
            	}
            	 
            	else {
            		// Fork to create process
		        pid = HANDLE_EINTR_EAGAIN(fork());
		        if (pid == -1) {
		        	char* msg = "Error in fork. Process cannot be processed for now. \n"; 
				HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
				log_from_command(buf, 0);
			 }
			else if (pid == 0) {
				// child process
				handle_command(&command_info, buf);
				exit(0);	
				is_done = 1;
			}
			else {
				// parent process
				int status;
				while(wait(&status) != -1 || errno != ECHILD) { }
				if (!WIFEXITED(status)) {
					char* msg = "Child process has not been exited normally. Command may not be successfully processed. \n"; 
					HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
					log_from_command(buf, 0);
				}
			}
		}
	     }
	}
    }
    return 0;
}

// Trim the given string by eliminating the leading and trailing spaces.
char* trim(char *string) {
    char *ptr = NULL;
    while (*string != '\0' && *string == ' ') {
    	string++; 
    }
    int len = strlen(string);
    if (len > 0) {
        ptr = string + len - 1;
        while (ptr != string && *ptr == ' ') { 
        	*ptr = '\0' ; 
        	ptr--; 
        } 
    }
    return string;  // return pointer to the modified start 
}

void my_strncpy(char * dest, const char* source, int n) {
	int i;
	for (i = 0; i < n && source[i] != '\0'; ++i) {
		dest[i] = source[i];
	}
}

int find_command(const char* buf, int* i, struct commandinfo_t* command_info) {
    int first_idx = *i;
    int after_last_idx = *i;
    while (buf[after_last_idx] != '\0' && buf[after_last_idx] != '"' && buf[after_last_idx] != ' ' && (after_last_idx - first_idx) < COMMAND_SIZE_LIMIT) {
        after_last_idx = after_last_idx + 1;
    }

    *i = after_last_idx;
    int n = after_last_idx - first_idx;
    if (n > COMMAND_SIZE_LIMIT) {
        return -1;
    }
    
    char command_str[n + 1];
    my_strncpy(command_str, &buf[first_idx], n);
    command_str[n] = '\0';

    command_info->command = -1;
    if (strcmp("gtuStudentGrades", command_str) == 0) {
        if (strlen(&buf[first_idx] + after_last_idx) == 0) {
            command_info->command = GTU_STUDENT_GRADES;
        }
        else {
            command_info->command = GTU_STUDENT_GRADES_WITH_TEXT;
        }
    }
    else if (strcmp("addStudentGrade", command_str) == 0) {
        command_info->command = ADD_STUDENT_GRADE;
    }
    else if (strcmp("searchStudent", command_str) == 0) {
        command_info->command = SEARCH_STUDENT;
    }
    else if (strcmp("sortAll", command_str) == 0) {
        command_info->command = SORT_ALL;
    }
    else if (strcmp("showAll", command_str) == 0) {
        command_info->command = SHOW_ALL;
    }
    else if (strcmp("listGrades", command_str) == 0) {
        command_info->command = LIST_GRADES;
    }
    else if (strcmp("listSome", command_str) == 0) {
        command_info->command = LIST_SOME;
    }
    
    return command_info->command;
}

int find_next_quote(const char* buf, const int* i) {
    int idx = *i;
    int is_found = 0;
    while (buf[idx] != '\0' && !is_found) {
        if (buf[idx] == '"') is_found = 1;
        else idx = idx + 1;
    }
    if (is_found) return idx;
    return -1;
}

int find_next_two_quote(const char* buf, int* i, int* first_quote, int* second_quote) {
    *first_quote = find_next_quote(buf, i);
    if (*first_quote == -1) {
        return -1;
    }
    
    *i = *first_quote + 1;
    *second_quote = find_next_quote(buf, i);
    if (*second_quote == -1) {
        return -1;
    }
    *i = *second_quote + 1;
    return 0;
}

int check_end_of_input(const char* buf, const int* i) {
    if (buf[*i] != '\0' && buf[*i] != '\n') {
        return -1;
    }
    return 0;
}

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

// Fill command_info struct according to the command entered by user
int fill_command_info(const char* buf, int* i, struct commandinfo_t* command_info) {
    int command = command_info->command;
    if (command == GTU_STUDENT_GRADES_WITH_TEXT || command == SORT_ALL || command == SHOW_ALL || command == LIST_GRADES) {
        int first_quote, second_quote;
        if (find_next_two_quote(buf, i, &first_quote, &second_quote) == -1) {
            return -1;
        }
        int n = second_quote - first_quote - 1;
        if (n > NAME_SIZE_LIMIT) {
            return -1;
        }
        my_strncpy(command_info->file_name, &buf[first_quote + 1], n);
        command_info->file_name[n] = '\0';
    }
    else if (command == ADD_STUDENT_GRADE || command == SEARCH_STUDENT) {	
        int first_quote, second_quote;
        if (find_next_two_quote(buf, i, &first_quote, &second_quote) == -1) {
            return -1;
        }
        int n = second_quote - first_quote - 1;
        if (n > NAME_SIZE_LIMIT) {
            return -1;
        }
        my_strncpy(command_info->name, &buf[first_quote + 1], n);
        command_info->name[n] = '\0';
    }

    if (command == GTU_STUDENT_GRADES_WITH_TEXT || command == SHOW_ALL || command == LIST_GRADES) {
        if (check_end_of_input(buf, i) == -1) {
            return -1;
        }
    }

    else if (command == ADD_STUDENT_GRADE) {
        int third_quote, fourth_quote;
        if (find_next_two_quote(buf, i, &third_quote, &fourth_quote) == -1) {
            return -1;
        }
        int n = fourth_quote - third_quote - 1;
        if (n > GRADE_SIZE_LIMIT) {
            return -1;
        }
        my_strncpy(command_info->grade, &buf[third_quote + 1], n);
        command_info->grade[n] = '\0';
    
    	int seventh_quote, eight_quote;
        if (find_next_two_quote(buf, i, &seventh_quote, &eight_quote) == -1) {
            return -1;
        }
        n = eight_quote - seventh_quote - 1;
        if (n > NAME_SIZE_LIMIT) {
            return -1;
        }
        if (check_end_of_input(buf, i) == -1) {
            return -1;
        }
        my_strncpy(command_info->file_name, &buf[seventh_quote + 1], n);
        command_info->file_name[n] = '\0';
    }
    
    else if (command == SEARCH_STUDENT) {
    	int seventh_quote, eight_quote;
        if (find_next_two_quote(buf, i, &seventh_quote, &eight_quote) == -1) {
            return -1;
        }
        int n = eight_quote - seventh_quote - 1;
        if (n > NAME_SIZE_LIMIT) {
            return -1;
        }
        if (check_end_of_input(buf, i) == -1) {
            return -1;
        }
        my_strncpy(command_info->file_name, &buf[seventh_quote + 1], n);
        command_info->file_name[n] = '\0';
    }
    
    else if (command == SORT_ALL) {
        char sort_by[SORT_BY_SIZE_LIMIT + 1];
        char sort_order[SORT_ORDER_SIZE_LIMIT + 1];

        int third_quote, fourth_quote;

        if (find_next_two_quote(buf, i, &third_quote, &fourth_quote) == -1) {
            // Default sorting
            command_info->sort_by = NAME;
            command_info->sort_order = ASC;
        }
        else {
            int n = fourth_quote - third_quote - 1;
            if (n > SORT_BY_SIZE_LIMIT) {
                return -1;
            }
            my_strncpy(sort_by, &buf[third_quote + 1], n);
            sort_by[n] = '\0';

            if (strcmp("name", sort_by) == 0) {
                command_info->sort_by = NAME;
            } else if (strcmp("grade", sort_by) == 0) {
                command_info->sort_by = GRADE;
            } else {
                return -1;
            }

            int fifth_quote, sixth_quote;
            if (find_next_two_quote(buf, i, &fifth_quote, &sixth_quote) == -1) {
                return -1;
            }
            n = sixth_quote - fifth_quote - 1;
            if (n > SORT_ORDER_SIZE_LIMIT) {
                return -1;
            }
            if (check_end_of_input(buf, i) == -1) {
                return -1;
            }
            my_strncpy(sort_order, &buf[fifth_quote + 1], n);
            sort_order[n] = '\0';

            if (strcmp("desc", sort_order) == 0) {
                command_info->sort_order = DESC;
            }
            else if (strcmp("asc", sort_order) == 0) {
                command_info->sort_order = ASC;
            }
            else {
                return -1;
            }
        }
    }
    else if (command == LIST_SOME) {
        char num_entries[NUM_ENTRIES_SIZE_LIMIT + 1];
        char page_number[PAGE_NUMBER_SIZE_LIMIT + 1];

        int third_quote, fourth_quote;
        if (find_next_two_quote(buf, i, &third_quote, &fourth_quote) == -1) {
            return -1;
        }
        int n = fourth_quote - third_quote - 1;
        if (n > NUM_ENTRIES_SIZE_LIMIT) {
            return -1;
        }
        my_strncpy(num_entries, &buf[third_quote + 1], n);
        num_entries[n] = '\0';

        if (convert_str_to_int(num_entries, &command_info->num_entries) == -1) {
            return -1;
        }
        if (command_info->num_entries < 0) {
        	return -1;
		}

        int fifth_quote, sixth_quote;
        if (find_next_two_quote(buf, i, &fifth_quote, &sixth_quote) == -1) {
            return -1;
        }
        n = sixth_quote - fifth_quote - 1;
        if (n > PAGE_NUMBER_SIZE_LIMIT) {
            return -1;
        }
        my_strncpy(page_number, &buf[fifth_quote + 1], n);
        page_number[n] = '\0';

        if (convert_str_to_int(page_number, &command_info->page_number) == -1) {
            return -1;
        }
        if (command_info->page_number <= 0) {
        	return -1;
		} 

        int seventh_quote, eight_quote;
        if (find_next_two_quote(buf, i, &seventh_quote, &eight_quote) == -1) {
            return -1;
        }
        n = eight_quote - seventh_quote - 1;
        if (n > NAME_SIZE_LIMIT) {
            return -1;
        }
        if (check_end_of_input(buf, i) == -1) {
            return -1;
        }
        my_strncpy(command_info->file_name, &buf[seventh_quote + 1], n);
        command_info->file_name[n] = '\0';
    }
    return 0;
}

// Parse the command entered by user
int parse(char* buf, struct commandinfo_t* command_info) {
    buf = trim(buf);

    int i = 0;
    if (find_command(buf, &i, command_info) == -1) {
        return -1;
    }
    
    if (fill_command_info(buf, &i, command_info) == -1) {
        return -1;
    }
    return 0;
}

// Close the given file
void close_file(int fd) {
	if (close(fd) < 0) {
		char* err_msg = strerror(errno);
		int err_msg_len = strlen(err_msg);
		char msg[err_msg_len + 50];
		int bytes = sprintf(msg, "Error on log file close. Reason: %s \n", err_msg);
		msg[bytes] = '\0';
		HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
	}
}

// Write the given message to the log file
void log_to_file(const char* msg) {
	int pid = HANDLE_EINTR_EAGAIN(fork());
	if (pid == -1) {
		char* msg = "Error in fork while logging. \n"; 
		HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
	 }
	else if (pid == 0) {
		// child process
		int fd = HANDLE_EINTR_EAGAIN(open(LOG_FILE_NAME, O_WRONLY | O_CREAT | O_APPEND, 0666));
		if (fd == -1) {
			char* err_msg = strerror(errno);
			int err_msg_len = strlen(err_msg);
			char msg[err_msg_len + 50];
			int bytes = sprintf(msg, "Log file cannot open. Reason: %s \n", err_msg);
			msg[bytes] = '\0';
			HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
			
			exit(0);
		}
		
		// Get lock
		struct flock lock;
		memset(&lock, 0, sizeof(lock));
		lock.l_type = F_WRLCK;
		if (HANDLE_EINTR_EAGAIN(fcntl(fd, F_SETLKW, &lock)) == -1) {
			char* err_msg = strerror(errno);
			int err_msg_len = strlen(err_msg);
			char msg[err_msg_len + 50];
			int bytes = sprintf(msg, "Error on getting log file lock. Reason: %s \n", err_msg);
			msg[bytes] = '\0';
			HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
			
			close_file(fd);
			exit(0);
		}
		
		if (HANDLE_EINTR_EAGAIN(write(fd, (void*) msg, sizeof(char) * strlen(msg))) == -1) {
			char* err_msg = strerror(errno);
			int err_msg_len = strlen(err_msg);
			char msg[err_msg_len + 50];
			int bytes = sprintf(msg, "Error on writing. Reason: %s \n", err_msg);
			msg[bytes] = '\0';
			HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
			
			close_file(fd);
			exit(0);	
		}
		
		// Release lock
		lock.l_type = F_UNLCK;
		if (HANDLE_EINTR_EAGAIN(fcntl(fd, F_SETLKW, &lock)) == -1) {
			char* err_msg = strerror(errno);
			int err_msg_len = strlen(err_msg);
			char msg[err_msg_len + 50];
			int bytes = sprintf(msg, "Error on releasing file lock. Reason: %s \n", err_msg);
			msg[bytes] = '\0';
			HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
			
			close_file(fd);
			exit(0);
		}
		
		close_file(fd);	
		exit(0);	
	}
	else {
		// parent process
		int status;
		while(wait(&status) != -1 || errno != ECHILD) { }
		if (!WIFEXITED(status)) {
			char* msg = "Log may not be written to the file successfully because the child process is not terminated normally. \n"; 
			HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
		}
	}
}

// Prepare msg and write to log file
void log_from_command(const char* buf, int is_success) {
	char msg[BUFFER_SIZE_LIMIT + 100];
	if (is_success) {
		int sprintf_writen = sprintf(msg, "[%s] command has been successfully processed\n", buf);
		msg[sprintf_writen] = '\0';
	}
	else {
		int sprintf_writen = sprintf(msg, "[%s] command cannot be processed\n", buf);
		msg[sprintf_writen] = '\0';
	}
	log_to_file(msg);
}

// Parse the record read from the file
struct commandinfo_t parse_record(const char* record, int record_len) {
	struct commandinfo_t info;
	int i;
	for (i = 0; record[i] != ','; ++i) { }
	
	my_strncpy(info.name, record, i);
	info.name[i] = '\0';
	
	int grade_n = record_len - i - 2; // one for ',' one for ' '
	my_strncpy(info.grade, &record[i + 2], grade_n);
	info.grade[grade_n] = '\0';
	return info;
}

// Add the given info to infos array dynamically
int add_dynamically(struct commandinfo_t* info, struct commandinfo_t** infos, int* len, int* size) {
	if (*len == *size) {
		// Realloc
		*size = *size * 2;
		struct commandinfo_t* infos_new = (struct commandinfo_t*) malloc(sizeof(struct commandinfo_t) * (*size));
		if (infos_new == NULL) {
			return -1;
		}
		int i;
		for (i = 0; i < *len; ++i) {
			infos_new[i] = (*infos)[i];
		}
		free(*infos);
		*infos = infos_new;
	}
	(*infos)[*len] = *info;
	*len = *len + 1;
	return 0;
}

void swap(struct commandinfo_t* infos, int idx1, int idx2) {
	struct commandinfo_t temp = infos[idx1];
	infos[idx1] = infos[idx2];
	infos[idx2] = temp;
}

int read_from_file(int fd, struct commandinfo_t** infos, int* len, const struct commandinfo_t* command_info) {
	char buffer[BUFFER_SIZE_LIMIT + 1];
	char record[BUFFER_SIZE_LIMIT + 1];
	int record_len = 0;
	
	// Set the record number interval according to the command
	int first_record;
	int last_record;
	int read_record_num = 0;
	if (command_info->command == LIST_SOME) {
		first_record = (command_info->page_number - 1) * command_info->num_entries + 1;
		last_record = command_info->page_number * command_info->num_entries;
	}
	else if (command_info->command == LIST_GRADES) {
		first_record = 1;
		last_record = 5;
	}
	
	int size = 10;
	*len = 0;
	*infos = (struct commandinfo_t*) malloc(sizeof(struct commandinfo_t) * size);
	if (infos == NULL) {
		return -1;
	}
	
	int is_done = 0;
	ssize_t bytes_read;
	do {
		// Read from file
		bytes_read = HANDLE_EINTR_EAGAIN(read(fd, (void*) buffer, BUFFER_SIZE_LIMIT));
		if (bytes_read < 0) {
			char* err_msg = strerror(errno);
			int err_msg_len = strlen(err_msg);
			char msg[err_msg_len + 50];
			int bytes = sprintf(msg, "Error on file read. Reason: %s \n", err_msg);
			msg[bytes] = '\0';
			HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
			
			return -1;
		}
		else if (bytes_read == 0) {
			is_done = 1;
		}
		else {
		    	int fi = 0;
			ssize_t i;
			for (i = 0; i < bytes_read && !is_done; i++) {

		        	if (buffer[i] == '\n') {
		      			int j;
					for (j = fi; j < i; ++j) {
						record[record_len] = buffer[j];
						++record_len;
					}
					struct commandinfo_t info = parse_record(record, record_len);  	
					++read_record_num;
					if (command_info->command == SEARCH_STUDENT) {
						if (strcmp(command_info->name, info.name) == 0) {
							if (add_dynamically(&info, infos, len, &size) == -1) {
								free(*infos);
								return -1;
							}	
							is_done = 1;
						}
					}
					else if (command_info->command == LIST_SOME || command_info->command == LIST_GRADES) {
						if (read_record_num >= first_record && read_record_num <= last_record) {
							if (add_dynamically(&info, infos, len, &size) == -1) {
								free(*infos);
								return -1;
							}
						} 
						if (read_record_num == last_record) {
							is_done = 1;
						}
					}
					else {
						if (add_dynamically(&info, infos, len, &size) == -1) {
								free(*infos);
								return -1;
						}
						if (command_info->command == SORT_ALL) {
							int is_sorted = 0;
							for (j = *len - 1; j > 0 && !is_sorted; --j) {
								int comp;
								if (command_info->sort_by == NAME) {
									comp = strcmp((*infos)[j - 1].name, (*infos)[j].name);
								}
								else {
									comp = strcmp((*infos)[j - 1].grade, (*infos)[j].grade);
								}
									
								if (command_info->sort_order == ASC) {
									if (comp <= 0) is_sorted = 1;
									else swap(*infos, j, j - 1);
								}
								else {
									if (comp >= 0) is_sorted = 1;
									else swap(*infos, j, j - 1);									
								}	
							}
						}
					} 
					record_len = 0;
					fi = i + 1;
				}
		    }
		    	int j;
			for (j = fi; j < bytes_read; ++j) {
				record[record_len] = buffer[j];
				++record_len;
			}	
		}
	} while(!is_done); 	
}

// Process the entered command
void handle_command(const struct commandinfo_t* command_info, const char* buf) {
	if (command_info->command == GTU_STUDENT_GRADES_WITH_TEXT) {	
		int fd = HANDLE_EINTR_EAGAIN(open(command_info->file_name, O_RDWR | O_CREAT, 0666));
		if (fd == -1) {
			char* err_msg = strerror(errno);
			int err_msg_len = strlen(err_msg);
			char msg[err_msg_len + 50];
			int bytes = sprintf(msg, "File cannot be created. Reason: %s \n", err_msg);
			msg[bytes] = '\0';
			HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
			
			log_from_command(buf, 0);
			return;
		}
		close_file(fd);
	}
	else {
		int fd = HANDLE_EINTR_EAGAIN(open(command_info->file_name, O_RDWR | O_APPEND));
		if (fd == -1) {
			char* err_msg = strerror(errno);
			int err_msg_len = strlen(err_msg);
			char msg[err_msg_len + 50];
			int bytes = sprintf(msg, "File cannot open. Reason: %s \n", err_msg);
			msg[bytes] = '\0';
			HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));	
			
			log_from_command(buf, 0);
			return;
		}
		
		// Get lock
		struct flock lock;
		memset(&lock, 0, sizeof(lock));
		lock.l_type = F_WRLCK;
		if (HANDLE_EINTR_EAGAIN(fcntl(fd, F_SETLKW, &lock)) == -1) {
			char* err_msg = strerror(errno);
			int err_msg_len = strlen(err_msg);
			char msg[err_msg_len + 50];
			int bytes = sprintf(msg, "Error on getting lock. Reason: %s \n", err_msg);
			msg[bytes] = '\0';
			HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
			
			close_file(fd);
			log_from_command(buf, 0);
			return;
		}
	
		if (command_info->command == ADD_STUDENT_GRADE) {
			int name_len = strlen(command_info->name);
			int grade_len = strlen(command_info->grade);
			int n = name_len + grade_len + 3; // one for ',' one for ' ', one for '\n'
			char record[n + 1]; // one for EOF
			my_strncpy(record, command_info->name, name_len);
			record[name_len] = ','; // Add comma separator
			record[name_len + 1] = ' '; // Add space after comma
			my_strncpy(&record[name_len + 2], command_info->grade, grade_len);
			record[n - 1] = '\n';
			record[n] = '\0';	
			if (HANDLE_EINTR_EAGAIN(write(fd, (void*) record, sizeof(char) * n)) == -1) {
				char* err_msg = strerror(errno);
				int err_msg_len = strlen(err_msg);
				char msg[err_msg_len + 50];
				int bytes = sprintf(msg, "Error on file write. Reason: %s \n", err_msg);
				msg[bytes] = '\0';
				HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
				
				close_file(fd);
				log_from_command(buf, 0);
				return;	
			}
		}	
		
		else if (command_info->command == SEARCH_STUDENT) {
			struct commandinfo_t* data;
			int len;
			if (read_from_file(fd, &data, &len, command_info) == -1) {
				char* err_msg = strerror(errno);
				int err_msg_len = strlen(err_msg);
				char msg[err_msg_len + 50];
				int bytes = sprintf(msg, "Error on file read. Reason: %s \n", err_msg);
				msg[bytes] = '\0';
				HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
				
				close_file(fd);
				log_from_command(buf, 0);
				return;
			}
			if (len == 0) {
				char* msg = "Student not found. \n"; 
				HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));	
			}
			else {
				char msg[NAME_SIZE_LIMIT + GRADE_SIZE_LIMIT + 10];
				int sprintf_writen = sprintf(msg, "%s %s \n", data[0].name, data[0].grade);
				msg[sprintf_writen] = '\0';
				HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
			}
			free(data);
		}
		
		else {
			struct commandinfo_t* data;
			int len;
			if (read_from_file(fd, &data, &len, command_info) == -1) {
				char* err_msg = strerror(errno);
				int err_msg_len = strlen(err_msg);
				char msg[err_msg_len + 50];
				int bytes = sprintf(msg, "Error on file read. Reason: %s \n", err_msg);
				msg[bytes] = '\0';
				HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
				
				close_file(fd);
				log_from_command(buf, 0);
				return;
			}
			int i;
			for (i = 0; i < len; ++i) {
				char msg[NAME_SIZE_LIMIT + GRADE_SIZE_LIMIT + 10];
				int sprintf_writen = sprintf(msg, "%s %s \n", data[i].name, data[i].grade);
				msg[sprintf_writen] = '\0';
				HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
			}
			free(data);
		}	
		
		// Release lock
		lock.l_type = F_UNLCK;
		if (HANDLE_EINTR_EAGAIN(fcntl(fd, F_SETLKW, &lock)) == -1) {
			char* err_msg = strerror(errno);
			int err_msg_len = strlen(err_msg);
			char msg[err_msg_len + 50];
			int bytes = sprintf(msg, "Error on releasing file lock. Reason: %s \n", err_msg);
			msg[bytes] = '\0';
			HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
			
			close_file(fd);
			return;
		}
		close_file(fd);
	}
	log_from_command(buf, 1);
}

void handle_gtu_student_grades_command(const char* buf) {
	char* msg = "########################################### \n"; 
	HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));

	msg = "WELCOME TO STUDENT GRADE MANAGEMENT SYSTEM \n";
	HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));

	msg = "########################################### \n\n";
	HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));

	msg = "Here is the list of commands you can execute: \n\n";
	HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));

	msg = "1- gtuStudentGrades \"filename.txt\": Creates a file with given filename. If file alreadys exists, it does not change its content \n\n";
	HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));

	msg = "2- addStudentGrade \"Name Surname\" \"AA\" \"filename.txt\": Adds the given entry to the given file. If file does not exists, then it prints error \n\n";
	HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));

	msg = "3- searchStudent \"Name Surname\" \"filename.txt\": Searchs for given student name in the given file and prints the name and grade if it is found. If it is not found, then it prints error \n\n";
	HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));

	msg = "4- sortAll \"filename.txt\" [optional] \"name/grade\" \"asc/desc\" : Sorts the entries in the file and prints them. By default it sorts them by name in ascending order. The last two arguments are optional. You can specify the sort by field by giving name or grade and sort order by giving asc or desc. If you enter one optional argument then you have to enter the other one, otherwise it does not accept \n\n";
	HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));

	msg = "5- showAll \"filename.txt\": Prints all of the entries in the file \n\n";
	HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));

	msg = "6- listGrades \"filename.txt\": Prints first 5 entries  \n\n";
	HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));

	msg = "7- listSome \"numofEntries\" \"pageNumber\" \"filename.txt\": Prints the selected entries according to the numofEntries and pageNumber arguments. Required conditions: numofEntries >= 0 and pageNumber > 0 \n\n";
	HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));

	msg = "8- exit: Closes the program \n\n";
	HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
	
	log_from_command(buf, 1);
}

void print_greeding_message() {
	char* msg = "Welcome! You can start entering commands. You can enter 'exit' to exit \n"; 
	HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
}

void print_newline() {
	char* msg = "\n>"; 
	HANDLE_EINTR_EAGAIN(write(1, (void*) msg, sizeof(char) * strlen(msg)));
}
