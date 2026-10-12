// The Nuke Panel, ported from the Alien Armageddon port's ekmod/ui/menu.cpp: same panel, fewer rows. Chrome, drag and
// resize, tab rail, row widgets, springs, tooltips, scrolling and key capture are AA's; every AA feature row is gone
// and a tab draws only once it has a row of ours. The Glass style (which AA never drew) is dropped.
#include <windows.h>   // 64-bit interlocked exchange, for the commit slot
#include "menu.h"
#include "panel_layout.h"
#include "canvas.h"
#include "font.h"
#include "anim.h"
#include "wallclock.h"
#include "tooltip.h"
#include "tipwrap.h"      // the hint wrapped to TIP_MAX_W
#include "bind.h"
#include "entry.h"
#include "input.h"        // ui/input.h: g_ui, filled once a frame
#include "../config.h"    // g_cfg, g_cfgDirty, config_bind_get/set
#include "../log.h"
#include "../input.h"     // input_panel_open: the logical open state
#include "../framecap.h"  // framecap_effective_fps: the Cap row's display value
#include "../dmgnum.h"    // dmgnum_armed: the Damage numbers hint
#include "../weapons.h"   // the Weapons tab stops and value text
#include "../aim.h"       // the Combat TARGETING labels and ranges
#include "../offscreen.h" // offscreen_refused: the Off-screen targets row
#include "../display.h"   // display_note: the Borderless window row
#include "../gamebind.h"  // gamebind_vk_bound: the "Also bound in the game" note
#include "../zoom.h"      // the FIELD OF VIEW rows
#include "../wheelzoom.h" // wheelzoom_reset_distance: the Distance row's reset
#include "../cutaway.h"   // cutaway_refused: the Cut-away walls row
#include "../mouseaim.h"  // the MOUSE LOOK rows and the reticle preview
#include "../moveanywhere.h"  // moveanywhere_refused
#include "../freecam.h"   // freecam_bind_refused: the camera's keys refuse the movement keys
#include "../photo.h"     // the Photo size range and its pixel-size line
#include "../photofolder.h"  // the Photo folder row
#include "../progress.h"  // the Progress tab's view, names and layout
#include "../areas.h"     // the Progress tab's This level section
#include "../areamap.h"   // its schematic map's layout and pip rows
#include "../gamespeed.h" // g_gameSpeedOn: the Game speed switch
#include "../speedrule.h" // the Speed slider's range and steps
#include <math.h>
#include <stdio.h>
#include <string.h>

// The first visible tab (menu_tab_visible).
static int s_tab = TAB_VISUALS;

// --- Where each tab is scrolled to ----------------------------------------
// Session state, not a setting: cleared at menu_draw's open edge. s_contentH, each tab's last measured height,
// survives a close so the first wheel notch of a reopened panel clamps properly.
static int s_scroll[TAB_COUNT]   = {};
static int s_contentH[TAB_COUNT] = {};
static double s_wheel = 0.0;

void menu_set_wheel(double notches) { s_wheel += notches; }

// --- The key capture ------------------------------------------------------
// One armed row for the whole panel; every transition is keycap_step's (bind.h).
static void menu_note_new_session();
static void menu_entry_close();
static int field_text_y(const UiRect& field);

static KeyCapState s_keycap = { KEYCAP_IDLE, -1 };

bool menu_keycap_armed() { return s_keycap.phase == KEYCAP_ARMED; }

void menu_keycap_bind(int vk) {
  KeyCapEvent e;
  e.kind = bind_is_mouse(vk) ? KEYCAP_EV_MOUSE : KEYCAP_EV_KEY;
  e.row = -1;
  e.vk = vk;
  const KeyCapResult r = keycap_step(s_keycap, e);
  s_keycap = r.state;
  // The camera's keys refuse a fixed movement key (freecam_bind_refused): the binding stays as it was.
  if (r.boundRow >= 0 && freecam_bind_refused(r.boundRow, r.boundVk)) {
    np_log("menu: row %d refused 0x%02X: a free camera movement key", r.boundRow, r.boundVk);
    return;
  }
  if (r.boundRow >= 0) config_bind_set(r.boundRow, r.boundVk);   // raises g_cfgDirty itself
}

void menu_keycap_cancel() {
  KeyCapEvent e;
  e.kind = KEYCAP_EV_ESCAPE;
  e.row = -1;
  e.vk = 0;
  s_keycap = keycap_step(s_keycap, e).state;
}

void menu_keycap_close() {
  // A capture left armed across a close would eat the first key back in the game.
  KeyCapEvent e;
  e.kind = KEYCAP_EV_CLOSE;
  e.row = -1;
  e.vk = 0;
  s_keycap = keycap_step(s_keycap, e).state;
  menu_entry_close();
  menu_note_new_session();
}

// --- The numeric entry field (AA's menu.cpp:180-740, ui/entry.h) ------------
// The store only: every transition is entry_step's. One field for the whole panel, like the key capture.
static EntryState s_entry = entry_begin();
// The left button went down inside the field's well and is still down: later frames drag the selection.
static bool s_entryCaretDrag = false;
// The field's drawn box, for telling a press inside the well from one outside (the click-away).
static UiRect s_entryFieldRect = { 0, 0, 0, 0 };
// row_slider_at drew the open field this frame; a tab switch or dropped row closes it by commit.
static bool s_entrySeenThisFrame = false;
static char s_entryClip[ENTRY_TEXT_CAP + 1] = { 0 };
// The cursor x before the body parks it, for a caret drag that leaves the panel.
static int s_frameMx = 0;

// --- The panel moves and resizes ---------------------------------------
static PanelDrag s_drag = { PDRAG_IDLE, 0, 0, { 0, 0, 0, 0 } };
static PanelBox  s_dragBox = { 0, 0, 0, 0 };
static bool      s_dragLive = false;

// A press a glyph or list consumed, latched for the whole hold (AA).
static bool      s_pressSpent = false;

// The slider row drawn last had its reset arrow pressed this frame (read by a row with a second value).
static bool      s_resetPressedThisRow = false;

// A press some control took this frame; an unclaimed one moves the panel.
static bool      s_pressClaimed = false;

static PanelBox panel_stored_box() {
  PanelBox b = { g_cfg.panelx, g_cfg.panely, g_cfg.panelw, g_cfg.panelh };
  return b;
}

// The stored box as it draws at `pct` on the logical screen: its height capped so the whole panel fits the screen
// at that scale (menu_panel_drawn). Drawing is not a write.
static PanelBox panel_drawn_box(int screenH, int pct, int logicalW, int logicalH) {
  return menu_panel_drawn(panel_stored_box(), logicalW, logicalH, ui_panel_h_cap(screenH, pct));
}

int menu_applied_scale(int screenW, int screenH) {
  return ui_scale_settled(g_cfg.uiScale, ui_scale_ceiling(screenW, screenH));
}

// The scale held for the whole of a press (ui_scale_effective), and the one the
// canvas actually drew the last panel at (100 when the scaled atlas was refused).
static int s_scaleHeld = UI_SCALE_DEFAULT;
static int s_drawnPct = UI_SCALE_DEFAULT;

PanelBox menu_panel_box(int screenW, int screenH) {
  const int logicalW = ui_unscale_px(screenW, s_drawnPct);
  const int logicalH = ui_unscale_px(screenH, s_drawnPct);
  const PanelBox laid = panel_drawn_box(screenH, s_drawnPct, logicalW, logicalH);
  return s_dragLive ? s_dragBox : laid;
}

UiRect menu_panel_rect(int screenW, int screenH) {
  const PanelBox b = menu_panel_box(screenW, screenH);
  const int x0 = ui_scale_px(b.x, s_drawnPct), y0 = ui_scale_px(b.y, s_drawnPct);
  return UiRect{ x0, y0, ui_scale_px(b.x + b.w, s_drawnPct) - x0, ui_scale_px(b.y + b.h, s_drawnPct) - y0 };
}

int  menu_active_tab() { return s_tab; }
void menu_set_tab(int tab) { if (tab >= 0 && tab < TAB_COUNT && menu_tab_visible(tab)) s_tab = tab; }

// AA's names and accents, verbatim, and Progress in its own teal.
static const char* TAB_NAMES[TAB_COUNT] = { "Visuals", "Combat", "Weapons", "View", "Progress", "Settings" };
static const unsigned TAB_COLOURS[TAB_COUNT] = { C_BLUE, C_RED, C_HAZARD, C_GREEN, C_PROGRESS, C_SETTINGS };

// Off and Simple: AA's config carries a 0/1 switch, and Glass was never drawn.
static int anim_style() { return g_cfg.animations ? ANIM_SIMPLE : ANIM_OFF; }

// --- The presentation state --------------------------------------------
// Cosmetic only: hit tests use the logical rect ui_row hands back, so a click mid-transition lands where the control
// is going. Arrays are indexed by row ordinal this frame; a tab switch snaps them.
struct MenuPresentation {
  unsigned lastMs;
  AnimSpring open;
  int openTarget;
  int wasOpen;
  unsigned tabSwitchMs;
  int tabFrom;
  AnimSpring knob[64];
  AnimSpring fill[64];
  double hover[64];
  double resetHover[64];
  int rowsThisFrame;
};
static MenuPresentation s_pres;

static const int ANIM_ROW_SLOTS = 64;
static bool s_tabSwitchedThisFrame = false;
static double s_frameDt = 0.0;

// The eased fold's frame state (collapse_begin): a frame count to tell a block drawn last frame from one
// coming back, a snap for a tab switch or a new session, the clip rows are drawn under (canvas_push_clip
// replaces rather than intersects) and the product of the enclosing folds' fades.
static unsigned s_frameNo = 0;
static bool s_foldSnap = false;
static UiRect s_clipNow = { 0, 0, 0, 0 };
static double s_foldAlpha = 1.0;

// --- Hints as tooltips --------------------------------------------------
// row_hint attaches to the row drawn just before it; the key is
// s_tab * 1000 + ordinal. The hint is copied: it may be a stack buffer.
static UiRect s_lastRow = { 0, 0, 0, 0 };
static int s_lastRowKey = -1;
static int s_hoverRowKey = -1;
static UiRect s_bodyRect = { 0, 0, 0, 0 };
// A row that ran out of layout budget and drew nothing: logged once a session.
static int s_rowsDropped = 0;
static bool s_rowsDroppedLogged = false;
static char s_hintBuf[256];   // the longest hints (an area row's) run past 96
static const char* s_hoverHint = nullptr;
static const char* s_tooltipText = nullptr;
static TooltipState s_tooltip = tooltip_begin();
static const int TOOLTIP_R = 4;
static const char* s_glyphHint = nullptr;

// What a panel session must not inherit from the one before: a drag armed by a
// panel closed mid-move, a spent press, a tooltip resting on the closing row.
static void menu_note_new_session() {
  s_drag.phase = PDRAG_IDLE;
  s_dragLive = false;
  s_pressSpent = false;
  s_tooltip = tooltip_begin();
  s_tooltipText = nullptr;
  s_hoverHint = nullptr;
  s_glyphHint = nullptr;
  s_hoverRowKey = -1;
  s_lastRowKey = -1;
}

const char* menu_tooltip_text() { return s_tooltipText; }

// tip_wrap's measure: a hint's piece in the tooltip's font.
static int tip_measure(const char* s, int n, void*) {
  char b[sizeof s_hintBuf];
  _snprintf_s(b, sizeof b, _TRUNCATE, "%.*s", n, s);
  return canvas_text_width(b, FONT_SMALL);
}

static void row_hint(const char* text) {
  if (s_lastRowKey < 0 || !text) return;
  if (!ui_hit(s_lastRow, g_ui.mx, g_ui.my)) return;
  s_hoverRowKey = s_lastRowKey;
  strncpy_s(s_hintBuf, sizeof(s_hintBuf), text, _TRUNCATE);
  s_hoverHint = s_hintBuf;
}

static unsigned s_nowOverride = 0;
void menu_set_now_ms(unsigned ms) { s_nowOverride = ms; }
static unsigned menu_now() { return s_nowOverride ? s_nowOverride : wallclock_ms(); }

bool menu_closing_animation_active() {
  if (anim_style() == ANIM_OFF || input_panel_open()) return false;
  // A spring nothing has stepped for a second is at rest, whatever it last held.
  if (menu_now() - s_pres.lastMs > ANIM_STALE_MS) { s_pres.open.x = 0.0; s_pres.open.v = 0.0; }
  return !anim_spring_settled(s_pres.open, 0.0, 0.01);
}

// The whole-panel open alpha; identity under ANIM_OFF.
static unsigned pres_colour(unsigned c) {
  if (anim_style() == ANIM_OFF) return c;
  const int srcA = (int)((c >> 24) & 0xFFu);
  const int a = (int)(srcA * s_pres.open.x + 0.5);
  return canvas_with_alpha(c, a);
}

// The tooltip's own fade, independent of the panel's open.
static unsigned alpha_scale(unsigned c, double a) {
  if (a < 0.0) a = 0.0;
  if (a > 1.0) a = 1.0;
  const int srcA = (int)((c >> 24) & 0xFFu);
  return canvas_with_alpha(c, (int)(srcA * a + 0.5));
}

// The open alpha further scaled by the tab crossfade, for tab content only.
static unsigned pres_content_colour(unsigned c) {
  const unsigned withOpen = pres_colour(c);
  if (anim_style() == ANIM_OFF) return withOpen;
  const double tabP = anim_ease_out(anim_progress(s_pres.tabSwitchMs, s_pres.lastMs, ANIM_TAB_MS));
  const int srcA = (int)((withOpen >> 24) & 0xFFu);
  const int a = (int)(srcA * tabP * s_foldAlpha + 0.5);
  return canvas_with_alpha(withOpen, a);
}

static unsigned pres_hover_lighten(unsigned c, double hoverFrac) {
  if (anim_style() == ANIM_OFF) return c;
  return anim_blend_colour(c, C_BONE, hoverFrac);
}

// For a field whose parts must not all move toward the same colour: a well,
// rim and text all lightened toward C_BONE lose the text into the well.
static unsigned pres_hover_toward(unsigned c, unsigned target, double hoverFrac) {
  if (anim_style() == ANIM_OFF) return c;
  return anim_blend_colour(c, target, hoverFrac);
}

static const int ROW_R = 4;
static const int SIMPLE_SLIDE_PX = 8;

// No row acts while the panel is being dragged or on a spent press.
static bool row_pressed() { return !s_dragLive && !s_pressSpent && g_ui.pressed; }
static bool row_down()    { return !s_dragLive && !s_pressSpent && g_ui.down; }

// --- Where a value lands from outside its own row -------------------------
// One slot, rowKey high and value low in one word (row_commit_pack, common.h): the reset glyph and the dropdown list
// write it, the owning row takes it on its next draw. Interlocked as in AA, though all of it runs on the main thread.
static LONG64 s_rowCommit = row_commit_pack(-1, 0);

static void row_commit_publish(int rowKey, int value) {
  InterlockedExchange64(&s_rowCommit, row_commit_pack(rowKey, value));
}

// Claims the pending commit for `key` only, exactly the word it read.
static bool row_commit_take(int key, int* value) {
  const LONG64 seen = InterlockedCompareExchange64(&s_rowCommit, 0, 0);
  if (row_commit_key(seen) != key) return false;
  if (InterlockedCompareExchange64(&s_rowCommit, row_commit_pack(-1, 0), seen) != seen) return false;
  if (value) *value = row_commit_value(seen);
  return true;
}

// A panel close means no row is about to ask for a pending commit: it goes with the field.
static void row_commit_drop() { InterlockedExchange64(&s_rowCommit, row_commit_pack(-1, 0)); }

bool menu_entry_open() { return s_entry.open != 0; }

void menu_entry_paste(const char* s) {
  if (!s_entry.open || !s) return;
  s_entry = entry_step(s_entry, ENTRY_EV_PASTE, 0, 0, EK_NONE, 0, 0, s).state;   // text in, never a commit
}

// The field goes with the panel: left open, it would keep eating keys with nothing on screen.
static void menu_entry_close() {
  s_entry = entry_begin();
  s_entryCaretDrag = false;
  row_commit_drop();
}

// The one place a click away becomes a commit: a left press outside the field, a right press opening a
// field on another row, or the field's row leaving the body. Escape is the only cancel (entry.h).
static void entry_clickaway_commit() {
  if (!s_entry.open) return;
  const int row = s_entry.rowKey;
  const EntryResult r = entry_step(s_entry, ENTRY_EV_CLICKAWAY, 0, 0, EK_NONE, 0, 0, 0);
  s_entry = r.state;
  if (r.commit) row_commit_publish(row, r.value);
}

static void menu_entry_begin_edit(int rowKey, int kind, int lo, int hi, const char* text) {
  // An armed capture goes down first: keys go to the field before the capture (panel.cpp).
  menu_keycap_cancel();
  s_entryCaretDrag = false;
  s_entry = entry_step(s_entry, ENTRY_EV_OPEN, 0, rowKey, kind, lo, hi, text).state;
}

const char* menu_entry_key_vk(int vk, bool shift, bool ctrl) {
  if (!s_entry.open) return nullptr;
  const int row = s_entry.rowKey;   // before the step: a commit closes the field
  int ch = 0;
  const int ev = entry_event_of_vk(vk, shift, ctrl, &ch);
  if (ev < 0 || ev == ENTRY_EV_PASTE) return nullptr;
  const EntryResult r = entry_step(s_entry, ev, ch, 0, EK_NONE, 0, 0, 0);
  s_entry = r.state;
  if (r.commit) row_commit_publish(row, r.value);
  if (!r.hasClip) return nullptr;
  int n = 0;
  while (n < ENTRY_TEXT_CAP && r.clip[n]) { s_entryClip[n] = r.clip[n]; n++; }
  s_entryClip[n] = 0;
  return s_entryClip;
}

// A prefix's width, measured rather than one advance multiplied (AA's entry_prefix_w).
static int entry_prefix_w(const EntryState& s, int i) {
  if (i < 0) i = 0;
  if (i > s.len) i = s.len;
  char buf[ENTRY_TEXT_CAP + 1];
  int n = 0;
  while (n < i && n < ENTRY_TEXT_CAP && s.text[n]) { buf[n] = s.text[n]; n++; }
  buf[n] = 0;
  return canvas_text_width(buf, FONT_MONO);
}
// The nearest caret boundary to a pixel, ties rounding up (AA's entry_index_measured).
static int entry_index_measured(const EntryState& s, int textX, int px) {
  int i = 0, wPrev = 0;
  while (i < s.len) {
    const int wNext = entry_prefix_w(s, i + 1);
    if (px - textX < (wPrev + wNext + 1) / 2) break;
    wPrev = wNext;
    i++;
  }
  return i;
}

// --- The row widgets ------------------------------------------------------
// AA's whole vocabulary, carried complete; some (the dropdown, the named and sub sliders) have no caller yet.

// The shared body of both switch rows; returns the new value. A disabled row
// is C_BONEFNT and inert. A row with no room draws nothing.
// `unavailable`: a refused feature keeps its label and shows MENU_UNAVAILABLE_TAG where the switch would be.
static int row_switch_at(UiRect& cursor, const char* label, int value, unsigned accent,
                         bool enabled, int indent, unsigned labelOn, bool unavailable = false) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return value; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const UiRect sw = { r.x + r.w - 42, r.y + 5, 42, 20 };
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;

  const bool hovered = enabled && ui_hit(sw, g_ui.mx, g_ui.my);
  if (anim_style() == ANIM_OFF) {
    s_pres.hover[idx] = hovered ? 1.0 : 0.0;
  } else {
    const double rate = hovered ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double hv = s_pres.hover[idx] + rate * s_frameDt;
    if (hv < 0.0) hv = 0.0; else if (hv > 1.0) hv = 1.0;
    s_pres.hover[idx] = hv;
  }

  // The knob's drawn ON fraction: sprung toward the value, snapped on a tab switch.
  int knobX;
  if (anim_style() == ANIM_OFF) {
    knobX = value ? sw.x + 23 : sw.x + 2;
  } else {
    const double targetX = value ? 1.0 : 0.0;
    if (s_tabSwitchedThisFrame) { s_pres.knob[idx].x = targetX; s_pres.knob[idx].v = 0.0; }
    else anim_spring_step(&s_pres.knob[idx], targetX, ANIM_SPRING_CRITICAL.stiffness,
                          ANIM_SPRING_CRITICAL.damping, s_frameDt);
    knobX = sw.x + 2 + (int)(21.0 * s_pres.knob[idx].x + 0.5);
  }

  if (indent > 0) canvas_rect(r.x + SUB_RULE_X, r.y, 1, r.h, pres_content_colour(C_RAISED));
  canvas_text(r.x + indent, r.y + 6, label,
              pres_content_colour(pres_hover_lighten(enabled ? labelOn : C_BONEFNT, s_pres.hover[idx])), FONT_UI);
  if (unavailable) {
    // A dim tag, right-aligned to the switch's slot; it takes no click.
    const int tagPad = 6;
    const int tagW = canvas_text_width(MENU_UNAVAILABLE_TAG, FONT_SMALL) + 2 * tagPad;
    const int tagX = sw.x + sw.w - tagW;
    canvas_round_rect_outline(tagX, sw.y, tagW, sw.h, ROW_R, 1, pres_content_colour(C_RAISED));
    canvas_text(tagX + tagPad, sw.y + (sw.h - font_height[FONT_SMALL]) / 2, MENU_UNAVAILABLE_TAG,
                pres_content_colour(C_BONEFNT), FONT_SMALL);
    return value;
  }
  canvas_bevel(sw.x, sw.y, sw.w, sw.h,
               pres_content_colour(value ? (enabled ? accent : C_RAISED) : C_WELL),
               pres_content_colour(C_BEVELLO), pres_content_colour(C_RAISED));
  canvas_rect(knobX, sw.y + 2, 17, 16,
              pres_content_colour((anim_style() == ANIM_OFF ? value != 0 : s_pres.knob[idx].x >= 0.5)
                                   ? C_BONE : C_RAISED));
  if (ui_row_accepts_click(enabled, row_pressed(), ui_hit(sw, g_ui.mx, g_ui.my))) {
    s_pressClaimed = true;
    g_cfgDirty = 1;
    return !value;
  }
  return value;
}

static int row_switch(UiRect& cursor, const char* label, int value, unsigned accent, bool enabled) {
  return row_switch_at(cursor, label, value, accent, enabled, 0, C_BONE);
}

static int row_switch_sub(UiRect& cursor, const char* label, int value, unsigned accent,
                          bool enabled) {
  return row_switch_at(cursor, label, value, accent, enabled, SUB_INDENT, C_BONEDIM);
}

// A switch whose feature may be refused (a hook or check failed): unavailable, the row keeps its label, shows
// the Unavailable tag instead of the switch and keeps the stored value. The hint says why.
static int row_switch_avail(UiRect& cursor, const char* label, int value, unsigned accent, bool enabled,
                            bool available) {
  return row_switch_at(cursor, label, value, accent, enabled && available, 0, C_BONE, !available);
}


// The slider inventory: each slider row drawn this frame, its track and readout column in logical
// pixels from the content's left edge (a sub-row indents only its label). Logged under Debug log once per tab per
// session, and again only if a fold opened more slider rows on that tab.
static char s_sliderInv[768];
static int  s_sliderInvLen = 0;
static int  s_sliderInvRows = 0;
static int  s_sliderLoggedRows[TAB_COUNT] = {};

static void slider_inventory_add(const char* label, const MenuSliderGeom& g) {
  if (s_sliderInvLen >= (int)sizeof(s_sliderInv) - 1) return;
  const int n = _snprintf_s(s_sliderInv + s_sliderInvLen, sizeof(s_sliderInv) - s_sliderInvLen, _TRUNCATE,
                            "%s%s x=%d w=%d readout=%d+%d", s_sliderInvRows ? "; " : "", label, g.trackX, g.trackW,
                            g.readoutX, g.readoutW);
  s_sliderInvLen = n < 0 ? (int)sizeof(s_sliderInv) - 1 : s_sliderInvLen + n;
  s_sliderInvRows++;
}

// The shared body of the slider rows. Every row has the same track and readout column (menu_slider_geom), so
// no track depends on its row's value, readout or indent. `kind` EK_NONE (a name or a fixed value) grows no reset
// glyph; `def` is the glyph's value.
static int row_slider_at(UiRect& cursor, const char* label, int value, int lo, int hi,
                         const char* valueText, bool enabled, int indent, unsigned labelOn,
                         int kind, int def) {
  s_resetPressedThisRow = false;
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return value; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  const int key = s_lastRowKey;
  const int valueW = canvas_text_width(valueText, FONT_MONO);
  const int valueX = r.x + r.w - VALUE_MARGIN - valueW;
  const MenuSliderGeom geom = menu_slider_geom(r.w, canvas_text_width(MENU_READOUT_RESERVE, FONT_MONO));
  slider_inventory_add(label, geom);
  const UiRect track = { r.x + geom.trackX, r.y + 11, geom.trackW, 8 };
  const UiRect grab = { track.x - 6, track.y - 8, track.w + 12, track.h + 16 };
  const UiRect glyph = { r.x + geom.glyphX, r.y + 5, RESET_W, RESET_W };
  // No reset glyph under the hand while its row's field is open (AA).
  const bool fieldHere = s_entry.open != 0 && s_entry.rowKey == key;
  const bool glyphLive = enabled && kind != EK_NONE && !fieldHere && !s_dragLive && ui_hit(r, g_ui.mx, g_ui.my);
  const bool glyphHot  = glyphLive && ui_hit(glyph, g_ui.mx, g_ui.my);

  s_resetPressedThisRow = glyphHot && row_pressed();
  if (s_resetPressedThisRow) {
    row_commit_publish(key, def);
    s_pressSpent = true;
    s_pressClaimed = true;
  }

  // A committed value lands clamped to [lo, hi], widened to admit `def` when
  // it sits outside that range (the frame-cap row's 0 sentinel); every other
  // caller's def is already inside [lo, hi], so behaviour there is unchanged.
  bool committed = false;
  int landedRaw = 0;
  if (row_commit_take(key, &landedRaw)) {
    const int commitLo = lo < def ? lo : def;
    const int commitHi = hi > def ? hi : def;
    const int landed = clamp_int(landedRaw, commitLo, commitHi);
    committed = landed != value;
    value = landed;
  }

  // A right press anywhere on an enabled numeric row opens the field (AA's menu.cpp:950-961), after
  // committing a field open elsewhere: leaving it is a commit, not a discard.
  if (kind != EK_NONE && enabled && !s_dragLive && g_ui.rpressed && ui_hit(r, g_ui.mx, g_ui.my)) {
    char prefill[ENTRY_TEXT_CAP + 1];
    entry_format(prefill, sizeof prefill, value, kind);
    entry_clickaway_commit();
    menu_entry_begin_edit(key, kind, lo, hi, prefill);
  }
  const bool fieldOpen = s_entry.open != 0 && s_entry.rowKey == key;

  if (glyphHot) { s_hoverRowKey = key; s_glyphHint = MENU_RESET_HINT; }

  const int kx = ui_slider_x(track, value, lo, hi);

  const bool hovered = enabled && ui_hit(grab, g_ui.mx, g_ui.my);
  if (anim_style() == ANIM_OFF) {
    s_pres.hover[idx] = hovered ? 1.0 : 0.0;
  } else {
    const double rate = hovered ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double hv = s_pres.hover[idx] + rate * s_frameDt;
    if (hv < 0.0) hv = 0.0; else if (hv > 1.0) hv = 1.0;
    s_pres.hover[idx] = hv;
  }
  if (anim_style() == ANIM_OFF) {
    s_pres.resetHover[idx] = glyphHot ? 1.0 : 0.0;
  } else {
    const double rate = glyphHot ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double rv = s_pres.resetHover[idx] + rate * s_frameDt;
    if (rv < 0.0) rv = 0.0; else if (rv > 1.0) rv = 1.0;
    s_pres.resetHover[idx] = rv;
  }

  int drawnKx;
  if (anim_style() == ANIM_OFF) {
    drawnKx = kx;
  } else {
    const double targetFrac = track.w > 0 ? (double)(kx - track.x) / (double)track.w : 0.0;
    if (s_tabSwitchedThisFrame) { s_pres.fill[idx].x = targetFrac; s_pres.fill[idx].v = 0.0; }
    else anim_spring_step(&s_pres.fill[idx], targetFrac, ANIM_SPRING_CRITICAL.stiffness,
                          ANIM_SPRING_CRITICAL.damping, s_frameDt);
    drawnKx = track.x + (int)(s_pres.fill[idx].x * track.w + 0.5);
    if (drawnKx < track.x) drawnKx = track.x;
    if (drawnKx > track.x + track.w) drawnKx = track.x + track.w;
  }

  if (indent > 0) canvas_rect(r.x + SUB_RULE_X, r.y, 1, r.h, pres_content_colour(C_RAISED));
  canvas_text(r.x + indent, r.y + 6, label,
              pres_content_colour(pres_hover_lighten(enabled ? labelOn : C_BONEFNT, s_pres.hover[idx])), FONT_UI);
  canvas_bevel(track.x, track.y, track.w, track.h, pres_content_colour(C_WELL),
               pres_content_colour(C_BEVELLO), pres_content_colour(C_RAISED));
  canvas_rect(track.x, track.y, drawnKx - track.x, track.h, pres_content_colour(C_HAZARDDIM));
  canvas_rect(drawnKx - 4, track.y - 4, 9, 16, pres_content_colour(enabled ? C_HAZARD : C_BONEFNT));
  if (fieldOpen) {
    // AA's field (its menu.cpp:1023-1096): a rounded well sized for the widest int, a C_HAZARD rim, the
    // selection in C_HAZARDDIM under the text, a caret blinking on 500 ms (steady under ANIM_OFF).
    const int fieldW = canvas_text_width(MENU_ENTRY_WIDEST, FONT_MONO) + 2 * FIELD_PAD;
    const UiRect field = { r.x + r.w - fieldW, r.y + 5, fieldW, 20 };
    s_entryFieldRect = field;
    s_entrySeenThisFrame = true;
    if (!ui_rect_inside(field, s_bodyRect)) entry_clickaway_commit();   // scrolled out: commit, not strand
    canvas_round_rect(field.x, field.y, field.w, field.h, ROW_R, pres_content_colour(C_WELL));
    canvas_round_rect_outline(field.x, field.y, field.w, field.h, ROW_R, 1, pres_content_colour(C_HAZARD));
    const int textY = field_text_y(field);
    const int textX = field.x + FIELD_PAD;
    // The raw button, not row_pressed(): the press into this well is also what spends the row's press.
    if (g_ui.pressed && ui_hit(field, g_ui.mx, g_ui.my)) {
      s_entry = entry_step(s_entry, ENTRY_EV_SET_CARET, entry_index_measured(s_entry, textX, g_ui.mx),
                           0, EK_NONE, 0, 0, 0).state;
      s_entryCaretDrag = true;
    } else if (s_entryCaretDrag && g_ui.down) {
      s_entry = entry_step(s_entry, ENTRY_EV_SET_CARET, entry_index_measured(s_entry, textX, s_frameMx),
                           1, EK_NONE, 0, 0, 0).state;
    }
    const int selA = entry_sel_lo(s_entry), selB = entry_sel_hi(s_entry);
    if (selB > selA) {
      const int ax = entry_prefix_w(s_entry, selA), bx = entry_prefix_w(s_entry, selB);
      canvas_rect(textX + ax, field.y + 4, bx - ax, field.h - 8, pres_content_colour(C_HAZARDDIM));
    }
    canvas_text(textX, textY, s_entry.text, pres_content_colour(C_BONE), FONT_MONO);
    const bool caretOn = (anim_style() == ANIM_OFF) || ((menu_now() / 500u) % 2u == 0u);
    if (caretOn)
      canvas_rect(textX + entry_prefix_w(s_entry, s_entry.caret) + 1, field.y + 4, 1, field.h - 8,
                  pres_content_colour(C_BONE));
  } else {
    canvas_text(valueX, r.y + 7, valueText, pres_content_colour(C_BONE), FONT_MONO);
  }

  // The reset button: a 20x20 well with a circular arrow drawn as two open
  // rings of seven segments (2px stroke) and an arrowhead (AA's geometry).
  if (glyphLive) {
    const double rf = s_pres.resetHover[idx];
    canvas_round_rect(glyph.x, glyph.y, glyph.w, glyph.h, ROW_R,
                      pres_content_colour(pres_hover_toward(C_WELL, C_RAISED, rf)));
    canvas_round_rect_outline(glyph.x, glyph.y, glyph.w, glyph.h, ROW_R, 1,
                              pres_content_colour(pres_hover_toward(C_RAISED, C_BONEDIM, rf)));
    static const int RING6[8][2] = {
      { 16, 10 }, { 15, 6 }, { 11, 4 }, { 7, 5 }, { 4, 8 },
      { 4, 12 }, { 7, 15 }, { 11, 16 }
    };
    static const int RING5[8][2] = {
      { 15, 10 }, { 14, 7 }, { 11, 5 }, { 7, 6 }, { 5, 8 },
      { 5, 12 }, { 7, 14 }, { 11, 15 }
    };
    static const int BARB[2] = { 13, 13 };
    const int ix = glyph.x - 1, iy = glyph.y;
    const unsigned gc = pres_content_colour(glyphHot ? C_BONE : C_BONEDIM);
    for (int i = 0; i < 7; i++) {
      canvas_line(ix + RING6[i][0], iy + RING6[i][1],
                  ix + RING6[i + 1][0], iy + RING6[i + 1][1], gc);
      canvas_line(ix + RING5[i][0], iy + RING5[i][1],
                  ix + RING5[i + 1][0], iy + RING5[i + 1][1], gc);
    }
    canvas_rect(ix + RING6[0][0] - 1, iy + RING6[0][1] - 1, 3, 3, gc);
    canvas_line(ix + RING6[0][0], iy + RING6[0][1], ix + BARB[0], iy + BARB[1], gc);
  }

  // The held button, not the edge: a slider is dragged, off the logical track.
  if (ui_row_accepts_click(enabled, row_down(), ui_hit(grab, g_ui.mx, g_ui.my))) {
    s_pressClaimed = true;
    const int v = ui_slider_value(track, g_ui.mx, lo, hi);
    if (v != value) { g_cfgDirty = 1; return v; }
  }
  if (committed) g_cfgDirty = 1;
  return value;
}

static int row_slider(UiRect& cursor, const char* label, int value, int lo, int hi,
                      const char* suffix, bool enabled, int def) {
  char buf[32];
  sprintf_s(buf, "%d%s", value, suffix);
  return row_slider_at(cursor, label, value, lo, hi, buf, enabled, 0, C_BONE, EK_INT, def);
}

static int row_slider_sub(UiRect& cursor, const char* label, int value, int lo, int hi,
                          const char* suffix, bool enabled, int def) {
  char buf[32];
  sprintf_s(buf, "%d%s", value, suffix);
  return row_slider_at(cursor, label, value, lo, hi, buf, enabled, SUB_INDENT, C_BONEDIM, EK_INT, def);
}

// A readout that is a name.
static int row_slider_named(UiRect& cursor, const char* label, int value, int lo, int hi,
                            const char* (*labelFn)(int), bool enabled) {
  return row_slider_at(cursor, label, value, lo, hi, labelFn(value), enabled, 0, C_BONE, EK_NONE, 0);
}

static int row_slider_named_sub(UiRect& cursor, const char* label, int value, int lo, int hi,
                                const char* (*labelFn)(int), bool enabled) {
  return row_slider_at(cursor, label, value, lo, hi, labelFn(value), enabled, SUB_INDENT, C_BONEDIM, EK_NONE, 0);
}

// --- A dropdown row ---------------------------------------------------------
// The open list is drawn after every row (menu_dropdown_draw), since rows
// paint in order with no z-order.
struct MenuDropdown {
  int  open;
  int  rowKey;
  UiRect box;
  int  count;
  const char* (*labelFn)(int);
};
static MenuDropdown s_dd = { 0, -1, { 0, 0, 0, 0 }, 0, 0 };
static bool s_ddSeenThisFrame = false;

static const int DD_CHEVRON_W = 14;
static const int DD_PAD       = 6;

static int row_dropdown(UiRect& cursor, const char* label, int value, int count,
                        const char* (*labelFn)(int), bool enabled) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return value; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  const int key = s_lastRowKey;

  int nameW = 0;
  for (int v = 0; v < count; v++) {
    const int w = canvas_text_width(labelFn(v), FONT_MONO);
    if (w > nameW) nameW = w;
  }
  const int boxW = nameW + DD_PAD * 2 + DD_CHEVRON_W;
  const UiRect box = { r.x + r.w - VALUE_MARGIN - boxW, r.y + 4, boxW, ROW_H - 8 };

  const bool hot = enabled && ui_hit(box, g_ui.mx, g_ui.my);
  const bool pressed = row_pressed();
  if (hot && pressed) s_pressSpent = true;
  const bool wasMine = s_dd.open != 0 && s_dd.rowKey == key;
  bool seen = false;
  const MenuDdState newSt = menu_dd_row_step(MenuDdState{ s_dd.open, s_dd.rowKey },
                                              key, hot, pressed,
                                              ui_rect_inside(box, s_bodyRect), &seen);
  s_dd.open = newSt.open; s_dd.rowKey = newSt.rowKey;
  if (seen) s_ddSeenThisFrame = true;
  const bool mine = newSt.open != 0 && newSt.rowKey == key;
  if (mine && !wasMine) { s_dd.box = box; s_dd.count = count; s_dd.labelFn = labelFn; }
  else if (mine) { s_dd.box = box; }

  const unsigned fill = enabled ? (hot || mine ? C_RAISED : C_WELL) : C_WELL;
  canvas_bevel(box.x, box.y, box.w, box.h, pres_content_colour(fill),
               pres_content_colour(C_BEVELHI), pres_content_colour(C_BEVELLO));
  canvas_text(r.x, r.y + 8, label, pres_colour(enabled ? C_BONE : C_BONEFNT), FONT_UI);
  canvas_text(box.x + DD_PAD, box.y + 4, labelFn(clamp_int(value, 0, count - 1)),
              pres_colour(enabled ? C_BONE : C_BONEFNT), FONT_MONO);
  {
    const int cx = box.x + box.w - DD_CHEVRON_W / 2 - DD_PAD / 2;
    const int cy = box.y + box.h / 2;
    const unsigned cc = pres_colour(enabled ? C_BONEDIM : C_BONEFNT);
    const int dy = mine ? -3 : 3;
    canvas_line(cx - 4, cy - dy / 2, cx, cy + dy, cc);
    canvas_line(cx, cy + dy, cx + 4, cy - dy / 2, cc);
  }

  int pickedRaw = 0;
  if (row_commit_take(key, &pickedRaw)) {
    const int v = clamp_int(pickedRaw, 0, count - 1);
    if (v != value) g_cfgDirty = 1;
    return v;
  }
  return value;
}

static void menu_dropdown_draw() {
  if (!s_dd.open || !s_dd.labelFn || s_dd.count <= 0) return;
  const int itemH = ROW_H - 6;
  const UiRect list = { s_dd.box.x, s_dd.box.y + s_dd.box.h + 2,
                        s_dd.box.w, itemH * s_dd.count + 4 };
  canvas_bevel(list.x, list.y, list.w, list.h, pres_content_colour(C_PANEL),
               pres_content_colour(C_BEVELHI), pres_content_colour(C_BEVELLO));
  bool onAnItem = false;
  for (int i = 0; i < s_dd.count; i++) {
    const UiRect it = { list.x + 2, list.y + 2 + i * itemH, list.w - 4, itemH };
    const bool hot = ui_hit(it, g_ui.mx, g_ui.my);
    if (hot) {
      onAnItem = true;
      canvas_rect(it.x, it.y, it.w, it.h, pres_content_colour(C_RAISED));
    }
    canvas_text(it.x + DD_PAD - 2, it.y + 3, s_dd.labelFn(i),
                pres_colour(hot ? C_BONE : C_BONEDIM), FONT_MONO);
    if (hot && row_pressed()) {
      s_pressSpent = true;
      row_commit_publish(s_dd.rowKey, i);
      s_dd.open = 0; s_dd.rowKey = -1;
      return;
    }
  }
  if (!onAnItem && row_pressed() && !ui_hit(s_dd.box, g_ui.mx, g_ui.my)) {
    s_pressSpent = true;
    s_dd.open = 0; s_dd.rowKey = -1;
  }
}

// A group heading: uppercase, bone-faint, small.
static void draw_group(UiRect& cursor, const char* heading) {
  if (cursor.h < GROUP_H) { s_rowsDropped = 1; return; }
  const UiRect r = ui_row(cursor, GROUP_H, GROUP_GAP);
  canvas_text(r.x, r.y + 5, heading, pres_content_colour(C_BONEFNT), FONT_SMALL);
}

// A line of text that is a row of its own (AA's note row).
static void draw_placeholder(UiRect& cursor, const char* text) {
  if (cursor.h < ROW_H) { s_rowsDropped = 1; return; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  canvas_text(r.x, r.y + 6, text, pres_content_colour(C_BONEFNT), FONT_SMALL);
}

// A field's width: the widest thing a bind field can ever show, so the well
// never changes width under the hand (AA's row_bind_at rule).
static int field_well_w() {
  int w = canvas_text_width(MENU_KEYCAP_PROMPT, FONT_MONO);
  const int hoverW = canvas_text_width(MENU_KEYCAP_HOVER, FONT_MONO);
  const int nameW  = canvas_text_width(BIND_LABEL_WIDEST, FONT_MONO);
  if (hoverW > w) w = hoverW;
  if (nameW  > w) w = nameW;
  return w + 2 * FIELD_PAD;
}

// FONT_MONO's ink band (cap top to descender) centred in the well.
static int field_text_y(const UiRect& field) {
  const Glyph& capA  = font_glyphs[FONT_MONO]['A' - 0x20];
  const Glyph& descY = font_glyphs[FONT_MONO]['y' - 0x20];
  const int inkTop = capA.by;
  const int inkH   = (descY.by + descY.h) - inkTop;
  return field.y + (field.h - inkH) / 2 - inkTop;
}

static void field_hover_step(int idx, bool hovered) {
  if (anim_style() == ANIM_OFF) {
    s_pres.hover[idx] = hovered ? 1.0 : 0.0;
  } else {
    const double rate = hovered ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double hv = s_pres.hover[idx] + rate * s_frameDt;
    if (hv < 0.0) hv = 0.0; else if (hv > 1.0) hv = 1.0;
    s_pres.hover[idx] = hv;
  }
}

// A binding row: a well that arms a capture on a click anywhere on the row;
// the next key the panel receives binds. The rim is hazard yellow while armed.
static void row_bind_at(UiRect& cursor, const char* label, int row, bool enabled,
                        int indent, unsigned labelOn) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  const int fieldW = field_well_w();
  const UiRect field = { r.x + r.w - fieldW, r.y + 5, fieldW, 20 };

  const bool hovered = enabled && ui_hit(field, g_ui.mx, g_ui.my);
  field_hover_step(idx, hovered);

  if (indent > 0) canvas_rect(r.x + SUB_RULE_X, r.y, 1, r.h, pres_content_colour(C_RAISED));
  canvas_text(r.x + indent, r.y + 6, label,
              pres_content_colour(enabled ? labelOn : C_BONEFNT), FONT_UI);
  const bool armed = s_keycap.phase == KEYCAP_ARMED && s_keycap.row == row;
  const int vk = config_bind_get(row);
  canvas_round_rect(field.x, field.y, field.w, field.h, ROW_R,
                    pres_content_colour(pres_hover_toward(C_WELL, C_RAISED, s_pres.hover[idx])));
  canvas_round_rect_outline(field.x, field.y, field.w, field.h, ROW_R, 1,
                            pres_content_colour(armed ? C_HAZARD
                                                      : pres_hover_toward(C_RAISED, C_BONEDIM, s_pres.hover[idx])));
  const char* value = bind_field_text(armed, hovered, vk, MENU_KEYCAP_PROMPT, MENU_KEYCAP_HOVER);
  const int valueW = canvas_text_width(value, FONT_MONO);
  canvas_text(field.x + (field.w - valueW) / 2, field_text_y(field), value,
              pres_content_colour(armed ? C_HAZARD : (enabled ? C_BONE : C_BONEFNT)), FONT_MONO);
  if (ui_row_accepts_click(enabled, row_pressed(), ui_hit(r, g_ui.mx, g_ui.my))) {
    KeyCapEvent e; e.kind = KEYCAP_EV_CLICK; e.row = row; e.vk = 0;
    s_keycap = keycap_step(s_keycap, e).state;
    s_pressClaimed = true;
  }
  // The game's own claim on this key (AA's menu.cpp:1470-1477): one line, found while choosing rather
  // than in a firefight. Read from the engine's live bind tables once per panel open (gamebind.h); absent
  // when they could not be read.
  if (vk && gamebind_vk_bound(vk)) draw_placeholder(cursor, MENU_GAMECFG_NOTE);
}

static void row_bind(UiRect& cursor, const char* label, int row) {
  row_bind_at(cursor, label, row, true, 0, C_BONE);
}

static void row_bind_sub(UiRect& cursor, const char* label, int row, bool enabled) {
  row_bind_at(cursor, label, row, enabled, SUB_INDENT, C_BONEDIM);
}

static int folder_measure(const char* s, void*) { return canvas_text_width(s, FONT_SMALL); }

// The Photo folder row (photofolder.h): the bind well stretched to the row's end, holding the folder shortened from
// the left, dim while it is the default. A click anywhere on the row opens the Windows folder picker once the panel
// has drawn (photo_folder_frame).
static void row_folder(UiRect& cursor, const char* label) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  int wellX = r.x + canvas_text_width(label, FONT_UI) + 12;
  if (wellX > r.x + r.w - 2 * FIELD_PAD) wellX = r.x + r.w - 2 * FIELD_PAD;
  const UiRect field = { wellX, r.y + 5, r.x + r.w - wellX, 20 };
  field_hover_step(idx, ui_hit(field, g_ui.mx, g_ui.my));
  canvas_text(r.x, r.y + 6, label, pres_content_colour(C_BONE), FONT_UI);
  canvas_round_rect(field.x, field.y, field.w, field.h, ROW_R,
                    pres_content_colour(pres_hover_toward(C_WELL, C_RAISED, s_pres.hover[idx])));
  canvas_round_rect_outline(field.x, field.y, field.w, field.h, ROW_R, 1,
                            pres_content_colour(pres_hover_toward(C_RAISED, C_BONEDIM, s_pres.hover[idx])));
  char raw[CONFIG_STR_CAP], shown[CONFIG_STR_CAP], fit[CONFIG_STR_CAP];
  bool isDefault = true;
  photo_folder_shown(raw, sizeof raw, &isDefault);
  photo_folder_display(raw, shown, sizeof shown);
  photo_folder_shorten(shown, field.w - 2 * FIELD_PAD, folder_measure, 0, fit, sizeof fit);
  canvas_text(field.x + FIELD_PAD, field.y + (field.h - font_height[FONT_SMALL]) / 2, fit,
              pres_content_colour(isDefault ? C_BONEFNT : C_BONE), FONT_SMALL);
  if (ui_row_accepts_click(true, row_pressed(), ui_hit(r, g_ui.mx, g_ui.my))) {
    photo_folder_request();
    s_pressClaimed = true;
  }
}

// The reticle preview: a sub-row two rows tall, a sunken well (the slider track's bevel) holding three flat
// swatches of the game's own palettes, each with the reticle centred at its true size.
static void row_reticle_strip(UiRect& cursor, bool enabled) {
  const int h = 2 * ROW_H + ROW_GAP;
  if (cursor.h < h) { s_lastRowKey = -1; s_rowsDropped = 1; return; }
  const UiRect r = ui_row(cursor, h, ROW_GAP);
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  canvas_rect(r.x + SUB_RULE_X, r.y, 1, r.h, pres_content_colour(C_RAISED));
  const UiRect well = { r.x + SUB_INDENT, r.y, r.w - SUB_INDENT, r.h };
  canvas_bevel(well.x, well.y, well.w, well.h, pres_content_colour(C_WELL),
               pres_content_colour(C_BEVELLO), pres_content_colour(C_RAISED));
  MaRect sw[3], sep[2];
  ma_swatch_layout(well.x + 1, well.y + 1, well.w - 2, well.h - 2, sw, sep);
  for (int i = 0; i < 3; ++i) {
    canvas_rect(sw[i].x, sw[i].y, sw[i].w, sw[i].h, pres_content_colour(ma_swatch_colour(i)));
    if (enabled) mouseaim_draw_reticle(sw[i].x + sw[i].w / 2, sw[i].y + sw[i].h / 2, pres_content_colour);
  }
  for (int i = 0; i < 2; ++i) canvas_rect(sep[i].x, sep[i].y, sep[i].w, sep[i].h, pres_content_colour(C_BEVELLO));
}

// Collapse facility (menu.h): `if (collapse_begin(&f, cursor, parentOn, rows)) { rows } collapse_end(f, cursor);`
// A collapsed block draws and hit-tests nothing. `rows` counts row ordinals (a heading, placeholder or "Also bound"
// note takes none); a collapsed block skips the larger of `rows` and its last drawn count, so a wrong `rows` is safe.
struct FoldSlot {
  int countedRows; int mismatchLogged;
  MenuFold anim; unsigned seenFrame; int fullH;   // fullH: the block's laid-out height, last frame it drew
};
// Keyed by tab and the block's first row ordinal, which a block above it never moves.
static FoldSlot s_folds[TAB_COUNT][ANIM_ROW_SLOTS];

struct MenuFoldBlock {
  MenuCollapse c; int start; int declared; int top;
  double frac; bool animating; bool clipped;
  UiRect savedClip; double savedAlpha; int savedMx, savedMy;
};

static FoldSlot& fold_slot(int start) { return s_folds[s_tab][start % ANIM_ROW_SLOTS]; }

static UiRect rect_intersect(const UiRect& a, const UiRect& b) {
  const int x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
  const int x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
  const int y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
  const UiRect r = { x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0 };
  return r;
}

static bool collapse_begin(MenuFoldBlock* f, UiRect& cursor, bool parentOn, int rows) {
  f->start = s_pres.rowsThisFrame;
  f->declared = rows;
  f->top = cursor.y;
  f->clipped = false;
  FoldSlot& slot = fold_slot(f->start);
  const unsigned now = s_pres.lastMs;
  // A block not drawn last frame (another tab, a shut parent block, a new session) pops rather than replays.
  const bool animate = anim_style() != ANIM_OFF && !s_foldSnap && slot.seenFrame + 1u == s_frameNo;
  slot.anim = menu_fold_step(slot.anim, parentOn ? 1 : 0, now, animate ? 1 : 0);
  slot.seenFrame = s_frameNo;
  f->frac = menu_fold_frac(slot.anim, now);
  f->animating = menu_fold_animating(slot.anim, now);
  const int reserve = slot.countedRows > rows ? slot.countedRows : rows;
  f->c = menu_collapse_begin(menu_fold_laid_out(slot.anim, now) ? 1 : 0, f->start, reserve);
  if (f->c.shown && f->animating) {
    // Last frame's height: this frame's is known only once the rows are laid out.
    const UiRect part = { s_bodyRect.x, cursor.y, s_bodyRect.w, menu_fold_height(slot.fullH, f->frac) };
    const UiRect clip = rect_intersect(part, s_clipNow);
    canvas_push_clip(clip.x, clip.y, clip.w, clip.h);   // pushes replace, so intersected here
    f->clipped = true;
    f->savedClip = s_clipNow;
    s_clipNow = clip;
    f->savedAlpha = s_foldAlpha;
    s_foldAlpha *= f->frac;
    f->savedMx = g_ui.mx; f->savedMy = g_ui.my;
    const bool inPart = clip.h > 0 && ui_hit(clip, g_ui.mx, g_ui.my);
    if (!menu_fold_hit_live(slot.anim, now, inPart)) {
      // A press on a row sliding shut does nothing, and does not grab the panel either.
      if (inPart && g_ui.pressed) s_pressClaimed = true;
      g_ui.mx = MENU_MOUSE_NOWHERE; g_ui.my = MENU_MOUSE_NOWHERE;
    }
  }
  return f->c.shown != 0;
}

static void collapse_end(const MenuFoldBlock& f, UiRect& cursor) {
  if (f.c.shown) {
    FoldSlot& slot = fold_slot(f.start);
    const int drawn = s_pres.rowsThisFrame - f.start;
    slot.countedRows = drawn;
    if (drawn != f.declared && !slot.mismatchLogged) {
      slot.mismatchLogged = 1;
      np_log("menu: fold at tab %d ordinal %d declares %d rows but drew %d", s_tab, f.start, f.declared, drawn);
    }
    slot.fullH = cursor.y - f.top;
    if (f.animating) {
      const int h = menu_fold_height(slot.fullH, f.frac);
      cursor.h += (cursor.y - f.top) - h;
      cursor.y = f.top + h;
    }
  }
  if (f.clipped) {
    canvas_pop_clip();
    s_clipNow = f.savedClip;
    s_foldAlpha = f.savedAlpha;
    g_ui.mx = f.savedMx; g_ui.my = f.savedMy;
  }
  s_lastRowKey = -1;
  s_pres.rowsThisFrame = menu_collapse_end(f.c, s_pres.rowsThisFrame);
}

// A planned feature this port does not have yet: drawn like a disabled row_switch (greyed label, inert well, no
// config key) so an AA-shaped group is never a row over nothing. Used for planned features only.
static void row_planned(UiRect& cursor, const char* label, const char* hint) {
  row_switch_at(cursor, label, 0, C_BONE, false, 0, C_BONE);
  row_hint(hint);
}

// A planned sub-row: the same greyed, inert shape, indented under its parent.
static void row_planned_sub(UiRect& cursor, const char* label, const char* hint) {
  row_switch_at(cursor, label, 0, C_BONE, false, SUB_INDENT, C_BONEDIM);
  row_hint(hint);
}

// --- The Progress tab ------------------------------------------------------------------------------
// The selector is session state, not a setting (progress.h's pg_sel_step); s_pgFresh carries a new panel session
// to the tab's first draw, whichever tab the panel opened on.
static PgSel s_pgSel = pg_sel_init();
static bool s_pgFresh = true;

// The readout columns, each as wide as its widest usual value, so they line up down the tab.
static void pg_col_widths(int colW[3]) {
  colW[0] = canvas_text_width("00/00", FONT_MONO);
  colW[1] = canvas_text_width("S 00/00", FONT_MONO);
  colW[2] = canvas_text_width("K 000/000", FONT_MONO);
}

// A complete part draws halfway to the panel colour.
static unsigned pg_dim(unsigned c, bool dim) { return dim ? anim_blend_colour(c, C_PANEL, 0.5) : c; }

static bool pg_row_begin(UiRect& cursor, UiRect* r) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return false; }
  *r = ui_row(cursor, ROW_H, ROW_GAP);
  s_lastRow = *r;
  s_lastRowKey = s_tab * 1000 + s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  return true;
}

// A foldable section heading: a chevron (down open, right folded), the caps text, and while folded the section's
// numbers right-aligned, dim (`openNote` instead while open). The whole row is the click target. It takes a row ordinal
// so two sections whose bodies draw none still key their fold animation apart. Returns whether the body shows.
static bool pg_fold_heading(UiRect& cursor, const char* heading, int sec, const char* summary,
                            const char* openNote = 0) {
  if (cursor.h < GROUP_H) { s_lastRowKey = -1; s_rowsDropped = 1; return !pg_folded(g_cfg.progFolds, sec); }
  const UiRect r = ui_row(cursor, GROUP_H, GROUP_GAP);
  s_pres.rowsThisFrame++;
  s_lastRowKey = -1;
  const bool hovered = ui_hit(r, g_ui.mx, g_ui.my);
  if (ui_row_accepts_click(true, row_pressed(), hovered)) {
    s_pressClaimed = true;
    g_cfg.progFolds = pg_fold_toggle(g_cfg.progFolds, sec);
    g_cfgDirty = 1;
  }
  const bool folded = pg_folded(g_cfg.progFolds, sec);
  const unsigned c = pres_content_colour(hovered ? C_BONE : C_BONEFNT);
  const int cy = r.y + 5 + font_height[FONT_SMALL] / 2;
  if (folded) canvas_tri(r.x + 1, cy - 3, r.x + 1, cy + 3, r.x + 5, cy, c);
  else canvas_tri(r.x, cy - 2, r.x + 6, cy - 2, r.x + 3, cy + 2, c);
  const int tx = r.x + 14;
  canvas_text(tx, r.y + 5, heading, c, FONT_SMALL);
  const char* right = folded ? summary : openNote;
  if (right && *right) {
    const int w = canvas_text_width(right, FONT_SMALL), x = r.x + r.w - VALUE_MARGIN - w;
    if (x > tx + canvas_text_width(heading, FONT_SMALL) + 12)
      canvas_text(x, r.y + 5, right, pres_content_colour(C_BONEFNT), FONT_SMALL);
  }
  return !folded;
}

// Nukes, secrets, kills, right-aligned in their columns: nukes green once all are found, else hazard yellow;
// secrets the secret yellow, kills the enemy red; a part not played is faint.
static void pg_readouts(const UiRect& r, const PgLayout& L, const int colW[3], const PgCounts& f, const PgCounts& t,
                        int haveTotal, int played, bool dim) {
  const unsigned colour[3] = { haveTotal && f.nukes >= t.nukes ? C_GREEN : C_HAZARD, C_HAZARD, C_RED };
  for (int i = 0; i < 3; ++i) {
    char b[24];
    pg_column_readout(b, sizeof b, i, f, t, haveTotal, played);
    const unsigned c = played ? pg_dim(colour[i], dim) : C_BONEFNT;
    canvas_text(r.x + L.col[i] + colW[i] - canvas_text_width(b, FONT_MONO), r.y + 7, b, pres_content_colour(c),
                FONT_MONO);
  }
}

// Hard nukes: the banked records with the part in play added live, as the corner readout counts them. The engine's
// unlock compares the banked sum only (levels-and-stats.md section 2), so the caption says when the path opens.
static void pg_row_hard(UiRect& cursor, int nukes, int total) {
  UiRect r;
  if (!pg_row_begin(cursor, &r)) return;
  char b[24];
  if (total > 0) sprintf_s(b, "%d/%d", nukes, total);
  else sprintf_s(b, "%d", nukes);
  canvas_text(r.x, r.y + 7, b, pres_content_colour(C_GREEN), FONT_MONO);
  static const char kCaption[] = "Hard nukes; the Wozma path opens once all are banked at a part's end";
  const int capX = r.x + canvas_text_width(b, FONT_MONO) + 10;
  if (capX + canvas_text_width(kCaption, FONT_SMALL) <= r.x + r.w - VALUE_MARGIN)
    canvas_text(capX, r.y + 10, kCaption, pres_content_colour(C_BONEFNT), FONT_SMALL);
}

// An episode's total: a thin nuke bar, then its finished parts' counts over every part's total.
static void pg_row_total(UiRect& cursor, const PgCounts& f, const PgCounts& t, const int colW[3]) {
  UiRect r;
  if (!pg_row_begin(cursor, &r)) return;
  static const char kLabel[] = "Total";
  const PgLayout L = pg_part_layout(r.w, VALUE_MARGIN, canvas_text_width(kLabel, FONT_UI), colW, 0, 0, 0);
  canvas_text(r.x, r.y + 6, kLabel, pres_content_colour(C_BONE), FONT_UI);
  if (L.barW > 0) {
    const int by = r.y + (r.h - PG_BAR_H) / 2;
    canvas_rect(r.x + L.barX, by, L.barW, PG_BAR_H, pres_content_colour(C_WELL));
    const int fillW = t.nukes > 0 ? clamp_int((int)((long long)L.barW * f.nukes / t.nukes), 0, L.barW) : 0;
    if (fillW > 0) canvas_rect(r.x + L.barX, by, fillW, PG_BAR_H, pres_content_colour(C_GREEN));
  }
  pg_readouts(r, L, colW, f, t, 1, 1, false);
}

// One part, a sub-row: its label, a "Now" tag on the part in play, the map name where it fits, one pip per nuke
// (filled found, hollow missing; the records do not say which), then the readouts. Complete dims, not played is
// faint with "Not played" where the pips would be.
static void pg_row_part(UiRect& cursor, int part, const char* map, const PgRow& row, const int colW[3]) {
  UiRect r;
  if (!pg_row_begin(cursor, &r)) return;
  char label[16];
  sprintf_s(label, "Part %d", part + 1);
  const bool unplayed = row.state == PG_UNPLAYED, dim = row.state == PG_COMPLETE;
  static const char kNow[] = "Now", kNotPlayed[] = "Not played";
  const int tagW = row.live ? canvas_text_width(kNow, FONT_SMALL) + 2 * PG_TAG_PAD : 0;
  const int mapW = map && *map ? canvas_text_width(map, FONT_SMALL) : 0;
  const int pips = row.haveTotal && row.total.nukes > row.found.nukes ? row.total.nukes : row.found.nukes;
  const PgLayout L = pg_part_layout(r.w, VALUE_MARGIN, SUB_INDENT + canvas_text_width(label, FONT_UI), colW, pips,
                                    tagW, mapW);
  canvas_rect(r.x + SUB_RULE_X, r.y, 1, r.h, pres_content_colour(C_RAISED));
  canvas_text(r.x + SUB_INDENT, r.y + 6, label,
              pres_content_colour(unplayed ? C_BONEFNT : (dim ? C_BONEDIM : C_BONE)), FONT_UI);
  if (L.tagX >= 0) {
    const UiRect tag = { r.x + L.tagX, r.y + 6, tagW, 18 };
    canvas_round_rect_outline(tag.x, tag.y, tag.w, tag.h, ROW_R, 1, pres_content_colour(C_GREEN));
    canvas_text(tag.x + PG_TAG_PAD, tag.y + (tag.h - font_height[FONT_SMALL]) / 2, kNow, pres_content_colour(C_GREEN),
                FONT_SMALL);
  }
  if (L.mapX >= 0) canvas_text(r.x + L.mapX, r.y + 10, map, pres_content_colour(C_BONEFNT), FONT_SMALL);
  if (unplayed) {
    const int x = L.pipX >= 0 ? L.pipX : L.barX;
    if (x + canvas_text_width(kNotPlayed, FONT_SMALL) <= L.col[0] - PG_GAP)
      canvas_text(r.x + x, r.y + 10, kNotPlayed, pres_content_colour(C_BONEFNT), FONT_SMALL);
  } else if (L.pipX >= 0) {
    const int py = r.y + (r.h - PG_PIP) / 2;
    const unsigned fill = pres_content_colour(pg_dim(C_GREEN, dim)), rim = pres_content_colour(C_HAZARD);
    for (int i = 0; i < pips; ++i) {
      const int px = r.x + L.pipX + i * (PG_PIP + PG_PIP_GAP);
      if (i < row.found.nukes) { canvas_rect(px, py, PG_PIP, PG_PIP, fill); continue; }
      canvas_rect(px, py, PG_PIP, 1, rim);
      canvas_rect(px, py + PG_PIP - 1, PG_PIP, 1, rim);
      canvas_rect(px, py + 1, 1, PG_PIP - 2, rim);
      canvas_rect(px + PG_PIP - 1, py + 1, 1, PG_PIP - 2, rim);
    }
  }
  pg_readouts(r, L, colW, row.found, row.total, row.haveTotal, !unplayed, dim);
}

// --- This level ----------------------------------------------------------------------------------------
// s cut to maxW with "..." (the route text can outgrow a narrow panel).
static void ar_fit(const char* s, char* out, int cap, int maxW, int font) {
  strncpy_s(out, (size_t)cap, s, _TRUNCATE);
  if (canvas_text_width(out, font) <= maxW) return;
  for (int n = (int)strlen(out); n > 0; --n) {
    _snprintf_s(out, (size_t)cap, _TRUNCATE, "%.*s...", n, s);
    if (canvas_text_width(out, font) <= maxW) return;
  }
  out[0] = 0;
}

static const char* ar_src_hint(int src) {
  switch (src) {
    case AR_SRC_LIVE: return "read from the loaded area.";
    case AR_SRC_FILE: return "read from the game's stored state of the area.";
    case AR_SRC_FRESH: return "not visited yet in this play: nothing found.";
    case AR_SRC_STALE: return "its stored state is older than this part: unknown.";
    case AR_SRC_RECORD: return "as last seen in the area this part.";
    case AR_SRC_DERIVED: return "the count the game's tally leaves for it.";
    case AR_SRC_SETTLING: return "the area has just loaded: unknown for a moment.";
    default: return "its state did not read cleanly: unknown; see nukepanel.log.";
  }
}

// --- Pip rows (rules in areamap.h) -----------------------------------------------------------------------------
// "S": a 9 px square per secret, filled hazard yellow when found, outlined when missing; "N" the same in green; "A" the
// one-shot ambushes in orange, filled once fired. "S ?" grey when unknown, "S 3/12" over the cap or the room.
static int am_prefix_w(void) {
  const int s = canvas_text_width("S", FONT_SMALL), n = canvas_text_width("N", FONT_SMALL),
            a = canvas_text_width("A", FONT_SMALL);
  const int w = s > n ? s : n;
  return w > a ? w : a;
}

// The row's width as drawn.
static int am_row_w(const AmRow& r) {
  const int pw = am_prefix_w();
  char b[16];
  switch (r.mode) {
    case AM_ROW_PIPS: return am_pips_w(r.total, pw);
    case AM_ROW_TEXT: sprintf_s(b, "%d/%d", r.found, r.total); break;
    case AM_ROW_UNKNOWN: strcpy_s(b, "?"); break;
    default: return 0;
  }
  return pw + AM_PIP_PREFIX_GAP + canvas_text_width(b, FONT_SMALL);
}

// One row with its pips' top at y.
static void am_row_draw(int x, int y, const AmRow& r, const char* prefix, unsigned colour, bool dim) {
  if (r.mode == AM_ROW_NONE) return;
  const int pw = am_prefix_w(), th = font_height[FONT_SMALL];
  const int ty = y + (AM_PIP - th) / 2;
  const unsigned c = pres_content_colour(pg_dim(colour, dim));
  canvas_text(x, ty, prefix, pres_content_colour(dim ? C_BONEDIM : C_BONE), FONT_SMALL);
  const int px = x + pw + AM_PIP_PREFIX_GAP;
  if (r.mode == AM_ROW_UNKNOWN) { canvas_text(px, ty, "?", pres_content_colour(C_BONEFNT), FONT_SMALL); return; }
  if (r.mode == AM_ROW_TEXT) {
    char b[16];
    sprintf_s(b, "%d/%d", r.found, r.total);
    canvas_text(px, ty, b, c, FONT_SMALL);
    return;
  }
  for (int i = 0; i < r.total; ++i) {
    const int qx = px + i * (AM_PIP + AM_PIP_GAP);
    if (i < r.found) { canvas_rect(qx, y, AM_PIP, AM_PIP, c); continue; }
    canvas_rect(qx, y, AM_PIP, 1, c);
    canvas_rect(qx, y + AM_PIP - 1, AM_PIP, 1, c);
    canvas_rect(qx, y + 1, 1, AM_PIP - 2, c);
    canvas_rect(qx + AM_PIP - 1, y + 1, 1, AM_PIP - 2, c);
  }
}

// An area's S and N rows, each for its own room in px.
static void am_rows(const ArView* v, int i, int sRoom, int nRoom, AmRow* s, AmRow* n) {
  const ArArea& a = v->part->areas[i];
  const ArFound& f = v->found[i];
  const ArNukeFound& nf = v->nfound[i];
  const int pw = am_prefix_w();
  *s = am_pip_row(a.nsecrets, f.known ? ar_found_count(&a, &f) : 0, f.known, am_pips_fit(sRoom, pw));
  *n = am_pip_row(a.nukeTotal, nf.known ? nf.count : 0, nf.known, am_pips_fit(nRoom, pw));
}

// Its A row: the ambushes that fire once, filled when fired.
static AmRow am_ambush_row(const ArView* v, int i, int room) {
  const ArArea& a = v->part->areas[i];
  const ArKills& k = v->kills[i];
  const int once = ar_once_ambushes(&a);
  return am_pip_row(once, k.known ? once - k.waiting : 0, k.known, am_pips_fit(room, am_prefix_w()));
}

// The widest pip row the part needs (for the map's box width and the list's columns), capped at AM_PIPS_MAX.
static void am_part_rows_w(const ArPart* p, int* sW, int* nW, int* aW = 0) {
  int s = 0, n = 0, m = 0;
  for (int i = 0; i < p->nareas; ++i) {
    if (p->areas[i].nsecrets > s) s = p->areas[i].nsecrets;
    if (p->areas[i].nukeTotal > n) n = p->areas[i].nukeTotal;
    if (ar_once_ambushes(&p->areas[i]) > m) m = ar_once_ambushes(&p->areas[i]);
  }
  const int pw = am_prefix_w();
  *sW = s ? am_pips_w(s < AM_PIPS_MAX ? s : AM_PIPS_MAX, pw) : 0;
  *nW = n ? am_pips_w(n < AM_PIPS_MAX ? n : AM_PIPS_MAX, pw) : 0;
  if (aW) *aW = m ? am_pips_w(m < AM_PIPS_MAX ? m : AM_PIPS_MAX, pw) : 0;
}

// The list's S, N and K columns, right-aligned at the row's end: K last, N and S before it.
struct ArCols { int sX, sW, nX, nW, kX, kW; };
static ArCols ar_cols(const UiRect& r, const ArPart* p) {
  ArCols c;
  am_part_rows_w(p, &c.sW, &c.nW);
  const int minW = canvas_text_width("S 00/00", FONT_SMALL);
  if (c.sW < minW) c.sW = minW;
  if (c.nW < minW) c.nW = minW;
  c.kW = canvas_text_width("K 000/000", FONT_MONO);
  c.kX = r.x + r.w - VALUE_MARGIN - c.kW;
  c.nX = c.kX - PG_COL_GAP * 2 - c.nW;
  c.sX = c.nX - PG_COL_GAP * 2 - c.sW;
  return c;
}

// The level's totals: S as the areas' sum (S ?/7 when it disagrees with the game's own tally), N as the game's own
// tally over the part's nukes, which also counts nukes from spawners and enemies the areas cannot place.
static void ar_row_total(UiRect& cursor, const ArView* v) {
  UiRect r;
  if (!pg_row_begin(cursor, &r)) return;
  static const char kLabel[] = "Total";
  canvas_text(r.x, r.y + 6, kLabel, pres_content_colour(C_BONE), FONT_UI);
  const ArCols c = ar_cols(r, v->part);
  char s[24], n[24];
  if (v->tally == AR_TALLY_OK) sprintf_s(s, "S %d/%d", v->foundSum, v->secretTotal);
  else sprintf_s(s, "S ?/%d", v->secretTotal);
  const int nTotal = v->nukePart >= 0 ? v->nukePart : v->nukeTotal;
  if (v->liveOk) sprintf_s(n, "N %d/%d", v->nukeLive, nTotal);
  else sprintf_s(n, "N ?/%d", nTotal);
  const bool sDone = v->tally == AR_TALLY_OK && v->foundSum >= v->secretTotal;
  const bool nDone = v->liveOk && v->nukeLive >= nTotal;
  canvas_text(c.sX + c.sW - canvas_text_width(s, FONT_MONO), r.y + 7, s, pres_content_colour(pg_dim(C_HAZARD, sDone)),
              FONT_MONO);
  canvas_text(c.nX + c.nW - canvas_text_width(n, FONT_MONO), r.y + 7, n, pres_content_colour(pg_dim(C_GREEN, nDone)),
              FONT_MONO);
  // K: the game's own tally (+0x50) over what the areas place; the part's listed kills beside it when they differ.
  char k[24], note[32];
  ar_kills_text(k, sizeof k, v->killLive, v->killTotal, v->liveOk);
  const bool kDone = v->liveOk && v->killLive >= v->killTotal;
  canvas_text(c.kX + c.kW - canvas_text_width(k, FONT_MONO), r.y + 7, k, pres_content_colour(pg_dim(C_RED, kDone)),
              FONT_MONO);
  note[0] = 0;
  if (v->killGame >= 0) ar_kills_game_note(v->killTotal, v->killGame, note, sizeof note);
  const int noteX = c.sX - PG_GAP - canvas_text_width(note, FONT_SMALL);
  if (note[0] && noteX > r.x + canvas_text_width(kLabel, FONT_UI) + PG_GAP)
    canvas_text(noteX, r.y + 10, note, pres_content_colour(C_BONEFNT), FONT_SMALL);
  char hint[320];
  const char* sPart = v->tally == AR_TALLY_OK ? "Secrets match the game's tally"
                    : v->tally == AR_TALLY_MISMATCH ? "Secrets disagree with the game's tally (see the log)"
                                                    : "Secrets: an area is unknown";
  int extra = 0;   // a blue laser that fired again: nukes the listed total does not count (re/pickups.md section 7)
  for (int i = 0; i < v->part->nareas && i < AR_AREAS_MAX; ++i) extra += v->nfound[i].known ? v->nfound[i].extra : 0;
  int len = 0;
  if (v->nukeTally == AR_TALLY_MISMATCH)
    len = sprintf_s(hint, "%s. Nukes are the game's tally; the areas' %d disagree (see the log).", sPart, v->nukeSum);
  else if (extra > 0)
    len = sprintf_s(hint, "%s. Nukes are the game's tally; %d lie in the areas, and a blue laser that fired again gave "
                    "%d more than the part lists.", sPart, v->nukeTotal, extra);
  else
    len = sprintf_s(hint, "%s. Nukes are the game's tally; %d lie in the areas.", sPart, v->nukeTotal);
  if (len > 0)
    _snprintf_s(hint + len, sizeof hint - len, _TRUNCATE, " Kills are the game's tally over the monsters the areas "
                "place, one per ambush monster and wired egg%s.", note[0] ? "; the game lists another total (see the log)"
                                                                         : "");
  row_hint(hint);
}

// One area, a sub-row: its short name, "You are here" on the current one, the missing secrets' names (faint, small)
// and its S and N pip rows in their columns. Finished dims.
static void ar_row_area(UiRect& cursor, const ArView* v, int i) {
  UiRect r;
  if (!pg_row_begin(cursor, &r)) return;
  const ArArea& a = v->part->areas[i];
  const ArFound& f = v->found[i];
  const int found = f.known ? ar_found_count(&a, &f) : 0;
  const bool done = am_finished(&a, &f, &v->nfound[i]), here = i == v->cur;
  static const char kHere[] = "You are here";
  const char* label = ar_label(&a);
  const ArCols c = ar_cols(r, v->part);
  AmRow s, n;
  am_rows(v, i, c.sW, c.nW, &s, &n);
  char missing[96] = "";
  if (f.known) ar_missing_text(&a, &f, missing, sizeof missing);
  const int labelEnd = r.x + SUB_INDENT + canvas_text_width(label, FONT_UI);
  const int tagW = here ? canvas_text_width(kHere, FONT_SMALL) + 2 * PG_TAG_PAD : 0;
  const int limit = c.sX - PG_GAP;
  int x = labelEnd + PG_GAP;
  canvas_rect(r.x + SUB_RULE_X, r.y, 1, r.h, pres_content_colour(C_RAISED));
  canvas_text(r.x + SUB_INDENT, r.y + 6, label, pres_content_colour(done ? C_BONEDIM : C_BONE), FONT_UI);
  if (tagW && x + tagW <= limit) {
    const UiRect tag = { x, r.y + 6, tagW, 18 };
    canvas_round_rect_outline(tag.x, tag.y, tag.w, tag.h, ROW_R, 1, pres_content_colour(C_GREEN));
    canvas_text(tag.x + PG_TAG_PAD, tag.y + (tag.h - font_height[FONT_SMALL]) / 2, kHere, pres_content_colour(C_GREEN),
                FONT_SMALL);
    x += tagW + PG_GAP;
  }
  if (missing[0] && x + canvas_text_width(missing, FONT_SMALL) > limit)   // the names do not fit: their count does
    sprintf_s(missing, "%d missing", a.nsecrets - found);
  if (missing[0] && x + canvas_text_width(missing, FONT_SMALL) <= limit)
    canvas_text(x, r.y + 10, missing, pres_content_colour(C_BONEFNT), FONT_SMALL);
  const int py = r.y + (r.h - AM_PIP) / 2;
  am_row_draw(c.sX + c.sW - am_row_w(s), py, s, "S", C_HAZARD, done);
  am_row_draw(c.nX + c.nW - am_row_w(n), py, n, "N", C_GREEN, done);
  const ArKills& kl = v->kills[i];
  char k[24];
  ar_kills_text(k, sizeof k, kl.found, a.killTotal, kl.known);
  if (a.killTotal > 0)
    canvas_text(c.kX + c.kW - canvas_text_width(k, FONT_MONO), r.y + 7, k,
                pres_content_colour(pg_dim(C_RED, kl.known && kl.found >= a.killTotal)), FONT_MONO);
  char hint[480];
  sprintf_s(hint, "%s: secrets %s Nukes %s Kills %s N counts the nukes the area places: pickups, crates, barrels, "
            "enemies and blue lasers. K counts its monsters, one per ambush monster and wired egg%s.", a.map,
            ar_src_hint(f.src), ar_src_hint(v->nfound[i].src), ar_src_hint(kl.src),
            kl.known && kl.waiting ? "; an ambush not fired yet is an orange pip on the map" : "");
  row_hint(hint);
}

// A faint line under a row: the route to an area with secrets or nukes still missing, or the total's note.
static void ar_row_route(UiRect& cursor, const char* route) {
  const int h = font_height[FONT_SMALL] + 6;
  if (cursor.h < h) { s_rowsDropped = 1; return; }
  const UiRect r = ui_row(cursor, h, ROW_GAP);
  canvas_rect(r.x + SUB_RULE_X, r.y, 1, r.h, pres_content_colour(C_RAISED));
  char b[AR_ROUTE_CAP + 8];
  ar_fit(route, b, sizeof b, r.w - SUB_INDENT - VALUE_MARGIN, FONT_SMALL);
  canvas_text(r.x + SUB_INDENT, r.y + 2, b, pres_content_colour(C_BONEDIM), FONT_SMALL);
}

// --- The schematic map (layout in areamap.h) ---------------------------------------------------------------
static const int AM_DASH = 4, AM_SPACE = 3, AM_ARROW = 6, AM_NOTCH = 5;

// A link segment, 1 or 2 px (the second line one pixel across the segment's main axis); a depth run is dashed.
static void am_segment(int x0, int y0, int x1, int y1, unsigned c, int thick, bool dashed) {
  const int dx = x1 - x0, dy = y1 - y0;
  const int ox = abs(dx) >= abs(dy) ? 0 : 1, oy = 1 - ox;
  const double len = sqrt((double)dx * dx + (double)dy * dy);
  const double step = dashed && len > AM_DASH ? AM_DASH + AM_SPACE : len + 1.0;
  for (double t = 0.0; t < len || t == 0.0; t += step) {
    const double u = dashed && t + AM_DASH < len ? t + AM_DASH : len;
    const int ax = x0 + (int)(dx * (len > 0 ? t / len : 0)), ay = y0 + (int)(dy * (len > 0 ? t / len : 0));
    const int bx = x0 + (int)(dx * (len > 0 ? u / len : 1)), by = y0 + (int)(dy * (len > 0 ? u / len : 1));
    canvas_line(ax, ay, bx, by, c);
    if (thick > 1) canvas_line(ax + ox, ay + oy, bx + ox, by + oy, c);
    if (len <= 0) break;
  }
}

// A one-way link's arrowhead, its tip on the target box's edge.
static void am_arrow(int fx, int fy, int tx, int ty, unsigned c) {
  const double dx = tx - fx, dy = ty - fy, len = sqrt(dx * dx + dy * dy);
  if (len < 1.0) return;
  const double ux = dx / len, uy = dy / len, bx = tx - ux * AM_ARROW, by = ty - uy * AM_ARROW;
  canvas_tri(tx, ty, (int)(bx - uy * 3.5), (int)(by + ux * 3.5), (int)(bx + uy * 3.5), (int)(by - ux * 3.5), c);
}

// The finished tick, 3 px thick, AM_TICK wide, its foot at (x + 3, y + 9).
static void am_tick(int x, int y, unsigned c) {
  for (int t = 0; t < 3; ++t) {
    canvas_line(x, y + 5 + t, x + 3, y + 8 + t, c);
    canvas_line(x + 3, y + 8 + t, x + AM_TICK - 1, y + 1 + t, c);
  }
}

// One area's box: its label (a tick before it when finished), then its S and N pip rows, or "all found".
static void am_box_draw(const ArView* v, int i, const AmBox& b, int x, int y) {
  const ArArea& a = v->part->areas[i];
  const bool here = i == v->cur,
             done = am_finished(&a, &v->found[i], &v->nfound[i]) && (!ar_once_ambushes(&a) || (v->kills[i].known &&
                                                                                             !v->kills[i].waiting));
  const unsigned green = C_GREEN;
  canvas_round_rect(x, y, b.w, b.h, ROW_R, pres_content_colour(done ? anim_blend_colour(green, C_PANEL, 0.82) : C_RECESS));
  const unsigned rim = done ? anim_blend_colour(green, C_PANEL, 0.5) : here ? green : C_BONEDIM;
  canvas_round_rect_outline(x, y, b.w, b.h, ROW_R, here && !done ? 2 : 1, pres_content_colour(rim));
  if (here) {   // the notch: a small green triangle centred above the box, pointing at it
    const int cx = x + b.w / 2;
    canvas_tri(cx - AM_NOTCH, y - AM_NOTCH - 3, cx + AM_NOTCH, y - AM_NOTCH - 3, cx, y - 2, pres_content_colour(green));
  }
  const int inner = b.w - 2 * AM_PAD_X;
  char label[AR_NAME];
  ar_fit(ar_label(&a), label, sizeof label, inner - (done ? AM_TICK + AM_TICK_GAP : 0), FONT_SMALL);
  const int lw = canvas_text_width(label, FONT_SMALL) + (done ? AM_TICK + AM_TICK_GAP : 0);
  int tx = x + (b.w - lw) / 2;
  const int ly = y + 3;
  if (done) {
    am_tick(tx, ly + (font_height[FONT_SMALL] - 11) / 2, pres_content_colour(green));
    tx += AM_TICK + AM_TICK_GAP;
  }
  canvas_text(tx, ly, label, pres_content_colour(done ? C_BONEDIM : C_BONE), FONT_SMALL);
  const int row0 = y + 24;
  if (done) {
    static const char kAll[] = "all found";
    canvas_text(x + (b.w - canvas_text_width(kAll, FONT_SMALL)) / 2, row0 + (AM_PIP - font_height[FONT_SMALL]) / 2 + 6,
                kAll, pres_content_colour(anim_blend_colour(green, C_PANEL, 0.35)), FONT_SMALL);
    return;
  }
  AmRow s, n;
  am_rows(v, i, inner, inner, &s, &n);
  const AmRow m = am_ambush_row(v, i, inner);
  const int sw = am_row_w(s), nw = am_row_w(n), mw = am_row_w(m);
  const int w = sw > nw ? (sw > mw ? sw : mw) : (nw > mw ? nw : mw);
  const int rx = x + (b.w - w) / 2;
  int ry = row0;
  if (s.mode != AM_ROW_NONE) { am_row_draw(rx, ry, s, "S", C_HAZARD, false); ry += AM_ROW_STEP; }
  if (n.mode != AM_ROW_NONE) { am_row_draw(rx, ry, n, "N", C_GREEN, false); ry += AM_ROW_STEP; }
  am_row_draw(rx, ry, m, "A", C_AMBUSH, false);
}

// The part's areas as boxes and its doorways as links, layered left to right from a01; the way to the area the door
// label points at (else the nearest missing secret) in hazard yellow. Laid out each frame from the 1 Hz view the rows
// below read.
static void ar_map(UiRect& cursor, const ArView* v) {
  const ArPart* p = v->part;
  int labelW[AR_AREAS_MAX] = { 0 };
  for (int i = 0; i < p->nareas && i < AR_AREAS_MAX; ++i)
    labelW[i] = canvas_text_width(ar_label(&p->areas[i]), FONT_SMALL);
  int sW, nW, aW;
  am_part_rows_w(p, &sW, &nW, &aW);
  static AmMap m;
  const int width = cursor.w - SUB_INDENT - VALUE_MARGIN;
  const int rowW = sW > nW ? (sW > aW ? sW : aW) : (nW > aW ? nW : aW);
  am_build(p, v->cur, v->found, labelW, width, &m, v->crossOk ? &v->cross : 0, rowW, am_box_rows(p));
  if (!m.ok) {
    static unsigned said = 0;   // once a session: the panel redraws this every frame
    if (!said++) np_log("progress: the area map of %s does not fit %d px (%d columns)", p->prefix, width, m.nlayers);
    return;
  }
  if (cursor.h < m.h) { s_rowsDropped = 1; return; }
  const UiRect r = ui_row(cursor, m.h, ROW_GAP);
  canvas_rect(r.x + SUB_RULE_X, r.y, 1, r.h, pres_content_colour(C_RAISED));
  const int ox = r.x + SUB_INDENT, oy = r.y;
  for (int pass = 0; pass < 2; ++pass)   // the route over the plain links
    for (int l = 0; l < m.nlinks; ++l) {
      const AmLink& L = m.link[l];
      if (L.route != pass) continue;
      const unsigned c = pres_content_colour(L.route ? C_HAZARD : C_BONEDIM);
      const int t = L.route ? 2 : 1;
      am_segment(ox + L.x0, oy + L.y0, ox + L.xm, oy + L.ym, c, t, L.run != 0);
      am_segment(ox + L.xm, oy + L.ym, ox + L.x1, oy + L.y1, c, t, L.run != 0);
      if (L.oneway) am_arrow(ox + L.xm, oy + L.ym, ox + L.x1, oy + L.y1, c);
    }
  for (int i = 0; i < m.n; ++i) am_box_draw(v, i, m.box[i], ox + m.box[i].x, oy + m.box[i].y);
}

static void ar_this_level_body(UiRect& cursor, const ArView* v);

static void ar_this_level(UiRect& cursor) {
  areas_demand();
  const ArView* v = areas_view();
  if (!v->ok || !v->part) return;
  // The level's name rides on the right: open alone, folded before the counts.
  char level[24], counts[40], kills[24], summary[96];
  sprintf_s(level, "%s", v->part->prefix);
  for (char* c = level; *c; ++c) *c = (char)toupper((unsigned char)*c);
  ar_summary_total(counts, sizeof counts, v->tally, v->foundSum, v->secretTotal, v->liveOk, v->nukeLive,
                   v->nukePart >= 0 ? v->nukePart : v->nukeTotal);
  ar_kills_text(kills, sizeof kills, v->killLive, v->killTotal, v->liveOk);
  sprintf_s(summary, "%s  %s  %s", level, counts, kills);
  const bool open = pg_fold_heading(cursor, PG_HEADING_LEVEL, PG_FOLD_LEVEL, summary, level);
  MenuFoldBlock fold;
  if (collapse_begin(&fold, cursor, open, 1 + v->part->nareas)) ar_this_level_body(cursor, v);
  collapse_end(fold, cursor);
}

static void ar_this_level_body(UiRect& cursor, const ArView* v) {
  ar_row_total(cursor, v);
  int un, unMiss;   // the part's nukes no area places: spawner scripts and enemy drops
  if (ar_unplaced(v->nukePart, v->nukeTotal, v->nukeLive, v->liveOk, v->nukeSum, v->nukeTally != AR_TALLY_UNKNOWN, &un,
                  &unMiss)) {
    char note[96];
    ar_unplaced_text(un, unMiss, note, sizeof note);
    ar_row_route(cursor, note);
  }
  ar_map(cursor, v);
  for (int i = 0; i < v->part->nareas; ++i) {
    ar_row_area(cursor, v, i);
    const ArFound& f = v->found[i];
    const ArNukeFound& nf = v->nfound[i];
    const ArArea& a = v->part->areas[i];
    const bool missS = f.known && ar_found_count(&a, &f) < a.nsecrets, missN = nf.known && nf.count < a.nukeTotal;
    if (i != v->cur && (missS || missN)) ar_row_route(cursor, v->route[i]);
  }
}

// --- Save awards (save-awards.md section 2) ---------------------------------------------------------------
// A grid of the Load Game slot's icons: rows the award, columns the difficulty, each slot earned (tinted glyph),
// missing (its progress) or never played. Tints are the game's (loadgame_awards_panel 0x100a40d0, AABBGGRR) as ARGB.
static const unsigned AW_TINT[LS_DIFFS] = { 0xFF28EF00, 0xFFFFAE12, 0xFFFF1D1E };
static const int AW_LABEL_W = 110, AW_SLOT_H = 44, AW_SLOT_GAP = 6, AW_BAR_H = 3, AW_BAR_PAD = 8;
static const char* const AW_ROW_NAMES[AW_ROWS] = { "Finished", "All nukes", "All secrets", "All kills", "Nuke bonus" };

// A hint on a rect smaller than a row, under a key no row ordinal reaches, so the tooltip restarts per slot.
static void aw_hint(const UiRect& r, int slot, const char* text) {
  if (!ui_hit(r, g_ui.mx, g_ui.my)) return;
  s_hoverRowKey = s_tab * 1000 + 500 + slot;
  strncpy_s(s_hintBuf, sizeof s_hintBuf, text, _TRUNCATE);
  s_hoverHint = s_hintBuf;
}

// A filled polygon of `n` sides (n large: a disc) or a sector, as a triangle fan around (cx, cy). Angles in
// degrees, clockwise from +x on screen.
static void aw_fan(int cx, int cy, double r, double a0, double a1, int n, unsigned c) {
  const double k = 3.14159265358979 / 180.0;
  for (int i = 0; i < n; ++i) {
    const double u = (a0 + (a1 - a0) * i / n) * k, w = (a0 + (a1 - a0) * (i + 1) / n) * k;
    canvas_tri(cx, cy, cx + (int)lround(r * cos(u)), cy + (int)lround(r * sin(u)), cx + (int)lround(r * cos(w)),
               cy + (int)lround(r * sin(w)), c);
  }
}

// The glyphs, centred on (cx, cy); `bg` is the slot's fill, for the cut-outs.
static void aw_glyph(int row, int cx, int cy, unsigned c, unsigned bg) {
  switch (row) {
    case AW_FINISHED:   // the radiation trefoil: three blades, a gap, a hub
      for (int i = 0; i < 3; ++i) aw_fan(cx, cy, 11.0, -120.0 + i * 120.0, -60.0 + i * 120.0, 6, c);
      aw_fan(cx, cy, 4.5, 0.0, 360.0, 16, bg);
      aw_fan(cx, cy, 2.5, 0.0, 360.0, 12, c);
      break;
    case AW_NUKES:      // a hexagon ring
      aw_fan(cx, cy, 11.0, -90.0, 270.0, 6, c);
      aw_fan(cx, cy, 5.5, -90.0, 270.0, 6, bg);
      break;
    case AW_SECRETS:
      canvas_text(cx - canvas_text_width("?", FONT_UI) / 2, cy - font_height[FONT_UI] / 2, "?", c, FONT_UI);
      break;
    case AW_KILLS:      // a skull: the dome, two eyes, a toothed jaw
      canvas_round_rect(cx - 8, cy - 10, 16, 13, 5, c);
      canvas_rect(cx - 5, cy - 6, 4, 4, bg);
      canvas_rect(cx + 1, cy - 6, 4, 4, bg);
      canvas_rect(cx - 5, cy + 3, 10, 6, c);
      canvas_rect(cx - 2, cy + 6, 1, 3, bg);
      canvas_rect(cx + 1, cy + 6, 1, 3, bg);
      break;
    default:
      break;
  }
}

// One slot. Earned: a 2 px tinted rim, a dark tinted fill and the glyph (the bonus its name). Missing on a played
// difficulty: a 1 px dim rim, the count over the total and a 3 px bar. Never played: faint "not played" or "-".
static void aw_slot(const UiRect& r, int row, int diff, const AwCell& cell) {
  const unsigned tint = AW_TINT[diff];
  char text[32];
  aw_cell_text(cell, row, diff, text, sizeof text);
  const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  if (cell.state == AW_EARNED) {
    const unsigned bg = anim_blend_colour(tint, C_PANEL, 0.82);
    canvas_round_rect(r.x, r.y, r.w, r.h, ROW_R, pres_content_colour(bg));
    canvas_round_rect_outline(r.x, r.y, r.w, r.h, ROW_R, 2, pres_content_colour(tint));
    if (text[0]) {
      const int font = canvas_text_width(text, FONT_UI) <= r.w - 8 ? FONT_UI : FONT_SMALL;
      canvas_text(cx - canvas_text_width(text, font) / 2, cy - font_height[font] / 2, text, pres_content_colour(tint),
                  font);
    } else {
      aw_glyph(row, cx, cy, pres_content_colour(tint), pres_content_colour(bg));
    }
    return;
  }
  const bool played = cell.state == AW_MISSING;
  canvas_round_rect_outline(r.x, r.y, r.w, r.h, ROW_R, 1,
                            pres_content_colour(played ? C_RAISED : anim_blend_colour(C_RAISED, C_PANEL, 0.5)));
  const bool counted = played && row != AW_BONUS;
  int font = counted ? FONT_MONO : FONT_SMALL;
  // "240 / 240 (at part end)" too wide for the slot: the note goes on a small line under the count.
  char* note = counted && canvas_text_width(text, font) > r.w - 6 ? strstr(text, " (") : nullptr;
  if (note) *note++ = 0;
  if (canvas_text_width(text, font) > r.w - 6) font = FONT_SMALL;
  const int textH = font_height[font] + (note ? font_height[FONT_SMALL] : 0);
  const int ty = counted ? r.y + (r.h - AW_BAR_PAD - AW_BAR_H - textH) / 2 : cy - font_height[font] / 2;
  canvas_text(cx - canvas_text_width(text, font) / 2, ty, text,
              pres_content_colour(counted ? C_BONE : played ? C_BONEDIM : C_BONEFNT), font);
  if (note)
    canvas_text(cx - canvas_text_width(note, FONT_SMALL) / 2, ty + font_height[font], note,
                pres_content_colour(C_BONEDIM), FONT_SMALL);
  if (!counted) return;
  const int bx = r.x + AW_BAR_PAD, bw = r.w - 2 * AW_BAR_PAD, by = r.y + r.h - AW_BAR_PAD - AW_BAR_H;
  canvas_rect(bx, by, bw, AW_BAR_H, pres_content_colour(C_WELL));
  const int fw = aw_bar_w(cell, bw);
  if (fw > 0) canvas_rect(bx, by, fw, AW_BAR_H, pres_content_colour(tint));
}

// The section: the heading, the column names, the five award rows, then Wozma, the cheated mark and the unused
// icons' footnote. Nothing while the records do not read (the tab's own placeholder says why).
static void aw_body(UiRect& cursor, const AwView* v, const PgNames* names, int colW);

static void aw_section(UiRect& cursor) {
  const AwView* v = progress_awards();
  if (!v->ok) return;
  const PgNames* names = progress_names();
  const int colW = (cursor.w - AW_LABEL_W - VALUE_MARGIN - 2 * AW_SLOT_GAP) / LS_DIFFS;
  if (colW < 40) {
    static unsigned said = 0;   // once a session: the panel redraws this every frame
    if (!said++) np_log("progress: the awards grid does not fit %d px", cursor.w);
    return;
  }
  char summary[32];
  aw_summary(summary, sizeof summary, *v);
  const bool open = pg_fold_heading(cursor, "SAVE AWARDS", PG_FOLD_AWARDS, summary);
  MenuFoldBlock fold;
  if (collapse_begin(&fold, cursor, open, 0)) aw_body(cursor, v, names, colW);   // the grid draws no row ordinals
  collapse_end(fold, cursor);
}

static void aw_body(UiRect& cursor, const AwView* v, const PgNames* names, int colW) {
  const int headH = font_height[FONT_UI] + 6;
  if (cursor.h < headH) { s_rowsDropped = 1; return; }
  {
    const UiRect h = ui_row(cursor, headH, ROW_GAP);
    for (int k = 0; k < LS_DIFFS; ++k) {
      const int x = h.x + AW_LABEL_W + k * (colW + AW_SLOT_GAP);
      canvas_text(x + (colW - canvas_text_width(names->diff[k], FONT_UI)) / 2, h.y + 3, names->diff[k],
                  pres_content_colour(AW_TINT[k]), FONT_UI);
    }
  }
  for (int row = 0; row < AW_ROWS; ++row) {
    if (cursor.h < AW_SLOT_H) { s_rowsDropped = 1; return; }
    const UiRect r = ui_row(cursor, AW_SLOT_H, AW_SLOT_GAP);
    canvas_text(r.x, r.y + (r.h - font_height[FONT_UI]) / 2, AW_ROW_NAMES[row], pres_content_colour(C_BONE), FONT_UI);
    for (int k = 0; k < LS_DIFFS; ++k) {
      const UiRect slot = { r.x + AW_LABEL_W + k * (colW + AW_SLOT_GAP), r.y, colW, r.h };
      aw_slot(slot, row, k, v->cell[row][k]);
      char tip[sizeof s_hintBuf];
      aw_tip(v, row, k, names, tip, sizeof tip);
      aw_hint(slot, row * LS_DIFFS + k, tip);
    }
  }
  if (cursor.h < ROW_H) { s_rowsDropped = 1; return; }
  const UiRect f = ui_row(cursor, ROW_H, 0);
  {
    // The W glyph's green orb before "Wozma beaten"; dim text alone while not beaten.
    const char* t = v->wozma ? "Wozma beaten" : "Wozma not beaten";
    const int dotR = 5, tx = v->wozma ? f.x + 2 * dotR + 6 : f.x;
    if (v->wozma) aw_fan(f.x + dotR, f.y + f.h / 2, dotR, 0.0, 360.0, 12, pres_content_colour(AW_TINT[0]));
    canvas_text(tx, f.y + 6, t, pres_content_colour(v->wozma ? C_BONE : C_BONEFNT), FONT_UI);
    const UiRect hit = { f.x, f.y, tx - f.x + canvas_text_width(t, FONT_UI), f.h };
    aw_hint(hit, 20, AW_WOZMA_TIP);
  }
  char cheated[64];
  if (aw_cheated_text(v->cheated, names, cheated, sizeof cheated)) {
    const int w = canvas_text_width(cheated, FONT_UI);
    const UiRect hit = { f.x + f.w - VALUE_MARGIN - w, f.y, w, f.h };
    canvas_text(hit.x, f.y + 6, cheated, pres_content_colour(AW_TINT[LS_HARD]), FONT_UI);
    aw_hint(hit, 21, AW_CHEATED_TIP);
  }
  const int nh = font_height[FONT_SMALL] + 6;
  if (cursor.h < nh) { s_rowsDropped = 1; return; }
  const UiRect n = ui_row(cursor, nh, ROW_GAP);
  char b[64];
  ar_fit("Unused by the game: All babes, Double damage", b, sizeof b, n.w - VALUE_MARGIN, FONT_SMALL);
  canvas_text(n.x, n.y + 2, b, pres_content_colour(C_BONEFNT), FONT_SMALL);
  const UiRect hit = { n.x, n.y, canvas_text_width(b, FONT_SMALL), n.h };
  aw_hint(hit, 22, AW_UNUSED_TIP);
}

static void pg_records_body(UiRect& cursor, bool inGame, int diff);

static void tab_progress(UiRect& cursor) {
  LsSession ses;
  const bool inGame = levelstats_session(&ses);
  if (inGame) ar_this_level(cursor);
  const int diff = pg_sel_step(&s_pgSel, inGame ? ses.diff : -1, s_pgFresh ? 1 : 0);
  s_pgFresh = false;
  progress_view(diff);   // reads the awards with the records, at the same 1 Hz
  aw_section(cursor);
  // The difficulty selector and every episode fold as one section.
  const PgView* v = progress_view(diff);
  const LsDef* def = levelstats_def();
  const bool rows = v->ok && v->episodes && def;
  PgCounts gf = { 0, 0, 0 }, gt = { 0, 0, 0 };
  int declared = 1 + (rows && diff == LS_HARD ? 1 : 0);
  for (int e = 0; rows && e < v->episodes; ++e) {
    gf.nukes += v->epFound[e].nukes; gf.secrets += v->epFound[e].secrets; gf.kills += v->epFound[e].kills;
    gt.nukes += v->epTotal[e].nukes; gt.secrets += v->epTotal[e].secrets; gt.kills += v->epTotal[e].kills;
    declared += 1 + v->parts[e];
  }
  char summary[80];
  pg_summary_records(summary, sizeof summary, progress_diff_name(diff), rows ? &gf : 0, rows ? &gt : 0);
  const bool open = pg_fold_heading(cursor, PG_HEADING_RECORDS, PG_FOLD_RECORDS, summary);
  MenuFoldBlock fold;
  if (collapse_begin(&fold, cursor, open, declared)) pg_records_body(cursor, inGame, diff);
  collapse_end(fold, cursor);
}

static void pg_records_body(UiRect& cursor, bool inGame, int diff) {
  {
    const int dirty = g_cfgDirty;   // a pick is not a setting: nothing to save
    s_pgSel.picked = row_slider_named(cursor, "Difficulty", diff, 0, LS_DIFFS - 1, progress_diff_name, true);
    g_cfgDirty = dirty;
    row_hint("Opens on the game's own difficulty; each difficulty keeps its own records.");
  }
  const PgView* v = progress_view(s_pgSel.picked);
  const LsDef* def = levelstats_def();
  if (!v->ok) {
    draw_placeholder(cursor, inGame ? "The records could not be read; see nukepanel.log."
                                    : "Load a level to read the records.");
    return;
  }
  if (!v->episodes || !def) {
    draw_placeholder(cursor, "episodes.def has not been read; see nukepanel.log.");
    return;
  }
  if (s_pgSel.picked == LS_HARD) {
    pg_row_hard(cursor, v->hardNukes, v->hardTotal);
    row_hint("Banked Hard parts plus this part so far. The game counts a part only once it ends; the path opens at "
             "the next nuke pickup after the total is banked.");
  }
  int colW[3];
  pg_col_widths(colW);
  const PgNames* names = progress_names();
  for (int e = 0; e < v->episodes; ++e) {
    char heading[64];
    pg_episode_heading(e, names->ep[e], heading, sizeof heading);
    draw_group(cursor, heading);
    {
      pg_row_total(cursor, v->epFound[e], v->epTotal[e], colW);
      row_hint("Finished parts' best counts over every part's total; the bar is nukes.");
      for (int p = 0; p < v->parts[e]; ++p) {
        const PgRow& row = v->rows[e][p];
        pg_row_part(cursor, p, def->partMap[e][p], row, colW);
        char hint[96];
        sprintf_s(hint, "%s: %s", def->partMap[e][p][0] ? def->partMap[e][p] : "part",
                  row.live ? "this run so far; the best is banked at the part's end."
                           : row.state == PG_UNPLAYED ? "not finished on this difficulty yet."
                                                      : "the best of each count over finished runs.");
        row_hint(hint);
      }
    }
  }
}

void menu_draw(int screenW, int screenH) {
  // --- The presentation preamble, once per frame ---------------------------
  s_pres.rowsThisFrame = 0;
  s_rowsDropped = 0;
  s_hoverRowKey = -1;
  s_hoverHint = nullptr;
  s_lastRowKey = -1;
  s_glyphHint = nullptr;
  if (!g_ui.down) { s_pressSpent = false; s_entryCaretDrag = false; }
  s_pressClaimed = false;
  s_entrySeenThisFrame = false;
  s_sliderInvLen = 0; s_sliderInv[0] = 0; s_sliderInvRows = 0;
  s_frameNo++;
  bool newSession = false;
  {
    const unsigned now = menu_now();
    // A gap this long means no frame drew the panel: its state is a finished session's.
    const bool stale = (now - s_pres.lastMs) > ANIM_STALE_MS;
    s_frameDt = anim_dt_clamped(s_pres.lastMs, now);
    s_pres.lastMs = now;
    s_pres.openTarget = input_panel_open() ? 1 : 0;
    if (s_pres.openTarget && (!s_pres.wasOpen || stale)) {
      newSession = true;
      s_pgFresh = true;
      menu_note_new_session();
      for (int i = 0; i < TAB_COUNT; i++) { s_scroll[i] = 0; s_sliderLoggedRows[i] = 0; }
      if (stale) { s_pres.open.x = 0.0; s_pres.open.v = 0.0; }
    }
    s_pres.wasOpen = s_pres.openTarget;
    if (anim_style() == ANIM_OFF) {
      s_pres.open.x = (double)s_pres.openTarget;
      s_pres.open.v = 0.0;
    } else {
      anim_spring_step(&s_pres.open, (double)s_pres.openTarget,
                       ANIM_SPRING_CRITICAL.stiffness, ANIM_SPRING_CRITICAL.damping, s_frameDt);
    }
  }
  // Logical pixels from here on; the canvas scales them and is back at 100 before this returns (the ESP shares it).
  // `settled` is what this screen can show; the drawn scale is held at its pre-press value while the button is down
  // (ui_scale_effective), so the panel resizes and the atlas builds once, on release. A refused atlas draws at 100.
  const int settled = menu_applied_scale(screenW, screenH);
  const int wantPct = ui_scale_effective(settled, s_scaleHeld, g_ui.down);
  if (!g_ui.down) s_scaleHeld = settled;
  canvas_set_scale(wantPct);
  const int pct = canvas_scale();
  s_drawnPct = pct;
  const int deviceMx = g_ui.mx, deviceMy = g_ui.my;
  g_ui.mx = ui_unscale_px(deviceMx, pct);
  g_ui.my = ui_unscale_px(deviceMy, pct);
  const int logicalW = ui_unscale_px(screenW, pct);
  const int logicalH = ui_unscale_px(screenH, pct);
  s_frameMx = g_ui.mx;

  // An open field claims the left press wherever it lands: inside the well it is the caret, outside it is
  // the close (a commit), and neither may also grab the panel. Before the drag machine takes the press.
  if (s_entry.open && (g_ui.pressed || g_ui.rpressed) && !s_dragLive) {
    if (g_ui.pressed) { s_pressClaimed = true; s_pressSpent = true; }
    if (!ui_hit(s_entryFieldRect, g_ui.mx, g_ui.my)) entry_clickaway_commit();
  }

  // --- The drag, before anything is drawn -----------------------------------
  // The grip is asked first inside panel_drag_step, so it wins at the corner.
  const PanelBox laid = panel_drawn_box(screenH, pct, logicalW, logicalH);
  PanelBox box = laid;
  bool gripHit = false;
  {
    const bool titleHit = ui_hit(panel_title_rect(laid, STRIP_H, STATUS_H), g_ui.mx, g_ui.my);
    gripHit = ui_hit(panel_grip_rect(s_dragLive ? s_dragBox : laid), g_ui.mx, g_ui.my);
    bool ended = false;
    s_drag = panel_drag_step(s_drag, g_ui.pressed, g_ui.down, titleHit, gripHit,
                             g_ui.mx, g_ui.my, laid, logicalW, logicalH, &box, &ended, ui_panel_h_cap(screenH, pct));
    s_dragLive = s_drag.phase != PDRAG_IDLE;
    if (ended) {
      box = s_dragBox;
      // A click that moved nothing stores nothing.
      if (box.x != laid.x || box.y != laid.y || box.w != laid.w || box.h != laid.h) {
        g_cfg.panelx = box.x; g_cfg.panely = box.y;
        // Only a resize stores a size: a move at a high UI scale must not save the capped one.
        if (box.w != laid.w) g_cfg.panelw = box.w;
        if (box.h != laid.h) g_cfg.panelh = box.h;
        g_cfgDirty = 1;
      }
    } else if (s_dragLive) {
      s_dragBox = box;
    }
  }
  const UiRect p = { box.x, box.y, box.w, box.h };

  // AA's menu_live_count, reduced to the cheats that exist: Enemies, Zoom out, the freecam.
  // The accuracy switch counts, as AA's accuracy rows do; its speed sliders do not.
  const int liveCount = (g_cfg.espOn ? 1 : 0) + (g_cfg.zoomOutOn ? 1 : 0) + (input_freecam_on() ? 1 : 0) +
                        (g_cfg.accArifle ? 1 : 0) + (g_cfg.silentOn && !aim_refused() ? 1 : 0) +
                        (g_cfg.tbotOn && !aim_tbot_refused() ? 1 : 0);
  const unsigned strip = liveCount ? C_RED : C_HAZARD;

  // The whole panel slides as one unit under Simple; hit tests stay logical.
  if (anim_style() == ANIM_SIMPLE) {
    const int slideY = (int)((1.0 - s_pres.open.x) * (double)SIMPLE_SLIDE_PX + 0.5);
    canvas_set_transform(0, -slideY, 1.0, 0, 0);
  } else {
    canvas_set_transform(0, 0, 1.0, 0, 0);
  }

  canvas_bevel(p.x, p.y, p.w, p.h,
               pres_colour(C_PANEL), pres_colour(C_BEVELHI), pres_colour(C_BEVELLO));
  canvas_hazard(p.x + 1, p.y + 1, p.w - 2, STRIP_H, pres_colour(strip), pres_colour(C_PANEL), 9);

  // Status line: the lamp and one sentence of state.
  const int sy = p.y + 1 + STRIP_H;
  canvas_rect(p.x + 1, sy, p.w - 2, STATUS_H, pres_colour(C_RECESS));
  canvas_rect(p.x + PAD, sy + 11, 9, 9, pres_colour(strip));
  char status[32];
  if (liveCount) sprintf_s(status, "%d cheat%s live", liveCount, liveCount == 1 ? "" : "s");
  else           sprintf_s(status, "Idle");
  canvas_text(p.x + PAD + 18, sy + 7, status, pres_colour(C_BONEDIM), FONT_SMALL);
  // The panel key by name, through the same fallback input.cpp applies.
  char closeText[32];
  sprintf_s(closeText, "%s closes",
            bind_label(panel_key_effective(config_bind_get(BIND_ROW_MENU))));
  canvas_text(p.x + p.w - PAD - canvas_text_width(closeText, FONT_SMALL), sy + 7,
              closeText, pres_colour(C_BONEFNT), FONT_SMALL);

  const int bodyY = sy + STATUS_H;
  const int bodyH = p.y + p.h - bodyY - 1;

  // Tab rail: every tab, in AA's order less Items. The click is applied after the content, so a pending commit is
  // taken by the row that owns it.
  canvas_rect(p.x + 1, bodyY, RAIL_W, bodyH, pres_colour(C_WELL));
  int railTab = -1;
  for (int i = 0; i < TAB_COUNT; i++) {
    const UiRect t = { p.x + 1, bodyY + i * MENU_RAIL_ENTRY_H, RAIL_W, MENU_RAIL_ENTRY_H };
    if (i == s_tab) {
      canvas_rect(t.x, t.y, t.w, t.h, pres_colour(C_PANEL));
      canvas_rect(t.x, t.y, 3, t.h, pres_colour(TAB_COLOURS[i]));
    }
    const int nameY = t.y + (t.h - font_height[FONT_UI]) / 2;
    canvas_text(t.x + RAIL_LABEL_X, nameY, TAB_NAMES[i],
                pres_colour(i == s_tab ? C_BONE : C_BONEDIM), FONT_UI);
    if (g_ui.pressed && ui_hit(t, g_ui.mx, g_ui.my)) {
      railTab = i;
      s_pressClaimed = true;
    }
  }

  s_tabSwitchedThisFrame = (s_tab != s_pres.tabFrom);
  if (s_tabSwitchedThisFrame) s_pres.tabSwitchMs = s_pres.lastMs;
  s_pres.tabFrom = s_tab;
  s_foldSnap = s_tabSwitchedThisFrame || newSession;

  // --- The tab body scrolls ------------------------------------------------
  // The offset comes off the cursor's starting y, so draw and hit test follow
  // together; content is laid out into MENU_CONTENT_BUDGET and measured whole.
  const UiRect bodyRect = { p.x + 1 + RAIL_W, bodyY, p.w - RAIL_W - 2, bodyH };
  s_bodyRect = bodyRect;
  s_ddSeenThisFrame = false;
  const int viewH = bodyH - 2 * PAD;
  s_scroll[s_tab] = menu_scroll_after_wheel(s_scroll[s_tab], s_wheel, s_contentH[s_tab], viewH);
  s_wheel = 0.0;
  const int scroll = s_scroll[s_tab];

  UiRect cursor = { p.x + 1 + RAIL_W + PAD, bodyY + PAD - scroll, p.w - RAIL_W - 2 * PAD - 2, MENU_CONTENT_BUDGET };
  const int contentTop = cursor.y;
  canvas_push_clip(bodyRect.x, bodyRect.y, bodyRect.w, bodyRect.h);
  s_clipNow = bodyRect;
  s_foldAlpha = 1.0;
  // The clip stops a scrolled-out row being drawn; parking the cursor stops
  // one being clicked outside the body.
  const int bodyMx = g_ui.mx, bodyMy = g_ui.my;
  if (!ui_hit(bodyRect, g_ui.mx, g_ui.my)) {
    g_ui.mx = MENU_MOUSE_NOWHERE; g_ui.my = MENU_MOUSE_NOWHERE;
  }
  switch (s_tab) {
    case TAB_VISUALS: {
      // AA's Visuals rows (its menu.cpp:1809-1983), keeping what exists here. No
      // ENEMIES heading: it would repeat its own first row.
      g_cfg.espOn = row_switch(cursor, "Enemies", g_cfg.espOn, C_RED, true);
      // Hidden while the parent is off (folds): none draws on its own.
      const bool enemiesOn = g_cfg.espOn != 0;
      MenuFoldBlock enemyBlock;
      if (collapse_begin(&enemyBlock, cursor, enemiesOn, 5)) {
        // AA's order: Bounding boxes first, health bars above the name (its menu.cpp:1821-1823). No Level row: the
        // variant is always appended.
        g_cfg.espBoundingBoxes = row_switch_sub(cursor, "Bounding boxes",  g_cfg.espBoundingBoxes, C_RED, enemiesOn);
        g_cfg.espHealthBars    = row_switch_sub(cursor, "Health bars",    g_cfg.espHealthBars,    C_RED, enemiesOn);
        // "Name and health" replaces the separate Names and Health numbers rows (AA's menu.cpp:1821): one switch
        // writes both existing config keys, so there is no migration and esp.cpp's gates are untouched.
        {
          const int nameHealth = row_switch_sub(cursor, "Name and health", g_cfg.espNames, C_RED, enemiesOn);
          g_cfg.espNames = nameHealth;
          g_cfg.espHealthNumbers = nameHealth;
        }
        g_cfg.espDistance      = row_switch_sub(cursor, "Distance",       g_cfg.espDistance,      C_RED, enemiesOn);
        g_cfg.espAmbushes      = row_switch_sub(cursor, "Ambushes",       g_cfg.espAmbushes,      C_RED, enemiesOn);
        row_hint("Spawners that have not released their monsters yet, and the zone that triggers them.");
      }
      collapse_end(enemyBlock, cursor);

      // One WORLD group, AA's own heading (its menu.cpp:1862-1868): AA has no OBJECTIVES heading, so Items, Nukes,
      // Babes and Secret areas sit together under WORLD in AA's order, item ESP first.
      draw_group(cursor, "WORLD");
      g_cfg.espItems    = row_switch(cursor, "Items and pickups", g_cfg.espItems, C_BLUE, true);
      MenuFoldBlock itemBlock;
      if (collapse_begin(&itemBlock, cursor, g_cfg.espItems != 0, 1)) {
        g_cfg.espHideTaken = row_switch_sub(cursor, "Hide what you can't take", g_cfg.espHideTaken, C_BLUE,
                                            g_cfg.espItems != 0);
        row_hint("Hides ammo while its pool is full and Ego the game never picks up; the rest is always taken.");
      }
      collapse_end(itemBlock, cursor);
      g_cfg.espNukes      = row_switch(cursor, "Nukes", g_cfg.espNukes, C_GREEN, true);
      MenuFoldBlock nukeBlock;
      if (collapse_begin(&nukeBlock, cursor, g_cfg.espNukes != 0, 1))
        g_cfg.espNukesArrow = row_switch_sub(cursor, "Off-screen arrow", g_cfg.espNukesArrow, C_GREEN,
                                             g_cfg.espNukes != 0);
      collapse_end(nukeBlock, cursor);
      g_cfg.espBabes      = row_switch(cursor, "Babes", g_cfg.espBabes, C_FRIENDLY, true);
      MenuFoldBlock babeBlock;
      if (collapse_begin(&babeBlock, cursor, g_cfg.espBabes != 0, 1))
        g_cfg.espBabesArrow = row_switch_sub(cursor, "Off-screen arrow", g_cfg.espBabesArrow, C_FRIENDLY,
                                             g_cfg.espBabes != 0);
      collapse_end(babeBlock, cursor);
      g_cfg.espSecrets      = row_switch(cursor, "Secret areas", g_cfg.espSecrets, C_HAZARD, true);
      MenuFoldBlock secretBlock;
      if (collapse_begin(&secretBlock, cursor, g_cfg.espSecrets != 0, 2)) {
        g_cfg.espSecretsArrow = row_switch_sub(cursor, "Off-screen arrow", g_cfg.espSecretsArrow, C_HAZARD,
                                               g_cfg.espSecrets != 0);
        // Singular, as AA names it (its menu.cpp:1903); the config key is
        // unchanged, this is a label-only rename.
        g_cfg.espSecretVolumes = row_switch_sub(cursor, "Secret volume", g_cfg.espSecretVolumes, C_HAZARD,
                                                g_cfg.espSecrets != 0);
        row_hint("Translucent boxes over secret trigger areas.");
      }
      collapse_end(secretBlock, cursor);
      // AA's own row and colour, after the secrets as AA has it (its menu.cpp:1943).
      g_cfg.espExplodables = row_switch(cursor, "Explodables", g_cfg.espExplodables, C_HAZARD, true);
      row_hint("Hides walls that are broken or can't be damaged yet.");
      // The two fixed-order puzzles (re/puzzle-e05p02.md, re/puzzle-e07p03.md); user's row, 2026-10-02.
      g_cfg.espPuzzles = row_switch(cursor, "Puzzle solutions", g_cfg.espPuzzles, C_HAZARD, true);
      row_hint("Next step of the floor-plate and cog puzzles, near one and until it is solved.");
      g_cfg.dmgNumbers = row_switch(cursor, "Damage numbers", g_cfg.dmgNumbers, C_HAZARD, true);
      row_hint(dmgnum_armed() ? "Your own hits only." : "Damage hook not armed");
      // The part's counts in the level.
      g_cfg.levelStats = row_switch(cursor, "Level stats", g_cfg.levelStats, C_HAZARD, true);
      row_hint("Kills, secrets and nukes in the current part, top left, as the level-end screen counts them.");
      break;
    }
    case TAB_PROGRESS:
      tab_progress(cursor);
      break;
    case TAB_COMBAT: {
      // AA's TARGETING rows, labels, hints and ranges (its menu.cpp:2015-2096). No Aimbot rows: the camera follows
      // the track, not Duke's aim (aim.md section 2). No Pick target/Cone/Max redirect (side-scroller): silent aim and
      // the triggerbot's locked mode take the nearest visible enemy in any direction, Duke's back included.
      {
        // AA's rows (its menu.cpp:2064-2082): the switch, then Fire when and Delay under it.
        const bool tbRefused = aim_tbot_refused() != 0;
        g_cfg.tbotOn = row_switch_avail(cursor, "Triggerbot", g_cfg.tbotOn, C_RED, true, !tbRefused);
        row_hint(tbRefused ? "The frame-start hook was refused. See nukepanel.log."
                           : "Fires by itself while the condition below holds");
        const bool tbOn = g_cfg.tbotOn != 0 && !tbRefused;
        g_cfg.tbotWhen = row_slider_named_sub(cursor, "Fire when", g_cfg.tbotWhen, TBOT_WHEN_CROSSHAIR,
                                              TBOT_WHEN_LOCKED, tbot_when_label, tbOn);
        row_hint("Crosshair: an enemy first in your line of fire. Not with the Pipebomb. With the X-3000, either "
                 "setting fires at any enemy the weapon can reach, through walls.");
        g_cfg.tbotDelay = row_slider_sub(cursor, "Delay", g_cfg.tbotDelay, TBOT_DELAY_MS_MIN, TBOT_DELAY_MS_MAX, " ms",
                                         tbOn, TBOT_DELAY_MS_DEF);
        row_hint("How long the condition must hold before it fires.");
      }
      // Armed only once switched on; a refusal then greys the row, as AA does (its row_switch(..., sa)).
      g_cfg.silentOn = row_switch_avail(cursor, "Silent aim", g_cfg.silentOn, C_RED, true, !aim_refused());
      row_hint(aim_refused() ? "The fire-direction hook was refused. See nukepanel.log."
                             : "Shots go to the target without moving your view, wherever it is.");
      // TARGETING is shown while Silent aim or the Triggerbot is live, as AA's tgtOn: the Triggerbot's Locked mode
      // uses these values too.
      const bool tgtOn = (g_cfg.silentOn != 0 && !aim_refused()) || (g_cfg.tbotOn != 0 && !aim_tbot_refused());
      MenuFoldBlock tgtBlock;
      if (collapse_begin(&tgtBlock, cursor, tgtOn, 6)) {
        draw_group(cursor, "TARGETING");
        g_cfg.aimPoint = row_slider_named(cursor, "Aim at", g_cfg.aimPoint, AIM_AT_BODY, AIM_AT_HEAD,
                                          aim_point_label, tgtOn);
        row_hint("Head aims at the head bone where the enemy has one.");
        g_cfg.aimRangeM = row_slider(cursor, "Aim range", g_cfg.aimRangeM, AIM_RANGE_MIN_M, AIM_RANGE_MAX_M, " m",
                                     tgtOn, AIM_RANGE_MAX_M);
        // User decision 2026-09-29: a switch, default on (re/offscreen-and-track.md A).
        g_cfg.aimOffscreen = row_switch_avail(cursor, "Off-screen targets", g_cfg.aimOffscreen, C_RED, tgtOn,
                                              !offscreen_refused());
        row_hint(offscreen_refused() ? "The bullet-clip hook was refused. See nukepanel.log."
                                     : "Shots reach enemies past the screen edge.");
        g_cfg.aimLead = row_switch(cursor, "Lead moving targets", g_cfg.aimLead, C_RED, tgtOn);
        row_hint("Only the PRPG and the Pulse Cannon need it; bullets arrive at once.");
        g_cfg.aimLeadPct = row_slider_sub(cursor, "Lead amount", g_cfg.aimLeadPct, 0, 100, "%",
                                          tgtOn && g_cfg.aimLead != 0, AIM_LEAD_DEF);
        g_cfg.aimMarker = row_switch(cursor, "Aim calibration marker", g_cfg.aimMarker, C_RED, tgtOn);
        row_hint("Shows the raw aim point and the led point.");
      }
      collapse_end(tgtBlock, cursor);
      break;
    }
    case TAB_WEAPONS: {
      // AA's Weapons tab (its menu.cpp:2102-2280); only the rifle has spread (weapons.md section 5).
      draw_group(cursor, "PERFECT ACCURACY");
      g_cfg.accArifle = row_switch(cursor, "Assault Rifle", g_cfg.accArifle, C_HAZARD, true);
      row_hint("Removes this weapon's own spread.");
      draw_group(cursor, "HANDLING");
      {
        char val[16];
        const int fire = weap_fire_pct_clamped(g_cfg.fireRatePct);
        weap_fire_text(fire, val, sizeof val);
        // Put back on the 50 stops, as AA's firerate_rate_clamped does, so the knob only rests on one.
        const int v = weap_fire_pct_clamped(row_slider_at(cursor, "Fire rate", fire, WEAP_FIRE_MIN, WEAP_FIRE_MAX,
                                                          val, true, 0, C_BONE, EK_INT, WEAP_FIRE_MIN));
        if (v != g_cfg.fireRatePct) { g_cfg.fireRatePct = v; g_cfgDirty = 1; }
      }
      {
        // Max is one knob stop past 1000, stored as 1100 (AA's RELOADRATE_SLIDER_MAX).
        char val[16];
        const int rel = weap_reload_pct_clamped(g_cfg.reloadPct);
        weap_reload_text(rel, val, sizeof val);
        const int v = weap_reload_pct_clamped(row_slider_at(cursor, "Reload speed", rel, WEAP_FIRE_MIN,
                                                            WEAP_RELOAD_SLIDER_MAX, val, true, 0, C_BONE,
                                                            EK_INT, WEAP_FIRE_MIN));
        if (v != g_cfg.reloadPct) { g_cfg.reloadPct = v; g_cfgDirty = 1; }
      }
      row_hint("Only the Golden Eagle reloads.");
      break;
    }
    case TAB_VIEW: {
      // AA's pair (its menu.cpp:2385-2397): the switch, then the slider as its sub-row. The value is the
      // engine's own horizontal degrees here; the reset returns the captured stock.
      draw_group(cursor, "FIELD OF VIEW");
      {
        const bool fovOk = fov_available();
        g_cfg.fovOn = row_switch_avail(cursor, "Custom field of view", g_cfg.fovOn, C_GREEN, true, fovOk);
        row_hint("Writes the game's own field of view. Off gives the stock value back.");
        MenuFoldBlock fovBlock;
        if (collapse_begin(&fovBlock, cursor, fovOk && g_cfg.fovOn != 0, 1)) {
          double stock = 0.0;
          const bool haveStock = fov_stock(&stock);
          const int shown = fov_slider_value(g_cfg.fovDeg, stock);
          char buf[24];
          fov_value_text(g_cfg.fovDeg, stock, haveStock ? 1 : 0, buf, sizeof buf);
          // def FOV_STOCK (0) sits outside [lo, hi]: row_slider_at admits it, as the frame cap's 0 does.
          const int r = row_slider_at(cursor, "Field of view", shown, FOV_MIN_DEG, FOV_MAX_DEG, buf,
                                      fovOk && g_cfg.fovOn != 0, SUB_INDENT, C_BONEDIM, EK_INT, FOV_STOCK);
          if (r != shown) g_cfg.fovDeg = fov_cfg_from_slider(r);
          row_hint("Horizontal degrees; the reset arrow gives the stock value back (shown with a decimal).");
        }
        collapse_end(fovBlock, cursor);
      }

      draw_group(cursor, "FREE CAMERA");
      // AA-style switch first, settings indented under it, every key row top-level (its menu.cpp:2382-2576).
      // The switch arms the camera's keys and starts no flight (panel clean-up 2026-10-02); input.cpp ends a
      // flight when it goes off.
      g_cfg.freecamOn = row_switch(cursor, "Free camera", g_cfg.freecamOn, C_GREEN, true);
      row_hint("Turns on the Camera, Teleport, Copy view, Paste view and Photo keys below.");
      const bool camOn = g_cfg.freecamOn != 0;
      // The settings and the four key rows fold together: the switch arms those keys.
      MenuFoldBlock camBlock;
      if (collapse_begin(&camBlock, cursor, camOn, 13)) {   // seven settings, five key rows, the photo folder
        // A percentage, as AA's (its menu.cpp:2415-2418); 100 % is the old 8 m/s default (panel clean-up).
        g_cfg.freecamSpeedPct = row_slider_sub(cursor, "Flight speed", g_cfg.freecamSpeedPct, FREECAM_SPEED_MIN_PCT,
                                               FREECAM_SPEED_MAX_PCT, "%", camOn, FREECAM_SPEED_DEF_PCT);
        row_hint("100% is 8 m/s; Shift flies 4 times faster, Alt 4 times slower.");
        g_cfg.freecamPause = row_switch_sub(cursor, "Pause the game", g_cfg.freecamPause, C_GREEN, camOn);
        row_hint("Freezes the world while the camera flies; it runs again when the camera ends.");
        g_cfg.freecamMarker = row_switch_sub(cursor, "Show player", g_cfg.freecamMarker, C_GREEN, camOn);
        row_hint("A cyan marker on Duke while the camera flies.");
        // AA's own order (its menu.cpp:2382-2602): Debug readout between Show player and Teleport marker.
        g_cfg.freecamDebug = row_switch_sub(cursor, "Debug readout", g_cfg.freecamDebug, C_GREEN, camOn);
        row_hint("Shows the camera's position and angle while flying.");
        g_cfg.teleportMarker = row_switch_sub(cursor, "Teleport marker", g_cfg.teleportMarker, C_GREEN, camOn);
        row_hint("Shows where Duke would land; red where he cannot.");
        g_cfg.freecamShot = row_switch_sub(cursor, "Screenshot mode", g_cfg.freecamShot, C_GREEN, camOn);
        row_hint("Hides the game HUD and all Nuke Panel overlays while flying.");
        {
          // The readout column holds nine cells (tb_0339), so the pixel size takes the line under the slider.
          const int mult = photo_mult_clamped(g_cfg.photoMult);
          char val[8], size[48];
          sprintf_s(val, "%dx", mult);
          const int v = row_slider_at(cursor, "Photo size", mult, PHOTO_MULT_MIN, PHOTO_MULT_MAX, val, camOn,
                                      SUB_INDENT, C_BONEDIM, EK_INT, PHOTO_MULT_DEF);
          if (v != g_cfg.photoMult) { g_cfg.photoMult = v; g_cfgDirty = 1; }
          row_hint("The screen times this on each side, as a PNG in the Photo folder.");
          int sw = 0, shh = 0;
          photo_screen_size(&sw, &shh);
          photo_size_text(mult, sw, shh, size, sizeof size);
          if (sw > 0 && shh > 0) draw_placeholder(cursor, size);
        }
        // Every key row top-level, AA's own shape (its menu.cpp:2561-2576): a
        // rebind here works whether or not the camera is currently flying.
        row_bind(cursor, "Camera key", BIND_ROW_CAMERA);
        row_hint("Toggles the camera; W/A/S/D, Space, Ctrl, Shift and Alt fly it and cannot be bound here.");
        row_bind(cursor, "Teleport key", BIND_ROW_TELEPORT);
        row_hint("Moves Duke to the spot under the crosshair. The camera's movement keys cannot be bound here.");
        // AA's two clipboard keys (its menu.cpp:2579-2581); the line is our own, tagged dnmp (campos.h).
        row_bind(cursor, "Copy view key", BIND_ROW_CAMCOPY);
        row_hint("Copies the camera's view to the clipboard. The movement keys cannot be bound here.");
        row_bind(cursor, "Paste view key", BIND_ROW_CAMPASTE);
        row_hint("Flies the camera to a copied view. The movement keys cannot be bound here.");
        row_bind(cursor, "Photo key", BIND_ROW_PHOTO);
        row_hint("Takes the photo while flying; again cancels. Files go to the Photo folder.");
        row_folder(cursor, "Photo folder");
        row_hint("Where photos are saved. Click to choose a folder.");
      }
      collapse_end(camBlock, cursor);

      draw_group(cursor, "CAMERA");
      g_cfg.zoomOutOn = row_switch(cursor, "Zoom out", g_cfg.zoomOutOn, C_GREEN, true);
      row_hint("Pulls the camera straight back along its view, through walls.");
      // Placed next to Zoom out, not this feature's own group: it is the same
      // camera question, only driven by the wheel instead of the switch.
      g_cfg.wheelZoom = row_switch(cursor, "Scroll-wheel zoom", g_cfg.wheelZoom, C_GREEN, true);
      row_hint("Its own distance, 1 m a notch, not saved: scroll back to pull the camera out, forward past 1 m to return.");
      MenuFoldBlock wheelBlock;
      if (collapse_begin(&wheelBlock, cursor, g_cfg.wheelZoom != 0, 1)) {
        g_cfg.wheelSmooth = row_switch_sub(cursor, "Smooth scrolling", g_cfg.wheelSmooth, C_GREEN, true);
        row_hint("Eases the zoom-out distance, and the field of view when the freecam wheel changes it.");
      }
      collapse_end(wheelBlock, cursor);
      g_cfg.zoomOutDist = row_slider(cursor, "Distance", g_cfg.zoomOutDist, ZOOMOUT_MIN_M, ZOOMOUT_MAX_M,
                                     " m", true, ZOOMOUT_DEF_M);
      // The reset arrow resets the wheel's own distance too.
      if (s_resetPressedThisRow) wheelzoom_reset_distance();
      row_hint("How far back Zoom out and the hold key put the camera; the reset also resets the wheel's.");
      row_bind(cursor, "Hold to zoom out", BIND_ROW_HOLDZOOMOUT);
      row_hint("Zooms out at this distance while held, even with the switch off.");
      {
        const bool cutOk = !cutaway_refused();
        g_cfg.cutaway = row_switch_avail(cursor, "Cut-away walls", g_cfg.cutaway, C_GREEN, true, cutOk);
        row_hint(cutOk ? "Makes every cut-away wall transparent, as if Duke were in its room."
                       : "The fader check failed. See nukepanel.log.");
      }

      // AA has no such group; the rows are this menu's own wanted-feature list. Mouse aim replaces the planned
      // "Mouse look" row.
      draw_group(cursor, "MOUSE LOOK");
      {
        const bool fireOk = !aim_refused();
        g_cfg.mouseAim = row_switch_avail(cursor, "Mouse aim", g_cfg.mouseAim, C_RED, true, fireOk);
        row_hint(fireOk ? "Shots follow the cursor; the left mouse button fires (the game's own bind)."
                        : "The fire-direction hook was refused. See nukepanel.log.");
        const bool maOn = g_cfg.mouseAim != 0 && fireOk;
        // Sensitivity, Show reticle and the reticle block fold under Mouse aim.
        MenuFoldBlock maBlock;
        if (collapse_begin(&maBlock, cursor, maOn, 8)) {   // two rows and the reticle block's six
          // Face the cursor and Torso bend are always on (panel clean-up 2026-10-02); a refusal is in the log.
          g_cfg.mouseSens = row_slider_sub(cursor, "Sensitivity", g_cfg.mouseSens, MA_SENS_MIN, MA_SENS_MAX, "%",
                                           maOn, MA_SENS_DEF);
          row_hint("How far the reticle moves for the mouse; 100% moves as the game's own cursor.");
          g_cfg.mouseReticle = row_switch_sub(cursor, "Show reticle", g_cfg.mouseReticle, C_RED, maOn);
          row_hint("Draws the aim reticle at the cursor.");
          // Hidden, not greyed, while Show reticle is off (panel clean-up 2026-10-02).
          const bool retOn = maOn && g_cfg.mouseReticle != 0;
          MenuFoldBlock retBlock;
          if (collapse_begin(&retBlock, cursor, g_cfg.mouseReticle != 0, 6)) {   // five rows and the preview strip
            g_cfg.retColour = row_slider_named_sub(cursor, "Reticle colour", g_cfg.retColour, 0, MA_RET_COLOURS - 1,
                                                   ma_reticle_colour_label, retOn);
            g_cfg.retSize = row_slider_sub(cursor, "Reticle size", g_cfg.retSize, MA_RET_SIZE_MIN, MA_RET_SIZE_MAX,
                                           " px", retOn, MA_RET_SIZE_DEF);
            g_cfg.retThick = row_slider_sub(cursor, "Reticle thickness", g_cfg.retThick, MA_RET_THICK_MIN,
                                            MA_RET_THICK_MAX, " px", retOn, MA_RET_THICK_DEF);
            g_cfg.retDot = row_switch_sub(cursor, "Centre dot", g_cfg.retDot, C_RED, retOn);
            g_cfg.retOutline = row_switch_sub(cursor, "Outline", g_cfg.retOutline, C_RED, retOn);
            row_hint("A black edge that keeps the reticle visible on bright and dark scenes.");
            row_reticle_strip(cursor, retOn);
          }
          collapse_end(retBlock, cursor);
        }
        collapse_end(maBlock, cursor);
      }
      // Independent switches: First person works whatever Move anywhere is.
      g_cfg.firstPerson = row_switch(cursor, "First person", g_cfg.firstPerson, C_GREEN, true);
      row_hint("The view from Duke's eyes: the mouse turns and looks, the arrow keys walk anywhere.");
      MenuFoldBlock fpBlock;
      if (collapse_begin(&fpBlock, cursor, g_cfg.firstPerson != 0, 3)) {
        row_bind(cursor, "Crouch key", BIND_ROW_FP_CROUCH);
        row_hint("Hold to crouch in first person; the view comes down with Duke.");
        row_bind(cursor, "Interact key", BIND_ROW_INTERACT);
        row_hint("Uses doors, switches and ladders in first person.");
        g_cfg.fpJumpPct = row_slider_sub(cursor, "Double jump strength", g_cfg.fpJumpPct, 100, 150, "%",
                                         g_cfg.firstPerson != 0, 120);
        row_hint("How high the second jump lifts Duke in first person, where he cannot grab ledges.");
      }
      collapse_end(fpBlock, cursor);
      {
        const bool mvOk = !moveanywhere_refused();
        g_cfg.moveAnywhere = row_switch_avail(cursor, "Move anywhere", g_cfg.moveAnywhere, C_GREEN, true, mvOk);
        row_hint(mvOk ? "The game's own camera and controls, but the track no longer holds Duke."
                      : "The track hook was refused. See nukepanel.log.");
        row_bind(cursor, "Interact key", BIND_ROW_INTERACT);
        row_hint("Uses doors, switches and ladders while Up and Down move in depth (also in first person).");
      }
      g_cfg.rescueFalls = row_switch(cursor, "Rescue falls", g_cfg.rescueFalls, C_GREEN, true);
      row_hint("A fall with no floor below puts Duke back on the last ground he stood on.");
      break;
    }
    case TAB_SETTINGS: {
      draw_group(cursor, "HOTKEYS");
      row_bind(cursor, "Panel key", BIND_ROW_MENU);
      row_hint("The key that opens and closes this panel.");

      // Game settings before PANEL, as AA's AUTOSAVE and STARTUP (its menu.cpp:2613-2689).
      draw_group(cursor, "FRAME RATE");
      g_cfg.frameCapOn = row_switch(cursor, "Frame cap", g_cfg.frameCapOn, C_GREEN, true);
      row_hint("Paces the frame rate to the value below.");
      // A sub-row: it does nothing while Frame cap is off (AA's fov pair, its menu.cpp:2361-2370). Folded with
      // the VSync note, which takes no row ordinal.
      MenuFoldBlock capBlock;
      if (collapse_begin(&capBlock, cursor, g_cfg.frameCapOn != 0, 1)) {
        FcCapInfo capInfo;
        {
          // 0 stored means "follow the monitor": the widget draws and drags at the effective fps but writes back
          // only when the row itself changed (a drag, or the reset glyph's 0). gl_sync forces vsync at the
          // launcher's Hz, so the slider's hi and the shown value follow that; no stored value above it is offered.
          capInfo = framecap_cap_info(input_window());
          const bool following = g_cfg.frameCapFps < 30;
          const int effective = clamp_int(capInfo.effectiveFps, 30, capInfo.sliderHi);
          char buf[24];
          menu_cap_readout(buf, sizeof buf, effective, following ? 1 : 0);
          const int r = row_slider_at(cursor, "Cap", effective, 30, capInfo.sliderHi, buf,
                                      g_cfg.frameCapOn != 0, SUB_INDENT, C_BONEDIM, EK_INT, 0);
          if (r != effective) g_cfg.frameCapFps = r;
        }
        row_hint("Frames per second at the cap; the reset arrow follows the monitor's refresh rate.");
        if (capInfo.vsyncCapped) {
          char buf[64];
          sprintf_s(buf, "VSync is on: the game cannot exceed %d fps", capInfo.sliderHi);
          draw_placeholder(cursor, buf);
        }
      }
      collapse_end(capBlock, cursor);
      g_cfg.jumpFix = row_switch(cursor, "Fix double jump", g_cfg.jumpFix, C_GREEN, true);
      row_hint("Keeps the double jump working at high frame rates.");
      g_cfg.queenFix = row_switch(cursor, "Fix Queen swipe", g_cfg.queenFix, C_GREEN, true);
      row_hint("The Cockroach Queen's running swipe hits once per swipe, as designed, not once a frame.");

      // The switch is not saved, off at every launch; the Speed value and the key are.
      draw_group(cursor, "GAME SPEED");
      g_gameSpeedOn = row_switch(cursor, "Game speed", g_gameSpeedOn, C_GREEN, true);
      row_hint("Scales the whole game's clock, like a speedhack. Sound keeps its normal speed.");
      MenuFoldBlock speedBlock;
      if (collapse_begin(&speedBlock, cursor, g_gameSpeedOn != 0, 1)) {
        const int cur = gs_pct_snap(g_cfg.gameSpeedPct);
        const int v = gs_pct_snap(row_slider_sub(cursor, "Speed", cur, GS_PCT_MIN, GS_PCT_MAX, "%",
                                                 g_gameSpeedOn != 0, GS_PCT_DEF));
        if (v != g_cfg.gameSpeedPct) { g_cfg.gameSpeedPct = v; g_cfgDirty = 1; }
        row_hint("100% is normal speed; lower is slow motion. The free camera flies at its own speed.");
      }
      collapse_end(speedBlock, cursor);
      row_bind(cursor, "Game speed key", BIND_ROW_GAMESPEED);
      row_hint("Turns Game speed on and off.");

      // Read at each save file's open (saveio.h): a change applies from the next save.
      draw_group(cursor, "SAVES");
      g_cfg.fastSaves = row_switch(cursor, "Fast saves", g_cfg.fastSaves, C_GREEN, true);
      row_hint("Collects the game's many tiny save writes into a few large ones; fixes the short freeze when the "
               "game saves.");

      // AA's STARTUP group, before DISPLAY (its menu.cpp:2709-2713). Stored only: the intro has
      // played by the time the panel exists, so the next launch's first frame mounts it (skipintro.h).
      draw_group(cursor, "STARTUP");
      g_cfg.skipIntro = row_switch(cursor, "Skip intro", g_cfg.skipIntro, C_GREEN, true);
      row_hint("Takes effect the next time the game starts.");

      // Game settings before PANEL, AA's own order.
      draw_group(cursor, "DISPLAY");
      g_cfg.borderless = row_switch(cursor, "Borderless window", g_cfg.borderless, C_GREEN, true);
      row_hint("Fills the monitor with no frame; the game stays windowed.");
      if (const char* note = display_note()) draw_placeholder(cursor, note);
      g_cfg.winKeys = row_switch(cursor, "Windows key and hotkeys", g_cfg.winKeys, C_GREEN, true);
      row_hint("Lets the Windows key and global hotkeys (e.g. ShareX) work while the game has focus.");

      draw_group(cursor, "PANEL");
      // AA's row, 75-200% in steps of 5 on every screen; the readout is the settled scale,
      // which this screen's ceiling caps (ui_scale_ceiling). Drawing is not a write: only a drag stores anything.
      {
        char scaleText[16];
        sprintf_s(scaleText, "%d%%", settled);
        const int v = ui_scale_step(row_slider_at(cursor, "UI scale", settled, UI_SCALE_MIN, UI_SCALE_MAX,
                                                  scaleText, true, 0, C_BONE, EK_INT, UI_SCALE_DEFAULT));
        if (v != settled) g_cfg.uiScale = v;
        if (!g_ui.down && pct != settled) row_hint("This size could not be built; see nukepanel.log.");
      }
      g_cfg.animations = row_switch(cursor, "Panel animations", g_cfg.animations, C_GREEN, true);
      row_hint("Fades and slides in the panel");

      draw_group(cursor, "DIAGNOSTICS");
      // Still gates the instruments' jump and position lines (instruments.cpp), so renamed rather than removed;
      // the readout rows are gone, the 5 s log lines carry the same counters (panel clean-up 2026-10-02).
      g_cfg.timing = row_switch(cursor, "Debug log", g_cfg.timing, C_GREEN, true);
      row_hint("Jump and position timings in nukepanel.log.");
      break;
    }
    default:
      break;
  }

  canvas_pop_clip();
  g_ui.mx = bodyMx; g_ui.my = bodyMy;

  // The owning row did not draw this frame (tab change, dropped row): close.
  if (s_dd.open && !s_ddSeenThisFrame) { s_dd.open = 0; s_dd.rowKey = -1; }
  // Likewise the field, through its own commit path.
  if (s_entry.open && !s_entrySeenThisFrame) entry_clickaway_commit();

  // How tall this tab is, measured after the fact for the next frame's wheel.
  const int contentH = cursor.y - contentTop;
  s_contentH[s_tab] = contentH;
  s_scroll[s_tab] = menu_scroll_clamp(scroll, contentH, viewH);

  // The scroll cue, only with something to scroll; it reports and takes no click. At the offset the rows were
  // drawn at this frame.
  {
    const MenuScrollCue cue = menu_scroll_cue(viewH, scroll, contentH, viewH);
    if (cue.shown) {
      if (cue.fade > 0.0) {
        const int fadeY = bodyRect.y + bodyRect.h - MENU_SCROLL_FADE_H;
        canvas_gradient_rect(bodyRect.x, fadeY, bodyRect.w, MENU_SCROLL_FADE_H,
                             pres_content_colour(canvas_with_alpha(C_PANEL, 0)),
                             pres_content_colour(alpha_scale(C_PANEL, cue.fade)));
      }
      const int barX = p.x + p.w - 1 - 3 - MENU_SCROLL_CUE_W;
      canvas_rect(barX, bodyY + PAD + cue.thumbY, MENU_SCROLL_CUE_W, cue.thumbH, pres_content_colour(C_BONEFNT));
    }
  }

  if (g_cfg.timing && s_sliderInvRows > s_sliderLoggedRows[s_tab]) {
    s_sliderLoggedRows[s_tab] = s_sliderInvRows;
    np_log("menu: sliders tab %d: %s | row w=%d at %d%%", s_tab, s_sliderInv, cursor.w, pct);
  }

  if (s_rowsDropped && !s_rowsDroppedLogged) {
    s_rowsDroppedLogged = true;
    np_log("menu: tab %d exhausted the %d px layout budget (laid out %d px) at panel %dx%d -- rows not drawn",
           s_tab, MENU_CONTENT_BUDGET, contentH, box.w, box.h);
  }

  if (railTab >= 0) menu_set_tab(railTab);

  // --- The resize grip: three diagonals in the corner, lit while hovered ------
  {
    const UiRect g = panel_grip_rect(box);
    const unsigned gc = pres_colour(gripHit ? C_BONEDIM : C_BONEFNT);
    for (int i = 1; i <= 3; i++) {
      const int off = i * 4;
      canvas_line(g.x + off, g.y + g.h - 1, g.x + g.w - 1, g.y + off, gc);
    }
  }

  menu_dropdown_draw();

  // --- The tooltip, after everything else the panel draws --------------------
  s_tooltip = tooltip_step(s_tooltip, s_hoverRowKey, g_ui.pressed || g_ui.rpressed,
                           s_pres.lastMs, anim_style(), s_frameDt);
  const char* const hint = s_glyphHint ? s_glyphHint : s_hoverHint;
  s_tooltipText = nullptr;
  if (s_tooltip.shown && s_tooltip.alpha > 0.0 && hint && s_pres.openTarget) {
    int st[TIP_LINES_MAX], ln[TIP_LINES_MAX], widest = 0;
    const int lines = tip_wrap(hint, tip_max_w(p.w, logicalW), tip_measure, nullptr, st, ln, TIP_LINES_MAX, &widest);
    const int pitch = font_height[FONT_SMALL] + TIP_LINE_GAP;
    const UiRect b = tooltip_box_place(g_ui.mx, g_ui.my, widest,
                                       lines > 0 ? lines * pitch - TIP_LINE_GAP : 0, logicalW, logicalH);
    canvas_round_rect(b.x, b.y, b.w, b.h, TOOLTIP_R,
                      alpha_scale(C_WELL, s_tooltip.alpha));
    canvas_round_rect_outline(b.x, b.y, b.w, b.h, TOOLTIP_R, 1,
                              alpha_scale(C_RAISED, s_tooltip.alpha));
    for (int i = 0; i < lines; ++i) {
      char line[sizeof s_hintBuf];
      _snprintf_s(line, sizeof line, _TRUNCATE, "%.*s", ln[i], hint + st[i]);
      canvas_text(b.x + TOOLTIP_PAD, b.y + TOOLTIP_PAD + i * pitch, line,
                  alpha_scale(C_BONE, s_tooltip.alpha), FONT_SMALL);
    }
    s_tooltipText = hint;
  }

  // --- A press no control took moves the panel ------------------------------
  // Decided last: "no control took this press" is only known once all have looked.
  if (g_ui.pressed && g_ui.down && !s_pressClaimed && !s_dragLive &&
      s_pres.openTarget && ui_hit(p, g_ui.mx, g_ui.my)) {
    s_drag.phase = PDRAG_MOVE;
    s_drag.start = box;
    s_drag.grabDx = g_ui.mx - box.x;
    s_drag.grabDy = g_ui.my - box.y;
    s_dragBox = box;
    s_dragLive = true;
  }

  g_ui.mx = deviceMx;
  g_ui.my = deviceMy;
  canvas_set_scale(UI_SCALE_DEFAULT);
}
