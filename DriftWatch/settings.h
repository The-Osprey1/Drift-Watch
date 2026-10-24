// Settings kept in flash (NVS) so they survive power loss and re-flashing the app.
#pragma once
#include <Arduino.h>

constexpr uint8_t BRIGHTNESS_LEVELS[] = {40, 100, 170, 255};
constexpr uint8_t BRIGHTNESS_LEVEL_COUNT = sizeof(BRIGHTNESS_LEVELS);

// Bumped when a stored value changes meaning. Version 1 is v0.7.0 and earlier,
// which stored no version; version 2 (v0.8.0) adds large text; version 3 (v0.9.0) adds
// raise-to-wake and which way the screen faces relative to the motion sensor.
constexpr uint8_t SETTINGS_VERSION = 3;

struct Settings {
  String tz;                    // POSIX time zone, e.g. "GMT0BST,M3.5.0/1,M10.5.0". Empty = detect from IP.
  bool use12h = false;
  uint8_t face = 0;
  uint8_t brightnessLevel = 2;  // index into BRIGHTNESS_LEVELS
  bool reducedMotion = false;   // slides and sweeps become instant cuts
  bool largeText = false;       // bigger text on the settings and setup screens; faces are unchanged
  bool raiseToWake = true;      // lifting the wrist turns the screen on
  int8_t screenFacing = 0;      // +1 or -1 once learned (see RaiseDetector), 0 = use the board default
};

extern Settings settings;

void loadSettings();
void saveSettings();
// Brings values read from an older version into range, keeping the user's choices.
// Returns the version the settings are now at.
uint8_t migrateSettings(Settings &s, uint8_t storedVersion);
inline uint8_t backlightLevel() { return BRIGHTNESS_LEVELS[settings.brightnessLevel % BRIGHTNESS_LEVEL_COUNT]; }
