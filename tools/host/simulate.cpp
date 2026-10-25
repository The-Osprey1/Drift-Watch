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
#include "orient.h"
#include "phone.h"
#include "power.h"
#include "ble.h"
#include "battery.h"
#include "raise.h"
#include "timekeep.h"
#include <math.h>
#include "screens.h"
#include "settings.h"
#include "serial_cmd.h"
#include "sim.h"
#include "theme.h"
#include "ui.h"
#include "sim_drive.h"

Face currentFaceForTest();

namespace {
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
  v.screenTurn = 1;
  v.touchChecked = true;
  v.bleName = "Drift-1A2B";
  v.bleSecondsLeft = 172;
  v.phoneSsid = "";
  v.batteryHours = -1;
  v.syncSource = net::Source::Wifi;
  v.syncAgo = "3 h ago";
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

    // v0.10.0: phone sync in every phase, the touch check, and the new details lines.
    struct P { const char *name; ble::State st; bool gotTime, wifi; const char *error; };
    P phones[] = {
        {"waiting", ble::State::Waiting, false, false, nullptr},
        {"connected", ble::State::Connected, false, false, nullptr},
        {"refused", ble::State::Connected, false, false, "network name is empty or too long"},
        {"timeset", ble::State::Connected, true, false, nullptr},
        {"timeset_wifi", ble::State::Connected, true, true, nullptr},
        {"closed", ble::State::Off, false, true, nullptr},
        {"failed", ble::State::Failed, false, false, nullptr},
    };
    for (const P &ph : phones) {
      screens::View w = v;
      w.screen = screens::Screen::PhoneSync;
      w.ble = ph.st;
      w.phoneGotTime = ph.gotTime;
      w.phoneWifiSaved = ph.wifi;
      w.phoneSsid = ph.wifi ? "A very long network name here" : "";
      w.phoneError = ph.error;
      bad += renderAll(s, w, std::string("phone_") + ph.name);
    }
    for (int step = 0; step < 2; step++)
      for (screens::CheckResult r : {screens::CheckResult::Tapping, screens::CheckResult::Matched, screens::CheckResult::NoMatch}) {
        if (step == 0 && r != screens::CheckResult::Tapping) continue;
        screens::View w = v;
        w.screen = screens::Screen::TouchCheck;
        w.checkStep = step;
        w.checkResult = r;
        static const char *rn[] = {"tap", "matched", "nomatch"};
        bad += renderAll(s, w, std::string("touchcheck_") + std::to_string(step + 1) + "_" + rn[(int)r]);
      }
    {
      screens::View w = v;
      w.screen = screens::Screen::Details;
      w.batteryHours = 61.4f;
      w.driftPpb = -48000;
      w.syncSource = net::Source::Phone;
      w.syncAgo = "12 days ago";
      w.imuLine1 = 12;
      w.imuLine2 = 40211;
      w.touchChecked = false;
      w.screenTurn = 2;
      bad += renderAll(s, w, "details_v010");
      std::string words = wordsOf(s, w);
      CHECK(says(words, "About 2 days 13 h left") && says(words, "12 days ago"), "details show the time left and the age of the last sync");
      w.charging = true;
      bad += renderAll(s, w, "details_charging");
    }
    {
      screens::View w = v;
      w.night = 1;
      w.touchChecked = false;
      w.screenTurn = 2;
      bad += renderAll(s, w, "settings_night");
      w.use12h = true;
      bad += renderAll(s, w, "settings_night_12h");
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

// Taps the touch check's two dots where they are drawn. With `snapSecond`, saves the second dot.
void doTouchCheck(bool snapSecond = false) {
  for (int i = 0; i < 2; i++) {
    if (i && snapSecond) snap("touch_check_2");
    tap(orient::TARGET_X[i], orient::TARGET_Y[i]);
    runFor(50);
  }
  runFor(1600);  // the result shows, then the check closes
}

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

// Gravity for a wrist resting screen up, rolled `deg` degrees about the forearm.
Accel rolled(float deg) {
  float r = deg * 3.14159265f / 180;
  return {sinf(r), 0, cosf(r)};
}
// A quick twist out to `deg` and back, each way over `stepMs` x 2.
void twist(float deg, uint32_t stepMs = 80) {
  wrist({{stepMs, rolled(deg / 2)}, {stepMs, rolled(deg)}, {stepMs, rolled(deg / 2)}, {300, FLAT}});
}

// Rolls smoothly from `from` to `to` degrees over `ms`, in 40 ms steps.
void rollRamp(float from, float to, uint32_t ms) {
  for (uint32_t t = 40; t <= ms; t += 40) wrist({{40, rolled(from + (to - from) * t / ms)}});
}

void twistScenarios() {
  // Twists here go to 60 degrees, so the screen stays partly up and raise to wake (which needs
  // the screen turned away first) is not what wakes it.
  printf("Wrist twist to wake\n");
  sim::accel = FLAT;
  runFor(16000);
  CHECK(screen() == "asleep", "asleep on the desk, got %s", screen().c_str());
  wrist({{1000, FLAT}});
  twist(60);
  CHECK(screen() == "face", "a quick twist out and back wakes it, got %s", screen().c_str());
  runFor(16000);

  printf("Wrist twist: what does not wake it\n");
  wrist({{1000, FLAT}});
  rollRamp(0, 50, 800);  // to 50 degrees: the screen stays partly up, so this is not a raise either
  rollRamp(50, 0, 800);
  wrist({{300, FLAT}});
  CHECK(screen() == "asleep", "a slow turn out and back (about 60 degrees a second) does not");
  wrist({{1000, FLAT}, {80, rolled(35)}, {80, rolled(70)}, {2000, rolled(70)}});
  CHECK(screen() == "asleep", "turning and staying turned does not");
  rollRamp(70, 0, 2400);  // back slowly, or coming back up would count as a raise
  wrist({{1000, FLAT}});
  twist(20);
  CHECK(screen() == "asleep", "a small wobble does not");
  for (int i = 0; i < 6; i++) wrist({{80, rolled(50)}, {80, rolled(-10)}});  // arm swinging: never still first
  wrist({{300, FLAT}});
  CHECK(screen() == "asleep", "continuous swinging does not");
  wrist({{1000, HANGING}, {80, {0.6f, -0.8f, 0}}, {80, {0.9f, -0.4f, 0}}, {80, {0.6f, -0.8f, 0}}, {300, HANGING}});
  CHECK(screen() == "asleep", "a hanging arm swung up and back does not");
  for (uint32_t t = 40; t <= 2400; t += 40) {  // slowly back to the desk, so it is not a raise
    float f = t / 2400.0f, x = 0, y = -(1 - f), z = f, n = sqrtf(y * y + z * z);
    wrist({{40, {x, y / n, z / n}}});
  }
  settings.twistToWake = false;
  wrist({{1000, FLAT}});
  twist(60);
  CHECK(screen() == "asleep", "with twist off in the setting, a twist does nothing");
  settings.twistToWake = true;
  settings.raiseToWake = false;
  wrist({{1000, FLAT}});
  twist(60);
  CHECK(screen() == "face", "with raise off, twist still works, got %s", screen().c_str());
  settings.raiseToWake = true;
  runFor(16000);
  sim::accel = FLAT;
}

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

  printf("Wake gesture setting\n");
  openSettings();
  CHECK(settings.raiseToWake && settings.twistToWake, "raise and twist both start on");
  CHECK(goTo(screens::Button::RaiseWake) && tapButton(screens::Button::RaiseWake) && settings.raiseToWake && !settings.twistToWake,
        "one tap: raise only");
  tapButton(screens::Button::RaiseWake);
  CHECK(!settings.raiseToWake && settings.twistToWake, "then twist only");
  tapButton(screens::Button::RaiseWake);
  CHECK(!settings.raiseToWake && !settings.twistToWake, "then off");
  tapButton(screens::Button::RaiseWake);
  CHECK(settings.raiseToWake && settings.twistToWake, "and back to both");
  pressBoot();
  settle();

  twistScenarios();

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

void v010Scenarios();
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
  CHECK(screen() == "touchcheck", "the first start with the screen turned checks touch, got %s", screen().c_str());
  snap("touch_check_1");
  CHECK(settings.screenTurn == DISPLAY_ROTATION && ui::display().rotation == DISPLAY_ROTATION,
        "the screen starts turned a quarter turn clockwise (turn %d)", ui::display().rotation);
  doTouchCheck(true);
  CHECK(touchChecked() && settings.touchMap == orient::expectedTouchMap(DISPLAY_ROTATION),
        "the touch check finds the expected map for that turn (map %d)", settings.touchMap);
  CHECK(screen() == "face", "then on to the face, got %s", screen().c_str());
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
  // The release hands over to a spring, which takes a little longer than FACE_MS to come to rest.
  CHECK(sim::clockMs - released <= 320, "settles %u ms after the last release",
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
  int lastPage = screens::SETTINGS_ITEMS / 2 - 1;
  for (int i = 3; i <= lastPage; i++) {
    swipeLeft();
    settle();
    CHECK(ui::page() == i, "and page %d (page %d)", i + 1, ui::page() + 1);
  }
  swipeLeft();
  settle();
  CHECK(ui::page() == lastPage && screen() == "panel", "no page after the last; it does not wrap or leave settings");
  swipeRight();
  settle();
  CHECK(ui::page() == lastPage - 1, "swipe right goes back a page");
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
  goTo(screens::Button::WifiSetup);
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
  goTo(screens::Button::SyncNow);
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

  printf("Radar beam turns steadily\n");
  settings.face = FACE_HOLO;
  pressBoot();
  runFor(motion::FACE_MS + 300);
  CHECK(currentFaceForTest() == FACE_RADAR, "on the radar face");
  float a0 = ui::beamAngle();
  runFor(900);
  float moved = fmodf(ui::beamAngle() - a0 + 360, 360);
  CHECK(fabsf(moved - 90) < 8, "the beam turns about 90 degrees in 0.9 s, one turn in 3.6 s (moved %.1f)", moved);
  uint64_t p0 = ui::display().pixelsPushed;
  runFor(1000);
  uint64_t frames = (ui::display().pixelsPushed - p0) / (240 * 240);
  CHECK(frames >= 20 && frames <= 31, "the beam is drawn at 20 to 30 frames a second (%llu)", (unsigned long long)frames);
  snap("radar_beam");
  runFor(1700);
  snap("radar_beam_b");

  printf("Radar at low battery: the beam steps with the seconds\n");
  sim::batteryMv = 3600;  // 15%
  runFor(10500);
  p0 = ui::display().pixelsPushed;
  runFor(3000);
  frames = (ui::display().pixelsPushed - p0) / (240 * 240);
  CHECK(frames <= 4, "about one frame a second at low battery (%llu in 3 s)", (unsigned long long)frames);
  time_t nowT = hal::now();
  tm nowTm;
  localtime_r(&nowT, &nowTm);
  CHECK(fabsf(ui::beamAngle() - nowTm.tm_sec * 6) < 0.5f, "the beam sits on the seconds dot (%.1f vs %d)", ui::beamAngle(), nowTm.tm_sec * 6);
  snap("radar_low_battery");
  sim::batteryMv = 3950;
  runFor(10500);

  printf("Other faces stay still between seconds\n");
  if (screen() != "face") {  // the screen slept while the battery recovered: wake it first
    pressBoot();
    settle();
  }
  CHECK(settings.face == FACE_RADAR, "still on the radar face (face %d, %s)", settings.face, screen().c_str());
  pressBoot();  // on to Phantom, which moves too
  runFor(motion::FACE_MS + 300);
  CHECK(settings.face == FACE_PHANTOM, "on Phantom (face %d)", settings.face);
  p0 = ui::display().pixelsPushed;
  runFor(1000);
  frames = (ui::display().pixelsPushed - p0) / (240 * 240);
  CHECK(frames >= 25 && frames <= 31, "Phantom is drawn at about 30 frames a second (%llu)", (unsigned long long)frames);
  pressBoot();  // Phantom is last: on to Digital
  runFor(motion::FACE_MS + 300);
  CHECK(settings.face == FACE_DIGITAL, "on Digital (face %d)", settings.face);
  p0 = ui::display().pixelsPushed;
  runFor(500);
  uint64_t perHalfSecond = ui::display().pixelsPushed - p0;
  CHECK(perHalfSecond <= 240 * 240, "an idle face pushes at most one frame per second (%llu px in 0.5 s)",
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
  settle();
  {
    // With no cached dial each radar frame is drawn from scratch, so the beam steps with the seconds.
    for (int i = 0; i < FACE_COUNT && settings.face != FACE_RADAR; i++) pressBoot();
    runFor(500);
    uint64_t p0 = ui::display().pixelsPushed;
    runFor(3000);
    uint64_t frames = (ui::display().pixelsPushed - p0) / (240 * 240);
    CHECK(settings.face == FACE_RADAR && frames <= 4, "low memory: the radar beam steps once a second (%llu frames in 3 s)", (unsigned long long)frames);
  }
  spriteBudget16 = -1;
  ui::begin();
  settle(2500);

  printf("First start with no Wi-Fi saved\n");
  sim::cold = true;
  sim::wifiSaved = false;
  sim::clockSet = false;
  ui::begin();
  runFor(motion::BOOT_MS + 400);
  // Never trapped in the hotspot: the watch starts on the face, and setup is only opened by hand.
  CHECK(screen() == "face", "starts on the face, not in Wi-Fi setup, got %s", screen().c_str());
  CHECK(net::portalState() == net::Portal::Closed, "no hotspot is opened on its own");
  runFor(1000);
  snap("face_notime");
  for (int f = 0; f < FACE_COUNT; f++) {
    TFT_eSPI parent;
    TFT_eSprite fs(&parent);
    fs.createSprite(240, 240);
    FaceContext fc{};
    time_t now = hal::now();
    localtime_r(&now, &fc.now);
    fc.timeValid = false;
    fc.sync = SyncView::Never;
    fc.noWifi = true;
    fc.beamDeg = -1;
    drawFaceStatic(fs, (Face)f, fc);
    drawFaceLive(fs, (Face)f, fc);
    bool noTime = false, setTime = false;
    for (const auto &t : fs.textLog) {
      noTime |= t.text == "NO TIME";
      setTime |= t.text == "SET TIME";
    }
    CHECK(!noTime && setTime, "face %d with no clock says SET TIME, never NO TIME", f);
  }
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
  v010Scenarios();
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

// Interface flows added in v0.10.0: the touch check when the panel turns the other way, the
// screen turn setting, the BOOT hold, phone sync, night mode and the Set time starting point.
void v010Scenarios() {
  screens::Control c;
  auto wake = [] {
    if (screen() == "asleep") {
      pressBoot();
      settle();
    }
  };

  printf("Touch check: a panel that turns the other way is lined up by two taps\n");
  sim::panelTurnsClockwise = false;
  sim::touchMount = 0;
  sim::cold = false;
  settings.touchCheckedTurn = TOUCH_NOT_CHECKED;
  saveSettings();
  ui::begin();
  settle();
  CHECK(screen() == "touchcheck", "with touch not checked, the watch starts on the touch check, got %s", screen().c_str());
  snap("touch_check_1");
  doTouchCheck();
  CHECK(touchChecked() && settings.touchMap == 1, "the check finds map 1 for a panel that turns anticlockwise (map %d)", settings.touchMap);
  CHECK(screen() == "face", "then the face, got %s", screen().c_str());
  openSettings();
  CHECK(screen() == "panel", "a swipe up now opens settings, got %s", screen().c_str());
  bool h12 = settings.use12h;
  CHECK(tapButton(screens::Button::TimeFormat) && settings.use12h != h12, "and taps land on the right control");
  tapButton(screens::Button::TimeFormat);

  printf("Touch check: a tap that fits no map is asked again\n");
  goTo(screens::Button::TouchCheck);
  CHECK(tapButton(screens::Button::TouchCheck) && screen() == "touchcheck", "Touch check opens from settings, got %s", screen().c_str());
  tap(30, 200);
  runFor(50);
  tap(32, 202);
  runFor(50);
  snap("touch_check_nomatch");
  CHECK(settings.touchMap == 1, "a bad pair of taps changes nothing");
  runFor(1600);
  CHECK(screen() == "touchcheck", "and the check starts again, got %s", screen().c_str());
  pressBoot();
  CHECK(screen() == "panel" && find(screens::Button::TouchCheck, c), "BOOT leaves it, back on the Touch check control");

  printf("Screen turn setting\n");
  goTo(screens::Button::ScreenTurn);
  uint8_t turn = settings.screenTurn;
  CHECK(tapButton(screens::Button::ScreenTurn) && settings.screenTurn == (turn + 1) % 4, "Screen turn steps to the next turn");
  CHECK(ui::display().rotation == settings.screenTurn, "and the panel is turned (rotation %d)", ui::display().rotation);
  CHECK(screen() == "touchcheck" && !touchChecked(), "touch is checked again for the new turn, got %s", screen().c_str());
  doTouchCheck();
  CHECK(touchChecked() && screen() == "panel", "checked, back on settings (%s)", screen().c_str());
  for (int i = 0; i < 3; i++) {
    goTo(screens::Button::ScreenTurn);
    tapButton(screens::Button::ScreenTurn);
    doTouchCheck();
  }
  CHECK(settings.screenTurn == turn && touchChecked() && settings.touchMap == 1, "four steps come back to the first turn and map");
  goTo(screens::Button::ScreenTurn);
  tapButton(screens::Button::ScreenTurn);
  pressBoot();
  CHECK(screen() == "panel" && !touchChecked() && activeTouchMap() == orient::expectedTouchMap(settings.screenTurn),
        "BOOT skips the check and the expected map is used (%d)", activeTouchMap());
  goTo(screens::Button::ScreenTurn);
  for (int i = 0; i < 3; i++) {  // the expected map is wrong for this panel, so tap through the raw positions
    screens::Control st{};
    CHECK(find(screens::Button::ScreenTurn, st), "Screen turn is on this page (%d)", i);
    tap(st.hit.x + st.hit.w / 2, st.hit.y + st.hit.h / 2);
    if (screen() == "touchcheck") doTouchCheck();
  }
  CHECK(settings.screenTurn == turn && touchChecked() && settings.touchMap == 1, "back to the first turn, checked (turn %d, map %d)", settings.screenTurn, settings.touchMap);
  pressBoot();
  settle();

  printf("Holding BOOT for 4 s opens the touch check from anywhere\n");
  wake();
  pressBoot(4200);
  CHECK(screen() == "touchcheck", "a 4 s hold opens the touch check, got %s", screen().c_str());
  doTouchCheck();
  CHECK(screen() == "face" && touchChecked(), "and returns to the face, got %s", screen().c_str());
  sim::panelTurnsClockwise = true;
  settings.touchMap = orient::expectedTouchMap(settings.screenTurn);
  saveSettings();

  printf("Phone sync\n");
  wake();
  sim::clockSet = false;
  sim::wifiSaved = false;
  sim::bleLines.clear();
  int opens = sim::bleOpens;
  openSettings();
  CHECK(goTo(screens::Button::PhoneSync) && tapButton(screens::Button::PhoneSync) && screen() == "phone",
        "Phone sync opens, got %s", screen().c_str());
  CHECK(sim::bleOpens == opens + 1 && ble::on(), "Bluetooth is on only now");
  snap("phone_waiting");
  int dozesBefore = sim::lowPowerDozes;
  runFor(30000);
  CHECK(screen() == "phone", "the screen stays on while the phone is found, got %s", screen().c_str());
  CHECK(sim::lowPowerDozes == dozesBefore, "and there is no light sleep while Bluetooth is on");
  sim::bleConnected = true;
  runFor(300);
  snap("phone_connected");
  sim::bleLines = {"Hello"};
  runFor(300);
  snap("phone_refused");
  CHECK(strstr(simBleStatus(), "reply=unknown line"), "a refused line is answered with the reason: %s", simBleStatus());
  sim::bleLines = {"N=Europe/London", "Z=GMT0BST,M3.5.0/1,M10.5.0", "T=1782903600.5"};
  runFor(300);
  CHECK(net::timeIsValid() && hal::now() >= 1782903600 && hal::now() < 1782903603, "the phone set the clock (%ld)", (long)hal::now());
  CHECK(settings.tz == "GMT0BST,M3.5.0/1,M10.5.0" && !strcmp(net::zoneName(), "Europe/London"), "and the time zone");
  CHECK(strstr(simBleStatus(), "reply=ok") && strstr(simBleStatus(), "time=17829036"), "the phone can read the result: %s", simBleStatus());
  int syncs = sim::syncsStarted;
  sim::bleLines = {"S=HomeNet", "P=secret words", "J"};
  runFor(300);
  CHECK(sim::wifiSaved && !strcmp(net::ssid(), "HomeNet") && sim::syncsStarted == syncs + 1, "Wi-Fi from the phone is saved and joined");
  snap("phone_time_set");
  CHECK(ui::page() == 0, "the time set screen starts on its first page (page %d)", ui::page() + 1);
  CHECK(goTo(screens::Button::Done), "the screen says the time is set and offers Done");
  tapButton(screens::Button::Done);
  CHECK(screen() == "face" && !ble::on(), "Done turns Bluetooth off, back to the face (%s)", screen().c_str());
  settle(1500);

  printf("Phone sync: Cancel, the 3 minute window, and no Bluetooth\n");
  openSettings();
  goTo(screens::Button::PhoneSync);
  tapButton(screens::Button::PhoneSync);
  goTo(screens::Button::Cancel);
  tapButton(screens::Button::Cancel);
  CHECK(screen() == "panel" && !ble::on() && find(screens::Button::PhoneSync, c), "Cancel turns Bluetooth off, back on its settings page");
  sim::bleConnected = false;
  tapButton(screens::Button::PhoneSync);
  runFor(ble::WINDOW_MS + 2000);
  CHECK(!ble::on(), "Bluetooth turns itself off after 3 minutes");
  wake();
  CHECK(screen() == "phone" && goTo(screens::Button::PhoneSync), "the screen says it closed and offers to open again (%s)", screen().c_str());
  snap("phone_closed");
  pressBoot();
  settle();
  sim::bleStarts = false;
  goTo(screens::Button::PhoneSync);
  tapButton(screens::Button::PhoneSync);
  snap("phone_failed");
  CHECK(screen() == "phone" && goTo(screens::Button::Back), "when Bluetooth does not start, the screen says so");
  sim::bleStarts = true;
  pressBoot();
  pressBoot();
  settle(1500);

  printf("Night mode turns raise to wake off\n");
  {
    time_t now = hal::now();
    tm lt;
    localtime_r(&now, &lt);
    // A night window around the current hour: 22-07 or 00-08 always covers 3 o'clock, so move the clock there.
    lt.tm_hour = 3;
    lt.tm_min = 0;
    hal::setTime(mktime(&lt));
  }
  settings.raiseToWake = true;
  settings.night = 1;
  sim::accel = FLAT;
  runFor(16000);
  CHECK(screen() == "asleep", "asleep, got %s", screen().c_str());
  wrist({{1000, HANGING}, {400, LOOKING}});
  CHECK(screen() == "asleep", "at 3 at night, raising the wrist does not wake it");
  pressBoot();
  CHECK(screen() == "face", "BOOT still wakes it, got %s", screen().c_str());
  settings.night = 0;
  runFor(16000);
  wrist({{1000, HANGING}, {400, LOOKING}});
  CHECK(screen() == "face", "with night mode off it wakes again, got %s", screen().c_str());
  runFor(16000);

  printf("Set time after a power loss starts from the last known time\n");
  wake();
  settings.lastKnownTime = 1798761600LL - 3600 * 5 - 60 * 17;  // 2026-12-31 18:43 UTC
  sim::clockSet = false;
  openSettings();
  goTo(screens::Button::SetTime);
  tapButton(screens::Button::SetTime);
  snap("settime_from_known");
  // editTime is shown on the hour page: read it back by saving straight away.
  for (int i = 0; i < 5; i++) {
    swipeLeft();
    settle();
  }
  tapButton(screens::Button::SaveTime);
  {
    time_t now = hal::now();
    tm lt;
    gmtime_r(&now, &lt);
    CHECK(lt.tm_year + 1900 == 2026 && lt.tm_mon == 11 && lt.tm_mday == 31 && lt.tm_hour == 18 && lt.tm_min == 43,
          "Set time started at the last known time (%04d-%02d-%02d %02d:%02d)", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
  }
  settle(1500);
}

// Pure logic added in v0.10.0: screen turn maps, the touch check solver, phone lines, drift
// learning, the battery estimate and the wrist gestures.
void logicChecks() {
  printf("Screen turn maps (v0.10.0)\n");
  bool inversesOk = true, turnsOk = true;
  for (uint8_t m = 0; m < orient::MAPS; m++)
    for (int x = 0; x < 240; x += 37)
      for (int y = 0; y < 240; y += 41) {
        int ax, ay, bx, by;
        orient::apply(m, x, y, ax, ay);
        orient::apply(orient::inverse(m), ax, ay, bx, by);
        inversesOk &= bx == x && by == y && ax >= 0 && ax < 240 && ay >= 0 && ay < 240;
      }
  // A quarter turn clockwise moves the top edge to the right edge.
  int qx, qy;
  orient::apply(1, 120, 0, qx, qy);
  turnsOk &= qx == 239 && qy == 120;
  orient::apply(3, 239, 120, qx, qy);
  turnsOk &= qx == 120 && qy == 0;
  CHECK(inversesOk, "every map is undone by its inverse and stays on the panel");
  CHECK(turnsOk, "map 1 turns a quarter clockwise and map 3 turns it back");
  CHECK(orient::expectedTouchMap(0) == 0 && orient::expectedTouchMap(1) == 3 && orient::expectedTouchMap(2) == 2 &&
            orient::expectedTouchMap(3) == 1,
        "expected touch maps for the four turns");

  printf("Touch check solver: every panel turn and every way the touch chip could be mounted\n");
  int solved = 0, cases = 0;
  for (uint8_t truth = 0; truth < orient::MAPS; truth++) {
    // Raw positions that map `truth` would take onto the dots, nudged by a sloppy finger.
    int rx[2], ry[2];
    for (int i = 0; i < 2; i++) {
      orient::apply(orient::inverse(truth), orient::TARGET_X[i], orient::TARGET_Y[i], rx[i], ry[i]);
      rx[i] += i ? -18 : 15;
      ry[i] += i ? 12 : -20;
    }
    orient::Solve r = orient::solveTouchMap(rx, ry);
    cases++;
    solved += r.ok && r.map == truth;
  }
  CHECK(solved == cases, "the solver finds the right map in %d of %d cases, with taps 25 px off", solved, cases);
  {
    int rx[2] = {120, 120}, ry[2] = {120, 120};  // both taps in the middle: no map fits both dots
    CHECK(!orient::solveTouchMap(rx, ry).ok, "two taps in the same place are refused");
  }

  printf("Phone lines\n");
  sim::clockSet = false;
  phone::begin();
  CHECK(!strcmp(phone::apply("T=1767225600.250"), "ok") && net::timeIsValid() && hal::now() == 1767225600,
        "T= sets the clock (%ld)", (long)hal::now());
  CHECK(net::lastSource() == net::Source::Phone && phone::session().gotTime, "and it counts as a phone sync");
  CHECK(strcmp(phone::apply("T=12345"), "ok") != 0, "a time before 2023 is refused");
  CHECK(strcmp(phone::apply("T=soon"), "ok") != 0, "T= with no number is refused");
  CHECK(!strcmp(phone::apply("Z=GMT0BST,M3.5.0/1,M10.5.0"), "ok") && settings.tz == "GMT0BST,M3.5.0/1,M10.5.0",
        "Z= sets the time zone rules");
  CHECK(!strcmp(phone::apply("N=Europe/London"), "ok") && !strcmp(net::zoneName(), "Europe/London"), "N= sets the zone name");
  CHECK(strcmp(phone::apply("J"), "ok") != 0, "J before S= is refused");
  CHECK(strcmp(phone::apply(("S=" + std::string(40, 'x')).c_str()), "ok") != 0,
        "a network name over 32 characters is refused");
  CHECK(strcmp(phone::apply("Hello"), "ok") != 0 && phone::session().error, "unknown lines are refused, with a reason");
  {
    char st[160];
    phone::status(st, sizeof st, 64);
    CHECK(strstr(st, "fw=" FW_VERSION) && strstr(st, "bat=64") && strstr(st, "time=1767225600"), "status tells the phone the firmware, time and battery: %s", st);
  }

  printf("Current Time Service writes\n");
  {
    char out[40];
    uint8_t ct[10] = {0xEA, 0x07, 3, 29, 1, 30, 5, 7, 0, 0};  // 2026-03-29 01:30:05
    CHECK(phone::fromCurrentTime(ct, sizeof ct, out, sizeof out) && !strcmp(out, "L=2026-03-29 01:30:05"), "Current Time becomes %s", out);
    uint8_t bad[10] = {0xEA, 0x07, 13, 1, 0, 0, 0, 0, 0, 0};
    CHECK(!phone::fromCurrentTime(bad, sizeof bad, out, sizeof out), "month 13 is refused");
    CHECK(!phone::fromCurrentTime(ct, 5, out, sizeof out), "a short write is refused");
    uint8_t lti[2] = {4, 4};  // UTC+1 plus one hour of summer time
    CHECK(phone::fromLocalTimeInfo(lti, 2, out, sizeof out) && !strcmp(out, "O=7200"), "Local Time Information becomes %s", out);
    uint8_t unknown[2] = {0x80, 0};
    CHECK(!phone::fromLocalTimeInfo(unknown, 2, out, sizeof out), "an unknown zone is ignored");
    phone::begin();
    CHECK(!strcmp(phone::apply("O=7200"), "ok") && !strcmp(phone::apply("L=2026-07-01 13:00:00"), "ok") &&
              hal::now() == 1782903600,  // 13:00 at UTC+2 is 11:00 UTC
          "O= then L= sets the right UTC time (%ld)", (long)hal::now());
    CHECK(phone::daysFromCivil(1970, 1, 1) == 0 && phone::daysFromCivil(2000, 3, 1) == 11017 && phone::daysFromCivil(2024, 2, 29) == 19782,
          "calendar arithmetic, including leap days");
  }

  printf("Clock drift learning\n");
  {
    const int64_t H = 3600LL * 1000;
    ClockTrim t;
    CHECK(!t.learn(1000 * H, 0), "the first trusted time only starts a span");
    CHECK(!t.learn(1002 * H, 5000) && t.ppb() == 0, "a span under 6 hours teaches nothing");
    CHECK(t.learn(1026 * H, 8640), "24 hours later, 8.64 s fast");
    CHECK(t.ppb() > 90000 && t.ppb() < 110000, "is learned as about 100 parts per million fast (%d ppb)", (int)t.ppb());
    int64_t added = 0;
    t.due(2000 * H);
    for (int i = 1; i <= 8640; i++) added += t.due(2000 * H + i * 10000LL);  // a day in 10 s steps
    CHECK(added < -8000 && added > -9300, "a day later the clock was slowed by %lld ms (8640 expected)", (long long)added);
    t.forget();
    CHECK(!t.learn(3000 * H, 20000) && t.ppb() > 90000, "after a hand setting the next sync starts a new span and keeps the rate");
    ClockTrim wild;
    wild.learn(0 + H, 0);
    wild.learn(25 * H, 3600LL * 1000);  // an hour off in a day: not drift
    CHECK(wild.ppb() == 0, "an error too big to be drift is ignored (%d ppb)", (int)wild.ppb());
  }

  printf("Battery time left\n");
  {
    battery::Estimate e;
    const uint32_t MIN = 60000;
    for (uint32_t m = 0; m <= 30; m++) e.add(m * MIN, 80);
    CHECK(e.hoursLeft() < 0, "still measuring at first");
    for (uint32_t m = 31; m <= 300; m++) e.add(m * MIN, 80 - (int)(m / 12));  // 5% an hour
    float h = e.hoursLeft();
    CHECK(h > 9 && h < 15, "falling 5%% an hour from about 55%%: about 11 hours left (%.1f)", h);
    CHECK(!e.charging(), "not charging");
    for (uint32_t m = 301; m <= 340; m++) e.add(m * MIN, 55 + (int)((m - 300) / 2));
    CHECK(e.charging(), "a rise means charging");
  }

  printf("Wrist down to sleep\n");
  {
    DropDetector d;
    uint32_t t = 1000;
    auto feed = [&](float z, uint32_t ms) {
      bool fired = false;
      for (uint32_t end = t + ms; t < end; t += 80) fired |= d.update(t, z, 1.0f);
      return fired;
    };
    CHECK(!feed(-0.5f, 1000), "not armed before a look: turning away does nothing");
    CHECK(!feed(0.9f, 400), "looking at it arms it");
    uint32_t awayAt = t;
    bool fired = false;
    uint32_t firedAt = 0;
    for (; t < awayAt + 1000 && !fired; t += 80)
      if (d.update(t, -0.3f, 1.0f)) { fired = true; firedAt = t; }
    CHECK(fired && firedAt - awayAt <= 400, "turning the screen away sleeps it within 400 ms (%u ms)", firedAt - awayAt);
    feed(0.9f, 400);
    d.touched(t);
    CHECK(!feed(-0.3f, 1000), "a touch puts it off");
    d.reset();
    feed(0.9f, 400);
    CHECK(!feed(0.1f + 0.15f, 1000), "a screen only tilted (still above the line) stays on");
  }

  printf("Raise to wake: a slow drift up is not a raise\n");
  {
    RaiseDetector r;
    uint32_t t = 0;
    bool woke = false;
    for (; t < 1000; t += 40) woke |= r.update(t, {0, -1, 0});
    // Turn up over 1.2 s at a steady 60 degrees a second: inside the time window, but no flick.
    Accel last{};
    for (int i = 0; i <= 30; i++, t += 40) {
      float a = (float)(i * 2.4f) * 3.14159f / 180;
      last = {0, -cosf(a), sinf(a)};
      woke |= r.update(t, last);
    }
    for (int i = 0; i < 15; i++, t += 40) woke |= r.update(t, last);  // held where the turn ended
    CHECK(!woke, "a steady 60 degrees a second does not wake it");
    woke |= r.update(t, {0.5f, last.y, last.z}), t += 40;  // a jolt while the wrist stays up
    for (int i = 0; i < 10; i++, t += 40) woke |= r.update(t, last);
    CHECK(!woke, "nor does a jolt afterwards, while the wrist stays up");
    r.reset();
    for (int i = 0; i < 25; i++, t += 40) woke |= r.update(t, {0, -1, 0});
    for (int i = 0; i <= 6; i++, t += 40) {
      float a = (float)(i * 10.5f) * 3.14159f / 180;
      last = {0, -cosf(a), sinf(a)};
      woke |= r.update(t, last);
    }
    for (int i = 0; i < 10; i++, t += 40) woke |= r.update(t, last);
    CHECK(woke && r.lastPeakRate() > RaiseDetector::FLICK_DPS, "a quick flick does (%.0f degrees a second)", r.lastPeakRate());
    bool shaken = false;
    r.reset();
    for (int i = 0; i < 25; i++, t += 40) r.update(t, {0, -1, 0});
    for (int i = 0; i < 20; i++, t += 40) shaken |= r.update(t, {0, -0.6f, 1.2f});  // 1.34 g: moving, not held
    CHECK(!shaken, "a screen that is up but being shaken does not wake it");
  }

  printf("Wrist twist detector\n");
  {
    TwistDetector t;
    uint32_t ms = 0;
    auto feed = [&](Accel a, uint32_t dur) {
      bool fired = false;
      for (uint32_t end = ms + dur; ms < end; ms += 80) fired |= t.update(ms, a, 1);
      return fired;
    };
    auto roll = [](float deg) { float r = deg * 3.14159265f / 180; return Accel{sinf(r), 0, cosf(r)}; };
    feed(roll(0), 1000);
    bool fired = feed(roll(35), 80) | feed(roll(70), 80) | feed(roll(35), 80) | feed(roll(0), 160);
    CHECK(fired && t.lastPeakRate() >= TwistDetector::TWIST_DPS, "a twist is seen (%.0f degrees a second)", t.lastPeakRate());
    fired = feed(roll(35), 80) | feed(roll(70), 80) | feed(roll(35), 80) | feed(roll(0), 160);
    CHECK(!fired, "a second twist straight away is not (it needs a still moment first)");
    feed(roll(0), 1200);
    fired = feed(roll(-35), 80) | feed(roll(-70), 80) | feed(roll(-35), 80) | feed(roll(0), 160);
    CHECK(fired, "after a pause, a twist the other way is seen too");
    TwistDetector down;
    ms = 0;
    bool d = false;
    for (int i = 0; i < 12; i++, ms += 80) d |= down.update(ms, roll(0), -1);  // screen facing down
    for (float a : {35.f, 70.f, 35.f, 0.f, 0.f}) { d |= down.update(ms, roll(a), -1); ms += 80; }
    CHECK(!d, "not with the screen facing down");
  }

  printf("Night mode\n");
  settings.night = 1;  // 22:00 to 07:00
  CHECK(nightNow(23) && nightNow(3) && nightNow(6) && !nightNow(7) && !nightNow(21), "22:00 to 07:00 wraps past midnight");
  settings.night = 3;  // 00:00 to 08:00
  CHECK(nightNow(0) && !nightNow(8) && !nightNow(23), "00:00 to 08:00");
  settings.night = 0;
  CHECK(!nightNow(23), "off is off");

  printf("Saved settings from v0.9.0 move to v0.10.0\n");
  sim::flash.clear();
  {
    Preferences p;
    p.begin("watch", false);
    p.putUChar("ver", 3);
    p.putUChar("face", 4);
    p.putBool("raise", true);
    p.end();
  }
  settings = Settings{};
  loadSettings();
  CHECK(settings.face == 4 && settings.screenTurn == DISPLAY_ROTATION && !touchChecked() && settings.night == 0 &&
            settings.driftPpb == 0 && settings.lastKnownTime == 0,
        "the face is kept; the screen turn starts at the default and touch is not yet checked");
  settings.driftPpb = -52000;
  settings.lastKnownTime = 1767225600LL;
  settings.touchMap = 3;
  settings.touchCheckedTurn = DISPLAY_ROTATION;
  saveSettings();
  settings = Settings{};
  loadSettings();
  CHECK(settings.driftPpb == -52000 && settings.lastKnownTime == 1767225600LL && touchChecked() && activeTouchMap() == 3,
        "drift, the last known time and the touch check survive a restart");
  {
    Preferences p;
    p.begin("watch", false);
    p.putUChar("turn", 9);
    p.putUChar("tmap", 12);
    p.putInt("drift", 900000000);
    p.end();
  }
  loadSettings();
  CHECK(settings.screenTurn < orient::TURNS && settings.touchMap < orient::MAPS && settings.driftPpb == 0,
        "damaged turn, map and drift values are brought back into range");
  sim::flash.clear();
  settings = Settings{};
  sim::clockSet = false;
}
}  // namespace

Face currentFaceForTest() { return (Face)(settings.face % FACE_COUNT); }


// ---- Touch read errors, serial commands and a failed frame allocation -----------------------
void touchDropoutScenarios() {
  printf("Touch: a dropped touch read is not a finger lift\n");
  sim::cold = false;
  ui::begin();
  settings.reducedMotion = false;  // slides, so a drag can be caught mid-way
  settle(2500);
  CHECK(screen() == "face", "starts on the face, got %s", screen().c_str());

  // Mid-swipe, the chip misses 30 ms of reads, then the swipe carries on.
  int face0 = settings.face;
  touchDown(190, 120);
  moveTo(150, 120, 50);
  sim::touchFails = true;
  runFor(30);
  sim::touchFails = false;
  CHECK(screen() == "face+slide", "a short dropout keeps the drag going, got %s", screen().c_str());
  moveTo(80, 120, 50);
  touchUp();
  settle();
  CHECK(settings.face == (face0 + 1) % FACE_COUNT, "and the swipe still lands on the next face (%d)", settings.face);

  // The chip stops answering for good mid-drag, with the face dragged past the commit point.
  face0 = settings.face;
  touchDown(190, 120);
  moveTo(90, 120, 60);
  sim::touchFails = true;
  runFor(200);
  settle();
  sim::fingerDown = false;
  sim::touchFails = false;
  settle();
  CHECK(screen() == "face" && settings.face == face0,
        "a long dropout springs the face back instead of finishing the swipe (face %d, was %d)", settings.face, face0);

  // A button held when reads fail must not act; the same press with a short blip still does.
  openSettings();
  screens::Control c;
  bool found = goTo(screens::Button::TimeFormat) && find(screens::Button::TimeFormat, c);
  CHECK(found, "Time format control found");
  if (found) {
    int cx = c.hit.x + c.hit.w / 2, cy = c.hit.y + c.hit.h / 2;
    bool was = settings.use12h;
    touchDown(cx, cy);
    runFor(40);
    sim::touchFails = true;
    runFor(120);
    sim::fingerDown = false;
    sim::touchFails = false;
    runFor(40);
    CHECK(settings.use12h == was, "a press cut off by failed reads does not toggle the setting");
    touchDown(cx, cy);
    runFor(40);
    sim::touchFails = true;
    runFor(20);
    sim::touchFails = false;
    runFor(20);
    touchUp();
    runFor(40);
    CHECK(settings.use12h != was, "a press with a 20 ms blip still toggles it on release");
    touchDown(cx, cy);
    runFor(40);
    touchUp();
    runFor(40);
    CHECK(settings.use12h == was, "and an ordinary tap toggles it back");
  }
  pressBoot();
  settle();

  printf("Touch: a chip that never answers does nothing\n");
  sim::touchFails = true;
  sim::fingerDown = false;
  int f = settings.face;
  runFor(500);
  CHECK(screen() == "face" && settings.face == f, "no touches are made up, got %s", screen().c_str());
  sim::touchFails = false;
}

// ---- Screen-off power: wake on motion, touch standby, sleep deadlines ------------------------
// A raise the way a wrist makes it: from hanging to looking over `ms`, eased in and out, with
// the sensor changing every 10 ms of script time, then held.
void smoothRaise(uint32_t ms, float toDeg = 63) {
  for (uint32_t t = 10; t <= ms; t += 10) {
    float u = t / (float)ms, e = u * u * (3 - 2 * u), a = e * toDeg * 3.14159265f / 180;
    sim::accel = {0, -cosf(a), sinf(a)};
    runFor(10);
  }
  runFor(400);
}

int lit() { return sim::backlightLog.empty() ? 0 : sim::backlightLog.back().second; }
const power::Controller &pc() { return ui::standbyController(); }
std::string pstate() { return power::stateName(pc().state()); }

// Back to sleep on a resting wrist: wait out the screen timeout and let the screen-off loop arm.
void sleepHanging() {
  sim::accel = HANGING;
  runFor(16000);
  runFor(500);
}

void powerScenarios() {
  printf("Power: wake on motion with raise only\n");
  power::options.wom = true;
  power::options.touchStandby = true;
  sim::cold = false;
  sim::imuPresent = true;
  settings = Settings{};
  settings.twistToWake = false;  // raise only: the mode wake on motion is used for
  settings.touchCheckedTurn = settings.screenTurn = DISPLAY_ROTATION;
  settings.touchMap = orient::expectedTouchMap(DISPLAY_ROTATION);
  saveSettings();
  ui::begin();
  settle(2500);
  CHECK(screen() == "face", "starts on the face, got %s", screen().c_str());
  sleepHanging();
  CHECK(screen() == "asleep" && pstate() == "wake on motion" && sim::womArmed, "asleep with the sensor armed (state %s)",
        pstate().c_str());
  CHECK(sim::touchStandby, "the touch chip is allowed its standby");

  int reads = sim::imuReads, touches = sim::touchReads, dz = sim::dozes;
  uint32_t t0 = sim::clockMs;
  uint64_t slept0 = sim::dozedMs;
  runFor(10UL * 60 * 1000);  // ten minutes on a still wrist
  int wakes = sim::dozes - dz;
  CHECK(screen() == "asleep" && sim::imuReads == reads, "ten still minutes: no wake and not one sample read (%d)",
        sim::imuReads - reads);
  CHECK(sim::touchReads == touches, "the resting touch chip is never read (%d reads)", sim::touchReads - touches);
  CHECK(wakes <= 25, "%d wakes in ten minutes (30 s sleeps, a battery reading each minute)", wakes);
  double asleepShare = (double)(sim::dozedMs - slept0) / (sim::clockMs - t0);
  CHECK(asleepShare > 0.99, "and %.2f%% of the time in light sleep", asleepShare * 100);
  fprintf(report, "\n## Screen-off power (simulated)\n\n- Ten still minutes, raise only, wake on motion: %d wakes, "
                  "%.2f%% of the time asleep, %d motion samples, %d touch reads.\n",
          wakes, asleepShare * 100, sim::imuReads - reads, sim::touchReads - touches);

  printf("Power: a motion interrupt never lights the screen by itself\n");
  uint32_t q0 = pc().stats().qualifications, r0 = pc().stats().rejected;
  sim::accel = {0.15f, -0.99f, 0.05f};  // bumped, then still again
  runFor(30);
  CHECK(pstate() == "qualifying" && screen() == "asleep" && lit() == 0, "a bump starts sampling, screen dark (state %s, light %d)",
        pstate().c_str(), lit());
  sim::accel = HANGING;
  runFor(1600);
  CHECK(screen() == "asleep" && lit() == 0 && pc().stats().qualifications == q0 + 1 && pc().stats().rejected == r0 + 1,
        "1.5 s of fresh samples show no raise: rejected, still dark");
  CHECK(pstate() == "wake on motion" && sim::womArmed && !sim::womEvent, "and the sensor is armed again, its event cleared");

  printf("Power: the first raise after a long rest\n");
  struct RaiseCase {
    const char *what;
    uint32_t ms;
    bool wakes;
  } raises[] = {{"an ordinary raise (0.5 s)", 500, true}, {"a brisk raise (0.4 s)", 400, true},
                {"a slow raise (2.5 s)", 2500, false}};
  for (const RaiseCase &rc : raises) {
    sleepHanging();
    runFor(5UL * 60 * 1000);
    uint32_t start = sim::clockMs;
    sim::firstImuReadAfterWake = 0;
    uint32_t a0 = pc().stats().accepted;
    smoothRaise(rc.ms);
    bool woke = screen() == "face";
    CHECK(woke == rc.wakes, "%s after 5 still minutes: %s (state %s)", rc.what, woke ? "wakes" : "stays dark", pstate().c_str());
    if (woke) {
      uint32_t lightAt = 0;
      for (auto &e : sim::backlightLog)
        if (e.first >= start && e.second > 0 && !lightAt) lightAt = e.first;
      fprintf(report, "- %s after 5 still minutes: wakes, first fresh sample %u ms and light %u ms after the raise began.\n",
              rc.what, sim::firstImuReadAfterWake ? sim::firstImuReadAfterWake - start : 0, lightAt ? lightAt - start : 0);
      CHECK(pc().stats().accepted == a0 + 1, "counted as an accepted gesture");
    }
  }
  sleepHanging();
  wrist({{300, {0, -0.9f, 1.8f}}, {300, {0.3f, -1.6f, 0.4f}}, {300, {-0.4f, -0.7f, 1.2f}}, {1600, HANGING}});
  CHECK(screen() == "asleep", "shaking does not wake it");

  printf("Power: repeated motion (walking) switches to polling for a while\n");
  sleepHanging();
  for (int i = 0; i < 5; i++) {
    sim::accel = {0.2f, -0.98f, 0.0f};
    runFor(200);
    sim::accel = HANGING;
    runFor(1500);
  }
  CHECK(screen() == "asleep" && pc().fallback() == power::Fallback::Quiet && pstate() == "polling",
        "after 4 rejected motions: polling, still dark (%s, %s)", pstate().c_str(), power::fallbackName(pc().fallback()));
  smoothRaise(500);
  CHECK(screen() == "face", "a raise still wakes it while polling, got %s", screen().c_str());

  printf("Power: touch and BOOT wake at once and do nothing else\n");
  sleepHanging();
  runFor(15000);  // the 30 s of polling after the walking above runs out
  sim::accel = {0.15f, -0.99f, 0.05f};
  runFor(30);
  CHECK(pstate() == "qualifying", "qualifying a bump (%s)", pstate().c_str());
  int faceBefore = settings.face;
  touchDown(120, 120);
  CHECK(screen() == "face", "a touch while qualifying wakes the screen, got %s", screen().c_str());
  runFor(900);  // held past the long-press time: the waking touch must not open settings
  touchUp();
  settle();
  CHECK(screen() == "face" && settings.face == faceBefore, "the waking touch does nothing else, got %s", screen().c_str());
  CHECK(!sim::touchStandby && !sim::womArmed, "touch tracking is back on and the sensor sampling");
  sim::accel = HANGING;
  sleepHanging();
  pressBoot();
  settle();
  CHECK(screen() == "face" && settings.face == faceBefore, "BOOT wakes it and does not change the face");

  printf("Power: a short touch pulse wakes the screen even with its report gone\n");
  sleepHanging();
  sim::touchPulse = true;
  runFor(50);
  CHECK(screen() == "face", "woken by the pulse alone, got %s", screen().c_str());

  printf("Power: a touch spanning standby and the wake cannot act\n");
  sleepHanging();
  sim::fingerX = sim::fingerY = 120;
  sim::fingerDown = true;
  sim::touchFails = true;  // the chip is still waking: the first reads get no answer
  runFor(40);
  CHECK(screen() == "face", "the touch line woke the screen, got %s", screen().c_str());
  sim::touchFails = false;
  runFor(1200);  // now answering, finger still down, past the long-press time
  CHECK(screen() == "face", "the finger that woke it never opens settings, got %s", screen().c_str());
  sim::fingerDown = false;
  runFor(60);
  swipeUp();
  settle();
  CHECK(screen() == "panel", "after lifting, the next swipe works normally, got %s", screen().c_str());
  pressBoot();
  settle();

  printf("Power: the touch chip does not answer after standby\n");
  sleepHanging();
  int restarts = sim::touchRestarts;
  sim::touchResumeFailures = 1;
  touchDown(120, 120);
  touchUp();
  settle();
  CHECK(screen() == "face" && sim::touchRestarts == restarts + 1 && !sim::touchStandby,
        "it is reset and set up again (%d restarts)", sim::touchRestarts - restarts);
  sim::touchStandbyOk = false;
  sleepHanging();
  touches = sim::touchReads;
  runFor(3000);
  CHECK(!sim::touchStandby && sim::touchReads > touches, "a chip without standby is left fully on and read as before");
  touchDown(120, 120);
  touchUp();
  settle();
  CHECK(screen() == "face", "and a touch still wakes the screen");
  sim::touchStandbyOk = true;

  printf("Power: night mode, no sensor, a radio on\n");
  settings.night = 1;  // 22:00 to 07:00
  hal::setTime(1792792800);  // 2026-10-23 22:00 UTC (the simulation runs in UTC)
  sleepHanging();
  int arms = sim::womArms;
  reads = sim::imuReads;
  CHECK(pstate() == "idle" && !sim::womArmed, "night mode: no gestures, so idle with the sensor not armed (%s)", pstate().c_str());
  smoothRaise(500);
  runFor(3000);
  CHECK(screen() == "asleep" && sim::womArms == arms && sim::imuReads == reads, "a raise does nothing and starts no sampling");
  touchDown(120, 120);
  touchUp();
  settle();
  CHECK(screen() == "face", "touch still wakes it at night");
  settings.night = 0;
  sim::imuPresent = false;
  sleepHanging();
  CHECK(pstate() == "idle", "no sensor: idle (%s)", pstate().c_str());
  touchDown(120, 120);
  touchUp();
  settle();
  sim::imuPresent = true;
  sleepHanging();
  net::startSync();
  runFor(300);
  CHECK(!sim::womArmed && pstate() == "polling", "a sync running: no light sleep, the sensor polled (%s)", pstate().c_str());
  settle(2500);
  runFor(500);
  CHECK(pstate() == "wake on motion", "armed again once the radio is off (%s)", pstate().c_str());

  printf("Power: sensor failures fall back to polling, and raise still works\n");
  sim::womArmFails = true;
  touchDown(120, 120);
  touchUp();
  settle();
  sleepHanging();
  CHECK(pstate() == "polling" && pc().fallback() == power::Fallback::ArmFailed, "arming fails: polling (%s)",
        power::fallbackName(pc().fallback()));
  smoothRaise(500);
  CHECK(screen() == "face", "a raise wakes it by polling, got %s", screen().c_str());
  sim::womArmFails = false;
  sleepHanging();
  sim::womSpurious = 20;
  runFor(500);
  CHECK(pc().fallback() == power::Fallback::Storm && pstate() == "polling" && screen() == "asleep",
        "INT2 chattering with no motion: polling (%s)", power::fallbackName(pc().fallback()));
  sim::womSpurious = 0;
  touchDown(120, 120);
  touchUp();
  settle();
  sleepHanging();
  sim::womLineStuck = true;
  runFor(200);
  CHECK(pc().fallback() == power::Fallback::StuckLine && pstate() == "polling", "INT2 stuck high: polling (%s)",
        power::fallbackName(pc().fallback()));
  sim::womLineStuck = false;
  touchDown(120, 120);
  touchUp();
  settle();

  printf("Power: twist modes keep polling\n");
  settings.twistToWake = true;
  settings.raiseToWake = false;
  sleepHanging();
  sim::accel = FLAT;
  runFor(2000);
  CHECK(pstate() == "polling" && pc().fallback() == power::Fallback::TwistNeedsHistory && !sim::womArmed,
        "twist only: polling (%s)", power::fallbackName(pc().fallback()));
  runFor(5UL * 60 * 1000);
  twist(60);
  CHECK(screen() == "face", "the first twist after 5 still minutes wakes it, got %s", screen().c_str());
  {
    // Why: the twist detector needs the still moment just before the twist and the turn out.
    // Fed only what a wake on motion interrupt would give it (samples from about 60 ms into
    // the twist, after a long gap), it misses the same twist.
    TwistDetector td;
    uint32_t t = 1000;
    for (int i = 0; i < 10; i++, t += 80) td.update(t, FLAT, 1);  // before the long sleep
    t += 300000;
    bool fired = false;
    Accel path[] = {rolled(45), rolled(60), rolled(30), FLAT, FLAT, FLAT};
    for (const Accel &a : path) fired |= td.update(t += 80, a, 1);
    CHECK(!fired, "the twist detector misses a twist seen only after a motion interrupt: polling is kept for twist");
  }
  settings.raiseToWake = true;
  settings.twistToWake = true;
  runFor(16000);

  printf("Power: sleeps wake for the next resync\n");
  touchDown(120, 120);
  touchUp();
  settle();
  sim::syncOutcome = net::Fail::ClockFailed;
  net::startSync();
  settle(2500);
  sim::syncOutcome = net::Fail::None;
  settings.raiseToWake = settings.twistToWake = false;  // idle: the longest sleeps
  sleepHanging();
  int started = sim::syncsStarted;
  runFor(29UL * 60 * 1000 - 20000);
  CHECK(sim::syncsStarted == started, "no resync before its time");
  runFor(120000);
  CHECK(sim::syncsStarted == started + 1, "the 30 minute retry after a failed sync still happens on time");
  settings.raiseToWake = settings.twistToWake = true;
  power::options = power::Options{};
  sim::accel = FLAT;
  touchDown(120, 120);
  touchUp();
  settle();
}

// Phantom: the frame scheduler and the face's place in the face list.
uint64_t framesIn(uint32_t ms) {
  uint64_t p0 = ui::display().pixelsPushed;
  runFor(ms);
  return (ui::display().pixelsPushed - p0) / (240 * 240);
}

void phantomScenarios() {
  printf("Phantom face\n");
  sim::cold = false;
  settings = Settings{};
  settings.touchCheckedTurn = settings.screenTurn = DISPLAY_ROTATION;
  settings.touchMap = orient::expectedTouchMap(DISPLAY_ROTATION);
  settings.face = FACE_PHANTOM;
  saveSettings();
  ui::begin();
  settle(2500);
  CHECK(screen() == "face" && currentFaceForTest() == FACE_PHANTOM, "starts on the stored Phantom face (%d)", currentFaceForTest());
  uint64_t f = framesIn(1000);
  CHECK(f >= 25 && f <= 31, "awake: about 30 frames a second (%llu)", (unsigned long long)f);
  snap("phantom_awake");

  settings.reducedMotion = true;
  runFor(1100);
  f = framesIn(3000);
  CHECK(f >= 2 && f <= 4, "reduced motion: one frame a second, the seconds step (%llu in 3 s)", (unsigned long long)f);
  settings.reducedMotion = false;

  sim::batteryMv = 3600;  // 15%
  runFor(61000);           // the next battery reading
  if (screen() != "face") {
    pressBoot();
    settle();
  }
  f = framesIn(3000);
  CHECK(f >= 2 && f <= 4, "low battery: one frame a second (%llu in 3 s)", (unsigned long long)f);
  sim::batteryMv = 3950;
  runFor(61000);
  if (screen() != "face") {
    pressBoot();
    settle();
  }
  f = framesIn(1000);
  CHECK(f >= 25, "battery back up: smooth again (%llu)", (unsigned long long)f);

  swipeUp();
  settle();
  CHECK(screen() == "panel", "settings open over Phantom, got %s", screen().c_str());
  uint32_t pushes0 = ui::display().pushes;
  runFor(1000);
  CHECK(ui::display().pushes - pushes0 <= 2, "no face frames behind the settings panel (%u pushes in 1 s)",
        ui::display().pushes - pushes0);
  pressBoot();
  settle();

  sim::accel = HANGING;
  runFor(16000);
  runFor(500);
  CHECK(screen() == "asleep", "the screen goes off, got %s", screen().c_str());
  pushes0 = ui::display().pushes;
  runFor(5000);
  CHECK(ui::display().pushes == pushes0, "nothing is drawn while the screen is off (%u pushes)", ui::display().pushes - pushes0);
  pressBoot();
  settle();

  // Six faces now: a swipe from Phantom wraps round to the first.
  CHECK(FACE_COUNT == 6, "six faces (%d)", FACE_COUNT);
  int seen = 0;
  for (int i = 0; i < FACE_COUNT; i++) {
    seen |= 1 << settings.face;
    swipeLeft();
    settle();
  }
  CHECK(seen == (1 << FACE_COUNT) - 1 && settings.face == FACE_PHANTOM, "swiping visits every face and comes back (seen %x, face %d)",
        seen, settings.face);
  ui::begin();  // warm restart: the stored choice comes back from flash
  settle(2500);
  CHECK(settings.face == FACE_PHANTOM && currentFaceForTest() == FACE_PHANTOM, "Phantom is kept across a restart (%d)",
        settings.face);
  for (int turn : {0, 1, 2, 3}) {
    settings.screenTurn = turn;
    runFor(100);
    CHECK(framesIn(500) >= 12, "draws smoothly with the screen turned %d", turn * 90);
  }
  settings.screenTurn = DISPLAY_ROTATION;

  spriteBudget16 = 0;  // no cached dial: stepped once a second
  ui::begin();
  settle(2500);
  runFor(500);
  f = framesIn(3000);
  CHECK(currentFaceForTest() == FACE_PHANTOM && f <= 4, "low memory: Phantom steps once a second (%llu frames in 3 s)",
        (unsigned long long)f);
  spriteBudget16 = -1;
  ui::begin();
  settle(2500);
}

void serialScenarios() {
  using namespace serialcmd;
  printf("Serial: lines are read without waiting\n");
  {
    LineBuffer b;
    bool any = false;
    for (const char *p = "TIME:12"; *p; p++) any |= b.push(*p);
    CHECK(!any, "half a line is not a line");
    for (const char *p = "34567\r\n"; *p; p++)
      if (b.push(*p)) {
        any = true;
        CHECK(strcmp(b.line(), "TIME:1234567") == 0, "the line comes out whole (%s)", b.line());
      }
    CHECK(any, "the newline ends it");
    CHECK(!b.push('\n'), "the \\n after \\r is not an extra empty line");
    std::string longLine(200, 'x');
    longLine += '\n';
    any = false;
    for (char ch : longLine) any |= b.push(ch);
    CHECK(!any, "a line longer than the buffer is dropped, not cut short");
    any = false;
    for (const char *p = "TIME:1800000000\n"; *p; p++) any |= b.push(*p);
    CHECK(any && strcmp(b.line(), "TIME:1800000000") == 0, "and the next line reads normally");
  }
  {
    Command t = parse("  TIME:1800000000  ");
    CHECK(t.kind == Kind::Time && t.time == 1800000000, "TIME: with spaces around it is read");
    CHECK(parse("TIME:99").kind == Kind::None, "a TIME: too small to be a date is refused");
    CHECK(parse("TIME:").kind == Kind::None && parse("time:1800000000").kind == Kind::None, "empty or lower case is refused");
    Command w = parse("WIFI:Home Net,pa,ss");
    CHECK(w.kind == Kind::Wifi && strcmp(w.ssid, "Home Net") == 0 && strcmp(w.password, "pa,ss") == 0,
          "WIFI: splits at the first comma (%s / %s)", w.ssid, w.password);
    Command open = parse("WIFI:Cafe,");
    CHECK(open.kind == Kind::Wifi && open.password[0] == 0, "an open network has an empty password");
    CHECK(parse("WIFI:,secret").kind == Kind::None && parse("WIFI:nocomma").kind == Kind::None,
          "a WIFI: line without a network name or comma is refused");
    std::string longSsid = "WIFI:" + std::string(33, 'n') + ",pw";
    CHECK(parse(longSsid.c_str()).kind == Kind::None, "a network name over 32 bytes is refused");
  }

  printf("Serial: a command sent in pieces sets the clock once it is complete\n");
  sim::cold = false;
  ui::begin();
  settle(2500);
  Serial.in = "TIME:18000";
  runFor(50);
  CHECK(!net::clockSetByHand(), "nothing happens before the newline");
  CHECK(Serial.in.empty(), "but the bytes are taken as they arrive");
  CHECK(screen() == "face", "and the watch keeps running, got %s", screen().c_str());
  Serial.in = "00000\n";
  runFor(50);
  CHECK(net::clockSetByHand() && hal::now() >= 1800000000 && hal::now() < 1800000010,
        "the clock is set from the finished line (%ld)", (long)hal::now());
  sim::wifiSaved = false;
  Serial.in = "WIFI:Home,secret\r\n";
  runFor(50);
  CHECK(sim::wifiSaved && strcmp(net::ssid(), "Home") == 0, "WIFI: saves the network (%s)", net::ssid());
  settle(2500);
}

void noMemoryScenario() {
  printf("Low memory: not even an 8-bit frame fits\n");
  spriteBudget16 = 0;
  spriteBudget8 = 0;
  sim::cold = false;
  ui::begin();
  TFT_eSPI &d = ui::display();
  int lit = 0;
  for (uint16_t px : d.buf) lit += px != 0;
  CHECK(lit > 200, "a notice is drawn straight to the screen (%d pixels lit)", lit);
  runFor(500);
  tap(120, 120);
  swipeLeft();
  CHECK(true, "the loop keeps running without a frame");
  spriteBudget16 = spriteBudget8 = -1;
}

int main(int argc, char **argv) {
  outDir = argc > 1 ? argv[1] : ".";
  setenv("TZ", "UTC0", 1);
  tzset();
  std::string reportPath = std::string(outDir) + "/report.md";
  report = fopen(reportPath.c_str(), "w");
  screenVariants();
  contrastAudit();
  migration();
  logicChecks();
  settings = Settings{};
  sim::flash.clear();
  timeSteps();
  scenarios();
  touchDropoutScenarios();
  phantomScenarios();
  serialScenarios();
  powerScenarios();
  noMemoryScenario();  // last: it leaves the first frame 8-bit
  fclose(report);
  printf("%d of %d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
