#include "settings.h"
#include <Preferences.h>

Settings settings;

uint8_t migrateSettings(Settings &s, uint8_t storedVersion) {
  // Version 1 to 2: nothing changes meaning; large text is new and starts off.
  // Version 2 to 3: raise-to-wake is new and starts on; the screen direction is not known yet.
  // Values are range-checked for every version, so a damaged entry cannot crash a face lookup.
  (void)storedVersion;
  if (s.brightnessLevel >= BRIGHTNESS_LEVEL_COUNT) s.brightnessLevel = 2;
  if (s.screenFacing != 1 && s.screenFacing != -1) s.screenFacing = 0;
  return SETTINGS_VERSION;
}

void loadSettings() {
  Preferences p;
  p.begin("watch", true);
  uint8_t version = p.isKey("ver") ? p.getUChar("ver", 1) : 1;  // missing on v0.7.0 and earlier
  settings.tz = p.getString("tz", "");
  settings.use12h = p.getBool("h12", false);
  settings.face = p.getUChar("face", 0);
  settings.brightnessLevel = p.getUChar("blevel", 2);
  settings.reducedMotion = p.getBool("lessmo", false);
  settings.largeText = p.isKey("bigtext") && p.getBool("bigtext", false);
  settings.raiseToWake = !p.isKey("raise") || p.getBool("raise", true);
  settings.screenFacing = p.isKey("facing") ? p.getChar("facing", 0) : 0;
  p.end();
  migrateSettings(settings, version);
}

void saveSettings() {
  Preferences p;
  p.begin("watch", false);
  p.putUChar("ver", SETTINGS_VERSION);
  p.putString("tz", settings.tz);
  p.putBool("h12", settings.use12h);
  p.putUChar("face", settings.face);
  p.putUChar("blevel", settings.brightnessLevel);
  p.putBool("lessmo", settings.reducedMotion);
  p.putBool("bigtext", settings.largeText);
  p.putBool("raise", settings.raiseToWake);
  p.putChar("facing", settings.screenFacing);
  p.end();
}
