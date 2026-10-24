// Stand-in for the ESP32 HTTPClient, as the time zone lookup uses it. See netsim.h.
#pragma once
#include <Arduino.h>
#include "netsim.h"

class HTTPClient {
 public:
  void setConnectTimeout(int32_t) {}
  void setTimeout(uint16_t) {}
  bool begin(const char *url) {
    netsim::httpUrl = url;
    return netsim::httpBeginOk;
  }
  int GET() {
    netsim::httpGets++;
    return netsim::httpCode;
  }
  String getString() { return String(netsim::httpBody); }
  void end() {}
};
