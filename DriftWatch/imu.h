// Minimal QMI8658 motion sensor driver: the accelerometer only, in its low-power mode,
// read over the I2C bus it shares with the touch chip. Used for the wrist gestures, and
// (when DRIFT_WOM is on) as a wake-on-motion interrupt while the screen is off. The register
// sequences live in qmi8658.cpp.
#pragma once
#include <stdint.h>

struct Accel {
  float x, y, z;  // in g, along the chip's own axes; z points out of the chip's top
};

namespace imu {
// Why a wake-on-motion call failed, for the fallback reason shown in the log.
enum class Status : uint8_t { Ok, NotPresent, BusError, CommandTimeout, AckTimeout };
const char *statusName(Status s);

struct WomConfig {
  uint8_t thresholdMg = 80;     // movement bigger than this on any axis is reported
  uint8_t blankingSamples = 4;  // samples ignored after arming
};

bool begin();             // true if the sensor answered and accepted its settings
bool present();
bool read(Accel &out);    // false if the sensor did not answer
bool readTemp(float &celsius);  // ambient temperature from the sensor in degrees C
// Edges counted on the sensor's interrupt lines (0 = INT1 on GPIO 4, 1 = INT2 on GPIO 3). The
// sensor's data-ready signal is sent to INT2, so a count that grows shows that line is wired
// as the pin tables say (INT1 is not driven, so its count stays 0). For the watch test only.
uint32_t lineEdges(int line);

// Wake on motion on INT2 (GPIO 3), which goes high when something moves. While it is armed
// there are no samples and read() must not be used; exitWakeOnMotion() restores sampling.
Status enterWakeOnMotion(const WomConfig &cfg);
Status readAndClearWakeEvent(bool &motion);  // reads and clears the sensor's motion flag
Status exitWakeOnMotion();
bool wakeOnMotionArmed();
bool motionLineHigh();    // INT2 level now
}
