// The Wi-Fi, web and time-server stand-ins behind the headers in this folder.
#include "netsim.h"
#include <string.h>
#include "HTTPClient.h"
#include "WiFi.h"
#include "WiFiManager.h"
#include "esp_sntp.h"
#include "esp_wifi.h"

namespace netsim {
int status = WL_DISCONNECTED;
int mode = WIFI_OFF;
int begins = 0;
std::string flashSsid, flashPassword;
int stationsOnHotspot = 0;
bool httpBeginOk = true;
int httpCode = 200;
std::string httpBody, httpUrl;
int httpGets = 0;
TaskMode taskMode = TaskMode::RunNow;
int tasksStarted = 0;
int sntpStatus = SNTP_SYNC_STATUS_RESET;
std::string configuredTz;
bool portalOpen = false;
std::string portalTitle;

namespace {
TaskFunction_t heldTask = nullptr;
void *heldArg = nullptr;
WiFiManager *openPortal = nullptr;
}  // namespace

void dropConnection(uint8_t reason) {
  status = WL_DISCONNECTED;
  if (WiFi.handler) {
    WiFiEventInfo_t info = {};
    info.wifi_sta_disconnected.reason = reason;
    WiFi.handler(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, info);
  }
}

bool runTask() {
  if (!heldTask) return false;
  TaskFunction_t fn = heldTask;
  heldTask = nullptr;
  fn(heldArg);
  return true;
}

void submitPortal(const char *tzField) {
  if (!openPortal) return;
  for (WiFiManagerParameter *p : openPortal->params)
    if (!strcmp(p->getID(), "tz")) p->setValue(tzField);
  if (openPortal->saved) openPortal->saved();
}

void reset() {
  status = WL_DISCONNECTED;
  mode = WIFI_OFF;
  begins = 0;
  flashSsid.clear();
  flashPassword.clear();
  stationsOnHotspot = 0;
  httpBeginOk = true;
  httpCode = 200;
  httpBody.clear();
  httpUrl.clear();
  httpGets = 0;
  taskMode = TaskMode::RunNow;
  tasksStarted = 0;
  heldTask = nullptr;
  sntpStatus = SNTP_SYNC_STATUS_RESET;
  configuredTz.clear();
}
}  // namespace netsim

WiFiClass WiFi;

bool WiFiClass::mode(int m) {
  netsim::mode = m;
  if (!(m & WIFI_STA)) netsim::status = WL_DISCONNECTED;
  return true;
}
int WiFiClass::begin() {
  netsim::begins++;
  return netsim::status;
}
int WiFiClass::begin(const char *ssid, const char *password) {
  netsim::flashSsid = ssid;
  netsim::flashPassword = password;
  return begin();
}
bool WiFiClass::disconnect(bool wifiOff) {
  netsim::status = WL_DISCONNECTED;
  if (wifiOff) netsim::mode = WIFI_OFF;
  return true;
}

int xTaskCreatePinnedToCore(TaskFunction_t fn, const char *, uint32_t, void *arg, int, TaskHandle_t *, int) {
  if (netsim::taskMode == netsim::TaskMode::Refuse) return 0;
  netsim::tasksStarted++;
  if (netsim::taskMode == netsim::TaskMode::Hold) {
    netsim::heldTask = fn;
    netsim::heldArg = arg;
  } else {
    fn(arg);
  }
  return pdPASS;
}

esp_err_t esp_wifi_get_config(wifi_interface_t, wifi_config_t *conf) {
  memset(conf, 0, sizeof *conf);
  strncpy((char *)conf->sta.ssid, netsim::flashSsid.c_str(), sizeof conf->sta.ssid);
  strncpy((char *)conf->sta.password, netsim::flashPassword.c_str(), sizeof conf->sta.password);
  return ESP_OK;
}
esp_err_t esp_wifi_set_config(wifi_interface_t, wifi_config_t *conf) {
  netsim::flashSsid.assign((const char *)conf->sta.ssid, strnlen((const char *)conf->sta.ssid, sizeof conf->sta.ssid));
  netsim::flashPassword.assign((const char *)conf->sta.password,
                               strnlen((const char *)conf->sta.password, sizeof conf->sta.password));
  return ESP_OK;
}
esp_err_t esp_wifi_set_storage(wifi_storage_t) { return ESP_OK; }
esp_err_t esp_wifi_set_country(const wifi_country_t *) { return ESP_OK; }

sntp_sync_status_t sntp_get_sync_status() { return (sntp_sync_status_t)netsim::sntpStatus; }
void sntp_set_sync_status(sntp_sync_status_t s) { netsim::sntpStatus = s; }
void configTzTime(const char *tz, const char *, const char *, const char *) { netsim::configuredTz = tz; }

WiFiManager::WiFiManager() { netsim::openPortal = this; }
WiFiManager::~WiFiManager() {
  if (netsim::openPortal == this) netsim::openPortal = nullptr;
  netsim::portalOpen = false;
}
void WiFiManager::setTitle(const String &t) { netsim::portalTitle = t.c_str(); }
bool WiFiManager::startConfigPortal(const char *) {
  netsim::portalOpen = true;
  return false;
}
void WiFiManager::stopConfigPortal() { netsim::portalOpen = false; }
