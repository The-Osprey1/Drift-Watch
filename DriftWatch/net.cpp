#include "net.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include "hal.h"
#include "settings.h"
#include "timekeep.h"

namespace {
constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t ZONE_TIMEOUT_MS = 6000;
constexpr uint32_t NTP_TIMEOUT_MS = 10000;
constexpr uint32_t PORTAL_TIMEOUT_MS = 300000;  // 5 minutes
constexpr uint32_t TRIM_MS = 10000;             // how often the clock is nudged for drift

net::Sync syncNow = net::Sync::Idle;
net::Result result = net::Result::None;
net::Fail fail = net::Fail::None;
net::JoinWhy why = net::JoinWhy::NoAnswer;
int whyCode = 0;
volatile uint8_t disconnectReason = 0;  // written by the Wi-Fi event task
bool byHand = false;
uint16_t finished = 0;
net::Portal portal = net::Portal::Off;
uint32_t stateStart = 0, portalStart = 0;
time_t lastSyncTime = 0;
net::Source source = net::Source::None;
ClockTrim trim;
uint32_t lastTrim = 0;
int64_t clockStartWall = 0;  // wall clock (ms) when the time server was asked, to measure drift
uint32_t clockStartMs = 0;
char zone[40] = "";
char savedSsid[33] = "";
char posixTz[64] = "UTC0";

WiFiManager *wm = nullptr;
WiFiManagerParameter *tzParam = nullptr;
WiFiManagerParameter *bandNote = nullptr;  // text only; kept for the life of the program
bool portalSaved = false;

// ---- Time zone lookup ------------------------------------------------------
// The HTTP request can take seconds, so it runs once in its own short-lived
// task on core 0 (where Wi-Fi already runs) and the loop just checks the flag.
volatile bool zoneDone = false, zoneOk = false;
volatile bool zoneTaskRunning = false;  // a cancelled sync can leave one finishing in the background
char zoneResultName[40], zoneResultPosix[64];

// Turns a UTC offset in seconds into a fixed POSIX zone. POSIX signs are
// backwards: UTC+1 is written "-1".
void posixFromOffset(long offset, char *out, size_t n) {
  long a = labs(offset);
  snprintf(out, n, "<%c%02ld%02ld>%c%ld:%02ld", offset < 0 ? '-' : '+', a / 3600, (a % 3600) / 60,
           offset < 0 ? '+' : '-', a / 3600, (a % 3600) / 60);
}

// Copies a value out of a flat JSON reply without a JSON library.
bool jsonField(const char *body, const char *key, char *out, size_t n) {
  char k[24];
  snprintf(k, sizeof k, "\"%s\":", key);
  const char *p = strstr(body, k);
  if (!p) return false;
  p += strlen(k);
  bool quoted = *p == '"';
  if (quoted) p++;
  size_t i = 0;
  while (*p && *p != (quoted ? '"' : ',') && *p != '}' && i + 1 < n) out[i++] = *p++;
  out[i] = 0;
  return true;
}

void zoneTask(void *) {
  HTTPClient http;
  http.setConnectTimeout(ZONE_TIMEOUT_MS / 2);
  http.setTimeout(ZONE_TIMEOUT_MS / 2);
  bool ok = false;
  if (http.begin("http://ip-api.com/json/?fields=status,timezone,offset") && http.GET() == 200) {
    String body = http.getString();
    char status[12], offset[12];
    ok = jsonField(body.c_str(), "status", status, sizeof status) && !strcmp(status, "success") &&
         jsonField(body.c_str(), "timezone", zoneResultName, sizeof zoneResultName) &&
         jsonField(body.c_str(), "offset", offset, sizeof offset);
    if (ok) posixFromOffset(atol(offset), zoneResultPosix, sizeof zoneResultPosix);
  }
  http.end();
  zoneOk = ok;
  zoneDone = true;  // written last; the loop reads the results only after seeing this
  zoneTaskRunning = false;
  vTaskDelete(nullptr);
}

// ---- Helpers ---------------------------------------------------------------
// Reads the network name saved in flash. Wi-Fi must be started for this.
void readSavedSsid() {
  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) strlcpy(savedSsid, (const char *)conf.sta.ssid, sizeof savedSsid);
}

void wifiOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void setState(net::Sync s) {
  syncNow = s;
  stateStart = hal::ms();
}

// Groups the radio's disconnect reasons (esp_wifi_types.h) by what the person can do.
net::JoinWhy classify(uint8_t r) {
  switch (r) {
    case 0: return net::JoinWhy::NoAnswer;  // still trying when the time ran out
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_BEACON_TIMEOUT: return net::JoinWhy::NotFound;
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_802_1X_AUTH_FAILED: return net::JoinWhy::Password;
    default: return net::JoinWhy::Refused;  // includes AUTH_EXPIRE (2), common with band-steering mesh routers
  }
}

void finish(net::Fail f) {
  if (f == net::Fail::JoinFailed) {
    whyCode = disconnectReason;
    why = classify(whyCode);
    hal::log("net: join failed, Wi-Fi reason %d (group %d)", whyCode, (int)why);
  }
  if (f == net::Fail::None) byHand = false;
  fail = f;
  result = f == net::Fail::None ? net::Result::Ok : net::Result::Failed;
  finished++;
  setState(net::Sync::Idle);
  wifiOff();
  hal::log("net: sync %s (reason %d)", f == net::Fail::None ? "done" : "failed", (int)f);
}

// A trusted time was just applied. `errorMs` is how far ahead the watch had been.
void learnDrift(int64_t errorMs) {
  if (trim.learn(hal::nowMs(), errorMs)) {
    settings.driftPpb = trim.ppb();
    saveSettings();
  }
  hal::log("net: clock was %lld ms %s, drift now %ld ppb", (long long)(errorMs < 0 ? -errorMs : errorMs),
           errorMs >= 0 ? "fast" : "slow", (long)trim.ppb());
}

void startClock() {
  clockStartWall = hal::nowMs();
  clockStartMs = hal::ms();
  setenv("TZ", posixTz, 1);
  tzset();
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  configTzTime(posixTz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  setState(net::Sync::Clock);
}

void closePortal(net::Portal endState) {
  if (wm) {
    wm->stopConfigPortal();
    delete wm;
    delete tzParam;
    wm = nullptr;
    tzParam = nullptr;
  }
  portal = endState;
  wifiOff();
}
}  // namespace

namespace net {

void begin() {
  trim.setPpb(settings.driftPpb);
  if (!settings.tz.isEmpty()) strlcpy(posixTz, settings.tz.c_str(), sizeof posixTz);
  setenv("TZ", posixTz, 1);
  tzset();
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) { disconnectReason = info.wifi_sta_disconnected.reason; },
               ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  readSavedSsid();
  wifiOff();
}

bool hasSavedWifi() { return savedSsid[0] != 0; }

void startSync() {
  if (portal == Portal::Waiting || portal == Portal::PhoneJoined) return;
  if (syncRunning()) return;
  if (!hasSavedWifi()) {
    finish(Fail::NoNetworkSaved);
    return;
  }
  disconnectReason = 0;
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  setState(Sync::Connecting);
}

void poll() {
  uint32_t elapsed = hal::ms() - stateStart;
  switch (syncNow) {
    case Sync::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        if (settings.tz.isEmpty() && !zoneTaskRunning) {
          zoneDone = zoneOk = false;
          zoneTaskRunning = true;
          setState(Sync::Zone);
          if (xTaskCreatePinnedToCore(zoneTask, "zone", 6144, nullptr, 1, nullptr, 0) != pdPASS) {
            zoneTaskRunning = false;
            startClock();
          }
        } else if (settings.tz.isEmpty()) {
          startClock();  // an earlier lookup is still finishing; keep the last known zone
        } else {
          strlcpy(posixTz, settings.tz.c_str(), sizeof posixTz);
          startClock();
        }
      } else if (elapsed > CONNECT_TIMEOUT_MS) {
        finish(Fail::JoinFailed);
      }
      break;
    case Sync::Zone:
      // The task always finishes within its own timeouts; the extra margin only
      // guards against a stuck socket.
      if (zoneDone || elapsed > ZONE_TIMEOUT_MS + 2000) {
        if (zoneDone && zoneOk) {
          strlcpy(zone, zoneResultName, sizeof zone);
          strlcpy(posixTz, zoneResultPosix, sizeof posixTz);
          hal::log("net: zone %s -> %s", zone, posixTz);
        }
        startClock();  // falls back to the last known zone, or UTC
      }
      break;
    case Sync::Clock:
      if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        lastSyncTime = time(nullptr);
        learnDrift(clockStartWall + (int64_t)(hal::ms() - clockStartMs) - hal::nowMs());
        source = Source::Wifi;
        finish(Fail::None);
      } else if (elapsed > NTP_TIMEOUT_MS) {
        finish(Fail::ClockFailed);
      }
      break;
    default:
      break;
  }

  // Nudge the clock for the drift learned from earlier syncs.
  if (hal::ms() - lastTrim >= TRIM_MS) {
    lastTrim = hal::ms();
    if (timeIsValid()) {
      int64_t step = trim.due(hal::nowMs());
      if (step) hal::adjustMs(step);
    }
  }

  if (wm) {
    wm->process();
    if (portalSaved) {
      settings.tz = tzParam->getValue();
      settings.tz.trim();
      saveSettings();
      readSavedSsid();
      closePortal(Portal::Saved);
      startSync();
    } else if (hal::ms() - portalStart > PORTAL_TIMEOUT_MS) {
      closePortal(Portal::TimedOut);
    } else {
      portal = WiFi.softAPgetStationNum() > 0 ? Portal::PhoneJoined : Portal::Waiting;
    }
  }
}

void cancelSync() {
  if (!syncRunning()) return;
  wifiOff();
  setState(Sync::Idle);
  hal::log("net: sync cancelled");
}

Sync syncState() { return syncNow; }
bool syncRunning() { return syncNow != Sync::Idle; }
Result lastResult() { return result; }
Fail failReason() { return fail; }
uint16_t attempts() { return finished; }
int syncStep() { return syncNow == Sync::Connecting ? 1 : syncNow == Sync::Zone ? 2 : syncNow == Sync::Clock ? 3 : 0; }
JoinWhy joinWhy() { return why; }
int joinCode() { return whyCode; }
bool radioOn() { return syncRunning() || wm != nullptr; }

void startPortal() {
  if (wm) return;
  if (syncRunning()) {
    wifiOff();
    setState(Sync::Idle);
  }
  // Start straight in hotspot mode. v0.8.0 switched to station mode here, and the library
  // then switched station mode off again, which stopped and restarted the radio just before
  // the hotspot came up and could leave it not broadcasting.
  WiFi.mode(WIFI_AP);
  // Worldwide settings: channels 1 to 11 are allowed everywhere, so any phone can see the
  // hotspot whatever country it is set to.
  wifi_country_t country = {};
  strcpy(country.cc, "01");
  country.schan = 1;
  country.nchan = 11;
  country.policy = WIFI_COUNTRY_POLICY_MANUAL;
  esp_wifi_set_country(&country);
  wm = new WiFiManager();
  // Spelled out rather than left to the library's defaults. The library already answers every
  // name lookup with this address and sends phones' "am I online?" checks to its page, which
  // is what makes iPhones and Android phones pop the setup page up by themselves.
  wm->setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  wm->setWiFiAPChannel(1);
  if (!bandNote)
    bandNote = new WiFiManagerParameter(
        "<p><b>This watch only works with 2.4 GHz Wi-Fi.</b> Only networks it can use are listed. "
        "If yours is missing, turn on 2.4 GHz on your router (some mesh routers call it a "
        "compatibility or IoT network).</p>");
  tzParam = new WiFiManagerParameter("tz", "Time zone (leave blank to detect automatically)", settings.tz.c_str(), 63);
  wm->setTitle("Drift-Watch");
  wm->addParameter(bandNote);
  wm->addParameter(tzParam);
  wm->setBreakAfterConfig(true);
  // Store the new network without joining it inside the library: its join waits in a
  // loop (up to 60 s by default) and would freeze the screen. Our own sync joins it
  // step by step instead. The library still waits once for this timeout (1 s minimum).
  wm->setSaveConnect(false);
  wm->setSaveConnectTimeout(1);
  portalSaved = false;
  wm->setSaveConfigCallback([] { portalSaved = true; });
  wm->setConfigPortalBlocking(false);
  wm->startConfigPortal(SETUP_AP_NAME);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // full power while the hotspot is open
  hal::log("net: hotspot %s %s at %s", SETUP_AP_NAME, (WiFi.getMode() & WIFI_AP) ? "up" : "NOT up",
           WiFi.softAPIP().toString().c_str());
  portal = Portal::Waiting;
  portalStart = hal::ms();
}

void cancelPortal() {
  if (wm) closePortal(Portal::Closed);
}

Portal portalState() { return portal; }

uint32_t portalSecondsLeft() {
  if (!wm) return 0;
  uint32_t used = hal::ms() - portalStart;
  return used >= PORTAL_TIMEOUT_MS ? 0 : (PORTAL_TIMEOUT_MS - used + 999) / 1000;
}

bool timeIsValid() { return time(nullptr) > 1700000000; }  // later than Nov 2023

void setClockByHand(time_t t) {
  hal::setTime(t);
  byHand = true;
  trim.forget();  // a hand setting is too rough to measure drift against
  hal::log("net: clock set by hand");
}

void setClockFromPhone(time_t utc, int ms) {
  int64_t trueMs = (int64_t)utc * 1000 + ms;
  int64_t errorMs = hal::nowMs() - trueMs;
  bool wasValid = timeIsValid();
  hal::setTimeMs(trueMs);
  if (wasValid) learnDrift(errorMs);  // a clock that was never set says nothing about drift
  lastSyncTime = utc;
  source = Source::Phone;
  byHand = false;
  fail = Fail::None;
  result = Result::Ok;
  finished++;
  hal::log("net: clock set from phone");
}

Source lastSource() { return source; }

void setZone(const char *posix, const char *name) {
  if (posix) {
    settings.tz = posix;
    saveSettings();
    strlcpy(posixTz, posix, sizeof posixTz);
    setenv("TZ", posixTz, 1);
    tzset();
  }
  if (name) strlcpy(zone, name, sizeof zone);
}

void saveNetwork(const char *newSsid, const char *password) {
  if (wm) closePortal(Portal::Closed);
  if (syncRunning()) cancelSync();
  WiFi.mode(WIFI_STA);
  wifi_config_t conf = {};
  strlcpy((char *)conf.sta.ssid, newSsid, sizeof conf.sta.ssid);
  strlcpy((char *)conf.sta.password, password, sizeof conf.sta.password);
  esp_wifi_set_storage(WIFI_STORAGE_FLASH);
  esp_wifi_set_config(WIFI_IF_STA, &conf);
  readSavedSsid();
  wifiOff();
  hal::log("net: network %s saved from phone", savedSsid);
}

int32_t driftPpb() { return trim.ppb(); }
bool clockSetByHand() { return byHand; }
time_t lastSync() { return lastSyncTime; }
const char *zoneName() { return zone; }
const char *ssid() { return savedSsid; }

}  // namespace net
