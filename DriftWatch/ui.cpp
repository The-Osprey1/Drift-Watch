// Interface state, core helpers, network sync scheduling and the main ui::begin / ui::loop entry points.
#include "ui.h"
#include "ui_parts.h"
#include "ble.h"
#include "gfx.h"
#include "hal.h"
#include "imu.h"
#include "orient.h"
#include "settings.h"
#include "theme.h"

namespace uiparts {

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

// Smooth power transitions: perceptual backlight curve + radial iris bloom/collapse.
bool fading = false;
uint32_t fadeStart = 0;
bool irisWaking = false;
uint32_t irisStart = 0;
bool sleeping = false;
uint32_t sleepStart = 0;

// Face drawing state.
uint32_t cacheKey = 0;
bool cacheValid = false, dirty = true;
int shownSecond = -1, lastMinute = -1;
bool faceMoving = false;    // the face animates between seconds: the radar beam, Phantom's movement
uint32_t sweepStart = 0;

EntranceState entrance;
Face settledFace = FACE_DIGITAL;  // the face last shown in its final state, or entering
MarkerState marker;
uint32_t releaseUntil = 0;  // a tapped control keeps its pressed look until then

int batteryPct = -1, batteryMv = -1;
battery::Estimate batteryLife;
battery::LowWarning lowWarning;
LowNotice lowNotice;
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
int settingsPage = 0, detailsPage = 0, setupPage = 0, setTimePage = 0, phonePage = 0, checkPage = 0, drawerPage = 0;
TimerState timer{};
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

CheckState check;
int rawX = 0, rawY = 0;  // latest touch position as the chip reported it
screens::SetupPhase seenPhase = screens::SetupPhase::Hotspot;
BootButton boot;
Finger finger;
bool touchFailing = false;  // the last touch read got no answer from the chip
Transition tr;

#if DRIFT_DIAG
DiagState diag;
#endif

// ---- Small helpers ---------------------------------------------------------------
bool motionOn() { return settings.motion != MOTION_OFF; }
// Entrances need the slide buffers and the cached dial; the low-memory fallback changes instantly.
bool entrancesOn() { return motionOn() && haveSpare && haveCache; }
uint32_t since(uint32_t t) { return hal::ms() - t; }
Face currentFace() { return (Face)(settings.face % FACE_COUNT); }
Face nextActiveFace(int cur, int dir) {
  uint16_t mask = settings.activeFaces & ALL_FACES_MASK;
  if (!mask) mask = ALL_FACES_MASK;
  int f = cur;
  for (int i = 0; i < FACE_COUNT; i++) {
    f = (f + FACE_COUNT + dir) % FACE_COUNT;
    if (mask & (1u << f)) return (Face)f;
  }
  return (Face)cur;
}

Face faceAt(int step) {
  if (step == 0) return currentFace();
  int dir = step > 0 ? 1 : -1;
  int count = abs(step);
  Face f = currentFace();
  for (int i = 0; i < count; i++) {
    f = nextActiveFace(f, dir);
  }
  return f;
}

void markSettingsChanged() {
  settingsDirty = true;
  settingsChangedAt = hal::ms();
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

}  // namespace uiparts

namespace ui {
using namespace uiparts;

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
  static const char *names[] = {"boot", "face", "panel", "details", "setup", "settime", "touchcheck", "phone", "drawer", "flashlight"};
  static const char *sliding[] = {"boot+slide",  "face+slide",    "panel+slide",      "details+slide",
                                  "setup+slide", "settime+slide", "touchcheck+slide", "phone+slide", "drawer+slide", "flashlight+slide"};
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
  fading = irisWaking = sleeping = false;
  standby = power::Controller{};
  touchResting = wakeGuard = false;
  settingsPage = detailsPage = setupPage = setTimePage = drawerPage = 0;
  timer = TimerState{};
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
    hal::backlight(backlightLevel());
  } else if (mode == Mode::Face) {
    renderFace(*cur, currentFace(), false);
    present(*cur);
    dirty = false;
    hal::backlight(backlightLevel());
  } else {
    screens::drawBoot(*cur, 0, "v" FW_VERSION);
    present(*cur);
    fading = true;
    fadeStart = hal::ms();
  }
  hal::log("boot: done, %s", sequence ? "startup sequence" : "face");
}

void stepTimer() {
  if (timer.active && (int32_t)(hal::ms() - timer.targetMs) >= 0) {
    timer.active = false;
    timer.alarming = true;
    timer.alarmStartMs = hal::ms();
    if (!awake) wakeScreen();
    lastActivity = hal::ms();
  }
  if (timer.alarming) {
    if (since(timer.alarmStartMs) > 30000) {
      timer.alarming = false;
      hal::backlight(backlightLevel());
      if (mode == Mode::Drawer) mode = Mode::Face;
      dirty = true;
      return;
    }
    lastActivity = hal::ms();
    if (since(lastPresent) >= 200) {
      bool on = ((hal::ms() - timer.alarmStartMs) / 200) % 2 == 0;
      hal::backlight(on ? 255 : 40);
      cur->fillSprite(on ? col::WARN : col::BG);
      const GFXfont *f = gfx::font(type::Role::Title, true);
      gfx::text(*cur, "TIMER", CX, 90, f, on ? col::BG : col::WARN, MC_DATUM);
      gfx::text(*cur, "UP!", CX, 130, f, on ? col::BG : col::WARN, MC_DATUM);
      const GFXfont *fb = gfx::font(type::Role::Body, false);
      gfx::text(*cur, "Tap to dismiss", CX, 175, fb, on ? col::BG : col::TEXT, MC_DATUM);
      present(*cur);
    }
  }
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
  stepTimer();
  if (awake ? since(lastBatteryRead) > BATTERY_READ_MS : since(lastBatteryRead) >= BATTERY_READ_ASLEEP_MS) readBattery();
  if (settingsDirty && since(settingsChangedAt) > SAVE_DELAY_MS) {
    saveSettings();
    settingsDirty = false;
  }

  if (!awake) {
    standbyStep();  // the panel is asleep; only touch, BOOT, motion, Wi-Fi and the clock need attention
    return;
  }

  bool held = finger.down || boot.down || setupHoldsScreen() || tr.kind != Slide::None || mode == Mode::Boot || timer.alarming || mode == Mode::Flashlight;
  if (held) lastActivity = hal::ms();
  if (!sleeping && since(lastActivity) > screenTimeout()) {
    startSleep();
    if (!awake) return;
  }
  if (sleeping) {
    stepSleep();
    if (awake) hal::idle(1);
    return;
  }

  stepFade();
  stepBootCheck();
  stepDrop();
  if (!awake || sleeping) return;
  if (tr.kind != Slide::None) stepTransition();
  else if (irisWaking) stepIris();
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

  bool busy = tr.kind != Slide::None || fading || irisWaking || sleeping || faceMoving || entrance.playing ||
              marker.moving || mode == Mode::Boot || finger.down || boot.down || releaseUntil;
  hal::idle(busy ? 1 : 10);
}

}  // namespace ui
