// Deterministic native firmware frames for reviewing the face revamp.
#include <TFT_eSPI.h>
#include <cstdio>
#include <cstdlib>
#include "faces.h"
uint32_t millis() { return 0; }
void delay(uint32_t) {}
void save(const char *dir, const char *name, int i, const TFT_eSPI &s) {
  char p[512]; snprintf(p, sizeof p, "%s/preview_%s_%03d.ppm", dir, name, i);
  FILE *f = fopen(p, "wb"); if (!f) { perror(p); exit(1); }
  fprintf(f, "P6\n240 240\n255\n");
  for (uint16_t c : s.buf) {
    unsigned char p[3] = {(unsigned char)((c>>11)*255/31),
      (unsigned char)(((c>>5)&63)*255/63),(unsigned char)((c&31)*255/31)};
    fwrite(p, 1, 3, f);
  }
  fclose(f);
}
int main(int argc, char **argv) {
  if (argc < 2) return 1;
  TFT_eSPI screen; TFT_eSprite s(&screen); s.createSprite(240,240);
  FaceContext c{}; c.now.tm_hour=10; c.now.tm_min=9; c.now.tm_sec=31;
  c.now.tm_wday=6; c.now.tm_mday=24; c.now.tm_mon=9; c.now.tm_yday=296;
  c.timeValid=true; c.batteryPct=86; c.sync=SyncView::Synced; c.beamDeg=-1;
  for (int f=0; f<FACE_COUNT; ++f) {
    drawFaceStatic(s,(Face)f,c); drawFaceLive(s,(Face)f,c);
    save(argv[1],faceName((Face)f),0,s);
  }
  // Eight seconds at 20 fps: real drawing code, no interpolation of rendered frames.
  for (int f : {FACE_RADAR, FACE_PHANTOM, FACE_ORBIT, FACE_LUNAR,
                 FACE_YOZAKURA, FACE_HANAMI, FACE_WASTELAND, FACE_MOJAVE}) {
    for (int i=0;i<160;++i) {
      uint32_t ms=i*50; c.now.tm_sec=(54+ms/1000)%60; c.ms=ms%1000;
      c.animMs=ms; c.animate=true; c.entranceMs=ms;
      c.entering=ms<faceEntranceMs((Face)f);
      c.beamDeg=f==FACE_RADAR ? (ms%3600)*0.1f : -1;
      drawFaceStatic(s,(Face)f,c); drawFaceLive(s,(Face)f,c);
      save(argv[1],faceName((Face)f),i+1,s);
    }
  }
}
