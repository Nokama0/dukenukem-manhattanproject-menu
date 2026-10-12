#pragma once
// Mouse aim: the pure rules. mouseaim.cpp reads the
// engine's cursor and Duke, aim.cpp's fire-dir detour takes the point. Evidence: docs/re/mouse-aim.md.
#include <math.h>
#include "w2s.h"     // Vec3, Mat4
#include "track.h"   // TrkPt
#include "canvas.h"  // the palette

// view's 3x3 part (row vectors: p_view = p_world * A + T) inverted; 0 when singular.
inline int ma_inv3(const Mat4& v, float out[9]) {
  const float a = v.m[0], b = v.m[1], c = v.m[2], d = v.m[4], e = v.m[5], f = v.m[6], g = v.m[8], h = v.m[9],
              i = v.m[10];
  const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (!(fabsf(det) > 1e-12f)) return 0;
  const float k = 1.f / det;
  out[0] = (e * i - f * h) * k; out[1] = (c * h - b * i) * k; out[2] = (b * f - c * e) * k;
  out[3] = (f * g - d * i) * k; out[4] = (a * i - c * g) * k; out[5] = (c * d - a * f) * k;
  out[6] = (d * h - e * g) * k; out[7] = (b * g - a * h) * k; out[8] = (a * e - b * d) * k;
  return 1;
}
inline Vec3 ma_row3(float x, float y, float z, const float m[9]) {
  const Vec3 r = { x * m[0] + y * m[3] + z * m[6], x * m[1] + y * m[4] + z * m[7], x * m[2] + y * m[5] + z * m[8] };
  return r;
}
inline int ma_unit(Vec3* v) {
  const float l = sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
  if (!(l > 1e-6f)) return 0;
  v->x /= l; v->y /= l; v->z /= l;
  return 1;
}

// The world ray under a cursor at NDC (nx, ny): x right, y up, -1..1, as inp_cursor_info returns it
// (mouse-aim.md section 4). The view-space point at z = 1 that projects there (the camera looks down +z,
// render.md section 3), then back through the view. o = the eye, d = unit, away from the camera.
inline int ma_cursor_ray(float nx, float ny, const Mat4& view, const Mat4& proj, Vec3* o, Vec3* d) {
  const float* P = proj.m;
  const float a = P[0] - nx * P[3], b = P[4] - nx * P[7], e = nx * (P[11] + P[15]) - (P[8] + P[12]);
  const float c = P[1] - ny * P[3], dd = P[5] - ny * P[7], f = ny * (P[11] + P[15]) - (P[9] + P[13]);
  const float det = a * dd - b * c;
  if (!(fabsf(det) > 1e-12f)) return 0;
  const float x = (e * dd - b * f) / det, y = (a * f - e * c) / det;
  float inv[9];
  if (!ma_inv3(view, inv)) return 0;
  *o = ma_row3(-view.m[12], -view.m[13], -view.m[14], inv);
  *d = ma_row3(x, y, 1.f, inv);
  return ma_unit(d);
}
// The camera's forward (+z in view space) in the world.
inline int ma_view_forward(const Mat4& view, Vec3* f) {
  float inv[9];
  if (!ma_inv3(view, inv)) return 0;
  *f = ma_row3(0.f, 0.f, 1.f, inv);
  return ma_unit(f);
}

// The track's direction at `at`: the polyline span nearest in XZ (the track has no height), unit, y = 0.
// Its sign follows the polyline's order. 0 with fewer than two points or only zero-length spans.
inline int ma_track_tangent(const TrkPt* p, int n, Vec3 at, Vec3* t) {
  int best = -1;
  float bestD = 0.f;
  for (int i = 0; i + 1 < n; ++i) {
    const float ex = p[i + 1].x - p[i].x, ez = p[i + 1].z - p[i].z, ee = ex * ex + ez * ez;
    if (!(ee > 1e-8f)) continue;   // span joints repeat a point
    float s = ((at.x - p[i].x) * ex + (at.z - p[i].z) * ez) / ee;
    s = s < 0.f ? 0.f : (s > 1.f ? 1.f : s);
    const float dx = p[i].x + s * ex - at.x, dz = p[i].z + s * ez - at.z, dist = dx * dx + dz * dz;
    if (best >= 0 && dist >= bestD) continue;
    best = i; bestD = dist;
  }
  if (best < 0) return 0;
  t->x = p[best + 1].x - p[best].x; t->y = 0.f; t->z = p[best + 1].z - p[best].z;
  return ma_unit(t);
}
// No track: the plane faces the camera, so its tangent is the horizontal right of the forward.
inline int ma_tangent_from_forward(Vec3 fwd, Vec3* t) {
  t->x = fwd.z; t->y = 0.f; t->z = -fwd.x;
  return ma_unit(t);
}
// The ray's crossing with the vertical plane through `origin` that holds `tangent` (mouse-aim.md section 2).
// 0 when the ray runs along the plane or points away from it.
inline int ma_plane_point(Vec3 ro, Vec3 rd, Vec3 origin, Vec3 tangent, Vec3* out) {
  const float nx = -tangent.z, nz = tangent.x;   // horizontal normal
  const float den = rd.x * nx + rd.z * nz;
  if (!(fabsf(den) > 1e-6f)) return 0;
  const float s = ((origin.x - ro.x) * nx + (origin.z - ro.z) * nz) / den;
  if (!(s > 0.f)) return 0;
  out->x = ro.x + s * rd.x; out->y = ro.y + s * rd.y; out->z = ro.z + s * rd.z;
  return 1;
}

// Who sets the shot's direction: silent aim while it holds a lock, else the
// cursor's point while mouse aim is on and has one this frame, else the game.
enum { MA_SRC_STOCK = 0, MA_SRC_SILENT, MA_SRC_MOUSE };
inline int ma_fire_source(int silentLocked, int mouseOn, int havePoint) {
  if (silentLocked) return MA_SRC_SILENT;
  return mouseOn && havePoint ? MA_SRC_MOUSE : MA_SRC_STOCK;
}

// --- Facing (experimental, mouse-aim.md sections 7 and 9). The lever is Duke's yaw +0x1AC: a one-frame left/right
// press does not turn him in place (measured), and turned and walked him once. Pixels either side of Duke where the
// side holds, so the facing never flickers under the cursor.
const float MA_FACE_DEAD_PX = 24.f;
// -1 left, +1 right of Duke on screen; inside the dead zone the last side (0 before any).
inline int ma_face_side(float cursorPx, float dukePx, float deadPx, int prev) {
  if (cursorPx > dukePx + deadPx) return 1;
  if (cursorPx < dukePx - deadPx) return -1;
  return prev;
}
// facingDx: screen x of Duke's forward (48 units ahead) minus his own. Under MA_FACE_MIN_DX pixels the track
// runs nearly along the view axis and the side is unknown: no write, and no verdict from that frame.
const float MA_FACE_MIN_DX = 4.f;
inline int ma_face_axis_ok(float facingDx) { return fabsf(facingDx) >= MA_FACE_MIN_DX; }
inline int ma_face_needs_turn(int want, float facingDx) {
  if (!want || !ma_face_axis_ok(facingDx)) return 0;
  return (facingDx > 0.f ? 1 : -1) != want;
}
// The reversed facing: yaw + 180, in [0, 360). Only the sign along the track survives the track snap
// (FUN_10096410 projects the forward onto the track plane), and +180 reverses the forward exactly.
inline float ma_face_flip_yaw(float yaw) {
  float y = fmodf(yaw + 180.f, 360.f);
  if (y < 0.f) y += 360.f;
  return y >= 360.f ? 0.f : y;
}
// The game takes input: player_read_input_mask returns early unless world+0xC8 has bit 8 and world+0x14 is 0
// (duke!0x1007d27a-0x1007d28a; paused or in a menu, aim.md section 5), and no cutscene letterbox is shown (the
// "system_03"/"letterbox" window, shot.h). Facing never writes otherwise.
inline int ma_face_world_live(unsigned worldC8, int world14, int letterbox) {
  return (worldC8 & 8u) && world14 == 0 && !letterbox;
}
// Only in PS_idle (1) with no move key in +0xE00: idle writes no yaw without input (its movement vector is
// zero, duke!0x10080130 -> 0x10087fd0), while walk and run take the facing from the keys every tick.
inline int ma_face_may_write(int state, unsigned mask) { return state == 1 && !(mask & 0x1Fu); }
// A write is watched this many frames; Duke moving farther than this (world units, 52 a metre) means it walked
// him. Two clean writes in a row that do not stick refuse the row until the next level.
enum { MA_FACE_WATCH_FRAMES = 30, MA_FACE_NOTURN_MAX = 2 };
const float MA_FACE_MOVE_EPS = 2.f;
// PS_* (duke!0x101642c8): 1 idle, 2 step, 3 running are grounded; 4 crouching, 5-8 jumps and the rest
// (ropes, ladders, slides) are not.
inline int ma_face_grounded(int state) { return state >= 1 && state <= 3; }
// A trial is evidence only when Duke stood idle and still at the write, no move key (mask 0x1F) was down on any
// watched frame, and he stayed grounded throughout; anything else is tainted and never refuses the row.
inline int ma_face_trial_tainted(int state0, int stillAtPress, unsigned maskOr, int groundedAll) {
  return !(state0 == 1 && stillAtPress && !(maskOr & 0x1Fu) && groundedAll);
}
// INPLACE: the facing held the wanted side every watched frame; NOTURN: it did not (reverted or never turned).
enum { MA_FACE_INPLACE = 0, MA_FACE_MOVED, MA_FACE_NOTURN, MA_FACE_UNCLEAN, MA_FACE_VERDICTS };
inline int ma_face_verdict(int tainted, float moved, int sideNow, int want) {
  if (tainted) return MA_FACE_UNCLEAN;
  if (moved > MA_FACE_MOVE_EPS) return MA_FACE_MOVED;
  return sideNow == want ? MA_FACE_INPLACE : MA_FACE_NOTURN;
}
// The row is refused when a clean write moved Duke, or after MA_FACE_NOTURN_MAX clean writes in a row that did
// not stick. A tainted trial leaves the run alone.
inline int ma_face_refuse(int verdict, int* noTurnRun) {
  if (verdict == MA_FACE_MOVED) return 1;
  if (verdict == MA_FACE_NOTURN) return ++*noTurnRun >= MA_FACE_NOTURN_MAX;
  if (verdict == MA_FACE_INPLACE) *noTurnRun = 0;
  return 0;
}

// --- Our cursor (a Sensitivity row) ----------------------------------
// Percent of one pixel a mickey: 100 moves as the engine's own cursor does (mouse-aim.md section 4).
enum { MA_SENS_MIN = 10, MA_SENS_MAX = 400, MA_SENS_DEF = 100 };
// One axis: the device's mickeys scaled by the sensitivity, clamped to [0, extent] (the render size).
inline float ma_cursor_step(float px, int mickeys, int sensPct, int extent) {
  if (sensPct < MA_SENS_MIN) sensPct = MA_SENS_MIN;
  if (sensPct > MA_SENS_MAX) sensPct = MA_SENS_MAX;
  const float v = px + (float)mickeys * (float)sensPct / 100.f;
  return v < 0.f ? 0.f : (v > (float)extent ? (float)extent : v);
}

// --- The reticle's style (colour, size, thickness, dot, outline) ------
// Cyan with a black outline by default: it reads on Duke's brown and orange levels and on the sky (violet
// was hard to see).
enum { MA_RET_COLOURS = 8, MA_RET_COLOUR_DEF = 1 };
enum { MA_RET_SIZE_MIN = 3, MA_RET_SIZE_MAX = 20, MA_RET_SIZE_DEF = 8 };
enum { MA_RET_THICK_MIN = 1, MA_RET_THICK_MAX = 4, MA_RET_THICK_DEF = 2 };
inline unsigned ma_reticle_colour(int i) {
  static const unsigned k[MA_RET_COLOURS] = { 0xFFFFFFFFu, C_PLAYER, C_GREEN, C_HAZARD, C_RED, C_BLUE, C_AIM,
                                              C_FRIENDLY };
  return i >= 0 && i < MA_RET_COLOURS ? k[i] : k[MA_RET_COLOUR_DEF];
}
inline const char* ma_reticle_colour_label(int i) {
  static const char* const k[MA_RET_COLOURS] = { "White", "Cyan", "Green", "Yellow", "Red", "Blue", "Violet",
                                                 "Magenta" };
  return i >= 0 && i < MA_RET_COLOURS ? k[i] : k[MA_RET_COLOUR_DEF];
}
struct MaRect { int x, y, w, h; };
// Left, right, up, down arms of `size` and the dot, all `thick` wide and centred on the same lines; the gap
// between an arm and the dot is thick + 1. Returns 5 with the dot, else 4.
inline int ma_reticle_rects(int cx, int cy, int size, int thick, int dot, MaRect out[5]) {
  const int t = thick, g = thick + 1, lo = t / 2;
  out[0] = MaRect{ cx - lo - g - size, cy - lo, size, t };
  out[1] = MaRect{ cx - lo + t + g, cy - lo, size, t };
  out[2] = MaRect{ cx - lo, cy - lo - g - size, t, size };
  out[3] = MaRect{ cx - lo, cy - lo + t + g, t, size };
  out[4] = MaRect{ cx - lo, cy - lo, t, t };
  return dot ? 5 : 4;
}

// --- The reticle preview strip -----------------------------------
// The strip's inner area split into three swatches with 1 px separators between; the last swatch takes the
// remainder, so the three and the two lines cover the width exactly.
inline void ma_swatch_layout(int x, int y, int w, int h, MaRect sw[3], MaRect sep[2]) {
  const int each = (w - 2) / 3;
  for (int i = 0; i < 3; ++i) {
    const int sx = x + i * (each + 1);
    sw[i] = MaRect{ sx, y, i == 2 ? x + w - sx : each, h };
  }
  for (int i = 0; i < 2; ++i) sep[i] = MaRect{ sw[i].x + sw[i].w, y, 1, h };
}
// The swatches' own colours: dark concrete, ochre brick, bright haze (opaque).
inline unsigned ma_swatch_colour(int i) {
  static const unsigned k[3] = { 0xFF3B3F42u, 0xFF7A5A2Au, 0xFFCFC8B6u };
  return k[i < 0 ? 0 : (i > 2 ? 2 : i)];
}

// --- mouseaim.cpp (DLL only; read, not run, by the suite) ------------------------------------
// Top of every frame-path call, with aim_lock_clear: the detour never aims at a stale point.
void mouseaim_clear(void);
// Frame path after aim_frame: the cursor, this frame's point on Duke's plane and the facing write.
void mouseaim_frame(const unsigned char* player, int W, int H);
// After aim_draw, under the panel: the reticle.
void mouseaim_draw(int W, int H);
// The Mouse aim switch, and the fire-dir detour not refused: off, nothing of mouse aim runs.
int mouseaim_on(void);
// This frame's aim point; 0 when there is none.
int mouseaim_point(Vec3* p);
int mouseaim_face_refused(void);
// The reticle at (cx, cy) in the canvas's current coordinates, no begin/end: the game view and the preview
// strip both draw through it, so the strip shows what the game shows. `tint` (may be null) maps each colour,
// the panel's open-alpha for the strip.
void mouseaim_draw_reticle(int cx, int cy, unsigned (*tint)(unsigned));
