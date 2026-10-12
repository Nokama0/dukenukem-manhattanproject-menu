#pragma once
// Damage numbers: every rule, and nothing else. AA's dmgnum.h (duke3d-eduke32-aa
// src/ekmod) with this port's victim (an entity pointer) and anchor (a box centre).
// dmgnum.cpp owns the hook, the engine reads and the draw.
#include <math.h>
#include "w2s.h"       // Vec3
#include "canvas.h"    // C_HAZARD, C_RED
#include "classes.h"   // CAT_ENEMY, CAT_BOSS

// 32 events, oldest overwritten: the draw cost stays fixed whatever the fire rate.
enum { DMG_MAX = 32 };
// Ages are unsigned `now - t0` differences, exact across the millisecond clock's wrap.
const unsigned DMG_LIFE_MS = 750;

// `at` is where the victim's box centre was when it was hit; `victim` only groups a
// stack and is never dereferenced, since a kill may free the entity.
// `kill`/`live` are int, not bool: AA lost a bool member of this shape to MSVC /O2.
struct DmgEvent { Vec3 at; int damage; int kill; const void* victim; unsigned t0; int live; };
struct DmgRing  { DmgEvent e[DMG_MAX]; int next; };

inline void dmg_reset(DmgRing* r) {
  if (!r) return;
  for (int i = 0; i < DMG_MAX; ++i) {
    DmgEvent& e = r->e[i];
    e.at.x = e.at.y = e.at.z = 0.f;
    e.damage = 0; e.kill = 0; e.victim = 0; e.t0 = 0u; e.live = 0;
  }
  r->next = 0;
}

// A burst past DMG_MAX inside one lifetime keeps the newest numbers, the ones worth seeing.
inline void dmg_push(DmgRing* r, const DmgEvent& ev) {
  if (!r) return;
  r->e[r->next] = ev;
  r->e[r->next].live = 1;
  r->next = (r->next + 1) % DMG_MAX;
}

inline int dmg_is_live(const DmgEvent& ev, unsigned now) {
  if (!ev.live) return 0;
  return (unsigned)(now - ev.t0) < DMG_LIFE_MS ? 1 : 0;
}

inline double dmg_progress(unsigned t0, unsigned now) {
  const unsigned age = now - t0;
  if (age >= DMG_LIFE_MS) return 1.0;
  return (double)age / (double)DMG_LIFE_MS;
}

// Ease-out 1-(1-p)^2 over one text line: knocked loose, then settling.
inline int dmg_rise_px(double p, int textH) {
  if (p < 0.0) p = 0.0;
  if (p > 1.0) p = 1.0;
  const double q = 1.0 - (1.0 - p) * (1.0 - p);
  return (int)floor(q * (double)textH + 0.5);
}

// Opaque for two thirds, then linear to nothing, so the number leaves without a pop.
inline int dmg_alpha(double p) {
  if (p <= 2.0 / 3.0) return 255;
  if (p >= 1.0) return 0;
  return (int)floor(255.0 * (1.0 - p) * 3.0 + 0.5);
}

inline unsigned dmg_colour(int kill) { return kill ? C_RED : C_HAZARD; }

// Live events on the same victim pushed before `idx`, walking from the oldest slot:
// times one text height, it turns a burst into a column instead of one smudge.
inline int dmg_stack_index(const DmgRing& r, int idx, unsigned now) {
  if (idx < 0 || idx >= DMG_MAX) return 0;
  if (!dmg_is_live(r.e[idx], now)) return 0;
  int n = 0;
  for (int k = 0; k < DMG_MAX; ++k) {
    const int s = (r.next + k) % DMG_MAX;
    if (s == idx) break;
    if (dmg_is_live(r.e[s], now) && r.e[s].victim == r.e[idx].victim) n++;
  }
  return n;
}

// Why a hit made no number. Every early return in the detour counts exactly one of
// these, so entered == kept + the rejections (dmgnum.cpp).
enum { DMG_KEPT = 0, DMG_REJ_OFF, DMG_REJ_ATTACKER, DMG_REJ_UNREAD, DMG_REJ_CLASS, DMG_REJ_DEAD,
       DMG_REJ_NODAMAGE, DMG_REASON_COUNT };

// User bit 0x20 at +0x4C marks a monster-base object, the only kind whose +0xB84 is
// health: the gate the ESP already uses (entities.cpp kUserMonster).
const unsigned DMG_USER_MONSTER = 0x20u;

// Decided before the original runs: whose hit, on what, and was it alive.
inline int dmg_pre_reason(int mine, int cat, unsigned user, float before) {
  if (!mine) return DMG_REJ_ATTACKER;
  if (cat != CAT_ENEMY && cat != CAT_BOSS) return DMG_REJ_CLASS;
  if (!(user & DMG_USER_MONSTER) || !(before > 0.f)) return DMG_REJ_DEAD;
  return DMG_KEPT;
}

// Decided after it: type 3/4 hits and invulnerable targets change no health.
inline int dmg_post_reason(float before, float after) {
  return (before - after) > 0.f ? DMG_KEPT : DMG_REJ_NODAMAGE;
}

inline int dmg_keep_reason(int mine, int cat, unsigned user, float before, float after) {
  const int pre = dmg_pre_reason(mine, cat, user, before);
  return pre != DMG_KEPT ? pre : dmg_post_reason(before, after);
}

// Rounded, never below 1: a kept hit always shows, and overkill shows in full.
inline int dmg_display(float damage) {
  const int n = (int)floor((double)damage + 0.5);
  return n < 1 ? 1 : n;
}

inline int dmg_is_kill(float before, float after) { return before > 0.f && after <= 0.f ? 1 : 0; }

// monster_apply_damage's SEH prologue (duke_base 0x1002f560). Bytes 3..6 are the
// handler's absolute address, relocated with the image (p3shared.dll takes duke_base's
// preferred base), so they are matched as "inside duke_base's image" instead.
enum { DMG_FP_LEN = 24, DMG_TRAMP_LEN = 7 };   // the trampoline copies push -1; push imm32
static const unsigned char kDmgFp[DMG_FP_LEN] = {
  0x6A, 0xFF, 0x68, 0, 0, 0, 0, 0x64, 0xA1, 0, 0, 0, 0, 0x50,
  0x64, 0x89, 0x25, 0, 0, 0, 0, 0x83, 0xEC, 0x28 };

inline int dmg_fp_match(const unsigned char* b, unsigned imageBase, unsigned imageSize) {
  if (!b) return 0;
  for (int i = 0; i < DMG_FP_LEN; ++i) {
    if (i >= 3 && i <= 6) continue;
    if (b[i] != kDmgFp[i]) return 0;
  }
  const unsigned h = (unsigned)b[3] | ((unsigned)b[4] << 8) | ((unsigned)b[5] << 16) | ((unsigned)b[6] << 24);
  return h >= imageBase && h - imageBase < imageSize ? 1 : 0;
}

// --- dmgnum.cpp (DLL only; read, not run, by the suite) ---------------------

// Main thread, from the entity walk: an enemy or boss's vtable. Arms the hook from
// its vt+0x2FC target once; logs each class whose target differs from the armed one.
void dmgnum_arm(const void* const* vt, const char* className);
// Main thread, frame path, after the ESP and before the panel (probe.cpp).
void dmgnum_draw(int W, int H);

int dmgnum_armed(void);
// Cumulative since load: entered == kept + the sum of rejected(1..DMG_REASON_COUNT-1).
unsigned dmgnum_entered(void);
unsigned dmgnum_kept(void);
unsigned dmgnum_rejected(int reason);
unsigned dmgnum_drawn(void);
