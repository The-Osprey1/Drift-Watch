# Drift-Watch firmware (v0.15.2)

Our own watch software for the Waveshare ESP32-S3-Touch-LCD-1.28, written from scratch.

## Status

- **v0.15.2** *(latest)* — Apple Human Interface & Cupertino Web Studio revamp: adaptive Light/Dark translucent glass workspace with simplified 4-tab navigation (`Face Gallery`, `Sync & Wi-Fi`, `Quick Tools`, `Diagnostics`), buttonless circular 240×240 OLED touch enclosure with unclipped typography, direct touch gesture alignment (**Swipe down** opens **Quick Drawer**, **Swipe up** opens **Settings**), collapsible secondary disclosure drawers, expanded serial/BLE live control commands (`BRIGHT:`, `MOTION:`, `FORMAT:`, `NIGHT:`, `FACE:`, `TIMER:`), and upstream `PR #22` Web Bluetooth picker & error-formatting improvements. See [`notes/NOTES-v0.15.2.md`](notes/NOTES-v0.15.2.md) and [`notes/RELEASE-NOTES-v0.15.2.md`](notes/RELEASE-NOTES-v0.15.2.md).
- **v0.15.1** *(partly verified on the watch: USB-C sync with the screen off and Windows sync on plug-in pass)* — Daily-driver fixes from the v0.15.0 test: the timer alarm now shows on every face (it was hidden on Radar, Phantom, and Orbit, Lunar and Yozakura with Expressive motion), the "TIMER UP!" picture clears when dismissed in Settings, the flashlight returns to the face after 2 minutes, the open drawer redraws once a second with a progress ring that empties, Wake gesture and Touch check sit under their own headings, serial data wakes a sleeping watch, the Windows script and web page wait for the watch to answer before syncing (and say when it fails), Windows syncs on every plug-in, and the web page reads the face list over USB-C and notices an unplugged cable. See [`notes/NOTES-v0.15.1.md`](notes/NOTES-v0.15.1.md). Released as [v0.15.1](https://github.com/The-Osprey1/DriftWatch/releases/tag/v0.15.1).
- **v0.15.0** *(verified on hardware)* — Quick Drawer pull-down shade with interactive countdown timers (1m, 3m, 5m presets, dial progress ring, background light sleep scheduling, visual strobe alarm), full-screen flashlight, Settings 4-Hub Overhaul with 1-tap category header fast-jumping, Instant USB-C Auto-Sync protocol (`TIME:`, `WIFI:`, `FACES:`, `INFO?`), zero-install Windows auto-sync script (`tools/windows/DriftWatchSync.ps1`), Web Serial companion auto-sync, PhoneSync QR code direct scanning, and QMI8658 onboard temperature telemetry. See [`notes/NOTES-v0.15.0.md`](notes/NOTES-v0.15.0.md).
- **v0.14.0** *(verified on hardware)* — Precision face revamp (upright vector instrument numerals on Digital, Radar, Orbit and Yozakura, quieter supporting marks, distinct analog hand styles), hot-swap active faces (from **Settings > Hot swap** or the Drift sync web page), chronometer startup sequence, radial iris wake and sleep transitions, optional 20 fps ambient face touches on **Motion: Expressive**, shared face-mask constants (`face_id.h`), and `ui.cpp` split into focused units. See [`notes/NOTES-v0.14.0.md`](notes/NOTES-v0.14.0.md).
- **v0.13.0** — Watchdog freeze recovery with restart reason on the details screen, stuck-I2C bus recovery at start, persisted Auto time zone with built-in summer time rules, two-try Wi-Fi join, failed-resync back-off, scan-to-join QR code on Wi-Fi setup, 15% and 5% low-battery warnings, and a charging bolt on every face. See [`notes/NOTES-v0.13.0.md`](notes/NOTES-v0.13.0.md).
- **v0.12.0** *(pending hardware test)* — Six themed faces (Orbit, Lunar, Yozakura, Hanami, Wasteland, Mojave) with one-shot entrances, three-level **Motion** setting (Off, Subtle, Expressive), and distance-scaled slide timing. See [`notes/NOTES-v0.12.0.md`](notes/NOTES-v0.12.0.md).
- **v0.11.1** *(verified booting on a real watch)* — iPhone Bluetooth MTU and write fixes (247-byte ATT MTU, acknowledged GATT writes, 20-byte fallback framing), WPA3 SAE H2E and 802.11w PMF with straight/curly apostrophe fallback, and live sync page at https://the-osprey1.github.io/DriftWatch/; diagnostic wake-on-motion power saving is still to be measured. Earlier versions (`v0.7.0`–`v0.11.0`) are covered in [`notes/`](notes/).

## The board (checked against the Waveshare wiki)

| Part | What it is |
|---|---|
| Chip | ESP32-S3R2: two cores, 2 MB PSRAM, 16 MB flash |
| Screen | 1.28" round IPS LCD, 240 x 240, GC9A01A driver, 4-wire SPI up to 80 MHz. Lit by a backlight, so black pixels do **not** save power |
| Touch | CST816S on I2C (address 0x15), with interrupt and reset pins |
| Motion sensor | QMI8658 on the same I2C bus, used for raise to wake, wrist twist and wrist-down sleep (read by polling; the diagnostic image can also use its wake-on-motion interrupt on INT2). Interrupt lines: INT1 on GPIO 4 (shared with the MOSFET 1 gate, so left undriven) and INT2 on GPIO 3 |
| Bluetooth | Bluetooth Low Energy, used only while Phone sync is open |
| USB-C | Goes through a CH343P USB-to-serial chip to UART0 (GPIO 43/44), not to the ESP32-S3's own USB pins. Serial runs at 115200 baud |
| Battery | Read through a 200k/100k divider on GPIO 1. The charger chip has no "charging" signal wired to the ESP32, so the watch cannot tell directly if it is charging (it guesses from a rising level) |
| Not on the board | Clock chip that keeps time when unpowered, buzzer, vibration motor |

## Faces

Twelve faces; swipe left or right to move between them, or narrow the swipe loop to your favourites with **Hot swap** (in Settings or on the Drift sync web page). The precision revamp uses upright vector time on Digital, Radar, Orbit and Yozakura, quieter supporting detail, and distinct analog hand styles. Native previews and animation states are in [`designs/precision/`](designs/precision/), alongside the [design brief](designs/precision/brief.md) and [design review](designs/precision/revamp-review.md); earlier preview sheets and entrance storyboards are in [`designs/archive/`](designs/archive/).

- **Digital:** large silver instrument numerals, a quiet date register with yellow brackets, one cyan second tick, battery at the top and a small DRIFT signature.
- **Stealth dial:** fine silver indices and baton hands, a cyan seconds hand, date window and battery ring.
- **Corpo dial:** restrained segmented yellow rim and indices, silver baton hands and a red seconds hand.
- **Hologram dial:** cyan indices and skeleton hands, white inlays and a silver seconds hand.
- **Radar:** silver instrument time in the middle, an hour ring that fills through the day, a minute track with a pointer, range rings, and a peripheral beam that turns every 3.6 s with a fading phosphor trail, clear of the central time and date. The seconds dot glows as the beam passes. Below 20% battery the beam sits on the seconds dot and moves once a second.
- **Phantom:** a dark dial with an opening at 12 showing a ticking balance wheel and a crimson jewel. The seconds hand slides in under the edge of the opening from 55 s, is hidden from 58 to 2 s, slides out again by 5 s and settles with a small bounce. With Motion Off or low battery the wheel stops and the seconds hand steps once a second.

Every face shows a small lightning bolt next to its battery mark while the watch is charging (v0.13.0). Below 15% battery, and again below 5%, a notice shows over the face for 5 seconds the next time you look at the watch.

The six themed faces (v0.12.0) come in pairs. Each plays one short entrance when you choose it by swiping or with BOOT, never when the screen wakes, and any touch ends it at once. The time is right from the first frame; only decoration moves.

- **Orbit** (Space, digital): ivory instrument numerals over stars and a ringed planet. The orbit track draws round from 12 and a cyan marker eases into the current minute (500 ms); at each new minute the marker moves on 6 degrees.
- **Lunar** (Space, analog): silver hands on navy, a pale blue seconds hand, a battery ring and a moon that a curved wipe reveals (300 ms).
- **Yozakura** (Sakura, digital): ivory instrument numerals on a night disc between two blossoming branches; two petals fall beside the time and date, never across them (900 ms).
- **Hanami** (Sakura, analog): plum hands on a porcelain dial; a branch grows along the lower left and its blossoms open in two groups (600 ms).
- **Wasteland** (Fallout, digital): green phosphor segments with the unlit ones faintly showing; dividers, brackets, the power bar and the sync line build up around the time (420 ms).
- **Mojave** (Fallout, analog): cream hands on dark leather with amber markers; the battery arc fills and a highlight runs over the rim (600 ms).

On **Motion: Expressive** (above 20% battery), Digital pulses its colon with a subtle scan line, Orbit and Lunar twinkle their stars, and Yozakura shimmers its blossoms at a 20 fps budget while the screen is on. On **Subtle** (the default), only Radar and Phantom animate between seconds; all other faces draw at most once a second.

## Controls

| Where | Action | What happens |
|---|---|---|
| Face | Swipe left / right | Next / previous active face. The face follows your finger; let go past 70 px, or flick, to change. It lands in 120 to 220 ms depending on the distance left (a flick in 140 ms at most), and a shorter drag returns in 150 ms |
| Face | Swipe down | Open the Quick Drawer (Tools shade) with Countdown Timers (1m, 3m, 5m), live countdown dial, and Flashlight |
| Face | Swipe up, hold for 0.6 s, or hold BOOT for 0.6 s | Open Settings (styled with the current face's accent colours). The panel follows your finger up; a cyan ring grows while you hold |
| Face | Short press of BOOT | Next active face |
| Quick Drawer | Swipe up or BOOT | Close drawer back to the face |
| Quick Drawer | Tap 1m / 3m / 5m Timer | Starts countdown; the drawer shows MM:SS and a ring that empties, redrawn once a second. With the screen off the watch wakes for the alarm |
| Quick Drawer | Tap Flashlight | Full-screen white at full brightness; tap anywhere or BOOT to return to the face at your brightness. It returns by itself after 2 minutes |
| Timer Alarm | Tap anywhere or press BOOT | Dismisses the flashing alarm (shown on every face and screen) and restores your brightness. It stops by itself after 30 s |
| Settings | Tap top header | 1-tap fast-jump across category sections without horizontal swiping |
| Set time | Tap - or +, or hold to repeat | Changes the field shown in cyan; swipe for the next field, then Save on the last page |
| Settings, details, setup | Swipe left / right | Next / previous page. Never changes the face |
| Settings, details, setup | Swipe down, or BOOT | Back one level (setup or details to settings, settings to face) |
| Settings, details, setup | Tap a control | Acts on release. Sliding off a control cancels it |
| Screen off | Any touch or BOOT | Wakes it with a radial iris bloom (instant with Motion Off or below 20% battery). That touch does nothing else |
| Screen off | Raise your wrist to look at the watch | Wakes it (on by default; off during Night mode) |
| Screen off | Twist your wrist out and straight back, with the watch resting screen up (on a desk, the arm of a chair) | Wakes it (on by default; off during Night mode) |
| Screen on | Turn the screen away after looking at it | Collapses the radial iris and turns the screen off (wrist-down sleep); touching or pressing BOOT mid-collapse catches the screen open |
| Anywhere | Hold BOOT for 4 s | Opens the touch check, even if touch is lined up wrong |

Settings is organized into 4 Category Hubs with interactive header fast-jumping (tap the top title at `y < 42` to jump between hubs instantly):

- **Hub 0: Display & Style** (Pages 0–1): **Brightness** (four steps), **Night mode** (turns gestures/wrist-down sleep off at night), **Motion** (Subtle, Expressive, Off), and **Hot swap** (toggles between All 12 faces and a quick 2-face pair).
- **Hub 1: Clock & Sync** (Pages 2–4): **Time format** (24h or 12h), **Set time** (manual time/date setting), **Phone sync** (Bluetooth sync & QR code), **Sync now** (NTP sync state & trigger), and **Wi-Fi setup** (shows saved SSID, scan QR code).
- **Hub 2: Gestures & Wake** (Page 5): **Wake gesture** (Raise, twist; Raise; Twist; or Off) and **Screen turn** (Normal, Right, Half turn, Left).
- **Hub 3: System & Info** (Page 6): **Touch check** (two-tap screen realignment) and **Status & Text size** (Details screen: battery, estimated time left, clock drift, QMI8658 onboard temperature, restart reason, and normal/large text toggle).

**Set time** works with no Wi-Fi at all: hour, minutes, day, month and year each have a page with - and +, then a page shows the result with **Save**. Swipe down or BOOT leaves without saving. If the clock was never set it starts from the last time the watch saved (it saves at most once an hour), or else from the date the firmware was built. A later successful sync replaces it. The board has no clock chip, so the time is lost when the battery runs flat or the watch restarts from power-off. Between syncs the watch learns how fast its clock drifts and corrects for it.

The screen turns off after 15 seconds (30 seconds on a setup result). While it is off, the chip goes into light sleep and wakes on a touch, BOOT, or every 0.1 s to check the motion sensor (with Wake gesture off it sleeps until the next thing that needs doing, at most 30 s). Light sleep is skipped while Wi-Fi or Bluetooth is on. The diagnostic image, with Wake gesture set to Raise, instead lets the motion sensor watch by itself and sleeps until it reports movement; the screen comes on only if the movement turns out to be a raise. Twist modes keep checking every 0.1 s. The serial port does not wake the watch, so press BOOT before sending a serial command while the screen is off. The time resyncs every 6 hours while the screen is off, only if a network is saved. After a failure it tries again in 30 minutes, then 1, 2 and 4 hours, then every 6 hours, so it does not keep switching Wi-Fi on while you are away from that network. If the time zone is Auto, the zone it last found is kept and used from the next start, with its own summer time rules for most zones that change their clocks.

## Flashing

1. Plug the watch into a computer with a USB-C **data** cable.
2. Open https://esp.huhn.me/ in Chrome or Edge, click Connect and pick the watch's port.
3. Choose [`firmware/drift_watch_v0.15.1.bin`](firmware/drift_watch_v0.15.1.bin) at address **0x0**, then Program.
4. Press reset or unplug and replug the watch.

If the browser can't find the watch, hold BOOT, tap RESET, release BOOT, then try again. To go back to the firmware the watch came with, flash your backup of it at 0x0 the same way.

[`firmware/drift_watch_v0.15.1_diag.bin`](firmware/drift_watch_v0.15.1_diag.bin) is the same firmware with timing and power logs, and with wake on motion and touch standby turned on: it prints drawing time, screen transfer time and free memory to the serial port (115200 baud on the USB-C port) every 10 seconds, and shows a short version on the details screen. Both builds print start-up and Wi-Fi messages there, including the radio's reason number when joining fails.

Working earlier versions (`v0.9.0` through `v0.15.0`) are published on [GitHub Releases](https://github.com/The-Osprey1/DriftWatch/releases). (`v0.7.0` and `v0.8.0` are omitted because they were built with "USB CDC on boot" turned on, which sends serial output to disconnected USB pins and stalls at start on this board.)

## First start

Cold power-on opens with a short **chronometer startup sequence** (12-point bezel shutter, counter-rotating telemetry rings and laser-scanned wordmark; any touch or BOOT press skips straight ahead, and Motion Off skips it altogether). On a fresh watch or a newly turned screen, the **touch check** then appears: tap the dot at the top, then the one on the right. That lines touch up with the screen, whichever way the panel turns. BOOT skips it. Then the watch shows the face. It never opens Wi-Fi setup by itself; with no clock set, the face says **SET TIME** and "Swipe up: set time".

There are four ways to set or sync the clock:

1. **Wi-Fi NTP (Automatic)**: Once connected to a 2.4 GHz network via **Wi-Fi setup** or Phone sync, the watch keeps time autonomously via NTP (`pool.ntp.org`) in the background—no phones, browsers, or Bluetooth required.
2. **Phone sync (Web companion)**: Fastest way to send time, time zone rules, active watch faces and Wi-Fi credentials in one tap from a browser over Bluetooth.
3. **USB Serial**: When plugged into a computer via USB-C, send a one-line serial command to set the clock or Wi-Fi instantly.
4. **Set time by hand**: Swipe up into settings and pick **Set time** to adjust the date and time directly on screen.

## Phone sync

1. Open https://the-osprey1.github.io/DriftWatch/ ([`companion/index.html`](companion/index.html), served over https by GitHub Pages) in Chrome or Edge on Android, Windows, macOS, Linux or a Chromebook, or in the free **Bluefy** app on an iPhone or iPad.
2. On the watch, open settings, then **Phone sync**. It shows its name, such as Drift-1A2B, for 3 minutes.
3. On the page, tap **Connect to the watch**, pick the name, then **Sync time**. The page sends your time zone (with its summer time rules) and the time. You can also toggle **Active watch faces (Hot swap)** to pick any combination of the twelve faces (`H=<mask>`), or send a Wi-Fi network and password (for an iPhone Personal Hotspot, turn on **Maximize Compatibility** in Settings > Personal Hotspot and keep that screen open while the watch joins).

> **Windows Bluetooth note:** Connect directly from Chrome or Edge. Do **not** pair the watch in the Windows OS Bluetooth Settings (`Bluetooth & other devices`). Because Drift-Watch is a BLE GATT peripheral without standard PC profiles (audio, mouse, keyboard), Windows will repeatedly connect and drop the link every second, causing `GATT Server is disconnected` errors in the browser. If you already paired it in Windows Settings, click **Remove device** first.

Bluetooth is only on while that screen is open. The link is not encrypted, so only send a Wi-Fi password with the watch close to you. Generic Bluetooth apps that use the standard Current Time Service (CTS, `0x1805`) can set the clock too.

## USB Serial sync

When plugged into a computer over USB-C, the watch accepts commands on the serial port at **115200 baud**, one per line:

- **Set time**: `TIME:<unix_seconds>` (e.g. `TIME:1729828800`); replies `Time set to <seconds>`
- **Set Wi-Fi**: `WIFI:<network>,<password>`
- **Active faces**: `FACES:<hex mask>` (e.g. `FACES:0x0041` for Digital and Orbit); replies `Faces mask set to 0x....`
- **Status**: `INFO?` or `?`; replies `DRIFT:VER=...,TIME=...,BAT=...,FACES=0x....`

Two things to know before sending anything:

- **Opening the port can restart the watch.** The USB chip's control lines are wired to the reset pin, and on Windows opening the port pulses them. The watch then needs about 0.75 s before it reads serial again.
- **With the screen off, the first bytes are lost.** They wake the watch (v0.15.1 and later), which then listens for 3 seconds without sleeping.

So send a newline and `INFO?` until the watch answers `DRIFT:VER=...`, then send `TIME:`. Both tools here do that:

- **Windows**: [`tools/windows/DriftWatchSync.ps1`](tools/windows/DriftWatchSync.ps1) finds the watch's port, syncs once, and only prints "Success!" when the watch confirms the time. To sync every time you plug the watch in, run [`tools/windows/RegisterAutoSyncTask.ps1`](tools/windows/RegisterAutoSyncTask.ps1) once in a normal PowerShell window: it starts a hidden watcher at each sign-in (and right away) that syncs on every plug-in.
- **Browser**: the Drift sync page's **USB-C Cable** button (Chrome or Edge), with **Auto-sync** to sync on plug-in.

**Linux, by hand** (the port name varies):
```bash
stty -F /dev/ttyACM0 115200 -hupcl; sleep 1; printf '\nINFO?\n' > /dev/ttyACM0; sleep 0.3; echo "TIME:$(date +%s)" > /dev/ttyACM0
```

## Wi-Fi setup

Open settings, then **Wi-Fi setup**. The screen walks you through it:

1. The first page is a code: point your phone's camera at it and tap the suggestion to join **Drift-Setup**. Or swipe for the steps, and in your phone's Wi-Fi list join **Drift-Setup** first. http://192.168.4.1 only works while your phone is on Drift-Setup; on your home Wi-Fi it just times out.
2. A page should open by itself; if not, scan the code again (once a phone has joined, it opens the page) or go to http://192.168.4.1. If the phone warns that Drift-Setup has no internet, choose to stay connected.
3. Pick your home network and type its password. **The watch only works with 2.4 GHz Wi-Fi**, so only 2.4 GHz networks are listed. If yours is missing, or the router mixes 2.4 and 5 GHz under one name and the watch can't join, turn on a 2.4 GHz network on the router (mesh routers often call it a compatibility or IoT network; on an iPhone Personal Hotspot, turn on **Maximize Compatibility** in Settings > Personal Hotspot). You can also type a time zone, or leave it blank to detect it.
4. Save. The watch says "Connecting" and shows each step (if the router does not answer within 15 seconds it says "Trying again" and tries once more) (joining Wi-Fi, finding the time zone, setting the clock), then "All set". If it can't join, it says why in plain words (network not found, password not accepted, the router turned the watch away, or no answer) and what to try, with **Edit Wi-Fi** (reopens the hotspot to fix the network or password) and **Retry**.

No Wi-Fi at all? Cancel setup and use **Set time** instead.

The hotspot closes after 5 minutes; the countdown is shown on the Cancel button. Cancel, swipe down or BOOT closes it sooner. While connecting, **Back** leaves it running in the background and **Cancel** stops it.

Time zone format, if you type one: `GMT0BST,M3.5.0/1,M10.5.0` (UK), `EST5EDT,M3.2.0,M11.1.0` (US Eastern), `CET-1CEST,M3.5.0,M10.5.0/3` (Central Europe).

## Building it yourself

[`build.sh`](build.sh) compiles with arduino-cli and joins everything into one `.bin`. You need arduino-cli with the esp32 core **2.0.17**, and the libraries **TFT_eSPI 2.5.43** and **WiFiManager 2.0.17** (tzapu). Run `DRIFT_DIAG=1 ./build.sh` for the diagnostics version. In the Arduino IDE, choose ESP32S3 Dev Module with **USB CDC On Boot: Disabled**, Flash Size 16MB, Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)" and PSRAM "QSPI PSRAM", and add the screen settings from `build.sh` to TFT_eSPI's setup. To turn off light sleep, build with `-DDRIFT_LIGHT_SLEEP=0` (or edit `board.h`). `-DDRIFT_WOM=1` and `-DDRIFT_TOUCH_STANDBY=1` turn on wake on motion and touch standby in a normal build (both are on in the diagnostic build). `DRIFT_WARNINGS=all ./build.sh` shows every compiler warning.

| File | What it holds |
|---|---|
| `DriftWatch/theme.h` | Design system: colours, type sizes, spacing, strokes, motion timing and easing |
| `DriftWatch/gfx.*` | Shared drawing parts: segment and vector digits, battery, icons, arcs, text fitting and line breaking, the readable font, skyline |
| `DriftWatch/face_id.h` | Shared face identifiers (`Face`), `FACE_COUNT` and `ALL_FACES_MASK` |
| `DriftWatch/faces.*` | The twelve faces, each split into a cached static layer and a live layer; the six themed faces and their entrances are in `faces_themed.cpp`, with shared pieces in `face_parts.h` |
| `DriftWatch/screens.*` | Paged settings, details and Wi-Fi setup screens, round-screen text flow, chronometer startup sequence. Each control has a drawn rectangle and a touch rectangle in one table used for drawing and hit-testing |
| `DriftWatch/ui.h`, `ui.cpp`, `ui_render.cpp`, `ui_motion.cpp`, `ui_input.cpp`, `ui_parts.h` | Interface state, battery/low-power policy and main loop (`ui.cpp`), sprite presentation and boot/face/diagnostic steps (`ui_render.cpp`), entrances, slides, radial iris transitions and standby (`ui_motion.cpp`), and navigation, touch, wrist gestures, BOOT, BLE and serial commands (`ui_input.cpp`) |
| `DriftWatch/net.*` | Wi-Fi setup hotspot and time sync, as step-by-step state machines that never block the screen. Sync stage and last result are tracked separately |
| `DriftWatch/hal_esp32.cpp`, `touch.*`, `settings.*`, `board.h` | Hardware: backlight, battery, BOOT button, light sleep, touch chip, saved settings, pins |
| `DriftWatch/imu*`, `qmi8658.*`, `raise.*` | QMI8658 motion sensor driver (its register sequences, including wake on motion, are plain logic in `qmi8658.*`), and the raise-to-wake and wrist-down detectors (plain logic, tested in the simulation) |
| `DriftWatch/power.*` | What the watch does while the screen is off: polling, wake on motion, checking a motion is a raise, fallbacks and statistics |
| `DriftWatch/orient.*` | Screen turns, touch maps and the two-tap touch check solver |
| `DriftWatch/timekeep.*` | Clock drift learning and correction |
| `DriftWatch/battery.*` | Battery percentage, the measured time left, charging, and the low battery warning levels |
| `DriftWatch/zones.h` | Summer time rules for the zones the Auto lookup can name, checked against the time zone database by `net_test` |
| `DriftWatch/qrcodegen.*` | Project Nayuki's QR code library (MIT licence, unchanged), for the scan code on Wi-Fi setup |
| `DriftWatch/phone.*`, `ble.h`, `ble_esp32.cpp` | Phone sync: the messages a phone may send (`Z=`, `T=`, `N=`, `S=`, `P=`, `H=`), and the Bluetooth service that carries them |
| `companion/index.html` | The Drift sync web page |
| `tools/host/` | Computer stand-ins and tests. `render_faces` draws every face at test times and checks text stays inside the round screen; `preview_faces` and `preview_sheet.py` render the native gallery and animation sheets; `simulate` drives the real interface and settings code with scripted touches and BOOT presses, checks every screen's layout and touch regions, and writes `report.md` (touch sizes and contrast); `simulate_network` runs the real interface and the real `net.cpp` together; `net_test` runs the real `net.cpp` (sync, time zone lookup, time server, setup hotspot, drift, restarts in a fresh process) against the Wi-Fi, web and time-server stand-ins in `tools/host/net/`; `power_test` checks the motion sensor's register sequences and the screen-off controller; `mutation_check.sh` puts known bugs back and checks the tests catch them; `Preferences.h` is an in-memory stand-in for saved settings; `sheet.py` makes preview sheets |

Host tests, from the project folder (needs g++ and the TFT_eSPI 2.5.43 source for its fonts):

```
git clone --depth 1 --branch V2.5.43 https://github.com/Bodmer/TFT_eSPI.git ../TFT_eSPI
tools/host/run_tests.sh ../TFT_eSPI
```

This builds and runs all five: `render_faces`, `simulate` (frames and `report.md` go to `tools/host/out`), `simulate_network`, `net_test` and `power_test`. `HOST_STRICT=1` adds warnings as errors and the address and undefined-behaviour sanitizers. `tools/host/mutation_check.sh ../TFT_eSPI` runs the mutation checks.

GitHub Actions ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) runs the strict tests and the mutation checks, renders the native face previews, and builds and checks both firmware images with a report of compiler warnings, on every push and pull request; the images are kept as a download on each run. On a push to main, once those pass, it publishes the Drift sync page (`companion/` only) to GitHub Pages. That needs one setting, once: **Settings, Pages, Build and deployment, Source: GitHub Actions**.

## Ideas for later

Step counter (the motion sensor can do it), wake on motion for the twist gesture (it needs a way to know the wrist was still just before), deep sleep, and Bluetooth pairing so the Wi-Fi password is encrypted. See section 9 of [`notes/NOTES-v0.10.0.md`](notes/NOTES-v0.10.0.md).

## Ready-made firmware files

The [`firmware/`](firmware/) folder holds the current ready-to-flash v0.15.1 image, `drift_watch_v0.15.1.bin`, and the diagnostic build, `drift_watch_v0.15.1_diag.bin` (timing and power logs, wake on motion and touch standby), with their checksums in [`SHA256SUMS-v0.15.1.txt`](firmware/SHA256SUMS-v0.15.1.txt) and build details in [`MANIFEST-v0.15.1.md`](firmware/MANIFEST-v0.15.1.md). Working earlier releases (`v0.9.0`–`v0.15.0`) are available under [GitHub Releases](https://github.com/The-Osprey1/DriftWatch/releases). All images are flashed at address `0x0`.

## Licence

MIT — see [`LICENSE`](LICENSE). `DriftWatch/qrcodegen.*` is Project Nayuki's QR Code generator library, included under its own MIT licence notice.
