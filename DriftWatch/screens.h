// Screens other than the watch faces: the paged settings screen, the details
// screen, the Wi-Fi setup screens and the startup sequence. Drawing and touch
// hit-testing share one layout, so a control is always exactly where it is drawn.
#pragma once
#include <TFT_eSPI.h>
#include <time.h>
#include "ble.h"
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
  // Added in v0.9.0: settings, then the set time screen
  SetTime, RaiseWake, Minus, Plus, SaveTime,
  // Added in v0.10.0
  PhoneSync, Night, ScreenTurn, TouchCheck,
};
const char *buttonName(Button b);

enum class Screen : uint8_t { Settings, Details, Setup, SetTime, TouchCheck, PhoneSync };

// Where the touch check is: tapping one of the two dots, or showing how it went.
enum class CheckResult : uint8_t { Tapping, Matched, NoMatch };

// The set time screen has one page per field, then a page to save.
enum class TimeField : uint8_t { Hour, Minute, Day, Month, Year, Save };
constexpr int TIME_PAGES = 6;
constexpr int YEAR_MIN = 2024, YEAR_MAX = 2099;
// Steps one field of `t` up or down, wrapping within its range. Only that field changes,
// except the day, which is kept inside the month. Normalises the weekday.
void stepTime(tm &t, TimeField f, int dir);

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
  net::JoinWhy joinWhy;     // why the latest join failed
  int joinCode;             // the radio's reason number, 0 if none
  int syncStep;
  net::Portal portal;
  uint32_t secondsLeft;     // hotspot time left
  const char *lastSyncText; // e.g. "14:02", or "" if never synced
  const char *nowText;      // the clock now, e.g. "20:48"
  const char *zone, *ssid;
  bool zoneManual;          // the time zone was typed in setup
  bool clockByHand;         // the clock was set on the watch and not synced since
  bool raiseToWake, twistToWake;
  bool imuPresent;          // the motion sensor answered
  tm edit;                  // the time being set on the set time screen
  // v0.10.0
  uint8_t screenTurn;       // 0..3, see orient.h
  bool touchChecked;        // the touch check was done for this screen turn
  uint8_t night;            // night mode choice, see settings.h
  int checkStep;            // touch check: which dot (0 or 1)
  CheckResult checkResult;
  ble::State ble;           // phone sync
  const char *bleName;
  uint32_t bleSecondsLeft;
  bool phoneGotTime, phoneWifiSaved;
  const char *phoneSsid;    // network the phone sent
  const char *phoneError;   // the last line the watch refused, or nullptr
  float batteryHours;       // measured time left, < 0 while measuring
  bool charging;
  long driftPpb;            // learned clock error
  net::Source syncSource;
  const char *syncAgo;      // e.g. "3 h ago", "" if never
  uint32_t imuLine1, imuLine2;  // edges seen on the motion sensor's interrupt lines
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
constexpr int SETTINGS_ITEMS = 12;
Button settingsItem(int i);

// Where phone sync is.
enum class PhonePhase : uint8_t { Waiting, Connected, TimeSet, Closed, Failed };
PhonePhase phonePhase(const View &v);

// Startup sequence, t from 0 to 1.
void drawBoot(TFT_eSprite &s, float t, const char *version);

}  // namespace screens
