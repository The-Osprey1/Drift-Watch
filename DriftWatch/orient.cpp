#include "orient.h"
#include <math.h>

namespace orient {

void apply(uint8_t map, int x, int y, int &ox, int &oy) {
  switch (map % MAPS) {
    case 1: ox = N - y; oy = x; break;
    case 2: ox = N - x; oy = N - y; break;
    case 3: ox = y; oy = N - x; break;
    case 4: ox = N - x; oy = y; break;
    case 5: ox = y; oy = x; break;
    case 6: ox = x; oy = N - y; break;
    case 7: ox = N - y; oy = N - x; break;
    default: ox = x; oy = y; break;
  }
}

uint8_t inverse(uint8_t map) {
  // Quarter turns undo each other; half turns and mirrors undo themselves.
  map %= MAPS;
  return map == 1 ? 3 : map == 3 ? 1 : map;
}

const char *turnName(uint8_t turn) {
  static const char *names[] = {"Normal", "Right", "Half turn", "Left"};  // short: they sit in a settings control
  return names[turn % TURNS];
}

uint8_t expectedTouchMap(uint8_t turn) {
  // A logical point drawn with turn t lands on the panel at (map 1) applied t times. A touch
  // comes back in the panel's frame, so it is turned back the other way.
  static const uint8_t back[] = {0, 3, 2, 1};
  return back[turn % TURNS];
}

Solve solveTouchMap(const int rawX[2], const int rawY[2]) {
  Solve best{false, 0, {9999, 9999}};
  float bestScore = 1e9f;
  for (uint8_t m = 0; m < MAPS; m++) {
    int err[2];
    for (int i = 0; i < 2; i++) {
      int x, y;
      apply(m, rawX[i], rawY[i], x, y);
      err[i] = (int)lroundf(hypotf((float)(x - TARGET_X[i]), (float)(y - TARGET_Y[i])));
    }
    float score = (float)(err[0] + err[1]);
    if (score < bestScore) {
      bestScore = score;
      best = {false, m, {err[0], err[1]}};
    }
  }
  best.ok = best.error[0] <= MATCH_PX && best.error[1] <= MATCH_PX;
  return best;
}

}  // namespace orient
