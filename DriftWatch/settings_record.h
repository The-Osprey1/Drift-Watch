// Stable little-endian settings envelope and schema-7 payload. No raw C++ object
// layout or String pointers are persisted. CRC detects accidental corruption.
#pragma once
#include <stdint.h>
#include <string.h>
#include "settings.h"
#include "numeric_input.h"
#include "orient.h"

namespace settingsrecord {
constexpr size_t HEADER_BYTES = 20, PAYLOAD_BYTES = 191, CRC_BYTES = 4, MAX_BYTES = 256;
constexpr uint8_t ENVELOPE_VERSION = 1;
constexpr uint16_t PAYLOAD_SCHEMA = 7;
constexpr uint32_t MAGIC = 0x43535744;  // DWSC in byte order
enum class Kind : uint8_t { Invalid, Supported, Unsupported };

struct Record {
  uint8_t bytes[MAX_BYTES] = {};
  size_t size = 0;
};

inline uint64_t get(const uint8_t *p, size_t width) {
  uint64_t result = 0;
  for (size_t i = 0; i < width; i++) result |= (uint64_t)p[i] << (8 * i);
  return result;
}
inline void put(uint8_t *p, uint64_t value, size_t width) {
  for (size_t i = 0; i < width; i++) p[i] = (uint8_t)(value >> (8 * i));
}
inline uint32_t crc(const uint8_t *p, size_t n) {
  uint32_t result = UINT32_MAX;
  for (size_t i = 0; i < n; i++) {
    result ^= p[i];
    for (int b = 0; b < 8; b++) result = (result >> 1) ^ (0xEDB88320u & (0u - (result & 1)));
  }
  return ~result;
}
inline void seal(Record &record) {
  put(record.bytes + record.size - CRC_BYTES, crc(record.bytes, record.size - CRC_BYTES), CRC_BYTES);
}
inline bool textValid(const String &text, size_t limit) {
  return text.length() <= limit && text.length() == strlen(text.c_str());
}
inline bool valid(const Settings &s) {
  return textValid(s.tz, 63) && textValid(s.autoTz, 63) && textValid(s.autoZone, 39) &&
      s.face < FACE_COUNT && s.brightnessLevel < BRIGHTNESS_LEVEL_COUNT && s.motion < MOTION_LEVELS &&
      (s.screenFacing == 0 || s.screenFacing == 1 || s.screenFacing == -1) &&
      s.screenTurn < orient::TURNS && s.touchMap < orient::MAPS &&
      (s.touchCheckedTurn == TOUCH_NOT_CHECKED || s.touchCheckedTurn < orient::TURNS) &&
      s.night < NIGHT_CHOICES && s.activeFaces && !(s.activeFaces & ~ALL_FACES_MASK) &&
      (s.activeFaces & (1u << s.face)) && s.driftPpb >= -30000000 && s.driftPpb <= 30000000 &&
      (s.lastKnownTime == 0 || (s.lastKnownTime >= 1700000000 &&
       s.lastKnownTime <= numericinput::maxTimeSeconds() && s.lastKnownTime <= 4102444799LL));
}
inline bool encode(const Settings &s, uint64_t generation, Record &record) {
  if (!generation || !valid(s)) return false;
  record = Record{};
  record.size = HEADER_BYTES + PAYLOAD_BYTES + CRC_BYTES;
  put(record.bytes, MAGIC, 4);
  record.bytes[4] = ENVELOPE_VERSION;
  put(record.bytes + 6, PAYLOAD_SCHEMA, 2);
  put(record.bytes + 8, generation, 8);
  put(record.bytes + 16, PAYLOAD_BYTES, 2);
  uint8_t *p = record.bytes + HEADER_BYTES;
  memcpy(p, s.tz.c_str(), s.tz.length()); p += 64;
  memcpy(p, s.autoTz.c_str(), s.autoTz.length()); p += 64;
  memcpy(p, s.autoZone.c_str(), s.autoZone.length()); p += 40;
  *p++ = (uint8_t)(s.use12h | s.largeText << 1 | s.raiseToWake << 2 | s.twistToWake << 3);
  *p++ = s.face; *p++ = s.brightnessLevel; *p++ = s.motion; *p++ = (uint8_t)s.screenFacing;
  *p++ = s.screenTurn; *p++ = s.touchMap; *p++ = s.touchCheckedTurn; *p++ = s.night;
  put(p, s.activeFaces, 2); p += 2;
  put(p, (uint32_t)s.driftPpb, 4); p += 4;
  put(p, (uint64_t)s.lastKnownTime, 8);
  seal(record);
  return true;
}
inline bool readText(const uint8_t *p, size_t width, String &out) {
  const uint8_t *end = (const uint8_t *)memchr(p, 0, width);
  if (!end) return false;
  for (const uint8_t *q = end; q < p + width; q++) if (*q) return false;
  char text[64];
  const size_t length = (size_t)(end - p);
  memcpy(text, p, length); text[length] = 0;
  out = text;
  return out.length() == length;
}
inline Kind decode(const Record &record, Settings &s, uint64_t &generation) {
  if (record.size < HEADER_BYTES + CRC_BYTES || record.size > MAX_BYTES) return Kind::Invalid;
  const uint8_t *b = record.bytes;
  if (get(b, 4) != MAGIC || !b[4] || get(b + 16, 2) + HEADER_BYTES + CRC_BYTES != record.size ||
      get(b + record.size - CRC_BYTES, CRC_BYTES) != crc(b, record.size - CRC_BYTES)) return Kind::Invalid;
  generation = get(b + 8, 8);
  if (b[4] != ENVELOPE_VERSION || get(b + 6, 2) != PAYLOAD_SCHEMA || b[5] || get(b + 18, 2))
    return Kind::Unsupported;
  if (!generation || get(b + 16, 2) != PAYLOAD_BYTES) return Kind::Invalid;
  Settings value;
  const uint8_t *p = b + HEADER_BYTES;
  if (!readText(p, 64, value.tz)) return Kind::Invalid;
  p += 64;
  if (!readText(p, 64, value.autoTz)) return Kind::Invalid;
  p += 64;
  if (!readText(p, 40, value.autoZone)) return Kind::Invalid;
  p += 40;
  uint8_t flags = *p++;
  if (flags & ~15u) return Kind::Invalid;
  value.use12h = flags & 1; value.largeText = flags & 2;
  value.raiseToWake = flags & 4; value.twistToWake = flags & 8;
  value.face = *p++; value.brightnessLevel = *p++; value.motion = *p++;
  value.screenFacing = (int8_t)*p++;
  value.screenTurn = *p++; value.touchMap = *p++; value.touchCheckedTurn = *p++; value.night = *p++;
  value.activeFaces = (uint16_t)get(p, 2); p += 2;
  value.driftPpb = (int32_t)(uint32_t)get(p, 4); p += 4;
  const uint64_t known = get(p, 8);
  if (known > INT64_MAX) return Kind::Invalid;
  value.lastKnownTime = (int64_t)known;
  if (!valid(value)) return Kind::Invalid;
  s = value;
  return Kind::Supported;
}
inline bool equal(const Record &a, const Record &b) {
  return a.size == b.size && !memcmp(a.bytes, b.bytes, a.size);
}
}  // namespace settingsrecord
