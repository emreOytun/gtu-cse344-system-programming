#include "utils.h"
#include <arpa/inet.h>
#include <sys/types.h>
#include <time.h>

int client_socket;

volatile int cancel_signal = 0;


void ignore_signal(int signal) { 
	if (signal) {}
}

int set_sigterm_sigint_handler_as_ignore() {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &ignore_signal;
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

void handle_sigterm_sigint(int signal) {	
	if (signal) {}
	cancel_signal = 1;
	
	char buffer[5] = "ACK";
	if (write_to_socket(client_socket, buffer, strlen(buffer) + 1) == -1) {
   		perror("Failed to send order");
		close(client_socket); // TODO: Client alacak close yerine
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


void error(const char *msg) {
    perror(msg);
    exit(1);
}

// Function to generate a random float within a specified range
float random_float(float min, float max) {
    return min + ((float)rand() / RAND_MAX) * (max - min);
}

int main(int argc, char *argv[]) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s [server_ip] [portnumber] [numberOfClients] [p] [q]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

	char ip_server[100];
	strcpy(ip_server, argv[1]);

    int server_port = atoi(argv[2]);
    int numberOfClients = atoi(argv[3]);
    float p = atof(argv[4]);
    float q = atof(argv[5]);
    
    struct sockaddr_in server_addr;
    
    // Create socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("Socket creation failed");
        exit(-1);
    }

    // Define server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(ip_server);

    // Connect to the server
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to the server failed");
        close(client_socket);
        return -1;
    }

	// Send number of clients
	char num_clients_str[NUM_CLIENTS_MAX_CHAR_SIZE + 1];
	int_to_str(numberOfClients, num_clients_str);
	
	if (write_to_socket(client_socket, num_clients_str, strlen(num_clients_str) + 1) == -1) {
   		perror("Failed to send order");
		close(client_socket);
		return -1;		
	}

    // Get PID of the client
    pid_t pid = getpid();

	char pid_str[PID_MAX_CHAR_SIZE];
	int_to_str(pid, pid_str);

	char order_buffer[PID_MAX_CHAR_SIZE + COORDINATE_MAX_CHAR_SIZE * 2 + 1];

	set_sigterm_sigint_handler_as_ignore();

    // Prepare message with PID and order details
	srand(time(NULL));
    int i = 0;
    for (i = 0; !cancel_signal && i < numberOfClients; ++i) {
    	float order_x = random_float(-p / 2.0, p / 2.0);
    	float order_y = random_float(-q / 2.0, q / 2.0);
    	
    	char order_x_str[COORDINATE_MAX_CHAR_SIZE + 1];
    	int_to_str(order_x, order_x_str);
    	
    	char order_y_str[COORDINATE_MAX_CHAR_SIZE + 1];
    	int_to_str(order_y, order_y_str);
    		
    	strcpy(order_buffer, pid_str);
		strcat(order_buffer, "/");
		strcat(order_buffer, order_x_str);
		strcat(order_buffer, "/");
		strcat(order_buffer, order_y_str);
		order_buffer[strlen(order_buffer)] = '\0';	
  		
		// Send the message to the server
		
		if (write_to_socket(client_socket, order_buffer, strlen(order_buffer) + 1) == -1) {
		    perror("Failed to send order");
		    cancel_signal = 1;
		}
    }
    
    set_sigterm_sigint_handler();
    
    char buffer[BUFFER_SIZE];
    int is_done = 0;
    while (!is_done) {
		if (read_from_socket(client_socket, buffer) == -1) {
		    error("Failed to receive confirmation");
		    close(client_socket);
		    return -1;
		}
		if (strcmp(buffer, "ACK") == 0) is_done = 1;
		else printf("%s", buffer);
    }
  
    // Close the socket
    close(client_socket);

    return 0;
}
