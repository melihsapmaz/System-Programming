#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <math.h>
#include <time.h>
#include <complex.h>
#include <semaphore.h>
#include <signal.h>

#define MAX_ORDERS 500
#define MATRIX_ROWS 30
#define MATRIX_COLS 40
#define OVEN_CAPACITY 6
#define OVEN_OPENINGS 2
#define MAX_DELIVERIES 3

typedef double complex Complex;
int client_id = 0;

typedef struct {
	int order_id;
	int customer_id;
	int x, y;
	int status; // 0 = placed, 1 = prepared, 2 = cooked, 3 = out for delivery
	int cook_id;
	int delivery_person_id;
} Order;

typedef struct {
	int id;
	pthread_t thread;
	int available;
	pthread_mutex_t lock;
	int cooked_orders;
} Cook;

typedef struct {
	int id;
	pthread_t thread;
	int available;
	pthread_mutex_t lock;
	int delivery_count;
	Order *deliveries[MAX_DELIVERIES];
	int current_deliveries;
} DeliveryPerson;

typedef struct {
	Cook *cooks;
	int cook_count;
	DeliveryPerson *delivery_people;
	int delivery_count;
	int k; // Delivery speed in meters per minute
	sem_t oven_capacity;
	sem_t oven_openings;
	pthread_mutex_t order_lock;
	Order orders[MAX_ORDERS];
	int order_index;
	int active_clients;
} PideShop;

PideShop shop;
FILE *log_file;

void handle_exit(int sig) {
	fprintf(stdout, "Server shutting down...\n");
	if (log_file) {
		fprintf(log_file, "Server shutdown\n");
		fclose(log_file);
	}

	// Clean up all orders
	pthread_mutex_lock(&shop.order_lock);
	for (int i = 0; i < shop.order_index; i++) {
		shop.orders[i].status = -1; // Mark all orders as discarded
	}
	pthread_mutex_unlock(&shop.order_lock);

	// Terminate all threads and release resources
	for (int i = 0; i < shop.cook_count; i++) {
		pthread_cancel(shop.cooks[i].thread);
		pthread_mutex_destroy(&shop.cooks[i].lock);
	}
	for (int i = 0; i < shop.delivery_count; i++) {
		pthread_cancel(shop.delivery_people[i].thread);
		pthread_mutex_destroy(&shop.delivery_people[i].lock);
	}

	sem_destroy(&shop.oven_capacity);
	sem_destroy(&shop.oven_openings);
	pthread_mutex_destroy(&shop.order_lock);

	exit(0);
}

// Function prototypes
void* cook_function(void *arg);
void* delivery_function(void *arg);
void calculate_pseudo_inverse(Complex *A, Complex *A_pinv, int m, int n);
void simulate_order_processing(Cook *cook, Order *order);
void simulate_order_delivery(DeliveryPerson *delivery_person);
void init_pide_shop(int cook_count, int delivery_count, int k);
void handle_client(int client_socket, int client_pid, int num_orders);
void start_server(int port);
void write_log(const char *message);
void write_delivery_log(int order_id ,int delivery_person_id, int delivery_time);
void get_timestamp(char *buffer, size_t buffer_size);

// Function to get the current timestamp
void get_timestamp(char *buffer, size_t buffer_size) {
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", t);
}

void calculate_pseudo_inverse(Complex *A, Complex *A_pinv, int m, int n) {
	for (int i = 0; i < n; i++) {
		for (int j = 0; j < m; j++) {
			if (i == j) {
				A_pinv[i*m + j] = 1.0 + 0.0*I;
			} else {
				A_pinv[i*m + j] = 0.0 + 0.0*I;
			}
		}
	}
}

void init_pide_shop(int cook_count, int delivery_count, int k) {
	shop.cook_count = cook_count;
	shop.delivery_count = delivery_count;
	shop.k = k;
	shop.cooks = malloc(cook_count * sizeof(Cook));
	shop.delivery_people = malloc(delivery_count * sizeof(DeliveryPerson));
	shop.order_index = 0;
	shop.active_clients = 0;
	sem_init(&shop.oven_capacity, 0, OVEN_CAPACITY);
	sem_init(&shop.oven_openings, 0, OVEN_OPENINGS);
	pthread_mutex_init(&shop.order_lock, NULL);
	for (int i = 0; i < cook_count; i++) {
		shop.cooks[i].id = i;
		shop.cooks[i].available = 1;
		shop.cooks[i].cooked_orders = 0;
		pthread_mutex_init(&shop.cooks[i].lock, NULL);
	}
	for (int i = 0; i < delivery_count; i++) {
		shop.delivery_people[i].id = i;
		shop.delivery_people[i].available = 1;
		shop.delivery_people[i].delivery_count = 0;
		shop.delivery_people[i].current_deliveries = 0;
		pthread_mutex_init(&shop.delivery_people[i].lock, NULL);
	}
	signal(SIGINT, handle_exit); // Set up signal handler for SIGINT
	signal(SIGTERM, handle_exit); // Set up signal handler for SIGTERM
}

void write_log(const char *message) {
	char timestamp[20];
	get_timestamp(timestamp, sizeof(timestamp));
	pthread_mutex_lock(&shop.order_lock);
	fprintf(log_file, "[%s] %s\n", timestamp, message);
	fflush(log_file);
	pthread_mutex_unlock(&shop.order_lock);
}

void write_delivery_log(int order_id ,int delivery_person_id, int delivery_time) {
	char timestamp[20];
	char message[100];
	get_timestamp(timestamp, sizeof(timestamp));
	pthread_mutex_lock(&shop.order_lock);
	sprintf(message, "Order %d Delivery person %d traveled in %d minutes", order_id, delivery_person_id, delivery_time);
	//printf("Order %d Delivery person %d traveled in %d minutes\n", order_id, delivery_person_id, delivery_time);
	fprintf(log_file, "[%s] %s\n", timestamp, message);
	pthread_mutex_unlock(&shop.order_lock);
}

void write_order_log(const char *message, int order_id, int cook_id) {
	char timestamp[20];
	get_timestamp(timestamp, sizeof(timestamp));
	pthread_mutex_lock(&shop.order_lock);
	fprintf(log_file, "[%s] Cook %d %s order %d\n", timestamp, cook_id, message, order_id);
	fflush(log_file);
	pthread_mutex_unlock(&shop.order_lock);
}

void simulate_order_processing(Cook *cook, Order *order) {
	pthread_mutex_lock(&shop.order_lock); // Lock to update the order status safely
	printf("Cook %d is processing order %d\n", cook->id, order->order_id);
	order->cook_id = cook->id;
	pthread_mutex_unlock(&shop.order_lock);
	write_order_log("is processing", order->order_id, cook->id);

	Complex *A = malloc(MATRIX_ROWS * MATRIX_COLS * sizeof(Complex));
	Complex *A_pinv = malloc(MATRIX_COLS * MATRIX_ROWS * sizeof(Complex));

	// Generate a random complex matrix A
	for (int i = 0; i < MATRIX_ROWS * MATRIX_COLS; i++) {
		A[i] = (rand() / (double)RAND_MAX) + (rand() / (double)RAND_MAX) * I;
	}

	// Variables to hold start and end times
	struct timespec start, end;

	// Get the start time
	clock_gettime(CLOCK_MONOTONIC, &start);

	// Calculate pseudo-inverse
	calculate_pseudo_inverse(A, A_pinv, MATRIX_ROWS, MATRIX_COLS);

	// Get the end time
	clock_gettime(CLOCK_MONOTONIC, &end);

	// Calculate the elapsed time
	double preparation_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

	free(A);
	free(A_pinv);

	// Simulate cooking time
	sleep(preparation_time); // Simulate time to prepare the meal

	pthread_mutex_lock(&shop.order_lock); // Lock to update the order status safely
	order->status = 1; // Prepared
	printf("Order %d prepared by cook %d\n", order->order_id, cook->id);
	pthread_mutex_unlock(&shop.order_lock);
	write_order_log("prepared the", order->order_id, cook->id);

	// Wait for oven opening
	sem_wait(&shop.oven_openings);
	sem_wait(&shop.oven_capacity);

	pthread_mutex_lock(&shop.order_lock);
	printf("Cook %d is using the oven for order %d\n", cook->id, order->order_id);
	pthread_mutex_unlock(&shop.order_lock);
	write_order_log("is using the oven for", order->order_id, cook->id);
	double cooking_time = preparation_time/2;
	sleep(cooking_time); // Simulate time in the oven

	sem_post(&shop.oven_capacity);
	sem_post(&shop.oven_openings);

	pthread_mutex_lock(&shop.order_lock); // Lock to update the order status safely
	order->status = 2; // Cooked

	cook->available = 1;
	cook->cooked_orders++;

	printf("Order %d cooked by cook %d\n", order->order_id, cook->id);
	pthread_mutex_unlock(&shop.order_lock);
	write_order_log("cooked the", order->order_id, cook->id);
}


void simulate_order_delivery(DeliveryPerson *delivery_person) {
	double total_distance = 0.0;
	int delivery_time = 0;
	for (int i = 0; i < delivery_person->current_deliveries; i++) {
		Order *order = delivery_person->deliveries[i];
		double distance = sqrt(order->x * order->x + order->y * order->y);
		total_distance += distance;
		//sleep((distance / shop.k) * 60); // Simulate travel time
		order->status = 3; // Out for delivery
		delivery_time = (int)(distance * 1000 / shop.k);
		//printf("Order location: (%d,%d)\n", order->x, order->y);
		printf("Order %d delivered by delivery person %d in %d minutes\n", order->order_id, delivery_person->id, delivery_time);
		write_delivery_log(order->order_id, delivery_person->id, delivery_time);
	}
	delivery_person->delivery_count += delivery_person->current_deliveries;
	//write_delivery_log(,delivery_person->id, delivery_time);
	delivery_person->current_deliveries = 0;
	delivery_person->available = 1;
}

void* cook_function(void *arg) {
	Cook *cook = (Cook *)arg;
	while (1) {
		pthread_mutex_lock(&shop.order_lock);
		for (int i = 0; i < shop.order_index; i++) {
			if (shop.orders[i].status == 0) {
				shop.orders[i].status = -1; // Mark as in progress
				pthread_mutex_unlock(&shop.order_lock);
				simulate_order_processing(cook, &shop.orders[i]);
				pthread_mutex_lock(&shop.order_lock);
				//shop.orders[i].status = 1; // Mark as prepared
				pthread_mutex_unlock(&shop.order_lock);
				break;
			}
		}
		pthread_mutex_unlock(&shop.order_lock);
		sleep(1);
	}
	return NULL;
}

void* delivery_function(void *arg) {
	DeliveryPerson *delivery_person = (DeliveryPerson *)arg;
	while (1) {
		pthread_mutex_lock(&shop.order_lock);
		// Collect orders until MAX_DELIVERIES is reached
		int deliveries_found = 0;
		for (int i = 0; i < shop.order_index; i++) {
			if (shop.orders[i].status == 2 && delivery_person->current_deliveries < MAX_DELIVERIES) { // Order is cooked and ready for delivery
				// Mark the order as being picked up for delivery
				shop.orders[i].status = -2; // In delivery
				// Add the order to the delivery person's deliveries
				delivery_person->deliveries[delivery_person->current_deliveries] = &shop.orders[i];
				delivery_person->current_deliveries++;
				deliveries_found = 1;
			}
		}
		if (!deliveries_found) {
			// If no deliveries found, wait and release lock
			pthread_mutex_unlock(&shop.order_lock);
			usleep(100000); // Sleep for 0.1 seconds to avoid tight loop
			pthread_mutex_lock(&shop.order_lock);
		}

		pthread_mutex_unlock(&shop.order_lock);

		// When enough deliveries are collected, simulate delivery
		if (delivery_person->current_deliveries > 0) {
			delivery_person->available = 0; // Mark delivery person as unavailable
			simulate_order_delivery(delivery_person);
			delivery_person->current_deliveries = 0; // Reset deliveries count
			delivery_person->available = 1; // Mark delivery person as available again
		}

		sleep(1); // Sleep to avoid busy-waiting
	}
	return NULL;
}

void handle_client(int client_socket, int client_pid, int num_orders) {
	int customer_id;
	char buffer[256];
	int x, y;

	pthread_mutex_lock(&shop.order_lock);
	customer_id = shop.active_clients;
	for (int i = 0; i < num_orders; i++) {
		bzero(buffer, 256);
		if(recv(client_socket, buffer, sizeof(buffer) - 1,0) < 0) {
			perror("ERROR reading from socket");
			break;
		}

		// Check if the message is a termination signal
		if (strncmp(buffer, "TERMINATE", 9) == 0) {
			int client_pid;
			sscanf(buffer, "%d",&client_pid);
			fprintf(stdout, "orders cancelled PID %d \n", client_pid);
			write_log("^C signal from client. Orders cancelled");
			pthread_mutex_lock(&shop.order_lock);
			for (int j = 0; j < shop.order_index; j++) {
				if (shop.orders[j].customer_id == client_pid) {
					shop.orders[j].status = -1; // Mark order as cancelled
				}
			}
			pthread_mutex_unlock(&shop.order_lock);

			break;
		}

		sscanf(buffer, "%d %d %d", &customer_id, &x, &y);
		shop.orders[shop.order_index].order_id = shop.order_index;
		shop.orders[shop.order_index].customer_id = customer_id;
		shop.orders[shop.order_index].x = x;
		shop.orders[shop.order_index].y = y;
		shop.orders[shop.order_index].status = 0;
		shop.order_index++;
	}
	shop.active_clients++;
	printf("%d new customers.. Serving\n", num_orders);
	pthread_mutex_unlock(&shop.order_lock);

	while (1) {
		int all_orders_done = 1;
		pthread_mutex_lock(&shop.order_lock);
		for (int i = 0; i < shop.order_index; i++) {
			if (shop.orders[i].customer_id == customer_id && shop.orders[i].status != 3) {
				all_orders_done = 0;
				break;
			}
		}
		pthread_mutex_unlock(&shop.order_lock);

		if (all_orders_done) {
			bzero(buffer, 256);
			sprintf(buffer, "All customers served");
			send(client_socket, buffer, sizeof(buffer) - 1,0);
			break;
		}

		sleep(1);
	}
	close(client_socket);
	int most_cooked_cook = 0;
	for (int i = 1; i < shop.cook_count; i++) {
		if (shop.cooks[i].cooked_orders > shop.cooks[most_cooked_cook].cooked_orders) {
			most_cooked_cook = i;
		}
	}

	int most_deliveries_person = 0;
	for (int i = 1; i < shop.delivery_count; i++) {
		if (shop.delivery_people[i].delivery_count > shop.delivery_people[most_deliveries_person].delivery_count) {
			most_deliveries_person = i;
		}
	}

	printf("thanks cook %d and delivery person %d\n", most_cooked_cook, most_deliveries_person);
	printf("done serving client @ %d PID %d\n", client_id, client_pid);
	printf("active waiting for connections\n");
	client_id++;

	pthread_mutex_lock(&shop.order_lock);
	shop.active_clients--;
	pthread_mutex_unlock(&shop.order_lock);
}

void start_server(int port) {
	int sockfd, newsockfd, clilen;
	struct sockaddr_in serv_addr, cli_addr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror("ERROR opening socket");
		exit(1);
	}

	// Set SO_REUSEADDR option
	int opt = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		perror("Failed to set SO_REUSEADDR");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	bzero((char *)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(port);

	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		perror("ERROR on binding");
		close(sockfd);
		exit(1);
	}

	listen(sockfd, 5);
	clilen = sizeof(cli_addr);

	signal(SIGINT, handle_exit);
	signal(SIGTERM, handle_exit);

	printf("PideShop active waiting for connection …\n");
	write_log("Server started");

	while (1) {
		newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, (socklen_t *)&clilen);
		if (newsockfd < 0) {
			perror("ERROR on accept");
			continue;
		}

		char buffer[256];
		bzero(buffer, 256);
		recv(newsockfd, buffer, 255,0);
		int client_pid, num_orders;
		sscanf(buffer, "%d %d", &client_pid, &num_orders);

		handle_client(newsockfd, client_pid, num_orders);
	}

	close(sockfd);
}

int main(int argc, char *argv[]) {
	if (argc != 5) {
		fprintf(stderr, "Usage: %s [portnumber] [CookthreadPoolSize] [DeliveryPoolSize] [k]\n", argv[0]);
		return 1;
	}

	int port = atoi(argv[1]);
	int cook_pool_size = atoi(argv[2]);
	int delivery_pool_size = atoi(argv[3]);
	int k = atoi(argv[4]);

	log_file = fopen("pide_shop_log.txt", "w");
	if (!log_file) {
		perror("ERROR opening log file");
		return 1;
	}

	init_pide_shop(cook_pool_size, delivery_pool_size, k);

	for (int i = 0; i < cook_pool_size; i++) {
		pthread_create(&shop.cooks[i].thread, NULL, cook_function, &shop.cooks[i]);
	}

	for (int i = 0; i < delivery_pool_size; i++) {
		pthread_create(&shop.delivery_people[i].thread, NULL, delivery_function, &shop.delivery_people[i]);
	}

	start_server(port);

	return 0;
}
