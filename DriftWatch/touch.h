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
bool read(TouchSample &out); // false if the chip did not answer (treat as no finger)
}
