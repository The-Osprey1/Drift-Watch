// Stand-in for the ESP-IDF Wi-Fi calls net.cpp makes. See netsim.h.
#pragma once
#include <stdint.h>

typedef int esp_err_t;
constexpr esp_err_t ESP_OK = 0;
enum wifi_interface_t { WIFI_IF_STA = 0, WIFI_IF_AP = 1 };
enum wifi_storage_t { WIFI_STORAGE_FLASH = 0, WIFI_STORAGE_RAM = 1 };
enum wifi_country_policy_t { WIFI_COUNTRY_POLICY_AUTO = 0, WIFI_COUNTRY_POLICY_MANUAL = 1 };
struct wifi_country_t {
  char cc[3];
  uint8_t schan, nchan;
  int8_t max_tx_power;
  wifi_country_policy_t policy;
};
union wifi_config_t {
  struct {
    uint8_t ssid[32];
    uint8_t password[64];
  } sta;
};
// Disconnect reasons (esp_wifi_types.h), the ones net.cpp names.
enum {
  WIFI_REASON_AUTH_EXPIRE = 2,
  WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT = 15,
  WIFI_REASON_MIC_FAILURE = 14,
  WIFI_REASON_802_1X_AUTH_FAILED = 23,
  WIFI_REASON_BEACON_TIMEOUT = 200,
  WIFI_REASON_NO_AP_FOUND = 201,
  WIFI_REASON_AUTH_FAIL = 202,
  WIFI_REASON_HANDSHAKE_TIMEOUT = 204,
};

esp_err_t esp_wifi_get_config(wifi_interface_t iface, wifi_config_t *conf);
esp_err_t esp_wifi_set_config(wifi_interface_t iface, wifi_config_t *conf);
esp_err_t esp_wifi_set_storage(wifi_storage_t storage);
esp_err_t esp_wifi_set_country(const wifi_country_t *country);
