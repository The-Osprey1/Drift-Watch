#include "phone.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "hal.h"
#include "net.h"
#include "numeric_input.h"
#include "settings.h"
#include "status_fields.h"

namespace phone {
namespace {
Session s{};
char password[65] = "";
constexpr time_t EARLIEST = 1700000000;  // Nov 2023: anything earlier is not a real time

bool startsWith(const char *line, const char *key) { return strncmp(line, key, strlen(key)) == 0; }

bool validDate(int year, int month, int day) {
  if (year < 2023 || year > 2099 || month < 1 || month > 12 || day < 1) return false;
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int last = days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
  return day <= last;
}

bool localDate(const char *text, tm &local) {
  int fields[6];
  const int low[] = {2023, 1, 1, 0, 0, 0}, high[] = {2099, 12, 31, 23, 59, 59};
  const char separators[] = {'-', '-', ' ', ':', ':'};
  const char *p = text;
  for (int i = 0; i < 6; i++) {
    char *end = nullptr;
    errno = 0;
    long long value = strtoll(p, &end, 10);
    if (end == p || errno == ERANGE || value < low[i] || value > high[i]) return false;
    fields[i] = (int)value;
    if (i < 5) {
      if (separators[i] == ' ') {
        if (!isspace((unsigned char)*end)) return false;
      } else if (*end != separators[i]) return false;
      p = end + 1;
    } else if (!numericinput::atEnd(end)) return false;
  }
  if (!validDate(fields[0], fields[1], fields[2])) return false;
  local = tm{};
  local.tm_year = fields[0] - 1900;
  local.tm_mon = fields[1] - 1;
  local.tm_mday = fields[2];
  local.tm_hour = fields[3];
  local.tm_min = fields[4];
  local.tm_sec = fields[5];
  local.tm_isdst = -1;
  return true;
}

// A fixed zone from an offset, for phones that only send Local Time Information. POSIX signs
// are backwards: UTC+1 is written "-1".
void posixFromOffset(long offset, char *out, size_t n) {
  long a = labs(offset);
  snprintf(out, n, "<%c%02ld%02ld>%c%ld:%02ld", offset < 0 ? '-' : '+', a / 3600, (a % 3600) / 60,
           offset < 0 ? '+' : '-', a / 3600, (a % 3600) / 60);
}

const char *refuse(const char *why) {
  s.error = why;
  return why;
}

const char *save(const Settings &candidate) {
  return commitSettings(candidate) ? "ok" : refuse("settings save unconfirmed");
}

const char *setTime(time_t utc, int ms) {
  if (utc < EARLIEST) return refuse("time is before 2023");
  net::setClockFromPhone(utc, ms);
  s.gotTime = true;
  s.timeSetTo = utc;
  return "ok";
}
}  // namespace

long daysFromCivil(int y, int m, int d) {
  // Howard Hinnant's algorithm: exact for the proleptic Gregorian calendar.
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

bool fromCurrentTime(const uint8_t *d, size_t n, char *out, size_t size) {
  // Exact Time 256: year (2 bytes, little-endian), month, day, hours, minutes, seconds, then
  // day of week, fractions and the adjust reason, which are not needed.
  if (n < 7) return false;
  int year = d[0] | d[1] << 8, mon = d[2], day = d[3], h = d[4], m = d[5], sec = d[6];
  if (!validDate(year, mon, day) || h > 23 || m > 59 || sec > 59) return false;
  snprintf(out, size, "L=%04d-%02d-%02d %02d:%02d:%02d", year, mon, day, h, m, sec);
  return true;
}

bool fromLocalTimeInfo(const uint8_t *d, size_t n, char *out, size_t size) {
  // Time zone in 15-minute steps (signed, -128 = unknown), then summer time offset in
  // 15-minute steps (0, 2, 4 or 8; 255 = unknown).
  if (n < 2 || (int8_t)d[0] == -128) return false;
  long offset = (int8_t)d[0] * 900L + (d[1] == 255 ? 0 : d[1] * 900L);
  snprintf(out, size, "O=%ld", offset);
  return true;
}

void begin() {
  s = Session{};
  password[0] = 0;
}

const Session &session() { return s; }

const char *apply(const char *line) {
  s.lines++;
  s.error = nullptr;  // the next reply replaces an earlier refusal, including a recovered save
  if (startsWith(line, "T=")) {
    time_t whole;
    int ms;
    if (!numericinput::timestamp(line + 2, EARLIEST, whole, ms)) return refuse("T= needs valid Unix seconds");
    return setTime(whole, ms);
  }
  if (startsWith(line, "L=")) {
    tm local{};  // no offset sent: read it in the watch's own time zone
    if (!localDate(line + 2, local)) return refuse("L= is not a valid date");
    if (s.haveOffset) {
      int64_t utc = (int64_t)daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday) * 86400 +
                    local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec - s.offset;
      if (utc < EARLIEST || utc > numericinput::maxTimeSeconds()) return refuse("L= time out of range");
      return setTime((time_t)utc, 0);
    }
    return setTime(mktime(&local), 0);
  }
  if (startsWith(line, "O=")) {
    int64_t offset;
    if (!numericinput::integer(line + 2, 10, -14 * 3600L, 14 * 3600L, offset)) return refuse("O= is out of range");
    if (!s.gotZone) {
      // Only an offset: a fixed zone. A Z= line, sent by the Drift sync page, keeps summer time rules.
      char posix[48];  // room for any long, so the compiler can see it never cuts the zone short
      posixFromOffset((long)offset, posix, sizeof posix);
      if (!net::setZone(posix, nullptr)) return refuse("settings save unconfirmed");
    }
    s.offset = (long)offset;
    s.haveOffset = true;
    return "ok";
  }
  if (startsWith(line, "Z=")) {
    const char *tz = line + 2;
    if (!*tz || strlen(tz) > 63) return refuse("Z= is empty or too long");
    if (!net::setZone(tz, nullptr)) return refuse("settings save unconfirmed");
    s.gotZone = true;
    return "ok";
  }
  if (startsWith(line, "N=")) {
    if (!net::setZone(nullptr, line + 2)) return refuse("settings save unconfirmed");
    return "ok";
  }
  if (startsWith(line, "S=")) {
    if (strlen(line + 2) > 32 || !line[2]) return refuse("network name is empty or too long");
    strncpy(s.ssid, line + 2, sizeof s.ssid - 1);
    s.ssid[sizeof s.ssid - 1] = 0;
    return "ok";
  }
  if (startsWith(line, "P=")) {
    if (strlen(line + 2) > 64) return refuse("password is too long");
    strncpy(password, line + 2, sizeof password - 1);
    password[sizeof password - 1] = 0;
    return "ok";
  }
  if (!strcmp(line, "J")) {
    if (!s.ssid[0]) return refuse("send S= before J");
    net::saveNetwork(s.ssid, password);
    memset(password, 0, sizeof password);
    s.wifiSaved = true;
    net::startSync();
    return "ok";
  }
  if (startsWith(line, "H=")) {
    int64_t mask;
    if (!numericinput::integer(line + 2, 0, 1, 0xFFFF, mask) || (mask & ALL_FACES_MASK) == 0) return refuse("no faces selected");
    Settings candidate = settings;
    candidate.activeFaces = (uint16_t)(mask & ALL_FACES_MASK);
    if (!(candidate.activeFaces & (1u << (candidate.face % FACE_COUNT)))) {
      for (int i = 0; i < FACE_COUNT; i++) {
        if (candidate.activeFaces & (1u << i)) {
          candidate.face = (uint8_t)i;
          break;
        }
      }
    }
    return save(candidate);
  }
  if (startsWith(line, "B=")) {
    int64_t b;
    if (!numericinput::integer(line + 2, 10, 0, 3, b)) return refuse("B= out of range");
    Settings candidate = settings;
    candidate.brightnessLevel = (uint8_t)b;
    const char *reply = save(candidate);
    if (!strcmp(reply, "ok")) hal::backlight(backlightLevel());
    return reply;
  }
  if (startsWith(line, "M=")) {
    int64_t m;
    if (!numericinput::integer(line + 2, 10, 0, 2, m)) return refuse("M= out of range");
    Settings candidate = settings;
    candidate.motion = (uint8_t)m;
    return save(candidate);
  }
  if (startsWith(line, "F=")) {
    int64_t f;
    if (!numericinput::integer(line + 2, 10, 0, FACE_COUNT - 1, f)) return refuse("F= out of range");
    Settings candidate = settings;
    candidate.face = (uint8_t)f;
    candidate.activeFaces |= (1u << candidate.face);
    return save(candidate);
  }
  if (startsWith(line, "FORMAT=") || startsWith(line, "FO=")) {
    const char *p = startsWith(line, "FO=") ? line + 3 : line + 7;
    int64_t f;
    if (numericinput::integer(p, 10, 12, 24, f) && (f == 12 || f == 24)) {
      Settings candidate = settings;
      candidate.use12h = (f == 12);
      return save(candidate);
    }
    return refuse("format must be 12 or 24");
  }
  if (startsWith(line, "NIGHT=") || startsWith(line, "NM=")) {
    const char *p = line[1] == 'M' ? line + 3 : line + 6;
    int64_t nm;
    if (numericinput::integer(p, 10, 0, NIGHT_CHOICES - 1, nm)) {
      Settings candidate = settings;
      candidate.night = (uint8_t)nm;
      return save(candidate);
    }
    return refuse("night out of range");
  }
  if (!strcmp(line, "?")) return "ok";
  return refuse("unknown line");
}

void status(char *out, size_t n, int batteryPct) {
  snprintf(out, n, "fw=" FW_VERSION ";time=%ld;bat=%d;face=%d;faces=%u;bright=%d;motion=%d;format=%d;night=%d", net::timeIsValid() ? (long)hal::now() : 0L,
           batteryPct, (int)(settings.face % FACE_COUNT),
           (unsigned int)(settings.activeFaces & ALL_FACES_MASK), (int)settings.brightnessLevel, (int)settings.motion,
           settings.use12h ? 12 : 24, (int)settings.night);
  statusfields::appendText(out, n, "tz", settings.tz.c_str());
  statusfields::appendText(out, n, "wifi", net::ssid());
}

}  // namespace phone
