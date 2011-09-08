#include "controller/companyLogic.h"

int initializeCompany();
void updateMap();
void wakeUpPlanes(int semId);
void waitUntilPlanesReady(int semId);
void updateDestinations();
void updateMapItems(Map* map, Plane* plane);
void setNewTarget(Map* map, Plane* plane);
int getScore(Plane* plane, int cityId);

static Company *company;
static Map *map;
static pthread_t* planeThreadId;
static int activePlanes;		// each bit in high indicated an active plane

/*
 * 1 - Initialize company.
 * 2 - Update map.
 * 3 - Wake up planes.
 * 4 - Update planes.
 * 5 - Send map updates.
 */
void companyStart(Map* initialMap, Company* cmp) {
	company = cmp;
	map = initialMap;
	int planesSemId = initializeCompany();
	int serverSemId = semaphore_get(SERVER_SEM_KEY);
	semaphore_increment(serverSemId, 0);// Tell the server that this company has been created.
	while (activePlanes != 0) {
		semaphore_decrement(serverSemId, company->id + 1);
		log_debug("[Company %d] Playing one turn", company->id);
		log_debug("[Company %d] Active planes: %d", company->id, activePlanes);
		updateMap();
		wakeUpPlanes(planesSemId);
		waitUntilPlanesReady(planesSemId);
		updateDestinations();
		sleep(1);
		log_debug("[Company %d] Finished turn OK", company->id);
		semaphore_increment(serverSemId, 0);
	}
	log_debug("[Company %d] I have supplied all the medications I can!", company->id);
	// Send killmyself package...
}

int initializeCompany() {
	planeThreadId = malloc(sizeof(pthread_t) * company->planeCount);
	int turnsSemId = semaphore_get(company->id);
	if (turnsSemId < 0) {
		fatal("Company - Error initializing semaphore.");
	}
	for(int i = 0; i < company->planeCount; i++) {
		pthread_create(planeThreadId + i, NULL, planeStart, company->plane[i]);
	}
	for(int i = 0; i < company->planeCount; i++) {
		// Wait for all planes to be ready...
		semaphore_decrement(turnsSemId, 0);
	}
	activePlanes = (1 << company->planeCount) - 1;		// There should be no more than 32 planes.
	return turnsSemId;
}

/*
 * 1 - for each message in the queue => apply update to map;
 */
void updateMap() {
    CityUpdatePackage cup;
    serializer_read_cityUpdate(&cup, SERVER_SEM_KEY, company->id);
}

void wakeUpPlanes(int semId) {
	log_debug("Planes wake up!");
	for(int i = 0; i < company->planeCount; i++) {
		log_debug("waking up plane: %d", PLANE_INDEX(company->plane[i]->id) + 1);
		semaphore_increment(semId, PLANE_INDEX(company->plane[i]->id) + 1);
	}
}

void waitUntilPlanesReady(int semId) {
	for(int i = 0; i < company->planeCount; i++) {
		semaphore_decrement(semId, 0);
	}
	log_debug("[Company %d] Waiting done!...", company->id);
}

void updateDestinations() {
	for(int i = 0; i < company->planeCount; i++) {
		if (company->plane[i]->distanceLeft == 0) {
			log_debug("[Company %d] Plane %d needs new target\n", company->id, company->plane[i]->id);
			updateMapItems(map, company->plane[i]);
			setNewTarget(map, company->plane[i]);
		}
	}
}

void updateMapItems(Map* map, Plane* plane) {
	log_debug("[Company %d] Updating items for plane %d", company->id, plane->id);
	for (int i = 0; i < plane->itemCount; ++i) {
		int cityStock = map->city[plane->cityIdFrom]->itemStock[i];
		int planeStock = plane->itemStock[i];
		if (cityStock < 0 && planeStock > 0) {
			int supplies = min(-cityStock, planeStock);
			plane->itemStock[i] -= supplies;
			map->city[plane->cityIdFrom]->itemStock[i] += supplies;
		}
	}
}

void setNewTarget(Map* map, Plane* plane) {
	int i;
	int newTargetScore;
	int bestCityScore = 0;
	int bestCityindex = NO_TARGET;
	for (i = 0; i < map->cityCount; i++) {
		int routeLength = map->cityDistance[plane->cityIdFrom][i];
		if (i == plane->cityIdFrom || routeLength == 0) {
			// Skip if current city or route does not exists
			continue;
		}
		newTargetScore = getScore(plane, i);
		if (bestCityScore < newTargetScore) {
			bestCityScore = newTargetScore;
			bestCityindex = i;
		}
	}
	if (bestCityindex == NO_TARGET) {
		// No more cities can be supplied
		log_debug("[Company %d] No more cities can be supplied by %d", company->id, plane->id);
		activePlanes &= ~(1 << PLANE_INDEX(plane->id));
		pthread_kill(planeThreadId[PLANE_INDEX(plane->id)], SIGKILL);
		planeThreadId[PLANE_INDEX(plane->id)] = (pthread_t) -1;
		plane_free(plane);
		return;
	}
	// Set new distance from currentTargetId to newTaget
	log_debug("[Company %d] Plane %d has been redirected to city: %d --> distance: %d", company->id, \
			plane->id, bestCityindex, map->cityDistance[plane->cityIdFrom][bestCityindex]);
	plane->cityIdTo = bestCityindex;
	plane->distanceLeft = map->cityDistance[plane->cityIdFrom][bestCityindex];
}

int getScore(Plane* plane, int cityId) {
	int score = 0;
	for (int i = 0; i < plane->itemCount; i++) {
		if (plane->itemStock[i] > 0 && map->city[cityId]->itemStock[i] < 0) {
			score += min(plane->itemStock[i], -map->city[cityId]->itemStock[i]);
		}
	}
	return score;
}

