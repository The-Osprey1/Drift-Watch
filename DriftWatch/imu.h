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
// Edges counted on the sensor's interrupt lines (0 = INT1 on GPIO 4, 1 = INT2 on GPIO 3). The
// sensor's data-ready signal is sent to INT2, so a count that grows shows that line is wired
// as the pin tables say (INT1 is not driven, so its count stays 0). For the watch test only;
// nothing depends on it yet.
uint32_t lineEdges(int line);
}
