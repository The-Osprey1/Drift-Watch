// Watch faces. Each face is split into a static layer, which changes at most
// once a minute and is cached, and a live layer drawn on top every second or
// animation frame.
#pragma once
#include <TFT_eSPI.h>
#include <time.h>

// Swipe order. The numbers are stored in the settings, so new faces only ever go at the end.
enum Face : uint8_t { FACE_DIGITAL, FACE_STEALTH, FACE_CORPO, FACE_HOLO, FACE_RADAR, FACE_PHANTOM, FACE_COUNT };

enum class SyncView : uint8_t { Synced, Syncing, Failed, Never };

struct FaceContext {
  tm now;
  bool timeValid;
  bool use12h;
  int batteryPct;     // -1 = unavailable
  SyncView sync;
  uint8_t syncStage;  // 1..3 while syncing (joining, time zone, clock), else 0
  bool noWifi;        // no network saved: the fix is Wi-Fi setup, not a sync
  float beamDeg;      // radar beam angle, degrees clockwise from 12, or < 0 for no beam
  // Time for faces that move between seconds. Faces never read a clock themselves: the caller
  // passes it in, so tests can draw any moment exactly.
  uint16_t ms;        // milliseconds into the second `now` shows (wall clock, so a sync moves it too)
  uint32_t animMs;    // monotonic time for decorative motion that is not the time of day
  bool animate;       // smooth motion allowed now: off with reduced motion, low battery or no
                      // cached dial, when hands step once a second and mechanisms stand still
};

// Draws the parts that only change once a minute (or when the date, battery
// or settings change).
void drawFaceStatic(TFT_eSprite &s, Face f, const FaceContext &c);
// Draws the per-second and animated parts over the static layer.
void drawFaceLive(TFT_eSprite &s, Face f, const FaceContext &c);
// Changes whenever the static layer needs redrawing.
uint32_t faceStaticKey(Face f, const FaceContext &c);
const char *faceName(Face f);
// Faces that need frames between seconds while `animate` is on.
bool faceAnimates(Face f);

// Phantom: a dark dial with an opening at 12 o'clock (328 to 32 degrees) onto a movement with a
// 4 Hz balance wheel. The seconds hand slips under the opening's lip and is hidden from 58 s to
// 2 s, then comes back out. One timeline, in fractional seconds s and theta = 6 s:
//   5 <= s < 55    full length, 100 px
//   55 <= s < 58   drawn in, 100 to 22 px, fading out over the last part
//   58 <= s, s < 2 hidden: no needle, tail or counterweight
//   2 <= s < 5     out again, 22 to 100 px, fading in over the first part
// then a small rebound just after 5 s. Pure functions, so tests check the boundaries directly.
namespace phantom {
constexpr float APERTURE_FROM = 328, APERTURE_TO = 32;   // degrees clockwise from 12
constexpr float APERTURE_R0 = 30, APERTURE_R1 = 106;     // inner lip and outer edge, px
constexpr float FULL_PX = 100, LIP_PX = 22;
constexpr float REBOUND_S = 0.35f, REBOUND_DEG = 1.8f;
constexpr float BALANCE_HZ = 4, BALANCE_DEG = 150;       // four whole swings a second
struct Needle {
  bool visible;
  float deg;     // clockwise from 12, rebound included
  float length;  // px from the centre
  float alpha;   // 0..1, for the fade at either end of the eclipse
};
// `s` in [0, 60). `smooth` adds the rebound; without it (reduced motion) the caller passes
// whole seconds and gets the same lengths without the bounce.
Needle needleAt(float s, bool smooth);
float reboundDeg(float s);           // the rebound alone, 0 outside 5 s .. 5 s + REBOUND_S
float balanceDeg(uint32_t animMs);   // balance wheel angle; 0 is its rest position
bool inAperture(float deg);
}
