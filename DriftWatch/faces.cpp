// Face designs. Accent hierarchy per face:
//   Digital  - yellow time first, magenta date second, cyan only for the live second.
//   Stealth  - silver hands and markers, cyan glow inlays, magenta seconds hand only.
//   Corpo    - yellow structure, black hands edged in yellow, red for seconds and inlays.
//   Hologram - cyan throughout, white inlays, magenta seconds hand.
//   Radar    - yellow time, cyan hour ring, magenta seconds dot.
#include "faces.h"
#include "gfx.h"
#include "theme.h"

using namespace layout;

namespace {
const char *DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char *MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

int displayHour(const FaceContext &c) {
  if (!c.use12h) return c.now.tm_hour;
  int h = c.now.tm_hour % 12;
  return h == 0 ? 12 : h;
}

void dateText(const FaceContext &c, char *buf, size_t n) {
  snprintf(buf, n, "%s %d %s", DAYS[c.now.tm_wday], c.now.tm_mday, MONTHS[c.now.tm_mon]);
}

// ---- Digital -----------------------------------------------------------------
constexpr int DIGI_TIME_Y = 104;
constexpr int DATE_X = 44, DATE_Y = 140, DATE_W = 152, DATE_H = 27;

void digitalStatic(TFT_eSprite &s, const FaceContext &c) {
  gfx::skyline(s, 212, c.timeValid ? c.now.tm_yday * 1440 + c.now.tm_hour * 60 + c.now.tm_min : 0);

  // Seconds track: quiet structure, the live tick is the only bright part.
  for (int i = 0; i < 60; i++) {
    bool major = i % 5 == 0;
    Pt a = gfx::polar(i * 6, major ? 107 : 110), b = gfx::polar(i * 6, 114);
    s.drawWideLine(a.x, a.y, b.x, b.y, major ? 2.0f : 1.2f, major ? col::N500 : col::N700, col::BG);
  }

  gfx::battery(s, CX, 46, c.batteryPct, col::N100);

  gfx::TimeLayout tl = gfx::timeLayout(CX, DIGI_TIME_Y, type::TIME_HERO);
  gfx::drawTime(s, tl, displayHour(c), c.now.tm_min, c.timeValid, c.use12h, col::YELLOW);
  if (c.use12h && c.timeValid)
    gfx::text(s, c.now.tm_hour < 12 ? "AM" : "PM", tl.x + tl.width, tl.y - 9, type::SMALL, col::CYAN, MR_DATUM);

  char buf[24];
  if (c.timeValid) {
    gfx::cutBox(s, DATE_X, DATE_Y, DATE_W, DATE_H, 7, col::MAGENTA, true);
    dateText(c, buf, sizeof buf);
    gfx::text(s, buf, CX, DATE_Y + DATE_H / 2 + 1, type::MEDIUM, col::BG);
  } else if (c.sync == SyncView::Syncing) {
    gfx::cutBox(s, DATE_X, DATE_Y, DATE_W, DATE_H, 7, col::CYAN, false);
    gfx::cutBox(s, DATE_X + 1, DATE_Y + 1, DATE_W - 2, DATE_H - 2, 6, col::CYAN, false);
    gfx::text(s, "SYNCING", CX, DATE_Y + DATE_H / 2 + 1, type::MEDIUM, col::CYAN);
  } else {
    gfx::cutBox(s, DATE_X, DATE_Y, DATE_W, DATE_H, 7, col::WARN, false);
    gfx::cutBox(s, DATE_X + 1, DATE_Y + 1, DATE_W - 2, DATE_H - 2, 6, col::WARN, false);
    gfx::iconAlert(s, DATE_X + 20, DATE_Y + DATE_H / 2, 14, col::WARN);
    gfx::text(s, "NO TIME", CX + 10, DATE_Y + DATE_H / 2 + 1, type::MEDIUM, col::WARN);
    gfx::text(s, "Swipe up to sync", CX, 182, type::SMALL, col::N300);
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
    rgb(0x0B0D12), 0, 0, col::N500, col::N100, 5, 84, col::CYAN,
    col::N100, col::WHITE, 0, col::CYAN, col::CYAN, true,
    col::MAGENTA, col::N100,
    col::N800, col::N500, col::N100,
    col::N600, col::CYAN, col::N100};

const DialStyle CORPO = {
    rgb(0x080808), col::YELLOW, 0, col::YELLOW_DIM, col::YELLOW, 8, 80, 0,
    col::BG, col::BG, col::YELLOW, col::RED, col::RED, false,
    col::RED, col::YELLOW,
    col::YELLOW, 0, col::BG,
    rgb(0x2E2A04), col::YELLOW, col::YELLOW};

const DialStyle HOLO = {
    rgb(0x020A0D), 0, col::CYAN_DEEP, col::CYAN_DIM, col::CYAN, 4, 86, 0,
    col::CYAN_DEEP, col::CYAN_DEEP, col::CYAN, col::CYAN, col::WHITE, false,
    col::MAGENTA, col::CYAN,
    col::BG, col::CYAN, col::CYAN,
    col::CYAN_DEEP, col::CYAN, col::CYAN};

const DialStyle &styleFor(Face f) { return f == FACE_CORPO ? CORPO : f == FACE_HOLO ? HOLO : STEALTH; }

// Sword-shaped hand: narrow at the pivot, widest at 72% of its length, pointed tip.
void swordShape(Pt *p, float deg, float len, float tail, float w) {
  p[0] = gfx::alongHand(deg, -tail, -w * 0.55f);
  p[1] = gfx::alongHand(deg, -tail, w * 0.55f);
  p[2] = gfx::alongHand(deg, len * 0.72f, w);
  p[3] = gfx::alongHand(deg, len, 0);
  p[4] = gfx::alongHand(deg, len * 0.72f, -w);
}

void hand(TFT_eSprite &s, const DialStyle &st, float deg, float len, float tail, float w,
          uint16_t fill, uint16_t inlay, bool halo) {
  Pt p[5];
  if (halo) {
    swordShape(p, deg, len + 1.5f, tail + 1.5f, w + 1.6f);
    gfx::fillPoly(s, p, 5, st.face);
  }
  if (st.handEdge) {
    swordShape(p, deg, len, tail, w);
    gfx::fillPoly(s, p, 5, st.handEdge);
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

void dialStatic(TFT_eSprite &s, Face f, const FaceContext &c) {
  const DialStyle &st = styleFor(f);
  s.fillCircle(CX, CY, 120, st.face);

  if (st.band) {
    // Segmented band, notched at every hour.
    for (int h = 0; h < 12; h++) gfx::arc(s, 119, 114, h * 30 + 2, h * 30 + 28, st.band, false);
  }
  if (st.atmosphere) s.drawCircle(CX, CY, 70, st.atmosphere);

  for (int i = 0; i < 60; i++) {
    if (i % 5 == 0) continue;
    Pt a = gfx::polar(i * 6, 104), b = gfx::polar(i * 6, 111);
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
    if (f == FACE_HOLO) s.drawRect(wx + 1, wy + 1, ww - 2, wh - 2, st.dateEdge);
  }
  if (c.timeValid) snprintf(buf, sizeof buf, "%d", c.now.tm_mday);
  else strcpy(buf, "--");
  gfx::text(s, buf, wx + ww / 2, wy + wh / 2 + 1, type::MEDIUM, st.dateInk);

  gfx::batteryGauge(s, CX, 168, 19, c.batteryPct, st.gaugeTrack, st.gaugeFill, st.gaugeInk);

  if (!c.timeValid) {
    gfx::iconAlert(s, CX, 64, 14, col::WARN);
    gfx::text(s, "NO TIME", CX, 82, type::SMALL, col::WARN);
    return;
  }
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
  s.drawCircle(CX, CY, 86, col::N700);
  for (int d = 0; d < 360; d += 90) {
    Pt a = gfx::polar(d, 78), b = gfx::polar(d, 90);
    s.drawWideLine(a.x, a.y, b.x, b.y, 1.5f, col::N600, col::BG);
  }

  // Hour ring: one segment per hour of the 12-hour cycle. Past hours dim, current bright.
  int hr12 = c.now.tm_hour % 12;
  for (int i = 0; i < 12; i++) {
    uint16_t k = !c.timeValid || i > hr12 ? col::N700 : i == hr12 ? col::CYAN : col::CYAN_DIM;
    gfx::arc(s, 117, 108, i * 30 + 1.5f, i * 30 + 28.5f, k, false);
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

void radarSweep(TFT_eSprite &s, float t) {
  // One clockwise pass with a short fading trail.
  static const uint16_t trail[] = {rgb(0x00A8B3), rgb(0x007780), rgb(0x004C52), rgb(0x002A2E)};
  float deg = t * 360;
  for (int i = 3; i >= 0; i--) {
    Pt a = gfx::polar(deg - (i + 1) * 4, 18), b = gfx::polar(deg - (i + 1) * 4, 96);
    s.drawWideLine(a.x, a.y, b.x, b.y, 2.0f, trail[i], col::BG);
  }
  Pt a = gfx::polar(deg, 18), b = gfx::polar(deg, 96);
  s.drawWideLine(a.x, a.y, b.x, b.y, 2.0f, col::CYAN, col::BG);
}

void radarLive(TFT_eSprite &s, const FaceContext &c) {
  if (c.sweep >= 0 && c.sweep < 1) radarSweep(s, c.sweep);

  if (c.timeValid) {
    Pt d = gfx::polar(c.now.tm_sec * 6, 94);
    s.fillSmoothCircle(d.x, d.y, 3, col::MAGENTA, col::BG);
  }

  gfx::battery(s, CX, 72, c.batteryPct, col::N300);
  gfx::TimeLayout tl = gfx::timeLayout(CX, 114, type::TIME_INSET);
  gfx::drawTime(s, tl, displayHour(c), c.now.tm_min, c.timeValid, c.use12h, col::YELLOW);

  char buf[28];
  if (c.timeValid) {
    dateText(c, buf, sizeof buf);
    if (c.use12h) strcat(buf, c.now.tm_hour < 12 ? "  AM" : "  PM");
    gfx::text(s, buf, CX, 152, type::SMALL, col::N300);
  } else {
    gfx::text(s, c.sync == SyncView::Syncing ? "SYNCING" : "NO TIME", CX, 152, type::SMALL,
              c.sync == SyncView::Syncing ? col::CYAN : col::WARN);
  }
}
}  // namespace

void drawFaceStatic(TFT_eSprite &s, Face f, const FaceContext &c) {
  s.fillSprite(col::BG);
  switch (f) {
    case FACE_DIGITAL: digitalStatic(s, c); break;
    case FACE_STEALTH:
    case FACE_CORPO:
    case FACE_HOLO: dialStatic(s, f, c); break;
    case FACE_RADAR: radarStatic(s, c); break;
    default: break;
  }
}

void drawFaceLive(TFT_eSprite &s, Face f, const FaceContext &c) {
  switch (f) {
    case FACE_DIGITAL: digitalLive(s, c); break;
    case FACE_STEALTH:
    case FACE_CORPO:
    case FACE_HOLO: dialLive(s, f, c); break;
    case FACE_RADAR: radarLive(s, c); break;
    default: break;
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
  return k;
}

const char *faceName(Face f) {
  static const char *names[] = {"DIGITAL", "STEALTH", "CORPO", "HOLOGRAM", "RADAR"};
  return f < FACE_COUNT ? names[f] : "?";
}
