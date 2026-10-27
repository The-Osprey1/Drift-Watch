// One atomic NVS marker, written only at timer state changes. It does not store a
// deadline: after a restart elapsed time is unknown and a timer must not resume.
#pragma once
#include <stdint.h>

namespace timerstore {
enum class Marker : uint8_t { Idle, Running, Due };
enum class Notice : uint8_t { None, Interrupted, Missed, StorageError };
bool load(Marker &marker);
bool save(Marker marker);
inline const char *noticeName(Notice notice) {
  return notice == Notice::Interrupted ? "interrupted" : notice == Notice::Missed ? "missed" :
         notice == Notice::StorageError ? "storage_error" : "none";
}
}  // namespace timerstore
