// Whole-record recovery and staged command semantics against real settings.cpp.
#include <stdio.h>
#include <string.h>
#include <Preferences.h>
#include "board.h"
#include "settings.h"
#include "settings_record.h"

namespace sim {
std::map<std::string, FlashValue> flash;
int settingsSaves = 0;
}
namespace {
using settingsrecord::Record;
using settingsrecord::Kind;
int checks = 0, failures = 0;
void check(bool value, const char *what) {
  checks++;
  if (!value) { failures++; printf("FAIL: %s\n", what); }
}
void faultsOff() {
  sim::preferenceFaults() = {};
  sim::preferenceWriteFailureKey().clear();
  sim::preferenceBeginFailureNamespace().clear();
}
void fresh() {
  faultsOff(); sim::flash.clear(); sim::settingsSaves = 0; settings = Settings{};
}
Settings sample(int variant = 0) {
  Settings value;
  value.tz = variant ? "<+0545>-5:45" : "GMT0BST,M3.5.0/1,M10.5.0";
  value.autoTz = "EST5EDT,M3.2.0,M11.1.0"; value.autoZone = "America/New_York";
  value.use12h = !variant; value.face = variant ? FACE_DIGITAL : FACE_PHANTOM;
  value.brightnessLevel = variant ? 1 : 3; value.motion = MOTION_EXPRESSIVE;
  value.largeText = true; value.raiseToWake = false; value.twistToWake = true;
  value.screenFacing = -1; value.screenTurn = 2; value.touchMap = 6; value.touchCheckedTurn = 2;
  value.night = 3; value.driftPpb = variant ? -29870 : 47921;
  value.lastKnownTime = 1800000000; value.activeFaces = ALL_FACES_MASK;
  return value;
}
bool equal(const Settings &a, const Settings &b) {
  Record left, right;
  return settingsrecord::encode(a, 1, left) && settingsrecord::encode(b, 1, right) && settingsrecord::equal(left, right);
}
void writeRecord(const char *key, const Record &record) {
  Preferences p; p.begin("watchcfg", false); p.putBytes(key, record.bytes, record.size); p.end();
}
Record record(const Settings &value, uint64_t generation) {
  Record result;
  check(settingsrecord::encode(value, generation, result), "test fixture encodes");
  return result;
}
bool sameFlash(const std::map<std::string, sim::FlashValue> &a, const std::map<std::string, sim::FlashValue> &b) {
  if (a.size() != b.size()) return false;
  for (const auto &entry : a) {
    auto it = b.find(entry.first);
    if (it == b.end()) return false;
    const auto &x = entry.second, &y = it->second;
    if (x.isString != y.isString || x.isBlob != y.isBlob || x.s != y.s || x.n != y.n || x.bytes != y.bytes) return false;
  }
  return true;
}
void legacySeed(uint8_t version = 6) {
  Preferences p; p.begin("watch", false);
  if (version != 1) p.putUChar("ver", version);
  p.putString("tz", "GMT0BST,M3.5.0/1,M10.5.0");
  p.putBool("h12", true); p.putUChar("face", FACE_PHANTOM); p.putUChar("blevel", 1);
  p.putBool("lessmo", true); p.putBool("raise", false); p.putLong64("known", 1800000000);
  p.end();
}
void codec() {
  const Settings value = sample();
  Record original = record(value, 0x0102030405060708ULL);
  check(original.size == 215 && !memcmp(original.bytes, "DWSC", 4) && original.bytes[6] == 7 &&
        original.bytes[8] == 8 && original.bytes[15] == 1, "envelope has fixed portable byte order");
  Settings read; uint64_t generation = 0;
  check(settingsrecord::decode(original, read, generation) == Kind::Supported && equal(read, value) &&
        generation == 0x0102030405060708ULL, "every setting round-trips in one snapshot");
  for (size_t byte = 0; byte < original.size; byte++) {
    for (int bit = 0; bit < 8; bit++) {
      Record changed = original; changed.bytes[byte] ^= (uint8_t)(1u << bit);
      check(settingsrecord::decode(changed, read, generation) == Kind::Invalid, "any single-bit damage is rejected");
    }
  }
  for (size_t n = 0; n < original.size; n++) {
    Record cut = original; cut.size = n;
    check(settingsrecord::decode(cut, read, generation) == Kind::Invalid, "every truncated envelope is rejected");
  }
  Record changed = original;
  changed.bytes[settingsrecord::HEADER_BYTES + 63] = 'x'; settingsrecord::seal(changed);
  check(settingsrecord::decode(changed, read, generation) == Kind::Invalid, "noncanonical string padding is rejected even with CRC");
  changed = original; changed.bytes[settingsrecord::HEADER_BYTES + 168] = 0x80; settingsrecord::seal(changed);
  check(settingsrecord::decode(changed, read, generation) == Kind::Invalid, "reserved payload flag is rejected");
  changed = original; changed.bytes[settingsrecord::HEADER_BYTES + 171] = 8; settingsrecord::seal(changed);
  check(settingsrecord::decode(changed, read, generation) == Kind::Invalid, "CRC cannot bless out-of-range motion");
  changed = original; settingsrecord::put(changed.bytes + 6, 8, 2); settingsrecord::seal(changed);
  check(settingsrecord::decode(changed, read, generation) == Kind::Unsupported, "integrity-valid future schema is recognized separately");
  changed = original; changed.bytes[4] = 2; settingsrecord::seal(changed);
  check(settingsrecord::decode(changed, read, generation) == Kind::Unsupported, "future envelope is protected");
  Settings bad = value; bad.tz = std::string(64, 'z');
  check(!settingsrecord::encode(bad, 1, changed), "oversized strings cannot be persisted");
  bad = value; bad.face = FACE_COUNT;
  check(!settingsrecord::encode(bad, 1, changed), "invalid face cannot be persisted");
  bad = value; bad.activeFaces = 1;
  check(!settingsrecord::encode(bad, 1, changed), "hidden active face cannot be persisted");
  bad = value; bad.lastKnownTime = -1;
  check(!settingsrecord::encode(bad, 1, changed), "invalid editor timestamp cannot be persisted");
  check(!settingsrecord::encode(value, 0, changed), "zero generation is reserved");
}
void migrations() {
  fresh();
  loadSettings();
  check(!strcmp(settingsStoreStatus(), "saved") && sim::settingsSaves == 1 && settings.screenTurn == DISPLAY_ROTATION,
        "blank watch writes one verified default record");
  const int writes = sim::settingsSaves;
  loadSettings(); saveSettings();
  check(sim::settingsSaves == writes, "restart and unchanged saves do not rewrite records");
  for (uint8_t version = 1; version <= 6; version++) {
    fresh(); legacySeed(version);
    const auto old = sim::flash;
    loadSettings();
    check(settings.use12h && settings.motion == MOTION_OFF && settings.face == FACE_PHANTOM &&
          settings.brightnessLevel == 1 && !settings.raiseToWake && !strcmp(settingsStoreStatus(), "saved"),
          "supported legacy schema imports with historical defaults");
    for (const auto &entry : old) {
      check(sim::flash[entry.first].n == entry.second.n && sim::flash[entry.first].s == entry.second.s,
            "migration leaves every legacy key untouched");
    }
    check(sim::flash.count("watchcfg/a") == 1, "legacy migration writes a complete new record");
  }
  fresh(); legacySeed();
  sim::flash["watch/face"].n = 255; sim::flash["watch/faces"] = {false, "", 0};
  sim::flash["watch/blevel"].n = 99; sim::flash["watch/known"].n = UINT64_MAX;
  loadSettings();
  check(settings.face < FACE_COUNT && settings.activeFaces == ALL_FACES_MASK && settings.brightnessLevel == 2 &&
        settings.lastKnownTime == 0 && !strcmp(settingsStoreStatus(), "saved"), "legacy damage is normalized before first record");
  fresh(); legacySeed(99); const auto future = sim::flash;
  loadSettings();
  check(!settingsStoreWritable() && !strcmp(settingsStoreStatus(), "readonly") && sameFlash(future, sim::flash),
        "future legacy schema cannot be silently migrated or overwritten");
  check(!commitSettings(sample(1)) && sameFlash(future, sim::flash), "future legacy save refusal leaves keys intact");
  fresh(); legacySeed();
  sim::preferenceFaults().failReadAt = 3;  // first legacy existence probe, after two empty slots
  loadSettings();
  check(!strcmp(settingsStoreStatus(), "error") && !sim::flash.count("watchcfg/a"), "failed legacy read is not migrated as defaults");
  faultsOff(); loadSettings();
  check(settings.brightnessLevel == 1 && !strcmp(settingsStoreStatus(), "saved"), "legacy read failure can be retried");
  settings = sample(); check(saveSettings(), "new changes commit after migration");
  Preferences p; p.begin("watch", false); p.putUChar("blevel", 0); p.end();
  loadSettings();
  check(equal(settings, sample()), "downgrade-era legacy edits cannot replace an existing new record");
}
void interruptedWrites() {
  fresh();
  const Settings old = sample(), next = sample(1);
  writeRecord("a", record(sample(1), 1)); writeRecord("b", record(old, 2));
  const auto baseline = sim::flash;
  const auto newest = baseline.at("watchcfg/b").bytes;
  const size_t length = record(next, 3).size;
  for (size_t boundary = 0; boundary < length; boundary++) {
    sim::flash = baseline; faultsOff(); settings = old;
    sim::preferenceFaults().blobWriteLimit = boundary;
    check(!commitSettings(next) && equal(settings, old), "each interrupted target write refuses staged success");
    check(sim::flash.at("watchcfg/b").bytes == newest, "each interrupted write leaves newest old record untouched");
    faultsOff(); loadSettings();
    check(equal(settings, old), "each interrupted write reboots to the complete old snapshot");
  }
  for (int failure = 0; failure < 4; failure++) {
    sim::flash = baseline; faultsOff(); settings = old;
    if (failure == 0) sim::preferenceFaults().failReadAfterWrite = true;
    if (failure == 1) sim::preferenceFaults().corruptReadAfterWrite = true;
    if (failure == 2) sim::preferenceFaults().failBeginAfterWrite = true;
    if (failure == 3) sim::preferenceFaults().failReadAt = 8;
    check(!commitSettings(next) && equal(settings, old) && !strcmp(settingsStoreStatus(), "error"),
          "unconfirmed readback never installs or acknowledges a staged change");
    faultsOff(); loadSettings();
    check(equal(settings, next), "unconfirmed save may reboot to the complete new record");
    const int saved = sim::settingsSaves;
    check(saveSettings() && sim::settingsSaves == saved, "retry recognizes already committed identical bytes");
  }
  for (int call = 1; call <= 6; call++) {
    sim::flash = baseline; faultsOff(); settings = old;
    sim::preferenceFaults().failReadAt = call;
    check(!commitSettings(next) && sameFlash(baseline, sim::flash), "scan read fault prevents any target write");
  }
  for (int call = 1; call <= 2; call++) {
    sim::flash = baseline; faultsOff(); settings = old;
    sim::preferenceFaults().failBeginAt = call;
    check(!commitSettings(next) && sameFlash(baseline, sim::flash), "scan/write open fault preserves all durable bytes");
  }
  sim::flash = baseline; faultsOff(); settings = old;
  sim::preferenceFaults().failWriteAt = 1;
  check(saveSettings() && sameFlash(baseline, sim::flash), "an unchanged save does not need the failing writer");
  check(!commitSettings(next) && sameFlash(baseline, sim::flash), "explicit write refusal preserves the old snapshot");
  faultsOff();
  check(commitSettings(next) && equal(settings, next), "transient write refusal can be retried successfully");
  settingsChanged();
  check(!strcmp(settingsStoreStatus(), "pending"), "runtime changes mark saved settings pending");
  check(saveSettings() && !strcmp(settingsStoreStatus(), "saved"), "identical durable payload clears pending without new data");
  fresh(); legacySeed(); const auto legacy = sim::flash;
  sim::preferenceFaults().blobWriteLimit = 70; loadSettings();
  check(!strcmp(settingsStoreStatus(), "error") && settings.brightnessLevel == 1, "interrupted initial migration keeps normalized RAM and reports failure");
  faultsOff(); loadSettings();
  check(!settingsStoreWritable() && !strcmp(settingsStoreStatus(), "readonly") && settings.brightnessLevel == 1,
        "interrupted first migration offers explicit read-only legacy fallback");
  for (const auto &entry : legacy)
    check(sim::flash[entry.first].n == entry.second.n && sim::flash[entry.first].s == entry.second.s,
          "interrupted first migration retains original legacy keys");
}
void protectedAndRecovered() {
  fresh();
  const Settings old = sample(), next = sample(1);
  Record a = record(old, 1), b = record(next, 2);
  writeRecord("a", a); writeRecord("b", b);
  sim::flash["watchcfg/b"].bytes[90] ^= 1;
  loadSettings();
  check(equal(settings, old) && !strcmp(settingsStoreStatus(), "recovered") && settingsStoreWritable(),
        "one CRC-corrupt record recovers the valid older snapshot visibly");
  check(commitSettings(next), "changed save repairs the corrupt target while keeping the valid slot");
  loadSettings(); check(equal(settings, next) && !strcmp(settingsStoreStatus(), "saved"), "repaired record reloads cleanly");
  fresh(); writeRecord("a", a);
  settingsrecord::put(b.bytes + 6, 8, 2); settingsrecord::seal(b); writeRecord("b", b);
  auto before = sim::flash; loadSettings();
  check(equal(settings, old) && !settingsStoreWritable() && !strcmp(settingsStoreStatus(), "readonly"),
        "future valid record protects both slots and offers supported read-only fallback");
  check(!commitSettings(next) && sameFlash(before, sim::flash), "future data cannot be rewritten by supported firmware");
  fresh(); legacySeed(); writeRecord("b", b); before = sim::flash; loadSettings();
  check(settings.brightnessLevel == 1 && !settingsStoreWritable() && sameFlash(before, sim::flash),
        "future-only record uses explicit read-only legacy snapshot without importing it");
  settingsChanged(); check(!strcmp(settingsStoreStatus(), "readonly"), "runtime changes cannot hide read-only protection");
  fresh(); writeRecord("a", record(old, 9)); writeRecord("b", record(next, 9)); before = sim::flash; loadSettings();
  check(!settingsStoreWritable() && !strcmp(settingsStoreStatus(), "readonly") && !commitSettings(next) && sameFlash(before, sim::flash),
        "equal generations with different snapshots are protected");
  fresh(); writeRecord("a", record(old, UINT64_MAX)); before = sim::flash; loadSettings();
  check(!settingsStoreWritable() && !commitSettings(next) && sameFlash(before, sim::flash), "generation exhaustion never wraps or rewrites");
  fresh(); writeRecord("a", a); sim::flash["watchcfg/a"].bytes[1] ^= 2; before = sim::flash; loadSettings();
  check(!settingsStoreWritable() && !strcmp(settingsStoreStatus(), "readonly") && !commitSettings(next) && sameFlash(before, sim::flash),
        "all-corrupt store is never silently replaced by writable defaults");
  fresh(); sim::preferenceBeginFailureNamespace() = "watchcfg"; loadSettings();
  check(!strcmp(settingsStoreStatus(), "error") && !sim::flash.count("watchcfg/a"), "namespace failure is not a blank-watch migration");
  faultsOff(); loadSettings();
  check(!strcmp(settingsStoreStatus(), "saved"), "store can recover after a transient namespace failure");
  fresh(); sim::preferenceFaults().failReadAt = 1; loadSettings();
  check(!strcmp(settingsStoreStatus(), "error") && !sim::flash.count("watchcfg/a"),
        "a failed existence probe cannot authorize a blank-watch import");
  faultsOff(); loadSettings();
  check(!strcmp(settingsStoreStatus(), "saved"), "a genuine blank watch imports after the probe recovers");
  const auto stable = sim::flash; const Settings valid = settings;
  Settings bad = settings; bad.brightnessLevel = 200;
  check(!commitSettings(bad) && equal(settings, valid) && sameFlash(stable, sim::flash), "invalid staged snapshot never mutates RAM or NVS");
}
}  // namespace
int main() {
  codec(); migrations(); interruptedWrites(); protectedAndRecovered();
  printf("Settings persistence: %d of %d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
