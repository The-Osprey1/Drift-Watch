// In-memory stand-in for the ESP32 Preferences (NVS) library, so the real
// settings.cpp runs in the simulation. Values survive between begin() calls
// the way flash does; sim::wipeFlash() and sim::flash give tests control.
#pragma once
#include <map>
#include <string>
#include <vector>
#include <limits>
#include "Arduino.h"

namespace sim {
struct FlashValue {
  bool isString;
  std::string s;
  uint64_t n;
  bool isBlob;
  std::vector<uint8_t> bytes;
  // Keep the historical three-value fixtures source-compatible, including the
  // GNU C++11 cross-check, where members with defaults are not aggregates.
  FlashValue(bool stringValue = false, const std::string &text = std::string(), uint64_t number = 0)
      : isString(stringValue), s(text), n(number), isBlob(false) {}
};
extern std::map<std::string, FlashValue> flash;  // "namespace/key" -> value
extern int settingsSaves;                         // read-write sessions that wrote something
// Opt-in failures for persistence tests. Inline function-local state is shared
// across translation units, without adding globals to unrelated host programs.
inline std::string &preferenceWriteFailureKey() { static std::string value; return value; }
inline std::string &preferenceBeginFailureNamespace() { static std::string value; return value; }
struct PreferenceFaults {
  int beginCalls = 0, readCalls = 0, writeCalls = 0, successfulWrites = 0;
  // One-based exact operation failures. isKey() is an existence check and is
  // deliberately excluded from readCalls so readback faults are unambiguous.
  int failBeginAt = 0, failReadAt = 0, failWriteAt = 0;
  size_t blobWriteLimit = (std::numeric_limits<size_t>::max)();
  bool failReadAfterWrite = false, corruptReadAfterWrite = false, failBeginAfterWrite = false;
};
inline PreferenceFaults &preferenceFaults() { static PreferenceFaults value; return value; }
}

class Preferences {
 public:
  bool begin(const char *ns, bool readOnly = false) {
    sim::PreferenceFaults &fault = sim::preferenceFaults();
    fault.beginCalls++;
    if (opened_) return false;
    ns_ = ns ? ns : "";
    readOnly_ = readOnly;
    wrote_ = false;
    opened_ = !ns_.empty() && ns_ != sim::preferenceBeginFailureNamespace() &&
              fault.beginCalls != fault.failBeginAt && !(fault.failBeginAfterWrite && fault.successfulWrites);
    return opened_;
  }
  void end() {
    if (wrote_) sim::settingsSaves++;
    wrote_ = false;
    opened_ = false;
  }
  bool isKey(const char *k) { return opened_ && sim::flash.count(key(k)) > 0; }
  // Error-aware probe used by the settings adapter; unlike isKey(), a read
  // failure must remain distinguishable from a genuinely absent key.
  bool keyExistsChecked(const char *k, bool &exists) {
    exists = false;
    if (!canRead()) return false;
    exists = sim::flash.count(key(k)) > 0;
    return true;
  }
  String getString(const char *k, const String &d = String()) {
    if (!canRead()) return d;
    auto it = sim::flash.find(key(k));
    return it == sim::flash.end() || !it->second.isString || it->second.isBlob ? d : String(it->second.s);
  }
  bool getBool(const char *k, bool d = false) { return num(k, d) != 0; }
  uint8_t getUChar(const char *k, uint8_t d = 0) { return (uint8_t)num(k, d); }
  uint16_t getUShort(const char *k, uint16_t d = 0) { return (uint16_t)num(k, d); }
  int8_t getChar(const char *k, int8_t d = 0) { return (int8_t)(uint8_t)num(k, (uint8_t)d); }
  int32_t getInt(const char *k, int32_t d = 0) { return (int32_t)(uint32_t)num(k, (uint32_t)d); }
  int64_t getLong64(const char *k, int64_t d = 0) { return (int64_t)num(k, (uint64_t)d); }
  size_t putString(const char *k, const String &v) { return put(k, {true, v.c_str(), 0}, v.length()); }
  size_t putBool(const char *k, bool v) { return put(k, {false, "", v}, 1); }
  size_t putUChar(const char *k, uint8_t v) { return put(k, {false, "", v}, sizeof v); }
  size_t putUShort(const char *k, uint16_t v) { return put(k, {false, "", v}, sizeof v); }
  size_t putChar(const char *k, int8_t v) { return put(k, {false, "", (uint8_t)v}, sizeof v); }
  size_t putInt(const char *k, int32_t v) { return put(k, {false, "", (uint32_t)v}, sizeof v); }
  size_t putLong64(const char *k, int64_t v) { return put(k, {false, "", (uint64_t)v}, sizeof v); }
  size_t getBytesLength(const char *k) {
    if (!canRead()) return 0;
    auto it = sim::flash.find(key(k));
    return it == sim::flash.end() || !it->second.isBlob ? 0 : it->second.bytes.size();
  }
  size_t getBytes(const char *k, void *out, size_t capacity) {
    if (!canRead()) return 0;
    auto it = sim::flash.find(key(k));
    if (it == sim::flash.end() || !it->second.isBlob || !out || capacity < it->second.bytes.size()) return 0;
    const std::vector<uint8_t> &bytes = it->second.bytes;
    if (!bytes.empty()) memcpy(out, bytes.data(), bytes.size());
    const sim::PreferenceFaults &fault = sim::preferenceFaults();
    if (fault.corruptReadAfterWrite && fault.successfulWrites && !bytes.empty())
      ((uint8_t *)out)[bytes.size() - 1] ^= 1;
    return bytes.size();
  }
  size_t putBytes(const char *k, const void *data, size_t length) {
    if (!canWrite(k) || !data || !length) return 0;
    size_t stored = (std::min)(length, sim::preferenceFaults().blobWriteLimit);
    if (!stored) return 0;  // before the first byte, the prior slot stays intact
    sim::FlashValue value;
    value.isBlob = true;
    const uint8_t *first = (const uint8_t *)data;
    value.bytes.assign(first, first + stored);
    store(k, value);
    return stored;
  }

 private:
  std::string key(const char *k) const { return ns_ + "/" + k; }
  bool canRead() {
    sim::PreferenceFaults &fault = sim::preferenceFaults();
    fault.readCalls++;
    return opened_ && fault.readCalls != fault.failReadAt && !(fault.failReadAfterWrite && fault.successfulWrites);
  }
  uint64_t num(const char *k, uint64_t d) {
    if (!canRead()) return d;
    auto it = sim::flash.find(key(k));
    return it == sim::flash.end() || it->second.isString || it->second.isBlob ? d : it->second.n;
  }
  bool canWrite(const char *k) {
    sim::PreferenceFaults &fault = sim::preferenceFaults();
    fault.writeCalls++;
    return opened_ && !readOnly_ && key(k) != sim::preferenceWriteFailureKey() && fault.writeCalls != fault.failWriteAt;
  }
  void store(const char *k, const sim::FlashValue &v) {
    sim::flash[key(k)] = v;
    wrote_ = true;
    sim::preferenceFaults().successfulWrites++;
  }
  size_t put(const char *k, const sim::FlashValue &v, size_t bytes) {
    if (!canWrite(k)) return 0;
    store(k, v);
    return bytes;
  }
  std::string ns_;
  bool readOnly_ = false, wrote_ = false, opened_ = false;
};
