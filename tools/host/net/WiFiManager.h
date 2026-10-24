// Stand-in for WiFiManager 2.0.17, as the setup hotspot uses it. See netsim.h.
#pragma once
#include <Arduino.h>
#include <functional>
#include <string>
#include <vector>
#include "WiFi.h"

class WiFiManagerParameter {
 public:
  explicit WiFiManagerParameter(const char *custom) : html_(custom) {}
  WiFiManagerParameter(const char *id, const char *label, const char *value, int length)
      : id_(id), label_(label), value_(value ? value : ""), length_(length) {}
  const char *getValue() const { return value_.c_str(); }
  const char *getID() const { return id_.c_str(); }
  void setValue(const char *v) { value_ = std::string(v).substr(0, length_); }

 private:
  std::string html_, id_, label_, value_;
  int length_ = 0;
};

class WiFiManager {
 public:
  WiFiManager();
  ~WiFiManager();
  void setAPStaticIPConfig(IPAddress, IPAddress, IPAddress) {}
  void setWiFiAPChannel(int) {}
  void setTitle(const String &t);
  void addParameter(WiFiManagerParameter *p) { params.push_back(p); }
  void setBreakAfterConfig(bool) {}
  void setSaveConnect(bool) {}
  void setSaveConnectTimeout(unsigned long) {}
  void setSaveConfigCallback(std::function<void()> f) { saved = f; }
  void setConfigPortalBlocking(bool) {}
  bool startConfigPortal(const char *apName);
  void stopConfigPortal();
  bool process() { return false; }

  std::vector<WiFiManagerParameter *> params;
  std::function<void()> saved;
};
