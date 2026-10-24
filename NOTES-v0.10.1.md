# Drift-Watch v0.10.1: hardening fixes

Four bug fixes on top of v0.10.0. Nothing else changes. **Not run on a watch yet.**

## What changed

| Bug | Before | Now |
|---|---|---|
| Touch read errors | `touch::read()` cleared the finger before the I2C read, so one failed read on the shared bus (touch chip and motion sensor) looked like the finger lifting: a held button acted, a drag committed | A failed read leaves the touch as it was. Reads failing for 50 ms (`TOUCH_DROPOUT_MS`) end the touch without acting: a pressed button is released without firing, and a dragged face springs back |
| Serial commands | `Serial.readStringUntil('\n')` waited up to 1 s for the end of a line, freezing drawing, touch and sleep | Bytes are collected as they arrive (`serial_cmd.h`, at most 256 per loop) and a line runs when its newline comes. A line over 127 bytes is dropped whole, so a long Wi-Fi password is never saved cut short. `TIME:` and `WIFI:` work as before |
| Auto time zone | `net::begin()` saved `PST8PDT,M3.2.0,M11.1.0` into `settings.tz` on the first start, so the empty value meaning Auto was gone for good | Pacific time is only the zone in use until a lookup answers. It is never saved, so Auto stays Auto across restarts |
| Screen memory | If neither a 16-bit nor an 8-bit frame fitted, the watch ran on with nothing drawn (TFT_eSPI skips drawing to a sprite that was never made, so it was a blank screen, not a crash) | It writes "Out of memory / Restart the watch" straight to the panel and stops |

## A watch that already ran v0.10.0

v0.10.0 already saved Pacific time on its first start, and v0.10.1 cannot tell that apart from Pacific time chosen on purpose, so it keeps it. To go back to Auto, open Wi-Fi setup and leave the time zone field blank.

## Checks

The simulation has 336 checks (29 new), all passing. With the old touch handling put back, 3 of the new touch checks fail.

- Touch: a 30 ms dropout mid-swipe keeps the drag and the swipe still lands; a dropout that lasts springs the face back; a button press cut off by failed reads does not toggle the setting, while a 20 ms blip still does; a chip that never answers makes up no touches.
- Serial: lines sent in pieces, `\r\n` endings, an over-long line dropped, the parser's accept and refuse cases, `TIME:` setting the clock and `WIFI:` saving the network through the real interface loop.
- Memory: with no frame at all, the notice is drawn and the loop keeps running.

The time zone fix is not covered: the simulation stands in for all of `net.cpp`. Testing the real `net.cpp` with Wi-Fi stand-ins is the next step (phase 2).

## On the watch

1. Flash `firmware/drift_watch_v0.10.1.bin` at 0x0.
2. Open a serial monitor at 115200 baud and type `TIME:` with a few digits but no Enter. The radar sweep and swipes should keep going. Finish the line and press Enter: "Time set to ..." should print.
3. Swipe and hold buttons as usual. Nothing should feel different.
4. After a Wi-Fi sync with the time zone blank, restart. Details should still say the zone is found automatically, not "typed in setup".
