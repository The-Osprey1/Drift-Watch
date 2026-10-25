// Settings kept in flash (NVS) so they survive power loss and re-flashing the app.
#pragma once
#include <Arduino.h>

constexpr uint8_t BRIGHTNESS_LEVELS[] = {40, 100, 170, 255};
constexpr uint8_t BRIGHTNESS_LEVEL_COUNT = sizeof(BRIGHTNESS_LEVELS);

// Bumped when a stored value changes meaning. Version 1 is v0.7.0 and earlier,
// which stored no version; version 2 (v0.8.0) adds large text; version 3 (v0.9.0) adds
// raise-to-wake and which way the screen faces relative to the motion sensor; version 4
// (v0.10.0) adds the screen turn, the checked touch map, night mode, the learned clock drift
// and the last time the clock was known; version 5 (v0.12.0) turns the two-way Motion setting
// into three levels.
constexpr uint8_t SETTINGS_VERSION = 5;

// How much the interface moves. Off: instant changes, no entrances, hands and the radar step once
// a second. Subtle (the default): slides, settles and one short entrance after choosing a face.
// Expressive: as Subtle, with each entrance played half as long again.
enum MotionLevel : uint8_t { MOTION_OFF, MOTION_SUBTLE, MOTION_EXPRESSIVE, MOTION_LEVELS };

// Night mode windows, in hours: raise-to-wake is off inside the window. Index 0 = night mode off.
constexpr uint8_t NIGHT_FROM[] = {0, 22, 23, 0};
constexpr uint8_t NIGHT_TO[] = {0, 7, 7, 8};
constexpr uint8_t NIGHT_CHOICES = sizeof(NIGHT_FROM);
constexpr uint8_t TOUCH_NOT_CHECKED = 0xFF;

struct Settings {
  String tz;                    // POSIX time zone, e.g. "GMT0BST,M3.5.0/1,M10.5.0". Empty = detect from IP.
  bool use12h = false;
  uint8_t face = 0;
  uint8_t brightnessLevel = 2;  // index into BRIGHTNESS_LEVELS
  uint8_t motion = MOTION_SUBTLE;  // MotionLevel; Off makes slides and sweeps instant cuts
  bool largeText = false;       // bigger text on the settings and setup screens; faces are unchanged
  bool raiseToWake = true;      // lifting the wrist turns the screen on
  bool twistToWake = true;      // so does a quick wrist twist out and back
  int8_t screenFacing = 0;      // +1 or -1 once learned (see RaiseDetector), 0 = use the board default
  uint8_t screenTurn = 0;       // TFT_eSPI rotation 0..3; loadSettings starts it at DISPLAY_ROTATION
  uint8_t touchMap = 0;         // orient:: map from raw touch to the screen, found by the touch check
  uint8_t touchCheckedTurn = TOUCH_NOT_CHECKED;  // the screen turn touchMap was checked for
  uint8_t night = 0;            // index into NIGHT_FROM / NIGHT_TO
  int32_t driftPpb = 0;         // learned clock error in parts per billion, + = the watch runs fast
  int64_t lastKnownTime = 0;    // wall clock when last saved, a starting point for Set time after power loss
};

extern Settings settings;

void loadSettings();
void saveSettings();
// Brings values read from an older version into range, keeping the user's choices.
// Returns the version the settings are now at.
uint8_t migrateSettings(Settings &s, uint8_t storedVersion);
// The touch map to use now: the checked one if it was checked for this screen turn.
uint8_t activeTouchMap();
bool touchChecked();
// True when night mode is on and the hour falls inside its window.
bool nightNow(int hour);
inline uint8_t backlightLevel() { return BRIGHTNESS_LEVELS[settings.brightnessLevel % BRIGHTNESS_LEVEL_COUNT]; }
