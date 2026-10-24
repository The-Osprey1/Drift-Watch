// Screens other than the watch faces: the paged settings screen, the details
// screen, the Wi-Fi setup screens and the startup sequence. Drawing and touch
// hit-testing share one layout, so a control is always exactly where it is drawn.
#pragma once
#include <TFT_eSPI.h>
#include "net.h"

namespace screens {

enum class Button : uint8_t {
  None,
  // Settings pages
  Brightness, TimeFormat, Motion, SyncNow, WifiSetup, Details,
  // Details
  TextSize,
  // Wi-Fi setup
  Cancel, Back, Retry, EditWifi, TryAgain, Done,
};
const char *buttonName(Button b);

enum class Screen : uint8_t { Settings, Details, Setup };

struct Rect {
  int16_t x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
// What is drawn and what responds to touch, set separately for every control.
struct Control {
  Button id;
  Rect visible, hit;
};

// Everything a screen shows. Filled in by ui.cpp, or by the tests.
struct View {
  Screen screen;
  int page;                 // clamped to the screen's page count
  bool large;               // large text
  bool reducedMotion;
  int batteryPct;           // < 0 if unreadable
  int batteryMv;
  uint8_t brightnessLevel;  // 0..3
  bool use12h;
  bool timeValid;
  net::Sync stage;          // Idle when no sync is running
  net::Result result;       // latest finished attempt
  net::Fail fail;
  int syncStep;
  net::Portal portal;
  uint32_t secondsLeft;     // hotspot time left
  const char *lastSyncText; // e.g. "14:02", or "" if never synced
  const char *zone, *ssid;
  bool zoneManual;          // the time zone was typed in setup
  const char *version;
  const char *diag;         // diagnostics line, or nullptr
  Button pressed;
  uint32_t ms;              // drives the progress spinner
};

// Draws the screen. Returns true if it shows a spinner that needs more frames.
bool draw(TFT_eSprite &s, const View &v);
int pageCount(TFT_eSprite &s, const View &v);
// The controls on the shown page. Returns how many were written to `out`.
int controls(TFT_eSprite &s, const View &v, Control *out, int max);
Button hit(TFT_eSprite &s, const View &v, int x, int y);
// The page that holds button `b` on this screen and text size, or -1.
int pageOf(TFT_eSprite &s, View v, Button b);

// Where the Wi-Fi setup flow is.
enum class SetupPhase : uint8_t { Hotspot, Connecting, Done, Failed, TimedOut };
SetupPhase setupPhase(const View &v);

// Settings items in page order; two per page, or one with large text.
constexpr int SETTINGS_ITEMS = 6;
Button settingsItem(int i);

// Startup sequence, t from 0 to 1.
void drawBoot(TFT_eSprite &s, float t, const char *version);

}  // namespace screens
