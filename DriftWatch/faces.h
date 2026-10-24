// Watch faces. Each face is split into a static layer, which changes at most
// once a minute and is cached, and a live layer drawn on top every second or
// animation frame.
#pragma once
#include <TFT_eSPI.h>
#include <time.h>

// Swipe order.
enum Face : uint8_t { FACE_DIGITAL, FACE_STEALTH, FACE_CORPO, FACE_HOLO, FACE_RADAR, FACE_COUNT };

enum class SyncView : uint8_t { Synced, Syncing, Failed, Never };

struct FaceContext {
  tm now;
  bool timeValid;
  bool use12h;
  int batteryPct;     // -1 = unavailable
  SyncView sync;
  uint8_t syncStage;  // 1..3 while syncing (joining, time zone, clock), else 0
  bool noWifi;        // no network saved: the fix is Wi-Fi setup, not a sync
  float beamDeg;      // radar beam angle, degrees clockwise from 12, or < 0 for no beam
};

// Draws the parts that only change once a minute (or when the date, battery
// or settings change).
void drawFaceStatic(TFT_eSprite &s, Face f, const FaceContext &c);
// Draws the per-second and animated parts over the static layer.
void drawFaceLive(TFT_eSprite &s, Face f, const FaceContext &c);
// Changes whenever the static layer needs redrawing.
uint32_t faceStaticKey(Face f, const FaceContext &c);
const char *faceName(Face f);
