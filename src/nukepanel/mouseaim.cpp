// Mouse aim: the reticle at our cursor (the engine mouse's deltas times Sensitivity), the cursor's point on Duke's
// plane for aim.cpp's fire-dir detour, and the experimental facing write. mouseaim.h owns the rules. Main thread only:
// the frame path runs in the same cooperative loop as the tick that reads the point (render.md section 1).
#include "mouseaim.h"
#include "aim.h"        // aim_fire_origin, aim_refused
#include "firstperson.h"  // firstperson_active: mouse aim stands down in first person
#include "engine.h"
#include "entities.h"
#include "teleport.h"   // teleport_track_polyline
#include "hotkeys.h"    // hotkeys_mouse_xy_take: the engine mouse's own deltas
#include "input.h"
#include "view.h"
#include "ndc.h"
#include "canvas.h"
#include "config.h"
#include "log.h"
#include "ui/common.h"  // clamp_double_to_int
#include <windows.h>
#include <math.h>
#include <string.h>

// Player fields (entities.md, aim.md section 2): +0x18C position, +0x1AC yaw (ypr_2_cartes: forward =
// (-sin yaw, 0, cos yaw)), +0xC10 state (PS_*), +0xE00 the input mask (1 left, 2 right, 4 jump, 8 up, 0x10 down).
static const unsigned kOfsYaw = 0x1AC, kOfsState = 0xC10, kOfsMask = 0xE00;
static const float kFacingProbe = 48.f;       // world units along the forward for its screen direction
static const float kStillEps = 0.5f;          // world units a frame: Duke stands still at the write

static int s_have;            // this frame's point, read by the next ticks' shots
static Vec3 s_point;
static int s_cursorOk;
static float s_nx, s_ny;
static unsigned s_frames, s_points, s_noCursor, s_freecam, s_noView, s_noRay, s_noEye, s_noPlane;
static unsigned s_planeTrack, s_planeCamera, s_trackUnread;
static float s_reprojErr;     // the last point's reprojection against the reticle, pixels
// Our cursor in render pixels: seeded from the engine's, then moved by the device's mickeys times Sensitivity.
static float s_cx, s_cy, s_engPx, s_engPy;
static int s_cW, s_cH, s_seeded;
static unsigned s_ownMoves, s_engFollows;
static double s_mickeys, s_engMoved;   // cumulative: our device deltas and the engine cursor's own movement
static int s_u2Logged;
static ULONGLONG s_nextLog;
static unsigned s_loggedSum = 0xFFFFFFFFu;

// Facing: one yaw write at a time, watched for MA_FACE_WATCH_FRAMES frames (mouse-aim.md section 9).
struct Probe {
  int active, frames, want, state0, still, grounded, held;   // held: the wanted side on every watched frame
  int axisLost;             // a watched frame saw the track end-on: the side was unknown, the trial is tainted
  unsigned mask0, maskOr;   // the mask at the write, and every watched frame's ORed in
  Vec3 pos0;
  float yaw0, yawSet, maxMoved;
};
static Probe s_probe;
static int s_side, s_faceRefused, s_noTurnRun;
static Vec3 s_lastPos;
static int s_haveLast;
static const void* s_world;   // player +0xE0: a new one is a new level, where a refused row is tested again
static unsigned s_faceWrites, s_faceWaits, s_faceAxis, s_faceNotLive, s_faceVerdict[MA_FACE_VERDICTS];

// In first person the view centre aims (firstperson.h): the cursor, the facing write and the torso bend stand down
// (free-move.md section 3).
int mouseaim_on(void) {
  return g_cfg.mouseAim != 0 && !aim_refused() && !firstperson_active() && !firstperson_view_held();
}
int mouseaim_face_refused(void) { return s_faceRefused; }
void mouseaim_clear(void) { s_have = 0; }
int mouseaim_point(Vec3* p) {
  if (!s_have) return 0;
  *p = s_point;
  return 1;
}

static Vec3 rdv(const unsigned char* p) { Vec3 v; memcpy(&v, p, sizeof v); return v; }
static float rdf(const unsigned char* p) { float f; memcpy(&f, p, sizeof f); return f; }
static unsigned rdu(const unsigned char* p) { unsigned u; memcpy(&u, p, sizeof u); return u; }

// Screen x of Duke's forward minus his own: the side he faces on screen. 0 when either end does not project.
static float facing_dx(Vec3 pos, float yawDeg, const Mat4& view, const Mat4& proj, float nearZ, int W, int H) {
  const float r = yawDeg * 0.017453292f;
  const Vec3 ahead = { pos.x - sinf(r) * kFacingProbe, pos.y, pos.z + cosf(r) * kFacingProbe };
  const ScreenPt a = w2s_project(pos, view, proj, nearZ, W, H);
  const ScreenPt b = w2s_project(ahead, view, proj, nearZ, W, H);
  return a.ok && b.ok ? b.x - a.x : 0.f;
}
static int side_of(float dx) { return dx > 0.f ? 1 : (dx < 0.f ? -1 : 0); }

static const char* side_name(int s) { return s > 0 ? "right" : (s < 0 ? "left" : "none"); }
static const char* kVerdictName[MA_FACE_VERDICTS] = { "stuck", "MOVED", "did not stick", "tainted (not evidence)" };

static void face_refuse(const char* why, float moved) {
  s_faceRefused = 1;
  s_probe.active = 0;
  np_log("mouseaim: Face the cursor REFUSED until the next level: %s (moved %.1f units)", why, moved);
}

// One watched frame of a write: keys, state, position and the side faced; at the end the verdict.
static void face_settle(const unsigned char* player, const Mat4& view, const Mat4& proj, float nearZ, int W, int H) {
  const Vec3 pos = rdv(player + 0x18C);
  const float dx = pos.x - s_probe.pos0.x, dz = pos.z - s_probe.pos0.z, moved = sqrtf(dx * dx + dz * dz);
  if (moved > s_probe.maxMoved) s_probe.maxMoved = moved;
  s_probe.maskOr |= rdu(player + kOfsMask);
  if (!ma_face_grounded((int)rdu(player + kOfsState))) s_probe.grounded = 0;
  const float yaw = rdf(player + kOfsYaw);
  const float fdx = facing_dx(pos, yaw, view, proj, nearZ, W, H);
  const int sideNow = side_of(fdx);
  if (!ma_face_axis_ok(fdx)) s_probe.axisLost = 1;
  else if (sideNow != s_probe.want) s_probe.held = 0;
  if (++s_probe.frames < MA_FACE_WATCH_FRAMES) return;
  s_probe.active = 0;
  const int tainted = ma_face_trial_tainted(s_probe.state0, s_probe.still, s_probe.maskOr, s_probe.grounded) ||
                      s_probe.axisLost;
  const int v = ma_face_verdict(tainted, s_probe.maxMoved, s_probe.held ? s_probe.want : -s_probe.want, s_probe.want);
  s_faceVerdict[v]++;
  if (s_faceWrites <= 32 || v == MA_FACE_MOVED || v == MA_FACE_NOTURN)
    np_log("mouseaim: face #%u keys 0x%X (watch 0x%X) still=%d grounded=%d: yaw %.1f -> wrote %.1f -> %.1f after %d "
           "frames, state %d -> %d, moved %.1f, faces %s (held=%d axisLost=%d), wanted %s: %s", s_faceWrites,
           s_probe.mask0, s_probe.maskOr, s_probe.still, s_probe.grounded, s_probe.yaw0, s_probe.yawSet, yaw,
           MA_FACE_WATCH_FRAMES, s_probe.state0, (int)rdu(player + kOfsState), s_probe.maxMoved, side_name(sideNow),
           s_probe.held, s_probe.axisLost,
           side_name(s_probe.want), kVerdictName[v]);
  if (ma_face_refuse(v, &s_noTurnRun))
    face_refuse(v == MA_FACE_MOVED ? "a clean yaw write moved Duke" : "two clean yaw writes in a row did not stick",
                s_probe.maxMoved);
}

static void face_frame(const unsigned char* player, const Mat4& view, const Mat4& proj, float nearZ, int W, int H) {
  const void* world = (const void*)(size_t)rdu(player + 0xE0);
  if (world != s_world) {
    if (s_world && s_faceRefused) np_log("mouseaim: new level; Face the cursor is tested again");
    s_world = world;
    s_faceRefused = 0;
    s_noTurnRun = 0;
    s_probe.active = 0;
    s_haveLast = 0;
  }
  if (s_faceRefused || input_panel_open() || input_console_open()) {
    s_probe.active = 0;
    s_haveLast = 0;
    return;
  }
  // Paused, in a menu or in a cutscene: no write, and a watched trial ends unjudged.
  const unsigned char* w = (const unsigned char*)world;
  unsigned char c8 = 0;
  int w14 = 1;
  if (w && guarded_readable(w + 0x14, 0xC8 + 1 - 0x14)) { c8 = w[0xC8]; memcpy(&w14, w + 0x14, sizeof w14); }
  if (!ma_face_world_live(c8, w14, input_ui_window("system_03", "letterbox") != 0)) {
    s_faceNotLive++;
    s_probe.active = 0;
    s_haveLast = 0;
    return;
  }
  const Vec3 pos = rdv(player + 0x18C);
  const float sx = pos.x - s_lastPos.x, sz = pos.z - s_lastPos.z;
  const int still = s_haveLast && sx * sx + sz * sz <= kStillEps * kStillEps;
  s_lastPos = pos;
  s_haveLast = 1;
  if (s_probe.active) { face_settle(player, view, proj, nearZ, W, H); return; }
  const ScreenPt d = w2s_project(pos, view, proj, nearZ, W, H);
  if (!d.ok) return;
  s_side = ma_face_side(ndc_to_px_x(s_nx, W), d.x, MA_FACE_DEAD_PX, s_side);
  const float yaw = rdf(player + kOfsYaw);
  const float fdx = facing_dx(pos, yaw, view, proj, nearZ, W, H);
  if (!ma_face_axis_ok(fdx)) { s_faceAxis++; return; }   // the track end-on: no trial
  if (!ma_face_needs_turn(s_side, fdx)) return;
  const int state = (int)rdu(player + kOfsState);
  const unsigned mask = rdu(player + kOfsMask);
  // Idle, standing still, no move key: the one place the engine leaves the yaw alone (mouseaim.h).
  if (!ma_face_may_write(state, mask) || !still) { s_faceWaits++; return; }
  unsigned char* const yawAt = const_cast<unsigned char*>(player) + kOfsYaw;
  if (!guarded_writable(yawAt, sizeof(float))) { face_refuse("player +0x1AC is not writable", 0.f); return; }
  const float set = ma_face_flip_yaw(yaw);
  memcpy(yawAt, &set, sizeof set);   // a field the engine writes itself (FUN_10096410, the state code)
  s_probe.active = 1; s_probe.frames = 0; s_probe.want = s_side; s_probe.held = 1; s_probe.axisLost = 0;
  s_probe.pos0 = pos; s_probe.yaw0 = yaw; s_probe.yawSet = set; s_probe.maxMoved = 0.f;
  s_probe.state0 = state;
  s_probe.mask0 = mask;
  s_probe.maskOr = mask;
  s_probe.still = still;
  s_probe.grounded = ma_face_grounded(state);
  s_faceWrites++;
}

// The plane's tangent: Duke's track at Duke, or the camera's horizontal right with no track.
static int plane_tangent(const unsigned char* player, const Mat4& view, Vec3* t) {
  const TrkPt* pts = 0;
  const int n = teleport_track_polyline(player, &pts);
  if (n >= 2 && ma_track_tangent(pts, n, rdv(player + 0x18C), t)) { s_planeTrack++; return 1; }
  if (n < 0) s_trackUnread++;
  Vec3 f;
  if (!ma_view_forward(view, &f) || !ma_tangent_from_forward(f, t)) return 0;
  s_planeCamera++;
  return 1;
}

// ma_log fields. own: frames our cursor took the device's deltas; follows: frames it followed the engine's instead.
// engineMoved keeps rising while we read the device: the engine's buffered events still arrive (hotkeys.h). frames ==
// points + nocursor + freecam + noview + noray + noeye + noplane; points == track + camera planes.
static void ma_log(int W, int H) {
  const ULONGLONG tick = GetTickCount64();
  if (tick < s_nextLog) return;
  s_nextLog = tick + 5000;
  const unsigned sum = s_frames + s_faceWrites + s_faceWaits;
  if (sum == s_loggedSum) return;
  s_loggedSum = sum;
  // Verdicts sum to the settled writes (one may be under watch); only stuck, moved and noStick are evidence (a tainted
  // trial never refuses the row). waits: frames that wanted a turn while Duke was not idle and still; axis: frames the
  // track ran end-on; notLive: frames paused, in a menu or under a letterbox.
  np_log("mouseaim: frames=%u points=%u nocursor=%u freecam=%u noview=%u noray=%u noeye=%u noplane=%u "
         "| track=%u camera=%u trackUnread=%u | cursor (%.3f,%.3f) %dx%d reprojErr=%.2fpx sens=%d own=%u follows=%u "
         "mickeys=%.0f engineMoved=%.0fpx "
         "| face refused=%d writes=%u waits=%u axis=%u notLive=%u stuck=%u moved=%u noStick=%u tainted=%u",
         s_frames, s_points, s_noCursor, s_freecam, s_noView, s_noRay, s_noEye, s_noPlane, s_planeTrack, s_planeCamera,
         s_trackUnread, s_nx, s_ny, W, H, s_reprojErr, g_cfg.mouseSens, s_ownMoves, s_engFollows, s_mickeys, s_engMoved,
         s_faceRefused, s_faceWrites, s_faceWaits, s_faceAxis, s_faceNotLive,
         s_faceVerdict[MA_FACE_INPLACE], s_faceVerdict[MA_FACE_MOVED], s_faceVerdict[MA_FACE_NOTURN],
         s_faceVerdict[MA_FACE_UNCLEAN]);
}

// The device's deltas move our cursor. Unread (panel, freecam, focus lost, WndProc input) it holds, unless
// the engine's cursor moved: then the engine read events we could not, and ours follows it (no sensitivity).
static bool cursor_update(int W, int H) {
  float ex, ey;
  g_eng.inp_cursor_info(&ex, &ey);
  if (!(ex >= -1.001f && ex <= 1.001f && ey >= -1.001f && ey <= 1.001f)) return false;
  const float epx = ndc_to_px_x(ex, W), epy = ndc_to_px_y(ey, H);
  const float edx = epx - s_engPx, edy = epy - s_engPy;
  const bool engMoved = s_seeded && (edx != 0.f || edy != 0.f);
  if (engMoved) s_engMoved += sqrt((double)(edx * edx + edy * edy));
  s_engPx = epx; s_engPy = epy;
  int dx, dy;
  const bool read = hotkeys_mouse_xy_take(&dx, &dy);
  if (!s_seeded || W != s_cW || H != s_cH) {
    // The seed is the engine's cursor; deltas other readers piled up while mouse aim was off are dropped.
    s_cx = epx; s_cy = epy; s_cW = W; s_cH = H; s_seeded = 1;
    dx = 0; dy = 0;
  }
  if (read) {
    s_cx = ma_cursor_step(s_cx, dx, g_cfg.mouseSens, W);
    s_cy = ma_cursor_step(s_cy, dy, g_cfg.mouseSens, H);
    s_mickeys += (double)(dx < 0 ? -dx : dx) + (double)(dy < 0 ? -dy : dy);
    s_ownMoves++;
  } else if (engMoved) {
    s_cx = epx; s_cy = epy;
    s_engFollows++;
  }
  s_nx = ndc_x(s_cx, W);
  s_ny = ndc_y(s_cy, H);
  return true;
}

void mouseaim_frame(const unsigned char* player, int W, int H) {
  ma_log(W, H);
  s_have = 0;
  s_cursorOk = 0;
  if (!mouseaim_on() || !player || !g_eng.inp_cursor_info) {
    s_probe.active = 0;
    s_seeded = 0;   // reseeded from the engine's cursor when mouse aim is back
    return;
  }
  s_frames++;
  if (!cursor_update(W, H)) { s_noCursor++; return; }
  s_cursorOk = 1;
  // The freecam's view is not the game's, and the cursor is frozen while it flies (mouse-aim.md section 4).
  if (input_freecam_on()) { s_freecam++; s_probe.active = 0; return; }
  Mat4 view, proj;
  float nearZ;
  if (!view_get(&view, &proj, &nearZ)) { s_noView++; return; }
  Vec3 ro, rd, eye, t;
  if (!ma_cursor_ray(s_nx, s_ny, view, proj, &ro, &rd)) { s_noRay++; return; }
  if (!aim_fire_origin(player, &eye)) { s_noEye++; return; }
  if (!plane_tangent(player, view, &t) || !ma_plane_point(ro, rd, eye, t, &s_point)) { s_noPlane++; return; }
  s_have = 1;
  s_points++;
  // The point back through the view against the reticle: it checks our inverse only. Whether the cursor's NDC is the 3D
  // viewport's needs a look at the reticle against the impacts in game.
  const ScreenPt sp = w2s_project(s_point, view, proj, nearZ, W, H);
  if (sp.ok) {
    const float ex = sp.x - ndc_to_px_x(s_nx, W), ey = sp.y - ndc_to_px_y(s_ny, H);
    s_reprojErr = sqrtf(ex * ex + ey * ey);
    if (!s_u2Logged) {
      s_u2Logged = 1;
      float fn = 0.f, ff = 0.f, fov = 0.f;
      if (g_eng.rend_projection_get) g_eng.rend_projection_get(&fn, &ff, &fov);
      np_log("mouseaim: first point (%.1f,%.1f,%.1f) from eye (%.1f,%.1f,%.1f); cursor ndc (%.3f,%.3f) -> reticle "
             "(%.1f,%.1f) in %dx%d, point reprojects to (%.1f,%.1f); fov %.2f proj[0]=%.4f proj[5]=%.4f",
             s_point.x, s_point.y, s_point.z, eye.x, eye.y, eye.z, s_nx, s_ny, ndc_to_px_x(s_nx, W),
             ndc_to_px_y(s_ny, H), W, H, sp.x, sp.y, fov, proj.m[0], proj.m[5]);
    }
  }
  face_frame(player, view, proj, nearZ, W, H);
}

// The style rows (mouseaim.h): every part keyed in black when Outline is on. Coordinates are the canvas's
// current ones, so the panel's strip (UI scale) and the game view (100%) share this one path.
void mouseaim_draw_reticle(int cx, int cy, unsigned (*tint)(unsigned)) {
  MaRect r[5];
  const int n = ma_reticle_rects(cx, cy, g_cfg.retSize, g_cfg.retThick, g_cfg.retDot, r);
  const unsigned colour = tint ? tint(ma_reticle_colour(g_cfg.retColour)) : ma_reticle_colour(g_cfg.retColour);
  const unsigned edge = tint ? tint(C_KEYLINE) : C_KEYLINE;
  if (g_cfg.retOutline)
    for (int i = 0; i < n; ++i) canvas_rect(r[i].x - 1, r[i].y - 1, r[i].w + 2, r[i].h + 2, edge);
  for (int i = 0; i < n; ++i) canvas_rect(r[i].x, r[i].y, r[i].w, r[i].h, colour);
}

// Hidden while the panel is open. An in-world preview while editing was tried and never showed in game;
// its cause was not found by reading, so it was removed rather than guessed at.
void mouseaim_draw(int W, int H) {
  if (!mouseaim_on() || !g_cfg.mouseReticle || !s_cursorOk || input_panel_open() || input_freecam_on() ||
      input_console_open())
    return;
  const int cx = clamp_double_to_int((double)s_cx + 0.5), cy = clamp_double_to_int((double)s_cy + 0.5);
  canvas_begin(W, H);
  mouseaim_draw_reticle(cx, cy, 0);
  canvas_end();
}
