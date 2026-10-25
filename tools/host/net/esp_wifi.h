// Stand-in for the ESP-IDF Wi-Fi calls net.cpp makes. See netsim.h.
#pragma once
#include <stdint.h>

typedef int esp_err_t;
constexpr esp_err_t ESP_OK = 0;
enum wifi_interface_t { WIFI_IF_STA = 0, WIFI_IF_AP = 1 };
enum wifi_storage_t { WIFI_STORAGE_FLASH = 0, WIFI_STORAGE_RAM = 1 };
enum wifi_country_policy_t { WIFI_COUNTRY_POLICY_AUTO = 0, WIFI_COUNTRY_POLICY_MANUAL = 1 };
enum wifi_scan_method_t { WIFI_FAST_SCAN = 0, WIFI_ALL_CHANNEL_SCAN = 1 };
enum wifi_sort_method_t { WIFI_CONNECT_AP_BY_SIGNAL = 0, WIFI_CONNECT_AP_BY_SECURITY = 1 };
enum wifi_auth_mode_t {
  WIFI_AUTH_OPEN = 0,
  WIFI_AUTH_WEP = 1,
  WIFI_AUTH_WPA_PSK = 2,
  WIFI_AUTH_WPA2_PSK = 3,
  WIFI_AUTH_WPA_WPA2_PSK = 4,
  WIFI_AUTH_WPA3_PSK = 6,
  WIFI_AUTH_WPA2_WPA3_PSK = 7,
};
enum wifi_sae_pwe_method_t {
  WPA3_SAE_PWE_UNSPECIFIED = 0,
  WPA3_SAE_PWE_HUNT_AND_PECK = 1,
  WPA3_SAE_PWE_HASH_TO_ELEMENT = 2,
  WPA3_SAE_PWE_BOTH = 3,
};
struct wifi_scan_threshold_t {
  int8_t rssi;
  wifi_auth_mode_t authmode;
};
struct wifi_pmf_config_t {
  bool capable;
  bool required;
};
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
    wifi_scan_method_t scan_method;
    wifi_sort_method_t sort_method;
    wifi_scan_threshold_t threshold;
    wifi_pmf_config_t pmf_cfg;
    wifi_sae_pwe_method_t sae_pwe_h2e;
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
