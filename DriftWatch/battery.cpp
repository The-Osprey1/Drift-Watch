#include "battery.h"

namespace battery {

int percentFromMilliVolts(int mv) {
  static const int volts[] = {3300, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200};
  static const int pct[] = {0, 5, 15, 30, 50, 65, 80, 90, 100};
  if (mv < 2500) return -1;  // nothing plausible on the pin
  if (mv <= volts[0]) return 0;
  for (int i = 1; i < 9; i++)
    if (mv < volts[i]) return pct[i - 1] + (pct[i] - pct[i - 1]) * (mv - volts[i - 1]) / (volts[i] - volts[i - 1]);
  return 100;
}

void Estimate::add(uint32_t ms, int pct) {
  if (pct < 0) return;
  if (!started_) {
    started_ = true;
    level_ = low_ = anchorLevel_ = (float)pct;
    anchorMs_ = ms;
    return;
  }
  level_ += 0.1f * ((float)pct - level_);
  if (level_ < low_) low_ = level_;
  float riseNeeded = (low_ > 100.0f - RISE) ? ((100.0f - low_) * 0.5f) : RISE;
  if (riseNeeded < 0.5f) riseNeeded = 0.5f;
  if (level_ > low_ + riseNeeded || (pct >= 99 && level_ >= 98.0f && level_ > low_ + 0.4f)) {
    // Rising: on the charger. Start measuring again once it comes off.
    charging_ = true;
    low_ = anchorLevel_ = level_;
    anchorMs_ = ms;
    return;
  }
  if (charging_ && level_ < anchorLevel_ - 1.5f) {
    charging_ = false;  // falling again: reset anchor and low point
    low_ = anchorLevel_ = level_;
    anchorMs_ = ms;
  }
  if (charging_) {
    anchorLevel_ = level_ > anchorLevel_ ? level_ : anchorLevel_;
    anchorMs_ = ms;
    return;
  }
  uint32_t span = ms - anchorMs_;
  float drop = anchorLevel_ - level_;
  if (span >= MIN_SPAN_MS && drop >= MIN_DROP) {
    float rate = drop / (span / 3600000.0f);
    ratePerHour_ = ratePerHour_ < 0 ? rate : 0.5f * ratePerHour_ + 0.5f * rate;
    anchorMs_ = ms;
    anchorLevel_ = level_;
  }
}

int LowWarning::update(float level, bool charging) {
  if (toldSoon_ && level >= SOON + MARGIN) toldSoon_ = false;
  if (toldNow_ && level >= NOW + MARGIN) toldNow_ = false;
  if (charging || level < 0) return 0;
  int crossed = 0;
  if (!toldSoon_ && level < SOON) {
    toldSoon_ = true;
    crossed = SOON;
  }
  if (!toldNow_ && level < NOW) {
    toldNow_ = true;
    crossed = NOW;
  }
  return crossed;
}

float Estimate::hoursLeft() const {
  if (ratePerHour_ <= 0 || charging_) return -1;
  return level_ / ratePerHour_;
}

}  // namespace battery
