// Entrances, face and panel slide transitions, radial iris wake/sleep and screen-off waits.
#include "ui_parts.h"
#include <math.h>
#include <string.h>
#include <utility>
#include "ble.h"
#include "gfx.h"
#include "hal.h"
#include "settings.h"
#include "theme.h"

namespace uiparts {

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
void changePage(int dir, float v) {
  int &page = pageFor(mode);
  int to = page + dir;
  if (to < 0 || to >= pageCount()) return;  // no wrap-around, and never a face change
  finishIris();
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

void openPanel(float v) {
  finishIris();
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
  finishIris();
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

void closeToFace(float v) {
  finishIris();
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

void openDrawer(float v) {
  finishIris();
  cancelEntrance(false);
  pressed = screens::Button::None;
  drawerPage = 0;
  if (!haveSpare || !motionOn()) {
    mode = Mode::Drawer;
    refreshScreen(true);
    return;
  }
  if (mode == Mode::Face) renderFace(*cur, currentFace(), false);
  drawScreen(*nb, Mode::Drawer, drawerPage);
  tr = Transition{};
  tr.kind = Slide::DrawerOpen;
  tr.pos = H;
  if (v) settleTo(0, -v);
  else animateTo(0, motion::PANEL_MS, H);
}

void closeDrawer(float v) {
  finishIris();
  pressed = screens::Button::None;
  if (!haveSpare || !motionOn()) {
    mode = Mode::Face;
    dirty = true;
    return;
  }
  renderFace(*nb, currentFace(), false);
  drawScreen(*cur, Mode::Drawer, drawerPage);
  tr = Transition{};
  tr.kind = Slide::DrawerClose;
  tr.pos = 0;
  if (v) settleTo(H, v);
  else animateTo(H, motion::PANEL_MS, H);
}

void openFlashlight() {
  finishIris();
  mode = Mode::Flashlight;
  flashlightStart = hal::ms();
  hal::backlight(255);
  drawScreen(*cur, Mode::Flashlight, 0);
  present(*cur);
}

void closeFlashlight() {
  mode = Mode::Face;
  hal::backlight(backlightLevel());
  dirty = true;
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
      case Slide::DrawerOpen: presentVertical(*nb, *cur, (int)tr.pos); break;
      case Slide::DrawerClose: presentVertical(*cur, *nb, (int)tr.pos); break;
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
    case Slide::DrawerOpen:
      if (tr.pos >= H) {
        tr = Transition{};
        dirty = true;
        break;
      }
      std::swap(cur, nb);
      tr = Transition{};
      mode = Mode::Drawer;
      uiKey = screenKey();
      break;
    case Slide::DrawerClose:
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

void finishIris() {
  if (!irisWaking && !sleeping) return;
  if (haveSpare) memcpy(cur->getPointer(), nb->getPointer(), W * H * 2);
  irisWaking = false;
  if (sleeping) {
    sleeping = false;
    hal::backlight(backlightLevel());
  }
  dirty = true;
}

void sleepScreen() {
  if (!awake) return;
  hal::backlight(0);
  tft.writecommand(0x10);  // panel sleep in
  awake = false;
  fading = false;
  irisWaking = false;
  sleeping = false;
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
    markSettingsChanged();
  }
  if (settingsDirty && settingsStoreWritable() &&
      (!settingsRetryAt || (int32_t)(hal::ms() - settingsRetryAt) >= 0)) flushSettings();
  standby.screenOff(hal::ms());
  touchRest();
}

void startSleep() {
  if (!awake || sleeping) return;
  if (!motionOn() || lowPower() || !haveSpare || tr.kind != Slide::None) {
    sleepScreen();
    return;
  }
  if (irisWaking) {
    irisWaking = false;
  } else if (mode == Mode::Face) {
    renderFace(*nb, currentFace(), false);
  } else {
    memcpy(nb->getPointer(), cur->getPointer(), W * H * 2);
  }
  fading = false;
  sleeping = true;
  sleepStart = hal::ms();
}

void wakeScreen() {
  if (awake) return;
  touchResume();
  standby.screenOn(hal::ms());
  tft.writecommand(0x11);  // panel sleep out; the panel needs 5 ms before the next command
  hal::idle(5);
  awake = true;
  sleeping = false;
  lastActivity = hal::ms();
  wristDrop.reset();
  showLowNotice();
  // Render the clean target into `nb` and bloom outward with a smooth iris + backlight ramp.
  if (motionOn() && !lowPower() && haveSpare) {
    if (mode == Mode::Face) {
      renderFace(*nb, currentFace(), false);
      shownSecond = faceContext(false).now.tm_sec;
      dirty = false;
    } else {
      drawScreen(*nb, mode, pageFor(mode));
      uiKey = screenKey();
    }
    irisWaking = true;
    fading = true;
    irisStart = fadeStart = hal::ms();
    gfx::irisTransition(*cur, *nb, 0.04f, faceTheme(currentFace()).primary, true);
    present(*cur);
  } else {
    irisWaking = false;
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
}

void stepFade() {
  if (!fading) return;
  float t = since(fadeStart) / (float)motion::WAKE_MS;
  if (t >= 1.0f) {
    hal::backlight(backlightLevel());
    fading = false;
    return;
  }
  float s = motion::easeInOut(t);
  hal::backlight((uint8_t)(backlightLevel() * s * (0.35f + 0.65f * s)));
}

void stepIris() {
  if (!irisWaking) return;
  float t = since(irisStart) / (float)motion::WAKE_MS;
  if (t >= 1.0f) {
    irisWaking = false;
    memcpy(cur->getPointer(), nb->getPointer(), W * H * 2);
    present(*cur);
    if (mode == Mode::Face) dirty = true;
    return;
  }
  if (since(lastPresent) >= ANIM_FRAME_MS) {
    gfx::irisTransition(*cur, *nb, motion::easeOut(t), faceTheme(currentFace()).primary, true);
    present(*cur);
  }
}

void stepSleep() {
  if (!sleeping) return;
  float t = since(sleepStart) / (float)motion::SLEEP_MS;
  if (t >= 1.0f) {
    sleeping = false;
    sleepScreen();
    return;
  }
  float p = 1.0f - motion::easeInOut(t);
  hal::backlight((uint8_t)(backlightLevel() * p * (0.35f + 0.65f * p) + 0.5f));
  if (since(lastPresent) >= ANIM_FRAME_MS) {
    gfx::irisTransition(*cur, *nb, p, faceTheme(currentFace()).primary, false);
    present(*cur);
  }
}


// ---- Screen-off waits -------------------------------------------------------------------------
power::Inputs standbyInputs() {
  power::Inputs in;
  bool on = gesturesOn();  // includes night mode and the sensor being there
  in.raise = on && settings.raiseToWake;
  in.twist = on && settings.twistToWake;
  in.serialHold = serialHoldUntil && (int32_t)(hal::ms() - serialHoldUntil) < 0;
  if (!in.serialHold) serialHoldUntil = 0;
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
  if (timer.active) take(timer.targetMs);
  if (nextSyncAt) take(nextSyncAt);
  if (settingsDirty && settingsStoreWritable())
    take(settingsRetryAt ? settingsRetryAt : settingsChangedAt + SAVE_DELAY_MS + 1);
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
    case power::Wake::Serial:
      holdForSerial();  // the bytes that woke it are lost; the computer sends again (screen stays off)
      break;
    case power::Wake::None:
      break;
  }
}


}  // namespace uiparts
