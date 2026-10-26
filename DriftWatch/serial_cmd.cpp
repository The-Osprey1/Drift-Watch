#include "serial_cmd.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "face_id.h"
#include "numeric_input.h"

namespace serialcmd {

bool LineBuffer::push(char c) {
  if (c == '\n' || c == '\r') {
    bool ready = len_ > 0 && !overflow_;
    buf_[ready ? len_ : 0] = '\0';
    len_ = 0;
    overflow_ = false;
    return ready;
  }
  if (overflow_) return false;
  if (len_ >= CAPACITY - 1) {
    overflow_ = true;
    len_ = 0;
    return false;
  }
  buf_[len_++] = c;
  return false;
}

Command parse(const char *line) {
  Command cmd;
  while (isspace((unsigned char)*line)) line++;
  size_t len = strlen(line);
  while (len && isspace((unsigned char)line[len - 1])) len--;

  if (len > 5 && strncmp(line, "TIME:", 5) == 0) {
    int64_t t;
    if (numericinput::integer(line + 5, 10, 100001, numericinput::maxTimeSeconds(), t)) {
      cmd.kind = Kind::Time;
      cmd.time = (time_t)t;
    }
  } else if (len > 5 && strncmp(line, "WIFI:", 5) == 0) {
    const char *ssid = line + 5, *end = line + len;
    const char *comma = (const char *)memchr(ssid, ',', end - ssid);
    if (!comma || comma == ssid) return cmd;  // a network name is needed; the password may be empty
    size_t ssidLen = comma - ssid, passLen = end - comma - 1;
    if (ssidLen >= sizeof cmd.ssid || passLen >= sizeof cmd.password) return cmd;
    memcpy(cmd.ssid, ssid, ssidLen);
    memcpy(cmd.password, comma + 1, passLen);
    cmd.kind = Kind::Wifi;
  } else if ((len > 6 && strncmp(line, "FACES:", 6) == 0) || (len > 2 && strncmp(line, "H=", 2) == 0)) {
    const char *arg = (line[0] == 'F') ? line + 6 : line + 2;
    int64_t m;
    if (numericinput::integer(arg, 0, 1, 0xFFFF, m)) {
      cmd.kind = Kind::Faces;
      cmd.facesMask = (uint16_t)m;
    }
  } else if ((len == 5 && strncmp(line, "INFO?", len) == 0) || (len == 1 && line[0] == '?')) {
    cmd.kind = Kind::Info;
  } else if (len > 6 && strncmp(line, "TIMER:", 6) == 0) {
    int64_t sec;
    if (numericinput::integer(line + 6, 10, 0, 86400, sec)) {
      cmd.kind = Kind::Timer;
      cmd.value = (int)sec;
    }
  } else if (len > 7 && strncmp(line, "BRIGHT:", 7) == 0) {
    int64_t lvl;
    if (numericinput::integer(line + 7, 10, 0, 3, lvl)) {
      cmd.kind = Kind::Bright;
      cmd.value = (int)lvl;
    }
  } else if (len > 7 && strncmp(line, "MOTION:", 7) == 0) {
    int64_t m;
    if (numericinput::integer(line + 7, 10, 0, 2, m)) {
      cmd.kind = Kind::Motion;
      cmd.value = (int)m;
    }
  } else if (len > 7 && strncmp(line, "FORMAT:", 7) == 0) {
    int64_t f;
    if (numericinput::integer(line + 7, 10, 12, 24, f) && (f == 12 || f == 24)) {
      cmd.kind = Kind::Format;
      cmd.value = (int)f;
    }
  } else if (len > 6 && strncmp(line, "NIGHT:", 6) == 0) {
    int64_t n;
    if (numericinput::integer(line + 6, 10, 0, 3, n)) {
      cmd.kind = Kind::Night;
      cmd.value = (int)n;
    }
  } else if (len > 5 && strncmp(line, "FACE:", 5) == 0) {
    int64_t f;
    if (numericinput::integer(line + 5, 10, 0, FACE_COUNT - 1, f)) {
      cmd.kind = Kind::Face;
      cmd.value = (int)f;
    }
  }
  return cmd;
}

}  // namespace serialcmd
