// The interface: which screen is showing, touch gestures, transitions, screen
// sleep and wake, and when to sync. Everything runs from ui::loop() without
// blocking, so touch and the clock stay live during animations and Wi-Fi.
//
// Controls
//   on a face
//     swipe left / right     next / previous face (the face follows your finger)
//     swipe up, or hold      open settings (the panel follows the finger up; a ring grows while you hold)
//     BOOT, short press      next face (on release)
//     BOOT, hold 0.6 s       open settings
//   on settings, details, setup or set time
//     swipe left / right     next / previous page; never changes the face
//     swipe down, or BOOT    back one level (setup, details or set time -> settings -> face)
//     tap                    a control acts on release, unless the finger moved
//     hold - or +            on set time, repeats, faster after a while
//   any touch or BOOT        wakes the screen; that touch or press does nothing else
//   raising the wrist        wakes the screen (raise to wake)
#pragma once
#include <TFT_eSPI.h>
#include "power.h"
#include "screens.h"

namespace ui {
void begin();
void loop();
TFT_eSPI &display();     // for the computer simulation, which reads the pixels
// "boot", "face", "panel", "details", "setup", "settime" or "asleep", plus "+slide" mid-transition
const char *screenName();
// For the simulation: the controls on screen now, and the page of the paged screen.
int controls(screens::Control *out, int max);
int page();
float beamAngle();  // the radar beam's angle now, < 0 when there is none
const power::Controller &standbyController();  // the screen-off waits, for the simulation and the log
// Entrances and Orbit's minute marker, for the simulation.
bool entrancePlaying();
uint32_t entrancesStarted();
uint32_t entrancesFinished();
bool markerMoving();
}
