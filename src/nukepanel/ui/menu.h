// Ported from the Alien Armageddon port's ui/menu.h. Kept: the tab set, the panel box, key capture, the structural
// lengths and the pure scroll and dropdown rules. Dropped: menu_live_count, the Combat-tab label helpers (AA
// features), the numeric entry field (entry.h) and menu_gamecfg_load.
#pragma once
#include "ui.h"
#include "common.h"        // clamp_int, clamp_double_to_int
#include "bind.h"          // bind_menu_key_effective, bind_is_mouse
#include "panel_layout.h"  // PanelBox, for menu_panel_box below
#include "anim.h"          // anim_ease_out, anim_progress: the eased fold
#include <stdio.h>         // _snprintf_s: menu_cap_readout

// AA's tabs in AA's order, less Items (item ESP lives in Visuals), plus Progress before Settings. The tab index is
// session state only; no config key stores it.
enum { TAB_VISUALS = 0, TAB_COMBAT = 1, TAB_WEAPONS = 2, TAB_VIEW = 3, TAB_PROGRESS = 4, TAB_SETTINGS = 5,
       TAB_COUNT = 6 };

// Every tab is drawn (an empty slot in a packed rail read as broken).
inline bool menu_tab_visible(int tab) { return tab >= 0 && tab < TAB_COUNT; }

// The panel's box in LOGICAL pixels, laid out against the logical screen (ui.h's UI scale).
PanelBox menu_panel_box(int screenW, int screenH);
// The same box in DEVICE pixels, at the scale the panel last drew with: the ESP draws at 100% and asks this to
// keep its edge arrows off the panel.
UiRect   menu_panel_rect(int screenW, int screenH);
int      menu_active_tab();
void     menu_set_tab(int tab);
void     menu_draw(int screenW, int screenH);
// The scale the panel settles at on this screen (ui_scale_settled of the stored setting).
int      menu_applied_scale(int screenW, int screenH);

// Wheel movement since the last frame, in notches (positive = pushed away),
// handed in before menu_draw. AA read ImGui's IO here.
void     menu_set_wheel(double notches);

// The key that actually toggles the panel, and the one the status line names. AA's bind_menu_key_effective minus
// mouse buttons: our WndProc toggles on key-downs only and may see no button messages while DirectInput holds the
// mouse exclusively, so a mouse panel key could leave the panel unreachable. Anything outside a byte is Insert too.
inline int panel_key_effective(int vk) {
  if (vk <= 0 || vk > 255) return 0x2D;          // VK_INSERT
  const int e = bind_menu_key_effective(vk);
  return bind_is_mouse(e) ? 0x2D : e;
}

// The panel's pieces of key-capture copy (AA's, verbatim).
const char* const MENU_KEYCAP_PROMPT = "Press a key";
const char* const MENU_KEYCAP_HOVER  = "Click to set";
const char* const MENU_RESET_HINT    = "Reset to default";

// row_planned's tooltip (menu.cpp): every planned-but-missing row uses this
// text verbatim, so a player reads the same thing on every one of them.
const char* const MENU_PLANNED_HINT  = "Not available yet.";

// The tag a refused feature's row shows where its switch would be (menu.cpp's row_switch_avail).
const char* const MENU_UNAVAILABLE_TAG = "Unavailable";

// AA's note row under a key row the game also binds (its MENU_GAMECFG_NOTE), from gamebind.h's live read.
const char* const MENU_GAMECFG_NOTE  = "Also bound in the game";

// The numeric entry field (AA's): a right press on a slider row opens it, typed keys go to it. One field
// for the whole panel; every transition is ui/entry.h's entry_step.
bool menu_entry_open();
// A queued key-down with its modifiers. Returns the text a Ctrl+C/Ctrl+X copied (the caller puts it on
// the clipboard), else null. Ctrl+V is the caller's: it reads the clipboard and calls menu_entry_paste.
const char* menu_entry_key_vk(int vk, bool shift, bool ctrl);
void menu_entry_paste(const char* s);

// Whether a binding row is waiting for a key; panel.cpp asks before routing one.
bool menu_keycap_armed();
// Binds `vk` to the armed row and disarms; refuses what keycap_step refuses.
void menu_keycap_bind(int vk);
// Escape with a row armed: disarms, the old binding stays.
void menu_keycap_cancel();
// The panel closing: disarms without consuming, and resets the session state.
void menu_keycap_close();

// Test hook: a fixed clock for menu_now(); 0 means the real wallclock_ms().
void menu_set_now_ms(unsigned ms);

// True while the panel is closing under animation: the logical state is already
// closed but the open spring has not settled at 0. Always false under ANIM_OFF.
bool menu_closing_animation_active();

// The hint the tooltip drew this frame, or null.
const char* menu_tooltip_text();

// Tab rail width, label padding and entry height (AA's).
const int RAIL_W = 116;
const int RAIL_LABEL_X = 12;
const int MENU_RAIL_ENTRY_H = 46;

// The panel's five structural lengths (AA's).
const int STRIP_H = 15, STATUS_H = 30, ROW_H = 30, ROW_GAP = 4, PAD = 14;
// A group heading's height and the gap under it (AA's menu.cpp constants).
const int GROUP_H = 22, GROUP_GAP = 6;

// Value column's right margin, sized for the widest named readout.
const int VALUE_MARGIN = 10;

// A sub-row's label indent and the x of the rule connecting it to its parent.
const int SUB_RULE_X = 3, SUB_INDENT = 21;

// --- The slider row's geometry ----------------------------------------------
const int TRACK_GAP        = 8;
const int TRACK_MAX_W      = 120;
const int TRACK_MIN_W      = 40;

// 24 logical pixels reserved between a slider's track and its readout on every
// row, so the reset glyph never sits on top of the drag target. A named slider
// has no glyph but keeps the slot, so its track lines up with the rest.
const int RESET_W          = 20;
const int RESET_RESERVE    = 24;

// The numeric entry field (ui/entry.h): its well fits the longest int with FIELD_PAD each side, right-aligned on the
// row. While it is open it covers the readout column, the reset glyph's slot (the glyph hides) and VALUE_MARGIN,
// never the track: tb_0340.
const int FIELD_PAD = 8;
const char* const MENU_ENTRY_WIDEST = "-2147483648";

// The widest readout any slider shows (tb_0339 checks every row's): the readout column is this wide on every row.
const char* const MENU_READOUT_RESERVE = "Crosshair";

// The leftmost a track may start: past the widest slider label, "Double jump strength" as a sub-row (21 + 204),
// and TRACK_GAP. tb_0339 checks every label against it.
const int MENU_SLIDER_TRACK_MIN_X = 233;

// Where a slider row's pieces sit, in logical pixels from the row's left edge.
struct MenuSliderGeom { int trackX, trackW, glyphX, readoutX, readoutW; };

// One geometry for every slider row: it depends on the row's width and the panel's readout column only, never on the
// row's own readout or indent. From the right: readout column, TRACK_GAP, the reset glyph's slot, then the track, as
// wide as fits right of MENU_SLIDER_TRACK_MIN_X within [TRACK_MIN_W, TRACK_MAX_W].
inline MenuSliderGeom menu_slider_geom(int rowW, int readoutW) {
  MenuSliderGeom g;
  g.readoutW = readoutW;
  g.readoutX = rowW - VALUE_MARGIN - readoutW;
  const int trackRight = g.readoutX - TRACK_GAP - RESET_RESERVE;
  g.trackW = clamp_int(trackRight - MENU_SLIDER_TRACK_MIN_X, TRACK_MIN_W, TRACK_MAX_W);
  g.trackX = trackRight - g.trackW;
  g.glyphX = trackRight + (RESET_RESERVE - RESET_W) / 2;
  return g;
}

// The Cap row's readout: always "N fps"; following the monitor is what the reset arrow returns to, and the hint
// says so (a unit that changed on the first drag read as a bug).
inline void menu_cap_readout(char* out, int cap, int fps, int following) {
  (void)following;
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "%d fps", fps);
}

// --- The UI scale (ui.h) on this panel ---------------------------------------

// The least the panel draws at: the hazard strip, the status bar and the six-tab rail, with the bevel's top and
// bottom pixel. The body beside the rail then holds six content rows. 323 px.
const int PANEL_CHROME_MIN_H = 1 + STRIP_H + STATUS_H + TAB_COUNT * MENU_RAIL_ENTRY_H + 1;

// The UI scale row moves in steps of 5: the nearest step inside [UI_SCALE_MIN, UI_SCALE_MAX].
inline int ui_scale_step(int pct) { return ui_scale_clamped((clamp_int(pct, 0, 1000) + 2) / 5 * 5); }

// The scale the panel settles at: the stored setting, capped by this screen's ceiling (AA's menu_applied_scale).
// The stored value is not lowered: a taller screen gives the player their number back.
inline int ui_scale_settled(int stored, int ceiling) {
  const int s = ui_scale_step(stored);
  return s < ceiling ? s : ceiling;
}

// The UI scale row's ceiling: the largest scale at which the least panel fits this screen, PANEL_CHROME_MIN_H tall
// with PANEL_VINSET above and below, and PANEL_MIN_W wide. 200% on any screen 766 px tall or more (674 with five
// tabs). AA's rule (the stored height, then PANEL_MIN_H 790 here) stopped at 165% on 1440 and 125% on 1080.
inline int ui_scale_ceiling(int screenW, int screenH) {
  const int byH = ui_scale_max_for(screenH, PANEL_CHROME_MIN_H, PANEL_VINSET);
  const int byW = ui_scale_max_for(screenW, PANEL_MIN_W, 0);
  return byH < byW ? byH : byW;
}

// The tallest the panel draws (and can be resized to) at `pct`: AA's D21 cap, floored at the chrome. The stored
// height is not lowered, so returning to a smaller scale gives the player their panel back.
inline int ui_panel_h_cap(int screenH, int pct) {
  const int cap = ui_panel_max_h_for(screenH, pct, PANEL_VINSET);
  return cap < PANEL_CHROME_MIN_H ? PANEL_CHROME_MIN_H : cap;
}

// Where the stored box draws on a logical screen at a scale: panel_layout with the height capped at `hCap`, as the
// drag lays out (menu.cpp). Under a cap below PANEL_MIN_H the height is the cap, so a resize changes the width only.
// Width needs no cap: panel_layout keeps it on the logical screen, which the ceiling keeps at PANEL_MIN_W or wider.
inline PanelBox menu_panel_drawn(PanelBox stored, int logicalW, int logicalH, int hCap) {
  return panel_layout(stored, logicalW, logicalH, hCap);
}

// --- The tab body scrolls (AA's rules, verbatim) ---------------------------

// One wheel notch: the conventional three lines, and a row is the only line.
const int MENU_SCROLL_STEP = 3 * (ROW_H + ROW_GAP);

// The height a tab's content is laid out into, far taller than any panel, so
// the content is measured whole and the offset's limit is contentH - viewH.
const int MENU_CONTENT_BUDGET = 4096;

const int MENU_SCROLLBAR_W = 5;
const int MENU_SCROLLBAR_MIN_THUMB = 20;

// Where menu_draw parks the logical cursor while the content draws with the
// real cursor outside the body: the clip stops a scrolled-out row being drawn
// there, this stops one being clicked there.
const int MENU_MOUSE_NOWHERE = -1000000;

inline int menu_scroll_max(int contentH, int viewH) {
  const int over = contentH - viewH;
  return over > 0 ? over : 0;
}

inline int menu_scroll_clamp(int offset, int contentH, int viewH) {
  return clamp_int(offset, 0, menu_scroll_max(contentH, viewH));
}

// A positive wheel is pushed away from the hand and walks back up the tab.
inline int menu_scroll_after_wheel(int offset, double wheel, int contentH, int viewH) {
  const double moved = (double)offset - wheel * (double)MENU_SCROLL_STEP;
  return menu_scroll_clamp(clamp_double_to_int(moved >= 0.0 ? moved + 0.5 : moved - 0.5),
                           contentH, viewH);
}

inline int menu_scroll_thumb_h(int trackH, int contentH, int viewH) {
  if (trackH <= 0 || contentH <= 0 || viewH <= 0) return 0;
  if (viewH >= contentH) return trackH;
  long long h = ((long long)trackH * (long long)viewH) / (long long)contentH;
  if (h < MENU_SCROLLBAR_MIN_THUMB) h = MENU_SCROLLBAR_MIN_THUMB;
  if (h > trackH) h = trackH;
  return (int)h;
}

// From the travel, so the thumb sits flush with the bottom at the last row.
inline int menu_scroll_thumb_y(int trackH, int thumbH, int offset,
                               int contentH, int viewH) {
  const int maxOff = menu_scroll_max(contentH, viewH);
  const int travel = trackH - thumbH;
  if (maxOff <= 0 || travel <= 0) return 0;
  const int off = clamp_int(offset, 0, maxOff);
  return (int)(((long long)travel * (long long)off + maxOff / 2) / (long long)maxOff);
}

// A sub-row block that collapses while its parent is off (panel clean-up 2026-10-02): collapsed, its rows are
// not laid out, so the tab's content height and scroll range shrink with it; either way it ends at the same
// row ordinal, so every row below keeps its animation slot and row key. `rows` is the block's ordinal count.
struct MenuCollapse { int shown; int ordinalEnd; };
inline MenuCollapse menu_collapse_begin(int parentOn, int ordinal, int rows) {
  MenuCollapse c = { parentOn != 0, ordinal + rows };
  return c;
}
inline int menu_collapse_end(const MenuCollapse& c, int ordinalNow) {
  if (!c.shown) return c.ordinalEnd;
  return ordinalNow > c.ordinalEnd ? ordinalNow : c.ordinalEnd;
}

// The eased fold: a block slides open or shut over MENU_FOLD_MS when its parent flips, its
// rows clipped to the eased height and faded. A flip mid-flight turns back from where the block is. Not
// animating (Panel animations off, a tab switch, a block not drawn last frame) pops, as before.
const unsigned MENU_FOLD_MS = 150;
struct MenuFold { int open; unsigned t0; double from; };

inline MenuFold menu_fold_init(int open) {
  MenuFold f = { open != 0, 0u, open != 0 ? 1.0 : 0.0 };
  return f;
}
// The shown fraction of the block's height, 0 shut and 1 open.
inline double menu_fold_frac(const MenuFold& f, unsigned now) {
  const double to = f.open ? 1.0 : 0.0;
  return f.from + (to - f.from) * anim_ease_out(anim_progress(f.t0, now, MENU_FOLD_MS));
}
inline MenuFold menu_fold_step(MenuFold f, int parentOn, unsigned now, int animate) {
  const int want = parentOn != 0;
  if (!animate) return menu_fold_init(want);
  if (want != f.open) { f.from = menu_fold_frac(f, now); f.open = want; f.t0 = now; }
  return f;
}
inline bool menu_fold_animating(const MenuFold& f, unsigned now) {
  return menu_fold_frac(f, now) != (f.open ? 1.0 : 0.0);
}
// Its rows are laid out while it is open, opening or still shutting.
inline bool menu_fold_laid_out(const MenuFold& f, unsigned now) {
  return f.open || menu_fold_frac(f, now) > 0.0;
}
inline int menu_fold_height(int fullH, double frac) {
  if (fullH <= 0 || !(frac > 0.0)) return 0;
  if (frac >= 1.0) return fullH;
  return clamp_int((int)(fullH * frac + 0.5), 0, fullH);
}
// Hit-testing follows the drawn rows: none while shutting, only the part already shown while opening.
inline bool menu_fold_hit_live(const MenuFold& f, unsigned now, bool inShownPart) {
  if (!menu_fold_animating(f, now)) return true;
  return f.open && inShownPart;
}

inline bool menu_scrollbar_visible(int contentH, int viewH) {
  return menu_scroll_max(contentH, viewH) > 0;
}

// The scroll cue: a slim thumb on the body's right edge and
// a fade over the bottom edge while content lies below, eased out over the last MENU_SCROLL_FADE_H pixels so it
// never pops. Drawn only on overflow; it takes no input, the wheel still scrolls.
const int MENU_SCROLL_CUE_W = 3;
const int MENU_SCROLL_FADE_H = 24;
struct MenuScrollCue { int shown; int thumbY; int thumbH; double fade; };

inline MenuScrollCue menu_scroll_cue(int trackH, int offset, int contentH, int viewH) {
  MenuScrollCue c = { 0, 0, 0, 0.0 };
  if (trackH <= 0 || !menu_scrollbar_visible(contentH, viewH)) return c;
  c.shown = 1;
  c.thumbH = menu_scroll_thumb_h(trackH, contentH, viewH);
  c.thumbY = menu_scroll_thumb_y(trackH, c.thumbH, offset, contentH, viewH);
  const int maxOff = menu_scroll_max(contentH, viewH);
  const int below = maxOff - clamp_int(offset, 0, maxOff);
  c.fade = below >= MENU_SCROLL_FADE_H ? 1.0 : (double)below / (double)MENU_SCROLL_FADE_H;
  return c;
}

// Whether `inner` is fully inside (or flush with) `outer`.
inline bool ui_rect_inside(const UiRect& inner, const UiRect& outer) {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.w <= outer.x + outer.w &&
         inner.y + inner.h <= outer.y + outer.h;
}

// A dropdown's open/owner pair and AA's per-row step (AA10b): `*seen` comes
// from the RETURNED state, so the opening frame is not mistaken for an
// unowned one.
struct MenuDdState { int open; int rowKey; };

inline MenuDdState menu_dd_row_step(MenuDdState s, int key, bool hot,
                                     bool pressed, bool boxInside, bool* seen) {
  const bool mine = s.open != 0 && s.rowKey == key;
  MenuDdState out = s;
  if (hot && pressed) {
    if (mine) { out.open = 0; out.rowKey = -1; }
    else { out.open = 1; out.rowKey = key; }
  } else if (mine && !boxInside) {
    out.open = 0; out.rowKey = -1;
  }
  if (seen) *seen = out.open != 0 && out.rowKey == key;
  return out;
}
