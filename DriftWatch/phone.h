// Phone sync over Bluetooth: what a phone may send and what the watch does with it. The
// Bluetooth side (ble.h) only turns writes into text lines; everything here is plain logic,
// so the computer simulation runs it.
//
// Lines the Drift sync page (companion/index.html) writes to the command characteristic,
// one per write, UTF-8:
//   T=<seconds since 1970, UTC>[.<ms>]   set the clock
//   Z=<POSIX time zone>                  e.g. GMT0BST,M3.5.0/1,M10.5.0 (keeps summer time rules)
//   N=<zone name>                        e.g. Europe/London, shown in Details
//   S=<network name>  P=<password>  J    save a Wi-Fi network, then join it and sync
//   ?                                    nothing; the status characteristic says the rest
// The standard Current Time Service (0x1805) is also offered, so generic Bluetooth apps can
// set the clock; its writes arrive here as
//   L=YYYY-MM-DD HH:MM:SS                local time (Current Time, 0x2A2B)
//   O=<seconds east of UTC>              zone plus summer time offset (Local Time Information, 0x2A0F)
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

namespace phone {
bool fromCurrentTime(const uint8_t *d, size_t n, char *out, size_t outSize);
bool fromLocalTimeInfo(const uint8_t *d, size_t n, char *out, size_t outSize);

struct Session {
  bool gotTime, gotZone, wifiSaved;
  time_t timeSetTo;       // the UTC time the phone sent, for the screen
  char ssid[33];          // network being saved
  bool haveOffset;
  long offset;            // from O=, seconds east of UTC
  const char *error;      // the last line that was refused, in words; nullptr if none
  uint16_t lines;         // lines applied this session
};

void begin();                    // a new phone sync window opened
const char *apply(const char *line);  // returns "ok" or why not
const Session &session();
// Status text for the phone: firmware, clock, zone, Wi-Fi and battery.
void status(char *out, size_t n, int batteryPct);

// Days since 1970-01-01 for a calendar date, without the time zone getting involved.
long daysFromCivil(int y, int m, int d);
}  // namespace phone
