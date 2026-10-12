// Scroll-wheel zoom glue (wheelzoom.h). Main thread only: the WndProc, the frame path and the DirectInput
// read share the engine's one thread (input.h). No engine write: the device read is the engine device's
// immediate data, a stream separate from the buffered GetDeviceData input.dll reads.
#include "wheelzoom.h"
#include "freecamfov.h"
#include "hotkeys.h"   // hotkeys_mouse_lz: the engine's mouse device, found by signature
#include "input.h"
#include "config.h"
#include "log.h"
#include <windows.h>

static int s_residue;
static int s_wheelM = WZ_WHEEL_START_M;   // the wheel's own distance: session only, never in the ini
static int s_engaged;                     // the wheel holds the zoom-out view (wz_engaged_after)
static float s_cur, s_view;               // eased: the slider's Distance (the hold), the view without the hold
static int s_haveCur;
static LARGE_INTEGER s_qpf, s_last;
static unsigned s_devNotches, s_msgNotches, s_applied;
static int s_lastPath;   // 0 none yet, 1 device, 2 message
static int s_wasLive;
static int s_flyNotches;   // notches turned in flight, taken by the FOV owner (freecamfov.h)

static const char* path_name(int p) { return p == 1 ? "the mouse device" : "WM_MOUSEWHEEL"; }

void wheelzoom_log_totals(void) {
  unsigned c[3];
  input_wheel_counts(c);
  np_log("wheelzoom: totals: notches via device %u, via messages %u, distance changes %u; WM_MOUSEWHEEL seen "
         "in play %u, panel %u, freecam %u", s_devNotches, s_msgNotches, s_applied, c[0], c[1], c[2]);
}

void wheelzoom_reset_distance(void) {
  if (s_wheelM != WZ_WHEEL_START_M) np_log("wheelzoom: Distance reset: wheel distance %d -> %d m", s_wheelM, WZ_WHEEL_START_M);
  s_wheelM = WZ_WHEEL_START_M;
}

static void set_engaged(int engaged, const char* why) {
  if (engaged != s_engaged) np_log("wheelzoom: zoom-out view %s by the wheel (%s)", engaged ? "engaged" : "released", why);
  s_engaged = engaged;
}

void wheelzoom_frame(void) {
  if (!s_qpf.QuadPart) { QueryPerformanceFrequency(&s_qpf); QueryPerformanceCounter(&s_last); }
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  float dt = (float)((double)(now.QuadPart - s_last.QuadPart) / (double)s_qpf.QuadPart);
  s_last = now;
  if (dt > 0.25f) dt = 0.25f;

  const int msg = input_play_wheel_take();   // drained every frame, so no stale notch lands later
  const bool flying = input_freecam_on();   // the wheel drives the FOV then, whatever the switch says
  const bool live = (g_cfg.wheelZoom != 0 || flying) && !input_panel_open() && !input_console_open();
  const int fresh = live && !s_wasLive;
  s_wasLive = live;
  int devLz = 0;
  const bool devOk = live && hotkeys_mouse_lz(&devLz);
  if (live) {
    const int delta = wz_pick(devOk ? 1 : 0, devLz, msg, fresh);
    const int n = wz_take_notches(&s_residue, delta);
    if (flying) {
      // Not the zoom-out distance: its state stays as it was (only the switch may still release it).
      set_engaged(wz_engaged_after(s_engaged, g_cfg.wheelZoom, 0, s_wheelM), "switch");
      s_flyNotches += n;
    } else {
      set_engaged(wz_engaged_after(s_engaged, 1, n, s_wheelM), "notch");
      if (n) {
        const int path = devOk ? 1 : 2;
        if (path == 1) s_devNotches += (unsigned)(n < 0 ? -n : n); else s_msgNotches += (unsigned)(n < 0 ? -n : n);
        if (path != s_lastPath) { np_log("wheelzoom: wheel read from %s", path_name(path)); s_lastPath = path; }
        const int d = wz_distance_after(s_wheelM, n);
        if (d != s_wheelM) {
          np_log("wheelzoom: %+d notch(es) via %s: wheel distance %d -> %d m", n, path_name(path), s_wheelM, d);
          s_wheelM = d;
          s_applied++;
        }
      }
    }
  } else {
    s_residue = 0;   // a part notch from before the panel opened is not carried across it
    set_engaged(wz_engaged_after(s_engaged, g_cfg.wheelZoom, 0, s_wheelM), "switch");
  }

  const int smooth = g_cfg.wheelZoom && g_cfg.wheelSmooth;
  const float slider = (float)wz_distance_after(g_cfg.zoomOutDist, 0);
  if (!s_haveCur) { s_cur = slider; s_view = wz_view_target(0, s_wheelM, g_cfg.zoomOutOn, g_cfg.zoomOutDist); s_haveCur = 1; }
  s_cur = wz_smooth_step(s_cur, slider, dt, smooth);
  s_view = wz_smooth_step(s_view, wz_view_target(s_engaged, s_wheelM, g_cfg.zoomOutOn, (int)slider), dt, smooth);
}

float wheelzoom_distance_m(void) { return s_haveCur ? s_cur : (float)g_cfg.zoomOutDist; }
float wheelzoom_view_m(void) { return s_view; }

int wheelzoom_take_fly_notches(void) { const int n = s_flyNotches; s_flyNotches = 0; return n; }
