// QMI8658 register logic, kept apart from the I2C wiring so the computer tests run the real
// sequences against a register model (tools/host/qmi_test.cpp).
//
// Register source: QST QMI8658C datasheet Rev A (register map, CTRL9 command protocol and the
// Wake on Motion section), cross-checked against the open SensorLib driver (lewisxhe/SensorLib,
// SensorQMI8658.hpp), which uses the same bits. The board's chip answers WHO_AM_I = 0x05; its
// REVISION_ID (0x01) is logged at start so a watch test can record the exact revision.
//   0x02 CTRL1   bit 6 address auto-increment, bit 4 INT2 enable, bit 3 INT1 enable
//   0x03 CTRL2   accelerometer: bits 6:4 range, bits 3:0 rate (12..15 = low-power rates)
//   0x08 CTRL7   bit 5 data-ready signal off, bit 1 gyroscope on, bit 0 accelerometer on
//   0x0A CTRL9   command register (0x08 = write the Wake on Motion setting, 0x00 = acknowledge)
//   0x0B CAL1_L  Wake on Motion threshold, 1 mg per count (0 turns it off)
//   0x0C CAL1_H  bits 7:6 pin and starting level (01 = INT2 starting low), bits 5:0 blanking
//   0x2D STATUSINT bit 7 = the CTRL9 command is done
//   0x2F STATUS1 bit 2 = motion seen; reading the register clears it
// Wake on Motion gives no samples: the sensor only says something moved. Normal sampling is
// restored by exitWakeOnMotion() before any reading is used.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace qmi8658 {
constexpr uint8_t REG_WHO_AM_I = 0x00, REG_REVISION = 0x01, REG_CTRL1 = 0x02, REG_CTRL2 = 0x03, REG_CTRL7 = 0x08,
                  REG_CTRL9 = 0x0A, REG_CAL1_L = 0x0B, REG_CAL1_H = 0x0C, REG_STATUSINT = 0x2D, REG_STATUS1 = 0x2F,
                  REG_TEMP_L = 0x33, REG_AX_L = 0x35, REG_RESET = 0x60;
constexpr uint8_t WHO_AM_I_VALUE = 0x05;

constexpr uint8_t CTRL1_AUTO_INCREMENT = 0x40;
constexpr uint8_t CTRL1_INT2_ENABLE = 0x10;
constexpr uint8_t CTRL1_INT1_ENABLE = 0x08;  // never set: on this board GPIO 4 also drives MOSFET1
constexpr uint8_t CTRL7_DRDY_OFF = 0x20, CTRL7_GYRO_ON = 0x02, CTRL7_ACCEL_ON = 0x01;
constexpr uint8_t CMD_ACK = 0x00, CMD_WRITE_WOM = 0x08;
constexpr uint8_t STATUSINT_CMD_DONE = 0x80;
constexpr uint8_t STATUS1_WOM = 0x04;
constexpr uint8_t CAL1H_INT2_START_LOW = 0x40;  // bits 7:6 = 01

// Normal sampling, as v0.10.1 runs it: +/-4 g at the 21 Hz low-power rate, gyroscope off,
// data ready sent to INT2 (only counted, for the wiring test).
constexpr uint8_t CTRL2_NORMAL = 0x10 | 0x0D;
constexpr float LSB_PER_G = 8192.0f;  // at +/-4 g

// The few bus calls the sequences need. Every one reports whether the chip answered.
class Bus {
 public:
  virtual bool read(uint8_t reg, uint8_t *buf, size_t len) = 0;
  virtual bool write(uint8_t reg, uint8_t val) = 0;
  virtual void waitMs(uint32_t ms) = 0;  // a short busy wait between status polls
  virtual ~Bus() {}
};

enum class Result : uint8_t { Ok, BusError, CommandTimeout, AckTimeout };
const char *resultName(Result r);

struct WomConfig {
  uint8_t thresholdMg = 80;     // movement on any axis bigger than this wakes the watch
  uint8_t blankingSamples = 4;  // samples ignored after arming (about 190 ms at 21 Hz)
  uint8_t ctrl2 = 0x10 | 0x0D;  // +/-4 g, 21 Hz low power: the rate the normal mode already uses
};

constexpr uint32_t CMD_TIMEOUT_MS = 50;  // the command normally completes within a few ms

// Starts normal sampling (also used to restore it). Keeps CTRL1 and CTRL7 bits it does not own.
Result startNormal(Bus &bus);
// Arms Wake on Motion on INT2, starting low and going high on motion, with data ready off so
// the line only moves for motion. On failure normal sampling is restored where the bus allows.
Result enterWakeOnMotion(Bus &bus, const WomConfig &cfg);
// Reads STATUS1, which also clears it. `motion` is true if Wake on Motion fired.
Result readAndClearWakeEvent(Bus &bus, bool &motion);
// Turns Wake on Motion off (threshold 0 through the same command) and restores normal sampling.
Result exitWakeOnMotion(Bus &bus);
// One CTRL9 command with its handshake: write it, wait for done, acknowledge, wait for done to clear.
Result command(Bus &bus, uint8_t cmd, uint32_t timeoutMs = CMD_TIMEOUT_MS);
}  // namespace qmi8658
