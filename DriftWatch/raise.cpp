#include "raise.h"
#include <math.h>

bool RaiseDetector::update(uint32_t ms, const Accel &a) {
  float z = a.z * sign_;
  float g = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  if (z < DOWN_Z) {
    sawLow_ = true;
    lowAt_ = ms;
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
  if (sawLow_ && ms - upSince_ >= HOLD_MS && upSince_ - lowAt_ <= WINDOW_MS) {
    sawLow_ = false;  // one wake per raise
    return true;
  }
  return false;
}

void RaiseDetector::reset() {
  sawLow_ = false;
  up_ = false;
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
