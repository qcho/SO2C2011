#include "controller/companyLogic.h"

#define PLANE_IS_ACTIVE(index)	!((inactivePlanes >> (index)) & 1)
#define HAS_ACTIVE_PLANES		(inactivePlanes ^ inactivePlanesMask)

void initializeCompany();
void updateMap();
void wakeUpPlanes();
void waitUntilPlanesReady();
void updateDestinations();
void updateServer();
void updateMapItems(Map* map, Plane* plane);
void setNewTarget(Map* map, Plane* plane);
float getScore(Plane* plane, int cityId, int* nextIfNoRoute);
int dfsDistanceTo(int from, int to, int* nextIfNoRoute);
int dfsDistance(int from, int to, int acumDistance, int info[2]);
void deactivatePlane(Plane* plane);
void createInactivePlanesBitMask();
char* getPlaneSemName(char *buffer, int index);

static Company *company;
static Map *map;
static pthread_t* planeThreadId;
static long inactivePlanesMask;
static long inactivePlanes;
// There should be no more than sizeof(activePlanes)*8 planes in this comapany.
int* visitedCities;		// Used by the DFS to find path between cities.

/*
 * 1 - Initialize company.
 * 2 - Update map.
 * 3 - Wake up planes.
 * 4 - Update planes.
 * 5 - Send map updates.
 */
void companyStart(Map* initialMap, Company* cmp) {
	company = cmp;
    char cmpSemName[10];
    sprintf(cmpSemName, "c%d", company->id);
	map = initialMap;
	initializeCompany();
	S_POST("companyReady");// Tell the server that this company has been created.
    createInactivePlanesBitMask();
	do {
		S_WAIT(cmpSemName);
		log_debug("[Company %d] Started turn---------------->", company->id);
		updateMap();
		wakeUpPlanes();
		waitUntilPlanesReady();
		updateDestinations();
        updateServer();
        log_debug("[Company %d] Finished turn---------------->", company->id);
		S_POST("server");
    } while (HAS_ACTIVE_PLANES);
	CompanyUpdatePackage update;
	update.companyId = company->id;
	update.status = FALSE;
    log_warning("Company %d died", company->id);
	serializer_write(&update, company->id + 1, SERVER_IPC_KEY, PACKAGE_TYPE_COMPANY_UPDATE);
    company_free(company, TRUE);
}

void createInactivePlanesBitMask() {
    inactivePlanesMask = 0;
    for (int i = 0; i < company->planeCount; i++) {
        inactivePlanesMask |= (1 << i);
    }
}

void initializeCompany() {
	inactivePlanes = 0;
	planeThreadId = malloc(sizeof(pthread_t) * company->planeCount);
    char semName[10];
    Plane* plane;
    sem_t* planeSem;
	for(int i = 0; i < company->planeCount; i++) {
        plane = company->plane[i];
	    planeSem = semaphore_create(getPlaneSemName(semName, i));
		pthread_create(planeThreadId + i, NULL, planeStart, plane);
        S_WAIT(semName);
	}
	visitedCities = malloc(sizeof(int) * map->cityCount);
	return;
}

/*
 * Reads all the updates bnroadcasted by the server and updates the company's map
 * 1 - for each message in the queue => apply update to map;
 */
void updateMap() {
    void* package;
    int packageType;
    CityUpdatePackage *update;
    do {
        package = serializer_read(company->id + 1, SERVER_IPC_KEY, &packageType);
        if (packageType == PACKAGE_TYPE_CITY_UPDATE) {
            update = (CityUpdatePackage*) package;
        	log_debug("[Company %d] update= cityId = %d - temId = %d - amoount = %d\n", company->id, update->cityId, update->itemId, update->amount);
            City *city = map->city[update->cityId];
            city->itemStock[update->itemId] += update->amount;
        }
    } while(package != NULL);
}

char* getPlaneSemName(char *buffer, int index) {
    sprintf(buffer, "c%d_p%d", company->id, PLANE_INDEX(company->plane[index]->id));
    return buffer;
}

void wakeUpPlanes(int semId) {
    char semName[10];
	for(int i = 0; i < company->planeCount; i++) {
		if (PLANE_IS_ACTIVE(i)) {
			S_POST(getPlaneSemName(semName, i));
		}
	}
}

void waitUntilPlanesReady(int semId) {
    char semName[10];
	for(int i = 0; i < company->planeCount; i++) {
		if (PLANE_IS_ACTIVE(i)) {
			S_WAIT(getPlaneSemName(semName, i));
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
	serializer_write(company, company->id + 1, SERVER_IPC_KEY, PACKAGE_TYPE_COMPANY);
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
            serializer_write(&update, PLANE_COMPANY_ID(plane->id) + 1, SERVER_IPC_KEY, PACKAGE_TYPE_CITY_UPDATE);
		}
	}
}

void setNewTarget(Map* map, Plane* plane) {
	int i, nextIfNoRoute;
	int newTargetScore;
	int bestCityScore = 0;
	int bestCityindex = NO_TARGET;
	for (i = 0; i < map->cityCount; i++) {
		if (i == plane->cityIdFrom) {
			// Skip if current city or route does not exists
			continue;
		}
		newTargetScore = getScore(plane, i, &nextIfNoRoute);
		if (bestCityScore < newTargetScore) {
			bestCityScore = newTargetScore;
			if (map->cityDistance[plane->cityIdFrom][i] == 0) {
				bestCityindex = nextIfNoRoute;
			} else {
				bestCityindex = i;
			}
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

float getScore(Plane* plane, int cityId, int* nextIfNoRoute) {
	float score = 0;
	int distance = map->cityDistance[plane->cityIdFrom][cityId];
	if (distance == 0) {
		log_debug("DOING A DFS!! %d -> %d", plane->cityIdFrom, cityId);
		int distance = dfsDistanceTo(plane->cityIdFrom, cityId, nextIfNoRoute);
		log_debug("PLANE From: %d, to: %d  - distance %d", plane->cityIdFrom, *nextIfNoRoute, distance);
	}
	for (int i = 0; i < plane->itemCount; i++) {
		if (plane->itemStock[i] > 0 && map->city[cityId]->itemStock[i] < 0) {
			score += min(plane->itemStock[i], -map->city[cityId]->itemStock[i]);
			score /= distance;
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

int dfsDistanceTo(int from, int to, int* nextIfNoRoute) {
	for(int i = 0; i < map->cityCount; i++) {
		visitedCities[i] = 0;
	}
	int info[2] = {0,0};
	dfsDistance(from, to, 0, info);
	*nextIfNoRoute = info[1];
	return info[0];
}

int dfsDistance(int from, int to, int acumDistance, int info[2]) {
	if (from == to) {
		info[0] = acumDistance;
		//printf("FOUND PATH: %d\n", info[0]);
		return 1;
	}
	//log_debug(LOG_JP, "from: %d to %d. Distance: %d", from, to, map->cityDistance[from][to]);
	visitedCities[from] = 1;
	int bestDist = -1, bestNext;
	for(int i = 0; i < map->cityCount; i++) {
		if (map->cityDistance[from][i] == 0 || visitedCities[i] == 1) {
			//log_debug("visitedCities[%d] = %d", i, visitedCities[i]);
			// Only go to unvisited neighbors!
			continue;
		}
		int currDist = acumDistance + map->cityDistance[from][i];
		//printf("going %d -> %d. Acum %d\n", from, i, currDist);
		int solutionFound = dfsDistance(i, to, currDist, info);
		if (solutionFound == 1 && (bestDist == -1 || info[0] < bestDist)) {
			bestDist = info[0];
			bestNext = (acumDistance == 0) ? i : from;
			info[1] = bestNext;
			//printf("NUEVA SOLUCION => %d\n", bestNext);
		}
	}

	visitedCities[from] = 0;
	return 0;
}

void deactivatePlane(Plane* plane) {
	inactivePlanes |= (1 << PLANE_INDEX(plane->id));
	pthread_cancel(planeThreadId[PLANE_INDEX(plane->id)]);
	planeThreadId[PLANE_INDEX(plane->id)] = (pthread_t) -1;
}

void company_closeIpc() {
    ipc_close(company->id + 1);
}
