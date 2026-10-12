// Weapons tab levers: one poll of the held weapon a frame, a write after each detected shot. weapons.h owns every rule;
// this file owns the reads, writes and counters. Main thread only, on the frame path after the weapon's own think in
// the same frame (docs/re/weapons.md section 1: vt+0xB0 runs once per rendered frame).
#include "weapons.h"
#include "engine.h"
#include "entities.h"
#include "config.h"
#include "log.h"
#include "aim.h"      // aim_rifle_nospread: silent aim borrows the no-spread lever
#include <windows.h>
#include <string.h>

// Slot 1, get_classname: the walk's own call (entities.cpp).
typedef const char* (__thiscall* ClassnameFn)(const void* self);

// weapons.md section 1 unless noted.
static const unsigned kOfsHeld = 0xDF0;        // player -> held weapon [A]; inside probe's kPlayerSpan 0xED0
static const unsigned kOfsState = 0xB10;       // dword, 1 idle / 6 attacking (shotgun fire logic param_1[0x2c4])
static const unsigned kOfsNow = 0xB14;         // float, (float)world+0x40
static const unsigned kOfsLastFire = 0xB18;    // float, last-fire time
static const unsigned kOfsAnimEnd = 0xB2C;     // float, skmod clock (compared at 0x10050b8c)
// 5 timed-event slots, stride 0xC: active byte +0, id dword +4, due float +8 (weap_schedule_event 0x10050f67-0x10050f89)
static const unsigned kOfsEvents = 0xB90;
static const int kEventSlots = 5;
static const unsigned kEventStride = 0xC;
static const int kPbombLaunchId = 0;           // weap_pbomb_fire_logic: weap_schedule_event(this, launch, 0)
// Golden Eagle (sections 2-4)
static const unsigned kOfsGeRefire = 0xBF8;    // refire gate / reload lock
static const unsigned kOfsGeAutoFire = 0xC2C;  // auto_fire_time
static const unsigned kOfsGeReloadTime = 0xC30;   // reload_time / multiplier
static const unsigned kOfsGeReloading = 0xC38; // byte, 1 while reloading (start_reload 0x1005542a)
// Assault Rifle (sections 2, 3, 5)
static const unsigned kOfsArRps = 0xC28;       // int rounds_per_sec, parse-time (0x10051f1a); fire logic FILD 0x10052870
static const unsigned kOfsArInterval = 0xCE8;  // 1/rounds_per_sec, parse-time (0x10051f38)
static const unsigned kOfsArConeStart = 0xD1C; // cone_ang_start, parse-time (0x10051df1)
static const unsigned kOfsArConeEnd = 0xD20;   // cone_ang_end, parse-time (0x10051e20)
// Pulse Cannon: cooldown set to 2.0 at discharge (0x1005954a), counted down by dt
static const unsigned kOfsPcCooldown = 0xCC8;
// The smallest weapon class (actor_weap_x3000, 0xC48): read before the class is known.
static const unsigned kWeapMinSpan = 0xC48;

struct Held { unsigned char* w; const void* vt; int kind; const char* cls; };
static Held s_held;
static WeapWatchF s_b18, s_cc8;
static WeapWatchI s_state, s_reload;
static int s_launchPending;

// The rifle the holds were applied to; restored there, after re-checking its vtable, when it leaves the hand.
static unsigned char* s_rifle;
static const void* s_rifleVt;
static WeapHold s_hInterval, s_hConeStart, s_hConeEnd;
static WeapHoldI s_hRps;
static int s_rifleLoggedRps;   // the rate last logged for this rifle; 0 = none

static unsigned s_shots[WK_COUNT], s_levers[WK_COUNT];
static unsigned s_reloads, s_reloadLevers, s_clamps, s_noLaunch, s_launches, s_noClock;
static unsigned s_refused, s_restores, s_restoreSkipped, s_changes;
static unsigned s_loggedSum = 0xFFFFFFFFu;
static ULONGLONG s_nextLog;

const char* weapons_held_name(void) { return weap_name(s_held.kind); }
unsigned weapons_shots(void) { unsigned n = 0; for (int k = 0; k < WK_COUNT; ++k) n += s_shots[k]; return n; }
unsigned weapons_levers(void) {
  unsigned n = s_reloadLevers;
  for (int k = 0; k < WK_COUNT; ++k) n += s_levers[k];
  return n;
}
unsigned weapons_reloads(void) { return s_reloads; }
unsigned char* weapons_held(const void** vt, int* kind) {
  *kind = s_held.kind;
  *vt = s_held.vt;
  return s_held.kind == WK_NONE || s_held.kind == WK_OTHER ? 0 : s_held.w;
}

static float rdf(const unsigned char* p) { float f; memcpy(&f, p, sizeof(f)); return f; }
static int rdi(const unsigned char* p) { int v; memcpy(&v, p, sizeof(v)); return v; }

// Every write re-reads the vtable first: a freed weapon's vptr is rewritten by its dtor chain
// (arifle 0x10051aa0 -> base 0x100542c0 stores 0x10131598), so a stale object never matches.
static bool wr_float(unsigned char* w, const void* vt, unsigned ofs, float v) {
  const void* now;
  memcpy(&now, w, sizeof(now));
  if (now != vt || !guarded_writable(w + ofs, sizeof(float))) { s_refused++; return false; }
  memcpy(w + ofs, &v, sizeof(v));
  return true;
}

static bool wr_int(unsigned char* w, const void* vt, unsigned ofs, int v) {
  const void* now;
  memcpy(&now, w, sizeof(now));
  if (now != vt || !guarded_writable(w + ofs, sizeof(int))) { s_refused++; return false; }
  memcpy(w + ofs, &v, sizeof(v));
  return true;
}

static void reset_watches(void) {
  s_b18.primed = 0; s_cc8.primed = 0; s_state.primed = 0; s_reload.primed = 0;
  s_launchPending = 0;
}

// Puts the rifle's stock values back and forgets it. Nothing is written unless the object is
// still readable and still carries the rifle's vtable.
static void rifle_release(void) {
  if (!s_rifle) return;
  if (s_hInterval.applied || s_hConeStart.applied || s_hConeEnd.applied || s_hRps.applied) {
    const void* vt = 0;
    if (guarded_readable(s_rifle, weap_span(WK_ARIFLE))) memcpy(&vt, s_rifle, sizeof(vt));
    if (vt && vt == s_rifleVt) {
      WeapHold* const holds[3] = { &s_hInterval, &s_hConeStart, &s_hConeEnd };
      const unsigned ofs[3] = { kOfsArInterval, kOfsArConeStart, kOfsArConeEnd };
      for (int i = 0; i < 3; ++i) {
        float out;
        if (weap_hold_step(holds[i], 0, rdf(s_rifle + ofs[i]), 0.f, &out) == WH_WRITE &&
            wr_float(s_rifle, s_rifleVt, ofs[i], out)) s_restores++;
      }
      int outI;
      if (weap_holdi_step(&s_hRps, 0, rdi(s_rifle + kOfsArRps), 0, &outI) == WH_WRITE &&
          wr_int(s_rifle, s_rifleVt, kOfsArRps, outI)) s_restores++;
    } else {
      s_restoreSkipped++;
      np_log("weapons: rifle %p gone (unreadable or vtable changed); its stock values were not written", s_rifle);
    }
  }
  memset(&s_hInterval, 0, sizeof(s_hInterval));
  memset(&s_hConeStart, 0, sizeof(s_hConeStart));
  memset(&s_hConeEnd, 0, sizeof(s_hConeEnd));
  memset(&s_hRps, 0, sizeof(s_hRps));
  s_rifleLoggedRps = 0;
  s_rifle = 0;
  s_rifleVt = 0;
}

// The class of w, or WK_NONE when it cannot be read safely. The class-name call runs only
// when the vtable differs from the held one.
static int identify(unsigned char* w, ReadCache* rc, const void** vtOut, const char** clsOut) {
  if (!guarded_readable_cached(rc, w, kWeapMinSpan)) return WK_NONE;
  void* const* vt;
  memcpy(&vt, w, sizeof(vt));
  *vtOut = vt;
  int kind;
  const char* cls;
  if (vt == s_held.vt && w == s_held.w) {
    kind = s_held.kind;
    cls = s_held.cls;
  } else {
    if (!entities_in_duke(vt) || !entities_in_duke(vt + 2) || !entities_in_duke(vt[1])) return WK_NONE;
    cls = ((ClassnameFn)vt[1])(w);
    if (!entities_in_duke(cls)) return WK_NONE;
    kind = weap_kind(cls);
  }
  *clsOut = cls;
  const unsigned span = weap_span(kind);
  if (span && !guarded_readable_cached(rc, w, span)) return WK_NONE;
  return kind;
}

static bool skmod_now(float* now) {
  if (!engine_resolve_skmod()) { s_noClock++; return false; }
  *now = (float)g_sk.skmod_get_time();
  return true;
}

// Shotgun and pipebomb: the attack state was just entered this frame.
static void anim_gated(unsigned char* w, const void* vt, int kind, float m) {
  const int entered = weap_entered(&s_state, rdi(w + kOfsState), 6);
  int pending = 0, haveLaunch = 0;
  float launchDue = 0.f;
  if (kind == WK_PBOMB) {
    for (int i = 0; i < kEventSlots; ++i) {
      const unsigned char* e = w + kOfsEvents + i * kEventStride;
      if (e[0] == 1 && rdi(e + 4) == kPbombLaunchId) {
        pending = 1;
        if (!haveLaunch || rdf(e + 8) > launchDue) launchDue = rdf(e + 8);
        haveLaunch = 1;
      }
    }
    // A launch slot that was active and is gone was dispatched: one bomb thrown.
    if (s_launchPending && !pending) s_launches++;
    s_launchPending = pending;
  }
  if (!entered) return;
  s_shots[kind]++;
  if (m == 1.f) return;
  if (kind == WK_PBOMB && !haveLaunch) { s_noLaunch++; return; }
  float now;
  if (!skmod_now(&now)) return;
  const float end = rdf(w + kOfsAnimEnd);
  const float nv = kind == WK_PBOMB ? weap_pbomb_end(end, now, m, launchDue) : weap_anim_end(end, now, m);
  if (kind == WK_PBOMB && nv == launchDue && weap_anim_end(end, now, m) < launchDue) s_clamps++;
  if (nv != end && wr_float(w, vt, kOfsAnimEnd, nv)) s_levers[kind]++;
}

static void geagle(unsigned char* w, const void* vt, float m) {
  const int reloading = w[kOfsGeReloading] != 0;
  if (weap_changed(&s_b18, rdf(w + kOfsLastFire))) {
    s_shots[WK_GEAGLE]++;
    // A reload's lock lives in the same field; the shot lever never shortens it.
    if (!reloading) {
      const float bf8 = rdf(w + kOfsGeRefire);
      const float nv = weap_geagle_bf8(bf8, rdf(w + kOfsGeAutoFire), m);
      if (nv != bf8 && wr_float(w, vt, kOfsGeRefire, nv)) s_levers[WK_GEAGLE]++;
    }
  }
  if (weap_rising(&s_reload, reloading)) {
    s_reloads++;
    const float bf8 = rdf(w + kOfsGeRefire);
    const float nv = weap_reload_bf8(bf8, rdf(w + kOfsGeReloadTime), g_cfg.reloadPct);
    const bool wrote = nv != bf8 && wr_float(w, vt, kOfsGeRefire, nv);
    if (wrote) s_reloadLevers++;
    char r[16];
    weap_reload_text(g_cfg.reloadPct, r, sizeof r);
    np_log("weapons: reload #%u r=%s lock %.3f -> %.3f%s", s_reloads, r, bf8, wrote ? nv : bf8,
           wrote ? "" : " (unchanged)");
  }
}

static void arifle(unsigned char* w, const void* vt, float m) {
  if (weap_changed(&s_b18, rdf(w + kOfsLastFire))) s_shots[WK_ARIFLE]++;
  s_rifle = w;
  s_rifleVt = vt;
  // Fire rate: rounds_per_sec and its interval move together, or the round count rounds to 0
  // and the engine switches weapon (weapons.md section 2, arifle row).
  const int rpsCur = rdi(w + kOfsArRps);
  weap_holdi_capture(&s_hRps, rpsCur);
  const unsigned ofs[3] = { kOfsArInterval, kOfsArConeStart, kOfsArConeEnd };
  WeapHold* const holds[3] = { &s_hInterval, &s_hConeStart, &s_hConeEnd };
  for (int i = 0; i < 3; ++i) weap_hold_capture(holds[i], rdf(w + ofs[i]));
  const int wantRate = m != 1.f && s_hRps.captured && s_hInterval.captured;
  const int rps = wantRate ? weap_arifle_rps(s_hRps.stock, m) : s_hRps.stock;
  const float iv = wantRate ? weap_arifle_interval_for(rps) : s_hInterval.stock;
  int outI;
  if (weap_holdi_step(&s_hRps, wantRate, rpsCur, rps, &outI) == WH_WRITE && wr_int(w, vt, kOfsArRps, outI))
    s_levers[WK_ARIFLE]++;
  // The interval only follows an rps the object really holds: a refused rps write with a short
  // interval would bring back the 0-round switch.
  const int rateOk = wantRate && rdi(w + kOfsArRps) == rps;
  for (int i = 0; i < 3; ++i) {
    const float cur = rdf(w + ofs[i]);
    // A silent-aim shot would still be deflected by the cone after the detour (aim.md section 1).
    const int want = i == 0 ? rateOk : (g_cfg.accArifle != 0 || aim_rifle_nospread() != 0);
    float out;
    if (weap_hold_step(holds[i], want, cur, i == 0 ? iv : 0.f, &out) == WH_WRITE && wr_float(w, vt, ofs[i], out))
      s_levers[WK_ARIFLE]++;
  }
  const int logRps = wantRate ? rps : 0;
  if (logRps != s_rifleLoggedRps) {
    s_rifleLoggedRps = logRps;
    if (wantRate)
      np_log("weapons: rifle rps %d -> %d, interval %.4f -> %.4f", s_hRps.stock, rps, s_hInterval.stock, iv);
    else
      np_log("weapons: rifle rps %d, interval %.4f restored", s_hRps.stock, s_hInterval.stock);
  }
}

static void pcannon(unsigned char* w, const void* vt, float m) {
  const float cd = rdf(w + kOfsPcCooldown);
  if (!weap_rose(&s_cc8, cd)) return;
  s_shots[WK_PCANNON]++;
  const float nv = weap_pcannon_cd(cd, m);
  if (nv != cd && wr_float(w, vt, kOfsPcCooldown, nv)) { s_levers[WK_PCANNON]++; weap_note(&s_cc8, nv); }
}

static void prpg(unsigned char* w, const void* vt, float m) {
  const float b18 = rdf(w + kOfsLastFire);
  if (!weap_changed(&s_b18, b18)) return;
  s_shots[WK_PRPG]++;
  const float nv = weap_prpg_b18(b18, rdf(w + kOfsNow), m);
  // Our own back-dating is not the next frame's shot.
  if (nv != b18 && wr_float(w, vt, kOfsLastFire, nv)) { s_levers[WK_PRPG]++; weap_note(&s_b18, nv); }
}

static void weap_log(void) {
  const ULONGLONG tick = GetTickCount64();
  if (tick < s_nextLog) return;
  s_nextLog = tick + 5000;
  const unsigned sum = weapons_shots() + weapons_levers() + s_reloads + s_launches + s_noLaunch + s_noClock +
                       s_refused + s_restores + s_restoreSkipped + s_changes;
  if (sum == s_loggedSum) return;   // cumulative: an unchanged line says nothing
  s_loggedSum = sum;
  char per[256];
  int n = 0;
  per[0] = 0;
  for (int k = WK_GEAGLE; k < WK_OTHER; ++k) {
    const int r = _snprintf_s(per + n, sizeof per - n, _TRUNCATE, " %s=%u/%u", weap_name(k), s_shots[k], s_levers[k]);
    if (r < 0) break;
    n += r;
  }
  np_log("weapons: held=%s shots/levers:%s reloads=%u reloadLevers=%u launches=%u clamps=%u noLaunch=%u "
         "noClock=%u refused=%u restores=%u restoreSkipped=%u changes=%u fire=%d reload=%d acc=%d",
         weap_name(s_held.kind), per, s_reloads, s_reloadLevers, s_launches, s_clamps, s_noLaunch, s_noClock,
         s_refused, s_restores, s_restoreSkipped, s_changes, g_cfg.fireRatePct, g_cfg.reloadPct, g_cfg.accArifle);
}

void weapons_frame(const unsigned char* player) {
  weap_log();
  unsigned char* w = 0;
  if (player) memcpy(&w, player + kOfsHeld, sizeof(w));
  ReadCache rc = {};   // zeroed each frame: a region freed since last frame is never trusted
  const void* vt = 0;
  const char* cls = 0;
  const int kind = w ? identify(w, &rc, &vt, &cls) : WK_NONE;

  if (w != s_held.w || vt != s_held.vt || kind != s_held.kind) {
    if (s_rifle && (w != s_rifle || kind != WK_ARIFLE)) rifle_release();
    reset_watches();
    s_changes++;
    np_log("weapons: held %s (%s) %p vt %p, was %s", weap_name(kind),
           cls ? cls : "-", (void*)w, vt, weap_name(s_held.kind));
    s_held.w = w; s_held.vt = vt; s_held.kind = kind; s_held.cls = cls;
  }
  if (kind == WK_NONE || kind == WK_OTHER) return;

  const float m = weap_mult(g_cfg.fireRatePct);
  switch (kind) {
    case WK_GEAGLE:  geagle(w, vt, m); break;
    case WK_SHOTGUN:
    case WK_PBOMB:   anim_gated(w, vt, kind, m); break;
    case WK_ARIFLE:  arifle(w, vt, m); break;
    case WK_PCANNON: pcannon(w, vt, m); break;
    case WK_PRPG:    prpg(w, vt, m); break;
    default: break;   // G.L.O.P.P. Ray and X-3000: no fire delay to scale (section 2)
  }
}
