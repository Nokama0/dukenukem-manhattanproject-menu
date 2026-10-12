// Enemy awareness while Duke is off the track (aiaware.h owns the rules). L1 runs on the frame path; the L2 and
// L3 detours run in the game tick. Main thread only (the engine is single-threaded, render.md section 1).
#include "aiaware.h"
#include "engine.h"
#include "entities.h"
#include "hook.h"
#include "config.h"
#include "log.h"
#include <windows.h>
#include <string.h>

static const unsigned kOfsWorld = 0xE0;       // object -> world (aim.cpp)
static const unsigned kOfsWorldTime = 0x40;   // world+0x40, a double: the clock actor_draw_mark_seen stamps (0x1008da22)
static const unsigned kOfsPhys = 0x4B8;       // a NULL physics slot makes every ray answer "clear" (aim.cpp)
// Monster fields (ai-offtrack.md section 1): seen byte, time seen, flags, AI state, eye height. monster_can_see_target
// reads +0xC00 on every monster vtable, so the span reaches it.
static const unsigned kSeen = 0xB71, kSeenAt = 0xB74, kFlags = 0xB7C, kState = 0xB94, kEye = 0xC00, kMonSpan = 0xC04;
// Target fields the original reads: type +0x48, user +0x4C, +0xA64, half-height +0x2BC.
static const unsigned kTgtSpan = 0xA68;

struct Matcher { __declspec(align(8)) unsigned char b[0x40]; };   // as aim.cpp: the ctor writes 0x24 bytes

static int s_active;                  // ai_gate this frame; both detours read it
static const void* s_player;          // Duke while active, else NULL: compared, never dereferenced, by the detours
static Vec3 s_chest;
static int s_haveChest;
static const void* s_lastPlayer;

// Blocked monsters wait AI_RETRY_MS (aiaware.h) so they cannot hold the per-frame cap.
struct Retry { const void* ent; ULONGLONG next; };
enum { kRetryMax = 64, kAsleepMax = 16 };
static Retry s_retry[kRetryMax];
static int s_nretry;
// A2's kill check: a monster we woke in state 1 (asleep) must leave it within 2 s.
struct Asleep { const void* ent; ULONGLONG at; };
static Asleep s_asleep[kAsleepMax];
static int s_nasleep;

// Per 5 s window (L1): frames walked, candidates, rays, blocked, woke (+0xB71 was 0), refreshed (was 1), refused
// writes, unread spans, and the time. rays == blocked + woke + refreshed + unwritable.
static unsigned s_frames, s_cands, s_rays, s_blocked, s_woke, s_refreshed, s_unwritable, s_unread, s_noWorld;
static double s_us;
static LARGE_INTEGER s_qpf;
// Cumulative: A2 outcomes; L2 calls == stock + pass + coneTrue + coneFalse; L3 calls (bullets' first thinks) ==
// other + kept + reaimed + unread.
static unsigned s_leftSleep, s_stillAsleep, s_asleepLost, s_asleepFull;
static unsigned s_seeCalls, s_seeStock, s_seePass, s_seeConeTrue, s_seeConeFalse;
static unsigned s_attCalls, s_attOther, s_attKept, s_attReaimed, s_attUnread;
static unsigned s_loggedSum = 0xFFFFFFFFu;
static ULONGLONG s_nextLog;
static int s_reaimLogged, s_coneLogged;

typedef bool (__fastcall* SeeFn)(void* self, void* edx, void* target);   // thiscall, RET 4, AL
typedef unsigned (__fastcall* HitFn)(void* self, void* edx);            // thiscall, no arguments, plain RET, EAX
typedef float (__thiscall* RatioFn)(const void* self);                   // vt+0x1F0, float in ST0 (0x100315ed)
// vt+0x160 get_world_pos: thiscall, no arguments, EAX = const vec3* (import thunk 0x100eba7c to p3shared's
// object_e::get_world_pos; call sites 0x100314ed on the monster, 0x100314fd on +0xBD4).
typedef const float* (__thiscall* PosFn)(const void* self);
static SeeFn s_seeTramp;
static HitFn s_hitTramp;
static int s_armDone;
static ULONGLONG s_armRetryAt;   // duke_base unreadable: the next scan
static unsigned s_armRetries;

static Vec3 rdv(const unsigned char* p) { Vec3 v; memcpy(&v, p, sizeof v); return v; }
static unsigned rdu(const unsigned char* p) { unsigned u; memcpy(&u, p, sizeof u); return u; }
static float rdf(const unsigned char* p) { float f; memcpy(&f, p, sizeof f); return f; }

// --- L2: monster_can_see_target without its facing cone ---------------------------------------
// The original's own matcher (0x1003153a-0x100315ba): everything but the monster itself, type-2 objects with
// user 0x1000/0x400/0x40, and world_e (0x100) with user 0x10000/0x20000.
static void see_matcher(Matcher* m, const void* self) {
  g_p3.tm_ctor(m->b);
  g_p3.tm_reset_rules(m->b, true);
  g_p3.tm_deny_ptr(m->b, self);
  g_p3.tm_deny_superset(m->b, 2, 0x1000, false);
  g_p3.tm_deny_superset(m->b, 2, 0x400, false);
  g_p3.tm_deny_superset(m->b, 2, 0x40, false);
  g_p3.tm_deny_superset(m->b, 0x100, 0x10000, false);
  g_p3.tm_deny_superset(m->b, 0x100, 0x20000, false);
}

// The original's range, hidden-player and three-ray tests, the cone dropped (aiaware.h, ai_see_*).
// A vtable slot that is a duke_base function (the walk's own vtable test).
static bool duke_slot(void* const* vt, unsigned off) {
  return entities_in_duke(vt) && entities_in_duke(vt + off / 4) && entities_in_duke(vt[off / 4]);
}
static bool world_pos(const unsigned char* e, Vec3* out) {
  void* const* vt = *(void* const* const*)e;
  if (!duke_slot(vt, 0x160)) return false;
  const float* p = ((PosFn)vt[0x160 / 4])(e);
  if (!p || !guarded_readable(p, 12)) return false;
  memcpy(out, p, sizeof *out);
  return true;
}

static bool see_coneless(const unsigned char* m, const unsigned char* t) {
  // The original has just read this byte (0x100314dd): the cheap test goes before any guarded read.
  if (!m[kSeen]) return false;
  if (!guarded_readable(m, kMonSpan) || !guarded_readable(t, kTgtSpan)) return false;
  Vec3 mw, tw;   // the range test's positions, as the original takes them
  if (!world_pos(m, &mw) || !world_pos(t, &tw)) return false;
  if (!ai_see_pre(m[kSeen] != 0, ai_dist2(mw, tw), ai_target_hidden(rdu(t + 0x48), rdu(t + 0x4C), rdu(t + 0xA64))))
    return false;
  // The rays use +0x18C, as the original's (0x100315fa-0x10031645).
  const Vec3 mp = rdv(m + 0x18C), tp = rdv(t + 0x18C);
  void* const* vt = *(void* const* const*)t;
  if (!duke_slot(vt, 0x1F0)) return false;
  const float ratio = ((RatioFn)vt[0x1F0 / 4])(t);
  float hs[3];
  ai_see_heights(tp.y, ratio, rdf(t + 0x2BC), hs);
  void* world;
  memcpy(&world, m + kOfsWorld, sizeof world);
  if (!world) return false;
  const Vec3 from = { mp.x, mp.y + rdf(m + kEye), mp.z };
  Matcher mt;
  see_matcher(&mt, m);
  bool ok = false;
  for (int i = 0; i < 3 && !ok; ++i) {
    const Vec3 to = { tp.x, hs[i], tp.z };
    const unsigned char* rec = (const unsigned char*)g_eng.phys_ray_test(world, &from.x, &to.x, mt.b, mt.b);
    if (!rec || !guarded_readable(rec, 4)) continue;
    const unsigned char* obj;
    memcpy(&obj, rec, sizeof obj);
    if (!obj) continue;
    if (obj == t) { ok = true; break; }
    if (guarded_readable(obj, 0x50)) ok = ai_see_hit_ok(0, rdu(obj + 0x48), rdu(obj + 0x4C));
  }
  g_p3.tm_dtor(mt.b);
  return ok;
}

static bool __fastcall see_detour(void* self, void* edx, void* target) {
  const bool stock = s_seeTramp(self, edx, target);
  s_seeCalls++;
  const int route = ai_see_route(stock ? 1 : 0, s_active, target && target == s_player);
  if (route == AI_SEE_STOCK) { s_seeStock++; return stock; }
  if (route == AI_SEE_PASS) { s_seePass++; return stock; }
  const bool r = see_coneless((const unsigned char*)self, (const unsigned char*)target);
  if (r) s_seeConeTrue++; else s_seeConeFalse++;
  if (r && !s_coneLogged) {
    s_coneLogged = 1;
    np_log("aiaware: first sight outside the cone: monster %p sees Duke %p", self, target);
  }
  return r;
}

// --- L3: monster bullets aimed at Duke's chest -----------------------------------------------
// Bullet fields (proj_add_ignore 0x10039000, proj_bullet_hitscan_ray 0x1003a9c0): ignore slot 0's on byte and entity,
// the first-think byte; +0x18C position and +0x1C0 velocity, written by the monster's fire before this think.
static const unsigned kBulSlotOn = 0xB34, kBulSlotEnt = 0xB3C, kBulFirst = 0xBEC, kBulSpan = 0xBF0;

static int shooter_is_enemy(const void* e) {
  const EntSnapshot& snap = entities_snapshot();   // last frame's walk: a monster that fires was in it
  for (int i = 0; i < snap.count; ++i)
    if (snap.ents[i].ent == e) return snap.ents[i].cat == CAT_ENEMY;
  return 0;
}

static unsigned __fastcall hit_detour(void* self, void* edx) {
  unsigned char* b = (unsigned char*)self;
  if (b && b[kBulFirst]) {   // before the original: proj_bullet_think calls it with this live bullet in ECX
    s_attCalls++;
    const void* shooter;
    memcpy(&shooter, b + kBulSlotEnt, sizeof shooter);
    const int on = b[kBulSlotOn] != 0;
    const int route = ai_bullet_route(1, on, on && shooter && shooter_is_enemy(shooter), s_active && s_haveChest);
    if (route == AI_BUL_OTHER) s_attOther++;
    else if (route == AI_BUL_KEPT) s_attKept++;
    else if (!guarded_writable(b + 0x18C, kBulSpan - 0x18C)) s_attUnread++;
    else {
      const Vec3 from = rdv(b + 0x18C), vel = rdv(b + 0x1C0);
      Vec3 out;
      if (!ai_reaim(from, s_chest, vel, &out)) s_attKept++;
      else {
        memcpy(b + 0x1C0, &out, sizeof out);   // the original copies it into the ray next
        s_attReaimed++;
        if (!s_reaimLogged) {
          s_reaimLogged = 1;
          np_log("aiaware: first monster bullet re-aimed: bullet %p from %p at (%.1f,%.1f,%.1f), velocity "
                 "(%.1f,%.1f,%.1f) -> (%.1f,%.1f,%.1f)", self, shooter, from.x, from.y, from.z, vel.x, vel.y, vel.z,
                 out.x, out.y, out.z);
        }
      }
    }
  }
  return s_hitTramp(self, edx);
}

// Lazy, the first frame the gate is on: off, nothing is patched. Each detour is refused alone on a mismatch.
static void arm(void) {
  if (s_armDone || !ai_arm_due(GetTickCount64(), s_armRetryAt)) return;
  if (!engine_resolve_p3shared() || !g_p3.tm_deny_ptr || !g_eng.phys_ray_test) {
    s_armDone = 1;
    np_log("aiaware: refused: p3shared matcher or phys_ray_test unresolved; L2/L3 not armed");
    return;
  }
  const unsigned char* see = 0;
  const unsigned char* hit = 0;
  const int ns = offs_scan_duke(kAiSeeFp, kAiSeeMask, AI_SEE_FP_LEN, &see);
  const int nh = offs_scan_duke(kAiHitFp, kAiHitMask, AI_HIT_FP_LEN, &hit);
  if (ns < 0 || nh < 0) {   // duke_base not readable yet: scan again in a second
    s_armRetryAt = ai_arm_retry_at(GetTickCount64());
    if (s_armRetries++ == 0) np_log("aiaware: duke_base image not readable yet; arming retries once a second");
    return;
  }
  s_armDone = 1;
  const unsigned char* base = 0;
  unsigned size = 0;
  entities_duke_image(&base, &size);
  if (ns != 1) {
    np_log("aiaware: L2 refused: monster_can_see_target matched %d time(s) (need 1)", ns);
  } else {
    unsigned char live[AI_SEE_HOOK_FP];
    memcpy(live, see, sizeof live);   // the relocation-free head the scan matched
    if (hook_install_at((void*)see, live, AI_SEE_HOOK_FP, AI_SEE_PROLOGUE, (void*)see_detour, (void**)&s_seeTramp))
      np_log("aiaware: L2 armed at %p (duke_base+%X)", (const void*)see, (unsigned)(see - base));
    else
      np_log("aiaware: L2 install at %p refused", (const void*)see);
  }
  if (nh != 1) {
    np_log("aiaware: L3 refused: proj_bullet_hitscan_ray matched %d time(s) (need 1)", nh);
    return;
  }
  unsigned char live[AI_HIT_FP_LEN];
  memcpy(live, hit, sizeof live);
  if (hook_install_at((void*)hit, live, AI_HIT_FP_LEN, AI_HIT_PROLOGUE, (void*)hit_detour, (void**)&s_hitTramp))
    np_log("aiaware: L3 armed at %p (duke_base+%X): every bullet's first think, re-aimed when an enemy fired it",
           (const void*)hit, (unsigned)(hit - base));
  else
    np_log("aiaware: L3 install at %p refused", (const void*)hit);
}

// --- L1: wake by the draw's own pair ------------------------------------------------------------
static const unsigned kDenyUser[4] = { 0x8000, 0x10000, 0x20000, 0x400 };   // aim.cpp's world-only matcher
static void world_matcher(Matcher* m) {
  g_p3.tm_ctor(m->b);
  g_p3.tm_reset_rules(m->b, false);
  g_p3.tm_accept_superset(m->b, 0x100, 0, true);
  for (int i = 0; i < 4; ++i) g_p3.tm_deny_superset(m->b, 0x100, kDenyUser[i], false);
}

static Retry* retry_find(const void* e) {
  for (int i = 0; i < s_nretry; ++i) if (s_retry[i].ent == e) return &s_retry[i];
  return 0;
}
static void retry_set(const void* e, ULONGLONG next) {
  Retry* r = retry_find(e);
  if (!r) {
    if (s_nretry >= kRetryMax) {   // full: drop the entry whose wait ends first
      int k = 0;
      for (int i = 1; i < s_nretry; ++i) if (s_retry[i].next < s_retry[k].next) k = i;
      r = &s_retry[k];
    } else {
      r = &s_retry[s_nretry++];
    }
    r->ent = e;
  }
  r->next = next;
}
static int asleep_find(const void* e) {
  for (int i = 0; i < s_nasleep; ++i) if (s_asleep[i].ent == e) return i;
  return -1;
}
static void asleep_drop(int i) { s_asleep[i] = s_asleep[--s_nasleep]; }

static void wake(const unsigned char* player, const EntSnapshot& snap, ULONGLONG nowMs) {
  if (!g_eng.phys_ray_visible || !engine_resolve_p3shared() || !g_p3.tm_ctor) { s_noWorld++; return; }
  void* world;
  memcpy(&world, player + kOfsWorld, sizeof world);   // inside probe's kPlayerSpan
  if (!world || !guarded_readable(world, kOfsPhys + 4)) { s_noWorld++; return; }
  const void* phys;
  memcpy(&phys, (const unsigned char*)world + kOfsPhys, sizeof phys);
  if (!phys) { s_noWorld++; return; }
  double clock;
  memcpy(&clock, (const unsigned char*)world + kOfsWorldTime, sizeof clock);
  const float now = (float)clock;   // FLD double; FSTP float, as the draw stores it
  ReadCache rc = {};
  AiCand c[AI_CAND_MAX];
  int n = 0;
  for (int i = 0; i < snap.count; ++i) {
    const EntSnap& s = snap.ents[i];
    if (s.cat != CAT_ENEMY || s.egg) continue;   // an egg has no monster AI to wake
    const unsigned char* e = (const unsigned char*)s.ent;
    const int ai = s_nasleep ? asleep_find(e) : -1;
    const float dx = s.pos.x - snap.playerPos.x, dy = s.pos.y - snap.playerPos.y, dz = s.pos.z - snap.playerPos.z;
    const float d2 = dx * dx + dy * dy + dz * dz;
    const bool pre = ai_wake_eligible(1, s.hashp, s.hp, d2, 0, 0, 0.f, 1);
    if (!pre && ai < 0) continue;
    if (!guarded_readable_cached(&rc, e, kMonSpan)) { s_unread++; continue; }
    if (ai >= 0) {   // A2: did the monster we woke leave state 1?
      if (rdu(e + kState) != 1) { s_leftSleep++; asleep_drop(ai); }
      else if (nowMs - s_asleep[ai].at > 2000) {
        if (s_stillAsleep++ < 8)
          np_log("aiaware: A2 monster %s %p still asleep (state 1) 2 s after the wake; +0xB71=%u +0xB7C=0x%X",
                 s.name ? s.name : "?", (const void*)e, e[kSeen], rdu(e + kFlags));
        asleep_drop(ai);
      }
    }
    if (!pre) continue;
    const Retry* r = retry_find(e);
    if (!ai_wake_eligible(1, s.hashp, s.hp, d2, rdu(e + kFlags), e[kSeen] != 0, now - rdf(e + kSeenAt),
                          !r || ai_retry_due(nowMs, r->next)))
      continue;
    AiCand k = { e, d2 };
    ai_cand_insert(c, &n, AI_CAND_MAX, k);
  }
  for (int i = 0; i < s_nasleep;) {   // not in this frame's walk for 4 s: removed or out of the snapshot
    if (nowMs - s_asleep[i].at > 4000) { s_asleepLost++; asleep_drop(i); } else ++i;
  }
  s_cands += (unsigned)n;
  const int rays = ai_rays_this_frame(n, AI_RAYS_PER_FRAME);
  if (!rays) return;
  Matcher wm;
  world_matcher(&wm);
  for (int k = 0; k < rays; ++k) {
    unsigned char* e = (unsigned char*)c[k].ent;
    const Vec3 mp = rdv(e + 0x18C);
    const Vec3 eye = { mp.x, mp.y + rdf(e + kEye), mp.z };   // the eye monster_can_see_target casts from
    s_rays++;
    if (!g_eng.phys_ray_visible(world, &eye.x, &s_chest.x, wm.b)) {
      s_blocked++;
      retry_set(e, nowMs + AI_RETRY_MS);
      continue;
    }
    if (!guarded_writable(e + kSeen, kSeenAt + 4 - kSeen)) { s_unwritable++; continue; }
    const int was = e[kSeen] != 0;
    const unsigned state = rdu(e + kState);
    e[kSeen] = 1;                               // the pair actor_draw_mark_seen writes (0x1008da1b, 0x1008da25)
    memcpy(e + kSeenAt, &now, sizeof now);
    if (was) { s_refreshed++; continue; }
    s_woke++;
    if (state == 1 && asleep_find(e) < 0) {
      if (s_nasleep < kAsleepMax) { Asleep a = { e, nowMs }; s_asleep[s_nasleep++] = a; }
      else s_asleepFull++;
    }
  }
  g_p3.tm_dtor(wm.b);
}

void aiaware_frame(const unsigned char* player) {
  const EntSnapshot& snap = entities_snapshot();
  const int have = player && snap.player == player;
  const int active = ai_gate(g_cfg.firstPerson, g_cfg.moveAnywhere, have) ? 1 : 0;
  if (active != s_active) np_log("aiaware: %s", active ? "on (First person or Move anywhere)" : "off");
  if (player != s_lastPlayer) { s_nretry = 0; s_nasleep = 0; s_lastPlayer = player; }
  s_haveChest = 0;
  if (active) {
    s_chest = ai_chest(snap.playerPos, rdv(player + 0x2B8), rdf(player + 0x2C4));   // inside kPlayerSpan
    s_haveChest = 1;
  }
  s_player = active ? player : 0;
  s_active = active;
  const ULONGLONG nowMs = GetTickCount64();
  if (active) {
    arm();
    if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    wake(player, snap, nowMs);
    QueryPerformanceCounter(&t1);
    s_us += (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)s_qpf.QuadPart;
    s_frames++;
  }
  if (nowMs < s_nextLog) return;
  s_nextLog = nowMs + 5000;
  const unsigned sum = s_frames + s_seeCalls + s_attCalls + s_leftSleep + s_stillAsleep + s_asleepLost;
  if (sum != s_loggedSum) {
    s_loggedSum = sum;
    np_log("aiaware: L1 frames=%u cands=%u rays=%u blocked=%u woke=%u refreshed=%u unwritable=%u unread=%u noworld=%u "
           "us/frame=%.1f | A2 left_sleep=%u still_asleep=%u lost=%u full=%u | L2 calls=%u stock=%u pass=%u "
           "coneless_true=%u coneless_false=%u | L3 calls=%u other=%u kept=%u reaimed=%u unread=%u",
           s_frames, s_cands, s_rays, s_blocked, s_woke, s_refreshed, s_unwritable, s_unread, s_noWorld,
           s_frames ? s_us / s_frames : 0.0, s_leftSleep, s_stillAsleep, s_asleepLost, s_asleepFull, s_seeCalls,
           s_seeStock, s_seePass, s_seeConeTrue, s_seeConeFalse, s_attCalls, s_attOther, s_attKept, s_attReaimed,
           s_attUnread);
  }
  s_frames = s_cands = s_rays = s_blocked = s_woke = s_refreshed = s_unwritable = s_unread = s_noWorld = 0;
  s_us = 0.0;
}
