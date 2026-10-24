#include "imu.h"
#include <Arduino.h>
#include <Wire.h>
#include "board.h"
#include "qmi8658.h"

namespace {
// The address depends on how the board wires the SA0 pin, so both are tried.
constexpr uint8_t ADDRS[] = {0x6B, 0x6A};

uint8_t addr = 0;
bool found = false, armed = false;
volatile uint32_t edges[2] = {0, 0};
// Only counts: no bus traffic from an interrupt, the loop does all of that.
void IRAM_ATTR edge1() { edges[0]++; }
void IRAM_ATTR edge2() { edges[1]++; }

class WireBus : public qmi8658::Bus {
 public:
  bool read(uint8_t reg, uint8_t *buf, size_t len) override {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr, (uint8_t)len) != len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
  }
  bool write(uint8_t reg, uint8_t val) override {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
  }
  void waitMs(uint32_t ms) override { delay(ms); }
} bus;

imu::Status toStatus(qmi8658::Result r) {
  switch (r) {
    case qmi8658::Result::Ok: return imu::Status::Ok;
    case qmi8658::Result::BusError: return imu::Status::BusError;
    case qmi8658::Result::CommandTimeout: return imu::Status::CommandTimeout;
    case qmi8658::Result::AckTimeout: return imu::Status::AckTimeout;
  }
  return imu::Status::BusError;
}
}  // namespace

namespace imu {

const char *statusName(Status s) {
  switch (s) {
    case Status::Ok: return "ok";
    case Status::NotPresent: return "no sensor";
    case Status::BusError: return "no answer on I2C";
    case Status::CommandTimeout: return "command not done";
    case Status::AckTimeout: return "command not acknowledged";
  }
  return "?";
}

bool begin() {
  found = armed = false;
  uint8_t id = 0, rev = 0;
  for (uint8_t a : ADDRS) {
    addr = a;
    if (bus.read(qmi8658::REG_WHO_AM_I, &id, 1) && id == qmi8658::WHO_AM_I_VALUE) {
      found = true;
      break;
    }
  }
  if (!found) {
    Serial.println("imu: QMI8658 not found");
    return false;
  }
  bus.write(qmi8658::REG_RESET, 0xB0);
  delay(15);
  bus.read(qmi8658::REG_REVISION, &rev, 1);
  found = bus.write(qmi8658::REG_CTRL1, qmi8658::CTRL1_AUTO_INCREMENT | qmi8658::CTRL1_INT2_ENABLE) &&
          qmi8658::startNormal(bus) == qmi8658::Result::Ok;
  pinMode(PIN_IMU_INT1, INPUT);
  pinMode(PIN_IMU_INT2, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_IMU_INT1), edge1, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_IMU_INT2), edge2, CHANGE);
  Serial.printf("imu: QMI8658 at 0x%02X, revision 0x%02X (%s)\n", addr, rev, found ? "ok" : "setup failed");
  return found;
}

bool present() { return found; }

bool read(Accel &out) {
  uint8_t d[6];
  if (!found || armed || !bus.read(qmi8658::REG_AX_L, d, sizeof d)) return false;
  out.x = (int16_t)(d[0] | d[1] << 8) / qmi8658::LSB_PER_G;
  out.y = (int16_t)(d[2] | d[3] << 8) / qmi8658::LSB_PER_G;
  out.z = (int16_t)(d[4] | d[5] << 8) / qmi8658::LSB_PER_G;
  return true;
}

uint32_t lineEdges(int line) { return line >= 0 && line < 2 ? edges[line] : 0; }

Status enterWakeOnMotion(const WomConfig &cfg) {
  if (!found) return Status::NotPresent;
  qmi8658::WomConfig c;
  c.thresholdMg = cfg.thresholdMg;
  c.blankingSamples = cfg.blankingSamples;
  Status s = toStatus(qmi8658::enterWakeOnMotion(bus, c));
  armed = s == Status::Ok;
  return s;
}

Status readAndClearWakeEvent(bool &motion) {
  motion = false;
  if (!found) return Status::NotPresent;
  return toStatus(qmi8658::readAndClearWakeEvent(bus, motion));
}

Status exitWakeOnMotion() {
  if (!found) return Status::NotPresent;
  armed = false;  // even on failure: the sequence ends by asking for normal sampling
  return toStatus(qmi8658::exitWakeOnMotion(bus));
}

bool wakeOnMotionArmed() { return armed; }
bool motionLineHigh() { return digitalRead(PIN_IMU_INT2) == HIGH; }

}  // namespace imu
