#pragma once
// Scroll-wheel zoom (docs/re/quick-wins.md section 3): each notch moves the wheel's own pull-back by 1 m, apart from
// the Distance slider (the wheel moving it made Hold to zoom out useless). Never saved: launch and the Distance reset
// arrow use ZOOMOUT_DEF_M. Read from the engine's DirectInput mouse (lZ; input.dll drops DIMOFS_Z), WM_MOUSEWHEEL else.
#include <math.h>
#include "config.h"   // ZOOMOUT_MIN_M, ZOOMOUT_MAX_M

// One notch, in both sources' units: WHEEL_DELTA, and DirectInput's lZ per detent.
enum { WZ_DELTA = 120 };
// c_dfDIMouse, the format input.dll sets (input!0x10006484: dwDataSize 16, DIDF_RELAXIS): DIMOUSESTATE
// { lX, lY, lZ, rgbButtons[4] }, so lZ is the wheel's change since the last GetDeviceState.
enum { WZ_DIMOUSESTATE_SIZE = 16, WZ_LZ_OFFSET = 8 };

// Whole notches in the residue plus delta; what is left (a free-spinning wheel's part notch) waits.
inline int wz_take_notches(int* residue, int delta) {
  const long long t = (long long)*residue + (long long)delta;
  const int n = (int)(t / WZ_DELTA);   // toward zero, so the remainder keeps the delta's sign
  *residue = (int)(t - (long long)n * WZ_DELTA);
  return n;
}

// The wheel turned away from you (positive) brings the camera closer, 1 m a notch, clamped to the
// Distance slider's range.
inline int wz_distance_after(int metres, int notches) {
  const long long v = (long long)metres - (long long)notches;
  return v < ZOOMOUT_MIN_M ? ZOOMOUT_MIN_M : v > ZOOMOUT_MAX_M ? ZOOMOUT_MAX_M : (int)v;
}

// Smooth scrolling: the drawn distance closes 1 - exp(-dt / tau) of its gap each frame, which is frame-rate
// independent, and lands on the target once within 0.01 m. Off: the target at once.
const float WZ_SMOOTH_TAU_S = 0.08f;
inline float wz_smooth_step(float cur, float target, float dt, int smooth) {
  if (!smooth) return target;
  if (!(dt > 0.f)) return cur;
  const float next = cur + (target - cur) * (1.f - expf(-dt / WZ_SMOOTH_TAU_S));
  return fabsf(target - next) < 0.01f ? target : next;
}

// The wheel engages the zoom-out view by itself: a notch toward you pulls the camera back to the wheel's distance and
// it stays until a notch closer arrives with that distance at the slider's minimum (ZOOMOUT_MIN_M, 1 m): then the Zoom
// out switch's view or the normal camera. Session state; turning Scroll-wheel zoom off lets go.
const int WZ_WHEEL_START_M = ZOOMOUT_DEF_M;
inline int wz_engaged_after(int engaged, int wheelOn, int notches, int wheelBeforeM) {
  if (!wheelOn) return 0;
  if (notches < 0) return 1;
  if (notches > 0 && (!engaged || wheelBeforeM <= ZOOMOUT_MIN_M)) return 0;
  return engaged;
}

// What the camera eases to without the hold key (the hold always shows the slider's Distance): the wheel's
// own distance while it is engaged, else the slider's while Zoom out is on, else no pull-back.
inline float wz_view_target(int engaged, int wheelM, int zoomSwitchOn, int sliderM) {
  return engaged ? (float)wheelM : zoomSwitchOn ? (float)sliderM : 0.f;
}

// This frame's wheel: the device's lZ when its read succeeded (acquired, so no message carries the same notch), else
// the WM_MOUSEWHEEL deltas (unacquired: panel or freecam). `fresh`, the first live frame again: lZ holds everything
// scrolled meanwhile (console open, device still acquired), so it and that frame's messages are thrown away.
inline int wz_pick(int devOk, int devLz, int msgDelta, int fresh) {
  if (fresh) return 0;
  return devOk ? devLz : msgDelta;
}

// --- The glue (wheelzoom.cpp), main thread only --------------------------------------------------
// Frame path, after input_frame and before panel_frame: takes this frame's wheel.
void wheelzoom_frame(void);
// The slider's Distance to draw for Hold to zoom out, metres: eased while Smooth scrolling is on.
float wheelzoom_distance_m(void);
// The pull-back to draw without the hold key (wz_view_target), metres, eased while Smooth scrolling is on;
// 0 is the normal camera.
float wheelzoom_view_m(void);
// The Distance row's reset arrow: the wheel's own distance back to the default as well.
void wheelzoom_reset_distance(void);
// The quit path: the cumulative counters.
void wheelzoom_log_totals(void);
