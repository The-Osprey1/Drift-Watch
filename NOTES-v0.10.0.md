# Drift-Watch v0.10.0: screen turn, radar, offline time, phone sync and gestures

This version answers the five-part overhaul brief: screen rotation, Bluetooth time sync and Wi-Fi hardening, a gesture engine, the radar sweep, and readability and polish. Everything here builds with no warnings and passes 290 scripted checks in the computer simulation, which runs the real interface, drawing, settings, phone-sync and gesture code. **None of v0.10.0 has been run on a watch yet.** Section 8 lists exactly what only a watch can prove, and section 9 what was left for later.

## 1. Plan and architecture

The work was done in the order the brief asked for: rotation, radar and offline time first; then Bluetooth; then gestures and power.

New modules are plain logic wherever possible, so the simulation tests the code that ships rather than a copy of it:

| Module | Job | Hardware? |
|---|---|---|
| `orient.*` | The eight ways a square can be turned or mirrored, the touch map expected for each screen turn, and the two-tap touch check solver | No |
| `timekeep.*` | `ClockTrim`: learns how fast the clock drifts between trusted times and nudges it | No |
| `battery.*` | Percentage from voltage (moved out of `ui`), and `Estimate`, the measured time left | No |
| `phone.*` | The text lines a phone may send over Bluetooth, and what the watch does with each | No |
| `ble.h`, `ble_esp32.cpp` | Bluetooth on the watch: advertises only while Phone sync is open, turns each write into a text line | Yes (the simulation fakes it) |
| `raise.*` | Raise to wake now needs a flick; new `DropDetector` for wrist-down sleep | No |
| `companion/index.html` | The Drift sync web page for a phone or computer | Browser |

Data flow for phone sync: a Bluetooth write arrives on the radio task, is copied into a queue as one line and nothing else happens there. The main loop takes lines from the queue, `phone::apply` acts on each, and the reply plus the watch's status is put back on the status characteristic for the phone to read. That keeps the Bluetooth task from touching the clock, settings or screen.

## 2. Screen turn and touch

**Pins checked.** The brief said both motion-sensor interrupt lines go to GPIO 4. The Waveshare pin table says INT1 is GPIO 4 and INT2 is GPIO 3, and GPIO 4 is also the gate of MOSFET 1 on this board. `board.h` now has `PIN_IMU_INT1 4` and `PIN_IMU_INT2 3`. INT1 is left undriven so the sensor can never switch that MOSFET; INT2 carries the data-ready signal (see section 6).

**The screen is turned in hardware.** `setRotation(1)` sets the panel's own scan direction, so the frame buffers stay 240 x 240 and nothing is redrawn differently. `DISPLAY_ROTATION` in `board.h` is the default (1).

**Touch is calibrated, not guessed.** The touch chip always reports positions in the panel's own frame. The brief's formula (X = 239 - Yraw, Y = Xraw) is one of the eight possible maps; which one is right depends on which way the panel really turns for `setRotation(1)`, and the datasheets do not settle that. So:

- `orient::expectedTouchMap(turn)` gives the map for the usual TFT_eSPI meaning (map 3 for turn 1). The brief's formula is map 1, the map for a panel that turns the other way.
- A **touch check** shows a dot near the top, then one on the right. The two raw positions decide which of the eight maps puts both taps within 56 px of their dots. It also catches a touch chip mounted turned or mirrored.
- It opens by itself on the first start (and whenever the screen turn has not been checked), after changing **Screen turn** in settings, from **Touch check** in settings, and from anywhere by **holding BOOT for 4 seconds**, which works even if touch is completely wrong.
- BOOT skips it and keeps the expected map. Two taps that fit no map show "No match" and ask again; nothing is saved.
- The result is saved with the turn it was checked for, so changing the turn asks again.

New settings: **Screen turn** (Normal, Right, Half turn, Left) and **Touch check** ("Lined up" or "Not done").

## 3. Radar

- The beam turns all the time the radar face is showing, one turn every **3.6 s**, at about 30 frames a second (33 ms frames).
- **Phosphor trail:** a 105-degree fan behind the beam, drawn as 3-degree wedges whose brightness falls off exponentially (it halves about every 22 degrees), from a bright phosphor green to the dark dial.
- **Rings and blips:** range rings at radius 38 and 62. The seconds dot is the blip: it glows as the beam passes over it and fades with the trail.
- **Low battery (below 20%, not charging):** the beam stops turning, sits on the seconds dot and moves with it, redrawn once a second with the rest of the face.
- Reduced motion keeps the beam off. If memory is short and there is no spare frame, it also steps once a second instead of animating.

## 4. Offline time and Wi-Fi

**Never trapped in the hotspot.** v0.9.0 opened Wi-Fi setup on the first start. Now the watch always starts on the face (after the touch check if touch is not checked). With no clock it shows dashes in dim yellow with **SET TIME** and "Swipe up: set time". The words "NO TIME" no longer appear anywhere on the dial. The hotspot only opens from **Wi-Fi setup** in settings.

**Clock drift.** The board has no clock chip and no 32 kHz crystal, so in light sleep the time is kept by the chip's internal RC oscillator, which can be off by a fraction of a percent. Each trusted time (a Wi-Fi sync or a phone sync) shows how far the clock drifted since the last one. Over spans of at least 6 hours the rate is learned and saved, and the clock is nudged by that rate every 10 seconds (in steps of at least 0.2 s). Errors bigger than 3% are thrown away, since those are a wrong time zone or a mis-set clock, not drift. Setting the time by hand keeps the learned rate but starts a new span. Details shows it, for example "Clock drift 4.1 s a day slow, corrected".

**Time across sleep and restarts.** Light sleep keeps the clock running, so nothing is lost with the screen off. The watch also saves the time to flash at most once an hour; after a power loss, **Set time** starts from that saved time instead of the build date.

**Hotspot hardening.** The library already answers every name lookup with the watch's address and sends phones' "am I online?" checks to its page, so the "missing captive DNS" in the brief was not missing. The hotspot settings are now spelled out anyway: fixed address 192.168.4.1, channel 1, worldwide country code with channels 1 to 11 (seen by phones set to any country), and full 19.5 dBm transmit power while it is open.

## 5. Phone sync over Bluetooth

- **Settings > Phone sync** turns Bluetooth on and advertises as `Drift-XXXX` (from the chip's Bluetooth address) for **3 minutes**, extended by each message. Bluetooth is off at all other times, so nobody can connect and light sleep is not affected. Cancel, Done, BOOT or the time running out turns it off and frees its memory.
- **The Drift sync page** (`companion/index.html`) works in Chrome or Edge on Android, Windows, macOS, Linux and Chromebooks, and in the Bluefy app on an iPhone. It sends the zone name, the zone's rules worked out from the browser (including summer time, checked for London, New York, Sydney, Kolkata, Auckland, St John's and Berlin), then the time to the millisecond. It can also send a Wi-Fi network and password. Host it on GitHub Pages (it must be served over https) or open it from any https host.
- **Standard Current Time Service** (0x1805) is offered too, so generic Bluetooth apps can set the clock.
- The watch shows each step: waiting with the name and countdown, phone connected, any refused message and why, then "Time set" with the clock and the saved network.
- Lines the watch accepts are listed in `phone.h`. Anything else is refused with a reason the phone can read.

**Security:** the link is not paired or encrypted. The Wi-Fi password travels in the clear over a few metres of radio while the window is open. Both the watch notes and the page say so. Pairing is in section 9.

## 6. Gestures and power

**Raise to wake** keeps the v0.9.0 rules and adds the brief's flick: the wrist must start turned away, turn up within 1.5 s **at 80 degrees a second or faster at some point**, then hold the screen up and steady (total acceleration within 0.2 g of 1 g) for 200 ms. The turn rate comes from how fast the direction of gravity moves between accelerometer samples, which is the roll and pitch rate the gyroscope would give for this purpose, so **the gyroscope stays off** (it draws far more than the accelerometer).

**Wrist-down sleep** is new. Once the screen has been held up and looked at for 300 ms, turning it away (or letting the arm drop) for 360 ms turns it off without waiting for the 15 s timeout. Samples come every 80 ms, so it acts within 400 ms. A touch puts it off for 2 s, so reading the watch lying down or showing it to someone does not blank it.

**Night mode** (Off, 22:00-07:00, 23:00-07:00, 00:00-08:00) turns raise to wake and wrist-down sleep off inside its window. Touch and BOOT still wake the screen.

**Motion-sensor interrupt:** the sensor's data-ready signal is now on INT2 (GPIO 3), and the watch counts edges on both lines. Details shows "Sensor lines: GPIO 4 n, GPIO 3 n" as a check of the wiring on the real board. Waking the chip from the interrupt is in section 9.

## 7. Readability and polish

- **Spring slides:** when a finger lets go mid-swipe, the slide carries on from the finger's speed with a critically damped spring instead of a fixed curve, so fast flicks finish fast and gentle releases settle softly. It always ends within 450 ms.
- **Backlight fade-in** on wake: 120 ms.
- **Battery time left:** measured from how fast the percentage actually falls (at least 45 minutes and a 3% drop), shown in Details as "About 13 h left" or "About 2 days 13 h left", "Time left: measuring" until then, and "Charging" when the level rises. Nothing is assumed about battery size.
- **Details** also shows the last sync's source and age ("by phone, 12 days ago"), the clock drift, the sensor lines, and the screen turn and touch check state.
- **Settings** now has 12 controls on 6 pages (12 with large text): Brightness, Time format; Set time, Phone sync; Sync now, Wi-Fi setup; Raise to wake, Night mode; Motion, Screen turn; Touch check, Status and Text size.
- Every new screen passes the layout checker in both text sizes: text inside the round edge, inside its control, touch regions at least the minimum size and never overlapping.

Previews: `designs/v0.10.0_new.png` (radar, touch check, settings and details) and `designs/v0.10.0_phone.png` (phone sync in every phase).

## 8. Test and validation matrix

"Simulation" means a scripted check in `tools/host/simulate.cpp` or `render_faces.cpp` running the real firmware code. "Watch" means only the real board can show it.

| Area | Requirement | Simulation | Watch |
|---|---|---|---|
| Screen turn | Picture turns with Screen turn; panel rotation set | Checked (rotation recorded, layouts drawn) | Check the picture really turns, and which way |
| Touch | Touch check finds the right map | Checked for all 8 maps with taps 25 px off, for both possible panel directions, and through the interface with a panel that turns the "wrong" way | **Run it.** Note the map in the serial log: `touch check raw (...) -> map N` |
| Touch | BOOT skips, bad taps ask again, 4 s BOOT hold opens it | Checked | Try the 4 s hold |
| Radar | 3.6 s a turn, about 30 frames a second, trail and blip | Checked (90 degrees in 0.9 s, 20-31 frames a second) | Watch it for smoothness; the diagnostic build logs frame times |
| Radar | Low battery: beam on the seconds dot, at most one frame a second | Checked (at most 4 frames in 3 s) | Only with a battery under 20% |
| Faces | No "NO TIME"; SET TIME with no clock | Checked on all five faces | Look |
| Start | Never opens the hotspot by itself | Checked | Start with no Wi-Fi saved |
| Drift | Learns the rate over 6 h+, ignores impossible errors, nudges the clock | Checked (100 parts per million learned and corrected to within 7% over a day) | Leave it a day between two syncs and read Details |
| Set time | Starts from the last saved time after a power loss | Checked | Pull the battery, then open Set time |
| Hotspot | Fixed address, channel 1, worldwide country, 19.5 dBm | Not simulated (radio) | Join Drift-Setup from a phone; setup page should pop up |
| Phone sync | Bluetooth on only while open; 3 min window; Cancel and Done turn it off | Checked | Check the name appears in the browser's list |
| Phone sync | Time, zone and name; Wi-Fi saved and joined; refused lines answered | Checked | **Run the Drift sync page** on a phone and on a computer |
| Phone sync | Current Time Service writes | Byte parsing checked | Try a generic Bluetooth app if you have one |
| Phone sync | Screen stays on, no light sleep while Bluetooth is on | Checked | |
| Phone sync | Bluetooth fails to start | Checked (screen says so) | |
| Raise | Needs a flick; slow turns and shaking do not wake | Checked | **Try it on the wrist**; thresholds may need tuning |
| Wrist down | Sleeps within 400 ms of turning away; not before a look; a touch puts it off | Checked | Try it on the wrist |
| Night mode | Raise does not wake inside the window; BOOT does | Checked | |
| Interrupt lines | Edges counted on GPIO 4 and 3 | Not simulated | Details should show GPIO 3 counting up while the screen is on |
| Battery | Time left measured, charging seen | Checked with a scripted 5% an hour | Takes about an hour of use to show |
| Settings | Migration from v0.9.0, damaged values repaired | Checked | Flash over v0.9.0 and check your settings stayed |
| Layout | Every screen in both text sizes | Checked (no problems) | Look |
| Build | No warnings; USB serial on boot stays off | Checked (`CDCOnBoot=default`) | Serial log should appear at 115200 baud |

Simulation result: **290 of 290 checks pass**, and `render_faces` finds no text or state problems.

### Checklist for the first run on the watch

1. Flash `firmware/drift_watch_v0.10.0_diag.bin` at 0x0, with a serial monitor at 115200 baud.
2. The touch check should appear after the startup sequence. Tap both dots. Note the `touch check ... -> map N` line. If the picture is upside down or mirrored, change **Screen turn** in settings.
3. Swipe through the faces. On Radar, check the beam turns smoothly and the trail fades; note the frame times in the log.
4. Open **Status and Text size**: check "Sensor lines" shows GPIO 3 counting while the screen is on.
5. Open the Drift sync page on a phone (Bluefy on iPhone), tap **Phone sync** on the watch, connect, and sync the time. Optionally send Wi-Fi.
6. Try raise to wake and wrist-down sleep on the wrist, then turn on Night mode and check raising no longer wakes it.
7. Open **Wi-Fi setup** and join Drift-Setup from a phone; the setup page should open by itself.
8. Send the log and anything odd back to this thread.

## 9. Deferred, and why

| Item | Why it was left |
|---|---|
| **Waking the chip from the motion sensor's interrupt** (wake-on-motion) | The brief's pin was wrong, INT1 shares GPIO 4 with a MOSFET gate, and the sensor's wake-on-motion setup could not be tested without the board. This version wires data-ready to INT2 and counts edges on both lines so the next version can be built on a confirmed pin. The screen-off wait still polls every 0.1 s in light sleep |
| **Deep sleep** | Light sleep already turns the processor off between checks; deep sleep restarts the chip on wake, which would need the interface state saved and restored. Worth measuring the current draw first |
| **Bluetooth pairing and encryption** | Pairing needs a confirm step on both sides and testing against Bluefy and Chrome, which handle it differently. The short window and the warning stand in for now |
| **Gyroscope** | Not needed: the turn rate from the accelerometer covers the flick check, and the gyroscope draws much more power |
| **Removing the old archived files from before the rename from the project folder** | Still an open question from v0.9.0, not part of this brief |

## 10. Notes on the brief

- The touch formula in the brief (map 1) and the usual TFT_eSPI meaning of `setRotation(1)` (map 3) disagree. The touch check settles it on the real watch.
- The motion sensor's two interrupt lines are on GPIO 4 and GPIO 3, not both on GPIO 4.
- WiFiManager already answered all name lookups for the captive page; the hotspot settings were spelled out regardless.
- "Keep time across sleep" needs no work for light sleep, which keeps the clock running; the new work is drift correction and remembering the time across a power loss.
