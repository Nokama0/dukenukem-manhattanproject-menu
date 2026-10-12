#pragma once
// Freecam wheel FOV: while the freecam
// flies, each wheel notch changes the field of view and the wheel owns g_cam_fov; afterwards Custom FOV, then
// the stock, own it again. Pure rules; the glue is in zoom.cpp (owner) and wheelzoom.cpp (notches).
#include "zoom.h"   // FOV_MIN_DEG, FOV_MAX_DEG, fov_want_deg, FOV_STOCK
#include "freecam.h"

enum { FCF_STEP_DEG = 2 };

// Away from you (positive) narrows, like a zoom-in; clamped to the Custom field of view
// slider's range.
inline double fcf_deg_after(double deg, int notches) {
  const double v = deg - (double)FCF_STEP_DEG * (double)notches;
  return v < FOV_MIN_DEG ? (double)FOV_MIN_DEG : v > FOV_MAX_DEG ? (double)FOV_MAX_DEG : v;
}

// One owner of g_cam_fov a frame: the flying wheel, then Custom FOV, then the stock.
enum { FCF_OWNER_STOCK = 0, FCF_OWNER_CUSTOM = 1, FCF_OWNER_WHEEL = 2 };
inline int fcf_owner(int flying, int customOn) {
  return flying ? FCF_OWNER_WHEEL : customOn ? FCF_OWNER_CUSTOM : FCF_OWNER_STOCK;
}

// The FOV a flight starts from.
inline double fcf_gameplay_deg(int customOn, int cfgDeg, double stockDeg) {
  return customOn ? fov_want_deg(cfgDeg, stockDeg) : stockDeg;
}

// What the zoom writer applies: on=0 releases (fov_step restores the stock string); stockString: write the
// captured string itself, not a rounding of it (Custom at its reset).
struct FcfPlan { int on; double want; int stockString; };
inline FcfPlan fcf_plan(int flying, int customOn, int cfgDeg, double stockDeg, double wheelDeg) {
  FcfPlan p = { 0, 0.0, 0 };
  switch (fcf_owner(flying, customOn)) {
    case FCF_OWNER_WHEEL:  p.on = 1; p.want = wheelDeg; break;
    case FCF_OWNER_CUSTOM: p.on = 1; p.want = fov_want_deg(cfgDeg, stockDeg); p.stockString = cfgDeg == FOV_STOCK; break;
    default: break;
  }
  return p;
}

// The Debug readout line with the FOV (freecam_readout's pose text, then the field of view).
inline void freecam_readout_fov(const FreecamPose& p, double fovDeg, char* out, int cap) {
  if (!out || cap <= 0) return;
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "cam x=%.0f y=%.0f z=%.0f yaw=%.1f pitch=%.1f fov=%.1f",
              p.eye[0], p.eye[1], p.eye[2], p.yaw, p.pitch, fovDeg);
}

// --- The glue, main thread only ------------------------------------------------------------------
// wheelzoom.cpp: whole notches the wheel turned during flight since the last call (the wheel zoom's
// own distance does not move meanwhile).
int wheelzoom_take_fly_notches(void);
// zoom.cpp: the FOV the wheel is driving (eased), true only while it owns g_cam_fov.
bool fov_flight_deg(double* deg);
