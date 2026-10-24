// Drives the real interface (ui.cpp) together with the real network code (net.cpp), settings
// (settings.cpp) and clock trim (timekeep.cpp), against the Wi-Fi, web and time-server
// stand-ins in tools/host/net. simulate.cpp tests the interface with net.cpp replaced by a
// script; this checks the two together: start-up sync, Wi-Fi setup through the real hotspot
// code, Cancel at each stage of a sync with late answers, the hotspot timing out, and quiet
// resyncs while the screen is off (with no light sleep while the radio is on).
// Exit code 1 on any failed check.
#include <Preferences.h>
#include <esp_sntp.h>
#include <netsim.h>
#include <WiFi.h>
#include "net.h"
#include "power.h"
#include "settings.h"
#include "orient.h"
#include "sim_drive.h"

namespace {
constexpr int64_t TRUE_START_MS = 1792788510000LL;  // 2026-10-23 20:48:30 UTC at power-on
constexpr const char *LONDON_REPLY = "{\"status\":\"success\",\"timezone\":\"Europe/London\",\"offset\":3600}";
constexpr const char *PACIFIC = "PST8PDT,M3.2.0,M11.1.0";

int64_t truthMs() { return TRUE_START_MS + sim::clockMs; }
std::string phase() {
  screens::Control c;
  if (screen() != "setup") return screen();
  if (find(screens::Button::Cancel, c) && find(screens::Button::Back, c)) return "connecting";
  if (find(screens::Button::TryAgain, c)) return "timed out";
  if (find(screens::Button::Done, c)) return "done";
  return "setup";
}

// Waits until the sync reaches `stage` (or `ms` pass).
bool waitFor(net::Sync stage, uint32_t ms) {
  for (uint32_t t = 0; t < ms && net::syncState() != stage; t += 10) runFor(10);
  return net::syncState() == stage;
}

void startUp() {
  printf("Start-up: the first sync joins, looks the zone up and sets the clock\n");
  sim::flash.clear();
  netsim::reset();
  netsim::flashSsid = "HomeNet";
  netsim::flashPassword = "secret";
  netsim::httpBody = LONDON_REPLY;
  sim::cold = true;
  sim::clockSet = false;
  settings = Settings{};
  settings.touchCheckedTurn = settings.screenTurn = DISPLAY_ROTATION;
  settings.touchMap = orient::expectedTouchMap(DISPLAY_ROTATION);
  saveSettings();
  ui::begin();
  CHECK(net::syncRunning() && netsim::mode == WIFI_STA, "a sync starts with the watch (%d)", (int)net::syncState());
  CHECK(!net::timeIsValid(), "and the clock is not valid yet");
  runFor(300);
  netsim::status = WL_CONNECTED;
  CHECK(waitFor(net::Sync::Clock, 500), "it joins and asks the time server (%d)", (int)net::syncState());
  CHECK(!net::timeIsValid(), "the clock is still not valid while the server has not answered");
  netsim::sntpAnswer(truthMs());
  runFor(200);
  CHECK(net::lastResult() == net::Result::Ok && net::timeIsValid() && llabs(hal::nowMs() - truthMs()) < 20,
        "the answer sets the clock (%lld ms off)", (long long)(hal::nowMs() - truthMs()));
  CHECK(netsim::configuredTz == "<+0100>-1:00" && !strcmp(net::zoneName(), "Europe/London"), "in the looked-up zone (%s)",
        netsim::configuredTz.c_str());
  CHECK(!net::radioOn() && netsim::mode == WIFI_OFF && !netsim::sntpRunning, "and the radio and time client are off");
  settle(4000);
  CHECK(screen() == "face", "the face shows (got %s)", screen().c_str());
  CHECK(settings.tz.isEmpty(), "Auto is still Auto");
}

void openSetup() {
  swipeUp();
  settle();
  goTo(screens::Button::WifiSetup);
  tapButton(screens::Button::WifiSetup);
  settle(200);
}

void setup() {
  printf("Wi-Fi setup: the real hotspot opens and Cancel closes it\n");
  openSetup();
  CHECK(screen() == "setup" && netsim::portalOpen && netsim::mode == WIFI_AP && net::portalState() == net::Portal::Waiting,
        "setup opens the hotspot (%s, mode %d)", screen().c_str(), netsim::mode);
  CHECK(goTo(screens::Button::Cancel) && tapButton(screens::Button::Cancel), "Cancel is offered and tapped");
  CHECK(screen() == "panel" && !netsim::portalOpen && netsim::mode == WIFI_OFF && net::portalState() == net::Portal::Closed,
        "the hotspot closes and the radio goes off (%s)", screen().c_str());

  printf("Wi-Fi setup: Cancel while joining the new network, and the late join is ignored\n");
  tapButton(screens::Button::WifiSetup);
  settle(200);
  netsim::stationsOnHotspot = 1;
  runFor(100);
  netsim::flashSsid = "Cafe";
  netsim::submitPortal("");
  runFor(100);
  CHECK(phase() == "connecting" && net::syncState() == net::Sync::Connecting, "the saved page starts a join (%s)",
        phase().c_str());
  int starts = netsim::sntpStarts, lookups = netsim::tasksStarted;
  tapButton(screens::Button::Cancel);
  CHECK(screen() == "panel" && !net::syncRunning() && !net::radioOn(), "Cancel stops it");
  netsim::status = WL_CONNECTED;
  runFor(1000);
  CHECK(!net::syncRunning() && netsim::sntpStarts == starts && netsim::tasksStarted == lookups,
        "the network answering late starts nothing");

  printf("Wi-Fi setup: Cancel while the zone is looked up\n");
  netsim::status = WL_DISCONNECTED;
  netsim::taskMode = netsim::TaskMode::Hold;
  netsim::httpBody = "{\"status\":\"success\",\"timezone\":\"Asia/Tokyo\",\"offset\":32400}";
  tapButton(screens::Button::WifiSetup);
  settle(200);
  netsim::submitPortal("");
  runFor(100);
  netsim::status = WL_CONNECTED;
  CHECK(waitFor(net::Sync::Zone, 500) && phase() == "connecting", "waiting on the lookup (%s)", phase().c_str());
  tapButton(screens::Button::Cancel);
  CHECK(!net::syncRunning() && !net::radioOn(), "Cancel stops the sync");
  netsim::runTask();
  runFor(500);
  CHECK(!strcmp(net::zoneName(), "Europe/London") && netsim::configuredTz == "<+0100>-1:00",
        "the lookup's late answer is not used (%s)", net::zoneName());
  netsim::taskMode = netsim::TaskMode::RunNow;

  printf("Wi-Fi setup: Cancel while waiting for the time server\n");
  netsim::status = WL_DISCONNECTED;
  tapButton(screens::Button::WifiSetup);
  settle(200);
  netsim::submitPortal("");
  runFor(100);
  netsim::status = WL_CONNECTED;
  CHECK(waitFor(net::Sync::Clock, 500) && netsim::sntpRunning, "waiting for the time server");
  int64_t off = hal::nowMs() - truthMs();
  tapButton(screens::Button::Cancel);
  CHECK(!netsim::sntpRunning && !netsim::sntpAnswer(truthMs() + 3600000), "Cancel stops the client, a late answer is not taken");
  runFor(500);
  CHECK(llabs(hal::nowMs() - truthMs() - off) < 5 && net::timeIsValid(), "the clock is untouched and still valid");

  printf("Wi-Fi setup: saved, joined, done\n");
  netsim::status = WL_DISCONNECTED;
  tapButton(screens::Button::WifiSetup);
  settle(200);
  netsim::submitPortal("  EST5EDT,M3.2.0,M11.1.0 ");
  runFor(300);
  netsim::status = WL_CONNECTED;
  waitFor(net::Sync::Clock, 500);
  netsim::sntpAnswer(truthMs());
  settle(600);
  CHECK(net::lastResult() == net::Result::Ok && phase() == "done", "the sync succeeds and setup says so (%s)", phase().c_str());
  CHECK(settings.tz == "EST5EDT,M3.2.0,M11.1.0" && netsim::configuredTz == "EST5EDT,M3.2.0,M11.1.0",
        "the typed zone is saved and used (%s)", netsim::configuredTz.c_str());
  CHECK(tapButton(screens::Button::Done) && screen() == "face", "Done goes to the face (%s)", screen().c_str());

  printf("Wi-Fi setup: the hotspot times out on screen, and opens again\n");
  netsim::stationsOnHotspot = 0;  // the phone has left
  openSetup();
  runFor(301000);
  CHECK(net::portalState() == net::Portal::TimedOut && !netsim::portalOpen && !net::radioOn(), "it closes after 5 minutes");
  if (screen() != "setup") {
    touchDown(120, 120);
    touchUp();
    settle();
  }
  CHECK(phase() == "timed out", "setup says so (%s)", phase().c_str());
  bool tapped = tapButton(screens::Button::TryAgain);
  CHECK(tapped && netsim::portalOpen && net::portalState() == net::Portal::Waiting && net::portalSecondsLeft() == 300,
        "Try again opens it with the full 5 minutes (tapped %d, open %d, state %d, %u s)", tapped, netsim::portalOpen,
        (int)net::portalState(), net::portalSecondsLeft());
  pressBoot();
  pressBoot();
  settle();
  CHECK(screen() == "face" && !netsim::portalOpen && !net::radioOn(), "BOOT cancels it and closes settings (%s)", screen().c_str());
}

void quietResync() {
  printf("Screen off: the 6-hour resync runs quietly, without light sleep while the radio is on\n");
  sim::accel = {0, -1, 0};  // arm hanging
  power::options.lightSleep = true;
  runFor(20000);
  CHECK(screen() == "asleep", "asleep (%s)", screen().c_str());
  uint32_t t0 = sim::clockMs;
  while (!net::syncRunning() && sim::clockMs - t0 < 7UL * 3600 * 1000) runFor(1000);
  CHECK(net::syncRunning(), "a sync starts about 6 hours after the last (%.2f h)", (sim::clockMs - t0) / 3600000.0);
  int low = sim::lowPowerDozes;
  runFor(300);
  netsim::status = WL_CONNECTED;
  waitFor(net::Sync::Clock, 500);
  runFor(500);
  CHECK(sim::lowPowerDozes == low && net::radioOn(), "no light sleep while Wi-Fi is on (%d light sleeps)", sim::lowPowerDozes - low);
  netsim::sntpAnswer(truthMs());
  runFor(200);
  CHECK(net::lastResult() == net::Result::Ok && !net::radioOn(), "the resync finishes and the radio goes off");
  runFor(5000);
  CHECK(sim::lowPowerDozes > low, "light sleep is used again afterwards");
  CHECK(screen() == "asleep", "and the screen never came on (%s)", screen().c_str());
}

void retryAfterFailure() {
  printf("Screen off: a resync with no answer from the time server is retried after 30 minutes and recovers\n");
  uint32_t t0 = sim::clockMs;
  while (!net::syncRunning() && sim::clockMs - t0 < 7UL * 3600 * 1000) runFor(1000);
  netsim::status = WL_CONNECTED;
  runFor(11500);  // no answer: the sync gives up after 10 s
  CHECK(net::failReason() == net::Fail::ClockFailed && net::timeIsValid() && !net::radioOn(),
        "the sync fails, the clock stays valid and the radio goes off");
  netsim::status = WL_DISCONNECTED;
  t0 = sim::clockMs;
  while (!net::syncRunning() && sim::clockMs - t0 < 3600UL * 1000) runFor(1000);
  CHECK(net::syncRunning() && llabs((int32_t)(sim::clockMs - t0) - 30 * 60 * 1000) < 15000,
        "a retry starts about 30 minutes later (%.1f min)", (sim::clockMs - t0) / 60000.0);
  netsim::status = WL_CONNECTED;
  waitFor(net::Sync::Clock, 500);
  netsim::sntpAnswer(truthMs());
  runFor(200);
  CHECK(net::lastResult() == net::Result::Ok && screen() == "asleep", "and succeeds with the screen still off");
}

void warmRestart() {
  printf("Restart: the typed zone and network come back from flash\n");
  sim::cold = false;
  ui::begin();
  CHECK(settings.tz == "EST5EDT,M3.2.0,M11.1.0" && !strcmp(net::ssid(), "Cafe"), "zone and network kept (%s, %s)",
        settings.tz.c_str(), net::ssid());
  netsim::status = WL_CONNECTED;
  waitFor(net::Sync::Clock, 1000);
  netsim::sntpAnswer(truthMs());
  settle(2500);
  CHECK(net::lastResult() == net::Result::Ok && netsim::configuredTz == "EST5EDT,M3.2.0,M11.1.0",
        "the sync after the restart uses it (%s)", netsim::configuredTz.c_str());
}
}  // namespace

int main(int argc, char **argv) {
  outDir = argc > 1 ? argv[1] : ".";
  setenv("TZ", "UTC0", 1);
  tzset();
  startUp();
  setup();
  quietResync();
  retryAfterFailure();
  warmRestart();
  (void)PACIFIC;
  printf("%d of %d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
