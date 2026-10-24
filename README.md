# Drift-Watch firmware (v0.10.0)

Our own watch software for the Waveshare ESP32-S3-Touch-LCD-1.28, written from scratch.

**Status:** v0.10.0 adds a turned screen with a two-tap touch check, a turning radar sweep, clock drift correction, phone sync over Bluetooth with the Drift sync web page, a flick check for raise to wake, a wrist twist to wake, wrist-down sleep, night mode and a measured battery time left. It builds with no warnings and passes 307 scripted checks in a computer simulation that runs the real drawing, touch, settings, phone-sync and gesture code. **v0.10.0 has not been run on a watch yet**. See `NOTES-v0.10.0.md` for the plan, what changed, the test matrix and the checklist for the watch (earlier `NOTES-*.md` files cover earlier versions).

## The board (checked against the Waveshare wiki)

| Part | What it is |
|---|---|
| Chip | ESP32-S3R2: two cores, 2 MB PSRAM, 16 MB flash |
| Screen | 1.28" round IPS LCD, 240 x 240, GC9A01A driver, 4-wire SPI up to 80 MHz. Lit by a backlight, so black pixels do **not** save power |
| Touch | CST816S on I2C (address 0x15), with interrupt and reset pins |
| Motion sensor | QMI8658 on the same I2C bus, used for raise to wake, wrist twist and wrist-down sleep (read by polling). Interrupt lines: INT1 on GPIO 4 (shared with the MOSFET 1 gate, so left undriven) and INT2 on GPIO 3 |
| Bluetooth | Bluetooth Low Energy, used only while Phone sync is open |
| USB-C | Goes through a CH343P USB-to-serial chip to UART0 (GPIO 43/44), not to the ESP32-S3's own USB pins. Serial runs at 115200 baud |
| Battery | Read through a 200k/100k divider on GPIO 1. The charger chip has no "charging" signal wired to the ESP32, so the watch cannot tell directly if it is charging (it guesses from a rising level) |
| Not on the board | Clock chip that keeps time when unpowered, buzzer, vibration motor |

## Faces

Swipe left or right to move between them. Previews: `designs/v0.7.0_faces.png` and `designs/v0.8.0_faces_native.png`.

- **Digital:** yellow segment time, magenta date strip, quiet seconds track with one cyan tick for the current second, battery at the top, a low dark skyline.
- **Stealth dial:** silver markers and hands with cyan inlays, a magenta seconds hand, a date window and a battery ring.
- **Corpo dial:** black dial, a thin segmented yellow rim, black hands edged in yellow, red seconds hand.
- **Hologram dial:** cyan markers and edged hands, white inlays, magenta seconds hand.
- **Radar:** large yellow time in the middle, an hour ring that fills through the day, a minute track with a pointer, range rings, and a beam that turns every 3.6 s with a fading phosphor trail. The seconds dot glows as the beam passes. Below 20% battery the beam sits on the seconds dot and moves once a second.

## Controls

| Where | Action | What happens |
|---|---|---|
| Face | Swipe left / right | Next / previous face. The face follows your finger; let go past a third of the way, or flick, to change |
| Face | Swipe up, hold for 0.6 s, or hold BOOT for 0.6 s | Open settings. A cyan ring grows while you hold |
| Face | Short press of BOOT | Next face |
| Set time | Tap - or +, or hold to repeat | Changes the field shown in cyan; swipe for the next field, then Save on the last page |
| Settings, details, setup | Swipe left / right | Next / previous page. Never changes the face |
| Settings, details, setup | Swipe down, or BOOT | Back one level (setup or details to settings, settings to face) |
| Settings, details, setup | Tap a control | Acts on release. Sliding off a control cancels it |
| Screen off | Any touch or BOOT | Wakes it. That touch does nothing else |
| Screen off | Raise your wrist to look at the watch | Wakes it (on by default; off during Night mode) |
| Screen off | Twist your wrist out and straight back, with the watch resting screen up (on a desk, the arm of a chair) | Wakes it (on by default; off during Night mode) |
| Screen on | Turn the screen away after looking at it | Turns it off at once (wrist-down sleep) |
| Anywhere | Hold BOOT for 4 s | Opens the touch check, even if touch is lined up wrong |

Settings has six pages of two controls each (twelve pages of one with large text):

1. **Brightness** (four steps) and **Time format** (24-hour or 12-hour).
2. **Set time** (shows the clock, or "Time not set") and **Phone sync** (sets the time, time zone and Wi-Fi from a phone or computer over Bluetooth). When the clock is not set, settings open on this page.
3. **Sync now**, which also shows the sync state, and **Wi-Fi setup** (shows the saved network).
4. **Wake gesture** (Raise, twist; Raise; Twist; or Off) and **Night mode** (Off, 22:00-07:00, 23:00-07:00 or 00:00-08:00; turns the wake gestures and wrist-down sleep off at night).
5. **Motion** (Full or Reduced; Reduced turns slides, the startup sequence, the wake fade, the radar beam and spinners into instant changes) and **Screen turn** (Normal, Right, Half turn or Left; runs the touch check afterwards).
6. **Touch check** (two taps line touch up with the screen) and **Status and Text size**, which opens the details screen: battery and time left, clock, last sync and how long ago, clock drift, time zone, network, motion sensor and its interrupt lines, screen turn, version, and the **Text size** setting (Normal or Large, about 140%, for settings and setup screens only).

**Set time** works with no Wi-Fi at all: hour, minutes, day, month and year each have a page with - and +, then a page shows the result with **Save**. Swipe down or BOOT leaves without saving. If the clock was never set it starts from the last time the watch saved (it saves at most once an hour), or else from the date the firmware was built. A later successful sync replaces it. The board has no clock chip, so the time is lost when the battery runs flat or the watch restarts from power-off. Between syncs the watch learns how fast its clock drifts and corrects for it.

The screen turns off after 15 seconds (30 seconds on a setup result). While it is off, the chip goes into light sleep and wakes on a touch, BOOT, or every 0.1 s to check the motion sensor (every 0.5 s with Wake gesture off). Light sleep is skipped while Wi-Fi or Bluetooth is on. The time resyncs every 6 hours while the screen is off (30 minutes after a failure), only if a network is saved.

## Flashing

1. Plug the watch into a computer with a USB-C **data** cable.
2. Open https://esp.huhn.me/ in Chrome or Edge, click Connect and pick the watch's port.
3. Choose `firmware/drift_watch_v0.10.0.bin` at address **0x0**, then Program.
4. Press reset or unplug and replug the watch.

If the browser can't find the watch, hold BOOT, tap RESET, release BOOT, then try again. To go back to the firmware the watch came with, flash your backup of it at 0x0 the same way.

`firmware/drift_watch_v0.10.0_diag.bin` is the same firmware with timing logs: it prints drawing time, screen transfer time and free memory to the serial port (115200 baud on the USB-C port) every 10 seconds, and shows a short version on the details screen. Both builds print start-up and Wi-Fi messages there, including the radio's reason number when joining fails.

**The v0.7.0 and v0.8.0 files in `firmware/` stall at start on this board**: they were built with "USB CDC on boot" turned on, which sends serial output to the ESP32-S3's own USB pins, and on this board those are not connected to the USB-C port. Use v0.9.0 or later, or rebuild an older version with the current `build.sh`.

## First start

After the startup sequence the **touch check** appears: tap the dot at the top, then the one on the right. That lines touch up with the screen, whichever way the panel turns. BOOT skips it. Then the watch shows the face. It never opens Wi-Fi setup by itself; with no clock set, the face says **SET TIME** and "Swipe up: set time".

To set the clock, use any one of: **Phone sync** (quickest), **Wi-Fi setup** (keeps itself right from then on), or **Set time** by hand.

## Phone sync

1. Open the Drift sync page (`companion/index.html`, served over https, for example with GitHub Pages) in Chrome or Edge on Android, Windows, macOS, Linux or a Chromebook, or in the free **Bluefy** app on an iPhone or iPad.
2. On the watch, open settings, then **Phone sync**. It shows its name, such as Drift-1A2B, for 3 minutes.
3. On the page, tap **Connect to the watch**, pick the name, then **Sync time**. The page sends your time zone (with its summer time rules) and the time. You can also send a Wi-Fi network and password.

Bluetooth is only on while that screen is open. The link is not encrypted, so only send a Wi-Fi password with the watch close to you. Generic Bluetooth apps that use the standard Current Time Service can set the clock too.

## Wi-Fi setup

Open settings, then **Wi-Fi setup**. The screen walks you through it:

1. On your phone, open the Wi-Fi list and join **Drift-Setup** first. http://192.168.4.1 only works while your phone is on Drift-Setup; on your home Wi-Fi it just times out.
2. A page should open by itself; if not, go to http://192.168.4.1. If the phone warns that Drift-Setup has no internet, choose to stay connected.
3. Pick your home network and type its password. **The watch only works with 2.4 GHz Wi-Fi**, so only 2.4 GHz networks are listed. If yours is missing, or the router mixes 2.4 and 5 GHz under one name and the watch can't join, turn on a 2.4 GHz network on the router (mesh routers often call it a compatibility or IoT network). You can also type a time zone, or leave it blank to detect it.
4. Save. The watch says "Connecting" and shows each step (joining Wi-Fi, finding the time zone, setting the clock), then "All set". If it can't join, it says why in plain words (network not found, password not accepted, the router turned the watch away, or no answer) and what to try, with **Edit Wi-Fi** (reopens the hotspot to fix the network or password) and **Retry**.

No Wi-Fi at all? Cancel setup and use **Set time** instead.

The hotspot closes after 5 minutes; the countdown is shown on the Cancel button. Cancel, swipe down or BOOT closes it sooner. While connecting, **Back** leaves it running in the background and **Cancel** stops it.

Time zone format, if you type one: `GMT0BST,M3.5.0/1,M10.5.0` (UK), `EST5EDT,M3.2.0,M11.1.0` (US Eastern), `CET-1CEST,M3.5.0,M10.5.0/3` (Central Europe).

## Building it yourself

`build.sh` compiles with arduino-cli and joins everything into one .bin. You need arduino-cli with the esp32 core **2.0.17**, and the libraries **TFT_eSPI 2.5.43** and **WiFiManager 2.0.17** (tzapu). Run `DRIFT_DIAG=1 ./build.sh` for the diagnostics version. `platformio.ini` is a PlatformIO alternative that hasn't been tested. In the Arduino IDE, choose ESP32S3 Dev Module with **USB CDC On Boot: Disabled**, Flash Size 16MB, Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)" and PSRAM "QSPI PSRAM", and add the screen settings from `build.sh` to TFT_eSPI's setup. To turn off light sleep, build with `-DDRIFT_LIGHT_SLEEP=0` (or edit `board.h`).

| File | What it holds |
|---|---|
| `DriftWatch/theme.h` | Design system: colours, type sizes, spacing, strokes, motion timing and easing |
| `DriftWatch/gfx.*` | Shared drawing parts: segment digits, battery, icons, arcs, text fitting and line breaking, the readable font, skyline |
| `DriftWatch/faces.*` | The five faces, each split into a cached static layer and a live layer |
| `DriftWatch/screens.*` | Paged settings, details and Wi-Fi setup screens, round-screen text flow, startup sequence. Each control has a drawn rectangle and a touch rectangle in one table used for drawing and hit-testing |
| `DriftWatch/ui.*` | Screens, gestures, transitions, sleep and wake, sync timing |
| `DriftWatch/net.*` | Wi-Fi setup hotspot and time sync, as step-by-step state machines that never block the screen. Sync stage and last result are tracked separately |
| `DriftWatch/hal_esp32.cpp`, `touch.*`, `settings.*`, `board.h` | Hardware: backlight, battery, BOOT button, light sleep, touch chip, saved settings, pins |
| `DriftWatch/imu*`, `raise.*` | QMI8658 motion sensor driver, and the raise-to-wake and wrist-down detectors (plain logic, tested in the simulation) |
| `DriftWatch/orient.*` | Screen turns, touch maps and the two-tap touch check solver |
| `DriftWatch/timekeep.*` | Clock drift learning and correction |
| `DriftWatch/battery.*` | Battery percentage and the measured time left |
| `DriftWatch/phone.*`, `ble.h`, `ble_esp32.cpp` | Phone sync: the messages a phone may send, and the Bluetooth service that carries them |
| `companion/index.html` | The Drift sync web page |
| `tools/host/` | Computer stand-ins and tests. `render_faces` draws every face at test times and checks text stays inside the round screen; `simulate` drives the real interface and settings code with scripted touches and BOOT presses, checks every screen's layout and touch regions, and writes `report.md` (touch sizes and contrast); `Preferences.h` is an in-memory stand-in for saved settings; `sheet.py` makes preview sheets |

Host tests, from the project folder (needs g++ and the TFT_eSPI library source for its fonts):

```
g++ -std=c++17 -O1 -I tools/host -I DriftWatch -I path/to/TFT_eSPI tools/host/tft_shim.cpp tools/host/render_faces.cpp DriftWatch/faces.cpp DriftWatch/gfx.cpp -o render_faces
g++ -std=c++17 -O1 -I tools/host -I DriftWatch -I path/to/TFT_eSPI tools/host/tft_shim.cpp tools/host/sim_stubs.cpp tools/host/simulate.cpp DriftWatch/ui.cpp DriftWatch/screens.cpp DriftWatch/faces.cpp DriftWatch/gfx.cpp DriftWatch/settings.cpp DriftWatch/raise.cpp DriftWatch/orient.cpp DriftWatch/phone.cpp DriftWatch/timekeep.cpp DriftWatch/battery.cpp -o simulate
mkdir -p out && ./render_faces out/ && ./simulate out/
```

## Ideas for later

Step counter (the motion sensor can do it), the motion sensor's own wake-on-motion interrupt (so the chip could sleep longer), deep sleep, and Bluetooth pairing so the Wi-Fi password is encrypted. See section 9 of `NOTES-v0.10.0.md`.

## Ready-made firmware files

The `firmware/` folder holds the ready-to-flash v0.10.0 image, `drift_watch_v0.10.0.bin`, and the timing-log build, `drift_watch_v0.10.0_diag.bin`, plus older versions (the v0.7.0 and v0.8.0 files stall at start on this board, see Flashing). All are flashed at address 0x0.
