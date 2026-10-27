#include <stdio.h>
#include <initializer_list>
#include "clock_status.h"

namespace {
int checks = 0, failures = 0;
void check(bool value, const char *description) {
  checks++;
  if (!value) { failures++; printf("FAIL: %s\n", description); }
}
}
int main() {
  using namespace clockstatus;
  Ledger clock;
  clock.begin(0, false);
  check(clock.quality(false) == Quality::Unset && clock.ageSeconds(false) == -1, "power loss is unset");
  check(clock.quality(true) == Quality::Unset, "a plausible epoch alone cannot establish an unset ledger");
  clock.begin(123, true);
  check(clock.quality(true) == Quality::Retained && clock.source(true) == Source::Retained, "startup provenance is retained");
  check(clock.ageSeconds(true) == -1, "startup age is unknown");
  clock.manual(200);
  clock.observe(100000);
  check(clock.quality(true) == Quality::Manual && clock.ageSeconds(true) == -1, "manual time is not a trusted sync");
  for (Source source : {Source::Wifi, Source::Phone, Source::Usb}) {
    clock.synced(0, source);
    check(clock.quality(true) == Quality::Synced && clock.source(true) == source, "trusted source recorded");
    clock.observe((uint32_t)STALE_AFTER_MS - 1);
    check(clock.quality(true) == Quality::Synced, "before stale boundary");
    clock.observe((uint32_t)STALE_AFTER_MS);
    check(clock.quality(true) == Quality::Stale && clock.ageSeconds(true) == 86400, "exact stale boundary remains valid");
  }
  clock.synced(0xFFFFFF00u, Source::Usb);
  clock.observe(0x000006D0u);
  check(clock.ageSeconds(true) == 2, "millis rollover preserves age");
  clock.synced(0, Source::Phone);
  uint64_t expected = 0;
  uint32_t tick = 0;
  for (int i = 0; i < 8; i++) {
    tick += 0x60000000u; expected += 0x60000000u; clock.observe(tick);
  }
  check(expected > UINT32_MAX && clock.ageSeconds(true) == (int64_t)(expected / 1000), "multiple wraps accumulate in 64 bits");
  int64_t age = clock.ageSeconds(true);
  check(clock.ageSeconds(true) == age, "wall-clock changes have no input to age");
  check(clock.quality(false) == Quality::Unset && clock.source(false) == Source::None, "invalid wall clock hides provenance");
  clock.synced(tick, Source::Wifi);
  check(clock.ageSeconds(true) == 0 && clock.quality(true) == Quality::Synced, "new trusted sync clears stale age");
  clock.begin(0, true);
  check(clock.quality(true) == Quality::Retained && clock.ageSeconds(true) == -1, "restart never reconstructs age from UTC");
  check(name(Quality::Stale)[0] == 's' && name(Source::Usb)[0] == 'u', "wire names available");
  printf("Clock status: %d of %d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
