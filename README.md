# Drift-Watch firmware (v0.8.0)

Our own watch software for the Waveshare ESP32-S3-Touch-LCD-1.28, written from scratch.

**Status:** it builds with no warnings, the image checks out, and the interface passes 136 scripted checks in a computer simulation that runs the real drawing, touch and settings code. It has **not** been run on a real watch yet. See `NOTES-v0.8.0.md` for what changed in this version and what still needs the board (`NOTES-v0.7.0.md` covers the earlier design pass).

## The board (checked against the Waveshare wiki)

| Part | What it is |
|---|---|
| Chip | ESP32-S3R2: two cores, 2 MB PSRAM, 16 MB flash |
| Screen | 1.28" round IPS LCD, 240 x 240, GC9A01A driver, 4-wire SPI up to 80 MHz. Lit by a backlight, so black pixels do **not** save power |
| Touch | CST816S on I2C (address 0x15), with interrupt and reset pins |
| Motion sensor | QMI8658 (wired up, not used yet) |
| Battery | Read through a 200k/100k divider on GPIO 1. The charger chip has no "charging" signal wired to the ESP32, so the watch cannot tell if it is charging |
| Not on the board | Clock chip that keeps time when unpowered, buzzer, vibration motor |

## Faces

Swipe left or right to move between them. Previews: `designs/v0.7.0_faces.png` and `designs/v0.8.0_faces_native.png`.

- **Digital:** yellow segment time, magenta date strip, quiet seconds track with one cyan tick for the current second, battery at the top, a low dark skyline.
- **Stealth dial:** silver markers and hands with cyan inlays, a magenta seconds hand, a date window and a battery ring.
- **Corpo dial:** black dial, a thin segmented yellow rim, black hands edged in yellow, red seconds hand.
- **Hologram dial:** cyan markers and edged hands, white inlays, magenta seconds hand.
- **Radar:** large yellow time in the middle, an hour ring that fills through the day, a minute track with a pointer, a magenta seconds dot. The sweep runs once when you arrive on the face and once a minute, then stops.

## Controls

| Where | Action | What happens |
|---|---|---|
| Face | Swipe left / right | Next / previous face. The face follows your finger; let go past a third of the way, or flick, to change |
| Face | Swipe up, hold for 0.6 s, or hold BOOT for 0.6 s | Open settings. A cyan ring grows while you hold |
| Face | Short press of BOOT | Next face |
| Settings, details, setup | Swipe left / right | Next / previous page. Never changes the face |
| Settings, details, setup | Swipe down, or BOOT | Back one level (setup or details to settings, settings to face) |
| Settings, details, setup | Tap a control | Acts on release. Sliding off a control cancels it |
| Screen off | Any touch or BOOT | Wakes it. That touch does nothing else |

Settings has three pages of two controls each (six pages of one with large text):

1. **Brightness** (four steps) and **Time format** (24-hour or 12-hour).
2. **Motion** (Full or Reduced; Reduced turns slides, the startup sequence, the wake fade, the radar sweep and spinners into instant changes) and **Sync now**, which also shows the sync state.
3. **Wi-Fi setup** (shows the saved network) and **Status and Text size**, which opens the details screen: battery, sync, time zone, network, version, and the **Text size** setting (Normal or Large, about 140%, for settings and setup screens only).

The screen turns off after 15 seconds (30 seconds on a setup result). The time resyncs every 6 hours while the screen is off (30 minutes after a failure).

## Flashing

1. Plug the watch into a computer with a USB-C **data** cable.
2. Open https://esp.huhn.me/ in Chrome or Edge, click Connect and pick the watch's port.
3. Choose `firmware/drift_watch_v0.8.0.bin` at address **0x0**, then Program.
4. Press reset or unplug and replug the watch.

If the browser can't find the watch, hold BOOT, tap RESET, release BOOT, then try again. To go back to the firmware the watch came with, flash your backup of it at 0x0 the same way.

`firmware/drift_watch_v0.8.0_diag.bin` is the same firmware with timing logs: it prints drawing time, screen transfer time and free memory over USB serial every 10 seconds, and shows a short version on the details screen.

## First start: Wi-Fi setup

If no Wi-Fi is saved, setup opens after the startup sequence. The screen walks you through it:

1. On your phone, join **Drift-Setup**.
2. A page should open by itself; if not, go to http://192.168.4.1.
3. Pick your home network and type its password. You can also type a time zone, or leave it blank to detect it.
4. Save. The watch says "Connecting" and shows each step (joining Wi-Fi, finding the time zone, setting the clock), then "All set". If it can't join or can't get the time, it says so, with **Edit Wi-Fi** (reopens the hotspot to fix the network or password) and **Retry**.

The hotspot closes after 5 minutes; the countdown is shown on the Cancel button. Cancel, swipe down or BOOT closes it sooner. While connecting, **Back** leaves it running in the background and **Cancel** stops it.

Time zone format, if you type one: `GMT0BST,M3.5.0/1,M10.5.0` (UK), `EST5EDT,M3.2.0,M11.1.0` (US Eastern), `CET-1CEST,M3.5.0,M10.5.0/3` (Central Europe).

## Building it yourself

`build.sh` compiles with arduino-cli and joins everything into one .bin. You need arduino-cli with the esp32 core **2.0.17**, and the libraries **TFT_eSPI 2.5.43** and **WiFiManager 2.0.17** (tzapu). Run `DRIFT_DIAG=1 ./build.sh` for the diagnostics version. `platformio.ini` is a PlatformIO alternative that hasn't been tested.

| File | What it holds |
|---|---|
| `DriftWatch/theme.h` | Design system: colours, type sizes, spacing, strokes, motion timing and easing |
| `DriftWatch/gfx.*` | Shared drawing parts: segment digits, battery, icons, arcs, text fitting and line breaking, the readable font, skyline |
| `DriftWatch/faces.*` | The five faces, each split into a cached static layer and a live layer |
| `DriftWatch/screens.*` | Paged settings, details and Wi-Fi setup screens, round-screen text flow, startup sequence. Each control has a drawn rectangle and a touch rectangle in one table used for drawing and hit-testing |
| `DriftWatch/ui.*` | Screens, gestures, transitions, sleep and wake, sync timing |
| `DriftWatch/net.*` | Wi-Fi setup hotspot and time sync, as step-by-step state machines that never block the screen. Sync stage and last result are tracked separately |
| `DriftWatch/hal_esp32.cpp`, `touch.*`, `settings.*`, `board.h` | Hardware: backlight, battery, BOOT button, touch chip, saved settings, pins |
| `tools/host/` | Computer stand-ins and tests. `render_faces` draws every face at test times and checks text stays inside the round screen; `simulate` drives the real interface and settings code with scripted touches and BOOT presses, checks every screen's layout and touch regions, and writes `report.md` (touch sizes and contrast); `Preferences.h` is an in-memory stand-in for saved settings; `sheet.py` makes preview sheets |

Host tests, from the project folder (needs g++ and the TFT_eSPI library source for its fonts):

```
g++ -std=c++17 -O1 -I tools/host -I DriftWatch -I path/to/TFT_eSPI tools/host/tft_shim.cpp tools/host/render_faces.cpp DriftWatch/faces.cpp DriftWatch/gfx.cpp -o render_faces
g++ -std=c++17 -O1 -I tools/host -I DriftWatch -I path/to/TFT_eSPI tools/host/tft_shim.cpp tools/host/sim_stubs.cpp tools/host/simulate.cpp DriftWatch/ui.cpp DriftWatch/screens.cpp DriftWatch/faces.cpp DriftWatch/gfx.cpp DriftWatch/settings.cpp -o simulate
mkdir -p out && ./render_faces out/ && ./simulate out/
```

## Ideas for later

Step counter and raise-to-wake (the motion sensor is wired up), lighter sleep between touches to save more battery, setting the time by hand without Wi-Fi.

## Ready-made firmware files

The `firmware/` folder holds the ready-to-flash v0.8.0 image, `drift_watch_v0.8.0.bin`, and the timing-log build, `drift_watch_v0.8.0_diag.bin`, plus the v0.7.0 files for going back. All are flashed at address 0x0.
