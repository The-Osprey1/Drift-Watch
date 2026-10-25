// Face numbers and active-face bitmask, kept free of display headers so network-only computer
// tests can include settings.h without TFT_eSPI.
#pragma once
#include <stdint.h>

// Swipe order. The numbers are stored in the settings, so new faces only ever go at the end.
// v0.12.0 adds three pairs of themed faces, a digital and an analog one each: Space (Orbit,
// Lunar), Sakura (Yozakura, Hanami) and Fallout (Wasteland, Mojave).
enum Face : uint8_t {
  FACE_DIGITAL, FACE_STEALTH, FACE_CORPO, FACE_HOLO, FACE_RADAR, FACE_PHANTOM,
  FACE_ORBIT, FACE_LUNAR, FACE_YOZAKURA, FACE_HANAMI, FACE_WASTELAND, FACE_MOJAVE,
  FACE_COUNT
};

static_assert(FACE_COUNT <= 16, "activeFaces is a 16-bit mask");
constexpr uint16_t ALL_FACES_MASK = (uint16_t)((1u << FACE_COUNT) - 1u);
