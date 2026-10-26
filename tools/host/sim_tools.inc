// Quick Drawer tools, timers and their alarm, the flashlight, the settings hubs and the serial
// commands, driven through the real interface code. Started as the v0.15.0 feature test; each
// fault it found (alarm hidden on 7 face and motion combinations, the alarm picture left on
// Settings, the drawer redrawing at 25 fps, a still progress ring, the flashlight never turning
// off, two settings under the wrong heading, serial lines lost in light sleep) has a check here.
// Included into simulate.cpp; P() is CHECK() with a tag, counted the same way.
namespace {
int P(const char *tag, bool ok, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int P(const char *tag, bool ok, const char *fmt, ...) {
  checks++;
  if (!ok) failures++;
  printf("  %s %s: ", ok ? "ok  " : "FAIL", tag);
  va_list a;
  va_start(a, fmt);
  vprintf(fmt, a);
  va_end(a);
  printf("\n");
  return ok;
}
int warnPx() {
  int n = 0;
  for (uint16_t px : ui::display().buf) n += px == col::WARN;
  return n;
}
int whitePx() {
  int n = 0;
  for (uint16_t px : ui::display().buf) n += px == 0xFFFF;
  return n;
}
const char *FN[] = {"Digital", "Stealth", "Corpo", "Hologram", "Radar", "Phantom",
                    "Orbit", "Lunar", "Yozakura", "Hanami", "Wasteland", "Mojave"};

// Runs `ms` of alarm, sampling every 20 ms: how many samples showed the full alarm picture, and
// how many backlight changes between 255 and 40 happened.
void alarmSeen(uint32_t ms, int &shown, int &samples, int &toggles) {
  size_t log0 = sim::backlightLog.size();
  shown = samples = 0;
  for (uint32_t t = 0; t < ms; t += 20) {
    runFor(20);
    samples++;
    if (warnPx() > 20000) shown++;
  }
  toggles = 0;
  for (size_t i = log0; i < sim::backlightLog.size(); i++) {
    uint8_t l = sim::backlightLog[i].second;
    if (l == 255 || l == 40) toggles++;
  }
}

void extraDrawer() {
  printf("\n== Quick Drawer\n");
  int opened = 0;
  for (int f = 0; f < FACE_COUNT; f++) {
    startOn((Face)f);
    swipeDown();
    settle();
    bool ok = screen() == "drawer";
    opened += ok;
    if (!ok) printf("    swipe down on %s gave %s\n", FN[f], screen().c_str());
  }
  P("drawer", opened == FACE_COUNT, "swipe down opens the drawer on %d of 12 faces", opened);

  startOn(FACE_DIGITAL);
  swipeDown();
  settle();
  screens::Control c;
  bool p0 = find(screens::Button::Timer1m, c) && find(screens::Button::Flashlight, c);
  snap("x_drawer_p0");
  swipeLeft();
  settle();
  bool p1 = ui::page() == 1 && find(screens::Button::Timer3m, c) && find(screens::Button::Timer5m, c);
  snap("x_drawer_p1");
  P("drawer", p0 && p1, "standard text: page 1 = 1 min + Flashlight, page 2 = 3 min + 5 min");
  swipeLeft();
  settle();
  P("drawer", ui::page() == 1, "no third page in standard text (page %d)", ui::page() + 1);
  swipeUp();
  settle();
  P("drawer", screen() == "face", "swipe up closes it (%s)", screen().c_str());
  swipeDown();
  settle();
  pressBoot();
  settle();
  P("drawer", screen() == "face" && settings.face == FACE_DIGITAL, "BOOT closes it without changing face (%s, face %d)",
    screen().c_str(), settings.face);
  swipeDown();
  settle();
  runFor(16000);
  P("drawer", screen() == "asleep", "an idle drawer lets the screen sleep after 15 s (%s)", screen().c_str());

  // Large text: four single-tool pages, inside the round glass.
  startOn(FACE_DIGITAL);
  settings.largeText = true;
  swipeDown();
  settle();
  screens::Button want[4] = {screens::Button::Timer1m, screens::Button::Flashlight, screens::Button::Timer3m,
                             screens::Button::Timer5m};
  int good = 0, worst = 0;
  for (int pg = 0; pg < 4; pg++) {
    screens::Control ctl[2];
    int n = ui::controls(ctl, 2);
    if (n == 1 && ctl[0].id == want[pg] && ui::page() == pg) good++;
    for (int i = 0; i < n; i++) {
      const auto &r = ctl[i].visible;
      int xs[2] = {r.x, r.x + r.w}, ys[2] = {r.y, r.y + r.h};
      for (int xi : xs)
        for (int yi : ys) worst = max(worst, (int)lroundf(hypotf(xi - 120.f, yi - 120.f)));
    }
    char nm[32];
    snprintf(nm, sizeof nm, "x_drawer_large_p%d", pg);
    snap(nm);
    swipeLeft();
    settle();
  }
  P("drawer", good == 4, "large text: %d of 4 pages hold one tool each", good);
  printf("    large text: farthest drawn box corner %d px from centre (round glass is 120)\n", worst);
  settings.largeText = false;
}

void extraTimers() {
  printf("\n== Timers\n");
  screens::Button btn[3] = {screens::Button::Timer1m, screens::Button::Timer3m, screens::Button::Timer5m};
  uint32_t want[3] = {60000, 180000, 300000};
  for (int i = 0; i < 3; i++) {
    startOn(FACE_DIGITAL);
    swipeDown();
    settle();
    if (i) {
      swipeLeft();
      settle();
    }
    uint32_t t0 = sim::clockMs;
    bool tapped = tapButton(btn[i]);
    settle(100);
    bool on = ui::timerActive() && ui::timerTotalMs() == want[i];
    // Keep the screen awake and watch for the end.
    uint32_t firedAt = 0;
    while (sim::clockMs - t0 < want[i] + 3000) {
      runFor(50);
      if (!firedAt && ui::timerAlarming()) firedAt = sim::clockMs;
    }
    long late = firedAt ? (long)(firedAt - t0) - (long)want[i] : -1;
    P("timer", tapped && on && firedAt && late >= 0 && late < 400,
      "%u min: starts, and the alarm starts %ld ms after the set time ",
      want[i] / 60000, late);
  }

  // Countdown on screen.
  startOn(FACE_DIGITAL);
  swipeDown();
  settle();
  tapButton(screens::Button::Timer1m);
  settle(1000);
  snap("x_timer_59");
  uint64_t p0 = ui::display().pixelsPushed;
  runFor(3000);
  uint64_t frames = (ui::display().pixelsPushed - p0) / (240 * 240);
  P("timer", frames >= 3 && frames <= 4, "the drawer countdown redraws once a second (%llu frames in 3 s)",
    (unsigned long long)frames);
  screens::Control c;
  P("timer", find(screens::Button::TimerReset, c) && !find(screens::Button::Timer1m, c),
    "while running, the drawer shows only Reset Timer");
  tapButton(screens::Button::TimerReset);
  settle(100);
  P("timer", !ui::timerActive() && !ui::timerAlarming(), "Reset Timer stops it");
  runFor(70000);
  P("timer", !ui::timerAlarming(), "and no alarm comes later");
  // Is the ring a progress ring? Compare two frames 20 s apart, outside the digits.
  startOn(FACE_DIGITAL);
  swipeDown();
  settle();
  tapButton(screens::Button::Timer1m);
  settle(500);
  std::vector<uint16_t> a = ui::display().buf;
  snap("x_timer_a");
  runFor(10000);
  std::vector<uint16_t> b = ui::display().buf;
  snap("x_timer_b");
  int ringDiff = 0;
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 240; x++) {
      float r = hypotf(x - 120.f, y - 120.f);
      if (r > 100 && r < 116 && a[y * 240 + x] != b[y * 240 + x]) ringDiff++;
    }
  P("timer", ringDiff > 0, "the ring around the countdown shows progress (%d ring pixels changed in 10 s)", ringDiff);
  ui::resetTimer();

  // Alarm on every face, screen awake on the face.
  printf("  alarm shown on each face (2 s timer started while the face is showing):\n");
  int faceOk = 0;
  std::string bad;
  for (int lvl = MOTION_SUBTLE; lvl <= MOTION_EXPRESSIVE; lvl++) {
    for (int f = 0; f < FACE_COUNT; f++) {
      startOn((Face)f);
      settings.motion = lvl;
      runFor(100);
      ui::startTimer(2000);
      runFor(2100);
      int shown, samples, toggles;
      alarmSeen(2000, shown, samples, toggles);
      bool ok = shown >= samples / 3;
      if (!ok) {
        char t[80];
        snprintf(t, sizeof t, "%s%s (%s: alarm in %d of %d samples, %d backlight flips)", bad.empty() ? "" : "; ",
                 FN[f], lvl == MOTION_SUBTLE ? "Subtle" : "Expressive", shown, samples, toggles);
        bad += t;
      }
      faceOk += ok;
      if (f == FACE_DIGITAL && lvl == MOTION_SUBTLE) snap("x_alarm_digital");
      if (f == FACE_RADAR && lvl == MOTION_SUBTLE) snap("x_alarm_radar");
      ui::resetTimer();
    }
  }
  P("alarm", faceOk == 2 * FACE_COUNT, "alarm visible on %d of 24 face and motion combinations. Hidden on: %s", faceOk,
    bad.empty() ? "none" : bad.c_str());

  // Flash cadence on Digital.
  startOn(FACE_DIGITAL);
  ui::startTimer(1000);
  runFor(1100);
  size_t l0 = sim::backlightLog.size();
  runFor(2000);
  int hi = 0, lo = 0;
  for (size_t i = l0; i < sim::backlightLog.size(); i++) {
    hi += sim::backlightLog[i].second == 255;
    lo += sim::backlightLog[i].second == 40;
  }
  P("alarm", hi >= 4 && lo >= 4, "Digital: backlight pulses 255/40 every 200 ms (%d high, %d low in 2 s)", hi, lo);
  int f0 = settings.face;
  tap(120, 120);
  settle(300);
  P("alarm", !ui::timerAlarming() && settings.face == f0 && lastBacklight() == backlightLevel() && warnPx() < 2000,
    "a tap dismisses it, restores brightness %d (now %d), and does nothing else", backlightLevel(), lastBacklight());
  ui::startTimer(1000);
  runFor(1500);
  pressBoot();
  settle(300);
  P("alarm", !ui::timerAlarming() && settings.face == f0 && screen() == "face", "BOOT dismisses it without changing face");
  ui::startTimer(1000);
  runFor(1500);
  runFor(30000);
  P("alarm", !ui::timerAlarming() && lastBacklight() == backlightLevel(), "it stops by itself after 30 s");
  settle(300);
  P("alarm", warnPx() < 2000, "and the face comes back (%d alarm pixels left)", warnPx());

  // Alarm while in settings: does the screen come back after dismissing?
  startOn(FACE_DIGITAL);
  ui::startTimer(3000);
  swipeUp();
  settle(600);
  runFor(3000);
  int shown, samples, toggles;
  alarmSeen(1000, shown, samples, toggles);
  P("alarm", shown > 0, "alarm shows while Settings is open (%d of %d samples)", shown, samples);
  tap(120, 120);
  settle(500);
  P("alarm", warnPx() < 2000, "after dismissing in Settings, Settings is drawn again (%d alarm pixels left, %s)",
    warnPx(), screen().c_str());
  snap("x_alarm_settings_after_dismiss");
  runFor(5000);
  printf("    5 s later: %d alarm pixels still on screen (%s)\n", warnPx(), screen().c_str());
  swipeLeft();
  settle();
  printf("    after a page swipe: %d alarm pixels (%s)\n", warnPx(), screen().c_str());

  // Timer while the screen is off.
  startOn(FACE_DIGITAL);
  swipeDown();
  settle();
  tapButton(screens::Button::Timer1m);
  uint32_t t0 = sim::clockMs;
  settle(100);
  swipeUp();
  settle();
  runFor(20000);
  bool slept = screen() == "asleep";
  int dz0 = sim::dozes;
  uint32_t firedAt = 0;
  while (sim::clockMs - t0 < 62000) {
    runFor(20);
    if (!firedAt && ui::timerAlarming()) firedAt = sim::clockMs;
  }
  P("timer", slept && firedAt && firedAt - t0 < 60300 && screen() != "asleep",
    "screen off during a 1 min timer: wakes for the alarm %ld ms after the set time (%d screen-off waits)",
    firedAt ? (long)(firedAt - t0) - 60000 : -1, sim::dozes - dz0);
  ui::resetTimer();
}

void extraFlashlight() {
  printf("\n== Flashlight\n");
  startOn(FACE_DIGITAL);
  swipeDown();
  settle();
  tapButton(screens::Button::Flashlight);
  settle(100);
  P("flash", screen() == "flashlight" && lastBacklight() == 255 && whitePx() == 240 * 240,
    "turns the whole screen white (%d of 57600 pixels) at backlight %d", whitePx(), lastBacklight());
  tap(120, 120);
  settle(100);
  P("flash", screen() == "face" && lastBacklight() == backlightLevel() && settings.face == FACE_DIGITAL,
    "a tap returns to the face at brightness %d", lastBacklight());
  swipeDown();
  settle();
  tapButton(screens::Button::Flashlight);
  settle(100);
  pressBoot();
  settle(100);
  P("flash", screen() == "face" && settings.face == FACE_DIGITAL, "BOOT returns to the face without changing it");
  swipeDown();
  settle();
  tapButton(screens::Button::Flashlight);
  settle(100);
}

void extraHubs() {
  printf("\n== Settings hubs\n");
  startOn(FACE_DIGITAL);
  swipeUp();
  settle(600);
  struct { screens::Button b; int hub; const char *name; } items[] = {
      {screens::Button::Brightness, 0, "Brightness"}, {screens::Button::Night, 0, "Night"},
      {screens::Button::Motion, 0, "Motion"},         {screens::Button::HotSwap, 0, "Hot Swap"},
      {screens::Button::TimeFormat, 1, "Time format"}, {screens::Button::SetTime, 1, "Set time"},
      {screens::Button::PhoneSync, 1, "Phone sync"},  {screens::Button::SyncNow, 1, "Sync now"},
      {screens::Button::WifiSetup, 1, "Wi-Fi setup"}, {screens::Button::RaiseWake, 2, "Raise to wake"},
      {screens::Button::ScreenTurn, 2, "Screen turn"}, {screens::Button::TouchCheck, 3, "Touch check"},
      {screens::Button::Details, 3, "Details"}};
  int hubOf[8] = {0, 0, 1, 1, 1, 2, 3, 3};  // page -> hub, as the header jumps (pages 1, 3, 6, 7)
  int ok = 0;
  std::string where;
  for (auto &it : items) {
    bool f = goTo(it.b);
    int pg = ui::page();
    bool right = f && pg < 8 && hubOf[pg] == it.hub;
    ok += right;
    char t[64];
    snprintf(t, sizeof t, "%s%s p%d", where.empty() ? "" : ", ", it.name, f ? pg + 1 : 0);
    where += t;
  }
  P("hubs", ok == 13, "%d of 13 settings sit in their hub (%s)", ok, where.c_str());
  goTo(screens::Button::Brightness);
  for (int i = 0; i < 7; i++) {
    char nm[32];
    snprintf(nm, sizeof nm, "x_settings_p%d", i + 1);
    snap(nm);
    swipeLeft();
    settle();
  }
  goTo(screens::Button::Brightness);
  int seq[5];
  for (int i = 0; i < 5; i++) {
    seq[i] = ui::page() + 1;
    tap(160, 27);
    settle();
  }
  P("hubs", seq[0] == 1 && seq[1] == 3 && seq[2] == 6 && seq[3] == 7 && seq[4] == 1,
    "header taps go page %d > %d > %d > %d > %d", seq[0], seq[1], seq[2], seq[3], seq[4]);
  settings.largeText = true;
  swipeDown();
  settle();
  swipeUp();
  settle(600);
  int lseq[6], pages = 0;
  for (int i = 0; i < 6; i++) {
    lseq[i] = ui::page() + 1;
    tap(160, 27);
    settle();
  }
  goTo(screens::Button::Brightness);
  for (int i = 0; i < 20; i++) {
    int b = ui::page();
    swipeLeft();
    settle();
    if (ui::page() == b) break;
  }
  pages = ui::page() + 1;
  P("hubs", lseq[1] != lseq[0] && lseq[2] != lseq[1], "large text (%d pages): header taps go %d > %d > %d > %d > %d > %d",
    pages, lseq[0], lseq[1], lseq[2], lseq[3], lseq[4], lseq[5]);
  settings.largeText = false;
}

void extraSerial() {
  printf("\n== Serial FACES: and INFO?\n");
  startOn(FACE_DIGITAL);
  int saves = sim::settingsSaves;
  Serial.out.clear();
  Serial.in = "FACES:0x0041\n";
  runFor(100);
  P("serial", settings.activeFaces == 0x0041 && sim::settingsSaves > saves && Serial.out.find("0x0041") != std::string::npos,
    "FACES:0x0041 sets Digital + Orbit, saves, replies \"%s\"", Serial.out.c_str());
  int seen[FACE_COUNT] = {};
  for (int i = 0; i < 6; i++) {
    swipeLeft();
    settle(800);
    seen[settings.face]++;
  }
  int others = 0;
  for (int f = 0; f < FACE_COUNT; f++)
    if (f != FACE_DIGITAL && f != FACE_ORBIT) others += seen[f];
  P("serial", others == 0 && seen[FACE_ORBIT] && seen[FACE_DIGITAL], "swiping then cycles only Digital and Orbit");
  Serial.out.clear();
  Serial.in = "H=4095\n";
  runFor(100);
  P("serial", settings.activeFaces == ALL_FACES_MASK, "H=4095 turns all 12 faces back on (0x%04X)", settings.activeFaces);
  Serial.out.clear();
  Serial.in = "FACES:0\n";
  runFor(100);
  P("serial", settings.activeFaces == ALL_FACES_MASK && Serial.out.empty(), "FACES:0 is ignored (reply \"%s\")",
    Serial.out.c_str());
  Serial.in = "FACES:0x8000\n";
  runFor(100);
  P("serial", settings.activeFaces == ALL_FACES_MASK, "FACES:0x8000 (no real face) falls back to all faces (0x%04X)",
    settings.activeFaces);
  settings.activeFaces = 0x0002;
  Serial.out.clear();
  Serial.in = "FACES:0x0002\n";
  runFor(100);
  P("serial", settings.face == FACE_STEALTH || screen() == "face",
    "FACES: that leaves out the face on screen (face now %s)", FN[settings.face]);
  Serial.in = "H=4095\n";
  runFor(100);
  Serial.out.clear();
  sim::serialSent = "INFO?\n";
  runFor(100);
  std::string r1 = Serial.out;
  Serial.out.clear();
  Serial.in = "?\n";
  runFor(100);
  std::string r2 = Serial.out;
  bool fmt = r1.rfind("DRIFT:VER=" FW_VERSION ",TIME=", 0) == 0 && r1.find(",BAT=") != std::string::npos &&
             r1.find(",FACES=0x0FFF") != std::string::npos;
  if (!r1.empty() && r1.back() == '\n') r1.pop_back();
  P("serial", fmt && r2.rfind("DRIFT:", 0) == 0, "INFO? and ? reply \"%s\"", r1.c_str());
  Serial.in = "INFO?\r\n";
  Serial.out.clear();
  runFor(100);
  P("serial", !Serial.out.empty(), "INFO? with a Windows line ending also answers");
}


// The flashlight goes back to the face by itself after 2 minutes, and the screen may then sleep.
void toolsTimeouts() {
  printf("\n== Flashlight time-out\n");
  startOn(FACE_DIGITAL);
  swipeDown();
  settle();
  tapButton(screens::Button::Flashlight);
  settle(100);
  runFor(115000);
  P("flash", screen() == "flashlight" && lastBacklight() == 255, "still on just before 2 minutes (%s)", screen().c_str());
  runFor(6000);
  P("flash", screen() == "face" && lastBacklight() == backlightLevel(), "back to the face at %d after 2 minutes (%s)",
    lastBacklight(), screen().c_str());
  runFor(20000);
  P("flash", screen() == "asleep", "and the screen then sleeps as usual (%s)", screen().c_str());
}

// Serial lines sent to a dark watch: the first bytes wake it and are lost (as on the watch), and
// the next ones, sent the way the Windows script and the web page do, are answered.
void toolsSerialAsleep() {
  printf("\n== Serial while the screen is off\n");
  startOn(FACE_DIGITAL);
  runFor(20000);
  P("serial", screen() == "asleep", "the screen is off (%s)", screen().c_str());
  int lost = sim::serialLost;
  Serial.out.clear();
  sim::serialSent = "\n";
  runFor(100);
  P("serial", sim::serialLost > lost && screen() == "asleep", "a newline wakes the processor and is lost; the screen stays off");
  Serial.in = "INFO?\n";
  runFor(100);
  P("serial", Serial.out.find("DRIFT:VER=" FW_VERSION) != std::string::npos, "INFO? sent next is answered (\"%s\")",
    Serial.out.c_str());
  Serial.out.clear();
  sim::serialSent = "TIME:1800000000\n";
  runFor(100);
  P("serial", Serial.out.find("Time set to 1800000000") != std::string::npos && (long)hal::now() >= 1800000000L,
    "then TIME: sets the clock and says so");
  int lp = sim::lowPowerDozes;
  runFor(2000);
  P("serial", sim::lowPowerDozes == lp, "no light sleep while the computer is talking");
  runFor(5000);
  P("serial", sim::lowPowerDozes > lp && screen() == "asleep", "light sleep again 3 s after the last byte");
}

void toolChecks() {
  settings = Settings{};
  sim::flash.clear();
  sim::imuPresent = true;
  extraDrawer();
  extraTimers();
  extraFlashlight();
  toolsTimeouts();
  extraHubs();
  extraSerial();
  toolsSerialAsleep();
  sim::imuPresent = false;
  settings = Settings{};
}
}  // namespace
