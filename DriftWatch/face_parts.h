// Pieces shared by faces.cpp and faces_themed.cpp. Not part of the face interface (faces.h).
#pragma once
#include <TFT_eSPI.h>
#include "faces.h"

namespace faceparts {
int displayHour(const FaceContext &c);
// Shown while the clock is not set: what is happening, or what to do about it.
const char *noTimeHint(const FaceContext &c);
// "FRI 23 OCT"
void dateText(const FaceContext &c, char *buf, size_t n);
// Sword-shaped hand: narrow at the pivot, widest at 72% of its length, pointed tip, with an inlay.
// `edge` (0 = none) outlines it; `halo` cuts a thin outline in `face` colour around it, so the
// minute hand stays distinct where it crosses the hour hand.
void swordHand(TFT_eSprite &s, float deg, float len, float tail, float w, uint16_t fill, uint16_t inlay,
               uint16_t edge, bool halo, uint16_t face);

// Themed faces (v0.12.0), in faces_themed.cpp.
void themedStatic(TFT_eSprite &s, Face f, const FaceContext &c);
void themedLive(TFT_eSprite &s, Face f, const FaceContext &c);
}  // namespace faceparts
