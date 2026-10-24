// Drives the real interface code (ui.cpp, screens.cpp, faces.cpp, settings.cpp)
// with scripted touches, button presses and Wi-Fi outcomes, checks the results
// and saves frames, including mid-transition ones. Also lays out every
// settings, details and setup screen in both text sizes and checks text,
// touch regions and contrast. Output: PPM frames, debug overlays, a hit-target
// report and a check log; exit code 1 on any failed check.
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <string>
#include <vector>
#include "board.h"
#include "faces.h"
#include "hal.h"
#include "screens.h"
#include "settings.h"
#include "sim.h"
#include "theme.h"
#include "ui.h"

Face currentFaceForTest();

namespace {
const char *outDir = ".";
int failures = 0, checks = 0;
FILE *report = nullptr;

#define CHECK(cond, ...)                         \
  do {                                           \
    checks++;                                    \
    if (!(cond)) {                               \
      failures++;                                \
      printf("  FAIL line %d: ", __LINE__);      \
      printf(__VA_ARGS__);                       \
      printf("\n");                              \
    }                                            \
  } while (0)

void writePpm(const std::string &name, const TFT_eSPI &s) {
  std::string path = std::string(outDir) + "/" + name + ".ppm";
  FILE *f = fopen(path.c_str(), "wb");
  fprintf(f, "P6\n%d %d\n255\n", s.width(), s.height());
  for (uint16_t c : s.buf) {
    uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63),
                      (uint8_t)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

void snap(const char *name) { writePpm(std::string("sim_") + name, ui::display()); }
std::string screen() { return ui::screenName(); }

void runFor(uint32_t ms) {
  uint32_t end = sim::clockMs + ms;
  while ((int32_t)(sim::clockMs - end) < 0) ui::loop();
}

void touchDown(int x, int y) {
  sim::fingerX = x;
  sim::fingerY = y;
  sim::fingerDown = true;
  ui::loop();
}
void touchUp() {
  sim::fingerDown = false;
  ui::loop();
}

// Moves the finger in steps of `stepMs`, like the touch chip reporting every 10-15 ms.
void moveTo(int x, int y, uint32_t ms, uint32_t stepMs = 12) {
  int x0 = sim::fingerX, y0 = sim::fingerY;
  int n = max<int>(1, ms / stepMs);
  for (int i = 1; i <= n; i++) {
    sim::fingerX = x0 + (x - x0) * i / n;
    sim::fingerY = y0 + (y - y0) * i / n;
    runFor(stepMs);
  }
}

void swipe(int x0, int y0, int x1, int y1, uint32_t ms) {
  touchDown(x0, y0);
  moveTo(x1, y1, ms);
  touchUp();
}
void swipeLeft() { swipe(190, 120, 80, 120, 90); }
void swipeRight() { swipe(60, 120, 170, 120, 90); }
void swipeUp() { swipe(120, 180, 120, 80, 120); }
void swipeDown() { swipe(120, 70, 120, 170, 120); }

void tap(int x, int y) {
  touchDown(x, y);
  runFor(60);
  touchUp();
}

void pressBoot(uint32_t holdMs = 40) {
  sim::bootDown = true;
  runFor(holdMs);
  sim::bootDown = false;
  runFor(20);
}

void settle(uint32_t ms = 400) { runFor(ms); }
void settle(uint32_t ms);

// Finds a control on the current screen and taps the centre of its hit region.
bool find(screens::Button b, screens::Control &out) {
  screens::Control ctl[2];
  int n = ui::controls(ctl, 2);
  for (int i = 0; i < n; i++)
    if (ctl[i].id == b) {
      out = ctl[i];
      return true;
    }
  return false;
}
bool tapButton(screens::Button b) {
  screens::Control c;
  if (!find(b, c)) return false;
  tap(c.hit.x + c.hit.w / 2, c.hit.y + c.hit.h / 2);
  return true;
}

void settle(uint32_t ms);
// Swipes to the page of the current screen that holds `b`: back to the first page, then forward.
bool goTo(screens::Button b) {
  screens::Control c;
  for (int i = 0; i < 12 && ui::page() > 0; i++) {
    swipeRight();
    settle(400);
  }
  for (int i = 0; i < 12 && !find(b, c); i++) {
    int before = ui::page();
    swipeLeft();
    settle(400);
    if (ui::page() == before) break;
  }
  return find(b, c);
}

// ---- Screen layout checks ----------------------------------------------------------
float cornerDistance(const screens::Rect &r) {
  float worst = 0;
  for (int x : {(int)r.x, r.x + r.w - 1})
    for (int y : {(int)r.y, r.y + r.h - 1}) worst = max(worst, hypotf(x - layout::CX, y - layout::CY));
  return worst;
}
bool overlaps(const screens::Rect &a, const screens::Rect &b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
int gapBetween(const screens::Rect &a, const screens::Rect &b) {
  int gx = max(b.x - (a.x + a.w), a.x - (b.x + b.w)), gy = max(b.y - (a.y + a.h), a.y - (b.y + b.h));
  return max(gx, gy);
}
bool inside(const TFT_eSPI::TextBox &t, const screens::Rect &r, int inset) {
  return t.ix >= r.x + inset && t.iy >= r.y + inset && t.ix + t.iw <= r.x + r.w - inset && t.iy + t.ih <= r.y + r.h - inset;
}

// Checks one drawn screen: text inside the circle and inside its control,
// controls large enough, inside the circle, apart, and each one labelled.
int layoutProblems(const TFT_eSprite &s, const char *label, const screens::Control *ctl, int n, int pages) {
  int bad = 0;
  auto problem = [&](const char *fmt, auto... args) {
    printf("  LAYOUT %s: ", label);
    printf(fmt, args...);
    printf("\n");
    bad++;
  };
  for (const auto &t : s.textLog) {
    if (!t.iw) continue;
    for (int x : {t.ix, t.ix + t.iw - 1})
      for (int y : {t.iy, t.iy + t.ih - 1}) {
        float d = hypotf(x - layout::CX, y - layout::CY);
        if (d > layout::SAFE_R) problem("\"%s\" corner (%d,%d) is %.1f px from centre", t.text.c_str(), x, y, d);
      }
    if (pages > 1 && t.iy + t.ih > 204) problem("\"%s\" runs into the page dots", t.text.c_str());
    if (t.text.find("...") != std::string::npos) {
      bool wifi = false;
      for (int i = 0; i < n; i++) wifi |= ctl[i].id == screens::Button::WifiSetup && inside(t, ctl[i].visible, 0);
      if (!wifi) problem("essential text shortened: \"%s\"", t.text.c_str());
    }
    // Text that starts inside a control must stay inside it; other text must not touch any control.
    int cx = t.ix + t.iw / 2, cy = t.iy + t.ih / 2;
    bool owned = false;
    for (int i = 0; i < n; i++) {
      if (ctl[i].visible.contains(cx, cy)) {
        owned = true;
        if (!inside(t, ctl[i].visible, 3)) problem("\"%s\" is not inside its %s control", t.text.c_str(), screens::buttonName(ctl[i].id));
      }
    }
    if (!owned)
      for (int i = 0; i < n; i++) {
        screens::Rect ink{(int16_t)t.ix, (int16_t)t.iy, (int16_t)t.iw, (int16_t)t.ih};
        if (overlaps(ink, ctl[i].hit)) problem("\"%s\" overlaps the %s control", t.text.c_str(), screens::buttonName(ctl[i].id));
      }
  }
  for (int i = 0; i < n; i++) {
    const screens::Control &c = ctl[i];
    if (c.hit.w < layout::TOUCH_MIN || c.hit.h < layout::TOUCH_MIN)
      problem("%s touch region %dx%d is under %d px", screens::buttonName(c.id), c.hit.w, c.hit.h, layout::TOUCH_MIN);
    if (cornerDistance(c.hit) > layout::SAFE_R) problem("%s touch region leaves the circle (%.1f px)", screens::buttonName(c.id), cornerDistance(c.hit));
    if (c.hit.x > c.visible.x || c.hit.y > c.visible.y || c.hit.x + c.hit.w < c.visible.x + c.visible.w ||
        c.hit.y + c.hit.h < c.visible.y + c.visible.h)
      problem("%s touch region does not cover what is drawn", screens::buttonName(c.id));
    bool labelled = false;
    for (const auto &t : s.textLog) labelled |= t.iw && inside(t, c.visible, 0);
    // Minus and plus on the set time screen are drawn as symbols: ink at the centre counts.
    if (c.id == screens::Button::Minus || c.id == screens::Button::Plus)
      labelled = s.readPixel(c.visible.x + c.visible.w / 2, c.visible.y + c.visible.h / 2) != col::BG;
    if (!labelled) problem("%s has no text (its words did not fit)", screens::buttonName(c.id));
    for (int j = i + 1; j < n; j++) {
      if (overlaps(c.hit, ctl[j].hit)) problem("%s and %s touch regions overlap", screens::buttonName(c.id), screens::buttonName(ctl[j].id));
      if (gapBetween(c.visible, ctl[j].visible) < layout::TOUCH_SPACING)
        problem("%s and %s are %d px apart", screens::buttonName(c.id), screens::buttonName(ctl[j].id), gapBetween(c.visible, ctl[j].visible));
    }
  }
  return bad;
}

// Debug view: what is drawn, the touch regions (cyan), drawn bounds (yellow) and the safe circle (magenta).
void writeOverlay(const std::string &name, const TFT_eSprite &s, const screens::Control *ctl, int n) {
  TFT_eSPI parent;
  TFT_eSprite o(&parent);
  o.createSprite(240, 240);
  o.buf = s.buf;
  for (uint16_t &p : o.buf) p = (p >> 1) & 0x7BEF;  // dim the screen so the boxes stand out
  o.drawCircle(layout::CX, layout::CY, layout::SAFE_R, col::MAGENTA);
  for (int i = 0; i < n; i++) {
    o.drawRect(ctl[i].hit.x, ctl[i].hit.y, ctl[i].hit.w, ctl[i].hit.h, col::CYAN);
    o.drawRect(ctl[i].visible.x + 1, ctl[i].visible.y + 1, ctl[i].visible.w - 2, ctl[i].visible.h - 2, col::YELLOW);
  }
  for (const auto &t : s.textLog)
    if (t.iw) o.drawRect(t.ix, t.iy, t.iw, t.ih, rgb(0x00A040));
  writePpm("debug_" + name, o);
}

screens::View baseView() {
  screens::View v{};
  v.screen = screens::Screen::Settings;
  v.batteryPct = 86;
  v.batteryMv = 3950;
  v.brightnessLevel = 2;
  v.timeValid = true;
  v.stage = net::Sync::Idle;
  v.result = net::Result::Ok;
  v.portal = net::Portal::Off;
  v.secondsLeft = 272;
  v.lastSyncText = "20:48";
  v.zone = "Europe/London";
  v.ssid = "HomeNet-5G";
  v.version = "v" FW_VERSION;
  v.ms = 300;
  v.nowText = "20:48";
  v.imuPresent = true;
  v.raiseToWake = true;
  v.edit.tm_year = 2026 - 1900;
  v.edit.tm_mon = 9;
  v.edit.tm_mday = 24;
  v.edit.tm_hour = 20;
  v.edit.tm_min = 48;
  screens::stepTime(v.edit, screens::TimeField::Hour, 0);
  return v;
}

// All the words a view shows across its pages, joined with spaces, so phrases that wrap still match.
std::string wordsOf(TFT_eSprite &s, screens::View v) {
  std::string all;
  for (int p = 0; p < screens::pageCount(s, v); p++) {
    v.page = p;
    s.textLog.clear();
    screens::draw(s, v);
    for (const auto &t : s.textLog) all += " " + t.text;
  }
  return all + " ";
}
bool says(const std::string &words, const char *phrase) {
  // Lines are drawn one at a time, so a phrase may be split by line breaks (now spaces).
  return words.find(phrase) != std::string::npos;
}

struct HitRow {
  std::string screen;
  screens::Control c;
};
std::vector<HitRow> hitRows;

// Draws every page of a view, checks it and saves the frames.
int renderAll(TFT_eSprite &s, screens::View v, const std::string &name) {
  int bad = 0, pages = screens::pageCount(s, v);
  for (int p = 0; p < pages; p++) {
    v.page = p;
    s.textLog.clear();
    screens::draw(s, v);
    screens::Control ctl[2];
    int n = screens::controls(s, v, ctl, 2);
    std::string label = name + (v.large ? "_large" : "") + "_p" + std::to_string(p + 1);
    bad += layoutProblems(s, label.c_str(), ctl, n, pages);
    writePpm(label, s);
    writeOverlay(label, s, ctl, n);
    for (int i = 0; i < n; i++) {
      bool seen = false;
      for (auto &r : hitRows) seen |= r.c.id == ctl[i].id && r.c.hit.w == ctl[i].hit.w && r.c.hit.h == ctl[i].hit.h &&
                                       r.c.hit.x == ctl[i].hit.x && r.c.hit.y == ctl[i].hit.y;
      if (!seen) hitRows.push_back({label, ctl[i]});
    }
  }
  return bad;
}

void screenVariants() {
  printf("Settings, details and setup screens: layout in both text sizes\n");
  TFT_eSPI parent;
  TFT_eSprite s(&parent);
  s.createSprite(240, 240);
  int bad = 0;
  for (bool large : {false, true}) {
    screens::View v = baseView();
    v.large = large;
    bad += renderAll(s, v, "settings");

    // Clock and sync combinations on the settings screen (the sync control sits on the second page).
    struct C { const char *name; bool valid; net::Sync stage; net::Result result; net::Fail fail; const char *synced; };
    C combos[] = {
        {"valid_syncing", true, net::Sync::Zone, net::Result::Ok, net::Fail::None, "14:02"},
        {"valid_failed", true, net::Sync::Idle, net::Result::Failed, net::Fail::JoinFailed, "14:02"},
        {"notime_syncing", false, net::Sync::Connecting, net::Result::None, net::Fail::None, ""},
        {"notime_failed", false, net::Sync::Idle, net::Result::Failed, net::Fail::ClockFailed, ""},
    };
    for (const C &c : combos) {
      screens::View w = v;
      w.timeValid = c.valid;
      w.stage = c.stage;
      w.syncStep = c.stage == net::Sync::Connecting ? 1 : c.stage == net::Sync::Zone ? 2 : 0;
      w.result = c.result;
      w.fail = c.fail;
      w.lastSyncText = c.synced;
      w.page = screens::pageOf(s, w, screens::Button::SyncNow);
      s.textLog.clear();
      screens::draw(s, w);
      screens::Control ctl[2];
      int n = screens::controls(s, w, ctl, 2);
      std::string label = std::string("settings_") + c.name + (large ? "_large" : "");
      bad += layoutProblems(s, label.c_str(), ctl, n, screens::pageCount(s, w));
      writePpm(label, s);
      bool failWords = false, busyWords = false;
      for (const auto &t : s.textLog) {
        failWords |= t.text == "Sync failed" || t.text == "Time not set";
        busyWords |= t.text == "Syncing";
      }
      bool runningNow = c.stage != net::Sync::Idle;
      CHECK(runningNow ? busyWords : failWords, "%s: sync control shows %s", label.c_str(), runningNow ? "Syncing" : "the failure");
    }

    // Details: status pages plus the text size page.
    screens::View d = v;
    d.screen = screens::Screen::Details;
    bad += renderAll(s, d, "details");
    d.result = net::Result::Failed;
    d.fail = net::Fail::JoinFailed;
    d.stage = net::Sync::Connecting;
    d.syncStep = 1;
    d.zone = "America/Argentina/ComodRivadavia";
    d.ssid = "A very long network name here";
    d.batteryPct = -1;
    bad += renderAll(s, d, "details_retrying");

    // Setup: every phase.
    struct S { const char *name; net::Portal portal; net::Sync stage; net::Result result; net::Fail fail; int step; };
    S setups[] = {
        {"hotspot", net::Portal::Waiting, net::Sync::Idle, net::Result::None, net::Fail::None, 0},
        {"phone", net::Portal::PhoneJoined, net::Sync::Idle, net::Result::None, net::Fail::None, 0},
        {"joining", net::Portal::Saved, net::Sync::Connecting, net::Result::None, net::Fail::None, 1},
        {"zone", net::Portal::Saved, net::Sync::Zone, net::Result::None, net::Fail::None, 2},
        {"clock", net::Portal::Saved, net::Sync::Clock, net::Result::None, net::Fail::None, 3},
        {"done", net::Portal::Saved, net::Sync::Idle, net::Result::Ok, net::Fail::None, 0},
        {"joinfail", net::Portal::Saved, net::Sync::Idle, net::Result::Failed, net::Fail::JoinFailed, 0},
        {"clockfail", net::Portal::Saved, net::Sync::Idle, net::Result::Failed, net::Fail::ClockFailed, 0},
        {"timeout", net::Portal::TimedOut, net::Sync::Idle, net::Result::None, net::Fail::None, 0},
    };
    for (const S &p : setups) {
      screens::View w = v;
      w.screen = screens::Screen::Setup;
      w.portal = p.portal;
      w.stage = p.stage;
      w.result = p.result;
      w.fail = p.fail;
      w.syncStep = p.step;
      bad += renderAll(s, w, std::string("setup_") + p.name);
    }
    // v0.9.0: set time pages, in both clock formats.
    for (bool h12 : {false, true}) {
      screens::View w = v;
      w.screen = screens::Screen::SetTime;
      w.use12h = h12;
      bad += renderAll(s, w, h12 ? "settime_12h" : "settime");
    }
    // Settings states added in v0.9.0.
    {
      screens::View w = v;
      w.imuPresent = false;
      w.page = screens::pageOf(s, w, screens::Button::RaiseWake);
      s.textLog.clear();
      screens::draw(s, w);
      screens::Control ctl[2];
      int n = screens::controls(s, w, ctl, 2);
      bad += layoutProblems(s, large ? "settings_nosensor_large" : "settings_nosensor", ctl, n, screens::pageCount(s, w));
      w = v;
      w.timeValid = false;
      w.ssid = "";
      w.lastSyncText = "";
      w.result = net::Result::Failed;
      w.fail = net::Fail::NoNetworkSaved;
      bad += renderAll(s, w, "settings_notime_nowifi");
      w.timeValid = true;
      w.clockByHand = true;
      bad += renderAll(s, w, "settings_byhand");
    }
    // Every join failure reason, on the setup result and in details.
    for (net::JoinWhy why : {net::JoinWhy::NotFound, net::JoinWhy::Password, net::JoinWhy::Refused, net::JoinWhy::NoAnswer}) {
      static const char *names[] = {"noanswer", "notfound", "password", "refused"};
      screens::View w = v;
      w.screen = screens::Screen::Setup;
      w.portal = net::Portal::Saved;
      w.result = net::Result::Failed;
      w.fail = net::Fail::JoinFailed;
      w.joinWhy = why;
      w.joinCode = why == net::JoinWhy::NotFound ? 201 : 15;
      bad += renderAll(s, w, std::string("setup_join_") + names[(int)why]);
      w.screen = screens::Screen::Details;
      bad += renderAll(s, w, std::string("details_join_") + names[(int)why]);
    }

    // Pressed states.
    screens::View pv = v;
    pv.pressed = screens::Button::Brightness;
    s.textLog.clear();
    screens::draw(s, pv);
    writePpm(std::string("settings_pressed") + (large ? "_large" : ""), s);
  }
  for (float t : {0.15f, 0.4f, 0.7f, 1.0f}) {
    s.textLog.clear();
    screens::drawBoot(s, t, "v" FW_VERSION);
    char label[32];
    snprintf(label, sizeof label, "boot_%03d", (int)(t * 100));
    bad += layoutProblems(s, label, nullptr, 0, 1);
    writePpm(label, s);
  }
  CHECK(bad == 0, "%d layout problems on settings, details and setup screens", bad);

  printf("Wi-Fi setup wording (v0.9.0)\n");
  {
    screens::View w = baseView();
    w.screen = screens::Screen::Setup;
    w.portal = net::Portal::Waiting;
    std::string step1 = wordsOf(s, w);
    w.page = 0;
    s.textLog.clear();
    screens::draw(s, w);
    bool firstPageJoins = false;
    for (const auto &t : s.textLog) firstPageJoins |= t.text == "Drift-Setup";
    CHECK(firstPageJoins && !says(step1, "open 192.168.4.1") && says(step1, "only opens once your phone is on Drift-Setup"),
          "step 1 says to join Drift-Setup first, and that the address only works after that");
    w.portal = net::Portal::PhoneJoined;
    std::string step2 = wordsOf(s, w);
    CHECK(says(step2, "Stay on Drift-Setup") && says(step2, "192.168.4.1") && says(step2, "2.4 GHz") && says(step2, "5 GHz"),
          "step 2 says to stay on Drift-Setup, open the address and pick a 2.4 GHz network");
    w.portal = net::Portal::Saved;
    w.result = net::Result::Failed;
    w.fail = net::Fail::JoinFailed;
    struct W { net::JoinWhy why; const char *words, *hint; };
    W cases[] = {{net::JoinWhy::NotFound, "Network not found.", "2.4 GHz"},
                 {net::JoinWhy::Password, "Password not accepted.", "Check the password"},
                 {net::JoinWhy::Refused, "turned the watch away", "2.4 GHz"},
                 {net::JoinWhy::NoAnswer, "No answer from the network.", "2.4 GHz"}};
    for (const W &c : cases) {
      w.joinWhy = c.why;
      w.screen = screens::Screen::Setup;
      std::string res = wordsOf(s, w);
      w.screen = screens::Screen::Details;
      std::string det = wordsOf(s, w);
      CHECK(says(res, c.words) && says(res, c.hint) && says(det, c.words), "join failure %d is explained: \"%s\"", (int)c.why, c.words);
    }
    w.joinCode = 201;
    w.joinWhy = net::JoinWhy::NotFound;
    CHECK(says(wordsOf(s, w), "Wi-Fi reason 201"), "details show the radio's reason number");
  }

  // Reduced motion: the progress symbol is still, so nothing needs redrawing.
  screens::View r = baseView();
  r.screen = screens::Screen::Setup;
  r.portal = net::Portal::Saved;
  r.stage = net::Sync::Connecting;
  r.syncStep = 1;
  r.reducedMotion = true;
  bool anim = screens::draw(s, r);
  std::vector<uint16_t> a = s.buf;
  r.ms = 777;
  screens::draw(s, r);
  CHECK(!anim && a == s.buf, "reduced motion shows a still symbol, not a frozen spinner");
  writePpm("setup_joining_reduced", s);
  r.reducedMotion = false;
  CHECK(screens::draw(s, r), "full motion asks for spinner frames while connecting");

  // Hit-target report.
  fprintf(report, "| Screen (first seen) | Control | Drawn x,y w x h | Touch x,y w x h | Touch size (mm) | Farthest corner |\n");
  fprintf(report, "|---|---|---|---|---|---|\n");
  for (const HitRow &h : hitRows) {
    const screens::Control &c = h.c;
    fprintf(report, "| %s | %s | %d,%d %dx%d | %d,%d %dx%d | %.1f x %.1f | %.1f px |\n", h.screen.c_str(),
            screens::buttonName(c.id), c.visible.x, c.visible.y, c.visible.w, c.visible.h, c.hit.x, c.hit.y, c.hit.w,
            c.hit.h, c.hit.w * 0.135f, c.hit.h * 0.135f, cornerDistance(c.hit));
  }
}

// ---- Contrast, from the colour tokens ---------------------------------------------------
float channel(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
// Relative luminance of the colour the panel shows: the RGB565 value expanded back to 8 bits.
float luminance(uint16_t c) {
  float r = ((c >> 11) & 31) / 31.0f, g = ((c >> 5) & 63) / 63.0f, b = (c & 31) / 31.0f;
  return 0.2126f * channel(r) + 0.7152f * channel(g) + 0.0722f * channel(b);
}
float contrast(uint16_t a, uint16_t b) {
  float la = luminance(a), lb = luminance(b);
  return (max(la, lb) + 0.05f) / (min(la, lb) + 0.05f);
}

void contrastAudit() {
  printf("Contrast of settings and setup colours (tokens, as shown in RGB565)\n");
  struct P { const char *use; uint16_t fg, bg; float need; };
  P pairs[] = {
      {"essential text (TEXT) on black", col::TEXT, col::BG, 4.5f},
      {"labels and supporting text (TEXT_2) on black", col::TEXT_2, col::BG, 4.5f},
      {"progress text (CYAN) on black", col::CYAN, col::BG, 4.5f},
      {"Wi-Fi setup text (MAGENTA) on black", col::MAGENTA, col::BG, 4.5f},
      {"warnings (WARN) on black", col::WARN, col::BG, 4.5f},
      {"pressed value (WHITE) on pressed fill", col::WHITE, col::CYAN_DEEP, 4.5f},
      {"pressed label (TEXT) on pressed fill", col::TEXT, col::CYAN_DEEP, 4.5f},
      {"progress text (CYAN) on pressed fill", col::CYAN, col::CYAN_DEEP, 4.5f},
      {"control edge (OUTLINE) on black", col::OUTLINE, col::BG, 3.0f},
      {"pressed edge (CYAN) on black", col::CYAN, col::BG, 3.0f},
      {"brightness bar, empty (OUTLINE) on black", col::OUTLINE, col::BG, 3.0f},
      {"current page dot (TEXT) on black", col::TEXT, col::BG, 3.0f},
      {"other page dots (OUTLINE) on black", col::OUTLINE, col::BG, 3.0f},
  };
  fprintf(report, "\n| Use | Ratio | Goal |\n|---|---|---|\n");
  for (const P &p : pairs) {
    float r = contrast(p.fg, p.bg);
    fprintf(report, "| %s | %.1f:1 | %.1f:1 |\n", p.use, r, p.need);
    CHECK(r >= p.need, "%s is %.2f:1, goal %.1f:1", p.use, r, p.need);
  }
  // For the record: the old panel's grey labels.
  fprintf(report, "| (v0.7.0 tile labels, N400 on black) | %.1f:1 | 4.5:1 |\n", contrast(col::N400, col::BG));
  fprintf(report, "| (v0.7.0 tile edges, N600 on black) | %.1f:1 | 3.0:1 |\n", contrast(col::N600, col::BG));
}

// ---- Interaction scenarios ---------------------------------------------------------
int lastBacklight() { return sim::backlightLog.empty() ? -1 : sim::backlightLog.back().second; }

void openSettings() {
  swipeUp();
  settle();
}

// Moves the simulated wrist through a list of (duration, acceleration) steps.
void wrist(std::initializer_list<std::pair<uint32_t, Accel>> steps) {
  for (const auto &st : steps) {
    sim::accel = st.second;
    runFor(st.first);
  }
}
const Accel HANGING = {0, -1, 0};           // arm down at the side: screen vertical
const Accel LOOKING = {0, -0.45f, 0.89f};   // turned up towards the eyes, about 27 degrees from level
const Accel FLAT = {0, 0, 1};               // on a table, screen up

void raiseScenarios() {
  printf("Raise to wake\n");
  sim::accel = FLAT;
  settings.raiseToWake = true;
  runFor(16000);
  CHECK(screen() == "asleep", "asleep, got %s", screen().c_str());
  int lowPower = sim::lowPowerDozes;
  runFor(5000);
  CHECK(screen() == "asleep", "lying flat and still does not wake it");
  CHECK(sim::lowPowerDozes > lowPower, "the screen-off wait uses light sleep");
  wrist({{1000, HANGING}, {400, LOOKING}});
  CHECK(screen() == "face", "raising the wrist wakes it, got %s", screen().c_str());
  runFor(16000);
  CHECK(screen() == "asleep", "it sleeps again while the wrist stays up, got %s", screen().c_str());
  runFor(3000);
  CHECK(screen() == "asleep", "a wrist that stays up does not wake it again");
  wrist({{800, HANGING}, {400, LOOKING}});
  CHECK(screen() == "face", "lowering and raising again wakes it");
  runFor(16000);

  printf("Raise to wake: what does not wake it\n");
  wrist({{800, HANGING}, {2500, {0, -0.2f, 0.4f}}, {400, LOOKING}});
  CHECK(screen() == "asleep", "a slow turn (over 1.5 s) does not wake it");
  wrist({{800, HANGING}, {100, LOOKING}, {300, HANGING}});
  CHECK(screen() == "asleep", "a brief swing through does not wake it");
  wrist({{800, HANGING}, {400, {0, -0.9f, 1.8f}}});
  CHECK(screen() == "asleep", "shaking (2 g) does not wake it");
  wrist({{800, HANGING}, {400, {0, 0.45f, -0.89f}}});
  CHECK(screen() == "asleep", "turning the screen face down does not wake it");
  settings.raiseToWake = false;
  wrist({{800, HANGING}, {400, LOOKING}});
  CHECK(screen() == "asleep", "with Raise to wake off, raising does nothing");
  settings.raiseToWake = true;

  printf("Raise to wake: the watch learns which way the screen faces\n");
  sim::accel = {0, 0, -1};  // a board where the sensor's z points into the screen
  for (int i = 0; i < 6; i++) {
    touchDown(120, 120);
    touchUp();
    runFor(300);
  }
  CHECK(settings.screenFacing == -1, "touches while looking teach it the screen faces -z (got %d)", settings.screenFacing);
  runFor(16000);
  wrist({{800, HANGING}, {400, {0, -0.45f, -0.89f}}});
  CHECK(screen() == "face", "raising now wakes it on that board, got %s", screen().c_str());
  sim::accel = FLAT;
  for (int i = 0; i < 6; i++) {
    touchDown(120, 120);
    touchUp();
    runFor(300);
  }
  CHECK(settings.screenFacing == 1, "and it learns back (got %d)", settings.screenFacing);

  printf("Raise to wake setting\n");
  openSettings();
  CHECK(goTo(screens::Button::RaiseWake) && tapButton(screens::Button::RaiseWake) && !settings.raiseToWake, "the setting turns it off");
  tapButton(screens::Button::RaiseWake);
  CHECK(settings.raiseToWake, "and on");
  pressBoot();
  settle();

  printf("Light sleep stays off while Wi-Fi runs\n");
  runFor(16000);
  CHECK(screen() == "asleep", "asleep");
  net::startSync();
  int lp = sim::lowPowerDozes, dz = sim::dozes;
  runFor(1000);
  CHECK(net::syncRunning() && sim::lowPowerDozes == lp && sim::dozes == dz, "no light sleep while a sync runs");
  settle(2000);
  lp = sim::lowPowerDozes;
  runFor(1000);
  CHECK(sim::lowPowerDozes > lp, "light sleep again once the sync ends");

  printf("No motion sensor\n");
  sim::imuPresent = false;
  ui::begin();
  settle(2500);
  runFor(16000);
  wrist({{800, HANGING}, {400, LOOKING}});
  CHECK(screen() == "asleep", "without the sensor nothing happens and nothing breaks");
  touchDown(120, 120); touchUp(); settle();
  openSettings();
  goTo(screens::Button::RaiseWake);
  snap("settings_no_sensor");
  pressBoot();
  sim::imuPresent = true;
  sim::accel = FLAT;
  ui::begin();
  settle(2500);
}

void timeSteps() {
  printf("Set time arithmetic\n");
  tm t{};
  t.tm_year = 2026 - 1900;
  t.tm_mon = 9;
  t.tm_mday = 24;
  screens::stepTime(t, screens::TimeField::Hour, 0);
  CHECK(t.tm_wday == 6, "24 Oct 2026 is a Saturday (got %d)", t.tm_wday);
  t.tm_mon = 0;
  t.tm_mday = 31;
  screens::stepTime(t, screens::TimeField::Month, +1);
  CHECK(t.tm_mon == 1 && t.tm_mday == 28, "31 Jan 2026 moved to February becomes 28 Feb (%d %d)", t.tm_mday, t.tm_mon + 1);
  t.tm_year = 2028 - 1900;
  t.tm_mon = 0;
  t.tm_mday = 31;
  screens::stepTime(t, screens::TimeField::Month, +1);
  CHECK(t.tm_mday == 29, "and 29 Feb in a leap year (%d)", t.tm_mday);
  screens::stepTime(t, screens::TimeField::Day, +1);
  CHECK(t.tm_mday == 1 && t.tm_mon == 1, "days wrap inside the month without changing it");
  t.tm_hour = 23;
  t.tm_min = 59;
  screens::stepTime(t, screens::TimeField::Minute, +1);
  CHECK(t.tm_min == 0 && t.tm_hour == 23, "minutes wrap without changing the hour");
  screens::stepTime(t, screens::TimeField::Hour, +1);
  CHECK(t.tm_hour == 0 && t.tm_mday == 1, "hours wrap without changing the day");
  t.tm_year = screens::YEAR_MAX - 1900;
  screens::stepTime(t, screens::TimeField::Year, +1);
  CHECK(t.tm_year + 1900 == screens::YEAR_MIN, "years wrap within %d-%d", screens::YEAR_MIN, screens::YEAR_MAX);
}

void scenarios() {
  printf("Startup\n");
  sim::cold = true;
  settings = Settings{};
  ui::begin();
  CHECK(screen() == "boot", "cold boot shows the startup sequence, got %s", screen().c_str());
  CHECK(sim::syncsStarted == 1, "sync starts during the startup sequence");
  runFor(150); snap("boot_a");
  runFor(300); snap("boot_b");
  runFor(300); snap("boot_c");
  runFor(500);
  CHECK(screen() == "face", "startup ends on the face, got %s", screen().c_str());
  snap("face_syncing");
  settle(1500);
  CHECK(net::lastResult() == net::Result::Ok && !net::syncRunning(), "sync finished");
  runFor(1000);
  snap("face_synced");

  printf("Swipe that follows the finger\n");
  int face0 = settings.face;
  touchDown(190, 120);
  moveTo(130, 120, 80);
  CHECK(screen() == "face+slide", "dragging moves the face, got %s", screen().c_str());
  snap("drag_60px");
  touchUp();
  runFor(60);
  snap("commit_mid");
  settle();
  CHECK(settings.face == (face0 + 1) % FACE_COUNT, "a 60 px flick goes to the next face (face %d)", settings.face);
  CHECK(screen() == "face", "slide finished, got %s", screen().c_str());

  printf("Small slow drag snaps back\n");
  int face1 = settings.face;
  touchDown(120, 120);
  moveTo(150, 122, 300, 30);
  runFor(150);  // finger stops before lifting, so it is not a flick
  touchUp();
  settle();
  CHECK(settings.face == face1, "a 30 px slow drag keeps the face");

  printf("Rapid swipes do not queue\n");
  int face2 = settings.face;
  uint32_t t0 = sim::clockMs;
  for (int i = 0; i < 3; i++) {
    if (i) runFor(60);  // a finger needs at least this long to lift and come back
    swipe(200, 120, 110, 120, 70);
  }
  uint32_t released = sim::clockMs;
  while (screen() != "face" && sim::clockMs - released < 2000) ui::loop();
  CHECK(settings.face == (face2 + 3) % FACE_COUNT, "three swipes move three faces (moved %d)",
        (settings.face - face2 + FACE_COUNT) % FACE_COUNT);
  CHECK(sim::clockMs - released <= motion::FACE_MS + 20, "settles %u ms after the last release",
        sim::clockMs - released);
  printf("  three swipes took %u ms in total\n", sim::clockMs - t0);

  printf("Catching a slide reverses it\n");
  int face3 = settings.face;
  swipe(200, 120, 120, 120, 60);
  runFor(40);
  touchDown(120, 120);
  snap("caught_mid_slide");
  moveTo(220, 120, 100);
  touchUp();
  settle();
  CHECK(settings.face == face3, "catching and dragging back keeps the face (face %d, expected %d)", settings.face, face3);

  printf("BOOT: short press is next face, on release\n");
  int face4 = settings.face;
  sim::bootDown = true;
  runFor(100);
  CHECK(settings.face == face4 && screen() == "face", "nothing happens while BOOT is still down");
  sim::bootDown = false;
  runFor(20);
  settle();
  CHECK(settings.face == (face4 + 1) % FACE_COUNT, "BOOT advances the face on release");

  printf("BOOT: long press opens settings, without also changing face\n");
  int face5 = settings.face;
  sim::bootDown = true;
  runFor(420);
  snap("boot_hold_ring");
  CHECK(screen() == "face", "still on the face before 600 ms");
  runFor(300);
  CHECK(screen() == "panel" || screen() == "panel+slide" || screen() == "face+slide", "settings opening after a long press, got %s", screen().c_str());
  sim::bootDown = false;
  settle();
  CHECK(screen() == "panel" && settings.face == face5, "settings open and the face is unchanged (face %d, was %d)", settings.face, face5);
  pressBoot();
  settle();
  CHECK(screen() == "face" && settings.face == face5, "BOOT in settings goes back to the face, no face change");

  printf("Long touch opens settings with a hold ring\n");
  settings.face = FACE_STEALTH;
  runFor(1100);
  touchDown(120, 120);
  runFor(420);
  snap("hold_ring");
  CHECK(screen() == "face", "still on the face before 600 ms");
  runFor(250);
  CHECK(screen() == "face+slide" || screen() == "panel", "panel opening after hold, got %s", screen().c_str());
  runFor(80);
  snap("panel_opening");
  touchUp();
  settle();
  CHECK(screen() == "panel" && ui::page() == 0, "settings open on page 1, got %s page %d", screen().c_str(), ui::page() + 1);
  snap("settings_p1");

  printf("Settings pages: swipes change pages, never faces\n");
  int faceP = settings.face;
  swipeLeft();
  runFor(motion::FACE_MS / 2);
  snap("settings_page_slide");
  settle();
  CHECK(screen() == "panel" && ui::page() == 1, "swipe left shows page 2 (page %d)", ui::page() + 1);
  swipeLeft();
  settle();
  CHECK(ui::page() == 2, "and page 3 (page %d)", ui::page() + 1);
  swipeLeft();
  settle();
  CHECK(ui::page() == 3, "and page 4 (page %d)", ui::page() + 1);
  swipeLeft();
  settle();
  CHECK(ui::page() == 3 && screen() == "panel", "no page after the last; it does not wrap or leave settings");
  swipeRight();
  settle();
  CHECK(ui::page() == 2, "swipe right goes back a page");
  CHECK(settings.face == faceP, "no settings swipe changed the face (face %d, was %d)", settings.face, faceP);
  goTo(screens::Button::Brightness);

  printf("Settings controls\n");
  int light = settings.brightnessLevel;
  screens::Control c;
  CHECK(find(screens::Button::Brightness, c), "Brightness is on page 1");
  touchDown(c.hit.x + c.hit.w / 2, c.hit.y + c.hit.h / 2);
  snap("settings_brightness_pressed");
  runFor(60);
  touchUp();
  CHECK(settings.brightnessLevel == (light + 1) % BRIGHTNESS_LEVEL_COUNT, "Brightness steps the level");
  CHECK(lastBacklight() == backlightLevel(), "backlight follows (%d vs %d)", lastBacklight(), backlightLevel());
  bool h12 = settings.use12h;
  CHECK(tapButton(screens::Button::TimeFormat) && settings.use12h != h12, "Time format switches 12/24 hour");
  tapButton(screens::Button::TimeFormat);
  // Edges of the hit regions: the extra rows between the controls belong to the nearest one.
  find(screens::Button::Brightness, c);
  int before = settings.brightnessLevel;
  tap(c.hit.x + 4, c.hit.y + c.hit.h - 2);  // below the drawn box, inside its touch region
  CHECK(settings.brightnessLevel != before, "a tap just under the drawn Brightness box still counts");
  screens::Control lower;
  find(screens::Button::TimeFormat, lower);
  int gapY = (c.hit.y + c.hit.h + lower.hit.y) / 2;
  before = settings.brightnessLevel;
  bool fmt = settings.use12h;
  tap(120, gapY);
  CHECK(settings.brightnessLevel == before && settings.use12h == fmt, "a tap in the gap between controls does nothing");

  printf("Drags never act\n");
  goTo(screens::Button::Motion);
  int motionPage = ui::page();
  find(screens::Button::Motion, c);
  touchDown(c.hit.x + c.hit.w / 2, c.hit.y + c.hit.h / 2);
  moveTo(c.hit.x + c.hit.w / 2, c.hit.y + c.hit.h / 2 + 24, 100);  // slides off: a drag is never a tap
  touchUp();
  CHECK(!settings.reducedMotion, "a press that drags away does not toggle Motion");
  touchDown(c.hit.x + c.hit.w - 10, c.hit.y + c.hit.h / 2);
  moveTo(c.hit.x - 40, c.hit.y + c.hit.h / 2, 90);  // a page swipe that starts on a control
  touchUp();
  settle();
  CHECK(!settings.reducedMotion && ui::page() == motionPage + 1, "a swipe that starts on a control changes page only (page %d)", ui::page() + 1);
  swipeRight();
  settle();
  CHECK(tapButton(screens::Button::Motion) && settings.reducedMotion, "Motion switches to Reduced");
  tapButton(screens::Button::Motion);
  CHECK(!settings.reducedMotion, "and back to Full");

  printf("Sync now: one sync at a time, and other settings keep working\n");
  int syncs = sim::syncsStarted;
  goTo(screens::Button::SyncNow);
  tapButton(screens::Button::SyncNow);
  CHECK(sim::syncsStarted == syncs + 1 && net::syncRunning(), "Sync now starts a sync");
  runFor(300);
  tapButton(screens::Button::SyncNow);
  tapButton(screens::Button::SyncNow);
  CHECK(sim::syncsStarted == syncs + 1, "taps while syncing do not start more (%d started)", sim::syncsStarted - syncs);
  snap("settings_syncing");
  uint64_t px0 = ui::display().pixelsPushed;
  runFor(400);
  CHECK(ui::display().pixelsPushed > px0, "the spinner animates while syncing");
  bool fmtBefore = settings.use12h;
  goTo(screens::Button::TimeFormat);
  tapButton(screens::Button::TimeFormat);
  CHECK(settings.use12h != fmtBefore && net::syncRunning(), "Time format still works during a sync");
  tapButton(screens::Button::TimeFormat);
  goTo(screens::Button::SyncNow);
  settle(1500);
  CHECK(net::lastResult() == net::Result::Ok && !net::syncRunning(), "sync done");
  snap("settings_synced");

  printf("An idle settings screen is not redrawn\n");
  px0 = ui::display().pixelsPushed;
  runFor(3000);
  CHECK(ui::display().pixelsPushed == px0, "no frames pushed in 3 s on an idle settings page (%llu px)",
        (unsigned long long)(ui::display().pixelsPushed - px0));

  printf("Details keeps the settings page, and holds the text size control\n");
  goTo(screens::Button::Details);
  CHECK(tapButton(screens::Button::Details) && screen() == "details", "Details opens, got %s", screen().c_str());
  snap("details_p1");
  int pagesSeen = 1;
  while (!find(screens::Button::TextSize, c) && pagesSeen < 6) {
    swipeLeft();
    settle();
    pagesSeen++;
  }
  CHECK(find(screens::Button::TextSize, c), "Text size is on the last details page (%d pages)", pagesSeen);
  int faceD = settings.face;
  CHECK(tapButton(screens::Button::TextSize) && settings.largeText, "Text size switches to Large");
  CHECK(find(screens::Button::TextSize, c), "after reflowing, the Text size control is still on screen");
  snap("details_large_textsize");
  pressBoot();
  CHECK(screen() == "panel", "BOOT goes back to settings, got %s", screen().c_str());
  screens::Control dc;
  CHECK(find(screens::Button::Details, dc), "back on the settings page that holds Details (page %d)", ui::page() + 1);
  CHECK(settings.face == faceD, "no face change");
  snap("settings_large_details");
  swipeRight();
  settle();
  snap("settings_large_wifi");
  // Back to normal text through the same path.
  tapButton(screens::Button::WifiSetup);  // also checks the large Wi-Fi control works
  CHECK(screen() == "setup", "Wi-Fi setup opens with large text, got %s", screen().c_str());
  snap("setup_hotspot_large");
  pressBoot();
  CHECK(screen() == "panel" && net::portalState() == net::Portal::Closed, "BOOT cancels the waiting hotspot");
  goTo(screens::Button::Details);
  tapButton(screens::Button::Details);
  while (!find(screens::Button::TextSize, c)) { swipeLeft(); settle(); }
  tapButton(screens::Button::TextSize);
  CHECK(!settings.largeText, "Text size back to Normal");
  swipeDown();
  settle();
  CHECK(screen() == "panel" && find(screens::Button::Details, c), "swipe down from details returns to the settings page with Details (page %d)", ui::page() + 1);

  printf("Swipe down closes settings\n");
  swipeDown();
  runFor(120);
  snap("panel_closing");
  settle();
  CHECK(screen() == "face", "swipe down returns to the face, got %s", screen().c_str());

  printf("Wi-Fi setup: failure, edit, retry, success\n");
  openSettings();
  goTo(screens::Button::WifiSetup);
  CHECK(tapButton(screens::Button::WifiSetup) && screen() == "setup", "Wi-Fi setup opens setup, got %s", screen().c_str());
  runFor(200);
  snap("setup_hotspot");
  CHECK(ui::page() == 0 && goTo(screens::Button::Cancel), "the hotspot screen offers Cancel after its instructions");
  goTo(screens::Button::None);  // back to the first page
  sim::setPortal(net::Portal::PhoneJoined);
  runFor(100);
  snap("setup_phone");
  sim::syncOutcome = net::Fail::JoinFailed;
  sim::setPortal(net::Portal::Saved);
  runFor(300);
  snap("setup_joining");
  CHECK(find(screens::Button::Back, c) && find(screens::Button::Cancel, c), "connecting offers Back and Cancel");
  settle(1500);
  snap("setup_joinfail");
  CHECK(net::lastResult() == net::Result::Failed && net::failReason() == net::Fail::JoinFailed, "join failure reported");
  runFor(20000);
  CHECK(screen() == "setup", "the failure is still on screen after 20 s, got %s", screen().c_str());
  CHECK(goTo(screens::Button::Retry) && tapButton(screens::Button::Retry) && net::syncRunning(), "Retry tries the saved network again");
  settle(1500);
  CHECK(goTo(screens::Button::EditWifi) && tapButton(screens::Button::EditWifi) && net::portalState() == net::Portal::Waiting, "Edit Wi-Fi reopens the hotspot");
  sim::syncOutcome = net::Fail::None;
  sim::setPortal(net::Portal::Saved);
  settle(2500);
  snap("setup_done");
  CHECK(tapButton(screens::Button::Done) && screen() == "face", "Done returns to the face, got %s", screen().c_str());

  printf("Wi-Fi setup: Back lets it finish, Cancel stops it\n");
  openSettings();
  goTo(screens::Button::WifiSetup);
  tapButton(screens::Button::WifiSetup);
  sim::setPortal(net::Portal::Saved);
  runFor(200);
  CHECK(tapButton(screens::Button::Back) && screen() == "panel" && find(screens::Button::WifiSetup, c), "Back returns to the settings page with Wi-Fi setup");
  CHECK(net::syncRunning(), "and the sync carries on");
  settle(2000);
  CHECK(net::lastResult() == net::Result::Ok, "and finishes");
  tapButton(screens::Button::WifiSetup);
  sim::setPortal(net::Portal::Saved);
  runFor(200);
  int cancelled = sim::syncsCancelled;
  CHECK(tapButton(screens::Button::Cancel) && screen() == "panel", "Cancel returns to settings");
  CHECK(sim::syncsCancelled == cancelled + 1 && !net::syncRunning(), "and stops the sync");
  CHECK(net::timeIsValid(), "a cancelled sync keeps the valid clock");

  printf("Wi-Fi setup: cancel, timeout, try again\n");
  tapButton(screens::Button::WifiSetup);
  CHECK(goTo(screens::Button::Cancel) && tapButton(screens::Button::Cancel) && screen() == "panel" && net::portalState() == net::Portal::Closed,
        "Cancel closes the waiting hotspot, back to settings");
  tapButton(screens::Button::WifiSetup);
  runFor(301000);
  CHECK(net::portalState() == net::Portal::TimedOut, "the hotspot times out after 5 minutes");
  // The screen slept meanwhile: the first touch only wakes it.
  touchDown(120, 120);
  touchUp();
  settle();
  CHECK(screen() == "setup", "it wakes on the timed-out screen, got %s", screen().c_str());
  snap("setup_timeout");
  CHECK(find(screens::Button::TryAgain, c) && find(screens::Button::Back, c), "timed out offers Try again and Back");
  CHECK(tapButton(screens::Button::TryAgain) && net::portalState() == net::Portal::Waiting, "Try again reopens the hotspot");
  pressBoot();
  pressBoot();
  settle();
  CHECK(screen() == "face", "two BOOT presses: hotspot cancelled, then settings closed (got %s)", screen().c_str());

  printf("Valid clock, failed sync: the time stays\n");
  sim::syncOutcome = net::Fail::JoinFailed;
  openSettings();
  swipeLeft(); settle();
  tapButton(screens::Button::SyncNow);
  settle(1600);
  CHECK(net::lastResult() == net::Result::Failed && net::timeIsValid(), "the clock is still valid after a failed sync");
  snap("settings_sync_failed");
  pressBoot();
  settle(1200);
  bool warned = false;
  // The face is drawn from the real code: look for the no-time words in its last frame.
  {
    TFT_eSPI parent;
    TFT_eSprite f(&parent);
    f.createSprite(240, 240);
    FaceContext fc{};
    time_t now = hal::now();
    localtime_r(&now, &fc.now);
    fc.timeValid = net::timeIsValid();
    fc.sync = SyncView::Failed;
    drawFaceStatic(f, currentFaceForTest(), fc);
    for (const auto &t : f.textLog) warned |= t.text == "NO TIME";
  }
  CHECK(!warned, "the face shows the time, not a warning");
  snap("face_valid_failed");
  sim::syncOutcome = net::Fail::None;

  printf("Sleep and wake\n");
  runFor(15500);
  CHECK(screen() == "asleep", "screen sleeps after 15 s, got %s", screen().c_str());
  CHECK(ui::display().lastCommand == 0x10 && lastBacklight() == 0, "panel and backlight off");
  int faceBefore = settings.face;
  uint64_t pushedBefore = ui::display().pixelsPushed;
  sim::backlightLog.clear();
  touchDown(200, 120);
  CHECK(ui::display().lastCommand == 0x11, "panel woken");
  CHECK(ui::display().pixelsPushed > pushedBefore, "time drawn before the light comes up");
  CHECK(lastBacklight() <= 0 || sim::backlightLog.size() <= 1, "backlight still low at first frame");
  moveTo(100, 120, 80);  // the waking touch swipes, but must not change face
  touchUp();
  settle();
  CHECK(settings.face == faceBefore, "the waking touch does not change face");
  bool rising = true;
  for (size_t i = 1; i < sim::backlightLog.size(); i++) rising &= sim::backlightLog[i].second >= sim::backlightLog[i - 1].second;
  uint32_t fadeMs = sim::backlightLog.empty() ? 0 : sim::backlightLog.back().first - sim::backlightLog.front().first;
  CHECK(rising && lastBacklight() == backlightLevel(), "backlight fades up to the set level");
  CHECK(fadeMs <= motion::WAKE_MS + 20, "fade takes %u ms", fadeMs);
  CHECK(screen() == "face", "wakes on the face, not the startup sequence, got %s", screen().c_str());

  printf("Wake from settings: on the face, and the waking tap does nothing\n");
  openSettings();
  int lightW = settings.brightnessLevel;
  runFor(15500);
  CHECK(screen() == "asleep", "asleep, got %s", screen().c_str());
  tap(120, 78);  // where Brightness was
  settle();
  CHECK(screen() == "face" && settings.brightnessLevel == lightW, "wakes on the face; the tap changed nothing");
  runFor(15500);
  int faceB = settings.face;
  pressBoot();
  settle();
  CHECK(screen() == "face" && settings.face == faceB, "BOOT wakes without changing the face");

  printf("Radar sweep runs on entry, then stops\n");
  settings.face = FACE_HOLO;
  pressBoot();
  runFor(motion::FACE_MS + 300);
  snap("radar_sweep");
  runFor(motion::SWEEP_MS);
  uint64_t p0 = ui::display().pixelsPushed;
  runFor(500);
  uint64_t perHalfSecond = ui::display().pixelsPushed - p0;
  CHECK(perHalfSecond <= 240 * 240, "idle face pushes at most one frame per second (%llu px in 0.5 s)",
        (unsigned long long)perHalfSecond);

  printf("Reduced motion\n");
  settings.reducedMotion = true;
  int face6 = settings.face;
  touchDown(190, 120);
  moveTo(120, 120, 80);
  CHECK(screen() == "face", "no drag-follow with reduced motion, got %s", screen().c_str());
  touchUp();
  CHECK(settings.face == (face6 + 1) % FACE_COUNT && screen() == "face", "swipe cuts to the next face");
  swipeUp();
  CHECK(screen() == "panel", "panel opens at once, got %s", screen().c_str());
  swipeLeft();
  CHECK(screen() == "panel" && ui::page() == 1, "pages change at once (%s, page %d)", screen().c_str(), ui::page() + 1);
  tapButton(screens::Button::SyncNow);
  px0 = ui::display().pixelsPushed;
  runFor(600);
  uint64_t pushed = ui::display().pixelsPushed - px0;
  CHECK(pushed <= 240 * 240, "while syncing, the still symbol needs no spinner frames (%llu px in 0.6 s)", (unsigned long long)pushed);
  snap("settings_syncing_reduced");
  settle(1500);
  pressBoot();
  CHECK(screen() == "face", "and closes at once");
  settings.reducedMotion = false;

  printf("Warm restart skips the startup sequence\n");
  sim::cold = false;
  ui::begin();
  CHECK(screen() == "face", "after a crash restart the face shows at once, got %s", screen().c_str());
  settle(2500);  // let the restart's sync finish before wiping the clock

  printf("Low memory: no spare frame, so cuts instead of slides\n");
  spriteBudget16 = 0;
  ui::begin();
  settle(2500);
  int faceL = settings.face;
  swipeLeft();
  CHECK(screen() == "face" && settings.face == (faceL + 1) % FACE_COUNT, "the face changes with a cut");
  swipeUp();
  CHECK(screen() == "panel", "settings open at once, got %s", screen().c_str());
  swipeLeft();
  CHECK(screen() == "panel" && ui::page() == 1, "pages change with a cut");
  goTo(screens::Button::Motion);
  bool lessMotion = settings.reducedMotion;  // as loaded from the simulated flash by begin()
  CHECK(tapButton(screens::Button::Motion) && settings.reducedMotion != lessMotion, "controls work");
  tapButton(screens::Button::Motion);
  snap("lowmem_settings");
  pressBoot();
  spriteBudget16 = -1;
  ui::begin();
  settle(2500);

  printf("First start with no Wi-Fi saved\n");
  sim::cold = true;
  sim::wifiSaved = false;
  sim::clockSet = false;
  ui::begin();
  runFor(motion::BOOT_MS + 400);
  CHECK(screen() == "setup", "opens Wi-Fi setup after the startup sequence, got %s", screen().c_str());
  pressBoot();
  CHECK(screen() == "panel", "BOOT cancels the hotspot and shows settings, got %s", screen().c_str());
  CHECK(find(screens::Button::SetTime, c), "with no clock and no Wi-Fi, leaving setup lands on Set time");
  pressBoot();
  settle();
  runFor(1000);
  snap("face_notime");
  swipeUp();
  settle();
  CHECK(find(screens::Button::SetTime, c), "with no time and no Wi-Fi, swipe up lands on Set time");

  printf("Set time by hand, with no Wi-Fi\n");
  CHECK(tapButton(screens::Button::SetTime) && screen() == "settime" && ui::page() == 0, "Set time opens on the hour page, got %s", screen().c_str());
  snap("settime_hour");
  for (int i = 0; i < 3; i++) tapButton(screens::Button::Plus);
  tapButton(screens::Button::Minus);
  find(screens::Button::Plus, c);
  touchDown(c.hit.x + c.hit.w / 2, c.hit.y + c.hit.h / 2);
  runFor(450 + 120 * 3 + 30);  // the first repeat at 450 ms, then every 120 ms
  touchUp();
  runFor(50);
  // Started at 12 from the build date: +3, -1, then four repeats while held and none on release.
  swipeLeft();
  settle();
  CHECK(ui::page() == 1, "swipe left goes to minutes");
  tapButton(screens::Button::Minus);  // 00 wraps to 59 without touching the hour
  tapButton(screens::Button::Plus);
  tapButton(screens::Button::Plus);   // 01
  swipeLeft(); settle();               // day
  swipeLeft(); settle();               // month
  swipeLeft(); settle();               // year
  swipeLeft(); settle();               // save
  CHECK(find(screens::Button::SaveTime, c), "the last page saves");
  snap("settime_save");
  int faceT = settings.face;
  CHECK(tapButton(screens::Button::SaveTime) && screen() == "face", "Save returns to the face, got %s", screen().c_str());
  CHECK(net::timeIsValid() && net::clockSetByHand(), "the clock is now set, and marked as set by hand");
  {
    time_t now = hal::now();
    tm lt;
    localtime_r(&now, &lt);
    CHECK(lt.tm_hour == 18 && lt.tm_min == 1, "the clock reads the time that was set (%02d:%02d, expected 18:01)", lt.tm_hour, lt.tm_min);
  }
  CHECK(settings.face == faceT, "no face change");
  runFor(1100);
  snap("face_time_by_hand");
  {
    TFT_eSPI parent;
    TFT_eSprite f(&parent);
    f.createSprite(240, 240);
    FaceContext fc{};
    time_t now = hal::now();
    localtime_r(&now, &fc.now);
    fc.timeValid = net::timeIsValid();
    fc.sync = SyncView::Never;
    fc.noWifi = true;
    drawFaceStatic(f, currentFaceForTest(), fc);
    bool warned = false;
    for (const auto &t : f.textLog) warned |= t.text == "NO TIME";
    CHECK(!warned, "the face shows the time set by hand");
  }
  int syncsNoWifi = sim::syncsStarted;
  runFor(40UL * 60 * 1000);
  CHECK(sim::syncsStarted == syncsNoWifi, "with no Wi-Fi saved, no resyncs are tried in the background");

  printf("Set time: Back leaves without saving\n");
  touchDown(120, 120); touchUp(); settle();  // wake
  openSettings();
  CHECK(goTo(screens::Button::SetTime) && tapButton(screens::Button::SetTime), "Set time opens again");
  tapButton(screens::Button::Plus);
  time_t before0 = hal::now();
  pressBoot();
  CHECK(screen() == "panel" && find(screens::Button::SetTime, c), "BOOT goes back to the settings page with Set time");
  CHECK(llabs((long long)(hal::now() - before0)) <= 1, "and the clock was not changed");
  pressBoot();
  settle();

  printf("Settings writes are batched\n");
  int saves = sim::settingsSaves;
  sim::clockSet = true;
  sim::wifiSaved = true;
  for (int i = 0; i < 4; i++) {
    runFor(60);
    swipe(200, 120, 110, 120, 70);
  }
  settle(300);
  CHECK(sim::settingsSaves == saves, "no flash write during rapid swipes");
  runFor(2500);
  CHECK(sim::settingsSaves == saves + 1, "one write after they stop (%d)", sim::settingsSaves - saves);
  openSettings();
  saves = sim::settingsSaves;
  tapButton(screens::Button::Brightness);
  tapButton(screens::Button::Brightness);
  tapButton(screens::Button::TimeFormat);
  runFor(500);
  CHECK(sim::settingsSaves == saves, "no flash write right after settings taps");
  runFor(2000);
  CHECK(sim::settingsSaves == saves + 1, "one write for three taps (%d)", sim::settingsSaves - saves);
  tapButton(screens::Button::TimeFormat);
  pressBoot();
  settle(2600);

  raiseScenarios();
}

void migration() {
  printf("Saved settings from v0.7.0 load safely\n");
  sim::flash.clear();
  // Exactly the keys v0.7.0 writes: no version, no large text.
  Preferences p;
  p.begin("watch", false);
  p.putString("tz", "GMT0BST,M3.5.0/1,M10.5.0");
  p.putBool("h12", true);
  p.putUChar("face", 3);
  p.putUChar("blevel", 1);
  p.putBool("lessmo", true);
  p.end();
  settings = Settings{};
  settings.largeText = true;  // must be replaced by the default, not kept from memory
  loadSettings();
  CHECK(!strcmp(settings.tz.c_str(), "GMT0BST,M3.5.0/1,M10.5.0") && settings.use12h && settings.face == 3 &&
            settings.brightnessLevel == 1 && settings.reducedMotion,
        "all v0.7.0 choices are kept");
  CHECK(!settings.largeText, "large text starts off");
  CHECK(settings.raiseToWake && settings.screenFacing == 0, "raise to wake starts on, screen direction not yet known");
  saveSettings();
  p.begin("watch", true);
  CHECK(p.getUChar("ver", 0) == SETTINGS_VERSION && p.isKey("bigtext"), "the next save records version %d", SETTINGS_VERSION);
  p.end();
  p.begin("watch", false);
  p.putUChar("blevel", 9);  // damaged or from a future build
  p.end();
  loadSettings();
  CHECK(settings.brightnessLevel < BRIGHTNESS_LEVEL_COUNT, "an out-of-range brightness is brought back into range");
  sim::flash.clear();
  settings = Settings{};
  loadSettings();
  CHECK(settings.brightnessLevel == 2 && !settings.largeText && !settings.use12h, "a blank watch gets the defaults");
}
}  // namespace

Face currentFaceForTest() { return (Face)(settings.face % FACE_COUNT); }

int main(int argc, char **argv) {
  outDir = argc > 1 ? argv[1] : ".";
  setenv("TZ", "UTC0", 1);
  tzset();
  std::string reportPath = std::string(outDir) + "/report.md";
  report = fopen(reportPath.c_str(), "w");
  screenVariants();
  contrastAudit();
  migration();
  settings = Settings{};
  sim::flash.clear();
  timeSteps();
  scenarios();
  fclose(report);
  printf("%d of %d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
