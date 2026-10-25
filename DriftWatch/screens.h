// Screens other than the watch faces: the paged settings screen, the details
// screen, the Wi-Fi setup screens and the startup sequence. Drawing and touch
// hit-testing share one layout, so a control is always exactly where it is drawn.
#pragma once
#include <TFT_eSPI.h>
#include <time.h>
#include "ble.h"
#include "net.h"
#include "theme.h"
#include "faces.h"

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
  HotSwap,
  // Quick Drawer & Tools
  Timer1m, Timer3m, Timer5m, Flashlight, TimerReset,
};
const char *buttonName(Button b);

enum class Screen : uint8_t { Settings, Details, Setup, SetTime, TouchCheck, PhoneSync, Drawer };

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
  bool reducedMotion;       // Motion Off
  uint8_t motion;           // MotionLevel, see settings.h
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
  int joinTry;              // 2 on the second try at joining Wi-Fi
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
  float tempC, chipTempC;       // ambient and chip temperatures (-99 if unmeasured)
  const char *version;
  const char *startReason;  // why the watch last started, nullptr after a normal power-on
  uint32_t restarts;        // restarts the watch did by itself on this charge
  const char *diag;         // diagnostics line, or nullptr
  Button pressed;
  uint32_t ms;              // drives the progress spinner
  uint16_t activeFaces;
  FaceTheme theme;
  // Quick Drawer & Timer
  bool timerActive;
  uint32_t timerRemainingMs;
  uint32_t timerTotalMs;
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
// Grouped into 4 category hubs: Display, Clock, Gestures, System.
constexpr int SETTINGS_ITEMS = 13;
Button settingsItem(int i);

enum class Hub : uint8_t { Display, Clock, Gestures, System };
constexpr int HUB_COUNT = 4;
Hub hubOfPage(int page, bool large = false);
const char *hubName(Hub h);
int hubFirstPage(Hub h, bool large = false);
Hub nextHub(Hub h);
Hub prevHub(Hub h);

// Where phone sync is.
enum class PhonePhase : uint8_t { Waiting, Connected, TimeSet, Closed, Failed };
PhonePhase phonePhase(const View &v);

// The low battery notice, drawn over the bottom of the face for a few seconds.
void drawLowBattery(TFT_eSprite &s, int pct, bool urgent);

// Startup sequence, t from 0 to 1.
void drawBoot(TFT_eSprite &s, float t, const char *version);

}  // namespace screens
