// Host stand-in for the parts of TFT_eSPI the watch uses. Drawing follows the
// library's own algorithms (triangle and circle fills, distance-based
// anti-aliasing, fonts 2 and 4 and the free bitmap fonts from the library's own
// font tables, with its datum and width rules), so previews match what the
// watch draws closely. Everything renders into memory.
#pragma once
#include "Arduino.h"
#include <vector>
#ifndef LOAD_GFXFF
#define LOAD_GFXFF 1  // as in build.sh: declares the library's free fonts
#endif
#include <Fonts/GFXFF/gfxfont.h>

enum { TL_DATUM, TC_DATUM, TR_DATUM, ML_DATUM, MC_DATUM, MR_DATUM, BL_DATUM, BC_DATUM, BR_DATUM,
       L_BASELINE, C_BASELINE, R_BASELINE };
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF

class TFT_eSPI {
 public:
  TFT_eSPI(int w = 240, int h = 240);
  virtual ~TFT_eSPI() {}
  void init() {}
  void setRotation(int r) { rotation = r & 3; }
  void writecommand(uint8_t c) { lastCommand = c; }
  int width() const { return w_; }
  int height() const { return h_; }

  void fillScreen(uint32_t c) { fillRect(0, 0, w_, h_, c); }
  void drawPixel(int32_t x, int32_t y, uint32_t c);
  uint16_t readPixel(int32_t x, int32_t y) const;
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c);
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c);
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c);
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c);
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c);
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c);
  void drawCircle(int32_t x0, int32_t y0, int32_t r, uint32_t c);
  void fillCircle(int32_t x0, int32_t y0, int32_t r, uint32_t c);
  void fillSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t c, uint32_t bg = 0x00FFFFFF);
  void drawWideLine(float ax, float ay, float bx, float by, float wd, uint32_t fg, uint32_t bg = 0x00FFFFFF);
  void drawWedgeLine(float ax, float ay, float bx, float by, float ar, float br, uint32_t fg, uint32_t bg = 0x00FFFFFF);
  void drawArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t start, uint32_t end, uint32_t fg, uint32_t bg,
               bool smooth = true);

  void setTextFont(uint8_t f) { font_ = f; gfxFont_ = nullptr; }
  void setFreeFont(const GFXfont *f);
  void setTextDatum(uint8_t d) { datum_ = d; }
  void setTextColor(uint16_t fg) { fg_ = bg_ = fg; }
  void setTextColor(uint16_t fg, uint16_t bg) { fg_ = fg; bg_ = bg; }
  int16_t textWidth(const char *s) const;
  int16_t fontHeight() const;
  int16_t drawString(const char *s, int32_t x, int32_t y);

  std::vector<uint16_t> buf;
  uint8_t lastCommand = 0;
  int rotation = 0;          // as set by setRotation; the simulated touch chip turns positions to match
  uint64_t pixelsPushed = 0;  // pixels sent by sprite pushes, to estimate panel transfer time
  uint32_t pushes = 0;

  // Every string drawn since the last clear, for edge and clipping checks: the
  // font's cell box, and the ink box of the pixels the glyphs actually set.
  struct TextBox { std::string text; int x, y, w, h; int ix, iy, iw, ih; const GFXfont *font; uint8_t builtin; };
  std::vector<TextBox> textLog;

 protected:
  void blendPixel(int32_t x, int32_t y, uint16_t fg, float alpha, uint32_t bg);
  int w_, h_;
  uint8_t font_ = 2, datum_ = TL_DATUM;
  const GFXfont *gfxFont_ = nullptr;
  int glyphAb_ = 0, glyphBb_ = 0;
  int inkX0_, inkY0_, inkX1_, inkY1_;
  void ink(int x, int y) { inkX0_ = std::min(inkX0_, x); inkY0_ = std::min(inkY0_, y); inkX1_ = std::max(inkX1_, x); inkY1_ = std::max(inkY1_, y); }
  uint16_t fg_ = 0xFFFF, bg_ = 0xFFFF;
};

// Number of 16-bit full frames the simulated PSRAM can still hold; -1 = no limit.
// Tests lower it to exercise the low-memory fallback.
extern int spriteBudget16;

class TFT_eSprite : public TFT_eSPI {
 public:
  explicit TFT_eSprite(TFT_eSPI *parent) : TFT_eSPI(0, 0), parent_(parent) {}
  void *createSprite(int16_t w, int16_t h);
  void deleteSprite() { buf.clear(); w_ = h_ = 0; }
  void *setColorDepth(int8_t d) { depth_ = d; return nullptr; }
  int8_t depth_ = 16;
  void *getPointer() { return buf.data(); }
  void fillSprite(uint32_t c) { fillScreen(c); }
  void pushSprite(int32_t x, int32_t y);
  bool pushSprite(int32_t tx, int32_t ty, int32_t sx, int32_t sy, int32_t sw, int32_t sh);

 private:
  TFT_eSPI *parent_;
};
