// Screen navigation, control activation, phone sync, wrist gestures, touch, BOOT and USB serial.
#include "ui_parts.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ble.h"
#include "gfx.h"
#include "hal.h"
#include "imu.h"
#include "orient.h"
#include "phone.h"
#include "serial_cmd.h"
#include "settings.h"
#include "theme.h"

namespace uiparts {

// ---- Actions --------------------------------------------------------------------------
screens::SetupPhase setupPhase() {
  screens::View v{};
  v.portal = net::portalState();
  v.stage = net::syncState();
  v.result = net::lastResult();
  return screens::setupPhase(v);
}

void showMode(Mode m) {
  finishIris();
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
  char line[128], status[220];
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
void goBack(float v) {
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
    case Mode::Drawer: closeDrawer(v); break;
    case Mode::Flashlight: closeFlashlight(); break;
    case Mode::Panel: closeToFace(v); break;
    default: break;
  }
}

void startTimer(uint32_t ms) {
  timer.active = true;
  timer.alarming = false;
  timer.targetMs = hal::ms() + ms;
  timer.totalMs = ms;
  refreshScreen(true);
}

// Ends the alarm and repaints whatever screen is under it: the face, or a settings page whose
// key has not changed (the alarm picture used to stay there until the next swipe).
void dismissAlarm() {
  timer.alarming = false;
  hal::backlight(backlightLevel());
  if (mode == Mode::Drawer) mode = Mode::Face;
  dirty = true;
  uiKey = 0;
  lastActivity = hal::ms();
  if (mode != Mode::Face && mode != Mode::Boot && tr.kind == Slide::None) refreshScreen(true);
}

void resetTimer() {
  timer.active = false;
  timer.alarming = false;
  timer.targetMs = 0;
  timer.totalMs = 0;
  refreshScreen(true);
}

void activate(screens::Button b) {
  using screens::Button;
  switch (b) {
    case Button::Brightness:
      settings.brightnessLevel = (settings.brightnessLevel + 1) % BRIGHTNESS_LEVEL_COUNT;
      fading = false;
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
    case Button::HotSwap: {
      uint16_t mask = settings.activeFaces & ALL_FACES_MASK;
      if (mask != ALL_FACES_MASK) {
        settings.activeFaces = ALL_FACES_MASK;  // restore all faces
      } else {
        // default 2-face pair: current face + Orbit (or Digital if currently on Orbit)
        int pair = (settings.face % FACE_COUNT == FACE_ORBIT) ? FACE_DIGITAL : FACE_ORBIT;
        settings.activeFaces = (uint16_t)((1u << (settings.face % FACE_COUNT)) | (1u << pair));
      }
      saveSettings();
      markSettingsChanged();
      dirty = true;
      return;
    }
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
    case Button::Timer1m:
      startTimer(60 * 1000);
      return;
    case Button::Timer3m:
      startTimer(180 * 1000);
      return;
    case Button::Timer5m:
      startTimer(300 * 1000);
      return;
    case Button::TimerReset:
      resetTimer();
      return;
    case Button::Flashlight:
      openFlashlight();
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
  if (sleeping || !gesturesOn() || mode != Mode::Face || tr.kind != Slide::None || finger.down || boot.down) return;
  if (since(lastImuRead) < IMU_AWAKE_MS) return;
  lastImuRead = hal::ms();
  Accel a;
  if (!imu::read(a)) return;
  float g = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  if (wristDrop.update(hal::ms(), a.z * wristRaise.facing(), g)) {
    hal::log("ui: wrist down, screen off");
    startSleep();
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
  if (sleeping) {
    // Catch a dimming screen mid-fade and smoothly bloom back to full brightness.
    float elapsed = min(1.0f, since(sleepStart) / (float)motion::SLEEP_MS);
    sleeping = false;
    irisWaking = true;
    fading = true;
    uint32_t already = (uint32_t)((1.0f - elapsed) * motion::WAKE_MS);
    irisStart = fadeStart = hal::ms() - already;
    finger.ignore = true;
    return;
  }
  if (mode == Mode::Boot) {
    // A touch skips the startup sequence.
    bootStart = hal::ms() - motion::BOOT_MS - BOOT_HOLD_MS;
    finger.ignore = true;
    return;
  }
  if (timer.alarming) {
    dismissAlarm();
    finger.ignore = true;
    return;
  }
  if (mode == Mode::Flashlight) {
    closeFlashlight();
    finger.ignore = true;
    return;
  }
  finishIris();
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
      finishIris();
      tr = Transition{};
      tr.kind = Slide::Face;
      tr.dragging = true;
      dirty = true;  // clears any hold ring
    }
    if (mode == Mode::Face && finger.axis == Axis::Vertical && dy > 0 && haveSpare && motionOn()) startPanelDrag();
  }

  if (tr.kind == Slide::PanelOpen && tr.dragging) tr.pos = constrain((float)dy, 0.0f, (float)H);  // pinned to the finger

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
    // Slide down opens settings panel
    bool open = (dy >= SWIPE_PX || vy > motion::FLICK_PX_PER_MS) && vy >= -motion::FLICK_PX_PER_MS;
    settleTo(open ? H : 0, vy);
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
    if (mode == Mode::Face && up) openDrawer(vy ? -vy : 0.01f);
    else if (mode == Mode::Face && down) openPanel(vy ? vy : 0.01f);
    else if (mode == Mode::Drawer && down) closeDrawer(vy ? vy : -0.01f);
    else if (pagedMode() && up) goBack(vy ? -vy : -0.01f);
    return;
  }
  if (finger.axis == Axis::None) {
    if (pressed != screens::Button::None) {
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
    } else if (mode == Mode::Panel && finger.y < 42 && abs(dx) < 20 && abs(dy) < 20) {
      // Tap on Category Hub header: jump to next/prev category hub
      screens::Hub curHub = screens::hubOfPage(settingsPage, settings.largeText);
      screens::Hub target = finger.x < 110 ? screens::prevHub(curHub) : screens::nextHub(curHub);
      int targetPage = screens::hubFirstPage(target, settings.largeText);
      if (targetPage != settingsPage) {
        settingsPage = targetPage;
        refreshScreen(true);
      }
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
    if (timer.alarming) {
      dismissAlarm();
      return;
    }
    if (mode == Mode::Flashlight) {
      closeFlashlight();
      return;
    }
    if (sleeping) {
      float elapsed = min(1.0f, since(sleepStart) / (float)motion::SLEEP_MS);
      sleeping = false;
      irisWaking = true;
      fading = true;
      uint32_t already = (uint32_t)((1.0f - elapsed) * motion::WAKE_MS);
      irisStart = fadeStart = hal::ms() - already;
      return;
    }
    if (awake) cancelEntrance(true);
    if (!awake) wakeScreen();  // the waking press does nothing else
    else if (mode == Mode::Boot) bootStart = hal::ms() - motion::BOOT_MS - BOOT_HOLD_MS;
    else {
      finishIris();
      if (mode == Mode::Face) boot.used = tr.kind != Slide::None;  // decided on release or at the hold time
      else if (tr.kind == Slide::None) goBack();
    }
    return;
  }
  // Released before the hold time on a face: next face.
  if (boot.used || mode != Mode::Face || tr.kind != Slide::None) return;
  boot.used = true;
  finishIris();
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
  } else if (cmd.kind == serialcmd::Kind::Faces) {
    settings.activeFaces = (cmd.facesMask & ALL_FACES_MASK) ? (cmd.facesMask & ALL_FACES_MASK) : ALL_FACES_MASK;
    if (!(settings.activeFaces & (1u << currentFace()))) settings.face = nextActiveFace(currentFace(), +1);
    saveSettings();
    cacheValid = false;
    dirty = true;
    Serial.printf("Faces mask set to 0x%04X\n", (unsigned int)settings.activeFaces);
  } else if (cmd.kind == serialcmd::Kind::Info) {
    Serial.printf("DRIFT:VER=" FW_VERSION ",TIME=%ld,BAT=%d,FACES=0x%04X,FACE=%d,BRIGHT=%d,MOTION=%d,FORMAT=%d\n",
                  (long)hal::now(), batteryPct, (unsigned int)settings.activeFaces,
                  (int)settings.face, (int)settings.brightnessLevel, (int)settings.motion, settings.use12h ? 12 : 24);
  } else if (cmd.kind == serialcmd::Kind::Timer) {
    if (cmd.value <= 0) {
      resetTimer();
      Serial.printf("%s\n", "Timer reset");
    } else {
      startTimer((uint32_t)cmd.value * 1000u);
      Serial.printf("Timer started for %d s\n", cmd.value);
    }
  } else if (cmd.kind == serialcmd::Kind::Bright) {
    settings.brightnessLevel = (uint8_t)cmd.value;
    if (awake) hal::backlight(backlightLevel());
    saveSettings();
    dirty = true;
    Serial.printf("Brightness set to %d\n", cmd.value);
  } else if (cmd.kind == serialcmd::Kind::Motion) {
    settings.motion = (uint8_t)cmd.value;
    saveSettings();
    dirty = true;
    Serial.printf("Motion set to %d\n", cmd.value);
  } else if (cmd.kind == serialcmd::Kind::Format) {
    settings.use12h = (cmd.value == 12);
    saveSettings();
    cacheValid = false;
    dirty = true;
    Serial.printf("Format set to %dh\n", cmd.value);
  } else if (cmd.kind == serialcmd::Kind::Night) {
    settings.night = (uint8_t)cmd.value;
    saveSettings();
    dirty = true;
    Serial.printf("Night mode set to %d\n", cmd.value);
  } else if (cmd.kind == serialcmd::Kind::Face) {
    settings.face = (uint8_t)(cmd.value % FACE_COUNT);
    settings.activeFaces |= (1u << settings.face);
    saveSettings();
    cacheValid = false;
    dirty = true;
    Serial.printf("Active face set to %d\n", cmd.value);
  }
}

// The computer is talking: stay out of light sleep a while, so the rest of its lines arrive.
void holdForSerial() {
  serialHoldUntil = hal::ms() + SERIAL_HOLD_MS;
  if (!serialHoldUntil) serialHoldUntil = 1;
}

// Takes only the bytes already waiting, so a line sent in pieces never stalls the interface.
void pollSerial() {
  static serialcmd::LineBuffer lineIn;
  for (int n = 0; n < SERIAL_BYTES_PER_POLL && Serial.available() > 0; n++) {
    int c = Serial.read();
    if (c < 0) break;
    holdForSerial();
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


}  // namespace uiparts
