#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <time.h>

typedef struct {
    int source_fd;
    int destination_fd;
} Task;


pthread_mutex_t buffer_mutex;
pthread_mutex_t print_mutex;
pthread_mutex_t stats_mutex;

Task* tasks;
int buf_size;
int buf_tail;
int buf_head;
int buf_length;
pthread_cond_t empty;
pthread_cond_t full;

volatile int running = 1;
volatile int force_quit = 0;

pthread_mutex_t running_mutex;
pthread_mutex_t force_quit_mutex;

int num_workers;

pthread_barrier_t barrier;
volatile int next_phase = 1;
volatile int next_phase_counter = 0;
pthread_mutex_t next_phase_mutex;

// Given source and destination directories
char src_dir[PATH_MAX];
char dest_dir[PATH_MAX];

// Statistics variables
int num_regular_files = 0;
int num_fifo_files = 0;
int num_directories = 0;
long total_bytes_copied = 0;
struct timespec start_time, end_time;

int is_running() {
	int res = -1;
	pthread_mutex_lock(&running_mutex);
    res = running;
    pthread_mutex_unlock(&running_mutex);
	return res;
}

int is_force_quit() {
	int res = -1;
	pthread_mutex_lock(&force_quit_mutex);
    res = force_quit;
    pthread_mutex_unlock(&force_quit_mutex);
	return res;
}

int check_if_subdirectory(const char* parent, const char* child) {
    size_t len = strlen(parent);
    return strncmp(parent, child, len) == 0 && (child[len] == '/' || child[len] == '\0');
}

int convert_str_to_int(const char* str, int* num) {
    char* end;
    const long l = strtol(str, &end, 10);
    if (end == str) {
    	*num = -1;
    	fprintf(stderr, "Invalid input: Not a deciml number \n");
        return -1;  // not a decimal number
    }
     else if (*end != '\0' && *end != '\n' && *end != ' ' && *end != '\t') {
        *num = -1;
    	fprintf(stderr, "Invalid input: Extra characters after numerical value \n");
        return -1;  // there are some extra characters after number
    }
    else if (l > INT_MAX) {
        *num = -1;
    	fprintf(stderr, "Invalid input: Exceeds INT_MAX \n");
        return -1;
    }
    else if (l < INT_MIN) {
        *num = -1;
    	fprintf(stderr, "Invalid input: Exceeds INT_MIN \n");
        return -1;
    }
    *num = (int) l;
    return 0;
}

void buffer_add(Task task) { 
  	pthread_mutex_lock(&buffer_mutex);
    
    while (buf_length == buf_size && is_running() && !is_force_quit()) {
        pthread_cond_wait(&empty, &buffer_mutex);
    }
      
    if (!is_running() || is_force_quit()) {
    	if (task.source_fd != -1) close(task.source_fd);
    	if (task.destination_fd != -1) close(task.destination_fd);
    
        pthread_mutex_unlock(&buffer_mutex);
        return;
    }

    tasks[buf_tail] = task;
    buf_tail = (buf_tail + 1) % buf_size;
    buf_length++;

	pthread_cond_signal(&full);
    pthread_mutex_unlock(&buffer_mutex);
}

Task buffer_remove() {
 	pthread_mutex_lock(&buffer_mutex);
         
    while (buf_length == 0 && is_running() && !is_force_quit()) {
        pthread_cond_wait(&full, &buffer_mutex);
    }
         
	if (is_force_quit() || (!is_running() && buf_length == 0)) {
		
		while (buf_length != 0) {
			Task task = tasks[buf_head];
			buf_head = (buf_head + 1) % buf_size;
			buf_length--;
			
			if (task.source_fd != -1) close(task.source_fd);
			if (task.destination_fd != -1) close(task.destination_fd);
		}
	
        pthread_mutex_unlock(&buffer_mutex);
        Task empty_task;
        empty_task.source_fd = -1;
        empty_task.destination_fd = -1;
        return empty_task;
    }

    Task task = tasks[buf_head];
    buf_head = (buf_head + 1) % buf_size;
    buf_length--;

	pthread_cond_signal(&empty);
    pthread_mutex_unlock(&buffer_mutex);

    return task;
}

int is_errno_eintr_or_eagain() {
	if (errno == EINTR || errno == EAGAIN) {
		return 1;
	}
	return 0;
}

int write_file_until_buf_size(int fifo_fd, const char* buf, ssize_t buf_size) {
	long total_bytes_written = 0;
	if (buf_size > 0) {
		// Write until all data is written
		while(total_bytes_written != buf_size) {
			int write_bytes = write(fifo_fd, (void*) &buf[total_bytes_written], buf_size - total_bytes_written);
			if (write_bytes == -1) {
				if (!is_errno_eintr_or_eagain()) return -1;			
			} 
			else total_bytes_written += write_bytes;
		}
	}
	return total_bytes_written;
}

void copy_task_file(Task task) {
    char buffer[1024];
    ssize_t read_bytes;
    ssize_t written_bytes;
    long total_bytes = 0;
    int is_done = 0;
    int is_error = 0;
    while (!is_done) {
    	read_bytes = read(task.source_fd, buffer, 1024);
    	if (read_bytes > 0) {
		    written_bytes = write_file_until_buf_size(task.destination_fd, buffer, read_bytes);
		    if (written_bytes == -1) {
		        pthread_mutex_lock(&print_mutex);
		        perror("Error writing to destination file");
		        pthread_mutex_unlock(&print_mutex);
		        
		        is_error = 1;
		        is_done = 1;
		    }
		    else {
		    	total_bytes += written_bytes;
		    }
        }
        else is_done = 1;
    }

    if (read_bytes == -1 || is_error) {
        pthread_mutex_lock(&print_mutex);
        perror("Error reading from source file");
        pthread_mutex_unlock(&print_mutex);
    }
    
    close(task.source_fd);
    close(task.destination_fd);
    
    // Update statistics
    pthread_mutex_lock(&stats_mutex);
    total_bytes_copied += total_bytes;
    pthread_mutex_unlock(&stats_mutex);
}

void copy_directory_files(const char* src_path, const char* dest_path) {
    struct dirent* file;
    DIR* dir = opendir(src_path);
    if (dir == NULL) {
        pthread_mutex_lock(&print_mutex);
        perror("Error opening directory");
        pthread_mutex_unlock(&print_mutex);
        return;
    }

    pthread_mutex_lock(&stats_mutex);
	num_directories++;
    pthread_mutex_unlock(&stats_mutex);

    while (!is_force_quit() && (file = readdir(dir)) != NULL) {
        char src_file_path[PATH_MAX];
        char dest_file_path[PATH_MAX];
        snprintf(src_file_path, PATH_MAX, "%s/%s", src_path, file->d_name);
        snprintf(dest_file_path, PATH_MAX, "%s/%s", dest_path, file->d_name);
        
        if (strcmp(file->d_name, "..") == 0) continue;
        if (strcmp(file->d_name, ".") == 0) continue;
        
        struct stat file_stat;
        int stat_res = stat(src_file_path, &file_stat); 
        if (stat_res == -1) {
            pthread_mutex_lock(&print_mutex);	
            perror("Error getting file status");
            pthread_mutex_unlock(&print_mutex);
            continue;
        }

        if (S_ISREG(file_stat.st_mode)) {
            int source_fd = open(src_file_path, O_RDONLY);
            if (source_fd == -1) {
                pthread_mutex_lock(&print_mutex);
                perror("Error opening source file");
                pthread_mutex_unlock(&print_mutex);
                continue;
            }

            int destination_fd = open(dest_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (destination_fd == -1) {
                pthread_mutex_lock(&print_mutex);
                perror("Error opening destination file");
                pthread_mutex_unlock(&print_mutex);
                close(source_fd);
                continue;
            }
            
            // Increment num_regular_files
            pthread_mutex_lock(&stats_mutex);
            num_regular_files++;
            pthread_mutex_unlock(&stats_mutex);

            Task task;
            task.source_fd = source_fd;
            task.destination_fd = destination_fd;

            buffer_add(task);
        }
        else if (S_ISDIR(file_stat.st_mode)) {
        	int mkdir_res = mkdir(dest_file_path, file_stat.st_mode); 
            if (mkdir_res == -1 && errno != EEXIST) {
                pthread_mutex_lock(&print_mutex);
                perror("Error creating directory");
                pthread_mutex_unlock(&print_mutex);
            }
            else {
            	copy_directory_files(src_file_path, dest_file_path);
            }
        } 
        else if (S_ISFIFO(file_stat.st_mode)) {        
        	int source_fd = open(src_file_path, O_RDONLY | O_NONBLOCK);
            if (source_fd == -1) {
                pthread_mutex_lock(&print_mutex);
                perror("Error opening source file");
                pthread_mutex_unlock(&print_mutex);
                
                continue;
            }

			// Remove the existing FIFO if it exists
            unlink(dest_file_path);

			// Create fifo
			int mkfifo_res = mkfifo(dest_file_path, 0666); 
			if (mkfifo_res == -1 && errno != EEXIST) {	
				pthread_mutex_lock(&print_mutex);
                perror("Error opening source file");
                pthread_mutex_unlock(&print_mutex);
                
                close(source_fd);
                continue;
			}
            int destination_fd = open(dest_file_path, O_RDWR);
            if (destination_fd == -1) {
                pthread_mutex_lock(&print_mutex);
                perror("Error opening destination file");
                pthread_mutex_unlock(&print_mutex);
                
                close(source_fd);
                continue;
            }
            
            // Increment num_regular_files
            pthread_mutex_lock(&stats_mutex);
            num_fifo_files++;
            pthread_mutex_unlock(&stats_mutex);
            
            Task task;
            task.source_fd = source_fd;
            task.destination_fd = destination_fd;

            buffer_add(task);
        }
    }

    closedir(dir);
}

void* manager(void* arg) {
    copy_directory_files(src_dir, dest_dir);

    pthread_mutex_lock(&running_mutex);
    running = 0;	
    pthread_mutex_unlock(&running_mutex);
       
   	pthread_mutex_lock(&buffer_mutex);
    pthread_cond_broadcast(&full);
    pthread_cond_broadcast(&empty);
    pthread_mutex_unlock(&buffer_mutex); 
   
	return NULL;
}

void* worker(void* arg) {
    while (next_phase) {
    	Task task = buffer_remove();
        if (task.source_fd != -1 && task.destination_fd != -1) {
            copy_task_file(task);
        }
        
        pthread_mutex_lock(&next_phase_mutex);
        ++next_phase_counter;
        if (next_phase_counter == num_workers) { // If this is the last thread that goes into barrier_wait
        	next_phase_counter = 0;
        	if (!is_force_quit() && (is_running() || buf_length != 0)) next_phase = 1;
        	else next_phase = 0;	
        }
        pthread_mutex_unlock(&next_phase_mutex);
        
        pthread_barrier_wait(&barrier);
    }
    return NULL;
}

void handle_sigint_sigterm(int signal) {
	printf("Signal received. Program is closing... \n");
   
   	pthread_mutex_lock(&force_quit_mutex);
    force_quit = 1;
    pthread_mutex_unlock(&force_quit_mutex);
   
    pthread_mutex_lock(&buffer_mutex);
    pthread_cond_broadcast(&empty); // Broadcast to prevent manager getting stuck on waiting
    pthread_mutex_unlock(&buffer_mutex);
    
    return;
}

int set_sigterm_sigint_handler() {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &handle_sigint_sigterm;
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

int main(int argc, char* argv[]) {
    set_sigterm_sigint_handler();

    if (argc != 5) {
    	fprintf(stderr, "Usage: ./program <buffer_buf_size> <num_workers> <source_directory> <destination_directory> \n");
        exit(EXIT_FAILURE);
    }

    convert_str_to_int(argv[1], &buf_size);
    if (buf_size == -1) exit(EXIT_FAILURE);
    
    convert_str_to_int(argv[2], &num_workers);
	if (num_workers == -1) exit(EXIT_FAILURE);

    // Check src_dir present
    if (realpath(argv[3], src_dir) == NULL) {
        pthread_mutex_lock(&print_mutex);
        perror("Error resolving source directory");
        pthread_mutex_unlock(&print_mutex);
        exit(EXIT_FAILURE);
    }
    // Check if the destination directory is present
    if (realpath(argv[4], dest_dir) == NULL) {
        if (errno == ENOENT) {
            // Destination directory does not exist, attempt to create it
            if (mkdir(argv[4], 0777) == -1) {
                pthread_mutex_lock(&print_mutex);
                perror("Error creating destination directory");
                pthread_mutex_unlock(&print_mutex);
                exit(EXIT_FAILURE);
            }

            // Check the newly created directory
            if (realpath(argv[4], dest_dir) == NULL) {
                pthread_mutex_lock(&print_mutex);
                perror("Error resolving newly created destination directory");
                pthread_mutex_unlock(&print_mutex);
                exit(EXIT_FAILURE);
            }
        } 
        else {
            pthread_mutex_lock(&print_mutex);
            perror("Error resolving destination directory");
            pthread_mutex_unlock(&print_mutex);
            exit(EXIT_FAILURE);
        }
    }

    // Check if source and destination directories are valid
    if (check_if_subdirectory(src_dir, dest_dir)) {
        pthread_mutex_lock(&print_mutex);
        fprintf(stderr, "Error: Destination directory cannot be a subdirectory of source directory.\n");
        pthread_mutex_unlock(&print_mutex);
        exit(EXIT_FAILURE);
    }
    if (check_if_subdirectory(dest_dir, src_dir)) {
        pthread_mutex_lock(&print_mutex);
        fprintf(stderr, "Error: Source directory cannot be a subdirectory of destination directory.\n");
        pthread_mutex_unlock(&print_mutex);
        exit(EXIT_FAILURE);
    }

    if (buf_size <= 0) {
        fprintf(stderr, "Error: Buffer buf_size must be greater than 0\n");
        exit(EXIT_FAILURE);
    }

    if (num_workers <= 0) {
        fprintf(stderr, "Error: Number of workers must be greater than 0\n");
        exit(EXIT_FAILURE);
    }
    
    tasks = malloc(buf_size * sizeof(Task));
    buf_tail = 0;
    buf_head = 0;
    buf_length = 0;
    pthread_mutex_init(&buffer_mutex, NULL);
    pthread_mutex_init(&running_mutex, NULL);
    pthread_mutex_init(&force_quit_mutex, NULL);
    
    pthread_cond_init(&empty, NULL);
    pthread_cond_init(&full, NULL);
	pthread_barrier_init(&barrier, NULL, num_workers);
    
	pthread_mutex_init(&print_mutex, NULL);
	pthread_mutex_init(&stats_mutex, NULL);
    
	// Initialize start time
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    pthread_t manager_thread;
    pthread_create(&manager_thread, NULL, manager, NULL);
	
    pthread_t* workers = malloc(num_workers * sizeof(pthread_t));
    
    for (int i = 0; i < num_workers; i++) {
        pthread_create(&workers[i], NULL, worker, NULL);
    }

    pthread_join(manager_thread, NULL);

    for (int i = 0; i < num_workers; i++) {
        pthread_join(workers[i], NULL); 
    }
	
    // Calculate end time
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    // Print statistics
    printf("\n---------------STATISTICS--------------------\n");
    printf("Consumers: %d - Buffer buf_size: %d\n", num_workers, buf_size);
    printf("Number of Regular File: %d\n", num_regular_files);
    printf("Number of FIFO File: %d\n", num_fifo_files);
    printf("Number of Directory: %d\n", num_directories);
    printf("TOTAL BYTES COPIED: %ld\n", total_bytes_copied);

    // Calculate total time
    long seconds = end_time.tv_sec - start_time.tv_sec;
    long nanoseconds = end_time.tv_nsec - start_time.tv_nsec;
    if (nanoseconds < 0) {
        seconds -= 1;
        nanoseconds += 1000000000;
    }
    long milliseconds = nanoseconds / 1000000;
    long minutes = seconds / 60;
    seconds = seconds % 60;

    printf("TOTAL TIME: %02ld:%02ld.%03ld (min:sec.mili)\n", minutes, seconds, milliseconds);
    
    while (buf_length != 0) {
		Task task = tasks[buf_head];
		buf_head = (buf_head + 1) % buf_size;
		buf_length--;
		
		if (task.source_fd != -1) close(task.source_fd);
		if (task.destination_fd != -1) close(task.destination_fd);
	}
	
	free(workers);
    free(tasks);
    
    pthread_mutex_destroy(&buffer_mutex);
    pthread_mutex_destroy(&running_mutex);
    pthread_mutex_destroy(&force_quit_mutex);
   
   	pthread_cond_destroy(&empty);
    pthread_cond_destroy(&full);
	pthread_barrier_destroy(&barrier);
   
    pthread_mutex_destroy(&print_mutex);
    pthread_mutex_destroy(&stats_mutex);

    return 0;
}
