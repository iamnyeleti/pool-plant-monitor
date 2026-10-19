#ifndef NETWORK_H
#define NETWORK_H

#include <WebServer.h>

void networkSetup();
WebServer& networkServer();
void networkBegin();
void networkUpdate();

#endif
