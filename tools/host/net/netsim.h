// Controls for the Wi-Fi, web and time-server stand-ins in this folder, so the real
// DriftWatch/net.cpp can be compiled on a computer and driven by net_test.cpp. Only the calls
// net.cpp makes are here, with the same names as the ESP32 Arduino core 2.0.17.
#pragma once
#include <stdint.h>
#include <functional>
#include <string>

namespace netsim {
// Radio.
extern int status;              // what WiFi.status() reports (WL_*)
extern int mode;                // last WiFi.mode() (WIFI_*)
extern int begins;              // WiFi.begin() calls
extern std::string flashSsid;   // the network saved in the radio's own flash
extern std::string flashPassword;
extern int stationsOnHotspot;   // phones joined to the setup hotspot
void dropConnection(uint8_t reason);  // the radio reports a disconnect with this reason

// Time zone lookup over HTTP.
extern bool httpBeginOk;
extern int httpCode;            // what GET() returns
extern std::string httpBody;
extern std::string httpUrl;     // last URL asked for
extern int httpGets;

// The lookup's background task: run at once, held until runTask(), or refused.
enum class TaskMode { RunNow, Hold, Refuse };
extern TaskMode taskMode;
extern int tasksStarted;
bool runTask();                 // runs a held task; false if none was held

// Time server.
extern int sntpStatus;          // what sntp_get_sync_status() reports
extern std::string configuredTz;  // the zone last passed to configTzTime()

// Setup hotspot (WiFiManager).
extern bool portalOpen;
extern std::string portalTitle;
void submitPortal(const char *tzField);  // a phone saves the setup page with this time zone field

void reset();                   // back to a fresh board: nothing saved, radio off
}
