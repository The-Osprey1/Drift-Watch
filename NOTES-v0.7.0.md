# Drift-Watch v0.7.0: polish pass notes

What changed, why, what was tested, and what still needs the real watch.

## 1. What was implemented

- **Design system** (`theme.h`): four identity colours (yellow, cyan, magenta, plus red for Corpo only), an orange warning colour that always comes with an icon or words, nine neutral greys, three text sizes, a 112 px safe circle for text, stroke widths, and every motion duration and touch threshold in one place.
- **Shared parts** (`gfx.*`): fixed-width segment digits used by every digital time, one battery icon, one battery ring, check, cross and warning icons, arcs measured from 12 o'clock, and text that shortens itself with "..." to fit.
- **All five faces rebuilt** around the per-face goals:
  - Digital: yellow time and magenta date kept; the glitch, colour-split edge, blinking colon and perspective grid are gone; the skyline is lower and darker; the battery is a plain icon with a percentage.
  - Stealth: solid silver markers, a 54 px hour hand and a 90 px minute hand that differ in width as well as length, a small pivot, and a thin halo so the hands stay separate where they cross.
  - Corpo: the wide yellow band is now a thin segmented rim, and the small numbers are removed.
  - Hologram: solid cyan markers and filled hands with cyan edges replace the faint stacked outlines.
  - Radar: one inner ring instead of several. The hour ring and minute track show real time; nothing pretends to be sensor data. The time is bigger and yellow.
- **Status panel, setup screens and startup sequence** (`screens.*`), described in section 2.
- **Interface state machine** (`ui.*`): face, panel, setup and boot modes, gesture recognition, transitions, sleep and wake, and settings writes held back until you stop changing things.
- **Non-blocking Wi-Fi** (`net.*`): joining, time zone lookup and clock sync run as steps polled from the main loop. The time zone web request runs in a short task of its own, so nothing freezes the screen. The setup hotspot is polled the same way.
- **Hardware layer** (`hal.h`): so the same interface code runs on the watch and in the computer simulation.
- **Diagnostics build** (`DRIFT_DIAG=1 ./build.sh`): logs average and worst drawing time, transfer time and free memory every 10 seconds. It is off in the normal build.

## 2. Design decisions

- **Hierarchy:** time first, then date and battery. Decoration uses the dark greys. Diagnostics (time zone, network, timings) appear only on the panel.
- **Accent rules per screen:** each face has one primary identity colour. Magenta on the panel is reserved for the Wi-Fi setup action, cyan for progress and pressed controls, orange for faults.
- **Digits:** every digit cell is the same width, so 11:11 and 20:48 sit in exactly the same place. The leading zero is blank in 12-hour mode, with AM/PM tucked above the minutes. All 11 test cases on all 5 faces pass the round-edge check (`designs/v0.7.0_time_tests.png`).
- **Never colour alone:** every sync state has a shape (check, cross, warning, spinner) and words. Brightness is shown by the number of filled bars. Setup steps show a number or a check.
- **Touch targets:** panel buttons are 90 x 44 px and pills are 88 x 44 px. The touch area extends 3 px beyond the drawn edge. Pressed states draw on the same poll as the touch.
- **Gestures:** horizontal is faces, vertical is levels (up opens, down closes), and BOOT always means back, except on a face, where it keeps its old job of next face. Hold is an alternative to swipe up, with a ring that shows the press registering. A tap on a face does nothing, so a brush against the screen never changes anything.
- **12/24 hour** moved from the setup web page to the panel, where it can be changed at any time.

## 3. Motion and performance trade-offs

| Motion | Time | Notes |
|---|---|---|
| Pressed state | immediate | Same poll as the touch |
| Face slide | 220 ms, follows the finger | Commits past 70 px or on a flick faster than 0.45 px/ms. A new touch catches a moving face where it is, so swipes never queue |
| Panel slide | 260 ms | Panel comes up from below and goes back down |
| Wake | Time is drawn first, then the backlight fades up over 160 ms | The fade never delays the time |
| Startup | 900 ms, power-on only | A touch skips it. Not shown after a crash restart or on wake |
| Radar sweep | 1.1 s on arrival and once a minute | Then it stops |
| Progress spinner | about 25 frames a second | Only while syncing or waiting for a phone |

All motion uses ease-out cubic (no bounce) and is timed from elapsed milliseconds, so a slow frame shortens nothing and lengthens nothing. **MOTION: LESS** turns all of the above into instant changes and makes the spinner a still ring.

**Smooth seconds were not added.** The panel has no tearing signal wired, and a full-screen redraw at 30 or more frames a second all day would cost battery for little gain. The seconds hand ticks once a second.

**Rendering:** each face is split into a static layer (redrawn only when the minute, date, battery, sync state or 12/24 setting changes) and a live layer (seconds and sweep). Each second the static layer is copied from a PSRAM cache and the live layer is drawn on top. When nothing is moving, the screen is updated once a second. The simulation confirms at most one full frame per second on an idle face.

**Memory:** there are three full 16-bit frames (115 KB each, about 346 KB of the 2 MB PSRAM): the screen, the incoming screen during a slide, and the static cache. If PSRAM fails, the watch falls back to one 8-bit frame with instant changes instead of slides.

**DMA was not used.** TFT_eSPI only does DMA from internal RAM, and the frames live in PSRAM. The screen transfer is done by the processor: at 80 MHz a full frame is about 11.5 ms of raw transfer, and probably 15 to 25 ms in practice. Side slides send the frame line by line, which adds overhead. These are estimates; the diagnostics build measures the real numbers.

**Other choices:**
- No extra tasks except the one-off time zone lookup.
- Nothing is allocated in the drawing loop.
- Settings are written to flash 2 seconds after the last change, not on every tap.
- While the screen is off, the loop wakes every 50 ms to check touch, BOOT and Wi-Fi.
- No battery-life claims are made; nothing has been measured.

## 4. What was tested here (no watch)

- **Firmware build:** arduino-cli, esp32 core 2.0.17, TFT_eSPI 2.5.43 and WiFiManager 2.0.17. No warnings from our code. The app uses 1,069,481 bytes (33% of 3 MB). The merged images `drift_watch_v0.7.0.bin` and `_diag.bin` were made, and esptool reports a valid checksum and hash.
- **Face rendering:** the real `faces.cpp` and `gfx.cpp` are compiled on a computer against a stand-in for TFT_eSPI that uses the library's own triangle, circle, smooth-line and arc algorithms and its font tables. Every face was drawn at 20:48, 11:11, 23:59, 00:00, 10:09, 12-hour 9:41 and 12:58, no battery reading, no time, syncing, and mid-sweep. There are 0 text boxes outside the safe circle.
- **Panel and setup screens:** 6 panel variants (including a 32-character time zone) and 7 setup states. All text is inside the circle.
- **Interaction simulation:** the real `ui.cpp`, `screens.cpp` and `faces.cpp` are driven with a fake clock, finger, BOOT button, battery and Wi-Fi. 48 of 48 checks pass:
  - cold boot shows the sequence while sync starts; a warm restart skips it
  - a 60 px flick changes face; a slow 30 px drag snaps back
  - three quick swipes move three faces and settle within 220 ms of the last release
  - catching a slide and dragging back keeps the face
  - hold opens the panel; LIGHT, CLOCK and SYNC work; a press that drags away does not act
  - swipe down and BOOT close the panel
  - the full Wi-Fi flow works, including failure, RETRY, success, CANCEL and BOOT
  - sleep after 15 s; a waking swipe does not change face; the time is drawn before the light fades up within 160 ms
  - the radar sweep stops; the idle face pushes at most one frame a second
  - reduced motion cuts instantly
  - no Wi-Fi saved opens setup
  - rapid swipes cause one flash write, not many
- **Bugs the simulation caught and that are now fixed:**
  - Flick speed was measured between polls instead of between touch reports, so flicks did not register.
  - Rapid swipes merged into one.
  - The "Drift-Setup" text clipped the screen edge.
  - The hold ring was left on the face as the panel slid in.
  - Panel labels touched the values below them.
- **Not simulated:** real drawing and transfer time (the simulated clock does not advance while drawing), the touch chip's real report rate, and the real Wi-Fi library.

## 5. Physical-watch checklist

1. **Flash and boot:** the startup sequence plays once, then the face shows. Press reset: it plays again. It should not play when the screen wakes.
2. **Colours:** yellow, cyan and magenta look right. If red and blue are swapped, the screen needs one setting in `build.sh`.
3. **Touch direction:** swipe left should bring in the next face from the right. If directions are wrong, set `TOUCH_SWAP_XY`, `TOUCH_FLIP_X` or `TOUCH_FLIP_Y` in `board.h`.
4. **Swipe feel:** the face follows your finger smoothly. Flicks register. A short slow drag snaps back. Three quick swipes move three faces.
5. **Hold:** the ring appears after about 0.2 s and the panel opens at 0.6 s. Taps on panel buttons register first time.
6. **Wake:** with the screen off, a touch shows the time and the light fades up. That touch does not change face. BOOT also wakes.
7. **Wi-Fi:** setup on first start, then joining, finding the time zone, setting the clock, and "All set". Then try a wrong password: you should see "Can't join Wi-Fi" and RETRY. The time zone matches your location.
8. **Battery:** the percentage is believable. Compare with a meter if you can. If it reads "--", the pin reading needs checking.
9. **Diagnostics build:** flash `_diag.bin`, open a serial monitor at 115200, and note the average and worst drawing time and the transfer time. Slides should feel smooth. If the transfer time is above about 30 ms, slides will look slow and the frame cap or slide method needs changing.
10. **Battery life:** compare a full day on v0.6.0 against v0.7.0, if you want numbers.

## 6. Limitations

- Not yet run on hardware. Touch axis orientation, colour order and battery calibration are unconfirmed.
- No charging indicator: the board does not tell the ESP32 when it is charging.
- No haptics or sound: there is no motor or buzzer.
- The clock is lost on power loss, and setting it needs Wi-Fi: there is no clock chip and no manual time setting yet.
- A time zone detected from the internet is a fixed offset. It is refreshed at every sync (every 6 hours), so a daylight-saving change can show up to 6 hours late. Typing a full time zone in setup avoids this.
- The screen-off loop polls every 50 ms rather than using the processor's light sleep. Light sleep with a touch interrupt is the next battery step, once the touch interrupt has been confirmed on the board.
- `platformio.ini` is untested; `build.sh` is the checked route.
