# Drift-Watch firmware (v0.7.0)

Our own watch software for the Waveshare ESP32-S3-Touch-LCD-1.28, written from scratch.

**Status:** it builds with no warnings, the image checks out, and the interface passes 48 scripted checks in a computer simulation that runs the real drawing and touch code. It has **not** been run on a real watch yet. See `NOTES-v0.7.0.md` for what still needs the board.

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

Swipe left or right to move between them. Previews: `designs/v0.7.0_faces.png`.

- **Digital:** yellow segment time, magenta date strip, quiet seconds track with one cyan tick for the current second, battery at the top, a low dark skyline.
- **Stealth dial:** silver markers and hands with cyan inlays, a magenta seconds hand, a date window and a battery ring.
- **Corpo dial:** black dial, a thin segmented yellow rim, black hands edged in yellow, red seconds hand.
- **Hologram dial:** cyan markers and edged hands, white inlays, magenta seconds hand.
- **Radar:** large yellow time in the middle, an hour ring that fills through the day, a minute track with a pointer, a magenta seconds dot. The sweep runs once when you arrive on the face and once a minute, then stops.

## Controls

| Action | What happens |
|---|---|
| Swipe left / right | Next / previous face. The face follows your finger; let go past a third of the way, or flick, to change |
| Swipe up, or hold for 0.6 s | Open the status and settings panel. A cyan ring grows while you hold |
| Swipe down, or BOOT | Go back one level (setup to panel, panel to face) |
| BOOT on a face | Next face |
| Any touch or BOOT while the screen is off | Wakes it. That touch does nothing else |

The panel shows battery, sync state and time zone, and has four buttons: **LIGHT** (four brightness steps), **CLOCK** (24H/12H), **MOTION** (FULL/LESS; LESS turns slides, the startup sequence, the wake fade and the radar sweep into instant changes), **SYNC** (sync the time now). **WI-FI SETUP** opens the setup hotspot.

The screen turns off after 15 seconds. The time resyncs every 6 hours while the screen is off (30 minutes after a failure).

## Flashing

1. Plug the watch into a computer with a USB-C **data** cable.
2. Open https://esp.huhn.me/ in Chrome or Edge, click Connect and pick the watch's port.
3. Choose `firmware/drift_watch_v0.7.0.bin` at address **0x0**, then Program.
4. Press reset or unplug and replug the watch.

If the browser can't find the watch, hold BOOT, tap RESET, release BOOT, then try again. To go back to the firmware the watch came with, flash your backup of it at 0x0 the same way.

`firmware/drift_watch_v0.7.0_diag.bin` is the same firmware with timing logs: it prints drawing time, screen transfer time and free memory over USB serial every 10 seconds, and shows a short version on the panel.

## First start: Wi-Fi setup

If no Wi-Fi is saved, setup opens after the startup sequence. The screen walks you through it:

1. On your phone, join **Drift-Setup**.
2. A page should open by itself; if not, go to http://192.168.4.1.
3. Pick your home network and type its password. You can also type a time zone, or leave it blank to detect it.
4. Save. The watch shows each step (joining Wi-Fi, finding the time zone, setting the clock), then "All set", or what went wrong with a RETRY button.

The hotspot closes after 5 minutes. CANCEL, swipe down or BOOT closes it sooner. The 12/24-hour choice moved from the setup page to the panel.

Time zone format, if you type one: `GMT0BST,M3.5.0/1,M10.5.0` (UK), `EST5EDT,M3.2.0,M11.1.0` (US Eastern), `CET-1CEST,M3.5.0,M10.5.0/3` (Central Europe).

## Building it yourself

`build.sh` compiles with arduino-cli and joins everything into one .bin. You need arduino-cli with the esp32 core **2.0.17**, and the libraries **TFT_eSPI 2.5.43** and **WiFiManager 2.0.17** (tzapu). Run `DRIFT_DIAG=1 ./build.sh` for the diagnostics version. `platformio.ini` is a PlatformIO alternative that hasn't been tested.

| File | What it holds |
|---|---|
| `DriftWatch/theme.h` | Design system: colours, type sizes, spacing, strokes, motion timing and easing |
| `DriftWatch/gfx.*` | Shared drawing parts: segment digits, battery, icons, arcs, text fitting, skyline |
| `DriftWatch/faces.*` | The five faces, each split into a cached static layer and a live layer |
| `DriftWatch/screens.*` | Status panel, Wi-Fi setup screens, startup sequence. Buttons are hit-tested from the same table they are drawn from |
| `DriftWatch/ui.*` | Screens, gestures, transitions, sleep and wake, sync timing |
| `DriftWatch/net.*` | Wi-Fi setup hotspot and time sync, as step-by-step state machines that never block the screen |
| `DriftWatch/hal_esp32.cpp`, `touch.*`, `settings.*`, `board.h` | Hardware: backlight, battery, BOOT button, touch chip, saved settings, pins |
| `tools/host/` | Computer stand-ins and tests. `render_faces` draws every face at test times and checks text stays inside the round screen; `simulate` drives the real interface with scripted touches; `sheet.py` makes preview sheets |

Host tests, from the project folder (needs g++ and the TFT_eSPI library source for its fonts):

```
g++ -std=c++17 -O1 -I tools/host -I DriftWatch -I path/to/TFT_eSPI tools/host/tft_shim.cpp tools/host/render_faces.cpp DriftWatch/faces.cpp DriftWatch/gfx.cpp -o render_faces
g++ -std=c++17 -O1 -I tools/host -I DriftWatch -I path/to/TFT_eSPI tools/host/tft_shim.cpp tools/host/sim_stubs.cpp tools/host/simulate.cpp DriftWatch/ui.cpp DriftWatch/screens.cpp DriftWatch/faces.cpp DriftWatch/gfx.cpp -o simulate
./render_faces out/ && ./simulate out/
```

## Ideas for later

Step counter and raise-to-wake (the motion sensor is wired up), lighter sleep between touches to save more battery, setting the time by hand without Wi-Fi.

## Ready-made firmware files

The `firmware/` folder holds the ready-to-flash v0.7.0 image, `drift_watch_v0.7.0.bin`, and the timing-log build, `drift_watch_v0.7.0_diag.bin`. Both are flashed at address 0x0.
