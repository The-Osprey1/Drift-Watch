# Drift-Watch v0.15.0 release notes

Comprehensive feature release compiling all post-v0.14.0 pull requests (PR #15 through #18):
- **PR #15** (`82e2711`): Digital watch face visual cleanup, PhoneSync QR code screen, QMI8658 temperature telemetry.
- **PR #16** (`94e4827`): Settings 4-Hub Overhaul with 1-tap category header navigation and hub-aware dot clusters.
- **PR #17** (`6ed7db2`): Swipe-Down Quick Drawer, Countdown Timers with dial display and visual strobe alarm, Flashlight tool, Instant USB-C Auto-Sync protocol, Windows PnP auto-sync script, and Web Serial companion listener.
- **PR #18** (`391caba`): Automatic CI build artifact distribution of merged binary images.
- **Hardware Verification**: Successfully flashed and verified on the physical Waveshare ESP32-S3-Touch-LCD-1.28 watch on COM6.

---

## What changes on the watch

### 1. Quick Drawer (Swipe-Down Gesture)
- **Top Pull-Down Shade**: Swiping downward from any watch face (`Mode::Face`) smoothly pulls down the Quick Tools drawer (`openDrawer()`).
- **One-Motion Dismissal**: Swiping upward or tapping the physical BOOT button instantly slides the drawer back up to the watch face.
- **Responsive Layout**:
  - *Standard Mode*: 2 clean pages (Page 0: 1-minute Timer + Flashlight; Page 1: 3-minute Timer + 5-minute Timer).
  - *Large Text Mode*: 4 single-tool tall pages, fully respecting circular display safety margins (<= 112 px from center).

### 2. Interactive Countdown Timers & Visual Strobe Alarm
- **Presets & Live Countdown**: Single-tap starting for 1-minute, 3-minute, and 5-minute countdowns. Active timer displays a clean `MM:SS` countdown and circular dial progress ring.
- **Background Light-Sleep Wakeup**: When the display turns off during a countdown, `nextDeadline()` schedules the ESP32-S3 hardware timer to wake automatically with sub-millisecond precision.
- **Visual Strobe Alarm**: Tailored specifically for buzzer-less hardware. Upon expiration, the screen wakes up and alternates high-contrast warning colors with pulsed backlight (200 ms cadence). A single tap anywhere or a BOOT press dismisses the alarm and restores normal backlight.
- **One-Tap Reset**: Active countdowns can be stopped or reset instantly with the lower "Reset Timer" button.

### 3. Flashlight Tool
- **Full-Screen White Illumination**: Tapping Flashlight in the Quick Drawer sets the LCD backlight to maximum (255 PWM) and fills the round panel with pure white (`0xFFFF`).
- **One-Touch Dismissal**: Tapping anywhere on the screen immediately exits flashlight mode and restores your configured brightness level.

### 4. Settings 4-Hub Overhaul
- **Eliminated the 7-Page Swiping Bottleneck**: Reorganized 13 settings into 4 dedicated category hubs:
  - **Hub 0**: *Display & Style* (Brightness, Night Mode, Motion, Hot Swap)
  - **Hub 1**: *Clock & Sync* (Time Format, Set Time, Phone Sync, Sync Now, Wi-Fi Setup)
  - **Hub 2**: *Gestures & Wake* (Raise to Wake, Screen Turn)
  - **Hub 3**: *System & Info* (Touch Check, Details)
- **1-Tap Category Fast-Jump**: Tapping the top header area (`y < 42`) in Settings cycles directly across hubs in one tap, bypassing linear horizontal pagination.
- **Hub-Aware Dot Clusters**: Category cluster indicators illuminate according to the active hub.

### 5. Instant USB-C Time Sync & Serial Protocol
- **Expanded Serial Commands**:
  - `TIME:<epoch>`: Instant hand clock update (zero drift).
  - `WIFI:<ssid>,<password>`: Program Wi-Fi credentials over USB serial.
  - `FACES:<hex>`: Configure active hot-swap face mask directly over serial.
  - `INFO?` / `?`: Instant watch telemetry report (`DRIFT:VER=0.15.0,TIME=...,BAT=...,FACES=...`).
- **Zero-Install Windows Script**: Included `tools/windows/DriftWatchSync.ps1` detects the WCH CH343 USB-to-UART bridge (VID `1A86`, PID `55D3`) and synchronizes UTC time in milliseconds.
- **Web Serial API Auto-Sync**: The companion web app (`companion/index.html`) now supports Web Serial with automatic detection and time sync on cable plug-in.

### 6. Sensor Telemetry & Visual Fixes
- **QMI8658 Onboard Temperature**: The Details screen now reads the IMU internal temperature sensor and displays board thermal readings.
- **PhoneSync QR Code**: Direct scan-to-connect QR code rendered on the PhoneSync screen for smartphone pairing.
- **Digital Watch Face Fix**: Eliminated the graphic animation yellow bracket carryover artifacts on the Digital instrument face.

---

## Checks & Verification

| Suite | Status | Details |
| :--- | :--- | :--- |
| `simulate` | Passed | 496 of 496 interface and drawer scenarios |
| `simulate_network` | Passed | 54 of 54 live network simulation tests |
| `net_test` | Passed | 239 of 239 NTP, drift correction and DST rules |
| `power_test` | Passed | 83 of 83 sensor registers and light-sleep controller |
| `render_faces` | Passed | 0 problems across all 12 precision faces |
| `mutation_check.sh` | Passed | 19 of 19 planted regression bugs caught |
| `HOST_STRICT` | Passed | Zero warnings under Address and Undefined Behaviour Sanitizers |
| Hardware Test | **Passed** | Verified live boot, CST816S touch, QMI8658 IMU, serial commands, and timer strobe on COM6 |
