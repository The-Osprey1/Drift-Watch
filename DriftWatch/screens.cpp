// Accent hierarchy: neutral text and outlines, cyan for progress and pressed
// controls, magenta only for the Wi-Fi setup identity, orange for faults (always
// with an icon and words). Text uses bold bitmap fonts in two roles (theme.h);
// large text swaps both roles one size up and lets the layout split pages
// instead of shrinking anything.
#include "screens.h"
#include <stdarg.h>
#include "gfx.h"
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

struct Doc {
  Block blocks[10];
  int nb = 0;
  Item items[2];
  int ni = 0;
  char text[8][72];
  int nt = 0;
  const char *fmt(const char *f, ...) {
    char *out = text[nt < 7 ? nt++ : 7];
    va_list ap;
    va_start(ap, f);
    vsnprintf(out, sizeof text[0], f, ap);
    va_end(ap);
    return out;
  }
  void add(const char *t, Role r, uint16_t c, Icon i = Icon::None, Place p = Place::Main) {
    if (nb < 10) blocks[nb++] = {t, r, c, i, p};
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
    if (pressed) gfx::cutBox(c.s, r.x, r.y, r.w, r.h, CUT, col::CYAN_DEEP, true);
    uint16_t edge = pressed ? col::CYAN : it.edge;
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

void dots(TFT_eSprite &s, int page, int pages) {
  if (pages < 2) return;
  int gap = 12, x0 = CX - (pages - 1) * gap / 2;
  for (int i = 0; i < pages; i++) {
    if (i == page) s.fillSmoothCircle(x0 + i * gap, DOTS_Y, 3, col::TEXT, col::BG);
    else s.drawCircle(x0 + i * gap, DOTS_Y, 3, col::OUTLINE);
  }
}

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
    case Button::Motion:
      it.label = "Motion";
      it.value = v.reducedMotion ? "Reduced" : "Full";
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
      it.label = v.imuPresent ? "Raise to wake" : "No sensor";
      it.value = v.raiseToWake && v.imuPresent ? "On" : "Off";
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
    default:
      break;
  }
  return it;
}

int settingsPerPage(const View &v) { return v.large ? 1 : 2; }
int settingsPages(const View &v) { return SETTINGS_ITEMS / settingsPerPage(v); }

int settingsControls(const View &v, int page, Control *out, int max) {
  int per = settingsPerPage(v), n = 0;
  for (int i = 0; i < per && n < max; i++) {
    Button b = settingsItem(page * per + i);
    if (v.large) out[n++] = {b, TALL, TALL};
    else out[n++] = {b, i == 0 ? UPPER : LOWER, i == 0 ? UPPER_HIT : LOWER_HIT};
  }
  return n;
}

bool drawSettings(TFT_eSprite &s, const View &v, int page) {
  Ctx c{s, v, true, false};
  handle(s);
  if (!v.large) {
    // Compact header; with large text the space goes to the control instead.
    const GFXfont *f = fontFor(Role::Body, false);
    gfx::text(s, "Settings", CX, 27, f, col::TEXT_2, MC_DATUM);
  }
  Control ctl[2];
  int n = settingsControls(v, page, ctl, 2);
  for (int i = 0; i < n; i++) {
    Doc d;
    drawItem(c, settingsItemFor(v, ctl[i].id, d), ctl[i].visible);
  }
  dots(s, page, settingsPages(v));
  return c.animated;
}

// ---- Set time -------------------------------------------------------------------------------
// One field per page with - and + side by side, the whole time or date above them with the
// field being set in cyan and underlined, then a page that shows the result and saves it.
const char *const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *const DAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
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
  if (v.batteryPct >= 0) d.add(d.fmt("Battery %d%%, %d.%02d V", v.batteryPct, v.batteryMv / 1000, v.batteryMv % 1000 / 10), Role::Body, col::TEXT);
  else d.add("Battery: no reading", Role::Body, col::TEXT_2);
  if (!v.timeValid) d.add("Time not set", Role::Body, col::WARN, Icon::Alert);
  if (running(v)) d.add(d.fmt("Syncing: %s, step %d of 3", stageText(v.stage), v.syncStep), Role::Body, col::CYAN, Icon::Busy);
  if (v.clockByHand && v.timeValid) d.add("Clock set by hand", Role::Body, col::TEXT);
  if (v.result == net::Result::Failed && v.fail == net::Fail::JoinFailed) {
    d.add(d.fmt("Last sync failed: %s", joinWords(v.joinWhy)), Role::Body, col::WARN, Icon::Alert);
    d.add(joinHint(v.joinWhy), Role::Body, col::TEXT_2);
    if (v.joinCode) d.add(d.fmt("Wi-Fi reason %d", v.joinCode), Role::Body, col::TEXT_2);
  } else if (v.result == net::Result::Failed) {
    d.add(d.fmt("Last sync failed: %s", failText(v.fail)), Role::Body, col::WARN, Icon::Alert);
    d.add(failHint(v.fail), Role::Body, col::TEXT_2);
  } else if (v.lastSyncText[0]) {
    d.add(d.fmt("Last sync %s", v.lastSyncText), Role::Body, col::TEXT, Icon::Check);
  } else if (!running(v)) {
    d.add("Not synced yet", Role::Body, col::TEXT_2);
  }
  if (v.zone[0]) d.add(d.fmt("Time zone %s", v.zone), Role::Body, col::TEXT);
  else d.add(v.zoneManual ? "Time zone typed in setup" : "Time zone not found yet", Role::Body, col::TEXT_2);
  d.add(v.ssid[0] ? d.fmt("Wi-Fi %s", v.ssid) : "No Wi-Fi saved", Role::Body, v.ssid[0] ? col::TEXT : col::TEXT_2);
  d.add(v.imuPresent ? "Motion sensor found" : "Motion sensor not found", Role::Body, col::TEXT_2);
  d.add(d.fmt("Firmware %s", v.version), Role::Body, col::TEXT_2);
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

void buildDoc(const View &v, Doc &d) {
  if (v.screen == Screen::Details) detailsDoc(v, d);
  else setupDoc(v, d);
}

// Page plan for a document: everything on one page if it fits, otherwise
// content pages followed by a page for the actions.
struct Plan {
  bool single;
  int pages;
  bool notesWithActions;
  int order[10], n;           // blocks in content-page order
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
  int mainIdx[10], nm = 0, afterIdx[10], na = 0, noteIdx[10], nn = 0;
  for (int i = 0; i < d.nb; i++) {
    if (d.blocks[i].place == Place::After) afterIdx[na++] = i;
    else if (d.blocks[i].place == Place::Note) noteIdx[nn++] = i;
    else mainIdx[nm++] = i;
  }

  // One page: main and note text above the actions, after text below them.
  Control ctl[2];
  singleRects(d.ni, ctl, d);
  int actTop = d.ni ? ctl[0].visible.y : PAGE_BOTTOM, actBottom = d.ni ? ctl[0].visible.y + ctl[0].visible.h : TOP;
  int above[10], nab = 0;
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

bool drawDoc(TFT_eSprite &s, const View &v, int page) {
  Doc d;
  buildDoc(v, d);
  Plan p = plan(s, v, d);
  page = constrain(page, 0, p.pages - 1);
  Ctx c{s, v, true, false};
  if (v.screen == Screen::Details) handle(s);
  Control ctl[2];
  int nc = docControls(s, v, d, p, page, ctl, 2);
  if (p.single) {
    int main[10], nm = 0, after[10], na = 0;
    for (int i = 0; i < d.nb; i++) (d.blocks[i].place == Place::After ? after[na++] : main[nm++]) = i;
    int actTop = nc ? ctl[0].visible.y : PAGE_BOTTOM, actBottom = nc ? ctl[0].visible.y + ctl[0].visible.h : TOP;
    flow(c, d, main, nm, TOP, actTop - 6);
    flow(c, d, after, na, actBottom + 4, AFTER_BOTTOM, false);
  } else if (page < p.contentPages) {
    flow(c, d, p.order + p.start[page], p.count[page], TOP, PAGE_BOTTOM);
  } else if (p.notesWithActions) {
    int notes[10], nn = 0;
    for (int i = 0; i < d.nb; i++)
      if (d.blocks[i].place == Place::Note) notes[nn++] = i;
    flow(c, d, notes, nn, TOP, ctl[0].visible.y - 6);
  }
  for (int i = 0; i < nc; i++) drawItem(c, d.items[i], ctl[i].visible);
  dots(s, page, p.pages);
  return c.animated;
}
}  // namespace

// ---- Public ------------------------------------------------------------------------------
Button settingsItem(int i) {
  // Setting the clock and syncing it share a page, so a watch with no time opens on both fixes.
  static const Button order[SETTINGS_ITEMS] = {Button::Brightness, Button::TimeFormat, Button::SetTime,
                                                Button::SyncNow,    Button::RaiseWake,  Button::Motion,
                                                Button::WifiSetup,  Button::Details};
  return i >= 0 && i < SETTINGS_ITEMS ? order[i] : Button::None;
}

const char *buttonName(Button b) {
  static const char *names[] = {"none",     "brightness", "time format", "motion",   "sync now",   "wi-fi setup",
                                "details",  "text size",  "cancel",      "back",     "retry",      "edit wi-fi",
                                "try again", "done",      "set time",    "raise to wake", "minus", "plus", "save time"};
  return names[(int)b];
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
  if (v.screen == Screen::Settings) return settingsPages(v);
  if (v.screen == Screen::SetTime) return TIME_PAGES;
  Doc d;
  buildDoc(v, d);
  return plan(s, v, d).pages;
}

bool draw(TFT_eSprite &s, const View &v) {
  s.fillSprite(col::BG);
  int page = constrain(v.page, 0, pageCount(s, v) - 1);
  if (v.screen == Screen::Settings) return drawSettings(s, v, page);
  if (v.screen == Screen::SetTime) return drawSetTime(s, v, page);
  return drawDoc(s, v, page);
}

int controls(TFT_eSprite &s, const View &v, Control *out, int max) {
  int page = constrain(v.page, 0, pageCount(s, v) - 1);
  if (v.screen == Screen::Settings) return settingsControls(v, page, out, max);
  if (v.screen == Screen::SetTime) return setTimeControls(v, page, out, max);
  Doc d;
  buildDoc(v, d);
  return docControls(s, v, d, plan(s, v, d), page, out, max);
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
      if (settingsItem(i) == b) return i / settingsPerPage(v);
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
