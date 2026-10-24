// Runs the real DriftWatch/net.cpp (Wi-Fi sync, time zone lookup, time server, setup
// hotspot) against the stand-ins in tools/host/net/, with the real settings.cpp and
// timekeep.cpp. simulate.cpp tests the interface with net.cpp replaced; this tests net.cpp
// itself: the time zone kept across restarts, failed and garbled lookups, join failures and
// timeouts. Exit code 1 on any failed check.
#include <math.h>
#include <algorithm>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <Preferences.h>
#include <string>
#include "hal.h"
#include "net.h"
#include <esp_sntp.h>
#include <netsim.h>
#include <WiFi.h>
#include "settings.h"

// ---- Hardware layer stand-ins -------------------------------------------------------------
namespace sim {
std::map<std::string, FlashValue> flash;
int settingsSaves = 0;
}  // namespace sim

// Three clocks. upMs is the time since power-on, kept in 64 bits so millis() (its low 32 bits)
// can roll over the way it does after 49.7 days. truthMs is the real time, which the time
// server knows. watchMs is the watch's own wall clock: it runs `ratePpm` fast or slow, the way
// the chip's oscillator does, and only the code under test sets or nudges it.
namespace {
uint64_t upMs = 0;
constexpr int64_t START_MS = 1792788510000LL;  // 2026-10-23 20:48:30 UTC
int64_t truthAtZero = START_MS;
double watchMs = START_MS;
double ratePpm = 0;
bool verbose = false;

void advance(uint32_t ms) {
  upMs += ms;
  watchMs += ms * (1 + ratePpm / 1e6);
}
int64_t truthMs() { return truthAtZero + (int64_t)upMs; }
int64_t watchErrorMs() { return (int64_t)floor(watchMs) - truthMs(); }
// Starts the uptime just short of a millis() rollover, keeping the truth and watch clocks.
void upNear(uint64_t ms) {
  truthAtZero += (int64_t)upMs - (int64_t)ms;
  upMs = ms;
}
}  // namespace

HostSerial Serial;
uint32_t millis() { return (uint32_t)upMs; }
uint32_t micros() { return (uint32_t)(upMs * 1000); }
void delay(uint32_t ms) { advance(ms); }

namespace hal {
uint32_t ms() { return (uint32_t)upMs; }
time_t now() { return (time_t)(nowMs() / 1000); }
int64_t nowMs() { return (int64_t)floor(watchMs); }
void setTime(time_t t) { setTimeMs((int64_t)t * 1000); }
void setTimeMs(int64_t ms) { watchMs = ms; }
void adjustMs(int64_t ms) { watchMs += ms; }
void log(const char *fmt, ...) {
  if (!verbose) return;
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "    [%6u ms] ", (uint32_t)upMs);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}
}  // namespace hal

// ---- Checks -------------------------------------------------------------------------------
namespace {
int failures = 0, checks = 0;

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

constexpr const char *PACIFIC = "PST8PDT,M3.2.0,M11.1.0";
constexpr const char *LONDON_REPLY = "{\"status\":\"success\",\"timezone\":\"Europe/London\",\"offset\":3600}";

std::string envTz() {
  const char *tz = getenv("TZ");
  return tz ? tz : "";
}
bool savedTz(std::string &out) {
  auto it = sim::flash.find("watch/tz");
  if (it == sim::flash.end()) return false;
  out = it->second.s;
  return true;
}

// Runs net::poll() for `ms`, as the interface loop does every few milliseconds.
void runFor(uint32_t ms, uint32_t stepMs = 10) {
  uint64_t end = upMs + ms;
  while (upMs < end) {
    net::poll();
    advance(stepMs);
  }
}

// A power-on: settings come back from flash and the radio state is as the board left it.
void restart() {
  settings = Settings{};
  loadSettings();
  net::begin();
}

// A fresh board with a network saved, then one sync that joins, looks up and gets the time.
void freshBoard() {
  ratePpm = 0;
  watchMs = (double)truthMs();
  sim::flash.clear();
  netsim::reset();
  netsim::flashSsid = "HomeNet";
  netsim::flashPassword = "secret";
  restart();
}

// Drives one sync through to its end: joins after 300 ms, the time server answers after 200 ms
// with the true time.
void syncOnce() {
  net::startSync();
  runFor(300);
  netsim::status = WL_CONNECTED;
  runFor(100);
  netsim::sntpAnswer(truthMs());
  runFor(100);
}

// The local hour at a fixed UTC time, using a POSIX zone the way the watch's clock does.
int localHourAt(const char *posix, time_t utc) {
  setenv("TZ", posix, 1);
  tzset();
  struct tm t;
  localtime_r(&utc, &t);
  return t.tm_hour;
}

void timeZoneKept() {
  printf("Time zone: Auto survives restarts\n");
  freshBoard();
  std::string tz;
  CHECK(settings.tz.isEmpty(), "a fresh board starts on Auto (tz \"%s\")", settings.tz.c_str());
  CHECK(!savedTz(tz) || tz.empty(), "starting saves no zone (saved \"%s\")", tz.c_str());
  CHECK(envTz() == PACIFIC, "until a lookup answers, the clock shows Pacific time (TZ %s)", envTz().c_str());
  restart();
  restart();
  CHECK(settings.tz.isEmpty() && (!savedTz(tz) || tz.empty()), "still Auto after two restarts");

  netsim::httpBody = LONDON_REPLY;
  syncOnce();
  CHECK(net::lastResult() == net::Result::Ok && !net::syncRunning(), "the sync finishes (result %d)",
        (int)net::lastResult());
  CHECK(netsim::tasksStarted == 1 && netsim::httpGets == 1, "Auto looks the zone up (%d lookups)", netsim::httpGets);
  CHECK(netsim::httpUrl.find("ip-api.com") != std::string::npos, "from the lookup service (%s)", netsim::httpUrl.c_str());
  CHECK(!strcmp(net::zoneName(), "Europe/London"), "the zone name is kept for Details (%s)", net::zoneName());
  CHECK(netsim::configuredTz == "<+0100>-1:00", "the time server is set to the found zone (%s)", netsim::configuredTz.c_str());
  CHECK(settings.tz.isEmpty() && (!savedTz(tz) || tz.empty()), "a found zone is not saved, so Auto stays Auto");

  restart();
  CHECK(settings.tz.isEmpty(), "after a restart it is still Auto");
  int before = netsim::httpGets;
  syncOnce();
  CHECK(netsim::httpGets == before + 1, "and the next sync looks the zone up again");
}

void manualZone() {
  printf("Time zone: a zone typed in setup is kept and never looked up\n");
  freshBoard();
  net::startPortal();
  CHECK(netsim::portalOpen && netsim::mode == WIFI_AP, "the setup hotspot opens");
  CHECK(netsim::portalTitle == "Drift-Watch", "with the Drift-Watch title (%s)", netsim::portalTitle.c_str());
  netsim::submitPortal("  GMT0BST,M3.5.0/1,M10.5.0  ");
  runFor(20);
  CHECK(settings.tz == "GMT0BST,M3.5.0/1,M10.5.0", "the typed zone is saved without its spaces (%s)", settings.tz.c_str());
  CHECK(!netsim::portalOpen && net::syncRunning(), "the hotspot closes and a sync starts");
  netsim::status = WL_CONNECTED;
  runFor(100);
  netsim::sntpAnswer(truthMs());
  runFor(100);
  CHECK(netsim::httpGets == 0, "no lookup with a typed zone (%d)", netsim::httpGets);
  CHECK(netsim::configuredTz == "GMT0BST,M3.5.0/1,M10.5.0", "the time server uses it (%s)", netsim::configuredTz.c_str());
  restart();
  CHECK(settings.tz == "GMT0BST,M3.5.0/1,M10.5.0" && envTz() == "GMT0BST,M3.5.0/1,M10.5.0",
        "it is still there after a restart (TZ %s)", envTz().c_str());

  printf("Time zone: a blank setup field goes back to Auto\n");
  net::startPortal();
  netsim::submitPortal("");
  runFor(20);
  std::string tz;
  CHECK(settings.tz.isEmpty() && savedTz(tz) && tz.empty(), "the saved zone is cleared (\"%s\")", tz.c_str());
  restart();
  CHECK(settings.tz.isEmpty() && envTz() == PACIFIC, "after a restart: Auto, Pacific until a lookup (TZ %s)", envTz().c_str());
  net::cancelSync();

  printf("Time zone: one sent by a phone is saved\n");
  net::setZone("EST5EDT,M3.2.0,M11.1.0", "America/New_York");
  CHECK(settings.tz == "EST5EDT,M3.2.0,M11.1.0" && savedTz(tz) && tz == "EST5EDT,M3.2.0,M11.1.0", "saved (%s)", tz.c_str());
  CHECK(!strcmp(net::zoneName(), "America/New_York"), "with its name (%s)", net::zoneName());
}

void badLookups() {
  struct Case {
    const char *what;
    bool beginOk;
    int code;
    const char *body;
  } cases[] = {
      {"the service cannot be reached", false, -1, ""},
      {"the service answers 500", true, 500, LONDON_REPLY},
      {"the service says fail", true, 200, "{\"status\":\"fail\",\"message\":\"private range\"}"},
      {"the reply has no offset", true, 200, "{\"status\":\"success\",\"timezone\":\"Europe/London\"}"},
      {"the reply is cut short", true, 200, "{\"status\":\"succ"},
      {"the reply is empty", true, 200, ""},
      {"the reply is not JSON", true, 200, "<html>captive portal login</html>"},
  };
  printf("Time zone: failed or garbled lookups fall back and the sync still sets the time\n");
  for (const Case &c : cases) {
    freshBoard();
    netsim::httpBeginOk = c.beginOk;
    netsim::httpCode = c.code;
    netsim::httpBody = c.body;
    std::string nameBefore = net::zoneName();  // RAM only: a real restart clears it, this one cannot
    syncOnce();
    CHECK(net::lastResult() == net::Result::Ok, "%s: the sync still succeeds (result %d)", c.what, (int)net::lastResult());
    CHECK(netsim::configuredTz == PACIFIC, "%s: Pacific is used (%s)", c.what, netsim::configuredTz.c_str());
    CHECK(nameBefore == net::zoneName(), "%s: the zone name is left as it was (%s)", c.what, net::zoneName());
    CHECK(settings.tz.isEmpty(), "%s: still Auto", c.what);
  }

  printf("Time zone: a lookup that never answers\n");
  freshBoard();
  netsim::taskMode = netsim::TaskMode::Hold;
  netsim::httpBody = LONDON_REPLY;
  net::startSync();
  netsim::status = WL_CONNECTED;
  runFor(100);
  CHECK(net::syncState() == net::Sync::Zone && net::syncStep() == 2, "the sync waits on the lookup (step %d)", net::syncStep());
  runFor(8100);
  CHECK(net::syncState() == net::Sync::Clock, "after the lookup's time is up it goes on to the time server");
  netsim::sntpAnswer(truthMs());
  runFor(100);
  CHECK(net::lastResult() == net::Result::Ok && netsim::configuredTz == PACIFIC, "and finishes on Pacific (%s)",
        netsim::configuredTz.c_str());
  int tasks = netsim::tasksStarted;
  syncOnce();
  CHECK(netsim::tasksStarted == tasks, "the next sync does not start a second lookup while the first is still out");
  CHECK(net::lastResult() == net::Result::Ok, "and still succeeds");
  CHECK(netsim::runTask(), "the late lookup can still finish");
  netsim::taskMode = netsim::TaskMode::RunNow;
  syncOnce();
  CHECK(netsim::tasksStarted == tasks + 1 && !strcmp(net::zoneName(), "Europe/London"),
        "after it, the next sync looks up again and finds the zone (%s)", net::zoneName());

  printf("Time zone: no memory for the lookup task\n");
  freshBoard();
  netsim::taskMode = netsim::TaskMode::Refuse;
  syncOnce();
  CHECK(net::lastResult() == net::Result::Ok && netsim::httpGets == 0, "the sync goes straight to the time server");
}

void offsets() {
  printf("Time zone: offsets from the lookup become the right local time\n");
  struct Case {
    long offset;
    const char *posix;
    int hourAtNoonUtc;
  } cases[] = {
      {0, "<+0000>-0:00", 12},  {3600, "<+0100>-1:00", 13},     {-18000, "<-0500>+5:00", 7},
      {19800, "<+0530>-5:30", 17}, {-12600, "<-0330>+3:30", 8}, {45900, "<+1245>-12:45", 0},
  };
  const time_t noonUtc = 1792843200;  // 2026-10-24 12:00 UTC
  for (const Case &c : cases) {
    freshBoard();
    char body[128];
    snprintf(body, sizeof body, "{\"status\":\"success\",\"timezone\":\"Test/Zone\",\"offset\":%ld}", c.offset);
    netsim::httpBody = body;
    syncOnce();
    CHECK(netsim::configuredTz == c.posix, "offset %ld gives %s (got %s)", c.offset, c.posix, netsim::configuredTz.c_str());
    int hour = localHourAt(netsim::configuredTz.c_str(), noonUtc);
    CHECK(hour == c.hourAtNoonUtc, "offset %ld: 12:00 UTC shows as %02d:xx (want %02d)", c.offset, hour, c.hourAtNoonUtc);
  }
  int pacificNoon = localHourAt(PACIFIC, noonUtc);
  CHECK(pacificNoon == 5, "the Pacific default is on summer time in October (12:00 UTC is %02d:00)", pacificNoon);
}

void joining() {
  printf("Wi-Fi: joining fails, times out, or has nothing to join\n");
  struct Case {
    uint8_t reason;
    net::JoinWhy why;
  } cases[] = {
      {0, net::JoinWhy::NoAnswer},     {201, net::JoinWhy::NotFound}, {200, net::JoinWhy::NotFound},
      {15, net::JoinWhy::Password},    {202, net::JoinWhy::Password}, {2, net::JoinWhy::Refused},
  };
  for (const Case &c : cases) {
    freshBoard();
    net::startSync();
    runFor(500);
    if (c.reason) netsim::dropConnection(c.reason);
    runFor(15000);
    CHECK(net::lastResult() == net::Result::Failed && net::failReason() == net::Fail::JoinFailed,
          "reason %d: the join fails after its time (fail %d)", c.reason, (int)net::failReason());
    CHECK(net::joinWhy() == c.why && net::joinCode() == c.reason, "reason %d: grouped as %d (got %d, code %d)", c.reason,
          (int)c.why, (int)net::joinWhy(), net::joinCode());
    CHECK(netsim::mode == WIFI_OFF && !net::radioOn(), "reason %d: the radio is off again", c.reason);
  }

  freshBoard();
  netsim::status = WL_CONNECTED;
  net::startSync();
  runFor(11000);
  CHECK(net::failReason() == net::Fail::ClockFailed, "no answer from the time server fails the sync (fail %d)",
        (int)net::failReason());

  freshBoard();
  netsim::flashSsid.clear();
  restart();
  CHECK(!net::hasSavedWifi(), "a board with no network saved knows it");
  net::startSync();
  CHECK(net::failReason() == net::Fail::NoNetworkSaved && !net::syncRunning(), "and a sync says so at once");

  freshBoard();
  net::saveNetwork("Cafe", "latte");
  CHECK(!strcmp(net::ssid(), "Cafe") && netsim::flashPassword == "latte" && !net::radioOn(),
        "a network from the phone is saved and the radio left off (%s)", net::ssid());
}

void portal() {
  printf("Setup hotspot: opens, shows phones, times out\n");
  freshBoard();
  net::startPortal();
  CHECK(net::portalState() == net::Portal::Waiting && net::radioOn(), "waiting for a phone");
  netsim::stationsOnHotspot = 1;
  runFor(20);
  CHECK(net::portalState() == net::Portal::PhoneJoined, "a phone joined");
  CHECK(net::portalSecondsLeft() > 290 && net::portalSecondsLeft() <= 300, "about 5 minutes left (%u)", net::portalSecondsLeft());
  net::startSync();
  CHECK(!net::syncRunning(), "a sync does not start while a phone is on the hotspot");
  runFor(301000, 1000);
  CHECK(net::portalState() == net::Portal::TimedOut && !netsim::portalOpen, "it closes after 5 minutes (%d)",
        (int)net::portalState());
}

// Starts a sync and lets it join, so it waits at the zone lookup (Auto) or the time server.
void joinAndWait() {
  net::startSync();
  runFor(300);
  netsim::status = WL_CONNECTED;
  runFor(100);
}

void cancelling() {
  printf("Cancel: at each stage, and whatever answers late is not used\n");
  // While joining: the network answers just after Cancel.
  freshBoard();
  netsim::httpBody = LONDON_REPLY;
  uint16_t tries = net::attempts();
  net::startSync();
  runFor(300);
  CHECK(net::syncState() == net::Sync::Connecting, "joining (%d)", (int)net::syncState());
  net::cancelSync();
  CHECK(!net::syncRunning() && netsim::mode == WIFI_OFF && !net::radioOn(), "cancel while joining: stopped, radio off");
  netsim::status = WL_CONNECTED;  // the join completes late
  runFor(2000);
  CHECK(!net::syncRunning() && netsim::tasksStarted == 0 && netsim::sntpStarts == 0,
        "a late join starts nothing (%d lookups, %d time server starts)", netsim::tasksStarted, netsim::sntpStarts);
  CHECK(net::attempts() == tries, "and a cancelled sync is not an attempt (%d)", net::attempts() - tries);

  // While the zone lookup is out: its late answer is dropped.
  freshBoard();
  netsim::taskMode = netsim::TaskMode::Hold;
  netsim::httpBody = LONDON_REPLY;
  std::string name0 = net::zoneName();  // RAM only: a real restart clears it, this one cannot
  joinAndWait();
  CHECK(net::syncState() == net::Sync::Zone, "waiting on the lookup (%d)", (int)net::syncState());
  net::cancelSync();
  CHECK(!net::syncRunning() && !net::radioOn(), "cancel during the lookup: stopped, radio off");
  CHECK(netsim::runTask(), "the lookup answers after the cancel");
  runFor(500);
  CHECK(name0 == net::zoneName() && envTz() == PACIFIC, "its answer is not used (zone \"%s\", TZ %s)", net::zoneName(),
        envTz().c_str());
  netsim::taskMode = netsim::TaskMode::RunNow;
  netsim::httpBody = "{\"status\":\"success\",\"timezone\":\"Asia/Tokyo\",\"offset\":32400}";
  syncOnce();
  CHECK(!strcmp(net::zoneName(), "Asia/Tokyo") && netsim::configuredTz == "<+0900>-9:00",
        "the next sync looks up afresh and uses its own answer (%s, %s)", net::zoneName(), netsim::configuredTz.c_str());

  // A lookup cancelled and a new sync started at once: the old answer arrives during the new one.
  freshBoard();
  netsim::taskMode = netsim::TaskMode::Hold;
  netsim::httpBody = LONDON_REPLY;
  name0 = net::zoneName();
  joinAndWait();
  net::cancelSync();
  netsim::status = WL_DISCONNECTED;
  joinAndWait();
  CHECK(net::syncState() == net::Sync::Clock && netsim::tasksStarted == 1,
        "with the old lookup still out, the new sync goes on to the time server (%d, %d lookups)", (int)net::syncState(),
        netsim::tasksStarted);
  netsim::runTask();
  runFor(100);
  CHECK(name0 == net::zoneName() && netsim::configuredTz == PACIFIC, "the old answer changes nothing (%s)",
        netsim::configuredTz.c_str());
  netsim::sntpAnswer(truthMs());
  runFor(100);
  CHECK(net::lastResult() == net::Result::Ok, "and the new sync finishes");

  // A lookup that ran out of time, answering while the sync waits on the time server.
  freshBoard();
  netsim::taskMode = netsim::TaskMode::Hold;
  netsim::httpBody = LONDON_REPLY;
  name0 = net::zoneName();
  joinAndWait();
  runFor(8100);
  CHECK(net::syncState() == net::Sync::Clock, "after 8 s the sync stops waiting for the lookup");
  netsim::runTask();
  runFor(100);
  CHECK(name0 == net::zoneName() && envTz() == PACIFIC, "the late answer is not used mid-sync (%s)", envTz().c_str());
  netsim::sntpAnswer(truthMs());
  runFor(100);
  CHECK(net::lastResult() == net::Result::Ok && envTz() == PACIFIC, "the sync finishes on the zone it had");

  // While waiting for the time server: the client is stopped, so a late answer cannot set the clock.
  freshBoard();
  netsim::httpBody = LONDON_REPLY;
  syncOnce();
  tries = net::attempts();
  int64_t before = hal::nowMs();
  uint64_t up0 = upMs;
  joinAndWait();
  CHECK(net::syncState() == net::Sync::Clock && netsim::sntpRunning, "waiting for the time server");
  int stops = netsim::sntpStops;
  net::cancelSync();
  CHECK(!netsim::sntpRunning && netsim::sntpStops == stops + 1, "cancel stops the time server client");
  CHECK(!netsim::sntpAnswer(truthMs() + 3600000), "so an answer after the cancel is not taken");
  runFor(500);
  CHECK(llabs(hal::nowMs() - before - (int64_t)(upMs - up0)) < 5, "and the clock is untouched (moved %lld ms in %llu ms)",
        (long long)(hal::nowMs() - before), (unsigned long long)(upMs - up0));
  CHECK(net::lastResult() == net::Result::Ok && net::attempts() == tries, "the earlier result stands");

  // Every way a sync ends stops the client.
  freshBoard();
  syncOnce();
  CHECK(!netsim::sntpRunning, "a successful sync stops the client");
  joinAndWait();
  runFor(10500);
  CHECK(net::failReason() == net::Fail::ClockFailed && !netsim::sntpRunning, "so does a time server timeout");
  joinAndWait();
  net::startPortal();
  CHECK(!net::syncRunning() && !netsim::sntpRunning && netsim::portalOpen, "and opening the hotspot mid-sync");
  net::cancelPortal();
}

void timeServer() {
  printf("Time server: failures keep a valid clock\n");
  freshBoard();
  syncOnce();
  time_t synced = net::lastSync();
  CHECK(net::timeIsValid() && synced == truthMs() / 1000 - 0, "the first sync sets the clock (%ld)", (long)synced);
  runFor(60000, 100);
  int64_t before = hal::nowMs();
  joinAndWait();
  runFor(10500);
  CHECK(net::failReason() == net::Fail::ClockFailed && net::lastResult() == net::Result::Failed, "no answer in 10 s fails the sync");
  CHECK(net::timeIsValid() && net::lastSync() == synced, "the clock stays valid and the last sync time stays (%ld)",
        (long)net::lastSync());
  CHECK(llabs(hal::nowMs() - before - 10900) < 100, "and the clock was not moved");
  CHECK(!netsim::sntpAnswer(truthMs()), "an answer after the timeout is not taken");
  // Answered at the very end of the wait still counts.
  joinAndWait();
  runFor(9800);
  netsim::sntpAnswer(truthMs());
  runFor(100);
  CHECK(net::lastResult() == net::Result::Ok, "an answer just inside the 10 s counts");
  // The time server answers only after the trusted time is applied: the sync never reports a time it does not have.
  CHECK(llabs(watchErrorMs()) <= 1, "after a sync the watch matches the true time (%lld ms)", (long long)watchErrorMs());
  CHECK(net::lastSync() == truthMs() / 1000 || net::lastSync() == truthMs() / 1000 - 1, "and the sync time is the true time");

  printf("Time server: a clock never set is made valid only by an answer\n");
  freshBoard();
  watchMs = 5000;  // a board fresh from power-on: 1970
  CHECK(!net::timeIsValid(), "an unset clock is not valid");
  joinAndWait();
  runFor(10500);
  CHECK(!net::timeIsValid() && net::failReason() == net::Fail::ClockFailed, "a failed sync leaves it unset");
  syncOnce();
  CHECK(net::timeIsValid() && llabs(watchErrorMs()) <= 1, "a sync sets it (%lld ms off)", (long long)watchErrorMs());
  CHECK(net::driftPpb() == 0, "and learns no drift from a clock that was never set (%ld)", (long)net::driftPpb());
}

void daylightSaving() {
  printf("Time zone: summer time rules against the fixed offsets Auto uses\n");
  // Europe/London leaves summer time on 2026-10-25 at 01:00 UTC.
  const time_t beforeChange = 1792889999;  // 2026-10-25 00:59:59 UTC
  const time_t afterChange = 1792890000;   // 2026-10-25 01:00:00 UTC
  const char *rule = "GMT0BST,M3.5.0/1,M10.5.0";
  CHECK(localHourAt(rule, beforeChange) == 1 && localHourAt(rule, afterChange) == 1,
        "a typed rule goes from 01:59:59 back to 01:00:00 by itself (%d, %d)", localHourAt(rule, beforeChange),
        localHourAt(rule, afterChange));
  CHECK(localHourAt(rule, afterChange + 11 * 3600) == 12, "and shows 12:00 at noon UTC afterwards");
  // Auto: the lookup gives today's offset only. The fixed zone keeps summer time until the next sync.
  freshBoard();
  netsim::httpBody = LONDON_REPLY;
  syncOnce();
  std::string autoTz = netsim::configuredTz;
  CHECK(localHourAt(autoTz.c_str(), afterChange + 11 * 3600) == 13,
        "Auto's fixed +01:00 still shows summer time after the change (%d), until it syncs again",
        localHourAt(autoTz.c_str(), afterChange + 11 * 3600));
  netsim::httpBody = "{\"status\":\"success\",\"timezone\":\"Europe/London\",\"offset\":0}";
  syncOnce();
  CHECK(netsim::configuredTz == "<+0000>-0:00" && localHourAt(netsim::configuredTz.c_str(), afterChange + 11 * 3600) == 12,
        "the next sync (at most 6 hours later on the watch) picks up winter time (%s)", netsim::configuredTz.c_str());
  // The Pacific default is a rule, so it changes on its own on 2026-11-01 at 09:00 UTC.
  CHECK(localHourAt(PACIFIC, 1793523599) == 1 && localHourAt(PACIFIC, 1793523600) == 1,
        "the Pacific default goes from 01:59:59 back to 01:00 by itself");
}

void drift() {
  printf("Drift: learned against a true clock over a day and over several days\n");
  freshBoard();
  ratePpm = 50;  // 4.3 s a day fast
  syncOnce();
  CHECK(llabs(watchErrorMs()) <= 1, "synced to the true time");
  const uint32_t DAY = 86400000;
  runFor(DAY, 1000);
  int64_t err = watchErrorMs();
  CHECK(llabs(err - 4320) < 30, "a day without a sync: 50 ppm fast is 4.32 s ahead (%lld ms)", (long long)err);
  syncOnce();
  CHECK(llabs(net::driftPpb() - 50000) < 1000, "the next sync learns the rate (%ld ppb)", (long)net::driftPpb());
  CHECK(settings.driftPpb == net::driftPpb(), "and saves it (%ld)", (long)settings.driftPpb);
  int64_t worst = 0;
  for (int h = 0; h < 24; h++) {
    runFor(3600000, 1000);
    worst = std::max<int64_t>(worst, llabs(watchErrorMs()));
  }
  CHECK(worst <= 250, "the next day with no sync: never more than 250 ms off (%lld ms)", (long long)worst);
  worst = 0;
  for (int h = 0; h < 72; h++) {
    runFor(3600000, 1000);
    worst = std::max<int64_t>(worst, llabs(watchErrorMs()));
  }
  CHECK(worst <= 300, "three more days with no sync: never more than 300 ms off (%lld ms)", (long long)worst);
  syncOnce();
  CHECK(llabs(net::driftPpb() - 50000) < 500, "a sync after the long gap refines the rate (%ld ppb)", (long)net::driftPpb());

  // The oscillator changes with temperature: slower now. Syncs every 6 hours follow it.
  ratePpm = -120;
  worst = 0;
  for (int i = 0; i < 4 * 4; i++) {
    runFor(6 * 3600000, 1000);
    if (i >= 8) worst = std::max<int64_t>(worst, llabs(watchErrorMs()));
    syncOnce();
  }
  CHECK(llabs(net::driftPpb() + 120000) < 2000, "a slower clock is learned too (%ld ppb)", (long)net::driftPpb());
  CHECK(worst <= 250, "and once learned the watch is never more than 250 ms off before a sync (%lld ms)", (long long)worst);

  printf("Drift: a sync over less than 6 hours teaches nothing\n");
  int32_t ppb = net::driftPpb();
  ratePpm = 0;
  runFor(3600000, 1000);
  syncOnce();
  CHECK(net::driftPpb() == ppb, "an hour is too short to learn from (%ld)", (long)net::driftPpb());

  printf("Drift: a clock set by hand is not learned from\n");
  net::setClockByHand(hal::now() + 120);
  runFor(7 * 3600000, 1000);
  ppb = net::driftPpb();
  syncOnce();
  CHECK(net::driftPpb() == ppb, "the sync after a hand setting starts a new span (%ld)", (long)net::driftPpb());
  ratePpm = 0;
}

void rollover() {
  printf("millis() rollover: timeouts and the clock across it\n");
  freshBoard();
  upNear(0x100000000ULL - 5000);
  net::startSync();
  runFor(14000);
  CHECK(net::syncRunning(), "a join that started 5 s before the rollover is still trying 14 s later");
  runFor(1500);
  CHECK(net::failReason() == net::Fail::JoinFailed && !net::syncRunning(), "and gives up at 15 s, across the rollover");
  upNear(0x100000000ULL - 200);
  syncOnce();
  CHECK(net::lastResult() == net::Result::Ok && llabs(watchErrorMs()) <= 1, "a sync across the rollover sets the clock");
  upNear(0x100000000ULL - 3000);
  net::startPortal();
  runFor(299000, 1000);
  CHECK(net::portalState() == net::Portal::Waiting && net::portalSecondsLeft() <= 2, "the hotspot counts down across it (%u s)",
        net::portalSecondsLeft());
  runFor(3000, 1000);
  CHECK(net::portalState() == net::Portal::TimedOut, "and closes at 5 minutes");

  freshBoard();
  ratePpm = 50;
  upNear(0x100000000ULL - 12 * 3600000ULL);
  syncOnce();
  runFor(86400000, 1000);
  syncOnce();
  CHECK(llabs(net::driftPpb() - 50000) < 1000, "drift is learned over a day that crosses the rollover (%ld ppb)",
        (long)net::driftPpb());
  upNear(0x100000000ULL - 12 * 3600000ULL);
  int64_t worst = 0;
  for (int h = 0; h < 24; h++) {
    runFor(3600000, 1000);
    worst = std::max<int64_t>(worst, llabs(watchErrorMs()));
  }
  CHECK(worst <= 250, "and the nudges carry on across it (%lld ms at worst)", (long long)worst);
  ratePpm = 0;
}

void parsing() {
  printf("Time zone lookup: replies with spaces, cut short, or with impossible offsets\n");
  struct Case {
    const char *what, *body, *posix, *name;  // posix nullptr: the reply must not be used
  } cases[] = {
      {"spaces around the colons", "{ \"status\" : \"success\", \"timezone\" : \"Europe/London\" , \"offset\" : 3600 }",
       "<+0100>-1:00", "Europe/London"},
      {"lines and tabs", "{\n\t\"status\":\t\"success\",\n\t\"timezone\": \"Asia/Kolkata\",\n\t\"offset\": 19800\n}\n",
       "<+0530>-5:30", "Asia/Kolkata"},
      {"keys in another order", "{\"offset\":-18000,\"timezone\":\"America/New_York\",\"status\":\"success\"}",
       "<-0500>+5:00", "America/New_York"},
      {"+14:00, the furthest ahead", "{\"status\":\"success\",\"timezone\":\"Pacific/Kiritimati\",\"offset\":50400}",
       "<+1400>-14:00", "Pacific/Kiritimati"},
      {"-12:00, the furthest behind", "{\"status\":\"success\",\"timezone\":\"Etc/GMT+12\",\"offset\":-43200}",
       "<-1200>+12:00", "Etc/GMT+12"},
      {"+05:45", "{\"status\":\"success\",\"timezone\":\"Asia/Kathmandu\",\"offset\":20700}", "<+0545>-5:45",
       "Asia/Kathmandu"},
      {"the number cut short", "{\"status\":\"success\",\"timezone\":\"Europe/London\",\"offset\":36", nullptr, nullptr},
      {"cut short after a comma", "{\"offset\":3600,\"timezone\":\"Europe/London\",\"status\":\"success\",", nullptr, nullptr},
      {"no closing brace", "{\"status\":\"success\",\"timezone\":\"Europe/London\",\"offset\":3600", nullptr, nullptr},
      {"a name cut short", "{\"status\":\"success\",\"timezone\":\"Europe/Lon", nullptr, nullptr},
      {"an offset past +14:00", "{\"status\":\"success\",\"timezone\":\"Bad/Zone\",\"offset\":54000}", nullptr, nullptr},
      {"an offset past -12:00", "{\"status\":\"success\",\"timezone\":\"Bad/Zone\",\"offset\":-46800}", nullptr, nullptr},
      {"an offset not in quarter hours", "{\"status\":\"success\",\"timezone\":\"Bad/Zone\",\"offset\":3601}", nullptr, nullptr},
      {"an offset with a fraction", "{\"status\":\"success\",\"timezone\":\"Bad/Zone\",\"offset\":3600.5}", nullptr, nullptr},
      {"an offset in quotes", "{\"status\":\"success\",\"timezone\":\"Bad/Zone\",\"offset\":\"3600\"}", nullptr, nullptr},
      {"a huge offset", "{\"status\":\"success\",\"timezone\":\"Bad/Zone\",\"offset\":99999999999}", nullptr, nullptr},
      {"a minus sign only", "{\"status\":\"success\",\"timezone\":\"Bad/Zone\",\"offset\":-}", nullptr, nullptr},
      {"an empty zone name", "{\"status\":\"success\",\"timezone\":\"\",\"offset\":0}", nullptr, nullptr},
      {"the key word only inside a value", "{\"status\":\"success\",\"note\":\"timezone\",\"offset\":0}", nullptr, nullptr},
      {"status with spaces inside", "{\"status\":\" success\",\"timezone\":\"Europe/London\",\"offset\":3600}", nullptr, nullptr},
      {"text before the object", "OK {\"status\":\"success\",\"timezone\":\"Europe/London\",\"offset\":3600}", nullptr, nullptr},
  };
  for (const Case &c : cases) {
    freshBoard();
    netsim::httpBody = c.body;
    std::string name0 = net::zoneName();
    syncOnce();
    CHECK(net::lastResult() == net::Result::Ok, "%s: the sync still succeeds", c.what);
    if (c.posix) {
      CHECK(netsim::configuredTz == c.posix && !strcmp(net::zoneName(), c.name), "%s: used (%s, %s)", c.what,
            netsim::configuredTz.c_str(), net::zoneName());
    } else {
      CHECK(netsim::configuredTz == PACIFIC && name0 == net::zoneName(), "%s: not used (%s, \"%s\")", c.what,
            netsim::configuredTz.c_str(), net::zoneName());
    }
  }
}

void portalMore() {
  printf("Setup hotspot: cancel, time out, save, and open again\n");
  freshBoard();
  net::startPortal();
  net::cancelPortal();
  CHECK(net::portalState() == net::Portal::Closed && !netsim::portalOpen && netsim::mode == WIFI_OFF && !net::radioOn(),
        "Cancel closes it and turns the radio off");
  CHECK(!net::syncRunning(), "without starting a sync");
  net::startPortal();
  CHECK(net::portalState() == net::Portal::Waiting && netsim::portalOpen && net::portalSecondsLeft() == 300,
        "it opens again after a cancel, with the full 5 minutes (%u)", net::portalSecondsLeft());
  runFor(120000, 1000);
  net::startPortal();
  CHECK(net::portalSecondsLeft() <= 180, "opening it again while open changes nothing (%u s left)", net::portalSecondsLeft());
  runFor(182000, 1000);
  CHECK(net::portalState() == net::Portal::TimedOut && !net::radioOn(), "it times out");
  net::startPortal();
  CHECK(net::portalState() == net::Portal::Waiting && net::portalSecondsLeft() == 300, "and opens again after a timeout");
  netsim::stationsOnHotspot = 1;
  runFor(20);
  netsim::flashSsid = "NewNet";
  netsim::submitPortal("");
  runFor(20);
  CHECK(net::portalState() == net::Portal::Saved && !netsim::portalOpen && net::syncRunning() && !strcmp(net::ssid(), "NewNet"),
        "saving closes it and joins the new network (%s)", net::ssid());
  netsim::status = WL_CONNECTED;
  runFor(100);
  netsim::sntpAnswer(truthMs());
  runFor(100);
  CHECK(net::lastResult() == net::Result::Ok && !net::radioOn(), "the sync finishes and the radio goes off");
  netsim::stationsOnHotspot = 0;
  net::startPortal();
  CHECK(net::portalState() == net::Portal::Waiting, "after a save it can be opened again");
  net::cancelPortal();
}

// ---- Restarts in a fresh process ------------------------------------------------------------
// The flash (settings and the radio's saved network) is written to a file, and the second half
// of the test runs in a new process that starts from that file, as a power cycle does: nothing
// in RAM survives.
void saveFlash(const char *path) {
  FILE *f = fopen(path, "w");
  for (auto &kv : sim::flash)
    fprintf(f, "%s\t%d\t%llu\t%s\n", kv.first.c_str(), kv.second.isString, (unsigned long long)kv.second.n,
            kv.second.s.c_str());
  fprintf(f, "radio/ssid\t1\t0\t%s\nradio/pass\t1\t0\t%s\n", netsim::flashSsid.c_str(), netsim::flashPassword.c_str());
  fclose(f);
}
void loadFlash(const char *path) {
  sim::flash.clear();
  FILE *f = fopen(path, "r");
  char line[512];
  while (f && fgets(line, sizeof line, f)) {
    line[strcspn(line, "\n")] = 0;
    char *k = strtok(line, "\t"), *isStr = strtok(nullptr, "\t"), *n = strtok(nullptr, "\t"), *v = strtok(nullptr, "\t");
    std::string value = v ? v : "";
    if (!strcmp(k, "radio/ssid")) netsim::flashSsid = value;
    else if (!strcmp(k, "radio/pass")) netsim::flashPassword = value;
    else sim::flash[k] = {*isStr == '1', value, strtoull(n, nullptr, 10)};
  }
  if (f) fclose(f);
}

int rebootStage(const char *stage, const char *path) {
  netsim::reset();
  if (!strcmp(stage, "auto-save")) {
    freshBoard();
    netsim::httpBody = LONDON_REPLY;
    ratePpm = 50;
    syncOnce();
    runFor(86400000, 1000);
    syncOnce();
    CHECK(net::driftPpb() > 49000, "before the restart: drift learned (%ld)", (long)net::driftPpb());
    saveFlash(path);
  } else if (!strcmp(stage, "auto-check")) {
    loadFlash(path);
    restart();
    std::string tz;
    CHECK(settings.tz.isEmpty() && (!savedTz(tz) || tz.empty()), "after a power cycle: still Auto (\"%s\")", tz.c_str());
    CHECK(envTz() == PACIFIC && !strcmp(net::zoneName(), ""), "Pacific until the first lookup, and no zone name kept in RAM (TZ %s)",
          envTz().c_str());
    CHECK(llabs(net::driftPpb() - 50000) < 1000, "the learned drift is back (%ld ppb)", (long)net::driftPpb());
    CHECK(!strcmp(net::ssid(), "HomeNet"), "the saved network is back (%s)", net::ssid());
    netsim::httpBody = LONDON_REPLY;
    syncOnce();
    CHECK(netsim::httpGets == 1 && netsim::configuredTz == "<+0100>-1:00", "and the first sync looks the zone up again");
    saveFlash(path);
  } else if (!strcmp(stage, "typed-save")) {
    loadFlash(path);
    restart();
    net::startPortal();
    netsim::submitPortal("GMT0BST,M3.5.0/1,M10.5.0");
    runFor(20);
    net::cancelSync();
    saveFlash(path);
  } else if (!strcmp(stage, "typed-check")) {
    loadFlash(path);
    restart();
    CHECK(settings.tz == "GMT0BST,M3.5.0/1,M10.5.0" && envTz() == "GMT0BST,M3.5.0/1,M10.5.0",
          "after a power cycle: the typed zone is in use (TZ %s)", envTz().c_str());
    syncOnce();
    CHECK(netsim::httpGets == 0 && netsim::configuredTz == "GMT0BST,M3.5.0/1,M10.5.0", "and never looked up");
  } else {
    printf("unknown stage %s\n", stage);
    return 2;
  }
  return failures ? 1 : 0;
}

void reboots(const char *self) {
  printf("Restarts: settings come back from flash in a fresh process\n");
  char path[] = "/tmp/drift_flash_XXXXXX";
  int fd = mkstemp(path);
  if (fd >= 0) close(fd);
  for (const char *stage : {"auto-save", "auto-check", "typed-save", "typed-check"}) {
    std::string cmd = std::string(self) + " --reboot " + stage + " " + path;
    int rc = system(cmd.c_str());
    CHECK(rc == 0, "%s: the process ran its checks without a failure (status %d)", stage, rc);
  }
  unlink(path);
}
}  // namespace

int main(int argc, char **argv) {
  if (argc == 4 && !strcmp(argv[1], "--reboot")) return rebootStage(argv[2], argv[3]);
  verbose = argc > 1 && !strcmp(argv[1], "-v");
  timeZoneKept();
  manualZone();
  badLookups();
  offsets();
  joining();
  portal();
  cancelling();
  timeServer();
  daylightSaving();
  drift();
  rollover();
  parsing();
  portalMore();
  fflush(stdout);
  reboots(argv[0]);
  printf("%d of %d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
