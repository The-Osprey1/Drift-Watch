// Battery percentage from voltage, and an estimate of the time left measured from how fast
// the percentage actually falls on this watch (the board has no current sensor or charge
// line, and the battery size is not known, so nothing is assumed). Pure logic with no
// hardware, so the computer simulation tests it.
#pragma once
#include <stdint.h>

namespace battery {
// Rough single-cell lithium curve; -1 when nothing plausible is on the pin.
int percentFromMilliVolts(int mv);

class Estimate {
 public:
  static constexpr uint32_t MIN_SPAN_MS = 45UL * 60 * 1000;  // measure at least 45 minutes
  static constexpr float MIN_DROP = 3;                        // and at least 3% of fall
  static constexpr float RISE = 3;                            // a rise this big means charging

  // Feeds one reading (every few seconds is fine).
  void add(uint32_t ms, int pct);
  // Hours left at the measured rate, or < 0 while still measuring.
  float hoursLeft() const;
  bool charging() const { return charging_; }
  float smoothed() const { return level_; }

 private:
  bool started_ = false, charging_ = false;
  float level_ = 0;        // smoothed percentage: single readings jump by a few percent
  float low_ = 0;          // lowest smoothed level since the start of the span
  uint32_t anchorMs_ = 0;  // start of the span being measured
  float anchorLevel_ = 0;
  float ratePerHour_ = -1; // % per hour, < 0 = not known yet
};
}  // namespace battery
