#include "timer_store.h"
#include <Preferences.h>

namespace timerstore {
namespace {
constexpr const char *NAMESPACE = "tools";
constexpr const char *KEY = "timer";
}

bool load(Marker &marker) {
  marker = Marker::Idle;
  Preferences p;
  // A writable open creates the namespace on a first-ever start; opening a
  // nonexistent namespace read-only fails on the real ESP32 Preferences API.
  if (!p.begin(NAMESPACE, false)) return false;
  uint8_t stored = p.isKey(KEY) ? p.getUChar(KEY, 255) : 0;
  p.end();
  if (stored > (uint8_t)Marker::Due) return false;
  marker = (Marker)stored;
  return true;
}

bool save(Marker marker) {
  if ((uint8_t)marker > (uint8_t)Marker::Due) return false;
  Preferences p;
  if (!p.begin(NAMESPACE, false)) return false;
  uint8_t value = (uint8_t)marker;
  // Replacing a running timer still means Running; no tick/checkpoint writes.
  if (p.isKey(KEY) && p.getUChar(KEY, 255) == value) {
    p.end();
    return true;
  }
  bool ok = p.putUChar(KEY, value) == sizeof value;
  p.end();
  return ok;
}
}  // namespace timerstore
