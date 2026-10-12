// Freecam "Draw the whole map" (no culling): the frustum exports report visible and g_cam_far is
// raised while the freecam flies with the switch set. g_cam_far goes through
// `uset` only (the zoom.cpp pattern); the frustum is changed only by our return values.
#include "nocull.h"
#include "engine.h"
#include "hook.h"
#include "cvar_read.h"
#include "config.h"
#include "input.h"
#include "freecam.h"   // freecam_pause_holding
#include "probe.h"
#include "log.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>   // atof

// All three open with `mov al,[0x4a0a5c]` (5 bytes, absolute operand: safe to copy
// into the trampoline), then test/jz/call rel32 to the lazy plane rebuild 0x41fd10.
// The fingerprint covers the call, so a moved rebuild refuses the patch.
static const unsigned char kSphereFp[] = {   // 0x41ff80
  0xa0, 0x5c, 0x0a, 0x4a, 0x00, 0x84, 0xc0, 0x74, 0x05, 0xe8, 0x82, 0xfd, 0xff, 0xff, 0x56, 0x57 };
static const unsigned char kDistFp[] = {     // 0x420080
  0xa0, 0x5c, 0x0a, 0x4a, 0x00, 0x84, 0xc0, 0x74, 0x05, 0xe8, 0x82, 0xfc, 0xff, 0xff, 0x8b, 0x4c };
static const unsigned char kBoxFp[] = {      // 0x41ffe0
  0xa0, 0x5c, 0x0a, 0x4a, 0x00, 0x83, 0xec, 0x08, 0x84, 0xc0, 0x74, 0x05, 0xe8, 0x1f, 0xfd, 0xff };

static bool (__cdecl* s_sphereTramp)(const float* c, float r);
static bool (__cdecl* s_distTramp)(const float* c, float r, float maxDist);
static bool (__cdecl* s_boxTramp)(const float* corners);

// Set once a frame by nocull_frame; the detours run in the next frame's world pass.
static int s_force;
// Cumulative: every call seen, and those answered "visible" without the engine's
// test. 64-bit and never reset: at ~10k sphere tests/frame a 32-bit counter
// would wrap in about 2 hours of play and the forced/total ratio would go wrong.
static unsigned long long s_calls[3], s_forced[3];

// Callers test AL only (duke!0x100ee83d after _sphere, 0x100eea61 after _dist); plain RET.
static bool __cdecl sphere_detour(const float* c, float r) {
  s_calls[0]++;
  if (s_force) { s_forced[0]++; return true; }
  return s_sphereTramp(c, r);
}
// _dist calls rend_sphere_visible itself (0x4200d0), so a pass-through reaches sphere_detour too.
static bool __cdecl dist_detour(const float* c, float r, float maxDist) {
  s_calls[1]++;
  if (s_force) { s_forced[1]++; return true; }
  return s_distTramp(c, r, maxDist);
}
static bool __cdecl box_detour(const float* corners) {
  s_calls[2]++;
  if (s_force) { s_forced[2]++; return true; }
  return s_boxTramp(corners);
}

int nocull_install(void) {
  int n = 0;
  if (g_eng.rend_sphere_visible)
    n += hook_install_at((void*)g_eng.rend_sphere_visible, kSphereFp, sizeof kSphereFp, 5,
                         (void*)sphere_detour, (void**)&s_sphereTramp);
  if (g_eng.rend_sphere_visible_dist)
    n += hook_install_at((void*)g_eng.rend_sphere_visible_dist, kDistFp, sizeof kDistFp, 5,
                         (void*)dist_detour, (void**)&s_distTramp);
  if (g_eng.rend_box_visible)
    n += hook_install_at((void*)g_eng.rend_box_visible, kBoxFp, sizeof kBoxFp, 5,
                         (void*)box_detour, (void**)&s_boxTramp);
  np_log("nocull: %d of 3 frustum detours installed", n);
  return n;
}

static cvar_t* s_farCvar;
static int s_resolveTried;

static cvar_t* far_cvar(void) {
  if (s_farCvar || s_resolveTried) return s_farCvar;
  s_resolveTried = 1;
  if (g_eng.cvar_get) s_farCvar = g_eng.cvar_get("g_cam_far");
  if (!s_farCvar) np_log("nocull: g_cam_far not found; the far plane stays stock");
  return s_farCvar;
}

static unsigned s_projPushes;   // cumulative: freecam pushes that re-applied the projection (paused world)
static unsigned s_overridePushes;   // cumulative: unpaused pushes that found the map's own far below the raised one
static float s_overrideLogged;      // the override far last logged; reset when the flight ends, so once per flight/map

static char s_stock[64] = "?";   // "?" until an apply transition actually reads one
static int s_haveStock, s_applied;
static ULONGLONG s_nextLog;

static void uset_far(const char* value) {
  char cmd[128];
  _snprintf_s(cmd, sizeof cmd, _TRUNCATE, "uset g_cam_far \"%s\"", value);
  g_eng.cmd_execute(cmd);
}

// far: while applied it is NOCULL_FAR by construction (we just wrote it, and
// nothing reads it again until restore), so the 5 s line needs no live read.
static void log_status(void) {
  float n = 0.f, f = 0.f, fov = 0.f;
  if (g_eng.rend_projection_get) g_eng.rend_projection_get(&n, &f, &fov);
  const double fps = probe_measured_fps();
  np_log("nocull: far=%s stock=%s projection-top near=%.1f far=%.0f frame=%.2fms (%.1f fps) "
         "forced sphere=%llu/%llu dist=%llu/%llu box=%llu/%llu paused-proj-pushes=%u override-pushes=%u", s_applied ? NOCULL_FAR : "?",
         s_stock, n, f, fps > 0.0 ? 1000.0 / fps : 0.0, fps, s_forced[0], s_calls[0], s_forced[1], s_calls[1],
         s_forced[2], s_calls[2], s_projPushes, s_overridePushes);
}

void nocull_frame(void) {
  const int active = nocull_active(input_freecam_on() ? 1 : 0, 1);   // always while flying (panel clean-up)
  if (active != s_force) {
    np_log("nocull: frustum %s", active ? "forced visible" : "stock");
    s_nextLog = GetTickCount64() + 5000;
  }
  s_force = active;

  cvar_t* cv = far_cvar();
  if (cv && g_eng.cmd_execute) {
    // Read the stock only on the frame that raises the far plane: that
    // read is the stock by construction, and held/restore/inactive frames
    // never need a fresh one -- this was 2-3 guarded reads every frame before.
    const bool willApply = active && !s_applied;
    char cur[64] = "?";
    const bool haveCur = willApply && cvar_read_string(cv, cur, sizeof cur);
    const int act = nocull_far_write(active, s_applied, haveCur);
    if (act == NOCULL_APPLY) {
      strncpy_s(s_stock, sizeof s_stock, cur, _TRUNCATE);
      s_haveStock = 1;
      uset_far(NOCULL_FAR);
      s_applied = 1;
      np_log("nocull: on far=%s stock=%s", NOCULL_FAR, s_stock);
    } else if (act == NOCULL_RESTORE) {
      uset_far(s_stock);
      s_applied = 0;
      np_log("nocull: off, g_cam_far -> stock %s", s_stock);
    }
  }

  if (!active) { s_overrideLogged = 0.f; return; }
  const ULONGLONG t = GetTickCount64();
  if (t >= s_nextLog) {
    s_nextLog = t + 5000;
    log_status();
  }
}

void nocull_restore_for_quit(void) {
  s_force = 0;
  if (!s_applied || !s_haveStock || !s_farCvar || !g_eng.cmd_execute) return;
  uset_far(s_stock);
  s_applied = 0;
  np_log("nocull: quit, g_cam_far restored to %s", s_stock);
}

// g_cam_near / g_cam_fov, resolved on first use like g_cam_far.
static cvar_t* s_nearCvar;
static cvar_t* s_fovCvar;
static int s_projResolveTried;

// A cvar's value as the builder's cache takes it (atof of the string, FUN_1008c440).
static int cvar_float(cvar_t* cv, float* out) {
  char buf[64];
  if (!cv || !cvar_read_string(cv, buf, sizeof buf)) return 0;
  *out = (float)atof(buf);
  return 1;
}

// The builder (duke!0x1008c4c0, in wm_draw before this push) has just put its cached values back; the frustum and draw
// come after, so applying here covers them. Paused: two cvar reads a frame while the world is frozen. Unpaused: only
// when the projection's far is below the raised one (the world's +0xF4 override), far alone, no cvar reads.
void nocull_push_projection(void) {
  if (!g_eng.rend_projection_get || !g_eng.rend_projection_change) return;
  const int holding = freecam_pause_holding() ? 1 : 0;
  NcProj cur = { 0.f, 0.f, 0.f }, want;
  g_eng.rend_projection_get(&cur.n, &cur.f, &cur.fov);
  const int override = nocull_far_override(s_applied, cur.f);
  if (override && !holding) {   // paused, a short far is also the stale cache: not evidence of an override
    s_overridePushes++;
    if (cur.f != s_overrideLogged) {
      s_overrideLogged = cur.f;
      np_log("nocull: the map's own far plane is %.0f (world +0xF4 override), pushing %s", cur.f, NOCULL_FAR);
    }
  }
  if (!holding && !override) return;
  if (!s_projResolveTried && g_eng.cvar_get) {
    s_projResolveTried = 1;
    s_nearCvar = g_eng.cvar_get("g_cam_near");
    s_fovCvar = g_eng.cvar_get("g_cam_fov");
    np_log("nocull: paused projection follows g_cam_near %s, g_cam_fov %s", s_nearCvar ? "found" : "MISSING",
           s_fovCvar ? "found" : "MISSING");
  }
  float cvNear = 0.f, cvFov = 0.f;
  const int haveNear = holding && cvar_float(s_nearCvar, &cvNear), haveFov = holding && cvar_float(s_fovCvar, &cvFov);
  if (!nocull_proj_push(1, s_applied, haveNear, cvNear, haveFov, cvFov, cur, &want)) return;
  g_eng.rend_projection_change(want.n, want.f, want.fov);
  s_projPushes++;
}
