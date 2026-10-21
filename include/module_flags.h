#ifndef MODULE_FLAGS_H
#define MODULE_FLAGS_H

// Defaults for the module-enable flags; override per environment with -DCFG_<NAME>=0|1.
#ifndef CFG_VIBRATION_CONNECTED
#define CFG_VIBRATION_CONNECTED 0
#endif
#ifndef CFG_RTC_CONNECTED
#define CFG_RTC_CONNECTED 0
#endif
#ifndef CFG_RADAR_CONNECTED
#define CFG_RADAR_CONNECTED 0
#endif
#ifndef CFG_RADAR_SERVER_CONNECTED
#define CFG_RADAR_SERVER_CONNECTED 0
#endif
#ifndef CFG_WATER_QUALITY_CONNECTED
#define CFG_WATER_QUALITY_CONNECTED 1
#endif
#ifndef CFG_DASHBOARD_CONNECTED
#define CFG_DASHBOARD_CONNECTED 1
#endif

#endif
