// Face designs. Accent hierarchy per face:
//   Digital  - silver instrument time, quiet date, yellow brackets, cyan live second.
//   Stealth  - silver baton hands and fine markers, cyan seconds hand.
//   Corpo    - yellow indices, silver baton hands, red seconds.
//   Hologram - cyan skeleton hands and indices, silver seconds.
//   Radar    - silver time, cyan hour ring, magenta blip, peripheral phosphor beam.
//   Phantom  - black and silver, a crimson jewel in the movement, the seconds hand eclipsed.
// The themed faces added in v0.12.0 (Orbit, Lunar, Yozakura, Hanami, Wasteland, Mojave) are in
// faces_themed.cpp.
// Without a set clock no face shows a warning: the time is dashed, and a calm line says how
// to set it (or what the sync is doing).
#include "faces.h"
#include <math.h>
#include "face_parts.h"
#include "gfx.h"
#include "theme.h"

using namespace layout;

namespace faceparts {
namespace {
const char *DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char *MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
}  // namespace

int displayHour(const FaceContext &c) {
  if (!c.use12h) return c.now.tm_hour;
  int h = c.now.tm_hour % 12;
  return h == 0 ? 12 : h;
}

const char *noTimeHint(const FaceContext &c) {
  if (c.sync == SyncView::Syncing) {
    static const char *stages[] = {"Syncing", "Joining Wi-Fi  1/3", "Finding zone  2/3", "Setting time  3/3"};
    return stages[c.syncStage <= 3 ? c.syncStage : 0];
  }
  return c.noWifi ? "Swipe up: set time" : "Swipe up: sync or set";
}

void dateText(const FaceContext &c, char *buf, size_t n) {
  snprintf(buf, n, "%s %d %s", DAYS[c.now.tm_wday], c.now.tm_mday, MONTHS[c.now.tm_mon]);
}

namespace {
void swordShape(Pt *p, float deg, float len, float tail, float w) {
  p[0] = gfx::alongHand(deg, -tail, -w * 0.55f);
  p[1] = gfx::alongHand(deg, -tail, w * 0.55f);
  p[2] = gfx::alongHand(deg, len * 0.72f, w);
  p[3] = gfx::alongHand(deg, len, 0);
  p[4] = gfx::alongHand(deg, len * 0.72f, -w);
}
}  // namespace

void swordHand(TFT_eSprite &s, float deg, float len, float tail, float w, uint16_t fill, uint16_t inlay,
               uint16_t edge, bool halo, uint16_t face) {
  Pt p[5];
  if (halo) {
    swordShape(p, deg, len + 1.5f, tail + 1.5f, w + 1.6f);
    gfx::fillPoly(s, p, 5, face);
  }
  if (edge) {
    swordShape(p, deg, len, tail, w);
    gfx::fillPoly(s, p, 5, edge);
    swordShape(p, deg, len - 3.5f, tail - 2, w - 2);
  } else {
    swordShape(p, deg, len, tail, w);
  }
  gfx::fillPoly(s, p, 5, fill);
  Pt in[] = {gfx::alongHand(deg, len * 0.30f, -w * 0.32f), gfx::alongHand(deg, len * 0.30f, w * 0.32f),
             gfx::alongHand(deg, len * 0.70f, w * 0.42f), gfx::alongHand(deg, len * 0.84f, 0),
             gfx::alongHand(deg, len * 0.70f, -w * 0.42f)};
  gfx::fillPoly(s, in, 5, inlay);
}
}  // namespace faceparts

namespace {
using faceparts::dateText;
using faceparts::displayHour;
using faceparts::noTimeHint;

// ---- Digital -----------------------------------------------------------------
constexpr int DIGI_TIME_Y = 104;
constexpr int DATE_X = 44, DATE_Y = 140, DATE_W = 152, DATE_H = 27;

void digitalStatic(TFT_eSprite &s, const FaceContext &c) {
  // A quiet lower register replaces the skyline. Structure belongs to the clock.
  gfx::text(s, "DRIFT", CX, 201, type::SMALL, col::N400);
  s.drawFastHLine(78, 184, 84, col::N700);

  // Seconds track: quiet structure, the live tick is the only bright part.
  for (int i = 0; i < 60; i++) {
    bool major = i % 5 == 0;
    Pt a = gfx::polar(i * 6, major ? 107 : 110), b = gfx::polar(i * 6, 114);
    s.drawWideLine(a.x, a.y, b.x, b.y, major ? 2.0f : 1.2f, major ? col::N500 : col::N700, col::BG);
  }

  gfx::battery(s, CX, 46, c.batteryPct, col::N100, c.charging);

  gfx::TimeLayout tl = gfx::timeLayout(CX, DIGI_TIME_Y, type::TIME_HERO);
  gfx::drawTime(s, tl, displayHour(c), c.now.tm_min, c.timeValid, c.use12h, c.timeValid ? col::N100 : col::N500);
  if (c.use12h && c.timeValid)
    gfx::text(s, c.now.tm_hour < 12 ? "AM" : "PM", tl.x + tl.width, tl.y - 9, type::SMALL, col::N300, MR_DATUM);

  char buf[24];
  if (c.timeValid) {
    s.drawFastVLine(DATE_X, DATE_Y + 5, DATE_H - 10, col::YELLOW);
    s.drawFastVLine(DATE_X + DATE_W, DATE_Y + 5, DATE_H - 10, col::YELLOW);
    dateText(c, buf, sizeof buf);
    gfx::text(s, buf, CX, DATE_Y + DATE_H / 2 + 1, type::SMALL, col::N100);
  } else if (c.sync == SyncView::Syncing) {
    gfx::cutBox(s, DATE_X, DATE_Y, DATE_W, DATE_H, 7, col::CYAN, false);
    gfx::cutBox(s, DATE_X + 1, DATE_Y + 1, DATE_W - 2, DATE_H - 2, 6, col::CYAN, false);
    gfx::text(s, "SYNCING", CX, DATE_Y + DATE_H / 2 + 1, type::MEDIUM, col::CYAN);
    gfx::text(s, noTimeHint(c), CX, 182, type::SMALL, col::CYAN);
  } else {
    gfx::cutBox(s, DATE_X, DATE_Y, DATE_W, DATE_H, 7, col::N600, false);
    gfx::text(s, "SET TIME", CX, DATE_Y + DATE_H / 2 + 1, type::MEDIUM, col::N300);
    gfx::text(s, noTimeHint(c), CX, 182, type::SMALL, col::N400);
  }
}

void digitalLive(TFT_eSprite &s, const FaceContext &c) {
  if (!c.timeValid) return;
  Pt a = gfx::polar(c.now.tm_sec * 6, 104), b = gfx::polar(c.now.tm_sec * 6, 116);
  s.drawWideLine(a.x, a.y, b.x, b.y, 2.5f, col::CYAN, col::BG);
}

// ---- Dials -------------------------------------------------------------------
struct DialStyle {
  uint16_t face;
  uint16_t band;            // segmented perimeter band, 0 = none
  uint16_t atmosphere;      // one faint inner ring, 0 = none
  uint16_t minorTick;
  uint16_t marker;
  float markerW, markerR0;  // marker width and inner radius
  uint16_t lume;            // dot on each marker, 0 = none
  uint16_t hourFill, minFill, handEdge;  // handEdge 0 = no edge
  uint16_t inlayHour, inlayMin;
  bool minuteHalo;          // dark outline around the minute hand where it crosses the hour hand
  uint16_t sec, pivot;
  uint16_t dateFill, dateEdge, dateInk;
  uint16_t gaugeTrack, gaugeFill, gaugeInk;
};

const DialStyle STEALTH = {
    rgb(0x0B0D12), 0, 0, col::N600, col::N100, 3.5f, 91, 0,
    col::N100, col::WHITE, 0, col::N300, col::N100, true,
    col::CYAN, col::N100,
    col::N800, col::N500, col::N100,
    col::N600, col::CYAN, col::N100};

const DialStyle CORPO = {
    rgb(0x080808), col::YELLOW_DIM, 0, col::N600, col::YELLOW, 5, 91, 0,
    col::N100, col::N100, 0, col::N100, col::N100, true,
    col::RED, col::YELLOW,
    col::N800, col::YELLOW_DIM, col::N100,
    rgb(0x2E2A04), col::YELLOW, col::YELLOW};

const DialStyle HOLO = {
    rgb(0x020A0D), 0, col::CYAN_DEEP, col::CYAN_DEEP, col::CYAN, 2.5f, 94, 0,
    col::CYAN_DEEP, col::CYAN_DEEP, col::CYAN, col::CYAN, col::WHITE, false,
    col::N100, col::CYAN,
    col::CYAN_DEEP, col::CYAN_DIM, col::N100,
    col::CYAN_DEEP, col::CYAN, col::CYAN};

const DialStyle &styleFor(Face f) { return f == FACE_CORPO ? CORPO : f == FACE_HOLO ? HOLO : STEALTH; }

void hand(TFT_eSprite &s, const DialStyle &st, float deg, float len, float tail, float w,
          uint16_t fill, uint16_t inlay, bool halo) {
  if (&st == &HOLO) {
    faceparts::swordHand(s, deg, len, tail, w, fill, inlay, st.handEdge, halo, st.face);
    return;
  }
  // Parallel baton hands distinguish the instrument dials from the floral sword hands.
  Pt a = gfx::alongHand(deg, -tail, 0), b = gfx::alongHand(deg, len, 0);
  if (halo) s.drawWideLine(a.x, a.y, b.x, b.y, w * 1.5f + 3, st.face);
  s.drawWideLine(a.x, a.y, b.x, b.y, w * 1.5f, fill);
  Pt i0 = gfx::alongHand(deg, len * 0.28f, 0), i1 = gfx::alongHand(deg, len - 5, 0);
  s.drawWideLine(i0.x, i0.y, i1.x, i1.y, w * 0.45f, inlay);
}

void dialStatic(TFT_eSprite &s, Face f, const FaceContext &c) {
  const DialStyle &st = styleFor(f);
  s.fillCircle(CX, CY, 120, st.face);

  if (st.band) {
    // Segmented band, notched at every hour.
    for (int h = 0; h < 12; h++) gfx::arc(s, 118, 116, h * 30 + 3, h * 30 + 27, st.band, false);
  }
  if (st.atmosphere) s.drawCircle(CX, CY, 70, st.atmosphere);

  for (int i = 0; i < 60; i++) {
    if (i % 5 == 0) continue;
    Pt a = gfx::polar(i * 6, 108), b = gfx::polar(i * 6, 111);
    s.drawWideLine(a.x, a.y, b.x, b.y, 1.2f, st.minorTick, st.face);
  }

  for (int h = 0; h < 12; h++) {
    if (h == 0) {
      float off = st.markerW * 0.6f + 1.5f;  // degrees, a pair at 12
      for (float d : {-off * 0.55f, off * 0.55f}) gfx::radialBar(s, d, st.markerR0, 110, st.markerW * 0.8f, st.marker);
    } else {
      gfx::radialBar(s, h * 30, st.markerR0, 110, st.markerW, st.marker);
    }
    if (st.lume) {
      Pt d = gfx::polar(h * 30, 104);
      if (h != 0) s.fillSmoothCircle(d.x, d.y, 1.6f, st.lume, st.marker);
    }
  }

  // Date window at 3 o'clock.
  char buf[8];
  const int wx = 148, wy = 109, ww = 36, wh = 23;
  if (st.dateFill != st.face) s.fillRect(wx, wy, ww, wh, st.dateFill);
  if (st.dateEdge) {
    s.drawRect(wx, wy, ww, wh, st.dateEdge);
    if (f == FACE_HOLO) s.drawFastHLine(wx + 4, wy + wh, ww - 8, st.dateEdge);
  }
  if (c.timeValid) snprintf(buf, sizeof buf, "%d", c.now.tm_mday);
  else strcpy(buf, "--");
  gfx::text(s, buf, wx + ww / 2, wy + wh / 2 + 1, type::MEDIUM, st.dateInk);

  gfx::batteryGauge(s, CX, 174, 16, c.batteryPct, st.gaugeTrack, st.gaugeFill, st.gaugeInk, c.charging);

  if (!c.timeValid) {
    // No hands without a time. Syncing is progress, not a fault, so it gets no warning sign.
    bool syncing = c.sync == SyncView::Syncing;
    gfx::text(s, syncing ? "SYNCING" : "SET TIME", CX, 70, type::SMALL, syncing ? col::CYAN : col::N300);
    gfx::text(s, noTimeHint(c), CX, 90, type::SMALL, syncing ? col::CYAN : col::N400);
    return;
  }
  gfx::text(s, f == FACE_CORPO ? "CORPO" : f == FACE_HOLO ? "HOLOGRAM" : "DRIFT", CX, 70,
            type::SMALL, col::N400);
  // Hour and minute hands move once a minute, so they live in the cached layer.
  float mn = c.now.tm_min, hr = (c.now.tm_hour % 12) + mn / 60.0f;
  hand(s, st, hr * 30, 54, 11, 4.8f, st.hourFill, st.inlayHour, false);
  hand(s, st, mn * 6, 90, 13, 3.5f, st.minFill, st.inlayMin, st.minuteHalo);
}

void dialLive(TFT_eSprite &s, Face f, const FaceContext &c) {
  const DialStyle &st = styleFor(f);
  if (c.timeValid) {
    float deg = c.now.tm_sec * 6;
    Pt tail = gfx::polar(deg + 180, 20), tip = gfx::polar(deg, 102);
    s.drawWideLine(tail.x, tail.y, tip.x, tip.y, 2.0f, st.sec, col::BG);
    Pt w0 = gfx::polar(deg + 180, 8), w1 = gfx::polar(deg + 180, 20);
    s.drawWideLine(w0.x, w0.y, w1.x, w1.y, 4.5f, st.sec, col::BG);
  }
  s.fillSmoothCircle(CX, CY, 5, st.pivot, col::BG);
  s.fillSmoothCircle(CX, CY, 2, st.face, st.pivot);
}

// ---- Radar -------------------------------------------------------------------
void radarStatic(TFT_eSprite &s, const FaceContext &c) {
  // Range rings and a cross of bearing ticks, all in logical screen space, so they follow the
  // screen turn like everything else.
  for (int r : {38, 62}) s.drawCircle(CX, CY, r, col::N800);
  s.drawCircle(CX, CY, 86, col::N700);
  for (int d = 0; d < 360; d += 90) {
    Pt a = gfx::polar(d, 78), b = gfx::polar(d, 90);
    s.drawWideLine(a.x, a.y, b.x, b.y, 1.5f, col::N600, col::BG);
  }

  // Hour ring: one segment per hour of the 12-hour cycle. Past hours dim, current bright.
  int hr12 = c.now.tm_hour % 12;
  for (int i = 0; i < 12; i++) {
    uint16_t k = !c.timeValid || i > hr12 ? col::N700 : i == hr12 ? col::CYAN : col::CYAN_DIM;
    gfx::arc(s, 117, 113, i * 30 + 1.5f, i * 30 + 28.5f, k, false);
  }

  // Minute track with a pointer at the current minute.
  gfx::arc(s, 103, 101, 0, 360, col::N700);
  if (c.timeValid) {
    float deg = c.now.tm_min * 6;
    if (c.now.tm_min > 0) gfx::arc(s, 103, 101, 0, deg, col::YELLOW_DIM);
    Pt tri[] = {gfx::alongHand(deg, 106, -4.5f), gfx::alongHand(deg, 106, 4.5f), gfx::alongHand(deg, 97, 0)};
    gfx::fillPoly(s, tri, 3, col::YELLOW);
  }
}

// Brightness of the phosphor `behind` degrees after the beam passed: exponential decay.
float phosphor(float behind) {
  if (behind < 0) behind += 360;
  return behind > motion::RADAR_TRAIL_DEG ? 0 : expf(-behind / motion::RADAR_DECAY_DEG);
}

void radarSweep(TFT_eSprite &s, float deg) {
  // The trail is a fan of thin wedges, each a little darker than the one ahead of it, drawn
  // oldest first so the beam ends on top. Kept dim so the time stays readable through it.
  constexpr float SLICE = 4, R0 = 76, R1 = 96;
  for (float behind = motion::RADAR_TRAIL_DEG; behind > 0; behind -= SLICE) {
    float a0 = deg - behind, a1 = a0 + SLICE + 0.6f;  // slight overlap: no hairline gaps
    uint16_t k = gfx::blend(col::PHOSPHOR_TRAIL, col::BG, 0.35f * phosphor(behind));
    Pt p[] = {gfx::polar(a0, R0), gfx::polar(a0, R1), gfx::polar(a1, R1), gfx::polar(a1, R0)};
    gfx::fillPoly(s, p, 4, k);
  }
  Pt a = gfx::polar(deg, R0), b = gfx::polar(deg, R1);
  s.drawWideLine(a.x, a.y, b.x, b.y, 2.0f, col::PHOSPHOR, col::BG);
}

void radarLive(TFT_eSprite &s, const FaceContext &c) {
  if (c.beamDeg >= 0) radarSweep(s, c.beamDeg);

  if (c.timeValid) {
    // The seconds dot is the blip: it flares as the beam passes over it, then fades.
    float sec = c.now.tm_sec * 6;
    Pt d = gfx::polar(sec, 94);
    float glow = c.beamDeg >= 0 ? phosphor(c.beamDeg - sec) : 0;
    if (glow > 0.05f) s.fillSmoothCircle(d.x, d.y, 3 + 3 * glow, gfx::blend(col::MAGENTA, col::BG, 0.45f * glow), col::BG);
    s.fillSmoothCircle(d.x, d.y, 3, glow > 0.5f ? col::WHITE : col::MAGENTA, col::BG);
  }

  gfx::battery(s, CX, 72, c.batteryPct, col::N300, c.charging);
  gfx::TimeLayout tl = gfx::timeLayout(CX, 114, type::TIME_INSET);
  gfx::drawTime(s, tl, displayHour(c), c.now.tm_min, c.timeValid, c.use12h, c.timeValid ? col::N100 : col::N500);

  char buf[28];
  if (c.timeValid) {
    dateText(c, buf, sizeof buf);
    if (c.use12h) strcat(buf, c.now.tm_hour < 12 ? "  AM" : "  PM");
    gfx::text(s, buf, CX, 152, type::SMALL, col::N300);
  } else {
    bool syncing = c.sync == SyncView::Syncing;
    gfx::text(s, syncing ? "SYNCING" : "SET TIME", CX, 152, type::SMALL, syncing ? col::CYAN : col::N300);
    gfx::text(s, noTimeHint(c), CX, 170, type::SMALL, syncing ? col::CYAN : col::N400);
  }
}
// ---- Phantom -----------------------------------------------------------------
// Layers, bottom to top: dial and movement plate (cached), the balance wheel and hairspring
// (each frame, clipped to the opening), the opening's lip, hour and minute hands, the seconds
// hand, the pivot. The lip is drawn over the seconds hand, so the hand slides under it.
namespace ph {
const uint16_t DIAL = rgb(0x090A0D), DIAL_EDGE = rgb(0x15171C), RECESS = rgb(0x030304);
const uint16_t PLATE = rgb(0x2B2F36), PLATE_GRAIN = rgb(0x363B43);
const uint16_t RING = rgb(0x8C939E), RING_DIM = rgb(0x3A3F47), LIP_HI = rgb(0xB9C0CA), LIP_LO = rgb(0x111316);
const uint16_t RIM = rgb(0xC8B27E), RIM_DARK = rgb(0x6E5F3E), SPRING = rgb(0x8F98A6);
const uint16_t JEWEL = 0xA004, JEWEL_HI = rgb(0xFF8FA0);
const uint16_t HAND = col::N100, HAND_EDGE = rgb(0x5A606B), SECONDS = rgb(0xE8ECF2);
constexpr float BAL_R = 70;              // balance centre, px from the dial centre at 12 o'clock
constexpr int BAL_X = CX, BAL_Y = CY - (int)BAL_R;
constexpr float RIM_R = 24, RIM_W = 3.2f;

// Inside the opening, with a margin in px from its edges (the lip and the sides).
bool inOpening(float x, float y, float margin) {
  float dx = x - CX, dy = y - CY, r = sqrtf(dx * dx + dy * dy);
  if (r < phantom::APERTURE_R0 + margin || r > phantom::APERTURE_R1 - margin) return false;
  float deg = atan2f(dx, -dy) * 57.29578f;  // clockwise from 12
  float edge = 32.0f - margin / r * 57.29578f;
  return fabsf(deg) < edge;
}

// Opening mask: one bit a pixel over its bounding box, worked out once.
constexpr int MX = 62, MY = 14, MW = 116, MH = 82;
uint8_t mask[MW * MH / 8 + 1];
bool maskReady = false;
bool masked(int x, int y) {
  if (!maskReady) {
    for (int j = 0; j < MH; j++)
      for (int i = 0; i < MW; i++)
        if (inOpening(MX + i + 0.5f, MY + j + 0.5f, 1.5f)) mask[(j * MW + i) >> 3] |= 1 << ((j * MW + i) & 7);
    maskReady = true;
  }
  int i = x - MX, j = y - MY;
  if (i < 0 || j < 0 || i >= MW || j >= MH) return false;
  return mask[(j * MW + i) >> 3] & (1 << ((j * MW + i) & 7));
}

// A pixel of the mechanism: only inside the opening, blended by coverage `a`.
void mechPixel(TFT_eSprite &s, int x, int y, uint16_t c, float a) {
  if (a <= 0 || !masked(x, y)) return;
  s.drawPixel(x, y, a >= 1 ? c : gfx::blend(c, s.readPixel(x, y), a));
}

// A thin line of the mechanism, clipped to the opening.
void mechLine(TFT_eSprite &s, float x0, float y0, float x1, float y1, uint16_t c) {
  int n = (int)(fmaxf(fabsf(x1 - x0), fabsf(y1 - y0)) * 1.5f) + 1;
  for (int i = 0; i <= n; i++) {
    float t = i / (float)n;
    mechPixel(s, lroundf(x0 + (x1 - x0) * t), lroundf(y0 + (y1 - y0) * t), c, 1);
  }
}

uint32_t hash(int x, int y) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

void phantomStatic(TFT_eSprite &s, const FaceContext &c) {
  // Dial: a faint lift towards the edge instead of a flat black.
  for (int r = 120; r > 0; r -= 6) s.fillCircle(CX, CY, r, gfx::blend(DIAL_EDGE, DIAL, r / 120.0f * 0.6f));
  // The opening: a recess, the movement plate with fine circular graining, and its edges.
  for (int y = MY; y < MY + MH; y++)
    for (int x = MX; x < MX + MW; x++) {
      if (!inOpening(x + 0.5f, y + 0.5f, 0)) continue;
      float d = hypotf(x - BAL_X, y - BAL_Y);
      bool grain = ((int)(d * 0.9f) % 3 == 0) ^ ((hash(x, y) & 7) == 0);
      uint16_t k = inOpening(x + 0.5f, y + 0.5f, 3) ? (grain ? PLATE_GRAIN : PLATE) : RECESS;
      s.drawPixel(x, y, k);
    }
  gfx::arc(s, (int)phantom::APERTURE_R1 + 1, (int)phantom::APERTURE_R1 - 1, phantom::APERTURE_FROM,
           360 + phantom::APERTURE_TO, LIP_LO, false);

  // Chapter ring: minute ticks and hour indices, left out wherever they would cross the opening.
  gfx::arc(s, 118, 116, 0, 360, RING_DIM);
  for (int i = 0; i < 60; i++) {
    float deg = i * 6;
    if (phantom::inAperture(deg) || phantom::inAperture(deg + 3) || phantom::inAperture(deg - 3)) continue;
    if (i % 5 == 0) gfx::radialBar(s, deg, 92, 111, 3.2f, RING);
    else {
      Pt a = gfx::polar(deg, 107), b = gfx::polar(deg, 112);
      s.drawWideLine(a.x, a.y, b.x, b.y, 1.0f, RING_DIM, DIAL);
    }
  }

  // Date at 3 o'clock and battery at 6, both well clear of the opening.
  char buf[8];
  const int wx = 150, wy = 109, ww = 34, wh = 22;
  s.drawRect(wx, wy, ww, wh, RING_DIM);
  if (c.timeValid) snprintf(buf, sizeof buf, "%d", c.now.tm_mday);
  else strcpy(buf, "--");
  gfx::text(s, buf, wx + ww / 2, wy + wh / 2 + 1, type::MEDIUM, col::N100);
  gfx::batteryGauge(s, CX, 170, 18, c.batteryPct, RING_DIM, RING, col::N100, c.charging);

  if (!c.timeValid) {
    bool syncing = c.sync == SyncView::Syncing;
    gfx::text(s, syncing ? "SYNCING" : "SET TIME", CX, 116, type::SMALL, syncing ? col::CYAN : col::N300);
    gfx::text(s, noTimeHint(c), CX, 136, type::SMALL, syncing ? col::CYAN : col::N400);
  }
}

// Balance wheel: rim, three arms and the hairspring, drawn pixel by pixel inside the opening.
void balance(TFT_eSprite &s, float deg) {
  float a = deg * DEG_TO_RAD, ca = cosf(a), sa = sinf(a);
  static const float ARM_C[3] = {1, -0.5f, -0.5f}, ARM_S[3] = {0, 0.8660254f, -0.8660254f};  // arms 120 degrees apart
  int r = (int)(RIM_R + RIM_W + 1);
  for (int y = BAL_Y - r; y <= BAL_Y + r; y++)
    for (int x = BAL_X - r; x <= BAL_X + r; x++) {
      float dx = x - BAL_X, dy = y - BAL_Y, d = sqrtf(dx * dx + dy * dy);
      float rim = RIM_W / 2 - fabsf(d - RIM_R);
      if (rim > -0.5f) {
        mechPixel(s, x, y, d < RIM_R ? RIM_DARK : RIM, fminf(1, rim + 0.5f));
        continue;
      }
      if (d > RIM_R || d < 3) continue;
      // Arms, turned with the wheel: three at 120 degrees.
      float u = dx * ca + dy * sa, v = -dx * sa + dy * ca;
      for (int k = 0; k < 3; k++) {
        float cu = ARM_C[k], su = ARM_S[k];
        float along = u * su - v * cu, across = u * cu + v * su;
        if (along > 0 && fabsf(across) < 1.4f) mechPixel(s, x, y, RIM, 1.4f - fabsf(across));
      }
    }
  // Hairspring: a flat spiral that winds and unwinds with the wheel, so it seems to breathe.
  float breathe = deg / phantom::BALANCE_DEG;  // -1..1
  float px = BAL_X + 4, py = BAL_Y;
  for (int i = 1; i <= 140; i++) {
    float t = i / 140.0f, th = t * 4.2f * 6.2831853f + a * 0.35f;
    float rr = 4 + t * (RIM_R - 9) * (1 + 0.06f * breathe * (1 - t));
    float qx = BAL_X + rr * cosf(th), qy = BAL_Y + rr * sinf(th);
    mechLine(s, px, py, qx, qy, SPRING);
    px = qx, py = qy;
  }
}

// The opening's inner edge, drawn after the seconds hand: a bright bevel over a dark shadow.
void lip(TFT_eSprite &s) {
  float from = phantom::APERTURE_FROM, to = 360 + phantom::APERTURE_TO;
  gfx::arc(s, (int)phantom::APERTURE_R0 + 1, (int)phantom::APERTURE_R0 - 2, from, to, LIP_LO, false);
  gfx::arc(s, (int)phantom::APERTURE_R0 - 2, (int)phantom::APERTURE_R0 - 3, from, to, LIP_HI, false);
  for (float d : {from, to}) {
    Pt a = gfx::polar(d, phantom::APERTURE_R0 - 3), b = gfx::polar(d, phantom::APERTURE_R1);
    s.drawWideLine(a.x, a.y, b.x, b.y, 2.0f, LIP_HI, DIAL);
  }
}

void dauphine(TFT_eSprite &s, float deg, float len, float w) {
  Pt edge[] = {gfx::alongHand(deg, -8, 0), gfx::alongHand(deg, len * 0.25f, w + 1), gfx::alongHand(deg, len + 1.5f, 0),
               gfx::alongHand(deg, len * 0.25f, -w - 1)};
  gfx::fillPoly(s, edge, 4, HAND_EDGE);
  Pt body[] = {gfx::alongHand(deg, -6, 0), gfx::alongHand(deg, len * 0.25f, w), gfx::alongHand(deg, len, 0),
               gfx::alongHand(deg, len * 0.25f, -w)};
  gfx::fillPoly(s, body, 4, HAND);
  Pt ridge[] = {gfx::alongHand(deg, 0, 0), gfx::alongHand(deg, len * 0.25f, w), gfx::alongHand(deg, len, 0)};
  gfx::fillPoly(s, ridge, 3, col::WHITE);  // the lit half of a faceted hand
}

void phantomLive(TFT_eSprite &s, const FaceContext &c) {
  // The mechanism stands still at rest without smooth motion.
  balance(s, c.animate ? phantom::balanceDeg(c.animMs) : 0);
  s.fillSmoothCircle(BAL_X, BAL_Y, 3, JEWEL, PLATE);
  s.drawPixel(BAL_X - 1, BAL_Y - 1, JEWEL_HI);

  float sec = c.now.tm_sec + (c.animate ? c.ms / 1000.0f : 0);
  phantom::Needle n = phantom::needleAt(sec, c.animate);
  if (c.timeValid && n.visible) {
    uint16_t k = gfx::blend(SECONDS, DIAL, n.alpha);
    Pt tail = gfx::polar(n.deg + 180, 18), tip = gfx::polar(n.deg, n.length);
    s.drawWideLine(tail.x, tail.y, tip.x, tip.y, 1.6f, k, DIAL);
    Pt w0 = gfx::polar(n.deg + 180, 9), w1 = gfx::polar(n.deg + 180, 18);
    s.drawWideLine(w0.x, w0.y, w1.x, w1.y, 4.0f, k, DIAL);  // counterweight
  }
  lip(s);
  if (c.timeValid) {
    float m = c.now.tm_min + sec / 60, h = (c.now.tm_hour % 12) + m / 60;
    dauphine(s, h * 30, 56, 5);
    dauphine(s, m * 6, 88, 3.6f);
  }
  s.fillSmoothCircle(CX, CY, 5, col::N300, DIAL);
  s.fillSmoothCircle(CX, CY, 2, DIAL, col::N300);
}
}  // namespace ph
}  // namespace

void drawFaceStatic(TFT_eSprite &s, Face f, const FaceContext &c) {
  s.fillSprite(col::BG);
  switch (f) {
    case FACE_DIGITAL: digitalStatic(s, c); break;
    case FACE_STEALTH:
    case FACE_CORPO:
    case FACE_HOLO: dialStatic(s, f, c); break;
    case FACE_RADAR: radarStatic(s, c); break;
    case FACE_PHANTOM: ph::phantomStatic(s, c); break;
    default: faceparts::themedStatic(s, f, c); break;
  }
}

void drawFaceLive(TFT_eSprite &s, Face f, const FaceContext &c) {
  switch (f) {
    case FACE_DIGITAL: digitalLive(s, c); break;
    case FACE_STEALTH:
    case FACE_CORPO:
    case FACE_HOLO: dialLive(s, f, c); break;
    case FACE_RADAR: radarLive(s, c); break;
    case FACE_PHANTOM: ph::phantomLive(s, c); break;
    default: faceparts::themedLive(s, f, c); break;
  }
}

uint32_t faceStaticKey(Face f, const FaceContext &c) {
  uint32_t minuteOfYear = c.timeValid ? c.now.tm_yday * 1440 + c.now.tm_hour * 60 + c.now.tm_min : 0xFFFFF;
  uint32_t k = f;
  k = k * 31 + c.timeValid;
  k = k * 1048583u + minuteOfYear;
  k = k * 131 + (uint32_t)(c.batteryPct + 1);
  k = k * 7 + c.use12h;
  k = k * 5 + (uint32_t)c.sync;
  k = k * 5 + c.syncStage;
  k = k * 2 + c.noWifi;
  k = k * 2 + c.charging;
  return k;
}

const char *faceName(Face f) {
  static const char *names[] = {"DIGITAL", "STEALTH", "CORPO",     "HOLOGRAM", "RADAR",     "Phantom",
                                "Orbit",   "Lunar",   "Yozakura", "Hanami",   "Wasteland", "Mojave"};
  static_assert(sizeof names / sizeof names[0] == FACE_COUNT, "a name for every face");
  return f < FACE_COUNT ? names[f] : "?";
}

bool faceAnimates(Face f, bool expressive) {
  if (f == FACE_RADAR || f == FACE_PHANTOM) return true;
  if (!expressive) return false;
  return f == FACE_ORBIT || f == FACE_LUNAR || f == FACE_YOZAKURA;
}

uint32_t faceEntranceMs(Face f) {
  switch (f) {
    case FACE_ORBIT: return orbit::ENTRANCE_MS;
    case FACE_LUNAR: return 300;
    case FACE_YOZAKURA: return yozakura::ENTRANCE_MS;
    case FACE_HANAMI: return 600;
    case FACE_WASTELAND: return 420;
    case FACE_MOJAVE: return 600;
    default: return 0;  // the six faces before v0.12.0 have no entrance
  }
}

bool faceMovesMarker(Face f) { return f == FACE_ORBIT; }

FaceTheme faceTheme(Face f) {
  switch (f) {
    case FACE_DIGITAL:   return {col::YELLOW, col::CYAN, col::CYAN_DEEP, col::N600};
    case FACE_STEALTH:   return {col::N100, col::CYAN, col::CYAN_DEEP, col::N600};
    case FACE_CORPO:     return {col::RED, col::YELLOW, rgb(0x400000), col::N700};
    case FACE_HOLO:      return {col::CYAN, col::WHITE, col::CYAN_DEEP, col::N600};
    case FACE_RADAR:     return {col::PHOSPHOR, col::YELLOW, rgb(0x052B1E), col::N700};
    case FACE_PHANTOM:   return {rgb(0xC41E3A), rgb(0xD4AF37), rgb(0x350810), col::N700};
    case FACE_ORBIT:     return {rgb(0x00F0FF), rgb(0xA8A0E8), rgb(0x003340), rgb(0x26385C)};
    case FACE_LUNAR:     return {rgb(0x96BEFF), rgb(0xCDD2DC), rgb(0x101830), rgb(0x283046)};
    case FACE_YOZAKURA:  return {rgb(0xDE8CA5), rgb(0xFAEEDC), rgb(0x2A1522), rgb(0x4A303C)};
    case FACE_HANAMI:    return {rgb(0xCC7086), rgb(0x461E3C), rgb(0x38182E), rgb(0x522E3A)};
    case FACE_WASTELAND: return {rgb(0x5CFF7A), rgb(0xFF8C28), rgb(0x0C2814), rgb(0x185C2A)};
    case FACE_MOJAVE:    return {rgb(0xF0A030), rgb(0xF5E6C8), rgb(0x352008), rgb(0x603E1C)};
    default:             return {col::CYAN, col::TEXT_2, col::CYAN_DEEP, col::OUTLINE};
  }
}


namespace phantom {
namespace {
float smoothstep(float u) { return u <= 0 ? 0 : u >= 1 ? 1 : u * u * (3 - 2 * u); }
}  // namespace

bool inAperture(float deg) {
  deg = fmodf(fmodf(deg, 360) + 360, 360);
  return deg >= APERTURE_FROM || deg <= APERTURE_TO;
}

float reboundDeg(float s) {
  if (s < 5 || s >= 5 + REBOUND_S) return 0;
  float f = (s - 5) / REBOUND_S;
  return expf(-8 * f) * sinf(4 * 3.14159265f * f) * REBOUND_DEG;
}

Needle needleAt(float s, bool smooth) {
  Needle n{true, 6 * s, FULL_PX, 1};
  if (s >= 58 || s < 2) return {false, 6 * s, 0, 0};
  if (s >= 55) {
    float u = (s - 55) / 3;  // 0 at 55 s, 1 at 58 s
    n.length = FULL_PX - (FULL_PX - LIP_PX) * smoothstep(u);
    n.alpha = u < 0.5f ? 1 : 1 - (u - 0.5f) / 0.5f;  // gone by the time it would vanish
  } else if (s < 5) {
    float u = (s - 2) / 3;  // 0 at 2 s, 1 at 5 s
    n.length = LIP_PX + (FULL_PX - LIP_PX) * smoothstep(u);
    n.alpha = u < 0.3f ? u / 0.3f : 1;
  } else if (smooth) {
    n.deg += reboundDeg(s);
  }
  return n;
}

float balanceDeg(uint32_t animMs) {
  // Four whole swings a second; the phase comes from elapsed time, so a slow frame never slows it.
  float phase = (animMs % 1000) / 1000.0f * BALANCE_HZ;
  return BALANCE_DEG * sinf(phase * 6.2831853f);
}
}  // namespace phantom
