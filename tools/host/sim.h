// Controls for the computer simulation: a fake clock, finger, BOOT button,
// battery and Wi-Fi, so the real interface code can be driven by scripts.
#pragma once
#include <stdint.h>
#include <time.h>
#include <string>
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

// Screen turn: how the simulated panel really turns the picture for setRotation(1). true is
// what orient.h expects (a quarter turn clockwise); false tests the other possibility, where
// only the touch check can line touch up. The finger positions in the scripts are always
// where the finger lands on the picture as drawn; the touch chip reports them in the panel's
// own frame.
extern bool panelTurnsClockwise;
extern uint8_t touchMount;        // orient:: map from the panel's frame to the touch chip's (0 = lined up)
void rawTouch(int x, int y, int &rx, int &ry);  // what the touch chip reports for a finger at (x, y)

// Bluetooth: lines a phone has written, and whether one is connected.
extern std::vector<std::string> bleLines;
extern bool bleConnected, bleStarts;  // bleStarts = false makes Bluetooth fail to start
extern int bleOpens;
}
const char *simBleStatus();  // what the watch last put in the status characteristic
namespace sim {

// Motion sensor and screen-off waits.
extern bool imuPresent;
extern Accel accel;               // what the sensor reads now
extern int dozes, lowPowerDozes;  // screen-off waits, and how many could use light sleep
extern uint32_t imuLineEdges[2];
}
