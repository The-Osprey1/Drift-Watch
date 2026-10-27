// Accent hierarchy: neutral text and outlines, cyan for progress and pressed
// controls, magenta only for the Wi-Fi setup identity, orange for faults (always
// with an icon and words). Text uses bold bitmap fonts in two roles (theme.h);
// large text swaps both roles one size up and lets the layout split pages
// instead of shrinking anything.
#include "screens.h"
#include <stdarg.h>
#include "date_names.h"
#include "gfx.h"
#include "orient.h"
#include "qrcodegen.h"
#include "settings.h"
#include "theme.h"

using namespace layout;
using type::Role;

namespace screens {
namespace {

// ---- Geometry ----------------------------------------------------------------------
// Every rectangle below has all four corners inside the 112 px safe circle and
// is at least 68 x 68 (about 9.2 mm). tools/host/simulate checks both.
// Settings: two stacked controls, or one tall control with large text.
constexpr Rect UPPER = {48, 44, 144, 68}, UPPER_HIT = {48, 40, 144, 76};
constexpr Rect LOWER = {48, 124, 144, 68}, LOWER_HIT = {48, 120, 144, 76};
constexpr Rect TALL = {30, 54, 180, 132};
// One action centred on its own page.
constexpr Rect MIDDLE = {48, 86, 144, 68}, MIDDLE_HIT = {48, 82, 144, 76};
// One large-text control under a note.
constexpr Rect LOWER_LARGE = {34, 100, 172, 88};
// Two actions side by side under the content. 8 px apart.
constexpr Rect ROW_LEFT = {30, 118, 86, 68}, ROW_RIGHT = {124, 118, 86, 68};

constexpr int TOP = 24;            // first text row on screens without a header
constexpr int PAGE_BOTTOM = 198;   // content stops here when the page has dots
constexpr int AFTER_BOTTOM = 214;  // text under the actions on a one-page screen
constexpr int DOTS_Y = 210;
constexpr int MARGIN = 6;          // text keeps this far inside the circle and controls
constexpr int CUT = 8;             // clipped corner size

enum class Icon : uint8_t { None, Check, Cross, Alert, Busy };
// Main text goes above the actions, After text below them. A Note belongs with
// the actions when they get a page of their own.
enum class Place : uint8_t { Main, After, Note };

struct Block {
  const char *text;
  Role role;
  uint16_t color;
  Icon icon;
  Place place;
};

// A control as drawn. Settings controls have a small label over a value; setup
// actions have only a value, set in the body size.
struct Item {
  Button id;
  const char *label;
  Icon labelIcon;
  uint16_t labelColor;
  const char *value;
  uint16_t valueColor, edge;
  int bars;  // brightness bars before the value, 0 = none
};

constexpr int MAX_BLOCKS = 24;

struct Doc {
  Block blocks[MAX_BLOCKS];
  int nb = 0;
  Item items[2];
  int ni = 0;
  char text[20][72];
  int nt = 0;
  const char *fmt(const char *f, ...) {
    char *out = text[nt < 19 ? nt++ : 19];
    va_list ap;
    va_start(ap, f);
    vsnprintf(out, sizeof text[0], f, ap);
    va_end(ap);
    return out;
  }
  void add(const char *t, Role r, uint16_t c, Icon i = Icon::None, Place p = Place::Main) {
    if (nb < MAX_BLOCKS) blocks[nb++] = {t, r, c, i, p};
  }
  void action(Button id, const char *value, uint16_t edge) {
    if (ni < 2) items[ni++] = {id, nullptr, Icon::None, 0, value, edge, edge, 0};
  }
};

// ---- Fonts and lines ---------------------------------------------------------------
struct Metrics {
  int ab, bb;  // tallest rise above and drop below the baseline, as TFT_eSPI computes them
};
Metrics metrics(const GFXfont *f) {
  Metrics m{0, 0};
  uint16_t n = f->last - f->first;
  for (uint16_t c = 0; c < n; c++) {
    const GFXglyph &g = f->glyph[c];
    m.ab = max(m.ab, -(int)g.yOffset);
    m.bb = max(m.bb, (int)g.height + g.yOffset);
  }
  return m;
}
// Paragraph lines use the font's own line advance; lines inside controls are packed tighter.
int tightLine(const GFXfont *f) {
  Metrics m = metrics(f);
  return m.ab + m.bb + 3;
}

const GFXfont *fontFor(Role r, bool large) { return gfx::font(r, large); }

// Shortens non-essential text (a network name) with "..." to fit.
const char *fitF(TFT_eSprite &s, const char *str, const GFXfont *f, int maxW, char *out, size_t n) {
  strncpy(out, str, n - 1);
  out[n - 1] = 0;
  if (gfx::textWidth(s, out, f) <= maxW) return out;
  size_t len = strlen(out);
  while (len > 0) {
    out[--len] = 0;
    char tmp[72];
    snprintf(tmp, sizeof tmp, "%s...", out);
    if (gfx::textWidth(s, tmp, f) <= maxW) {
      strncpy(out, tmp, n - 1);
      out[n - 1] = 0;
      break;
    }
  }
  return out;
}

// ---- Icons -------------------------------------------------------------------------
// Small rotating arc. With reduced motion it becomes a still clock symbol, so
// it reads as "in progress" rather than as a frozen spinner.
void busy(TFT_eSprite &s, int cx, int cy, int size, uint32_t ms, bool still, uint16_t c) {
  if (still) {
    gfx::iconPending(s, cx, cy, size, c);
    return;
  }
  int r = size / 2, w = max(2, size / 7);
  s.drawArc(cx, cy, r, r - w, 0, 360, col::N700, col::BG, true);
  uint32_t a = (ms * 360 / 900) % 360, b = (a + 100) % 360;
  if (b > a) s.drawArc(cx, cy, r, r - w, a, b, c, col::BG, true);
  else {
    s.drawArc(cx, cy, r, r - w, a, 360, c, col::BG, true);
    if (b > 0) s.drawArc(cx, cy, r, r - w, 0, b, c, col::BG, true);
  }
}

struct Ctx {
  TFT_eSprite &s;
  const View &v;
  bool draw;
  bool animated;
};

void icon(Ctx &c, Icon i, int cx, int cy, int size, uint16_t col) {
  if (!c.draw) return;
  switch (i) {
    case Icon::Check: gfx::iconCheck(c.s, cx, cy, size, col); break;
    case Icon::Cross: gfx::iconCross(c.s, cx, cy, size - 2, col); break;
    case Icon::Alert: gfx::iconAlert(c.s, cx, cy, size, col); break;
    case Icon::Busy:
      busy(c.s, cx, cy, size, c.v.ms, c.v.reducedMotion, col);
      if (!c.v.reducedMotion) c.animated = true;
      break;
    default: break;
  }
}

// ---- Paragraph flow ----------------------------------------------------------------
// Lays a block out from row `y`, each line as wide as the circle allows at that
// height. Returns the row after it, or -1 if it does not fit above `bottom`.
int flowBlock(Ctx &c, const Block &b, int y, int bottom) {
  const GFXfont *f = fontFor(b.role, c.v.large);
  Metrics m = metrics(f);
  int lh = gfx::lineHeight(f), iconSize = m.ab, iconW = b.icon == Icon::None ? 0 : iconSize + 6;
  int pos = 0, n = strlen(b.text);
  bool first = true;
  while (pos < n) {
    if (y + lh > bottom) return -1;
    int maxW = gfx::chordWidth(y + 2, y + lh - 2, MARGIN) - (first ? iconW : 0);
    int skip = 0, len = gfx::nextLine(c.s, b.text, pos, f, maxW, skip);
    if (len == 0) break;
    // Near the top the circle is narrow. Rather than split a word there, move down to where it fits.
    int end = pos + skip + len;
    bool split = end < n && b.text[end] != ' ' && b.text[end - 1] != '/';
    if (split && y < CY && y + 2 + lh <= bottom) {
      y += 2;
      continue;
    }
    if (c.draw) {
      char line[96];
      int l = min(len, (int)sizeof line - 1);
      memcpy(line, b.text + pos + skip, l);
      line[l] = 0;
      int tw = gfx::textWidth(c.s, line, f), w = tw + (first ? iconW : 0);
      int x = CX - w / 2, base = y + (lh - (m.ab + m.bb)) / 2 + m.ab;
      if (first && iconW) icon(c, b.icon, x + iconSize / 2, base - m.ab / 2, iconSize, b.color);
      gfx::text(c.s, line, x + (first ? iconW : 0), base, f, b.color, L_BASELINE);
    }
    pos += skip + len;
    y += lh;
    first = false;
  }
  return y + 2;
}

// Flows the blocks listed in `idx` from row `top`. Returns how many fitted, in
// order, and the row after the last one in `end`.
int flowFrom(Ctx &c, const Doc &d, const int *idx, int n, int top, int bottom, int &end) {
  int y = top;
  for (int i = 0; i < n; i++) {
    bool keep = c.draw;
    c.draw = false;
    int next = flowBlock(c, d.blocks[idx[i]], y, bottom);
    c.draw = keep;
    if (next < 0) {
      end = y;
      return i;
    }
    if (c.draw) flowBlock(c, d.blocks[idx[i]], y, bottom);
    y = next;
  }
  end = y;
  return n;
}

// As flowFrom, but centred in the space. Lines are wider further from the top
// of the circle, so text may need fewer lines lower down: the start row that
// gives the shortest layout is found, then the slack is shared above and below.
int flow(Ctx &c, const Doc &d, const int *idx, int n, int top, int bottom, bool centre = true) {
  bool keep = c.draw;
  c.draw = false;
  int end, fit = flowFrom(c, d, idx, n, top, bottom, end), start = top;
  if (centre) {
    int bestH = fit == n ? end - top : 9999;
    for (int y = top + 2; y + 2 < bottom; y += 2) {
      int e, f = flowFrom(c, d, idx, n, y, bottom, e);
      if (f == n && e - y < bestH) {
        bestH = e - y;
        fit = n;
        start = y;
        end = e;
      }
    }
    if (fit == n) {
      int shifted = start + (bottom - end) / 2, end2;
      if (flowFrom(c, d, idx, n, shifted, bottom, end2) == n) start = shifted;
    }
  }
  if (fit < n) fit = flowFrom(c, d, idx, n, top, bottom, end);
  c.draw = keep;
  if (c.draw) flowFrom(c, d, idx, fit, start, bottom, end);
  return fit;
}

// ---- Controls ----------------------------------------------------------------------
// Draws an item in `r`: label lines then value lines, centred as a group. Returns
// false if the words do not fit inside the control.
bool drawItem(Ctx &c, const Item &it, const Rect &r) {
  bool pressed = c.v.pressed == it.id && it.id != Button::None;
  if (c.draw) {
    uint16_t pressedBg = c.v.theme.pressed ? c.v.theme.pressed : col::CYAN_DEEP;
    if (pressed) gfx::cutBox(c.s, r.x, r.y, r.w, r.h, CUT, pressedBg, true);
    uint16_t activeEdge = c.v.theme.primary ? c.v.theme.primary : col::CYAN;
    uint16_t subtleEdge = c.v.theme.outline ? c.v.theme.outline : col::OUTLINE;
    uint16_t edge = pressed ? activeEdge : (it.edge == col::CYAN ? activeEdge : it.edge == col::OUTLINE ? subtleEdge : it.edge);
    gfx::cutBox(c.s, r.x, r.y, r.w, r.h, CUT, edge, false);
    gfx::cutBox(c.s, r.x + 1, r.y + 1, r.w - 2, r.h - 2, CUT - 1, edge, false);
  }
  const GFXfont *lf = fontFor(Role::Body, c.v.large);
  const GFXfont *vf = fontFor(it.label ? Role::Title : Role::Body, c.v.large);
  Metrics lm = metrics(lf), vm = metrics(vf);
  int lh = tightLine(lf), vh = tightLine(vf), inner = r.w - 2 * MARGIN;
  int labelIconW = it.labelIcon == Icon::None ? 0 : lm.ab + 5;
  int barW = c.v.large ? 9 : 7, barGap = 3, barsW = it.bars ? 4 * barW + 3 * barGap + 8 : 0;

  // Up to two lines each, measured first.
  struct L { int start, len; } labelL[2], valueL[2];
  int nl = 0, nv = 0, pos = 0, skip = 0, len;
  char fitted[48];
  const char *label = it.label;
  if (label) {
    // The network name may be long and is not essential, so it is shortened, never wrapped.
    if (it.id == Button::WifiSetup) label = fitF(c.s, label, lf, inner - labelIconW, fitted, sizeof fitted);
    while (nl < 2 && (len = gfx::nextLine(c.s, label, pos, lf, inner - labelIconW, skip)) > 0) {
      labelL[nl++] = {pos + skip, len};
      pos += skip + len;
    }
    if (label[pos] && strspn(label + pos, " ") != strlen(label + pos)) return false;
  }
  pos = 0;
  while (nv < 2 && (len = gfx::nextLine(c.s, it.value, pos, vf, inner - barsW, skip)) > 0) {
    valueL[nv++] = {pos + skip, len};
    pos += skip + len;
  }
  if (it.value[pos] && strspn(it.value + pos, " ") != strlen(it.value + pos)) return false;
  int total = nl * lh + nv * vh + (nl && nv ? 4 : 0);
  if (total > r.h - 8) return false;

  int y = r.y + (r.h - total) / 2;
  uint16_t labelColor = pressed && it.labelColor == col::TEXT_2 ? col::TEXT : it.labelColor;
  uint16_t valueColor = pressed ? col::WHITE : it.valueColor;
  char line[72];
  auto put = [&](const char *src, const L &l, const GFXfont *f, const Metrics &m, int lineH, int extraW, uint16_t k,
                 int &outX, int &outBase) {
    int n = min(l.len, (int)sizeof line - 1);
    memcpy(line, src + l.start, n);
    line[n] = 0;
    int w = gfx::textWidth(c.s, line, f) + extraW;
    outX = r.x + r.w / 2 - w / 2;
    outBase = y + (lineH - (m.ab + m.bb)) / 2 + m.ab;
    if (c.draw) gfx::text(c.s, line, outX + extraW, outBase, f, k, L_BASELINE);
    y += lineH;
  };
  int x, base;
  for (int i = 0; i < nl; i++) {
    put(label, labelL[i], lf, lm, lh, i == 0 ? labelIconW : 0, labelColor, x, base);
    if (i == 0 && labelIconW) icon(c, it.labelIcon, x + lm.ab / 2, base - lm.ab / 2, lm.ab, labelColor);
  }
  if (nl && nv) y += 4;
  for (int i = 0; i < nv; i++) {
    put(it.value, valueL[i], vf, vm, vh, i == 0 ? barsW : 0, valueColor, x, base);
    if (i == 0 && it.bars && c.draw) {
      // Bars rise left to right; the count of filled bars is the level, colour only reinforces it.
      for (int b = 0; b < 4; b++) {
        int bh = vm.ab * (4 + 2 * b) / 10, bx = x + b * (barW + barGap), by = base - bh;
        if (b < it.bars) c.s.fillRect(bx, by, barW, bh, valueColor);
        else c.s.drawRect(bx, by, barW, bh, col::OUTLINE);
      }
    }
  }
  return true;
}

void dots(TFT_eSprite &s, int page, int pages, uint16_t activeCol, uint16_t outlineCol) {
  if (pages < 2) return;
  int gap = 12, x0 = CX - (pages - 1) * gap / 2;
  Hub activeHub = hubOfPage(page, false);
  for (int i = 0; i < pages; i++) {
    int x = x0 + i * gap;
    if (i == page) {
      s.fillSmoothCircle(x, DOTS_Y, 3, activeCol, col::BG);
    } else if (pages == (SETTINGS_ITEMS + 1) / 2 && hubOfPage(i, false) == activeHub) {
      s.fillSmoothCircle(x, DOTS_Y, 2, activeCol, col::BG);
    } else {
      s.drawCircle(x, DOTS_Y, 3, outlineCol);
    }
  }
}
void dots(TFT_eSprite &s, int page, int pages) { dots(s, page, pages, col::TEXT, col::OUTLINE); }

// Swipe-down handle at the top edge, the hint that this screen closes downwards.
void handle(TFT_eSprite &s) { s.fillRect(CX - 12, 13, 24, 3, col::N600); }

// ---- Status words --------------------------------------------------------------------
const char *stageText(net::Sync s) {
  switch (s) {
    case net::Sync::Connecting: return "Joining Wi-Fi";
    case net::Sync::Zone: return "Finding time zone";
    default: return "Setting time";
  }
}

// Shorter words for the sync control, which is narrower than a paragraph.
const char *stageShort(net::Sync s) { return s == net::Sync::Zone ? "Finding zone" : stageText(s); }

const char *failText(net::Fail f) {
  switch (f) {
    case net::Fail::NoNetworkSaved: return "No Wi-Fi saved";
    case net::Fail::JoinFailed: return "Can't join Wi-Fi";
    default: return "No time server";
  }
}

// Why joining failed, from the radio's reason, in words a person can act on.
const char *joinWords(net::JoinWhy w) {
  switch (w) {
    case net::JoinWhy::NotFound: return "Network not found.";
    case net::JoinWhy::Password: return "Password not accepted.";
    case net::JoinWhy::Refused: return "The router turned the watch away.";
    default: return "No answer from the network.";
  }
}

// What to do about it. The watch has a 2.4 GHz radio only, which is behind most failures
// with dual-band and mesh routers, so the hints say so wherever it could be the cause.
const char *joinHint(net::JoinWhy w) {
  switch (w) {
    case net::JoinWhy::NotFound: return "The watch only works with 2.4 GHz Wi-Fi. Check 2.4 GHz is on and the watch is near the router.";
    case net::JoinWhy::Password: return "Check the password with Edit Wi-Fi. Capital letters count.";
    case net::JoinWhy::Refused: return "Turn on 2.4 GHz and WPA2 on the router. Mesh routers may need a separate 2.4 GHz network.";
    default: return "The watch only works with 2.4 GHz Wi-Fi. Move closer to the router, then Retry.";
  }
}

const char *failHint(net::Fail f) {
  switch (f) {
    case net::Fail::NoNetworkSaved: return "Run Wi-Fi setup, or set the time by hand.";
    default: return "Wi-Fi joined, but no time came back. The internet may be down.";
  }
}

bool running(const View &v) { return v.stage != net::Sync::Idle; }

// ---- Settings --------------------------------------------------------------------------
Item settingsItemFor(const View &v, Button b, Doc &d) {
  Item it{b, nullptr, Icon::None, col::TEXT_2, "", col::TEXT, col::OUTLINE, 0};
  switch (b) {
    case Button::Brightness:
      it.label = "Brightness";
      it.value = d.fmt("%d of %d", v.brightnessLevel + 1, BRIGHTNESS_LEVEL_COUNT);
      it.bars = v.brightnessLevel + 1;
      break;
    case Button::TimeFormat:
      it.label = "Time format";
      it.value = v.use12h ? "12-hour" : "24-hour";
      break;
    case Button::HotSwap: {
      it.label = "Hot swap";
      uint16_t mask = v.activeFaces & ALL_FACES_MASK;
      int count = 0;
      for (int i = 0; i < FACE_COUNT; i++) {
        if (mask & (1u << i)) count++;
      }
      if (count >= FACE_COUNT || count == 0) {
        it.value = d.fmt("All (%d)", FACE_COUNT);
      } else if (count == 2) {
        int other = -1;
        for (int i = 0; i < FACE_COUNT; i++) {
          if (mask & (1u << i)) {
            if (other == -1) other = i;
            else if (i != (int)(settings.face % FACE_COUNT)) other = i;
          }
        }
        it.value = d.fmt("2: %s", faceName((Face)other));
        it.valueColor = it.edge = v.theme.primary ? v.theme.primary : col::CYAN;
      } else {
        it.value = d.fmt("%d active", count);
        it.valueColor = it.edge = v.theme.primary ? v.theme.primary : col::CYAN;
      }
      break;
    }
    case Button::Motion:
      it.label = "Motion";
      it.value = v.motion == MOTION_OFF ? "Off" : v.motion == MOTION_EXPRESSIVE ? "Expressive" : "Subtle";
      break;
    case Button::SetTime:
      // Like Sync now: the label is the state, the value is what a tap does.
      if (v.timeValid) {
        it.label = v.clockByHand ? d.fmt("%s by hand", v.nowText) : d.fmt("Clock %s", v.nowText);
      } else {
        it.label = "Time not set";
        it.labelIcon = Icon::Alert;
        it.labelColor = col::WARN;
      }
      it.value = "Set time";
      break;
    case Button::RaiseWake:
      it.label = v.imuPresent ? "Wake gesture" : "No sensor";
      it.value = !v.imuPresent                       ? "Off"
                 : v.raiseToWake && v.twistToWake ? "Raise, twist"
                 : v.raiseToWake                  ? "Raise"
                 : v.twistToWake                  ? "Twist"
                                                  : "Off";
      break;
    case Button::SyncNow:
      // The label carries the state; the value is what a tap does.
      if (running(v)) {
        it.label = stageShort(v.stage);
        it.labelIcon = Icon::Busy;
        it.labelColor = col::CYAN;
        it.value = "Syncing";
        it.valueColor = col::CYAN;
      } else if (!v.ssid[0]) {
        it.label = "No Wi-Fi saved";  // not a failure: there is nothing to sync with yet
        it.value = "Sync now";
      } else if (v.result == net::Result::Failed) {
        it.label = "Sync failed";
        it.labelIcon = Icon::Alert;
        it.labelColor = col::WARN;
        it.value = "Sync now";
      } else if (v.lastSyncText[0]) {
        it.label = d.fmt("Synced %s", v.lastSyncText);
        it.labelIcon = Icon::Check;
        it.value = "Sync now";
      } else {
        it.label = v.timeValid ? "Not synced yet" : "Time not set";
        it.labelIcon = v.timeValid ? Icon::None : Icon::Alert;
        it.labelColor = v.timeValid ? col::TEXT_2 : col::WARN;
        it.value = "Sync now";
      }
      break;
    case Button::WifiSetup:
      it.label = v.ssid[0] ? v.ssid : "No Wi-Fi saved";
      it.value = "Wi-Fi setup";
      it.valueColor = it.edge = col::MAGENTA;
      break;
    case Button::Details:
      it.label = "Status and";
      it.value = "Text size";
      break;
    case Button::PhoneSync:
      it.label = "Phone sync";
      it.value = "Bluetooth";
      it.valueColor = it.edge = col::CYAN;
      break;
    case Button::Night:
      it.label = "Night mode";
      if (!v.night) it.value = "Off";
      else if (v.use12h) {
        int from = NIGHT_FROM[v.night % NIGHT_CHOICES], to = NIGHT_TO[v.night % NIGHT_CHOICES];
        it.value = d.fmt("%d%s-%d%s", from % 12 ? from % 12 : 12, from < 12 ? "am" : "pm", to % 12 ? to % 12 : 12, to < 12 ? "am" : "pm");
      } else {
        it.value = d.fmt("%02d:00-%02d:00", NIGHT_FROM[v.night % NIGHT_CHOICES], NIGHT_TO[v.night % NIGHT_CHOICES]);
      }
      break;
    case Button::ScreenTurn:
      it.label = "Screen turn";
      it.value = orient::turnName(v.screenTurn);
      break;
    case Button::TouchCheck:
      it.label = "Touch check";
      it.labelIcon = v.touchChecked ? Icon::Check : Icon::None;
      it.value = v.touchChecked ? "Lined up" : "Not done";
      break;
    default:
      break;
  }
  return it;
}

int settingsPerPage(const View &v) { return v.large ? 1 : 2; }
// With two items a page the Clock hub has five, so its last page holds one and the Gestures hub
// starts on a fresh page: the slot after Wi-Fi setup is left empty. Without it Raise to wake sat
// under the Clock heading and Touch check under Gestures.
constexpr int CLOCK_HUB_END = 9;  // first item after the Clock hub
int settingsSlot(const View &v, int item) { return !v.large && item >= CLOCK_HUB_END ? item + 1 : item; }
int settingsSlots(const View &v) { return settingsSlot(v, SETTINGS_ITEMS - 1) + 1; }
Button settingsAtSlot(const View &v, int slot) {
  if (!v.large && slot == CLOCK_HUB_END) return Button::None;
  return settingsItem(!v.large && slot > CLOCK_HUB_END ? slot - 1 : slot);
}
int settingsPages(const View &v) {
  int per = settingsPerPage(v);
  return (settingsSlots(v) + per - 1) / per;
}

int settingsControls(const View &v, int page, Control *out, int max) {
  int per = settingsPerPage(v), n = 0;
  for (int i = 0; i < per && n < max; i++) {
    int idx = page * per + i;
    if (idx >= settingsSlots(v)) break;
    Button b = settingsAtSlot(v, idx);
    if (b == Button::None) continue;
    if (v.large) out[n++] = {b, TALL, TALL};
    else out[n++] = {b, i == 0 ? UPPER : LOWER, i == 0 ? UPPER_HIT : LOWER_HIT};
  }
  return n;
}

bool drawSettings(TFT_eSprite &s, const View &v, int page) {
  Ctx c{s, v, true, false};
  handle(s);
  if (!v.large) {
    // Compact category hub header; with large text the space goes to the control instead.
    const GFXfont *f = fontFor(Role::Body, false);
    Hub h = hubOfPage(page, false);
    gfx::text(s, hubName(h), CX, 27, f, v.theme.secondary ? v.theme.secondary : col::TEXT_2, MC_DATUM);
  }
  Control ctl[2];
  int n = settingsControls(v, page, ctl, 2);
  for (int i = 0; i < n; i++) {
    Doc d;
    drawItem(c, settingsItemFor(v, ctl[i].id, d), ctl[i].visible);
  }
  dots(s, page, settingsPages(v), v.theme.primary ? v.theme.primary : col::TEXT, v.theme.outline ? v.theme.outline : col::OUTLINE);
  return c.animated;
}

// ---- Set time -------------------------------------------------------------------------------
// One field per page with - and + side by side, the whole time or date above them with the
// field being set in cyan and underlined, then a page that shows the result and saves it.
// Title case suits the screens; the faces use the upper-case tables.
constexpr auto &MONTHS = datenames::MONTHS_TITLE;
constexpr auto &DAYS = datenames::DAYS_TITLE;
constexpr int VALUE_BASE = 98;  // baseline of the time or date being set

int daysIn(int year, int month0) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return month0 == 1 && leap ? 29 : d[month0 % 12];
}

// Day of the week for a calendar date (0 = Sunday), without the time zone getting involved.
int weekday(int y, int m0, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m0 < 2) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m0] + d) % 7;
}

const char *fieldTitle(TimeField f) {
  static const char *t[] = {"Set hour", "Set minutes", "Set day", "Set month", "Set year", "Save time"};
  return t[(int)f];
}

// The time as "14:05" or "2:05 PM".
const char *timeWords(const View &v, const tm &t, char *buf, size_t n) {
  int h = t.tm_hour;
  if (v.use12h) snprintf(buf, n, "%d:%02d %s", h % 12 ? h % 12 : 12, t.tm_min, h < 12 ? "AM" : "PM");
  else snprintf(buf, n, "%02d:%02d", h, t.tm_min);
  return buf;
}

// Draws the value line in pieces so the one being set stands out. Gaps are added as
// pixels: a trailing space counts for nothing in the width of a bitmap font string.
void timeValue(TFT_eSprite &s, const View &v, TimeField f) {
  const GFXfont *big = gfx::font(Role::Title, true);
  const tm &t = v.edit;
  char p[4][12];
  bool active[4] = {};
  int gap[4] = {}, n = 0;  // gap before each piece
  if (f == TimeField::Hour || f == TimeField::Minute) {
    int h = t.tm_hour;
    snprintf(p[n], sizeof p[0], v.use12h ? "%d" : "%02d", v.use12h ? (h % 12 ? h % 12 : 12) : h);
    active[n++] = f == TimeField::Hour;
    snprintf(p[n++], sizeof p[0], ":");
    snprintf(p[n], sizeof p[0], "%02d", t.tm_min);
    active[n++] = f == TimeField::Minute;
    if (v.use12h) {
      gap[n] = 10;
      snprintf(p[n++], sizeof p[0], "%s", h < 12 ? "AM" : "PM");  // changes with the hour
    }
  } else {
    snprintf(p[n], sizeof p[0], "%d", t.tm_mday);
    active[n++] = f == TimeField::Day;
    gap[n] = 10;
    snprintf(p[n], sizeof p[0], "%s", MONTHS[t.tm_mon % 12]);
    active[n++] = f == TimeField::Month;
    gap[n] = 10;
    snprintf(p[n], sizeof p[0], "%d", t.tm_year + 1900);
    active[n++] = f == TimeField::Year;
  }
  int total = 0;
  for (int i = 0; i < n; i++) total += gap[i] + gfx::textWidth(s, p[i], big);
  int x = CX - total / 2;
  for (int i = 0; i < n; i++) {
    x += gap[i];
    int w = gfx::textWidth(s, p[i], big);
    gfx::text(s, p[i], x, VALUE_BASE, big, active[i] ? col::CYAN : col::TEXT_2, L_BASELINE);
    if (active[i]) s.fillRect(x, VALUE_BASE + 6, w, 3, col::CYAN);  // marks the field without relying on colour
    x += w;
  }
}

// Minus and plus as solid bars, bigger and bolder than the font's signs.
void stepSymbol(TFT_eSprite &s, const Rect &r, bool plus, uint16_t c, bool large) {
  int len = large ? 28 : 22, w = large ? 5 : 4, cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  s.fillRect(cx - len / 2, cy - w / 2, len, w, c);
  if (plus) s.fillRect(cx - w / 2, cy - len / 2, w, len, c);
}

int setTimeControls(const View &v, int page, Control *out, int max) {
  if (max < 1) return 0;
  if (page >= TIME_PAGES - 1) {
    out[0] = {Button::SaveTime, LOWER, LOWER_HIT};
    return 1;
  }
  out[0] = {Button::Minus, ROW_LEFT, ROW_LEFT};
  if (max < 2) return 1;
  out[1] = {Button::Plus, ROW_RIGHT, ROW_RIGHT};
  return 2;
}

bool drawSetTime(TFT_eSprite &s, const View &v, int page) {
  Ctx c{s, v, true, false};
  handle(s);
  TimeField f = (TimeField)constrain(page, 0, TIME_PAGES - 1);
  const GFXfont *body = fontFor(Role::Body, v.large);
  gfx::text(s, fieldTitle(f), CX, 44, body, col::TEXT_2, MC_DATUM);
  Control ctl[2];
  int n = setTimeControls(v, page, ctl, 2);
  if (f == TimeField::Save) {
    char buf[16], date[24];
    gfx::text(s, timeWords(v, v.edit, buf, sizeof buf), CX, 78, gfx::font(Role::Title, true), col::TEXT, MC_DATUM);
    snprintf(date, sizeof date, "%s %d %s %d", DAYS[v.edit.tm_wday % 7], v.edit.tm_mday, MONTHS[v.edit.tm_mon % 12],
             v.edit.tm_year + 1900);
    gfx::text(s, date, CX, 105, body, col::TEXT, MC_DATUM);
    Item save{Button::SaveTime, nullptr, Icon::None, 0, "Save", col::CYAN, col::CYAN, 0};
    drawItem(c, save, ctl[0].visible);
  } else {
    timeValue(s, v, f);
    for (int i = 0; i < n; i++) {
      Item it{ctl[i].id, nullptr, Icon::None, 0, "", col::TEXT, col::OUTLINE, 0};
      drawItem(c, it, ctl[i].visible);
      bool pressed = v.pressed == ctl[i].id;
      stepSymbol(s, ctl[i].visible, ctl[i].id == Button::Plus, pressed ? col::WHITE : col::TEXT, v.large);
    }
  }
  dots(s, page, TIME_PAGES);
  return false;
}

// ---- Details and setup documents ----------------------------------------------------------
void detailsDoc(const View &v, Doc &d) {
  d.add("Status", Role::Body, col::TEXT_2);
  if (v.batteryPct >= 0) {
    d.add(d.fmt("Battery %d%%, %d.%02d V", v.batteryPct, v.batteryMv / 1000, v.batteryMv % 1000 / 10), Role::Body, col::TEXT);
    // Measured on this watch from how fast the level falls, never assumed.
    if (v.charging) d.add("Charging", Role::Body, col::TEXT_2);
    else if (v.batteryHours < 0) d.add("Time left: measuring", Role::Body, col::TEXT_2);
    else if (v.batteryHours < 48) d.add(d.fmt("About %d h left", (int)(v.batteryHours + 0.5f)), Role::Body, col::TEXT);
    else d.add(d.fmt("About %d days %d h left", (int)v.batteryHours / 24, (int)v.batteryHours % 24), Role::Body, col::TEXT);
  } else {
    d.add("Battery: no reading", Role::Body, col::TEXT_2);
  }
  if (!v.timeValid) d.add("Time not set", Role::Body, col::WARN, Icon::Alert);
  if (running(v)) d.add(d.fmt("Syncing: %s, step %d of 3", stageText(v.stage), v.syncStep), Role::Body, col::CYAN, Icon::Busy);
  if (v.clockByHand && v.timeValid) d.add("Clock set by hand", Role::Body, col::TEXT);
  else if (v.timeValid && v.clockQuality == clockstatus::Quality::Retained)
    d.add("Clock retained, sync age unknown", Role::Body, col::TEXT_2);
  else if (v.timeValid && v.clockQuality == clockstatus::Quality::Stale)
    d.add("Clock needs a fresh sync", Role::Body, col::WARN, Icon::Alert);
  if (v.result == net::Result::Failed && v.fail == net::Fail::JoinFailed) {
    d.add(d.fmt("Last sync failed: %s", joinWords(v.joinWhy)), Role::Body, col::WARN, Icon::Alert);
    d.add(joinHint(v.joinWhy), Role::Body, col::TEXT_2);
    if (v.joinCode) d.add(d.fmt("Wi-Fi reason %d", v.joinCode), Role::Body, col::TEXT_2);
  } else if (v.result == net::Result::Failed) {
    d.add(d.fmt("Last sync failed: %s", failText(v.fail)), Role::Body, col::WARN, Icon::Alert);
    d.add(failHint(v.fail), Role::Body, col::TEXT_2);
  } else if (v.lastSyncText[0]) {
    const char *from = v.syncSource == net::Source::Phone ? " by phone" :
                       v.syncSource == net::Source::Usb ? " by USB" : " by Wi-Fi";
    d.add(d.fmt("Last sync %s%s, %s", v.lastSyncText, from, v.syncAgo),
          Role::Body, col::TEXT, Icon::Check);
  } else if (!running(v)) {
    d.add("Not synced yet", Role::Body, col::TEXT_2);
  }
  if (v.zone[0]) d.add(d.fmt("Time zone %s", v.zone), Role::Body, col::TEXT);
  else d.add(v.zoneManual ? "Time zone typed in setup" : "Time zone not found yet", Role::Body, col::TEXT_2);
  d.add(v.ssid[0] ? d.fmt("Wi-Fi %s", v.ssid) : "No Wi-Fi saved", Role::Body, v.ssid[0] ? col::TEXT : col::TEXT_2);
  if (v.driftPpb) {
    float perDay = v.driftPpb * 86400e-9f;
    d.add(d.fmt("Clock drift %.1f s a day %s, corrected", perDay < 0 ? -perDay : perDay, perDay < 0 ? "slow" : "fast"), Role::Body,
          col::TEXT_2);
  }
  d.add(v.imuPresent ? "Motion sensor found" : "Motion sensor not found", Role::Body, col::TEXT_2);
  // For the watch test: which of the sensor's interrupt lines reach the processor.
  if (v.imuPresent) {
    d.add(d.fmt("Sensor lines: GPIO 4 %lu, GPIO 3 %lu", (unsigned long)v.imuLine1, (unsigned long)v.imuLine2), Role::Body, col::TEXT_2);
    if (v.tempC > -50.0f) {
      if (v.chipTempC > -50.0f) {
        d.add(d.fmt("Temp %.1f C (chip %.0f C)", v.tempC, v.chipTempC), Role::Body, col::TEXT_2);
      } else {
        d.add(d.fmt("Temp %.1f C", v.tempC), Role::Body, col::TEXT_2);
      }
    }
  }
  d.add(d.fmt("Screen %s, touch %s", orient::turnName(v.screenTurn), v.touchChecked ? "checked" : "not checked"), Role::Body,
        col::TEXT_2);
  d.add(d.fmt("Firmware %s", v.version), Role::Body, col::TEXT_2);
  if (v.startReason) d.add(d.fmt("Last start: %s", v.startReason), Role::Body, v.restarts ? col::WARN : col::TEXT_2);
  if (v.restarts)
    d.add(v.restarts == 1 ? "Restarted by itself once on this charge" : d.fmt("Restarted by itself %lu times on this charge", (unsigned long)v.restarts),
          Role::Body, col::TEXT_2);
  if (v.diag) d.add(v.diag, Role::Body, col::TEXT_2);
  d.add("Bigger text on these screens. Faces stay the same.", Role::Body, col::TEXT_2, Icon::None, Place::Note);
  d.items[0] = {Button::TextSize, "Text size", Icon::None, col::TEXT_2, v.large ? "Large" : "Normal", col::TEXT, col::OUTLINE, 0};
  d.ni = 1;
}

void setupDoc(const View &v, Doc &d) {
  switch (setupPhase(v)) {
    case SetupPhase::Hotspot: {
      // The address only answers on the watch's own hotspot, so joining it always comes
      // first, and step 2 only appears once a phone has joined.
      bool joined = v.portal == net::Portal::PhoneJoined;
      d.add(joined ? "Step 2 of 2" : "Step 1 of 2", Role::Body, col::TEXT_2);
      if (joined) {
        d.add("Phone joined. Stay on Drift-Setup and open", Role::Body, col::TEXT);
        d.add(net::SETUP_ADDRESS, Role::Title, col::CYAN);
        d.add("Pick a 2.4 GHz network. 5 GHz won't work.", Role::Body, col::TEXT_2, Icon::None, Place::Note);
      } else {
        d.add("In your phone's Wi-Fi list, join", Role::Body, col::TEXT);
        d.add(net::SETUP_AP_NAME, Role::Title, col::MAGENTA);
        d.add("192.168.4.1 only opens once your phone is on Drift-Setup.", Role::Body, col::TEXT_2, Icon::None, Place::Note);
      }
      // The countdown sits in the Cancel control: both are about the hotspot.
      d.action(Button::Cancel, "Cancel", col::TEXT_2);
      d.items[0].label = d.fmt("Closes in %u:%02u", (unsigned)(v.secondsLeft / 60), (unsigned)(v.secondsLeft % 60));
      d.items[0].labelColor = col::TEXT_2;
      d.items[0].valueColor = col::TEXT;
      break;
    }
    case SetupPhase::Connecting:
      // Real stages only, with no percentage: the watch cannot know how long each will take.
      d.add("Connecting", Role::Title, col::TEXT);
      d.add(d.fmt("%s  %d/3", stageShort(v.stage), max(1, v.syncStep)), Role::Body, col::CYAN, Icon::Busy);
      if (v.joinTry > 1 && v.stage == net::Sync::Connecting) d.add("No answer yet. Trying again", Role::Body, col::TEXT);
      d.add("Back lets it finish", Role::Body, col::TEXT_2);
      d.action(Button::Back, "Back", col::TEXT_2);
      d.action(Button::Cancel, "Cancel", col::TEXT_2);
      break;
    case SetupPhase::Done:
      d.add("All set", Role::Title, col::CYAN, Icon::Check);
      d.add(d.fmt("Time set to %s", v.lastSyncText), Role::Body, col::TEXT);
      d.action(Button::Done, "Done", col::CYAN);
      break;
    case SetupPhase::Failed:
      if (v.fail == net::Fail::ClockFailed) {
        d.add("No time", Role::Title, col::WARN, Icon::Cross);
        d.add("Wi-Fi joined. No time server answered.", Role::Body, col::TEXT);
      } else {
        d.add("Can't join", Role::Title, col::WARN, Icon::Cross);
        d.add(joinWords(v.joinWhy), Role::Body, col::TEXT);
        d.add(joinHint(v.joinWhy), Role::Body, col::TEXT_2);
      }
      d.action(Button::EditWifi, "Edit Wi-Fi", col::MAGENTA);
      d.action(Button::Retry, "Retry", col::CYAN);
      break;
    case SetupPhase::TimedOut:
      d.add("Timed out", Role::Title, col::WARN, Icon::Alert);
      // True: nothing is stored until the phone page is saved.
      d.add("Nothing was changed.", Role::Body, col::TEXT);
      d.action(Button::TryAgain, "Try again", col::MAGENTA);
      d.action(Button::Back, "Back", col::TEXT_2);
      break;
  }
}

void phoneDoc(const View &v, Doc &d) {
  switch (phonePhase(v)) {
    case PhonePhase::Waiting:
      d.add("Phone sync", Role::Title, col::TEXT);
      d.add("On your phone, open the Drift sync page and tap Connect. Pick", Role::Body, col::TEXT);
      d.add(v.bleName, Role::Title, col::CYAN);
      d.add("iPhone: use the Bluefy app. Android or a computer: Chrome or Edge.", Role::Body, col::TEXT_2, Icon::None, Place::Note);
      d.action(Button::Cancel, "Cancel", col::TEXT_2);
      d.items[0].label = d.fmt("Closes in %u:%02u", (unsigned)(v.bleSecondsLeft / 60), (unsigned)(v.bleSecondsLeft % 60));
      d.items[0].labelColor = col::TEXT_2;
      d.items[0].valueColor = col::TEXT;
      break;
    case PhonePhase::Connected:
      d.add("Phone connected", Role::Title, col::CYAN, Icon::Check);
      d.add("Tap Sync time on the phone.", Role::Body, col::TEXT);
      if (v.phoneError) d.add(d.fmt("Last message refused: %s", v.phoneError), Role::Body, col::WARN, Icon::Alert);
      d.action(Button::Cancel, "Cancel", col::TEXT_2);
      break;
    case PhonePhase::TimeSet:
      d.add("Time set", Role::Title, col::CYAN, Icon::Check);
      d.add(d.fmt("Clock %s", v.nowText), Role::Body, col::TEXT);
      if (v.phoneWifiSaved) d.add(d.fmt("Wi-Fi %s saved. The watch joins it next.", v.phoneSsid), Role::Body, col::TEXT_2);
      d.action(Button::Done, "Done", col::CYAN);
      break;
    case PhonePhase::Closed:
      d.add("Phone sync closed", Role::Title, col::TEXT);
      d.add(v.phoneWifiSaved ? d.fmt("Wi-Fi %s saved.", v.phoneSsid) : "Nothing was changed.", Role::Body, col::TEXT_2);
      d.action(Button::PhoneSync, "Open again", col::CYAN);
      d.action(Button::Back, "Back", col::TEXT_2);
      break;
    case PhonePhase::Failed:
      d.add("No Bluetooth", Role::Title, col::WARN, Icon::Cross);
      d.add("Bluetooth did not start. Restart the watch and try again.", Role::Body, col::TEXT);
      d.action(Button::Back, "Back", col::TEXT_2);
      break;
  }
}

void buildDoc(const View &v, Doc &d) {
  if (v.screen == Screen::Details) detailsDoc(v, d);
  else if (v.screen == Screen::PhoneSync) phoneDoc(v, d);
  else setupDoc(v, d);
}

// ---- Touch check ----------------------------------------------------------------------------
// A dot to tap, then a second; the raw positions decide how touch is turned (orient.h). The
// dots are drawn in screen space, so they always appear where the screen turn puts them.
bool drawTouchCheck(TFT_eSprite &s, const View &v) {
  const GFXfont *body = fontFor(Role::Body, v.large), *title = fontFor(Role::Title, v.large);
  // Short words: the middle of the circle is the only wide part, and the dots take some of it.
  if (v.checkResult == CheckResult::Matched) {
    gfx::iconCheck(s, CX, 92, 30, col::CYAN);
    gfx::text(s, "Lined up", CX, 136, title, col::TEXT, MC_DATUM);
    return false;
  }
  if (v.checkResult == CheckResult::NoMatch) {
    gfx::iconAlert(s, CX, 92, 30, col::WARN);
    gfx::text(s, "No match", CX, 136, title, col::TEXT, MC_DATUM);
    gfx::text(s, "Tap again", CX, 164, body, col::TEXT_2, MC_DATUM);
    return false;
  }
  int step = constrain(v.checkStep, 0, 1);
  int tx = orient::TARGET_X[step], ty = orient::TARGET_Y[step];
  s.drawCircle(tx, ty, 15, col::CYAN);
  s.drawCircle(tx, ty, 14, col::CYAN);
  s.fillSmoothCircle(tx, ty, 5, col::CYAN, col::BG);
  gfx::text(s, "Touch check", CX, 88, body, col::TEXT_2, MC_DATUM);
  gfx::text(s, "BOOT skips", CX, 114, body, col::N400, MC_DATUM);
  gfx::text(s, "Tap the dot", CX, 158, title, col::TEXT, MC_DATUM);
  gfx::text(s, step ? "2 of 2" : "1 of 2", CX, 190, body, col::TEXT_2, MC_DATUM);
  return false;
}

// Page plan for a document: everything on one page if it fits, otherwise
// content pages followed by a page for the actions.
struct Plan {
  bool single;
  int pages;
  bool notesWithActions;
  int order[MAX_BLOCKS], n;   // blocks in content-page order
  int start[8], count[8];     // per content page, into `order`
  int contentPages;
};

void singleRects(int ni, Control *out, const Doc &d) {
  if (ni == 1) out[0] = {d.items[0].id, LOWER, LOWER_HIT};
  if (ni == 2) {
    out[0] = {d.items[0].id, ROW_LEFT, ROW_LEFT};
    out[1] = {d.items[1].id, ROW_RIGHT, ROW_RIGHT};
  }
}

void actionPageRects(const View &v, const Doc &d, bool withNotes, Control *out) {
  if (d.ni == 1) {
    if (withNotes) out[0] = {d.items[0].id, v.large ? LOWER_LARGE : LOWER, v.large ? LOWER_LARGE : LOWER_HIT};
    else out[0] = {d.items[0].id, v.large ? TALL : MIDDLE, v.large ? TALL : MIDDLE_HIT};
  } else if (d.ni == 2) {
    out[0] = {d.items[0].id, UPPER, UPPER_HIT};
    out[1] = {d.items[1].id, LOWER, LOWER_HIT};
  }
}

// Draws (or only measures) the items in their rectangles; false if any words do not fit.
bool placeItems(Ctx &c, const Doc &d, const Control *ctl) {
  bool ok = true;
  for (int i = 0; i < d.ni; i++) ok &= drawItem(c, d.items[i], ctl[i].visible);
  return ok;
}

Plan plan(TFT_eSprite &s, const View &v, const Doc &d) {
  Plan p{};
  Ctx c{s, v, false, false};
  int mainIdx[MAX_BLOCKS], nm = 0, afterIdx[MAX_BLOCKS], na = 0, noteIdx[MAX_BLOCKS], nn = 0;
  for (int i = 0; i < d.nb; i++) {
    if (d.blocks[i].place == Place::After) afterIdx[na++] = i;
    else if (d.blocks[i].place == Place::Note) noteIdx[nn++] = i;
    else mainIdx[nm++] = i;
  }

  // One page: main and note text above the actions, after text below them.
  Control ctl[2];
  singleRects(d.ni, ctl, d);
  int actTop = d.ni ? ctl[0].visible.y : PAGE_BOTTOM, actBottom = d.ni ? ctl[0].visible.y + ctl[0].visible.h : TOP;
  int above[MAX_BLOCKS], nab = 0;
  for (int i = 0; i < nm; i++) above[nab++] = mainIdx[i];
  for (int i = 0; i < nn; i++) above[nab++] = noteIdx[i];
  if (flow(c, d, above, nab, TOP, actTop - 6) == nab && flow(c, d, afterIdx, na, actBottom + 4, AFTER_BOTTOM, false) == na &&
      placeItems(c, d, ctl)) {
    p.single = true;
    p.pages = 1;
    return p;
  }

  // Split: notes stay with the actions if they fit there.
  actionPageRects(v, d, true, ctl);
  p.notesWithActions = nn > 0 && d.ni > 0 && flow(c, d, noteIdx, nn, TOP, ctl[0].visible.y - 6) == nn;
  for (int i = 0; i < nm; i++) p.order[p.n++] = mainIdx[i];
  for (int i = 0; i < na; i++) p.order[p.n++] = afterIdx[i];
  if (!p.notesWithActions)
    for (int i = 0; i < nn; i++) p.order[p.n++] = noteIdx[i];
  int at = 0;
  while (at < p.n && p.contentPages < 7) {
    // As many whole blocks as fit when placed at their best height on the page.
    int fit = p.n - at;
    while (fit > 1 && flow(c, d, p.order + at, fit, TOP, PAGE_BOTTOM) < fit) fit--;
    // A block taller than a page still gets a page; it is drawn cut off and the tests report it.
    p.start[p.contentPages] = at;
    p.count[p.contentPages++] = fit;
    at += fit;
  }
  p.pages = p.contentPages + (d.ni ? 1 : 0);
  return p;
}

int docControls(TFT_eSprite &s, const View &v, const Doc &d, const Plan &p, int page, Control *out, int max) {
  Control ctl[2];
  if (p.single) singleRects(d.ni, ctl, d);
  else if (d.ni && page == p.pages - 1) actionPageRects(v, d, p.notesWithActions, ctl);
  else return 0;
  int n = min(max, d.ni);
  for (int i = 0; i < n; i++) out[i] = ctl[i];
  return n;
}

// ---- Scan-to-join code -------------------------------------------------------------------
// While the setup hotspot is open, its first page is a code for the phone's camera. Before a phone
// has joined, it is the standard Wi-Fi code (iPhone and Android cameras offer to join Drift-Setup);
// once one has, it opens the setup page, for phones that do not pop it up by themselves.
// Largest white square, quiet zone included, with its corners inside the safe circle. Large text
// takes two lines under it, so the code is a little smaller there (3 px modules, about 11 mm).
constexpr int QR_SIDE = 132, QR_SIDE_LARGE = 100;
constexpr int QR_CY = 100, QR_CY_LARGE = 86;
constexpr int QR_QUIET = 2;   // modules of white around the code; phones read 2 reliably on a screen

const char *qrText(const View &v) {
  static char join[64], open[32];
  if (v.screen == Screen::PhoneSync && phonePhase(v) == PhonePhase::Waiting) {
    return "https://the-osprey1.github.io/DriftWatch/";
  }
  if (v.screen != Screen::Setup || v.portal == net::Portal::Saved) return nullptr;
  if (v.portal == net::Portal::Waiting) {
    snprintf(join, sizeof join, "WIFI:T:nopass;S:%s;;", net::SETUP_AP_NAME);
    return join;
  }
  if (v.portal == net::Portal::PhoneJoined) {
    snprintf(open, sizeof open, "http://%s", net::SETUP_ADDRESS);
    return open;
  }
  return nullptr;
}
int qrPages(const View &v) { return qrText(v) ? 1 : 0; }

void drawQrPage(TFT_eSprite &s, const View &v, int pages) {
  uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(4)], temp[qrcodegen_BUFFER_LEN_FOR_VERSION(4)];
  bool joined = v.portal == net::Portal::PhoneJoined;
  if (qrcodegen_encodeText(qrText(v), temp, code, qrcodegen_Ecc_MEDIUM, 1, 4, qrcodegen_Mask_AUTO, true)) {
    int n = qrcodegen_getSize(code), px = (v.large ? QR_SIDE_LARGE : QR_SIDE) / (n + 2 * QR_QUIET);
    int side = px * (n + 2 * QR_QUIET), x0 = CX - side / 2, y0 = (v.large ? QR_CY_LARGE : QR_CY) - side / 2;
    s.fillRect(x0, y0, side, side, TFT_WHITE);
    for (int y = 0; y < n; y++)
      for (int x = 0; x < n; x++)
        if (qrcodegen_getModule(code, x, y))
          s.fillRect(x0 + (x + QR_QUIET) * px, y0 + (y + QR_QUIET) * px, px, px, TFT_BLACK);
  }
  const GFXfont *f = fontFor(Role::Body, v.large);
  if (v.screen == Screen::PhoneSync) {
    if (v.large) {
      gfx::text(s, "Scan to", CX, 160, f, col::TEXT, MC_DATUM);
      gfx::text(s, "open sync", CX, 186, f, col::TEXT, MC_DATUM);
    } else {
      gfx::text(s, "Scan to open sync", CX, 184, f, col::TEXT, MC_DATUM);
    }
  } else if (v.large) {
    gfx::text(s, "Scan to", CX, 160, f, col::TEXT, MC_DATUM);
    gfx::text(s, joined ? "open setup" : "join Wi-Fi", CX, 186, f, col::TEXT, MC_DATUM);
  } else {
    gfx::text(s, joined ? "Scan to open setup" : "Scan to join Wi-Fi", CX, 184, f, col::TEXT, MC_DATUM);
  }
  dots(s, 0, pages);
}

bool drawDoc(TFT_eSprite &s, const View &v, int page) {
  Doc d;
  buildDoc(v, d);
  Plan p = plan(s, v, d);
  int first = qrPages(v);
  if (first && page == 0) {
    drawQrPage(s, v, p.pages + first);
    return false;
  }
  page = constrain(page - first, 0, p.pages - 1);
  Ctx c{s, v, true, false};
  if (v.screen == Screen::Details) handle(s);
  Control ctl[2];
  int nc = docControls(s, v, d, p, page, ctl, 2);
  if (p.single) {
    int main[MAX_BLOCKS], nm = 0, after[MAX_BLOCKS], na = 0;
    for (int i = 0; i < d.nb; i++) (d.blocks[i].place == Place::After ? after[na++] : main[nm++]) = i;
    int actTop = nc ? ctl[0].visible.y : PAGE_BOTTOM, actBottom = nc ? ctl[0].visible.y + ctl[0].visible.h : TOP;
    flow(c, d, main, nm, TOP, actTop - 6);
    flow(c, d, after, na, actBottom + 4, AFTER_BOTTOM, false);
  } else if (page < p.contentPages) {
    flow(c, d, p.order + p.start[page], p.count[page], TOP, PAGE_BOTTOM);
  } else if (p.notesWithActions) {
    int notes[MAX_BLOCKS], nn = 0;
    for (int i = 0; i < d.nb; i++)
      if (d.blocks[i].place == Place::Note) notes[nn++] = i;
    flow(c, d, notes, nn, TOP, ctl[0].visible.y - 6);
  }
  for (int i = 0; i < nc; i++) drawItem(c, d.items[i], ctl[i].visible);
  dots(s, page + first, p.pages + first);
  return c.animated;
}

int drawerPages(const View &v) {
  return v.timerActive || v.timerNotice != timerstore::Notice::None ? 1 : (v.large ? 4 : 2);
}

int drawerControls(const View &v, int page, Control *out, int max) {
  if (v.timerActive || v.timerNotice != timerstore::Notice::None) {
    if (max >= 1) {
      out[0] = {Button::TimerReset, LOWER, LOWER_HIT};
      return 1;
    }
    return 0;
  }
  if (v.large) {
    if (max < 1) return 0;
    Button b = page == 0 ? Button::Timer1m : page == 1 ? Button::Flashlight : page == 2 ? Button::Timer3m : Button::Timer5m;
    out[0] = {b, TALL, TALL};
    return 1;
  }
  int n = 0;
  if (page == 0) {
    if (n < max) out[n++] = {Button::Timer1m, UPPER, UPPER_HIT};
    if (n < max) out[n++] = {Button::Flashlight, LOWER, LOWER_HIT};
  } else {
    if (n < max) out[n++] = {Button::Timer3m, UPPER, UPPER_HIT};
    if (n < max) out[n++] = {Button::Timer5m, LOWER, LOWER_HIT};
  }
  return n;
}

bool drawDrawer(TFT_eSprite &s, const View &v, int page) {
  Ctx c{s, v, true, false};
  handle(s);
  if (!v.large && !v.timerActive && v.timerNotice == timerstore::Notice::None) {
    const GFXfont *f = fontFor(Role::Body, false);
    gfx::text(s, "Quick Tools", CX, 27, f, v.theme.secondary ? v.theme.secondary : col::TEXT_2, MC_DATUM);
  }

  if (v.timerNotice != timerstore::Notice::None && !v.timerActive) {
    gfx::text(s, "Timer", CX, 44, fontFor(Role::Body, v.large), col::TEXT_2, MC_DATUM);
    const char *label = v.timerNotice == timerstore::Notice::Interrupted ? "Interrupted" :
                        v.timerNotice == timerstore::Notice::Missed ? "Missed" : "Save failed";
    gfx::text(s, label, CX, 80, fontFor(Role::Title, false), col::WARN, MC_DATUM);
    if (!v.timerStoreOk && v.timerNotice != timerstore::Notice::StorageError)
      gfx::text(s, "Not saved", CX, 105, fontFor(Role::Body, false), col::WARN, MC_DATUM);
    Control ctl[1];
    Item it{Button::TimerReset, nullptr, Icon::None, 0, "Clear notice", col::WARN, col::WARN, 0};
    if (drawerControls(v, page, ctl, 1)) drawItem(c, it, ctl[0].visible);
    return false;
  }
  if (v.timerActive) {
    int sec = (int)(v.timerRemainingMs / 1000);
    int min = sec / 60;
    sec %= 60;
    char timeStr[16];
    snprintf(timeStr, sizeof timeStr, "%02d:%02d", min, sec);
    gfx::text(s, timeStr, CX, 85, fontFor(Role::Title, true), col::TEXT, MC_DATUM);
    if (!v.timerStoreOk)
      gfx::text(s, "Save failed", CX, 108, fontFor(Role::Body, false), col::WARN, MC_DATUM);
    if (v.timerTotalMs > 0) {
      // The ring empties from 12 o'clock as the time runs down; it moves once a second with the
      // numbers, so the drawer is redrawn by its key (ui_render.cpp) and never animates.
      float left = fminf(1.0f, v.timerRemainingMs / (float)v.timerTotalMs);
      s.drawCircle(CX, CY, 108, col::OUTLINE);
      if (left > 0) gfx::arc(s, 110, 105, 0, 360 * left, col::CYAN);
    }
    Control ctl[2];
    int n = drawerControls(v, page, ctl, 2);
    for (int i = 0; i < n; i++) {
      Item it{Button::TimerReset, nullptr, Icon::None, 0, "Reset Timer", col::WARN, col::WARN, 0};
      drawItem(c, it, ctl[i].visible);
    }
    return false;
  }

  Control ctl[2];
  int n = drawerControls(v, page, ctl, 2);
  for (int i = 0; i < n; i++) {
    Item it{ctl[i].id, nullptr, Icon::None, 0, "", col::TEXT, col::TEXT, 0};
    if (ctl[i].id == Button::Timer1m) { it.label = "Timer"; it.value = "1 Minute"; it.valueColor = col::CYAN; }
    else if (ctl[i].id == Button::Flashlight) { it.label = "Light"; it.value = "Flashlight"; it.valueColor = col::TEXT; }
    else if (ctl[i].id == Button::Timer3m) { it.label = "Timer"; it.value = "3 Minutes"; it.valueColor = col::CYAN; }
    else if (ctl[i].id == Button::Timer5m) { it.label = "Timer"; it.value = "5 Minutes"; it.valueColor = col::CYAN; }
    drawItem(c, it, ctl[i].visible);
  }
  dots(s, page, drawerPages(v), v.theme.primary ? v.theme.primary : col::TEXT, v.theme.outline ? v.theme.outline : col::OUTLINE);
  return c.animated;
}
}  // namespace

// ---- Public ------------------------------------------------------------------------------
Button settingsItem(int i) {
  // Grouped into 4 Category Hubs:
  // Hub 0: Display & Style (items 0-3, pages 0-1)
  // Hub 1: Clock & Sync (items 4-8, pages 2-4; page 4 holds Wi-Fi setup alone)
  // Hub 2: Gestures & Wake (items 9-10, page 5)
  // Hub 3: System & Info (items 11-12, page 6)
  static const Button order[SETTINGS_ITEMS] = {
      Button::Brightness, Button::Night,      Button::Motion,   Button::HotSwap,
      Button::TimeFormat, Button::SetTime,    Button::PhoneSync, Button::SyncNow, Button::WifiSetup,
      Button::RaiseWake,  Button::ScreenTurn,
      Button::TouchCheck, Button::Details};
  return i >= 0 && i < SETTINGS_ITEMS ? order[i] : Button::None;
}

Hub hubOfPage(int page, bool large) {
  if (large) {
    if (page < 4) return Hub::Display;
    if (page < 9) return Hub::Clock;
    if (page < 11) return Hub::Gestures;
    return Hub::System;
  }
  if (page < 2) return Hub::Display;
  if (page < 5) return Hub::Clock;
  if (page < 6) return Hub::Gestures;
  return Hub::System;
}

int hubFirstPage(Hub h, bool large) {
  if (large) {
    switch (h) {
      case Hub::Display:  return 0;
      case Hub::Clock:    return 4;
      case Hub::Gestures: return 9;
      case Hub::System:   return 11;
    }
  } else {
    switch (h) {
      case Hub::Display:  return 0;
      case Hub::Clock:    return 2;
      case Hub::Gestures: return 5;
      case Hub::System:   return 6;
    }
  }
  return 0;
}

const char *hubName(Hub h) {
  switch (h) {
    case Hub::Display:  return "Display";
    case Hub::Clock:    return "Clock";
    case Hub::Gestures: return "Gestures";
    case Hub::System:   return "System";
  }
  return "Settings";
}

Hub nextHub(Hub h) {
  return (Hub)(((uint8_t)h + 1) % HUB_COUNT);
}

Hub prevHub(Hub h) {
  return (Hub)(((uint8_t)h + HUB_COUNT - 1) % HUB_COUNT);
}

const char *buttonName(Button b) {
  static const char *names[] = {"none",     "brightness", "time format", "motion",   "sync now",   "wi-fi setup",
                                "details",  "text size",  "cancel",      "back",     "retry",      "edit wi-fi",
                                "try again", "done",      "set time",    "raise to wake", "minus", "plus", "save time",
                                "phone sync", "night mode", "screen turn", "touch check", "hot swap",
                                "timer 1m", "timer 3m", "timer 5m", "flashlight", "timer reset"};
  int idx = (int)b;
  return idx >= 0 && idx < (int)(sizeof names / sizeof names[0]) ? names[idx] : "unknown";
}

SetupPhase setupPhase(const View &v) {
  if (v.portal == net::Portal::TimedOut) return SetupPhase::TimedOut;
  if (v.portal == net::Portal::Saved) {
    if (running(v)) return SetupPhase::Connecting;
    if (v.result == net::Result::Ok) return SetupPhase::Done;
    if (v.result == net::Result::Failed) return SetupPhase::Failed;
    return SetupPhase::Connecting;  // the sync starts on the next poll
  }
  return SetupPhase::Hotspot;
}

PhonePhase phonePhase(const View &v) {
  if (v.ble == ble::State::Failed) return PhonePhase::Failed;
  if (v.phoneGotTime) return PhonePhase::TimeSet;
  if (v.ble == ble::State::Off) return PhonePhase::Closed;
  if (v.ble == ble::State::Connected) return PhonePhase::Connected;
  return PhonePhase::Waiting;
}

void stepTime(tm &t, TimeField f, int dir) {
  auto wrap = [](int x, int lo, int hi) { int n = hi - lo + 1; return lo + ((x - lo) % n + n) % n; };
  int year = t.tm_year + 1900;
  switch (f) {
    case TimeField::Hour: t.tm_hour = wrap(t.tm_hour + dir, 0, 23); break;
    case TimeField::Minute: t.tm_min = wrap(t.tm_min + dir, 0, 59); break;
    case TimeField::Day: t.tm_mday = wrap(t.tm_mday + dir, 1, daysIn(year, t.tm_mon)); break;
    case TimeField::Month: t.tm_mon = wrap(t.tm_mon + dir, 0, 11); break;
    case TimeField::Year: year = wrap(year + dir, YEAR_MIN, YEAR_MAX); t.tm_year = year - 1900; break;
    default: break;
  }
  year = t.tm_year + 1900;
  t.tm_mday = constrain(t.tm_mday, 1, daysIn(year, t.tm_mon));  // 31 Jan to February becomes 28 or 29 Feb
  t.tm_sec = 0;
  t.tm_wday = weekday(year, t.tm_mon, t.tm_mday);
  t.tm_isdst = -1;
}

int pageCount(TFT_eSprite &s, const View &v) {
  if (v.screen == Screen::Drawer) return drawerPages(v);
  if (v.screen == Screen::Settings) return settingsPages(v);
  if (v.screen == Screen::SetTime) return TIME_PAGES;
  if (v.screen == Screen::TouchCheck) return 1;
  Doc d;
  buildDoc(v, d);
  return plan(s, v, d).pages + qrPages(v);
}

bool draw(TFT_eSprite &s, const View &v) {
  s.fillSprite(col::BG);
  int page = constrain(v.page, 0, pageCount(s, v) - 1);
  if (v.screen == Screen::Drawer) return drawDrawer(s, v, page);
  if (v.screen == Screen::Settings) return drawSettings(s, v, page);
  if (v.screen == Screen::SetTime) return drawSetTime(s, v, page);
  if (v.screen == Screen::TouchCheck) return drawTouchCheck(s, v);
  return drawDoc(s, v, page);
}

int controls(TFT_eSprite &s, const View &v, Control *out, int max) {
  int page = constrain(v.page, 0, pageCount(s, v) - 1);
  if (v.screen == Screen::Drawer) return drawerControls(v, page, out, max);
  if (v.screen == Screen::Settings) return settingsControls(v, page, out, max);
  if (v.screen == Screen::SetTime) return setTimeControls(v, page, out, max);
  if (v.screen == Screen::TouchCheck) return 0;  // taps there are raw positions, not controls
  int first = qrPages(v);
  if (page < first) return 0;  // the scan page has no controls; swipe or BOOT as usual
  Doc d;
  buildDoc(v, d);
  return docControls(s, v, d, plan(s, v, d), page - first, out, max);
}

Button hit(TFT_eSprite &s, const View &v, int x, int y) {
  Control ctl[2];
  int n = controls(s, v, ctl, 2);
  for (int i = 0; i < n; i++)
    if (ctl[i].hit.contains(x, y)) return ctl[i].id;
  return Button::None;
}

int pageOf(TFT_eSprite &s, View v, Button b) {
  if (v.screen == Screen::Settings) {
    for (int i = 0; i < SETTINGS_ITEMS; i++)
      if (settingsItem(i) == b) return settingsSlot(v, i) / settingsPerPage(v);
    return -1;
  }
  int pages = pageCount(s, v);
  for (int p = 0; p < pages; p++) {
    v.page = p;
    Control ctl[2];
    int n = controls(s, v, ctl, 2);
    for (int i = 0; i < n; i++)
      if (ctl[i].id == b) return p;
  }
  return -1;
}

// ---- Low battery notice -------------------------------------------------------------
// A box over the bottom of the face, with its corners inside the safe circle.
constexpr Rect LOW_BOX = {52, 158, 136, 46};

void drawLowBattery(TFT_eSprite &s, int pct, bool urgent) {
  const Rect &r = LOW_BOX;
  s.fillRect(r.x, r.y, r.w, r.h, col::BG);
  s.drawRect(r.x, r.y, r.w, r.h, col::WARN);
  s.drawRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, col::WARN);
  const GFXfont *f = gfx::font(Role::Body, false);
  char line[24];
  snprintf(line, sizeof line, "Battery %d%%", pct < 0 ? 0 : pct);
  constexpr int ICON = 14, GAP = 6;
  int tw = gfx::textWidth(s, line, f), x0 = CX - (ICON + GAP + tw) / 2;
  gfx::iconAlert(s, x0 + ICON / 2, r.y + 14, ICON, col::WARN);
  gfx::text(s, line, x0 + ICON + GAP, r.y + 14, f, col::WARN, ML_DATUM);
  gfx::text(s, urgent ? "Charge now" : "Charge soon", CX, r.y + 33, f, col::TEXT, MC_DATUM);
}

void drawTimerNotice(TFT_eSprite &s, timerstore::Notice notice) {
  constexpr Rect r = {45, 158, 150, 46};
  s.fillRect(r.x, r.y, r.w, r.h, col::BG);
  s.drawRect(r.x, r.y, r.w, r.h, col::WARN);
  const GFXfont *f = gfx::font(Role::Body, false);
  const char *label = notice == timerstore::Notice::Interrupted ? "Timer interrupted" :
                      notice == timerstore::Notice::Missed ? "Timer missed" : "Timer save error";
  gfx::text(s, label, CX, r.y + 14, f, col::WARN, MC_DATUM);
  gfx::text(s, "Open Quick Tools", CX, r.y + 33, f, col::TEXT, MC_DATUM);
}

// ---- Startup ------------------------------------------------------------------
// Cyberpunk chronometer core ignition: singularity shockwaves, cascading 12-hour
// bezel shutter, counter-rotating telemetry rings, split chamfered cyber-rails,
// and a laser-scanned geometric vector wordmark. Shown on cold power-on.
namespace {
void drawDriftWordmark(TFT_eSprite &s, float cy, uint16_t c, float dx = 0.0f, float dy = 0.0f) {
  constexpr float H = 26.0f, SKEW = 0.16f, W_STROKE = 3.5f;
  auto seg = [&](float ox, float x0, float y0, float x1, float y1) {
    float ax = ox + x0 + (H - y0) * SKEW + dx, ay = cy - H * 0.5f + y0 + dy;
    float bx = ox + x1 + (H - y1) * SKEW + dx, by = cy - H * 0.5f + y1 + dy;
    s.drawWideLine(ax, ay, bx, by, W_STROKE, c);
  };
  // D (chamfered sci-fi)
  seg(56.4f, 0, 0, 0, 26);   seg(56.4f, 0, 0, 15, 0);   seg(56.4f, 15, 0, 20, 5);
  seg(56.4f, 20, 5, 20, 21); seg(56.4f, 20, 21, 15, 26); seg(56.4f, 15, 26, 0, 26);
  // R
  seg(85.4f, 0, 0, 0, 26);   seg(85.4f, 0, 0, 15, 0);   seg(85.4f, 15, 0, 20, 5);
  seg(85.4f, 20, 5, 20, 9);  seg(85.4f, 20, 9, 15, 13); seg(85.4f, 15, 13, 0, 13);
  seg(85.4f, 11, 13, 20, 26);
  // I
  seg(114.4f, 1, 0, 9, 0);   seg(114.4f, 5, 0, 5, 26);  seg(114.4f, 1, 26, 9, 26);
  // F
  seg(133.4f, 0, 0, 0, 26);  seg(133.4f, 0, 0, 18, 0);  seg(133.4f, 0, 13, 14, 13);
  // T
  seg(159.4f, 0, 0, 20, 0);  seg(159.4f, 10, 0, 10, 26);
}
}  // namespace

void drawBoot(TFT_eSprite &s, float t, const char *version) {
  s.fillSprite(col::BG);
  auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; };
  auto phaseOut = [&](float start, float len) { return motion::easeOut((t - start) / len); };
  auto phaseIO = [&](float start, float len) { return motion::easeInOut((t - start) / len); };

  // 1. Subtle nocturnal skyline along the lower dial chord (inside r <= 108).
  float skyP = phaseOut(0.22f, 0.45f);
  if (skyP > 0.0f) {
    constexpr int HORIZON = 194;
    uint16_t bldCol = gfx::blend(col::N800, col::BG, skyP);
    uint16_t roofCol = gfx::blend(col::N700, col::BG, skyP);
    uint16_t winCol = gfx::blend(col::WINDOW, col::BG, skyP);
    static const uint8_t widths[] = {10, 13, 9, 14, 11, 15, 10, 12, 11};
    static const uint8_t heights[] = {7, 13, 9, 17, 11, 15, 8, 12, 6};
    int bx = 62;
    for (int i = 0; i < 9; i++) {
      int bw = widths[i], bh = (int)lroundf(heights[i] * skyP);
      if (bh > 1) {
        int top = HORIZON - bh;
        s.fillRect(bx, top, bw, bh, bldCol);
        s.drawFastHLine(bx, top, bw, roofCol);
        if (skyP > 0.55f && (i & 1) == 0 && bh >= 8) {
          s.fillRect(bx + 3, top + 3, 2, 2, winCol);
          if (bw >= 12) s.fillRect(bx + bw - 5, top + 6, 2, 2, winCol);
        }
      }
      bx += bw + 2;
    }
    s.drawFastHLine(58, HORIZON, 124, gfx::blend(col::MAGENTA_DIM, col::BG, skyP));
  }

  // 2. Singularity spark & dual expanding shockwave rings.
  float w1 = phaseOut(0.00f, 0.34f);
  if (w1 > 0.0f) {
    int r1 = (int)lroundf(4.0f + 111.0f * w1);
    float settle = clamp01((t - 0.18f) / 0.22f);
    uint16_t c1 = gfx::blend(col::N600, col::CYAN, settle);
    if (t < 0.12f) c1 = gfx::blend(col::WHITE, col::CYAN, 1.0f - t / 0.12f);
    gfx::arc(s, r1, max(1, r1 - 2), 0.0f, 360.0f, c1);
  }
  float w2 = phaseOut(0.07f, 0.32f);
  float w2Fade = 1.0f - clamp01((t - 0.25f) / 0.17f);
  if (w2 > 0.0f && w2Fade > 0.02f) {
    int r2 = (int)lroundf(2.0f + 104.0f * w2);
    gfx::arc(s, r2, max(1, r2 - 2), 0.0f, 360.0f, gfx::blend(col::MAGENTA, col::BG, w2Fade * 0.85f));
  }
  if (t < 0.22f) {
    float flare = 1.0f - clamp01(t / 0.22f);
    int cr = max(2, (int)lroundf(15.0f * flare));
    s.fillSmoothCircle(CX, CY - 4, cr, gfx::blend(col::CYAN, col::BG, flare * 0.55f), col::BG);
    s.fillSmoothCircle(CX, CY - 4, max(1, cr / 2), gfx::blend(col::WHITE, col::BG, flare), col::BG);
  }

  // 3. Cascading 12-point chronometer bezel shutter.
  for (int i = 0; i < 12; i++) {
    float tp = phaseOut(0.06f + i * 0.022f, 0.14f);
    if (tp <= 0.0f) continue;
    bool cardinal = (i % 3) == 0;
    float rOut = 115.0f;
    float rIn = rOut - (cardinal ? 9.5f : 5.5f) * tp;
    uint16_t target = (i == 0 || i == 6) ? col::CYAN : cardinal ? col::MAGENTA : col::N500;
    uint16_t tc = tp < 0.92f ? gfx::blend(col::WHITE, target, 1.0f - tp) : target;
    gfx::radialBar(s, i * 30.0f, rIn, rOut, cardinal ? 2.4f : 1.4f, tc);
  }

  // 4. Counter-rotating precision telemetry arcs & 12-o'clock reticle.
  float ap = phaseOut(0.12f, 0.48f);
  if (ap > 0.0f) {
    float spin = (1.0f - ap) * 135.0f;
    float halfSpan = 46.0f * ap;
    gfx::arc(s, 102, 99, spin - halfSpan, spin + halfSpan, col::CYAN);
    gfx::arc(s, 102, 99, 180.0f + spin - halfSpan, 180.0f + spin + halfSpan, col::CYAN);
    if (ap < 0.99f) {
      Pt tipA = gfx::polar(spin + halfSpan, 100.5f);
      Pt tipB = gfx::polar(180.0f + spin + halfSpan, 100.5f);
      gfx::dot(s, tipA.x, tipA.y, 2.8f, col::WHITE);
      gfx::dot(s, tipB.x, tipB.y, 2.8f, col::WHITE);
    }
  }
  float mp = phaseOut(0.18f, 0.48f);
  if (mp > 0.0f) {
    float mSpin = -(1.0f - mp) * 120.0f;
    float mHalf = 28.0f * mp;
    uint16_t mCol = gfx::blend(col::MAGENTA, col::MAGENTA_DIM, 0.35f + 0.65f * mp);
    gfx::arc(s, 95, 93, 90.0f + mSpin - mHalf, 90.0f + mSpin + mHalf, mCol);
    gfx::arc(s, 95, 93, 270.0f + mSpin - mHalf, 270.0f + mSpin + mHalf, mCol);
    if (mp < 0.99f) {
      Pt tipL = gfx::polar(90.0f + mSpin - mHalf, 94.0f);
      Pt tipR = gfx::polar(270.0f + mSpin - mHalf, 94.0f);
      gfx::dot(s, tipL.x, tipL.y, 2.2f, col::MAGENTA);
      gfx::dot(s, tipR.x, tipR.y, 2.2f, col::MAGENTA);
    }
  }
  float rp = phaseOut(0.50f, 0.24f);
  if (rp > 0.0f) {
    float dr = 4.0f * rp;
    Pt dia[] = {{(float)CX, 68.0f - dr}, {CX + dr, 68.0f}, {(float)CX, 68.0f + dr}, {CX - dr, 68.0f}};
    gfx::fillPoly(s, dia, 4, rp < 0.9f ? col::WHITE : col::CYAN);
  }

  // 5. Twin split chamfered cyber-rails & laser-scanned geometric "DRIFT" logo.
  constexpr int LOGO_CY = 116;
  float grow = phaseOut(0.22f, 0.24f);
  float split = phaseOut(0.32f, 0.24f);
  float reveal = phaseIO(0.42f, 0.38f);
  int half = (int)lroundf(84.0f * grow);
  int gap = (int)lroundf(23.0f * split);
  int yTop = LOGO_CY - gap, yBot = LOGO_CY + gap;

  if (half > 0) {
    s.fillRect(CX - half, yTop - 1, 2 * half, 2, col::CYAN);
    if (gap > 0) s.fillRect(CX - half, yBot - 1, 2 * half, 2, col::MAGENTA);
    if (split > 0.25f) {
      int wing = (int)lroundf(6.0f * split);
      s.drawWideLine(CX - half, yTop, CX - half - 5, yTop + wing, 2.0f, col::CYAN);
      s.drawWideLine(CX + half, yTop, CX + half + 5, yTop + wing, 2.0f, col::CYAN);
      if (gap > 4) {
        s.drawWideLine(CX - half, yBot, CX - half - 5, yBot - wing, 2.0f, col::MAGENTA);
        s.drawWideLine(CX + half, yBot, CX + half + 5, yBot - wing, 2.0f, col::MAGENTA);
      }
    }
  }

  if (split > 0.55f && reveal > 0.005f) {
    // Chromatic accent offset + crisp Cyber-Yellow geometric wordmark.
    drawDriftWordmark(s, (float)LOGO_CY, reveal < 0.96f ? col::CYAN_DIM : col::MAGENTA_DIM, 2.0f, 1.5f);
    float sheen = (t > 0.78f && t < 0.98f) ? sinf((t - 0.78f) / 0.20f * 3.14159265f) * 0.45f : 0.0f;
    uint16_t wordCol = sheen > 0.01f ? gfx::blend(col::WHITE, col::YELLOW, sheen) : col::YELLOW;
    drawDriftWordmark(s, (float)LOGO_CY, wordCol);

    if (reveal < 0.999f) {
      int scanX = 50 + (int)lroundf(142.0f * reveal);
      int wipeH = max(0, yBot - yTop - 3);
      if (wipeH > 0) {
        // Mask unrevealed portion to the right of the scan beam.
        s.fillRect(scanX, yTop + 2, (CX + 86) - scanX, wipeH, col::BG);
        // Trailing phosphor wash to the left of the laser beam.
        for (int k = 1; k <= 4; k++) {
          int tx = scanX - k * 3;
          if (tx > CX - half) {
            uint16_t glow = gfx::blend(col::CYAN, col::BG, (5 - k) * 0.11f);
            s.drawFastVLine(tx, yTop + 2, wipeH, glow);
          }
        }
        // High-energy laser core + rail contact sparks.
        s.fillRect(scanX - 1, yTop + 1, 3, max(1, yBot - yTop - 1), col::CYAN);
        s.drawFastVLine(scanX, yTop, max(1, yBot - yTop + 1), col::WHITE);
        gfx::dot(s, (float)scanX, (float)yTop, 3.2f, col::WHITE);
        gfx::dot(s, (float)scanX, (float)yBot, 3.2f, col::WHITE);
      }
    }
  }

  // 6. Firmware version readout & flanking neon telemetry pips.
  float vp = phaseOut(0.72f, 0.24f);
  if (vp > 0.05f) {
    int vy = LOGO_CY + gap + 18;
    gfx::text(s, version, CX, vy, type::SMALL, gfx::blend(col::N300, col::BG, vp), MC_DATUM);
    s.setTextFont(type::SMALL);
    int halfW = s.textWidth(version) / 2 + 9;
    gfx::dot(s, (float)(CX - halfW), (float)vy, 1.8f, gfx::blend(col::CYAN, col::BG, vp));
    gfx::dot(s, (float)(CX + halfW), (float)vy, 1.8f, gfx::blend(col::MAGENTA, col::BG, vp));
  }
}

}  // namespace screens
