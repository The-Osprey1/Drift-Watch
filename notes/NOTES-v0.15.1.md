# Drift-Watch v0.15.1 release notes

Daily-driver fixes for the faults found in the v0.15.0 feature test and the COM6 bench check. Nothing new to learn on the watch: the same tools, now reliable. On the watch, USB-C sync with the screen off and Windows sync on plug-in have passed; the other checks are at the end.

## What changes on the watch

| Fault in v0.15.0 | Fix in v0.15.1 |
|---|---|
| The timer alarm never showed on Radar or Phantom, or on Orbit, Lunar and Yozakura with Expressive motion: those faces redraw every frame, which kept restarting the alarm's 200 ms flash wait, and drew over it | The alarm flashes on its own clock and owns the screen while it runs: nothing else draws until it is dismissed or stops after 30 s. A timer that ends while the screen is dimming or blooming takes the screen over too |
| Dismissing the alarm inside Settings left the orange "TIMER UP!" picture until the next swipe | Dismissing repaints whatever screen is underneath |
| The flashlight never turned off and kept the watch from sleeping | It returns to the face after 2 minutes; the screen then sleeps as usual |
| The open drawer redrew about 25 times a second while a timer ran, and its "progress ring" was a still circle | It redraws once a second, with the countdown, and a cyan ring that empties from 12 o'clock |
| Wake gesture showed under the Clock heading and Touch check under Gestures | The Clock hub's last page holds Wi-Fi setup alone, so Gestures (page 6) and System (page 7) start on their own pages. Large text was already right |
| Serial data sent while the screen was off was lost: light sleep only woke for touch, BOOT, motion and the timer | Bytes on the serial port wake the processor (the waking bytes are lost; the screen stays off), and after any serial data the watch skips light sleep for 3 s so the next lines arrive |

## Computer side

**Bench check on COM6 (v0.15.0):** opening the port from Windows restarts the watch (`rst:0x1 (POWERON)`), because the CH343 USB chip's control lines are wired to reset. The watch reads serial again about 0.75 s later; anything sent before is lost. The old script sent `TIME:` after 200 ms and printed "Success!" over an empty reply.

- **`tools/windows/DriftWatchSync.ps1`** now waits for the watch: it sends a newline and `INFO?` every 0.35 s until the watch answers `DRIFT:VER=...` (up to 5 s, which covers a restart or a sleeping watch), then sends `TIME:` with a fresh time and only reports success on `Time set to`. It says when the watch restarted, and exits 1 on failure. `-Watch` keeps it running and syncs on every plug-in.
- **`tools/windows/RegisterAutoSyncTask.ps1`** registers a task for your own account (no administrator window needed) that starts `DriftWatchSync.ps1 -Watch` hidden at each sign-in, and starts it straight away. The watcher listens for the watch's USB chip (VID 1A86, PID 55D3) appearing and syncs each time, instead of syncing once at sign-in.
- **Drift sync page (`companion/index.html`), USB-C:** the same wait-for-an-answer before anything is sent; Sync time and Save faces wait for the watch to confirm; the face grid reads `FACES=0x0FFF` (hex, as `INFO?` sends it) as well as the Bluetooth `faces=4095`; unplugging the cable shows "USB-C cable unplugged" and returns to not connected; auto-sync on plug-in only reports success when the time was confirmed. It asks the browser to keep the reset lines released, which may avoid the restart in Chrome and Edge.

## Tests

All host tests in strict mode (warnings are errors, address and undefined behaviour checkers on):

| Suite | Result |
|---|---|
| `render_faces` | 0 problems |
| `simulate` (interface) | 560 of 560 (47 new: the v0.15.0 feature checks, now permanent in `tools/host/sim_tools.inc`, plus flashlight time-out and serial-while-asleep checks) |
| `simulate_network` | 54 of 54 |
| `net_test` | 239 of 239 |
| `power_test` | 87 of 87 (4 new: serial wake and the serial hold) |
| `mutation_check.sh` | 26 of 26 planted bugs caught (7 new, one per fault above) |

The simulator now models serial bytes that arrive during light sleep as lost (`sim::serialSent`), and keeps what the watch prints in `Serial.out` for checks to read.

## Still to check on the watch

1. Start a 1 minute timer on Radar, close the drawer, let the screen go dark: it should light and flash about 60 s later. Repeat on Phantom.
2. Start a timer, open Settings, wait for the alarm and tap: Settings should come back clean.
3. Flashlight: it should return to the face after 2 minutes.
4. **Passed on the watch:** with the screen off, `DriftWatchSync.ps1` prints "Success! Watch time set to ...", so serial data wakes this board from light sleep.
5. **Passed on the watch:** after `RegisterAutoSyncTask.ps1`, unplugging and plugging in the watch syncs it.
6. On the Drift sync page over USB-C: Save faces should show the watch's faces ticked, and pulling the cable should show "USB-C cable unplugged".
