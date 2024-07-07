#include "utils.h"
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>	
#include <semaphore.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <complex.h>

#define MAX_OVEN_CAPACITY 6
#define MAX_OVEN_APPARATUS 3
#define MAX_DELIVERY_BAG 3

#define PROCESSING 0
#define CANCELLING 1
#define CANCELLED 2
#define DELIVERED 3

#define IN_ORDERS_NEW_QUEUE 0
#define IN_COOK 1
#define IN_OVEN_OPENING_QUEUE 2
#define IN_OVEN_OPENING 3
#define IN_COOKING_QUEUE 4
#define IN_COOKING 5
#define IN_DELIVERY_WAITING_QUEUE 6
#define IN_DELIVERY_PERSON 7

#define NO_MUTEX 0
#define YES_MUTEX 1

#define NO_PRINTF 0
#define YES_PRINTF 1 

volatile int server_wake_up = 0;

struct Cook {
	int id;
	volatile int total_tasks_done;
	
	volatile int is_stopped;

	pthread_mutex_t mutex;
	
	volatile int oven_waiting_flag;
	sem_t oven_waiting_sem;
};

struct DelPerson {
	int id;
	volatile int total_tasks_done;
	
	pthread_mutex_t mutex;

	struct Order* orders_in_bag[MAX_DELIVERY_BAG];
	int in_bag_len;
};

struct Order {
	int id;
	int pid;
	int socket;
	float x;
	float y;
		
	struct Cook* cook;
	struct OvenSlot* ovenSlot;
	struct DelPerson* delPerson;
};

struct OvenSlot {
	int id;

	volatile int is_stopped;	
	volatile int is_finished;
};

struct OvenOpening {
	volatile int is_stopped;
};

int num_clients = 0;
pthread_mutex_t num_clients_mutex;
sem_t num_clients_sem;

sem_t day_completed_sem; // Manager is waiting for that after taking the orders

int total_cook_num = 0; // Total cook number given as parameter
int total_delivery_person_num = 0; // Total delivery person number given as parameter
float delivery_speed;

struct Order* orders = NULL; // Orders taken from client
int total_cur_orders = 0;

struct Cook* cooks = NULL;

struct DelPerson* delPersons = NULL;

struct OvenSlot* ovenSlots = NULL;

struct OvenOpening* ovenOpening = NULL;

struct Order** orders_new = NULL; // Cooks will take
volatile int orders_new_len = 0;
pthread_mutex_t orders_new_mutex;
sem_t orders_new_full_sem;

struct Order** orders_oven_waiting = NULL; // Oven will take
int oven_waiting_len = 0;
pthread_mutex_t oven_waiting_mutex;
sem_t oven_waiting_sem;

struct Order** orders_cooking = NULL;
int orders_cooking_len = 0;
pthread_mutex_t orders_cooking_mutex;
sem_t orders_cooking_full_sem;
sem_t orders_cooking_empty_sem;

struct Order** orders_delivery_waiting = NULL; // Delivery persons will take
int delivery_waiting_len = 0;
pthread_mutex_t delivery_waiting_mutex;
sem_t delivery_waiting_full_sem;

pthread_mutex_t manager_mutex;
sem_t pide_apparatus_sem;

volatile int num_threads_in_barrier = 0;
volatile int wait_in_barrier = 0; 
pthread_mutex_t num_threads_in_barrier_mutex;
pthread_barrier_t delPersons_barrier;

volatile int is_stopped = 0;
volatile int is_stopped_ser = 0;

volatile int is_delivery_stopped = 0;
volatile int is_cancelled = 0;
volatile int delPersons_can_stop = 0;

int server_socket, client_socket;
int client_pid = 0;
char client_pid_str[20];
int client_port = 0;
char client_ip[25];

pthread_t* cook_threads = NULL;
pthread_t* delPerson_threads = NULL;
pthread_t* oven_opening_thr = NULL;
pthread_t* oven_slot_threads = NULL;

pthread_mutex_t log_mutex;

volatile int server_in_accept = 0;

double cook_sleep_time = 0.0f;

void create_matrix(double complex matrix[30][40]) {
    for (int i = 0; i < 30; i++) {
        for (int j = 0; j < 40; j++) {
            double real_part = ((double)rand() / RAND_MAX) * 2.0 - 1.0;
            double imag_part = ((double)rand() / RAND_MAX) * 2.0 - 1.0;
            matrix[i][j] = real_part + imag_part * I;
        }
    }
}

void transpose_matrix(double complex matrix[30][40], double complex transpose[40][30]) {
    for (int i = 0; i < 30; i++) {
        for (int j = 0; j < 40; j++) {
            transpose[j][i] = matrix[i][j];
        }
    }
}

void multiply_matrices(int rows_A, int cols_A, int cols_B, double complex A[rows_A][cols_A], double complex B[cols_A][cols_B], double complex result[rows_A][cols_B]) {
    for (int i = 0; i < rows_A; i++) {
        for (int j = 0; j < cols_B; j++) {
            result[i][j] = 0;
            for (int k = 0; k < cols_A; k++) {
                result[i][j] += A[i][k] * B[k][j];
            }
        }
    }
}

void invert_matrix(int n, double complex matrix[40][40], double complex inverse[40][40]) {

    double complex identity[40][40];
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i == j) {
                identity[i][j] = 1.0 + 0.0 * I;
            } else {
                identity[i][j] = 0.0 + 0.0 * I;
            }
        }
    }

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            inverse[i][j] = matrix[i][j];
        }
    }

    for (int i = 0; i < n; i++) {
        double complex pivot = inverse[i][i];
        for (int j = 0; j < n; j++) {
            inverse[i][j] /= pivot;
            identity[i][j] /= pivot;
        }
        for (int k = 0; k < n; k++) {
            if (k != i) {
                double complex factor = inverse[k][i];
                for (int j = 0; j < n; j++) {
                    inverse[k][j] -= factor * inverse[i][j];
                    identity[k][j] -= factor * identity[i][j];
                }
            }
        }
    }

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            inverse[i][j] = identity[i][j];
        }
    }
}

void calculate_pseudo_inverse(double complex A[30][40], double complex A_pinv[40][30]) {
    double complex A_transpose[40][30];
    double complex A_transpose_A[40][40];
    double complex A_transpose_A_inv[40][40];

    for (int i = 0; i < 30; i++) {
        for (int j = 0; j < 40; j++) {
            A_transpose[j][i] = A[i][j];
        }
    }

    multiply_matrices(40, 30, 40, A_transpose, A, A_transpose_A);

    invert_matrix(40, A_transpose_A, A_transpose_A_inv);

    multiply_matrices(40, 40, 30, A_transpose_A_inv, A_transpose, A_pinv);
}

void find_order_preparation_time() {
    double complex matrix[30][40];
    double complex pseudo_inverse[40][30];
    create_matrix(matrix);

    clock_t start_time = clock();
    calculate_pseudo_inverse(matrix, pseudo_inverse);
    clock_t end_time = clock();

    cook_sleep_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
}

void sigpipe_handler(int signum) {
	if (signum) {}
}

void setup_sigpipe_handler() {
    struct sigaction sa;
    sa.sa_handler = sigpipe_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);
}

void handle_sigterm_sigint(int signal) {
	if (signal) {}
	is_stopped_ser = 1;
	
	if (server_in_accept) close(server_socket);
	
	server_wake_up = 1;
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

void ignore_signal(int signal) { 
	if (signal) {}
	printf("Orders sending in a batch transmission right now ... You cannot cancel them while sending ... \n");
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


void write_log(const char* log_msg, int use_mutex, int write_to_stdout) {
	const char* log_file_name = "server_log.txt";
	
	if (use_mutex == YES_MUTEX) pthread_mutex_lock(&log_mutex);
	
	if (write_to_stdout == YES_PRINTF) {
		printf("%s", log_msg);
	}
	
	int log_lock_fd = open(log_file_name, O_RDWR | O_CREAT | O_APPEND, 0666);
	if (log_lock_fd == -1) {
		perror("Problem in getting log file lock");
		if (use_mutex == YES_MUTEX) pthread_mutex_unlock(&log_mutex);
		return;
	}
	
	if (write_log_until_size(log_lock_fd, log_msg, strlen(log_msg)) == -1) {
		perror("Problem in writing to log file");
	}
	
	if (close(log_lock_fd) == -1) {
		perror("Error in unlocking log file lock");
	}
	
	if (use_mutex == YES_MUTEX) pthread_mutex_unlock(&log_mutex);
} 

void send_log_to_client_and_file(char* buf) {
	
	pthread_mutex_lock(&log_mutex);

	if (write_to_socket(client_socket, buf, strlen(buf) + 1) == -1) {
   		perror("Failed to send order");
	}

	write_log(buf, NO_MUTEX, NO_PRINTF);

	pthread_mutex_unlock(&log_mutex);
}

void cleanup() {
	
	// Stop threads
	is_stopped = 1;
	is_cancelled = 0;
	
	// Join threads
	pthread_join(*oven_opening_thr, NULL);
	
	int i;
	for (i = 0; i < MAX_OVEN_CAPACITY; ++i) {
		pthread_join(oven_slot_threads[i], NULL);
	}
	
	for (i = 0; i < total_cook_num; ++i) {
		pthread_join(cook_threads[i], NULL);
	}	

	for (i = 0; i < total_delivery_person_num; ++i) {
		pthread_join(delPerson_threads[i], NULL);
	}
	
	sem_destroy(&day_completed_sem);
	sem_destroy(&orders_new_full_sem);
	sem_destroy(&oven_waiting_sem);
	sem_destroy(&pide_apparatus_sem);
	
	sem_destroy(&orders_cooking_full_sem);
	sem_destroy(&orders_cooking_empty_sem);
	
	sem_destroy(&delivery_waiting_full_sem);
	
	sem_destroy(&num_clients_sem);
	
	pthread_mutex_destroy(&orders_new_mutex);
	pthread_mutex_destroy(&oven_waiting_mutex);
	pthread_mutex_destroy(&delivery_waiting_mutex);
	pthread_mutex_destroy(&manager_mutex);
	pthread_mutex_destroy(&orders_cooking_mutex);
    pthread_mutex_destroy(&num_clients_mutex);
    pthread_mutex_destroy(&num_threads_in_barrier_mutex);

	pthread_mutex_destroy(&log_mutex);
    
    for (i = 0; i < total_cook_num; ++i) {
		struct Cook* cook = &cooks[i];
			
		pthread_mutex_destroy(&cook->mutex);
		
		sem_destroy(&cook->oven_waiting_sem);
	}
	
	for (i = 0; i < total_delivery_person_num; ++i) {
		struct DelPerson* delPerson = &delPersons[i];
		pthread_mutex_destroy(&delPerson->mutex);
	}
	
    close(server_socket);
	
	if (orders != NULL) {
		free(orders);
		orders = NULL;
	}
	if (orders_new != NULL) { 
		free(orders_new);
		orders_new = NULL;
	}
	if (orders_oven_waiting != NULL) {
		free(orders_oven_waiting);
		orders_oven_waiting = NULL;
	}
	if (orders_cooking != NULL) {
		free(orders_cooking);
		orders_cooking = NULL;
 	}
	if (orders_delivery_waiting != NULL) {
		free(orders_delivery_waiting);
		orders_delivery_waiting = NULL;
	}
	
	if (cooks != NULL) free(cooks);
	if (delPersons != NULL) free(delPersons);
	if (ovenSlots != NULL) free(ovenSlots);
	if (ovenOpening != NULL) free(ovenOpening);
}

void cancel_orders() {
	is_cancelled = 1;
	
	// For delPerson threads
	int i;
	for (i = 0; i < total_delivery_person_num; ++i) {
		sem_post(&delivery_waiting_full_sem);
		
		if (pthread_mutex_trylock(&delPersons[i].mutex) != 0) {
			// delPerson is inside sleep
			while (pthread_mutex_trylock(&delPersons[i].mutex) != 0) {	
				sleep(0.02);
			}
			pthread_mutex_unlock(&delPersons[i].mutex);
		}
		else pthread_mutex_unlock(&delPersons[i].mutex);	
	}
	
	// For cook threads
	for (i = 0; i < total_cook_num; ++i) {
		sem_post(&orders_new_full_sem);
		
		while (pthread_mutex_trylock(&cooks[i].mutex) != 0) {	
			sleep(0.02);
		}
		pthread_mutex_unlock(&cooks[i].mutex);
		
		sem_post(&cooks[i].oven_waiting_sem);
	}
	
	// For oven opening thread
	sem_post(&oven_waiting_sem);
	sem_post(&orders_cooking_empty_sem);
	
	// For oven slot threads
	for (i = 0; i < MAX_OVEN_CAPACITY; ++i) {
		sem_post(&orders_cooking_full_sem);
	}
	
	for (i = 0; i < MAX_OVEN_CAPACITY; ++i) {
		sem_post(&orders_cooking_full_sem);
		while (ovenSlots[i].is_stopped != 1) {	
			sleep(0.02);
		}
		ovenSlots[i].is_stopped = 0;
	}
	
	while (!is_delivery_stopped) sleep(0.3);
	is_delivery_stopped = 0;
	
	for (i = 0; i < total_cook_num; ++i) {	
		while (!&cooks[i].is_stopped) sleep(0.3);
		cooks[i].is_stopped = 0;
	}
	
	while (!ovenOpening->is_stopped) sleep(0.3);
	ovenOpening->is_stopped = 0;
}

void* client_listener_thread(void* arg) {
	if (arg) {}
	char buffer[50];
    if (read_from_socket(client_socket, buffer) == -1) {
    	return NULL;
    }
    
    set_sigterm_sigint_handler_as_ignore();
    if (!is_cancelled /*&& !is_stopped*/) {
		// Msg read successfully
		is_cancelled = 1;
		server_wake_up = 1;
    }
    
    return NULL;
}

volatile int total_delivery_gelenler = 0;

void* delPerson_thread(void* arg) {
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	int id = *((int*) arg);
	struct DelPerson* delPerson = &delPersons[id];
	free(arg);

	while (!delPersons_can_stop) {	
		sem_wait(&delivery_waiting_full_sem);
		
		int readyToGo = 0;
			
		if (!is_cancelled /*&& !is_stopped*/) {
			pthread_mutex_lock(&delivery_waiting_mutex);
			if (delivery_waiting_len > 0) {
				pthread_mutex_lock(&manager_mutex); // Manager gives the fully prepared pide to the del person
				struct Order* order = orders_delivery_waiting[delivery_waiting_len - 1];
				delPerson->orders_in_bag[delPerson->in_bag_len++] = order;
				--delivery_waiting_len;
				++total_delivery_gelenler;
				
				char msg[70];
				char order_id[20];
				char personnel_id[20];
				int_to_str(delPerson->id, personnel_id);
				int_to_str(order->id, order_id);
				strcpy(msg, "Order ");
				strcat(msg, order_id);
				strcat(msg, " is taken by delivery person ");
				strcat(msg, personnel_id);
				strcat(msg, "\n");
				send_log_to_client_and_file(msg);
				
				pthread_mutex_unlock(&manager_mutex);
			}
			pthread_mutex_unlock(&delivery_waiting_mutex);
		}
		
		
		if (!is_cancelled && /*!is_stopped &&*/ !wait_in_barrier) {
			pthread_mutex_lock(&num_clients_mutex);
			--num_clients;
			if (num_clients == 0) {
			
				// Wake up all del persons to tell they need to deliver their orders since there are no more clients to wait for 3's multiplicant
				wait_in_barrier = 1;
				int i;
				for (i = 0; i < total_delivery_person_num; ++i) {
					sem_post(&delivery_waiting_full_sem);
				}
	 		}
			pthread_mutex_unlock(&num_clients_mutex);
		}
		
		if (wait_in_barrier) readyToGo = 1; 
		
		
		pthread_mutex_lock(&delPerson->mutex);
		if (delPerson->in_bag_len == MAX_DELIVERY_BAG) readyToGo = 1;
	
		int teslim_edilen = 0;
			 	
		if (!is_cancelled && /*!is_stopped &&*/ readyToGo) {
			float lastX = 0;
			float lastY = 0;
			int i;
			int is_cancelled_any = 0;
			for (i = 0; !is_cancelled && /*!is_stopped &&*/ i < delPerson->in_bag_len + 1; ++i) {
				if (is_cancelled_any && i != delPerson->in_bag_len) {
					break;
				}
			
				float newX;
				float newY;
				struct Order* order = NULL;
				if (i < delPerson->in_bag_len) {
					order = (delPerson->orders_in_bag)[i];
					newX = order->x;
					newY = order->y;
				}
				else {
					// Last one goes back to the pideci
					newX = 0.0f;
					newY = 0.0f;
				}
				
				float xDif = lastX - newX;
				float yDif = lastY - newY;
				float distance = sqrt(xDif * xDif + yDif * yDif); // distance in km
				float timeToDeliver = (distance / delivery_speed) * 60; // ((distance in m / (m/min speed = k)) = min) * 60 = seconds
				
				// Wait for pide to be cooked
				float remaining_sleep_duration = timeToDeliver;

			 	if (!is_cancelled && /*!is_stopped &&*/ remaining_sleep_duration > 0) {
					if (order != NULL) {
						char msg[70];
						char order_id[20];
						char personnel_id[20];
						int_to_str(delPerson->id, personnel_id);
						int_to_str(order->id, order_id);
						strcpy(msg, "Order ");
						strcat(msg, order_id);
						strcat(msg, " is going to the client by delivery personnel ");
						strcat(msg, personnel_id);
						strcat(msg, "\n");
						send_log_to_client_and_file(msg);
					}
				
					sleep(remaining_sleep_duration);
				
					// Delivered successfully	
					if (order != NULL) {
						if (!is_cancelled) {		
							lastX = newX;
							lastY = newY;
							++teslim_edilen;
							
							delPerson->total_tasks_done++;
							
							char msg[70];
							char order_id[20];
							char personnel_id[20];
							int_to_str(delPerson->id, personnel_id);
							int_to_str(order->id, order_id);
							strcpy(msg, "Order ");
							strcat(msg, order_id);
							strcat(msg, " is delivered by delivery personnel ");
							strcat(msg, personnel_id);
							strcat(msg, "\n");
							send_log_to_client_and_file(msg);
						}
					}	
				}
			}
			delPerson->in_bag_len = 0; // Reset the kurye's bag
		}
		pthread_mutex_unlock(&delPerson->mutex);
		readyToGo = 0;
		
		if (is_cancelled || /*is_stopped ||*/ wait_in_barrier) {
		
			pthread_mutex_lock(&num_threads_in_barrier_mutex);
			++num_threads_in_barrier;
			if (num_threads_in_barrier == total_delivery_person_num) {
				// last thread entering in barrier
			
				//if (is_stopped) delPersons_can_stop = 1;
				if (is_cancelled) {
					while (is_cancelled) {
						is_delivery_stopped = 1;
						sleep(0.5);
					}
					if (is_stopped) delPersons_can_stop = 1;
					is_delivery_stopped = 0;
				}
				
				if (!is_cancelled && (/*is_stopped ||*/ wait_in_barrier)) {
					server_wake_up = 1;
				}
				num_threads_in_barrier = 0;
				wait_in_barrier = 0;
				
			}
			pthread_mutex_unlock(&num_threads_in_barrier_mutex);
			pthread_barrier_wait(&delPersons_barrier);
		}
	}

	return NULL;
}

void* oven_slot_thread(void* arg) {
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	int id = *((int*) arg);
	struct OvenSlot* ovenSlot = &ovenSlots[id];
	free(arg);
	
	while (!is_stopped) {
		if (is_cancelled) {
			while (is_cancelled) {
				ovenSlot->is_stopped = 1;
				sleep(0.5);
			}
		}
		if (is_stopped) break;
		ovenSlot->is_stopped = 0;
	
		sem_wait(&orders_cooking_full_sem);
		//if (is_stopped) break;
		if (is_cancelled) {
			while (is_cancelled) {
				ovenSlot->is_stopped = 1;	
				sleep(0.5);
			}
		}
		if (is_stopped) break;
		ovenSlot->is_stopped = 0;
			
		// Take order from queueu which will be cooking
		struct Order* order = NULL;
		pthread_mutex_lock(&orders_cooking_mutex);
		if (orders_cooking_len > 0) {
			order = orders_cooking[orders_cooking_len - 1];
			--orders_cooking_len;
			order->ovenSlot = ovenSlot;
		}
		else {
			pthread_mutex_unlock(&orders_cooking_mutex);
			continue;
		}
		pthread_mutex_unlock(&orders_cooking_mutex);
		
		sem_post(&orders_cooking_empty_sem);
		
		// Wait for pide to be cooked
		float remaining_sleep_duration = 0.1;

		if (!is_cancelled /*&& !is_stopped*/ && remaining_sleep_duration > 0) {
			sleep(remaining_sleep_duration);	
		}
		
		// Get cook mutex since it will be get apparatus and remove the pide from the oven
		struct Cook* cook = order->cook;
	
		// Interrupt cook so that it stops what he is doing right now to place the pide to the oven
		while (!is_cancelled /*&& !is_stopped*/ && pthread_mutex_trylock(&cook->mutex) != 0) {
			sleep(cook_sleep_time / 2);
		}

		// Cook takes pide apparatus 
		sem_wait(&pide_apparatus_sem);
		
		// Add the prepared food to delivery waiting queue
		pthread_mutex_lock(&delivery_waiting_mutex);
		
		// Get manager lock since cook will pass the prepared food to manager, and manager will pass it to a delivery personnel
		pthread_mutex_lock(&manager_mutex);
		orders_delivery_waiting[delivery_waiting_len++] = order;
		cook->total_tasks_done++;
		
		char msg[70];
		char order_id[20];
		char personnel_id[20];
		int_to_str(cook->id, personnel_id);
		int_to_str(order->id, order_id);
		strcpy(msg, "Order ");
		strcat(msg, order_id);
		strcat(msg, " is removed from the oven by cook ");
		strcat(msg, personnel_id);
		strcat(msg, "\n");
		send_log_to_client_and_file(msg);
		
		sem_post(&delivery_waiting_full_sem);
		
		pthread_mutex_unlock(&manager_mutex);
		pthread_mutex_unlock(&delivery_waiting_mutex);
		
		sem_post(&pide_apparatus_sem);
		pthread_mutex_unlock(&cook->mutex);
		
	}	
	
	ovenSlot->is_finished = 1;
	return NULL;
}

void* oven_opening_thread(void* arg) {
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	if (arg) {}
	
	while (!is_stopped) {
		if (is_cancelled) {
			while (is_cancelled) {
				ovenOpening->is_stopped = 1;
				sleep(0.5);
			}
		}
		if (is_stopped) break;
		ovenOpening->is_stopped = 0;
	
		sem_wait(&oven_waiting_sem);
		
		if (is_cancelled) {
			while (is_cancelled) {
				ovenOpening->is_stopped = 1;
				sleep(0.5);
			}
		}
		
		if (is_stopped) break;
		ovenOpening->is_stopped = 0;
		
		// Wait for empty slot in oven
		sem_wait(&orders_cooking_empty_sem);
		//if (is_stopped) break;
		
		
		if (is_cancelled) {	
			while (is_cancelled) {
				ovenOpening->is_stopped = 1;
				sleep(0.5);
			}
		}
		
		if (is_stopped) break;
		ovenOpening->is_stopped = 0;
				
		// Remove element from oven opening array
		struct Order* order = NULL;
		pthread_mutex_lock(&oven_waiting_mutex);
		if (oven_waiting_len > 0) {
			order = orders_oven_waiting[oven_waiting_len - 1];
			--oven_waiting_len;
		}
		else {
			sem_post(&orders_cooking_empty_sem);
			pthread_mutex_unlock(&oven_waiting_mutex);
			continue;
		}
		pthread_mutex_unlock(&oven_waiting_mutex);
		
		struct Cook* cook = order->cook;
		
		// Interrupt cook so that it stops what he is doing right now to place the pide to the oven
		while (!is_cancelled /*&& !is_stopped*/ && pthread_mutex_trylock(&cook->mutex) != 0) {	
			sleep(0.02);
		}
		
		// Cook takes pide apparatus 
		sem_wait(&pide_apparatus_sem);
		
		// Add pide to an empty slot in the oven
		pthread_mutex_lock(&orders_cooking_mutex);
		char msg[70];
		char order_id[20];
		char personnel_id[20];
		int_to_str(cook->id, personnel_id);
		int_to_str(order->id, order_id);
		strcpy(msg, "Order ");
		strcat(msg, order_id);
		strcat(msg, " is inserted into oven by cook ");
		strcat(msg, personnel_id);
		strcat(msg, " It's cooking \n");
		send_log_to_client_and_file(msg);
		
		orders_cooking[orders_cooking_len++] = order;
		pthread_mutex_unlock(&orders_cooking_mutex);

		sem_post(&orders_cooking_full_sem);
		
		// Release the pide apparatus
		sem_post(&pide_apparatus_sem);
		
		cook->oven_waiting_flag = 0;
		sem_post(&cook->oven_waiting_sem);
		pthread_mutex_unlock(&cook->mutex);	
	}
	return NULL;
}

void* cook_thread(void* arg) {
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	int id = *((int*) arg);
	struct Cook* cook = &cooks[id];
	free(arg);
	
	while (!is_stopped) {
		
		if (is_cancelled) {
			while (is_cancelled) {
				cook->is_stopped = 1;
				sleep(0.5);
			}
		}
		if (is_stopped) break;
		cook->is_stopped = 0;
	
		// Wait until there are any orders
		sem_wait(&orders_new_full_sem);
			
		if (is_cancelled) {
			while (is_cancelled) {
				cook->is_stopped = 1;
				sleep(0.5);
			}
		}
		if (is_stopped) break;
		cook->is_stopped = 0;

		// Cook start doing job, take its mutex
		pthread_mutex_lock(&cook->mutex);
	
		// Take the order
		
		pthread_mutex_lock(&orders_new_mutex);
		pthread_mutex_lock(&manager_mutex);
		struct Order* order = NULL;
		if (orders_new_len > 0) {
			order = orders_new[orders_new_len - 1];
			--orders_new_len;
			order->cook = cook;	
			
			char msg[70];
			char order_id[20];
			char personnel_id[20];
			int_to_str(cook->id, personnel_id);
			int_to_str(order->id, order_id);
			strcpy(msg, "Order ");
			strcat(msg, order_id);
			strcat(msg, " is taken by cook ");
			strcat(msg, personnel_id);
			strcat(msg, " for preparing \n");
			send_log_to_client_and_file(msg);
			
		}
		else {
			pthread_mutex_unlock(&manager_mutex);
			pthread_mutex_unlock(&orders_new_mutex);
			pthread_mutex_unlock(&cook->mutex);
			continue;
		}
		pthread_mutex_unlock(&manager_mutex);
		pthread_mutex_unlock(&orders_new_mutex);
			
		// Prepare the order
		float remaining_sleep_duration = 0.2;
		 	
		if (!is_cancelled /*&& !is_stopped*/ && remaining_sleep_duration > 0.0f) {			
			sleep(cook_sleep_time);	
		}
		
		// Wait for oven opening and taking apparatus inside a queue
		pthread_mutex_unlock(&cook->mutex); // Since cook is waiting for oven and apparatus, unlock mutex
		
		pthread_mutex_lock(&oven_waiting_mutex);
		orders_oven_waiting[oven_waiting_len++] = order;
		pthread_mutex_unlock(&oven_waiting_mutex);
		
		cook->oven_waiting_flag = 1;	
		sem_post(&oven_waiting_sem); // As for buffer size incremented so consumers can continue
		
		while (!is_cancelled /*&& !is_stopped*/ && cook->oven_waiting_flag) {
			sem_wait(&cook->oven_waiting_sem);
		}
	}
		
	return NULL;
}

void manager_cancel_orders() {
	cancel_orders();
	
	pthread_mutex_lock(&orders_new_mutex);
	orders_new_len = 0;
	pthread_mutex_unlock(&orders_new_mutex);
	
	pthread_mutex_lock(&oven_waiting_mutex);
	oven_waiting_len = 0;
	pthread_mutex_unlock(&oven_waiting_mutex);
	
	pthread_mutex_lock(&orders_cooking_mutex);
	orders_cooking_len = 0;
	pthread_mutex_unlock(&orders_cooking_mutex);
	
	pthread_mutex_lock(&delivery_waiting_mutex);
	delivery_waiting_len = 0;
	pthread_mutex_unlock(&delivery_waiting_mutex);
	
	int i;
	for (i = 0; i < total_delivery_person_num; ++i) {
		struct DelPerson* delPerson = &delPersons[i];
		delPerson->in_bag_len = 0;	
	}
	
	set_sigterm_sigint_handler();
}

int main(int argc, char *argv[]) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s [ip] [portnumber] [CookThreadPoolSize] [DeliveryPoolSize] [k m/s]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
   
   	set_sigterm_sigint_handler_as_ignore();
    setup_sigpipe_handler();
    
    char ip[100];
    strcpy(ip, argv[1]);
    
    int port = atoi(argv[2]);
    total_cook_num = atoi(argv[3]);
    total_delivery_person_num = atoi(argv[4]);
    delivery_speed = atoi(argv[5]);

	// Init threads, mutexes, semaphores, cond. vars, cooks, del. persons, memory for array, memory for dynamic memory inside structs in arrays
	
	find_order_preparation_time();
	
	cooks = (struct Cook*) malloc(sizeof(struct Cook) * total_cook_num);
	delPersons = (struct DelPerson*) malloc(sizeof(struct DelPerson) * total_delivery_person_num);
	ovenSlots = (struct OvenSlot*) malloc(sizeof(struct OvenSlot) * MAX_OVEN_CAPACITY);
	ovenOpening = (struct OvenOpening*) malloc(sizeof(struct OvenOpening) * 1);
	
	sem_init(&day_completed_sem, 0, 0);
	sem_init(&orders_new_full_sem, 0, 0);
	sem_init(&oven_waiting_sem, 0, 0);
	sem_init(&pide_apparatus_sem, 0, MAX_OVEN_APPARATUS);
	sem_init(&orders_cooking_full_sem, 0, 0);
	sem_init(&orders_cooking_empty_sem, 0, MAX_OVEN_CAPACITY);
	sem_init(&delivery_waiting_full_sem, 0, 0);
	sem_init(&num_clients_sem, 0, 0);
	
	pthread_mutex_init(&orders_new_mutex, NULL);
	pthread_mutex_init(&oven_waiting_mutex, NULL);
	pthread_mutex_init(&delivery_waiting_mutex, NULL);
	pthread_mutex_init(&manager_mutex, NULL);
	pthread_mutex_init(&orders_cooking_mutex, NULL);
	pthread_mutex_init(&num_clients_mutex, NULL);
	
	pthread_mutex_init(&num_threads_in_barrier_mutex, NULL);
	pthread_barrier_init(&delPersons_barrier, NULL, total_delivery_person_num);
	
	pthread_mutex_init(&log_mutex, NULL);
	
	// Init cooks array
	int i;
	for (i = 0; i < total_cook_num; ++i) {
		struct Cook* cook = &cooks[i];
		cook->id = i;
		cook->total_tasks_done = 0;
		cook->oven_waiting_flag = 0;
		cook->is_stopped = 0;
		
		pthread_mutex_init(&cook->mutex, NULL);
		
		sem_init(&cook->oven_waiting_sem, 0, 0);
	} 

	// Init del. persons array
	is_delivery_stopped = 1;
	for (i = 0; i < total_delivery_person_num; ++i) {
		struct DelPerson* delPerson = &delPersons[i];
		delPerson->id = i;
		delPerson->total_tasks_done = 0;
		delPerson->in_bag_len = 0;
		
		//pthread_mutex_init(&delPerson->sleep_mutex, NULL);
		//pthread_cond_init(&delPerson->sleep_cond, NULL);
		
		pthread_mutex_init(&delPerson->mutex, NULL);
	}
	
	// Init oven slots array
	for (i = 0; i < MAX_OVEN_CAPACITY; ++i) {
		struct OvenSlot* ovenSlot = &ovenSlots[i];
		
		ovenSlot->id = i;
		ovenSlot->is_stopped = 0;
		
		ovenSlot->is_stopped = 0;
		ovenSlot->is_finished = 0;
	}
	
	// Init oven opening
	ovenOpening->is_stopped = 0;

	// Init threads
	
	// Init cooks
	// Init del persons
	// Init oven opening
	// Init 6 oven_slot
	
	pthread_t cook_threads_local[total_cook_num];
	pthread_t delPerson_threads_local[total_delivery_person_num];
	pthread_t oven_opening_thr_local;
	pthread_t oven_slot_threads_local[MAX_OVEN_CAPACITY];
	
	cook_threads = cook_threads_local;
	delPerson_threads = delPerson_threads_local;
	oven_opening_thr = &oven_opening_thr_local;
	oven_slot_threads = oven_slot_threads_local;
	
	pthread_create(oven_opening_thr, NULL, oven_opening_thread, NULL);
	
	for (i = 0; i < MAX_OVEN_CAPACITY; ++i) {
		int* id = (int*) malloc(sizeof(int));
		*id = i;
		pthread_create(&oven_slot_threads[i], NULL, oven_slot_thread, (void*) id);
	}
	
	
	for (i = 0; i < total_cook_num; ++i) {
		int* id = (int*) malloc(sizeof(int));
		*id = i;
		pthread_create(&cook_threads[i], NULL, cook_thread, (void*) id);
	}
	

	for (i = 0; i < total_delivery_person_num; ++i) {
		int* id = (int*) malloc(sizeof(int));
		*id = i;
		pthread_create(&delPerson_threads[i], NULL, delPerson_thread, (void*) id);
	}

    // Server setup
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

	int direct_exit = 0;
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        direct_exit = 1;
    }

    if (listen(server_socket, 5) < 0) {
        perror("Listen failed");
        direct_exit = 1;
    }
	
	char msg[200];	
	
	int first_enter = 1;
	int is_destroyed = 0;
	int is_created = 0;
	
	set_sigterm_sigint_handler();
	if (!direct_exit) {
		while (!is_stopped_ser) {
			is_created = 0;
			server_wake_up = 0;
			printf("PideShop server active, waiting for connections...\n");
			server_in_accept = 1;
			client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
			server_in_accept = 0;
			
			int client_listener_up = 0;
			pthread_t client_listener_thr;
			
			if (!is_stopped_ser) {
				client_port = ntohs(client_addr.sin_port);
				strcpy(client_ip, inet_ntoa(client_addr.sin_addr));
			
				if (first_enter) first_enter = 0;
				else if (is_destroyed) {
					is_created = 1;
					pthread_mutex_init(&orders_new_mutex, NULL);
					pthread_mutex_init(&oven_waiting_mutex, NULL);
					pthread_mutex_init(&delivery_waiting_mutex, NULL);
					pthread_mutex_init(&manager_mutex, NULL);
					pthread_mutex_init(&orders_cooking_mutex, NULL);
					pthread_mutex_init(&num_clients_mutex, NULL);
					
					pthread_mutex_init(&num_threads_in_barrier_mutex, NULL);
					
					pthread_mutex_init(&log_mutex, NULL);
				}
				
				int discard_client = 0;
				char num_clients_str[NUM_CLIENTS_MAX_CHAR_SIZE + 1];
				if (read_from_socket(client_socket, num_clients_str) == -1) {
					perror("Failed to receive confirmation");
					discard_client = 1;
				}
				
				if (!discard_client) {
					pthread_mutex_lock(&num_clients_mutex);
					num_clients = atoi(num_clients_str);
					pthread_mutex_unlock(&num_clients_mutex);
					
					total_cur_orders = num_clients;
					
					strcpy(msg, num_clients_str);
					strcat(msg, " new customers.. Serving \n");
					write_log(msg, YES_MUTEX, YES_PRINTF);
					
					orders = (struct Order*) malloc(sizeof(struct Order) * total_cur_orders);
					orders_new = (struct Order**) malloc(sizeof(struct Order*) * total_cur_orders);
					orders_oven_waiting = (struct Order**) malloc(sizeof(struct Order*) * total_cur_orders);
					orders_cooking = (struct Order**) malloc(sizeof(struct Order*) * total_cur_orders);
					orders_delivery_waiting = (struct Order**) malloc(sizeof(struct Order*) * total_cur_orders);
					
					int okunan = 0;
					char order_buffer[PID_MAX_CHAR_SIZE + COORDINATE_MAX_CHAR_SIZE * 2 + 1];
					int i;
					for (i = 0; !discard_client && !is_stopped_ser && i < total_cur_orders; ++i) {
						
						if (read_from_socket(client_socket, order_buffer) == -1) {
							perror("Failed to receive confirmation");
							discard_client = 1;
							break;
						}
						
						++okunan;
					
						struct Order* order = &orders[i];
						
						char* del = "/";
								
						char* token = strtok(order_buffer, del);
						order->id = i;
						order->pid = atoi(token);
						client_pid = order->pid;
						int_to_str(client_pid, client_pid_str);
					
						token = strtok(NULL, del);
						order->x = atof(token);
						
						token = strtok(NULL, del);
						order->y = atof(token);	
						
						order->socket = client_socket;	
						
						// Add to array for cooks	
						pthread_mutex_lock(&orders_new_mutex);
						orders_new[orders_new_len++] = order;
						pthread_mutex_unlock(&orders_new_mutex);
						
						
						sem_post(&orders_new_full_sem);
						sem_post(&num_clients_sem);
					}
					
					if (discard_client) {
						manager_cancel_orders();
					}
					else {
						pthread_create(&client_listener_thr, NULL, client_listener_thread, NULL); 
						client_listener_up = 1;
						 		
						while (server_wake_up == 0) { sleep(0.3); };
						server_wake_up = 0;
						
						if (is_cancelled) {
							manager_cancel_orders();
							is_cancelled = 0; // reset for new connection
							
							char client_pid_str[10];
							int_to_str(client_pid, client_pid_str);
						
							strcpy(msg, "order cancelled @ ");
							strcat(msg, client_ip);
							strcat(msg, " PID ");
							strcat(msg, client_pid_str);
							strcat(msg, "\n");
							
							write_log(msg, YES_MUTEX, YES_PRINTF);
						
							pthread_mutex_lock(&log_mutex);
						
							strcpy(msg, "^C signal .. cancelling orders .. editing log .. \n");
							write_to_socket(client_socket, msg, strlen(msg) + 1);						
							

							char* ack_msg = "ACK";
							if (write_to_socket(client_socket, ack_msg, strlen(ack_msg) + 1) == -1) {
								perror("Failed to send order");
							}
										
							// It only waits for client to be disconnected. There is no need to make an error check here
							read_from_socket(client_socket, msg);
						
							pthread_mutex_unlock(&log_mutex);
						}	
						else if (!is_stopped_ser) {
							char client_pid_str[10];
							int_to_str(client_pid, client_pid_str);
						
							strcpy(msg, "done serving client @ ");
							strcat(msg, client_ip);
							strcat(msg, " PID ");
							strcat(msg, client_pid_str);
							strcat(msg, "\n");
							
							write_log(msg, YES_MUTEX, YES_PRINTF);
							
							// Find best cook
							int best_cook_id = 0;
							for (i = 1; i < total_cook_num; ++i) {
								if (cooks[i].total_tasks_done > cooks[best_cook_id].total_tasks_done) {
									best_cook_id = i;
								}
							}
							
							// Find best del. personnel
							int best_del_id = 0;
							for (i = 1; i < total_delivery_person_num; ++i) {
								if (delPersons[i].total_tasks_done > cooks[best_del_id].total_tasks_done) {
									best_del_id = i;
								}
							}
							
							char msg[70];
							char best_cook_str[20];
							char best_del_str[20];
							int_to_str(best_cook_id, best_cook_str);
							int_to_str(best_del_id, best_del_str);
							strcpy(msg, "Thanks Cook ");
							strcat(msg, best_cook_str);
							strcat(msg, " and Moto ");
							strcat(msg, best_del_str);
							strcat(msg, "\n");
							
							write_log(msg, YES_MUTEX, YES_PRINTF);
							
							pthread_mutex_lock(&log_mutex);
							
							strcpy(msg, "All customers served \nlog file written \n");
							write_to_socket(client_socket, msg, strlen(msg) + 1);						
						
							char* ack_msg = "ACK";
							if (write_to_socket(client_socket, ack_msg, strlen(ack_msg) + 1) == -1) {
								perror("Failed to send order");
							}
							  	
							// It only waits for client to be disconnected. There is no need to make an error check here
							read_from_socket(client_socket, msg); 
							
							pthread_mutex_unlock(&log_mutex);
						}
						else {
							// is_stopped = 1
							strcpy(msg, "Server stopped ... \n");	
							write_log(msg, YES_MUTEX, YES_PRINTF);
						
							pthread_mutex_lock(&log_mutex);
						
							strcpy(msg, "Server burned ... Goodbye ... \n");
							write_to_socket(client_socket, msg, strlen(msg) + 1);						
							
							// Send message that server is closing
							char* ack_msg = "ACK";
							if (write_to_socket(client_socket, ack_msg, strlen(ack_msg) + 1) == -1) {
								perror("Failed to send order");
							}
							  	
							// It only waits for client to be disconnected. There is no need to make an error check here
							read_from_socket(client_socket, msg);
							
							pthread_mutex_unlock(&log_mutex);
						}
					}
				}
				
				if (!is_stopped_ser) {
					manager_cancel_orders();
					sleep(0.5);
						
					if (orders != NULL) {
						free(orders);
						orders = NULL;
					}
					if (orders_new != NULL) { 
						free(orders_new);
						orders_new = NULL;
					}
					if (orders_oven_waiting != NULL) {
						free(orders_oven_waiting);
						orders_oven_waiting = NULL;
					}
					if (orders_cooking != NULL) {
						free(orders_cooking);
						orders_cooking = NULL;
				 	}
					if (orders_delivery_waiting != NULL) {
						free(orders_delivery_waiting);
						orders_delivery_waiting = NULL;
					}
					
					if (!is_stopped_ser) is_cancelled = 0; // reset for new connection
					
					close(client_socket);
					if (client_listener_up) {
						pthread_join(client_listener_thr, NULL);
					}
					
					is_destroyed = 1;
					
					pthread_mutex_destroy(&orders_new_mutex);
					pthread_mutex_destroy(&oven_waiting_mutex);
					pthread_mutex_destroy(&delivery_waiting_mutex);
					pthread_mutex_destroy(&manager_mutex);
					pthread_mutex_destroy(&orders_cooking_mutex);
					pthread_mutex_destroy(&num_clients_mutex);
					pthread_mutex_destroy(&num_threads_in_barrier_mutex);

					pthread_mutex_destroy(&log_mutex);
				}
			}
			else {
				//manager_cancel_orders();
				sleep(0.5);
				
				if (!is_stopped_ser) is_cancelled = 0; // reset for new connection
				
				is_destroyed = 1;
				
				pthread_mutex_destroy(&orders_new_mutex);
				pthread_mutex_destroy(&oven_waiting_mutex);
				pthread_mutex_destroy(&delivery_waiting_mutex);
				pthread_mutex_destroy(&manager_mutex);
				pthread_mutex_destroy(&orders_cooking_mutex);
				pthread_mutex_destroy(&num_clients_mutex);
				pthread_mutex_destroy(&num_threads_in_barrier_mutex);

				pthread_mutex_destroy(&log_mutex);
			}
		}
	}
	else {
		// direct_exit == 1 (Before accept, failed in socket creation)
		manager_cancel_orders();
		sleep(0.5);
		
		if (!is_stopped_ser) is_cancelled = 0; // reset for new connection
		
		is_destroyed = 1;
		
		pthread_mutex_destroy(&orders_new_mutex);
		pthread_mutex_destroy(&oven_waiting_mutex);
		pthread_mutex_destroy(&delivery_waiting_mutex);
		pthread_mutex_destroy(&manager_mutex);
		pthread_mutex_destroy(&orders_cooking_mutex);
		pthread_mutex_destroy(&num_clients_mutex);
		pthread_mutex_destroy(&num_threads_in_barrier_mutex);

		pthread_mutex_destroy(&log_mutex);
	}
	
	if (is_destroyed) {
		is_created = 1;
	
		pthread_mutex_init(&orders_new_mutex, NULL);
		pthread_mutex_init(&oven_waiting_mutex, NULL);
		pthread_mutex_init(&delivery_waiting_mutex, NULL);
		pthread_mutex_init(&manager_mutex, NULL);
		pthread_mutex_init(&orders_cooking_mutex, NULL);
		pthread_mutex_init(&num_clients_mutex, NULL);
		
		pthread_mutex_init(&num_threads_in_barrier_mutex, NULL);
		
		pthread_mutex_init(&log_mutex, NULL);
	}
	  
    // Destroy threads, mutexes, semaphores, cond. vars, cooks, del. persons, memory for array, memory for dynamic memory inside structs in arrays
   
   	// Wake up the sleeping threads so that they exit since it is stopped 
	
	if (is_stopped_ser) {
		
		pthread_cancel(*oven_opening_thr);
		pthread_join(*oven_opening_thr, NULL);
	
		for (i = 0; i < MAX_OVEN_CAPACITY; ++i) {
			pthread_cancel(oven_slot_threads[i]);
			pthread_join(oven_slot_threads[i], NULL);
		}
		
		for (i = 0; i < total_cook_num; ++i) {
			pthread_cancel(cook_threads[i]);
			pthread_join(cook_threads[i], NULL);
		}
	
		for (i = 0; i < total_delivery_person_num; ++i) {
			pthread_cancel(delPerson_threads[i]);
			pthread_join(delPerson_threads[i], NULL);
		}
	
		sem_destroy(&day_completed_sem);
		sem_destroy(&orders_new_full_sem);
		sem_destroy(&oven_waiting_sem);
		sem_destroy(&pide_apparatus_sem);
		
		sem_destroy(&orders_cooking_full_sem);
		sem_destroy(&orders_cooking_empty_sem);
		
		sem_destroy(&delivery_waiting_full_sem);
		
		sem_destroy(&num_clients_sem);
		
		if (is_created) {
			pthread_mutex_destroy(&orders_new_mutex);
			pthread_mutex_destroy(&oven_waiting_mutex);
			pthread_mutex_destroy(&delivery_waiting_mutex);
			pthread_mutex_destroy(&manager_mutex);
			pthread_mutex_destroy(&orders_cooking_mutex);
			pthread_mutex_destroy(&num_clients_mutex);
			pthread_mutex_destroy(&num_threads_in_barrier_mutex);
			
			pthread_mutex_destroy(&log_mutex);
		}
		
		for (i = 0; i < total_cook_num; ++i) {
			struct Cook* cook = &cooks[i];
				
			pthread_mutex_destroy(&cook->mutex);
			
			sem_destroy(&cook->oven_waiting_sem);
		}
		
		for (i = 0; i < total_delivery_person_num; ++i) {
			struct DelPerson* delPerson = &delPersons[i];
			pthread_mutex_destroy(&delPerson->mutex);
		}
		
		close(server_socket);
		
		if (orders != NULL) {
			free(orders);
			orders = NULL;
		}
		if (orders_new != NULL) { 
			free(orders_new);
			orders_new = NULL;
		}
		if (orders_oven_waiting != NULL) {
			free(orders_oven_waiting);
			orders_oven_waiting = NULL;
		}
		if (orders_cooking != NULL) {
			free(orders_cooking);
			orders_cooking = NULL;
	 	}
		if (orders_delivery_waiting != NULL) {
			free(orders_delivery_waiting);
			orders_delivery_waiting = NULL;
		}
		
		if (cooks != NULL) free(cooks);
		if (delPersons != NULL) free(delPersons);
		if (ovenSlots != NULL) free(ovenSlots);
		if (ovenOpening != NULL) free(ovenOpening);
	}
	else {
		cleanup();
 	}
 	
	return 0;
}

