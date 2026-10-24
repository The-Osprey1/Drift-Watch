// Controls for the computer simulation: a fake clock, finger, BOOT button,
// battery and Wi-Fi, so the real interface code can be driven by scripts.
#pragma once
#include <stdint.h>
#include <time.h>
#include <vector>
#include "imu.h"
#include "net.h"

namespace sim {
extern uint32_t clockMs;          // advanced by hal::idle()
extern bool fingerDown, bootDown, cold, clockSet;
extern int16_t fingerX, fingerY;
extern int batteryMv;
extern time_t epochAtZero;        // wall-clock time when clockMs was 0
extern std::vector<std::pair<uint32_t, uint8_t>> backlightLog;  // (ms, level)
extern int settingsSaves;

// Wi-Fi script.
extern net::Fail syncOutcome;     // None = success
extern bool wifiSaved;
extern int syncsStarted, syncsCancelled;
void setPortal(net::Portal p);
extern char zone[64];
extern net::JoinWhy joinWhy;      // reported with a scripted JoinFailed

// Motion sensor and screen-off waits.
extern bool imuPresent;
extern Accel accel;               // what the sensor reads now
extern int dozes, lowPowerDozes;  // screen-off waits, and how many could use light sleep
}
