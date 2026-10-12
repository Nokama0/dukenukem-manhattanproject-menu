#pragma once
// Pure frame-pacing rules, in engine ticks (QueryPerformanceCounter ticks in
// framecap.cpp; the suite uses its own units). No Windows API or engine
// state here -- see framecap.cpp for that glue.

// Next deadline: period <= 0 means no cap (returns now). Otherwise steps by
// one period; if now has fallen more than a whole period behind (a stall),
// resyncs to now + period instead of bursting to catch up.
inline long long fc_next_deadline(long long prevDeadline, long long now, long long period) {
  if (period <= 0) return now;
  long long next = prevDeadline + period;
  if (now - next > period) next = now + period;
  return next;
}

// Ticks left to wait before `deadline`, 0 once it has passed.
inline long long fc_wait_ticks(long long deadline, long long now) {
  return deadline > now ? deadline - now : 0;
}

// Effective cap fps from the stored config value and the queried monitor Hz
// (0 if unknown). An explicit stored value in [30,240] always wins;
// otherwise follows the monitor when known, else falls back to 60.
inline int fc_effective_fps(int stored, int monitorHz) {
  if (stored >= 30 && stored <= 240) return stored;
  if (monitorHz >= 30 && monitorHz <= 240) return monitorHz;
  return 60;
}

// The Cap slider's hi and the fps actually used, given gl_sync: the launcher forces vsync at the monitor refresh, so
// the game cannot exceed it. hi stays 240 when vsync is off or the refresh unknown; the 60 fallback (fc_effective_fps)
// is never above a known refresh, so it needs no clamp.
struct FcCapClamp { int sliderHi; int effectiveFps; };
inline FcCapClamp fc_cap_clamp(int stored, int refreshHz, int vsyncOn) {
  const bool capped = vsyncOn != 0 && refreshHz >= 30 && refreshHz <= 240;
  FcCapClamp out;
  out.sliderHi = capped ? refreshHz : 240;
  out.effectiveFps = fc_effective_fps(stored, refreshHz);
  if (capped && out.effectiveFps > refreshHz) out.effectiveFps = refreshHz;
  return out;
}

// The engine glue (framecap.cpp): monitor-refresh query/cache, the
// waitable-timer wait, config and window state. Declared here so callers
// need only this header.
#include <windows.h>
// The cap in effect this frame: caches the monitor Hz once `wnd` is known, then applies fc_cap_clamp to the stored
// value and the live gl_sync cvar. Never writes g_cfg; the UI and the pacing call this, not the stored value.
int framecap_effective_fps(HWND wnd);

// Same resolve, plus the Cap slider's hi and whether vsync is the reason the
// fps is capped (for the "VSync is on..." hint row).
struct FcCapInfo { int sliderHi; int effectiveFps; int refreshHz; bool vsyncCapped; };
FcCapInfo framecap_cap_info(HWND wnd);

void framecap_frame(HWND wnd);
