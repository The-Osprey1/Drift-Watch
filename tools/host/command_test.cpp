// Headless checks of the real Serial/phone parsers. No fonts, display or radio needed:
// Build command: g++ -std=c++17 -Wall -Wextra -Werror -I tools/host -I DriftWatch
//   tools/host/command_test.cpp DriftWatch/serial_cmd.cpp DriftWatch/phone.cpp -o command_test
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include <string>
#include "hal.h"
#include "net.h"
#include "numeric_input.h"
#include "phone.h"
#include "serial_cmd.h"
#include "settings.h"

namespace {
int checks = 0, failed = 0;
int saves = 0, clockWrites = 0, zoneWrites = 0, backlights = 0, networkWrites = 0, syncs = 0;
time_t clockSeconds = 1800000000;
int clockFraction = 0;
std::string fakeZoneName, fakeNetworkName;

void check(bool ok, const std::string &message) {
  checks++;
  if (!ok) {
    failed++;
    printf("FAIL: %s\n", message.c_str());
  }
}

// Rejected input may record a protocol error and line count, but must leave every
// setting, time/zone/network side effect and meaningful phone-session field unchanged.
std::string snapshot() {
  const phone::Session &s = phone::session();
  char out[512];
  snprintf(out, sizeof out, "%d/%d/%d/%d/%d/%d/%lld/%d/%d/%d/%d/%d/%d/%u/%d/%d/%d/%lld/%d/%ld/%s",
           saves, clockWrites, zoneWrites, backlights, networkWrites, syncs, (long long)clockSeconds, clockFraction,
           (int)settings.face, (int)settings.brightnessLevel, (int)settings.motion, (int)settings.night,
           settings.use12h ? 1 : 0, (unsigned)settings.activeFaces,
           s.gotTime ? 1 : 0, s.gotZone ? 1 : 0, s.wifiSaved ? 1 : 0, (long long)s.timeSetTo,
           s.haveOffset ? 1 : 0, s.offset, s.ssid);
  return std::string(out) + "/" + settings.tz.c_str() + "/" + fakeZoneName + "/" + fakeNetworkName;
}

void refusePhone(const std::string &line) {
  std::string before = snapshot();
  check(strcmp(phone::apply(line.c_str()), "ok") != 0, "phone refuses " + line);
  check(snapshot() == before, "phone changes nothing for " + line);
}

void serialChecks() {
  using serialcmd::Kind;
  struct Family { const char *prefix; Kind kind; int low, high; };
  const Family families[] = {
      {"TIMER:", Kind::Timer, 0, 86400}, {"BRIGHT:", Kind::Bright, 0, 3},
      {"MOTION:", Kind::Motion, 0, 2}, {"FORMAT:", Kind::Format, 12, 24},
      {"NIGHT:", Kind::Night, 0, 3}, {"FACE:", Kind::Face, 0, FACE_COUNT - 1}};
  for (const Family &f : families) {
    for (int value : {f.low, f.high}) {
      std::string line = std::string(" \t") + f.prefix + " +" + std::to_string(value) + " \r\n";
      auto cmd = serialcmd::parse(line.c_str());
      check(cmd.kind == f.kind && cmd.value == value, "serial valid boundary/whitespace " + line);
    }
    for (const char *arg : {"", " \t", "garbage", "1garbage", "1 2", "0x1", "1.0", "+", "--1",
                            "999999999999999999999999999999999999", "-99999999999999999999999999999999999"}) {
      std::string line = std::string(f.prefix) + arg;
      check(serialcmd::parse(line.c_str()).kind == Kind::None, "serial refuses " + line);
    }
    for (int value : {f.low - 1, f.high + 1}) {
      std::string line = std::string(f.prefix) + std::to_string(value);
      check(serialcmd::parse(line.c_str()).kind == Kind::None, "serial refuses boundary " + line);
    }
  }
  check(serialcmd::parse("FORMAT:13").kind == Kind::None, "serial refuses intermediate format");
  for (const char *line : {" INFO? \t", " ? \r\n"})
    check(serialcmd::parse(line).kind == Kind::Info, std::string("serial info whitespace ") + line);

  for (const char *line : {"FACES:65", "FACES:0x0041", "H=0101", " H=0x0041 \t"}) {
    auto cmd = serialcmd::parse(line);
    check(cmd.kind == Kind::Faces && cmd.facesMask == 65, std::string("serial face-mask alias ") + line);
  }
  check(serialcmd::parse("FACES:65535").facesMask == 65535, "serial accepts full 16-bit mask");
  for (const char *prefix : {"FACES:", "H="})
    for (const char *arg : {"", " ", "garbage", "1x", "1 2", "0x", "0", "-1", "65536",
                            "999999999999999999999999999999999999"}) {
      std::string line = std::string(prefix) + arg;
      check(serialcmd::parse(line.c_str()).kind == Kind::None, "serial refuses mask " + line);
    }

  auto t = serialcmd::parse(" \tTIME: +1800000000 \t");
  check(t.kind == Kind::Time && t.time == 1800000000, "serial valid time whitespace");
  check(serialcmd::parse("TIME:100001").kind == Kind::Time, "serial preserves earliest accepted seconds");
  std::string limit = "TIME:" + std::to_string(numericinput::maxTimeSeconds());
  check(serialcmd::parse(limit.c_str()).kind == Kind::Time, "serial accepts representable timestamp limit");
  for (const char *arg : {"", " ", "garbage", "1800000000x", "1800000000 1", "1800000000.5", "1e9",
                          "100000", "-1800000000", "999999999999999999999999999999999999"}) {
    std::string line = std::string("TIME:") + arg;
    check(serialcmd::parse(line.c_str()).kind == Kind::None, "serial refuses time " + line);
  }
  std::string tooLate = "TIME:" + std::to_string(numericinput::maxTimeSeconds() + 1);
  check(serialcmd::parse(tooLate.c_str()).kind == Kind::None, "serial refuses timestamp beyond safe milliseconds");

  serialcmd::LineBuffer buffer;
  bool ready = false;
  for (char c : std::string("BRIGHT:2\r\n"))
    if (buffer.push(c)) ready = serialcmd::parse(buffer.line()).kind == Kind::Bright;
  check(ready, "serial framing passes a complete new command");
  ready = false;
  for (char c : std::string("FACE:") + std::string(150, '1') + "\n") ready |= buffer.push(c);
  check(!ready, "serial oversized numeric line is dropped whole");
  for (char c : std::string("FACE:11\n"))
    if (buffer.push(c)) ready = serialcmd::parse(buffer.line()).value == 11;
  check(ready, "serial parser recovers after oversized line");
}

void phoneChecks() {
  phone::begin();
  struct Family { const char *prefix; int low, high; };
  const Family families[] = {{"B=", 0, 3}, {"M=", 0, 2}, {"F=", 0, FACE_COUNT - 1},
                             {"FO=", 12, 24}, {"FORMAT=", 12, 24}, {"NM=", 0, 3}, {"NIGHT=", 0, 3}};
  for (const Family &f : families) {
    for (int value : {f.low, f.high}) {
      std::string line = std::string(f.prefix) + " +" + std::to_string(value) + " \t";
      check(strcmp(phone::apply(line.c_str()), "ok") == 0, "phone accepts boundary/whitespace " + line);
      if (!strcmp(f.prefix, "B=")) check(settings.brightnessLevel == value, "phone brightness applied");
      else if (!strcmp(f.prefix, "M=")) check(settings.motion == value, "phone motion applied");
      else if (!strcmp(f.prefix, "F=")) check(settings.face == value && (settings.activeFaces & (1u << value)), "phone face applied");
      else if (!strcmp(f.prefix, "FO=") || !strcmp(f.prefix, "FORMAT=")) check(settings.use12h == (value == 12), "phone format applied");
      else check(settings.night == value, "phone night applied");
    }
    for (const char *arg : {"", " \t", "garbage", "1garbage", "1 2", "0x1", "1.0", "+", "--1",
                            "999999999999999999999999999999999999", "-99999999999999999999999999999999999"})
      refusePhone(std::string(f.prefix) + arg);
    refusePhone(std::string(f.prefix) + std::to_string(f.low - 1));
    refusePhone(std::string(f.prefix) + std::to_string(f.high + 1));
  }
  refusePhone("FO=13");
  refusePhone("FORMAT=23");
  for (const char *line : {"H=65", "H=0x0041", "H=0101", "H= +0x0041 \t"}) {
    check(strcmp(phone::apply(line), "ok") == 0 && settings.activeFaces == 65,
          std::string("phone accepts mask alias ") + line);
  }
  check(strcmp(phone::apply("H=65535"), "ok") == 0 && settings.activeFaces == ALL_FACES_MASK,
        "phone limits a valid 16-bit mask to available faces");
  for (const char *arg : {"", " ", "garbage", "1x", "1 2", "0x", "0", "-1", "32768", "65536",
                          "999999999999999999999999999999999999"}) refusePhone(std::string("H=") + arg);

  for (const char *line : {"T=1800000000", "T= +1800000000.250 \t", "T=1800000000.001", "T=1800000000.999999"}) {
    check(strcmp(phone::apply(line), "ok") == 0 && clockSeconds == 1800000000,
          std::string("phone accepts decimal time ") + line);
    int expected = strstr(line, ".250") ? 250 : strstr(line, ".001") ? 1 : strstr(line, ".999") ? 999 : 0;
    check(clockFraction == expected, std::string("phone preserves fractional milliseconds ") + line);
  }
  for (const char *arg : {"", " ", "garbage", "nan", "inf", "-inf", "1e309", "1.8e9", "1800000000x",
                          "1800000000.250x", "1800000000.", "1800000000 1", "12345", "-1800000000",
                          "999999999999999999999999999999999999"}) refusePhone(std::string("T=") + arg);
  std::string latest = "T=" + std::to_string(numericinput::maxTimeSeconds()) + ".999";
  check(strcmp(phone::apply(latest.c_str()), "ok") == 0 && clockSeconds == numericinput::maxTimeSeconds() && clockFraction == 999,
        "phone accepts the latest safely representable fractional timestamp");
  refusePhone("T=" + std::to_string(numericinput::maxTimeSeconds() + 1));

  for (const char *line : {"O= +50400 \t", "O=-50400", "O=0", "O=7200"})
    check(strcmp(phone::apply(line), "ok") == 0, std::string("phone accepts offset ") + line);
  for (const char *arg : {"", " ", "garbage", "7200x", "1 2", "50401", "-50401",
                          "999999999999999999999999999999999999"}) refusePhone(std::string("O=") + arg);
  check(strcmp(phone::apply("L=2026-07-01 13:00:00"), "ok") == 0 && clockSeconds == 1782903600,
        "phone local date uses the unchanged valid offset after rejections");
  check(strcmp(phone::apply("L=2024-02-29 12:34:56 \t"), "ok") == 0, "phone accepts real leap day and trailing space");
  for (const char *arg : {"", "garbage", "2026-02-29 12:00:00", "2026-04-31 12:00:00", "2026-00-01 12:00:00",
                          "2026-13-01 12:00:00", "2026-01-00 12:00:00", "2026-01-01 24:00:00",
                          "2026-01-01 12:60:00", "2026-01-01 12:00:60", "2026-01-01 12:00:00junk",
                          "2026-01-01 12:00", "2026-01-01 12:00:00 1", "999999999999999999999-01-01 00:00:00"})
    refusePhone(std::string("L=") + arg);

  uint8_t current[10] = {0xE8, 0x07, 2, 29, 12, 34, 56, 0, 0, 0};
  char out[40];
  check(phone::fromCurrentTime(current, sizeof current, out, sizeof out) && !strcmp(out, "L=2024-02-29 12:34:56"),
        "standard Current Time Service accepts leap day");
  current[0] = 0xE9;
  check(!phone::fromCurrentTime(current, sizeof current, out, sizeof out), "standard Current Time Service refuses invalid leap day");

  phone::begin();  // Also check the local-zone path before any explicit offset is supplied.
  int before = clockWrites;
  check(strcmp(phone::apply("L=2026-07-01 13:00:00 \t"), "ok") == 0 && clockWrites == before + 1,
        "phone accepts a valid date in the current local zone");
  refusePhone("L=2026-02-29 12:00:00");
  refusePhone("L=2026-01-01 24:00:00");
  refusePhone("L=2026-01-01 12:00:00 trailing");
  refusePhone("O=999999999999999999999999999999999999");
}
}  // namespace

Settings settings;
void saveSettings() { saves++; }
namespace hal {
time_t now() { return clockSeconds; }
void backlight(uint8_t) { backlights++; }
}
namespace net {
bool timeIsValid() { return clockSeconds >= 1700000000; }
void setClockFromPhone(time_t seconds, int ms) {
  clockWrites++;
  clockSeconds = seconds;
  clockFraction = ms;
}
void setZone(const char *posix, const char *name) {
  zoneWrites++;
  if (posix) settings.tz = posix;
  if (name) fakeZoneName = name;
}
void saveNetwork(const char *ssid, const char *) { networkWrites++; fakeNetworkName = ssid; }
void startSync() { syncs++; }
const char *ssid() { return fakeNetworkName.c_str(); }
}

int main() {
  serialChecks();
  phoneChecks();
  printf("Commands: %d of %d checks passed\n", checks - failed, checks);
  return failed ? 1 : 0;
}
