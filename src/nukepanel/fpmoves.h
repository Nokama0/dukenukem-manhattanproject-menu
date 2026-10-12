#pragma once
// First person extras (free-move.md section 7): Crouch key, Interact key and the air jump.
// Pure rules; fpmoves.cpp owns the detour and the engine calls.
#include <string.h>
#include <math.h>

// --- Crouch: the 16/17 pre-tick (duke!0x1007fba0) stands Duke every tick after its think (push 1.0; mov ecx,esi; call
// [edx+0x1EC] = actor_set_height_ratio 0x1008fc90, thiscall(ratio), RET 4). The walk state crouches with 0.5 through
// the same call (playerstate_move_update 0x100859c6); its sweep (0x1008fe7a) refuses to grow into a ceiling.
const float FP_CROUCH_RATIO = 0.5f;
// The argument the stand call passes: the crouch ratio for our player from that call site while the key is held in
// first person, else the engine's own.
inline float fp_crouch_arg(int isPlayer, int fromStandCall, int fpActive, int held, float engineArg) {
  return isPlayer && fromStandCall && fpActive && held ? FP_CROUCH_RATIO : engineArg;
}
// How far our eye (pos + 40) comes down for height ratio r: the eye's height above the feet (pos.y - half.y)
// scales with r.
inline float fp_crouch_eye_drop(float r, float halfY) {
  if (r > 1.f) r = 1.f;
  if (r < 0.f) r = 0.f;
  return (1.f - r) * (40.f + halfY);
}
// The view's drop eases toward the engine's ratio (about 150 ms). The ratio itself stays instant,
// as the walk state's own crouch is (one 0.5 call, 0x100859d7); only our eye moves gradually. Exponential, time
// constant FP_CROUCH_TAU_MS: 96 % of the way after 150 ms.
const float FP_CROUCH_TAU_MS = 45.f;
inline float fp_ease_drop(float cur, float target, float dtMs) {
  if (!(dtMs > 0.f)) return cur;
  if (dtMs >= 20.f * FP_CROUCH_TAU_MS) return target;
  return target + (cur - target) * expf(-dtMs / FP_CROUCH_TAU_MS);
}
// The stand call in the pre-tick's code: the offset of its return address, or -1.
enum { FP_STAND_CALL_LEN = 13, FP_PRETICK_SCAN = 0x40 };
inline int fp_find_stand_call(const unsigned char* code, int len) {
  static const unsigned char k[FP_STAND_CALL_LEN] = { 0x68, 0x00, 0x00, 0x80, 0x3F, 0x8B, 0xCE, 0xFF, 0x92, 0xEC,
                                                      0x01, 0x00, 0x00 };
  for (int i = 0; i + FP_STAND_CALL_LEN <= len; ++i)
    if (memcmp(code + i, k, sizeof k) == 0) return i + FP_STAND_CALL_LEN;
  return -1;
}
// actor_set_height_ratio's entry: fld [esp+4]; sub esp,0x30 (7, the copied prologue); fcomp [abs].
enum { FP_RATIO_FP_LEN = 9, FP_RATIO_PROLOGUE = 7 };
inline int fp_fp_is_ratio(const unsigned char* b) {
  static const unsigned char k[FP_RATIO_FP_LEN] = { 0xD9, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x30, 0xD8, 0x1D };
  return b && memcmp(b, k, sizeof k) == 0 ? 1 : 0;
}

// --- Interact: the engine's use is state 3's touch scan, playerstate_move_touch_scan duke!0x10086120 (vt+0x48,
// thiscall(state, player), RET 4; ECX never read). On an Up edge (E00 & 8, not E04 & 8, no 0x200) it sends the net
// messages: use (11 -> state 7), ladder (10 -> 5), grab (5 -> 8, 6/7 -> 11). State 17 never runs it: we do, masks set.
inline void fp_use_masks(unsigned e00, unsigned e04, unsigned* n00, unsigned* n04) {
  *n00 = (e00 | 0x8u) & ~(0x10u | 0x200u);
  *n04 = e04 & ~0x8u;
}
// player_tick's g_fps switch (duke!0x1007b10d, return 0x1007b112) puts state 17 back the same tick; after an
// Interact press it is skipped while Duke is in the state the use started (ladder 5/6, use 7, hang 8/9, bars 10,
// 11), for at most FP_USE_HOLD_MS. Back in state 3, the switch returns him to 17 itself.
const unsigned FP_USE_HOLD_MS = 10000;
inline bool fp_hold_scripted(int fromSwitch, int newState, int curState, unsigned long long sinceUseMs) {
  if (!fromSwitch || newState != 17 || sinceUseMs > FP_USE_HOLD_MS) return false;
  return curState >= 5 && curState <= 11;   // ladder 5/6, use 7, hang 8, pull-up 9, bars 10, 11
}
// The switch's call in player_tick: cmp eax,0x11; jz; cmp eax,0x10; jnz; push 3; mov ecx,esi; call player_set_state.
// The offset of its return address, or -1; *target: the call's target as an offset from code.
enum { FP_SWITCH_LEN = 19 };
inline int fp_find_switch_call(const unsigned char* code, int len, int* target) {
  static const unsigned char k[FP_SWITCH_LEN - 4] = { 0x83, 0xF8, 0x11, 0x74, 0x05, 0x83, 0xF8, 0x10, 0x75, 0x09,
                                                      0x6A, 0x03, 0x8B, 0xCE, 0xE8 };
  for (int i = 0; i + FP_SWITCH_LEN <= len; ++i) {
    if (memcmp(code + i, k, sizeof k) != 0) continue;
    int rel;
    memcpy(&rel, code + i + FP_SWITCH_LEN - 4, 4);
    *target = i + FP_SWITCH_LEN + rel;
    return i + FP_SWITCH_LEN;
  }
  return -1;
}
// player_set_state's entry: push ebx; mov ebx,[esp+8] (5, the copied prologue); push esi; mov esi,ecx; push edi;
// mov edi,[esi+0xb7c]. The touch scan's: sub esp,0xc4; push ebx/ebp/esi; mov esi,[esp+0xd4].
enum { FP_SETSTATE_FP_LEN = 15, FP_SETSTATE_PROLOGUE = 5, FP_SCAN_FP_LEN = 16 };
inline int fp_fp_is_setstate(const unsigned char* b) {
  static const unsigned char k[FP_SETSTATE_FP_LEN] = { 0x53, 0x8B, 0x5C, 0x24, 0x08, 0x56, 0x8B, 0xF1, 0x57, 0x8B,
                                                       0xBE, 0x7C, 0x0B, 0x00, 0x00 };
  return b && memcmp(b, k, sizeof k) == 0 ? 1 : 0;
}
inline int fp_fp_is_scan(const unsigned char* b) {
  static const unsigned char k[FP_SCAN_FP_LEN] = { 0x81, 0xEC, 0xC4, 0x00, 0x00, 0x00, 0x53, 0x55, 0x56, 0x8B, 0xB4,
                                                   0x24, 0xD4, 0x00, 0x00, 0x00 };
  return b && memcmp(b, k, sizeof k) == 0 ? 1 : 0;
}

// A key's press edge, tracked every frame whether or not the mode is active, so a key held through a scripted
// state does not press again when first person returns.
inline bool fp_key_edge(int down, int* last) {
  const bool e = down && !*last;
  *last = down;
  return e;
}

// --- Air jump: state 17 jumps only from the ground (playerstate_fps_think 0x1007fc20); the boost lives in the air
// branch of playerstate_move_update (0x10085b2a-0x10085d32, double-jump.md section 1), mirrored here. A press within
// FPJ_TAKEOFF_S of the ground jump's stamp (E10) is that jump's own press, not an air press.
enum { FPJ_NONE = 0, FPJ_BOOST, FPJ_GATE1, FPJ_GATE2, FPJ_GATE3, FPJ_TAKEOFF };
const double FPJ_TAKEOFF_S = 0.05;
// now: world+0x40; E10 the ground jump's time, E14 the last air press (armed while E14 < E10), E70 the jump's start y.
inline int fpj_press(double now, float e10, float e14, float y, float e70) {
  if (now - (double)e10 <= FPJ_TAKEOFF_S) return FPJ_TAKEOFF;
  if (!(now - (double)e10 > 0.2)) return FPJ_GATE1;   // 0.2f at 0x10126cf8
  if (!(e14 < e10)) return FPJ_GATE2;
  if (!(y >= e70 - 50.f)) return FPJ_GATE3;           // 50.0f at 0x10126a74
  return FPJ_BOOST;
}
// The sweet path (0x10085c58): |vy| within g_p_boost_sweet, or E64 > 0 while falling.
inline bool fpj_sweet(float vy, float e64, float sweet) {
  return sweet >= fabsf(vy) || (e64 > 0.f && vy < 0.f);
}
// The upward speed after the boost: g_p_boost, or on the sweet path the speed that still reaches the apex of a
// g_p_jump jump plus a g_p_boost boost: sqrt(|J^2 + B^2 - 2 g dy|), dy = y - E70 (0x10085c58-0x10085cf9).
inline float fpj_velocity(float vy, float e64, float jump, float boost, float sweet, float g, float dy) {
  if (!fpj_sweet(vy, e64, sweet)) return boost;
  return sqrtf(fabsf(jump * jump + boost * boost - 2.f * g * dy));
}
// Gravity from two airborne frames, away from the 611 u/s terminal speed (measured: 1800 u/s^2).
const float FPJ_G_DEFAULT = 1800.f;
inline bool fpj_measure_g(float vyPrev, float vy, double dt, float* g) {
  if (!(dt > 0.0 && dt <= 0.1) || fabsf(vyPrev) > 590.f || fabsf(vy) > 590.f) return false;
  const float m = (float)((vyPrev - vy) / dt);
  if (!(m >= 200.f && m <= 10000.f)) return false;
  *g = m;
  return true;
}
// Double jump strength: first person cannot grab the ledges a vanilla short jump reaches, so the
// upward speed we give is scaled, 100-150 %.
enum { FPJ_PCT_MIN = 100, FPJ_PCT_MAX = 150, FPJ_PCT_DEF = 120 };
inline float fpj_scaled(float v, int pct) {
  if (pct < FPJ_PCT_MIN) pct = FPJ_PCT_MIN;
  if (pct > FPJ_PCT_MAX) pct = FPJ_PCT_MAX;
  return v * (float)pct / 100.f;
}
// The player's vt+0x200 (0x10078a50, thiscall(const vec3*), RET 4), the velocity add the boost calls twice.
enum { FPJ_ADDVEL_FP_LEN = 10 };
inline int fp_fp_is_addvel(const unsigned char* b) {
  static const unsigned char k[FPJ_ADDVEL_FP_LEN] = { 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x57, 0x8B, 0xF1, 0xE8 };
  return b && memcmp(b, k, sizeof k) == 0 ? 1 : 0;
}

// --- Body: player_draw duke!0x1007d4d0 (player vt+0xB4, thiscall(arg), RET 4) returns at once in state 17
// (0x1007d4e3); our view hold through a ladder or use state skips it the same way. Entry: sub esp,0xe8 (6, the copied
// prologue); push esi; mov esi,ecx; mov eax,[esi+0xeb4]; push edi.
enum { FP_DRAW_FP_LEN = 16, FP_DRAW_PROLOGUE = 6 };
inline int fp_fp_is_draw(const unsigned char* b) {
  static const unsigned char k[FP_DRAW_FP_LEN] = { 0x81, 0xEC, 0xE8, 0x00, 0x00, 0x00, 0x56, 0x8B, 0xF1, 0x8B, 0x86,
                                                   0xB4, 0x0E, 0x00, 0x00, 0x57 };
  return b && memcmp(b, k, sizeof k) == 0 ? 1 : 0;
}

// --- Pipebomb throw: weap_pbomb_launch duke!0x100578f0 throws along the fire dir (vt+0x210) at 580+rand%40 when the
// weapon's +0xC3C is 1, else lobs 45 degrees up at 330+rand%40. +0xC3C is +0xB84 copied at the throw's start
// (weap_pbomb_fire_logic 0x100574e0); weap_read_owner_pose 0x10051584 sets +0xB84 from the player's +0xC1C == 2.
inline bool fp_pbomb_want(int fpActive, int heldPbomb, int crouchHeld) { return fpActive && heldPbomb && !crouchHeld; }
// +0xC1C is the upper-body state the walk state writes on Up (FUN_10077620(2) at 0x10085a70) and player_tick copies
// from +0xC18 (0x1007c3e3). State 17 never writes it, so every throw lobbed. First person writes +0xC18 = 2 while the
// bomb is held, so the throw follows the view (fire dir = view centre), unless Crouch is held: the engine's own lob.
enum { FP_UP_NONE = 0, FP_UP_WRITE, FP_UP_RESTORE };
const int FP_UPPER_THROW = 2;
// ours: our 2 is in place; back to the value it replaced, unless the engine wrote its own since.
inline int fp_pbomb_upper(int want, int ours, int cur) {
  if (want) return cur != FP_UPPER_THROW ? FP_UP_WRITE : FP_UP_NONE;
  return ours && cur == FP_UPPER_THROW ? FP_UP_RESTORE : FP_UP_NONE;
}

// --- fpmoves.cpp (DLL only; read, not run, by the suite) --------------------------------
// Frame path after firstperson_frame: arms the detours, reads the keys.
void fpmoves_frame(const unsigned char* player);
// Our eye's drop for the substituted view (0 when standing).
float fpmoves_eye_drop(void);
// One of our keys down for the game: bound and live, this process in front, no panel, console or free camera.
bool fpmoves_key_down(int vk);
// Duke's body is not drawn while hide holds (first person's view hold); the detour is armed on the first hide.
void fpmoves_hide_body(const unsigned char* player, int hide);
