// Wi-Fi setup hotspot and internet time sync, run as small state machines so
// the screen and touch keep working while they run. Call net::poll() often.
// Wi-Fi is only switched on while syncing or while the setup hotspot is open.
#pragma once
#include <stdint.h>
#include <time.h>

namespace net {
constexpr const char *SETUP_AP_NAME = "Drift-Setup";
constexpr const char *SETUP_ADDRESS = "192.168.4.1";

enum class Sync : uint8_t { Idle, Connecting, Zone, Clock, Done, Failed };
enum class Fail : uint8_t { None, NoNetworkSaved, JoinFailed, ClockFailed };
enum class Portal : uint8_t { Off, Waiting, PhoneJoined, Saved, Closed, TimedOut };

void begin();               // applies the saved time zone; no radio use
void poll();

void startSync();           // ignored while a sync or the hotspot is running
Sync syncState();
Fail failReason();
int syncStep();             // 1..3 while syncing, for "step 2 of 3" progress

void startPortal();         // stops any sync first
void cancelPortal();
Portal portalState();
uint32_t portalSecondsLeft();

bool hasSavedWifi();
bool timeIsValid();
time_t lastSync();
const char *zoneName();     // e.g. "Europe/London" when found from the internet, else ""
const char *ssid();         // saved network name, or ""
}
