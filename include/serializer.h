#ifndef SERIALIZER_H_
#define SERIALIZER_H_

#include "common.h"
#include "map.h"


typedef enum {
	CITY_STOCK = 0,
	COMPANY_KILL = 1
} msg_t;

typedef struct {
	int cityId;
	int itemId;
	int amount;
} CityUpdatePackage;

typedef struct {
	int companyId;
	int status;			// ON or OFF
} CompanyUpdatePackage;

#define MSG "%3d_%3d_%3d"

int serializer_write_cityUpdate(CityUpdatePackage* msg, int from, int to);

int serializer_write_companyUpdate(CompanyUpdatePackage* msg, int from, int to);

#endif