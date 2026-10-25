// The few hardware services the interface needs. hal_esp32.cpp implements them
// on the watch; tools/host/hal_host.cpp fakes them for previews and tests.
#pragma once
#include <stdint.h>
#include <time.h>

namespace hal {
void begin();
uint32_t ms();
time_t now();                        // wall-clock seconds; kept by the chip, not by drawing
void idle(uint32_t ms);
void backlight(uint8_t level);       // 0 = off
int batteryMilliVolts();             // battery voltage after the divider is undone; < 0 if unreadable
bool bootButtonDown();
bool coldBoot();                     // true after power-on or reset, false after a crash restart
uint32_t freeHeap();
uint32_t freePsram();
void log(const char *fmt, ...);
void setTime(time_t t);              // sets the wall clock, for the manual time setting
int64_t nowMs();                     // wall clock in milliseconds
void setTimeMs(int64_t ms);
void adjustMs(int64_t ms);           // moves the wall clock by this much (drift correction)

// What ended a wait while the screen is off, as a set of bits: more than one can be true at
// once (a touch during a motion wake, say), and the caller gives touch and BOOT priority.
constexpr uint8_t WAKE_TIMER = 1, WAKE_MOTION = 2, WAKE_TOUCH = 4, WAKE_BUTTON = 8, WAKE_FAULT = 16;
struct DozeArm {
  bool lowPower = false;  // light sleep; otherwise a plain wait that only a timer ends
  bool motion = false;    // also wake when the motion sensor's INT2 line goes high
};
// Waits up to `ms` while the screen is off and returns the wake bits. In light sleep it wakes
// early on touch, BOOT or (if armed) motion. A line already at its wake level is not armed,
// so a stuck line cannot keep the chip busy; WAKE_FAULT means light sleep could not be set up or
// entered (the wait then still lasted, as a plain delay).
uint8_t doze(uint32_t ms, const DozeArm &arm);
}
