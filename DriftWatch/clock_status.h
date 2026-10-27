// Provenance and age of the current clock. Elapsed age never uses UTC, so a time
// correction cannot make an old sync look fresh. Observe at least once per 32-bit
// millis wrap (the watch polls at most 30 seconds apart).
#pragma once
#include <stdint.h>

namespace clockstatus {
enum class Source : uint8_t { None, Manual, Wifi, Phone, Usb, Retained };
enum class Quality : uint8_t { Unset, Manual, Synced, Stale, Retained };
constexpr uint64_t STALE_AFTER_MS = 24ULL * 3600 * 1000;

inline const char *name(Source source) {
  switch (source) {
    case Source::Manual: return "manual";
    case Source::Wifi: return "wifi";
    case Source::Phone: return "phone";
    case Source::Usb: return "usb";
    case Source::Retained: return "retained";
    default: return "none";
  }
}
inline const char *name(Quality quality) {
  switch (quality) {
    case Quality::Manual: return "manual";
    case Quality::Synced: return "synced";
    case Quality::Stale: return "stale";
    case Quality::Retained: return "retained";
    default: return "unset";
  }
}

class Ledger {
 public:
  void begin(uint32_t now, bool plausible) {
    source_ = plausible ? Source::Retained : Source::None;
    lastMs_ = now;
    ageMs_ = 0;
    trusted_ = false;
  }
  void observe(uint32_t now) {
    uint32_t elapsed = now - lastMs_;
    lastMs_ = now;
    if (trusted_) ageMs_ = UINT64_MAX - ageMs_ < elapsed ? UINT64_MAX : ageMs_ + elapsed;
  }
  void manual(uint32_t now) {
    source_ = Source::Manual;
    lastMs_ = now;
    ageMs_ = 0;
    trusted_ = false;
  }
  void synced(uint32_t now, Source source) {
    source_ = source;
    lastMs_ = now;
    ageMs_ = 0;
    trusted_ = true;
  }
  Quality quality(bool plausible) const {
    if (!plausible || source_ == Source::None) return Quality::Unset;
    if (source_ == Source::Manual) return Quality::Manual;
    if (!trusted_) return Quality::Retained;
    return ageMs_ >= STALE_AFTER_MS ? Quality::Stale : Quality::Synced;
  }
  Source source(bool plausible) const { return plausible ? source_ : Source::None; }
  int64_t ageSeconds(bool plausible) const {
    if (!plausible || !trusted_) return -1;
    // Even UINT64_MAX milliseconds fits signed seconds after division by 1000.
    return (int64_t)(ageMs_ / 1000);
  }
 private:
  Source source_ = Source::None;
  uint32_t lastMs_ = 0;
  uint64_t ageMs_ = 0;
  bool trusted_ = false;
};
}  // namespace clockstatus
