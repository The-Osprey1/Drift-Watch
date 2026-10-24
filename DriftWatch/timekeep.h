// Keeping the clock right between syncs. The board has no clock chip or 32 kHz crystal, so
// while the processor is in light sleep the time is kept by its internal RC oscillator,
// which can be off by a fraction of a percent and changes with temperature. Each trusted
// time (a Wi-Fi or phone sync) shows how far the watch drifted since the previous one; the
// rate is learned and the clock is nudged by that rate from then on. Pure logic with no
// hardware, so the computer simulation tests it.
#pragma once
#include <stdint.h>

class ClockTrim {
 public:
  static constexpr int64_t MIN_SPAN_MS = 6LL * 3600 * 1000;  // learn only over at least 6 hours
  static constexpr int32_t MAX_PPB = 30000000;               // 3%: anything more is not drift

  void setPpb(int32_t ppb);
  int32_t ppb() const { return ppb_; }
  // A trusted time arrived at `trueMs` (wall clock, ms). `errorMs` is how far ahead the watch
  // was just before it was corrected (+ = it ran fast). Returns true if the rate changed.
  bool learn(int64_t trueMs, int64_t errorMs);
  // The clock was set by hand: too rough to learn from, so the next sync starts a new span.
  // The learned rate is kept.
  void forget();
  // Milliseconds to add to the clock now (negative slows it down). Call every few seconds
  // with the wall clock; nudges come in steps of at least NUDGE_MS.
  int64_t due(int64_t nowMs);
  static constexpr int64_t NUDGE_MS = 200;

 private:
  int32_t ppb_ = 0;
  int64_t refMs_ = 0;   // last trusted time, 0 = none since start or since a hand setting
  int64_t lastMs_ = 0;  // wall clock at the last call to due()
  double owedMs_ = 0;   // correction built up but not yet applied
};
