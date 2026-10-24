// Bluetooth for phone sync. Bluetooth is only on while the Phone sync screen is open (up to
// WINDOW_MS, longer while a phone keeps writing), so nobody can connect at other times and
// light sleep is not disturbed. Writes from the phone arrive as text lines (see phone.h);
// ble_esp32.cpp runs the real radio, and the computer simulation fakes it.
//
// Services offered while open:
//   Drift sync      8f1d0001-6b5c-4c8e-9d3a-2a6b7c8d9e01
//     command       8f1d0002-6b5c-4c8e-9d3a-2a6b7c8d9e01  write: one line per write
//     status        8f1d0003-6b5c-4c8e-9d3a-2a6b7c8d9e01  read and notify: status and replies
//   Current Time    0x1805, with Current Time 0x2A2B and Local Time Information 0x2A0F (write)
// The link is not paired or encrypted: keep the window short and close it when done.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ble {
constexpr uint32_t WINDOW_MS = 180000;  // 3 minutes from opening or from the last line written

enum class State : uint8_t { Off, Waiting, Connected, Failed };

void open();              // starts Bluetooth and advertises as name()
void close();             // stops Bluetooth and frees its memory
void poll();              // closes the window when its time is up
State state();
bool on();                // the radio is running (no light sleep while it is)
uint32_t secondsLeft();
const char *name();       // e.g. "Drift-1A2B", from the chip's Bluetooth address
bool nextLine(char *out, size_t n);   // takes the next line a phone wrote; false if none
void setStatus(const char *text);     // what the phone reads from status (notified if connected)
}  // namespace ble
