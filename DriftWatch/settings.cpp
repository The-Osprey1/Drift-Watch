#include "settings.h"
#include <Preferences.h>

Settings settings;

void loadSettings() {
  Preferences p;
  p.begin("watch", true);
  settings.tz = p.getString("tz", "");
  settings.use12h = p.getBool("h12", false);
  settings.face = p.getUChar("face", 0);
  settings.brightnessLevel = p.getUChar("blevel", 2);
  settings.reducedMotion = p.getBool("lessmo", false);
  p.end();
}

void saveSettings() {
  Preferences p;
  p.begin("watch", false);
  p.putString("tz", settings.tz);
  p.putBool("h12", settings.use12h);
  p.putUChar("face", settings.face);
  p.putUChar("blevel", settings.brightnessLevel);
  p.putBool("lessmo", settings.reducedMotion);
  p.end();
}
