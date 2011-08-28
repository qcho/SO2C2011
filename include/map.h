#ifndef MAP_H_
#define MAP_H_

#include "common.h"
#include <pthread.h>

typedef struct {
	pthread_t thread;
	int originCityIndex;
	int destinationCityIndex;
	int distanceToDestination;
	int *itemStock;
	int id;
} Plane;

typedef struct {
	char *name;
	Plane *plane;
	int planeCount;
} Company;

typedef struct {
	char *name;
	int *itemStock;
	int *cityDistance;
} City;

typedef struct {
	int turn;
	int cityCount;
	City *city;
	int itemCount;
	char **itemName;
} Map;

void map_init(int maxCityCount);
City *newCity(char* name);
void map_addCity(City city);
int map_getStockId(char* name);

Map *map;

#endif

