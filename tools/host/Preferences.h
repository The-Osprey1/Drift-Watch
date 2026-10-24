// In-memory stand-in for the ESP32 Preferences (NVS) library, so the real
// settings.cpp runs in the simulation. Values survive between begin() calls
// the way flash does; sim::wipeFlash() and sim::flash give tests control.
#pragma once
#include <map>
#include <string>
#include "Arduino.h"

namespace sim {
struct FlashValue {
  bool isString;
  std::string s;
  uint32_t n;
};
extern std::map<std::string, FlashValue> flash;  // "namespace/key" -> value
extern int settingsSaves;                         // read-write sessions that wrote something
}

class Preferences {
 public:
  bool begin(const char *ns, bool readOnly = false) {
    ns_ = ns;
    readOnly_ = readOnly;
    wrote_ = false;
    return true;
  }
  void end() {
    if (wrote_) sim::settingsSaves++;
    wrote_ = false;
  }
  bool isKey(const char *k) { return sim::flash.count(key(k)) > 0; }
  String getString(const char *k, const String &d = String()) {
    auto it = sim::flash.find(key(k));
    return it == sim::flash.end() || !it->second.isString ? d : String(it->second.s);
  }
  bool getBool(const char *k, bool d = false) { return num(k, d) != 0; }
  uint8_t getUChar(const char *k, uint8_t d = 0) { return (uint8_t)num(k, d); }
  int8_t getChar(const char *k, int8_t d = 0) { return (int8_t)(uint8_t)num(k, (uint8_t)d); }
  size_t putString(const char *k, const String &v) { return put(k, {true, v.c_str(), 0}); }
  size_t putBool(const char *k, bool v) { return put(k, {false, "", v}); }
  size_t putUChar(const char *k, uint8_t v) { return put(k, {false, "", v}); }
  size_t putChar(const char *k, int8_t v) { return put(k, {false, "", (uint8_t)v}); }

 private:
  std::string key(const char *k) const { return ns_ + "/" + k; }
  uint32_t num(const char *k, uint32_t d) {
    auto it = sim::flash.find(key(k));
    return it == sim::flash.end() || it->second.isString ? d : it->second.n;
  }
  size_t put(const char *k, const sim::FlashValue &v) {
    if (readOnly_) return 0;
    sim::flash[key(k)] = v;
    wrote_ = true;
    return 1;
  }
  std::string ns_;
  bool readOnly_ = false, wrote_ = false;
};
