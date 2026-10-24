// Shared drawing components built on TFT_eSPI sprites.
#pragma once
#include <TFT_eSPI.h>
#include "theme.h"

struct Pt { float x, y; };

namespace gfx {

// Point on a circle around the screen centre, angle in degrees clockwise from 12.
Pt polar(float deg, float r);
// Point along a direction from the centre: `along` the hand, `side` across it.
Pt alongHand(float deg, float along, float side);

void fillPoly(TFT_eSprite &s, const Pt *p, int n, uint16_t c);
// Mixes two RGB565 colours: `a` = 1 gives `fg`, 0 gives `bg`.
uint16_t blend(uint16_t fg, uint16_t bg, float a);
void strokePoly(TFT_eSprite &s, const Pt *p, int n, uint16_t c);
// Straight radial bar from r0 to r1, `w` pixels wide.
void radialBar(TFT_eSprite &s, float deg, float r0, float r1, float w, uint16_t c);
// Box with the top-left and bottom-right corners cut off.
void cutBox(TFT_eSprite &s, int x, int y, int w, int h, int cut, uint16_t c, bool filled);
// Ring segment clockwise from 12 o'clock.
void arc(TFT_eSprite &s, int r, int ir, float fromDeg, float toDeg, uint16_t c, bool smooth = true);

// Segment-style time digits. Every digit cell has the same width, so the time
// never shifts as it changes. `height` is in pixels.
struct TimeLayout {
  float x, y, height, width, digitW, scale;
};
TimeLayout timeLayout(float cx, float cy, float height);
void drawTime(TFT_eSprite &s, const TimeLayout &l, int hh, int mm, bool valid, bool blankLeadingZero, uint16_t c);

// Battery: outline icon plus percentage text, centred on (cx, cy).
// pct < 0 means the reading is unavailable.
void battery(TFT_eSprite &s, int cx, int cy, int pct, uint16_t ink);
// Battery as a ring gauge with the percentage inside, for dials.
void batteryGauge(TFT_eSprite &s, int cx, int cy, int r, int pct, uint16_t track, uint16_t fill, uint16_t ink);

// Status icons drawn as shapes, so meaning never depends on colour alone.
void iconCheck(TFT_eSprite &s, int cx, int cy, int size, uint16_t c);
void iconCross(TFT_eSprite &s, int cx, int cy, int size, uint16_t c);
void iconAlert(TFT_eSprite &s, int cx, int cy, int size, uint16_t c);

// Small clock symbol: "in progress" without motion, used instead of a spinner with reduced motion.
void iconPending(TFT_eSprite &s, int cx, int cy, int size, uint16_t c);

// Text helpers. All text is drawn transparent so it sits cleanly on any background.
void text(TFT_eSprite &s, const char *str, int x, int y, uint8_t font, uint16_t c, uint8_t datum = MC_DATUM);
// Shortens `str` with "..." until it fits `maxW` pixels. Returns the fitted copy in `out`.
// Only for text that is not essential (network and zone names); essential text wraps instead.
const char *fit(TFT_eSprite &s, const char *str, uint8_t font, int maxW, char *out, size_t outSize);

// Bold bitmap fonts for settings and setup, by role and text size (see type::Role).
const GFXfont *font(type::Role role, bool large);
void text(TFT_eSprite &s, const char *str, int x, int y, const GFXfont *f, uint16_t c, uint8_t datum = MC_DATUM);
int textWidth(TFT_eSprite &s, const char *str, const GFXfont *f);
inline int lineHeight(const GFXfont *f) { return f->yAdvance; }
// Length of the next line of `str` from `start` that fits `maxW` pixels, breaking at
// spaces, else after '/', and mid-word only if one word is wider than the line. Leading spaces
// are skipped and returned in `skip`. Returns 0 at the end of the string.
int nextLine(TFT_eSprite &s, const char *str, int start, const GFXfont *f, int maxW, int &skip);
// Widest text that fits rows [top, bottom) inside the safe circle, less `margin` each side.
int chordWidth(int top, int bottom, int margin);

// Low skyline silhouette along the bottom of the screen. Windows change with `seed`.
void skyline(TFT_eSprite &s, int horizon, uint32_t seed);

}  // namespace gfx
