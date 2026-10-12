#pragma once
// The free camera's pose and its rules, pure so the suite pins them. The pose is substituted for the engine's in the
// rend_camera_push detour (view.cpp). Matrices follow docs/re/render.md section 3: row vectors, the camera looks down
// +z, the inverse's rows are right, up, forward, eye; the world is Y-up.
#include <math.h>
#include <stdio.h>   // _snprintf_s: freecam_readout
#include "config.h"   // the Flight speed range
#include "ui/bind.h" // the camera's bind rows

// The movement keys, one bit each, read once a frame by freecam.cpp.
enum { FCK_FWD = 1, FCK_BACK = 2, FCK_LEFT = 4, FCK_RIGHT = 8,
       FCK_UP = 16, FCK_DOWN = 32, FCK_FAST = 64, FCK_SLOW = 128 };

// Short of vertical, so yaw stays defined and the camera never flips over.
const float FREECAM_PITCH_MAX = 89.f;
const float FREECAM_FAST_MULT = 4.f;
const float FREECAM_SLOW_MULT = 0.25f;

// Degrees per WM_MOUSEMOVE count. Counts, not time, so turning does not depend
// on the frame rate or the frame cap.
const float FREECAM_SENS_DEG = 0.1f;

// Degrees. yaw 0 looks down +z and grows toward +x; pitch 0 is level, + is up.
// hand is the sign of the captured camera's determinant: -1 when the engine's
// right row points the other way, so a mirrored camera still round-trips.
struct FreecamPose { float eye[3]; float yaw, pitch, hand; };

inline float freecam_rad(float deg) { return deg * 0.017453292519943f; }
inline float freecam_deg(float rad) { return rad * 57.29577951308232f; }

inline void freecam_axes(const FreecamPose& p, float right[3], float up[3], float fwd[3]) {
  const float sy = sinf(freecam_rad(p.yaw)), cy = cosf(freecam_rad(p.yaw));
  const float sp = sinf(freecam_rad(p.pitch)), cp = cosf(freecam_rad(p.pitch));
  fwd[0] = sy * cp;         fwd[1] = sp;  fwd[2] = cy * cp;
  right[0] = p.hand * cy;   right[1] = 0.f; right[2] = -p.hand * sy;
  up[0] = -sp * sy;         up[1] = cp;   up[2] = -sp * cy;
}

// Eye and orientation from a camera->world matrix. Roll is dropped: the engine's
// cameras have none that this port has seen, and the freecam flies level.
inline FreecamPose freecam_pose_from_inverse(const float inv[16]) {
  FreecamPose p;
  const float* r = inv; const float* u = inv + 4; const float* f = inv + 8;
  for (int k = 0; k < 3; ++k) p.eye[k] = inv[12 + k];
  const float cx = r[1] * u[2] - r[2] * u[1], cyv = r[2] * u[0] - r[0] * u[2], cz = r[0] * u[1] - r[1] * u[0];
  p.hand = (cx * f[0] + cyv * f[1] + cz * f[2]) < 0.f ? -1.f : 1.f;
  const float flen = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
  float fy = flen > 0.f ? f[1] / flen : 0.f;
  if (fy > 1.f) fy = 1.f; else if (fy < -1.f) fy = -1.f;
  p.pitch = freecam_deg(asinf(fy));
  if (p.pitch > FREECAM_PITCH_MAX) p.pitch = FREECAM_PITCH_MAX;
  if (p.pitch < -FREECAM_PITCH_MAX) p.pitch = -FREECAM_PITCH_MAX;
  // Straight up or down the forward row has no heading; the right row keeps one.
  if (f[0] * f[0] + f[2] * f[2] > 1e-6f * flen * flen) p.yaw = freecam_deg(atan2f(f[0], f[2]));
  else p.yaw = freecam_deg(atan2f(-r[2] * p.hand, r[0] * p.hand));
  return p;
}

// view = world->camera, inv = camera->world, each the other's inverse.
inline void freecam_matrices(const FreecamPose& p, float view[16], float inv[16]) {
  float ax[3][3];
  freecam_axes(p, ax[0], ax[1], ax[2]);
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) { inv[i * 4 + j] = ax[i][j]; view[j * 4 + i] = ax[i][j]; }
    inv[i * 4 + 3] = 0.f; view[i * 4 + 3] = 0.f;
    view[12 + i] = -(p.eye[0] * ax[i][0] + p.eye[1] * ax[i][1] + p.eye[2] * ax[i][2]);
  }
  for (int k = 0; k < 3; ++k) inv[12 + k] = p.eye[k];
  inv[15] = 1.f; view[15] = 1.f;
}

// Client-pixel mouse counts: right turns right (through hand), up looks up.
inline void freecam_look(FreecamPose* p, float dx, float dy, float degPerCount) {
  p->yaw += p->hand * dx * degPerCount;
  p->yaw = fmodf(p->yaw, 360.f);
  if (p->yaw > 180.f) p->yaw -= 360.f; else if (p->yaw <= -180.f) p->yaw += 360.f;
  p->pitch -= dy * degPerCount;
  if (p->pitch > FREECAM_PITCH_MAX) p->pitch = FREECAM_PITCH_MAX;
  if (p->pitch < -FREECAM_PITCH_MAX) p->pitch = -FREECAM_PITCH_MAX;
}

// Both held cancel out (x4 * x0.25), as the siblings' keys do.
inline float freecam_speed_mult(int keys) {
  float m = 1.f;
  if (keys & FCK_FAST) m *= FREECAM_FAST_MULT;
  if (keys & FCK_SLOW) m *= FREECAM_SLOW_MULT;
  return m;
}

// Forward follows the look (pitch included), strafe the right row, Up/Down world +Y.
// Opposing keys cancel. speed in world units per second, dt in seconds.
inline void freecam_move(FreecamPose* p, int keys, float speed, float dt) {
  float r[3], u[3], f[3];
  freecam_axes(*p, r, u, f);
  const float s = speed * freecam_speed_mult(keys) * dt;
  const float fw = (float)((keys & FCK_FWD) != 0) - (float)((keys & FCK_BACK) != 0);
  const float sd = (float)((keys & FCK_RIGHT) != 0) - (float)((keys & FCK_LEFT) != 0);
  const float vt = (float)((keys & FCK_UP) != 0) - (float)((keys & FCK_DOWN) != 0);
  for (int k = 0; k < 3; ++k) p->eye[k] += s * (fw * f[k] + sd * r[k]);
  p->eye[1] += s * vt;
}

// The movement keys, fixed at the old bind rows' defaults (panel clean-up 2026-10-02), indexed by FCK_ bit:
// W, S, A, D, Space, Ctrl, Shift, Alt.
const int FREECAM_MOVE_VK[8] = { 'W', 'S', 'A', 'D', 0x20, 0x11, 0x10, 0x12 };

// The camera's own keys may not take a movement key: bound there, one press would also fly the camera. The
// Crouch and Interact keys refuse them too, and so do the Photo key and the Game speed key (read
// before the camera's keys, input.cpp).
inline bool freecam_bind_refused(int row, int vk) {
  if (row != BIND_ROW_CAMERA && row != BIND_ROW_TELEPORT && row != BIND_ROW_CAMCOPY && row != BIND_ROW_CAMPASTE &&
      row != BIND_ROW_FP_CROUCH && row != BIND_ROW_INTERACT && row != BIND_ROW_PHOTO && row != BIND_ROW_GAMESPEED)
    return false;
  for (int i = 0; i < 8; ++i) if (FREECAM_MOVE_VK[i] == vk) return true;
  return false;
}

// Flight speed: a percentage of the old 8 m/s default, clamped to the row's range.
inline float freecam_speed_mps(int pct) {
  if (pct < FREECAM_SPEED_MIN_PCT) pct = FREECAM_SPEED_MIN_PCT;
  if (pct > FREECAM_SPEED_MAX_PCT) pct = FREECAM_SPEED_MAX_PCT;
  return (float)FREECAM_SPEED_BASE_M * (float)pct / 100.f;
}

// The crosshair at the screen centre and the marker on Duke, in C_PLAYER's cyan.
// The marker is the siblings' diamond labelled "You" (AA esp_layout.h, from Megaton).
const int FREECAM_CROSS_ARM = 7, FREECAM_CROSS_GAP = 3, FREECAM_CROSS_T = 2;
const int FREECAM_MARKER_HALF = 6;   // the diamond spans 2*this + 1 rows
const char* const FREECAM_MARKER_LABEL = "You";
inline int freecam_marker_row_halfwidth(int row) {
  const int d = row - FREECAM_MARKER_HALF;
  const int w = FREECAM_MARKER_HALF - (d < 0 ? -d : d);
  return w < 0 ? 0 : w;
}

// The Debug readout's one line: the pose in its own units (world units, degrees), as AA's readout draws
// Build's. Truncated to the buffer, never overrun.
inline void freecam_readout(const FreecamPose& p, char* out, int cap) {
  if (!out || cap <= 0) return;
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "cam x=%.0f y=%.0f z=%.0f yaw=%.1f pitch=%.1f",
              p.eye[0], p.eye[1], p.eye[2], p.yaw, p.pitch);
}
// AA's readout colour (its esp.cpp corner lines): light grey, outlined.
const unsigned FREECAM_READOUT_RGBA = 0xFFC8C8C8u;
const int FREECAM_READOUT_MARGIN = 8;

// --- The glue (freecam.cpp), main thread only --------------------------------
// view.cpp's push detour, for every client.dll push: true and the pair to push while the freecam is on. The first
// push after enabling seeds the pose from that push's own camera->world matrix `inv`.
bool freecam_substitute(const float* inv, float outView[16], float outInv[16]);
// Frame path, after input_frame: steps the pose from this frame's keys and mouse.
void freecam_frame(void);
// Frame path, after the ESP and before the panel: crosshair and Duke marker.
void freecam_draw(int W, int H);
// The flying eye and its forward (the crosshair), for the teleport ray: false unless on and seeded.
bool freecam_ray(float eye[3], float fwd[3]);
// True while "Pause the game" holds the world asleep (freecampause.h).
bool freecam_pause_holding();
