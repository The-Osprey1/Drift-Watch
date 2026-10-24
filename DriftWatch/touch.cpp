#include "touch.h"
#include <Arduino.h>
#include <Wire.h>
#include "board.h"

namespace {
constexpr uint8_t ADDR = 0x15;
constexpr uint8_t REG_DATA = 0x01;        // gesture, fingers, x high, x low, y high, y low
constexpr uint8_t REG_CHIP_ID = 0xA7;
constexpr uint8_t REG_DISABLE_SLEEP = 0xFE;

bool readRegs(uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(ADDR, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}
}  // namespace

namespace touch {

bool begin() {
  pinMode(PIN_TP_INT, INPUT_PULLUP);
  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW);
  delay(10);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(60);

  uint8_t id = 0;
  bool ok = readRegs(REG_CHIP_ID, &id, 1);
  // Without this the chip dozes off after a few seconds and stops answering.
  writeReg(REG_DISABLE_SLEEP, 0x01);
  Serial.printf("touch: chip id 0x%02X (%s)\n", id, ok ? "ok" : "no reply");
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

}  // namespace touch
