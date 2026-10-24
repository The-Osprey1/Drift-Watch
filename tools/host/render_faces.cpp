// Renders every face from the real firmware drawing code at native 240 x 240
// for a set of test times and states, and checks that no text crosses the
// safe circle (using the ink of the drawn glyphs) and that the four
// clock/sync combinations show the right words. Output: one PPM per case plus
// a report on stdout.
#include <TFT_eSPI.h>
#include <map>
#include "faces.h"
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
  printf("%d text edge or state problems\n", problems);
  return problems ? 1 : 0;
}
