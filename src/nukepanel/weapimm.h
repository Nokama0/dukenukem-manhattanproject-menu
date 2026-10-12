#pragma once
// Enemies the held weapon cannot hurt (e.g. the SecurityMech). The pure rules: how each weapon's
// damage enters a monster, and what each monster class's own handlers refuse, in the engine's order (vt+0x194,
// vt+0x198, vt+0x2FC). aim.cpp reads the facts; the table and its citations are docs/re/aim.md section 11.
#include <string.h>
#include "w2s.h"       // Vec3
#include "weapons.h"   // WK_*

// How a weapon's damage reaches a monster. via194: through vt+0x194, damage info byte +0x20 = `splash`: the bullet
// (proj_bullet_damage_hit duke!0x1003ad90, type 0), the rocket's and pipebomb's splash (0x10039080, type 9, splash 1).
// Otherwise vt+0x198: plasma (0x1004e260, type 10), X-3000 (0x1005c8f0, type 0); the Ray has its own gate (glopp_gate).
struct WimmSpec { int known; int via194; int splash; int type; };
inline WimmSpec wimm_spec(int kind) {
  WimmSpec s = { 0, 0, 0, -1 };
  switch (kind) {
    case WK_GEAGLE: case WK_SHOTGUN: case WK_ARIFLE: s.known = 1; s.via194 = 1; s.type = 0; break;
    case WK_PRPG: case WK_PBOMB:                     s.known = 1; s.via194 = 1; s.splash = 1; s.type = 9; break;
    case WK_PCANNON:                                 s.known = 1; s.type = 10; break;
    case WK_X3000:                                   s.known = 1; s.type = 0; break;
    default: break;
  }
  return s;
}

// A monster class by its handlers. SMECH: vt+0x194 is smech_damage_from_info duke!0x1002a9b0 (the three
// monst_securitymech classes). SHELL: vt+0x198 is the roachoid3/B wrapper 0x100290b0. MONSTER: vt+0x194 is
// actor_damage_from_info 0x1008b630 and nothing below refuses by class. UNREAD: anything else stays a candidate.
enum { WIMM_CLASS_UNREAD = 0, WIMM_CLASS_MONSTER, WIMM_CLASS_SMECH, WIMM_CLASS_SHELL };
inline int wimm_class(int dfiStock, int dfiSmech, int sendShell) {
  if (dfiSmech) return WIMM_CLASS_SMECH;
  if (!dfiStock) return WIMM_CLASS_UNREAD;
  return sendShell ? WIMM_CLASS_SHELL : WIMM_CLASS_MONSTER;
}

// The roachoid wrapper zeroes the amount while +0xC68 is this state (0x100290b0: cmp [ecx+0xc68],0xb).
const int WIMM_SHELL_STATE = 0xB;
// monster_apply_damage: with flag +0xB7C 0x200 its first test returns for types 6, 7 and 8.
const unsigned WIMM_FLAG_IGNORE_678 = 0x200;

struct WimmFacts {
  int cls;          // WIMM_CLASS_*
  int read;         // flags, state and scale were read
  unsigned flags;   // +0xB7C
  int state;        // +0xC68
  float scale;      // +0xC08, the def's `scale` (desc reader 0x1002c170, at 0x1002c58a)
  float k;          // the SecurityMech handler's constant, read through its own operand (10.0 stock)
  int hit;          // hitLocalY holds the hit point in the monster's own frame
  float hitLocalY;  // the hit point through the inverse world matrix +0x134, y
};
// Why the held weapon cannot hurt this enemy now, WIMM_OK when it can. Only a read refusal excludes
// (wimm_refuses): UNREAD_CLASS, UNREAD and NO_HIT (a SecurityMech bullet with no hit point) stay candidates.
enum { WIMM_OK = 0, WIMM_UNREAD_CLASS, WIMM_UNREAD, WIMM_NO_HIT, WIMM_IMM_LOW_HIT, WIMM_IMM_SHELL, WIMM_IMM_TYPE,
       WIMM_COUNT };
inline int wimm_refuses(int r) { return r >= WIMM_IMM_LOW_HIT && r < WIMM_COUNT; }
inline const char* wimm_label(int r) {
  static const char* const k[WIMM_COUNT] = { "ok", "unreadClass", "unread", "noHit", "lowHit", "shell", "type" };
  return r >= 0 && r < WIMM_COUNT ? k[r] : "?";
}
inline int wimm_gate(const WimmSpec& s, const WimmFacts& f) {
  if (!s.known) return WIMM_OK;
  if (f.cls == WIMM_CLASS_UNREAD) return WIMM_UNREAD_CLASS;
  if (!f.read) return WIMM_UNREAD;
  // vt+0x194: smech_damage_from_info sends a splash info on, and a bullet's only when the hit point's local y is
  // above scale * k (`fcomp; test ah,5; jnp send`, 0x1002a9e1..0x1002a9f6: a NaN does not send). Else it only
  // alerts the mech. The plasma and the X-3000 skip vt+0x194.
  if (s.via194 && !s.splash && f.cls == WIMM_CLASS_SMECH) {
    if (!f.hit) return WIMM_NO_HIT;
    if (!(f.scale * f.k < f.hitLocalY)) return WIMM_IMM_LOW_HIT;
  }
  // vt+0x198: every weapon's damage, via vt+0x194 or not, passes it.
  if (f.cls == WIMM_CLASS_SHELL && f.state == WIMM_SHELL_STATE) return WIMM_IMM_SHELL;
  // vt+0x2FC monster_apply_damage. No weapon of Duke's deals 6/7/8; kept for the engine's order.
  if ((f.flags & WIMM_FLAG_IGNORE_678) && (s.type == 6 || s.type == 7 || s.type == 8)) return WIMM_IMM_TYPE;
  return WIMM_OK;
}

// p through the row-vector matrix m (translation m[12..14]), y only: p3shared's mult_point_matrix (0x10016ec0,
// selected at 0x10009c9e) computes out[j] = sum_i p[i] * m[4i + j] + m[12 + j].
inline float wimm_local_y(Vec3 p, const float* m) { return p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13]; }

// smech_damage_from_info duke!0x1002a9b0 through its `jnp send` (0x1002a9f6): the +0x134 matrix, the info's
// hit point +0xC, its splash byte +0x20, scale +0xC08 and local y at [esp+0x24]. Bytes 25..28 (the
// mult_point_matrix import) and 57..60 (the constant's address) are absolute, so relocated: not compared.
enum { WIMM_SMECH_FP_LEN = 76, WIMM_SMECH_K_AT = 57 };
static const unsigned char kWimmSmechFp[WIMM_SMECH_FP_LEN] = {
  0x83, 0xEC, 0x24, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x38, 0x6A, 0x0C, 0x8B, 0xF1, 0x6A, 0x0C, 0x6A, 0x01, 0x8D, 0x86,
  0x34, 0x01, 0x00, 0x00, 0x50, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x8D, 0x57, 0x0C, 0x8D, 0x4C, 0x24, 0x30, 0xFF, 0x10,
  0x8A, 0x47, 0x20, 0x84, 0xC0, 0x0F, 0x85, 0x2F, 0x01, 0x00, 0x00, 0xD9, 0x86, 0x08, 0x0C, 0x00, 0x00, 0xD8, 0x0D,
  0x00, 0x00, 0x00, 0x00, 0xD8, 0x5C, 0x24, 0x24, 0xDF, 0xE0, 0xF6, 0xC4, 0x05, 0x0F, 0x8B, 0x14, 0x01, 0x00, 0x00 };
// 1 with *kAddr = the address of the float the scale is multiplied by.
inline int wimm_fp_smech(const unsigned char* b, unsigned* kAddr) {
  if (!b) return 0;
  for (int i = 0; i < WIMM_SMECH_FP_LEN; ++i) {
    if ((i >= 25 && i < 29) || (i >= WIMM_SMECH_K_AT && i < WIMM_SMECH_K_AT + 4)) continue;
    if (b[i] != kWimmSmechFp[i]) return 0;
  }
  const unsigned char* a = b + WIMM_SMECH_K_AT;
  *kAddr = (unsigned)a[0] | ((unsigned)a[1] << 8) | ((unsigned)a[2] << 16) | ((unsigned)a[3] << 24);
  return 1;
}
// The roachoid3/B vt+0x198 duke!0x100290b0: `cmp [ecx+0xc68],0xb; jnz; mov [esp+8],0` (amount := 0), the
// pass-through of the amount, then `jmp monster_damage_send`. 1 with *target = the jump's destination.
enum { WIMM_SHELL_FP_LEN = 26, WIMM_SHELL_LEN = 30 };
static const unsigned char kWimmShellFp[WIMM_SHELL_FP_LEN] = {
  0x83, 0xB9, 0x68, 0x0C, 0x00, 0x00, 0x0B, 0x75, 0x08, 0xC7, 0x44, 0x24, 0x08, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x54,
  0x24, 0x08, 0x89, 0x54, 0x24, 0x08, 0xE9 };
inline int wimm_fp_shell(const unsigned char* b, unsigned at, unsigned* target) {
  if (!b || memcmp(b, kWimmShellFp, WIMM_SHELL_FP_LEN)) return 0;
  const unsigned rel = (unsigned)b[26] | ((unsigned)b[27] << 8) | ((unsigned)b[28] << 16) | ((unsigned)b[29] << 24);
  *target = at + WIMM_SHELL_LEN + rel;
  return 1;
}
