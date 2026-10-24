// Wrist gestures from the accelerometer: raise-to-wake spots the wrist turning the screen up
// towards the eyes and holding it there; wrist-down sleep spots the screen being turned away
// or the arm dropped after a look. Pure logic with no hardware, so the computer simulation
// tests the real thing.
//
// The turn rate comes from how fast the direction of gravity moves between samples, which
// is the wrist's roll and pitch rate: the part of the gyroscope's reading that matters here.
// That keeps the gyroscope off (it draws far more than the accelerometer's low-power mode).
#pragma once
#include <stdint.h>
#include "imu.h"

// Turn rate between two samples, in degrees per second, from the angle between the two
// gravity directions. Returns 0 if the samples are too close or too far apart in time.
float tiltRate(const Accel &a, uint32_t aMs, const Accel &b, uint32_t bMs);

class RaiseDetector {
 public:
  // Feeds one sample; returns true once for each raise.
  bool update(uint32_t ms, const Accel &a);
  // Forgets the motion so far: called when the screen turns off, so a wrist that is still
  // up has to come down and rise again before it wakes the screen.
  void reset();
  // The screen is being looked at now (the person touched it or pressed BOOT), so the
  // direction gravity points tells which way the screen faces on this board.
  void learnFacing(const Accel &a);
  int8_t facing() const { return sign_; }  // +1: the screen faces along the chip's +z; -1: along -z
  void setFacing(int8_t s);
  float lastPeakRate() const { return lastPeak_; }  // turn rate of the latest raise, for the log

  // Tuning, in g along the screen's outward direction (+1 = screen facing straight up).
  static constexpr float DOWN_Z = 0.35f;     // screen turned away: arm hanging, typing, screen down
  static constexpr float UP_Z = 0.65f;       // screen tilted up within about 50 degrees of level
  static constexpr float STEADY_G = 0.2f;    // total acceleration within this of 1 g: not being shaken
  static constexpr uint32_t HOLD_MS = 200;   // held up this long
  static constexpr uint32_t WINDOW_MS = 1500; // the turn up took no longer than this
  static constexpr float FLICK_DPS = 80;     // and turned at least this fast at some point

 private:
  int8_t sign_ = 1;
  float learned_ = 0.5f;    // running average of z while being looked at
  bool sawLow_ = false;
  uint32_t lowAt_ = 0, upSince_ = 0;
  bool up_ = false;
  bool havePrev_ = false;
  Accel prev_{};
  uint32_t prevMs_ = 0, peakAt_ = 0;
  float peak_ = 0, lastPeak_ = 0;  // fastest turn in the last WINDOW_MS
};

// Wrist-down sleep: once the screen has been held up to look at, turning it away (or letting
// the arm drop) for AWAY_MS turns the screen off without waiting for the timeout. It is only
// armed after a steady look, and any touch puts it off for a while, so reading the watch
// lying down or holding it out to show someone does not blank it.
class DropDetector {
 public:
  bool update(uint32_t ms, float screenZ, float totalG);  // true once when it is time to sleep
  void reset();                                           // the screen just woke
  void touched(uint32_t ms) { touchedAt_ = ms; armed_ = false; upSince_ = 0; awaySince_ = 0; }

  static constexpr float AWAY_Z = 0.2f;        // screen turned past about 78 degrees from level
  static constexpr uint32_t AWAY_MS = 360;     // for this long (samples come every 80 ms)
  static constexpr uint32_t LOOK_MS = 300;     // armed after the screen was up this long
  static constexpr uint32_t TOUCH_QUIET_MS = 2000;

 private:
  bool armed_ = false;
  uint32_t upSince_ = 0, awaySince_ = 0, touchedAt_ = 0;
};

// Wrist twist to wake: a quick turn of the wrist out and straight back, like shaking off a
// drop of water, from wherever the watch was resting. Works when raise to wake does not: the
// wrist already up on a desk, or lying on the arm of a chair. It needs the forearm to be off
// vertical (turning a hanging arm does not move gravity), the screen resting at least partly
// face up (so a hanging arm swung up and down is not mistaken for one), and a still moment
// first, so arm swings while walking do not count.
class TwistDetector {
 public:
  // `facing` is RaiseDetector::facing(): which way the screen points along the chip's z.
  bool update(uint32_t ms, const Accel &a, int8_t facing);  // true once for each twist
  void reset();
  float lastPeakRate() const { return lastPeak_; }

  static constexpr float CALM_DPS = 30;        // resting: turning slower than this
  static constexpr float CALM_G = 0.15f;       // and within this of 1 g
  static constexpr float REST_Z = 0.35f;       // with the screen at least this far face up
  static constexpr uint32_t CALM_MS = 300;     // for this long before the twist
  static constexpr float LEAVE_DEG = 10;       // the twist has started
  static constexpr float OUT_DEG = 40;         // turned at least this far
  static constexpr uint32_t OUT_MS = 400;      // that quickly
  static constexpr float TWIST_DPS = 150;      // and this fast at some point
  static constexpr float BACK_DEG = 15;        // then back within this of where it started
  static constexpr uint32_t BACK_MS = 400;     // this soon after reaching OUT_DEG (a lowered arm stays down longer)
  static constexpr uint32_t COOLDOWN_MS = 1000;

 private:
  enum class Phase : uint8_t { Resting, Leaving, Out } phase_ = Phase::Resting;
  bool haveRef_ = false, havePrev_ = false;
  float rx_ = 0, ry_ = 0, rz_ = 1;  // resting direction of gravity, unit length
  Accel prev_{};
  uint32_t prevMs_ = 0, calmSince_ = 0, leftAt_ = 0, outAt_ = 0, firedAt_ = 0;
  float peak_ = 0, lastPeak_ = 0;
};

