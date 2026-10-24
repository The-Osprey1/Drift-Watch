# Drift-Watch v0.9.0: fixes and features from the first watch test

v0.8.0 was run on a real watch for the first time on 2026-10-24. This version answers that test report. Everything here was built and checked in the computer simulation that runs the real firmware code. **None of v0.9.0 has been run on a watch yet.**

## 1. What the watch test found, checked against the code

| Finding | Checked | Result |
|---|---|---|
| The ready-made .bin stalled at start and printed nothing; rebuilding with the hardware serial port fixed it | `build.sh` had `CDCOnBoot=cdc` and `platformio.ini` had `ARDUINO_USB_CDC_ON_BOOT=1`. The Waveshare page and the NuttX board notes both say the USB-C port goes through a CH343P USB-to-serial chip on UART0 (GPIO 43/44) | **Confirmed and fixed.** Both now build with USB CDC on boot off. The built image's compile flags show `ARDUINO_USB_CDC_ON_BOOT=0` |
| Joining fails with "JoinFailed (reason 2)" and does not say why | The "2" in the log line `net: sync failed (reason 2)` was the firmware's own number for "could not join" (`Fail::JoinFailed`), not the radio's reason. v0.8.0 never read why the radio gave up | **Fixed.** The radio's disconnect reason is now recorded, logged as `Wi-Fi reason N`, and shown in plain words |
| The ESP32-S3 only has 2.4 GHz Wi-Fi; dual-band mesh routers trip it up | True of the chip. Nothing on the watch or the phone page said so | **Fixed.** Said on the watch, on the phone page and in the failure hints |
| People open 192.168.4.1 while still on home Wi-Fi | The step 2 screen (open the address) only appears once a phone has joined the hotspot, but it did not say to stay on it | **Fixed.** Both steps now say to join Drift-Setup first and stay on it |
| `startPortal()` switched to station mode just before the hotspot started | True. The library then turned station mode off again, which stops the radio completely, before starting the hotspot | **Fixed.** It starts straight in hotspot mode, and logs whether the hotspot came up |
| No clock chip, no way to set the time without Wi-Fi | True (no clock chip on the board; v0.8.0 notes listed "no manual time setting") | **Added** Set time |
| The QMI8658 motion sensor is not used | True: nothing read it | **Added** raise-to-wake |
| The processor stays fully awake with the screen off | True: the screen-off loop was a 50 ms delay | **Added** light sleep |

The report's measured draw (13-16 ms) and transfer (15-40 ms) times and 1.73 MB of free PSRAM need no change.

## 2. Boot stall

- `build.sh`: `CDCOnBoot=default` (Disabled) instead of `cdc`, with a comment saying why.
- `platformio.ini`: `ARDUINO_USB_CDC_ON_BOOT=0`.
- `board.h` and the README now say where serial goes: UART0, GPIO 43 TX / 44 RX, through the CH343P to the USB-C port, 115200 baud.
- The README warns that the v0.7.0 and v0.8.0 files in `firmware/` stall on this board.

## 3. Wi-Fi setup

**On the watch**

| Screen | v0.8.0 | v0.9.0 |
|---|---|---|
| Hotspot, step 1 | "On your phone, join Drift-Setup" | "In your phone's Wi-Fi list, join Drift-Setup", then (with Cancel) "192.168.4.1 only opens once your phone is on Drift-Setup." |
| Hotspot, step 2 | "In a browser, open 192.168.4.1" | "Phone joined. Stay on Drift-Setup and open 192.168.4.1", then (with Cancel) "Pick a 2.4 GHz network. 5 GHz won't work." |
| Can't join | "Check the network and password." | The reason and what to do, below |

| Radio reasons | Shown as | Hint |
|---|---|---|
| 201 network not found, 200 signal lost | Network not found. | The watch only works with 2.4 GHz Wi-Fi. Check 2.4 GHz is on and the watch is near the router. |
| 202, 15, 204, 14, 23 (handshake and password failures) | Password not accepted. | Check the password with Edit Wi-Fi. Capital letters count. |
| Any other, including 2 (common with band-steering mesh routers) | The router turned the watch away. | Turn on 2.4 GHz and WPA2 on the router. Mesh routers may need a separate 2.4 GHz network. |
| No reason before the 15 s timeout | No answer from the network. | The watch only works with 2.4 GHz Wi-Fi. Move closer to the router, then Retry. |

Details also shows the reason and "Wi-Fi reason N". The serial log prints `net: join failed, Wi-Fi reason N (group G)`.

**On the phone page:** a bold note above the time zone box: "This watch only works with 2.4 GHz Wi-Fi. Only networks it can use are listed. If yours is missing, turn on 2.4 GHz on your router (some mesh routers call it a compatibility or IoT network)."

**Hotspot start:** `WiFi.mode(WIFI_AP)` replaces `WiFi.mode(WIFI_STA)` before the library starts the hotspot. The log line `net: hotspot Drift-Setup up at 192.168.4.1` (or `NOT up`) shows whether it worked.

The longer wording means the hotspot screens now take two pages (instructions, then the note and Cancel) and a join failure takes three (reason, hint, actions). BOOT and swipe down still cancel from any page.

## 4. Set time

- New **Set time** control on settings page 2, next to Sync now. Its label shows the clock ("Clock 20:48", or "20:48 by hand"), or "Time not set" in orange.
- When the clock is not set, settings open on that page, and leaving Wi-Fi setup lands there too. The face hint says "Swipe up: set time" with no Wi-Fi saved, otherwise "Swipe up: sync or set".
- Six pages: hour, minutes, day, month, year, then **Save** with the full result ("20:48", "Sat 24 Oct 2026"). Each field page shows the whole time or date with the field being set in cyan and underlined, and large - and + buttons (86 x 68 px each, the same as the other side-by-side pairs).
- Holding - or + repeats after 0.45 s, every 0.12 s, then every 0.05 s after eight steps.
- Each field wraps on its own (minutes past 59 go to 00 without changing the hour). Days stay inside the month (31 Jan moved to February becomes 28 or 29 Feb). Years run 2024 to 2099.
- 12-hour format shows "8:48 PM" and the hour steps through AM and PM.
- If the clock was never set it starts from the firmware's build date at 12:00.
- Swipe down or BOOT leaves without saving; so does the screen turning off.
- Saved time uses the saved time zone. A later successful sync replaces it.
- With no network saved, the watch no longer tries a background sync every 30 minutes.

## 5. Raise-to-wake

- **Driver** (`imu.h`, `imu_qmi8658.cpp`): finds the QMI8658 at I2C address 0x6B or 0x6A (who-am-I 0x05), resets it, and runs only the accelerometer in its 21 Hz low-power mode at +/-4 g. The gyroscope stays off.
- **Detector** (`raise.h`, `raise.cpp`, plain logic tested in the simulation): wakes when the screen goes from turned away (under 0.35 g along the screen's outward direction) to tilted up towards you (at least 0.65 g, about 50 degrees from level), within 1.5 s, and stays there for 0.15 s while the total is within 0.3 g of 1 g. One wake per raise: after the screen times out, the wrist has to come down and rise again.
- **Which way is up:** the sensor sits on the board, and it is not certain whether its z axis points out of the screen or out of the back. Every touch or BOOT press, the watch reads which way gravity points (the person is looking at the screen then) and learns the direction, saving it with the settings. The starting guess is `IMU_SCREEN_FACING_DEFAULT` in `board.h`.
- **Setting:** Raise to wake, On or Off, on settings page 3. On by default. Shows "No sensor" if the chip did not answer. Details says "Motion sensor found" or "not found".
- The sensor is polled over I2C while the screen is off (every 80-100 ms). Its interrupt pins are not used: the NuttX board notes say GPIO 4 and 3, but the Waveshare page lists GPIO 4 for something else, so that is unconfirmed.

## 6. Light sleep

- With the screen off and Wi-Fi off, the chip goes into light sleep (`esp_light_sleep_start`) for 0.1 s at a time with raise-to-wake on, or 0.5 s with it off.
- It wakes early on the touch chip's interrupt line (GPIO 5) or BOOT (GPIO 0). A line that is already low is not used as a wake source, so a held finger or a stuck line cannot keep the chip spinning; polling still sees it.
- After a touch or BOOT wake the input is read straight away, so the waking touch still does nothing else. If the touch was over before it could be read, the screen wakes anyway.
- Light sleep is skipped while a sync or the hotspot is running; the plain 50 ms wait is used then.
- The serial port is flushed before each sleep. Lines printed while asleep may arrive late.
- `DRIFT_LIGHT_SLEEP` in `board.h` (or `-DDRIFT_LIGHT_SLEEP=0`) turns it off.

## 7. Settings version 3

Adds `raise` (raise-to-wake, default on) and `facing` (learned screen direction, 0 until learned). Settings saved by v0.7.0 and v0.8.0 load with their values kept.

## 8. What was tested here (no watch)

- **Firmware build** with arduino-cli, esp32 core 2.0.17, TFT_eSPI 2.5.43 and WiFiManager 2.0.17, using the updated `build.sh`. No compiler warnings. The compile flags show `ARDUINO_USB_CDC_ON_BOOT=0`. esptool reports a valid checksum and hash for the app.

  | Build | App size | Global variables | Merged image |
  |---|---|---|---|
  | v0.8.0 (rebuilt here as a baseline) | 1,089,653 bytes (34%) | 49,196 bytes | 1,155,552 bytes |
  | v0.9.0 | 1,134,865 bytes (36%) | 49,648 bytes | 1,200,816 bytes |
  | v0.9.0 diagnostics | 1,135,253 bytes (36%) | 49,712 bytes | 1,201,200 bytes |

- **Face rendering (`render_faces`):** all five faces and states, 0 text edge or state problems.
- **Interaction simulation (`simulate`): 185 of 185 checks pass** (136 in v0.8.0, all kept, some moved to the new settings order). New checks:
  - **Layout** of every set time page in both text sizes and both clock formats, the new settings states, and every join failure on the setup and details screens: text inside the circle, touch regions at least 68 px, apart and inside the circle.
  - **Wording:** step 1 names Drift-Setup on its first page and does not send people to the address yet; step 2 says to stay on Drift-Setup and pick 2.4 GHz; each join failure shows its reason and hint; details show the radio's number.
  - **Set time:** opens on the hour from the build date, taps and hold-to-repeat step the hour (none extra on release), minutes wrap alone, Save sets the clock to exactly the time chosen and marks it as set by hand, the face then shows the time, no background syncs with no Wi-Fi, Back leaves the clock untouched.
  - **Date arithmetic:** weekday of 24 Oct 2026, 31 Jan to February in normal and leap years, each field wrapping on its own, year range.
  - **Raise-to-wake:** lying flat does not wake; hanging then raised wakes; staying up after it sleeps does not wake again; lowering and raising does; a slow turn (over 1.5 s), a brief swing, shaking at 2 g and turning face down do not; Off does nothing; it learns a reversed sensor from touches and then wakes on that board, and learns back; the setting toggles; no sensor is handled.
  - **Light sleep:** used while the screen is off, not used while a sync runs, used again after.
  - **Upgrade:** v0.7.0 settings load with raise-to-wake on and the direction unknown.
- **Not simulated:** the real radio's reason numbers from real routers, how the hotspot behaves after the start-order change, the QMI8658's real axes and noise, how a real wrist moves, light sleep's effect on battery, I2C and the backlight after waking, and touch chip interrupt timing.

Previews in `designs/`: `v0.9.0_settime.png` (every set time page, 12-hour, large text), `v0.9.0_settings_and_setup.png` (the new settings pages and hotspot steps), `v0.9.0_join_failures.png` (each failure reason and the details pages), and `v0.9.0_report.md` (touch sizes and contrast).

## 9. Physical-watch checklist

1. **Flash** `drift_watch_v0.9.0.bin` at 0x0. Open a serial monitor at 115200 baud on the watch's port. Expected: the startup sequence on screen, and lines such as `touch: chip id ...`, `imu: QMI8658 at 0x6B (ok)` and `ui: frame 16-bit ...` on serial. If it stalls, note the last line printed.
2. **Motion sensor:** Details should say "Motion sensor found". If serial says `imu: QMI8658 not found`, raise-to-wake shows "No sensor"; please send that line.
3. **Raise-to-wake:** wear it, let the screen go off, lower your arm, then lift it to look. It should light within about half a second. Try it 10 times and count misses. Then check for false wakes: walking, typing, resting your arm on a desk. If it never wakes, tap the screen a few times while looking at it (it learns which way up the screen is) and try again.
4. **Set time:** with Wi-Fi off or no network saved, Settings, page 2, Set time. Set a time a few minutes ahead, Save, and compare with a phone after 10 minutes. Hold + to check the repeat speed.
5. **Wi-Fi setup:** open Wi-Fi setup and check serial says `net: hotspot Drift-Setup up at 192.168.4.1`. Check the phone sees Drift-Setup within a few seconds and the page shows the 2.4 GHz note.
6. **Join failures:** try a wrong password, then a 5 GHz-only network if you have one. Note what the watch says and the `Wi-Fi reason N` line on serial or in Details. This tells us whether the reason groups match real routers.
7. **Light sleep:** with the screen off, a tap and BOOT should wake it as quickly as before, and the waking tap should do nothing else. Check the backlight stays fully dark while asleep. If anything is odd, try a build with `DRIFT_LIGHT_SLEEP=0` to compare.
8. **Battery:** if you can, note the battery percentage before and after a few hours with the screen mostly off, with Raise to wake on.

## 10. Limitations

- Not yet run on hardware. Everything in section 9 is unconfirmed.
- The time set by hand is lost when the watch loses power (no clock chip), and the chip's own clock may drift by seconds a day.
- Raise-to-wake thresholds are first guesses and may need tuning on a real wrist. There is no "turn wrist away to sleep" yet.
- The watch cannot see 5 GHz networks at all, so for a 5 GHz-only network it can only say "not found".
- No battery-life claim is made for light sleep until it is measured.
