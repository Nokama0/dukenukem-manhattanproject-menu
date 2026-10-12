#pragma once
// Torso bend: the pure rules. torso.cpp detours player_spine_bend
// duke!0x1007c7f0 and replays its body with this angle (mouse-aim.md sections 1a and 1d).
#include <math.h>
#include "w2s.h"   // Vec3

// Degrees, + up. The engine only ever bends 0..22 up; the range below is unproven (mouse-aim.md).
const float TB_BEND_MIN = -35.f, TB_BEND_MAX = 35.f;
// Degrees a second. The original rises at 230 (duke!0x10136d80); it subtracts 100 (0x10126a78) when its target
// is 0, but its clamp to [0, target] then snaps the bend straight to the target, so it never eases down. The
// down ease at 100 is ours, using the same constant.
const float TB_RATE_UP = 230.f, TB_RATE_DOWN = 100.f;

// The elevation from the chest to the aim point, clamped; *clamped 1 when the clamp moved it.
inline float tb_bend_target(Vec3 chest, Vec3 aim, int* clamped) {
  const float dx = aim.x - chest.x, dy = aim.y - chest.y, dz = aim.z - chest.z;
  const float h = sqrtf(dx * dx + dz * dz);
  float a = (h > 0.f || dy != 0.f) ? atan2f(dy, h) * 57.29577951f : 0.f;
  *clamped = 0;
  if (a < TB_BEND_MIN) { a = TB_BEND_MIN; *clamped = 1; }
  if (a > TB_BEND_MAX) { a = TB_BEND_MAX; *clamped = 1; }
  return a;
}
// One tick toward the target at the stock rates, never past it.
inline float tb_bend_step(float cur, float target, float dt) {
  if (!(dt > 0.f)) return cur;
  if (cur < target) { const float v = cur + TB_RATE_UP * dt; return v > target ? target : v; }
  if (cur > target) { const float v = cur - TB_RATE_DOWN * dt; return v < target ? target : v; }
  return cur;
}

// Who drives a tick's bend: ours with an angle; with none, ours toward 0 while a bend we set is left (so the
// stock snap never pops it); else the engine.
enum { TB_STOCK = 0, TB_OURS, TB_EASE_OUT };
inline int tb_owner(int haveAngle, int owned, float bend) {
  if (haveAngle) return TB_OURS;
  return owned && bend != 0.f ? TB_EASE_OUT : TB_STOCK;
}

// player_spine_bend duke!0x1007c7f0 through `cmp eax,3; jnz` at +0x58: the dt read (world+0x38), the
// +0xC10/+0xDF0/+0xB08/+0x111C/+0xC1C target tests; the three absolute float constants are masked. The first 0x1C
// bytes hold no relocation and the hook compares them; 6 (sub esp,0x10; push esi; mov esi,ecx) move to the trampoline.
enum { TB_FP_LEN = 0x5D, TB_HOOK_FP_LEN = 0x1C, TB_TRAMP_LEN = 6 };
static const unsigned char kTbSpineFp[TB_FP_LEN] = {
  0x83, 0xEC, 0x10, 0x56, 0x8B, 0xF1, 0x8B, 0x86, 0xE0, 0x00, 0x00, 0x00, 0xDD, 0x40, 0x38, 0x8B, 0x86, 0x10, 0x0C,
  0x00, 0x00, 0x83, 0xF8, 0x03, 0xD9, 0x5C, 0x24, 0x04, 0xD9, 0x05, 0x00, 0x00, 0x00, 0x00, 0xBA, 0x02, 0x00, 0x00,
  0x00, 0x75, 0x46, 0x8B, 0x8E, 0xF0, 0x0D, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x25, 0x84, 0x91, 0x08, 0x0B, 0x00, 0x00,
  0x74, 0x1D, 0x8B, 0x8E, 0x1C, 0x11, 0x00, 0x00, 0xDD, 0xD8, 0x66, 0x83, 0x79, 0x02, 0x06, 0x74, 0x08, 0xD9, 0x05,
  0x00, 0x00, 0x00, 0x00, 0xEB, 0x06, 0xD9, 0x05, 0x00, 0x00, 0x00, 0x00, 0x83, 0xF8, 0x03, 0x75, 0x12 };
static const unsigned char kTbSpineMask[TB_FP_LEN] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1 };

// --- torso.cpp (DLL only; read, not run, by the suite) ---------------------------------------
// Frame path after mouseaim_frame: arms the detour on first use and sets the next ticks' target angle.
void torso_frame(const unsigned char* player);
// Top of every frame-path call, with mouseaim_clear: the ticks never bend toward a stale angle.
void torso_clear(void);
int torso_refused(void);
