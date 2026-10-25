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
// Says once when the battery falls below 15% and once more below 5%. A level is said again only
// after the battery has been charged 5 points above it, so a reading wavering on the line does
// not repeat it. Fed the smoothed level, so one low reading (the radio drawing current) does not
// set it off. Nothing is said while charging.
class LowWarning {
 public:
  static constexpr int SOON = 15, NOW = 5, MARGIN = 5;
  // The level just crossed (SOON or NOW; NOW when both were crossed at once), or 0.
  int update(float level, bool charging);

 private:
  bool toldSoon_ = false, toldNow_ = false;
};
}  // namespace battery
