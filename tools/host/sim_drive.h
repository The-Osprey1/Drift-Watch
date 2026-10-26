// Script helpers shared by the interface simulations (simulate.cpp with the fake network,
// simulate_network.cpp with the real one): the check counter, the clock, the finger, BOOT and
// finding controls on screen.
#pragma once
#include <stdio.h>
#include <string>
#include <TFT_eSPI.h>
#include "hal.h"
#include "screens.h"
#include "sim.h"
#include "ui.h"

namespace {
const char *outDir = ".";  // where frames and the report go
int failures = 0, checks = 0;
[[maybe_unused]] FILE *report = nullptr;  // simulate.cpp writes its report here

#define CHECK(cond, ...)                         \
  do {                                           \
    checks++;                                    \
    if (!(cond)) {                               \
      failures++;                                \
      printf("  FAIL line %d: ", __LINE__);      \
      printf(__VA_ARGS__);                       \
      printf("\n");                              \
    }                                            \
  } while (0)

inline void writePpm(const std::string &name, const TFT_eSPI &s) {
  std::string path = std::string(outDir) + "/" + name + ".ppm";
  FILE *f = fopen(path.c_str(), "wb");
  fprintf(f, "P6\n%d %d\n255\n", s.width(), s.height());
  for (uint16_t c : s.buf) {
    uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63),
                      (uint8_t)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

inline void snap(const char *name) { writePpm(std::string("sim_") + name, ui::display()); }
inline std::string screen() { return ui::screenName(); }

inline void runFor(uint32_t ms) {
  uint32_t end = sim::clockMs + ms;
  sim::runUntil = end;  // a screen-off sleep ends here, where the script changes something
  while ((int32_t)(sim::clockMs - end) < 0) ui::loop();
}

inline void touchDown(int x, int y) {
  sim::fingerX = x;
  sim::fingerY = y;
  sim::fingerDown = true;
  ui::loop();
}
inline void touchUp() {
  sim::fingerDown = false;
  ui::loop();
}

// Moves the finger in steps of `stepMs`, like the touch chip reporting every 10-15 ms.
inline void moveTo(int x, int y, uint32_t ms, uint32_t stepMs = 12) {
  int x0 = sim::fingerX, y0 = sim::fingerY;
  int n = max<int>(1, ms / stepMs);
  for (int i = 1; i <= n; i++) {
    sim::fingerX = x0 + (x - x0) * i / n;
    sim::fingerY = y0 + (y - y0) * i / n;
    runFor(stepMs);
  }
}

inline void swipe(int x0, int y0, int x1, int y1, uint32_t ms) {
  touchDown(x0, y0);
  moveTo(x1, y1, ms);
  touchUp();
}
inline void swipeLeft() { swipe(190, 120, 80, 120, 90); }
inline void swipeRight() { swipe(60, 120, 170, 120, 90); }
inline void swipeUp() { swipe(120, 70, 120, 170, 120); }    // slide down (opens Settings)
inline void swipeDown() { swipe(120, 180, 120, 80, 120); }  // swipe up (opens Quick Actions)

inline void tap(int x, int y) {
  touchDown(x, y);
  runFor(60);
  touchUp();
}

inline void pressBoot(uint32_t holdMs = 40) {
  sim::bootDown = true;
  runFor(holdMs);
  sim::bootDown = false;
  runFor(20);
}

inline void settle(uint32_t ms = 400) { runFor(ms); }

// Finds a control on the current screen and taps the centre of its hit region.
inline bool find(screens::Button b, screens::Control &out) {
  screens::Control ctl[2];
  int n = ui::controls(ctl, 2);
  for (int i = 0; i < n; i++)
    if (ctl[i].id == b) {
      out = ctl[i];
      return true;
    }
  return false;
}
inline bool tapButton(screens::Button b) {
  screens::Control c;
  if (!find(b, c)) return false;
  tap(c.hit.x + c.hit.w / 2, c.hit.y + c.hit.h / 2);
  return true;
}

// Swipes to the page of the current screen that holds `b`: back to the first page, then forward.
inline bool goTo(screens::Button b) {
  screens::Control c;
  for (int i = 0; i < 16 && ui::page() > 0; i++) {
    swipeRight();
    settle(400);
  }
  for (int i = 0; i < 16 && !find(b, c); i++) {
    int before = ui::page();
    swipeLeft();
    settle(400);
    if (ui::page() == before) break;
  }
  return find(b, c);
}

}  // namespace
