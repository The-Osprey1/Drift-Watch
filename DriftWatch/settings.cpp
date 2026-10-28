#include "settings.h"
#include <Preferences.h>
#include "board.h"
#include "orient.h"
#include "settings_record.h"
#if defined(ARDUINO_ARCH_ESP32)
#include <nvs.h>
#endif

Settings settings;

namespace {
using settingsrecord::Kind;
using settingsrecord::Record;
constexpr const char *STORE = "watchcfg";
constexpr const char *SLOTS[] = {"a", "b"};
const char *storeStatus = "pending";
bool storeWritable = true;

// Preferences::isKey hides read errors as absence. Probe with the error-aware
// NVS API so a failed existence check cannot authorize a blank-store import.
bool present(Preferences &p, const char *name, const char *key, bool &ok) {
#if defined(ARDUINO_ARCH_ESP32)
  (void)p;
  nvs_handle_t handle;
  esp_err_t error = nvs_open(name, NVS_READONLY, &handle);
  if (error != ESP_OK) { ok = false; return false; }
  size_t size = 0;
  error = nvs_get_blob(handle, key, nullptr, &size);
  nvs_close(handle);
  if (error == ESP_ERR_NVS_NOT_FOUND) return false;
  if (error == ESP_OK || error == ESP_ERR_NVS_TYPE_MISMATCH) return true;
  ok = false;
  return false;
#else
  (void)name;
  bool exists = false;
  if (!p.keyExistsChecked(key, exists)) ok = false;
  return exists;
#endif
}

Settings defaults() {
  Settings value;
  value.screenTurn = DISPLAY_ROTATION;
  return value;
}

struct Slot {
  bool exists = false, unreadable = false, oversized = false;
  Kind kind = Kind::Invalid;
  uint64_t generation = 0;
  Settings value;
  Record record;
};
struct Scan {
  Slot slot[2];
  bool opened = false, ioError = false, protectedData = false, conflict = false;
  int newest = -1;
  bool any() const { return slot[0].exists || slot[1].exists; }
};

Scan scan() {
  Scan result;
  Preferences p;
  // Writable open creates an absent namespace on first use. No key is written
  // merely by examining the store. A failed open is an error, never blank data.
  if (!p.begin(STORE, false)) { result.ioError = true; return result; }
  result.opened = true;
  for (int i = 0; i < 2; i++) {
    Slot &slot = result.slot[i];
    bool probeOkay = true;
    slot.exists = present(p, STORE, SLOTS[i], probeOkay);
    if (!probeOkay) { result.ioError = true; continue; }
    if (!slot.exists) continue;
    const size_t n = p.getBytesLength(SLOTS[i]);
    if (!n) { slot.unreadable = true; result.ioError = true; continue; }
    if (n > settingsrecord::MAX_BYTES) {
      slot.oversized = true;
      result.protectedData = true;  // could be a larger future envelope
      continue;
    }
    slot.record.size = n;
    if (p.getBytes(SLOTS[i], slot.record.bytes, n) != n) {
      slot.unreadable = true;
      result.ioError = true;
      continue;
    }
    slot.kind = settingsrecord::decode(slot.record, slot.value, slot.generation);
    if (slot.kind == Kind::Unsupported) result.protectedData = true;
    if (slot.kind == Kind::Supported &&
        (result.newest < 0 || slot.generation > result.slot[result.newest].generation)) result.newest = i;
  }
  p.end();
  if (result.slot[0].kind == Kind::Supported && result.slot[1].kind == Kind::Supported &&
      result.slot[0].generation == result.slot[1].generation &&
      !settingsrecord::equal(result.slot[0].record, result.slot[1].record)) {
    result.conflict = result.protectedData = true;
  }
  if (result.newest >= 0 && result.slot[result.newest].generation == UINT64_MAX) result.protectedData = true;
  return result;
}

// Reading twice with different fallback values distinguishes an absent/failed
// typed read from a successful stored value. If a failed read happens to equal
// the actual value, it still cannot introduce a different value into migration.
template <class T>
T legacyNumber(Preferences &p, const char *key, T fallback, T other,
               T (Preferences::*read)(const char *, T), bool &ok) {
  if (!present(p, "watch", key, ok)) return fallback;
  const T a = (p.*read)(key, fallback), b = (p.*read)(key, other);
  if (a != b) ok = false;
  return a;
}
String legacyText(Preferences &p, const char *key, bool &ok) {
  if (!present(p, "watch", key, ok)) return "";
  String a = p.getString(key, "\1missing-a"), b = p.getString(key, "\1missing-b");
  if (strcmp(a.c_str(), b.c_str())) ok = false;
  return a;
}
enum class Legacy : uint8_t { Okay, Failed, Unsupported };
Legacy legacy(Settings &out) {
  out = defaults();
  Preferences p;
  if (!p.begin("watch", false)) return Legacy::Failed;
  bool ok = true;
  const uint8_t version = legacyNumber<uint8_t>(p, "ver", 1, 255, &Preferences::getUChar, ok);
  if (!ok || !version || version > 6) { p.end(); return ok ? Legacy::Unsupported : Legacy::Failed; }
  out.tz = legacyText(p, "tz", ok);
  out.autoTz = legacyText(p, "atz", ok);
  out.autoZone = legacyText(p, "azone", ok);
  out.use12h = legacyNumber<bool>(p, "h12", false, true, &Preferences::getBool, ok);
  out.face = legacyNumber<uint8_t>(p, "face", 0, 255, &Preferences::getUChar, ok);
  out.brightnessLevel = legacyNumber<uint8_t>(p, "blevel", 2, 255, &Preferences::getUChar, ok);
  if (present(p, "watch", "motion", ok)) out.motion = legacyNumber<uint8_t>(p, "motion", MOTION_SUBTLE, 255, &Preferences::getUChar, ok);
  else out.motion = legacyNumber<bool>(p, "lessmo", false, true, &Preferences::getBool, ok) ? MOTION_OFF : MOTION_SUBTLE;
  out.largeText = legacyNumber<bool>(p, "bigtext", false, true, &Preferences::getBool, ok);
  out.raiseToWake = legacyNumber<bool>(p, "raise", true, false, &Preferences::getBool, ok);
  out.twistToWake = legacyNumber<bool>(p, "twist", true, false, &Preferences::getBool, ok);
  out.screenFacing = legacyNumber<int8_t>(p, "facing", 0, -128, &Preferences::getChar, ok);
  out.screenTurn = legacyNumber<uint8_t>(p, "turn", DISPLAY_ROTATION, 255, &Preferences::getUChar, ok);
  out.touchMap = legacyNumber<uint8_t>(p, "tmap", 0, 255, &Preferences::getUChar, ok);
  out.touchCheckedTurn = legacyNumber<uint8_t>(p, "tturn", TOUCH_NOT_CHECKED, 254, &Preferences::getUChar, ok);
  out.night = legacyNumber<uint8_t>(p, "night", 0, 255, &Preferences::getUChar, ok);
  out.driftPpb = legacyNumber<int32_t>(p, "drift", 0, INT32_MIN, &Preferences::getInt, ok);
  out.lastKnownTime = legacyNumber<int64_t>(p, "known", 0, INT64_MIN, &Preferences::getLong64, ok);
  out.activeFaces = legacyNumber<uint16_t>(p, "faces", ALL_FACES_MASK, 0, &Preferences::getUShort, ok);
  p.end();
  if (!ok) return Legacy::Failed;
  migrateSettings(out, version);
  return Legacy::Okay;
}

bool persist(const Settings &candidate) {
  if (!settingsrecord::valid(candidate)) { storeStatus = "error"; return false; }
  Scan current = scan();
  if (current.ioError) { storeStatus = "error"; return false; }
  if (current.protectedData || (current.any() && current.newest < 0)) {
    storeStatus = "readonly"; storeWritable = false; return false;
  }
  if (!current.any()) {
    Settings previous;
    const Legacy old = legacy(previous);
    if (old != Legacy::Okay) {
      storeStatus = old == Legacy::Unsupported ? "readonly" : "error";
      storeWritable = old != Legacy::Unsupported;
      return false;
    }
  }
  storeWritable = true;
  const uint64_t generation = current.newest < 0 ? 1 : current.slot[current.newest].generation + 1;
  Record next;
  if (!settingsrecord::encode(candidate, generation, next)) { storeStatus = "error"; return false; }
  if (current.newest >= 0) {
    Record same;
    if (settingsrecord::encode(candidate, current.slot[current.newest].generation, same) &&
        settingsrecord::equal(same, current.slot[current.newest].record)) {
      storeStatus = "saved";
      return true;
    }
  }
  const int target = current.newest == 0 ? 1 : 0;
  Preferences p;
  if (!p.begin(STORE, false)) { storeStatus = "error"; return false; }
  const bool written = p.putBytes(SLOTS[target], next.bytes, next.size) == next.size;
  p.end();
  if (!written || !p.begin(STORE, true)) { storeStatus = "error"; return false; }
  Record readback;
  readback.size = p.getBytesLength(SLOTS[target]);
  const bool read = readback.size == next.size &&
      p.getBytes(SLOTS[target], readback.bytes, readback.size) == readback.size;
  p.end();
  Settings verified;
  uint64_t savedGeneration = 0;
  if (!read || !settingsrecord::equal(next, readback) ||
      settingsrecord::decode(readback, verified, savedGeneration) != Kind::Supported || savedGeneration != generation) {
    storeStatus = "error";
    return false;
  }
  storeStatus = "saved";
  return true;
}
}  // namespace

uint8_t migrateSettings(Settings &s, uint8_t storedVersion) {
  // Historic key defaults are applied in the legacy loader, then normalized.
  // New record payloads are validated strictly instead of partly defaulted.
  (void)storedVersion;
  if (!settingsrecord::textValid(s.tz, 63)) s.tz = "";
  if (!settingsrecord::textValid(s.autoTz, 63)) s.autoTz = "";
  if (!settingsrecord::textValid(s.autoZone, 39)) s.autoZone = "";
  if (s.brightnessLevel >= BRIGHTNESS_LEVEL_COUNT) s.brightnessLevel = 2;
  if (s.screenFacing != 1 && s.screenFacing != -1) s.screenFacing = 0;
  s.screenTurn %= orient::TURNS;
  if (s.touchMap >= orient::MAPS || (s.touchCheckedTurn != TOUCH_NOT_CHECKED && s.touchCheckedTurn >= orient::TURNS)) {
    s.touchMap = 0;
    s.touchCheckedTurn = TOUCH_NOT_CHECKED;
  }
  if (s.night >= NIGHT_CHOICES) s.night = 0;
  if (s.motion >= MOTION_LEVELS) s.motion = MOTION_SUBTLE;
  s.activeFaces &= ALL_FACES_MASK;
  if (!s.activeFaces) s.activeFaces = ALL_FACES_MASK;
  s.face %= FACE_COUNT;
  if (!(s.activeFaces & (1u << s.face))) {
    for (uint8_t i = 0; i < FACE_COUNT; i++) if (s.activeFaces & (1u << i)) { s.face = i; break; }
  }
  if (s.driftPpb > 30000000 || s.driftPpb < -30000000) s.driftPpb = 0;
  if (s.lastKnownTime < 1700000000 || s.lastKnownTime > numericinput::maxTimeSeconds() || s.lastKnownTime > 4102444799LL)
    s.lastKnownTime = 0;
  return SETTINGS_VERSION;
}

void loadSettings() {
  settings = defaults();
  storeWritable = true;
  const Scan current = scan();
  if (current.newest >= 0) {
    settings = current.slot[current.newest].value;
    if (current.protectedData) { storeStatus = "readonly"; storeWritable = false; }
    else if (current.ioError) storeStatus = "error";
    else {
      const Slot &other = current.slot[1 - current.newest];
      storeStatus = other.exists && other.kind == Kind::Invalid ? "recovered" : "saved";
    }
    return;
  }
  if (current.ioError) { storeStatus = "error"; return; }
  if (current.any()) {
    // The legacy snapshot can be old: expose it only read-only with a visible
    // recovery status. Never silently import it over a corrupt/future store.
    Settings fallback;
    if (legacy(fallback) == Legacy::Okay) settings = fallback;
    storeStatus = "readonly";
    storeWritable = false;
    return;
  }
  Settings imported;
  const Legacy old = legacy(imported);
  if (old == Legacy::Unsupported) { storeStatus = "readonly"; storeWritable = false; return; }
  if (old == Legacy::Failed) { storeStatus = "error"; return; }
  settings = imported;
  storeStatus = "pending";
  // First import is one verified complete record. Legacy keys are never changed.
  persist(settings);
}

bool saveSettings() { return persist(settings); }
bool commitSettings(const Settings &candidate) {
  if (!persist(candidate)) return false;
  settings = candidate;
  return true;
}
const char *settingsStoreStatus() { return storeStatus; }
bool settingsStoreWritable() { return storeWritable; }
void settingsChanged() {
  if (!strcmp(storeStatus, "saved") || !strcmp(storeStatus, "recovered")) storeStatus = "pending";
}

bool touchChecked() { return settings.touchCheckedTurn == settings.screenTurn; }
uint8_t activeTouchMap() { return touchChecked() ? settings.touchMap : orient::expectedTouchMap(settings.screenTurn); }
bool nightNow(int hour) {
  if (!settings.night || settings.night >= NIGHT_CHOICES) return false;
  int from = NIGHT_FROM[settings.night], to = NIGHT_TO[settings.night];
  return from < to ? hour >= from && hour < to : hour >= from || hour < to;
}
