// Design system: the one place for colours, type, spacing, strokes and
// motion timing. Faces and screens use these names instead of raw values.
#pragma once
#include <stdint.h>

// 24-bit #RRGGBB to the panel's 16-bit RGB565.
constexpr uint16_t rgb(uint32_t hex) {
  return ((hex >> 8) & 0xF800) | ((hex >> 5) & 0x07E0) | ((hex >> 3) & 0x001F);
}

struct FaceTheme {
  uint16_t primary;
  uint16_t secondary;
  uint16_t pressed;
  uint16_t outline;
};

namespace col {
// Identity colours. Each face picks one as primary and uses the others sparingly.
constexpr uint16_t YELLOW = rgb(0xFCEE0A);
constexpr uint16_t CYAN = rgb(0x00F0FF);
constexpr uint16_t MAGENTA = rgb(0xFF2A6D);
constexpr uint16_t RED = rgb(0xFF3B30);      // Corpo accents
constexpr uint16_t WARN = rgb(0xFF7A1A);     // low battery, errors (always paired with text or an icon)

// Neutrals, darkest to lightest.
constexpr uint16_t BG = rgb(0x000000);
constexpr uint16_t N900 = rgb(0x07080B);     // dial faces
constexpr uint16_t N800 = rgb(0x0E1016);     // panels, buildings
constexpr uint16_t N700 = rgb(0x1A1E27);     // faint structure lines
constexpr uint16_t N600 = rgb(0x2A303C);     // inactive ticks and tracks
constexpr uint16_t N500 = rgb(0x464E5E);     // minor detail
constexpr uint16_t N400 = rgb(0x6B7385);     // tertiary text
constexpr uint16_t N300 = rgb(0x9AA2B4);     // secondary text
constexpr uint16_t N100 = rgb(0xDCE2EA);     // silver
constexpr uint16_t WHITE = rgb(0xFFFFFF);

// Tinted darks for atmosphere.
constexpr uint16_t YELLOW_DIM = rgb(0x5C5604);
constexpr uint16_t CYAN_DIM = rgb(0x0A4A55);
constexpr uint16_t CYAN_DEEP = rgb(0x05262C);
constexpr uint16_t MAGENTA_DIM = rgb(0x4A1024);
constexpr uint16_t WINDOW = rgb(0x6E6610);   // lit windows, kept low so the skyline stays background
constexpr uint16_t PHOSPHOR = rgb(0x19FFB0);  // radar beam: the green-cyan of a P7 radar tube
constexpr uint16_t PHOSPHOR_TRAIL = rgb(0x0B8F68);  // trail at full strength, before fading

// Roles on the settings and setup screens. Contrast on black, from these tokens
// (checked by tools/host/simulate): N100 16.3:1, N300 8.2:1, CYAN 15.2:1,
// MAGENTA 5.8:1, WARN 8.0:1 for text; OUTLINE 4.3:1 for control edges (3:1 goal).
constexpr uint16_t TEXT = N100;              // essential text and values
constexpr uint16_t TEXT_2 = N300;            // labels and supporting text
constexpr uint16_t OUTLINE = N400;           // control edges

// Configurator Cupertino Studio elevated platter tokens
constexpr uint16_t PANEL_SOLID = rgb(0x1C1C1E);  // Elevated solid card/platter fill (--panel-solid)
constexpr uint16_t PANEL_INSET = rgb(0x2C2C2E);  // Inset controls / pressed platter fill (--panel-inset)
constexpr uint16_t PANEL_BORDER = rgb(0x383D48); // Subtle hairline card outline (~rgba(255,255,255,0.14))
constexpr int PLATTER_RAD = 14;                  // Standard platter corner radius
}  // namespace col

namespace type {
// Built-in TFT_eSPI fonts, used by the watch faces. Line heights: font 2 = 16 px, font 4 = 26 px.
constexpr uint8_t SMALL = 2;    // secondary information
constexpr uint8_t MEDIUM = 4;   // date
// Settings and setup text uses bold bitmap fonts (FreeSansBold, see gfx::font) in two roles.
// Measured capital heights: 9 pt = 13 px, 12 pt = 18 px, 18 pt = 26 px.
//   role    normal   large text
//   Body    9 pt     12 pt  (labels, instructions, button words; 138%)
//   Title   12 pt    18 pt  (values and headlines; 144%)
enum class Role : uint8_t { Body, Title };
// Segment time digits, height in pixels.
constexpr float TIME_HERO = 64;   // digital face
constexpr float TIME_INSET = 50;  // radar face
}  // namespace type

namespace layout {
constexpr int CX = 120, CY = 120;
constexpr int SAFE_R = 112;     // all text and controls stay inside this circle
constexpr int DECOR_R = 119;    // decoration may reach this far
constexpr int GAP = 4;          // base spacing unit
// Touch target goal for settings and setup: 68 px is about 9.2 mm at 0.135 mm per pixel.
constexpr int TOUCH_MIN = 68;
constexpr int TOUCH_SPACING = 8; // smallest gap between neighbouring touch regions
}  // namespace layout

namespace stroke {
constexpr float FINE = 1.0f;
constexpr float NORMAL = 2.0f;
constexpr float BOLD = 3.0f;
}  // namespace stroke

namespace motion {
// Durations in milliseconds. Everything is timed from elapsed time, not frames.
// Three layers, in strict priority: interaction (drag, settle, panel, press), then a face's one
// entrance after it is chosen, then data (seconds, Orbit's minute marker, battery, sync state).
constexpr uint32_t PRESS_MS = 0;        // pressed states draw on the touch-down poll
constexpr uint32_t RELEASE_MS = 100;    // a tapped control keeps its accent this long after release
constexpr uint32_t STATE_MS = 140;      // small state changes
constexpr uint32_t FACE_MS = 220;       // face-to-face slide started by BOOT
constexpr uint32_t PANEL_MS = 220;      // status panel slide
constexpr uint32_t WAKE_MS = 300;       // smooth iris bloom and perceptual backlight fade-in
constexpr uint32_t SLEEP_MS = 280;      // smooth iris collapse and perceptual backlight fade-out
constexpr uint32_t BOOT_MS = 1750;      // cold-boot chronometer ignition sequence
// Radar: one turn of the beam every 3.6 s (16.7 turns a minute), like a real plan position
// indicator, with a phosphor trail that fades exponentially behind it.
constexpr uint32_t RADAR_TURN_MS = 3600;
constexpr float RADAR_TRAIL_DEG = 105;  // how far behind the beam the trail is drawn
constexpr float RADAR_DECAY_DEG = 32;   // the trail's brightness falls to 37% over this angle
constexpr uint32_t RADAR_FRAME_MS = 33; // about 30 frames a second: 3.3 degrees a frame
constexpr int LOW_BATTERY_PCT = 20;     // below this the beam only moves with the seconds
// Phantom: the balance wheel and seconds hand at about 30 frames a second while it is on screen.
constexpr uint32_t PHANTOM_FRAME_MS = 33;
constexpr uint32_t AMBIENT_FRAME_MS = 50;  // 20 fps ambient animation for lively faces
// Entrances and Orbit's minute marker run at the slide frame rate, then the face drops back to
// one frame a second.
constexpr uint32_t ENTRANCE_FRAME_MS = 33;
constexpr uint32_t MARKER_MS = 180;     // Orbit's minute marker moves 6 degrees at each new minute
// Expressive plays each entrance this much longer (as a fraction: 3 / 2).
constexpr uint32_t EXPRESSIVE_NUM = 3, EXPRESSIVE_DEN = 2;

// Touch thresholds.
constexpr int SLOP_PX = 10;             // movement before a touch counts as a drag
constexpr int COMMIT_PX = 70;           // drag distance that completes a face change
constexpr float FLICK_PX_PER_MS = 0.45f; // or a quick flick
constexpr uint32_t LONG_PRESS_MS = 600;

// After a release the face (or page) settles with ease-out, no bounce. The time depends on the
// distance left: 120 ms plus 100 ms for a whole screen width, between 120 and 220 ms, and a
// flick lands in at most 140 ms. Cancelling a short drag returns in 150 ms. The panel takes
// 180 to 240 ms the same way.
constexpr uint32_t SETTLE_MIN_MS = 120, SETTLE_MAX_MS = 220, SETTLE_FLICK_MS = 140, CANCEL_MS = 150;
constexpr uint32_t PANEL_MIN_MS = 180, PANEL_MAX_MS = 240;
inline uint32_t settleMs(float remainingPx, bool flick) {
  float ms = SETTLE_MIN_MS + 100.0f * remainingPx / 240.0f;
  uint32_t t = ms < SETTLE_MIN_MS ? SETTLE_MIN_MS : ms > SETTLE_MAX_MS ? SETTLE_MAX_MS : (uint32_t)(ms + 0.5f);
  return flick && t > SETTLE_FLICK_MS ? SETTLE_FLICK_MS : t;
}
inline uint32_t panelSettleMs(float remainingPx) {
  float ms = PANEL_MIN_MS + 60.0f * remainingPx / 240.0f;
  return ms > PANEL_MAX_MS ? PANEL_MAX_MS : (uint32_t)(ms + 0.5f);
}

// Ease-out cubic: fast start, decisive stop, no overshoot.
inline float easeOut(float t) {
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  float u = 1 - t;
  return 1 - u * u * u;
}

// Smooth S-curve for gentle start and gentle landing.
inline float easeInOut(float t) {
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  return t * t * (3.0f - 2.0f * t);
}
}  // namespace motion
