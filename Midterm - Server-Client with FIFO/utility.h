#define BASE_DIR "/tmp"
#define SERVER_BASE_PATH "/tmp/server_"
#define CLIENT_REQ_BASE_PATH "/tmp/client_req_"
#define CLIENT_RES_BASE_PATH "/tmp/client_res_"
#define ALL_PERMISSIONS 0777

#define STDIN_FD 0
#define STDOUT_FD 1

#define BUF_INIT_SIZE 1024
#define PID_STR_SIZE 64
#define FIFO_PATH_SIZE 128
#define COMMAND_MAX_SIZE 5096

#define COMPLETED 1
#define NOT_COMPLETED_YET 0

#define HELP 500
#define LIST 501
#define READF 502
#define WRITET 503
#define UPLOAD 504
#define DOWNLOAD 505
#define ARCHSERVER 506
#define KILLSERVER 507
#define QUIT 508
#define UNKNOWN 509

#define CONNECT 600
#define TRY_CONNECT 601

#define QUEUE_FULL 602
#define QUEUE_WAITING 603
#define CLIENT_CONNECTED 604

#define ERR_FILE_EXISTS 700
#define ERR_FILE_NOT_EXISTS 701
#define TRANSMISSION_STARTED 702

struct metadata_t {
	int len;
	int is_sent_completely;
};

struct user_request_t {
	int command; // One of the commands defined above
	int help_command; // One of the commands defined above
	int line;
	char file_name[COMMAND_MAX_SIZE];
	char str[COMMAND_MAX_SIZE];
};

struct connect_request_t {
	int client_pid; 
	int connect_type; // CONNECT or TRY_CONNECT
};

// Convert string to integer
int convert_str_to_int(const char* str, int* num);
int get_command_from_str(const char* token);

int is_command_without_file(int command);
int is_command_without_line(int command);

int lock_file(const char* file_name, struct flock* lock, int flags);
int unlock_file(int* fd_lock_file, struct flock* lock);

int parse_user_request(char* command, struct user_request_t* userrequest);

int is_errno_eintr_or_eagain();

void unlink_file_if_possible(int* fd, const char* file_path);

void close_file_if_possible(int* fd);

void send_sigterm_if_possible(int pid);

void sigpipe_handler(int unused);

int set_sigpipe_to_ignore();

void signal_and_wait_all_children(pid_t* child_pids, int child_counter);

int open_file(const char* file_path, int flags, int mode);
int open_fifo(const char* fifo_path, int flags);

int check_if_file_exists(const char* file_path);

int read_fifo(int fifo_fd, void* buf, size_t size);

int read_fifo_retry_if_eof(int fifo_fd, void* buf, size_t size);

int write_file(int fifo_fd, const void* buf, size_t size);
int write_fifo(int fifo_fd, const void* buf, size_t size);

int create_metadata_and_sent_to_fifo(int len, int is_sent_compeletely, int fifo_fd);

int write_file_until_size(int fifo_fd, const char* buf, size_t size);
int write_fifo_until_size(int fifo_fd, const char* buf, size_t size);

int read_file_until_size(int fifo_fd, char* buf, size_t size);
int read_fifo_until_size(int fifo_fd, char* buf, size_t size);

int send_metadata_and_write_until_size(int fifo_fd, const char* buf, size_t size, int completed);

int read_metadata_and_read_until_size(int fifo_fd, char* buf);

