// Panel accent hierarchy: neutral text and outlines, cyan for progress and
// pressed controls, magenta only for the Wi-Fi setup action, orange for faults.
#include "screens.h"
#include "gfx.h"
#include "theme.h"

using namespace layout;

namespace screens {
namespace {

struct Rect {
  int x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// ---- Layout tables ----------------------------------------------------------
// Every corner sits inside the safe circle; render_faces checks the text.
constexpr Rect TILE_RECTS[] = {{28, 82, 90, 44}, {122, 82, 90, 44}, {28, 130, 90, 44}, {122, 130, 90, 44}};
constexpr Button TILE_BUTTONS[] = {Button::Light, Button::Clock, Button::Motion, Button::Sync};
constexpr Rect PANEL_PILL = {76, 178, 88, 44};
constexpr Rect SETUP_PILL = {78, 178, 84, 44};
// Touch areas grow a few pixels past the drawn edge so near-misses still count.
constexpr int HIT_MARGIN = 3;

bool hit(const Rect &r, int x, int y) {
  return Rect{r.x - HIT_MARGIN, r.y - HIT_MARGIN, r.w + 2 * HIT_MARGIN, r.h + 2 * HIT_MARGIN}.contains(x, y);
}

bool syncing(net::Sync s) { return s == net::Sync::Connecting || s == net::Sync::Zone || s == net::Sync::Clock; }

const char *syncStepText(net::Sync s) {
  switch (s) {
    case net::Sync::Connecting: return "Joining Wi-Fi";
    case net::Sync::Zone: return "Finding time zone";
    default: return "Setting clock";
  }
}

const char *failText(net::Fail f) {
  switch (f) {
    case net::Fail::NoNetworkSaved: return "No Wi-Fi saved";
    case net::Fail::JoinFailed: return "Can't join Wi-Fi";
    default: return "No time server";
  }
}

const char *failHint(net::Fail f) {
  switch (f) {
    case net::Fail::NoNetworkSaved: return "Run Wi-Fi setup first";
    case net::Fail::JoinFailed: return "Check name and password";
    default: return "Internet may be down";
  }
}

// Small rotating arc. With reduced motion it is a still three-quarter ring.
void spinner(TFT_eSprite &s, int cx, int cy, int r, uint32_t ms, bool still, uint16_t c) {
  s.drawArc(cx, cy, r, r - 2, 0, 360, col::N700, col::BG, true);
  uint32_t a = still ? 180 : (ms * 360 / 900) % 360;
  uint32_t b = (a + 100) % 360;
  if (b > a) s.drawArc(cx, cy, r, r - 2, a, b, c, col::BG, true);
  else {
    s.drawArc(cx, cy, r, r - 2, a, 360, c, col::BG, true);
    if (b > 0) s.drawArc(cx, cy, r, r - 2, 0, b, c, col::BG, true);
  }
}

// One line of status: an icon shape, then text, centred together.
enum class Icon : uint8_t { None, Check, Cross, Alert, Spinner };
void statusLine(TFT_eSprite &s, int cy, Icon icon, const char *str, uint16_t c, uint32_t ms, bool still) {
  s.setTextFont(type::SMALL);
  int tw = s.textWidth(str), iw = icon == Icon::None ? 0 : 14 + 6;
  int x = CX - (tw + iw) / 2;
  switch (icon) {
    case Icon::Check: gfx::iconCheck(s, x + 7, cy, 14, c); break;
    case Icon::Cross: gfx::iconCross(s, x + 7, cy, 12, c); break;
    case Icon::Alert: gfx::iconAlert(s, x + 7, cy, 13, c); break;
    case Icon::Spinner: spinner(s, x + 7, cy, 6, ms, still, c); break;
    default: break;
  }
  gfx::text(s, str, x + iw, cy, type::SMALL, c, ML_DATUM);
}

void pill(TFT_eSprite &s, const Rect &r, const char *label, uint16_t c, bool pressed) {
  if (pressed) gfx::cutBox(s, r.x, r.y, r.w, r.h, 8, c, true);
  gfx::cutBox(s, r.x, r.y, r.w, r.h, 8, c, false);
  gfx::cutBox(s, r.x + 1, r.y + 1, r.w - 2, r.h - 2, 7, c, false);
  gfx::text(s, label, r.x + r.w / 2, r.y + r.h / 2 + 1, type::SMALL, pressed ? col::BG : c);
}

void tile(TFT_eSprite &s, const Rect &r, const char *label, bool pressed) {
  if (pressed) gfx::cutBox(s, r.x, r.y, r.w, r.h, 6, col::CYAN_DEEP, true);
  gfx::cutBox(s, r.x, r.y, r.w, r.h, 6, pressed ? col::CYAN : col::N600, false);
  gfx::text(s, label, r.x + r.w / 2, r.y + 9, type::SMALL, pressed ? col::CYAN : col::N400);
}

void tileValue(TFT_eSprite &s, const Rect &r, const char *value, bool pressed) {
  gfx::text(s, value, r.x + r.w / 2, r.y + 33, type::MEDIUM, pressed ? col::WHITE : col::N100);
}

// Swipe-down handle at the top edge, the hint that the panel closes downwards.
void handle(TFT_eSprite &s) { s.fillRect(CX - 12, 13, 24, 3, col::N600); }

// Widest text that fits a band of the circle centred on row y with half-height h.
int chordWidth(int y, int h, int margin) {
  int dy = max(abs(y - h - CY), abs(y + h - CY));
  float half = sqrtf(max(0.0f, (float)(SAFE_R * SAFE_R - dy * dy)));
  return (int)(2 * half) - 2 * margin;
}
}  // namespace

// ---- Panel ------------------------------------------------------------------
void drawPanel(TFT_eSprite &s, const PanelView &v) {
  s.fillSprite(col::BG);
  handle(s);
  gfx::battery(s, CX, 32, v.batteryPct, col::N100);

  // Sync status line: shape plus words, so it never depends on colour.
  char buf[48];
  if (syncing(v.sync)) {
    snprintf(buf, sizeof buf, "%s  %d/3", syncStepText(v.sync), v.syncStep);
    statusLine(s, 52, Icon::Spinner, buf, col::CYAN, v.ms, v.reducedMotion);
  } else if (v.sync == net::Sync::Failed) {
    statusLine(s, 52, Icon::Cross, failText(v.fail), col::WARN, v.ms, true);
  } else if (v.lastSyncText[0]) {
    snprintf(buf, sizeof buf, "Synced %s", v.lastSyncText);
    statusLine(s, 52, Icon::Check, buf, col::N100, v.ms, true);
  } else {
    statusLine(s, 52, Icon::Alert, v.timeValid ? "Not synced yet" : "Clock not set", col::WARN, v.ms, true);
  }

  // Third line: failure hint, diagnostics, or zone and network.
  char line[64], fitted[64];
  if (v.sync == net::Sync::Failed) snprintf(line, sizeof line, "%s", failHint(v.fail));
  else if (v.diag) snprintf(line, sizeof line, "%s", v.diag);
  else if (v.zone[0]) snprintf(line, sizeof line, "%s", v.zone);
  else snprintf(line, sizeof line, "%s", v.ssid[0] ? v.ssid : "No network saved");
  gfx::fit(s, line, type::SMALL, chordWidth(69, 8, 10), fitted, sizeof fitted);
  gfx::text(s, fitted, CX, 69, type::SMALL, col::N400);

  for (int i = 0; i < 4; i++) {
    const Rect &r = TILE_RECTS[i];
    bool p = v.pressed == TILE_BUTTONS[i];
    switch (TILE_BUTTONS[i]) {
      case Button::Light: {
        tile(s, r, "LIGHT", p);
        // Four bars: the count shows the level, colour only reinforces it.
        for (int b = 0; b < 4; b++) {
          int bx = r.x + r.w / 2 - 25 + b * 13, bh = 6 + b * 3, by = r.y + 38 - bh;
          if (b <= v.brightnessLevel) s.fillRect(bx, by, 10, bh, p ? col::WHITE : col::N100);
          else s.drawRect(bx, by, 10, bh, col::N600);
        }
        break;
      }
      case Button::Clock:
        tile(s, r, "CLOCK", p);
        tileValue(s, r, v.use12h ? "12H" : "24H", p);
        break;
      case Button::Motion:
        tile(s, r, "MOTION", p);
        tileValue(s, r, v.reducedMotion ? "LESS" : "FULL", p);
        break;
      case Button::Sync:
        tile(s, r, "SYNC", p);
        if (syncing(v.sync)) spinner(s, r.x + r.w / 2, r.y + 29, 8, v.ms, v.reducedMotion, col::CYAN);
        else tileValue(s, r, v.sync == net::Sync::Failed ? "RETRY" : "NOW", p);
        break;
      default:
        break;
    }
  }
  pill(s, PANEL_PILL, "WI-FI SETUP", col::MAGENTA, v.pressed == Button::WifiSetup);
}

Button hitPanel(int x, int y) {
  for (int i = 0; i < 4; i++)
    if (hit(TILE_RECTS[i], x, y)) return TILE_BUTTONS[i];
  if (hit(PANEL_PILL, x, y)) return Button::WifiSetup;
  return Button::None;
}

// ---- Wi-Fi setup --------------------------------------------------------------
namespace {
enum class SetupPhase : uint8_t { Steps, Saving, Done, Failed, TimedOut };

SetupPhase phase(const SetupView &v) {
  if (v.portal == net::Portal::TimedOut) return SetupPhase::TimedOut;
  if (v.portal == net::Portal::Saved) {
    if (v.sync == net::Sync::Done) return SetupPhase::Done;
    if (v.sync == net::Sync::Failed) return SetupPhase::Failed;
    return SetupPhase::Saving;
  }
  return SetupPhase::Steps;
}

Button setupButton(SetupPhase p) {
  switch (p) {
    case SetupPhase::Steps: return Button::Cancel;
    case SetupPhase::Saving: return Button::Close;
    case SetupPhase::Done: return Button::Done;
    default: return Button::Retry;
  }
}

const char *buttonLabel(Button b) {
  switch (b) {
    case Button::Cancel: return "CANCEL";
    case Button::Close: return "CLOSE";
    case Button::Done: return "DONE";
    default: return "RETRY";
  }
}

void stepBadge(TFT_eSprite &s, int cx, int cy, const char *n, bool done, bool current) {
  if (done) {
    s.drawCircle(cx, cy, 9, col::N500);
    gfx::iconCheck(s, cx, cy, 11, col::N300);
  } else if (current) {
    s.fillSmoothCircle(cx, cy, 9, col::CYAN, col::BG);
    gfx::text(s, n, cx, cy + 1, type::SMALL, col::BG);
  } else {
    s.drawCircle(cx, cy, 9, col::N500);
    gfx::text(s, n, cx, cy + 1, type::SMALL, col::N300);
  }
}

// Big centred outcome: icon, headline, one line of detail.
void outcome(TFT_eSprite &s, Icon icon, uint16_t c, const char *title, const char *detail, uint32_t ms, bool still) {
  switch (icon) {
    case Icon::Check: gfx::iconCheck(s, CX, 82, 34, c); break;
    case Icon::Cross: gfx::iconCross(s, CX, 82, 26, c); break;
    case Icon::Alert: gfx::iconAlert(s, CX, 82, 30, c); break;
    case Icon::Spinner: spinner(s, CX, 82, 16, ms, still, c); break;
    default: break;
  }
  char fitted[48];
  gfx::fit(s, title, type::MEDIUM, chordWidth(124, 13, 8), fitted, sizeof fitted);
  gfx::text(s, fitted, CX, 124, type::MEDIUM, col::N100);
  gfx::fit(s, detail, type::SMALL, chordWidth(150, 8, 8), fitted, sizeof fitted);
  gfx::text(s, fitted, CX, 150, type::SMALL, col::N300);
}
}  // namespace

bool setupFinished(const SetupView &v) {
  SetupPhase p = phase(v);
  return p == SetupPhase::Done || p == SetupPhase::Failed || p == SetupPhase::TimedOut;
}

void drawSetup(TFT_eSprite &s, const SetupView &v) {
  s.fillSprite(col::BG);
  SetupPhase p = phase(v);
  gfx::text(s, "WI-FI SETUP", CX, 30, type::SMALL, col::N300);
  s.drawFastHLine(CX - 30, 41, 60, col::MAGENTA_DIM);

  char buf[48];
  switch (p) {
    case SetupPhase::Steps: {
      bool joined = v.portal == net::Portal::PhoneJoined;
      stepBadge(s, 40, 70, "1", joined, !joined);
      gfx::text(s, "On your phone, join", 56, 60, type::SMALL, joined ? col::N400 : col::N300, ML_DATUM);
      gfx::text(s, net::SETUP_AP_NAME, 56, 82, type::MEDIUM, joined ? col::N400 : col::CYAN, ML_DATUM);
      stepBadge(s, 40, 118, "2", false, joined);
      gfx::text(s, "Then open", 56, 108, type::SMALL, joined ? col::N300 : col::N400, ML_DATUM);
      gfx::text(s, net::SETUP_ADDRESS, 56, 128, type::MEDIUM, joined ? col::CYAN : col::N400, ML_DATUM);
      if (joined) {
        statusLine(s, 152, Icon::Check, "Phone connected", col::N100, v.ms, true);
      } else {
        statusLine(s, 152, Icon::Spinner, "Waiting for phone", col::CYAN, v.ms, v.reducedMotion);
      }
      snprintf(buf, sizeof buf, "%u:%02u left", (unsigned)(v.secondsLeft / 60), (unsigned)(v.secondsLeft % 60));
      gfx::text(s, buf, CX, 168, type::SMALL, col::N400);
      break;
    }
    case SetupPhase::Saving:
      snprintf(buf, sizeof buf, "%s  %d/3", syncStepText(v.sync), max(1, v.syncStep));
      outcome(s, Icon::Spinner, col::CYAN, "Saved", buf, v.ms, v.reducedMotion);
      break;
    case SetupPhase::Done:
      snprintf(buf, sizeof buf, "Clock synced %s", v.lastSyncText);
      outcome(s, Icon::Check, col::CYAN, "All set", buf, v.ms, true);
      break;
    case SetupPhase::Failed:
      outcome(s, Icon::Cross, col::WARN, failText(v.fail), failHint(v.fail), v.ms, true);
      break;
    case SetupPhase::TimedOut:
      outcome(s, Icon::Alert, col::WARN, "Setup timed out", "Nothing was changed", v.ms, true);
      break;
  }
  Button b = setupButton(p);
  pill(s, SETUP_PILL, buttonLabel(b), b == Button::Cancel || b == Button::Close ? col::N300 : col::CYAN,
       v.pressed == b);
}

Button hitSetup(const SetupView &v, int x, int y) {
  return hit(SETUP_PILL, x, y) ? setupButton(phase(v)) : Button::None;
}

// ---- Startup ------------------------------------------------------------------
// A line draws out from the centre, splits into two rails, and the name is
// revealed between them, left to right. Shown on power-on only.
void drawBoot(TFT_eSprite &s, float t, const char *version) {
  s.fillSprite(col::BG);
  auto phaseT = [&](float start, float len) { return motion::easeOut((t - start) / len); };
  float grow = phaseT(0.0f, 0.35f), split = phaseT(0.30f, 0.35f), reveal = phaseT(0.55f, 0.35f);

  int half = (int)(80 * grow), gap = (int)(24 * split);
  if (half > 0) {
    s.fillRect(CX - half, CY - gap - 1, 2 * half, 2, col::CYAN);
    if (gap > 0) s.fillRect(CX - half, CY + gap - 1, 2 * half, 2, col::MAGENTA);
  }
  if (reveal > 0) {
    gfx::text(s, "DRIFT", CX, CY + 1, type::MEDIUM, col::YELLOW);
    int edge = CX - 80 + (int)(160 * reveal);
    if (reveal < 1) {
      s.fillRect(edge, CY - gap + 1, CX + 81 - edge, 2 * gap - 2, col::BG);
      s.fillRect(edge, CY - gap + 2, 2, 2 * gap - 4, col::CYAN);  // scan edge
    }
    if (reveal >= 1) gfx::text(s, version, CX, CY + gap + 16, type::SMALL, col::N400);
  }
}

}  // namespace screens
