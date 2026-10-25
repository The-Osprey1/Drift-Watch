# Drift-Watch v0.12.0 release notes

Motion levels, entrances, and six new themed faces. Built from the v0.8 motion prototypes (`anim.py`, `faces.py`, `drift.py` and `MOTION-SPEC.md`), carried into the firmware on top of v0.11.1.

## What changes on the watch

- **Twelve faces.** Orbit, Lunar, Yozakura, Hanami, Wasteland and Mojave come after Phantom. The six older faces look and behave as before.
- **One entrance per face choice.** Picking a face plays its entrance once: Orbit 500 ms, Lunar 300 ms, Yozakura 900 ms, Hanami 600 ms, Wasteland 420 ms, Mojave 600 ms. The older faces have none. Waking the watch never plays one. A touch, the BOOT button, opening settings, going to sleep or changing face stops it at once and shows the settled face.
- **The time is correct from the first frame.** Entrances move only decoration. The time pixels are the same in every entrance frame as in the settled face.
- **Orbit minute marker** glides 6 degrees over 180 ms at each minute change (only for a single minute step; a jump such as a clock sync just redraws).
- **Swipes follow the finger.** The face is pinned to the finger while dragging, then settles in 120 to 220 ms by distance and speed. A flick settles in at most 140 ms, a cancelled swipe returns in 150 ms, and a new touch catches a sliding face where it is.
- **Settings panel follows an upward drag** and settles in 180 to 240 ms. It opens if dragged up 40 pixels or flicked up.
- **Release accent.** A short (100 ms) highlight on release, so a tap feels answered.
- **Wake** ramps the backlight over 160 ms (was 120 ms) and shows the face as it is, with no entrance. The wake touch itself does nothing.
- **Motion setting has three levels:** Off, Subtle (default) and Expressive. Expressive plays entrances 1.5 times as long. Off shows every change at once. Your old setting carries over: Reduced becomes Off, Full becomes Subtle.

## Rules kept

- No `delay()` and no heap use in the drawing loop; no new full-screen buffers. Entrances draw into the existing live layer at about 16 ms a frame and stop when they end.
- After an entrance the face goes back to its normal idle rate (at most one frame a second).
- Nothing decorative moves while the screen is off.
- Yozakura petals never enter the time area (x 38 to 202, y 72 to 164). The host test checks the petal path every millisecond and the pixels every 2 ms.
- The static and live layer split and the PSRAM cache are unchanged. Without spare memory (8-bit fallback) entrances are skipped and the face shows settled.

## Changed files and why

| File | Why |
|---|---|
| `DriftWatch/settings.h`, `settings.cpp` | Motion level Off/Subtle/Expressive replaces the on/off flag. Settings version 5. Reads the old flag when the new key is missing; still writes the old flag so an older build reads it sensibly. |
| `DriftWatch/theme.h` | Motion timings from the spec (settle, flick, cancel, panel, release, wake, entrance frame, marker) and the settle-time formulas. Old spring constants removed. |
| `DriftWatch/faces.h` | Six new faces; entrance and marker fields in the face context; entrance lengths; Yozakura petal path and protected area; Orbit marker math. |
| `DriftWatch/face_parts.h` (new) | Small shared helpers (hour, date text, no-time hint, sword hand) so the new faces reuse the existing look. |
| `DriftWatch/faces.cpp` | Helpers moved into the shared namespace; names and entrance lengths for the new faces. |
| `DriftWatch/faces_themed.cpp` (new) | The six themed faces. Everything an entrance moves is drawn by the same code as the settled face, so the last entrance frame equals the settled face and Motion Off. |
| `DriftWatch/gfx.h`, `gfx.cpp` | Arcs and dots at any centre and with a chosen background; ghost segments for Wasteland's time. |
| `DriftWatch/ui.cpp`, `ui.h` | Pinned drag, distance and speed based settle, cancel and catch, panel drag, release accent, entrance start and cancel, Orbit marker, wake ramp, three-level Motion button, test hooks. |
| `DriftWatch/screens.h`, `screens.cpp` | Motion row shows Off, Subtle or Expressive. |
| `DriftWatch/board.h` | Version 0.12.0. |
| `tools/host/*` | New face file in the builds; entrance checks and storyboards in `render_faces`; motion scenarios and settings migration in `simulate`; three new planted bugs in `mutation_check.sh`. |
| `designs/v0.12.0_*.png` | Faces, entrance storyboards and motion timeline, all from the real renderer at 240 by 240. |
| `README.md` | Faces, controls timing and Motion setting. |

## Where this differs from the prototypes

- Wasteland uses the watch's font 2 for its status line. The prototype used a monospaced font the watch does not have.
- The prototypes did not define Expressive; here it plays entrances 1.5 times as long and leaves interaction timing as Subtle.
- The analog themed faces reuse the existing sword hands rather than new hand shapes.
- Wake ramp went from 120 ms to 160 ms and the panel settle from 260 ms to the 180 to 240 ms range, as the spec asks.

## Tests (host, this commit)

| Check | Result |
|---|---|
| `render_faces` with entrance checks | 0 problems |
| `simulate` (interface) | 456 of 456 |
| `simulate_network` | 39 of 39 |
| `net_test` | 223 of 223 |
| `power_test` | 83 of 83 |
| `mutation_check.sh` | 13 of 13 planted bugs caught (new: entrance outlives touch, entrance on wake, petal crosses time) |
| Strict host build (warnings as errors, address and undefined-behaviour sanitizers) | clean |
| Firmware build, normal and diagnostic | both built, 0 warnings; `validate_image.py 0.12.0` passes both |

## Not run

- **Not run on the watch.** No hardware here. Everything above is host simulation plus a firmware build.
- **No measured frame rates, screen transfer times or battery figures.** The diagnostic image logs a `diag: entrance and marker frames` line on the watch; read it there to get real numbers.
- How the motion feels in the hand still needs a person to try it.
