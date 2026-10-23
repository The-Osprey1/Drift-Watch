// Host stand-in for the parts of TFT_eSPI the watch uses. Drawing follows the
// library's own algorithms (triangle and circle fills, distance-based
// anti-aliasing, fonts 2 and 4 from the library's font tables), so previews
// match what the watch draws closely. Everything renders into memory.
#pragma once
#include "Arduino.h"
#include <vector>

enum { TL_DATUM, TC_DATUM, TR_DATUM, ML_DATUM, MC_DATUM, MR_DATUM, BL_DATUM, BC_DATUM, BR_DATUM };
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF

class TFT_eSPI {
 public:
  TFT_eSPI(int w = 240, int h = 240);
  virtual ~TFT_eSPI() {}
  void init() {}
  void setRotation(int) {}
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

  void setTextFont(uint8_t f) { font_ = f; }
  void setTextDatum(uint8_t d) { datum_ = d; }
  void setTextColor(uint16_t fg) { fg_ = bg_ = fg; }
  void setTextColor(uint16_t fg, uint16_t bg) { fg_ = fg; bg_ = bg; }
  int16_t textWidth(const char *s) const;
  int16_t fontHeight() const;
  int16_t drawString(const char *s, int32_t x, int32_t y);

  std::vector<uint16_t> buf;
  uint8_t lastCommand = 0;
  uint64_t pixelsPushed = 0;  // pixels sent by sprite pushes, to estimate panel transfer time
  uint32_t pushes = 0;

  // Every string drawn since the last clear, with its box, for edge checks.
  struct TextBox { std::string text; int x, y, w, h; };
  std::vector<TextBox> textLog;

 protected:
  void blendPixel(int32_t x, int32_t y, uint16_t fg, float alpha, uint32_t bg);
  int w_, h_;
  uint8_t font_ = 2, datum_ = TL_DATUM;
  uint16_t fg_ = 0xFFFF, bg_ = 0xFFFF;
};

class TFT_eSprite : public TFT_eSPI {
 public:
  explicit TFT_eSprite(TFT_eSPI *parent) : TFT_eSPI(0, 0), parent_(parent) {}
  void *createSprite(int16_t w, int16_t h);
  void deleteSprite() { buf.clear(); w_ = h_ = 0; }
  void *setColorDepth(int8_t) { return nullptr; }
  void *getPointer() { return buf.data(); }
  void fillSprite(uint32_t c) { fillScreen(c); }
  void pushSprite(int32_t x, int32_t y);
  bool pushSprite(int32_t tx, int32_t ty, int32_t sx, int32_t sy, int32_t sw, int32_t sh);

 private:
  TFT_eSPI *parent_;
};
