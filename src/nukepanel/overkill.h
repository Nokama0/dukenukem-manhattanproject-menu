#pragma once
// "Don't waste shots": an enemy whose death is already guaranteed by damage on its
// way is no target for silent aim's pick or the Triggerbot. The pure rules; overkill.cpp owns the send detour,
// the projectile walk and the log. Every address is from docs/re/aim.md section 10.
#include <math.h>
#include <string.h>
#include "w2s.h"   // Vec3

// Why an enemy is doomed, in the order the running sum is taken: damage already sent but not yet applied
// (the net message latency, aim.md section 4b), a direct hit in flight, a rocket's splash, the rifle's burst.
enum { OVK_ALIVE = 0, OVK_DEALT, OVK_INFLIGHT, OVK_SPLASH, OVK_BURST, OVK_CAUSES };
inline const char* ovk_cause_label(int c) {
  static const char* const k[OVK_CAUSES] = { "alive", "dealt", "inFlight", "splash", "burst" };
  return c >= 0 && c < OVK_CAUSES ? k[c] : "?";
}

struct OvkPending { float dealt, direct, splash, burst; };

// monster_apply_damage duke!0x1002f560 subtracts the amount unscaled (no armour, no difficulty factor; the
// receive 0x10031990 passes it straight through) and a monster dies at health <= 0: doomed when the damage on
// its way reaches the health. withBurst 0 is silent aim's pick: the burst's own rounds still need the lock.
inline int ovk_cause(float hp, const OvkPending& p, int withBurst) {
  if (!(hp > 0.f)) return OVK_ALIVE;   // already dead: ent_alive refuses it, no doom to report
  float sum = p.dealt;
  if (sum >= hp) return OVK_DEALT;
  if ((sum += p.direct) >= hp) return OVK_INFLIGHT;
  if ((sum += p.splash) >= hp) return OVK_SPLASH;
  if (withBurst && sum + p.burst >= hp) return OVK_BURST;
  return OVK_ALIVE;
}

// What monster_damage_send duke!0x1002f3f0 sent and the health has not yet lost: hpFirst is the health at the
// first send of this run, sent the sum of the run's amounts, hpNow the health now. Never negative.
inline float ovk_unapplied(float hpFirst, float sent, float hpNow) {
  const float applied = hpFirst > hpNow ? hpFirst - hpNow : 0.f;
  return sent > applied ? sent - applied : 0.f;
}
// The sends that will take health: monster_apply_damage ignores types 3 and 4 (the morph counter) and, with
// flag +0xB7C 0x200, types 6/7/8; it subtracts only an amount > 0.
inline int ovk_send_counts(int type, float amount, unsigned flags) {
  if (!(amount > 0.f) || type == 3 || type == 4) return 0;
  return (flags & 0x200) && (type == 6 || type == 7 || type == 8) ? 0 : 1;
}
// A run stays while some of it is unapplied and its last send is younger than the TTL (a message that never
// lands, for example its monster removed, must not doom it for good). The latency is ~3 frames (aim.md 4b).
const unsigned OVK_SEND_TTL_MS = 500;
inline int ovk_send_keep(float unapplied, unsigned ageMs) { return unapplied > 0.f && ageMs <= OVK_SEND_TTL_MS; }

// The splash distance FUN_1008bc30 measures from the explosion p to an actor of box centre c and half-extents
// h: 0 inside the box, else |d| minus the projection of the nearest corner on d (d = p - c), 0 when that
// projection passes p.
inline float ovk_splash_dist(Vec3 p, Vec3 c, Vec3 h) {
  const float d[3] = { p.x - c.x, p.y - c.y, p.z - c.z }, e[3] = { h.x, h.y, h.z };
  if (fabsf(d[0]) <= e[0] && fabsf(d[1]) <= e[1] && fabsf(d[2]) <= e[2]) return 0.f;
  float best = -1.f, k[3] = { 0.f, 0.f, 0.f };
  for (int i = 0; i < 8; ++i) {
    const float q[3] = { (i & 1) ? e[0] : -e[0], (i & 2) ? e[1] : -e[1], (i & 4) ? e[2] : -e[2] };
    const float s = (d[0] - q[0]) * (d[0] - q[0]) + (d[1] - q[1]) * (d[1] - q[1]) + (d[2] - q[2]) * (d[2] - q[2]);
    if (best < 0.f || s < best) { best = s; k[0] = q[0]; k[1] = q[1]; k[2] = q[2]; }
  }
  const float l2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
  const float t = (k[0] * d[0] + k[1] * d[1] + k[2] * d[2]) / l2;
  return t > 1.f ? 0.f : (1.f - t) * sqrtf(l2);
}
// The splash FUN_10039080 (proj vt+0x21C) deals: (r - d) / r * damage inside the radius, nothing outside.
inline float ovk_splash_damage(float radius, float dist, float dmg) {
  return radius > 0.f && dist < radius ? (radius - dist) / radius * dmg : 0.f;
}
// The rifle's committed rounds (+0xC2C) at its per-round damage (+0xCE4).
inline float ovk_burst_damage(int roundsLeft, float perRound) { return roundsLeft > 0 ? (float)roundsLeft * perRound : 0.f; }
// proj_plasma_collide duke!0x1004e260: a full charge (+0xBD4 >= 1) not yet passed through a monster gives the
// first monster at most 100 and keeps the rest; otherwise the victim takes the whole +0xBC0.
inline float ovk_plasma_direct(float dmg, float charge, int pierced, int monster) {
  return monster && !pierced && charge >= 1.f && dmg > 100.f ? 100.f : dmg;
}
// The plasma's damage +0xBC0 at charge c: (proj_damage +0xC44 - proj_min_damage +0xC48) * c + min, set on the
// ball each charging frame (weap_pcannon_fire_logic 0x100588a0; doubled under the power-up the spawns test).
inline float ovk_plasma_charge_damage(float maxDmg, float minDmg, float charge) { return (maxDmg - minDmg) * charge + minDmg; }
// The rocket's block matcher +0x220 (its init duke!0x1004f300): accept all, deny the deny_superset pairs
// below; its think 0x1004f480 also flies on through a dying monster (user 0x20 with +0xB94 >= 0x14).
inline int ovk_proj_stops(unsigned type, unsigned user, int dyingMonster) {
  static const unsigned deny[7][2] = { { 2, 0x100 }, { 2, 0x1000 }, { 2, 0x400 }, { 2, 0x800 }, { 0x100, 0x10000 },
                                       { 0x100, 0x20000 }, { 2, 0x20000000 } };
  for (int i = 0; i < 7; ++i)
    if ((type & deny[i][0]) == deny[i][0] && (user & deny[i][1]) == deny[i][1]) return 0;
  return (type & 2) && (user & 0x20) && dyingMonster ? 0 : 1;
}

// The per-frame pending table: at most OVK_MAX enemies, the rest counted in full.
enum { OVK_MAX = 64 };
struct OvkTable { const void* ent[OVK_MAX]; OvkPending p[OVK_MAX]; int n; unsigned full; };
inline void ovk_reset(OvkTable* t) { memset(t, 0, sizeof *t); }
inline void ovk_add(OvkTable* t, const void* ent, int cause, float amount) {
  int i = 0;
  while (i < t->n && t->ent[i] != ent) ++i;
  if (i == t->n) {
    if (t->n >= OVK_MAX) { t->full++; return; }
    t->ent[i] = ent;
    memset(&t->p[i], 0, sizeof t->p[i]);
    t->n++;
  }
  OvkPending& p = t->p[i];
  if (cause == OVK_DEALT) p.dealt += amount;
  else if (cause == OVK_INFLIGHT) p.direct += amount;
  else if (cause == OVK_SPLASH) p.splash += amount;
  else if (cause == OVK_BURST) p.burst += amount;
}
inline OvkPending ovk_get(const OvkTable& t, const void* ent) {
  for (int i = 0; i < t.n; ++i) if (t.ent[i] == ent) return t.p[i];
  OvkPending z = { 0.f, 0.f, 0.f, 0.f };
  return z;
}

// The send ledger, one run per monster, written by the detour inside the tick and read once a frame.
struct OvkSend { const void* ent; float hpFirst, sent; unsigned tLast; };
struct OvkLedger { OvkSend s[OVK_MAX]; int n; unsigned full; };
inline void ovk_ledger_note(OvkLedger* l, const void* ent, float hpNow, float amount, unsigned now) {
  int i = 0;
  while (i < l->n && l->s[i].ent != ent) ++i;
  if (i == l->n) {
    if (l->n >= OVK_MAX) { l->full++; return; }
    l->s[i].ent = ent; l->s[i].hpFirst = hpNow; l->s[i].sent = 0.f;
    l->n++;
  }
  l->s[i].sent += amount;
  l->s[i].tLast = now;
}
inline void ovk_ledger_drop(OvkLedger* l, int i) {
  if (i < 0 || i >= l->n) return;
  l->s[i] = l->s[l->n - 1];
  l->n--;
}

// Duke's live projectiles by class (dumps/duke_base.classes.txt).
enum { OVK_P_NONE = 0, OVK_P_BULLET, OVK_P_ROCKET, OVK_P_PLASMA, OVK_P_PBOMB, OVK_P_KINDS };
inline int ovk_proj_kind(const char* cls) {
  if (!cls) return OVK_P_NONE;
  if (!strcmp(cls, "actor_proj_bullet")) return OVK_P_BULLET;
  if (!strcmp(cls, "actor_proj_prpg")) return OVK_P_ROCKET;
  if (!strcmp(cls, "actor_proj_plasma")) return OVK_P_PLASMA;
  if (!strcmp(cls, "actor_proj_pbomb")) return OVK_P_PBOMB;
  return OVK_P_NONE;
}

// monster_damage_send duke!0x1002f3f0 through its `jz` (relative, so position-free). The head (sub esp,8;
// push esi; mov esi,ecx) is the trampoline's 6 bytes; once detoured it reads E9 rel32 90.
enum { OVK_SEND_FP_LEN = 25, OVK_SEND_TRAMP_LEN = 6 };
static const unsigned char kOvkSendFp[OVK_SEND_FP_LEN] = {
  0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1, 0x8B, 0x86, 0xE0, 0x00, 0x00, 0x00, 0xF6, 0x80, 0xC8, 0x00, 0x00, 0x00, 0x08,
  0x0F, 0x84, 0x48, 0x01, 0x00, 0x00 };
// What a monster's vt+0x198 holds: monster_damage_send itself (stock, or already under our detour), or another
// entry. Some classes reach it through a wrapper (monst_fly 0x10027870, the ratoids and roachoids 0x100290b0: a
// few instructions, then JMP 0x1002f3f0): those are skipped, and the detour is armed from another monster.
enum { OVK_SLOT_OTHER = 0, OVK_SLOT_STOCK, OVK_SLOT_DETOURED };
inline int ovk_send_slot_kind(const unsigned char* b) {
  if (!b || memcmp(b + OVK_SEND_TRAMP_LEN, kOvkSendFp + OVK_SEND_TRAMP_LEN, OVK_SEND_FP_LEN - OVK_SEND_TRAMP_LEN))
    return OVK_SLOT_OTHER;
  if (!memcmp(b, kOvkSendFp, OVK_SEND_TRAMP_LEN)) return OVK_SLOT_STOCK;
  return b[0] == 0xE9 && b[5] == 0x90 ? OVK_SLOT_DETOURED : OVK_SLOT_OTHER;
}
// The rifle fires its committed rounds only while attacking (state +0xB10 == 6, weap_arifle_fire_logic
// 0x10052800); out of ammo it zeroes +0xC2C itself (0x100528f4). Rounds it can still fire, capped at 64.
inline int ovk_burst_left(int state, int committed) {
  if (state != 6 || committed <= 0) return 0;
  return committed > 64 ? 64 : committed;
}
// A press the Triggerbot would have made at a doomed target, counted as shots: every one of a semi-automatic's
// alternate presses, and one per hold of an automatic. wouldFire: the fire decision before the doom test.
inline int ovk_withheld_step(int wouldFire, int doomed, int semi, int* prev) {
  const int w = wouldFire && doomed;
  const int c = w && !*prev;
  *prev = semi ? c : w;
  return c;
}
inline int ovk_fp_monster_send(const unsigned char* b) {
  if (!b) return 0;
  const int head = !memcmp(b, kOvkSendFp, OVK_SEND_TRAMP_LEN) || (b[0] == 0xE9 && b[5] == 0x90);
  return head && !memcmp(b + OVK_SEND_TRAMP_LEN, kOvkSendFp + OVK_SEND_TRAMP_LEN,
                         OVK_SEND_FP_LEN - OVK_SEND_TRAMP_LEN) ? 1 : 0;
}

// --- overkill.cpp (DLL only; read, not run, by the suite) ---------------------------------------
// Once a frame from aim_frame, after its frame context: arms the send detour, steps the ledger, walks Duke's
// projectiles and rebuilds the table. haveCtx 0: no ray this frame, so only the ledger and the burst count.
void overkill_frame(const unsigned char* player, int haveCtx);
// This frame's verdict on ent: OVK_ALIVE or the cause that dooms it.
int overkill_cause(const void* ent, int withBurst);
// The Triggerbot pressed the rifle at ent: its burst's rounds are ent's pending damage.
void overkill_burst_target(const void* ent);
// The target whose burst still has rounds to fire this frame (silent aim keeps it locked), or NULL.
const void* overkill_burst_ent(void);
// A Triggerbot frame that would have held fire at a doomed target.
void overkill_withheld(int cause);
// Silent aim's pick skipped a doomed candidate.
void overkill_pick_skip(int cause);
