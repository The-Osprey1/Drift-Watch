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
time_t epochAtZero = 1792788510;  // 2026-10-23 20:48:30 UTC
std::vector<std::pair<uint32_t, uint8_t>> backlightLog;
int settingsSaves = 0;
std::map<std::string, FlashValue> flash;
net::Fail syncOutcome = net::Fail::None;
bool wifiSaved = true;
int syncsStarted = 0, syncsCancelled = 0;
char zone[64] = "Europe/London";
net::JoinWhy joinWhy = net::JoinWhy::NotFound;
bool imuPresent = true;
Accel accel = {0, 0, 1};  // lying flat, screen up
int dozes = 0, lowPowerDozes = 0;
uint32_t imuLineEdges[2] = {0, 0};
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
time_t now() { return sim::clockSet ? sim::epochAtZero + sim::clockMs / 1000 : 5 + sim::clockMs / 1000; }
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
void setTime(time_t t) {
  sim::clockSet = true;
  sim::epochAtZero = t - sim::clockMs / 1000;
}
int64_t nowMs() { return (int64_t)now() * 1000 + sim::clockMs % 1000; }
void setTimeMs(int64_t ms) { setTime((time_t)(ms / 1000)); }
void adjustMs(int64_t ms) {
  if (sim::clockSet) sim::epochAtZero += ms / 1000;
}
// Light sleep ends early on a touch or BOOT press; the scripts set those between loops,
// so a wait that starts with nothing pressed simply runs its full time.
WakeCause doze(uint32_t ms, bool lowPower) {
  sim::dozes++;
  if (lowPower) sim::lowPowerDozes++;
  sim::clockMs += ms;
  return WakeCause::Timer;
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
bool begin() { return sim::imuPresent; }
bool present() { return sim::imuPresent; }
bool read(Accel &out) {
  out = sim::accel;
  return sim::imuPresent;
}
uint32_t lineEdges(int line) { return line >= 0 && line < 2 ? sim::imuLineEdges[line] : 0; }
}  // namespace imu

namespace touch {
bool begin() { return true; }
bool read(TouchSample &out) {
  out.down = false;
  if (sim::touchFails) return false;
  out.down = sim::fingerDown;
  int rx, ry;
  sim::rawTouch(sim::fingerX, sim::fingerY, rx, ry);
  out.x = rx;
  out.y = ry;
  return true;
}
}  // namespace touch

// Wi-Fi: each sync step takes a fixed time, then the scripted outcome happens.
// Same rules as net.cpp: the running stage, the last result and the clock are separate.
namespace {
net::Sync syncNow = net::Sync::Idle;
net::Result result = net::Result::None;
uint16_t finished = 0;
net::Fail fail = net::Fail::None;
bool byHand = false;
net::Portal portal = net::Portal::Off;
uint32_t stepStart = 0, portalStart = 0;
time_t lastSyncTime = 0;
char simZone[64] = "";
net::Source source = net::Source::None;
char savedSsid[33] = "";
constexpr uint32_t STEP_MS[] = {0, 1200, 400, 400};
}  // namespace

void sim::setPortal(net::Portal p) {
  portal = p;
  if (p == net::Portal::Saved) {
    sim::wifiSaved = true;
    net::startSync();
  }
}

namespace net {
void begin() {}
void poll() {
  uint32_t e = sim::clockMs - stepStart;
  auto next = [&](Sync s) { syncNow = s; stepStart = sim::clockMs; };
  auto end = [&](Fail f) { if (f == Fail::None) byHand = false; fail = f; result = f == Fail::None ? Result::Ok : Result::Failed; finished++; next(Sync::Idle); };
  switch (syncNow) {
    case Sync::Connecting:
      if (e >= STEP_MS[1]) {
        if (sim::syncOutcome == Fail::JoinFailed) end(Fail::JoinFailed);
        else next(Sync::Zone);
      }
      break;
    case Sync::Zone:
      if (e >= STEP_MS[2]) { strcpy(simZone, sim::zone); next(Sync::Clock); }
      break;
    case Sync::Clock:
      if (e >= STEP_MS[3]) {
        if (sim::syncOutcome == Fail::ClockFailed) end(Fail::ClockFailed);
        else { sim::clockSet = true; lastSyncTime = hal::now(); source = Source::Wifi; end(Fail::None); }
      }
      break;
    default: break;
  }
  if ((portal == Portal::Waiting || portal == Portal::PhoneJoined) && sim::clockMs - portalStart > 300000)
    portal = Portal::TimedOut;
}
void startSync() {
  if (portal == Portal::Waiting || portal == Portal::PhoneJoined) return;
  if (syncNow != Sync::Idle) return;
  sim::syncsStarted++;
  if (!sim::wifiSaved) { fail = Fail::NoNetworkSaved; result = Result::Failed; finished++; return; }
  syncNow = Sync::Connecting;
  stepStart = sim::clockMs;
}
void cancelSync() { if (syncNow != Sync::Idle) { syncNow = Sync::Idle; sim::syncsCancelled++; } }
Sync syncState() { return syncNow; }
bool syncRunning() { return syncNow != Sync::Idle; }
Result lastResult() { return result; }
uint16_t attempts() { return finished; }
Fail failReason() { return fail; }
int syncStep() { return syncNow == Sync::Connecting ? 1 : syncNow == Sync::Zone ? 2 : syncNow == Sync::Clock ? 3 : 0; }
void startPortal() { portal = Portal::Waiting; portalStart = sim::clockMs; syncNow = Sync::Idle; }
void cancelPortal() { if (portal == Portal::Waiting || portal == Portal::PhoneJoined) portal = Portal::Closed; }
Portal portalState() { return portal; }
uint32_t portalSecondsLeft() {
  if (portal != Portal::Waiting && portal != Portal::PhoneJoined) return 0;
  uint32_t used = sim::clockMs - portalStart;
  return used >= 300000 ? 0 : (300000 - used + 999) / 1000;
}
bool hasSavedWifi() { return sim::wifiSaved; }
bool timeIsValid() { return sim::clockSet; }
void setClockByHand(time_t t) { hal::setTime(t); byHand = true; }
bool clockSetByHand() { return byHand; }
JoinWhy joinWhy() { return sim::joinWhy; }
int joinCode() { return sim::joinWhy == JoinWhy::NotFound ? 201 : sim::joinWhy == JoinWhy::Password ? 15 : 0; }
bool radioOn() { return syncNow != Sync::Idle || portal == Portal::Waiting || portal == Portal::PhoneJoined; }
time_t lastSync() { return lastSyncTime; }
const char *zoneName() { return simZone; }
const char *ssid() { return !sim::wifiSaved ? "" : savedSsid[0] ? savedSsid : "HomeNet-5G"; }
void setClockFromPhone(time_t utc, int) {
  hal::setTime(utc);
  lastSyncTime = utc;
  source = Source::Phone;
  byHand = false;
  fail = Fail::None;
  result = Result::Ok;
  finished++;
}
Source lastSource() { return source; }
void setZone(const char *posix, const char *name) {
  if (posix) {
    settings.tz = posix;
    saveSettings();
  }
  if (name) snprintf(simZone, sizeof simZone, "%s", name);
}
void saveWifi(const char *s, const char *p) {
  saveNetwork(s, p);
  startSync();
}
void saveNetwork(const char *s, const char *) {
  snprintf(savedSsid, sizeof savedSsid, "%s", s);
  sim::wifiSaved = true;
}
int32_t driftPpb() { return settings.driftPpb; }
}  // namespace net

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
