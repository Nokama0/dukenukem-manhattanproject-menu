// Custom field of view over g_cam_fov (zoom.h, docs/re/quick-wins.md section 2): the slider's value in place of a zoom
// level. The engine's own cvar string, main thread only; the exact stock string goes back when the switch goes off and
// at quit. g_cam_fov is not archived and WideGen rewrites it every launch, so no crash stash is needed.
#include "zoom.h"
#include "freecamfov.h"
#include "wheelzoom.h"   // wz_smooth_step: the ease
#include "engine.h"
#include "config.h"
#include "cvar_read.h"
#include "input.h"      // input_panel_open: the stock is re-read only while the panel shows it
#include "view.h"       // view_last_fov: the projection the engine applied
#include "log.h"
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static cvar_t* s_fovCvar;
static int s_resolveTried;

static cvar_t* fov_cvar(void) {
  if (s_fovCvar || s_resolveTried) return s_fovCvar;
  s_resolveTried = 1;
  if (g_eng.cvar_get) s_fovCvar = g_eng.cvar_get("g_cam_fov");
  if (!s_fovCvar) np_log("fov: g_cam_fov not found; Custom field of view unavailable");
  return s_fovCvar;
}

static FovState s_st;
static char s_stock[64];          // read on the apply frame, or the last read while not applied
static double s_stockDeg;
static int s_haveStock, s_unreadLogged;
static unsigned s_rewrites;
static ULONGLONG s_nextRewriteLog;
static int s_wasInLevel, s_levelFrames;
static unsigned s_reassertFrames, s_reasserts;
static int s_wroteStock;                  // the applied value is the stock string itself (Custom at its reset)
// Freecam wheel FOV (freecamfov.h): the wheel's target and its eased, drawn value.
static int s_flyOwns;
static double s_flyTarget, s_flyCur;
static LARGE_INTEGER s_flyQpf, s_flyLast;

static void uset(const char* value) {
  char cmd[128];
  _snprintf_s(cmd, sizeof cmd, _TRUNCATE, "uset g_cam_fov \"%s\"", value);
  g_eng.cmd_execute(cmd);
}

// The stored 0 writes the captured string itself, not a rounding of it.
static void write_want(double want, int stockString) {
  if (stockString) { uset(s_stock); return; }
  char v[32];
  _snprintf_s(v, sizeof v, _TRUNCATE, "%.4f", want);
  uset(v);
}

static bool read_stock(cvar_t* cv) {
  char cur[64];
  if (!cvar_read_string(cv, cur, sizeof cur)) {
    if (!s_unreadLogged++) np_log("fov: g_cam_fov unreadable; custom value not applied");
    return false;
  }
  strcpy_s(s_stock, sizeof s_stock, cur);
  s_stockDeg = atof(cur);
  s_haveStock = 1;
  return true;
}

bool fov_available(void) { return fov_cvar() != NULL && g_eng.cmd_execute != NULL; }

bool fov_stock(double* deg) {
  // The panel's first open frame draws before fov_frame's read: read here, so no "stock 40" flashes.
  if (!s_haveStock && fov_cvar()) read_stock(fov_cvar());
  if (!s_haveStock) return false;
  *deg = s_stockDeg;
  return true;
}

// The projection the camera applied beside the cvar, once per level, 60 frames in (the camera has
// run by then). A mismatch names a map with the per-server +0xf4 override.
static void level_log(cvar_t* cv, int inLevel) {
  if (inLevel && !s_wasInLevel) s_levelFrames = 0;
  s_wasInLevel = inLevel;
  if (!inLevel || s_levelFrames >= 60 || ++s_levelFrames != 60) return;
  char cur[64] = "?";
  cvar_read_string(cv, cur, sizeof cur);
  const double eng = (double)view_last_fov();
  const double want = atof(cur);
  np_log("fov: level: projection fov=%.3f g_cam_fov=\"%s\" custom=%s%s", eng, cur, s_st.applied ? "on" : "off",
         (eng - want > 0.01 || want - eng > 0.01) ? " MISMATCH (per-map override?)" : "");
}

void fov_frame(int inLevel) {
  cvar_t* cv = fov_cvar();
  if (!cv || !g_eng.cmd_execute) return;
  level_log(cv, inLevel);
  // The menu shows and resets to the stock: kept fresh while it is on screen and ours is not applied.
  if (!s_st.applied && input_panel_open()) read_stock(cv);

  // One owner a frame (fcf_owner). A flight starts from the gameplay FOV, so it needs the stock read.
  float dt = 0.f;
  if (!s_flyQpf.QuadPart) { QueryPerformanceFrequency(&s_flyQpf); QueryPerformanceCounter(&s_flyLast); }
  { LARGE_INTEGER now; QueryPerformanceCounter(&now);
    dt = (float)((double)(now.QuadPart - s_flyLast.QuadPart) / (double)s_flyQpf.QuadPart); s_flyLast = now;
    if (dt > 0.25f) dt = 0.25f; }
  const int flying = input_freecam_on() ? 1 : 0;
  const int notches = wheelzoom_take_fly_notches();   // drained every frame, so none lands late
  if (flying && !s_flyOwns && !s_st.applied) read_stock(cv);   // retried each frame until it reads
  const int owns = flying && s_haveStock;
  if (owns && !s_flyOwns) {   // the rising edge: from the gameplay FOV, never a stale or zero value
    s_flyTarget = s_flyCur = fcf_gameplay_deg(g_cfg.fovOn != 0, g_cfg.fovDeg, s_stockDeg);
    np_log("fov: freecam wheel takes g_cam_fov from %.4f", s_flyCur);
  }
  s_flyOwns = owns;
  if (s_flyOwns) {
    if (notches) s_flyTarget = fcf_deg_after(s_flyTarget, notches);
    s_flyCur = (double)wz_smooth_step((float)s_flyCur, (float)s_flyTarget, dt, g_cfg.wheelSmooth);
  }
  const FcfPlan plan = fcf_plan(s_flyOwns, g_cfg.fovOn != 0, g_cfg.fovDeg, s_stockDeg, s_flyCur);
  const int on = plan.on;
  const double want = plan.want;
  const int act = fov_step(s_st, on, want);
  if (act == FOV_APPLY) {
    if (!read_stock(cv)) return;   // tried again next frame
    // The stock was re-read just above, so the plan is made again from it.
    const FcfPlan ap = fcf_plan(s_flyOwns, g_cfg.fovOn != 0, g_cfg.fovDeg, s_stockDeg, s_flyCur);
    const double w = ap.want;
    if (zoom_should_write(s_stock, w)) write_want(w, ap.stockString);
    s_wroteStock = ap.stockString;
    fov_done(&s_st, act, w);
    np_log("fov: on, stock \"%s\" -> %.4f", s_stock, w);
  } else if (act == FOV_REWRITE) {
    write_want(want, plan.stockString);
    s_wroteStock = plan.stockString;
    fov_done(&s_st, act, want);
    s_rewrites++;
    const ULONGLONG t = GetTickCount64();
    if (t >= s_nextRewriteLog) {   // a slider drag writes each degree it crosses
      s_nextRewriteLog = t + 1000;
      np_log("fov: now %.4f (%u write(s) since on)", want, s_rewrites);
    }
  } else if (act == FOV_RESTORE) {
    uset(s_stock);
    fov_done(&s_st, act, 0.0);
    np_log("fov: off, g_cam_fov back to stock \"%s\"", s_stock);
  } else if (s_st.applied && ++s_reassertFrames >= 60) {
    // Once a second while applied: a console `uset g_cam_fov` is written over. Nothing in
    // the engine writes it (docs/re/quick-wins.md), so this only answers the player's own typing.
    s_reassertFrames = 0;
    char cur[64];
    const int haveCur = cvar_read_string(cv, cur, sizeof cur) ? 1 : 0;
    if (fov_needs_reassert(s_st, on, haveCur, haveCur ? atof(cur) : 0.0)) {
      write_want(s_st.wrote, s_wroteStock);
      np_log("fov: g_cam_fov was \"%s\" under our %.4f; written again (%u)", cur, s_st.wrote, ++s_reasserts);
    }
  }
}

bool fov_flight_deg(double* deg) {
  if (!s_flyOwns) return false;
  *deg = s_flyCur;
  return true;
}

void fov_restore_for_quit(void) {
  if (!s_st.applied || !s_haveStock || !g_eng.cmd_execute) return;
  uset(s_stock);
  fov_done(&s_st, FOV_RESTORE, 0.0);
  np_log("fov: quit, g_cam_fov back to stock \"%s\"", s_stock);
}
