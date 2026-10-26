// Tests the motion sensor's register sequences (the real DriftWatch/qmi8658.cpp, against a
// register model of the QMI8658) and the screen-off power controller (the real power.cpp,
// with a scripted clock and wake sources). Exit code 1 on any failed check.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>
#include "hal.h"
#include "imu.h"
#include "power.h"
#include "qmi8658.h"

namespace {
int failures = 0, checks = 0;
bool verbose = false;

#define CHECK(cond, ...)                         \
  do {                                           \
    checks++;                                    \
    if (!(cond)) {                               \
      failures++;                                \
      printf("  FAIL line %d: ", __LINE__);      \
      printf(__VA_ARGS__);                       \
      printf("\n");                              \
    }                                            \
  } while (0)

// ---- Register model ----------------------------------------------------------------------
// Behaves as the datasheet describes the parts the firmware uses: CTRL9 commands set
// STATUSINT bit 7 after a few polls, the acknowledge clears it, the Wake on Motion setting is
// latched from CAL1 when command 0x08 runs, motion sets STATUS1 bit 2 and raises INT2, and
// reading STATUS1 clears both. Data ready pulses INT2 while it is enabled.
struct Op {
  bool write;
  uint8_t reg, val;
};

class FakeQmi : public qmi8658::Bus {
 public:
  uint8_t regs[256] = {};
  std::vector<Op> ops;
  int failAt = -1;          // this bus call (counted from 0) gets no answer
  int busCalls = 0;
  int doneAfter = 2, ackAfter = 1;  // STATUSINT polls before done is set / cleared
  bool neverDone = false, neverAck = false;
  int statusPolls = 0;
  uint32_t waitedMs = 0;
  // The Wake on Motion setting the chip latched with the last 0x08 command.
  uint8_t womThreshold = 0, womCal1h = 0, ctrl7AtCommand = 0, ctrl2AtCommand = 0;
  int commands = 0;
  bool int2 = false;

  FakeQmi() { reset(); }
  void reset() {
    memset(regs, 0, sizeof regs);
    regs[qmi8658::REG_WHO_AM_I] = 0x05;
    regs[qmi8658::REG_CTRL1] = qmi8658::CTRL1_AUTO_INCREMENT | qmi8658::CTRL1_INT2_ENABLE;
    regs[qmi8658::REG_CTRL2] = qmi8658::CTRL2_NORMAL;
    regs[qmi8658::REG_CTRL7] = qmi8658::CTRL7_ACCEL_ON;
    ops.clear();
    failAt = -1;
    busCalls = statusPolls = commands = 0;
    waitedMs = 0;
    womThreshold = womCal1h = 0;
    int2 = false;
    neverDone = neverAck = false;
    pendingDone = pendingAck = -1;
  }
  bool womActive() const { return womThreshold > 0 && (regs[qmi8658::REG_CTRL7] & qmi8658::CTRL7_ACCEL_ON); }
  bool drdyOnInt2() const {
    return (regs[qmi8658::REG_CTRL1] & qmi8658::CTRL1_INT2_ENABLE) && !(regs[qmi8658::REG_CTRL7] & qmi8658::CTRL7_DRDY_OFF) &&
           (regs[qmi8658::REG_CTRL7] & qmi8658::CTRL7_ACCEL_ON);
  }
  bool sampling() const {
    return (regs[qmi8658::REG_CTRL7] & qmi8658::CTRL7_ACCEL_ON) && !womActive() &&
           regs[qmi8658::REG_CTRL2] == qmi8658::CTRL2_NORMAL;
  }
  void motion() {
    if (!womActive()) return;
    regs[qmi8658::REG_STATUS1] |= qmi8658::STATUS1_WOM;
    int2 = true;
  }

  bool read(uint8_t reg, uint8_t *buf, size_t len) override {
    if (busCalls++ == failAt) return false;
    for (size_t i = 0; i < len; i++) {
      uint8_t r = (uint8_t)(reg + i);
      if (r == qmi8658::REG_STATUSINT) {
        statusPolls++;
        if (pendingDone > 0 && --pendingDone == 0) regs[r] |= qmi8658::STATUSINT_CMD_DONE;
        if (pendingAck > 0 && --pendingAck == 0) regs[r] &= ~qmi8658::STATUSINT_CMD_DONE;
      }
      buf[i] = regs[r];
      ops.push_back({false, r, buf[i]});
      if (r == qmi8658::REG_STATUS1) {
        regs[r] &= ~qmi8658::STATUS1_WOM;
        int2 = false;
      }
    }
    return true;
  }
  bool write(uint8_t reg, uint8_t val) override {
    if (busCalls++ == failAt) return false;
    ops.push_back({true, reg, val});
    if (reg == qmi8658::REG_CTRL9) {
      if (val == qmi8658::CMD_WRITE_WOM) {
        commands++;
        womThreshold = regs[qmi8658::REG_CAL1_L];
        womCal1h = regs[qmi8658::REG_CAL1_H];
        ctrl7AtCommand = regs[qmi8658::REG_CTRL7];
        ctrl2AtCommand = regs[qmi8658::REG_CTRL2];
        pendingDone = neverDone ? -1 : doneAfter;
      } else if (val == qmi8658::CMD_ACK && (regs[qmi8658::REG_STATUSINT] & qmi8658::STATUSINT_CMD_DONE)) {
        pendingAck = neverAck ? -1 : ackAfter;
      }
    }
    regs[reg] = val;
    return true;
  }
  void waitMs(uint32_t ms) override { waitedMs += ms; }

  // Index of the first write of `val` to `reg` (any value if val < 0), or -1.
  int firstWrite(uint8_t reg, int val = -1, int from = 0) const {
    for (size_t i = from; i < ops.size(); i++)
      if (ops[i].write && ops[i].reg == reg && (val < 0 || ops[i].val == val)) return (int)i;
    return -1;
  }
  bool everWrote(uint8_t reg, uint8_t mask) const {
    for (const Op &o : ops)
      if (o.write && o.reg == reg && (o.val & mask)) return true;
    return false;
  }

 private:
  int pendingDone = -1, pendingAck = -1;
};

void registerSequences() {
  using namespace qmi8658;
  printf("Wake on motion: the arming sequence\n");
  FakeQmi q;
  q.regs[REG_CTRL7] |= 0x40;  // a bit owned by another feature: must survive
  WomConfig cfg;
  Result r = enterWakeOnMotion(q, cfg);
  CHECK(r == Result::Ok, "arming succeeds (%s)", resultName(r));
  int off = q.firstWrite(REG_CTRL7), ctrl2 = q.firstWrite(REG_CTRL2), thr = q.firstWrite(REG_CAL1_L),
      cal1h = q.firstWrite(REG_CAL1_H), cmd = q.firstWrite(REG_CTRL9, CMD_WRITE_WOM), ack = q.firstWrite(REG_CTRL9, CMD_ACK);
  CHECK(off >= 0 && !(q.ops[off].val & (CTRL7_ACCEL_ON | CTRL7_GYRO_ON)), "sensors are turned off first");
  CHECK(off < ctrl2 && ctrl2 < cmd && thr < cmd && cal1h < cmd, "range, rate and CAL1 are written before the command");
  CHECK(cmd < ack, "the command is acknowledged after it is done");
  int on = q.firstWrite(REG_CTRL7, -1, ack);
  CHECK(on > ack && (q.ops[on].val & CTRL7_ACCEL_ON), "the accelerometer comes back on after the handshake");
  CHECK(!(q.ctrl7AtCommand & (CTRL7_ACCEL_ON | CTRL7_GYRO_ON)), "the setting is latched with the sensors off");
  CHECK(q.womThreshold == 80, "threshold 80 mg (got %d)", q.womThreshold);
  CHECK((q.womCal1h & 0xC0) == CAL1H_INT2_START_LOW, "routed to INT2, starting low (CAL1_H 0x%02X)", q.womCal1h);
  CHECK((q.womCal1h & 0x3F) == 4, "with 4 samples of blanking (CAL1_H 0x%02X)", q.womCal1h);
  CHECK(q.ctrl2AtCommand == cfg.ctrl2, "range and rate set for wake on motion (CTRL2 0x%02X)", q.ctrl2AtCommand);
  CHECK(q.womActive(), "wake on motion is running");
  CHECK(!q.drdyOnInt2(), "data ready is off, so INT2 only moves for motion");
  CHECK(q.regs[REG_CTRL1] & CTRL1_INT2_ENABLE, "INT2 stays enabled");
  CHECK(!q.everWrote(REG_CTRL1, CTRL1_INT1_ENABLE), "INT1 is never driven (GPIO 4 is also MOSFET1)");
  CHECK(!q.everWrote(REG_CTRL7, CTRL7_GYRO_ON), "the gyroscope is never turned on");
  CHECK(q.regs[REG_CTRL7] & 0x40, "other CTRL7 bits are kept");
  CHECK(q.regs[REG_CTRL1] & CTRL1_AUTO_INCREMENT, "CTRL1 auto-increment is kept");
  CHECK(q.statusPolls <= 6, "the handshake took %d status polls", q.statusPolls);

  printf("Wake on motion: events and clearing\n");
  bool motion = true;
  r = readAndClearWakeEvent(q, motion);
  CHECK(r == Result::Ok && !motion, "nothing has moved yet");
  q.motion();
  CHECK(q.int2, "motion raises INT2");
  r = readAndClearWakeEvent(q, motion);
  CHECK(r == Result::Ok && motion && !q.int2, "reading STATUS1 reports the motion and clears the line");
  readAndClearWakeEvent(q, motion);
  CHECK(!motion, "a second read shows it cleared");
  q.failAt = q.busCalls;
  r = readAndClearWakeEvent(q, motion);
  CHECK(r == Result::BusError && !motion, "a failed read is reported, never taken as motion (%s)", resultName(r));

  printf("Wake on motion: turning it off restores sampling\n");
  q.ops.clear();
  q.motion();
  r = exitWakeOnMotion(q);
  CHECK(r == Result::Ok, "leaving succeeds (%s)", resultName(r));
  CHECK(q.womThreshold == 0, "a zero threshold is written through the command (got %d)", q.womThreshold);
  CHECK(!(q.ctrl7AtCommand & (CTRL7_ACCEL_ON | CTRL7_GYRO_ON)), "with the sensors off");
  CHECK(q.sampling(), "normal sampling again (CTRL2 0x%02X, CTRL7 0x%02X)", q.regs[REG_CTRL2], q.regs[REG_CTRL7]);
  CHECK(q.drdyOnInt2(), "data ready is back on INT2, as in v0.10.1");
  CHECK(!q.int2 && !(q.regs[REG_STATUS1] & STATUS1_WOM), "an event from while it was turning off is dropped");
  CHECK(q.regs[REG_CTRL7] & 0x40, "other CTRL7 bits are still kept");

  printf("Wake on motion: an adjustable threshold\n");
  q.reset();
  cfg.thresholdMg = 120;
  cfg.blankingSamples = 0x3F;
  enterWakeOnMotion(q, cfg);
  CHECK(q.womThreshold == 120 && (q.womCal1h & 0x3F) == 0x3F && (q.womCal1h & 0xC0) == 0x40,
        "120 mg and the longest blanking (CAL1_H 0x%02X)", q.womCal1h);

  printf("Wake on motion: handshake timeouts are bounded\n");
  q.reset();
  q.neverDone = true;
  r = enterWakeOnMotion(q, WomConfig{});
  CHECK(r == Result::CommandTimeout, "a command that never completes times out (%s)", resultName(r));
  CHECK(q.waitedMs <= 2 * CMD_TIMEOUT_MS + 4, "within its time limit (waited %u ms)", q.waitedMs);
  CHECK(q.sampling() || q.regs[REG_CTRL7] & CTRL7_ACCEL_ON, "and sampling is restored");
  CHECK(!(q.regs[REG_CTRL7] & CTRL7_DRDY_OFF), "with data ready back on");
  q.reset();
  q.neverAck = true;
  r = enterWakeOnMotion(q, WomConfig{});
  CHECK(r == Result::AckTimeout, "a command whose done bit never clears times out too (%s)", resultName(r));
  CHECK(q.regs[REG_CTRL7] & CTRL7_ACCEL_ON, "and the accelerometer is on again");

  printf("Wake on motion: a missed I2C transfer at every step\n");
  q.reset();
  enterWakeOnMotion(q, WomConfig{});
  int steps = q.busCalls;
  int bad = 0;
  for (int k = 0; k < steps; k++) {
    q.reset();
    q.failAt = k;
    r = enterWakeOnMotion(q, WomConfig{});
    bool restored = (q.regs[REG_CTRL7] & CTRL7_ACCEL_ON) && !(q.regs[REG_CTRL7] & CTRL7_DRDY_OFF) &&
                    q.regs[REG_CTRL2] == CTRL2_NORMAL;
    bool claimed = r == Result::Ok;
    // The only acceptable outcomes: armed and reported Ok, or not armed, reported, and sampling.
    if (claimed ? !q.womActive() : !restored) {
      bad++;
      printf("    transfer %d: result %s, CTRL7 0x%02X, CTRL2 0x%02X\n", k, resultName(r), q.regs[REG_CTRL7], q.regs[REG_CTRL2]);
    }
    if (q.everWrote(REG_CTRL7, CTRL7_GYRO_ON) || q.everWrote(REG_CTRL1, CTRL1_INT1_ENABLE)) bad++;
  }
  CHECK(bad == 0, "each of %d failure points is reported and leaves the sensor sampling (%d bad)", steps, bad);
  q.reset();
  enterWakeOnMotion(q, WomConfig{});
  int armedAt = q.busCalls;
  exitWakeOnMotion(q);
  steps = q.busCalls - armedAt;
  bad = 0;
  for (int k = 0; k < steps; k++) {
    q.reset();
    enterWakeOnMotion(q, WomConfig{});
    q.failAt = q.busCalls + k;
    r = exitWakeOnMotion(q);
    if (r == Result::Ok && !q.sampling()) bad++;
    if (r != Result::Ok) {
      q.failAt = -1;
      if (exitWakeOnMotion(q) != Result::Ok || !q.sampling()) bad++;  // a retry recovers
    }
  }
  CHECK(bad == 0, "leaving: each of %d failure points is reported, and a retry restores sampling (%d bad)", steps, bad);

  printf("Wake on motion: 100 arm, motion, clear and leave cycles\n");
  q.reset();
  bad = 0;
  for (int i = 0; i < 100; i++) {
    if (enterWakeOnMotion(q, WomConfig{}) != Result::Ok || !q.womActive() || q.drdyOnInt2()) bad++;
    q.motion();
    bool m = false;
    if (readAndClearWakeEvent(q, m) != Result::Ok || !m || q.int2) bad++;
    if (exitWakeOnMotion(q) != Result::Ok || !q.sampling() || !q.drdyOnInt2()) bad++;
  }
  CHECK(bad == 0, "every cycle arms, reports, clears and restores (%d bad)", bad);
  CHECK(q.commands == 200, "two commands a cycle (%d)", q.commands);
}

// ---- Power controller --------------------------------------------------------------------
// hal and imu for power.cpp: a scripted clock and wake sources, and imu calls that run the real
// register sequences against the model above, the way imu_qmi8658.cpp does on the watch.
uint32_t clockMs = 1000;
struct Doze {
  uint32_t asked;
  bool lowPower, motion, serial;
};
std::vector<Doze> dozes;
std::deque<std::pair<uint32_t, uint8_t>> events;  // (after ms, bits) for the next waits
FakeQmi chip;
bool imuArmed = false, sensorPresent = true;
bool imuClearFails = false, lineStuck = false, sleepSetupFails = false;
int samples = 0;
uint32_t firstSampleAt = 0;
int idles = 0;

void resetWorld() {
  clockMs = 1000;
  dozes.clear();
  events.clear();
  chip.reset();
  imuArmed = false;
  sensorPresent = true;
  imuClearFails = lineStuck = sleepSetupFails = false;
  samples = 0;
  firstSampleAt = 0;
  idles = 0;
  power::options = power::Options{};
  power::options.wom = true;
  power::options.lightSleep = true;
}
}  // namespace

namespace hal {
uint32_t ms() { return clockMs; }
void idle(uint32_t ms) {
  idles++;
  clockMs += ms;
}
uint8_t doze(uint32_t ms, const DozeArm &arm) {
  dozes.push_back({ms, arm.lowPower, arm.motion, arm.serial});
  if (sleepSetupFails && arm.lowPower) {
    clockMs += ms;
    return WAKE_FAULT | WAKE_TIMER;
  }
  if (!events.empty() && events.front().first < ms) {
    auto e = events.front();
    events.pop_front();
    if ((e.second & WAKE_MOTION) && !(arm.motion)) {  // not armed: that line cannot wake it
      clockMs += ms;
      return WAKE_TIMER;
    }
    clockMs += e.first;
    if (e.second & WAKE_MOTION) chip.motion();
    return e.second;
  }
  if (!events.empty()) events.front().first -= ms;
  clockMs += ms;
  return WAKE_TIMER;
}
void log(const char *fmt, ...) {
  if (!verbose) return;
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "    [%6u ms] ", clockMs);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}
}  // namespace hal

namespace imu {
const char *statusName(Status s) {
  static const char *names[] = {"ok", "no sensor", "no answer on I2C", "command not done", "command not acknowledged"};
  return names[(int)s];
}
Status toStatus(qmi8658::Result r) {
  return r == qmi8658::Result::Ok ? Status::Ok : r == qmi8658::Result::BusError ? Status::BusError
         : r == qmi8658::Result::CommandTimeout ? Status::CommandTimeout : Status::AckTimeout;
}
bool read(Accel &out) {
  if (!sensorPresent || imuArmed) return false;
  samples++;
  if (!firstSampleAt) firstSampleAt = clockMs;
  out = {0, -1, 0};
  return true;
}
bool readTemp(float &celsius) {
  if (!sensorPresent || imuArmed) return false;
  celsius = 23.5f;
  return true;
}
Status enterWakeOnMotion(const WomConfig &cfg) {
  if (!sensorPresent) return Status::NotPresent;
  qmi8658::WomConfig c;
  c.thresholdMg = cfg.thresholdMg;
  c.blankingSamples = cfg.blankingSamples;
  Status s = toStatus(qmi8658::enterWakeOnMotion(chip, c));
  imuArmed = s == Status::Ok;
  return s;
}
Status readAndClearWakeEvent(bool &motion) {
  motion = false;
  if (imuClearFails) return Status::BusError;
  return toStatus(qmi8658::readAndClearWakeEvent(chip, motion));
}
Status exitWakeOnMotion() {
  imuArmed = false;
  firstSampleAt = 0;
  return toStatus(qmi8658::exitWakeOnMotion(chip));
}
bool wakeOnMotionArmed() { return imuArmed; }
bool motionLineHigh() { return lineStuck || chip.int2; }
}  // namespace imu

namespace {
int fed = 0, acceptAt = -1;
bool feed(uint32_t, const Accel &) { return ++fed == acceptAt; }

power::Inputs raiseOnly() {
  power::Inputs in;
  in.raise = true;
  return in;
}

void controller() {
  using power::State;
  using power::Wake;
  printf("Power: raise only arms wake on motion, and a motion only starts sampling\n");
  resetWorld();
  power::Controller c;
  c.screenOff(clockMs);
  events.push_back({20000, hal::WAKE_MOTION});
  fed = 0;
  acceptAt = -1;
  Wake w = c.step(raiseOnly(), clockMs + 60000, feed);
  CHECK(dozes.size() == 1 && dozes.back().motion && dozes.back().lowPower, "armed, then light sleep with INT2 armed");
  CHECK(chip.commands == 2, "the motion turns wake on motion off again (%d commands)", chip.commands);
  CHECK(dozes.back().asked == 30000, "the sleep lasts up to 30 s with nothing due (asked %u)", dozes.back().asked);
  CHECK(w == Wake::None && c.state() == State::MotionQualifying, "a motion interrupt does not wake the screen (state %s)",
        power::stateName(c.state()));
  CHECK(chip.sampling(), "sampling is back on for the decision");
  uint32_t wokeAt = clockMs;
  for (int i = 0; i < 30 && w == Wake::None && c.state() == State::MotionQualifying; i++) w = c.step(raiseOnly(), clockMs + 60000, feed);
  CHECK(w == Wake::None && fed >= 18 && fed <= 20, "with no gesture it samples for 1.5 s (%d samples)", fed);
  CHECK(firstSampleAt == 0 || firstSampleAt - wokeAt >= 50, "the first sample waits one sampling period after restoring");
  CHECK(c.state() == State::StandbyWom && imuArmed && c.stats().rejected == 1, "then arms again, screen still off (state %s)",
        power::stateName(c.state()));

  printf("Power: a recognised gesture wakes the screen\n");
  events.push_back({5000, hal::WAKE_MOTION});
  fed = 0;
  acceptAt = 3;
  w = Wake::None;
  for (int i = 0; i < 10 && w == Wake::None; i++) w = c.step(raiseOnly(), clockMs + 60000, feed);
  CHECK(w == Wake::Gesture && fed == 3 && c.stats().accepted == 1, "on the third fresh sample (%d)", fed);
  c.screenOn(clockMs);
  CHECK(chip.sampling() && !imuArmed, "the sensor is sampling for wrist-down sleep");

  printf("Power: touch and BOOT come first\n");
  c.screenOff(clockMs);
  events.push_back({1000, hal::WAKE_MOTION | hal::WAKE_TOUCH});
  w = c.step(raiseOnly(), clockMs + 60000, feed);
  CHECK(w == Wake::Touch && c.stats().qualifications == 2, "touch with motion: the touch wins and nothing qualifies");
  c.screenOn(clockMs);
  CHECK(!imuArmed && chip.sampling(), "and the sensor is back to sampling");
  c.screenOff(clockMs);
  events.push_back({1000, hal::WAKE_BUTTON});
  CHECK(c.step(raiseOnly(), clockMs + 60000, feed) == Wake::Button, "BOOT wakes it");
  c.screenOn(clockMs);
  c.screenOff(clockMs);
  events.push_back({1000, hal::WAKE_MOTION});
  c.step(raiseOnly(), clockMs + 60000, feed);
  events.push_back({20, hal::WAKE_TOUCH});
  fed = 0;
  acceptAt = -1;
  w = c.step(raiseOnly(), clockMs + 60000, feed);
  CHECK(w == Wake::Touch, "a touch while qualifying wakes it at once");
  c.screenOn(clockMs);

  printf("Power: sleeps end at the next deadline\n");
  resetWorld();
  power::Controller d;
  d.screenOff(clockMs);
  power::Inputs none;
  d.step(none, clockMs + 4200, feed);
  CHECK(d.state() == State::StandbyIdle && dozes.back().asked == 4200 && !dozes.back().motion,
        "gestures off: light sleep until the deadline, 4.2 s (asked %u)", dozes.back().asked);
  d.step(none, clockMs + 90000, feed);
  CHECK(dozes.back().asked == 30000, "never more than 30 s (asked %u)", dozes.back().asked);
  d.step(none, clockMs - 5, feed);
  CHECK(dozes.back().asked == 1, "a deadline already passed gives a 1 ms wait, not a long one");
  CHECK(samples == 0 && chip.commands == 0, "gestures off: the sensor is never read or armed");
  power::options.maxSleepMs = 12000;
  d.step(none, clockMs + 90000, feed);
  CHECK(dozes.back().asked == 12000, "the longest sleep is adjustable (asked %u)", dozes.back().asked);

  printf("Power: serial data wakes the processor, and holds off light sleep\n");
  resetWorld();
  power::Controller sc;
  sc.screenOff(clockMs);
  events.push_back({3000, hal::WAKE_SERIAL});
  Wake sw = sc.step(none, clockMs + 60000, feed);
  CHECK(dozes.back().serial && dozes.back().lowPower, "light sleep with the serial port armed");
  CHECK(sw == Wake::Serial && clockMs < 1000 + 4000, "bytes arriving end the sleep early as a serial wake");
  power::Inputs talking;
  talking.serialHold = true;
  sc.step(talking, clockMs + 60000, feed);
  CHECK(!dozes.back().lowPower && dozes.back().asked <= 50, "while the computer talks: plain short waits, no light sleep (asked %u)",
        dozes.back().asked);
  events.push_back({10, hal::WAKE_SERIAL | hal::WAKE_TOUCH});
  CHECK(sc.step(none, clockMs + 60000, feed) == Wake::Touch, "a touch at the same time still wins");

  printf("Power: twist modes keep polling\n");
  resetWorld();
  power::Controller t;
  t.screenOff(clockMs);
  power::Inputs both;
  both.raise = both.twist = true;
  for (int i = 0; i < 20; i++) t.step(both, clockMs + 60000, feed);
  CHECK(t.state() == State::StandbyPolling && t.fallback() == power::Fallback::TwistNeedsHistory && chip.commands == 0,
        "raise and twist: polling, wake on motion never armed (%s)", power::fallbackName(t.fallback()));
  CHECK(dozes.back().asked == 100 && samples >= 10, "a sample about every 80 ms between 100 ms sleeps (%d samples)", samples);
  power::Inputs twistOnly;
  twistOnly.twist = true;
  t.step(twistOnly, clockMs + 60000, feed);
  CHECK(t.state() == State::StandbyPolling && chip.commands == 0, "twist only: the same");
  t.step(raiseOnly(), clockMs + 60000, feed);
  CHECK(t.state() == State::StandbyWom && chip.commands == 1, "switching to raise only arms it");
  t.step(none, clockMs + 60000, feed);
  CHECK(t.state() == State::StandbyIdle && chip.commands == 2 && chip.sampling(), "night mode (no gestures) turns it off again");

  printf("Power: failures fall back to polling with a reason\n");
  resetWorld();
  chip.neverDone = true;
  power::Controller f;
  f.screenOff(clockMs);
  f.step(raiseOnly(), clockMs + 60000, feed);
  CHECK(f.state() == State::StandbyPolling && f.fallback() == power::Fallback::ArmFailed, "the handshake timed out: %s",
        power::fallbackName(f.fallback()));
  CHECK(chip.regs[qmi8658::REG_CTRL7] & qmi8658::CTRL7_ACCEL_ON, "and the sensor still samples, so polling works");
  chip.neverDone = false;
  for (int i = 0; i < 5; i++) f.step(raiseOnly(), clockMs + 60000, feed);
  CHECK(f.state() == State::StandbyPolling, "it stays polling for the rest of this screen-off period");
  f.screenOn(clockMs);
  f.screenOff(clockMs);
  f.step(raiseOnly(), clockMs + 60000, feed);
  CHECK(f.state() == State::StandbyWom, "the next screen-off tries again, and arms");
  f.screenOn(clockMs);
  chip.neverDone = true;
  for (int i = 0; i < 3; i++) {
    f.screenOff(clockMs);
    f.step(raiseOnly(), clockMs + 60000, feed);
    f.screenOn(clockMs);
  }
  chip.neverDone = false;
  f.screenOff(clockMs);
  f.step(raiseOnly(), clockMs + 60000, feed);
  CHECK(f.state() == State::StandbyPolling, "after 3 failed periods in a row it stays on polling");

  resetWorld();
  power::Controller g;
  g.screenOff(clockMs);
  imuClearFails = true;
  events.push_back({2000, hal::WAKE_MOTION});
  g.step(raiseOnly(), clockMs + 60000, feed);
  CHECK(g.fallback() == power::Fallback::ClearFailed && g.state() == State::StandbyPolling, "the event cannot be cleared: %s",
        power::fallbackName(g.fallback()));
  imuClearFails = false;

  resetWorld();
  power::Controller h;
  h.screenOff(clockMs);
  h.step(raiseOnly(), clockMs + 60000, feed);
  lineStuck = true;
  events.push_back({2000, hal::WAKE_MOTION});
  chip.womThreshold = 0;  // the line went high but the sensor has no event
  h.step(raiseOnly(), clockMs + 60000, feed);
  CHECK(h.fallback() == power::Fallback::StuckLine && h.state() == State::StandbyPolling, "INT2 stays high: %s",
        power::fallbackName(h.fallback()));
  lineStuck = false;

  resetWorld();
  power::Controller s;
  s.screenOff(clockMs);
  s.step(raiseOnly(), clockMs + 60000, feed);
  uint32_t t0 = clockMs;
  for (int i = 0; i < 5; i++) {
    events.push_back({100, hal::WAKE_MOTION});
    chip.womThreshold = 0;  // pulses with no motion event
    s.step(raiseOnly(), clockMs + 60000, feed);
    chip.womThreshold = 80;
  }
  CHECK(s.fallback() == power::Fallback::Storm && s.state() == State::StandbyPolling,
        "INT2 pulsing with no motion: a storm after %u ms, then polling (%s)", clockMs - t0, power::fallbackName(s.fallback()));
  CHECK(s.stats().spurious == 5 && s.stats().qualifications == 0, "none of those started a qualification");

  resetWorld();
  power::Controller l;
  l.screenOff(clockMs);
  sleepSetupFails = true;
  l.step(none, clockMs + 60000, feed);
  CHECK(l.fallback() == power::Fallback::SleepFailed, "light sleep cannot be set up: %s", power::fallbackName(l.fallback()));
  size_t n = dozes.size();
  l.step(raiseOnly(), clockMs + 60000, feed);
  CHECK(dozes.size() > n && !dozes.back().lowPower && dozes.back().asked <= 50 && chip.commands == 0,
        "from then on plain 50 ms waits and polling (asked %u)", dozes.back().asked);
  sleepSetupFails = false;

  printf("Power: rejected motions in a row poll for a while\n");
  resetWorld();
  power::Controller q;
  q.screenOff(clockMs);
  fed = 0;
  acceptAt = -1;
  for (int r = 0; r < 4; r++) {
    events.push_back({500, hal::WAKE_MOTION});
    do q.step(raiseOnly(), clockMs + 600000, feed);
    while (q.state() == State::MotionQualifying || q.stats().rejected == (uint32_t)r);
  }
  CHECK(q.stats().rejected == 4 && q.state() == State::StandbyPolling && q.fallback() == power::Fallback::Quiet,
        "4 rejected motions in 30 s: polling (%s)", power::fallbackName(q.fallback()));
  uint32_t quietStart = clockMs;
  while (clockMs - quietStart < 29000) q.step(raiseOnly(), clockMs + 600000, feed);
  CHECK(q.state() == State::StandbyPolling, "for 30 s");
  while (clockMs - quietStart < 31000) q.step(raiseOnly(), clockMs + 600000, feed);
  CHECK(q.state() == State::StandbyWom, "then wake on motion again (%s)", power::stateName(q.state()));

  printf("Power: with a radio on there is no light sleep\n");
  resetWorld();
  power::Controller rd;
  rd.screenOff(clockMs);
  rd.step(raiseOnly(), clockMs + 60000, feed);
  size_t before = dozes.size();
  for (int i = 0; i < 10; i++) rd.stepRadio(raiseOnly(), feed);
  CHECK(dozes.size() == before && idles == 10 && !imuArmed && rd.state() == State::StandbyPolling,
        "plain 50 ms waits, the sensor polled, wake on motion off");

  printf("Power: not built in, or no light sleep\n");
  resetWorld();
  power::options.wom = false;
  power::Controller nb;
  nb.screenOff(clockMs);
  nb.step(raiseOnly(), clockMs + 60000, feed);
  CHECK(nb.state() == State::StandbyPolling && nb.fallback() == power::Fallback::NotBuilt && chip.commands == 0,
        "DRIFT_WOM=0: polling, as v0.10.1 (%s)", power::fallbackName(nb.fallback()));
  resetWorld();
  power::options.lightSleep = false;
  power::Controller ns;
  ns.screenOff(clockMs);
  ns.step(none, clockMs + 60000, feed);
  CHECK(!dozes.back().lowPower && dozes.back().asked == 50, "DRIFT_LIGHT_SLEEP=0: plain 50 ms waits (asked %u)",
        dozes.back().asked);
  ns.step(raiseOnly(), clockMs + 60000, feed);
  CHECK(ns.fallback() == power::Fallback::NoLightSleep && chip.commands == 0, "and no wake on motion");
}
}  // namespace

int main(int argc, char **argv) {
  verbose = argc > 1 && !strcmp(argv[1], "-v");
  registerSequences();
  controller();
  printf("%d of %d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
