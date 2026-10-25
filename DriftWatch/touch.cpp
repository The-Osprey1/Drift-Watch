#include "touch.h"
#include <Arduino.h>
#include <Wire.h>
#include "board.h"

namespace {
constexpr uint8_t ADDR = 0x15;
constexpr uint8_t REG_DATA = 0x01;        // gesture, fingers, x high, x low, y high, y low
constexpr uint8_t REG_CHIP_ID = 0xA7;
constexpr uint8_t REG_FW_VERSION = 0xA9;
constexpr uint8_t REG_DISABLE_SLEEP = 0xFE;  // 1 = never doze off, 0 = automatic standby allowed
// Chip IDs of the CST816 family (S, T, D), which all have automatic standby. Anything else is
// left always on.
constexpr uint8_t STANDBY_IDS[] = {0xB4, 0xB5, 0xB6};

uint8_t chipId = 0;

bool readRegs(uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(ADDR, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

void resetPulse() {
  digitalWrite(PIN_TP_RST, LOW);
  delay(10);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(60);
}
}  // namespace

namespace touch {

bool begin() {
  pinMode(PIN_TP_INT, INPUT_PULLUP);
  pinMode(PIN_TP_RST, OUTPUT);
  resetPulse();
  uint8_t fw = 0;
  bool ok = readRegs(REG_CHIP_ID, &chipId, 1);
  readRegs(REG_FW_VERSION, &fw, 1);
  // Without this the chip dozes off after a few seconds and stops answering.
  bool awakeSet = writeReg(REG_DISABLE_SLEEP, 0x01);
  Serial.printf("touch: chip id 0x%02X, firmware 0x%02X (%s%s)\n", chipId, fw, ok ? "ok" : "no reply",
                awakeSet ? "" : ", keep-awake not written");
  return ok;
}

bool read(TouchSample &out) {
  uint8_t d[6];
  out.down = false;
  if (!readRegs(REG_DATA, d, sizeof d)) return false;
  out.down = (d[1] & 0x0F) > 0;
  int x = ((d[2] & 0x0F) << 8) | d[3];
  int y = ((d[4] & 0x0F) << 8) | d[5];
  out.x = x;
  out.y = y;
  return true;
}

bool standbySupported() {
  for (uint8_t id : STANDBY_IDS)
    if (chipId == id) return true;
  return false;
}

bool setAutoStandby(bool enabled) {
  if (enabled && !standbySupported()) return false;
  // A chip already in standby can miss the first transfer while it wakes, so try a few times.
  for (int i = 0; i < 3; i++) {
    if (writeReg(REG_DISABLE_SLEEP, enabled ? 0x00 : 0x01)) return true;
    delay(2);
  }
  return false;
}

bool restart() { return begin(); }

}  // namespace touch
