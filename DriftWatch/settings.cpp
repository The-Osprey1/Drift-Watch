#include "settings.h"
#include <Preferences.h>
#include "board.h"
#include "orient.h"

Settings settings;

uint8_t migrateSettings(Settings &s, uint8_t storedVersion) {
  // Version 1 to 2: nothing changes meaning; large text is new and starts off.
  // Version 2 to 3: raise-to-wake is new and starts on; the screen direction is not known yet.
  // Version 3 to 4: the screen turn starts at the board default (loadSettings), so the touch
  // check runs once on the first start; night mode starts off and no drift is known.
  // Version 4 to 5: Motion Reduced becomes Off and Full becomes Subtle (loadSettings reads the
  // old key when the new one is missing).
  // Version 5 to 6: no zone found by Auto is known yet; the next lookup saves one.
  // Values are range-checked for every version, so a damaged entry cannot crash a face lookup.
  (void)storedVersion;
  if (s.brightnessLevel >= BRIGHTNESS_LEVEL_COUNT) s.brightnessLevel = 2;
  if (s.screenFacing != 1 && s.screenFacing != -1) s.screenFacing = 0;
  s.screenTurn %= orient::TURNS;
  if (s.touchMap >= orient::MAPS || s.touchCheckedTurn >= orient::TURNS) {
    s.touchMap = 0;
    s.touchCheckedTurn = TOUCH_NOT_CHECKED;
  }
  if (s.night >= NIGHT_CHOICES) s.night = 0;
  if (s.motion >= MOTION_LEVELS) s.motion = MOTION_SUBTLE;
  if ((s.activeFaces & ALL_FACES_MASK) == 0) s.activeFaces = ALL_FACES_MASK;
  if (s.driftPpb > 30000000 || s.driftPpb < -30000000) s.driftPpb = 0;  // over 3%: not a real clock error
  return SETTINGS_VERSION;
}

void loadSettings() {
  Preferences p;
  p.begin("watch", true);
  uint8_t version = p.isKey("ver") ? p.getUChar("ver", 1) : 1;  // missing on v0.7.0 and earlier
  settings.tz = p.getString("tz", "");
  settings.autoTz = p.isKey("atz") ? p.getString("atz", "") : "";
  settings.autoZone = p.isKey("azone") ? p.getString("azone", "") : "";
  settings.use12h = p.getBool("h12", false);
  settings.face = p.getUChar("face", 0);
  settings.brightnessLevel = p.getUChar("blevel", 2);
  // v0.11 and earlier kept only "lessmo" (reduced motion on or off).
  if (p.isKey("motion")) settings.motion = p.getUChar("motion", MOTION_SUBTLE);
  else settings.motion = p.getBool("lessmo", false) ? MOTION_OFF : MOTION_SUBTLE;
  settings.largeText = p.isKey("bigtext") && p.getBool("bigtext", false);
  settings.raiseToWake = !p.isKey("raise") || p.getBool("raise", true);
  settings.twistToWake = !p.isKey("twist") || p.getBool("twist", true);
  settings.screenFacing = p.isKey("facing") ? p.getChar("facing", 0) : 0;
  settings.screenTurn = p.isKey("turn") ? p.getUChar("turn", DISPLAY_ROTATION) : DISPLAY_ROTATION;
  settings.touchMap = p.getUChar("tmap", 0);
  settings.touchCheckedTurn = p.isKey("tturn") ? p.getUChar("tturn", TOUCH_NOT_CHECKED) : TOUCH_NOT_CHECKED;
  settings.night = p.getUChar("night", 0);
  settings.driftPpb = p.getInt("drift", 0);
  settings.lastKnownTime = p.getLong64("known", 0);
  settings.activeFaces = p.isKey("faces") ? p.getUShort("faces", ALL_FACES_MASK) : ALL_FACES_MASK;
  if ((settings.activeFaces & ALL_FACES_MASK) == 0) settings.activeFaces = ALL_FACES_MASK;
  p.end();
  migrateSettings(settings, version);
}

void saveSettings() {
  Preferences p;
  p.begin("watch", false);
  p.putUChar("ver", SETTINGS_VERSION);
  p.putString("tz", settings.tz);
  p.putString("atz", settings.autoTz);
  p.putString("azone", settings.autoZone);
  p.putBool("h12", settings.use12h);
  p.putUChar("face", settings.face);
  p.putUChar("blevel", settings.brightnessLevel);
  p.putUChar("motion", settings.motion);
  p.putBool("lessmo", settings.motion == MOTION_OFF);  // so older firmware flashed back keeps the choice
  p.putBool("bigtext", settings.largeText);
  p.putBool("raise", settings.raiseToWake);
  p.putBool("twist", settings.twistToWake);
  p.putChar("facing", settings.screenFacing);
  p.putUChar("turn", settings.screenTurn);
  p.putUChar("tmap", settings.touchMap);
  p.putUChar("tturn", settings.touchCheckedTurn);
  p.putUChar("night", settings.night);
  p.putInt("drift", settings.driftPpb);
  p.putLong64("known", settings.lastKnownTime);
  p.putUShort("faces", settings.activeFaces);
  p.end();
}

bool touchChecked() { return settings.touchCheckedTurn == settings.screenTurn; }

uint8_t activeTouchMap() { return touchChecked() ? settings.touchMap : orient::expectedTouchMap(settings.screenTurn); }

bool nightNow(int hour) {
  if (!settings.night) return false;
  int from = NIGHT_FROM[settings.night], to = NIGHT_TO[settings.night];
  return from < to ? hour >= from && hour < to : hour >= from || hour < to;
}
