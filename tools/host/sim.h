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
extern bool touchFails;           // the touch chip stops answering (a dropped I2C read)
extern int16_t fingerX, fingerY;
extern int batteryMv;
extern int64_t wallAtZeroMs;      // wall-clock time (ms) when clockMs was 0, once the clock is set
extern uint32_t runUntil;         // end of the script step now running: a sleep never runs past it
extern std::vector<std::pair<uint32_t, uint8_t>> backlightLog;  // (ms, level)
extern int settingsSaves;

// Wi-Fi script, for the fake net in sim_net_fake.cpp (simulate_network uses the real net.cpp).
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

// Start reason and the watchdog: how long the loop went without feeding it.
extern const char *startReason;   // what hal::startReason() returns, nullptr = power on
extern uint32_t restarts;
extern uint32_t watchdogSeconds, lastFeed, longestUnfed;
extern int watchdogFeeds;
}
const char *simBleStatus();  // what the watch last put in the status characteristic
namespace sim {

// Motion sensor and screen-off waits.
extern bool imuPresent;
extern Accel accel;               // what the sensor reads now
extern int dozes, lowPowerDozes;  // screen-off waits, and how many could use light sleep
extern int motionDozes;           // waits with the motion line armed
extern uint64_t dozedMs;          // time spent in screen-off waits
extern int serialLost;          // serial bytes dropped because they arrived in light sleep
extern std::string serialSent;  // bytes the computer is sending; they arrive during the next wait
extern uint32_t imuLineEdges[2];

// Wake on motion. While armed, a change of more than the threshold on any axis from the pose
// at arming raises the event (STATUS1) and the INT2 line, `womLatencyMs` after the wait began
// (one sample at the 128 Hz wake-on-motion rate, plus waking up).
extern bool womArmed, womEvent;
extern Accel womRef;
extern float womThresholdG;
extern uint32_t womLatencyMs;
extern int womArms, womExits, womClears, imuReads;
extern bool womArmFails;          // the sensor does not finish the command handshake
extern bool womClearFails;        // STATUS1 cannot be read
extern bool womLineStuck;         // INT2 stays high whatever is cleared
extern int womSpurious;           // pulses on INT2 with no motion event behind them
extern uint32_t firstImuReadAfterWake;  // clock when the first sample after a motion wake was read

// Touch chip standby and its interrupt line.
extern bool touchStandby;         // automatic standby allowed (0xFE = 0)
extern bool touchStandbyOk;       // the chip accepts the standby setting
extern int touchStandbyWrites, touchReads, touchRestarts;
extern int touchResumeFailures;   // the next N "standby off" writes fail (the chip still waking)
extern bool touchPulse;           // a short interrupt pulse whose report is already gone
extern bool sleepFails;           // light sleep cannot be set up
}
