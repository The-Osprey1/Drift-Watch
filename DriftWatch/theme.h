// Design system: the one place for colours, type, spacing, strokes and
// motion timing. Faces and screens use these names instead of raw values.
#pragma once
#include <stdint.h>

// 24-bit #RRGGBB to the panel's 16-bit RGB565.
constexpr uint16_t rgb(uint32_t hex) {
  return ((hex >> 8) & 0xF800) | ((hex >> 5) & 0x07E0) | ((hex >> 3) & 0x001F);
}

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
constexpr float TIME_HERO = 56;   // digital face
constexpr float TIME_INSET = 44;  // radar face
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
constexpr uint32_t PRESS_MS = 0;        // pressed states draw on the touch-down poll
constexpr uint32_t STATE_MS = 140;      // small state changes
constexpr uint32_t FACE_MS = 220;       // face-to-face slide
constexpr uint32_t PANEL_MS = 260;      // status panel slide
constexpr uint32_t WAKE_MS = 120;       // backlight fade-in after the time is already drawn
constexpr uint32_t BOOT_MS = 900;       // cold-boot sequence
// Radar: one turn of the beam every 3.6 s (16.7 turns a minute), like a real plan position
// indicator, with a phosphor trail that fades exponentially behind it.
constexpr uint32_t RADAR_TURN_MS = 3600;
constexpr float RADAR_TRAIL_DEG = 105;  // how far behind the beam the trail is drawn
constexpr float RADAR_DECAY_DEG = 32;   // the trail's brightness falls to 37% over this angle
constexpr uint32_t RADAR_FRAME_MS = 33; // about 30 frames a second: 3.3 degrees a frame
constexpr int LOW_BATTERY_PCT = 20;     // below this the beam only moves with the seconds

// Touch thresholds.
constexpr int SLOP_PX = 10;             // movement before a touch counts as a drag
constexpr int COMMIT_PX = 70;           // drag distance that completes a face change
constexpr float FLICK_PX_PER_MS = 0.45f; // or a quick flick
constexpr uint32_t LONG_PRESS_MS = 600;

// Spring used for slides after a swipe: it starts at the finger's speed and settles on the
// target without bouncing (critically damped). OMEGA sets how quickly, per millisecond.
constexpr float SPRING_OMEGA = 0.03f;   // half way in about 50 ms, settled in about 200 ms

// Ease-out cubic: fast start, decisive stop, no overshoot.
inline float easeOut(float t) {
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  float u = 1 - t;
  return 1 - u * u * u;
}
}  // namespace motion
