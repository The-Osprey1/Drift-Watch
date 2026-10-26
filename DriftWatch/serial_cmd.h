// Commands typed over the USB-C serial port (115200 baud), one per line. Out-of-range
// values are not a command at all, so a typo changes nothing:
//   TIME:<unix seconds>         sets the clock
//   WIFI:<network>,<password>   saves a Wi-Fi network and joins it
//   FACES:<mask>, H=<mask>      which faces are in the swipe order (16-bit; H= takes 0x hex)
//   FACE:<0..FACE_COUNT-1>      shows that face, turning it on if it was off
//   TIMER:<0..86400>            starts a countdown of that many seconds; 0 resets it
//   BRIGHT:<0..3>               screen brightness, as a step not a percentage
//   MOTION:<0..2>               how much the interface moves (off, subtle, expressive)
//   FORMAT:<12|24>              clock format
//   NIGHT:<0..3>                night mode window (0 = off)
//   INFO?, ?                    prints one DRIFT: line of version and current settings
// Bytes are collected as they arrive, so a half-sent line never holds up the interface the
// way Serial.readStringUntil did (it waited up to a second for the newline). Pure logic with
// no hardware, so the computer simulation tests it.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

namespace serialcmd {

// Collects bytes into lines. A line longer than the buffer is dropped whole rather than cut
// short, so a long Wi-Fi password is never saved with its end missing.
class LineBuffer {
 public:
  static constexpr size_t CAPACITY = 128;  // "WIFI:" + 32-byte network + "," + 63-byte password fits

  // Adds one byte. Returns true when it ended a non-empty line, which line() then holds
  // until the next push.
  bool push(char c);
  const char *line() const { return buf_; }

 private:
  char buf_[CAPACITY] = {};
  size_t len_ = 0;
  bool overflow_ = false;  // this line ran past the buffer; skip to its end
};

enum class Kind : uint8_t { None, Time, Wifi, Faces, Info, Timer, Bright, Motion, Format, Night, Face };
struct Command {
  Kind kind = Kind::None;
  time_t time = 0;
  uint16_t facesMask = 0;
  int value = 0;
  char ssid[33] = {};
  char password[64] = {};
};

// Reads one line. Leading and trailing spaces are ignored. Anything that is not a valid
// command gives Kind::None.
Command parse(const char *line);

}  // namespace serialcmd
