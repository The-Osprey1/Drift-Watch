#include "ui.h"
#include <utility>
#include "battery.h"
#include "ble.h"
#include "board.h"
#include "faces.h"
#include "gfx.h"
#include "hal.h"
#include "imu.h"
#include "net.h"
#include "orient.h"
#include "phone.h"
#include "power.h"
#include "raise.h"
#include "screens.h"
#include "serial_cmd.h"
#include "settings.h"
#include "theme.h"
#include "touch.h"

namespace {
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
constexpr uint32_t BOOT_HOLD_MS = 250;                 // name stays up briefly after the sequence
constexpr uint32_t HOLD_HINT_MS = 200;                 // a still press starts showing the hold ring
constexpr uint32_t FLICK_WINDOW_MS = 80;               // finger must still be moving this recently to flick
constexpr uint32_t REPEAT_DELAY_MS = 450;               // holding - or + starts repeating after this
constexpr uint32_t REPEAT_MS = 120, REPEAT_FAST_MS = 50;  // then this often, faster after 8 steps
constexpr uint32_t IMU_AWAKE_MS = 80;                  // motion samples while the screen is on, for wrist-down sleep
constexpr uint32_t CHECK_RESULT_MS = 1400;             // touch check result stays up this long
constexpr uint32_t BOOT_CHECK_MS = 4000;               // holding BOOT this long opens the touch check anywhere
constexpr uint32_t KNOWN_TIME_SAVE_S = 3600;           // remember the clock in flash at most hourly
constexpr uint32_t TOUCH_DROPOUT_MS = 50;              // failed touch reads this long end a touch, without acting
constexpr int SERIAL_BYTES_PER_POLL = 256;             // bounds the time one poll spends on serial input
constexpr int SWIPE_PX = 40;
constexpr int W = 240, H = 240;

enum class Mode : uint8_t { Boot, Face, Panel, Details, Setup, SetTime, TouchCheck, PhoneSync };

TFT_eSPI tft;
TFT_eSprite spriteA(&tft), spriteB(&tft), cache(&tft);
// `cur` is what is on screen; `nb` is the incoming screen during a transition.
// They swap when a transition finishes, so nothing is redrawn twice.
TFT_eSprite *cur = &spriteA, *nb = &spriteB;
bool haveSpare = false, haveCache = false;
bool haveFrame = true;  // false if not even an 8-bit frame fitted: only the memory notice shows

Mode mode = Mode::Boot;
bool awake = true;
uint32_t bootStart = 0, lastActivity = 0, lastPresent = 0, settingsChangedAt = 0;
bool settingsDirty = false;

// Backlight fade-in on wake.
bool fading = false;
uint32_t fadeStart = 0;

// Face drawing state.
uint32_t cacheKey = 0;
bool cacheValid = false, dirty = true;
int shownSecond = -1, lastMinute = -1;
bool faceMoving = false;    // the face animates between seconds: the radar beam, Phantom's movement
uint32_t sweepStart = 0;

// A face's entrance (faces.h): played once when a slide lands on a face other than the one last
// shown settled, never on wake. A touch, BOOT, settings or sleep ends it at once, and the face is
// drawn in its final state. Nothing queues: a new choice starts a new entrance.
struct {
  bool playing = false;
  Face face = FACE_DIGITAL;
  uint32_t start = 0, length = 0;  // length in real time, after the Expressive stretch
  uint32_t started = 0, finished = 0;
} entrance;
Face settledFace = FACE_DIGITAL;  // the face last shown in its final state, or entering
// Orbit's minute marker moving on to a new minute.
struct {
  bool moving = false;
  uint32_t start = 0;
  int minute = -1;  // minute of the day it was last drawn at, -1 = draw the next one without moving
} marker;
uint32_t releaseUntil = 0;  // a tapped control keeps its pressed look until then

int batteryPct = -1, batteryMv = -1;
battery::Estimate batteryLife;
battery::LowWarning lowWarning;
// The low battery notice: waiting for the face to be on screen, then shown until `until`.
struct LowNotice {
  bool pending = false, urgent = false;
  uint32_t until = 0;  // 0 = not showing
} lowNotice;
uint32_t lastBatteryRead = 0;
uint32_t nextSyncAt = 0;
uint16_t seenAttempts = 0;
uint8_t failsInARow = 0;  // syncs that failed since the last one that worked
net::Sync seenSync = net::Sync::Idle;
net::Portal seenPortal = net::Portal::Off;
uint32_t uiKey = 0;
bool uiAnimated = false;   // the last drawn screen has a spinner that needs frames
screens::Button pressed = screens::Button::None;
// Page shown on each paged screen. The settings page is kept while details or setup are open.
int settingsPage = 0, detailsPage = 0, setupPage = 0, setTimePage = 0;
tm editTime{};  // what the set time screen shows; applied only by Save

RaiseDetector wristRaise;
TwistDetector wristTwist;
DropDetector wristDrop;
uint32_t lastImuRead = 0;
bool wokeByRaise = false;
// Screen-off waits (power.h). `touchResting` is true while the touch chip is allowed its
// automatic standby; it is not read then, only its interrupt line can wake the watch.
power::Controller standby;
bool touchResting = false;
// Set by a touch or BOOT wake: a finger found down afterwards is the one that woke the watch,
// even if the chip only answers again after the screen is already on, so it never acts.
// Cleared by the first answer that shows no finger.
bool wakeGuard = false;

// Touch check: two raw taps, then the result for a moment. `checkReturn` is where it goes
// afterwards: the settings page it was opened from, or the face.
struct {
  int step = 0;
  screens::CheckResult result = screens::CheckResult::Tapping;
  int rawX[2] = {}, rawY[2] = {};
  uint32_t resultAt = 0;
  Mode back = Mode::Face;
  screens::Button backTo = screens::Button::None;
} check;
int rawX = 0, rawY = 0;  // latest touch position as the chip reported it
screens::SetupPhase seenPhase = screens::SetupPhase::Hotspot;

// BOOT button: on a face a short press is next face and a long press opens settings,
// so a face acts on release (or at the hold time). Everywhere else it acts on press.
struct {
  bool down = false;
  bool used = false;      // this press has already done its one thing
  bool checkOpened = false;  // held long enough to open the touch check
  uint32_t downAt = 0;
} boot;

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
} finger;
bool touchFailing = false;  // the last touch read got no answer from the chip

// The one transition that can be running. A new touch takes over a face
// slide where it is, so swipes never queue up.
enum class Slide : uint8_t { None, Face, PanelOpen, PanelClose, Page };
struct Transition {
  Slide kind = Slide::None;
  bool dragging = false, animating = false;
  float pos = 0, from = 0, to = 0, grabPos = 0;
  int shownPos = -9999;    // last offset pushed to the panel
  uint32_t start = 0, duration = 0;
  int neighbour = -1;      // face drawn in `nb`
  int toPage = 0;          // page drawn in `nb` during a page slide
} tr;

#if DRIFT_DIAG
struct {
  uint32_t frames = 0, renderUs = 0, pushUs = 0, worstUs = 0, lastLog = 0;
  // Entrance and minute-marker frames, drawing and pushing together.
  uint32_t motionFrames = 0, motionUs = 0, motionWorstUs = 0;
  char line[48] = "";
} diag;
#endif

// ---- Small helpers ---------------------------------------------------------------
bool motionOn() { return settings.motion != MOTION_OFF; }
// Entrances need the slide buffers and the cached dial; the low-memory fallback changes instantly.
bool entrancesOn() { return motionOn() && haveSpare && haveCache; }
uint32_t since(uint32_t t) { return hal::ms() - t; }
Face currentFace() { return (Face)(settings.face % FACE_COUNT); }
Face faceAt(int step) { return (Face)((settings.face + FACE_COUNT + step) % FACE_COUNT); }

void markSettingsChanged() {
  settingsDirty = true;
  settingsChangedAt = hal::ms();
}

void present(TFT_eSprite &s) {
#if DRIFT_DIAG
  uint32_t t = micros();
#endif
  s.pushSprite(0, 0);
  lastPresent = hal::ms();
#if DRIFT_DIAG
  diag.pushUs += micros() - t;
#endif
}

// Shows `a` and `b` side by side, shifted so `offset` columns of `a` are off the left edge.
void presentHorizontal(TFT_eSprite &a, TFT_eSprite &b, int offset) {
#if DRIFT_DIAG
  uint32_t t = micros();
#endif
  offset = constrain(offset, 0, W);
  if (offset < W) a.pushSprite(0, 0, offset, 0, W - offset, H);
  if (offset > 0) b.pushSprite(W - offset, 0, 0, 0, offset, H);
  lastPresent = hal::ms();
#if DRIFT_DIAG
  diag.pushUs += micros() - t;
#endif
}

// Shows `top` above `bottom`, shifted so `offset` rows of `top` are off the top edge.
void presentVertical(TFT_eSprite &top, TFT_eSprite &bottom, int offset) {
  offset = constrain(offset, 0, H);
  if (offset < H) top.pushSprite(0, 0, 0, offset, W, H - offset);
  if (offset > 0) bottom.pushSprite(0, H - offset, 0, 0, W, offset);
  lastPresent = hal::ms();
}

void readBattery() {
  batteryMv = hal::batteryMilliVolts();
  batteryPct = battery::percentFromMilliVolts(batteryMv);
  batteryLife.add(hal::ms(), batteryPct);
  lastBatteryRead = hal::ms();
  if (int level = lowWarning.update(batteryPct < 0 ? -1 : batteryLife.smoothed(), batteryLife.charging())) {
    lowNotice.pending = true;
    lowNotice.urgent = level == battery::LowWarning::NOW;
    hal::log("battery: below %d%%", level);
  }
}

// Shows a waiting low battery notice once the face is on screen; called before the face is drawn.
void showLowNotice() {
  if (!lowNotice.pending || !awake || mode != Mode::Face) return;
  lowNotice.pending = false;
  lowNotice.until = hal::ms() + LOW_NOTICE_MS;
  if (!lowNotice.until) lowNotice.until = 1;  // 0 means none
  dirty = true;
}

// Below 20% and not charging, the radar beam only moves with the seconds.
bool lowPower() { return batteryPct >= 0 && batteryPct < motion::LOW_BATTERY_PCT && !batteryLife.charging(); }

// Night mode turns the wrist gestures off inside its window (touch and BOOT still wake the screen).
bool gesturesOn() {
  if (!(settings.raiseToWake || settings.twistToWake) || !imu::present()) return false;
  if (!settings.night || !net::timeIsValid()) return true;
  time_t t = hal::now();
  tm n;
  localtime_r(&t, &n);
  return !nightNow(n.tm_hour);
}

bool syncBusy() { return net::syncRunning(); }

FaceContext faceContext(bool withSweep) {
  FaceContext c{};
  // One reading of the wall clock for the whole frame: the time shown and the fraction of a
  // second the smooth hands use can never disagree.
  int64_t wall = hal::nowMs();
  time_t t = (time_t)(wall / 1000);
  localtime_r(&t, &c.now);
  c.ms = (uint16_t)(wall % 1000);
  c.animMs = hal::ms();
  c.animate = withSweep && faceMoving;
  c.timeValid = net::timeIsValid();
  c.use12h = settings.use12h;
  c.batteryPct = batteryPct;
  if (syncBusy()) c.sync = SyncView::Syncing;
  else if (net::lastResult() == net::Result::Failed) c.sync = SyncView::Failed;
  else c.sync = net::lastSync() ? SyncView::Synced : SyncView::Never;
  c.syncStage = net::syncStep();
  c.noWifi = !net::hasSavedWifi();
  c.beamDeg = -1;
  if (withSweep && currentFace() == FACE_RADAR && motionOn()) {
    if (faceMoving) c.beamDeg = (since(sweepStart) % motion::RADAR_TURN_MS) * 360.0f / motion::RADAR_TURN_MS;
    else if ((lowPower() || !haveCache) && c.timeValid) c.beamDeg = c.now.tm_sec * 6;  // one step a second, with the seconds dot
  }
  return c;
}

// Where the entrance is on the face's own timeline. Expressive plays it slower.
uint32_t entranceTimeline() {
  uint32_t t = since(entrance.start);
  if (settings.motion == MOTION_EXPRESSIVE) t = t * motion::EXPRESSIVE_DEN / motion::EXPRESSIVE_NUM;
  return min(t, faceEntranceMs(entrance.face));
}

// A face that will play its entrance when a slide lands on it is drawn as the entrance starts,
// so its decoration never shows first and then disappears.
bool entersOnLanding(Face f) { return entrancesOn() && faceEntranceMs(f) && f != settledFace; }

// Draws a face into `dst`. The current face reuses its cached static layer. `entranceStart`
// draws it as its entrance's first frame (a face sliding in).
void renderFace(TFT_eSprite &dst, Face f, bool withSweep, bool entranceStart = false) {
#if DRIFT_DIAG
  uint32_t t = micros();
#endif
  FaceContext c = faceContext(withSweep);
  if (entranceStart) {
    c.entering = true;
    c.entranceMs = 0;
  } else if (entrance.playing && f == entrance.face && f == currentFace()) {
    c.entering = true;
    c.entranceMs = entranceTimeline();
  }
  if (marker.moving && f == currentFace()) {
    c.markerMoving = true;
    c.markerMs = min(since(marker.start), motion::MARKER_MS);
  }
  if (haveCache && f == currentFace()) {
    uint32_t key = faceStaticKey(f, c);
    if (!cacheValid || key != cacheKey) {
      drawFaceStatic(cache, f, c);
      cacheKey = key;
      cacheValid = true;
    }
    memcpy(dst.getPointer(), cache.getPointer(), W * H * 2);
  } else {
    drawFaceStatic(dst, f, c);
  }
  drawFaceLive(dst, f, c);
  if (lowNotice.until && f == currentFace() && mode == Mode::Face) screens::drawLowBattery(dst, batteryPct, lowNotice.urgent);
#if DRIFT_DIAG
  uint32_t used = micros() - t;
  diag.renderUs += used;
  diag.worstUs = max(diag.worstUs, used);
  diag.frames++;
#endif
}

void lastSyncText(char *buf, size_t n) {
  time_t t = net::lastSync();
  if (!t) {
    buf[0] = 0;
    return;
  }
  tm s;
  localtime_r(&t, &s);
  int h = settings.use12h ? (s.tm_hour % 12 ? s.tm_hour % 12 : 12) : s.tm_hour;
  snprintf(buf, n, settings.use12h ? "%d:%02d" : "%02d:%02d", h, s.tm_min);
}

int phonePage = 0, checkPage = 0;
int &pageFor(Mode m) {
  return m == Mode::Details     ? detailsPage
         : m == Mode::Setup     ? setupPage
         : m == Mode::SetTime   ? setTimePage
         : m == Mode::PhoneSync ? phonePage
         : m == Mode::TouchCheck ? checkPage
                                 : settingsPage;
}

// "3 h ago" style age of the last sync.
void syncAgoText(char *buf, size_t n) {
  time_t t = net::lastSync();
  if (!t) {
    buf[0] = 0;
    return;
  }
  long ago = (long)(hal::now() - t);
  if (ago < 120) snprintf(buf, n, "just now");
  else if (ago < 7200) snprintf(buf, n, "%ld min ago", ago / 60);
  else if (ago < 172800) snprintf(buf, n, "%ld h ago", ago / 3600);
  else snprintf(buf, n, "%ld days ago", ago / 86400);
}

screens::View view(Mode m, int page) {
  static char synced[8];
  lastSyncText(synced, sizeof synced);
  screens::View v{};
  v.screen = m == Mode::Details ? screens::Screen::Details
             : m == Mode::Setup ? screens::Screen::Setup
             : m == Mode::SetTime ? screens::Screen::SetTime
             : m == Mode::TouchCheck ? screens::Screen::TouchCheck
             : m == Mode::PhoneSync ? screens::Screen::PhoneSync
                                  : screens::Screen::Settings;
  v.page = page;
  v.large = settings.largeText;
  v.reducedMotion = !motionOn();
  v.motion = settings.motion;
  v.batteryPct = batteryPct;
  v.batteryMv = batteryMv;
  v.brightnessLevel = settings.brightnessLevel % BRIGHTNESS_LEVEL_COUNT;
  v.use12h = settings.use12h;
  v.timeValid = net::timeIsValid();
  v.stage = net::syncState();
  v.result = net::lastResult();
  v.fail = net::failReason();
  v.syncStep = net::syncStep();
  v.joinTry = net::joinTry();
  v.portal = net::portalState();
  v.secondsLeft = net::portalSecondsLeft();
  v.lastSyncText = synced;
  v.zone = net::zoneName();
  v.ssid = net::ssid();
  v.zoneManual = !settings.tz.isEmpty();
  v.joinWhy = net::joinWhy();
  v.joinCode = net::joinCode();
  v.clockByHand = net::clockSetByHand();
  v.raiseToWake = settings.raiseToWake;
  v.twistToWake = settings.twistToWake;
  v.imuPresent = imu::present();
  v.edit = editTime;
  v.screenTurn = settings.screenTurn;
  v.touchChecked = touchChecked();
  v.night = settings.night;
  v.checkStep = check.step;
  v.checkResult = check.result;
  v.ble = ble::state();
  v.bleName = ble::name();
  v.bleSecondsLeft = ble::secondsLeft();
  const phone::Session &ps = phone::session();
  v.phoneGotTime = ps.gotTime;
  v.phoneWifiSaved = ps.wifiSaved;
  v.phoneSsid = ps.ssid;
  v.phoneError = ps.error;
  v.batteryHours = batteryLife.hoursLeft();
  v.charging = batteryLife.charging();
  v.driftPpb = net::driftPpb();
  v.syncSource = net::lastSource();
  static char ago[24];
  syncAgoText(ago, sizeof ago);
  v.syncAgo = ago;
  v.imuLine1 = imu::lineEdges(0);
  v.imuLine2 = imu::lineEdges(1);
  static char nowBuf[8];
  {
    time_t t = hal::now();
    tm n;
    localtime_r(&t, &n);
    int h = settings.use12h ? (n.tm_hour % 12 ? n.tm_hour % 12 : 12) : n.tm_hour;
    snprintf(nowBuf, sizeof nowBuf, settings.use12h ? "%d:%02d" : "%02d:%02d", h, n.tm_min);
  }
  v.nowText = nowBuf;
  v.version = "v" FW_VERSION;
  v.startReason = hal::startReason();
  v.restarts = hal::unexpectedRestarts();
#if DRIFT_DIAG
  v.diag = diag.line;
#else
  v.diag = nullptr;
#endif
  v.pressed = pressed;
  v.ms = hal::ms();
  return v;
}
screens::View view() { return view(mode, pageFor(mode)); }
bool pagedMode() {
  return mode == Mode::Panel || mode == Mode::Details || mode == Mode::Setup || mode == Mode::SetTime || mode == Mode::PhoneSync;
}
int pageCount() { return screens::pageCount(*cur, view()); }

// Everything on a settings, details or setup screen except the spinner, so a
// redraw is only pushed when something the user can read has changed.
uint32_t screenKey() {
  uint32_t k = (uint32_t)mode;
  k = k * 31 + (uint32_t)pageFor(mode);
  k = k * 31 + (uint32_t)net::syncState();
  k = k * 31 + (uint32_t)net::lastResult();
  k = k * 31 + (uint32_t)net::attempts();
  k = k * 31 + (uint32_t)net::portalState();
  k = k * 31 + (uint32_t)net::failReason();
  k = k * 31 + (uint32_t)pressed;
  k = k * 31 + settings.brightnessLevel * 16 + settings.use12h * 8 + settings.largeText * 2 + net::timeIsValid();
  k = k * 7 + settings.motion;
  k = k * 131 + (uint32_t)(batteryPct + 1);
  k = k * 1031 + net::portalSecondsLeft();
  k = k * 31 + (uint32_t)net::lastSync();
  k = k * 31 + (uint32_t)net::joinWhy() * 2 + net::clockSetByHand();
  k = k * 31 + settings.raiseToWake * 2 + settings.twistToWake;
  // The clock shown on the Set time control: only its page is redrawn when the minute turns.
  // Set time is the third item, with two items a page (one with large text).
  bool clockShown = mode != Mode::Panel || settingsPage == (settings.largeText ? 2 : 1);
  if (clockShown) k = k * 61 + (uint32_t)(hal::now() / 60);
  k = k * 31 + (uint32_t)(editTime.tm_year * 400 + editTime.tm_mon * 32 + editTime.tm_mday);
  k = k * 31 + (uint32_t)(editTime.tm_hour * 60 + editTime.tm_min);
  k = k * 31 + settings.screenTurn * 8 + settings.night * 2 + touchChecked();
  k = k * 31 + (uint32_t)check.step * 4 + (uint32_t)check.result;
  k = k * 31 + (uint32_t)ble::state();
  k = k * 1031 + ble::secondsLeft();
  k = k * 31 + phone::session().lines;
  if (mode == Mode::Details) {
    // Time left and the age of the last sync, as worded on the screen.
    float h = batteryLife.hoursLeft();
    k = k * 1031 + (uint32_t)(h < 0 ? 0 : (int)(h + 0.5f) + 1);
    char ago[24];
    syncAgoText(ago, sizeof ago);
    for (const char *p = ago; *p; p++) k = k * 31 + (uint8_t)*p;
  }
  return k;
}

void drawScreen(TFT_eSprite &s, Mode m, int page) { uiAnimated = screens::draw(s, view(m, page)); }

void refreshScreen(bool force) {
  uint32_t k = screenKey();
  bool spin = uiAnimated && motionOn() && since(lastPresent) >= SPINNER_FRAME_MS;
  if (!force && k == uiKey && !spin) return;
  uiKey = k;
  drawScreen(*cur, mode, pageFor(mode));
  present(*cur);
}

// Settings open on the first page, except when the clock is not set: then they open on
// the page with the fixes, Set time and Sync now (one or the other with large text).
int firstSettingsPage() {
  if (net::timeIsValid()) return 0;
  screens::Button fix = net::hasSavedWifi() ? screens::Button::SyncNow : screens::Button::SetTime;
  return screens::pageOf(*cur, view(Mode::Panel, 0), fix);
}

// ---- Entrances ----------------------------------------------------------------------
void startEntrance(Face f) {
  if (!entrancesOn() || !faceEntranceMs(f)) return;
  entrance.playing = true;
  entrance.face = f;
  entrance.start = hal::ms();
  entrance.length = faceEntranceMs(f);
  if (settings.motion == MOTION_EXPRESSIVE) entrance.length = entrance.length * motion::EXPRESSIVE_NUM / motion::EXPRESSIVE_DEN;
  entrance.started++;
  marker.moving = false;  // the entrance brings the marker in itself
  dirty = true;
}

// Ends a running entrance. With `redraw` the face is drawn in its final state straight away, on
// the same poll as the touch or press that ended it.
void cancelEntrance(bool redraw) {
  if (!entrance.playing) return;
  entrance.playing = false;
  dirty = true;
  if (redraw && awake && mode == Mode::Face && tr.kind == Slide::None) {
    renderFace(*cur, currentFace(), true);
    present(*cur);
    dirty = false;
  }
}

// A face slide has come to rest. If it rests on a face other than the one shown before, that
// face plays its entrance once.
void landed() {
  if (currentFace() == settledFace) return;
  settledFace = currentFace();
  startEntrance(settledFace);
}

// ---- Transitions ------------------------------------------------------------------
void animateTo(float target, uint32_t fullDuration, float fullDistance) {
  tr.dragging = false;
  tr.animating = true;
  tr.from = tr.pos;
  tr.to = target;
  tr.start = hal::ms();
  // Shorter distances take proportionally less time, with a floor so a small
  // snap-back still reads as motion rather than a jump.
  float part = fabsf(target - tr.pos) / fullDistance;
  tr.duration = motionOn() ? (uint32_t)(fullDuration * max(0.35f, min(1.0f, part))) : 0;
}

// After a release the slide settles with ease-out and no bounce, in a time set by the distance
// left and the release speed `v` (px per ms): see motion::settleMs. A face dragged back where
// it started returns in motion::CANCEL_MS; the panel uses its own, slightly longer range.
void settleTo(float target, float v) {
  float remaining = fabsf(target - tr.pos);
  bool flick = fabsf(v) > motion::FLICK_PX_PER_MS;
  uint32_t ms = tr.kind == Slide::PanelOpen || tr.kind == Slide::PanelClose ? motion::panelSettleMs(remaining)
                : tr.kind == Slide::Face && target == 0                    ? motion::CANCEL_MS
                                                                           : motion::settleMs(remaining, flick);
  tr.dragging = false;
  tr.animating = true;
  tr.from = tr.pos;
  tr.to = target;
  tr.start = hal::ms();
  tr.duration = motionOn() ? ms : 0;
}

// Makes sure `nb` holds the face on the side the current one is moving away from.
void prepareNeighbour(float pos) {
  int want = pos < 0 ? faceAt(+1) : faceAt(-1);
  if (tr.neighbour == want) return;
  renderFace(*nb, (Face)want, false, entersOnLanding((Face)want));
  tr.neighbour = want;
}

// Settings, details and setup pages slide sideways like faces, but only on release.
void changePage(int dir, float v = 0) {
  int &page = pageFor(mode);
  int to = page + dir;
  if (to < 0 || to >= pageCount()) return;  // no wrap-around, and never a face change
  pressed = screens::Button::None;
  if (!haveSpare || !motionOn()) {
    page = to;
    refreshScreen(true);
    return;
  }
  drawScreen(*nb, mode, to);
  tr = Transition{};
  tr.kind = Slide::Page;
  tr.toPage = to;
  if (v) settleTo(dir > 0 ? -W : W, v);
  else animateTo(dir > 0 ? -W : W, motion::FACE_MS, W);
}

void showFaceSlide() {
  int p = (int)lroundf(tr.pos);
  if (p <= 0) presentHorizontal(*cur, *nb, -p);  // next face comes in from the right
  else presentHorizontal(*nb, *cur, W - p);       // previous face comes in from the left
}

void commitFace(int step) {
  settings.face = faceAt(step);
  markSettingsChanged();
  std::swap(cur, nb);  // the incoming face is already drawn
  cacheValid = false;
  dirty = true;
  shownSecond = -1;
  lastMinute = -1;
  tr = Transition{};
  faceMoving = false;  // stepFace starts the beam on the radar face
  marker.minute = -1;
}

// Mid-drag arrival on the neighbour: it becomes current and the drag continues.
void landOn(int step) {
  if (tr.neighbour != faceAt(step)) renderFace(*nb, faceAt(step), false, entersOnLanding(faceAt(step)));
  settings.face = faceAt(step);
  markSettingsChanged();
  std::swap(cur, nb);
  tr.neighbour = faceAt(-step);  // the face just left is now the neighbour
  cacheValid = false;
  faceMoving = false;
  marker.minute = -1;
}

// Instant face change, used with Motion Off or without the spare buffer: no entrance either.
void cutToFace(int step) {
  settings.face = faceAt(step);
  settledFace = currentFace();
  markSettingsChanged();
  cacheValid = false;
  dirty = true;
  faceMoving = false;
  marker.minute = -1;
  tr = Transition{};
}

void openPanel(float v = 0) {
  cancelEntrance(false);
  pressed = screens::Button::None;
  settingsPage = firstSettingsPage();
  if (!haveSpare || !motionOn()) {
    mode = Mode::Panel;
    refreshScreen(true);
    return;
  }
  if (mode == Mode::Face) renderFace(*cur, currentFace(), false);  // without the hold ring
  drawScreen(*nb, Mode::Panel, settingsPage);
  tr = Transition{};
  tr.kind = Slide::PanelOpen;
  tr.pos = 0;
  if (v) settleTo(H, v);
  else animateTo(H, motion::PANEL_MS, H);
}

// An upward drag on a face pulls the settings panel up under the finger.
void startPanelDrag() {
  cancelEntrance(false);
  pressed = screens::Button::None;
  settingsPage = firstSettingsPage();
  renderFace(*cur, currentFace(), false);  // without the hold ring
  drawScreen(*nb, Mode::Panel, settingsPage);
  tr = Transition{};
  tr.kind = Slide::PanelOpen;
  tr.dragging = true;
  tr.pos = 0;
}

void closeToFace(float v = 0) {
  pressed = screens::Button::None;
  if (!haveSpare || !motionOn()) {
    mode = Mode::Face;
    dirty = true;
    return;
  }
  renderFace(*nb, currentFace(), false);
  tr = Transition{};
  tr.kind = Slide::PanelClose;
  tr.pos = H;
  if (v) settleTo(0, v);
  else animateTo(0, motion::PANEL_MS, H);
}

void stepTransition() {
  if (tr.kind == Slide::None) return;
  if (tr.animating) {
    float t = tr.duration ? since(tr.start) / (float)tr.duration : 1;
    tr.pos = tr.from + (tr.to - tr.from) * motion::easeOut(t);
    if (t >= 1) {
      tr.pos = tr.to;
      tr.animating = false;
    }
    // A flick can carry a face past centre to the other side.
    if (tr.kind == Slide::Face && tr.pos != 0) prepareNeighbour(tr.pos);
  }
  int p = (int)lroundf(tr.pos);
  bool finished = !tr.animating && !tr.dragging;
  if (p != tr.shownPos && (finished || since(lastPresent) >= ANIM_FRAME_MS)) {
    tr.shownPos = p;
    switch (tr.kind) {
      case Slide::Face:
      case Slide::Page: showFaceSlide(); break;
      case Slide::PanelOpen: presentVertical(*cur, *nb, (int)tr.pos); break;
      case Slide::PanelClose: presentVertical(*nb, *cur, (int)tr.pos); break;
      default: break;
    }
  }
  if (tr.animating || tr.dragging) return;

  // Finished.
  switch (tr.kind) {
    case Slide::Face:
      if (tr.pos <= -W) commitFace(+1);
      else if (tr.pos >= W) commitFace(-1);
      else {
        tr = Transition{};
        dirty = true;
      }
      landed();
      break;
    case Slide::PanelOpen:
      if (tr.pos <= 0) {  // dragged up a little and let go: back to the face
        tr = Transition{};
        dirty = true;
        break;
      }
      std::swap(cur, nb);
      tr = Transition{};
      mode = Mode::Panel;
      uiKey = screenKey();
      break;
    case Slide::PanelClose:
      std::swap(cur, nb);
      tr = Transition{};
      mode = Mode::Face;
      dirty = true;
      break;
    case Slide::Page:
      std::swap(cur, nb);
      pageFor(mode) = tr.toPage;
      tr = Transition{};
      uiKey = screenKey();
      break;
    default:
      break;
  }
}

// ---- Sleep and wake -----------------------------------------------------------------
// The touch chip may doze while the screen is off (DRIFT_TOUCH_STANDBY): it is set once when
// the screen goes off and once when it comes on, never on every screen-off wake.
void touchRest() {
  if (!power::options.touchStandby || touchResting) return;
  touchResting = touch::setAutoStandby(true);
  if (!touchResting) hal::log("ui: touch standby not available, touch stays fully on");
}

void touchResume() {
  if (!touchResting) return;
  touchResting = false;
  if (!touch::setAutoStandby(false)) {
    hal::log("ui: touch did not answer after standby, restarting it");
    touch::restart();
  }
}

void sleepScreen() {
  if (!awake) return;
  hal::backlight(0);
  tft.writecommand(0x10);  // panel sleep in
  awake = false;
  fading = false;
  pressed = screens::Button::None;
  tr = Transition{};
  faceMoving = false;
  entrance.playing = false;  // decoration never moves with the screen off
  marker.moving = false;
  marker.minute = -1;
  releaseUntil = 0;
  lowNotice.until = 0;  // a notice cut short is not shown again
  // The screen always wakes on the face, so a stale panel is never the first thing seen.
  // Setup stays, so its progress or result is still there on wake. An unsaved time is dropped.
  if (mode == Mode::Panel || mode == Mode::Details || mode == Mode::SetTime || mode == Mode::TouchCheck) mode = Mode::Face;
  if (mode == Mode::PhoneSync) {
    ble::close();
    mode = Mode::Face;
  }
  wristRaise.reset();  // a wrist that is still up must come down and rise again
  wristTwist.reset();
  wokeByRaise = false;
  // Remember roughly what time it is, as a starting point for Set time if the power is lost.
  if (net::timeIsValid() && hal::now() - settings.lastKnownTime >= (time_t)KNOWN_TIME_SAVE_S) {
    settings.lastKnownTime = hal::now();
    settingsDirty = true;
  }
  if (settingsDirty) {
    saveSettings();
    settingsDirty = false;
  }
  standby.screenOff(hal::ms());
  touchRest();
}

void wakeScreen() {
  if (awake) return;
  touchResume();
  standby.screenOn(hal::ms());
  tft.writecommand(0x11);  // panel sleep out; the panel needs 5 ms before the next command
  hal::idle(5);
  awake = true;
  lastActivity = hal::ms();
  wristDrop.reset();
  // Draw the current time first, then fade the light up over it.
  showLowNotice();
  if (mode == Mode::Face) {
    renderFace(*cur, currentFace(), false);
    present(*cur);
    shownSecond = faceContext(false).now.tm_sec;
    dirty = false;
  } else {
    refreshScreen(true);
  }
  if (motionOn()) {
    fading = true;
    fadeStart = hal::ms();
  } else {
    hal::backlight(backlightLevel());
  }
}

void stepFade() {
  if (!fading) return;
  float t = since(fadeStart) / (float)motion::WAKE_MS;
  hal::backlight((uint8_t)(backlightLevel() * motion::easeOut(t)));
  if (t >= 1) fading = false;
}

// ---- Actions --------------------------------------------------------------------------
screens::SetupPhase setupPhase() {
  screens::View v{};
  v.portal = net::portalState();
  v.stage = net::syncState();
  v.result = net::lastResult();
  return screens::setupPhase(v);
}

void showMode(Mode m) {
  mode = m;
  pressed = screens::Button::None;
  if (m == Mode::Face) dirty = true;
  else refreshScreen(true);
}

// Setup is reached from the Wi-Fi control (or at first start), so leaving it lands there,
// unless the clock is still not set: then it lands on Set time and Sync now.
void showSettingsAtWifi() {
  settingsPage = net::timeIsValid() ? screens::pageOf(*cur, view(Mode::Panel, 0), screens::Button::WifiSetup)
                                    : firstSettingsPage();
  showMode(Mode::Panel);
}

// The build date, as a starting point when the clock has never been set.
tm buildDate() {
  tm t{};
  char mon[4] = "";
  int day = 1, year = screens::YEAR_MIN;
  sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
  const char *names = "JanFebMarAprMayJunJulAugSepOctNovDec", *at = strstr(names, mon);
  t.tm_mon = at && mon[0] ? (at - names) / 3 : 0;
  t.tm_mday = day;
  t.tm_year = constrain(year, screens::YEAR_MIN, screens::YEAR_MAX) - 1900;
  t.tm_hour = 12;
  screens::stepTime(t, screens::TimeField::Hour, 0);  // works out the weekday
  return t;
}

void openSetTime() {
  if (net::timeIsValid()) {
    time_t now = hal::now();
    localtime_r(&now, &editTime);
    screens::stepTime(editTime, screens::TimeField::Hour, 0);  // seconds to zero
  } else {
    // After a power loss, start from the last time the watch knew (saved at most an hour
    // before), else from the firmware's build date.
    editTime = buildDate();
    time_t known = (time_t)settings.lastKnownTime;
    tm k;
    localtime_r(&known, &k);
    if (known > mktime(&editTime) && k.tm_year + 1900 <= screens::YEAR_MAX) {
      editTime = k;
      screens::stepTime(editTime, screens::TimeField::Hour, 0);
    }
  }
  setTimePage = 0;
  showMode(Mode::SetTime);
}

void saveTime() {
  tm t = editTime;
  t.tm_isdst = -1;  // let the time zone rules decide
  net::setClockByHand(mktime(&t));
  cacheValid = false;
  lastMinute = -1;
  showMode(Mode::Face);
}

void openSetup() {
  net::startPortal();
  setupPage = 0;
  showMode(Mode::Setup);
}

void showSettingsAt(screens::Button b) {
  settingsPage = max(0, screens::pageOf(*cur, view(Mode::Panel, 0), b));
  showMode(Mode::Panel);
}

// ---- Screen turn and touch check ----------------------------------------------------------
void applyTurn() {
  tft.setRotation(settings.screenTurn);
  hal::log("ui: screen turn %d (%s), touch map %d%s", settings.screenTurn, orient::turnName(settings.screenTurn),
           activeTouchMap(), touchChecked() ? " checked" : " expected");
}

void openTouchCheck(Mode back, screens::Button backTo) {
  check = {};
  check.back = back;
  check.backTo = backTo;
  pressed = screens::Button::None;
  showMode(Mode::TouchCheck);
}

void leaveTouchCheck() {
  if (check.back == Mode::Panel) showSettingsAt(check.backTo);
  else showMode(Mode::Face);
}

// A raw tap on the touch check: the first dot, the second, then the verdict.
void touchCheckTap(int x, int y) {
  if (check.result != screens::CheckResult::Tapping) return;
  check.rawX[check.step] = x;
  check.rawY[check.step] = y;
  if (check.step == 0) {
    check.step = 1;
  } else {
    orient::Solve r = orient::solveTouchMap(check.rawX, check.rawY);
    hal::log("ui: touch check raw (%d,%d) (%d,%d) -> map %d, off by %d and %d px, %s", check.rawX[0], check.rawY[0],
             check.rawX[1], check.rawY[1], r.map, r.error[0], r.error[1], r.ok ? "ok" : "no match");
    if (r.ok) {
      settings.touchMap = r.map;
      settings.touchCheckedTurn = settings.screenTurn;
      markSettingsChanged();
    }
    check.result = r.ok ? screens::CheckResult::Matched : screens::CheckResult::NoMatch;
    check.resultAt = hal::ms();
  }
  refreshScreen(true);
}

void stepTouchCheck() {
  if (check.result == screens::CheckResult::Tapping || since(check.resultAt) < CHECK_RESULT_MS) return;
  if (check.result == screens::CheckResult::Matched) {
    leaveTouchCheck();
  } else {
    check.step = 0;  // try again
    check.result = screens::CheckResult::Tapping;
    refreshScreen(true);
  }
}

// ---- Phone sync -------------------------------------------------------------------------------
void openPhoneSync() {
  phone::begin();
  ble::open();
  phonePage = 0;
  showMode(Mode::PhoneSync);
}

void closePhoneSync() {
  ble::close();
  showSettingsAt(screens::Button::PhoneSync);
}

// Applies what the phone wrote and tells it how that went.
void stepPhone() {
  ble::poll();
  // A new phase (found, time set, closed) starts on its first page.
  static screens::PhonePhase shownPhase = screens::PhonePhase::Waiting;
  if (mode == Mode::PhoneSync) {
    screens::PhonePhase ph = screens::phonePhase(view());
    if (ph != shownPhase && tr.kind == Slide::None) phonePage = 0;
    shownPhase = ph;
  }
  char line[128], status[160];
  bool any = false;
  while (ble::nextLine(line, sizeof line)) {
    const char *reply = phone::apply(line);
    hal::log("ui: phone %c... -> %s", line[0], reply);  // never logs the password itself
    any = true;
    snprintf(status, sizeof status, "reply=%s;", reply);
    size_t n = strlen(status);
    phone::status(status + n, sizeof status - n, batteryPct);
    ble::setStatus(status);
  }
  if (any) {
    lastActivity = hal::ms();
    cacheValid = false;  // the time or zone may have changed
    lastMinute = -1;
  }
}

// Back goes up one level. In setup it depends on the phase: while the hotspot
// waits for a phone, back cancels it (nothing has been saved yet); while
// connecting, back leaves it running and the settings show its progress.
void goBack(float v = 0) {
  pressed = screens::Button::None;
  switch (mode) {
    case Mode::TouchCheck:
      leaveTouchCheck();  // skipped: the touch map stays as it was
      break;
    case Mode::PhoneSync:
      closePhoneSync();
      break;
    case Mode::Setup:
      if (setupPhase() == screens::SetupPhase::Hotspot) net::cancelPortal();
      showSettingsAtWifi();
      break;
    case Mode::SetTime:
      // Leaves without saving.
      settingsPage = screens::pageOf(*cur, view(Mode::Panel, 0), screens::Button::SetTime);
      showMode(Mode::Panel);
      break;
    case Mode::Details:
      // Back to the settings page that holds Details, in the current text size.
      settingsPage = screens::pageOf(*cur, view(Mode::Panel, 0), screens::Button::Details);
      showMode(Mode::Panel);
      break;
    case Mode::Panel: closeToFace(v); break;
    default: break;
  }
}

void activate(screens::Button b) {
  using screens::Button;
  switch (b) {
    case Button::Brightness:
      settings.brightnessLevel = (settings.brightnessLevel + 1) % BRIGHTNESS_LEVEL_COUNT;
      hal::backlight(backlightLevel());
      markSettingsChanged();
      break;
    case Button::TimeFormat:
      settings.use12h = !settings.use12h;
      cacheValid = false;
      markSettingsChanged();
      break;
    case Button::Motion:
      // Subtle, Expressive, Off, and round again.
      settings.motion = (settings.motion + 1) % MOTION_LEVELS;
      markSettingsChanged();
      break;
    case Button::SyncNow:
      net::startSync();  // ignored while one is already running, so taps never stack up
      break;
    case Button::SetTime:
      openSetTime();
      return;
    case Button::RaiseWake: {
      // Wake gesture: both, then raise only, twist only, off, and round again.
      int g = settings.raiseToWake && settings.twistToWake ? 0 : settings.raiseToWake ? 1 : settings.twistToWake ? 2 : 3;
      g = (g + 1) % 4;
      settings.raiseToWake = g == 0 || g == 1;
      settings.twistToWake = g == 0 || g == 2;
      markSettingsChanged();
      break;
    }
    case Button::Minus:
    case Button::Plus:
      screens::stepTime(editTime, (screens::TimeField)setTimePage, b == Button::Plus ? +1 : -1);
      break;
    case Button::SaveTime:
      saveTime();
      return;
    case Button::WifiSetup:
    case Button::EditWifi:
    case Button::TryAgain:
      openSetup();
      return;
    case Button::Details:
      detailsPage = 0;
      showMode(Mode::Details);
      return;
    case Button::TextSize: {
      settings.largeText = !settings.largeText;
      markSettingsChanged();
      // Pages reflow with the new size; stay on the page with this control.
      detailsPage = screens::pageOf(*cur, view(Mode::Details, 0), Button::TextSize);
      break;
    }
    case Button::Retry:
      net::startSync();
      break;
    case Button::PhoneSync:
      openPhoneSync();
      return;
    case Button::Night:
      settings.night = (settings.night + 1) % NIGHT_CHOICES;
      markSettingsChanged();
      break;
    case Button::ScreenTurn:
      // Turn the screen, then check touch for the new turn straight away. BOOT skips the check
      // and keeps the expected touch map for this turn.
      settings.screenTurn = (settings.screenTurn + 1) % orient::TURNS;
      markSettingsChanged();
      applyTurn();
      openTouchCheck(Mode::Panel, Button::ScreenTurn);
      return;
    case Button::TouchCheck:
      openTouchCheck(Mode::Panel, Button::TouchCheck);
      return;
    case Button::Cancel:
      if (mode == Mode::PhoneSync) {
        closePhoneSync();
        return;
      }
      if (setupPhase() == screens::SetupPhase::Connecting) net::cancelSync();
      else net::cancelPortal();
      showSettingsAtWifi();
      return;
    case Button::Back:
      if (mode == Mode::PhoneSync) closePhoneSync();
      else showSettingsAtWifi();  // a running sync carries on
      return;
    case Button::Done:
      if (mode == Mode::PhoneSync) ble::close();
      showMode(Mode::Face);
      return;
    default:
      break;
  }
  refreshScreen(true);
}

screens::Button hitTest(int x, int y) {
  if (!pagedMode()) return screens::Button::None;
  return screens::hit(*cur, view(), x, y);
}

// ---- Motion -----------------------------------------------------------------------------
// Someone pressing the screen or BOOT is looking at it, which shows which way the screen
// faces relative to the motion sensor. The answer is kept so raise-to-wake works either way up.
void noteLooking() {
  Accel a;
  if (!imu::present() || !imu::read(a)) return;
  wristRaise.learnFacing(a);
  if (wristRaise.facing() != settings.screenFacing) {
    settings.screenFacing = wristRaise.facing();
    markSettingsChanged();
    hal::log("ui: screen faces %s z", wristRaise.facing() > 0 ? "+" : "-");
  }
}

// While the screen is off, every fresh motion sample goes to both detectors (from polling or
// from a motion interrupt's qualifying window, see power.h). True when one recognises a gesture.
bool feedGesture(uint32_t ms, const Accel &a) {
  // Both detectors see every sample, so each keeps its own picture of the wrist.
  bool raised = wristRaise.update(ms, a) && settings.raiseToWake;
  bool twisted = wristTwist.update(ms, a, wristRaise.facing()) && settings.twistToWake;
  if (raised) hal::log("ui: raise to wake (turned at %d deg/s)", (int)wristRaise.lastPeakRate());
  else if (twisted) hal::log("ui: wrist twist wake (turned at %d deg/s)", (int)wristTwist.lastPeakRate());
  return raised || twisted;
}

// While the screen is on a face, turning the wrist away after a look puts it to sleep early.
void stepDrop() {
  if (!gesturesOn() || mode != Mode::Face || tr.kind != Slide::None || finger.down || boot.down) return;
  if (since(lastImuRead) < IMU_AWAKE_MS) return;
  lastImuRead = hal::ms();
  Accel a;
  if (!imu::read(a)) return;
  float g = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  if (wristDrop.update(hal::ms(), a.z * wristRaise.facing(), g)) {
    hal::log("ui: wrist down, screen off");
    sleepScreen();
  }
}

// ---- Touch ------------------------------------------------------------------------------
void onFingerDown(const TouchSample &s) {
  finger = Finger{};
  finger.down = true;
  finger.x0 = finger.x = s.x;
  finger.y0 = finger.y = s.y;
  finger.t0 = finger.lastMoveAt = finger.lastSampleAt = hal::ms();
  lastActivity = hal::ms();
  wristDrop.touched(hal::ms());
  noteLooking();

  if (!awake || wakeGuard) {
    wakeScreen();
    finger.ignore = true;  // the waking touch never also acts
    return;
  }
  if (mode == Mode::Boot) {
    // A touch skips the startup sequence.
    bootStart = hal::ms() - motion::BOOT_MS - BOOT_HOLD_MS;
    finger.ignore = true;
    return;
  }
  releaseUntil = 0;
  cancelEntrance(true);  // a new touch ends a running entrance, never queues behind it
  if (tr.kind == Slide::Face) {
    // Catch a moving face where it is. A slide that was already committing
    // counts as done, so each quick swipe moves one more face.
    tr.animating = false;
    tr.dragging = true;
    if (tr.to <= -W && tr.pos < 0) {
      landOn(+1);
      tr.pos += W;
    } else if (tr.to >= W && tr.pos > 0) {
      landOn(-1);
      tr.pos -= W;
    }
    tr.grabPos = tr.pos;
    finger.axis = Axis::Horizontal;
    return;
  }
  if (tr.kind != Slide::None) {
    finger.ignore = true;  // panel and page slides are short; let them finish
    return;
  }
  if (mode == Mode::TouchCheck) {
    finger.ignore = true;  // a tap here is a raw position, never a control or a swipe
    touchCheckTap(rawX, rawY);
    return;
  }
  pressed = hitTest(s.x, s.y);
  if (pressed != screens::Button::None) refreshScreen(true);  // pressed state on the same poll
}

void onFingerMove(const TouchSample &s) {
  uint32_t now = hal::ms();
  // The touch chip only reports a new position every 10-20 ms, so velocity is
  // measured between reports that moved, not between polls.
  if (s.x != finger.x || s.y != finger.y) {
    uint32_t dt = max<uint32_t>(1, now - finger.lastMoveAt);
    float ivx = (s.x - finger.x) / (float)dt, ivy = (s.y - finger.y) / (float)dt;
    finger.vx = 0.6f * ivx + 0.4f * finger.vx;
    finger.vy = 0.6f * ivy + 0.4f * finger.vy;
    finger.lastMoveAt = now;
  }
  finger.x = s.x;
  finger.y = s.y;
  finger.lastSampleAt = now;
  lastActivity = now;
  if (finger.ignore) return;

  int dx = finger.x - finger.x0, dy = finger.y - finger.y0;
  if (finger.axis == Axis::None && (abs(dx) > motion::SLOP_PX || abs(dy) > motion::SLOP_PX)) {
    finger.axis = abs(dx) >= abs(dy) ? Axis::Horizontal : Axis::Vertical;
    if (pressed != screens::Button::None) {
      pressed = screens::Button::None;  // a drag is never also a tap
      refreshScreen(true);
    }
    if (mode == Mode::Face && finger.axis == Axis::Horizontal && haveSpare && motionOn()) {
      tr = Transition{};
      tr.kind = Slide::Face;
      tr.dragging = true;
      dirty = true;  // clears any hold ring
    }
    if (mode == Mode::Face && finger.axis == Axis::Vertical && dy < 0 && haveSpare && motionOn()) startPanelDrag();
  }

  if (tr.kind == Slide::PanelOpen && tr.dragging) tr.pos = constrain((float)-dy, 0.0f, (float)H);  // pinned to the finger

  if (tr.kind == Slide::Face && tr.dragging) {
    // Dragging a whole screen width lands on the neighbour and carries on, so
    // fast repeated swipes each count.
    float pos = tr.grabPos + dx;
    while (pos <= -W) { landOn(+1); pos += W; tr.grabPos += W; }
    while (pos >= W) { landOn(-1); pos -= W; tr.grabPos -= W; }
    tr.pos = pos;
    prepareNeighbour(tr.pos);
  }
}

void onFingerUp() {
  finger.down = false;
  if (finger.ignore) return;
  int dx = finger.x - finger.x0, dy = finger.y - finger.y0;
  bool moving = since(finger.lastMoveAt) < FLICK_WINDOW_MS;
  float vx = moving ? finger.vx : 0, vy = moving ? finger.vy : 0;

  if (tr.kind == Slide::Face && tr.dragging) {
    // A flick decides by direction; otherwise distance decides.
    float target = 0;
    if (vx < -motion::FLICK_PX_PER_MS) target = -W;
    else if (vx > motion::FLICK_PX_PER_MS) target = W;
    else if (tr.pos < -motion::COMMIT_PX) target = -W;
    else if (tr.pos > motion::COMMIT_PX) target = W;
    settleTo(target, vx);
    return;
  }
  if (tr.kind == Slide::PanelOpen && tr.dragging) {
    // Same rule as before the panel followed the finger: far enough up, or flicked up, opens.
    bool open = (dy <= -SWIPE_PX || vy < -motion::FLICK_PX_PER_MS) && vy <= motion::FLICK_PX_PER_MS;
    settleTo(open ? H : 0, -vy);
    return;
  }
  // Sideways swipes are scoped to the mode: faces on a face, pages on a paged
  // screen. One swipe only ever does one of the two.
  if (finger.axis == Axis::Horizontal) {
    int dir = dx <= -SWIPE_PX || vx < -motion::FLICK_PX_PER_MS ? +1 : dx >= SWIPE_PX || vx > motion::FLICK_PX_PER_MS ? -1 : 0;
    if (dir && mode == Mode::Face) cutToFace(dir);  // reduced motion, or no spare buffer: change on release
    else if (dir && pagedMode()) changePage(dir, vx ? vx : (dir > 0 ? -0.01f : 0.01f));
    return;
  }
  if (finger.axis == Axis::Vertical) {
    bool up = dy <= -SWIPE_PX || vy < -motion::FLICK_PX_PER_MS;
    bool down = dy >= SWIPE_PX || vy > motion::FLICK_PX_PER_MS;
    if (mode == Mode::Face && up) openPanel(vy ? -vy : 0.01f);
    else if (pagedMode() && down) goBack(vy ? -vy : -0.01f);
    return;
  }
  if (finger.axis == Axis::None && pressed != screens::Button::None) {
    screens::Button b = pressed;
    pressed = screens::Button::None;
    if (finger.repeats) {
      refreshScreen(true);  // a held - or + already stepped; release adds nothing
    } else if (hitTest(finger.x, finger.y) == b) {
      // The control keeps its accent briefly after acting (unless Motion is Off or the tap
      // moved to another screen), drawn in the same frame as the change it made.
      if (motionOn()) pressed = b;
      Mode before = mode;
      activate(b);
      if (pressed == b && mode == before) releaseUntil = hal::ms() + motion::RELEASE_MS;
      else if (pressed == b) {
        pressed = screens::Button::None;
        refreshScreen(true);
      }
    } else {
      refreshScreen(true);
    }
  }
}

// The touch chip stopped answering in the middle of a touch. Its last report was a finger
// still down, so this is not a release: nothing under the finger acts, and a face being
// dragged springs back to where it started.
void cancelFinger() {
  finger.down = false;
  if (finger.ignore) return;
  if ((tr.kind == Slide::Face || tr.kind == Slide::PanelOpen) && tr.dragging) settleTo(0, 0);
  if (pressed != screens::Button::None) {
    pressed = screens::Button::None;
    refreshScreen(true);
  }
  dirty = true;  // clears a started hold ring
}

// Holding - or + on the set time screen steps again and again, faster after a while.
void stepRepeat() {
  using screens::Button;
  if (mode != Mode::SetTime || !finger.down || finger.ignore || finger.axis != Axis::None) return;
  if (pressed != Button::Minus && pressed != Button::Plus) return;
  uint32_t now = hal::ms();
  if (!finger.repeatAt) finger.repeatAt = finger.t0 + REPEAT_DELAY_MS;
  if ((int32_t)(now - finger.repeatAt) < 0) return;
  screens::stepTime(editTime, (screens::TimeField)setTimePage, pressed == Button::Plus ? +1 : -1);
  finger.repeats++;
  finger.repeatAt = now + (finger.repeats > 8 ? REPEAT_FAST_MS : REPEAT_MS);
  refreshScreen(true);
}

// Returns true while the hold ring is on screen, so the face does not paint over it.
// A still finger or a held BOOT button both open settings after the long-press time.
bool stepHold() {
  if (mode != Mode::Face || tr.kind != Slide::None) return false;
  bool fingerHold = finger.down && !finger.ignore && finger.axis == Axis::None;
  bool bootHold = boot.down && !boot.used;
  if (!fingerHold && !bootHold) return false;
  uint32_t held = fingerHold ? since(finger.t0) : since(boot.downAt);
  if (held >= motion::LONG_PRESS_MS) {
    if (fingerHold) {
      finger.longFired = true;
      finger.ignore = true;  // the rest of this touch does nothing
    } else {
      boot.used = true;      // releasing the button afterwards does nothing
    }
    openPanel();
    return true;
  }
  if (held < HOLD_HINT_MS) return false;
  // The hold ring grows from 12 o'clock so the user can see the press registering.
  if (since(lastPresent) >= ANIM_FRAME_MS) {
    float p = (held - HOLD_HINT_MS) / (float)(motion::LONG_PRESS_MS - HOLD_HINT_MS);
    renderFace(*cur, currentFace(), true);
    gfx::arc(*cur, 119, 116, 0, max(6.0f, 360 * p), col::CYAN);
    present(*cur);
    dirty = true;  // repaint without the ring after release
  }
  return true;
}

void pollBoot() {
  bool down = hal::bootButtonDown();
  if (down == boot.down) return;
  boot.down = down;
  lastActivity = hal::ms();
  if (down) {
    boot.downAt = hal::ms();
    boot.used = true;
    boot.checkOpened = false;
    noteLooking();
    if (awake) cancelEntrance(true);
    if (!awake) wakeScreen();  // the waking press does nothing else
    else if (mode == Mode::Boot) bootStart = hal::ms() - motion::BOOT_MS - BOOT_HOLD_MS;
    else if (mode == Mode::Face) boot.used = tr.kind != Slide::None;  // decided on release or at the hold time
    else if (tr.kind == Slide::None) goBack();
    return;
  }
  // Released before the hold time on a face: next face.
  if (boot.used || mode != Mode::Face || tr.kind != Slide::None) return;
  boot.used = true;
  dirty = true;  // clears a started hold ring
  if (haveSpare && motionOn()) {
    tr = Transition{};
    tr.kind = Slide::Face;
    prepareNeighbour(-1);
    animateTo(-W, motion::FACE_MS, W);
  } else {
    cutToFace(+1);
  }
}

// Holding BOOT for 4 s opens the touch check from anywhere: the way back if touch is so far off
// that the Touch check control cannot be tapped.
void stepBootCheck() {
  if (!boot.down || boot.checkOpened || !awake || since(boot.downAt) < BOOT_CHECK_MS) return;
  boot.checkOpened = true;
  tr = Transition{};
  openTouchCheck(Mode::Face, screens::Button::None);
}

void runSerialCommand(const char *line) {
  serialcmd::Command cmd = serialcmd::parse(line);
  if (cmd.kind == serialcmd::Kind::Time) {
    net::setClockByHand(cmd.time);
    dirty = true;
    cacheValid = false;
    Serial.printf("Time set to %ld\n", (long)cmd.time);
  } else if (cmd.kind == serialcmd::Kind::Wifi) {
    net::saveWifi(cmd.ssid, cmd.password);
    Serial.printf("Configuring Wi-Fi for SSID: %s\n", cmd.ssid);
  }
}

// Takes only the bytes already waiting, so a line sent in pieces never stalls the interface.
void pollSerial() {
  static serialcmd::LineBuffer lineIn;
  for (int n = 0; n < SERIAL_BYTES_PER_POLL && Serial.available() > 0; n++) {
    int c = Serial.read();
    if (c < 0) break;
    if (lineIn.push((char)c)) runSerialCommand(lineIn.line());
  }
}

void pollInput() {
  pollSerial();
  if (touchResting) {
    pollBoot();  // the resting touch chip is not read; its interrupt line wakes the watch
    return;
  }
  TouchSample s{};
  if (!touch::read(s)) {
    // The shared I2C bus can drop a read. That says nothing about the finger, so the touch
    // carries on as it was; only a run of failures ends it, and then without acting.
    static uint32_t failingSince = 0;
    if (!touchFailing) failingSince = hal::ms();
    touchFailing = true;
    if (finger.down && since(failingSince) >= TOUCH_DROPOUT_MS) cancelFinger();
    pollBoot();
    return;
  }
  touchFailing = false;
  if (!s.down) wakeGuard = false;
  // The chip reports positions in the panel's own frame; turn them to match the screen.
  rawX = s.x;
  rawY = s.y;
  int x, y;
  orient::apply(activeTouchMap(), s.x, s.y, x, y);
  s.x = x;
  s.y = y;
  if (s.down && !finger.down) onFingerDown(s);
  else if (s.down) onFingerMove(s);
  else if (finger.down) onFingerUp();

  pollBoot();
}

// ---- Per-mode drawing -----------------------------------------------------------------------
void stepBoot() {
  uint32_t t = since(bootStart);
  if (t >= motion::BOOT_MS + BOOT_HOLD_MS) {
    // Always on to the face, never into the Wi-Fi hotspot: without a time the face says how to
    // set one. The first start with a new screen turn checks touch first.
    if (!touchChecked()) {
      openTouchCheck(Mode::Face, screens::Button::None);
    } else {
      mode = Mode::Face;
      dirty = true;
    }
    return;
  }
  if (since(lastPresent) < ANIM_FRAME_MS) return;
  screens::drawBoot(*cur, min(1.0f, t / (float)motion::BOOT_MS), "v" FW_VERSION);
  present(*cur);
}

void stepFace() {
  FaceContext c = faceContext(false);
  lastMinute = c.now.tm_min;
  // The radar beam and Phantom's movement run all the time their face is on screen, unless
  // motion is reduced, the battery is low, or memory is short and there is no cached dial to
  // draw over (then they step with the seconds, drawn once a second). With the screen off or
  // another screen up this is not called at all.
  bool moving = faceAnimates(currentFace()) && motionOn() && !lowPower() && haveCache;
  if (moving != faceMoving) {
    faceMoving = moving;
    sweepStart = hal::ms();
    dirty = true;
  }
  uint32_t frameMs = currentFace() == FACE_RADAR ? motion::RADAR_FRAME_MS : motion::PHANTOM_FRAME_MS;

  // The entrance runs at the slide frame rate and ends on its exact final state.
  if (entrance.playing && (entrance.face != currentFace() || since(entrance.start) >= entrance.length)) {
    if (entrance.face == currentFace()) entrance.finished++;
    entrance.playing = false;
    dirty = true;
  }
  // Orbit's marker moves on to a new minute only when the clock ticked into it with the face
  // on screen; any other jump (wake, a sync, setting the time) just redraws it there.
  if (faceMovesMarker(currentFace()) && c.timeValid) {
    int minute = c.now.tm_hour * 60 + c.now.tm_min;
    if (marker.minute >= 0 && minute == (marker.minute + 1) % 1440 && entrancesOn() && !entrance.playing) {
      marker.moving = true;
      marker.start = hal::ms();
    }
    marker.minute = minute;
  }
  if (marker.moving && (!faceMovesMarker(currentFace()) || since(marker.start) >= motion::MARKER_MS)) {
    marker.moving = false;
    dirty = true;
  }
  bool stepping = (entrance.playing || marker.moving) && since(lastPresent) >= motion::ENTRANCE_FRAME_MS;
  showLowNotice();
  if (lowNotice.until && (int32_t)(hal::ms() - lowNotice.until) >= 0) {
    lowNotice.until = 0;
    dirty = true;
  }

  bool due = dirty || stepping || c.now.tm_sec != shownSecond || (faceMoving && since(lastPresent) >= frameMs);
  if (!due) return;
#if DRIFT_DIAG
  uint32_t t0 = micros();
#endif
  renderFace(*cur, currentFace(), true);
  present(*cur);
#if DRIFT_DIAG
  if (entrance.playing || marker.moving) {
    uint32_t used = micros() - t0;
    diag.motionFrames++;
    diag.motionUs += used;
    diag.motionWorstUs = max(diag.motionWorstUs, used);
  }
#endif
  shownSecond = c.now.tm_sec;
  dirty = false;
}

void stepNetwork() {
  net::poll();
  if (net::attempts() != seenAttempts) {
    seenAttempts = net::attempts();
    // With no network saved there is nothing to retry; the clock may have been set by hand.
    // Failures in a row wait 30 minutes, then 1, 2 and 4 hours, then every 6 hours like a working
    // sync: away from the saved network, the radio is not switched on every half hour for nothing.
    bool ok = net::lastResult() == net::Result::Ok;
    failsInARow = ok ? 0 : failsInARow < 8 ? failsInARow + 1 : 8;
    uint32_t wait = ok ? RESYNC_MS : min<uint32_t>(RESYNC_MS, RETRY_SYNC_MS << (failsInARow - 1));
    nextSyncAt = !net::hasSavedWifi() ? 0 : hal::ms() + wait;
    if (net::hasSavedWifi() && !nextSyncAt) nextSyncAt = 1;  // 0 means none; millis() can wrap onto it
    dirty = true;
  }
  net::Sync s = net::syncState();
  if (s != seenSync) {
    seenSync = s;
    dirty = true;
  }
  net::Portal p = net::portalState();
  if (p != seenPortal) {
    seenPortal = p;
    lastActivity = hal::ms();
  }
  // A new setup phase has a new layout; start it on its first page, and give the
  // result its full time on screen.
  screens::SetupPhase ph = setupPhase();
  if (ph != seenPhase) {
    seenPhase = ph;
    setupPage = 0;
    if (mode == Mode::Setup) {
      pressed = screens::Button::None;
      lastActivity = hal::ms();
    }
  }
  // Resync quietly while the screen is off so it never interrupts anything.
  if (!awake && nextSyncAt && (int32_t)(hal::ms() - nextSyncAt) >= 0) {
    nextSyncAt = 0;
    net::startSync();
  }
}

bool setupHoldsScreen() {
  if (mode == Mode::PhoneSync) return ble::on();  // the screen stays on while Bluetooth is open
  if (mode != Mode::Setup) return false;
  net::Portal p = net::portalState();
  return p == net::Portal::Waiting || p == net::Portal::PhoneJoined || syncBusy();
}

uint32_t screenTimeout() {
  if (mode == Mode::PhoneSync) return RESULT_TIMEOUT_MS;
  if (mode != Mode::Setup) return SCREEN_TIMEOUT_MS;
  screens::SetupPhase p = setupPhase();
  bool result = p == screens::SetupPhase::Done || p == screens::SetupPhase::Failed || p == screens::SetupPhase::TimedOut;
  return result ? RESULT_TIMEOUT_MS : SCREEN_TIMEOUT_MS;
}

// ---- Screen-off waits -------------------------------------------------------------------------
power::Inputs standbyInputs() {
  power::Inputs in;
  bool on = gesturesOn();  // includes night mode and the sensor being there
  in.raise = on && settings.raiseToWake;
  in.twist = on && settings.twistToWake;
  return in;
}

// The next moment something needs the processor while the screen is off, so a sleep can last
// until then and no longer: the next resync, a settings write, the next battery reading, and
// the next hour while night mode is on (night mode turns the gestures on and off on the hour).
// The clock's drift nudges need no deadline: ClockTrim works from the time that has passed, so
// a nudge after a 30 s sleep is just a larger one (tested in net_test).
uint32_t nextDeadline() {
  uint32_t now = hal::ms(), at = now + power::options.maxSleepMs;
  auto take = [&](uint32_t t) {
    if ((int32_t)(t - at) < 0) at = (int32_t)(t - now) > 0 ? t : now;
  };
  if (nextSyncAt) take(nextSyncAt);
  if (settingsDirty) take(settingsChangedAt + SAVE_DELAY_MS + 1);
  take(lastBatteryRead + BATTERY_READ_ASLEEP_MS);
  if (settings.night && net::timeIsValid()) {
    time_t t = hal::now();
    tm n;
    localtime_r(&t, &n);
    take(now + (uint32_t)((60 - n.tm_min) * 60 - n.tm_sec) * 1000 + 1);
  }
  return at;
}

void standbyStep() {
  power::Inputs in = standbyInputs();
  power::Wake w = net::radioOn() || ble::on() ? standby.stepRadio(in, feedGesture)
                                              : standby.step(in, nextDeadline(), feedGesture);
  switch (w) {
    case power::Wake::Gesture:
      wakeScreen();
      wokeByRaise = true;
      break;
    case power::Wake::Touch:
    case power::Wake::Button:
      wakeGuard = true;
      touchResume();
      pollInput();               // a touch or press still down wakes the screen and does nothing else
      if (!awake) wakeScreen();  // it was over before it could be read (a short touch pulse): wake anyway
      break;
    case power::Wake::None:
      break;
  }
}

#if DRIFT_DIAG
void stepDiag() {
  if (since(diag.lastLog) < 10000) return;
  uint32_t n = max<uint32_t>(1, diag.frames);
  snprintf(diag.line, sizeof diag.line, "draw %lums  push %lums  heap %luk", (unsigned long)(diag.renderUs / n / 1000),
           (unsigned long)(diag.pushUs / n / 1000), (unsigned long)(hal::freeHeap() / 1024));
  hal::log("diag: frames %lu, draw avg %lu us worst %lu us, push avg %lu us, heap %lu, psram %lu",
           (unsigned long)diag.frames, (unsigned long)(diag.renderUs / n), (unsigned long)diag.worstUs,
           (unsigned long)(diag.pushUs / n), (unsigned long)hal::freeHeap(), (unsigned long)hal::freePsram());
  if (diag.motionFrames)
    hal::log("diag: entrance and marker frames %lu, draw and push avg %lu us worst %lu us", (unsigned long)diag.motionFrames,
             (unsigned long)(diag.motionUs / diag.motionFrames), (unsigned long)diag.motionWorstUs);
  diag.frames = diag.renderUs = diag.pushUs = diag.worstUs = 0;
  diag.motionFrames = diag.motionUs = diag.motionWorstUs = 0;
  diag.lastLog = hal::ms();
}
#endif

// Not even an 8-bit frame fitted in memory, so nothing can be drawn the usual way. This
// writes straight to the panel, which needs no frame, so the screen says why it is blank.
void showNoMemory() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(col::CYAN, TFT_BLACK);
  tft.setTextFont(4);
  tft.drawString("DRIFT", W / 2, 90);
  tft.setTextColor(col::TEXT_2, TFT_BLACK);
  tft.setTextFont(2);
  tft.drawString("Out of memory", W / 2, 130);
  tft.drawString("Restart the watch", W / 2, 152);
  hal::backlight(backlightLevel());
}
}  // namespace

namespace ui {

TFT_eSPI &display() { return tft; }

int controls(screens::Control *out, int max) { return pagedMode() ? screens::controls(*cur, view(), out, max) : 0; }
int page() { return pageFor(mode); }
float beamAngle() { return faceContext(true).beamDeg; }
const power::Controller &standbyController() { return standby; }
bool entrancePlaying() { return entrance.playing; }
uint32_t entrancesStarted() { return entrance.started; }
uint32_t entrancesFinished() { return entrance.finished; }
bool markerMoving() { return marker.moving; }

const char *screenName() {
  static const char *names[] = {"boot", "face", "panel", "details", "setup", "settime", "touchcheck", "phone"};
  static const char *sliding[] = {"boot+slide",  "face+slide",    "panel+slide",      "details+slide",
                                  "setup+slide", "settime+slide", "touchcheck+slide", "phone+slide"};
  if (!awake) return "asleep";
  return (tr.kind != Slide::None ? sliding : names)[(int)mode];
}

void begin() {
  hal::begin();
  // Each start-up step is logged before it runs, so a start that stalls shows where.
  hal::watchdogStart(DRIFT_WATCHDOG_S);
  // Start from a clean interface state (the simulation calls begin() more than once).
  cur = &spriteA;
  nb = &spriteB;
  tr = Transition{};
  finger = Finger{};
  pressed = screens::Button::None;
  awake = true;
  standby = power::Controller{};
  touchResting = wakeGuard = false;
  settingsPage = detailsPage = setupPage = setTimePage = 0;
  failsInARow = 0;
  loadSettings();
  if (settings.face >= FACE_COUNT) settings.face = 0;
  settings.brightnessLevel %= BRIGHTNESS_LEVEL_COUNT;
  settledFace = currentFace();
  entrance.playing = marker.moving = false;
  marker.minute = -1;
  releaseUntil = 0;

  hal::log("boot: settings v%u, face %u", (unsigned)SETTINGS_VERSION, (unsigned)settings.face);
  hal::log("boot: screen");
  tft.init();
  applyTurn();  // sprites stay 240 x 240: the panel turns the picture as it is written
  tft.fillScreen(TFT_BLACK);
  // Three full 16-bit frames (115 KB each) in PSRAM: what is on screen, the
  // incoming screen during a slide, and the cached static face layer. Without
  // PSRAM the watch still works, with cuts instead of slides.
  haveSpare = haveCache = false;
  bool a = spriteA.createSprite(W, H) != nullptr;
  haveFrame = true;
  if (!a) {
    spriteA.setColorDepth(8);
    haveFrame = spriteA.createSprite(W, H) != nullptr;
  } else {
    haveSpare = spriteB.createSprite(W, H) != nullptr;
    haveCache = cache.createSprite(W, H) != nullptr;
  }
  hal::log("ui: frame %s, spare %s, cache %s, free psram %lu", a ? "16-bit" : haveFrame ? "8-bit" : "none", haveSpare ? "yes" : "no",
           haveCache ? "yes" : "no", (unsigned long)hal::freePsram());
  if (!haveFrame) {
    showNoMemory();
    return;
  }

  hal::log("boot: touch");
  touch::begin();
  hal::log("boot: motion sensor");
  imu::begin();
  wristRaise = RaiseDetector{};
  wristDrop = DropDetector{};
  check = {};
  batteryLife = battery::Estimate{};
  lowWarning = battery::LowWarning{};
  lowNotice = LowNotice{};
  faceMoving = false;
  phonePage = checkPage = 0;
  wristRaise.setFacing(settings.screenFacing ? settings.screenFacing : IMU_SCREEN_FACING_DEFAULT);
  readBattery();
  hal::log("boot: battery %d mV, network", batteryMv);
  net::begin();
  if (net::hasSavedWifi()) net::startSync();  // runs alongside the startup sequence

  bool sequence = hal::coldBoot() && motionOn();
  mode = sequence ? Mode::Boot : Mode::Face;
  bootStart = lastActivity = hal::ms();
  if (!sequence && !touchChecked()) {
    openTouchCheck(Mode::Face, screens::Button::None);
  } else if (mode == Mode::Face) {
    renderFace(*cur, currentFace(), false);
    present(*cur);
    dirty = false;
  } else {
    screens::drawBoot(*cur, 0, "v" FW_VERSION);
    present(*cur);
  }
  hal::backlight(backlightLevel());
  hal::log("boot: done, %s", sequence ? "startup sequence" : "face");
}

void loop() {
  hal::watchdogFeed();
  if (!haveFrame) {
    hal::idle(100);  // nothing can be drawn; the notice from begin() stays up
    return;
  }
  pollInput();
  stepNetwork();
  if (ble::on() || ble::state() != ble::State::Off) stepPhone();
  if (awake ? since(lastBatteryRead) > BATTERY_READ_MS : since(lastBatteryRead) >= BATTERY_READ_ASLEEP_MS) readBattery();
  if (settingsDirty && since(settingsChangedAt) > SAVE_DELAY_MS) {
    saveSettings();
    settingsDirty = false;
  }

  if (!awake) {
    standbyStep();  // the panel is asleep; only touch, BOOT, motion, Wi-Fi and the clock need attention
    return;
  }

  bool held = finger.down || boot.down || setupHoldsScreen() || tr.kind != Slide::None || mode == Mode::Boot;
  if (held) lastActivity = hal::ms();
  if (since(lastActivity) > screenTimeout()) {
    sleepScreen();
    return;
  }

  stepFade();
  stepBootCheck();
  stepDrop();
  if (!awake) return;
  if (tr.kind != Slide::None) stepTransition();
  else if (mode == Mode::Boot) stepBoot();
  else if (mode == Mode::Face) {
    if (!stepHold()) stepFace();
  } else if (mode == Mode::TouchCheck) {
    stepTouchCheck();
    refreshScreen(false);
  } else {
    stepRepeat();
    if (releaseUntil && (int32_t)(hal::ms() - releaseUntil) >= 0) {
      releaseUntil = 0;
      if (!finger.down) pressed = screens::Button::None;  // the accent after a tap ends
    }
    refreshScreen(false);
  }
  if (mode != Mode::Face) entrance.playing = marker.moving = false;
#if DRIFT_DIAG
  stepDiag();
#endif

  bool busy = tr.kind != Slide::None || fading || faceMoving || entrance.playing || marker.moving || mode == Mode::Boot ||
              finger.down || boot.down || releaseUntil;
  hal::idle(busy ? 1 : 10);
}

}  // namespace ui
