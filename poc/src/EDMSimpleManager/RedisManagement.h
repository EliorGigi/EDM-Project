#include "unistd.h"
#include "stdio.h"
#include <iostream>



void initializeServer(char* pathToConfig, char* port);
void setConfigFileForServer();
void createCluster();
void addServerToCluster();
void removeServerFromCluster();
void reshardCluster();