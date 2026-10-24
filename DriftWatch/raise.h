// Raise-to-wake: spots the wrist turning the screen up towards the eyes and holding it
// there. Pure logic with no hardware, so the computer simulation tests the real thing.
#pragma once
#include <stdint.h>
#include "imu.h"

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

  // Tuning, in g along the screen's outward direction (+1 = screen facing straight up).
  static constexpr float DOWN_Z = 0.35f;     // screen turned away: arm hanging, typing, screen down
  static constexpr float UP_Z = 0.65f;       // screen tilted up within about 50 degrees of level
  static constexpr float STEADY_G = 0.3f;    // total acceleration within this of 1 g: not being shaken
  static constexpr uint32_t HOLD_MS = 150;   // held up this long
  static constexpr uint32_t WINDOW_MS = 1500; // the turn up took no longer than this

 private:
  int8_t sign_ = 1;
  float learned_ = 0.5f;    // running average of z while being looked at
  bool sawLow_ = false;
  uint32_t lowAt_ = 0, upSince_ = 0;
  bool up_ = false;
};
