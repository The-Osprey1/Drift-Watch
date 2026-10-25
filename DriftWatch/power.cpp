#include "power.h"
#include "hal.h"

namespace power {
Options options;

namespace {
constexpr uint32_t PLAIN_WAIT_MS = 50;      // without light sleep, as v0.10.1
constexpr uint32_t RADIO_WAIT_MS = 50;      // Wi-Fi or Bluetooth on: the processor stays awake
constexpr uint32_t LOG_EVERY_MS = 10000;    // fallback messages at most this often

bool before(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }

// Drops entries older than `window` and adds `now`; returns how many are left.
uint8_t remember(uint32_t *times, uint8_t &count, uint32_t now, uint32_t window) {
  uint8_t kept = 0;
  for (uint8_t i = 0; i < count; i++)
    if (now - times[i] < window) times[kept++] = times[i];
  if (kept < 8) times[kept++] = now;
  count = kept;
  return kept;
}
}  // namespace

const char *stateName(State s) {
  static const char *names[] = {"awake", "idle", "polling", "wake on motion", "qualifying"};
  return names[(int)s];
}

const char *fallbackName(Fallback f) {
  static const char *names[] = {"none",          "not in this build", "no light sleep", "twist needs history",
                                "arm failed",    "clear failed",      "stuck line",     "interrupt storm",
                                "sleep failed",  "quiet after rejects"};
  return names[(int)f];
}

void Controller::screenOff(uint32_t ms) {
  if (state_ != State::Awake) return;
  if (onAt_) stats_.screenOnMs += ms - onAt_;
  stats_.standbys++;
  offAt_ = lastSample_ = ms;
  faulted_ = false;
  spuriousCount_ = 0;
  state_ = State::StandbyIdle;  // step() picks the real one on its first call
}

void Controller::screenOn(uint32_t ms) {
  if (state_ == State::Awake) return;
  if (state_ == State::StandbyWom && imu::exitWakeOnMotion() != imu::Status::Ok)
    hal::log("power: could not leave wake on motion");
  stats_.standbyMs += ms - offAt_;
  onAt_ = ms;
  if (faulted_ && faults_ < 255) faults_++;
  state_ = State::Awake;
#if DRIFT_DIAG
  if (!lastLog_ || ms - lastLog_ >= 60000) {
    lastLog_ = ms;
    hal::log("power: %lu standbys, %lu early wakes (%lu motion, %lu spurious), %lu qualified, %lu accepted, "
             "%lu rejected, slept %lu of %lu s, fallback %s",
             (unsigned long)stats_.standbys, (unsigned long)stats_.wakes, (unsigned long)stats_.motionWakes,
             (unsigned long)stats_.spurious, (unsigned long)stats_.qualifications, (unsigned long)stats_.accepted,
             (unsigned long)stats_.rejected, (unsigned long)(stats_.sleptMs / 1000), (unsigned long)(stats_.standbyMs / 1000),
             fallbackName(fallback_));
  }
#endif
}

// The standby state the inputs ask for, and why it is not wake on motion when it is not.
State Controller::want(const Inputs &in, uint32_t now, Fallback &why) const {
  why = Fallback::None;
  if (!in.raise && !in.twist) return State::StandbyIdle;
  if (!options.wom) why = Fallback::NotBuilt;
  else if (!options.lightSleep || sleepBroken_) why = sleepBroken_ ? Fallback::SleepFailed : Fallback::NoLightSleep;
  else if (in.twist) why = Fallback::TwistNeedsHistory;
  else if (faulted_ || faults_ >= options.maxFaults) why = fallback_;
  else if (quiet_ && before(now, quietUntil_)) why = Fallback::Quiet;
  return why == Fallback::None ? State::StandbyWom : State::StandbyPolling;
}

void Controller::fail(Fallback f, uint32_t now) {
  if (state_ == State::StandbyWom) imu::exitWakeOnMotion();  // best effort, back to sampling
  fallback_ = f;
  faulted_ = true;
  stats_.fallbacks++;
  state_ = State::StandbyPolling;
  lastSample_ = now;
  if (!logged_ || now - lastLog_ >= LOG_EVERY_MS) {
    hal::log("power: falling back to polling (%s)", fallbackName(f));
    lastLog_ = now;
    logged_ = true;
  }
}

void Controller::enter(State s, uint32_t now) {
  if (s == state_) return;
  if (state_ == State::StandbyWom && imu::exitWakeOnMotion() != imu::Status::Ok) {
    fail(Fallback::ArmFailed, now);
    return;
  }
  state_ = s;
  if (s == State::StandbyWom) {
    imu::WomConfig cfg;
    cfg.thresholdMg = options.womThresholdMg;
    cfg.blankingSamples = options.womBlankingSamples;
    imu::Status r = imu::enterWakeOnMotion(cfg);
    if (r != imu::Status::Ok) {
      state_ = State::StandbyPolling;  // the driver already tried to restore sampling
      hal::log("power: wake on motion not armed (%s)", imu::statusName(r));
      fail(Fallback::ArmFailed, now);
      return;
    }
    fallback_ = Fallback::None;
  }
  lastSample_ = now;
}

void Controller::rejected(uint32_t now) {
  stats_.rejected++;
  if (remember(rejects_, rejectCount_, now, options.quietWindowMs) >= options.quietRejects) {
    // Walking or riding: rather than qualifying every few seconds, poll for a while.
    quiet_ = true;
    quietUntil_ = now + options.quietMs;
    rejectCount_ = 0;
    fallback_ = Fallback::Quiet;
  }
}

// Reads one sample for the gesture detectors. True if they recognised a gesture.
bool Controller::sample(uint32_t now, GestureFn feed) {
  lastSample_ = now;
  Accel a;
  if (!imu::read(a)) return false;
  return feed && feed(now, a);
}

Wake Controller::stepRadio(const Inputs &in, GestureFn feed) {
  // Wi-Fi and Bluetooth need the processor awake, so no light sleep and no wake on motion.
  uint32_t now = hal::ms();
  if (state_ == State::StandbyWom || state_ == State::MotionQualifying) enter(State::StandbyPolling, now);
  if ((in.raise || in.twist) && now - lastSample_ >= options.sampleMs && sample(now, feed)) {
    stats_.accepted++;
    return Wake::Gesture;
  }
  hal::idle(RADIO_WAIT_MS);
  return Wake::None;
}

Wake Controller::step(const Inputs &in, uint32_t untilMs, GestureFn feed) {
  uint32_t now = hal::ms();
  if (quiet_ && !before(now, quietUntil_)) quiet_ = false;
  Fallback why;
  State target = want(in, now, why);
  if (state_ == State::MotionQualifying && target != State::StandbyWom) state_ = State::StandbyPolling;
  if (state_ != State::MotionQualifying) {
    if (target != State::StandbyWom && !faulted_) fallback_ = why;
    enter(target, now);
  }

  // How long to wait: up to the next deadline, never past maxSleepMs.
  uint32_t left = before(now, untilMs) ? untilMs - now : 1;
  uint32_t longest = left < options.maxSleepMs ? left : options.maxSleepMs;
  hal::DozeArm arm;
  arm.lowPower = options.lightSleep && !sleepBroken_;
  uint32_t wait = longest;
  switch (state_) {
    case State::StandbyIdle: break;
    case State::StandbyWom:
      // Light sleep cannot arm a line that is already high, and it would never wake for it.
      if (imu::motionLineHigh()) {
        stats_.motionWakes++;
        motionLine(now);
        return Wake::None;
      }
      arm.motion = true;
      break;
    case State::StandbyPolling: {
      if (now - lastSample_ >= options.sampleMs && sample(now, feed)) {
        stats_.accepted++;
        return Wake::Gesture;
      }
      wait = options.pollDozeMs;
      break;
    }
    case State::MotionQualifying: {
      uint32_t due = lastSample_ + (lastSample_ == qualifyStart_ ? options.firstSampleMs : options.sampleMs);
      wait = before(now, due) ? due - now : 0;
      if (!wait) {
        if (sample(now, feed)) {
          stats_.accepted++;
          return Wake::Gesture;
        }
        if (now - qualifyStart_ >= options.qualifyMs) {
          rejected(now);
          enter(want(in, now, why), now);  // arm again, or poll for a while after many rejects
          return Wake::None;
        }
        wait = options.sampleMs;
      }
      break;
    }
    case State::Awake: return Wake::None;
  }
  if (!arm.lowPower) wait = wait < PLAIN_WAIT_MS ? wait : PLAIN_WAIT_MS;  // a plain wait has to poll touch
  if (wait > longest) wait = longest;
  if (!wait) wait = 1;

  uint32_t t0 = hal::ms();
  uint8_t bits = hal::doze(wait, arm);
  uint32_t t1 = hal::ms();
  stats_.sleptMs += t1 - t0;
  if (bits & hal::WAKE_FAULT) {
    sleepBroken_ = true;
    fail(Fallback::SleepFailed, t1);
  }
  if (bits & (hal::WAKE_TOUCH | hal::WAKE_BUTTON | hal::WAKE_MOTION)) stats_.wakes++;
  // Touch and BOOT come first: they always wake the screen straight away.
  if (bits & hal::WAKE_TOUCH) return Wake::Touch;
  if (bits & hal::WAKE_BUTTON) return Wake::Button;
  if (!(bits & hal::WAKE_MOTION) || state_ != State::StandbyWom) return Wake::None;

  stats_.motionWakes++;
  motionLine(t1);
  return Wake::None;
}

// INT2 is high: find out why. Only a real motion event leads to sampling and a decision; the
// screen stays dark meanwhile.
void Controller::motionLine(uint32_t now) {
  bool motion = false;
  if (imu::readAndClearWakeEvent(motion) != imu::Status::Ok) {
    fail(Fallback::ClearFailed, now);
    return;
  }
  if (!motion) {
    // High with no motion event behind it.
    stats_.spurious++;
    if (imu::motionLineHigh()) fail(Fallback::StuckLine, now);
    else if (remember(spurious_, spuriousCount_, now, options.stormWindowMs) >= options.stormWakes)
      fail(Fallback::Storm, now);
    return;
  }
  if (imu::exitWakeOnMotion() != imu::Status::Ok) {
    state_ = State::StandbyPolling;
    fail(Fallback::ArmFailed, now);
    return;
  }
  stats_.qualifications++;
  state_ = State::MotionQualifying;
  qualifyStart_ = lastSample_ = now;
}
}  // namespace power
