#include "phone.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "hal.h"
#include "net.h"
#include "settings.h"

namespace phone {
namespace {
Session s{};
char password[65] = "";
constexpr time_t EARLIEST = 1700000000;  // Nov 2023: anything earlier is not a real time

bool startsWith(const char *line, const char *key) { return strncmp(line, key, strlen(key)) == 0; }

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
  if (year < 2023 || year > 2099 || mon < 1 || mon > 12 || day < 1 || day > 31 || h > 23 || m > 59 || sec > 59) return false;
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
  if (startsWith(line, "T=")) {
    char *end = nullptr;
    double t = strtod(line + 2, &end);
    if (end == line + 2) return refuse("T= needs a number");
    time_t whole = (time_t)t;
    return setTime(whole, (int)((t - (double)whole) * 1000));
  }
  if (startsWith(line, "L=")) {
    int y, mo, d, h, mi, sec;
    if (sscanf(line + 2, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6) return refuse("L= is not a date");
    if (s.haveOffset) return setTime((time_t)(daysFromCivil(y, mo, d) * 86400L + h * 3600L + mi * 60L + sec - s.offset), 0);
    tm local{};  // no offset sent: read it in the watch's own time zone
    local.tm_year = y - 1900;
    local.tm_mon = mo - 1;
    local.tm_mday = d;
    local.tm_hour = h;
    local.tm_min = mi;
    local.tm_sec = sec;
    local.tm_isdst = -1;
    return setTime(mktime(&local), 0);
  }
  if (startsWith(line, "O=")) {
    s.offset = atol(line + 2);
    if (s.offset < -14 * 3600L || s.offset > 14 * 3600L) return refuse("O= is out of range");
    s.haveOffset = true;
    if (!s.gotZone) {
      // Only an offset: a fixed zone. A Z= line, sent by the Drift sync page, keeps summer time rules.
      char posix[24];
      posixFromOffset(s.offset, posix, sizeof posix);
      net::setZone(posix, nullptr);
    }
    return "ok";
  }
  if (startsWith(line, "Z=")) {
    const char *tz = line + 2;
    if (!*tz || strlen(tz) > 63) return refuse("Z= is empty or too long");
    net::setZone(tz, nullptr);
    s.gotZone = true;
    return "ok";
  }
  if (startsWith(line, "N=")) {
    net::setZone(nullptr, line + 2);
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
  if (!strcmp(line, "?")) return "ok";
  return refuse("unknown line");
}

void status(char *out, size_t n, int batteryPct) {
  snprintf(out, n, "fw=" FW_VERSION ";time=%ld;tz=%s;wifi=%s;bat=%d", net::timeIsValid() ? (long)hal::now() : 0L,
           settings.tz.c_str(), net::ssid(), batteryPct);
}

}  // namespace phone
