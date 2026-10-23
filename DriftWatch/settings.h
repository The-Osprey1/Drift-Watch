// Settings kept in flash (NVS) so they survive power loss and re-flashing the app.
#pragma once
#include <Arduino.h>

constexpr uint8_t BRIGHTNESS_LEVELS[] = {40, 100, 170, 255};
constexpr uint8_t BRIGHTNESS_LEVEL_COUNT = sizeof(BRIGHTNESS_LEVELS);

struct Settings {
  String tz;                    // POSIX time zone, e.g. "GMT0BST,M3.5.0/1,M10.5.0". Empty = detect from IP.
  bool use12h = false;
  uint8_t face = 0;
  uint8_t brightnessLevel = 2;  // index into BRIGHTNESS_LEVELS
  bool reducedMotion = false;   // slides and sweeps become instant cuts
};

extern Settings settings;

void loadSettings();
void saveSettings();
inline uint8_t backlightLevel() { return BRIGHTNESS_LEVELS[settings.brightnessLevel % BRIGHTNESS_LEVEL_COUNT]; }
