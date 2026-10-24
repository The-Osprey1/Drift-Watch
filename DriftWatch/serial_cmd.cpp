#include "serial_cmd.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

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
    char digits[24];
    size_t n = len - 5;
    if (n >= sizeof digits) return cmd;
    memcpy(digits, line + 5, n);
    digits[n] = '\0';
    long t = atol(digits);
    if (t > 100000) {
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
  }
  return cmd;
}

}  // namespace serialcmd
