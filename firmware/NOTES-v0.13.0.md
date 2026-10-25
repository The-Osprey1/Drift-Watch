# Drift-Watch v0.13.0 release notes

Everyday use and reliability, on top of v0.12.0 (PR #7). Merged to `main` in PR #8 (`de0aa12`); the images here are built from that source (`68f5c5a`), so they do not include the face revamp from PR #9. Nothing here has run on a watch yet.

## What changes on the watch

### Reliability

- **Restarts itself if it freezes.** The main loop now feeds the chip's task watchdog. If the loop stops for 60 seconds, the watch restarts instead of staying stuck until the battery runs flat. 60 seconds is twice the longest screen-off wait (30 s), so a normal sleep never trips it. The watchdog is fed just before and after each light sleep too.
- **Says why it last restarted.** The details screen shows "Last start: Crashed, restarted", "Froze, restarted by watchdog" or "Battery too low, restarted", and how many times the watch restarted itself on this charge (kept in memory that survives a restart but not a power loss). Nothing is shown after a normal power-on. A restart after a crash goes straight to the face, as before.
- **Frees a stuck sensor connection at start.** If the touch chip or motion sensor was mid-transfer when the chip restarted, it can hold the shared I2C data line low and make both look dead. The start now checks the line and, if it is held, clocks it free and sends a stop (the usual I2C bus recovery). The I2C timeout is also cut from 50 ms to 20 ms per transfer.
- **Start-up steps are logged.** The USB-C serial port (115200 baud) prints the firmware version and start reason, then "boot: screen", "boot: touch", "boot: motion sensor", "boot: network" and "boot: done". If a start ever stalls, the last line says where.
- **Auto time zone survives a restart.** Before, a watch on Auto showed US Pacific time after any restart until Wi-Fi answered. The zone Auto finds is now saved (only when it changes) and used from the next start, with its name in Details.
- **Summer time on the right day with Auto.** The lookup service only gives the zone name and today's offset, so the clock used to stay on summer or winter time until the next sync (up to 6 hours). `zones.h` now holds the rules for the zones that change their clocks (Europe, North America, Australia, New Zealand and a few others; 106 names). The rule is used only if it gives the same offset as the service right now, so an out-of-date rule can never move the clock. Every other zone keeps its fixed offset, which is right all year for it.
- **Two tries at joining Wi-Fi.** Many mesh and band-steering routers turn a device away once and then let it in. A join that has not worked after 15 seconds gets one more 15-second try before it counts as failed. The setup screen says "No answer yet. Trying again" during the second try.
- **Failed resyncs back off.** With the screen off, a failed sync used to retry every 30 minutes forever, turning the radio on each time. Failures in a row now wait 30 minutes, then 1, 2 and 4 hours, then every 6 hours. A success starts the count again.

### Everyday use

- **Scan to join on Wi-Fi setup.** The setup hotspot opens on a page with a code. A phone camera reads it and offers to join Drift-Setup (the standard Wi-Fi code that iPhone and Android cameras read). Once a phone has joined, the code changes to open the setup page, for phones that do not pop it up by themselves. Swipe for the written steps and Cancel, as before. Large text makes the code a little smaller (3-pixel squares, about 11 mm) to fit two lines of text under it.
- **Low battery notice.** Below 15% battery, and again below 5%, a box over the bottom of the face says "Battery 14%, Charge soon" (or "Charge now") for 5 seconds, the next time the face is on screen. It goes by the smoothed level, so one low reading while the radio is busy does not set it off. A level is said again only after the battery has been charged 5 points above it.
- **Charging bolt on every face.** All twelve faces show a small lightning bolt next to their battery mark while the watch is charging. It is static, so it costs no extra frames.

## How reliable the charging bolt is

The board has no wire from the charger chip to the processor, so the watch cannot read "charging" directly. It only sees the battery voltage on GPIO 1. The battery module (unchanged since v0.10.0) calls it charging when the smoothed level rises 3 points above its lowest recent value, and stops when it falls 1 point below the highest. In practice:

- **Plugging in** pushes the measured voltage up straight away, but the level is smoothed, so the bolt appears after a few readings: roughly half a minute with the screen on (one reading every 10 s) and a few minutes with it off (one a minute).
- **Unplugging** drops the voltage, and the bolt goes after a few readings in the same way.
- **Plugging in a nearly full battery** may never show the bolt: at 100% there is no rise to see.
- **Charging while the radio is busy** (a sync) can hide a small rise, so the bolt may come a reading or two late.

These are what the code does in simulation. The real voltage jump on plugging in has not been measured on this watch.

## Not done

- **Splitting `ui.cpp`** (the last item from the earlier review). It is a large move of code with no change on the watch, and It can be done as its own update now that v0.12.0 and v0.13.0 are both merged.

## Tests (host, this commit)

| Check | Result |
|---|---|
| `render_faces` (now also: every face shows the bolt, and only the bolt changes) | 0 problems |
| `simulate` (interface) | 479 of 479 |
| `simulate_network` (new: back-off timing) | 54 of 54 |
| `net_test` (new: zones kept across restarts, rules checked hour by hour against the time zone database for 2026 and 2027, second join try) | 239 of 239 |
| `power_test` | 83 of 83 |
| `mutation_check.sh` | 19 of 19 planted bugs caught (new: watchdog not fed, Auto zone not restored, zone rule not checked, join not retried, no back-off, low battery repeats) |
| Scan codes decoded from the rendered frames with OpenCV (normal and large text, both codes) | all four decode to the intended text |

The images in this folder were built from the v0.13.0 source with `build.sh` and pass `tools/validate_image.py 0.13.0`.

## Not run

- **Not run on the watch.** The watchdog's 60-second limit, light sleep with the watchdog on, the I2C recovery and the charging bolt need a watch to confirm.
- The scan code has been decoded from the simulated screen, not yet with a phone camera pointed at the real one.

## Changed files

| File | Why |
|---|---|
| `DriftWatch/hal.h`, `hal_esp32.cpp` | Watchdog, start reason and restart count, I2C bus recovery, shorter I2C timeout, boot log |
| `DriftWatch/board.h` | Version 0.13.0, `DRIFT_WATCHDOG_S` |
| `DriftWatch/settings.*` | Settings version 6: Auto's last zone and its name |
| `DriftWatch/zones.h` (new) | Summer time rules by zone name |
| `DriftWatch/net.*` | Rules for the found zone, kept across restarts; second join try; `joinTry()` |
| `DriftWatch/ui.cpp` | Feeds the watchdog, boot log, start reason in Details, resync back-off, low battery notice, charging passed to the faces |
| `DriftWatch/screens.*` | Scan page on Wi-Fi setup, "Trying again", start reason lines, low battery box; room for more detail lines |
| `DriftWatch/battery.*` | `LowWarning` |
| `DriftWatch/gfx.*`, `faces.*`, `faces_themed.cpp` | Charging bolt on every face |
| `DriftWatch/qrcodegen.*` (new) | Project Nayuki's QR code library, MIT licence, unchanged |
| `tools/host/*` | Tests for all of the above; new planted bugs |
