#pragma once
// Weapons tab: fire rate, Golden Eagle reload speed, Assault Rifle accuracy.
// The pure rules; weapons.cpp owns the engine reads and writes. Every field and
// lever is cited from docs/re/weapons.md.
#include <stdio.h>
#include <string.h>

// Sliders: fire 100-400 %, reload 100-1000 % plus a Max stop one step past (stored as 1100).
enum { WEAP_FIRE_MIN = 100, WEAP_FIRE_MAX = 400, WEAP_STEP = 50,
       WEAP_RELOAD_MAX = 1000, WEAP_RELOAD_SLIDER_MAX = 1100 };

enum { WK_NONE = 0, WK_GEAGLE, WK_SHOTGUN, WK_ARIFLE, WK_PBOMB, WK_PCANNON, WK_PRPG, WK_GRAY, WK_X3000,
       WK_OTHER, WK_COUNT };

// get_classname (vt+4) -> kind. span = the class's alloc size (dumps/duke_base.classes.txt), so a
// read never runs past the object; 0 for a class we do not read.
struct WeapClass { const char* key; int kind; const char* name; unsigned span; };
static const WeapClass kWeapClasses[] = {
  { "actor_weap_geagle",  WK_GEAGLE,  "Golden Eagle",   0xD58 },
  { "actor_weap_shotgun", WK_SHOTGUN, "Shotgun",        0xD28 },
  { "actor_weap_arifle",  WK_ARIFLE,  "Assault Rifle",  0xD78 },
  { "actor_weap_pbomb",   WK_PBOMB,   "Pipebomb",       0xC4C },
  { "actor_weap_pcannon", WK_PCANNON, "Pulse Cannon",   0xCCC },
  { "actor_weap_prpg",    WK_PRPG,    "PRPG",           0x4944 },
  { "actor_weap_gray",    WK_GRAY,    "G.L.O.P.P. Ray", 0xCC0 },
  { "actor_weap_x3000",   WK_X3000,   "X-3000",         0xC48 },
};
static const int kWeapClassCount = (int)(sizeof(kWeapClasses) / sizeof(kWeapClasses[0]));

inline int weap_kind(const char* cls) {
  if (!cls) return WK_NONE;
  for (int i = 0; i < kWeapClassCount; i++)
    if (strcmp(cls, kWeapClasses[i].key) == 0) return kWeapClasses[i].kind;
  return WK_OTHER;
}
inline const char* weap_name(int kind) {
  for (int i = 0; i < kWeapClassCount; i++) if (kWeapClasses[i].kind == kind) return kWeapClasses[i].name;
  return kind == WK_NONE ? "--" : "other";
}
inline unsigned weap_span(int kind) {
  for (int i = 0; i < kWeapClassCount; i++) if (kWeapClasses[i].kind == kind) return kWeapClasses[i].span;
  return 0;
}

inline int weap_round_step(int v, int lo, int hi) {
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  return ((v - lo + WEAP_STEP / 2) / WEAP_STEP) * WEAP_STEP + lo;
}
// A hand-edited ini value lands on the nearest knob stop.
inline int weap_fire_pct_clamped(int v) { return weap_round_step(v, WEAP_FIRE_MIN, WEAP_FIRE_MAX); }
inline int weap_reload_pct_clamped(int v) { return weap_round_step(v, WEAP_FIRE_MIN, WEAP_RELOAD_SLIDER_MAX); }
inline bool weap_reload_is_max(int pct) { return weap_reload_pct_clamped(pct) == WEAP_RELOAD_SLIDER_MAX; }
inline void weap_fire_text(int pct, char* out, int cap) {
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "%d%%", weap_fire_pct_clamped(pct));
}
inline void weap_reload_text(int pct, char* out, int cap) {
  if (weap_reload_is_max(pct)) _snprintf_s(out, (size_t)cap, _TRUNCATE, "Max");
  else _snprintf_s(out, (size_t)cap, _TRUNCATE, "%d%%", weap_reload_pct_clamped(pct));
}
// The fire-rate multiplier m; exactly 1 at 100 %, so every lever below is then the identity.
inline float weap_mult(int pct) { return (float)weap_fire_pct_clamped(pct) / 100.f; }

// Golden Eagle refire delay after a shot: +0xBF8 := +0xC2C (auto_fire_time) / m (weapons.md section 2).
inline float weap_geagle_bf8(float bf8, float autoFire, float m) { return m == 1.f ? bf8 : autoFire / m; }
// Pulse Cannon: the cooldown +0xCC8 the discharge just set (2.0, 0x1005954a), / m.
inline float weap_pcannon_cd(float cd, float m) { return m == 1.f ? cd : cd / m; }
// Pulse Cannon: the charge +0xCBC its first held frame leaves (weap_pcannon_fire_logic 0x100588a0): the held time
// +0xBF4 over 4 s, capped at 1, but at least one ammo unit's charge +0xC54 = 1 / proj_ammo_rate (parse 0x10058449).
inline float weap_pcannon_first_charge(float perAmmo, float heldS) {
  float c = heldS / 4.f;
  if (c > 1.f) c = 1.f;
  return c > perAmmo ? c : perAmmo;
}
// PRPG gate is `now - B18 > 0.5` with 0.5 a shared constant: back-date B18 instead of touching it.
inline float weap_prpg_b18(float b18, float now, float m) {
  return m == 1.f ? b18 : now - 0.5f * (1.f - 1.f / m);
}
// Shotgun/pipebomb: the attack ends at anim end +0xB2C (skmod clock); shorten what is left of it.
inline float weap_anim_end(float end, float now, float m) {
  if (m == 1.f || end <= now) return end;
  return now + (end - now) / m;
}
// Pipebomb: never before its launch event (id 0) is due, or the throw ends with no bomb;
// never after stock either.
inline float weap_pbomb_end(float end, float now, float m, float launchDue) {
  if (m == 1.f) return end;
  float r = weap_anim_end(end, now, m);
  if (r < launchDue) r = launchDue;
  if (r > end) r = end;
  return r;
}
// Golden Eagle reload lock +0xBF8 on the reload's rising edge: +0xC30 / r, 0 at Max (section 4).
inline float weap_reload_bf8(float bf8, float reloadTime, int reloadPct) {
  const int r = weap_reload_pct_clamped(reloadPct);
  if (r == WEAP_FIRE_MIN) return bf8;
  if (r == WEAP_RELOAD_SLIDER_MAX) return 0.f;
  return reloadTime / ((float)r / 100.f);
}
// Assault Rifle: the fire logic fires ftol(+0xC28 * elapsed) rounds once elapsed >= +0xCE8, so both
// fields scale together; a scaled interval with stock rps rounds to 0 and the engine takes its
// no-ammo branch (weapon switch). rps (int, parse-time) := stock * m, rounded, at least 1.
inline int weap_arifle_rps(int stock, float m) {
  if (m == 1.f) return stock;
  const int r = (int)((float)stock * m + 0.5f);
  return r < 1 ? 1 : r;
}
// +0xCE8 := 1/rps, raised by an ulp until rps * interval >= 1: the gate's elapsed is then always worth
// at least one round. int * float is exact in double, and rounding is monotone at any x87 precision.
inline float weap_arifle_interval_for(int rps) {
  if (rps < 1) rps = 1;
  float iv = 1.f / (float)rps;
  for (int i = 0; i < 4 && (double)rps * (double)iv < 1.0; ++i) {
    unsigned b;
    memcpy(&b, &iv, sizeof b);
    ++b;
    memcpy(&iv, &b, sizeof iv);
  }
  return iv;
}

// Polled edge detectors. The first sample after a (re)prime only records, so a new
// weapon's first frame is never a shot; weap_note records our own write.
struct WeapWatchF { int primed; float prev; };
inline int weap_changed(WeapWatchF* w, float cur) {
  const int hit = w->primed && cur != w->prev;
  w->primed = 1; w->prev = cur;
  return hit;
}
inline int weap_rose(WeapWatchF* w, float cur) {
  const int hit = w->primed && cur > w->prev;
  w->primed = 1; w->prev = cur;
  return hit;
}
inline void weap_note(WeapWatchF* w, float written) { w->primed = 1; w->prev = written; }
struct WeapWatchI { int primed; int prev; };
inline int weap_entered(WeapWatchI* w, int cur, int state) {
  const int hit = w->primed && w->prev != state && cur == state;
  w->primed = 1; w->prev = cur;
  return hit;
}
inline int weap_rising(WeapWatchI* w, int cur) {
  const int hit = w->primed && !w->prev && cur;
  w->primed = 1; w->prev = cur;
  return hit;
}

// A parse-time field held at a target while wanted, its stock put back after. Never
// applied without a captured stock (a zero is never one), and captured only while not
// applied, so our own write can never become the stock.
struct WeapHold { int captured; int applied; float stock; };
enum { WH_NONE = 0, WH_WRITE = 1 };
inline void weap_hold_capture(WeapHold* h, float cur) {
  if (h->applied || cur == 0.f) return;
  h->captured = 1; h->stock = cur;
}
inline int weap_hold_step(WeapHold* h, int want, float cur, float target, float* out) {
  if (want && h->captured) {
    h->applied = 1;
    if (cur == target) return WH_NONE;
    *out = target;
    return WH_WRITE;
  }
  if (!want && h->applied) {
    h->applied = 0;
    if (cur == h->stock) return WH_NONE;
    *out = h->stock;
    return WH_WRITE;
  }
  return WH_NONE;
}
// The same hold for an int field (the rifle's rounds_per_sec +0xC28).
struct WeapHoldI { int captured; int applied; int stock; };
inline void weap_holdi_capture(WeapHoldI* h, int cur) {
  if (h->applied || cur <= 0) return;
  h->captured = 1; h->stock = cur;
}
inline int weap_holdi_step(WeapHoldI* h, int want, int cur, int target, int* out) {
  if (want && h->captured) {
    h->applied = 1;
    if (cur == target) return WH_NONE;
    *out = target;
    return WH_WRITE;
  }
  if (!want && h->applied) {
    h->applied = 0;
    if (cur == h->stock) return WH_NONE;
    *out = h->stock;
    return WH_WRITE;
  }
  return WH_NONE;
}

// --- The glue (weapons.cpp), main thread, frame path ------------------------
// Once a frame before the draws; `player` is probe.cpp's validated player or NULL.
void weapons_frame(const unsigned char* player);
// The DIAGNOSTICS row: the held class and the cumulative counters.
const char* weapons_held_name(void);
unsigned weapons_shots(void);
unsigned weapons_levers(void);
unsigned weapons_reloads(void);
// This frame's held weapon as weapons_frame identified it (readable for its class span), or NULL
// for none or an unknown class; *kind gets the WK_* value either way.
unsigned char* weapons_held(const void** vt, int* kind);
