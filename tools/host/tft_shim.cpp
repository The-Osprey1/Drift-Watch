#include "TFT_eSPI.h"
// Font tables straight from the TFT_eSPI library (build with -I <TFT_eSPI folder>).
#include <Fonts/Font16.h>
#include <Fonts/Font32rle.h>

namespace {
constexpr float LO_ALPHA = 1.0f / 32.0f, HI_ALPHA = 1.0f - LO_ALPHA;

uint16_t blend565(uint8_t alpha, uint16_t fgc, uint16_t bgc) {
  uint32_t rxb = bgc & 0xF81F;
  rxb += ((fgc & 0xF81F) - rxb) * (alpha >> 2) >> 6;
  uint32_t xgx = bgc & 0x07E0;
  xgx += ((fgc & 0x07E0) - xgx) * alpha >> 8;
  return (rxb & 0xF81F) | (xgx & 0x07E0);
}

float wedgeDistance(float xpax, float ypay, float bax, float bay, float dr) {
  float h = fmaxf(fminf((xpax * bax + ypay * bay) / (bax * bax + bay * bay), 1.0f), 0.0f);
  float dx = xpax - bax * h, dy = ypay - bay * h;
  return sqrtf(dx * dx + dy * dy) + h * dr;
}
}  // namespace

TFT_eSPI::TFT_eSPI(int w, int h) : buf(w * h, 0), w_(w), h_(h) {}

void TFT_eSPI::drawPixel(int32_t x, int32_t y, uint32_t c) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
  buf[y * w_ + x] = c;
}

uint16_t TFT_eSPI::readPixel(int32_t x, int32_t y) const {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return 0;
  return buf[y * w_ + x];
}

void TFT_eSPI::blendPixel(int32_t x, int32_t y, uint16_t fg, float alpha, uint32_t bg) {
  if (alpha <= LO_ALPHA) return;
  if (alpha > HI_ALPHA) { drawPixel(x, y, fg); return; }
  uint16_t b = bg == 0x00FFFFFF ? readPixel(x, y) : (uint16_t)bg;
  drawPixel(x, y, blend565((uint8_t)(alpha * 255), fg, b));
}

void TFT_eSPI::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) {
  for (int i = 0; i < w; i++) drawPixel(x + i, y, c);
}
void TFT_eSPI::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) {
  for (int i = 0; i < h; i++) drawPixel(x, y + i, c);
}
void TFT_eSPI::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
  for (int j = 0; j < h; j++) drawFastHLine(x, y + j, w, c);
}
void TFT_eSPI::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
  drawFastHLine(x, y, w, c);
  drawFastHLine(x, y + h - 1, w, c);
  drawFastVLine(x, y, h, c);
  drawFastVLine(x + w - 1, y, h, c);
}

void TFT_eSPI::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) {
  bool steep = abs(y1 - y0) > abs(x1 - x0);
  if (steep) { std::swap(x0, y0); std::swap(x1, y1); }
  if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }
  int32_t dx = x1 - x0, dy = abs(y1 - y0), err = dx >> 1, ystep = y0 < y1 ? 1 : -1;
  for (; x0 <= x1; x0++) {
    if (steep) drawPixel(y0, x0, c); else drawPixel(x0, y0, c);
    err -= dy;
    if (err < 0) { y0 += ystep; err += dx; }
  }
}

void TFT_eSPI::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c) {
  int32_t a, b, y, last;
  if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
  if (y1 > y2) { std::swap(y2, y1); std::swap(x2, x1); }
  if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
  if (y0 == y2) {
    a = b = x0;
    if (x1 < a) a = x1; else if (x1 > b) b = x1;
    if (x2 < a) a = x2; else if (x2 > b) b = x2;
    drawFastHLine(a, y0, b - a + 1, c);
    return;
  }
  int32_t dx01 = x1 - x0, dy01 = y1 - y0, dx02 = x2 - x0, dy02 = y2 - y0, dx12 = x2 - x1, dy12 = y2 - y1;
  int32_t sa = 0, sb = 0;
  last = y1 == y2 ? y1 : y1 - 1;
  for (y = y0; y <= last; y++) {
    a = x0 + sa / dy01;
    b = x0 + sb / dy02;
    sa += dx01;
    sb += dx02;
    if (a > b) std::swap(a, b);
    drawFastHLine(a, y, b - a + 1, c);
  }
  sa = dx12 * (y - y1);
  sb = dx02 * (y - y0);
  for (; y <= y2; y++) {
    a = x1 + sa / dy12;
    b = x0 + sb / dy02;
    sa += dx12;
    sb += dx02;
    if (a > b) std::swap(a, b);
    drawFastHLine(a, y, b - a + 1, c);
  }
}

void TFT_eSPI::drawCircle(int32_t x0, int32_t y0, int32_t r, uint32_t c) {
  int32_t f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
  drawPixel(x0, y0 + r, c); drawPixel(x0, y0 - r, c); drawPixel(x0 + r, y0, c); drawPixel(x0 - r, y0, c);
  while (x < y) {
    if (f >= 0) { y--; ddy += 2; f += ddy; }
    x++; ddx += 2; f += ddx;
    drawPixel(x0 + x, y0 + y, c); drawPixel(x0 - x, y0 + y, c); drawPixel(x0 + x, y0 - y, c); drawPixel(x0 - x, y0 - y, c);
    drawPixel(x0 + y, y0 + x, c); drawPixel(x0 - y, y0 + x, c); drawPixel(x0 + y, y0 - x, c); drawPixel(x0 - y, y0 - x, c);
  }
}

void TFT_eSPI::fillCircle(int32_t x0, int32_t y0, int32_t r, uint32_t c) {
  int32_t x = 0, dx = 1, dy = r + r, p = -(r >> 1);
  drawFastHLine(x0 - r, y0, dy + 1, c);
  while (x < r) {
    if (p >= 0) {
      drawFastHLine(x0 - x, y0 + r, 2 * x + 1, c);
      drawFastHLine(x0 - x, y0 - r, 2 * x + 1, c);
      dy -= 2; p -= dy; r--;
    }
    dx += 2; p += dx; x++;
    drawFastHLine(x0 - r, y0 + x, 2 * r + 1, c);
    drawFastHLine(x0 - r, y0 - x, 2 * r + 1, c);
  }
}

void TFT_eSPI::fillSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t c, uint32_t bg) {
  for (int yy = y - r - 1; yy <= y + r + 1; yy++)
    for (int xx = x - r - 1; xx <= x + r + 1; xx++) {
      float d = sqrtf((float)(xx - x) * (xx - x) + (float)(yy - y) * (yy - y));
      blendPixel(xx, yy, c, r + 0.5f - d, bg);
    }
}

void TFT_eSPI::drawWideLine(float ax, float ay, float bx, float by, float wd, uint32_t fg, uint32_t bg) {
  drawWedgeLine(ax, ay, bx, by, wd / 2.0f, wd / 2.0f, fg, bg);
}

void TFT_eSPI::drawWedgeLine(float ax, float ay, float bx, float by, float ar, float br, uint32_t fg, uint32_t bg) {
  if (fabsf(ax - bx) < 0.01f && fabsf(ay - by) < 0.01f) bx += 0.01f;
  int x0 = floorf(fminf(ax - ar, bx - br)), x1 = ceilf(fmaxf(ax + ar, bx + br));
  int y0 = floorf(fminf(ay - ar, by - br)), y1 = ceilf(fmaxf(ay + ar, by + br));
  float rdt = ar - br, bax = bx - ax, bay = by - ay;
  for (int yp = y0; yp <= y1; yp++)
    for (int xp = x0; xp <= x1; xp++)
      blendPixel(xp, yp, fg, (ar + 0.5f) - wedgeDistance(xp - ax, yp - ay, bax, bay, rdt), bg);
}

void TFT_eSPI::drawArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t start, uint32_t end, uint32_t fg,
                       uint32_t bg, bool smooth) {
  if (end > 360) end = 360;
  if (start > 360) start = 360;
  if (start == end) return;
  for (int yy = y - r - 1; yy <= y + r + 1; yy++)
    for (int xx = x - r - 1; xx <= x + r + 1; xx++) {
      float dx = xx - x, dy = yy - y, d = sqrtf(dx * dx + dy * dy);
      // Angle from 6 o'clock, clockwise on screen.
      float a = atan2f(-dx, dy) * 57.29578f;
      if (a < 0) a += 360;
      bool in = start < end ? (a >= start && a < end) : (a >= start || a < end);
      if (!in) continue;
      if (smooth) {
        float alpha = fminf(r + 0.5f - d, d - ir + 0.5f);
        blendPixel(xx, yy, fg, alpha, bg);
      } else if (d <= r && d >= ir) {
        drawPixel(xx, yy, fg);
      }
    }
}

// ---- Text --------------------------------------------------------------------
void TFT_eSPI::setFreeFont(const GFXfont *f) {
  font_ = 1;
  gfxFont_ = f;
  glyphAb_ = glyphBb_ = 0;
  // Same scan as the library, including its skipping of the last glyph.
  uint16_t n = f->last - f->first;
  for (uint16_t c = 0; c < n; c++) {
    const GFXglyph &g = f->glyph[c];
    int ab = -g.yOffset, bb = g.height - ab;
    glyphAb_ = std::max(glyphAb_, ab);
    glyphBb_ = std::max(glyphBb_, bb);
  }
}

int16_t TFT_eSPI::textWidth(const char *s) const {
  if (gfxFont_) {
    int w = 0;
    for (; *s; s++) {
      unsigned char c = *s;
      if (c < gfxFont_->first || c > gfxFont_->last) continue;
      const GFXglyph &g = gfxFont_->glyph[c - gfxFont_->first];
      w += s[1] ? g.xAdvance : g.xOffset + g.width;  // the library's rule for the last character
    }
    return w;
  }
  const unsigned char *tbl = font_ == 4 ? widtbl_f32 : widtbl_f16;
  int w = 0;
  for (; *s; s++) {
    unsigned char ch = *s;
    w += (ch > 31 && ch < 128) ? tbl[ch - 32] : tbl[0];
  }
  return w;
}

int16_t TFT_eSPI::fontHeight() const {
  if (gfxFont_) return gfxFont_->yAdvance;
  return font_ == 4 ? chr_hgt_f32 : chr_hgt_f16;
}

int16_t TFT_eSPI::drawString(const char *s, int32_t x, int32_t y) {
  inkX0_ = inkY0_ = 1 << 20;
  inkX1_ = inkY1_ = -(1 << 20);
  if (gfxFont_) {
    // Datum handling as in TFT_eSPI::drawString for free fonts: the y position
    // is moved to the baseline, and the box height is the tallest glyph rise.
    int cw = textWidth(s), ch = glyphAb_;
    y += ch;
    int baseline = ch;
    if (datum_ == BL_DATUM || datum_ == BC_DATUM || datum_ == BR_DATUM) ch += glyphBb_;
    switch (datum_) {
      case TC_DATUM: x -= cw / 2; break;
      case TR_DATUM: x -= cw; break;
      case ML_DATUM: y -= ch / 2; break;
      case MC_DATUM: x -= cw / 2; y -= ch / 2; break;
      case MR_DATUM: x -= cw; y -= ch / 2; break;
      case BL_DATUM: y -= ch; break;
      case BC_DATUM: x -= cw / 2; y -= ch; break;
      case BR_DATUM: x -= cw; y -= ch; break;
      case L_BASELINE: y -= baseline; break;
      case C_BASELINE: x -= cw / 2; y -= baseline; break;
      case R_BASELINE: x -= cw; y -= baseline; break;
    }
    int px = x;
    for (const char *p = s; *p; p++) {
      unsigned char c = *p;
      if (c < gfxFont_->first || c > gfxFont_->last) continue;
      const GFXglyph &g = gfxFont_->glyph[c - gfxFont_->first];
      const uint8_t *bm = gfxFont_->bitmap + g.bitmapOffset;
      uint8_t bits = 0, bit = 0;
      for (int yy = 0; yy < g.height; yy++)
        for (int xx = 0; xx < g.width; xx++) {
          if (!(bit++ & 7)) bits = *bm++;
          if (bits & 0x80) {
            int gx = px + g.xOffset + xx, gy = y + g.yOffset + yy;
            ink(gx, gy);
            drawPixel(gx, gy, fg_);
          }
          bits <<= 1;
        }
      px += g.xAdvance;
    }
    TextBox b{s, (int)x, (int)y - glyphAb_, cw, glyphAb_ + glyphBb_, 0, 0, 0, 0, gfxFont_, 0};
    if (inkX1_ >= inkX0_) b.ix = inkX0_, b.iy = inkY0_, b.iw = inkX1_ - inkX0_ + 1, b.ih = inkY1_ - inkY0_ + 1;
    textLog.push_back(b);
    return cw;
  }
  int cw = textWidth(s), ch = fontHeight();
  switch (datum_) {
    case TC_DATUM: x -= cw / 2; break;
    case TR_DATUM: x -= cw; break;
    case ML_DATUM: y -= ch / 2; break;
    case MC_DATUM: x -= cw / 2; y -= ch / 2; break;
    case MR_DATUM: x -= cw; y -= ch / 2; break;
    case BL_DATUM: y -= ch; break;
    case BC_DATUM: x -= cw / 2; y -= ch; break;
    case BR_DATUM: x -= cw; y -= ch; break;
  }
  size_t logAt = textLog.size();
  textLog.push_back({s, (int)x, (int)y, cw, ch, 0, 0, 0, 0, nullptr, font_});
  int px = x;
  for (; *s; s++) {
    unsigned char c = *s;
    if (c < 32 || c > 127) c = 32;
    int idx = c - 32;
    if (font_ == 4) {
      int width = widtbl_f32[idx], height = chr_hgt_f32, total = width * height, pc = 0;
      const unsigned char *p = chrtbl_f32[idx];
      if (fg_ != bg_) fillRect(px, y, width, height, bg_);
      while (pc < total) {
        unsigned char line = *p++;
        if (line & 0x80) {
          int n = (line & 0x7F) + 1;
          while (n--) { ink(px + pc % width, y + pc / width); drawPixel(px + pc % width, y + pc / width, fg_); pc++; }
        } else {
          pc += line + 1;
        }
      }
      px += width;
    } else {
      int width = widtbl_f16[idx], height = chr_hgt_f16, bytes = (width + 6) / 8;
      const unsigned char *p = chrtbl_f16[idx];
      if (fg_ != bg_) fillRect(px, y, width, height, bg_);
      for (int i = 0; i < height; i++)
        for (int k = 0; k < bytes; k++) {
          unsigned char line = p[bytes * i + k];
          for (int bit = 0; bit < 8; bit++)
            if (line & (0x80 >> bit)) { ink(px + k * 8 + bit, y + i); drawPixel(px + k * 8 + bit, y + i, fg_); }
        }
      px += width;
    }
  }
  TextBox &b = textLog[logAt];
  if (inkX1_ >= inkX0_) b.ix = inkX0_, b.iy = inkY0_, b.iw = inkX1_ - inkX0_ + 1, b.ih = inkY1_ - inkY0_ + 1;
  return cw;
}

// ---- Sprites -------------------------------------------------------------------
int spriteBudget16 = -1;

void *TFT_eSprite::createSprite(int16_t w, int16_t h) {
  if (depth_ == 16 && spriteBudget16 >= 0) {
    if (spriteBudget16 == 0) return nullptr;
    spriteBudget16--;
  }
  w_ = w;
  h_ = h;
  buf.assign(w * h, 0);
  return buf.data();
}

void TFT_eSprite::pushSprite(int32_t x, int32_t y) { pushSprite(x, y, 0, 0, w_, h_); }

bool TFT_eSprite::pushSprite(int32_t tx, int32_t ty, int32_t sx, int32_t sy, int32_t sw, int32_t sh) {
  parent_->pixelsPushed += (uint64_t)max(0, sw) * max(0, sh);
  parent_->pushes++;
  for (int j = 0; j < sh; j++)
    for (int i = 0; i < sw; i++) {
      int x = sx + i, y = sy + j;
      if (x < 0 || y < 0 || x >= w_ || y >= h_) continue;
      parent_->drawPixel(tx + i, ty + j, buf[y * w_ + x]);
    }
  return true;
}
