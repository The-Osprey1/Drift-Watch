// Wi-Fi setup hotspot and internet time sync, run as small state machines so
// the screen and touch keep working while they run. Call net::poll() often.
// Wi-Fi is only switched on while syncing or while the setup hotspot is open.
#pragma once
#include <stdint.h>
#include <time.h>

namespace net {
constexpr const char *SETUP_AP_NAME = "Drift-Setup";
constexpr const char *SETUP_ADDRESS = "192.168.4.1";

// Three separate facts: whether the clock is valid (timeIsValid), whether a sync
// is running and at which stage (syncState), and how the latest finished attempt
// ended (lastResult, failReason). A failed sync never clears a valid clock.
enum class Sync : uint8_t { Idle, Connecting, Zone, Clock };
enum class Result : uint8_t { None, Ok, Failed };
enum class Fail : uint8_t { None, NoNetworkSaved, JoinFailed, ClockFailed };
enum class Portal : uint8_t { Off, Waiting, PhoneJoined, Saved, Closed, TimedOut };
// Why joining failed, read from the radio's own disconnect reason and grouped by what the
// person can do about it. The watch only has a 2.4 GHz radio, so a 5 GHz-only network
// shows up as NotFound.
enum class JoinWhy : uint8_t { NoAnswer, NotFound, Password, Refused };

void begin();               // applies the saved time zone; no radio use
void poll();

void startSync();           // ignored while a sync or the hotspot is running
void cancelSync();          // stops a running sync; the previous result stands
Sync syncState();           // stage of the running sync, Idle when none is running
bool syncRunning();
Result lastResult();        // of the latest attempt that finished
Fail failReason();          // why the latest attempt failed, None if it did not
uint16_t attempts();        // counts finished attempts, so callers can see a new result
int syncStep();             // 1..3 while syncing, for "step 2 of 3" progress
int joinTry();              // 2 while joining for the second time (one retry before a join fails), else 1
JoinWhy joinWhy();          // for the latest JoinFailed
int joinCode();             // the radio's reason number behind it (0 = none reported), for the log and details
bool radioOn();             // Wi-Fi is switched on: a sync or the hotspot is running

void startPortal();         // stops any sync first
void cancelPortal();
Portal portalState();
uint32_t portalSecondsLeft();

void saveWifi(const char *ssid, const char *password);
bool hasSavedWifi();
bool timeIsValid();
void setClockByHand(time_t t);  // the time set on the watch itself; a later sync replaces it
// A time sent by a phone over Bluetooth counts as a sync, like one over Wi-Fi.
void setClockFromPhone(time_t utc, int ms);
enum class Source : uint8_t { None, Wifi, Phone };
Source lastSource();            // where the latest successful sync came from
// Sets the POSIX time zone (saved) and/or the zone name shown in Details; nullptr leaves one as it is.
void setZone(const char *posix, const char *name);
// Saves a network to join, as the setup page does. Call startSync() to join it.
void saveNetwork(const char *ssid, const char *password);
int32_t driftPpb();             // the clock's learned error rate (see timekeep.h)
bool clockSetByHand();          // true until the next successful sync
time_t lastSync();
const char *zoneName();     // e.g. "Europe/London" when found from the internet, else ""
const char *ssid();         // saved network name, or ""
}
