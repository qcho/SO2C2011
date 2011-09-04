#include "main.h"

void startSimulation();
void initEnvironment();
void startSimulationDisplayer();

int main() {
    initEnvironment();
    startSimulationDisplayer();
    startSimulation();
    printf("\n\nSimulation Done!\n\n");
	return 0;
}

void startSimulation() {
    pid_t pId;
    // Initialize companies
    for (int i = 0; i < map->companyCount; i++) {
        switch((pId = fork())) {
            case 0:
                signal_createHandlerThread(FALSE);
                companyStart(map->company[i]);
                break;
            case ERROR:
                fatal("Fork Error");
                break;
            default:
                childPid[i] = pId;
                break;
        }
    }
    for (int i = 0; i < map->companyCount; ++i) {
        wait(NULL);
    }
    signal_abortSimulation(1);
}

void initEnvironment() {
    signal_createHandlerThread(TRUE);
    parseMap("resources/loads/ciudades.txt");
    map_addCompany(parseCompany("resources/loads/empresa.txt", 123456));
    childPid = malloc(sizeof(int) * (map->companyCount + 1));
    return;
}

void startSimulationDisplayer() {
    pid_t pId;
    switch ((pId = fork())) {
        case 0:
            signal_createHandlerThread(FALSE);
            displaySimulation();
            _exit(0);
            break;
        case ERROR:
            fatal("Error forking UI");
            break;
        default:
            childPid[map->companyCount] = pId;
            break;
    }
}
