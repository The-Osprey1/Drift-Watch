// Minimal Arduino stand-in so the watch code compiles on a computer for previews and tests.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>

#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295f
#endif
#define PROGMEM

using std::max;
using std::min;

template <class T, class L, class H>
auto constrain(T x, L lo, H hi) -> decltype(x + lo + hi) { return x < lo ? lo : x > hi ? hi : x; }

class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  const char *c_str() const { return s_.c_str(); }
  bool isEmpty() const { return s_.empty(); }
  size_t length() const { return s_.size(); }
  String &operator=(const char *s) { s_ = s ? s : ""; return *this; }
  bool operator==(const char *o) const { return s_ == o; }
  void trim() {
    size_t a = s_.find_first_not_of(" \t\r\n"), b = s_.find_last_not_of(" \t\r\n");
    s_ = a == std::string::npos ? "" : s_.substr(a, b - a + 1);
  }
  bool startsWith(const char *p) const { return s_.rfind(p, 0) == 0; }
  int indexOf(char c) const { size_t i = s_.find(c); return i == std::string::npos ? -1 : (int)i; }
  String substring(int from) const { return from >= (int)s_.size() ? String() : String(s_.substr(from)); }
  String substring(int from, int to) const { return from >= to ? String() : String(s_.substr(from, to - from)); }
  long toInt() const { return atol(s_.c_str()); }
 private:
  std::string s_;
};

// Serial: the simulation types into `in` (bytes not yet read), and what the watch prints is
// dropped.
struct HostSerial {
  std::string in;
  int available() { return (int)in.size(); }
  int read() {
    if (in.empty()) return -1;
    int c = (unsigned char)in[0];
    in.erase(0, 1);
    return c;
  }
  template <class... A> void printf(const char *, A...) {}
};
extern HostSerial Serial;

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
