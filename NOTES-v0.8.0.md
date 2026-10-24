# Drift-Watch v0.8.0: settings usability notes

What changed, why, what was tested, and what still needs the real watch. Everything here was checked in a computer simulation that runs the real firmware code. **None of it has been run on a watch yet.**

## 1. Interaction structure

The faces are unchanged. Everything else that used to be one crowded panel is now a set of short, paged screens.

```
Face ──swipe up / hold 0.6 s / long BOOT──▶ Settings (3 pages, 6 with large text)
                                              │  page 1: Brightness, Time format
                                              │  page 2: Motion, Sync now
                                              │  page 3: Wi-Fi setup, Status and Text size
                                              ├──Wi-Fi setup──▶ Setup (hotspot, connecting, result)
                                              └──Status and Text size──▶ Details (status lines, Text size)
Swipe down or BOOT: back one level. Setup and Details return to the settings page you came from.
```

| Where you are | Swipe left / right | Swipe up | Swipe down or BOOT | Tap |
|---|---|---|---|---|
| Face | Next / previous face | Settings | (BOOT short press: next face) | Nothing |
| Settings, Details, Setup | Next / previous page (no wrap-around) | Nothing | Back one level | The control under the finger |

- **One gesture, one effect.** A horizontal swipe changes pages only on a paged screen and faces only on a face. A swipe or drag never activates a control: a press that moves more than the drag threshold is cancelled and its pressed look is removed.
- **BOOT.** A short press on a face (released before 0.6 s) is next face, acting on release. A long press on a face opens settings, with the same cyan ring as a finger hold, and does **not** also change face. Elsewhere BOOT is back. The bootloader behaviour is untouched: holding BOOT while pressing RESET still enters download mode, because that happens before our code runs.
- **Opening page.** Settings open on page 1, except when the clock is not set: then they open on the Sync now page, or on the Wi-Fi page if no network is saved.
- **Page indicator.** Dots at the bottom, not touchable. The "Settings" heading shows on normal-size settings pages only.

## 2. What changed on each screen

**Settings controls** are two stacked angular controls per page (drawn 144 x 68 px at (48,44) and (48,124)). Each shows a label and its current value, always visible:

- **Brightness:** "3 of 4" plus four bars. Four levels as before.
- **Time format** (was CLOCK): "24-hour" or "12-hour".
- **Motion:** "Full" or "Reduced".
- **Sync now:** the label line carries the sync state with a symbol and words: a check and "Synced 20:48", a warning and "Sync failed", "Time not set", or the current stage ("Joining Wi-Fi", "Finding zone", "Setting time") with a spinner. The value reads "Syncing" while a sync runs. Tapping it again during a sync does nothing, and the other settings keep working while it runs.
- **Wi-Fi setup:** the label shows the saved network name (shortened with "..." if very long, the only place text is shortened) or "No Wi-Fi saved". Magenta, as before.
- **Status and Text size:** opens Details.

**Details** lists battery, clock and sync state, time zone, network, firmware version and (diagnostics build only) timings, then the **Text size** control (Normal or Large). Status lines wrap onto more pages instead of being cut short.

**Large text** is a saved setting. Body text goes from FreeSansBold 9 pt to 12 pt (about 138%) and titles from 12 pt to 18 pt (about 144%). Screens reflow and split into more pages rather than shrink anything. Settings then hold one bigger control per page (180 x 132 px). The faces are not affected.

**Wi-Fi setup** now tells you what is actually happening:

| Phase | What it shows | Controls |
|---|---|---|
| Hotspot | "Step 1 of 2: On your phone, join Drift-Setup", then once the phone joins "Step 2 of 2: In a browser, open 192.168.4.1" | Cancel, with "Closes in m:ss" |
| Connecting | "Connecting", the real stage and "1/3", "2/3" or "3/3", and "Back lets it finish" | Back (keeps going), Cancel (stops) |
| Done | "All set", "Time set to 20:48" | Done |
| Can't join | "Check the network and password." | Edit Wi-Fi, Retry |
| No time | "Wi-Fi joined. No time server answered." | Edit Wi-Fi, Retry |
| Timed out | "Nothing was changed." | Try again, Back |

- "Saved" is gone: the watch says "Connecting" once your phone submits the page.
- "Nothing was changed" appears only on a timeout, where it is true: the hotspot closed before anything was saved.
- The watch never says "wrong password"; it can only tell that it could not join. Edit Wi-Fi reopens the hotspot so the network or password can be corrected.
- Results stay on screen for 30 seconds before the screen sleeps, instead of 15.
- With reduced motion, every spinner is replaced by a still clock symbol.

**Faces** keep their design. The no-time and sync wording is clearer:

- While syncing, the dials show cyan "SYNCING".
- Otherwise they show a warning symbol and "NO TIME", plus a hint: "Swipe up: Wi-Fi setup" when no network is saved, or "Swipe up to sync".
- A valid clock with a failed sync keeps showing the time.

## 3. Engineering changes

- **Clock and sync are modelled separately** (`net.h`): whether the clock is valid, which sync stage is running (Idle, Connecting, Zone, Clock), and the result of the last finished attempt (None, Ok, Failed), with an attempt counter. All four combinations of valid or unknown clock with syncing or failed sync have their own wording and are tested.
- **A blocking wait in the setup library was removed.** In v0.7.0, saving on the phone page made WiFiManager try to join the network itself, which could freeze the watch for up to 60 seconds. It now saves and returns (one 1-second wait inside the library remains), and our own step-by-step sync joins the network while the screen keeps drawing.
- **A second time zone lookup cannot start** while one is still running.
- **One layout table per screen.** Each control has a drawn rectangle and a touch rectangle, set separately. Drawing, touch testing and the tests all read the same table.
- **Readable font.** Settings and instructions use FreeSansBold (9, 12 and 18 pt, about 10 KB of flash). The segment digits for the time are unchanged.
- **Round-screen text flow.** Each line is as wide as the circle allows at its height. Lines break at spaces, and after "/" only when no space fits. A word is split only as a last resort. Near the top of the circle a line moves down rather than split a word.
- **Saved settings version 2.** It adds the large text setting. Settings saved by v0.7.0 load with their old values, with large text off. A brightness value out of range is brought back into range.
- **Writes stay deferred**: settings are saved 2 seconds after the last change. The time zone typed on the phone page is still saved straight away, because the person submitted it.
- **No redraws when idle.** A settings screen with nothing moving is not redrawn. The spinner asks for frames only while it is visible and motion is on.
- **Same motion timings:** page slides use the face slide time (220 ms) and are instant with reduced motion or when memory is low.

## 4. Hit-target report

Measured from the layout table by the simulator (`report.md` lists every control on every page). 1 px = 0.135 mm.

| Control group | Drawn | Touch | Touch size | Farthest corner from centre |
|---|---|---|---|---|
| Stacked controls (settings, details, Cancel, Done) | 144 x 68 | 144 x 76 | 19.4 x 10.3 mm | 107.6 px |
| Side-by-side pairs (Back/Cancel, Edit Wi-Fi/Retry, Try again/Back) | 86 x 68 | 86 x 68 | 11.6 x 9.2 mm | 111.0 px |
| Large text, one per page | 180 x 132 | 180 x 132 | 24.3 x 17.8 mm | 111.6 px |

On every page and both text sizes, the simulator checks that touch regions:

- are at least 68 x 68 px;
- stay inside the 112 px safe circle;
- cover what is drawn;
- do not overlap;
- are at least 8 px apart.

Side-by-side pairs are 8 px apart. Stacked controls are 12 px apart when drawn, and their touch regions are 4 px taller at each end while still staying 4 px apart. That gap is under the 8 px goal. This is the one documented exception: the simulator applies the 8 px rule to drawn edges and checks separately that touch regions never overlap. A tap in the gap does nothing (tested).

## 5. Contrast check

Computed from the colour tokens after conversion to the screen's 16-bit colour format, using the standard relative luminance formula. **This is a contrast check against common targets, not an accessibility certification.** The real panel's gamma and viewing angle will change what you see.

| Use | Ratio | Target |
|---|---|---|
| Essential text on black | 16.3:1 | 4.5:1 |
| Labels and supporting text on black | 8.2:1 | 4.5:1 |
| Cyan progress text on black | 15.2:1 | 4.5:1 |
| Magenta Wi-Fi text on black | 5.8:1 | 4.5:1 |
| Orange warnings on black | 8.0:1 | 4.5:1 |
| Pressed value / label / cyan on the pressed fill | 16.3 / 12.6 / 11.8:1 | 4.5:1 |
| Control edges, empty brightness bars, other page dots | 4.3:1 | 3:1 |
| Pressed edge, current page dot | 15.2 / 16.3:1 | 3:1 |
| *v0.7.0 tile labels (for comparison)* | *4.3:1* | *4.5:1, failed* |
| *v0.7.0 tile edges (for comparison)* | *1.6:1* | *3:1, failed* |

## 6. What was tested here (no watch)

- **Firmware build:** arduino-cli with esp32 core 2.0.17, TFT_eSPI 2.5.43 and WiFiManager 2.0.17, using the unchanged `build.sh`. No compiler warnings.

  | Build | App size | Global variables | Merged image |
  |---|---|---|---|
  | v0.7.0 (rebuilt here as a baseline) | 1,069,129 bytes (33%) | 49,156 bytes | 1,135,024 bytes |
  | v0.8.0 | 1,089,653 bytes (34%) | 49,196 bytes | 1,155,552 bytes |
  | v0.8.0 diagnostics | 1,090,041 bytes (34%) | 49,260 bytes | 1,155,936 bytes |

  esptool reports a valid checksum and hash for the bootloader and the app in the merged image.
- **Face rendering (`render_faces`):** all five faces at every test time, plus the new states: syncing with a valid clock, failed with a valid clock, no time after a failure, and no time with no Wi-Fi saved. 0 text edge or state problems.
- **Interaction simulation (`simulate`):** the real `ui.cpp`, `screens.cpp`, `faces.cpp`, `gfx.cpp` and `settings.cpp` driven by a fake clock, finger, BOOT button, battery, Wi-Fi and flash. **136 of 136 checks pass.** They cover:
  - **Layout**, on every page in both text sizes: text inside the circle and clear of the page dots, no "..." except the network name, text inside its control, touch-region size, circle, overlap and spacing rules, and every control labelled.
  - **Contrast and reduced motion:** the contrast table above, and the still symbol replacing the spinner.
  - **Upgrade:** settings saved by v0.7.0 load correctly.
  - **Face gestures:** face swipes, snap back, rapid swipes and catching a slide, as before.
  - **BOOT:** a short press changes face; a long press opens settings without changing face.
  - **Settings navigation:** the hold ring; page swipes never change the face.
  - **Controls:** every control's action, taps on control edges and in the gaps, and drags that never act.
  - **Sync:** a second tap during a sync starts nothing, and other settings keep working.
  - **Redraws:** none while idle.
  - **Details:** the text size control, and returning to the right settings page.
  - **Setup:** failure, Edit Wi-Fi, Retry, success, Back (keeps going), Cancel (stops), timeout and Try again.
  - **Clock:** a valid clock survives a failed sync.
  - **Sleep and wake:** a waking tap does nothing; settings and details return to the face.
  - **Other:** the radar sweep, reduced motion, warm restart, low memory (cuts instead of slides), first start with no Wi-Fi, and batched flash writes.
- **Not simulated:** real drawing and transfer times, the touch chip's report rate and accuracy at the screen edge, the real Wi-Fi library and router behaviour, and how the fonts look on the actual panel.

Previews in `designs/`:

| File | What it shows |
|---|---|
| `v0.8.0_settings_native.png` and `v0.8.0_faces_native.png` | Native 240 x 240 |
| `v0.8.0_settings.png` and `v0.8.0_settings_large.png` | Settings and details, normal and large text, enlarged 2x |
| `v0.8.0_setup.png` and `v0.8.0_setup_large.png` | Every setup phase |
| `v0.8.0_faces_states.png` | The new no-time and sync states on the faces |
| `v0.8.0_flow.png` | Frames captured during the simulated scenarios |
| `v0.8.0_debug_overlays.png` | Drawn and touch rectangles and the safe circle (kept separate from the clean previews) |

## 7. Known trade-offs

- **Large text multiplies pages.** The 18 pt title only fits near the middle of the circle, so "Connecting" with large text takes three pages (status, the Back hint, then the actions). Details with a long time zone and network name can take up to eight pages. Nothing is cut or shrunk; it is more swiping.
- **The stacked touch regions are 4 px apart**, under the 8 px goal (see section 4).
- **One 1-second wait** inside WiFiManager remains when the phone page is saved.
- **No haptics or sound:** press feedback is visual only, because the board has no motor or buzzer.

## 8. Physical-watch checklist

1. **Flash** `drift_watch_v0.8.0.bin` at 0x0. Settings from v0.7.0 should survive: brightness, time format and motion as before, text size Normal.
2. **Swipe up** on a face: settings open on page 1 (or Sync now / Wi-Fi if the clock is not set). Swipe left and right: pages change, the face behind does not. Swipe down returns to the same face.
3. **Taps:** each control responds first time at its centre and near its edges. The cyan pressed look appears immediately. Start a tap and slide away: nothing happens and the highlight clears. Note any control that misses taps near the screen edge, especially Back and Cancel at the sides.
4. **BOOT:** a short press on a face changes face. Hold it on a face: the ring grows and settings open at about 0.6 s, with no face change. In settings, BOOT goes back. Hold BOOT while tapping RESET: the watch should still enter download mode.
5. **Sync now:** shows each stage, then "Synced" with the time. Tap it twice quickly: only one sync runs. Change brightness while it syncs.
6. **Wi-Fi setup:** the screen moves from Step 1 to Step 2 when your phone joins. After saving on the phone, the watch says "Connecting" and the screen keeps animating (the old version could freeze here). Try Back during Connecting (it should finish and the Sync now line should update) and Cancel (it should stop). Try a wrong password: "Can't join", then Edit Wi-Fi.
7. **Large text:** turn it on in Details. Check the text is sharp and comfortable to read at arm's length, and that no word is cut at the round edge.
8. **Reduced motion:** spinners become a still clock symbol, and pages cut instead of sliding.
9. **Readability outdoors:** check the grey labels and control edges in daylight at full and lowest brightness.
10. **Diagnostics build:** flash `_diag.bin` and confirm the drawing and transfer times are about the same as v0.7.0 on the faces.

## 9. Limitations

- Not yet run on hardware. Everything in section 8 is unconfirmed.
- No claims about battery life, touch accuracy or haptics are made.
- The v0.7.0 limitations still apply: no charging indicator, no clock chip, no manual time setting, and a detected time zone is a fixed offset.
- This is not Wear OS or watchOS software and does not follow their guidelines; the touch and contrast targets were chosen for this screen.
