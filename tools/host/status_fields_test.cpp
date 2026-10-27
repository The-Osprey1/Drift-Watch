#include <stdio.h>
#include <string.h>
#include "status_fields.h"

int main() {
  int checks = 0, failed = 0;
  auto check = [&](bool value, const char *description) {
    checks++;
    if (!value) { failed++; printf("FAIL: %s\n", description); }
  };
  statusfields::Snapshot v;
  v.batteryMv = 4200; v.boardTempC = 28.5f; v.driftPpb = -142;
  v.quality = "synced"; v.source = "usb"; v.syncAge = 42;
  v.timer = "running"; v.remainingSeconds = 60;
  char out[513] = "DRIFT:VER=test";
  check(statusfields::append(out, sizeof out, v, true), "serial fields fit");
  check(strstr(out, ",PROTO=1,CAPS=0x1F,MV=4200,TEMP=28.5,DRIFT=-142") != nullptr, "serial measured fields and caps");
  check(strstr(out, ",TIMEQ=synced,SOURCE=usb,SYNCAGE=42,TIMER=running,REMAIN=60") != nullptr, "serial quality and timer");
  strcpy(out, "fw=test");
  check(statusfields::append(out, sizeof out, v, false), "BLE fields fit");
  check(strstr(out, ";caps=0x1D;") != nullptr, "BLE does not advertise timer control");
  v.boardTempC = NAN; v.batteryMv = -1; v.syncAge = -1;
  strcpy(out, "fw=test");
  check(statusfields::append(out, sizeof out, v, false) && strstr(out, ";mv=-1;temp=na;") &&
        strstr(out, ";syncage=-1;"), "unavailable measurements are explicit");
  char small[16] = "fw=test";
  check(!statusfields::append(small, sizeof small, v, false) && !strcmp(small, "fw=test"),
        "insufficient space never emits partial telemetry");
  strcpy(out, "fw=test");
  check(statusfields::appendText(out, sizeof out, "wifi", "Home;reply=ok,100%\r\n"), "escaped text fits");
  check(strstr(out, ";wifi=Home%3Breply=ok%2C100%25%0D%0A") != nullptr &&
        strstr(out, ";reply=ok") == nullptr, "SSID cannot inject status or ACK fields");
  strcpy(small, "fw=test");
  check(!statusfields::appendText(small, sizeof small, "wifi", "long network") && !strcmp(small, "fw=test"),
        "long optional field omitted whole");
  printf("Status fields: %d of %d checks passed\n", checks - failed, checks);
  return failed ? 1 : 0;
}
