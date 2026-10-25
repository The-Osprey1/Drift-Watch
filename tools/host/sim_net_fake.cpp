// The fake net for the fast interface simulation (simulate): each sync step takes a fixed time,
// then the scripted outcome in sim.h happens. simulate_network links the real net.cpp with the
// stand-ins in tools/host/net/ instead of this file.
#include "hal.h"
#include "net.h"
#include "settings.h"
#include "sim.h"

namespace sim {
net::Fail syncOutcome = net::Fail::None;
bool wifiSaved = true;
int syncsStarted = 0, syncsCancelled = 0;
char zone[64] = "Europe/London";
net::JoinWhy joinWhy = net::JoinWhy::NotFound;
}  // namespace sim

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
int joinTry() { return 1; }
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

