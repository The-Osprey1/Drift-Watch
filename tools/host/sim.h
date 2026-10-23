// Controls for the computer simulation: a fake clock, finger, BOOT button,
// battery and Wi-Fi, so the real interface code can be driven by scripts.
#pragma once
#include <stdint.h>
#include <time.h>
#include <vector>
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
extern int syncsStarted;
void setPortal(net::Portal p);
extern char zone[64];
}
