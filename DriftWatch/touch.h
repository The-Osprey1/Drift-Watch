// Minimal CST816S touch driver. It reports raw finger position; the interface
// decides what is a tap, drag or long press.
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
