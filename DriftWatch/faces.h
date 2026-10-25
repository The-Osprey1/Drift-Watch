// Watch faces. Each face is split into a static layer, which changes at most
// once a minute and is cached, and a live layer drawn on top every second or
// animation frame.
#pragma once
#include <TFT_eSPI.h>
#include <time.h>
#include "face_id.h"
#include "theme.h"

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
  // v0.12.0. Left at zero, both mean settled: the face is drawn in its final state, which is
  // exactly what Motion Off shows and where every entrance ends.
  bool entering;      // the face's entrance is playing
  uint32_t entranceMs;  // time into the entrance, on the face's own timeline (see faceEntranceMs)
  bool markerMoving;  // Orbit: the minute marker is moving on to the new minute
  uint32_t markerMs;  // time since that minute began, 0 .. motion::MARKER_MS
  // v0.13.0
  bool charging;      // on the charger, as far as the battery level shows (see battery.h): a small bolt by the battery
};

// Draws the parts that only change once a minute (or when the date, battery
// or settings change).
void drawFaceStatic(TFT_eSprite &s, Face f, const FaceContext &c);
// Draws the per-second and animated parts over the static layer.
void drawFaceLive(TFT_eSprite &s, Face f, const FaceContext &c);
// Changes whenever the static layer needs redrawing.
uint32_t faceStaticKey(Face f, const FaceContext &c);
const char *faceName(Face f);
FaceTheme faceTheme(Face f);
// Faces that need frames between seconds while `animate` is on. `expressive` adds the ambient
// touches (colon pulse, scan line, star twinkle, petal shimmer) to Subtle's Radar and Phantom.
bool faceAnimates(Face f, bool expressive = false);
// Length of the face's entrance, played once after the face is chosen (never on wake), or 0 if
// it has none. Only the decoration moves: the time is correct from its first frame.
uint32_t faceEntranceMs(Face f);
// Faces whose minute marker moves at a new minute (Orbit), instead of jumping.
bool faceMovesMarker(Face f);

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

// Yozakura's entrance: two petals fall on curved paths beside the time and date, which they never
// cross. Pure functions, so the tests can check every millisecond of both paths.
namespace yozakura {
constexpr int PROTECT_X0 = 38, PROTECT_Y0 = 72, PROTECT_X1 = 202, PROTECT_Y1 = 164;  // time and date
constexpr uint32_t ENTRANCE_MS = 900;
constexpr int PETALS = 2;
struct Petal {
  bool visible;
  float x, y;    // centre, px
  float rot;     // degrees
  float size;    // half length, px
  float alpha;   // 0..1, fading near the end of its fall
};
Petal petalAt(int i, uint32_t ms);
}

// Orbit: the minute marker on the orbit track, in degrees clockwise from 12.
namespace orbit {
constexpr float TRACK_R = 104;
constexpr uint32_t ENTRANCE_MS = 500;
// `minuteDeg` is the true minute (6 degrees a minute). In the entrance the marker eases through
// its last 25 degrees; at a new minute it moves the last 6 over motion::MARKER_MS.
float markerDeg(float minuteDeg, const FaceContext &c);
bool markerShown(const FaceContext &c);
}
