#include "ui.h"
#include <utility>
#include "board.h"
#include "faces.h"
#include "gfx.h"
#include "hal.h"
#include "net.h"
#include "screens.h"
#include "settings.h"
#include "theme.h"
#include "touch.h"

namespace {
constexpr uint32_t SCREEN_TIMEOUT_MS = 15000;
constexpr uint32_t RESYNC_MS = 6UL * 60 * 60 * 1000;   // every 6 hours
constexpr uint32_t RETRY_SYNC_MS = 30UL * 60 * 1000;   // after a failed sync
constexpr uint32_t BATTERY_READ_MS = 10000;
constexpr uint32_t SAVE_DELAY_MS = 2000;               // batch settings writes to spare the flash
constexpr uint32_t ANIM_FRAME_MS = 16;                 // frame cap while animating; the push is the real limit
constexpr uint32_t SPINNER_FRAME_MS = 40;
constexpr uint32_t BOOT_HOLD_MS = 250;                 // name stays up briefly after the sequence
constexpr uint32_t HOLD_HINT_MS = 200;                 // a still press starts showing the hold ring
constexpr uint32_t FLICK_WINDOW_MS = 80;               // finger must still be moving this recently to flick
constexpr int SWIPE_PX = 40;
constexpr int W = 240, H = 240;

enum class Mode : uint8_t { Boot, Face, Panel, Setup };

TFT_eSPI tft;
TFT_eSprite spriteA(&tft), spriteB(&tft), cache(&tft);
// `cur` is what is on screen; `nb` is the incoming screen during a transition.
// They swap when a transition finishes, so nothing is redrawn twice.
TFT_eSprite *cur = &spriteA, *nb = &spriteB;
bool haveSpare = false, haveCache = false;

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
bool sweeping = false;
uint32_t sweepStart = 0;

int batteryPct = -1, batteryMv = -1;
uint32_t lastBatteryRead = 0;
uint32_t nextSyncAt = 0;
net::Sync seenSync = net::Sync::Idle;
net::Portal seenPortal = net::Portal::Off;
uint32_t uiKey = 0;
screens::Button pressed = screens::Button::None;

// One finger's progress from touch-down to release.
enum class Axis : uint8_t { None, Horizontal, Vertical };
struct Finger {
  bool down = false;
  bool ignore = false;    // started by waking the screen, or already used up
  Axis axis = Axis::None;
  bool longFired = false;
  int16_t x0 = 0, y0 = 0, x = 0, y = 0;
  uint32_t t0 = 0, lastMoveAt = 0, lastSampleAt = 0;
  float vx = 0, vy = 0;   // smoothed velocity, px per ms
} finger;

// The one transition that can be running. A new touch takes over a face
// slide where it is, so swipes never queue up.
enum class Slide : uint8_t { None, Face, PanelOpen, PanelClose };
struct Transition {
  Slide kind = Slide::None;
  bool dragging = false, animating = false;
  float pos = 0, from = 0, to = 0, grabPos = 0;
  int shownPos = -9999;    // last offset pushed to the panel
  uint32_t start = 0, duration = 0;
  int neighbour = -1;      // face drawn in `nb`
} tr;

#if DRIFT_DIAG
struct {
  uint32_t frames = 0, renderUs = 0, pushUs = 0, worstUs = 0, lastLog = 0;
  char line[48] = "";
} diag;
#endif

// ---- Small helpers ---------------------------------------------------------------
bool motionOn() { return !settings.reducedMotion; }
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
  offset = constrain(offset, 0, W);
  if (offset < W) a.pushSprite(0, 0, offset, 0, W - offset, H);
  if (offset > 0) b.pushSprite(W - offset, 0, 0, 0, offset, H);
  lastPresent = hal::ms();
}

// Shows `top` above `bottom`, shifted so `offset` rows of `top` are off the top edge.
void presentVertical(TFT_eSprite &top, TFT_eSprite &bottom, int offset) {
  offset = constrain(offset, 0, H);
  if (offset < H) top.pushSprite(0, 0, 0, offset, W, H - offset);
  if (offset > 0) bottom.pushSprite(0, H - offset, 0, 0, W, offset);
  lastPresent = hal::ms();
}

int percentFromMilliVolts(int mv) {
  static const int volts[] = {3300, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200};
  static const int pct[] = {0, 5, 15, 30, 50, 65, 80, 90, 100};
  if (mv < 2500) return -1;  // nothing plausible on the pin
  if (mv <= volts[0]) return 0;
  for (int i = 1; i < 9; i++)
    if (mv < volts[i]) return pct[i - 1] + (pct[i] - pct[i - 1]) * (mv - volts[i - 1]) / (volts[i] - volts[i - 1]);
  return 100;
}

void readBattery() {
  batteryMv = hal::batteryMilliVolts();
  batteryPct = percentFromMilliVolts(batteryMv);
  lastBatteryRead = hal::ms();
}

bool syncBusy() {
  net::Sync s = net::syncState();
  return s == net::Sync::Connecting || s == net::Sync::Zone || s == net::Sync::Clock;
}

FaceContext faceContext(bool withSweep) {
  FaceContext c{};
  time_t t = hal::now();
  localtime_r(&t, &c.now);
  c.timeValid = net::timeIsValid();
  c.use12h = settings.use12h;
  c.batteryPct = batteryPct;
  if (syncBusy()) c.sync = SyncView::Syncing;
  else if (net::syncState() == net::Sync::Failed) c.sync = SyncView::Failed;
  else c.sync = net::lastSync() ? SyncView::Synced : SyncView::Never;
  c.sweep = withSweep && sweeping ? min(1.0f, since(sweepStart) / (float)motion::SWEEP_MS) : -1;
  return c;
}

// Draws a face into `dst`. The current face reuses its cached static layer.
void renderFace(TFT_eSprite &dst, Face f, bool withSweep) {
#if DRIFT_DIAG
  uint32_t t = micros();
#endif
  FaceContext c = faceContext(withSweep);
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

screens::PanelView panelView() {
  static char synced[8];
  lastSyncText(synced, sizeof synced);
  screens::PanelView v{};
  v.batteryPct = batteryPct;
  v.batteryMv = batteryMv;
  v.brightnessLevel = settings.brightnessLevel % BRIGHTNESS_LEVEL_COUNT;
  v.use12h = settings.use12h;
  v.reducedMotion = settings.reducedMotion;
  v.timeValid = net::timeIsValid();
  v.sync = net::syncState();
  v.fail = net::failReason();
  v.syncStep = net::syncStep();
  v.lastSyncText = synced;
  v.zone = net::zoneName();
  v.ssid = net::ssid();
#if DRIFT_DIAG
  v.diag = diag.line;
#else
  v.diag = nullptr;
#endif
  v.pressed = pressed;
  v.ms = hal::ms();
  return v;
}

screens::SetupView setupView() {
  static char synced[8];
  lastSyncText(synced, sizeof synced);
  screens::SetupView v{};
  v.portal = net::portalState();
  v.sync = net::syncState();
  v.fail = net::failReason();
  v.syncStep = net::syncStep();
  v.secondsLeft = net::portalSecondsLeft();
  v.lastSyncText = synced;
  v.pressed = pressed;
  v.ms = hal::ms();
  v.reducedMotion = settings.reducedMotion;
  return v;
}

// Everything on the panel or setup screen except the spinner, so a redraw is
// only pushed when something the user can read has changed.
uint32_t screenKey() {
  uint32_t k = (uint32_t)mode;
  k = k * 31 + (uint32_t)net::syncState();
  k = k * 31 + (uint32_t)net::portalState();
  k = k * 31 + (uint32_t)net::failReason();
  k = k * 31 + (uint32_t)net::syncStep();
  k = k * 31 + (uint32_t)pressed;
  k = k * 31 + settings.brightnessLevel * 4 + settings.use12h * 2 + settings.reducedMotion;
  k = k * 131 + (uint32_t)(batteryPct + 1);
  k = k * 1031 + net::portalSecondsLeft();
  k = k * 31 + (uint32_t)net::lastSync();
  return k;
}

bool spinnerShowing() {
  if (!motionOn()) return false;
  if (syncBusy()) return true;
  net::Portal p = net::portalState();
  return mode == Mode::Setup && p == net::Portal::Waiting;
}

void drawScreen(TFT_eSprite &s) {
  if (mode == Mode::Setup) screens::drawSetup(s, setupView());
  else screens::drawPanel(s, panelView());
}

void refreshScreen(bool force) {
  uint32_t k = screenKey();
  bool spin = spinnerShowing() && since(lastPresent) >= SPINNER_FRAME_MS;
  if (!force && k == uiKey && !spin) return;
  uiKey = k;
  drawScreen(*cur);
  present(*cur);
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

// Makes sure `nb` holds the face on the side the current one is moving away from.
void prepareNeighbour(float pos) {
  int want = pos < 0 ? faceAt(+1) : faceAt(-1);
  if (tr.neighbour == want) return;
  renderFace(*nb, (Face)want, false);
  tr.neighbour = want;
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
  if (currentFace() == FACE_RADAR && motionOn()) {
    sweeping = true;
    sweepStart = hal::ms();
  } else {
    sweeping = false;
  }
}

// Mid-drag arrival on the neighbour: it becomes current and the drag continues.
void landOn(int step) {
  if (tr.neighbour != faceAt(step)) renderFace(*nb, faceAt(step), false);
  settings.face = faceAt(step);
  markSettingsChanged();
  std::swap(cur, nb);
  tr.neighbour = faceAt(-step);  // the face just left is now the neighbour
  cacheValid = false;
  sweeping = false;
}

// Instant face change, used with reduced motion or without the spare buffer.
void cutToFace(int step) {
  settings.face = faceAt(step);
  markSettingsChanged();
  cacheValid = false;
  dirty = true;
  sweeping = false;
  tr = Transition{};
}

void openPanel() {
  pressed = screens::Button::None;
  if (!haveSpare || !motionOn()) {
    mode = Mode::Panel;
    refreshScreen(true);
    return;
  }
  if (mode == Mode::Face) renderFace(*cur, currentFace(), false);  // without the hold ring
  Mode keep = mode;
  mode = Mode::Panel;
  screens::drawPanel(*nb, panelView());
  mode = keep;
  tr = Transition{};
  tr.kind = Slide::PanelOpen;
  tr.pos = 0;
  animateTo(H, motion::PANEL_MS, H);
}

void closeToFace() {
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
  animateTo(0, motion::PANEL_MS, H);
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
      case Slide::Face: showFaceSlide(); break;
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
      break;
    case Slide::PanelOpen:
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
    default:
      break;
  }
}

// ---- Sleep and wake -----------------------------------------------------------------
void sleepScreen() {
  if (!awake) return;
  hal::backlight(0);
  tft.writecommand(0x10);  // panel sleep in
  awake = false;
  fading = false;
  pressed = screens::Button::None;
  tr = Transition{};
  sweeping = false;
  // The screen always wakes on the face, so a stale panel is never the first thing seen.
  if (mode == Mode::Panel) mode = Mode::Face;
  if (settingsDirty) {
    saveSettings();
    settingsDirty = false;
  }
}

void wakeScreen() {
  if (awake) return;
  tft.writecommand(0x11);  // panel sleep out; the panel needs 5 ms before the next command
  hal::idle(5);
  awake = true;
  lastActivity = hal::ms();
  // Draw the current time first, then fade the light up over it.
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
void goBack() {
  pressed = screens::Button::None;
  if (mode == Mode::Setup) {
    net::cancelPortal();
    mode = Mode::Panel;
    refreshScreen(true);
  } else if (mode == Mode::Panel) {
    closeToFace();
  }
}

void activate(screens::Button b) {
  using screens::Button;
  switch (b) {
    case Button::Light:
      settings.brightnessLevel = (settings.brightnessLevel + 1) % BRIGHTNESS_LEVEL_COUNT;
      hal::backlight(backlightLevel());
      markSettingsChanged();
      break;
    case Button::Clock:
      settings.use12h = !settings.use12h;
      cacheValid = false;
      markSettingsChanged();
      break;
    case Button::Motion:
      settings.reducedMotion = !settings.reducedMotion;
      markSettingsChanged();
      break;
    case Button::Sync:
      net::startSync();
      break;
    case Button::WifiSetup:
    case Button::Retry:
      net::startPortal();
      mode = Mode::Setup;
      break;
    case Button::Cancel:
      goBack();
      return;
    case Button::Close:
    case Button::Done:
      mode = Mode::Face;
      dirty = true;
      return;
    default:
      break;
  }
  refreshScreen(true);
}

screens::Button hitTest(int x, int y) {
  if (mode == Mode::Panel) return screens::hitPanel(x, y);
  if (mode == Mode::Setup) return screens::hitSetup(setupView(), x, y);
  return screens::Button::None;
}

// ---- Touch ------------------------------------------------------------------------------
void onFingerDown(const TouchSample &s) {
  finger = Finger{};
  finger.down = true;
  finger.x0 = finger.x = s.x;
  finger.y0 = finger.y = s.y;
  finger.t0 = finger.lastMoveAt = finger.lastSampleAt = hal::ms();
  lastActivity = hal::ms();

  if (!awake) {
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
    finger.ignore = true;  // panel slides are short; let them finish
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
  }

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
    animateTo(target, motion::FACE_MS, W);
    return;
  }
  if (finger.axis == Axis::Horizontal && mode == Mode::Face) {
    // Reduced motion, or no spare buffer: change on release.
    if (dx <= -SWIPE_PX || vx < -motion::FLICK_PX_PER_MS) cutToFace(+1);
    else if (dx >= SWIPE_PX || vx > motion::FLICK_PX_PER_MS) cutToFace(-1);
    return;
  }
  if (finger.axis == Axis::Vertical) {
    bool up = dy <= -SWIPE_PX || vy < -motion::FLICK_PX_PER_MS;
    bool down = dy >= SWIPE_PX || vy > motion::FLICK_PX_PER_MS;
    if (mode == Mode::Face && up) openPanel();
    else if ((mode == Mode::Panel || mode == Mode::Setup) && down) goBack();
    return;
  }
  if (finger.axis == Axis::None && pressed != screens::Button::None) {
    screens::Button b = pressed;
    pressed = screens::Button::None;
    if (hitTest(finger.x, finger.y) == b) activate(b);
    else refreshScreen(true);
  }
}

// Returns true while the hold ring is on screen, so the face does not paint over it.
bool stepHold() {
  if (!finger.down || finger.ignore || finger.axis != Axis::None) return false;
  if (mode != Mode::Face || tr.kind != Slide::None) return false;
  uint32_t held = since(finger.t0);
  if (held >= motion::LONG_PRESS_MS) {
    finger.longFired = true;
    finger.ignore = true;  // the rest of this touch does nothing
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

void pollInput() {
  TouchSample s{};
  touch::read(s);
  if (s.down && !finger.down) onFingerDown(s);
  else if (s.down) onFingerMove(s);
  else if (finger.down) onFingerUp();

  static bool bootWas = false;
  bool boot = hal::bootButtonDown();
  bool bootPressed = boot && !bootWas;
  bootWas = boot;
  if (!bootPressed) return;
  lastActivity = hal::ms();
  if (!awake) {
    wakeScreen();
    return;
  }
  if (mode == Mode::Boot) bootStart = hal::ms() - motion::BOOT_MS - BOOT_HOLD_MS;
  else if (mode == Mode::Face && tr.kind == Slide::None) {
    if (haveSpare && motionOn()) {
      tr = Transition{};
      tr.kind = Slide::Face;
      prepareNeighbour(-1);
      animateTo(-W, motion::FACE_MS, W);
    } else {
      cutToFace(+1);
    }
  } else if (tr.kind == Slide::None) {
    goBack();
  }
}

// ---- Per-mode drawing -----------------------------------------------------------------------
void stepBoot() {
  uint32_t t = since(bootStart);
  if (t >= motion::BOOT_MS + BOOT_HOLD_MS) {
    if (!net::hasSavedWifi()) {
      net::startPortal();
      mode = Mode::Setup;
      refreshScreen(true);
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
  if (c.now.tm_min != lastMinute) {
    if (lastMinute >= 0 && currentFace() == FACE_RADAR && motionOn()) {
      sweeping = true;
      sweepStart = hal::ms();
    }
    lastMinute = c.now.tm_min;
  }
  if (sweeping && since(sweepStart) >= motion::SWEEP_MS) {
    sweeping = false;
    dirty = true;  // final frame without the beam
  }
  bool due = dirty || c.now.tm_sec != shownSecond || (sweeping && since(lastPresent) >= ANIM_FRAME_MS * 2);
  if (!due) return;
  renderFace(*cur, currentFace(), true);
  present(*cur);
  shownSecond = c.now.tm_sec;
  dirty = false;
}

void stepNetwork() {
  net::poll();
  net::Sync s = net::syncState();
  if (s != seenSync) {
    if (s == net::Sync::Done) nextSyncAt = hal::ms() + RESYNC_MS;
    if (s == net::Sync::Failed) nextSyncAt = hal::ms() + RETRY_SYNC_MS;
    seenSync = s;
    dirty = true;
  }
  net::Portal p = net::portalState();
  if (p != seenPortal) {
    seenPortal = p;
    lastActivity = hal::ms();
  }
  // Resync quietly while the screen is off so it never interrupts anything.
  if (!awake && nextSyncAt && (int32_t)(hal::ms() - nextSyncAt) >= 0) {
    nextSyncAt = 0;
    net::startSync();
  }
}

bool setupHoldsScreen() {
  if (mode != Mode::Setup) return false;
  net::Portal p = net::portalState();
  return p == net::Portal::Waiting || p == net::Portal::PhoneJoined || syncBusy();
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
  diag.frames = diag.renderUs = diag.pushUs = diag.worstUs = 0;
  diag.lastLog = hal::ms();
}
#endif
}  // namespace

namespace ui {

TFT_eSPI &display() { return tft; }

const char *screenName() {
  static const char *names[] = {"boot", "face", "panel", "setup"};
  static const char *sliding[] = {"boot+slide", "face+slide", "panel+slide", "setup+slide"};
  if (!awake) return "asleep";
  return (tr.kind != Slide::None ? sliding : names)[(int)mode];
}

void begin() {
  hal::begin();
  loadSettings();
  if (settings.face >= FACE_COUNT) settings.face = 0;
  settings.brightnessLevel %= BRIGHTNESS_LEVEL_COUNT;

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  // Three full 16-bit frames (115 KB each) in PSRAM: what is on screen, the
  // incoming screen during a slide, and the cached static face layer. Without
  // PSRAM the watch still works, with cuts instead of slides.
  bool a = spriteA.createSprite(W, H) != nullptr;
  if (!a) {
    spriteA.setColorDepth(8);
    spriteA.createSprite(W, H);
  } else {
    haveSpare = spriteB.createSprite(W, H) != nullptr;
    haveCache = cache.createSprite(W, H) != nullptr;
  }
  hal::log("ui: frame %s, spare %s, cache %s, free psram %lu", a ? "16-bit" : "8-bit", haveSpare ? "yes" : "no",
           haveCache ? "yes" : "no", (unsigned long)hal::freePsram());

  touch::begin();
  readBattery();
  net::begin();
  if (net::hasSavedWifi()) net::startSync();  // runs alongside the startup sequence

  bool sequence = hal::coldBoot() && motionOn();
  mode = sequence ? Mode::Boot : Mode::Face;
  if (!sequence && !net::hasSavedWifi()) {
    net::startPortal();
    mode = Mode::Setup;
  }
  bootStart = lastActivity = hal::ms();
  if (mode == Mode::Face) {
    renderFace(*cur, currentFace(), false);
    present(*cur);
    dirty = false;
  } else if (mode == Mode::Setup) {
    refreshScreen(true);
  } else {
    screens::drawBoot(*cur, 0, "v" FW_VERSION);
    present(*cur);
  }
  hal::backlight(backlightLevel());
}

void loop() {
  pollInput();
  stepNetwork();
  if (since(lastBatteryRead) > BATTERY_READ_MS) readBattery();
  if (settingsDirty && since(settingsChangedAt) > SAVE_DELAY_MS) {
    saveSettings();
    settingsDirty = false;
  }

  if (!awake) {
    hal::idle(50);  // the panel is asleep; only touch, BOOT and Wi-Fi need attention
    return;
  }

  bool held = finger.down || setupHoldsScreen() || tr.kind != Slide::None || mode == Mode::Boot;
  if (held) lastActivity = hal::ms();
  if (since(lastActivity) > SCREEN_TIMEOUT_MS) {
    sleepScreen();
    return;
  }

  stepFade();
  if (tr.kind != Slide::None) stepTransition();
  else if (mode == Mode::Boot) stepBoot();
  else if (mode == Mode::Face) {
    if (!stepHold()) stepFace();
  } else refreshScreen(false);
#if DRIFT_DIAG
  stepDiag();
#endif

  bool busy = tr.kind != Slide::None || fading || sweeping || mode == Mode::Boot || finger.down;
  hal::idle(busy ? 1 : 10);
}

}  // namespace ui
