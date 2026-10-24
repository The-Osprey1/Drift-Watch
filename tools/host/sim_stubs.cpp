// Computer stand-ins for the hardware layer, touch chip, settings storage and
// Wi-Fi, driven by the variables in sim.h.
#include <stdarg.h>
#include <Preferences.h>
#include "ble.h"
#include "hal.h"
#include "orient.h"
#include "ui.h"
#include "net.h"
#include "settings.h"
#include "sim.h"
#include "touch.h"

namespace sim {
uint32_t clockMs = 0;
bool fingerDown = false, bootDown = false, cold = true, clockSet = false;
bool touchFails = false;
int16_t fingerX = 120, fingerY = 120;
int batteryMv = 3950;
int64_t wallAtZeroMs = 1792788510000LL;  // 2026-10-23 20:48:30 UTC
uint32_t runUntil = 0;
std::vector<std::pair<uint32_t, uint8_t>> backlightLog;
int settingsSaves = 0;
std::map<std::string, FlashValue> flash;
bool imuPresent = true;
Accel accel = {0, 0, 1};  // lying flat, screen up
int dozes = 0, lowPowerDozes = 0, motionDozes = 0;
uint64_t dozedMs = 0;
uint32_t imuLineEdges[2] = {0, 0};
bool womArmed = false, womEvent = false;
Accel womRef = {0, 0, 1};
float womThresholdG = 0.08f;
uint32_t womLatencyMs = 13;  // 8 ms (one sample at 128 Hz) + 5 ms to wake and leave light sleep
int womArms = 0, womExits = 0, womClears = 0, imuReads = 0;
bool womArmFails = false, womClearFails = false, womLineStuck = false;
int womSpurious = 0;
uint32_t firstImuReadAfterWake = 0;
bool touchStandby = false, touchStandbyOk = true;
int touchStandbyWrites = 0, touchReads = 0, touchRestarts = 0, touchResumeFailures = 0;
bool touchPulse = false, sleepFails = false;
bool panelTurnsClockwise = true;
uint8_t touchMount = 0;
std::vector<std::string> bleLines;
bool bleConnected = false, bleStarts = true;
int bleOpens = 0;

void rawTouch(int x, int y, int &rx, int &ry) {
  // Where the drawn point is on the panel: setRotation(r) turns the picture r quarter turns.
  int turn = ui::display().rotation, px = x, py = y;
  uint8_t quarter = panelTurnsClockwise ? 1 : 3;
  for (int i = 0; i < turn; i++) orient::apply(quarter, px, py, px, py);
  orient::apply(touchMount, px, py, rx, ry);
}
}  // namespace sim

HostSerial Serial;
uint32_t millis() { return sim::clockMs; }
uint32_t micros() { return sim::clockMs * 1000; }
void delay(uint32_t ms) { sim::clockMs += ms; }

namespace hal {
void begin() {}
uint32_t ms() { return sim::clockMs; }
int64_t nowMs() { return sim::clockSet ? sim::wallAtZeroMs + sim::clockMs : 5000 + (int64_t)sim::clockMs; }
time_t now() { return (time_t)(nowMs() / 1000); }
void idle(uint32_t ms) { sim::clockMs += ms; }
void backlight(uint8_t level) {
  if (sim::backlightLog.empty() || sim::backlightLog.back().second != level)
    sim::backlightLog.push_back({sim::clockMs, level});
}
int batteryMilliVolts() { return sim::batteryMv; }
bool bootButtonDown() { return sim::bootDown; }
bool coldBoot() { return sim::cold; }
uint32_t freeHeap() { return 200000; }
uint32_t freePsram() { return 1700000; }
void setTimeMs(int64_t ms) {
  sim::clockSet = true;
  sim::wallAtZeroMs = ms - sim::clockMs;
}
void setTime(time_t t) { setTimeMs((int64_t)t * 1000); }
void adjustMs(int64_t ms) {
  if (sim::clockSet) sim::wallAtZeroMs += ms;
}

namespace {
bool womFires() {
  if (!sim::womArmed) return false;
  const Accel &a = sim::accel, &r = sim::womRef;
  return fabsf(a.x - r.x) > sim::womThresholdG || fabsf(a.y - r.y) > sim::womThresholdG ||
         fabsf(a.z - r.z) > sim::womThresholdG;
}
}  // namespace

// Screen-off waits. The scripts change the finger, BOOT and the wrist between script steps, so
// a wait never runs past the end of the step (sim::runUntil): on the watch the change itself
// would end it. Lines already at their wake level when the wait starts are not armed, as on
// the watch (BOOT held, a stuck motion line); touch counts as its line pulsing.
uint8_t doze(uint32_t ms, const DozeArm &arm) {
  sim::dozes++;
  uint32_t left = (int32_t)(sim::runUntil - sim::clockMs) > 0 ? sim::runUntil - sim::clockMs : 10;
  uint32_t t = ms < left ? ms : left;
  uint32_t start = sim::clockMs;
  auto done = [&](uint8_t bits) {
    sim::dozedMs += sim::clockMs - start;
    return bits;
  };
  if (!arm.lowPower) {
    sim::clockMs += t;
    return done(WAKE_TIMER);
  }
  if (sim::sleepFails) {
    sim::clockMs += t;
    return done(WAKE_FAULT | WAKE_TIMER);
  }
  sim::lowPowerDozes++;
  bool motionLineHigh = sim::womLineStuck || sim::womEvent;
  bool motionArmed = arm.motion && !motionLineHigh;
  if (motionArmed) sim::motionDozes++;
  uint8_t bits = 0;
  if (sim::fingerDown || sim::touchPulse) bits |= WAKE_TOUCH;
  sim::touchPulse = false;
  if (motionArmed && sim::womSpurious > 0) {
    sim::womSpurious--;
    bits |= WAKE_MOTION;
  }
  if (motionArmed && womFires()) {
    sim::womEvent = true;
    bits |= WAKE_MOTION;
  }
  if (bits) {
    sim::clockMs += (bits & WAKE_MOTION) && !(bits & WAKE_TOUCH) ? (sim::womLatencyMs < t ? sim::womLatencyMs : t) : 1;
    return done(bits);
  }
  sim::clockMs += t;
  return done(WAKE_TIMER);
}
void log(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "    [%6u ms] ", sim::clockMs);
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
bool begin() {
  sim::womArmed = sim::womEvent = false;
  return sim::imuPresent;
}
bool present() { return sim::imuPresent; }
bool read(Accel &out) {
  if (!sim::imuPresent || sim::womArmed) return false;  // no samples while wake on motion is armed
  sim::imuReads++;
  if (!sim::firstImuReadAfterWake) sim::firstImuReadAfterWake = sim::clockMs;
  out = sim::accel;
  return true;
}
uint32_t lineEdges(int line) { return line >= 0 && line < 2 ? sim::imuLineEdges[line] : 0; }
Status enterWakeOnMotion(const WomConfig &cfg) {
  if (!sim::imuPresent) return Status::NotPresent;
  if (sim::womArmFails) return Status::CommandTimeout;
  sim::womArms++;
  sim::womArmed = true;
  sim::womEvent = false;
  sim::womRef = sim::accel;
  sim::womThresholdG = cfg.thresholdMg / 1000.0f;
  return Status::Ok;
}
Status readAndClearWakeEvent(bool &motion) {
  motion = false;
  if (!sim::imuPresent) return Status::NotPresent;
  if (sim::womClearFails) return Status::BusError;
  sim::womClears++;
  motion = sim::womEvent;
  sim::womEvent = false;
  return Status::Ok;
}
Status exitWakeOnMotion() {
  if (!sim::imuPresent) return Status::NotPresent;
  sim::womExits++;
  sim::womArmed = sim::womEvent = false;
  sim::firstImuReadAfterWake = 0;
  return Status::Ok;
}
bool wakeOnMotionArmed() { return sim::womArmed; }
bool motionLineHigh() { return sim::womLineStuck || sim::womEvent; }
}  // namespace imu

namespace touch {
bool begin() {
  sim::touchStandby = false;
  return true;
}
bool read(TouchSample &out) {
  out.down = false;
  sim::touchReads++;
  if (sim::touchFails) return false;
  out.down = sim::fingerDown;
  int rx, ry;
  sim::rawTouch(sim::fingerX, sim::fingerY, rx, ry);
  out.x = rx;
  out.y = ry;
  return true;
}
bool standbySupported() { return sim::touchStandbyOk; }
bool setAutoStandby(bool enabled) {
  sim::touchStandbyWrites++;
  if (enabled && !sim::touchStandbyOk) return false;
  if (!enabled && sim::touchResumeFailures > 0) {
    sim::touchResumeFailures--;
    return false;
  }
  sim::touchStandby = enabled;
  return true;
}
bool restart() {
  sim::touchRestarts++;
  return begin();
}
}  // namespace touch

// Bluetooth: open and close only change state; scripts push lines into sim::bleLines.
namespace {
ble::State bleState = ble::State::Off;
uint32_t bleOpenedAt = 0, bleLastLine = 0;
std::string bleStatus;
}
namespace ble {
void open() {
  sim::bleOpens++;
  bleOpenedAt = sim::clockMs;
  bleLastLine = 0;
  if (bleState == State::Waiting || bleState == State::Connected) return;
  bleState = sim::bleStarts ? State::Waiting : State::Failed;
}
void close() {
  bleState = State::Off;
  sim::bleConnected = false;
}
void poll() {
  if (bleState != State::Waiting && bleState != State::Connected) return;
  bleState = sim::bleConnected ? State::Connected : State::Waiting;
  if (!secondsLeft()) close();
}
State state() { return bleState; }
bool on() { return bleState == State::Waiting || bleState == State::Connected; }
uint32_t secondsLeft() {
  if (!on()) return 0;
  uint32_t used = sim::clockMs - (bleLastLine ? bleLastLine : bleOpenedAt);
  return used >= WINDOW_MS ? 0 : (WINDOW_MS - used + 999) / 1000;
}
const char *name() { return "Drift-1A2B"; }
bool nextLine(char *out, size_t n) {
  if (!on() || sim::bleLines.empty()) return false;
  snprintf(out, n, "%s", sim::bleLines.front().c_str());
  sim::bleLines.erase(sim::bleLines.begin());
  bleLastLine = sim::clockMs;
  return true;
}
void setStatus(const char *text) { bleStatus = text; }
}  // namespace ble
const char *simBleStatus() { return bleStatus.c_str(); }
