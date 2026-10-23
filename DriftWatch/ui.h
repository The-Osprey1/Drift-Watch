// The interface: which screen is showing, touch gestures, transitions, screen
// sleep and wake, and when to sync. Everything runs from ui::loop() without
// blocking, so touch and the clock stay live during animations and Wi-Fi.
//
// Controls
//   swipe left / right     next / previous face (the face follows your finger)
//   swipe up, or hold      open the status and settings panel
//   swipe down, or BOOT    go back one level (panel or setup -> face)
//   BOOT on a face         next face
//   any touch or BOOT      wakes the screen; that touch does nothing else
#pragma once
#include <TFT_eSPI.h>

namespace ui {
void begin();
void loop();
TFT_eSPI &display();     // for the computer simulation, which reads the pixels
const char *screenName(); // "boot", "face", "panel", "setup" or "asleep", plus "+slide" mid-transition
}
