# Drift-Watch v0.15.1: daily-driver fixes

> **Partly tested on the watch.** Every change below passes the computer tests, and both images build with no warnings. On the real watch, USB-C sync with the screen off and Windows sync on plug-in have passed. The alarm, Settings and flashlight fixes and the Drift sync page over USB-C are not yet checked on the watch. Keep `drift_watch_v0.15.0.bin` handy in case you need to go back.

The same tools as v0.15.0, now dependable: every fault found in the v0.15.0 feature test is fixed.

## Fixed on the watch

- **The timer alarm shows on every face.** Before, it never showed on Radar or Phantom, or on Orbit, Lunar and Yozakura with Expressive motion. Those faces redraw constantly and drew over the alarm. Now the alarm takes over the screen until you dismiss it or it stops after 30 s.
- **Dismissing the alarm in Settings leaves Settings clean.** The orange "TIMER UP!" picture no longer stays on screen until the next swipe.
- **The flashlight turns itself off** after 2 minutes, and the screen then sleeps as usual. Before, it stayed at full brightness and kept the watch awake.
- **The open drawer saves battery while a timer runs.** It redraws once a second instead of about 25 times, and the ring around the countdown now empties as time runs down.
- **Settings headings are right.** Wake gesture is under Gestures and Touch check is under System.
- **A sleeping watch hears the USB-C cable.** Data on the serial port now wakes the processor (the screen stays off), and the watch then listens for 3 seconds. The first bytes that wake it are lost, so the computer tools retry.

## Fixed on the computer

- **The Windows sync script waits for the watch.** Opening the port restarts the watch, and it needs about 0.75 s before it listens again. The script now asks the watch for its status until it answers (up to 5 s), then sends the time. It only says "Success!" when the watch confirms the time, and it says clearly when sync fails.
- **Windows syncs every time you plug the watch in**, not only when you sign in. Run `RegisterAutoSyncTask.ps1` once in a normal PowerShell window. You don't need an administrator window.
- **The Drift sync page over USB-C** uses the same wait-for-an-answer step. It shows which faces are on the watch, confirms Sync time and Save faces, and says "USB-C cable unplugged" when you pull the cable.

## Files

| File | What it is |
|---|---|
| `drift_watch_v0.15.1.bin` | Normal firmware. Flash at address `0x0`. |
| `drift_watch_v0.15.1_diag.bin` | Diagnostic build with timing and power logs, wake on motion and touch standby. Flash at `0x0`. |
| `SHA256SUMS-v0.15.1.txt` | Checksums for both images. |

## Checks (computer only)

Face drawing 0 problems, interface 560 of 560, interface with network 54 of 54, network 239 of 239, power 87 of 87, and 26 of 26 planted bugs caught. All run in strict mode on `main` at `d7e345a`, in [CI run 40258377261](https://github.com/The-Osprey1/DriftWatch/actions/runs/40258377261). The 47 new interface checks and 7 new planted bugs cover each fix above.

## On the watch

Passed:

- With the screen off, `DriftWatchSync.ps1` sets the time and prints "Success! Watch time set to ...". Serial data does wake this board from light sleep.
- After `RegisterAutoSyncTask.ps1`, unplugging the watch and plugging it back in syncs it.

Still to check:

1. Start a 1 minute timer on Radar, close the drawer and let the screen go dark. It should light up and flash about 60 s later. Repeat on Phantom.
2. Start a timer, open Settings, wait for the alarm and tap. Settings should come back clean.
3. Turn on the flashlight. It should go back to the face after 2 minutes.
4. On the Drift sync page over USB-C, the face grid should show the watch's faces, and pulling the cable should show "USB-C cable unplugged".
