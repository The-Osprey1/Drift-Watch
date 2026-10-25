// Stand-in for the ESP32 WiFi library: just what net.cpp calls. See netsim.h.
#pragma once
#include <Arduino.h>
#include <stdio.h>
#include <functional>
#include "esp_wifi.h"
#include "netsim.h"

// FreeRTOS, which Arduino.h brings in on the watch.
typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;
constexpr int pdPASS = 1;
int xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, int prio,
                            TaskHandle_t *handle, int core);
inline void vTaskDelete(TaskHandle_t) {}  // the task function returns straight after

enum { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_DISCONNECTED = 6 };
enum { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 };
enum wifi_power_t { WIFI_POWER_19_5dBm = 78 };
enum WiFiEvent_t { ARDUINO_EVENT_WIFI_STA_DISCONNECTED = 5 };
struct WiFiEventInfo_t {
  struct {
    uint8_t reason;
  } wifi_sta_disconnected;
};

class IPAddress {
 public:
  IPAddress(uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint8_t d = 0) : a_{a, b, c, d} {}
  String toString() const {
    char s[16];
    snprintf(s, sizeof s, "%u.%u.%u.%u", a_[0], a_[1], a_[2], a_[3]);
    return String(s);
  }

 private:
  uint8_t a_[4];
};

class WiFiClass {
 public:
  using Handler = std::function<void(WiFiEvent_t, WiFiEventInfo_t)>;
  bool mode(int m);
  int getMode() { return netsim::mode; }
  int status() { return netsim::status; }
  int begin();
  int begin(const char *ssid, const char *password);
  bool disconnect(bool wifiOff = false);
  void onEvent(Handler h, WiFiEvent_t) { handler = h; }
  bool setTxPower(wifi_power_t) { return true; }
  IPAddress softAPIP() { return netsim::mode & WIFI_AP ? IPAddress(192, 168, 4, 1) : IPAddress(); }
  int softAPgetStationNum() { return netsim::stationsOnHotspot; }
  Handler handler;
};
extern WiFiClass WiFi;
