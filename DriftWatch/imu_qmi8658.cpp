#include "imu.h"
#include <Arduino.h>
#include <Wire.h>

namespace {
// The address depends on how the board wires the SA0 pin, so both are tried.
constexpr uint8_t ADDRS[] = {0x6B, 0x6A};
constexpr uint8_t REG_WHO_AM_I = 0x00;  // reads 0x05
constexpr uint8_t REG_CTRL1 = 0x02;     // serial interface
constexpr uint8_t REG_CTRL2 = 0x03;     // accelerometer range and rate
constexpr uint8_t REG_CTRL7 = 0x08;     // which sensors run
constexpr uint8_t REG_AX_L = 0x35;      // x, y, z, low byte first
constexpr uint8_t REG_RESET = 0x60;

constexpr uint8_t CTRL1_AUTO_INCREMENT = 0x40;  // little-endian, internal clock on
constexpr uint8_t CTRL2_4G_21HZ_LOW_POWER = 0x10 | 0x0D;
constexpr uint8_t CTRL7_ACCEL_ON = 0x01;        // gyroscope stays off
constexpr float LSB_PER_G = 8192.0f;            // at +/-4 g

uint8_t addr = 0;
bool found = false;

bool readRegs(uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
}  // namespace

namespace imu {

bool begin() {
  found = false;
  uint8_t id = 0;
  for (uint8_t a : ADDRS) {
    addr = a;
    if (readRegs(REG_WHO_AM_I, &id, 1) && id == 0x05) {
      found = true;
      break;
    }
  }
  if (!found) {
    Serial.println("imu: QMI8658 not found");
    return false;
  }
  writeReg(REG_RESET, 0xB0);
  delay(15);
  found = writeReg(REG_CTRL1, CTRL1_AUTO_INCREMENT) && writeReg(REG_CTRL2, CTRL2_4G_21HZ_LOW_POWER) &&
          writeReg(REG_CTRL7, CTRL7_ACCEL_ON);
  Serial.printf("imu: QMI8658 at 0x%02X (%s)\n", addr, found ? "ok" : "setup failed");
  return found;
}

bool present() { return found; }

bool read(Accel &out) {
  uint8_t d[6];
  if (!found || !readRegs(REG_AX_L, d, sizeof d)) return false;
  out.x = (int16_t)(d[0] | d[1] << 8) / LSB_PER_G;
  out.y = (int16_t)(d[2] | d[3] << 8) / LSB_PER_G;
  out.z = (int16_t)(d[4] | d[5] << 8) / LSB_PER_G;
  return true;
}

}  // namespace imu
