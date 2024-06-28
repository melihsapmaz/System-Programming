#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define PICKUP_CAPACITY 4
#define AUTOMOBILE_CAPACITY 8
#define TEMPORARY_AUTOMOBILE_CAPACITY 8
#define TEMPORARY_PICKUP_CAPACITY 4
#define MAX_CAR_OWNERS 20

// Counter variables for available spots
int mFree_pickup = PICKUP_CAPACITY;
int mFree_automobile = AUTOMOBILE_CAPACITY;

// Counter variables for available spots in the temporary parking lot
int mFree_temporary_automobile = TEMPORARY_AUTOMOBILE_CAPACITY;
int mFree_temporary_pickup = TEMPORARY_PICKUP_CAPACITY;

// Semaphores
sem_t newPickup;
sem_t inChargeforPickup;
sem_t newAutomobile;
sem_t inChargeforAutomobile;

// Mutex for protecting counter variables
pthread_mutex_t pickup_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t automobile_lock = PTHREAD_MUTEX_INITIALIZER;

// Mutex for protecting counter variables of the temporary parking lot
pthread_mutex_t temporary_automobile_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t temporary_pickup_lock = PTHREAD_MUTEX_INITIALIZER;

// Function prototypes
void* carOwner(void* arg);
void* carAttendant(void* arg);

// Car owner function
void* carOwner(void* arg) {
	int vehicleType = *((int*)arg);
	free(arg); // Free allocated memory for vehicleType

	if (vehicleType == 1) { // Automobile
		pthread_mutex_lock(&temporary_automobile_lock);
		if (mFree_temporary_automobile > 0) {
			mFree_temporary_automobile--;
			printf("Automobile owner: parked in temporary lot. Available temporary auto spots: %d\n", mFree_temporary_automobile);
			sem_post(&newAutomobile); // Signal a new automobile
			pthread_mutex_unlock(&temporary_automobile_lock);
			sem_wait(&inChargeforAutomobile); // Wait for the attendant to park it
			pthread_mutex_lock(&automobile_lock);
			if (mFree_automobile > 0) {
				printf("Automobile owner: my automobile is parked in the main lot.\n");
			}
			else {
				printf("Automobile owner: no parking spot available in the main lot. Leaving...\n");
			}
			pthread_mutex_unlock(&automobile_lock);
		} else {
			pthread_mutex_unlock(&temporary_automobile_lock);
			printf("Automobile owner: no temporary parking spot available. Leaving...\n");
			return NULL; // Car owner leaves
		}
	} else { // Pickup
		pthread_mutex_lock(&temporary_pickup_lock);
		if (mFree_temporary_pickup > 0) {
			mFree_temporary_pickup--;
			printf("Pickup owner: parked in temporary lot. Available temporary pickup spots: %d\n", mFree_temporary_pickup);
			sem_post(&newPickup); // Signal a new pickup
			pthread_mutex_unlock(&temporary_pickup_lock);
			sem_wait(&inChargeforPickup); // Wait for the attendant to park it
			pthread_mutex_lock(&pickup_lock);
			if (mFree_pickup > 0) {
				printf("Pickup owner: my pickup is parked in the main lot.\n");
			}
			else {
				printf("Pickup owner: no parking spot available in the main lot. Leaving...\n");
			}
			pthread_mutex_unlock(&pickup_lock);
		} else {
			pthread_mutex_unlock(&temporary_pickup_lock);
			printf("Pickup owner: no temporary parking spot available. Leaving...\n");
			return NULL; // Car owner leaves
		}
	}
	return NULL;
}

// Car attendant function
void* carAttendant(void* arg) {
	int vehicleType = *((int*)arg);

	if (vehicleType == 1) { // Automobile
		while (1) {
			sem_wait(&newAutomobile); // Wait for a new automobile
			pthread_mutex_lock(&automobile_lock);
			if (mFree_automobile > 0) {
				mFree_temporary_automobile++;
				mFree_automobile--;
				printf("Automobile attendant: parked an automobile. Available automobile spots: %d\n", mFree_automobile);
			} else {
				printf("Automobile attendant: no parking spot available in the main lot. The automobile is waiting in the temporary lot...\n");
			}
			pthread_mutex_unlock(&automobile_lock);
			sem_post(&inChargeforAutomobile); // Signal that parking is done
		}
	} else { // Pickup
		while (1) {
			sem_wait(&newPickup); // Wait for a new pickup
			pthread_mutex_lock(&pickup_lock);
			if (mFree_pickup > 0) {
				mFree_temporary_pickup++;
				mFree_pickup--;
				printf("Pickup attendant: parked a pickup. Available pickup spots: %d\n", mFree_pickup);
			} else {
				printf("Pickup attendant: no parking spot available in the main lot. The pickup is waiting in the temporary parking lot...\n");
			}
			pthread_mutex_unlock(&pickup_lock);
			sem_post(&inChargeforPickup); // Signal that parking is done
		}
	}
	return NULL;
}

int main() {
	pthread_t automobileAttendantThread, pickupAttendantThread;
	pthread_t ownerThreads[MAX_CAR_OWNERS]; // Adjust size as needed

	// Initialize semaphores
	sem_init(&newPickup, 0, 0);
	sem_init(&inChargeforPickup, 0, 0);
	sem_init(&newAutomobile, 0, 0);
	sem_init(&inChargeforAutomobile, 0, 0);

	// Create car attendant threads
	int* autoType = malloc(sizeof(int));
	*autoType = 1; // Automobile
	pthread_create(&automobileAttendantThread, NULL, carAttendant, autoType);

	int* pickupType = malloc(sizeof(int));
	*pickupType = 2; // Pickup
	pthread_create(&pickupAttendantThread, NULL, carAttendant, pickupType);

	// Create a fixed number of car owner threads
	for (int i = 0; i < MAX_CAR_OWNERS; i++) {
		int* vehicleType = malloc(sizeof(int));
		*vehicleType = (rand() % 2) + 1; // Randomly generate 1 (automobile) or 2 (pickup)
		if (*vehicleType == 1) {
			pthread_create(&ownerThreads[i], NULL, carOwner, vehicleType);
		} else {
			pthread_create(&ownerThreads[i], NULL, carOwner, vehicleType);
		}
		sleep(1); // Simulate time delay between arrivals
	}

	// Join owner threads
	for (int i = 0; i < 20; i++) {
		pthread_join(ownerThreads[i], NULL);
	}

	// Join attendant threads (they will run indefinitely in this example)
	// pthread_join(automobileAttendantThread, NULL);
	// pthread_join(pickupAttendantThread, NULL);

	// Destroy semaphores
	sem_destroy(&newPickup);
	sem_destroy(&inChargeforPickup);
	sem_destroy(&newAutomobile);
	sem_destroy(&inChargeforAutomobile);

	return 0;
}

