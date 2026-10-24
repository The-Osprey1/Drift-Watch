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
  if (sawLow_ && ms - upSince_ >= HOLD_MS) {
    // Decided once the screen has been held up: a raise if it was quick, otherwise it was too
    // slow, and a later jolt while the wrist stays up does not turn it into one.
    sawLow_ = false;
    if (upSince_ - lowAt_ <= WINDOW_MS && peak_ >= FLICK_DPS) {
      lastPeak_ = peak_;
      return true;
    }
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

bool TwistDetector::update(uint32_t ms, const Accel &a, int8_t facing) {
  float g = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  float rate = havePrev_ ? tiltRate(prev_, prevMs_, a, ms) : 0;
  prev_ = a;
  prevMs_ = ms;
  havePrev_ = true;
  if (g < 0.3f) return false;  // free fall or a bad read: no direction
  float ux = a.x / g, uy = a.y / g, uz = a.z / g;
  bool calm = rate < CALM_DPS && fabsf(g - 1) <= CALM_G;
  if (!haveRef_) {
    rx_ = ux, ry_ = uy, rz_ = uz;
    haveRef_ = true;
    calmSince_ = calm ? ms : 0;
    return false;
  }
  float c = ux * rx_ + uy * ry_ + uz * rz_;
  float angle = acosf(c > 1 ? 1 : c < -1 ? -1 : c) * 57.29578f;

  switch (phase_) {
    case Phase::Resting:
      if (calm) {
        // Follow slow drift of the resting position.
        rx_ = 0.7f * rx_ + 0.3f * ux, ry_ = 0.7f * ry_ + 0.3f * uy, rz_ = 0.7f * rz_ + 0.3f * uz;
        float n = sqrtf(rx_ * rx_ + ry_ * ry_ + rz_ * rz_);
        rx_ /= n, ry_ /= n, rz_ /= n;
        if (!calmSince_) calmSince_ = ms ? ms : 1;
        return false;
      }
      if (angle >= LEAVE_DEG && calmSince_ && ms - calmSince_ >= CALM_MS && ms - firedAt_ >= COOLDOWN_MS &&
          rz_ * facing >= REST_Z) {
        phase_ = Phase::Leaving;
        leftAt_ = ms;
        peak_ = rate;
      } else if (angle >= LEAVE_DEG) {
        calmSince_ = 0;  // moving without a still moment first: not a twist
      }
      return false;
    case Phase::Leaving:
      peak_ = rate > peak_ ? rate : peak_;
      if (angle >= OUT_DEG && peak_ >= TWIST_DPS) {
        phase_ = Phase::Out;
        outAt_ = ms;
      } else if (ms - leftAt_ > OUT_MS || angle < LEAVE_DEG / 2) {
        phase_ = Phase::Resting;
        calmSince_ = 0;
      }
      return false;
    case Phase::Out:
      peak_ = rate > peak_ ? rate : peak_;
      if (angle <= BACK_DEG) {
        phase_ = Phase::Resting;
        calmSince_ = 0;
        firedAt_ = ms;
        lastPeak_ = peak_;
        return true;
      }
      if (ms - outAt_ > BACK_MS) {
        // Turned and stayed: a new resting position, not a twist.
        phase_ = Phase::Resting;
        calmSince_ = 0;
        rx_ = ux, ry_ = uy, rz_ = uz;
      }
      return false;
  }
  return false;
}

void TwistDetector::reset() {
  phase_ = Phase::Resting;
  haveRef_ = havePrev_ = false;
  calmSince_ = 0;
  peak_ = 0;
}

