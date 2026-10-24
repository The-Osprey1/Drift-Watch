// Runs the real DriftWatch/net.cpp (Wi-Fi sync, time zone lookup, time server, setup
// hotspot) against the stand-ins in tools/host/net/, with the real settings.cpp and
// timekeep.cpp. simulate.cpp tests the interface with net.cpp replaced; this tests net.cpp
// itself: the time zone kept across restarts, failed and garbled lookups, join failures and
// timeouts. Exit code 1 on any failed check.
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

namespace {
uint32_t clockMs = 0;
int64_t wallMs = 1792788510000LL;  // 2026-10-23 20:48:30 UTC
bool verbose = false;
}  // namespace

HostSerial Serial;
uint32_t millis() { return clockMs; }
uint32_t micros() { return clockMs * 1000; }
void delay(uint32_t ms) { clockMs += ms; }

namespace hal {
uint32_t ms() { return clockMs; }
time_t now() { return (time_t)(nowMs() / 1000); }
int64_t nowMs() { return wallMs + clockMs; }
void setTime(time_t t) { setTimeMs((int64_t)t * 1000); }
void setTimeMs(int64_t ms) { wallMs = ms - clockMs; }
void adjustMs(int64_t ms) { wallMs += ms; }
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
  uint32_t end = clockMs + ms;
  while ((int32_t)(clockMs - end) < 0) {
    net::poll();
    clockMs += stepMs;
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
  sim::flash.clear();
  netsim::reset();
  netsim::flashSsid = "HomeNet";
  netsim::flashPassword = "secret";
  restart();
}

// Drives one sync through to its end: joins after 300 ms, the time server answers after 200 ms.
void syncOnce() {
  net::startSync();
  runFor(300);
  netsim::status = WL_CONNECTED;
  runFor(100);
  netsim::sntpStatus = SNTP_SYNC_STATUS_COMPLETED;
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
  netsim::sntpStatus = SNTP_SYNC_STATUS_COMPLETED;
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
  netsim::sntpStatus = SNTP_SYNC_STATUS_COMPLETED;
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
}  // namespace

int main(int argc, char **argv) {
  verbose = argc > 1 && !strcmp(argv[1], "-v");
  timeZoneKept();
  manualZone();
  badLookups();
  offsets();
  joining();
  portal();
  printf("%d of %d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
