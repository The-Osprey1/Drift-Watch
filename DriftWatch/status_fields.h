// Transport-neutral measured status fields. No hardware reads or settings writes.
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace statusfields {
constexpr uint32_t SETTINGS = 1, TIMER_CONTROL = 2, TELEMETRY = 4, TIME_QUALITY = 8, TIMER_RECOVERY = 16;
constexpr uint32_t DURABLE_SETTINGS = 32;
constexpr uint32_t SERIAL_CAPS = SETTINGS | TIMER_CONTROL | TELEMETRY | TIME_QUALITY | TIMER_RECOVERY | DURABLE_SETTINGS;
constexpr uint32_t BLE_CAPS = SETTINGS | TELEMETRY | TIME_QUALITY | TIMER_RECOVERY | DURABLE_SETTINGS;

struct Snapshot {
  int batteryMv = -1;
  float boardTempC = -99;
  int32_t driftPpb = 0;
  const char *quality = "unset", *source = "none", *timer = "idle";
  const char *store = "saved";
  int64_t syncAge = -1;
  uint32_t remainingSeconds = 0;
};

inline bool append(char *out, size_t n, const Snapshot &v, bool serial) {
  const size_t used = strnlen(out, n);
  if (used >= n) return false;
  char temperature[16] = "na";
  if (isfinite(v.boardTempC) && v.boardTempC >= -50 && v.boardTempC <= 150)
    snprintf(temperature, sizeof temperature, "%.1f", (double)v.boardTempC);
  const char *format = serial
      ? ",PROTO=1,CAPS=0x%02lX,MV=%d,TEMP=%s,DRIFT=%ld,TIMEQ=%s,SOURCE=%s,SYNCAGE=%lld,TIMER=%s,REMAIN=%lu,STORE=%s"
      : ";proto=1;caps=0x%02lX;mv=%d;temp=%s;drift=%ld;timeq=%s;source=%s;syncage=%lld;timer=%s;remain=%lu;store=%s";
  int length = snprintf(out + used, n - used, format, (unsigned long)(serial ? SERIAL_CAPS : BLE_CAPS),
      v.batteryMv, temperature, (long)v.driftPpb, v.quality, v.source,
      (long long)v.syncAge, v.timer, (unsigned long)v.remainingSeconds, v.store);
  if (length < 0 || (size_t)length >= n - used) { out[used] = 0; return false; }
  return true;
}

// Optional string fields may contain wire delimiters in an SSID or typed zone.
// Omit an optional field if it will not fit, rather than sending a partial value.
inline bool appendText(char *out, size_t n, const char *key, const char *value) {
  char encoded[192];
  size_t at = 0;
  constexpr const char *ESCAPE_HEX = "0123456789ABCDEF";
  for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
    bool escape = *p == '%' || *p == ';' || *p == ',' || *p == '\r' || *p == '\n';
    size_t need = escape ? 3 : 1;
    if (at + need >= sizeof encoded) return false;
    if (escape) { encoded[at++] = '%'; encoded[at++] = ESCAPE_HEX[*p >> 4]; encoded[at++] = ESCAPE_HEX[*p & 15]; }
    else encoded[at++] = (char)*p;
  }
  encoded[at] = 0;
  size_t used = strnlen(out, n), required = strlen(key) + at + 2;
  if (used >= n || required >= n - used) return false;
  snprintf(out + used, n - used, ";%s=%s", key, encoded);
  return true;
}
}  // namespace statusfields
