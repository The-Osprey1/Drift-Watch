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
}
