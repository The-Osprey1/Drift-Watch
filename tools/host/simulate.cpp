// Drives the real interface code (ui.cpp, screens.cpp, faces.cpp) with
// scripted touches, button presses and Wi-Fi outcomes, checks the results and
// saves frames, including mid-transition ones. Also edge-checks every panel
// and setup screen variant. Output: PPM frames plus a report; exit code 1 on
// any failed check.
#include <TFT_eSPI.h>
#include <string>
#include "faces.h"
#include "screens.h"
#include "settings.h"
#include "sim.h"
#include "theme.h"
#include "ui.h"

namespace {
const char *outDir = ".";
int failures = 0, checks = 0;

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

void tap(int x, int y) {
  touchDown(x, y);
  runFor(60);
  touchUp();
}

void pressBoot() {
  sim::bootDown = true;
  runFor(40);
  sim::bootDown = false;
  runFor(20);
}

void settle(uint32_t ms = 400) { runFor(ms); }

// ---- Edge checks for the panel and setup screens -------------------------------
int edgeProblems(const TFT_eSPI &s, const char *label) {
  int bad = 0;
  for (const auto &t : s.textLog) {
    int xs[] = {t.x, t.x + t.w}, ys[] = {t.y + 2, t.y + t.h - 2};
    for (int x : xs)
      for (int y : ys) {
        float d = hypotf(x - layout::CX, y - layout::CY);
        if (d > layout::SAFE_R) {
          printf("  EDGE %s: \"%s\" corner (%d,%d) is %.1f px from centre\n", label, t.text.c_str(), x, y, d);
          bad++;
        }
      }
  }
  return bad;
}

void screenVariants() {
  printf("Panel and setup screens: text inside the circle\n");
  TFT_eSPI parent;
  TFT_eSprite s(&parent);
  s.createSprite(240, 240);
  int bad = 0;
  struct P { const char *name; net::Sync sync; net::Fail fail; int step; const char *synced, *zone; int bat; bool h12, less; int light; screens::Button pressed; };
  P panels[] = {
      {"synced", net::Sync::Done, net::Fail::None, 0, "20:48", "Europe/London", 86, false, false, 2, screens::Button::None},
      {"longzone", net::Sync::Done, net::Fail::None, 0, "8:05", "America/Argentina/ComodRivadavia", 100, true, true, 3, screens::Button::None},
      {"syncing", net::Sync::Zone, net::Fail::None, 2, "", "", 12, false, false, 0, screens::Button::Sync},
      {"joinfail", net::Sync::Failed, net::Fail::JoinFailed, 0, "", "", -1, false, false, 1, screens::Button::None},
      {"clockfail", net::Sync::Failed, net::Fail::ClockFailed, 0, "", "", 55, false, false, 1, screens::Button::WifiSetup},
      {"nonet", net::Sync::Failed, net::Fail::NoNetworkSaved, 0, "", "", 55, false, false, 1, screens::Button::Light},
  };
  for (const P &p : panels) {
    screens::PanelView v{p.bat, 3900, (uint8_t)p.light, p.h12, p.less, true, p.sync, p.fail, p.step, p.synced, p.zone,
                         "HomeNet-5G", nullptr, p.pressed, 300};
    s.textLog.clear();
    screens::drawPanel(s, v);
    std::string label = std::string("panel_") + p.name;
    bad += edgeProblems(s, label.c_str());
    writePpm(label, s);
  }
  struct S { const char *name; net::Portal portal; net::Sync sync; net::Fail fail; int step; screens::Button pressed; };
  S setups[] = {
      {"waiting", net::Portal::Waiting, net::Sync::Idle, net::Fail::None, 0, screens::Button::None},
      {"joined", net::Portal::PhoneJoined, net::Sync::Idle, net::Fail::None, 0, screens::Button::None},
      {"saving", net::Portal::Saved, net::Sync::Connecting, net::Fail::None, 1, screens::Button::None},
      {"done", net::Portal::Saved, net::Sync::Done, net::Fail::None, 0, screens::Button::Done},
      {"joinfail", net::Portal::Saved, net::Sync::Failed, net::Fail::JoinFailed, 0, screens::Button::None},
      {"clockfail", net::Portal::Saved, net::Sync::Failed, net::Fail::ClockFailed, 0, screens::Button::None},
      {"timeout", net::Portal::TimedOut, net::Sync::Idle, net::Fail::None, 0, screens::Button::None},
  };
  for (const S &p : setups) {
    screens::SetupView v{p.portal, p.sync, p.fail, p.step, 272, "20:48", p.pressed, 300, false};
    s.textLog.clear();
    screens::drawSetup(s, v);
    std::string label = std::string("setup_") + p.name;
    bad += edgeProblems(s, label.c_str());
    writePpm(label, s);
  }
  for (float t : {0.15f, 0.4f, 0.7f, 1.0f}) {
    s.textLog.clear();
    screens::drawBoot(s, t, "v" "0.7.0");
    char label[32];
    snprintf(label, sizeof label, "boot_%03d", (int)(t * 100));
    bad += edgeProblems(s, label);
    writePpm(label, s);
  }
  CHECK(bad == 0, "%d text boxes cross the safe circle", bad);
}

// ---- Interaction scenarios ---------------------------------------------------------
int lastBacklight() { return sim::backlightLog.empty() ? -1 : sim::backlightLog.back().second; }

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
  CHECK(net::syncState() == net::Sync::Done, "sync finished");
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

  printf("BOOT on a face goes to the next face\n");
  int face4 = settings.face;
  pressBoot();
  settle();
  CHECK(settings.face == (face4 + 1) % FACE_COUNT, "BOOT advances the face");

  printf("Long press opens the panel with a hold ring\n");
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
  CHECK(screen() == "panel", "panel open, got %s", screen().c_str());
  snap("panel");

  printf("Panel buttons\n");
  int light = settings.brightnessLevel;
  touchDown(73, 104);
  snap("panel_light_pressed");
  runFor(60);
  touchUp();
  CHECK(settings.brightnessLevel == (light + 1) % BRIGHTNESS_LEVEL_COUNT, "LIGHT steps the brightness");
  CHECK(lastBacklight() == backlightLevel(), "backlight follows (%d vs %d)", lastBacklight(), backlightLevel());
  bool h12 = settings.use12h;
  tap(167, 104);
  CHECK(settings.use12h != h12, "CLOCK switches 12/24 hour");
  touchDown(73, 152);
  moveTo(73, 175, 100);  // slides off: a drag is never a tap
  touchUp();
  CHECK(!settings.reducedMotion, "a press that drags away does not toggle MOTION");
  int syncs = sim::syncsStarted;
  tap(167, 152);
  CHECK(sim::syncsStarted == syncs + 1, "SYNC starts a sync");
  runFor(700);
  snap("panel_syncing");
  settle(1500);
  snap("panel_synced");
  tap(167, 104);  // back to 24 h for the rest

  printf("Swipe down closes the panel\n");
  swipe(120, 80, 120, 170, 120);
  runFor(120);
  snap("panel_closing");
  settle();
  CHECK(screen() == "face", "swipe down returns to the face, got %s", screen().c_str());

  printf("BOOT closes the panel\n");
  swipe(120, 180, 120, 80, 120);
  settle();
  CHECK(screen() == "panel", "swipe up opens the panel, got %s", screen().c_str());
  pressBoot();
  settle();
  CHECK(screen() == "face", "BOOT returns to the face, got %s", screen().c_str());

  printf("Wi-Fi setup flow\n");
  swipe(120, 180, 120, 80, 120);
  settle();
  tap(120, 200);
  CHECK(screen() == "setup", "WI-FI SETUP opens setup, got %s", screen().c_str());
  runFor(200);
  snap("setup_waiting");
  sim::setPortal(net::Portal::PhoneJoined);
  runFor(100);
  snap("setup_joined");
  sim::syncOutcome = net::Fail::JoinFailed;
  sim::setPortal(net::Portal::Saved);
  runFor(300);
  snap("setup_saving");
  settle(1500);
  snap("setup_failed");
  CHECK(net::syncState() == net::Sync::Failed, "join failure reported");
  tap(120, 200);  // RETRY reopens the hotspot
  CHECK(net::portalState() == net::Portal::Waiting, "RETRY reopens the hotspot");
  sim::syncOutcome = net::Fail::None;
  sim::setPortal(net::Portal::Saved);
  settle(2500);
  snap("setup_done");
  tap(120, 200);  // DONE
  CHECK(screen() == "face", "DONE returns to the face, got %s", screen().c_str());

  printf("Cancel and BOOT leave setup\n");
  swipe(120, 180, 120, 80, 120);
  settle();
  tap(120, 200);
  tap(120, 200);  // CANCEL
  CHECK(screen() == "panel" && net::portalState() == net::Portal::Closed, "CANCEL closes the hotspot, back to panel");
  tap(120, 200);
  pressBoot();
  CHECK(screen() == "panel" && net::portalState() == net::Portal::Closed, "BOOT closes the hotspot, back to panel");
  pressBoot();
  settle();

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
  int face5 = settings.face;
  touchDown(190, 120);
  moveTo(120, 120, 80);
  CHECK(screen() == "face", "no drag-follow with reduced motion, got %s", screen().c_str());
  touchUp();
  CHECK(settings.face == (face5 + 1) % FACE_COUNT && screen() == "face", "swipe cuts to the next face");
  swipe(120, 180, 120, 80, 120);
  CHECK(screen() == "panel", "panel opens at once, got %s", screen().c_str());
  pressBoot();
  CHECK(screen() == "face", "and closes at once");
  settings.reducedMotion = false;

  printf("Warm restart skips the startup sequence\n");
  sim::cold = false;
  ui::begin();
  CHECK(screen() == "face", "after a crash restart the face shows at once, got %s", screen().c_str());

  settle(2500);  // let the restart's sync finish before wiping the clock

  printf("First start with no Wi-Fi saved\n");
  sim::cold = true;
  sim::wifiSaved = false;
  sim::clockSet = false;
  ui::begin();
  runFor(motion::BOOT_MS + 400);
  CHECK(screen() == "setup", "opens Wi-Fi setup after the startup sequence, got %s", screen().c_str());
  pressBoot();
  pressBoot();
  settle();
  runFor(1000);
  snap("face_notime");

  printf("Settings writes are batched\n");
  int saves = sim::settingsSaves;
  sim::clockSet = true;
  for (int i = 0; i < 4; i++) {
    runFor(60);
    swipe(200, 120, 110, 120, 70);
  }
  settle(300);
  CHECK(sim::settingsSaves == saves, "no flash write during rapid swipes");
  runFor(2500);
  CHECK(sim::settingsSaves == saves + 1, "one write after they stop (%d)", sim::settingsSaves - saves);
}
}  // namespace

int main(int argc, char **argv) {
  outDir = argc > 1 ? argv[1] : ".";
  setenv("TZ", "UTC0", 1);
  tzset();
  screenVariants();
  scenarios();
  printf("%d of %d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
