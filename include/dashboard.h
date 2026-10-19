#ifndef DASHBOARD_H
#define DASHBOARD_H

void dashboardSetup();
void dashboardUpdate();

bool dashboardOccupancyOnline();
int dashboardOccupancyEventCount();
bool dashboardOccupancyEventActive();
float dashboardOccupancyChangePercent();
String dashboardOccupancySenderIP();
String dashboardOccupancyLastEventTime();

#endif
