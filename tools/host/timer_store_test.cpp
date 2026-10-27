// The real one-key timer persistence implementation, with injected NVS failures.
#include <stdio.h>
#include <Preferences.h>
#include "timer_store.h"

namespace sim {
std::map<std::string, FlashValue> flash;
int settingsSaves = 0;
}

namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *what) {
  checks++;
  if (!ok) { failures++; printf("FAIL: %s\n", what); }
}
}

int main() {
  using timerstore::Marker;
  Marker value;
  check(timerstore::load(value) && value == Marker::Idle, "missing marker starts idle");
  check(sim::settingsSaves == 0, "read does not write a marker");
  check(timerstore::save(Marker::Running), "running marker saves");
  check(timerstore::load(value) && value == Marker::Running, "running survives reconstruction");
  int writes = sim::settingsSaves;
  check(timerstore::save(Marker::Running) && sim::settingsSaves == writes, "unchanged state is not rewritten");
  sim::preferenceWriteFailureKey() = "tools/timer";
  check(!timerstore::save(Marker::Due), "failed due write is reported");
  check(timerstore::load(value) && value == Marker::Running, "failed write preserves prior atomic marker");
  sim::preferenceWriteFailureKey().clear();
  check(timerstore::save(Marker::Due), "due marker saves");
  check(timerstore::load(value) && value == Marker::Due, "missed occurrence survives reconstruction");
  check(timerstore::save(Marker::Idle), "acknowledgement clears marker");
  check(timerstore::load(value) && value == Marker::Idle, "cleared occurrence stays cleared");
  sim::flash["tools/timer"] = {false, "", 255};
  check(!timerstore::load(value), "unknown future/corrupt marker is not treated as idle");
  sim::flash["tools/timer"] = {true, "wrong type", 0};
  check(!timerstore::load(value), "wrong marker type is rejected");
  sim::preferenceBeginFailureNamespace() = "tools";
  check(!timerstore::load(value) && !timerstore::save(Marker::Idle), "namespace failures are visible");
  sim::preferenceBeginFailureNamespace().clear();
  check(timerstore::save(Marker::Idle), "explicit clear can repair invalid marker");
  printf("Timer persistence: %d of %d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
