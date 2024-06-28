#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>

int sockfd;

void handle_exit(int sig) {
	fprintf(stdout, "signal .. cancelling orders.. editing log..\n");

	// Send a termination message to the server if necessary
	char buffer[256];
	bzero(buffer, 256);
	sprintf(buffer, "TERMINATE %d\n", getpid());
	send(sockfd, buffer, sizeof(buffer) - 1, 0);

	// Close the socket
	close(sockfd);

	exit(0);
}

void generate_orders(int port, int p, int q, int num_orders) {
	struct sockaddr_in serv_addr;
	char buffer[256];

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror("ERROR opening socket");
		return;
	}

	bzero((char *)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	serv_addr.sin_port = htons(port);

	if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		printf("Server down. Unable to connect.\n");
		return;
	}

	int pid = getpid();

	bzero(buffer, 256);
	sprintf(buffer, "%d %d\n", pid, num_orders);
	send(sockfd, buffer, sizeof(buffer) - 1,0);
	srand(time(NULL));
	for (int i = 0; i < num_orders; i++) {
		bzero(buffer, 256);
		int x = rand() % p;
		int y = rand() % q;
		sprintf(buffer, "%d %d %d\n", i, x, y);
		send(sockfd, buffer, sizeof(buffer) - 1,0);
	}
}

int main(int argc, char *argv[]) {
	if (argc != 5) {
		fprintf(stderr, "Usage: %s [portnumber] [numberOfClients] [p] [q]\n", argv[0]);
		return 1;
	}

	int port = atoi(argv[1]);
	int num_clients = atoi(argv[2]);
	int p = atoi(argv[3]);
	int q = atoi(argv[4]);
	int pid = getpid();
	printf("PID %d\n", pid);

	signal(SIGINT, handle_exit); // Set up signal handler for SIGINT
	signal(SIGTERM, handle_exit); // Set up signal handler for SIGTERM

	generate_orders(port, p, q, num_clients);
	sleep(1);

	//Read the response from the server
	char response[256];
	bzero(response, 256);
	if(recv(sockfd, response, sizeof(response) - 1, 0) < 0) {
		perror("Error reading from socket");
		return 1;
	}
	printf("%s\n", response);

	close(sockfd);

	printf("log file written ..\n");

	return 0;
}
