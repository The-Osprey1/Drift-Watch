# Drift-Watch firmware (v0.11.1)

Our own watch software for the Waveshare ESP32-S3-Touch-LCD-1.28, written from scratch.

**Status:** v0.10.0 adds a turned screen with a two-tap touch check, a turning radar sweep, clock drift correction, phone sync over Bluetooth with the Drift sync web page, a flick check for raise to wake, a wrist twist to wake, wrist-down sleep, night mode and a measured battery time left. v0.10.1 fixes four bugs in it (touch read dropouts, non-blocking serial commands, keeping Auto time zone across first start, and an out-of-memory screen notice); see `NOTES-v0.10.1.md`. v0.11.0 adds the Phantom face, a candidate low-power wait for the screen-off time (the motion sensor's own wake-on-motion interrupt and the touch chip's standby, in the diagnostic image only), stricter network code, and GitHub Pages publishing for the Drift sync page; see `NOTES-v0.11.0.md`. **v0.11.1** fixes iPhone Bluetooth truncation and write failures (247-byte ATT MTU, acknowledged GATT writes, and 20-byte fallback framing), enables WPA3 SAE H2E and 802.11w PMF with straight/curly apostrophe fallback for modern routers and iPhone Personal Hotspots, publishes the live sync page at https://the-osprey1.github.io/DriftWatch/, and verifies boot on a real watch; power saving in the diagnostic image is still to be measured. See `NOTES-v0.11.1.md`. **v0.12.0** adds six themed faces (Orbit, Lunar, Yozakura, Hanami, Wasteland, Mojave), each with a short entrance when you choose it, a three-level Motion setting (Off, Subtle, Expressive), and new slide timing; it has not run on the watch yet. See `NOTES-v0.12.0.md` (earlier `NOTES-*.md` files cover earlier versions).

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

Twelve faces; swipe left or right to move between them. Previews: `designs/v0.7.0_faces.png`, `designs/v0.8.0_faces_native.png`, for Phantom `designs/v0.11.0_phantom_timeline.png` and `designs/v0.11.0_phantom_states.png`, and for the six themed faces `designs/v0.12.0_faces.png` and `designs/v0.12.0_storyboards.png` (each entrance from 0 ms to its final state).

- **Digital:** yellow segment time, magenta date strip, quiet seconds track with one cyan tick for the current second, battery at the top, a low dark skyline.
- **Stealth dial:** silver markers and hands with cyan inlays, a magenta seconds hand, a date window and a battery ring.
- **Corpo dial:** black dial, a thin segmented yellow rim, black hands edged in yellow, red seconds hand.
- **Hologram dial:** cyan markers and edged hands, white inlays, magenta seconds hand.
- **Radar:** large yellow time in the middle, an hour ring that fills through the day, a minute track with a pointer, range rings, and a beam that turns every 3.6 s with a fading phosphor trail. The seconds dot glows as the beam passes. Below 20% battery the beam sits on the seconds dot and moves once a second.
- **Phantom:** a dark dial with an opening at 12 showing a ticking balance wheel and a crimson jewel. The seconds hand slides in under the edge of the opening from 55 s, is hidden from 58 to 2 s, slides out again by 5 s and settles with a small bounce. With Motion Off or low battery the wheel stops and the seconds hand steps once a second.

The six themed faces (v0.12.0) come in pairs. Each plays one short entrance when you choose it by swiping or with BOOT, never when the screen wakes, and any touch ends it at once. The time is right from the first frame; only decoration moves.

- **Orbit** (Space, digital): ivory segment time over stars and a ringed planet. The orbit track draws round from 12 and a cyan marker eases into the current minute (500 ms); at each new minute the marker moves on 6 degrees.
- **Lunar** (Space, analog): silver hands on navy, a pale blue seconds hand, a battery ring and a moon that a curved wipe reveals (300 ms).
- **Yozakura** (Sakura, digital): ivory time on a night disc between two blossoming branches; two petals fall beside the time and date, never across them (900 ms).
- **Hanami** (Sakura, analog): plum hands on a porcelain dial; a branch grows along the lower left and its blossoms open in two groups (600 ms).
- **Wasteland** (Fallout, digital): green phosphor segments with the unlit ones faintly showing; dividers, brackets, the power bar and the sync line build up around the time (420 ms).
- **Mojave** (Fallout, analog): cream hands on dark leather with amber markers; the battery arc fills and a highlight runs over the rim (600 ms).

## Controls

| Where | Action | What happens |
|---|---|---|
| Face | Swipe left / right | Next / previous face. The face follows your finger; let go past 70 px, or flick, to change. It lands in 120 to 220 ms depending on the distance left (a flick in 140 ms at most), and a shorter drag returns in 150 ms |
| Face | Swipe up, hold for 0.6 s, or hold BOOT for 0.6 s | Open settings. The panel follows your finger up; a cyan ring grows while you hold |
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
5. **Motion** (Subtle, Expressive or Off. Subtle is the default: slides, the wake fade, the radar beam, Phantom's movement and one short entrance on the themed faces. Expressive plays those entrances half as long again. Off turns slides, entrances, the startup sequence, the wake fade, the radar beam and spinners into instant changes. A watch updated from v0.11 keeps its choice: Reduced becomes Off, Full becomes Subtle) and **Screen turn** (Normal, Right, Half turn or Left; runs the touch check afterwards).
6. **Touch check** (two taps line touch up with the screen) and **Status and Text size**, which opens the details screen: battery and time left, clock, last sync and how long ago, clock drift, time zone, network, motion sensor and its interrupt lines, screen turn, version, and the **Text size** setting (Normal or Large, about 140%, for settings and setup screens only).

**Set time** works with no Wi-Fi at all: hour, minutes, day, month and year each have a page with - and +, then a page shows the result with **Save**. Swipe down or BOOT leaves without saving. If the clock was never set it starts from the last time the watch saved (it saves at most once an hour), or else from the date the firmware was built. A later successful sync replaces it. The board has no clock chip, so the time is lost when the battery runs flat or the watch restarts from power-off. Between syncs the watch learns how fast its clock drifts and corrects for it.

The screen turns off after 15 seconds (30 seconds on a setup result). While it is off, the chip goes into light sleep and wakes on a touch, BOOT, or every 0.1 s to check the motion sensor (with Wake gesture off it sleeps until the next thing that needs doing, at most 30 s). Light sleep is skipped while Wi-Fi or Bluetooth is on. The diagnostic image, with Wake gesture set to Raise, instead lets the motion sensor watch by itself and sleeps until it reports movement; the screen comes on only if the movement turns out to be a raise. Twist modes keep checking every 0.1 s. The serial port does not wake the watch, so press BOOT before sending a serial command while the screen is off. The time resyncs every 6 hours while the screen is off (30 minutes after a failure), only if a network is saved.

## Flashing

1. Plug the watch into a computer with a USB-C **data** cable.
2. Open https://esp.huhn.me/ in Chrome or Edge, click Connect and pick the watch's port.
3. Choose `firmware/drift_watch_v0.11.1.bin` at address **0x0**, then Program.
4. Press reset or unplug and replug the watch.

If the browser can't find the watch, hold BOOT, tap RESET, release BOOT, then try again. To go back to the firmware the watch came with, flash your backup of it at 0x0 the same way.

`firmware/drift_watch_v0.11.1_diag.bin` is the same firmware with timing and power logs, and with wake on motion and touch standby turned on: it prints drawing time, screen transfer time and free memory to the serial port (115200 baud on the USB-C port) every 10 seconds, and shows a short version on the details screen. Both builds print start-up and Wi-Fi messages there, including the radio's reason number when joining fails.

**The v0.7.0 and v0.8.0 files in `firmware/` stall at start on this board**: they were built with "USB CDC on boot" turned on, which sends serial output to the ESP32-S3's own USB pins, and on this board those are not connected to the USB-C port. Use v0.9.0 or later, or rebuild an older version with the current `build.sh`.

## First start

After the startup sequence the **touch check** appears: tap the dot at the top, then the one on the right. That lines touch up with the screen, whichever way the panel turns. BOOT skips it. Then the watch shows the face. It never opens Wi-Fi setup by itself; with no clock set, the face says **SET TIME** and "Swipe up: set time".

There are four ways to set or sync the clock:

1. **Wi-Fi NTP (Automatic)**: Once connected to a 2.4 GHz network via **Wi-Fi setup** or Phone sync, the watch keeps time autonomously via NTP (`pool.ntp.org`) in the background—no phones, browsers, or Bluetooth required.
2. **Phone sync (Web companion)**: Fastest way to send time, time zone rules, and Wi-Fi credentials in one tap from a browser over Bluetooth.
3. **USB Serial**: When plugged into a computer via USB-C, send a one-line serial command to set the clock or Wi-Fi instantly.
4. **Set time by hand**: Swipe up into settings and pick **Set time** to adjust the date and time directly on screen.

## Phone sync

1. Open https://the-osprey1.github.io/DriftWatch/ (`companion/index.html`, served over https by GitHub Pages) in Chrome or Edge on Android, Windows, macOS, Linux or a Chromebook, or in the free **Bluefy** app on an iPhone or iPad.
2. On the watch, open settings, then **Phone sync**. It shows its name, such as Drift-1A2B, for 3 minutes.
3. On the page, tap **Connect to the watch**, pick the name, then **Sync time**. The page sends your time zone (with its summer time rules) and the time. You can also send a Wi-Fi network and password (for an iPhone Personal Hotspot, turn on **Maximize Compatibility** in Settings > Personal Hotspot and keep that screen open while the watch joins).

> **Windows Bluetooth note:** Connect directly from Chrome or Edge. Do **not** pair the watch in the Windows OS Bluetooth Settings (`Bluetooth & other devices`). Because Drift-Watch is a BLE GATT peripheral without standard PC profiles (audio, mouse, keyboard), Windows will repeatedly connect and drop the link every second, causing `GATT Server is disconnected` errors in the browser. If you already paired it in Windows Settings, click **Remove device** first.

Bluetooth is only on while that screen is open. The link is not encrypted, so only send a Wi-Fi password with the watch close to you. Generic Bluetooth apps that use the standard Current Time Service (CTS, `0x1805`) can set the clock too.

## USB Serial sync

When plugged into a computer over USB-C, the watch accepts commands on the serial port at **115200 baud**:

- **Set time**: `TIME:<unix_seconds>` (e.g. `TIME:1729828800`)
- **Set Wi-Fi**: `WIFI:<network>,<password>`

**PowerShell 1-liner:**
```powershell
$p = New-Object System.IO.Ports.SerialPort "COM6", 115200; $p.Open(); $p.WriteLine("TIME:" + [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()); $p.Close()
```

**Bash / Linux 1-liner:**
```bash
echo "TIME:$(date +%s)" > /dev/ttyACM0
```

## Wi-Fi setup

Open settings, then **Wi-Fi setup**. The screen walks you through it:

1. On your phone, open the Wi-Fi list and join **Drift-Setup** first. http://192.168.4.1 only works while your phone is on Drift-Setup; on your home Wi-Fi it just times out.
2. A page should open by itself; if not, go to http://192.168.4.1. If the phone warns that Drift-Setup has no internet, choose to stay connected.
3. Pick your home network and type its password. **The watch only works with 2.4 GHz Wi-Fi**, so only 2.4 GHz networks are listed. If yours is missing, or the router mixes 2.4 and 5 GHz under one name and the watch can't join, turn on a 2.4 GHz network on the router (mesh routers often call it a compatibility or IoT network; on an iPhone Personal Hotspot, turn on **Maximize Compatibility** in Settings > Personal Hotspot). You can also type a time zone, or leave it blank to detect it.
4. Save. The watch says "Connecting" and shows each step (joining Wi-Fi, finding the time zone, setting the clock), then "All set". If it can't join, it says why in plain words (network not found, password not accepted, the router turned the watch away, or no answer) and what to try, with **Edit Wi-Fi** (reopens the hotspot to fix the network or password) and **Retry**.

No Wi-Fi at all? Cancel setup and use **Set time** instead.

The hotspot closes after 5 minutes; the countdown is shown on the Cancel button. Cancel, swipe down or BOOT closes it sooner. While connecting, **Back** leaves it running in the background and **Cancel** stops it.

Time zone format, if you type one: `GMT0BST,M3.5.0/1,M10.5.0` (UK), `EST5EDT,M3.2.0,M11.1.0` (US Eastern), `CET-1CEST,M3.5.0,M10.5.0/3` (Central Europe).

## Building it yourself

`build.sh` compiles with arduino-cli and joins everything into one .bin. You need arduino-cli with the esp32 core **2.0.17**, and the libraries **TFT_eSPI 2.5.43** and **WiFiManager 2.0.17** (tzapu). Run `DRIFT_DIAG=1 ./build.sh` for the diagnostics version. `platformio.ini` is a PlatformIO alternative that hasn't been tested. In the Arduino IDE, choose ESP32S3 Dev Module with **USB CDC On Boot: Disabled**, Flash Size 16MB, Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)" and PSRAM "QSPI PSRAM", and add the screen settings from `build.sh` to TFT_eSPI's setup. To turn off light sleep, build with `-DDRIFT_LIGHT_SLEEP=0` (or edit `board.h`). `-DDRIFT_WOM=1` and `-DDRIFT_TOUCH_STANDBY=1` turn on wake on motion and touch standby in a normal build (both are on in the diagnostic build). `DRIFT_WARNINGS=all ./build.sh` shows every compiler warning.

| File | What it holds |
|---|---|
| `DriftWatch/theme.h` | Design system: colours, type sizes, spacing, strokes, motion timing and easing |
| `DriftWatch/gfx.*` | Shared drawing parts: segment digits, battery, icons, arcs, text fitting and line breaking, the readable font, skyline |
| `DriftWatch/faces.*` | The faces, each split into a cached static layer and a live layer; the six themed faces and their entrances are in `faces_themed.cpp`, with shared pieces in `face_parts.h` |
| `DriftWatch/screens.*` | Paged settings, details and Wi-Fi setup screens, round-screen text flow, startup sequence. Each control has a drawn rectangle and a touch rectangle in one table used for drawing and hit-testing |
| `DriftWatch/ui.*` | Screens, gestures, transitions, sleep and wake, sync timing |
| `DriftWatch/net.*` | Wi-Fi setup hotspot and time sync, as step-by-step state machines that never block the screen. Sync stage and last result are tracked separately |
| `DriftWatch/hal_esp32.cpp`, `touch.*`, `settings.*`, `board.h` | Hardware: backlight, battery, BOOT button, light sleep, touch chip, saved settings, pins |
| `DriftWatch/imu*`, `qmi8658.*`, `raise.*` | QMI8658 motion sensor driver (its register sequences, including wake on motion, are plain logic in `qmi8658.*`), and the raise-to-wake and wrist-down detectors (plain logic, tested in the simulation) |
| `DriftWatch/power.*` | What the watch does while the screen is off: polling, wake on motion, checking a motion is a raise, fallbacks and statistics |
| `DriftWatch/orient.*` | Screen turns, touch maps and the two-tap touch check solver |
| `DriftWatch/timekeep.*` | Clock drift learning and correction |
| `DriftWatch/battery.*` | Battery percentage and the measured time left |
| `DriftWatch/phone.*`, `ble.h`, `ble_esp32.cpp` | Phone sync: the messages a phone may send, and the Bluetooth service that carries them |
| `companion/index.html` | The Drift sync web page |
| `tools/host/` | Computer stand-ins and tests. `render_faces` draws every face at test times and checks text stays inside the round screen; `simulate` drives the real interface and settings code with scripted touches and BOOT presses, checks every screen's layout and touch regions, and writes `report.md` (touch sizes and contrast); `simulate_network` runs the real interface and the real `net.cpp` together; `net_test` runs the real `net.cpp` (sync, time zone lookup, time server, setup hotspot, drift, restarts in a fresh process) against the Wi-Fi, web and time-server stand-ins in `tools/host/net/`; `power_test` checks the motion sensor's register sequences and the screen-off controller; `mutation_check.sh` puts known bugs back and checks the tests catch them; `Preferences.h` is an in-memory stand-in for saved settings; `sheet.py` makes preview sheets |

Host tests, from the project folder (needs g++ and the TFT_eSPI 2.5.43 source for its fonts):

```
git clone --depth 1 --branch V2.5.43 https://github.com/Bodmer/TFT_eSPI.git ../TFT_eSPI
tools/host/run_tests.sh ../TFT_eSPI
```

This builds and runs all five: `render_faces`, `simulate` (frames and `report.md` go to `tools/host/out`), `simulate_network`, `net_test` and `power_test`. `HOST_STRICT=1` adds warnings as errors and the address and undefined-behaviour sanitizers. `tools/host/mutation_check.sh ../TFT_eSPI` runs the mutation checks.

GitHub Actions (`.github/workflows/ci.yml`) runs the strict tests and the mutation checks, and builds and checks both firmware images with a report of compiler warnings, on every push and pull request; the images are kept as a download on each run. On a push to main, once those pass, it publishes the Drift sync page (`companion/` only) to GitHub Pages. That needs one setting, once: **Settings, Pages, Build and deployment, Source: GitHub Actions**.

## Ideas for later

Step counter (the motion sensor can do it), wake on motion for the twist gesture (it needs a way to know the wrist was still just before), deep sleep, and Bluetooth pairing so the Wi-Fi password is encrypted. See section 9 of `NOTES-v0.10.0.md`.

## Ready-made firmware files

The `firmware/` folder holds the ready-to-flash v0.11.1 image, `drift_watch_v0.11.1.bin`, and the diagnostic build, `drift_watch_v0.11.1_diag.bin` (timing and power logs, wake on motion and touch standby), with their checksums in `SHA256SUMS-v0.11.1.txt` and a manifest in `MANIFEST-v0.11.1.md`, plus older versions (the v0.7.0 and v0.8.0 files stall at start on this board, see Flashing). All are flashed at address 0x0.
