#include "controller/companyLogic.h"

#define PLANE_IS_ACTIVE(index)	!((inactivePlanes >> (index)) & 1)
#define HAS_ACTIVE_PLANES		(inactivePlanes ^ inactivePlanesMask)

int initializeCompany();
void updateMap();
void wakeUpPlanes(int semId);
void waitUntilPlanesReady(int semId);
void updateDestinations();
void updateServer();
void updateMapItems(Map* map, Plane* plane);
void setNewTarget(Map* map, Plane* plane);
int getScore(Plane* plane, int cityId);
void deactivatePlane(Plane* plane);
void createInactivePlanesBitMask();
static Company *company;
static Map *map;
static pthread_t* planeThreadId;
static long inactivePlanesMask;
static long inactivePlanes;
// There should be no more than sizeof(activePlanes)*8 planes in this comapany.

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
    createInactivePlanesBitMask();
	do {
		semaphore_decrement(serverSemId, company->id + 1);
		log_debug("[Company %d] Started turn---------------->", company->id);
		updateMap();
		wakeUpPlanes(planesSemId);
		waitUntilPlanesReady(planesSemId);
		updateDestinations();
        updateServer();
        log_debug("[Company %d] Finished turn---------------->", company->id);
		semaphore_increment(serverSemId, 0);
    } while (HAS_ACTIVE_PLANES);
	CompanyUpdatePackage update;
	update.companyId = company->id;
	update.status = FALSE;
	serializer_write_companyUpdate(&update, company->id + 1, SERVER_IPC_KEY);
    company_free(company, TRUE);
}

void createInactivePlanesBitMask() {
    inactivePlanesMask = 0;
    for (int i = 0; i < company->planeCount; i++) {
        inactivePlanesMask |= (1 << i);
    }
}

int initializeCompany() {
	inactivePlanes = 0;
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
	return turnsSemId;
}

/*
 * Reads all the updates bnroadcasted by the server and updates the company's map
 * 1 - for each message in the queue => apply update to map;
 */
void updateMap() {
    void* package;
    int packageType;
    CityUpdatePackage *update;
    do
    {
        package = serializer_read(company->id + 1, SERVER_IPC_KEY, &packageType);
        if (packageType == PACKAGE_TYPE_CITY_UPDATE) {
            update = (CityUpdatePackage*) package;
            City *city = map->city[update->cityId];
            city->itemStock[update->itemId] += update->amount;
        }
    } while(package != NULL);
}

void wakeUpPlanes(int semId) {
	for(int i = 0; i < company->planeCount; i++) {
		if (PLANE_IS_ACTIVE(i)) {
			semaphore_increment(semId, PLANE_INDEX(company->plane[i]->id) + 1);
		}
	}
}

void waitUntilPlanesReady(int semId) {
	for(int i = 0; i < company->planeCount; i++) {
		if (PLANE_IS_ACTIVE(i)) {
			semaphore_decrement(semId, 0);
		}
	}
}

void updateDestinations() {
	for(int i = 0; i < company->planeCount; i++) {
		if (PLANE_IS_ACTIVE(i) && company->plane[i]->distanceLeft == 0) {
			updateMapItems(map, company->plane[i]);
			setNewTarget(map, company->plane[i]);
		}
	}
}

/*
 * Write to the server the changes on this company.
 * In this case we serialize the whole company.
 */
void updateServer() {
    log_warning("Company %d writing update to server", company->id);
	serializer_write_company(company, company->id + 1, SERVER_IPC_KEY);
}
/*
 * Plane is supposed to just have arrived to its target.
 * 1 - City needs to be updated
 * 2 - Sends up-date package to the server.
 */
void  updateMapItems(Map* map, Plane* plane) {
	CityUpdatePackage update;
	for (int i = 0; i < plane->itemCount; ++i) {
		int cityStock = map->city[plane->cityIdFrom]->itemStock[i];
		int planeStock = plane->itemStock[i];
		if (cityStock < 0 && planeStock > 0) {
			int supplies = min(-cityStock, planeStock);
			plane->itemStock[i] -= supplies;
			map->city[plane->cityIdFrom]->itemStock[i] += supplies;
			update.cityId = plane->cityIdFrom;
            update.itemId = i;
            update.amount = supplies;
            serializer_write_cityUpdate(&update, PLANE_COMPANY_ID(plane->id) + 1, SERVER_IPC_KEY);
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
		deactivatePlane(plane);
		return;
	}
	// Set new distance from currentTargetId to newTaget
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
	// penalize score if other plane is going to that city
	for (int i = 0; i < company->planeCount; ++i) {
		if (company->plane[i]->cityIdTo == cityId) {
			score *= 0.7;
		}
	}
	return score;
}

void deactivatePlane(Plane* plane) {
	inactivePlanes |= (1 << PLANE_INDEX(plane->id));
	pthread_cancel(planeThreadId[PLANE_INDEX(plane->id)]);
	planeThreadId[PLANE_INDEX(plane->id)] = (pthread_t) -1;
}

void company_closeIpc() {
    ipc_close(company->id + 1);
}
