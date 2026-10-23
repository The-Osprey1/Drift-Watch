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

void arc(TFT_eSprite &s, int r, int ir, float fromDeg, float toDeg, uint16_t c, bool smooth) {
  float span = toDeg - fromDeg;
  if (span <= 0) return;
  // TFT_eSPI measures from 6 o'clock, hence the 180 offset.
  if (span >= 360) {
    s.drawArc(CX, CY, r, ir, 0, 360, c, col::BG, smooth);
    return;
  }
  uint32_t a = (uint32_t)lroundf(fmodf(fromDeg + 180 + 720, 360));
  uint32_t b = (uint32_t)lroundf(fmodf(toDeg + 180 + 720, 360));
  if (a == b) return;
  s.drawArc(CX, CY, r, ir, a, b, c, col::BG, smooth);
}

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

void digit(TFT_eSprite &s, float x, float y, float sc, int d, uint16_t c) {
  uint8_t bits = d < 0 ? 0x40 : DIGIT_SEGS[d];  // -1 draws a dash
  for (int i = 0; i < 7; i++)
    if (bits & (1 << i)) segment(s, x, y, sc, i, c);
}
}  // namespace

TimeLayout timeLayout(float cx, float cy, float height) {
  float sc = height / DH, w = DW * sc;
  float total = 4 * w + 2 * (6 * sc) + 2 * (6 * sc) + 8 * sc + DH * SKEW * sc;
  return {cx - total / 2, cy - height / 2, height, total, w, sc};
}

void drawTime(TFT_eSprite &s, const TimeLayout &l, int hh, int mm, bool valid, bool blankLeadingZero, uint16_t c) {
  const float sc = l.scale, w = l.digitW, sp = 6 * sc, cg = 6 * sc, cw = 8 * sc;
  const float xs[] = {l.x, l.x + w + sp, l.x + 2 * w + sp + 2 * cg + cw, l.x + 3 * w + 2 * sp + 2 * cg + cw};
  int ds[] = {hh / 10, hh % 10, mm / 10, mm % 10};
  for (int i = 0; i < 4; i++) {
    if (i == 0 && valid && blankLeadingZero && ds[0] == 0) continue;
    digit(s, xs[i], l.y, sc, valid ? ds[i] : -1, c);
  }
  // Colon: two diamonds, centred in the gap and following the lean.
  float colonX = l.x + 2 * w + sp + cg;
  for (float fy : {DH * 0.3f, DH * 0.7f}) {
    float cx = colonX + cw / 2 + (DH - fy) * SKEW * sc;
    float cy = l.y + fy * sc, r = 3.5f * sc;
    Pt p[] = {{cx, cy - r}, {cx + r, cy}, {cx, cy + r}, {cx - r, cy}};
    fillPoly(s, p, 4, c);
  }
}

// ---- Battery ---------------------------------------------------------------
void battery(TFT_eSprite &s, int cx, int cy, int pct, uint16_t ink) {
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
}

void batteryGauge(TFT_eSprite &s, int cx, int cy, int r, int pct, uint16_t track, uint16_t fill, uint16_t ink) {
  // 270-degree gauge, open at the bottom.
  bool low = pct >= 0 && pct <= 15;
  s.drawArc(cx, cy, r, r - 3, 45, 315, track, col::BG, true);
  if (pct > 0) s.drawArc(cx, cy, r, r - 3, 45, 45 + 270 * pct / 100, low ? col::WARN : fill, col::BG, true);
  char buf[16];
  if (pct < 0) strcpy(buf, "--");
  else snprintf(buf, sizeof buf, "%d", pct);
  text(s, buf, cx, cy, type::SMALL, low ? col::WARN : ink);
}

// ---- Icons -----------------------------------------------------------------
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
