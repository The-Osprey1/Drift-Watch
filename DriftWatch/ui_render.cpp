// Frame presentation, face composition, screen View building and per-mode rendering.
#include "ui_parts.h"
#include <stdio.h>
#include <string.h>
#include "ble.h"
#include "gfx.h"
#include "hal.h"
#include "imu.h"
#include "orient.h"
#include "phone.h"
#include "settings.h"
#include "theme.h"

namespace uiparts {

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
  c.charging = batteryLife.charging();
  if (syncBusy()) c.sync = SyncView::Syncing;
  else if (net::lastResult() == net::Result::Failed) c.sync = SyncView::Failed;
  else c.sync = net::clockQuality() == clockstatus::Quality::Synced ? SyncView::Synced : SyncView::Never;
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
void renderFace(TFT_eSprite &dst, Face f, bool withSweep, bool entranceStart) {
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
  else if (timer.notice != timerstore::Notice::None && f == currentFace() && mode == Mode::Face)
    screens::drawTimerNotice(dst, timer.notice);
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

int &pageFor(Mode m) {
  return m == Mode::Details     ? detailsPage
         : m == Mode::Setup     ? setupPage
         : m == Mode::SetTime   ? setTimePage
         : m == Mode::PhoneSync ? phonePage
         : m == Mode::TouchCheck ? checkPage
         : m == Mode::Drawer    ? drawerPage
                                 : settingsPage;
}

// "3 h ago" style age of the last sync.
void syncAgoText(char *buf, size_t n) {
  time_t t = net::lastSync();
  if (!t) {
    buf[0] = 0;
    return;
  }
  int64_t ago = net::syncAgeSeconds();
  if (ago < 0) { snprintf(buf, n, "age unknown"); return; }
  if (ago < 120) snprintf(buf, n, "just now");
  else if (ago < 7200) snprintf(buf, n, "%lld min ago", (long long)(ago / 60));
  else if (ago < 172800) snprintf(buf, n, "%lld h ago", (long long)(ago / 3600));
  else snprintf(buf, n, "%lld days ago", (long long)(ago / 86400));
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
             : m == Mode::Drawer ? screens::Screen::Drawer
                                  : screens::Screen::Settings;
  v.page = page;
  v.large = settings.largeText;
  v.reducedMotion = !motionOn();
  v.motion = settings.motion;
  v.activeFaces = settings.activeFaces;
  v.theme = faceTheme(currentFace());
  v.timerActive = timer.active;
  v.timerRemainingMs = remainingTimerMs();
  v.timerTotalMs = timer.totalMs;
  v.timerNotice = timer.notice;
  v.timerStoreOk = timer.storeOk;
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
  v.clockQuality = net::clockQuality();
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
  v.tempC = -99.0f;
  v.chipTempC = -99.0f;
  if (m == Mode::Details && v.imuPresent) {
    imu::readTemp(v.tempC);
#if defined(ARDUINO)
    v.chipTempC = temperatureRead();
#else
    v.chipTempC = 36.0f;
#endif
  }
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
  return mode == Mode::Panel || mode == Mode::Details || mode == Mode::Setup || mode == Mode::SetTime || mode == Mode::PhoneSync || mode == Mode::Drawer;
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
  k = k * 31 + (settings.activeFaces & ALL_FACES_MASK);
  k = k * 31 + (uint32_t)currentFace();
  k = k * 131 + (uint32_t)(batteryPct + 1);
  k = k * 1031 + net::portalSecondsLeft();
  k = k * 31 + (uint32_t)net::lastSync();
  k = k * 31 + (uint32_t)net::joinWhy() * 2 + net::clockSetByHand();
  k = k * 31 + settings.raiseToWake * 2 + settings.twistToWake;
  // The clock shown on the Set time control: only its page is redrawn when the minute turns.
  bool clockShown = mode != Mode::Panel || settingsPage == screens::pageOf(*cur, view(Mode::Panel, 0), screens::Button::SetTime);
  if (clockShown) k = k * 61 + (uint32_t)(hal::now() / 60);
  k = k * 31 + (uint32_t)(editTime.tm_year * 400 + editTime.tm_mon * 32 + editTime.tm_mday);
  k = k * 31 + (uint32_t)(editTime.tm_hour * 60 + editTime.tm_min);
  k = k * 31 + settings.screenTurn * 8 + settings.night * 2 + touchChecked();
  k = k * 11 + (uint32_t)timer.notice * 2 + timer.storeOk;
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
  if (mode == Mode::Drawer && timer.active) {
    k = k * 61 + remainingTimerMs() / 1000;
  }
  return k;
}

void drawScreen(TFT_eSprite &s, Mode m, int page) {
  if (m == Mode::Flashlight) {
    s.fillSprite(0xFFFF);
    uiAnimated = false;
    return;
  }
  uiAnimated = screens::draw(s, view(m, page));
}

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
      if (motionOn() && haveSpare) {
        renderFace(*nb, currentFace(), false);
        shownSecond = faceContext(false).now.tm_sec;
        dirty = false;
        irisWaking = true;
        irisStart = hal::ms();
        gfx::irisTransition(*cur, *nb, 0.06f, faceTheme(currentFace()).primary, true);
        present(*cur);
      } else {
        dirty = true;
      }
    }
    return;
  }
  if (since(lastPresent) < ANIM_FRAME_MS) return;
  constexpr uint32_t OUTRO_MS = 140;
  uint32_t outroStart = motion::BOOT_MS + BOOT_HOLD_MS - OUTRO_MS;
  if (t > outroStart && haveSpare && motionOn() && touchChecked()) {
    float p = 1.0f - (t - outroStart) / (float)OUTRO_MS;
    screens::drawBoot(*nb, 1.0f, "v" FW_VERSION);
    gfx::irisTransition(*cur, *nb, motion::easeInOut(p), col::CYAN, false);
  } else {
    screens::drawBoot(*cur, min(1.0f, t / (float)motion::BOOT_MS), "v" FW_VERSION);
  }
  present(*cur);
}

void stepFace() {
  FaceContext c = faceContext(false);
  lastMinute = c.now.tm_min;
  // The radar beam and Phantom's movement run all the time their face is on screen (and
  // Expressive adds ambient touches on Digital, Orbit, Lunar and Yozakura), unless motion is
  // off, the battery is low, or memory is short and there is no cached dial to draw over (then
  // they step with the seconds, drawn once a second). With the screen off or another screen up
  // this is not called at all.
  bool moving = faceAnimates(currentFace(), settings.motion == MOTION_EXPRESSIVE) && motionOn() && !lowPower() && haveCache;
  if (moving != faceMoving) {
    faceMoving = moving;
    sweepStart = hal::ms();
    dirty = true;
  }
  uint32_t frameMs = currentFace() == FACE_RADAR ? motion::RADAR_FRAME_MS
                   : currentFace() == FACE_PHANTOM ? motion::PHANTOM_FRAME_MS
                   : motion::AMBIENT_FRAME_MS;

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

}  // namespace uiparts
