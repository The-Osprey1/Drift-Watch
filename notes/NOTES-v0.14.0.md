# Drift-Watch v0.14.0 release notes

Precision face revamp (PR #9), hot-swap faces, chronometer boot sequence and radial iris wake/sleep transitions (PR #11), post-merge repair of conflicts, frame budgets and wake limits (PR #12, `68b3a5c`), and split of `ui.cpp` into focused units (PR #13, `6e02bce`). Released images built from `main` at `68b3a5c`. `v0.11.1` verified boot on a real watch; `v0.12.0` through `v0.14.0` have not yet been flashed on hardware.

## What changes on the watch

### Faces, boot and transitions

- **Precision face revamp (PR #9).** Upright vector instrument numerals on Digital, Radar, Orbit and Yozakura, quieter supporting marks, and distinct analog hand styles across the dials.
- **Hot-swap active faces (PR #11 & PR #12).** Choose which of the twelve faces stay in your swipe loop:
  - In **Settings > Hot swap**, tap to switch between **All (12)** and a quick two-face pair (your current face plus Orbit, or Digital when on Orbit), highlighted in the current face's theme colour.
  - On the **Drift sync web page** (`companion/index.html`, or over Bluetooth via `H=<mask>`), toggle any combination of the twelve faces; if your current face is turned off, the watch moves to the first enabled face. The mask is stored in NVS (`faces`) and survives restarts.
- **Chronometer startup sequence (PR #11).** Cold boot now opens with a 12-point bezel shutter, counter-rotating telemetry arcs, hero hold and smooth iris transition onto the face (or touch check on a new screen turn). Any touch or BOOT press skips straight to the face.
- **Radial iris wake and sleep (PR #11 & PR #12).** Waking from standby blooms outward from the centre in the current face's accent colour alongside a perceptual backlight ramp; going to sleep collapses smoothly in reverse, and a touch or BOOT press mid-collapse catches the screen and blooms back open. Both iris transitions are skipped below 20% battery (`lowPower()`) or with **Motion: Off**, and the v0.13.0 low-battery notice still shows on the face for 5 seconds below 15% and 5%.
- **Ambient face touches on Expressive (PR #11 & PR #12).** On **Motion: Expressive**, Digital (colon pulse and subtle scan line), Orbit (star twinkle), Lunar (star twinkle) and Yozakura (petal shimmer) animate at the 20 fps ambient budget while the screen is on and battery is at least 20%. On **Subtle** (the default), only Radar and Phantom animate between seconds; all other faces draw at most once a second.

### Repairs and structure (PR #12 & PR #13)

- **Restored v0.13.0 wrist-gesture thresholds and 100 ms standby polling.** Keeps raise-to-wake (`UP_Z = 0.65`, `STEADY_G = 0.2`, `HOLD_MS = 200`, `FLICK_DPS = 80`), wrist-twist (`CALM_DPS = 30`, `CALM_G = 0.15`, `CALM_MS = 300`, `OUT_DEG = 40`, `TWIST_DPS = 150`, `BACK_DEG = 15`, `BACK_MS = 400`), and screen-off polling (`pollDozeMs = 100`) at their tested false-wake budgets.
- **Settings page count fixed.** Removed the blank 14th slot so Settings has 13 real controls across 7 pages (or 13 single-control pages with Large text, with no empty trailing page).
- **Shared face-mask definition.** Extracted `enum Face`, `FACE_COUNT` and `ALL_FACES_MASK` into `DriftWatch/face_id.h` so `settings`, `phone`, `screens` and `ui` never hardcode `12` or `0x0FFF`.
- **Modular `ui` implementation (PR #13).** Split the 2,095-line `DriftWatch/ui.cpp` into `ui.cpp` (state, battery/low-power, sync back-off, public loop), `ui_render.cpp` (sprite presentation and boot/face/diag steps), `ui_motion.cpp` (entrances, slides, iris transitions and standby), `ui_input.cpp` (navigation, touch, gestures, BOOT, BLE and serial), and `ui_parts.h`.

## Tests (CI on `main`)

| Check | Result |
|---|---|
| `render_faces` | 0 problems |
| `simulate` (interface, including Expressive ambient budgets, low-battery pause, Settings pages and `H=` mask persistence) | 491 of 491 |
| `simulate_network` (interface with real network code) | 54 of 54 |
| `net_test` (network code and DST rules) | 239 of 239 |
| `power_test` (sensor registers and screen-off waits) | 83 of 83 |
| `mutation_check.sh` | 19 of 19 planted bugs caught (including restored `entrance-on-wake`) |
| `tools/validate_image.py 0.14.0` | both images pass (layout, partition checksum, size, version, no stray names or test passwords) |
| Firmware compiler warnings (`DriftWatch/`) | 0 in both normal and diagnostic builds |

## Hardware verification status

- **Pending on hardware** for `v0.12.0`–`v0.14.0` (boot sequence, iris transitions, hot-swap faces, watchdog + light sleep, I2C bus recovery, QR scan code on panel, and charging bolt). `v0.11.1` was verified booting on the real watch.
