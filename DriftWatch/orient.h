// Screen turn and touch alignment. The panel can be turned in quarter turns (TFT_eSPI
// setRotation), and the touch chip always reports positions in the panel's own frame, so
// touches have to be turned back the same way. Which way the panel actually turns for a
// given setRotation value is not certain from the datasheets, so the touch map is
// confirmed on the watch with a two-tap touch check (see solveTouchMap). Pure logic with
// no hardware, so the computer simulation tests it.
#pragma once
#include <stdint.h>

namespace orient {
constexpr int N = 239;  // last pixel on each axis of the 240 x 240 panel

// The eight ways a square can be turned or mirrored, as maps from one frame to another.
//   0 (x, y)          1 (N-y, x)        2 (N-x, N-y)      3 (y, N-x)
//   4 (N-x, y)        5 (y, x)          6 (x, N-y)        7 (N-y, N-x)
// 1 turns the picture a quarter turn clockwise, 3 a quarter turn anticlockwise.
constexpr uint8_t MAPS = 8;
void apply(uint8_t map, int x, int y, int &outX, int &outY);
uint8_t inverse(uint8_t map);

// Screen turn settings, in setRotation order.
constexpr uint8_t TURNS = 4;
const char *turnName(uint8_t turn);  // the fonts have no degree sign, so "Right", "Half turn" and so on (a quarter turn clockwise is "Right")

// The touch map expected for a screen turn, before any touch check: assumes setRotation(1)
// turns the picture a quarter turn clockwise (the usual TFT_eSPI meaning) and that the touch
// chip lines up with the panel at turn 0, as it did on the first watch test.
uint8_t expectedTouchMap(uint8_t turn);

// The touch check shows two dots in turn, then works out which of the eight maps takes the
// raw touch positions onto them.
constexpr int TARGET_X[2] = {120, 192}, TARGET_Y[2] = {48, 120};
constexpr int MATCH_PX = 56;  // each tap must land this close once mapped
struct Solve {
  bool ok;
  uint8_t map;
  int error[2];  // pixels from each dot after mapping
};
Solve solveTouchMap(const int rawX[2], const int rawY[2]);
}  // namespace orient
