#pragma once
// Freecam "Draw the whole map" (camera-culling.md section 2): a raised g_cam_far plus the three frustum exports
// forced to "visible". The visibility tree (PVS) is not touched here (docs/re/pvs.md).

// The far plane written while active. Near is unchanged (still 16), so z-buffer
// precision inside the stock 8000 range is unchanged; only geometry beyond it
// gets coarse depth steps (docs/re/camera-culling.md section 2).
#define NOCULL_FAR "1000000"
#define NOCULL_FAR_F 1000000.0f   // the same value, for rend_projection_change

inline int nocull_active(int freecamOn, int switchOn) { return (freecamOn && switchOn) ? 1 : 0; }

// What to write to g_cam_far this frame: only on transitions, never an apply without a captured stock (so a raise
// can always be restored). The glue reads the stock cvar only on the apply transition, so haveStock means
// "read this frame", not "read some time ago".
enum { NOCULL_NONE = 0, NOCULL_APPLY = 1, NOCULL_RESTORE = 2 };
inline int nocull_far_write(int activeNow, int applied, int haveStock) {
  if (activeNow && !applied && haveStock) return NOCULL_APPLY;
  if (!activeNow && applied) return NOCULL_RESTORE;
  return NOCULL_NONE;
}

// The projection the freecam push applies while our pause holds the world: the builder's cache is refreshed only by
// the world tick (freecam-pause.md section 6), so frozen it is stale. Far is the raised one when applied, near and fov
// the live cvars when read and sane, else the projection's own. 1 (and *out) only when it differs from cur.
struct NcProj { float n, f, fov; };

// A world with its own far plane (world +0xF4 != -1, freecam-pause.md section 6) makes the builder apply that
// instead of g_cam_far every frame, paused or not. We never read the field: with our raised far applied, a
// projection far below it is that override (or the frozen stale cache), and the push raises it again.
inline int nocull_far_override(int farApplied, float curFar) { return farApplied && curFar < NOCULL_FAR_F; }

// push: our pause holds, or nocull_far_override fired. Only the pause reads the live near/fov cvars (haveNear /
// haveFov are 0 otherwise), so an unpaused override push changes the far alone.
inline int nocull_proj_push(int push, int farApplied, int haveNear, float cvNear, int haveFov, float cvFov,
                            NcProj cur, NcProj* out) {
  if (!push) return 0;
  NcProj w = cur;
  if (farApplied) w.f = NOCULL_FAR_F;
  if (haveNear && cvNear > 0.f) w.n = cvNear;
  if (haveFov && cvFov > 0.f && cvFov < 180.f) w.fov = cvFov;
  if (w.n == cur.n && w.f == cur.f && w.fov == cur.fov) return 0;
  *out = w;
  return 1;
}

// --- The glue (nocull.cpp), main thread only --------------------------------
// DllMain: detours rend_sphere_visible, _dist and rend_box_visible. Returns how many installed (0..3).
int nocull_install(void);
// Frame path, after freecam_frame: the active decision, the far-plane writes, the 5 s log.
void nocull_frame(void);
// The quit path, before the engine writes config.cfg: puts the stock g_cam_far back.
void nocull_restore_for_quit(void);
// The freecam's client push, before the engine copies the matrices: nocull_proj_push through
// rend_projection_change. Counted in the 5 s line.
void nocull_push_projection(void);
