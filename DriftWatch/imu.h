// Minimal QMI8658 motion sensor driver: the accelerometer only, in its low-power mode,
// read over the I2C bus it shares with the touch chip. Used for raise-to-wake.
#pragma once
#include <stdint.h>

struct Accel {
  float x, y, z;  // in g, along the chip's own axes; z points out of the chip's top
};

namespace imu {
bool begin();             // true if the sensor answered and accepted its settings
bool present();
bool read(Accel &out);    // false if the sensor did not answer
}
