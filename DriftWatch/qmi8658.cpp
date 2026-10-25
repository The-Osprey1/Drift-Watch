#include "qmi8658.h"

namespace qmi8658 {
namespace {
bool readReg(Bus &bus, uint8_t reg, uint8_t &val) { return bus.read(reg, &val, 1); }

// Sets and clears bits in one register, leaving the others as the chip has them.
bool update(Bus &bus, uint8_t reg, uint8_t set, uint8_t clear) {
  uint8_t v;
  if (!readReg(bus, reg, v)) return false;
  return bus.write(reg, (uint8_t)((v & ~clear) | set));
}

// Polls STATUSINT until the done bit reads `want`, every millisecond up to `timeoutMs`.
Result waitDone(Bus &bus, bool want, uint32_t timeoutMs, Result onTimeout) {
  for (uint32_t waited = 0;; waited++) {
    uint8_t s;
    if (!readReg(bus, REG_STATUSINT, s)) return Result::BusError;
    if (((s & STATUSINT_CMD_DONE) != 0) == want) return Result::Ok;
    if (waited >= timeoutMs) return onTimeout;
    bus.waitMs(1);
  }
}

// Sensors off, so the Wake on Motion setting is written to an idle chip.
bool sensorsOff(Bus &bus) { return update(bus, REG_CTRL7, 0, CTRL7_ACCEL_ON | CTRL7_GYRO_ON); }
}  // namespace

const char *resultName(Result r) {
  switch (r) {
    case Result::Ok: return "ok";
    case Result::BusError: return "no answer on I2C";
    case Result::CommandTimeout: return "command not done";
    case Result::AckTimeout: return "command not acknowledged";
  }
  return "?";
}

Result command(Bus &bus, uint8_t cmd, uint32_t timeoutMs) {
  if (!bus.write(REG_CTRL9, cmd)) return Result::BusError;
  Result r = waitDone(bus, true, timeoutMs, Result::CommandTimeout);
  if (r != Result::Ok) return r;
  if (!bus.write(REG_CTRL9, CMD_ACK)) return Result::BusError;
  return waitDone(bus, false, timeoutMs, Result::AckTimeout);
}

Result startNormal(Bus &bus) {
  // INT1 stays undriven (GPIO 4 also switches the MOSFET1 pads); data ready goes to INT2.
  if (!update(bus, REG_CTRL1, CTRL1_AUTO_INCREMENT | CTRL1_INT2_ENABLE, CTRL1_INT1_ENABLE)) return Result::BusError;
  if (!bus.write(REG_CTRL2, CTRL2_NORMAL)) return Result::BusError;
  if (!update(bus, REG_CTRL7, CTRL7_ACCEL_ON, CTRL7_GYRO_ON | CTRL7_DRDY_OFF)) return Result::BusError;
  return Result::Ok;
}

Result enterWakeOnMotion(Bus &bus, const WomConfig &cfg) {
  Result r = Result::BusError;
  bool ok = sensorsOff(bus) && bus.write(REG_CTRL2, cfg.ctrl2) && bus.write(REG_CAL1_L, cfg.thresholdMg) &&
            bus.write(REG_CAL1_H, (uint8_t)(CAL1H_INT2_START_LOW | (cfg.blankingSamples & 0x3F)));
  if (ok) r = command(bus, CMD_WRITE_WOM);
  if (r == Result::Ok) {
    // Accelerometer back on with data ready off, or INT2 would also pulse at the sample rate
    // and wake the watch 21 times a second. INT2 must stay enabled: it carries the motion.
    bool armed = update(bus, REG_CTRL1, CTRL1_INT2_ENABLE, CTRL1_INT1_ENABLE) &&
                 update(bus, REG_CTRL7, CTRL7_ACCEL_ON | CTRL7_DRDY_OFF, CTRL7_GYRO_ON);
    uint8_t stale;
    if (armed && readReg(bus, REG_STATUS1, stale)) return Result::Ok;  // starts with no old event
    r = Result::BusError;
  }
  exitWakeOnMotion(bus);  // best effort: back to the known polling setup
  return r;
}

Result readAndClearWakeEvent(Bus &bus, bool &motion) {
  uint8_t s = 0;
  motion = false;
  if (!readReg(bus, REG_STATUS1, s)) return Result::BusError;
  motion = (s & STATUS1_WOM) != 0;
  return Result::Ok;
}

Result exitWakeOnMotion(Bus &bus) {
  // The documented way out: sensors off, a zero threshold written through the same command,
  // then normal sampling again.
  if (!sensorsOff(bus) || !bus.write(REG_CAL1_L, 0) || !bus.write(REG_CAL1_H, 0)) {
    startNormal(bus);
    return Result::BusError;
  }
  Result r = command(bus, CMD_WRITE_WOM);
  Result n = startNormal(bus);
  uint8_t stale;
  readReg(bus, REG_STATUS1, stale);  // drop an event that fired while turning it off
  return r != Result::Ok ? r : n;
}
}  // namespace qmi8658
