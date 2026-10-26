// Strict numeric arguments shared by the serial and phone command parsers. Whitespace
// around a value is allowed; a partial conversion must never become a command.
#pragma once
#include <ctype.h>
#include <errno.h>
#include <limits>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

namespace numericinput {

inline bool atEnd(const char *p) {
  while (isspace((unsigned char)*p)) p++;
  return *p == '\0';
}

inline bool integer(const char *text, int base, int64_t low, int64_t high, int64_t &value) {
  char *end = nullptr;
  errno = 0;
  long long parsed = strtoll(text, &end, base);
  if (end == text || errno == ERANGE || !atEnd(end) || parsed < low || parsed > high) return false;
  value = (int64_t)parsed;
  return true;
}

// The hardware clock also exposes milliseconds as int64_t. Check that conversion before
// passing a timestamp to it, and respect a narrower time_t on the embedded toolchain.
inline int64_t maxTimeSeconds() {
  constexpr int64_t millisecondsLimit = INT64_MAX / 1000 - 1;
  return (std::numeric_limits<time_t>::max)() < millisecondsLimit
             ? (int64_t)(std::numeric_limits<time_t>::max)()
             : millisecondsLimit;
}

// T= is decimal Unix seconds with an optional fractional part. Parse the fraction as
// digits rather than floating point: NaN/Inf, overflow and rounding at time_t's limit
// must be rejected before a cast. Extra fractional digits are truncated to milliseconds.
inline bool timestamp(const char *text, int64_t earliest, time_t &seconds, int &milliseconds) {
  char *end = nullptr;
  errno = 0;
  long long whole = strtoll(text, &end, 10);
  if (end == text || errno == ERANGE || whole < earliest || whole > maxTimeSeconds()) return false;
  int fraction = 0, place = 100;
  if (*end == '.') {
    end++;
    if (!isdigit((unsigned char)*end)) return false;
    while (isdigit((unsigned char)*end)) {
      if (place) {
        fraction += (*end - '0') * place;
        place /= 10;
      }
      end++;
    }
  }
  if (!atEnd(end)) return false;
  seconds = (time_t)whole;
  milliseconds = fraction;
  return true;
}

}  // namespace numericinput
