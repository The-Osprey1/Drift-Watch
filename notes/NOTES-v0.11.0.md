# Drift-Watch v0.11.0: screen-off power, the Phantom face, and network tests

**Software acceptance: done (see Checks). Hardware and power acceptance: pending.** Nothing in this release has been run on a watch. The normal image keeps the v0.10.1 way of waiting with the screen off (reading the motion sensor every 80 ms); the new wake-on-motion and touch-standby path is only switched on in the diagnostic image, as a candidate to test on the watch. One change reaches both images: with Wake gesture off, light sleep now lasts until the next thing that needs doing (at most 30 s) instead of 0.5 s.

Built on pull request #5 (`phase2-net-sim-ci`, f5033af), which itself sits on main bcd2be5 (v0.10.1). Merge #5 first.

## What changed

### Screen-off power (diagnostic image only, for now)

| Part | What it does |
|---|---|
| `qmi8658.*` | The motion sensor's register sequences as plain logic over a small bus interface, so the computer tests run the same code. Wake on motion: sensors off, data-ready signal off (`CTRL7` bit 5), threshold in `CAL1_L`, interrupt pin, starting level and blanking in `CAL1_H`, command `0x08` through `CTRL9` with its done/acknowledge handshake (`STATUSINT` bit 7, then `0x00`), then the accelerometer alone. Every poll is bounded and every I2C result is checked. Exit restores normal sampling (+/-4 g at 21 Hz, gyroscope off) and the data-ready setting. Bits owned by other code are kept (read, change, write). A failed arm restores normal sampling before it reports the failure |
| `imu.h`, `imu_qmi8658.cpp` | `enterWakeOnMotion(config)`, `readAndClearWakeEvent(motion)` (reads `STATUS1` 0x2F, bit 2) and `exitWakeOnMotion()`, each returning a status. INT2 on GPIO 3 only; INT1 (GPIO 4, also the MOSFET 1 gate) is still never driven. Threshold starts at 80 mg, blanking 4 samples, both settable in `power::options` |
| `hal.h`, `hal_esp32.cpp` | `hal::doze()` returns a set of wake bits (timer, motion, touch, button, fault). Light sleep uses `gpio_wakeup_enable(PIN_IMU_INT2, GPIO_INTR_HIGH_LEVEL)` and `esp_sleep_enable_gpio_wakeup()`. Only lines that are idle are armed, so a stuck line cannot keep the chip busy. The INT2 edge counter is paused while its line is armed for a level wake, and restored after. The lines are read straight after waking, before anything clears them; a GPIO wake with every line idle again is taken as a short touch pulse. Every call's result is checked; a refused sleep with nothing to show waits 10 ms so it cannot spin |
| `power.*` | The screen-off controller: Awake, Idle (no gesture on), Polling (the v0.10.1 way, and the fallback), Wake on motion, and Qualifying. A motion interrupt only wakes the processor: it clears the event, turns sampling back on and feeds fresh samples (first after 50 ms, then every 80 ms, up to 1.5 s) to the existing raise detector. Only a recognised raise lights the screen; anything else re-arms with the screen still dark. Touch and BOOT always come first. Four rejected motions within 30 s (walking) switch to polling for 30 s. Each sleep runs only to the next deadline (resync or retry, night-mode hour, battery reading, settings write), at most 30 s |
| `touch.*` | `setAutoStandby(bool)`: 0xFE = 0 (allow the chip's own standby) when the screen goes off, 0xFE = 1 when it comes on, with 3 tries and a chip reset if the chip does not answer. Only chips that report a known ID (0xB4, 0xB5, 0xB6) are put in standby. The resting chip is never read; a touch while it rests wakes the screen and is used up |

Fallbacks, each kept as a reason for the log and the statistics: not in this build, no light sleep, twist needs history, arm failed, clear failed, stuck line, interrupt storm (5 wakes with no motion behind them in 2 s), sleep failed, quiet after rejects. After 3 failed standby periods it stays on polling.

**Twist stays on polling.** The twist detector needs a still moment measured just before the twist. After a motion interrupt there is none (the sensor keeps no history while it watches, and samples from before a long sleep are not a turn rate), so a twist seen only after the interrupt is missed. Rather than break "Twist" and "Raise, twist", any mode with twist on keeps polling, as in v0.10.1. Wake on motion is used for "Raise" only.

Switches (`power.h`): `DRIFT_WOM` and `DRIFT_TOUCH_STANDBY`, both on in `DRIFT_DIAG=1` builds and off in normal builds. `DRIFT_LIGHT_SLEEP=0` still turns light sleep off, and with it wake on motion. Light sleep is never used while Wi-Fi or Bluetooth is on.

Counted while running and logged at most once a minute in the diagnostic image: times the screen went off, early wakes (and how many were motion and how many spurious), qualifications, accepted and rejected gestures, fallbacks, time asleep, time with the screen off and on. Both images log the sensor's revision register and the touch chip's ID and firmware version at start.

**Serial commands with the screen off.** The serial port does not wake the chip from light sleep, and bytes that arrive while it sleeps can be lost. Press BOOT (or touch the screen) to wake the watch first, then send `TIME:` or `WIFI:`. With the screen on the chip never light-sleeps. With a wake gesture on, the normal image still wakes every 0.1 s, so a command waits at most that long; with Wake gesture off, either image can sleep up to 30 s.

**Register source.** The QMI8658 register and command values were checked against the open-source SensorLib driver (`SensorQMI8658.hpp`), which follows the QMI8658C Rev A datasheet. The datasheet itself could not be downloaded from where this was built, so the values have not been checked against it line by line. The watch's sensor revision is logged at start so it can be compared.

### Phantom face

A sixth face, after Radar. Stored face numbers 0 to 4 are unchanged. Previews at native size: `designs/v0.11.0_phantom_timeline.png` (every timeline boundary) and `designs/v0.11.0_phantom_states.png` (12-hour, reduced motion, low and unknown battery, time not set, midnight).

- Dark dial, silver chapter ring, an opening from 328 to 32 degrees showing a grained plate and a 4 Hz balance wheel with a breathing hairspring and a crimson jewel (RGB565 0xA004). Indices are left out inside the opening. Date at 3, battery at 6, SET TIME when the clock is not set.
- The seconds hand, by the fractional second s: full length (100 px) from 5 to 55 s; drawn in under the lip from 100 to 22 px between 55 and 58 s, fading out over the second half; hidden from 58 to 2 s, tail and counterweight included; out again from 22 to 100 px between 2 and 5 s, fading in over the first 30%. Hour and minute hands are always shown.
- At 5 s a small rebound: the angle moves by exp(-8f) x sin(4 pi f) x 1.8 degrees, f = (s - 5) / 0.35 s, only from 5 s to 5.35 s. This is the only time the hand moves backwards, by under a degree.
- `FaceContext` now carries the millisecond of the wall clock and a separate animation time. The faces never read a clock themselves, so tests give them exact times. The hands follow the wall clock (so they follow a sync); the balance follows the animation time.
- The frame scheduler that only drove Radar now drives any moving face: Radar as before, Phantom every 33 ms. With reduced motion, low battery or no spare frame in memory the balance stops, there is no rebound, and the seconds hand steps once a second. Nothing is drawn while the screen is off or another screen is open, and the face never keeps the screen awake.
- The diagnostic image also logs the time to send each frame to the panel.

Not measured yet: drawing plus transfer time per Phantom frame on the watch (the diagnostic log reports it).

### Network and time

- `net.cpp` reads the wall clock only through `hal::now()` (it used `time(nullptr)` for "is the clock set" and "last sync").
- The time zone lookup's answer goes through a slot published with a number (its epoch). A cancelled sync, or one that stopped waiting, sets the number it waits for to 0, so an answer that comes late is never used. The flags shared with the lookup task and the Wi-Fi event task are atomics, not `volatile`.
- The time-server client is stopped on Cancel and whenever a sync ends, so an answer that comes late cannot set the clock.
- Stricter reading of the lookup reply. Three defects found by the new tests and fixed:
  - Spaces after a colon (`"status": "success"`) made the lookup fail.
  - A reply cut short inside the offset (`..."offset":36`) was used, giving the wrong zone.
  - Offsets outside -12 h to +14 h, or not whole quarter hours, were used.
- `timekeep.cpp`: the correction still owed (up to 200 ms) when a sync arrives is now taken off the measured error. Left in, it made a 6-hour span learn a rate up to 9 ppm off. Found by the new drift test against a true clock.
- `ui.cpp`: the next resync time can no longer wrap onto 0, which means "none".

**Auto's limit.** Auto looks up the current offset only, a fixed zone such as `<+0100>-1:00`. It does not know summer-time rules, so after a change it shows the old offset until the next sync (at most 6 hours later, or 30 minutes after a failure). A zone typed in setup, or sent by the Drift sync page, keeps its rules and changes by itself.

## Checks

All run on a computer; `tools/host/run_tests.sh` builds and runs them, and CI runs them with warnings as errors and the address and undefined-behaviour sanitizers.

| Test | Result | What it covers |
|---|---|---|
| `render_faces` | 0 problems | Every face at 16 times and states, plus 21 Phantom moments at and either side of each boundary, with checks of the seconds hand's pixels, the timeline function, the rebound and the balance |
| `simulate` | 404 of 404 | The 336 checks of v0.10.1, 45 screen-off power checks, 20 Phantom checks (about 30 frames a second awake; one a second with reduced motion, low battery or low memory; none asleep or behind settings; six faces swiped through; the choice kept across a restart; all four screen turns) and 3 more from a test fix |
| `simulate_network` (new) | 39 of 39 | The real interface and the real `net.cpp`, `settings.cpp` and `timekeep.cpp` together: start-up sync, setup through the real hotspot code, Cancel while joining, during the lookup and while waiting for the time server (late answers ignored), the hotspot timing out and opening again, a quiet resync with no light sleep while Wi-Fi is on, a failed resync retried after 30 minutes, a restart |
| `net_test` | 214 of 214 | The 99 checks of #5, plus: Cancel at each stage with late answers; the time server not answering, timing out and recovering, with a valid clock kept; summer time against Auto's fixed offsets; drift learned against a separate true clock over a day and over several days, fast and slow, from Wi-Fi and phone syncs, not from a hand setting; `millis()` rolling over during a join, a sync, the hotspot countdown and a day of drift; 21 reply-parsing cases; the hotspot cancelled, timed out, saved and opened again; four restarts, each in a fresh process that starts from a saved flash file |
| `power_test` (new) | 83 of 83 | The sensor's register sequences against a register model (order, threshold, pin, handshake, timeouts, I2C failures, restore, data-ready off) and the controller (no screen before a recognised raise, touch and BOOT first, long rest, slow raise, shake, walking, repeated and stuck interrupts, storms, no sensor, night mode, radio on, polling fallback, deadlines) |
| `mutation_check.sh` (new) | 7 of 7 caught | Puts known bugs back one at a time: the Auto zone saved on first start, a motion interrupt lighting the screen (caught by two tests), a late zone answer used, the time client left running, a cut-short reply used, the owed correction ignored |

The time-server tests use a stand-in that sets the clock and then reports the sync as done, as ESP-IDF does. They do not send packets: lost or late packets on a real network are not tested.

Simulated screen-off figures (the simulation's model, not measurements): ten still minutes with Raise only and wake on motion, 21 wakes and no sensor or touch reads; an ordinary raise lights the screen 546 ms after it starts, a brisk one 526 ms; a slow raise stays dark.

## Building and files

Built with arduino-cli 1.1.1, ESP32 Arduino core 2.0.17, TFT_eSPI 2.5.43 and WiFiManager 2.0.17, with `build.sh` unchanged apart from `DRIFT_WARNINGS`. No warnings in our code with all warnings on. CI records any warnings in the core and libraries in a report rather than hiding them.

- `firmware/drift_watch_v0.11.0.bin`: polling, as v0.10.1. Flash at 0x0.
- `firmware/drift_watch_v0.11.0_diag.bin`: wake on motion, touch standby and the timing and power logs. Flash at 0x0.
- `firmware/SHA256SUMS-v0.11.0.txt` and `firmware/MANIFEST-v0.11.0.md`: checksums, the source commit, versions, flags, test counts and the flash layout. `tools/validate_image.py` checks each image's layout, size, version and contents.

## GitHub Pages

CI now publishes `companion/` (the Drift sync page, nothing else) to GitHub Pages on a push to main, after the computer tests and both firmware builds pass. Pull requests never publish. One setting is needed once: in the repository's **Settings, Pages, Build and deployment**, set **Source** to **GitHub Actions**. Until that is set and a push to main has deployed, there is no live page. The address will be shown on the deployment in the Actions run.

Serving the page over https lets browsers that have Web Bluetooth use it; it does not add Web Bluetooth to browsers without it (iPhones still need Bluefy), and the Bluetooth link itself is still not encrypted.

## On the watch (pending)

Hardware:

1. Flash the diagnostic image and open a serial monitor at 115200 baud. Note the sensor revision and touch chip ID lines.
2. Wake gesture: Raise. Let the screen go off and leave the watch still for 10 minutes. The power line should show few wakes and no spurious ones.
3. Raise the wrist after a long rest: the screen should light within about half a second. Shake it, walk, tap the table: the screen should stay dark. Repeat sleep and wake at least 100 times, mixing touch, BOOT and raises.
4. Check INT2 clears after each wake and arms again: the power log should show motion wakes and no stuck-line or interrupt-storm fallback.
5. Night mode on: no wakes from motion. Phone sync or Wi-Fi setup open: no light sleep.
6. Phantom: watch the hand go under the lip at 55 s and come out at 2 s, and check swipes stay responsive. Note the push time from the log.

Power, at the battery with USB unplugged, same brightness and radios off, v0.10.1 against v0.11.0 diagnostic: screen off and still, screen off on a moving wrist, Phantom on screen. Record the average current, wakes per minute, raise-to-light time and false wakes. No battery-life claim until then.
