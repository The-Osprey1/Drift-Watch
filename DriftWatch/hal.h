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

// What ended a wait while the screen is off.
enum class WakeCause : uint8_t { Timer, Touch, Button };
// Waits up to `ms` while the screen is off. With `lowPower` the chip goes into light sleep
// and wakes early on a touch or the BOOT button; without it this is a plain wait.
WakeCause doze(uint32_t ms, bool lowPower);
}
