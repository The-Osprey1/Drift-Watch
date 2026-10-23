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
 private:
  std::string s_;
};

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
