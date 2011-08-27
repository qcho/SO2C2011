#ifndef __SEMAPHORE_H__
#define __SEMAPHORE_H__

#include <sys/sem.h>

union semun {
	int val;
	struct semid_ds *buf;
	unsigned short int *array;
	struct seminfo *__buf;
};

int semaphore_create(int key, int semSize, int flags);

int semaphore_increment(int id, int semnum);

int semaphore_decrement(int id, int semnum);

int semaphore_operation(int id, int op, int semnum);

#endif
