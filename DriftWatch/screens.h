// Screens other than the watch faces: the status and settings panel, the
// Wi-Fi setup screens and the startup sequence. Drawing and touch hit-testing
// share one layout table, so a button is always exactly where it is drawn.
#pragma once
#include <TFT_eSPI.h>
#include "net.h"

namespace screens {

enum class Button : uint8_t { None, Light, Clock, Motion, Sync, WifiSetup, Cancel, Retry, Close, Done };

struct PanelView {
  int batteryPct;           // < 0 if unreadable
  int batteryMv;
  uint8_t brightnessLevel;  // 0..3
  bool use12h, reducedMotion, timeValid;
  net::Sync sync;
  net::Fail fail;
  int syncStep;
  const char *lastSyncText; // e.g. "14:02", or "" if never synced
  const char *zone, *ssid;
  const char *diag;         // diagnostics line, or nullptr
  Button pressed;
  uint32_t ms;              // drives the progress spinner
};
void drawPanel(TFT_eSprite &s, const PanelView &v);
Button hitPanel(int x, int y);

struct SetupView {
  net::Portal portal;
  net::Sync sync;
  net::Fail fail;
  int syncStep;
  uint32_t secondsLeft;
  const char *lastSyncText;
  Button pressed;
  uint32_t ms;
  bool reducedMotion;
};
void drawSetup(TFT_eSprite &s, const SetupView &v);
Button hitSetup(const SetupView &v, int x, int y);
// True when the setup flow has finished, one way or another, and only needs a tap to leave.
bool setupFinished(const SetupView &v);

// Startup sequence, t from 0 to 1.
void drawBoot(TFT_eSprite &s, float t, const char *version);

}  // namespace screens
