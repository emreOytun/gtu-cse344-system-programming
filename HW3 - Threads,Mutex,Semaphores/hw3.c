#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

sem_t newPickup;
sem_t inChargeforPickup;
sem_t newAutomobile;
sem_t inChargeforAutomobile;

// Counters for temporary parking spaces
volatile int mFree_automobile = 8;
volatile int mFree_pickup = 4;
volatile int program_stop = 0;

// Mutexes as binary semaphores to prevent race conditions
sem_t mutex_auto; // Mutex for mFree_automobile global variable
sem_t mutex_pickup; // Mutex for mFree_pickup global variable
sem_t valet_auto;   // Mutex for automobile valet
sem_t valet_pickup; // Mutex for pickup valet
sem_t printf_lock; // Mutex for printf usage

const int total_coming_vehicle_num = 50;
void* carOwner(void* arg) {
    for (int i = 0; i < total_coming_vehicle_num; i++) {
        int vehicle_type = rand() % 2; // 0 for car, 1 for pickup

         if (vehicle_type == 0) { // Car
            sem_wait(&mutex_auto);
            if (mFree_automobile > 0) {
            	sem_wait(&printf_lock);
                printf("Car owner arrives. Free car parking lots before parking: %d\n", mFree_automobile);
                sem_post(&printf_lock);
                
                sem_post(&newAutomobile);
                sem_post(&mutex_auto);

                // Wait for the valet to park the vehicle
                sem_wait(&inChargeforAutomobile);
            } else {
            	sem_wait(&printf_lock);
                printf("No free car spots. Car owner leaves.\n");
                sem_post(&printf_lock);
                
                sem_post(&mutex_auto);
            }
        }
        else { // Pickup
            sem_wait(&mutex_pickup); // Critical region for mFree_pickup variable
            if (mFree_pickup > 0) {
                sem_wait(&printf_lock);
                printf("Pickup owner arrives. Free pickup parking lots before parking: %d\n", mFree_pickup);
                sem_post(&printf_lock);
                
                sem_post(&newPickup); // Increment newPickup semaphore to signal the valet 
                sem_post(&mutex_pickup); // Exit critical region for mFree_pickup variable

                sem_wait(&inChargeforPickup); // Wait until valet has parked the vehicle
            } else {
            	sem_wait(&printf_lock);
                printf("No free pickup parking lots. Pickup owner leaves.\n");
                sem_post(&printf_lock);
                
                sem_post(&mutex_pickup); // Exit critical region for mFree_pickup variable
            }
        }

        sleep(1); // 1 seconds interval for the next car arrival
    }
    
    program_stop = 1;
    return NULL;
}

void* carAttendant(void* arg) {
	int vehicle_type = *((int*)arg);

    while (!program_stop) {
        // Check for new automobiles
        if (vehicle_type == 0) {
		    if (sem_wait(&newAutomobile) == 0) {
		    	if (!program_stop) {
				    sem_wait(&valet_auto); // Lock automobile valet
				    sem_wait(&mutex_auto);
				    mFree_automobile--;
				    
				    sem_wait(&printf_lock);
				    printf("Car valet parks a car. Free car parking lots after parking: %d\n", mFree_automobile);
				    sem_post(&printf_lock);
				    
				    sem_post(&mutex_auto);
				    sem_post(&valet_auto); // Unlock automobile valet
				    sem_post(&inChargeforAutomobile);
		        }
		    }
        }
        
        // Check for new pickups
        else {
		    if (sem_wait(&newPickup) == 0) {
		    	if (!program_stop) {
				    sem_wait(&valet_pickup); // Lock valet_pickup so that we ensure there is only one valet for pickups (necessary because carRemover for pickup should not work at the same time)
				    sem_wait(&mutex_pickup); // Critical region for mFree_pickup variable
				    mFree_pickup--;
				    
				    sem_wait(&printf_lock);
				    printf("Pickup valet parks a pickup. Free pickup parking lots after parking: %d\n", mFree_pickup);
				    sem_post(&printf_lock);
				    
				    sem_post(&mutex_pickup); // Exit critical region for mFree_pickup variable
				    sem_post(&valet_pickup); // Unlock valet_pickup so that pickup valet is free now
				    sem_post(&inChargeforPickup); // Signal to the carOwner thread indicating the vehicle has been parked
		        }
		    }
        }
    }
    return NULL;
}

// Function to periodically remove cars and pickups from the parking lot
void* carRemover(void* arg) {
    while (!program_stop) {
        sleep(10); // Sleep for 10 seconds

        // Remove a car if available
        sem_wait(&valet_auto); // Lock automobile valet
        sem_wait(&mutex_auto);
        if (mFree_automobile < 8) {
            mFree_automobile++;
            
            sem_wait(&printf_lock);
            printf("Car valet removes a car. Free car parking lots: %d\n", mFree_automobile);
            sem_post(&printf_lock);
        }
        sem_post(&mutex_auto);
        sem_post(&valet_auto); // Unlock automobile valet

        // Remove a pickup if available
        sem_wait(&valet_pickup); // Lock pickup valet
        sem_wait(&mutex_pickup);
        if (mFree_pickup < 4) {
            mFree_pickup++;
            
            sem_wait(&printf_lock);
            printf("Pickup valet removes a pickup. Free pickup parking lots: %d\n", mFree_pickup);
            sem_post(&printf_lock);
        }
        sem_post(&mutex_pickup);
        sem_post(&valet_pickup); // Unlock pickup valet
    }
    return NULL;
}


int main() {
    srand(time(NULL));
    
    // Initialize semaphores
    sem_init(&newPickup, 0, 0);
	sem_init(&inChargeforPickup, 0, 0);
	sem_init(&newAutomobile, 0, 0);
	sem_init(&inChargeforAutomobile, 0, 0);
	sem_init(&mutex_auto, 0, 1);
	sem_init(&mutex_pickup, 0, 1);
	sem_init(&valet_auto, 0, 1);
	sem_init(&valet_pickup, 0, 1);
	sem_init(&printf_lock, 0, 1);

	pthread_t carOwnerThread, carAttendantPickupThread, carAttendantAutomobileThread, carRemoverThread;

	// Create threads 
	int automobile_arg = 0;
	int pickup_arg = 1;
	
	pthread_create(&carOwnerThread, NULL, carOwner, NULL);
	pthread_create(&carAttendantAutomobileThread, NULL, carAttendant, (void*) &automobile_arg);
	pthread_create(&carAttendantPickupThread, NULL, carAttendant, (void*) &pickup_arg);
	pthread_create(&carRemoverThread, NULL, carRemover, NULL);

	// Wait for the car owner thread to finish
	pthread_join(carOwnerThread, NULL);
	
	// Here, I'm posting these two semaphores so that attendant threads can wake up and see the program has stopped 
	sem_post(&newPickup);
	sem_post(&newAutomobile);
	
	pthread_join(carAttendantAutomobileThread, NULL);
	pthread_join(carAttendantPickupThread, NULL);
	pthread_join(carRemoverThread, NULL);

	// Destroy semaphores
	sem_destroy(&newPickup);
	sem_destroy(&inChargeforPickup);
	sem_destroy(&newAutomobile);
	sem_destroy(&inChargeforAutomobile);
	sem_destroy(&mutex_auto);
	sem_destroy(&mutex_pickup);
	sem_destroy(&valet_auto);
	sem_destroy(&valet_pickup);
	sem_destroy(&printf_lock);

	return 0;
}
