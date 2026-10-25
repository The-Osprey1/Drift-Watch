#include "gfx.h"
#include <math.h>
#include <string.h>

using namespace layout;

namespace gfx {

Pt polar(float deg, float r) {
  float a = deg * DEG_TO_RAD;
  return {CX + r * sinf(a), CY - r * cosf(a)};
}

Pt alongHand(float deg, float along, float side) {
  float a = deg * DEG_TO_RAD, ux = sinf(a), uy = -cosf(a);
  return {CX + ux * along - uy * side, CY + uy * along + ux * side};
}

void fillPoly(TFT_eSprite &s, const Pt *p, int n, uint16_t c) {
  for (int i = 1; i < n - 1; i++)
    s.fillTriangle(lroundf(p[0].x), lroundf(p[0].y), lroundf(p[i].x), lroundf(p[i].y),
                   lroundf(p[i + 1].x), lroundf(p[i + 1].y), c);
}

uint16_t blend(uint16_t fg, uint16_t bg, float a) {
  if (a <= 0) return bg;
  if (a >= 1) return fg;
  auto mix = [a](int f, int b) { return (int)(b + (f - b) * a + 0.5f); };
  int r = mix(fg >> 11, bg >> 11), g = mix((fg >> 5) & 63, (bg >> 5) & 63), b = mix(fg & 31, bg & 31);
  return (uint16_t)(r << 11 | g << 5 | b);
}

void strokePoly(TFT_eSprite &s, const Pt *p, int n, uint16_t c) {
  for (int i = 0; i < n; i++) {
    const Pt &a = p[i], &b = p[(i + 1) % n];
    s.drawLine(lroundf(a.x), lroundf(a.y), lroundf(b.x), lroundf(b.y), c);
  }
}

void radialBar(TFT_eSprite &s, float deg, float r0, float r1, float w, uint16_t c) {
  float h = w / 2;
  Pt p[] = {alongHand(deg, r0, -h), alongHand(deg, r0, h), alongHand(deg, r1, h), alongHand(deg, r1, -h)};
  fillPoly(s, p, 4, c);
}

void cutBox(TFT_eSprite &s, int x, int y, int w, int h, int cut, uint16_t c, bool filled) {
  Pt p[] = {{(float)x + cut, (float)y}, {(float)x + w - 1, (float)y}, {(float)x + w - 1, (float)y + h - 1 - cut},
            {(float)x + w - 1 - cut, (float)y + h - 1}, {(float)x, (float)y + h - 1}, {(float)x, (float)y + cut}};
  if (filled) fillPoly(s, p, 6, c);
  else strokePoly(s, p, 6, c);
}

namespace {
void arcAround(TFT_eSprite &s, int cx, int cy, int r, int ir, float fromDeg, float toDeg, uint16_t c, bool smooth,
               uint16_t bg) {
  float span = toDeg - fromDeg;
  if (span <= 0) return;
  // TFT_eSPI measures from 6 o'clock, hence the 180 offset.
  if (span >= 360) {
    s.drawArc(cx, cy, r, ir, 0, 360, c, bg, smooth);
    return;
  }
  uint32_t a = (uint32_t)lroundf(fmodf(fromDeg + 180 + 720, 360));
  uint32_t b = (uint32_t)lroundf(fmodf(toDeg + 180 + 720, 360));
  if (a == b) return;
  s.drawArc(cx, cy, r, ir, a, b, c, bg, smooth);
}
}  // namespace

void arc(TFT_eSprite &s, int r, int ir, float fromDeg, float toDeg, uint16_t c, bool smooth, uint16_t bg) {
  arcAround(s, CX, CY, r, ir, fromDeg, toDeg, c, smooth, bg);
}

void arcAt(TFT_eSprite &s, int cx, int cy, int r, int ir, float fromDeg, float toDeg, uint16_t c, uint16_t bg) {
  arcAround(s, cx, cy, r, ir, fromDeg, toDeg, c, true, bg);
}

void dot(TFT_eSprite &s, float x, float y, float r, uint16_t c, uint32_t bg) { s.drawWedgeLine(x, y, x, y, r, r, c, bg); }

// ---- Segment digits --------------------------------------------------------
// Unscaled digit cell is 34 x 64 units, leaning right.
namespace {
constexpr float DW = 34, DH = 64, DT = 8, SEG_GAP = 1.5f, SKEW = 0.15f;
const uint8_t DIGIT_SEGS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

void segment(TFT_eSprite &s, float ox, float oy, float sc, int seg, uint16_t c) {
  const float h = DT / 2;
  Pt p[6];
  if (seg == 0 || seg == 3 || seg == 6) {
    float y = seg == 0 ? h : seg == 6 ? DH / 2 : DH - h;
    float x0 = h + SEG_GAP, x1 = DW - h - SEG_GAP;
    Pt q[] = {{x0, y}, {x0 + h, y - h}, {x1 - h, y - h}, {x1, y}, {x1 - h, y + h}, {x0 + h, y + h}};
    memcpy(p, q, sizeof p);
  } else {
    float x = (seg == 1 || seg == 2) ? DW - h : h;
    bool upper = seg == 1 || seg == 5;
    float y0 = upper ? h + SEG_GAP : DH / 2 + SEG_GAP, y1 = upper ? DH / 2 - SEG_GAP : DH - h - SEG_GAP;
    Pt q[] = {{x, y0}, {x + h, y0 + h}, {x + h, y1 - h}, {x, y1}, {x - h, y1 - h}, {x - h, y0 + h}};
    memcpy(p, q, sizeof p);
  }
  for (Pt &pt : p) {
    pt.x = ox + (pt.x + (DH - pt.y) * SKEW) * sc;
    pt.y = oy + pt.y * sc;
  }
  fillPoly(s, p, 6, c);
}

// d = -1 draws a dash, -2 nothing (a blank leading zero). Unlit segments get `ghost` if >= 0.
void digit(TFT_eSprite &s, float x, float y, float sc, int d, uint16_t c, int32_t ghost) {
  uint8_t bits = d == -2 ? 0 : d < 0 ? 0x40 : DIGIT_SEGS[d];
  for (int i = 0; i < 7; i++) {
    if (bits & (1 << i)) segment(s, x, y, sc, i, c);
    else if (ghost >= 0) segment(s, x, y, sc, i, (uint16_t)ghost);
  }
}
}  // namespace

// Upright, tabular instrument numerals. A tiny vector alphabet, no font allocation.
// Coordinates are stroke centres in a 28 x 58 cell; round joins come from wide lines.
namespace {
struct Numeral { uint8_t n; Pt p[16]; };
const Numeral NUMERALS[10] = {
  {9, {{6,0},{22,0},{28,6},{28,52},{22,58},{6,58},{0,52},{0,6},{6,0}}},
  {3, {{6,8},{16,0},{16,58}}},
  {9, {{0,8},{6,0},{22,0},{28,6},{28,19},{0,48},{0,58},{28,58},{28,58}}},
  {11,{{0,0},{22,0},{28,6},{28,22},{22,29},{10,29},{22,29},{28,36},{28,52},{22,58},{0,58}}},
  {5, {{22,58},{22,0},{0,37},{28,37},{28,37}}},
  {9, {{28,0},{0,0},{0,27},{22,27},{28,33},{28,52},{22,58},{0,58},{0,58}}},
  {12,{{26,0},{8,0},{0,8},{0,52},{6,58},{22,58},{28,52},{28,35},{22,29},{0,29},{0,29},{0,29}}},
  {3, {{0,0},{28,0},{8,58}}},
  {16,{{6,29},{0,22},{0,6},{6,0},{22,0},{28,6},{28,22},{22,29},{6,29},{0,36},{0,52},{6,58},{22,58},{28,52},{28,36},{22,29}}},
  {12,{{28,29},{6,29},{0,23},{0,6},{6,0},{22,0},{28,6},{28,50},{20,58},{2,58},{2,58},{2,58}}}
};
void instrumentDigit(TFT_eSprite &s, float x, float y, float sc, int digit, uint16_t ink) {
  constexpr float WIDTH = 5.2f;
  if (digit < 0) {
    s.drawWideLine(x + 5 * sc, y + 32 * sc, x + 29 * sc, y + 32 * sc, WIDTH * sc, ink);
    return;
  }
  const Numeral &g = NUMERALS[digit];
  for (int i = 1; i < g.n; ++i)
    s.drawWideLine(x + (g.p[i-1].x + 3) * sc, y + (g.p[i-1].y + 3) * sc,
                   x + (g.p[i].x + 3) * sc, y + (g.p[i].y + 3) * sc, WIDTH * sc, ink);
}
}  // namespace

TimeLayout timeLayout(float cx, float cy, float height, TimeStyle style) {
  float sc = height / DH, w = DW * sc;
  float total = 4 * w + 2 * (6 * sc) + 2 * (6 * sc) + 8 * sc + (style == TimeStyle::Segment ? DH * SKEW * sc : 0);
  return {cx - total / 2, cy - height / 2, height, total, w, sc, style};
}

void drawTime(TFT_eSprite &s, const TimeLayout &l, int hh, int mm, bool valid, bool blankLeadingZero, uint16_t c,
              int32_t ghost) {
  const float sc = l.scale, w = l.digitW, sp = 6 * sc, cg = 6 * sc, cw = 8 * sc;
  const float xs[] = {l.x, l.x + w + sp, l.x + 2 * w + sp + 2 * cg + cw, l.x + 3 * w + 2 * sp + 2 * cg + cw};
  int ds[] = {hh / 10, hh % 10, mm / 10, mm % 10};
  for (int i = 0; i < 4; i++) {
    bool blank = i == 0 && valid && blankLeadingZero && ds[0] == 0;
    if (blank && ghost < 0) continue;
    if (l.style == TimeStyle::Instrument) {
      if (!blank) instrumentDigit(s, xs[i], l.y, sc, valid ? ds[i] : -1, c);
    } else digit(s, xs[i], l.y, sc, blank ? -2 : valid ? ds[i] : -1, c, ghost);
  }
  // Colon: two diamonds, centred in the gap and following the lean.
  float colonX = l.x + 2 * w + sp + cg;
  for (float fy : {DH * 0.3f, DH * 0.7f}) {
    float cx = colonX + cw / 2 + (l.style == TimeStyle::Segment ? (DH - fy) * SKEW * sc : 0);
    float cy = l.y + fy * sc, r = 3.5f * sc;
    if (l.style == TimeStyle::Instrument) { dot(s, cx, cy, 2.1f * sc, c); continue; }
    Pt p[] = {{cx, cy - r}, {cx + r, cy}, {cx, cy + r}, {cx - r, cy}};
    fillPoly(s, p, 4, c);
  }
}

// ---- Battery ---------------------------------------------------------------
void battery(TFT_eSprite &s, int cx, int cy, int pct, uint16_t ink, bool charging) {
  char buf[16];
  if (pct < 0) strcpy(buf, "--");
  else snprintf(buf, sizeof buf, "%d%%", pct);
  bool low = pct >= 0 && pct <= 15;
  uint16_t c = pct < 0 ? col::N400 : low ? col::WARN : ink;

  s.setTextFont(type::SMALL);
  const int bw = 22, bh = 12, capW = 2, gap = 5;
  int tw = s.textWidth(buf);
  int x = cx - (bw + capW + gap + tw) / 2, y = cy - bh / 2;
  s.drawRect(x, y, bw, bh, c);
  s.fillRect(x + bw, y + 3, capW, bh - 6, c);
  if (pct > 0) s.fillRect(x + 2, y + 2, max(1, (bw - 4) * pct / 100), bh - 4, c);
  if (pct < 0) s.drawFastHLine(x + 6, cy, bw - 12, c);
  text(s, buf, x + bw + capW + gap, cy, type::SMALL, c, ML_DATUM);
  if (charging) chargeBolt(s, x - 7, cy, 11, c);
}

void batteryGauge(TFT_eSprite &s, int cx, int cy, int r, int pct, uint16_t track, uint16_t fill, uint16_t ink, bool charging) {
  // 270-degree gauge, open at the bottom.
  bool low = pct >= 0 && pct <= 15;
  s.drawArc(cx, cy, r, r - 3, 45, 315, track, col::BG, true);
  if (pct > 0) s.drawArc(cx, cy, r, r - 3, 45, 45 + 270 * pct / 100, low ? col::WARN : fill, col::BG, true);
  char buf[16];
  if (pct < 0) strcpy(buf, "--");
  else snprintf(buf, sizeof buf, "%d", pct);
  text(s, buf, cx, cy, type::SMALL, low ? col::WARN : ink);
  if (charging) chargeBolt(s, cx, cy + r - 3, 9, low ? col::WARN : fill);
}

// ---- Icons -----------------------------------------------------------------
void chargeBolt(TFT_eSprite &s, int cx, int cy, int h, uint16_t c) {
  // Two triangles that overlap across the middle make the zigzag.
  float u = h / 10.0f;
  s.fillTriangle(cx + 1 * u, cy - 5 * u, cx - 3 * u, cy + 1 * u, cx + 1 * u, cy + 1 * u, c);
  s.fillTriangle(cx - 1 * u, cy + 5 * u, cx + 3 * u, cy - 1 * u, cx - 1 * u, cy - 1 * u, c);
}

void iconCheck(TFT_eSprite &s, int cx, int cy, int size, uint16_t c) {
  float u = size / 10.0f;
  s.drawWideLine(cx - 4 * u, cy, cx - 1 * u, cy + 3 * u, 2.5f * u, c);
  s.drawWideLine(cx - 1 * u, cy + 3 * u, cx + 4.5f * u, cy - 3.5f * u, 2.5f * u, c);
}

void iconCross(TFT_eSprite &s, int cx, int cy, int size, uint16_t c) {
  float u = size / 10.0f;
  s.drawWideLine(cx - 3.5f * u, cy - 3.5f * u, cx + 3.5f * u, cy + 3.5f * u, 2.5f * u, c);
  s.drawWideLine(cx + 3.5f * u, cy - 3.5f * u, cx - 3.5f * u, cy + 3.5f * u, 2.5f * u, c);
}

void iconAlert(TFT_eSprite &s, int cx, int cy, int size, uint16_t c) {
  float u = size / 10.0f;
  Pt tri[] = {{(float)cx, cy - 5 * u}, {cx + 5.5f * u, cy + 4.5f * u}, {cx - 5.5f * u, cy + 4.5f * u}};
  fillPoly(s, tri, 3, c);
  s.fillRect(cx - (int)(0.75f * u), cy - 2 * u, max(2, (int)(1.5f * u)), 3.5f * u, col::BG);
  s.fillRect(cx - (int)(0.75f * u), cy + 2.4f * u, max(2, (int)(1.5f * u)), max(2, (int)(1.3f * u)), col::BG);
}

void iconPending(TFT_eSprite &s, int cx, int cy, int size, uint16_t c) {
  float r = size / 2.0f;
  s.drawWideLine(cx, cy, cx, cy - r * 0.55f, max(1.5f, size / 7.0f), c);
  s.drawWideLine(cx, cy, cx + r * 0.45f, cy, max(1.5f, size / 7.0f), c);
  s.drawArc(cx, cy, (int)r, (int)r - max(2, size / 7), 0, 360, c, col::BG, true);
}

// ---- Text ------------------------------------------------------------------
void text(TFT_eSprite &s, const char *str, int x, int y, uint8_t font, uint16_t c, uint8_t datum) {
  s.setTextFont(font);
  s.setTextDatum(datum);
  s.setTextColor(c);  // foreground only = transparent background
  s.drawString(str, x, y);
}

const char *fit(TFT_eSprite &s, const char *str, uint8_t font, int maxW, char *out, size_t outSize) {
  s.setTextFont(font);
  strncpy(out, str, outSize - 1);
  out[outSize - 1] = 0;
  if (s.textWidth(out) <= maxW) return out;
  size_t n = strlen(out);
  while (n > 0) {
    out[--n] = 0;
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%s...", out);
    if (s.textWidth(tmp) <= maxW) {
      strncpy(out, tmp, outSize - 1);
      out[outSize - 1] = 0;
      return out;
    }
  }
  return out;
}

// TFT_eSPI declares all its free fonts when built with LOAD_GFXFF (build.sh sets
// it); only the three used here end up in flash, about 10 KB together.
const GFXfont *font(type::Role role, bool large) {
  if (role == type::Role::Title) return large ? &FreeSansBold18pt7b : &FreeSansBold12pt7b;
  return large ? &FreeSansBold12pt7b : &FreeSansBold9pt7b;
}

void text(TFT_eSprite &s, const char *str, int x, int y, const GFXfont *f, uint16_t c, uint8_t datum) {
  s.setFreeFont(f);
  s.setTextDatum(datum);
  s.setTextColor(c);
  s.drawString(str, x, y);
}

int textWidth(TFT_eSprite &s, const char *str, const GFXfont *f) {
  s.setFreeFont(f);
  return s.textWidth(str);
}

int nextLine(TFT_eSprite &s, const char *str, int start, const GFXfont *f, int maxW, int &skip) {
  skip = 0;
  while (str[start + skip] == ' ') skip++;
  const char *p = str + start + skip;
  int n = strlen(p);
  if (n == 0) return 0;
  s.setFreeFont(f);
  char tmp[96];
  auto width = [&](int len) {
    len = min(len, (int)sizeof tmp - 1);
    memcpy(tmp, p, len);
    tmp[len] = 0;
    return (int)s.textWidth(tmp);
  };
  if (width(n) <= maxW) return n;
  // Prefer breaking at a space; break after '/' (long zone names) only if no space works.
  int best = 0;
  for (int pass = 0; pass < 2 && best == 0; pass++)
    for (int i = 1; i < n; i++) {
      bool brk = pass == 0 ? p[i] == ' ' : p[i - 1] == '/';
      if (!brk) continue;
      int len = i;
      while (len > 0 && p[len - 1] == ' ') len--;
      if (width(len) > maxW) break;
      best = len;
    }
  if (best > 0) return best;
  // One word is wider than the line: break it where it stops fitting.
  int len = 1;
  while (len < n && width(len + 1) <= maxW) len++;
  return len;
}

int chordWidth(int top, int bottom, int margin) {
  int dy = max(abs(top - CY), abs(bottom - CY));
  float half = sqrtf(max(0.0f, (float)(SAFE_R * SAFE_R - dy * dy)));
  return (int)(2 * half) - 2 * margin;
}

// ---- Skyline ---------------------------------------------------------------
namespace {
// Tiny repeatable random numbers, so the skyline is identical every frame.
uint32_t lcg(uint32_t &state) {
  state = state * 1103515245u + 12345u;
  return (state >> 16) & 0x7FFF;
}
}  // namespace

void skyline(TFT_eSprite &s, int horizon, uint32_t seed) {
  uint32_t r = 0xC0FFEE;
  for (int x = -4; x < 244;) {
    int w = 9 + lcg(r) % 12;
    int h = 6 + lcg(r) % 14 + abs(x + w / 2 - CX) / 9;
    int top = horizon - h;
    s.fillRect(x, top, w, h, col::N800);
    s.drawFastHLine(x, top, w, col::N700);
    uint32_t wr = seed ^ ((uint32_t)(x + 8) * 2654435761u);
    for (int wy = top + 4; wy < horizon - 3; wy += 5)
      for (int wx = x + 3; wx < x + w - 3; wx += 4)
        if (lcg(wr) % 100 < 9) s.fillRect(wx, wy, 2, 2, col::WINDOW);
    x += w + 1 + lcg(r) % 3;
  }
  s.drawFastHLine(0, horizon, 240, col::MAGENTA_DIM);
}

}  // namespace gfx
