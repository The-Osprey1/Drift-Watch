# Drift-Watch v0.15.2: Cupertino Web Studio & Direct Touch Gesture Architecture

## Summary

`v0.15.2` brings a cohesive Apple Human Interface & Cupertino design system to the Drift-Watch Web Studio (`companion/index.html`) and aligns the 240×240 circular OLED touch gestures (`Swipe up` = Quick Actions, `Slide down` = Settings) across both the companion simulator and the ESP32-S3 firmware (`DriftWatch/ui_input.cpp`).

## Web Studio (`companion/index.html`)

- **Adaptive Cupertino Dark & Light Appearance**: Translucent glass header and cards (`backdrop-filter: blur(28px) saturate(190%)`), specular 1px borders, dynamic per-face `--face-accent` color syncing across all 12 horological faces, and a 1-click Light/Dark appearance toggle in the top header.
- **Simplified 2-Column Workspace**:
  - **Left Sticky Stage**: Buttonless circular 240×240 OLED touch enclosure (`480×480` HiDPI canvas) with no physical crown or hardware buttons, direct touch/drag gesture support, 3-pill screen switcher (`Watch Face`, `Quick Actions`, `Settings`), and collapsible hardware spec disclosure.
  - **Right 4-Tab Control Deck**:
    1. **Face Gallery**: Spacious 2-column face cards with unclipped titles and descriptions, active face checkboxes (`FACES:0x...`), motion mode selector (`Off`, `Subtle`, `Expressive`), and a collapsible *Collection Presets & Saved Profiles* disclosure drawer.
    2. **Sync & Wi-Fi**: Transport connection bar (**Bluetooth**, **USB-C Cable**, **Virtual Watch**), 1-click **Sync Time & Time Zone**, 2.4 GHz Wi-Fi credentials form, and collapsible *Offline Setup & Windows Auto-Sync* utilities.
    3. **Quick Actions**: Interactive countdown timer with 1m / 3m / 5m / 10m presets, split-lap stopwatch, daily alarm & 2-minute auto-timeout flashlight torch, and a collapsible *On-Watch Settings & Brightness* drawer.
    4. **Diagnostics**: Live battery discharge and QMI8658 IMU temperature/drift telemetry curves, interactive serial/BLE command console (`INFO?`, `TIME:`, `FACES:`, `TIMER:`, `BRIGHT:`, `MOTION:`, `FORMAT:`, `NIGHT:`, `FACE:`), and firmware download links.
- **Bluetooth Picker Reliability (`PR #22`)**: Tapping **Bluetooth** always opens `navigator.bluetooth.requestDevice(...)` directly rather than reusing a potentially unreachable `getDevices()` handle, and formats connection errors safely when mobile Web Bluetooth browsers omit `err.message`.

## Watch Firmware (`DriftWatch/`)

- **Direct Touch Navigation (`DriftWatch/ui_input.cpp`, `DriftWatch/ui_motion.cpp`)**:
  - **Swipe up** on the watch face opens **Quick Actions** (`Mode::Drawer`), and **Swipe down** inside Quick Actions closes it back to the watch face.
  - **Slide down** on the watch face pulls down **Settings** (`Mode::Panel`) pinned to the finger, and **Swipe up** inside Settings returns to the watch face.
- **Expanded USB-C / BLE Live Control Commands (`DriftWatch/serial_cmd.cpp`, `DriftWatch/ui_input.cpp`)**:
  - Added `TIMER:<sec>`, `BRIGHT:<0..3>`, `MOTION:<0..2>`, `FORMAT:<12|24>`, `NIGHT:<0..2>`, and `FACE:<0..11>`, plus extended `INFO?` status reporting (`DRIFT:VER=0.15.2,...`).
