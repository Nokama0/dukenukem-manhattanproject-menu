#pragma once
// Enemy awareness while Duke is off the track (evidence docs/re/ai-offtrack.md, levers L1-L3). The pure rules;
// aiaware.cpp owns the walk and the detours.
// Active only while First person or Move anywhere is on: off, nothing is written and both detours pass through.
#include <math.h>
#include <stddef.h>
#include "w2s.h"        // Vec3
#include "offscreen.h"  // offs_find: the fingerprint scan rule

const float AI_WAKE_RADIUS = 1500.f;   // user decision 2026-10-02
// Rays per frame, nearest first. A monster seen within AI_REFRESH_S (by the draw or by us) needs no ray: the
// engine clears +0xB71 only after 10 s unseen (actor_draw_mark_seen duke!0x1008d9e0).
enum { AI_RAYS_PER_FRAME = 8, AI_CAND_MAX = 64 };
const float AI_REFRESH_S = 1.f;
const unsigned AI_RETRY_MS = 250;     // a monster whose ray was blocked waits this long, so it cannot hog the cap
const unsigned AI_FLAG_OFF = 0x200;   // +0xB7C: dead/off (ai-offtrack.md section 1)

inline bool ai_gate(int fpOn, int mvOn, int havePlayer) { return havePlayer && (fpOn || mvOn); }

// L1 candidate. isEnemy: CAT_ENEMY only. Bosses are excluded: their wake paths were not read (ai-offtrack.md).
// sinceSeen = world time - +0xB74; negative (the clock went back, a new level) counts as stale.
inline bool ai_wake_eligible(int isEnemy, int hashp, float hp, float dist2, unsigned flags, int seen, float sinceSeen,
                             int retryDue) {
  if (!isEnemy || !hashp || !(hp > 0.f) || !(dist2 <= AI_WAKE_RADIUS * AI_WAKE_RADIUS)) return false;
  if (flags & AI_FLAG_OFF) return false;
  if (seen && sinceSeen >= 0.f && sinceSeen < AI_REFRESH_S) return false;
  return retryDue != 0;
}
inline bool ai_retry_due(unsigned long long now, unsigned long long next) { return now >= next; }

struct AiCand { const void* ent; float dist2; };
// Keeps c sorted nearest first; full, the farthest goes (or c, when it is the farthest).
inline void ai_cand_insert(AiCand* a, int* n, int max, AiCand c) {
  int i = *n;
  if (i >= max) {
    if (!(c.dist2 < a[max - 1].dist2)) return;
    i = max - 1;
  } else {
    ++*n;
  }
  while (i > 0 && a[i - 1].dist2 > c.dist2) { a[i] = a[i - 1]; --i; }
  a[i] = c;
}
inline int ai_rays_this_frame(int n, int cap) { return n < cap ? n : cap; }

// L2: monster_can_see_target duke!0x100314b0 runs first; only a stock "no" for Duke with a mode on is retested
// without the facing cone.
enum { AI_SEE_STOCK = 0, AI_SEE_PASS, AI_SEE_CONELESS };
inline int ai_see_route(int stock, int active, int targetIsPlayer) {
  if (stock) return AI_SEE_STOCK;
  if (!active || !targetIsPlayer) return AI_SEE_PASS;
  return AI_SEE_CONELESS;
}
// The original's tests before the cone (0x100314dd-0x100315e2): its seen byte, distance^2 <= 1e6, and not a
// hidden player (type 2, user bit 1, +0xA64 == 0).
inline bool ai_target_hidden(unsigned type, unsigned user, unsigned a64) { return (type & 2) && (user & 1) && !a64; }
// Squared distance between the two vt+0x160 world positions (0x100314eb-0x10031523).
inline float ai_dist2(Vec3 a, Vec3 b) {
  const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
  return dx * dx + dy * dy + dz * dz;
}
inline bool ai_see_pre(int seen, float dist2, int hidden) { return seen && dist2 <= 1e6f && !hidden; }
// Its three ray heights (0x10031650, 0x100317ee, 0x10031854): ratio = target vt+0x1F0, halfH = target +0x2BC.
inline void ai_see_heights(float y, float ratio, float halfH, float out[3]) {
  const float s = ratio * halfH;
  const float y0 = ratio < 0.75f ? y - s : y;
  out[0] = y0; out[1] = y0 - s; out[2] = y0 + s;
}
// A ray counts when its first object is the target, or a type-2 object with user bit 0x8000 (0x100317c2).
inline bool ai_see_hit_ok(int hitIsTarget, unsigned type, unsigned user) {
  return hitIsTarget || ((type & 2) && (user & 0x8000));
}

// L3: a monster bullet's velocity +0x1C0 is turned at Duke's chest, speed kept. Every bullet's first think runs
// proj_bullet_hitscan_ray (duke!0x1003a9c0), copying +0x1C0 into its ray (+0xBFC) while +0xBEC is set, so turning it
// turns shot and tracer together. firstThink: +0xBEC; shooterEnemy: ignore slot 0 (+0xB34, +0xB3C) is a CAT_ENEMY.
enum { AI_BUL_PASS = 0, AI_BUL_OTHER, AI_BUL_KEPT, AI_BUL_REAIM };
inline int ai_bullet_route(int firstThink, int slotOn, int shooterEnemy, int active) {
  if (!firstThink) return AI_BUL_PASS;
  if (!slotOn || !shooterEnemy) return AI_BUL_OTHER;
  return active ? AI_BUL_REAIM : AI_BUL_KEPT;
}
inline bool ai_reaim(Vec3 from, Vec3 to, Vec3 vel, Vec3* out) {
  const float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
  const float l = sqrtf(dx * dx + dy * dy + dz * dz);
  const float sp = sqrtf(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
  if (!(l > 1e-3f) || !(sp > 1e-3f)) return false;
  out->x = dx / l * sp; out->y = dy / l * sp; out->z = dz / l * sp;
  return true;
}
// Duke's chest: his hitbox (hitbox_from: centre y = pos.y - o, half y = half.y - |o|) raised by half its half-height.
inline Vec3 ai_chest(Vec3 pos, Vec3 half, float o) {
  float hy = half.y - fabsf(o);
  if (hy < 0.f) hy = 0.f;
  const Vec3 c = { pos.x, pos.y - o + 0.5f * hy, pos.z };
  return c;
}

// monster_can_see_target duke!0x100314b0: SEH prologue (the handler push 0x1011e6e9 relocates, masked), then
// `mov ebp,[esp+0x84]; test ebp,ebp; ...; mov al,[esi+0xb71]`. Unique (Ghidra search, 2026-10-02). The copied
// prologue is `mov eax,fs:[0]` (6 bytes, no relative operand).
enum { AI_SEE_FP_LEN = 53, AI_SEE_PROLOGUE = 6, AI_SEE_HOOK_FP = 8 };
static const unsigned char kAiSeeFp[AI_SEE_FP_LEN] = {
  0x64, 0xA1, 0, 0, 0, 0, 0x6A, 0xFF, 0x68, 0, 0, 0, 0, 0x50, 0x64, 0x89, 0x25, 0, 0, 0, 0,
  0x83, 0xEC, 0x6C, 0x53, 0x55, 0x8B, 0xAC, 0x24, 0x84, 0, 0, 0, 0x85, 0xED, 0x56, 0x57, 0x8B, 0xF1, 0x0F, 0x84,
  0, 0, 0, 0, 0x8A, 0x86, 0x71, 0x0B, 0, 0, 0x84, 0xC0 };
static const unsigned char kAiSeeMask[AI_SEE_FP_LEN] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 };
// proj_bullet_hitscan_ray duke!0x1003a9c0 (thiscall, no arguments, plain RET; only caller proj_bullet_think), through
// `mov byte [esi+0xbec],0; call rel32`; the jz and call rel32 are masked. Unique (Ghidra byte search, 2026-10-02).
// Copied prologue: sub esp,0x18; push ebx; push ebp; push esi; mov esi,ecx (8).
enum { AI_HIT_FP_LEN = 35, AI_HIT_PROLOGUE = 8 };
static const unsigned char kAiHitFp[AI_HIT_FP_LEN] = {
  0x83, 0xEC, 0x18, 0x53, 0x55, 0x56, 0x8B, 0xF1, 0x8A, 0x86, 0xEC, 0x0B, 0x00, 0x00, 0x84, 0xC0, 0x57, 0x0F, 0x84,
  0, 0, 0, 0, 0xC6, 0x86, 0xEC, 0x0B, 0x00, 0x00, 0x00, 0xE8, 0, 0, 0, 0 };
static const unsigned char kAiHitMask[AI_HIT_FP_LEN] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 };

// Arming retries an unreadable duke_base image once a second, not every frame.
const unsigned long long AI_ARM_RETRY_MS = 1000;
inline bool ai_arm_due(unsigned long long now, unsigned long long retryAt) { return now >= retryAt; }
inline unsigned long long ai_arm_retry_at(unsigned long long now) { return now + AI_ARM_RETRY_MS; }

// --- aiaware.cpp (DLL only; read, not run, by the suite) ------------------------------------
// Frame path, main thread, after entities_frame and both modes' frames: the gate, L1's rays and writes, the lazy
// arming of the two detours, and the 5 s counter line.
void aiaware_frame(const unsigned char* player);
