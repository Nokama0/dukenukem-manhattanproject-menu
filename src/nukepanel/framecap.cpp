// Frame-cap engine glue: monitor-refresh resolve, the waitable-timer wait and
// the pacing schedule (framecap.h). Waits at the end of the rend_end_frame
// detour, not a sys_frame hook: its prologue has a rel32 call (2026-09-27).
#include "framecap.h"
#include "config.h"
#include "log.h"
#include "engine.h"
#include "cvar_read.h"
#include <stdlib.h>

// Refresh rate (Hz) of the monitor holding `wnd`, 0 if any step fails.
static int monitor_refresh_hz(HWND wnd) {
  if (!wnd) return 0;
  const HMONITOR mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
  if (!mon) return 0;
  MONITORINFOEXA mi;
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoA(mon, (MONITORINFO*)&mi)) return 0;
  DEVMODEA dm;
  dm.dmSize = sizeof(dm);
  if (!EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) return 0;
  return (int)dm.dmDisplayFrequency;
}

// Cached once resolved; retried every call until wnd is known, since the
// window may not exist yet on the first frame. Never writes g_cfg -- a
// stored default would ride along on the next unrelated config write.
static int s_monitorHz;
static bool s_monitorResolved;
static bool s_saidFallback;

// Retried every call until wnd is known, since the window may not exist yet
// on the first frame. Returns the cached Hz, 0 if still unresolved.
static int resolve_monitor(HWND wnd) {
  if (!s_monitorResolved) {
    const int hz = monitor_refresh_hz(wnd);
    if (hz >= 30 && hz <= 240) {
      s_monitorHz = hz;
      s_monitorResolved = true;
      np_log("framecap: default %d from monitor", hz);
    } else if (!s_saidFallback) {
      s_saidFallback = true;
      np_log("framecap: default fallback 60, window unknown");
    }
  }
  return s_monitorResolved ? s_monitorHz : 0;
}

// The Zoom launcher forces `gl_sync` at 175 Hz; read it with zoom.cpp's cvar reader so the Cap slider and pacing
// both know the game cannot exceed the monitor refresh.
static cvar_t* s_syncCvar;
static int s_syncResolveTried;

static cvar_t* sync_cvar(void) {
  if (s_syncCvar || s_syncResolveTried) return s_syncCvar;
  s_syncResolveTried = 1;
  if (g_eng.cvar_get) s_syncCvar = g_eng.cvar_get("gl_sync");
  if (!s_syncCvar) np_log("framecap: gl_sync not found");
  return s_syncCvar;
}

static bool vsync_on(void) {
  cvar_t* cv = sync_cvar();
  if (!cv) return false;
  char buf[64];
  if (!cvar_read_string(cv, buf, sizeof buf)) return false;
  return atof(buf) != 0.0;
}

FcCapInfo framecap_cap_info(HWND wnd) {
  const int refresh = resolve_monitor(wnd);
  const bool vsync = vsync_on();
  const FcCapClamp c = fc_cap_clamp(g_cfg.frameCapFps, refresh, vsync ? 1 : 0);
  FcCapInfo info;
  info.sliderHi = c.sliderHi;
  info.effectiveFps = c.effectiveFps;
  info.refreshHz = refresh;
  info.vsyncCapped = vsync && refresh >= 30 && refresh <= 240;
  return info;
}

int framecap_effective_fps(HWND wnd) {
  return framecap_cap_info(wnd).effectiveFps;
}

static LARGE_INTEGER s_qpf;
static long long s_deadline;
static bool s_haveDeadline;

// Created once and reused via repeated SetWaitableTimer calls; left open for
// the process's life, like the rest of this proxy.
static HANDLE s_timer;
static bool s_timerTried;
static bool s_timerBroken;

static HANDLE get_timer(void) {
  if (s_timerTried) return s_timer;
  s_timerTried = true;
  s_timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                   TIMER_ALL_ACCESS);
  if (!s_timer) np_log("framecap: CreateWaitableTimerExW failed (%lu); falling back to Sleep",
                       GetLastError());
  return s_timer;
}

// Waits until `s_deadline`: a waitable timer to within 1 ms, then a QPC spin
// for that tail. Falls back to Sleep(1) down to 2 ms out when the timer
// cannot be created or armed, so a timer failure never spins the whole gap.
static void wait_for_deadline(long long waitTicks) {
  const long long spinTicks = s_qpf.QuadPart / 1000;         // 1 ms, spun on QPC
  const long long sleepFloor = s_qpf.QuadPart * 2 / 1000;    // stop Sleep()-ing 2 ms out
  bool waited = false;
  if (waitTicks > spinTicks) {
    const HANDLE timer = get_timer();
    if (timer) {
      const long long timerTicks = waitTicks - spinTicks;
      LARGE_INTEGER due;
      due.QuadPart = -(LONGLONG)((double)timerTicks * 1.0e7 / (double)s_qpf.QuadPart);
      if (due.QuadPart >= 0) due.QuadPart = -1;   // must be strictly negative (relative)
      if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
        WaitForSingleObject(timer, INFINITE);
        waited = true;
      } else if (!s_timerBroken) {
        s_timerBroken = true;
        np_log("framecap: SetWaitableTimer failed (%lu); falling back to Sleep", GetLastError());
      }
    }
  }
  if (!waited) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    while (s_deadline - now.QuadPart > sleepFloor) {
      Sleep(1);
      QueryPerformanceCounter(&now);
    }
  }
  LARGE_INTEGER now;
  do { QueryPerformanceCounter(&now); } while (now.QuadPart < s_deadline);
}

void framecap_frame(HWND wnd) {
  if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
  LARGE_INTEGER before;
  QueryPerformanceCounter(&before);

  const bool minimized = wnd && IsIconic(wnd);
  const bool visible = wnd && IsWindowVisible(wnd) != 0;
  const bool foreground = wnd && GetForegroundWindow() == wnd;
  const int fps = framecap_effective_fps(wnd);
  // Never wait while minimised, or while neither foreground nor visible
  // (alt-tabbed behind another window): a busy spin there burns a core for
  // nothing the player can see.
  const bool skip = !g_cfg.frameCapOn || fps <= 0 || minimized || (!foreground && !visible);
  if (skip) {
    s_haveDeadline = false;
    return;
  }

  const long long period = s_qpf.QuadPart / fps;
  if (!s_haveDeadline) {
    s_deadline = before.QuadPart + period;
    s_haveDeadline = true;
    return;
  }
  const long long waitTicks = fc_wait_ticks(s_deadline, before.QuadPart);
  if (waitTicks > 0) wait_for_deadline(waitTicks);

  LARGE_INTEGER after;
  QueryPerformanceCounter(&after);
  s_deadline = fc_next_deadline(s_deadline, after.QuadPart, period);
}
