#include "timekeep.h"

void ClockTrim::setPpb(int32_t ppb) {
  ppb_ = ppb > MAX_PPB ? MAX_PPB : ppb < -MAX_PPB ? -MAX_PPB : ppb;
}

bool ClockTrim::learn(int64_t trueMs, int64_t errorMs) {
  bool changed = false;
  int64_t span = trueMs - refMs_;
  if (refMs_ && span >= MIN_SPAN_MS) {
    // The nudges since the last sync already corrected for the old rate, so what is left
    // over is the error in that rate. Up to NUDGE_MS of that correction can still be owed and
    // not yet applied; it is part of the old rate's work, not a new error, so it is taken off.
    // (Left in, it made a 6 hour span off by up to 9 ppm.)
    double residual = ((double)errorMs + owedMs_) / (double)span * 1e9;
    // Off by more than any clock drifts (a time zone mix-up, a clock set wrongly by hand):
    // nothing to learn, so start a new span from here.
    if (residual > MAX_PPB || residual < -MAX_PPB) {
      refMs_ = trueMs;
      lastMs_ = trueMs;
      owedMs_ = 0;
      return false;
    }
    int32_t before = ppb_;
    setPpb((int32_t)(ppb_ + residual));
    changed = ppb_ != before;
  }
  refMs_ = trueMs;
  lastMs_ = trueMs;
  owedMs_ = 0;
  return changed;
}

void ClockTrim::forget() {
  refMs_ = 0;
  owedMs_ = 0;
  lastMs_ = 0;
}

int64_t ClockTrim::due(int64_t nowMs) {
  if (!lastMs_ || nowMs < lastMs_) {
    lastMs_ = nowMs;  // first call, or the clock was set backwards
    return 0;
  }
  owedMs_ -= (double)(nowMs - lastMs_) * ppb_ / 1e9;
  lastMs_ = nowMs;
  if (owedMs_ > -NUDGE_MS && owedMs_ < NUDGE_MS) return 0;
  int64_t step = (int64_t)owedMs_;
  owedMs_ -= step;
  lastMs_ += step;  // the caller moves the clock by this much
  return step;
}
