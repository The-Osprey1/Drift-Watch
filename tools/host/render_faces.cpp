// Renders every face from the real firmware drawing code at native 240 x 240
// for a set of test times and states, and checks that no text crosses the
// safe circle (using the ink of the drawn glyphs) and that the four
// clock/sync combinations show the right words. Output: one PPM per case plus
// a report on stdout.
#include <TFT_eSPI.h>
#include <map>
#include <string.h>
#include "faces.h"
#include "gfx.h"
#include "theme.h"

uint32_t millis() { return 0; }
void delay(uint32_t) {}

namespace {
void writePpm(const char *path, const TFT_eSPI &s) {
  FILE *f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", s.width(), s.height());
  for (uint16_t c : s.buf) {
    uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63),
                      (uint8_t)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

int edgeProblems(const TFT_eSPI &s, const char *label) {
  int bad = 0;
  for (const auto &t : s.textLog) {
    if (!t.iw) continue;
    int xs[] = {t.ix, t.ix + t.iw - 1}, ys[] = {t.iy, t.iy + t.ih - 1};
    for (int x : xs)
      for (int y : ys) {
        float d = hypotf(x - layout::CX, y - layout::CY);
        if (d > layout::SAFE_R) {
          printf("  EDGE %s: \"%s\" corner (%d,%d) is %.1f px from centre (limit %d)\n", label, t.text.c_str(), x, y, d,
                 layout::SAFE_R);
          bad++;
        }
      }
  }
  return bad;
}

tm makeTime(int h, int m, int s, int wday = 5, int mday = 23, int mon = 9) {
  tm t{};
  t.tm_hour = h; t.tm_min = m; t.tm_sec = s; t.tm_wday = wday; t.tm_mday = mday; t.tm_mon = mon; t.tm_yday = 295;
  return t;
}
}  // namespace

int main(int argc, char **argv) {
  const char *outDir = argc > 1 ? argv[1] : ".";
  TFT_eSPI screen;
  TFT_eSprite frame(&screen);
  frame.createSprite(240, 240);

  struct Case { const char *name; tm t; bool valid, h12; int bat; SyncView sync; float beamDeg; uint8_t stage = 0; bool noWifi = false; };
  std::vector<Case> cases = {
      {"2048", makeTime(20, 48, 37), true, false, 86, SyncView::Synced, -1},
      {"1111", makeTime(11, 11, 11), true, false, 52, SyncView::Synced, -1},
      {"2359", makeTime(23, 59, 59, 3, 30, 8), true, false, 100, SyncView::Synced, -1},
      {"0000", makeTime(0, 0, 5, 4, 1, 9), true, false, 8, SyncView::Synced, -1},
      {"1009", makeTime(10, 9, 31), true, false, 64, SyncView::Synced, -1},
      {"12h_0941", makeTime(9, 41, 12, 3, 17, 4), true, true, 64, SyncView::Synced, -1},
      {"12h_1258", makeTime(12, 58, 50, 0, 31, 11), true, true, 31, SyncView::Synced, -1},
      {"wed_sep", makeTime(18, 30, 0, 3, 28, 8), true, false, -1, SyncView::Synced, -1},
      {"notime", makeTime(0, 0, 0), false, false, 70, SyncView::Never, -1},
      {"syncing", makeTime(0, 0, 0), false, false, 70, SyncView::Syncing, -1, 1},
      // The four clock/sync combinations (with "notime" and "syncing" above).
      {"valid_syncing", makeTime(20, 48, 37), true, false, 86, SyncView::Syncing, -1, 2},
      {"valid_failed", makeTime(20, 48, 37), true, false, 86, SyncView::Failed, -1},
      {"notime_failed", makeTime(0, 0, 0), false, false, 70, SyncView::Failed, -1},
      {"notime_nowifi", makeTime(0, 0, 0), false, false, 70, SyncView::Failed, -1, 0, true},
      {"beam", makeTime(20, 48, 2), true, false, 86, SyncView::Synced, 126},
      {"beam_low", makeTime(20, 48, 40), true, false, 15, SyncView::Synced, 240},
  };

  int problems = 0;
  for (int f = 0; f < FACE_COUNT; f++) {
    for (const Case &c : cases) {
      FaceContext ctx{c.t, c.valid, c.h12, c.bat, c.sync, c.stage, c.noWifi, c.beamDeg};
      frame.textLog.clear();
      drawFaceStatic(frame, (Face)f, ctx);
      drawFaceLive(frame, (Face)f, ctx);
      char label[64], path[256];
      snprintf(label, sizeof label, "%s_%s", faceName((Face)f), c.name);
      problems += edgeProblems(frame, label);
      // Words for the clock/sync state. A valid clock never shows a warning, whatever the sync is doing.
      // v0.10.0: the dial never says NO TIME; it asks to SET TIME instead.
      bool noTime = false, banned = false, syncing = false, hint = false;
      for (const auto &t : frame.textLog) {
        noTime |= t.text == "SET TIME";
        banned |= t.text == "NO TIME";
        syncing |= t.text == "SYNCING";
        hint |= t.text.rfind("Swipe up", 0) == 0;
      }
      bool ok = !banned && (c.valid ? !noTime && !syncing && !hint
                                   : c.sync == SyncView::Syncing ? syncing && !noTime : noTime && hint && !syncing);
      if (!ok) {
        printf("  STATE %s: set-time %d, no-time %d, syncing %d, hint %d\n", label, noTime, banned, syncing, hint);
        problems++;
      }
      snprintf(path, sizeof path, "%s/%s.ppm", outDir, label);
      writePpm(path, frame);
    }
  }
  // Phantom's seconds timeline: the pure function at each boundary, then native frames at the
  // same moments, with the drawing given the exact time (faces never read a clock).
  auto bad = [&](bool ok, const char *what) {
    if (!ok) {
      printf("  PHANTOM %s\n", what);
      problems++;
    }
  };
  using phantom::needleAt;
  bad(needleAt(5, true).visible && needleAt(5, true).length == 100 && needleAt(54.999f, true).length == 100,
      "full length from 5 s to 55 s");
  bad(needleAt(55, true).length == 100 && needleAt(56.5f, true).length < 100 && needleAt(56.5f, true).length > 22,
      "drawn in from 55 s");
  bad(needleAt(57.999f, true).visible && needleAt(57.999f, true).alpha < 0.01f && needleAt(57.999f, true).length < 23,
      "faded out and at the lip just before 58 s, so hiding it at 58 s shows no jump");
  bad(!needleAt(58, true).visible && !needleAt(59.999f, true).visible && !needleAt(0, true).visible &&
          !needleAt(1.999f, true).visible,
      "hidden from 58 s to 2 s");
  bad(needleAt(2, true).visible && needleAt(2, true).length == 22 && needleAt(2, true).alpha == 0, "comes back at the lip, unseen, at 2 s");
  bad(needleAt(4.999f, true).length > 99.9f && needleAt(4.999f, true).alpha == 1, "out to full length by 5 s");
  bad(phantom::reboundDeg(4.99f) == 0 && phantom::reboundDeg(3.5f) == 0, "no rebound before 5 s, during the re-emergence");
  float peak = 0;
  for (float t = 5; t < 5.4f; t += 0.001f) peak = fmaxf(peak, fabsf(phantom::reboundDeg(t)));
  bad(peak > 0.5f && peak <= 1.8f && phantom::reboundDeg(5.35f) == 0, "a rebound of under 1.8 degrees, over by 5.35 s");
  bad(needleAt(5.0875f, false).deg == 6 * 5.0875f, "no rebound with reduced motion");
  // The rebound is the only time the hand moves against the clock, and only by a fraction of a degree.
  float lead = 0, back = 0, backOutside = 0;
  for (float t = 2; t < 57.99f; t += 0.001f) {
    float d = needleAt(t, true).deg;
    lead = fmaxf(lead, d);
    back = fmaxf(back, lead - d);
    if ((t < 5 || t > 5 + phantom::REBOUND_S) && lead - d > 0.001f) backOutside = lead - d;
  }
  bad(back < 1.0f && backOutside == 0, "only the rebound runs back, by under a degree");
  bad(fabsf(phantom::balanceDeg(999) - phantom::balanceDeg(1000)) < 6 && phantom::balanceDeg(0) == 0 &&
          fabsf(phantom::balanceDeg(62) - phantom::balanceDeg(312)) < 0.01f,
      "the balance swings four times a second and runs on across the second boundary");
  bad(phantom::inAperture(328) && phantom::inAperture(0) && phantom::inAperture(32) && !phantom::inAperture(327) &&
          !phantom::inAperture(33),
      "the opening spans 328 to 32 degrees");
  bad(!strcmp(faceName(FACE_PHANTOM), "Phantom") && FACE_PHANTOM == 5, "Phantom is face 5, after the five stored ones");

  // Native frames.
  const uint16_t SECONDS = rgb(0xE8ECF2);
  struct Moment {
    const char *name;
    int sec, ms;
    bool animate, valid, h12;
    int bat;
    bool needle;  // the seconds hand should be visible
  } moments[] = {
      {"00s", 0, 0, true, true, false, 86, false},      {"01s999", 1, 999, true, true, false, 86, false},
      {"02s000", 2, 0, true, true, false, 86, false},   {"02s001", 2, 1, true, true, false, 86, false},
      {"03s500", 3, 500, true, true, false, 86, true},  {"04s999", 4, 999, true, true, false, 86, true},
      {"05s000", 5, 0, true, true, false, 86, true},    {"05s090", 5, 90, true, true, false, 86, true},
      {"30s000", 30, 0, true, true, false, 86, true},   {"54s999", 54, 999, true, true, false, 86, true},
      {"55s000", 55, 0, true, true, false, 86, true},   {"56s500", 56, 500, true, true, false, 86, true},
      {"57s999", 57, 999, true, true, false, 86, false}, {"58s000", 58, 0, true, true, false, 86, false},
      {"59s999", 59, 999, true, true, false, 86, false}, {"reduced_57s", 57, 0, false, true, false, 86, true},
      {"reduced_58s", 58, 0, false, true, false, 86, false}, {"12h", 30, 0, true, true, true, 86, true},
      {"low_battery", 30, 0, false, true, false, 12, true}, {"unknown_battery", 30, 0, true, true, false, -1, true},
      {"notime", 30, 0, true, false, false, 70, false},
  };
  for (const Moment &m : moments) {
    FaceContext ctx{makeTime(10, 9, m.sec), m.valid, m.h12, m.bat, SyncView::Synced, 0, false, -1};
    ctx.ms = m.ms;
    ctx.animMs = 1000 + m.sec * 1000 + m.ms;
    ctx.animate = m.animate;
    if (!m.valid) ctx.sync = SyncView::Never;
    frame.textLog.clear();
    drawFaceStatic(frame, FACE_PHANTOM, ctx);
    drawFaceLive(frame, FACE_PHANTOM, ctx);
    // The hand's core pixels are its colour faded by its alpha, as the timeline gives it.
    phantom::Needle n = needleAt(m.sec + (m.animate ? m.ms / 1000.0f : 0), m.animate);
    uint16_t ink = gfx::blend(SECONDS, rgb(0x090A0D), n.alpha);
    int needlePx = 0;
    for (uint16_t px : frame.buf) needlePx += n.visible && n.alpha > 0.3f && px == ink;
    char label[64], path[256];
    snprintf(label, sizeof label, "phantom_%s", m.name);
    problems += edgeProblems(frame, label);
    if ((needlePx > 20) != m.needle) {
      printf("  PHANTOM %s: %d seconds-hand pixels, expected it %s\n", label, needlePx, m.needle ? "visible" : "hidden");
      problems++;
    }
    snprintf(path, sizeof path, "%s/%s.ppm", outDir, label);
    writePpm(path, frame);
  }

  printf("%d text edge or state problems\n", problems);
  return problems ? 1 : 0;
}
