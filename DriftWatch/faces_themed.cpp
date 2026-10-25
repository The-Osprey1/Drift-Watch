// Themed faces (v0.12.0): a digital and an analog face for each of three themes.
//   Space    Orbit      ivory time, lavender date, a cyan minute marker on an orbit track
//            Lunar      silver hands on navy, a pale blue seconds hand and a moon complication
//   Sakura   Yozakura   ivory time on a night disc between two blossoming branches
//            Hanami     plum hands on porcelain, a branch along the lower left
//   Fallout  Wasteland  phosphor green segments with the unlit ones still faintly there
//            Mojave     cream hands on dark leather, amber markers
//
// Each face has one finite entrance, played once after the face is chosen (ui.cpp decides when)
// and never on wake. The time is correct from the entrance's first frame: only decoration moves.
// Everything an entrance moves is drawn in the live layer, from the same code in every frame, so
// a settled face (entering = false) is exactly the entrance's last frame. That settled state is
// also what Motion Off draws.
//
// Geometry and timing follow the host prototypes in MOTION-SPEC.md (native 240 x 240).
#include <math.h>
#include "face_parts.h"
#include "faces.h"
#include "gfx.h"
#include "theme.h"

using namespace layout;
using faceparts::dateText;
using faceparts::noTimeHint;

namespace {
constexpr uint32_t SETTLED = 0x7FFFFFFF;  // timeline position of a face that is not entering

uint32_t timeline(const FaceContext &c) { return c.entering ? c.entranceMs : SETTLED; }

// 0..1 progress of a part of an entrance that starts at `start` and lasts `dur` ms.
float seg(uint32_t t, uint32_t start, uint32_t dur) {
  if (t <= start) return 0;
  if (dur == 0 || t - start >= dur) return 1;
  return (t - start) / (float)dur;
}
float lerp(float a, float b, float p) { return a + (b - a) * p; }

Pt polarAt(float cx, float cy, float r, float deg) {
  float a = deg * DEG_TO_RAD;
  return {cx + r * sinf(a), cy - r * cosf(a)};
}

uint16_t batteryInk(int pct, uint16_t ink) { return pct >= 0 && pct <= 15 ? col::WARN : ink; }

void percentText(char *buf, size_t n, int pct, bool sign) {
  if (pct < 0) snprintf(buf, n, "--");
  else snprintf(buf, n, sign ? "%d%%" : "%d", pct);
}

// Date with AM or PM after it in 12-hour mode.
void dateLine(const FaceContext &c, char *buf, size_t n) {
  dateText(c, buf, n);
  if (c.use12h) strncat(buf, c.now.tm_hour < 12 ? "  AM" : "  PM", n - strlen(buf) - 1);
}

// "SET TIME" or "SYNCING" where the date goes, and the hint at (hx, hy).
void noTimeLines(TFT_eSprite &s, const FaceContext &c, int y, int hx, int hy, uint16_t ink, uint16_t busy, uint16_t hint) {
  bool syncing = c.sync == SyncView::Syncing;
  gfx::text(s, syncing ? "SYNCING" : "SET TIME", CX, y, type::SMALL, syncing ? busy : ink);
  gfx::text(s, noTimeHint(c), hx, hy, type::SMALL, syncing ? busy : hint);
}

// Small battery outline with its fill, top-left at (x, y).
void miniBattery(TFT_eSprite &s, int x, int y, int w, int h, int pct, uint16_t c) {
  s.drawRect(x, y, w + 1, h + 1, c);
  s.fillRect(x + w + 1, y + h * 3 / 10 + 1, 2, h * 4 / 10 + 1, c);
  if (pct > 0) s.fillRect(x + 2, y + 2, max(1, (w - 3) * pct / 100), h - 3, c);
  if (pct < 0) s.drawFastHLine(x + 4, y + h / 2, w - 7, c);
}

// ---- Sakura pieces --------------------------------------------------------------------------
// Petal: a rounded teardrop with a notch at its tip, made once as a unit outline.
constexpr int PETAL_N = 28;
Pt petalUnit[PETAL_N + 1];
bool petalReady = false;

void makePetal() {
  for (int i = 0; i <= PETAL_N; i++) {
    float th = -1.5707963f + 6.2831853f * i / PETAL_N;
    float px = cosf(th) * 0.62f, py = sinf(th);
    if (py < -0.55f) {  // the notch, at the tip
      float k = (-py - 0.55f) / 0.45f;
      px *= 1 + 0.25f * k;
      py += 0.18f * k * (1 - fabsf(px) / 0.62f);
    }
    petalUnit[i] = {px, py};
  }
  petalReady = true;
}

void petal(TFT_eSprite &s, float x, float y, float size, float rotDeg, uint16_t c) {
  if (!petalReady) makePetal();
  float a = rotDeg * DEG_TO_RAD, ca = cosf(a), sa = sinf(a);
  Pt p[PETAL_N + 2];
  p[0] = {x, y};  // fanned from the centre, which sees the whole outline
  for (int i = 0; i <= PETAL_N; i++) {
    float u = petalUnit[i].x * size, v = petalUnit[i].y * size;
    p[i + 1] = {x + u * ca - v * sa, y + u * sa + v * ca};
  }
  gfx::fillPoly(s, p, PETAL_N + 2, c);
}

// Five-petal blossom, `open` 0..1 as it opens. `edge` 0 = no edge.
void blossom(TFT_eSprite &s, float x, float y, float r, uint16_t petalC, uint16_t centre, float rot, float open,
             uint16_t edge) {
  if (open <= 0) return;
  float pr = r * motion::easeOut(open);
  for (int i = 0; i < 5; i++) {
    float a = rot + i * 72;
    Pt q = polarAt(x, y, pr * 0.62f, a);
    if (edge) petal(s, q.x, q.y, pr * 0.72f + 0.7f, a + 180, edge);
    petal(s, q.x, q.y, pr * 0.72f, a + 180, petalC);
  }
  gfx::dot(s, x, y, fmaxf(0.6f, pr * 0.22f), centre);
}

// Tapered branch along `pts`, revealed from its base by `progress` 0..1.
void branch(TFT_eSprite &s, const Pt *pts, int n, float w0, float w1, uint16_t c, float progress) {
  if (progress <= 0) return;
  float total = 0;
  for (int i = 0; i + 1 < n; i++) total += hypotf(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
  float target = total * fminf(1, progress), run = 0;
  for (int i = 0; i + 1 < n && run < target; i++) {
    float len = hypotf(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
    float f = fminf(1, (target - run) / len);
    float ex = lerp(pts[i].x, pts[i + 1].x, f), ey = lerp(pts[i].y, pts[i + 1].y, f);
    float ra = lerp(w0, w1, run / total) / 2, rb = lerp(w0, w1, (run + len * f) / total) / 2;
    s.drawWedgeLine(pts[i].x, pts[i].y, ex, ey, ra, rb, c);
    run += len;
  }
}

// Seconds hand: a thin needle with a short tail, a round pivot and a dark centre.
void needle(TFT_eSprite &s, float deg, float len, float tail, float w, uint16_t c, uint16_t face) {
  Pt a = gfx::polar(deg + 180, tail), b = gfx::polar(deg, len);
  s.drawWideLine(a.x, a.y, b.x, b.y, w, c, face);
}
void pivot(TFT_eSprite &s, uint16_t c, uint16_t face) {
  s.fillSmoothCircle(CX, CY, 3, c, face);
  s.fillSmoothCircle(CX, CY, 1, face, c);
}

// Hour and minute hands at the exact time: the minute hand carries the seconds, the hour hand
// the minutes. The minute hand's halo keeps it distinct where it crosses the hour hand.
void hands(TFT_eSprite &s, const FaceContext &c, uint16_t fill, uint16_t inlay, uint16_t face, float hourW, float minW) {
  float m = c.now.tm_min + c.now.tm_sec / 60.0f, h = (c.now.tm_hour % 12) + m / 60;
  faceparts::swordHand(s, h * 30, 54, 11, hourW, fill, inlay, 0, false, face);
  faceparts::swordHand(s, m * 6, 90, 13, minW, fill, inlay, 0, true, face);
}

// Words on an analog face without a time: no hands, two lines in the middle.
void dialNoTime(TFT_eSprite &s, const FaceContext &c, uint16_t ink, uint16_t busy, uint16_t hint) {
  noTimeLines(s, c, 100, CX, 136, ink, busy, hint);
}

void dateWindow(TFT_eSprite &s, const FaceContext &c, int x, int y, int w, int h, uint16_t fill, uint16_t edge, uint16_t ink) {
  s.fillRect(x, y, w, h, fill);
  s.drawRect(x, y, w, h, edge);
  char buf[8];
  if (c.timeValid) snprintf(buf, sizeof buf, "%d", c.now.tm_mday);
  else strcpy(buf, "--");
  gfx::text(s, buf, x + w / 2, y + h / 2 + 1, type::SMALL, ink);
}

// ---- Orbit (Space, digital) -------------------------------------------------------------------
namespace orb {
const uint16_t IVORY = rgb(0xECF0F8), LAV = rgb(0xA8A0E8), TRACK = rgb(0x26385C), TRAIL = rgb(0x005A6E);
const uint16_t MARKER_GLOW = rgb(0x005064), INK = rgb(0x96A0BE), DASH = rgb(0x4A4E5A);
const uint16_t PLANET = rgb(0x10162C), ATMOS = rgb(0x4678D2), RING_FRONT = rgb(0x96A5C8), RING_BACK = rgb(0x46506E);
const uint16_t STAR_NEAR = rgb(0xBEC8DC), STAR_FAR = rgb(0x505A6E);
constexpr float PX = 168, PY = 172, PR = 30;

float rnd(uint32_t &state) {
  state = state * 1103515245u + 12345u;
  return ((state >> 8) & 0xFFFF) / 65536.0f;
}

void drawStatic(TFT_eSprite &s, const FaceContext &c) {
  // Stars, kept away from the time, date and planet.
  uint32_t r = 7;
  for (int i = 0; i < 26; i++) {
    Pt p{};
    for (int k = 0; k < 20; k++) {
      p = polarAt(CX, CY, 20 + rnd(r) * 92, rnd(r) * 360);
      bool nearText = p.x > 30 && p.x < 212 && p.y > 62 && p.y < 160;
      if (!nearText && hypotf(p.x - PX, p.y - PY) >= 46) break;
    }
    bool far = rnd(r) < 0.6f;
    gfx::dot(s, p.x, p.y, far ? 0.7f : 1.3f, far ? STAR_FAR : STAR_NEAR, col::BG);
  }
  // Planet with a thin lit rim towards the upper left, and a tilted ring.
  s.fillSmoothCircle(PX, PY, PR, PLANET, col::BG);
  gfx::arcAt(s, PX, PY, PR, PR - 1, 250, 380, ATMOS, PLANET);
  Pt ring[60];
  const float tilt = -22 * DEG_TO_RAD, ct = cosf(tilt), st = sinf(tilt);
  for (int i = 0; i < 60; i++) {
    float a = 6.2831853f * i / 60, x = cosf(a) * 44, y = sinf(a) * 9;
    ring[i] = {PX + x * ct - y * st, PY + x * st + y * ct};
  }
  for (int i = 0; i < 60; i++) {
    const Pt &a = ring[i], &b = ring[(i + 1) % 60];
    bool front = sinf(6.2831853f * i / 60) > 0;
    if (front) s.drawWideLine(a.x, a.y, b.x, b.y, 1.6f, RING_FRONT);
    else if (hypotf(a.x - PX, a.y - PY) > PR + 1) s.drawWideLine(a.x, a.y, b.x, b.y, 1.2f, RING_BACK);
  }

  char buf[28];
  percentText(buf, sizeof buf, c.batteryPct, true);
  uint16_t bat = batteryInk(c.batteryPct, INK);
  miniBattery(s, 104, 36, 16, 8, c.batteryPct, bat);
  if (c.charging) gfx::chargeBolt(s, 97, 40, 10, bat);
  gfx::text(s, buf, 141, 40, type::SMALL, bat);

  gfx::TimeLayout tl = gfx::timeLayout(CX, 70 + 26, 52);
  gfx::drawTime(s, tl, faceparts::displayHour(c), c.now.tm_min, c.timeValid, c.use12h, c.timeValid ? IVORY : DASH);
  if (c.timeValid) {
    dateLine(c, buf, sizeof buf);
    gfx::text(s, buf, CX, 142, type::SMALL, LAV);
  } else {
    noTimeLines(s, c, 142, CX, 58, col::N300, col::CYAN, col::N400);
  }
}

void drawLive(TFT_eSprite &s, const FaceContext &c) {
  uint32_t e = timeline(c);
  // The track draws clockwise from 12 over 300 ms.
  float tp = motion::easeOut(seg(e, 0, 300));
  if (tp > 0) gfx::arc(s, (int)orbit::TRACK_R, (int)orbit::TRACK_R - 1, 0, 360 * tp, TRACK);
  if (!c.timeValid || !orbit::markerShown(c)) return;
  float deg = orbit::markerDeg(c.now.tm_min * 6, c);
  if (c.entering && e < orbit::ENTRANCE_MS) {
    float mp = motion::easeOut(seg(e, 150, 350));
    gfx::arc(s, (int)orbit::TRACK_R + 1, (int)orbit::TRACK_R - 1, deg - 14 * (1 - mp), deg, TRAIL);  // fading trail
  }
  Pt m = gfx::polar(deg, orbit::TRACK_R);
  gfx::dot(s, m.x, m.y, 4.2f, MARKER_GLOW, col::BG);
  gfx::dot(s, m.x, m.y, 2.6f, col::CYAN);
}
}  // namespace orb

// ---- Lunar (Space, analog) --------------------------------------------------------------------
namespace lun {
const uint16_t BG = rgb(0x0A0E1E), SILVER = rgb(0xCDD2DC), DIM = rgb(0x464E64), SEC = rgb(0x96BEFF);
const uint16_t TRACK = rgb(0x283046), WINDOW = rgb(0x141A30), MOON_BG = rgb(0x0E1224), MOON_EDGE = rgb(0x5A6482);
const uint16_t MOON = rgb(0xD7DCE6), CRATER = rgb(0xAFB6C4);
constexpr int MOON_X = CX, MOON_Y = 164, MOON_R = 20;  // (MR is a register name in the ESP32 headers)

void drawStatic(TFT_eSprite &s, const FaceContext &c) {
  s.fillCircle(CX, CY, 120, BG);
  for (int h = 0; h < 12; h++) {
    if (h == 0) {
      for (int dx : {-4, 4}) s.drawWideLine(CX + dx, CY - 108, CX + dx, CY - 92, 3, SILVER, BG);
    } else {
      Pt a = gfx::polar(h * 30, 108), b = gfx::polar(h * 30, 94);
      s.drawWideLine(a.x, a.y, b.x, b.y, 3, SILVER, BG);
    }
  }
  for (int m = 0; m < 60; m++) {
    if (m % 5 == 0) continue;
    Pt d = gfx::polar(m * 6, 108);
    gfx::dot(s, d.x, d.y, 0.8f, DIM, BG);
  }
  // Battery ring above the centre.
  gfx::arcAt(s, CX, 72, 14, 12, 0, 360, TRACK, BG);
  if (c.batteryPct > 0) gfx::arcAt(s, CX, 72, 14, 12, 0, 360 * c.batteryPct / 100.0f, batteryInk(c.batteryPct, SEC), BG);
  char buf[8];
  percentText(buf, sizeof buf, c.batteryPct, false);
  gfx::text(s, buf, CX, 72, type::SMALL, batteryInk(c.batteryPct, SILVER));
  if (c.charging) gfx::chargeBolt(s, CX + 22, 72, 10, batteryInk(c.batteryPct, SEC));
  dateWindow(s, c, 166, 112, 25, 17, WINDOW, DIM, SILVER);
  if (!c.timeValid) dialNoTime(s, c, col::N300, col::CYAN, col::N400);
}

// Moon complication below the centre, revealed by a clockwise wipe over the first 300 ms.
void moon(TFT_eSprite &s, uint32_t e) {
  s.fillSmoothCircle(MOON_X, MOON_Y, MOON_R, MOON_BG, BG);
  gfx::arcAt(s, MOON_X, MOON_Y, MOON_R, MOON_R - 1, 0, 360, MOON_EDGE, BG);
  s.fillSmoothCircle(MOON_X, MOON_Y, 13, MOON, MOON_BG);
  gfx::dot(s, MOON_X + 5, MOON_Y - 1, 11.5f, MOON_BG);  // the crescent's shadow
  gfx::dot(s, MOON_X - 7, MOON_Y + 3, 1.6f, CRATER);
  gfx::dot(s, MOON_X - 4, MOON_Y - 5, 1.1f, CRATER);
  gfx::dot(s, MOON_X - 9, MOON_Y - 3, 0.9f, CRATER);
  float wipe = motion::easeOut(seg(e, 0, 300));
  if (wipe < 1) gfx::arcAt(s, MOON_X, MOON_Y, MOON_R + 1, 0, 360 * wipe, 360, BG, BG);  // not yet revealed
}

void drawLive(TFT_eSprite &s, const FaceContext &c) {
  moon(s, timeline(c));
  if (c.timeValid) {
    hands(s, c, SILVER, SILVER, BG, 4.8f, 3.5f);
    needle(s, c.now.tm_sec * 6, 96, 16, 1.6f, SEC, BG);
  }
  pivot(s, SEC, BG);
}
}  // namespace lun

// ---- Yozakura (Sakura, digital, night) ----------------------------------------------------------
namespace yoz {
const uint16_t BG = rgb(0x140E1A), DISC = rgb(0x22182C), DISC_EDGE = rgb(0x342642), IVORY = rgb(0xFAEEDC);
const uint16_t PINK = rgb(0xDE8CA5), PINK_HI = rgb(0xF2BECD), ROSE = rgb(0xE296AA), BARK = rgb(0x4A303C);
const uint16_t PETAL_EDGE = rgb(0x78465F), INK = rgb(0x96788C);
const Pt BR_TR[] = {{236, 46}, {206, 56}, {182, 60}, {164, 70}, {152, 84}};
const Pt BR_BL[] = {{4, 190}, {34, 184}, {58, 178}, {78, 170}, {90, 160}};

void drawStatic(TFT_eSprite &s, const FaceContext &c) {
  s.fillCircle(CX, CY, 120, BG);
  s.fillSmoothCircle(CX, CY, 78, DISC, BG);
  gfx::arc(s, 79, 78, 0, 360, DISC_EDGE, true, BG);
  branch(s, BR_TR, 5, 5, 1.8f, BARK, 1);
  branch(s, BR_BL, 5, 5, 1.8f, BARK, 1);
  static const float BLOSSOMS[][4] = {{206, 56, 9, 10}, {182, 62, 7, 40}, {160, 76, 6, 70},
                                      {34, 184, 9, 20}, {60, 178, 7, 55}, {82, 168, 6, 80}};
  for (const auto &b : BLOSSOMS) {
    blossom(s, b[0], b[1], b[2], PINK, IVORY, b[3], 1, PETAL_EDGE);
    blossom(s, b[0], b[1], b[2] * 0.55f, PINK_HI, IVORY, b[3], 1, 0);
  }
  char buf[28];
  percentText(buf, sizeof buf, c.batteryPct, true);
  uint16_t bat = batteryInk(c.batteryPct, INK);
  miniBattery(s, 98, 52, 14, 7, c.batteryPct, bat);
  if (c.charging) gfx::chargeBolt(s, 91, 56, 9, bat);
  gfx::text(s, buf, 119, 56, type::SMALL, bat, ML_DATUM);

  if (c.timeValid) {
    gfx::TimeLayout tl = gfx::timeLayout(CX, 84 + 23, 46);
    gfx::drawTime(s, tl, faceparts::displayHour(c), c.now.tm_min, true, c.use12h, IVORY);
    dateLine(c, buf, sizeof buf);
    gfx::text(s, buf, CX, 150, type::SMALL, ROSE);
    blossom(s, CX, 180, 6, PINK, IVORY, 15, 1, 0);  // the seal under the date
  } else {
    // The disc is too narrow at the bottom for the hint beside the blossoms, so the words take
    // the time's place and the hint the date's.
    bool syncing = c.sync == SyncView::Syncing;
    gfx::text(s, syncing ? "SYNCING" : "SET TIME", CX, 108, type::MEDIUM, syncing ? PINK_HI : ROSE);
    gfx::text(s, noTimeHint(c), CX, 150, type::SMALL, syncing ? PINK_HI : INK);
  }
}

void drawLive(TFT_eSprite &s, const FaceContext &c) {
  uint32_t e = timeline(c);
  for (int i = 0; i < yozakura::PETALS; i++) {
    yozakura::Petal p = yozakura::petalAt(i, e);
    if (p.visible) petal(s, p.x, p.y, p.size, p.rot, gfx::blend(PINK_HI, BG, p.alpha));
  }
}
}  // namespace yoz

// ---- Hanami (Sakura, analog, porcelain) ---------------------------------------------------------
namespace han {
const uint16_t BG = rgb(0xF3E9DC), PLUM = rgb(0x461E3C), ROSE = rgb(0xCC7086), PINK = rgb(0xECAABA);
const uint16_t RING = rgb(0xDECEBE), WINDOW = rgb(0xFFFAF4), BARK = rgb(0x522E3A), HINT = rgb(0x7A5A6A);
const Pt BRANCH[] = {{14, 148}, {34, 178}, {60, 200}, {88, 214}, {112, 222}};

void drawStatic(TFT_eSprite &s, const FaceContext &c) {
  s.fillCircle(CX, CY, 120, BG);
  gfx::arc(s, 111, 110, 0, 360, RING, true, BG);
  for (int h = 0; h < 12; h++) {
    Pt p = gfx::polar(h * 30, 98);
    if (h == 0 || h == 6 || h == 9) blossom(s, p.x, p.y, 6, PINK, PLUM, h * 30, 1, 0);
    else if (h != 3) gfx::dot(s, p.x, p.y, 1.8f, PLUM, BG);
  }
  dateWindow(s, c, 164, 112, 27, 17, WINDOW, PLUM, PLUM);
  gfx::arcAt(s, CX, 164, 13, 11, 0, 360, RING, BG);
  if (c.batteryPct > 0) gfx::arcAt(s, CX, 164, 13, 11, 0, 360 * c.batteryPct / 100.0f, batteryInk(c.batteryPct, ROSE), BG);
  char buf[8];
  percentText(buf, sizeof buf, c.batteryPct, false);
  gfx::text(s, buf, CX, 164, type::SMALL, batteryInk(c.batteryPct, PLUM));
  if (c.charging) gfx::chargeBolt(s, CX + 21, 164, 10, batteryInk(c.batteryPct, ROSE));
  if (!c.timeValid) dialNoTime(s, c, PLUM, ROSE, HINT);
}

void drawLive(TFT_eSprite &s, const FaceContext &c) {
  uint32_t e = timeline(c);
  // The branch grows from its base over 350 ms, then its blossoms open in two groups.
  branch(s, BRANCH, 5, 4.5f, 1.6f, BARK, motion::easeOut(seg(e, 0, 350)));
  float g1 = seg(e, 200, 250), g2 = seg(e, 330, 270);
  blossom(s, 36, 176, 8, PINK, PLUM, 20, g1, ROSE);
  blossom(s, 56, 196, 6.5f, PINK, PLUM, 50, g1, ROSE);
  blossom(s, 88, 210, 7.5f, PINK, PLUM, 10, g2, ROSE);
  blossom(s, 108, 218, 5.5f, PINK, PLUM, 75, g2, ROSE);
  if (c.timeValid) {
    hands(s, c, PLUM, PLUM, BG, 4.8f, 3.5f);
    needle(s, c.now.tm_sec * 6, 96, 16, 1.6f, ROSE, BG);
  }
  pivot(s, ROSE, BG);
}
}  // namespace han

// ---- Wasteland (Fallout, digital, phosphor) -------------------------------------------------------
namespace wst {
const uint16_t BG = rgb(0x030804), GREEN = rgb(0x5CFF7A), FRAME = rgb(0x28964A), DIM = rgb(0x185C2A);
const uint16_t GHOST = rgb(0x0A2210), ORANGE = rgb(0xFF8C28);

void drawStatic(TFT_eSprite &s, const FaceContext &c) {
  s.fillCircle(CX, CY, 120, BG);
  gfx::TimeLayout tl = gfx::timeLayout(CX, 74 + 25, 50);
  gfx::drawTime(s, tl, faceparts::displayHour(c), c.now.tm_min, c.timeValid, c.use12h, c.timeValid ? GREEN : DIM, GHOST);
  char buf[28];
  if (c.timeValid) {
    dateLine(c, buf, sizeof buf);
    gfx::text(s, buf, CX, 142, type::SMALL, DIM);
  } else {
    bool syncing = c.sync == SyncView::Syncing;
    gfx::text(s, syncing ? "SYNCING" : "SET TIME", CX, 142, type::SMALL, syncing ? GREEN : FRAME);
  }
  gfx::text(s, "PWR", 70, 50, type::SMALL, DIM);
}

void drawLive(TFT_eSprite &s, const FaceContext &c) {
  uint32_t e = timeline(c);
  // Dividers extend from the centre (0-160 ms), then the brackets grow from their middles.
  float dp = motion::easeOut(seg(e, 0, 160));
  if (dp > 0) {
    float half = 74 * dp;
    for (int y : {62, 160}) s.drawWideLine(CX - half, y, CX + half, y, 1.4f, FRAME, BG);
  }
  float bp = motion::easeOut(seg(e, 160, 100));
  if (bp > 0) {
    for (int side : {-1, 1}) {
      float x = side < 0 ? 40 : 200, hh = 34 * bp;
      s.drawWideLine(x, 110 - hh, x, 110 + hh, 1.6f, FRAME, BG);
      if (bp > 0.95f)
        for (int y : {76, 144}) s.drawWideLine(x, y, x - 6 * side, y, 1.6f, FRAME, BG);
    }
  }
  // Battery segments fill to the real level (220-400 ms).
  int lit = c.batteryPct < 0 ? 0 : (c.batteryPct * 8 + 50) / 100;
  float sp = seg(e, 220, 180);
  int shown = sp > 0 ? (int)ceilf(lit * sp - 0.001f) : 0;
  uint16_t on = batteryInk(c.batteryPct, GREEN);
  for (int i = 0; i < 8; i++) {
    int x0 = 90 + i * 11;
    if (i < shown) s.fillRect(x0, 46, 9, 9, on);
    else s.drawRect(x0, 46, 9, 9, DIM);
  }
  if (c.charging) gfx::chargeBolt(s, 82, 50, 10, on);
  // The status line appears whole at 400 ms.
  if (e < 400) return;
  if (!c.timeValid) {
    gfx::text(s, noTimeHint(c), CX, 176, type::SMALL, c.sync == SyncView::Syncing ? GREEN : FRAME);
    return;
  }
  char buf[16];
  uint16_t k = GREEN;
  switch (c.sync) {
    case SyncView::Synced: strcpy(buf, "SYNC OK"); break;
    case SyncView::Syncing: snprintf(buf, sizeof buf, "SYNC %d/3", c.syncStage ? c.syncStage : 1); break;
    case SyncView::Failed:
      strcpy(buf, "SYNC FAIL");
      k = ORANGE;  // a failure is orange text, never a flicker
      break;
    default:
      strcpy(buf, "NO SYNC");
      k = DIM;
      break;
  }
  gfx::text(s, buf, 60, 172, type::SMALL, k, ML_DATUM);
  percentText(buf, sizeof buf, c.batteryPct, true);
  gfx::text(s, buf, 180, 172, type::SMALL, on, MR_DATUM);
}
}  // namespace wst

// ---- Mojave (Fallout, analog, amber) --------------------------------------------------------------
namespace moj {
const uint16_t BG = rgb(0x22160E), AMBER = rgb(0xF0A030), AMBER_DIM = rgb(0x603E1C), CREAM = rgb(0xF5E6C8);
const uint16_t WINDOW = rgb(0x140C08);

void drawStatic(TFT_eSprite &s, const FaceContext &c) {
  s.fillCircle(CX, CY, 120, BG);
  gfx::arc(s, 111, 109, 0, 360, AMBER_DIM, true, BG);
  for (int h = 0; h < 12; h++) {
    float a = h * 30;
    if (h == 0) gfx::radialBar(s, a, 86, 106, 6, AMBER);
    else if (h == 6 || h == 9) gfx::radialBar(s, a, 90, 106, 4, AMBER);
    else if (h != 3) {
      Pt p = gfx::polar(a, 100);
      gfx::dot(s, p.x, p.y, 2, CREAM, BG);
    }
  }
  for (int m = 0; m < 60; m++) {
    if (m % 5 == 0) continue;
    Pt a = gfx::polar(m * 6, 108), b = gfx::polar(m * 6, 104);
    s.drawWideLine(a.x, a.y, b.x, b.y, 1, AMBER_DIM, BG);
  }
  dateWindow(s, c, 166, 111, 27, 19, WINDOW, AMBER, CREAM);
  gfx::arcAt(s, CX, 166, 16, 13, 0, 360, AMBER_DIM, BG);
  if (!c.timeValid) dialNoTime(s, c, CREAM, AMBER, col::N300);
}

void drawLive(TFT_eSprite &s, const FaceContext &c) {
  uint32_t e = timeline(c);
  // A short highlight travels over the top of the rim (0-450 ms) and fades out by 600 ms.
  if (e < 600) {
    float a = lerp(300, 420, motion::easeOut(seg(e, 0, 450)));
    gfx::arc(s, 111, 109, a - 24, a, gfx::blend(AMBER, AMBER_DIM, 1 - seg(e, 450, 150)), true, BG);
  }
  // The battery arc fills to the real value over 300 ms.
  float fp = motion::easeOut(seg(e, 0, 300));
  if (c.batteryPct > 0 && fp > 0) gfx::arcAt(s, CX, 166, 16, 13, 0, 360 * c.batteryPct / 100.0f * fp, batteryInk(c.batteryPct, AMBER), BG);
  char buf[8];
  percentText(buf, sizeof buf, c.batteryPct, false);
  gfx::text(s, buf, CX, 166, type::SMALL, batteryInk(c.batteryPct, CREAM));
  if (c.charging) gfx::chargeBolt(s, CX + 25, 166, 10, batteryInk(c.batteryPct, AMBER));
  if (c.timeValid) {
    hands(s, c, CREAM, CREAM, BG, 5.5f, 4.0f);
    needle(s, c.now.tm_sec * 6, 98, 16, 1.6f, AMBER, BG);
  }
  pivot(s, AMBER, BG);
}
}  // namespace moj
}  // namespace

namespace faceparts {
void themedStatic(TFT_eSprite &s, Face f, const FaceContext &c) {
  switch (f) {
    case FACE_ORBIT: orb::drawStatic(s, c); break;
    case FACE_LUNAR: lun::drawStatic(s, c); break;
    case FACE_YOZAKURA: yoz::drawStatic(s, c); break;
    case FACE_HANAMI: han::drawStatic(s, c); break;
    case FACE_WASTELAND: wst::drawStatic(s, c); break;
    case FACE_MOJAVE: moj::drawStatic(s, c); break;
    default: break;
  }
}

void themedLive(TFT_eSprite &s, Face f, const FaceContext &c) {
  switch (f) {
    case FACE_ORBIT: orb::drawLive(s, c); break;
    case FACE_LUNAR: lun::drawLive(s, c); break;
    case FACE_YOZAKURA: yoz::drawLive(s, c); break;
    case FACE_HANAMI: han::drawLive(s, c); break;
    case FACE_WASTELAND: wst::drawLive(s, c); break;
    case FACE_MOJAVE: moj::drawLive(s, c); break;
    default: break;
  }
}
}  // namespace faceparts

namespace yozakura {
Petal petalAt(int i, uint32_t ms) {
  // Start, length, quadratic Bezier control points, rotation from and to, and size.
  struct Path { uint32_t start, dur; float x0, y0, x1, y1, x2, y2, r0, r1, size; };
  static const Path P[PETALS] = {
      {0, 850, 176, 30, 214, 118, 190, 212, 0, 150, 5.2f},     // down the right side
      {150, 750, 68, 26, 26, 108, 48, 198, 20, -120, 4.4f},    // down the left side, a little later
  };
  if (i < 0 || i >= PETALS) return {false, 0, 0, 0, 0, 0};
  const Path &p = P[i];
  float q = seg(ms, p.start, p.dur);
  if (q <= 0 || q >= 1) return {false, 0, 0, 0, p.size, 0};
  float u = 1 - q;
  float x = u * u * p.x0 + 2 * u * q * p.x1 + q * q * p.x2;
  float y = u * u * p.y0 + 2 * u * q * p.y1 + q * q * p.y2;
  x += 5 * sinf(q * 3.14159265f * 2.2f);  // gentle sway
  // Never over the time and date: a petal that would cross the block is held beside it. The
  // margin covers the petal's own reach from its centre in any rotation, plus its edge pixel.
  float m = p.size * 1.1f + 2;
  if (x > PROTECT_X0 - m && x < PROTECT_X1 + m && y > PROTECT_Y0 - m && y < PROTECT_Y1 + m)
    x = x > CX ? PROTECT_X1 + m : PROTECT_X0 - m;
  float alpha = q > 0.85f ? (1 - q) / 0.15f : 1;  // fades out near the edge
  return {true, x, y, lerp(p.r0, p.r1, q), p.size, alpha};
}
}  // namespace yozakura

namespace orbit {
float markerDeg(float minuteDeg, const FaceContext &c) {
  float deg = minuteDeg;
  if (c.markerMoving) deg -= 6 * (1 - motion::easeOut(c.markerMs / (float)motion::MARKER_MS));
  if (c.entering) deg -= 25 * (1 - motion::easeOut(seg(c.entranceMs, 150, 350)));
  return deg;
}

bool markerShown(const FaceContext &c) { return !c.entering || c.entranceMs >= 150; }
}  // namespace orbit
