#include "raise.h"
#include <math.h>

float tiltRate(const Accel &a, uint32_t aMs, const Accel &b, uint32_t bMs) {
  uint32_t dt = bMs - aMs;
  if (dt < 20 || dt > 400) return 0;  // too close to measure, or a gap in the samples
  float na = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z), nb = sqrtf(b.x * b.x + b.y * b.y + b.z * b.z);
  if (na < 0.3f || nb < 0.3f) return 0;  // free fall or a bad read: no direction
  float c = (a.x * b.x + a.y * b.y + a.z * b.z) / (na * nb);
  c = c > 1 ? 1 : c < -1 ? -1 : c;
  return acosf(c) * 57.29578f / (dt / 1000.0f);
}

bool RaiseDetector::update(uint32_t ms, const Accel &a) {
  float z = a.z * sign_;
  float g = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  float rate = havePrev_ ? tiltRate(prev_, prevMs_, a, ms) : 0;
  prev_ = a;
  prevMs_ = ms;
  havePrev_ = true;
  if (z < DOWN_Z) {
    sawLow_ = true;
    lowAt_ = ms;
  }
  // Fastest turn in the last WINDOW_MS, wherever it fell between down and up.
  if (rate >= peak_ || ms - peakAt_ > WINDOW_MS) {
    peak_ = rate;
    peakAt_ = ms;
  }
  bool upNow = z >= UP_Z && fabsf(g - 1) <= STEADY_G;
  if (!upNow) {
    up_ = false;
    return false;
  }
  if (!up_) {
    up_ = true;
    upSince_ = ms;
  }
  if (sawLow_ && ms - upSince_ >= HOLD_MS && upSince_ - lowAt_ <= WINDOW_MS && peak_ >= FLICK_DPS) {
    sawLow_ = false;  // one wake per raise
    lastPeak_ = peak_;
    return true;
  }
  return false;
}

void RaiseDetector::reset() {
  sawLow_ = false;
  up_ = false;
  havePrev_ = false;
  peak_ = 0;
}

void RaiseDetector::learnFacing(const Accel &a) {
  // Only clear readings count: a screen held on edge says nothing about which side is the front.
  if (fabsf(a.z) < 0.5f) return;
  learned_ = 0.8f * learned_ + 0.2f * (a.z > 0 ? 1.0f : -1.0f);
  if (learned_ > 0.3f) sign_ = 1;
  else if (learned_ < -0.3f) sign_ = -1;
}

void RaiseDetector::setFacing(int8_t s) {
  sign_ = s < 0 ? -1 : 1;
  learned_ = 0.5f * sign_;
}

bool DropDetector::update(uint32_t ms, float z, float g) {
  if (touchedAt_ && ms - touchedAt_ < TOUCH_QUIET_MS) return false;
  bool steadyUp = z >= RaiseDetector::UP_Z && fabsf(g - 1) <= 0.3f;
  if (!armed_) {
    if (!steadyUp) {
      upSince_ = 0;
      return false;
    }
    if (!upSince_) upSince_ = ms ? ms : 1;
    if (ms - upSince_ >= LOOK_MS) armed_ = true;
    return false;
  }
  if (z >= AWAY_Z) {
    awaySince_ = 0;
    return false;
  }
  if (!awaySince_) awaySince_ = ms ? ms : 1;
  if (ms - awaySince_ < AWAY_MS) return false;
  armed_ = false;
  awaySince_ = 0;
  return true;
}

void DropDetector::reset() {
  armed_ = false;
  upSince_ = awaySince_ = 0;
  touchedAt_ = 0;
}
