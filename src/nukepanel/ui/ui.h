// Verbatim from Megaton's src/duke3dmod/ui.h; kept identical on purpose -- do not edit here.
#pragma once
// For clamp_int and clamp_ll_to_int, which the UI SCALE block at the foot of this header is written in terms of.
// canvas.cpp includes ui.h directly, so the clamps have to arrive WITH this header, not whatever the including file
// pulled in first.
#include "common.h"

struct UiRect { int x, y, w, h; };

struct UiInput {
  int  mx, my;      // cursor position in overlay pixels
  bool down;        // left button currently held
  bool pressed;     // left button went down this frame (edge)
  // Right button, carried by the same rules as the left: input.cpp's poll hook sets rdown, input_begin_frame derives
  // rpressed via ui_press_edge. A right-click on a slider row opens the numeric entry field (entry.h) instead of
  // dragging the knob, a once-per-click action reading the edge.
  bool rdown;       // right button currently held
  bool rpressed;    // right button went down this frame (edge)
};

bool   ui_hit(const UiRect& r, int x, int y);
// Carves a row of `height` off the top of `cursor`, then advances it past height+gap.
UiRect ui_row(UiRect& cursor, int height, int gap);
int    ui_slider_value(const UiRect& track, int mx, int lo, int hi);
int    ui_slider_x(const UiRect& track, int value, int lo, int hi);

// True exactly on the frame `down` is true and `wasDown` (its value on the previous frame) is false: a press edge. Held
// and released read false, so a control triggers once per click rather than every frame the button stays down.
bool   ui_press_edge(bool down, bool wasDown);

// True for the initial SDL_KEYDOWN of a physical key press, false for an auto-repeat of the held key (repeat != 0).
// One-shot actions bound to a key (the panel toggle, Escape) gate on this so holding the key does not re-trigger them.
bool   ui_key_is_initial_press(bool repeat);

// Decides the value for a "save the real state, force it while the panel owns input, restore it on close" toggle (SDL
// relative-mouse mode, cursor visibility): `openValue` while the panel owns input, otherwise `savedValue`. Never
// invents a default; the caller supplies a value it observed via a query call.
int    ui_saved_toggle_value(bool panelOwnsInput, int openValue, int savedValue);

// True if any of the first `count` bytes is nonzero. Used by the engine input-clear instrumentation to log whether the
// keystatus bytes it just zeroed are still zero right after: if not, something writes them back the same frame.
bool   ui_bytes_any_nonzero(const unsigned char* bytes, int count);

// Whether a row control acts on this frame's mouse state. A disabled row is inert, not merely dimmed: a click on a
// greyed sub-row would silently change a setting the player was told was unavailable. `buttonState` is the press edge
// for a switch (once per click) or the held state for a slider (track the drag).
bool   ui_row_accepts_click(bool enabled, bool buttonState, bool hit);

// UI scale. The panel is laid out once in logical pixels and the canvas multiplies every coordinate on the way to the
// screen, so the factor is spent in one place. Floor is 75: at 50 a 30px row is 15px tall and its switch 10px, a
// control nobody can hit. The bound lives here so the loader and drawer share it.
const int UI_SCALE_MIN     = 75;
const int UI_SCALE_MAX     = 200;
const int UI_SCALE_DEFAULT = 100;

inline int ui_scale_clamped(int pct) { return clamp_int(pct, UI_SCALE_MIN, UI_SCALE_MAX); }

// Whether a scale needs its own rasterised atlas. 100 does not: it IS the
// baked one. Its own function so canvas.cpp and the suite cannot disagree
// about which scales rebuild. (2026-09-18)
inline int ekm_scale_needs_atlas(int pct) {
  return ui_scale_clamped(pct) != UI_SCALE_DEFAULT ? 1 : 0;
}

// Logical -> device. Half away from zero: the panel's bevels and sub-row rules are one pixel wide and truncation would
// erase them at 75%. Widened before the multiply since project_world saturates its coordinates and a saturated one
// times two wraps.
inline int ui_scale_px(int px, int pct) {
  const long long p = (long long)ui_scale_clamped(pct);
  const long long v = (long long)px * p;
  return clamp_ll_to_int(v >= 0 ? (v + 50) / 100 : (v - 50) / 100);
}

// Device -> logical, for the cursor and the screen size the panel lays
// itself out against. Not an exact inverse of the above (both round); only
// needs to land within a pixel, and a row is thirty.
inline int ui_unscale_px(int px, int pct) {
  const long long p = (long long)ui_scale_clamped(pct);
  const long long v = (long long)px * 100;
  return clamp_ll_to_int(v >= 0 ? (v + p / 2) / p : (v - p / 2) / p);
}

// Largest legal scale at which the panel's whole logical height fits a screen this tall (a higher one clips rows, even
// the row that sets it): the largest pct with (total * pct + 50) / 100 <= screenH, i.e. (100*screenH + 49) / total,
// snapped down to a multiple of five before the clamp. A non-positive total answers UI_SCALE_MAX.
inline int ui_scale_max_for(int screenH, int panelLogicalH, int insetLogical) {
  const long long total = (long long)panelLogicalH + 2LL * (long long)insetLogical;
  if (total <= 0) return UI_SCALE_MAX;
  long long pct = ((long long)screenH * 100 + 49) / total;
  pct -= pct % 5;
  return clamp_int(clamp_ll_to_int(pct), UI_SCALE_MIN, UI_SCALE_MAX);
}

// Which scale the panel draws at this frame: the settled one, except while the left button is held, when it keeps the
// applied one. row_slider_at has no drag capture, so if the layout moved with the value the row would slide out from
// under a still cursor and the feedback loop would slam the value between the ends; the panel resizes once, on release.
inline int ui_scale_effective(int stored, int applied, bool dragging) {
  return dragging ? applied : stored;
}

// Inverse of ui_scale_max_for: the largest logical panel height whose ceiling stays at or above `pct`. A resize drag
// let the panel grow past it, so the ceiling dropped under the stored scale and the scale silently fell; menu.cpp
// clamps the drag to this. Bisection (the ceiling never rises with height); pct <= UI_SCALE_MIN answers INT_MAX.
inline int ui_panel_max_h_for(int screenH, int pct, int insetLogical) {
  const int pctC = ui_scale_clamped(pct);
  if (pctC <= UI_SCALE_MIN) return 0x7fffffff;
  long long lo = 0;
  long long hi = (long long)screenH * 100 + 100000;
  if (ui_scale_max_for(screenH, clamp_ll_to_int(lo), insetLogical) < pctC) return 0;
  while (lo < hi) {
    const long long mid = lo + (hi - lo + 1) / 2;
    if (ui_scale_max_for(screenH, clamp_ll_to_int(mid), insetLogical) >= pctC) lo = mid;
    else hi = mid - 1;
  }
  return clamp_ll_to_int(lo);
}
