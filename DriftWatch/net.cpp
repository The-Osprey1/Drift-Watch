#include "net.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <atomic>
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
std::atomic<uint8_t> disconnectReason{0};  // written by the Wi-Fi event task
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
char joiningSsid[33] = "";
bool triedAltQuote = false;
constexpr const char *DEFAULT_POSIX_TZ = "PST8PDT,M3.2.0,M11.1.0";  // US Pacific, until Auto finds the zone
char posixTz[64] = "UTC0";

WiFiManager *wm = nullptr;
WiFiManagerParameter *tzParam = nullptr;   // kept for the life of the program too: the library
                                           // class has no virtual destructor, so it is never deleted
WiFiManagerParameter *bandNote = nullptr;  // text only; kept for the life of the program
bool portalSaved = false;

// ---- Time zone lookup ------------------------------------------------------
// The HTTP request can take seconds, so it runs once in its own short-lived task on core 0
// (where Wi-Fi already runs) and the loop just checks for its answer. Each lookup has a
// number (its epoch). The task writes its answer into the slot and then publishes its epoch;
// the loop reads the slot only when the published epoch is the one the running sync is waiting
// for. A sync that was cancelled or gave up waiting sets that to 0, so a late answer is never
// used. Only one task runs at a time, so the slot is never written while the loop reads it.
struct ZoneAnswer {
  bool ok;
  char name[40], posix[64];
};
ZoneAnswer zoneSlot;
std::atomic<uint32_t> zoneReady{0};          // epoch of the answer now in zoneSlot
std::atomic<bool> zoneTaskRunning{false};    // a cancelled sync can leave one finishing in the background
uint32_t zoneEpoch = 0, lastEpoch = 0;       // the lookup the running sync waits for (0 = none)

constexpr long MIN_OFFSET_S = -12 * 3600, MAX_OFFSET_S = 14 * 3600;  // the real range of UTC offsets

// Turns a UTC offset in seconds into a fixed POSIX zone. POSIX signs are
// backwards: UTC+1 is written "-1".
void posixFromOffset(long offset, char *out, size_t n) {
  long a = labs(offset);
  snprintf(out, n, "<%c%02ld%02ld>%c%ld:%02ld", offset < 0 ? '-' : '+', a / 3600, (a % 3600) / 60,
           offset < 0 ? '+' : '-', a / 3600, (a % 3600) / 60);
}

const char *skipSpace(const char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return p;
}

// True when the reply is one whole object: a reply cut short on a weak connection is not used.
bool wholeObject(const char *body) {
  const char *p = skipSpace(body), *end = body + strlen(body);
  while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
  return *p == '{' && end - p >= 2 && end[-1] == '}';
}

// Copies one value out of the flat JSON object the lookup service sends, without a JSON
// library. Spaces are allowed around the colon; a string must end with its closing quote and
// a number must be a whole number, each followed by a comma or the closing brace.
bool jsonField(const char *body, const char *key, bool quoted, char *out, size_t n) {
  char k[24];
  snprintf(k, sizeof k, "\"%s\"", key);
  for (const char *p = strstr(body, k); p; p = strstr(p + 1, k)) {
    const char *q = skipSpace(p + strlen(k));
    if (*q != ':') continue;  // the same word inside a value, not the key
    q = skipSpace(q + 1);
    size_t i = 0;
    if (quoted) {
      if (*q++ != '"') return false;
      while (*q && *q != '"') {
        if (*q == '\\' || i + 1 >= n) return false;  // no escapes in a zone name; too long to keep
        out[i++] = *q++;
      }
      if (*q++ != '"') return false;
    } else {
      if (*q == '-') out[i++] = *q++;
      while (*q >= '0' && *q <= '9') {
        if (i + 1 >= n) return false;
        out[i++] = *q++;
      }
      if (i == 0 || out[i - 1] == '-') return false;
    }
    out[i] = 0;
    q = skipSpace(q);
    return *q == ',' || *q == '}';
  }
  return false;
}

// Reads the lookup service's reply. Only a whole, successful reply with a real offset is used.
bool parseZoneReply(const char *body, ZoneAnswer &a) {
  char status[12], offset[12];
  if (!wholeObject(body) || !jsonField(body, "status", true, status, sizeof status) || strcmp(status, "success") ||
      !jsonField(body, "timezone", true, a.name, sizeof a.name) || !a.name[0] ||
      !jsonField(body, "offset", false, offset, sizeof offset))
    return false;
  long off = strtol(offset, nullptr, 10);
  // Every zone in use is a whole number of quarter hours from UTC, within -12 h to +14 h.
  if (off < MIN_OFFSET_S || off > MAX_OFFSET_S || off % 900) return false;
  posixFromOffset(off, a.posix, sizeof a.posix);
  return true;
}

void zoneTask(void *arg) {
  uint32_t epoch = (uint32_t)(uintptr_t)arg;
  ZoneAnswer a = {};
  HTTPClient http;
  http.setConnectTimeout(ZONE_TIMEOUT_MS / 2);
  http.setTimeout(ZONE_TIMEOUT_MS / 2);
  if (http.begin("http://ip-api.com/json/?fields=status,timezone,offset") && http.GET() == 200) {
    String body = http.getString();
    a.ok = parseZoneReply(body.c_str(), a);
  }
  http.end();
  zoneSlot = a;
  zoneReady.store(epoch, std::memory_order_release);  // after the slot: the loop reads it only after seeing this
  zoneTaskRunning.store(false, std::memory_order_release);
  vTaskDelete(nullptr);
}

// ---- Helpers ---------------------------------------------------------------
void copyStaField(uint8_t *dst, size_t dstSize, const char *src) {
  memset(dst, 0, dstSize);
  size_t n = strlen(src);
  if (n > dstSize) n = dstSize;
  memcpy(dst, src, n);
}

// Sets the station fields that a zeroed wifi_config_t (or WiFiManager's save) leaves off:
// all-channel scan for mesh routers, Protected Management Frames (802.11w) for WPA2/WPA3
// routers, and WPA3 SAE hash-to-element (H2E) for iPhone Personal Hotspots.
void fillStationDefaults(wifi_config_t &conf) {
  conf.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  conf.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  conf.sta.threshold.rssi = -127;
  conf.sta.threshold.authmode = conf.sta.password[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
  conf.sta.pmf_cfg.capable = true;
  conf.sta.pmf_cfg.required = false;
  conf.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
}

// iPhones name their hotspot with a curly apostrophe (U+2019, UTF-8 E2 80 99) and iOS Smart
// Punctuation types one in web forms, while computer keyboards and most routers use straight
// ASCII "'". When an SSID with one is not found, the join tries the other once.
bool flipApostrophe(const char *in, char *out, size_t outSize) {
  bool hasCurly = strstr(in, "\xe2\x80\x99") || strstr(in, "\xe2\x80\x98");
  bool hasStraight = strchr(in, '\'') != nullptr;
  if (!hasCurly && !hasStraight) return false;
  size_t j = 0;
  for (size_t i = 0; in[i];) {
    if (hasCurly && (uint8_t)in[i] == 0xe2 && (uint8_t)in[i + 1] == 0x80 &&
        ((uint8_t)in[i + 2] == 0x99 || (uint8_t)in[i + 2] == 0x98)) {
      if (j + 1 >= outSize) return false;
      out[j++] = '\'';
      i += 3;
    } else if (!hasCurly && in[i] == '\'') {
      if (j + 3 >= outSize) return false;
      memcpy(out + j, "\xe2\x80\x99", 3);
      j += 3;
      i++;
    } else {
      if (j + 1 >= outSize) return false;
      out[j++] = in[i++];
    }
  }
  out[j] = 0;
  return true;
}

// Reads the network name saved in flash. Wi-Fi must be started for this.
void readSavedSsid() {
  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
    size_t n = strnlen((const char *)conf.sta.ssid, sizeof conf.sta.ssid);
    memcpy(savedSsid, conf.sta.ssid, n);
    savedSsid[n] = 0;
  }
}

void restoreSavedSsid() {
  if (!joiningSsid[0] || !strcmp(joiningSsid, savedSsid)) return;
  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
    copyStaField(conf.sta.ssid, sizeof conf.sta.ssid, savedSsid);
    fillStationDefaults(conf);
    esp_wifi_set_config(WIFI_IF_STA, &conf);
  }
  joiningSsid[0] = 0;
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

// Stops the time server client, so an answer that comes late cannot set the clock.
void stopClock() {
  if (sntp_enabled()) sntp_stop();
}

void finish(net::Fail f) {
  if (f == net::Fail::JoinFailed) {
    whyCode = disconnectReason.load();
    why = classify(whyCode);
    hal::log("net: join failed, Wi-Fi reason %d (group %d)", whyCode, (int)why);
  }
  if (f == net::Fail::None) byHand = false;
  else restoreSavedSsid();
  fail = f;
  result = f == net::Fail::None ? net::Result::Ok : net::Result::Failed;
  finished++;
  setState(net::Sync::Idle);
  zoneEpoch = 0;
  stopClock();
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
  zoneEpoch = 0;  // an answer still to come from the lookup is not used
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
    wm = nullptr;
  }
  portal = endState;
  wifiOff();
}
}  // namespace

namespace net {

void saveWifi(const char *ssid, const char *password) {
  closePortal(Portal::Saved);
  WiFi.disconnect(true);
  delay(50);
  disconnectReason.store(0);
  triedAltQuote = false;
  WiFi.mode(WIFI_STA);
  wifi_config_t conf = {};
  copyStaField(conf.sta.ssid, sizeof conf.sta.ssid, ssid);
  copyStaField(conf.sta.password, sizeof conf.sta.password, password);
  fillStationDefaults(conf);
  esp_wifi_set_storage(WIFI_STORAGE_FLASH);
  esp_wifi_set_config(WIFI_IF_STA, &conf);
  readSavedSsid();
  strlcpy(joiningSsid, savedSsid, sizeof joiningSsid);
  WiFi.begin();
  setState(Sync::Connecting);
  hal::log("net: configured Wi-Fi for %s", savedSsid);
}

void begin() {
  trim.setPpb(settings.driftPpb);
  // An empty settings.tz means Auto (look the zone up on each sync), so the Pacific default
  // is only the zone in use until a lookup answers. It is never saved: saving it would turn
  // Auto into a fixed zone on the first start.
  strlcpy(posixTz, settings.tz.isEmpty() ? DEFAULT_POSIX_TZ : settings.tz.c_str(), sizeof posixTz);
  setenv("TZ", posixTz, 1);
  tzset();
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) { disconnectReason.store(info.wifi_sta_disconnected.reason); },
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
  disconnectReason.store(0);
  triedAltQuote = false;
  strlcpy(joiningSsid, savedSsid, sizeof joiningSsid);
  WiFi.mode(WIFI_STA);
  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
    fillStationDefaults(conf);
    esp_wifi_set_config(WIFI_IF_STA, &conf);
  }
  WiFi.begin();
  setState(Sync::Connecting);
}

void poll() {
  uint32_t elapsed = hal::ms() - stateStart;
  switch (syncNow) {
    case Sync::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        if (joiningSsid[0] && strcmp(joiningSsid, savedSsid) != 0) strlcpy(savedSsid, joiningSsid, sizeof savedSsid);
        if (settings.tz.isEmpty() && !zoneTaskRunning.load(std::memory_order_acquire)) {
          if (++lastEpoch == 0) lastEpoch = 1;  // never 0, which means none
          zoneEpoch = lastEpoch;
          zoneTaskRunning.store(true);
          setState(Sync::Zone);
          if (xTaskCreatePinnedToCore(zoneTask, "zone", 6144, (void *)(uintptr_t)zoneEpoch, 1, nullptr, 0) != pdPASS) {
            zoneTaskRunning.store(false);
            startClock();
          }
        } else if (settings.tz.isEmpty()) {
          startClock();  // an earlier lookup is still finishing; keep the last known zone
        } else {
          strlcpy(posixTz, settings.tz.c_str(), sizeof posixTz);
          startClock();
        }
      } else if (!triedAltQuote && disconnectReason.load() == WIFI_REASON_NO_AP_FOUND) {
        triedAltQuote = true;
        char alt[33];
        if (flipApostrophe(savedSsid, alt, sizeof alt)) {
          WiFi.disconnect(false);
          disconnectReason.store(0);
          wifi_config_t conf = {};
          if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
            copyStaField(conf.sta.ssid, sizeof conf.sta.ssid, alt);
            fillStationDefaults(conf);
            esp_wifi_set_config(WIFI_IF_STA, &conf);
          }
          strlcpy(joiningSsid, alt, sizeof joiningSsid);
          WiFi.begin();
          stateStart = hal::ms();
          hal::log("net: %s not found, trying %s", savedSsid, joiningSsid);
        }
      } else if (elapsed > CONNECT_TIMEOUT_MS) {
        finish(Fail::JoinFailed);
      }
      break;
    case Sync::Zone: {
      // The task always finishes within its own timeouts; the extra margin only
      // guards against a stuck socket.
      bool answered = zoneEpoch && zoneReady.load(std::memory_order_acquire) == zoneEpoch;
      if (answered || elapsed > ZONE_TIMEOUT_MS + 2000) {
        if (answered && zoneSlot.ok) {
          strlcpy(zone, zoneSlot.name, sizeof zone);
          strlcpy(posixTz, zoneSlot.posix, sizeof posixTz);
          hal::log("net: zone %s -> %s", zone, posixTz);
        }
        startClock();  // falls back to the last known zone, or the Pacific default
      }
      break;
    }
    case Sync::Clock:
      // The time server client sets the clock first and only then reports the sync as done.
      if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        lastSyncTime = hal::now();
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
  zoneEpoch = 0;
  stopClock();
  restoreSavedSsid();
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
  if (syncRunning()) cancelSync();
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
        "compatibility or IoT network). For an iPhone Personal Hotspot, turn on Maximize "
        "Compatibility in Settings &gt; Personal Hotspot.</p>");
  if (!tzParam) tzParam = new WiFiManagerParameter("tz", "Time zone (leave blank to detect automatically)", "", 63);
  tzParam->setValue(settings.tz.c_str(), 63);  // the setup page opens with the zone in use now
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

bool timeIsValid() { return hal::now() > 1700000000; }  // later than Nov 2023

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
  copyStaField(conf.sta.ssid, sizeof conf.sta.ssid, newSsid);
  copyStaField(conf.sta.password, sizeof conf.sta.password, password);
  fillStationDefaults(conf);
  esp_wifi_set_storage(WIFI_STORAGE_FLASH);
  esp_wifi_set_config(WIFI_IF_STA, &conf);
  readSavedSsid();
  joiningSsid[0] = 0;
  wifiOff();
  hal::log("net: network %s saved from phone", savedSsid);
}

int32_t driftPpb() { return trim.ppb(); }
bool clockSetByHand() { return byHand; }
time_t lastSync() { return lastSyncTime; }
const char *zoneName() { return zone; }
const char *ssid() { return savedSsid; }

}  // namespace net
