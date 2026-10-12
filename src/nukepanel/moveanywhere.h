#pragma once
// Move anywhere (docs/re/free-move.md sections 1b, 5): the game's own camera and controls, but the track stops holding
// Duke. Pure rules; moveanywhere.cpp owns the detours. Layer 1 (player_track_move_vector 0x10087fd0) keeps left/right;
// skipped: actor_constrain_move_to_track 0x1008e360; undone (x/z only): actor_snap_to_track 0x1008e310 (see the note).
#include <string.h>
#include <math.h>
#include "w2s.h"   // Vec3

inline bool mv_acts(int switchOn, int isPlayer, int fpActive) { return switchOn && isPlayer && !fpActive; }

// x and z from before the snap (the only two it writes), y from after.
inline Vec3 mv_snap_keep(Vec3 before, Vec3 after) {
  const Vec3 k = { before.x, after.y, before.z };
  return k;
}

// The switch's edges; MV_OFF re-picks the track by player_teleport on his own spot, as First person's exit.
enum { MV_NONE = 0, MV_ON, MV_OFF };
inline int mv_step(int want, int on) {
  if (want && !on) return MV_ON;
  if (!want && on) return MV_OFF;
  return MV_NONE;
}

// The exit's re-pick: player_teleport cancels the motion and forces state 3, so it waits until Duke is
// grounded in idle, step or running (PS 1-3, +0xC10). Another player (a level change) starts on a track, and in
// first person the mode's own exit re-picks.
enum { MV_REPICK_NONE = 0, MV_REPICK_WAIT, MV_REPICK_NOW, MV_REPICK_DROP };
inline int mv_repick(int pending, int samePlayer, int ps, int fpActive) {
  if (!pending) return MV_REPICK_NONE;
  if (!samePlayer || fpActive) return MV_REPICK_DROP;
  return ps >= 1 && ps <= 3 ? MV_REPICK_NOW : MV_REPICK_WAIT;
}

// actor_constrain_move_to_track's entry: sub esp,0x60; push ebx; push esi (5, the copied prologue); mov esi,ecx;
// mov al,[esi+0xb1c]; xor bl,bl; test al,al. No relative operand in it.
enum { MV_CONSTRAIN_FP_LEN = 17, MV_CONSTRAIN_PROLOGUE = 5 };
// actor_snap_to_track whole, through its RET: mov eax,ecx; mov ecx,[eax+0xb20] (8, the copied prologue); ...;
// push eax; call [edx+0x1d0]; ret. The jz targets are short and inside the function.
enum { MV_SNAP_FP_LEN = 32, MV_SNAP_PROLOGUE = 8 };
inline int mv_fp_is_constrain(const unsigned char* b) {
  static const unsigned char k[MV_CONSTRAIN_FP_LEN] = {
    0x83, 0xEC, 0x60, 0x53, 0x56, 0x8B, 0xF1, 0x8A, 0x86, 0x1C, 0x0B, 0x00, 0x00, 0x32, 0xDB, 0x84, 0xC0 };
  return b && memcmp(b, k, sizeof k) == 0 ? 1 : 0;
}
inline int mv_fp_is_snap(const unsigned char* b) {
  static const unsigned char k[MV_SNAP_FP_LEN] = {
    0x8B, 0xC1, 0x8B, 0x88, 0x20, 0x0B, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x13, 0x8A, 0x90, 0x1C, 0x0B, 0x00, 0x00,
    0x84, 0xD2, 0x74, 0x09, 0x8B, 0x11, 0x50, 0xFF, 0x92, 0xD0, 0x01, 0x00, 0x00, 0xC3 };
  return b && memcmp(b, k, sizeof k) == 0 ? 1 : 0;
}

// --- Depth on Up/Down (replace look-up and crouch while the switch is on) ----------------------------------------
// player_read_input_mask duke!0x1007d250 (vt+0x154) turns impulse 0x67 (up) into mask bit 8 and 0x68 (down) into 0x10
// via inp_impulse_value (prism!0x406a30, import slot [0x10183e94]); both read 0 to it while we act. +1 away, -1 toward.
inline int mv_depth_axis(int upHeld, int downHeld) { return (upHeld ? 1 : 0) - (downHeld ? 1 : 0); }

inline int mv_unit_flat(Vec3* v) {
  v->y = 0.f;
  const float l = sqrtf(v->x * v->x + v->z * v->z);
  if (!(l > 1e-4f)) return 0;
  v->x /= l; v->z /= l;
  return 1;
}
// Away from the camera, flat: its forward without the track's own direction (so Up is pure depth), or the
// flattened forward with no track. 0 when the camera looks straight down.
inline int mv_away(Vec3 camFwd, int haveTangent, Vec3 tangent, Vec3* away) {
  Vec3 a = camFwd;
  a.y = 0.f;
  Vec3 t = tangent;
  if (haveTangent && mv_unit_flat(&t)) {
    const float d = a.x * t.x + a.z * t.z;
    a.x -= d * t.x; a.z -= d * t.z;
  }
  if (!mv_unit_flat(&a)) return 0;
  *away = a;
  return 1;
}
// Whether player_track_move_vector walks along the track, as it decides (0x10087fd0): a left/right axis that does
// not cancel (mask 1 is -1, 2 is +1), no 0x200, +0xB1C set and a track (+0xB20).
inline int mv_lr_walks(unsigned mask, int trackOn, int hasTrack) {
  if ((mask & 0x200u) || !trackOn || !hasTrack) return 0;
  return ((mask & 1u) != 0) != ((mask & 2u) != 0);
}
// A key as the depth detour holds it: the mask reader returns before its key reads while the world takes no input
// (0x1007d281/0x1007d28a), so ours clear then rather than stay held.
inline int mv_key_held(int held, int worldLive) { return held && worldLive ? 1 : 0; }
// Up and Down go back to the game where they are the engine's controls. Ladder: state 5 (net message 10 from state
// 3's touch scan duke!0x10086120 -> 0x1007ede1) walks to the grab point, state 6 climbs (playerstate_climb_think
// 0x100842e0); hanging is state 8 (0x1007ea89). atLadder: head in an area_ladder volume (grab test 0x1009a460).
enum { MV_STATE_LADDER = 5, MV_STATE_CLIMB = 6, MV_STATE_HANG = 8 };
inline bool mv_keys_to_game(int state, int atLadder) {
  return state == MV_STATE_LADDER || state == MV_STATE_CLIMB || state == MV_STATE_HANG || atLadder != 0;
}

// Interact: the engine's "use" is Up itself (cmd_use duke!0x100e5da0 -> one tick of mask 8 in state 3,
// 0x1007d453-0x1007d46b; touch scan 0x10086120 takes doors, switches, ladders). The up read returns the game's value
// while it has the keys, 1.0 once after an Interact press, else 0 (our depth keeps the key).
inline float mv_up_read(int giveKeys, int usePending, float v) {
  if (giveKeys) return v;
  return usePending ? 1.f : 0.f;
}

// A queued use is dropped once Up/Down go back to the game: it would fire after the ladder.
inline int mv_use_pending(int pending, int give) { return give ? 0 : pending; }

// The camera's frame-to-frame measurement. A snap is the camera eye moving more than Duke did by this much.
const float MV_SNAP_UNITS = 30.f;
inline bool mv_cam_snap(float camMove, float dukeMove) { return camMove - dukeMove > MV_SNAP_UNITS; }
// a - b in degrees, wrapped to [-180, 180).
inline float mv_yaw_diff(float a, float b) {
  float d = fmodf(a - b, 360.f);
  if (d >= 180.f) d -= 360.f;
  if (d < -180.f) d += 360.f;
  return d;
}

// Duke left the line: a snap would have moved him more than this (world units). Otherwise the exit's return to
// the track is skipped.
const float MV_OFFLINE_EPS = 0.5f;
inline bool mv_left_line(float undoneMax) { return undoneMax > MV_OFFLINE_EPS; }

// player_track_move_vector duke!0x10087fd0's result with depth added. lrHeld: left/right reached it (mask 1/2, no
// 0x200, mv_lr_walks), so `in` is the walk along the track; otherwise `in` is zero or a yaw fallback and is replaced.
// Unit length, flat: callers multiply by walk speed, so a diagonal is as fast as a walk.
inline Vec3 mv_move_vector(Vec3 in, int lrHeld, int depth, Vec3 away) {
  if (!depth) return in;
  Vec3 base = { 0.f, 0.f, 0.f };
  if (lrHeld) {
    base = in;
    if (!mv_unit_flat(&base)) base.x = base.y = base.z = 0.f;   // no walk from the engine: depth alone moves him
  }
  Vec3 o = { base.x + away.x * (float)depth, 0.f, base.z + away.z * (float)depth };
  if (!mv_unit_flat(&o)) return in;
  return o;
}
// `push imp; call [slot]` in code[0..len): the offset of the call's return address, or -1.
inline int mv_find_impulse_call(const unsigned char* code, int len, int imp, unsigned slot) {
  for (int i = 0; i + 8 <= len; ++i) {
    if (code[i] != 0x6A || code[i + 1] != (unsigned char)imp || code[i + 2] != 0xFF || code[i + 3] != 0x15) continue;
    const unsigned s = (unsigned)code[i + 4] | ((unsigned)code[i + 5] << 8) | ((unsigned)code[i + 6] << 16) |
                       ((unsigned)code[i + 7] << 24);
    if (s == slot) return i + 8;
  }
  return -1;
}
// player_track_move_vector's entry: sub esp,0xc; mov edx,[esp+0x14] (7, the copied prologue); then the three FLDs of
// +0xE54/+0xE50/+0xE4C. thiscall (ECX the state, unused) with (out, player, bool, angles), RET 0x10, EAX = out.
enum { MV_MOVEVEC_FP_LEN = 27, MV_MOVEVEC_PROLOGUE = 7 };
// player_read_input_mask's entry: push ecx/ebx/esi/edi; mov edi,[esp+0x14]; xor ebx,ebx; cmp edi,ebx; mov esi,ecx.
// Its body is 0x25C bytes (0x1007d250-0x1007d4ac), the span the two impulse reads are looked for in.
enum { MV_READMASK_FP_LEN = 14, MV_READMASK_SPAN = 0x25C };
// inp_impulse_value: mov eax,[esp+4]; mov ecx,[esp+8] (8, the copied prologue); imul eax,eax,0x15; add eax,ecx;
// fld [eax*4+...]. cdecl (impulse, channel), the value in ST0, plain RET.
enum { MV_IMPVAL_FP_LEN = 16, MV_IMPVAL_PROLOGUE = 8 };
inline int mv_fp_is_movevec(const unsigned char* b) {
  static const unsigned char k[MV_MOVEVEC_FP_LEN] = {
    0x83, 0xEC, 0x0C, 0x8B, 0x54, 0x24, 0x14, 0xD9, 0x82, 0x54, 0x0E, 0x00, 0x00, 0x53, 0xD9, 0x82, 0x50, 0x0E, 0x00,
    0x00, 0x56, 0xD9, 0x82, 0x4C, 0x0E, 0x00, 0x00 };
  return b && memcmp(b, k, sizeof k) == 0 ? 1 : 0;
}
inline int mv_fp_is_readmask(const unsigned char* b) {
  static const unsigned char k[MV_READMASK_FP_LEN] = {
    0x51, 0x53, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x14, 0x33, 0xDB, 0x3B, 0xFB, 0x8B, 0xF1 };
  return b && memcmp(b, k, sizeof k) == 0 ? 1 : 0;
}
inline int mv_fp_is_impval(const unsigned char* b) {
  static const unsigned char k[MV_IMPVAL_FP_LEN] = {
    0x8B, 0x44, 0x24, 0x04, 0x8B, 0x4C, 0x24, 0x08, 0x6B, 0xC0, 0x15, 0x03, 0xC1, 0xD9, 0x04, 0x85 };
  return b && memcmp(b, k, sizeof k) == 0 ? 1 : 0;
}

// --- moveanywhere.cpp (DLL only; read, not run, by the suite) --------------------------------
// Frame path after firstperson_frame: the switch, arming on its first use, the exit's re-pick and the camera log.
void moveanywhere_frame(const unsigned char* player, int W, int H);
// A detour was refused (fingerprint or install): the row shows Unavailable.
int moveanywhere_refused(void);
