// Renders every face from the real firmware drawing code at native 240 x 240
// for a set of test times and states, and checks that no text crosses the
// safe circle (using the ink of the drawn glyphs) and that the four
// clock/sync combinations show the right words. Output: one PPM per case plus
// a report on stdout.
#include <algorithm>
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

  struct Case { const char *name; tm t; bool valid, h12; int bat; SyncView sync; float beamDeg; uint8_t stage = 0; bool noWifi = false; bool charging = false; };
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
      {"charging", makeTime(10, 9, 31), true, false, 64, SyncView::Synced, -1, 0, false, true},
      {"charging_low", makeTime(10, 9, 31), true, false, 12, SyncView::Synced, -1, 0, false, true},
  };

  int problems = 0;
  for (int f = 0; f < FACE_COUNT; f++) {
    for (const Case &c : cases) {
      FaceContext ctx{c.t, c.valid, c.h12, c.bat, c.sync, c.stage, c.noWifi, c.beamDeg};
      ctx.charging = c.charging;
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
  for (const Case &c : cases) {
    FaceContext ctx{c.t, c.valid, c.h12, c.bat, c.sync, c.stage, c.noWifi, c.beamDeg};
    ctx.charging = c.charging;
    frame.textLog.clear();
    drawAodDigital(frame, ctx);
    char label[64], path[256];
    snprintf(label, sizeof label, "AOD_%s", c.name);
    problems += edgeProblems(frame, label);
    snprintf(path, sizeof path, "%s/%s.ppm", outDir, label);
    writePpm(path, frame);
  }
  // The vector alphabet must fit its declared cell at every actual face size.
  // Distinct hashes catch a missing/duplicated numeral; the blank leading cell stays empty.
  for (auto style : {gfx::TimeStyle::Instrument, gfx::TimeStyle::Segment}) {
    for (float height : {44.0f, 50.0f, 52.0f, 60.0f, 64.0f}) {
      auto l = gfx::timeLayout(120, 104, height, style);
      std::vector<std::vector<uint16_t>> glyphs;
      for (int d = 0; d < 10; ++d) {
        frame.fillSprite(0);
        gfx::drawTime(frame, l, d * 11, d * 11, true, false, col::WHITE);
        int ink = 0, outside = 0;
        for (int y = 0; y < 240; ++y)
          for (int x = 0; x < 240; ++x) if (frame.readPixel(x,y)) {
            ++ink;
            outside += x < floorf(l.x)-1 || x > ceilf(l.x+l.width)+1 ||
                       y < floorf(l.y)-1 || y > ceilf(l.y+l.height)+1;
          }
        bool unique = std::find(glyphs.begin(), glyphs.end(), frame.buf) == glyphs.end();
        if (!unique || outside || ink < 100) {
          printf("  NUMERAL digit %d height %.0f: ink %d, outside %d, unique %d\n", d, height, ink, outside, unique);
          ++problems;
        }
        glyphs.push_back(frame.buf);
      }
      frame.fillSprite(0);
      gfx::drawTime(frame, l, 9, 41, true, true, col::WHITE);
      int leading = 0;
      for (int y=0; y<240; ++y)
        for (int x=(int)floorf(l.x); x<(int)floorf(l.x+l.digitW); ++x)
          leading += frame.readPixel(x,y) != 0;
      if (leading) { printf("  NUMERAL blank leading cell has %d pixels\n", leading); ++problems; }
    }
  }
  // The animated radar must never paint over the central time/date register.
  {
    FaceContext ctx{makeTime(10,9,31),true,false,86,SyncView::Synced,0,false,-1};
    drawFaceStatic(frame, FACE_RADAR, ctx); drawFaceLive(frame, FACE_RADAR, ctx);
    auto base = frame.buf;
    int central = 0, moving = 0;
    for (int angle=0; angle<360; angle+=9) {
      ctx.beamDeg = (float)angle;
      drawFaceStatic(frame, FACE_RADAR, ctx); drawFaceLive(frame, FACE_RADAR, ctx);
      for (int i=0; i<240*240; ++i) if (base[i] != frame.buf[i]) {
        ++moving;
        central += hypotf(i%240-120, i/240-120) < 74;
      }
    }
    if (central || moving < 1000) {
      printf("  RADAR central animation pixels %d, moving %d\n",central,moving); ++problems;
    }
  }

  // v0.13.0: every face shows a small charging bolt, and only that changes.
  {
    TFT_eSprite plain(&screen);
    plain.createSprite(240, 240);
    for (int f = 0; f < FACE_COUNT; f++) {
      FaceContext ctx{makeTime(10, 9, 31), true, false, 64, SyncView::Synced, 0, false, -1};
      drawFaceStatic(plain, (Face)f, ctx);
      drawFaceLive(plain, (Face)f, ctx);
      ctx.charging = true;
      drawFaceStatic(frame, (Face)f, ctx);
      drawFaceLive(frame, (Face)f, ctx);
      int diff = 0, x0 = 240, y0 = 240, x1 = 0, y1 = 0;
      for (int y = 0; y < 240; y++)
        for (int x = 0; x < 240; x++)
          if (plain.readPixel(x, y) != frame.readPixel(x, y)) {
            diff++;
            x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
          }
      // A bolt is about 6 x 10 pixels: some pixels change, all within one small patch.
      bool ok = diff >= 15 && diff <= 120 && x1 - x0 <= 12 && y1 - y0 <= 14;
      if (!ok) {
        printf("  CHARGE %s: %d pixels changed over %d x %d\n", faceName((Face)f), diff, x1 - x0 + 1, y1 - y0 + 1);
        problems++;
      }
      if (faceStaticKey((Face)f, ctx) == faceStaticKey((Face)f, FaceContext{makeTime(10, 9, 31), true, false, 64, SyncView::Synced, 0, false, -1})) {
        printf("  CHARGE %s: the cached dial would not redraw when charging starts\n", faceName((Face)f));
        problems++;
      }
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

  // ---- Entrances (v0.12.0) ----------------------------------------------------------------
  auto ent = [&](bool ok, const char *fmt, auto... args) {
    if (!ok) {
      printf("  ENTRANCE ");
      if constexpr (sizeof...(args) == 0) fputs(fmt, stdout);
      else printf(fmt, args...);
      printf("\n");
      problems++;
    }
  };
  TFT_eSprite other(&screen);
  other.createSprite(240, 240);
  auto draw = [&](TFT_eSprite &into, Face f, FaceContext ctx) {
    drawFaceStatic(into, f, ctx);
    drawFaceLive(into, f, ctx);
  };
  auto at = [&](tm t, bool entering, uint32_t ms) {
    FaceContext ctx{t, true, false, 64, SyncView::Synced, 0, false, -1};
    ctx.entering = entering;
    ctx.entranceMs = ms;
    return ctx;
  };
  const uint32_t SPEC_MS[] = {0, 0, 0, 0, 0, 0, 500, 300, 900, 600, 420, 600};
  static_assert(sizeof SPEC_MS / sizeof SPEC_MS[0] == FACE_COUNT, "a length for every face");
  tm A = makeTime(10, 9, 30), B = makeTime(14, 48, 30);  // hands clear of the lower dial, seconds alike
  std::vector<uint16_t> settledA, settledB;
  for (int fi = 0; fi < FACE_COUNT; fi++) {
    Face f = (Face)fi;
    uint32_t len = faceEntranceMs(f);
    ent(len == SPEC_MS[fi], "%s entrance is %u ms (spec %u)", faceName(f), len, SPEC_MS[fi]);
    if (!len) continue;
    draw(frame, f, at(A, false, 0));
    settledA = frame.buf;
    draw(frame, f, at(B, false, 0));
    settledB = frame.buf;
    // The time's pixels: inside 96 px of the centre, where 10:09 and 14:48 differ. Orbit's
    // marker on its track (104 px) is outside, and moves in the entrance by design.
    std::vector<int> timePx;
    for (int i = 0; i < 240 * 240; i++)
      if (settledA[i] != settledB[i] && hypotf(i % 240 - 120, i / 240 - 120) < 96) timePx.push_back(i);
    ent(timePx.size() > 40, "%s: the time covers %zu px", faceName(f), timePx.size());
    // Every entrance frame, from the first, shows exactly the settled time.
    int wrong = 0, worstMs = -1;
    for (uint32_t ms = 0; ms <= len; ms += 10) {
      draw(frame, f, at(A, true, ms));
      int bad = 0;
      for (int i : timePx) bad += frame.buf[i] != settledA[i];
      if (bad > wrong) wrong = bad, worstMs = ms;
    }
    ent(wrong == 0, "%s: %d time pixels differ from the settled face at %d ms", faceName(f), wrong, worstMs);
    // The last frame is the settled face (what Motion Off draws), pixel for pixel.
    for (uint32_t ms : {len, len + 1, len + 5000}) {
      draw(frame, f, at(A, true, ms));
      int diff = 0;
      for (int i = 0; i < 240 * 240; i++) diff += frame.buf[i] != settledA[i];
      ent(diff == 0, "%s: the frame at %u ms differs from the settled face in %d px", faceName(f), ms, diff);
    }
    // Something does move: halfway through is not the settled face.
    draw(frame, f, at(A, true, len / 2));
    int moved = 0;
    for (int i = 0; i < 240 * 240; i++) moved += frame.buf[i] != settledA[i];
    ent(moved > 50, "%s: halfway through, the entrance differs from the end in only %d px", faceName(f), moved);
    // Storyboard: 0 ms, a quarter, half, three quarters, and the final state.
    for (int k = 0; k <= 4; k++) {
      uint32_t ms = len * k / 4;
      draw(frame, f, at(A, k < 4, ms));
      char path[256];
      snprintf(path, sizeof path, "%s/storyboard_%s_%d_%04ums.ppm", outDir, faceName(f), k, k < 4 ? ms : len);
      writePpm(path, frame);
    }
  }
  // Old faces are untouched by entrance fields.
  for (int fi = 0; fi < FACE_PHANTOM; fi++) {
    draw(frame, (Face)fi, at(A, false, 0));
    draw(other, (Face)fi, at(A, true, 0));
    ent(frame.buf == other.buf, "%s changes with entrance fields it does not use", faceName((Face)fi));
  }

  // Yozakura: no petal ever enters the time and date block, checked on the pure paths every
  // millisecond (with the petal's reach in any rotation) and on the drawn pixels every 2 ms.
  {
    using namespace yozakura;
    int inside = 0, firstMs = -1;
    for (uint32_t ms = 0; ms <= ENTRANCE_MS + 50; ms++)
      for (int i = 0; i < PETALS; i++) {
        Petal p = petalAt(i, ms);
        if (!p.visible) continue;
        float reach = p.size * 1.05f + 1;
        if (p.x + reach > PROTECT_X0 && p.x - reach < PROTECT_X1 && p.y + reach > PROTECT_Y0 && p.y - reach < PROTECT_Y1) {
          if (firstMs < 0) firstMs = ms;
          inside++;
        }
      }
    ent(inside == 0, "Yozakura: a petal reaches into the protected block %d times, first at %d ms", inside, firstMs);
    bool any = false;
    for (int i = 0; i < PETALS; i++) any |= petalAt(i, 400).visible;
    ent(any && !petalAt(0, ENTRANCE_MS).visible && !petalAt(1, ENTRANCE_MS).visible, "Yozakura: petals fall, and are gone at the end");
    draw(frame, FACE_YOZAKURA, at(A, false, 0));
    settledA = frame.buf;
    int px = 0;
    for (uint32_t ms = 0; ms <= ENTRANCE_MS; ms += 2) {
      draw(frame, FACE_YOZAKURA, at(A, true, ms));
      for (int y = PROTECT_Y0; y <= PROTECT_Y1; y++)
        for (int x = PROTECT_X0; x <= PROTECT_X1; x++) px += frame.buf[y * 240 + x] != settledA[y * 240 + x];
    }
    ent(px == 0, "Yozakura: %d decorative pixels drawn inside the time and date block", px);
  }

  // Orbit's marker: eases its last 25 degrees into the true minute, never past it, and moves 6
  // degrees at a new minute.
  {
    FaceContext c = at(A, true, 150);
    float m = 9 * 6, last = -1e9f;
    bool mono = true;
    for (uint32_t ms = 150; ms <= 600; ms++) {
      c.entranceMs = ms;
      float d = orbit::markerDeg(m, c);
      mono &= d >= last && d <= m;
      last = d;
    }
    c.entranceMs = 150;
    ent(fabsf(orbit::markerDeg(m, c) - (m - 25)) < 0.01f && mono && last == m && !orbit::markerShown(at(A, true, 149)),
        "Orbit: the marker appears at 150 ms 25 degrees back and eases forward into the minute");
    c = at(A, false, 0);
    c.markerMoving = true;
    c.markerMs = 0;
    float d0 = orbit::markerDeg(m, c);
    c.markerMs = 90;
    float d1 = orbit::markerDeg(m, c);
    c.markerMs = motion::MARKER_MS;
    ent(fabsf(d0 - (m - 6)) < 0.01f && d1 > m - 6 && d1 < m && orbit::markerDeg(m, c) == m,
        "Orbit: at a new minute the marker moves 6 degrees");
  }

  printf("%d text edge or state problems\n", problems);
  return problems ? 1 : 0;
}
