// "Don't waste shots": the monster_damage_send detour (damage sent, not yet applied), the walk of Duke's live
// projectiles once a frame, the rifle's burst and the log. overkill.h owns every rule; aim.cpp asks for verdicts.
// Main thread only (game tick and frame path share one loop, render.md section 1): no locking. See docs/re/aim.md s10.
#include "overkill.h"
#include "aim.h"
#include "engine.h"
#include "entities.h"
#include "weapons.h"
#include "hook.h"
#include "log.h"
#include "ui/wallclock.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

// monster_damage_send duke!0x1002f3f0 = monster vt+0x198: thiscall (type, amount, vec3* dir, inflictor,
// attacker), RET 0x14 (entities.md section 6).
typedef void (__thiscall* SendFn)(void* self, int type, float amount, float* dir, void* inflictor, void* attacker);
typedef const char* (__thiscall* ClassnameFn)(const void* self);

static const unsigned kSlotSend = 0x198 / 4;
static const unsigned kOfsWorld = 0xE0, kOfsServerByte = 0xC8;   // the original's own gate: world+0xC8 & 8
static const unsigned kOfsMFlags = 0xB7C;                      // monster flags, then health +0xB84
// Projectile fields: proj base ctor duke!0x10038c90 (user |= 0x400, attacker +0xB30 via proj_set_attacker
// 0x10039980, damage +0xBC0, radius +0xBC4, budget +0xBCC), position +0x18C, velocity/direction +0x1C0.
static const unsigned kOfsPos = 0x18C, kOfsVel = 0x1C0, kOfsAttacker = 0xB30, kOfsDamage = 0xBC0,
                      kOfsRadius = 0xBC4, kOfsBudget = 0xBCC;
static const unsigned kOfsBulletFresh = 0xBEC;   // byte, 1 until proj_bullet_hitscan_ray casts (0x1003a9d7)
static const unsigned kOfsRocketLife = 0xC00, kOfsRocketDone = 0xC04;   // ctor 0x1004f220: 1.0 s; explode 0x1004f965
// proj_plasma_collide 0x1004e260: charge +0xBD4, state +0xBE0 (0 flying), pierce count +0x27C0; think
// 0x1004ced0 removes it while +0x27D0 is set.
static const unsigned kOfsPlasmaCharge = 0xBD4, kOfsPlasmaState = 0xBE0, kOfsPlasmaPierce = 0x27C0,
                      kOfsPlasmaGone = 0x27D0;
// Readable spans, through the last field read (the class allocs are larger: dumps/duke_base.classes.txt).
static const unsigned kSpan[OVK_P_KINDS] = { 0x50, 0xBF0, 0xC08, 0x27D4, 0xB34 };
static const float kBulletLen = 5000.f;   // the bullet's longest cast (game_clip_ray_to_view_sides, aim.md 4)
static const float kPlasmaLife = 5.f;     // destroy_time = now + 5 (weap_pcannon_discharge 0x100590f0)
static const float kLosBack = 4.f;        // the splash's sight ray starts this far back from a wall hit
// The rifle: committed rounds +0xC2C (weapons.md section 2), per-round damage proj_damage +0xCE4 (section 3).
static const unsigned kOfsBurst = 0xC2C, kOfsRifleDamage = 0xCE4, kOfsWeapState = 0xB10;
// The Pulse Cannon: proj_damage +0xC44, proj_min_damage +0xC48 (ctor 0x10058140; weapons.md section 3) and the
// charge per ammo unit +0xC54 (1 / proj_ammo_rate, parse 0x10058449).
static const unsigned kOfsPcMax = 0xC44, kOfsPcMin = 0xC48, kOfsPcPerAmmo = 0xC54;
// The uncharged shot's damage expected from the held cannon's own fields, and the in-flight balls' damage seen
// (cumulative range): a tap's ball should read the expected value (doubled under the damage power-up).
static float s_tapExpect, s_plasmaMin, s_plasmaMax;
// kProjMax of Duke's own projectiles get rays; kScanMax bounds every projectile read (enemies' included).
enum { kProjMax = 32, kScanMax = 256, kLosMax = 32, kVtMax = 32, kProbeMax = 16 };

static SendFn s_tramp;
static void* s_target;
static int s_refused;
static OvkLedger s_ledger;
static OvkTable s_table;
static const void* s_burstEnt;
static int s_burstLive;   // s_burstEnt has rounds left this frame
static const void* s_prevDoomed[OVK_MAX];
static int s_nprevDoomed;
struct ProjVt { const void* vt; int kind; };
static ProjVt s_pvt[kVtMax];
static int s_npvt;
static int s_losFrame;   // sight rays this frame, at most kLosMax

// Cumulative. sends: monster sends counted; sendSkipped: sends that take no health (types 3/4, 6-8 under 0x200);
// sendExpired: runs dropped by the TTL with damage still unapplied. proj[] by kind; fresh: bullets seen before
// their cast; predicted: projectiles whose victim was predicted; doomed[]: enemies newly doomed, by cause.
static unsigned s_frames, s_sends, s_sendSkipped, s_sendNoServer, s_sendUnread, s_sendExpired, s_proj[OVK_P_KINDS],
                s_projOther, s_notMine, s_projUnread, s_projCapped, s_fresh, s_predicted, s_noVictim, s_losRays,
                s_losHidden, s_losCapped, s_doomed[OVK_CAUSES], s_withheld, s_withheldBy[OVK_CAUSES],
                s_pickSkipped, s_burstFrames, s_vtFull;
static unsigned s_loggedSum = 0xFFFFFFFFu;
static ULONGLONG s_nextLog;

static Vec3 rdv(const unsigned char* p) { Vec3 v; memcpy(&v, p, sizeof v); return v; }
static float rdf(const unsigned char* p) { float f; memcpy(&f, p, sizeof f); return f; }
static int rdi(const unsigned char* p) { int v; memcpy(&v, p, sizeof v); return v; }
static float vlen(Vec3 v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }

// The original runs first, so the ledger only notes what it really sent.
static void __fastcall send_detour(void* self, void* /*edx*/, int type, float amount, float* dir, void* inflictor,
                                   void* attacker) {
  s_tramp(self, type, amount, dir, inflictor, attacker);
  const unsigned char* m = (const unsigned char*)self;
  const unsigned char* world;
  memcpy(&world, m + kOfsWorld, sizeof world);   // the original has just read both (0x1002f3f6-0x1002f402)
  if (!world || !(world[kOfsServerByte] & 8)) { s_sendNoServer++; return; }
  if (!guarded_readable(m + kOfsMFlags, 12)) { s_sendUnread++; return; }
  unsigned flags;
  memcpy(&flags, m + kOfsMFlags, sizeof flags);
  if (!ovk_send_counts(type, amount, flags)) { s_sendSkipped++; return; }
  s_sends++;
  ovk_ledger_note(&s_ledger, self, rdf(m + kOfsMFlags + 8), amount, wallclock_ms());
}

// Armed from a live monster's own vt+0x198 the first time silent aim or the Triggerbot runs. A class whose slot is
// a wrapper (ovk_send_slot_kind) is skipped, logged once, and the next monster tried; only a refused install is final.
static const void* s_armTried[kVtMax];
static int s_narmTried;
static void arm(const EntSnapshot& snap) {
  if (s_target || s_refused) return;
  const unsigned char* base = 0;
  unsigned size = 0;
  if (!entities_duke_image(&base, &size)) return;
  for (int i = 0; i < snap.count; ++i) {
    const EntSnap& mon = snap.ents[i];
    if (mon.cat != CAT_ENEMY || !mon.hashp || mon.egg != EGG_NONE) continue;
    int tried = 0;
    for (int j = 0; j < s_narmTried && !tried; ++j) tried = s_armTried[j] == mon.vt;
    if (tried) continue;
    if (s_narmTried < kVtMax) s_armTried[s_narmTried++] = mon.vt;
    const void* const* slot = (const void* const*)mon.vt + kSlotSend;
    if (!entities_in_duke(slot) || !entities_in_duke((const unsigned char*)(slot + 1) - 1)) continue;
    void* const target = (void*)*slot;
    if (!entities_in_duke(target) || !guarded_readable(target, OVK_SEND_FP_LEN)) continue;
    const unsigned char* t = (const unsigned char*)target;
    if (ovk_send_slot_kind(t) != OVK_SLOT_STOCK) {
      np_log("overkill: %s vt+0x198 -> %p (%02X %02X %02X %02X %02X %02X) is not monster_damage_send's stock entry; "
             "another monster is tried", mon.name, target, t[0], t[1], t[2], t[3], t[4], t[5]);
      continue;
    }
    if (!hook_install_at(target, kOvkSendFp, OVK_SEND_FP_LEN, OVK_SEND_TRAMP_LEN, (void*)send_detour, (void**)&s_tramp)) {
      s_refused = 1;
      np_log("overkill: install at %p refused; no send ledger", target);
      return;
    }
    s_target = target;
    np_log("overkill: armed at %p (duke_base+%X) from %s vt %p", target, (unsigned)(t - base), mon.name, mon.vt);
    return;
  }
}

// A monster the rule judges: an enemy with monster health (no egg, no boss: their own handlers, aim.md section 7).
static const EntSnap* monster_of(const EntSnapshot& snap, const void* ent) {
  if (!ent) return 0;
  for (int i = 0; i < snap.count; ++i) {
    const EntSnap& e = snap.ents[i];
    if (e.ent != ent) continue;
    return e.cat == CAT_ENEMY && e.hashp && e.egg == EGG_NONE ? &e : 0;
  }
  return 0;
}

static int proj_kind_of(const unsigned char* p, const void* vt) {
  for (int i = 0; i < s_npvt; ++i) if (s_pvt[i].vt == vt) return s_pvt[i].kind;
  const void* const* v = (const void* const*)vt;
  int kind = OVK_P_NONE;
  const char* cls = 0;
  if (entities_in_duke(v) && entities_in_duke(v + 2) && entities_in_duke(v[1])) {
    cls = ((ClassnameFn)v[1])(p);
    if (entities_in_duke(cls)) kind = ovk_proj_kind(cls);
  }
  if (s_npvt < kVtMax) {
    s_pvt[s_npvt].vt = vt; s_pvt[s_npvt].kind = kind; s_npvt++;
    np_log("overkill: projectile class %s (vt %p) -> kind %d", cls && entities_in_duke(cls) ? cls : "?", vt, kind);
  } else {
    s_vtFull++;
  }
  return kind;
}

// The first thing a rocket or a plasma ball stops at along from -> to (its block matcher, ovk_proj_stops), or -1.
static int first_stop(const AimProbeHit* h, int n) {
  for (int i = 0; i < n; ++i)
    if (!h[i].read || ovk_proj_stops(h[i].type, h[i].user, h[i].dying)) return i;   // unread: taken as a wall
  return -1;
}

static void direct_hit(const EntSnapshot& snap, const void* obj, float dmg) {
  if (monster_of(snap, obj) && dmg > 0.f) { ovk_add(&s_table, obj, OVK_INFLIGHT, dmg); s_predicted++; }
  else s_noVictim++;
}

// A bullet before its cast: the bullet's own ray (its nearest object takes it, aim.md section 4b).
static void bullet(const EntSnapshot& snap, const unsigned char* p) {
  if (p[kOfsBulletFresh] != 1) return;   // cast already: its damage is in the ledger
  s_fresh++;
  const Vec3 from = rdv(p + kOfsPos), d = rdv(p + kOfsVel);
  const float l = vlen(d);
  if (!(l > 0.f)) return;
  const Vec3 to = { from.x + d.x / l * kBulletLen, from.y + d.y / l * kBulletLen, from.z + d.z / l * kBulletLen };
  AimProbeHit h[kProbeMax];
  const int n = aim_probe_ray(from, to, 1, h, kProbeMax);
  const float budget = rdf(p + kOfsBudget);
  const float dmg = rdf(p + kOfsDamage) * (budget > 0.f && budget < 1.f ? budget : 1.f);
  if (n) direct_hit(snap, h[0].obj, dmg); else s_noVictim++;
}

// A rocket in flight: it explodes at the first thing it stops at, or where its life ends, and splashes every
// actor within its radius it can see (0x10039080). The one it struck counts as the in-flight hit.
static void rocket(const EntSnapshot& snap, const unsigned char* p) {
  if (p[kOfsRocketDone]) return;
  const Vec3 from = rdv(p + kOfsPos), v = rdv(p + kOfsVel);
  const float life = rdf(p + kOfsRocketLife), speed = vlen(v);
  const float t = life > 0.f && life < 10.f ? life : 0.f;
  Vec3 at = { from.x + v.x * t, from.y + v.y * t, from.z + v.z * t };
  const void* struck = 0;
  if (speed > 0.f && t > 0.f) {
    AimProbeHit h[kProbeMax];
    const int i = first_stop(h, aim_probe_ray(from, at, 0, h, kProbeMax));
    if (i >= 0) { at = h[i].at; struck = h[i].obj; }
  }
  const float r = rdf(p + kOfsRadius), dmg = rdf(p + kOfsDamage);
  if (!(r > 0.f && r < 10000.f) || !(dmg > 0.f)) { s_noVictim++; return; }
  const Vec3 back = speed > 0.f ? Vec3{ at.x - v.x / speed * kLosBack, at.y - v.y / speed * kLosBack,
                                        at.z - v.z / speed * kLosBack } : at;
  s_predicted++;
  for (int k = 0; k < snap.count; ++k) {
    const EntSnap& e = snap.ents[k];
    if (e.cat != CAT_ENEMY || !e.hashp || e.egg != EGG_NONE || !(e.hp > 0.f)) continue;
    const float dist = ovk_splash_dist(at, e.hb.c, e.hb.half);
    const float amount = ovk_splash_damage(r, dist, dmg);
    if (!(amount > 0.f)) continue;
    if (e.ent != struck) {
      if (s_losFrame >= kLosMax) { s_losCapped++; continue; }
      s_losFrame++;
      s_losRays++;
      if (!aim_probe_visible(back, e.hb.c)) { s_losHidden++; continue; }
    }
    ovk_add(&s_table, e.ent, e.ent == struck ? OVK_INFLIGHT : OVK_SPLASH, amount);
  }
}

// A plasma ball in flight: the first actor it touches takes its damage, no splash (proj_plasma_collide).
static void plasma(const EntSnapshot& snap, const unsigned char* p) {
  if (rdi(p + kOfsPlasmaState) != 0 || rdi(p + kOfsPlasmaGone) != 0) return;
  const Vec3 from = rdv(p + kOfsPos), v = rdv(p + kOfsVel);
  if (!(v.x * v.x + v.y * v.y + v.z * v.z > 0.001f)) return;   // still charging at the muzzle (the think's test)
  Vec3 to = { from.x + v.x * kPlasmaLife, from.y + v.y * kPlasmaLife, from.z + v.z * kPlasmaLife };
  const float l = vlen(aim_sub(to, from));
  if (l > kBulletLen) to = Vec3{ from.x + (to.x - from.x) / l * kBulletLen, from.y + (to.y - from.y) / l * kBulletLen,
                                 from.z + (to.z - from.z) / l * kBulletLen };
  const float dmg = rdf(p + kOfsDamage);
  if (!(s_plasmaMax > 0.f) || dmg < s_plasmaMin) s_plasmaMin = dmg;
  if (dmg > s_plasmaMax) s_plasmaMax = dmg;
  AimProbeHit h[kProbeMax];
  const int i = first_stop(h, aim_probe_ray(from, to, 0, h, kProbeMax));
  if (i < 0) { s_noVictim++; return; }
  direct_hit(snap, h[i].obj, ovk_plasma_direct(dmg, rdf(p + kOfsPlasmaCharge),
                                              rdi(p + kOfsPlasmaPierce) > 0, 1));
}

// Duke's live projectiles, through the engine's own key for them (FUN_1007f090: "s=%d?t=%d?Y=%d", 1, 0x400).
static void projectiles(const unsigned char* player, const EntSnapshot& snap) {
  if (!g_eng.spawn_search || !g_eng.spawn_search_get || !g_eng.spawn_search_stop) return;
  int sid;
  memcpy(&sid, player + 0x78, sizeof sid);   // the walk's own server key (entities.cpp)
  ReadCache rc = {};
  g_eng.spawn_search("s=%d?t=%d?Y=%d", sid, 1, 0x400);
  const unsigned char* p;
  int scanned = 0, mine = 0;
  while ((p = (const unsigned char*)g_eng.spawn_search_get()) != 0) {
    if (scanned++ >= kScanMax) { s_projCapped++; continue; }
    if (!guarded_readable_cached(&rc, p, kSpan[0])) { s_projUnread++; continue; }
    const void* vt;
    memcpy(&vt, p, sizeof vt);
    const int kind = proj_kind_of(p, vt);
    if (kind == OVK_P_NONE) { s_projOther++; continue; }
    if (!guarded_readable_cached(&rc, p, kSpan[kind])) { s_projUnread++; continue; }
    const void* attacker;
    memcpy(&attacker, p + kOfsAttacker, sizeof attacker);
    if (attacker != player) { s_notMine++; continue; }
    if (mine++ >= kProjMax) { s_projCapped++; continue; }
    s_proj[kind]++;
    if (kind == OVK_P_BULLET) bullet(snap, p);
    else if (kind == OVK_P_ROCKET) rocket(snap, p);
    else if (kind == OVK_P_PLASMA) plasma(snap, p);
    // OVK_P_PBOMB: Duke detonates it with his next press (weap_pbomb_fire_logic -> FUN_1007f090 -> vt+0x210, and
    // its think explodes it in the same frame): no damage is on its way until then, so it is only counted.
  }
  g_eng.spawn_search_stop();
}

static void ovk_log(void) {
  const ULONGLONG tick = GetTickCount64();
  if (tick < s_nextLog) return;
  s_nextLog = tick + 5000;
  unsigned sum = s_sends + s_sendSkipped + s_fresh + s_predicted + s_noVictim + s_withheld + s_pickSkipped +
                 s_burstFrames + s_projOther + s_notMine;
  for (int k = 0; k < OVK_P_KINDS; ++k) sum += s_proj[k];
  if (sum == s_loggedSum) return;   // cumulative: an unchanged line says nothing
  s_loggedSum = sum;
  // doomed[c]: enemies that turned doomed by cause c (with the burst); withheld == the sum of withheldBy;
  // pickSkipped: silent aim candidates skipped as doomed (aim: doomed=). proj == bullet + rocket + plasma + pbomb.
  np_log("overkill: armed=%d frames=%u sends=%u sendSkipped=%u sendNoServer=%u sendUnread=%u sendExpired=%u "
         "ledger=%d ledgerFull=%u | proj bullet=%u rocket=%u plasma=%u pbomb=%u other=%u notMine=%u unread=%u "
         "capped=%u fresh=%u predicted=%u noVictim=%u los=%u losHidden=%u losCapped=%u classes=%d | doomed dealt=%u "
         "inFlight=%u splash=%u burst=%u | withheld=%u (dealt=%u inFlight=%u splash=%u burst=%u) pickSkipped=%u "
         "burstFrames=%u tableFull=%u | plasma tapExpect=%.1f seen=%.1f..%.1f", s_target != 0, s_frames, s_sends, s_sendSkipped, s_sendNoServer, s_sendUnread,
         s_sendExpired, s_ledger.n, s_ledger.full, s_proj[OVK_P_BULLET], s_proj[OVK_P_ROCKET], s_proj[OVK_P_PLASMA],
         s_proj[OVK_P_PBOMB], s_projOther, s_notMine, s_projUnread, s_projCapped, s_fresh, s_predicted, s_noVictim,
         s_losRays, s_losHidden, s_losCapped, s_npvt, s_doomed[OVK_DEALT], s_doomed[OVK_INFLIGHT],
         s_doomed[OVK_SPLASH], s_doomed[OVK_BURST], s_withheld, s_withheldBy[OVK_DEALT], s_withheldBy[OVK_INFLIGHT],
         s_withheldBy[OVK_SPLASH], s_withheldBy[OVK_BURST], s_pickSkipped, s_burstFrames, s_table.full, s_tapExpect,
         s_plasmaMin, s_plasmaMax);
}

void overkill_frame(const unsigned char* player, int haveCtx) {
  ovk_log();
  s_frames++;
  s_losFrame = 0;
  const unsigned full = s_table.full;
  ovk_reset(&s_table);
  s_table.full = full;   // cumulative
  if (!player) return;
  const EntSnapshot& snap = entities_snapshot();
  arm(snap);
  // Sent and not yet applied: health has not fallen by it yet.
  const unsigned now = wallclock_ms();
  for (int i = 0; i < s_ledger.n;) {
    const OvkSend& r = s_ledger.s[i];
    const EntSnap* e = monster_of(snap, r.ent);
    const float un = e ? ovk_unapplied(r.hpFirst, r.sent, e->hp) : 0.f;
    if (!ovk_send_keep(un, now - r.tLast)) {
      if (e && un > 0.f) s_sendExpired++;
      ovk_ledger_drop(&s_ledger, i);
      continue;
    }
    ovk_add(&s_table, r.ent, OVK_DEALT, un);
    ++i;
  }
  // The rifle's committed rounds, on the target the Triggerbot pressed at.
  const void* wvt;
  int kind;
  const unsigned char* w = weapons_held(&wvt, &kind);
  int left = 0;
  float per = 0.f;
  if (w && kind == WK_ARIFLE) {
    left = ovk_burst_left(rdi(w + kOfsWeapState), rdi(w + kOfsBurst));
    per = rdf(w + kOfsRifleDamage);
  }
  if (w && kind == WK_PCANNON)   // the first held frame's charge at a zero hold time: one ammo unit's
    s_tapExpect = ovk_plasma_charge_damage(rdf(w + kOfsPcMax), rdf(w + kOfsPcMin),
                                           weap_pcannon_first_charge(rdf(w + kOfsPcPerAmmo), 0.f));
  s_burstLive = 0;
  if (left <= 0 || !(per > 0.f && per < 1000.f) || !monster_of(snap, s_burstEnt)) s_burstEnt = 0;
  else { ovk_add(&s_table, s_burstEnt, OVK_BURST, ovk_burst_damage(left, per)); s_burstFrames++; s_burstLive = 1; }
  if (haveCtx) projectiles(player, snap);
  // Newly doomed enemies, by cause (with the burst, as the Triggerbot judges).
  const void* doomed[OVK_MAX];
  int nd = 0;
  for (int i = 0; i < s_table.n; ++i) {
    const EntSnap* e = monster_of(snap, s_table.ent[i]);
    const int c = e ? ovk_cause(e->hp, s_table.p[i], 1) : OVK_ALIVE;
    if (c == OVK_ALIVE) continue;
    doomed[nd++] = s_table.ent[i];
    int was = 0;
    for (int j = 0; j < s_nprevDoomed && !was; ++j) was = s_prevDoomed[j] == s_table.ent[i];
    if (!was) s_doomed[c]++;
  }
  memcpy(s_prevDoomed, doomed, sizeof(void*) * (size_t)nd);
  s_nprevDoomed = nd;
}

int overkill_cause(const void* ent, int withBurst) {
  int i = 0;
  while (i < s_table.n && s_table.ent[i] != ent) ++i;
  if (i >= s_table.n) return OVK_ALIVE;   // nothing on its way: the common case, no snapshot search
  const EntSnap* e = monster_of(entities_snapshot(), ent);
  return e ? ovk_cause(e->hp, s_table.p[i], withBurst) : OVK_ALIVE;
}

void overkill_burst_target(const void* ent) { s_burstEnt = ent; }
const void* overkill_burst_ent(void) { return s_burstLive ? s_burstEnt : 0; }
void overkill_withheld(int cause) {
  s_withheld++;   // shots: see ovk_withheld_step
  if (cause > 0 && cause < OVK_CAUSES) s_withheldBy[cause]++;
}
void overkill_pick_skip(int cause) { (void)cause; s_pickSkipped++; }
