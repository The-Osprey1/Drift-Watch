// Shared state and helpers across the interface units (ui.cpp, ui_render.cpp, ui_motion.cpp,
// ui_input.cpp). Not part of the public interface (ui.h).
#pragma once
#include <TFT_eSPI.h>
#include <time.h>
#include "battery.h"
#include "board.h"
#include "faces.h"
#include "net.h"
#include "power.h"
#include "raise.h"
#include "screens.h"
#include "touch.h"

namespace uiparts {

constexpr uint32_t SCREEN_TIMEOUT_MS = 15000;
constexpr uint32_t RESULT_TIMEOUT_MS = 30000;          // a setup result stays up longer, to be read and acted on
constexpr uint32_t RESYNC_MS = 6UL * 60 * 60 * 1000;   // every 6 hours
constexpr uint32_t RETRY_SYNC_MS = 30UL * 60 * 1000;   // after a failed sync; doubles with each failure in a row
constexpr uint32_t BATTERY_READ_MS = 10000;
constexpr uint32_t BATTERY_READ_ASLEEP_MS = 60000;     // screen off: the estimate needs no more, and the sleeps get longer
constexpr uint32_t SAVE_DELAY_MS = 2000;               // batch settings writes to spare the flash
constexpr uint32_t ANIM_FRAME_MS = 16;                 // frame cap while animating; the push is the real limit
constexpr uint32_t SPINNER_FRAME_MS = 40;
constexpr uint32_t LOW_NOTICE_MS = 5000;               // the low battery notice stays this long on the face
constexpr uint32_t BOOT_HOLD_MS = 350;                 // hero hold + smooth implosion at end of boot
constexpr uint32_t HOLD_HINT_MS = 200;                 // a still press starts showing the hold ring
constexpr uint32_t FLICK_WINDOW_MS = 80;               // finger must still be moving this recently to flick
constexpr uint32_t REPEAT_DELAY_MS = 450;              // holding - or + starts repeating after this
constexpr uint32_t REPEAT_MS = 120, REPEAT_FAST_MS = 50;  // then this often, faster after 8 steps
constexpr uint32_t IMU_AWAKE_MS = 80;                  // motion samples while the screen is on, for wrist-down sleep
constexpr uint32_t CHECK_RESULT_MS = 1400;             // touch check result stays up this long
constexpr uint32_t BOOT_CHECK_MS = 4000;               // holding BOOT this long opens the touch check anywhere
constexpr uint32_t KNOWN_TIME_SAVE_S = 3600;           // remember the clock in flash at most hourly
constexpr uint32_t TOUCH_DROPOUT_MS = 50;              // failed touch reads this long end a touch, without acting
constexpr uint32_t ALARM_FLASH_MS = 200;               // the timer alarm flips between bright and dim this often
constexpr uint32_t ALARM_MS = 30000;                   // an alarm nobody dismisses stops by itself
constexpr uint32_t FLASHLIGHT_MS = 2UL * 60 * 1000;    // the flashlight goes back to the face after this
constexpr uint32_t SERIAL_HOLD_MS = 3000;              // after serial data, no light sleep for this long
constexpr int SERIAL_BYTES_PER_POLL = 256;             // bounds the time one poll spends on serial input
constexpr int SWIPE_PX = 40;
constexpr int W = 240, H = 240;

enum class Mode : uint8_t { Boot, Face, Panel, Details, Setup, SetTime, TouchCheck, PhoneSync, Drawer, Flashlight };

// A face's entrance (faces.h): played once when a slide lands on a face other than the one last
// shown settled, never on wake. A touch, BOOT, settings or sleep ends it at once, and the face is
// drawn in its final state. Nothing queues: a new choice starts a new entrance.
struct EntranceState {
  bool playing = false;
  Face face = FACE_DIGITAL;
  uint32_t start = 0, length = 0;  // length in real time, after the Expressive stretch
  uint32_t started = 0, finished = 0;
};

// Orbit's minute marker moving on to a new minute.
struct MarkerState {
  bool moving = false;
  uint32_t start = 0;
  int minute = -1;  // minute of the day it was last drawn at, -1 = draw the next one without moving
};

// The low battery notice: waiting for the face to be on screen, then shown until `until`.
struct LowNotice {
  bool pending = false, urgent = false;
  uint32_t until = 0;  // 0 = not showing
};

// Touch check: two raw taps, then the result for a moment. `back` is where it goes afterwards:
// the settings page it was opened from, or the face.
struct CheckState {
  int step = 0;
  screens::CheckResult result = screens::CheckResult::Tapping;
  int rawX[2] = {}, rawY[2] = {};
  uint32_t resultAt = 0;
  Mode back = Mode::Face;
  screens::Button backTo = screens::Button::None;
};

// BOOT button: on a face a short press is next face and a long press opens settings,
// so a face acts on release (or at the hold time). Everywhere else it acts on press.
struct BootButton {
  bool down = false;
  bool used = false;         // this press has already done its one thing
  bool checkOpened = false;  // held long enough to open the touch check
  uint32_t downAt = 0;
};

// One finger's progress from touch-down to release.
enum class Axis : uint8_t { None, Horizontal, Vertical };
struct Finger {
  bool down = false;
  bool ignore = false;    // started by waking the screen, or already used up
  Axis axis = Axis::None;
  bool longFired = false;
  uint32_t repeatAt = 0;  // next repeat of a held - or +; 0 until repeating starts
  int repeats = 0;
  int16_t x0 = 0, y0 = 0, x = 0, y = 0;
  uint32_t t0 = 0, lastMoveAt = 0, lastSampleAt = 0;
  float vx = 0, vy = 0;   // smoothed velocity, px per ms
};

// The one transition that can be running. A new touch takes over a face
// slide where it is, so swipes never queue up.
enum class Slide : uint8_t { None, Face, PanelOpen, PanelClose, Page, DrawerOpen, DrawerClose };
struct Transition {
  Slide kind = Slide::None;
  bool dragging = false, animating = false;
  float pos = 0, from = 0, to = 0, grabPos = 0;
  int shownPos = -9999;    // last offset pushed to the panel
  uint32_t start = 0, duration = 0;
  int neighbour = -1;      // face drawn in `nb`
  int toPage = 0;          // page drawn in `nb` during a page slide
};

struct TimerState {
  bool active = false;
  bool alarming = false;
  uint32_t targetMs = 0;
  uint32_t totalMs = 0;
  uint32_t alarmStartMs = 0;
  int32_t alarmShown = -1;  // the flash step last drawn; the alarm draws on its own clock, not the face's
};

#if DRIFT_DIAG
struct DiagState {
  uint32_t frames = 0, renderUs = 0, pushUs = 0, worstUs = 0, lastLog = 0;
  // Entrance and minute-marker frames, drawing and pushing together.
  uint32_t motionFrames = 0, motionUs = 0, motionWorstUs = 0;
  char line[48] = "";
};
extern DiagState diag;
#endif

// ---- State (defined in ui.cpp) ------------------------------------------------------------
extern TFT_eSPI tft;
extern TFT_eSprite spriteA, spriteB, cache;
extern TFT_eSprite *cur, *nb;
extern bool haveSpare, haveCache, haveFrame;

extern Mode mode;
extern bool awake;
extern uint32_t bootStart, lastActivity, lastPresent, settingsChangedAt;
extern bool settingsDirty;

extern bool fading, irisWaking, sleeping;
extern uint32_t fadeStart, irisStart, sleepStart;

extern uint32_t cacheKey;
extern bool cacheValid, dirty;
extern int shownSecond, lastMinute;
extern bool faceMoving;
extern uint32_t sweepStart;

extern EntranceState entrance;
extern Face settledFace;
extern MarkerState marker;
extern uint32_t releaseUntil;

extern int batteryPct, batteryMv;
extern battery::Estimate batteryLife;
extern battery::LowWarning lowWarning;
extern LowNotice lowNotice;
extern uint32_t lastBatteryRead, nextSyncAt;
extern uint16_t seenAttempts;
extern uint8_t failsInARow;
extern net::Sync seenSync;
extern net::Portal seenPortal;
extern uint32_t uiKey;
extern bool uiAnimated;
extern screens::Button pressed;
extern int settingsPage, detailsPage, setupPage, setTimePage, phonePage, checkPage, drawerPage;
extern tm editTime;
extern TimerState timer;
extern uint32_t flashlightStart;
extern uint32_t serialHoldUntil;

extern RaiseDetector wristRaise;
extern TwistDetector wristTwist;
extern DropDetector wristDrop;
extern uint32_t lastImuRead;
extern bool wokeByRaise;
extern power::Controller standby;
extern bool touchResting, wakeGuard;

extern CheckState check;
extern int rawX, rawY;
extern screens::SetupPhase seenPhase;
extern BootButton boot;
extern Finger finger;
extern bool touchFailing;
extern Transition tr;

// ---- Helpers & orchestration (ui.cpp) -----------------------------------------------------
bool motionOn();
bool entrancesOn();
uint32_t since(uint32_t t);
Face currentFace();
Face nextActiveFace(int cur, int dir);
Face faceAt(int step);
void markSettingsChanged();
void readBattery();
void showLowNotice();
bool lowPower();
bool gesturesOn();
bool syncBusy();
void stepNetwork();
bool setupHoldsScreen();
uint32_t screenTimeout();

// ---- Rendering & frame composition (ui_render.cpp) ----------------------------------------
void present(TFT_eSprite &s);
void presentHorizontal(TFT_eSprite &a, TFT_eSprite &b, int offset);
void presentVertical(TFT_eSprite &top, TFT_eSprite &bottom, int offset);
FaceContext faceContext(bool withSweep);
uint32_t entranceTimeline();
bool entersOnLanding(Face f);
void renderFace(TFT_eSprite &dst, Face f, bool withSweep, bool entranceStart = false);
void lastSyncText(char *buf, size_t n);
int &pageFor(Mode m);
void syncAgoText(char *buf, size_t n);
screens::View view(Mode m, int page);
screens::View view();
bool pagedMode();
int pageCount();
uint32_t screenKey();
void drawScreen(TFT_eSprite &s, Mode m, int page);
void refreshScreen(bool force);
int firstSettingsPage();
void stepBoot();
void stepFace();
#if DRIFT_DIAG
void stepDiag();
#endif
void showNoMemory();

// ---- Entrances, slides, sleep & wake (ui_motion.cpp) --------------------------------------
void startEntrance(Face f);
void cancelEntrance(bool redraw);
void landed();
void animateTo(float target, uint32_t fullDuration, float fullDistance);
void settleTo(float target, float v);
void prepareNeighbour(float pos);
void changePage(int dir, float v = 0);
void showFaceSlide();
void commitFace(int step);
void landOn(int step);
void cutToFace(int step);
void openPanel(float v = 0);
void startPanelDrag();
void closeToFace(float v = 0);
void openDrawer(float v = 0);
void closeDrawer(float v = 0);
void openFlashlight();
void dismissAlarm();
void closeFlashlight();
void startTimer(uint32_t ms);
void resetTimer();
void stepTimer();
void stepTransition();
void touchRest();
void touchResume();
void finishIris();
void sleepScreen();
void startSleep();
void wakeScreen();
void stepFade();
void stepIris();
void stepSleep();
power::Inputs standbyInputs();
uint32_t nextDeadline();
void standbyStep();

// ---- Actions, touch, buttons, wrist motion & serial (ui_input.cpp) ------------------------
screens::SetupPhase setupPhase();
void showMode(Mode m);
void showSettingsAtWifi();
tm buildDate();
void openSetTime();
void saveTime();
void openSetup();
void showSettingsAt(screens::Button b);
void applyTurn();
void openTouchCheck(Mode back, screens::Button backTo);
void leaveTouchCheck();
void touchCheckTap(int x, int y);
void stepTouchCheck();
void openPhoneSync();
void closePhoneSync();
void stepPhone();
void goBack(float v = 0);
void activate(screens::Button b);
screens::Button hitTest(int x, int y);
void noteLooking();
bool feedGesture(uint32_t ms, const Accel &a);
void stepDrop();
void onFingerDown(const TouchSample &s);
void onFingerMove(const TouchSample &s);
void onFingerUp();
void cancelFinger();
void stepRepeat();
bool stepHold();
void pollBoot();
void stepBootCheck();
void runSerialCommand(const char *line);
void pollSerial();
void holdForSerial();
void pollInput();

}  // namespace uiparts
