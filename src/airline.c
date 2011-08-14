#include "../include/airline.h"

#define	MSJ_SIZE	20		// FIXME: To be removed...

void initPlanes(int planes, int** rdPipes, int** wrPipes);

Airline* createAirline(char* name, int numberOfPlanes) {
	Airline* airline = (Airline*) malloc(sizeof(Airline));
	airline->name = name;
	airline->planesCount = numberOfPlanes;
	airline->targetedCities = malloc(sizeof(int) * numberOfPlanes);
	int i;
	for (i = 0; i < numberOfPlanes; i++) {
		airline->targetedCities[i] = FALSE;
	}
	return airline;
}

void airlineProcess(Airline* airline) {
	char ch, buf[MSJ_SIZE];
	int** rdPipes = createIntMatrix(airline->planesCount, 2);
	int** wrPipes = createIntMatrix(airline->planesCount, 2);
	initPlanes(airline->planesCount, rdPipes, wrPipes);
	fd_set masterRdFd, masterWrFd;
	int i;
	// closes all unwanted write file descriptors
	for (i = 0; i < airline->planesCount; i++) {
		close(rdPipes[i][WRITE]);
		close(wrPipes[i][READ]);
	}
	// Sets all the bit masks for the select system call
	FD_ZERO(&masterRdFd);
	FD_SET(0, &masterRdFd);
	for (i = 0; i < airline->planesCount; i++) {
		FD_SET(rdPipes[i][READ], &masterRdFd);
		FD_SET(wrPipes[i][WRITE], &masterWrFd);
	}
	// Call to select with no timeout, it will block until an event occurs
	fd_set readCpy = masterRdFd;
	fd_set writeCpy = masterWrFd;
	while (select(rdPipes[airline->planesCount - 1][READ] + 1, &readCpy, &writeCpy, NULL, NULL) > 0) {
		for (i = 0; i < airline->planesCount; i++) {
			if (FD_ISSET(rdPipes[i][READ], &readCpy)) {
				if (read(rdPipes[i][READ], buf, MSJ_SIZE) > 0) {
					printf("Message from child %d -- %s\n", i, buf);
					write(wrPipes[i][WRITE], "Response from airline\n", 25);
				}
			}
		}
		// if all sub-processes are dead, return to the main program.
		if (waitpid(-1, NULL, WNOHANG) == -1) {
			return;
		}
		readCpy = masterRdFd;
	}
}

void initPlanes(int planes, int** rdPipes, int** wrPipes) {
	int i;
	for (i = 0; i < planes; i++) {
		if (pipe(rdPipes[i]) == -1 || pipe(wrPipes[i]) == -1) {
			fatal("Pipe call error");
		}
		switch (fork()) {
			case -1:
				fatal("Fork call error");
			case 0:
				planeProcess(createPlane(i), rdPipes[i], wrPipes[i]);
		}
	}
}



