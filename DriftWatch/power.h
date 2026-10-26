// How the watch waits while the screen is off. The screen-off loop asks the controller for one
// wait at a time; it sleeps until the next thing that needs doing, and says when the screen
// should come back on.
//
//   StandbyIdle        no wrist gesture is on (setting, night mode or no sensor): light sleep
//                      until a touch, BOOT or the next deadline
//   StandbyPolling     the v0.10.1 way: the motion sensor is read every 80 ms between short
//                      light sleeps. Used for gesture modes that need it and as the fallback
//   StandbyWom         the motion sensor watches by itself (wake on motion) and the processor
//                      sleeps until the sensor's INT2 line, a touch, BOOT or a deadline
//   MotionQualifying   INT2 fired: sampling is back on and fresh samples (up to 1.5 s, one every
//                      80 ms) decide. Only a recognised raise turns the screen on; anything else
//                      clears the event and arms the sensor again, with the screen still dark.
//
// A motion interrupt never lights the screen by itself, and the motion sensor says nothing
// about where the wrist was before it moved, so samples from before a long sleep are never
// used as a turn rate. Wrist twist needs a still moment measured just before the twist, which
// wake on motion cannot provide, so any mode with twist on stays on polling (Fallback
// TwistNeedsHistory) rather than missing twists.
//
// Any failure (sensor setup, the command handshake, clearing the event, a stuck or chattering
// line, light sleep refused) falls back to polling and is kept as a reason for the log.
#pragma once
#include <stdint.h>
#include "board.h"
#include "imu.h"

#ifndef DRIFT_WOM
#define DRIFT_WOM DRIFT_DIAG    // the diagnostic build is the candidate; the normal one polls
#endif
#ifndef DRIFT_TOUCH_STANDBY
#define DRIFT_TOUCH_STANDBY DRIFT_DIAG
#endif

namespace power {
enum class State : uint8_t { Awake, StandbyIdle, StandbyPolling, StandbyWom, MotionQualifying };
enum class Fallback : uint8_t {
  None,
  NotBuilt,           // DRIFT_WOM is 0 in this build
  NoLightSleep,       // DRIFT_LIGHT_SLEEP is 0
  TwistNeedsHistory,  // a twist mode is on (see above)
  ArmFailed,          // the sensor refused the setting or the command handshake timed out
  ClearFailed,        // the motion event could not be read and cleared
  StuckLine,          // INT2 stayed high after clearing
  Storm,              // INT2 woke the watch again and again with no motion behind it
  SleepFailed,        // light sleep could not be set up
  Quiet,              // many rejected motions in a row (walking): polling for a while
};
const char *stateName(State s);
const char *fallbackName(Fallback f);

// What the screen-off loop must do after a wait.
enum class Wake : uint8_t { None, Touch, Button, Gesture, Serial };

struct Options {
  bool wom = DRIFT_WOM;                    // use wake on motion where the gesture mode allows
  bool touchStandby = DRIFT_TOUCH_STANDBY;  // let the touch chip doze while the screen is off
  bool lightSleep = DRIFT_LIGHT_SLEEP;
  uint32_t maxSleepMs = 30000;   // longest single sleep, even with nothing due
  uint32_t qualifyMs = 1500;     // fresh samples taken after a motion interrupt
  uint32_t sampleMs = 80;        // between samples while qualifying or polling
  uint32_t firstSampleMs = 50;   // first sample after restoring sampling: one 21 Hz period
  uint32_t pollDozeMs = 100;     // polling: light sleep between reads, as v0.10.1
  uint8_t womThresholdMg = 80;
  uint8_t womBlankingSamples = 4;
  uint8_t quietRejects = 4;      // this many rejected motions
  uint32_t quietWindowMs = 30000;  // within this long
  uint32_t quietMs = 30000;      // switch to polling for this long
  uint8_t stormWakes = 5;        // this many wakes with no motion behind them
  uint32_t stormWindowMs = 2000;   // within this long count as a storm
  uint8_t maxFaults = 3;         // after this many failed standby periods, stay on polling
};
extern Options options;

struct Inputs {
  bool raise = false, twist = false;  // the gestures allowed now (setting, night mode, sensor)
  bool serialHold = false;            // the computer is talking over serial: no light sleep, which drops bytes
};

struct Stats {
  uint32_t standbys = 0;        // times the screen went off
  uint32_t wakes = 0;           // waits that ended early (not by their timer)
  uint32_t motionWakes = 0;
  uint32_t spurious = 0;        // INT2 woke the watch but the sensor had no motion event
  uint32_t qualifications = 0;
  uint32_t accepted = 0, rejected = 0;
  uint32_t fallbacks = 0;
  uint64_t sleptMs = 0;         // inside light sleep or a plain screen-off wait
  uint64_t standbyMs = 0;       // screen off in total
  uint64_t screenOnMs = 0;
};

// Feeds one fresh sample to the wrist gesture detectors; true when one recognises a gesture.
using GestureFn = bool (*)(uint32_t ms, const Accel &a);

class Controller {
 public:
  void screenOff(uint32_t ms);
  // The screen came on (touch, BOOT or a gesture): wake on motion is turned off again and
  // normal sampling restored, so wrist-down sleep can read the sensor.
  void screenOn(uint32_t ms);
  // One wait with the screen off and both radios off. `untilMs` is the next deadline the
  // interface has (a resync, a night mode boundary, a settings write); the wait never goes
  // past it or options.maxSleepMs.
  Wake step(const Inputs &in, uint32_t untilMs, GestureFn feed);
  // Screen off with Wi-Fi or Bluetooth on: no light sleep, and the sensor is polled.
  Wake stepRadio(const Inputs &in, GestureFn feed);

  State state() const { return state_; }
  Fallback fallback() const { return fallback_; }
  const Stats &stats() const { return stats_; }
  void resetStats() { stats_ = Stats{}; }

 private:
  State want(const Inputs &in, uint32_t now, Fallback &why) const;
  void enter(State s, uint32_t now);
  void fail(Fallback f, uint32_t now);
  void rejected(uint32_t now);
  bool sample(uint32_t now, GestureFn feed);
  void motionLine(uint32_t now);

  State state_ = State::Awake;
  Fallback fallback_ = Fallback::None;
  Stats stats_;
  bool faulted_ = false;          // a hardware fallback this standby period
  bool sleepBroken_ = false;      // light sleep failed: plain waits from now on
  uint8_t faults_ = 0;            // standby periods that ended in a hardware fallback
  uint32_t offAt_ = 0, onAt_ = 0, qualifyStart_ = 0, lastSample_ = 0, quietUntil_ = 0;
  bool quiet_ = false;
  uint32_t rejects_[8] = {};      // times of recent rejected motions
  uint8_t rejectCount_ = 0;
  uint32_t spurious_[8] = {};
  uint8_t spuriousCount_ = 0;
  uint32_t lastLog_ = 0;
  bool logged_ = false;
};
}  // namespace power
