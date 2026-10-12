#pragma once
// Pure zoom rules: a true optical magnification of the horizontal FOV (halving the tangent of the half-angle), not a
// division of the degree count.
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

// Effective FOV in degrees for a stock FOV at the given zoom level (a
// multiplier, clamped to >= 1). A stock outside (1, 179) degrees returns
// unchanged: atan(tan(x/2)) is unstable near the poles.
inline double zoom_fov(double stockDeg, double level) {
  if (level < 1.0) level = 1.0;
  if (stockDeg <= 1.0 || stockDeg >= 179.0) return stockDeg;
  const double halfRad = stockDeg * (3.14159265358979323846 / 180.0) * 0.5;
  const double zoomedHalfRad = atan(tan(halfRad) / level);
  return zoomedHalfRad * 2.0 * (180.0 / 3.14159265358979323846);
}

// Whether the engine's cvar string (its own v_atof reader, input-console.md
// section 1) needs writing to reach wantDeg: only when it differs by more
// than 0.001 degrees, so re-applying the same zoom every frame is a no-op.
inline int zoom_should_write_deg(double have, double wantDeg) {
  const double diff = have - wantDeg;
  return (diff > 0.001 || diff < -0.001) ? 1 : 0;
}
inline int zoom_should_write(const char* engineValue, double wantDeg) {
  if (!engineValue) return 1;
  return zoom_should_write_deg(atof(engineValue), wantDeg);
}

// Safe to recapture the stock value only when steady: not applying now or
// last frame, and nothing written this frame -- the release frame's cvar
// read is still the stale zoomed value (read before its own write-back).
inline int zoom_may_recapture(int applyingNow, int appliedLastFrame, int wroteThisFrame) {
  return (!applyingNow && !appliedLastFrame && !wroteThisFrame) ? 1 : 0;
}

// --- Custom field of view (quick-wins.md section 2) ---------------------------------------------
// The slider holds the engine's own horizontal degrees; 0 stored is the captured stock value (WideGen's ~79.755 here),
// which the reset arrow writes. 40..150 covers well below and above stock; past ~150 on 21:9 the edges stretch.
enum { FOV_MIN_DEG = 40, FOV_MAX_DEG = 150, FOV_STOCK = 0 };

inline double fov_want_deg(int cfgDeg, double stockDeg) {
  if (cfgDeg == FOV_STOCK) return stockDeg;
  return (double)(cfgDeg < FOV_MIN_DEG ? FOV_MIN_DEG : cfgDeg > FOV_MAX_DEG ? FOV_MAX_DEG : cfgDeg);
}
// Where the knob sits: the stored value, or the stock rounded to a whole degree.
inline int fov_slider_value(int cfgDeg, double stockDeg) {
  const int v = cfgDeg == FOV_STOCK ? (int)(stockDeg + 0.5) : cfgDeg;
  return v < FOV_MIN_DEG ? FOV_MIN_DEG : v > FOV_MAX_DEG ? FOV_MAX_DEG : v;
}

// The row's readout, never wider than FOV_READOUT_RESERVE, which fits the panel's readout column ("150.0 (stock)"
// had squeezed the track to TRACK_MIN_W). The stock shows to a tenth below 100; the
// hint says the reset is the stock.
const char* const FOV_READOUT_RESERVE = "99.9";
inline void fov_value_text(int cfgDeg, double stockDeg, int haveStock, char* out, int cap) {
  if (!out || cap <= 0) return;
  if (cfgDeg != FOV_STOCK) _snprintf_s(out, (size_t)cap, _TRUNCATE, "%d", fov_slider_value(cfgDeg, stockDeg));
  else if (!haveStock) _snprintf_s(out, (size_t)cap, _TRUNCATE, "--");
  else if (stockDeg < 99.95) _snprintf_s(out, (size_t)cap, _TRUNCATE, "%.1f", stockDeg);
  else _snprintf_s(out, (size_t)cap, _TRUNCATE, "%.0f", stockDeg);
}

// One write per change, never per frame. APPLY captures the stock string before its first write,
// RESTORE writes that exact string back.
enum { FOV_NONE = 0, FOV_APPLY = 1, FOV_REWRITE = 2, FOV_RESTORE = 3 };
struct FovState { int applied; double wrote; };
inline int fov_step(const FovState& s, int on, double want) {
  if (on && !s.applied) return FOV_APPLY;
  if (on && zoom_should_write_deg(s.wrote, want)) return FOV_REWRITE;
  if (!on && s.applied) return FOV_RESTORE;
  return FOV_NONE;
}
// Something else wrote g_cam_fov while ours is applied (a console uset): write ours over it again. The
// glue reads the cvar for this once a second, not every frame (a cvar read is guarded reads).
inline bool fov_needs_reassert(const FovState& s, int on, int haveCur, double curDeg) {
  return on && s.applied && haveCur && zoom_should_write_deg(curDeg, s.wrote);
}
// What the row saves: the reset's 0 as it is, anything else inside the slider's range (the entry field
// already clamps; this also mends a hand-edited 1..39).
inline int fov_cfg_from_slider(int v) {
  if (v == FOV_STOCK) return FOV_STOCK;
  return v < FOV_MIN_DEG ? FOV_MIN_DEG : v > FOV_MAX_DEG ? FOV_MAX_DEG : v;
}
inline void fov_done(FovState* s, int act, double want) {
  if (act == FOV_APPLY || act == FOV_REWRITE) { s->applied = 1; s->wrote = want; }
  else if (act == FOV_RESTORE) { s->applied = 0; s->wrote = 0.0; }
}

// The engine glue (zoom.cpp), main thread only. inLevel: a player entity this frame, for the once-per-level FOV
// log line (the per-map +0xf4 override).
void fov_frame(int inLevel);
// The quit path, before config.cfg is written: the stock string back if we hold a custom value.
void fov_restore_for_quit(void);
// The stock value for the menu's readout and reset; false until g_cam_fov has been read once.
bool fov_stock(double* deg);
// g_cam_fov resolved: the rows are live.
bool fov_available(void);
