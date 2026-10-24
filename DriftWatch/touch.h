// Minimal CST816S touch driver. It reports the raw finger position in the panel's own
// frame; the interface turns it to match the screen turn (orient.h) and decides what is a
// tap, drag or long press.
#pragma once
#include <stdint.h>

struct TouchSample {
  bool down;
  int16_t x, y;
};

namespace touch {
bool begin();                // true if the touch chip answered
bool read(TouchSample &out); // false if the chip did not answer: says nothing about the finger
// Automatic standby (register 0xFE = 0): the chip scans slowly while nobody touches it and
// still pulls its interrupt line on a touch, so a touch can wake the watch. Off (0xFE = 1)
// keeps it tracking at full rate, as v0.10.1 always did. This is not the chip's deep sleep,
// which needs a reset to leave. Returns false if the chip does not support it or did not
// answer; the chip is then left as it was.
bool setAutoStandby(bool enabled);
bool standbySupported();     // the chip ID is one known to have automatic standby
// Reset pulse and setup again, for a chip that stops answering after standby.
bool restart();
}
