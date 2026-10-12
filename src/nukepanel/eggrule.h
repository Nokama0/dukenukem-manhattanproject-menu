#pragma once
// Roach eggs (class roach_pod): when one is a target, and its label (docs/re/entities.md section 8). An egg hatches
// only through its "hatch" input pin, which the map wires from an area_switch or a timer; an egg no wire names never
// hatches, so it is drawn as inactive and never targeted.
#include <string.h>

enum { EGG_NONE = 0, EGG_ACTIVE, EGG_INACTIVE, EGG_UNREAD, EGG_GONE };
// The roach_pod ctor duke!0x10072280 stores +0xC4C = 25.0 and registers no property that changes it.
const float EGG_HEALTH_MAX = 25.f;
// A wire's target is at most 0x3F characters (pin_t::connect p3shared!0x1001bd40).
enum { EGG_NAME_LEN = 64 };

struct EggFacts {
  int hatched;    // byte +0xC48: set by the hatch 0x100727e0, the signal handler 0x10072680 tests it first
  int intact;     // byte +0xAF0: 1 from actor_model_ctor 0x1008f170, cleared by the egg's death 0x100727bd
  int solid;      // byte +0x2E0: cleared by the gib 0x100728b0, which also queues the removal (+0xC58)
  float health;   // +0xC4C, lowered by the egg's vt+0x198 0x10072770
  int wired;      // a wire names "<this egg>.hatch": 1, none: 0, the scan failed: -1
};

// Gone first, in the signal handler's own order (0x10072680 tests +0xC48 before the pin): hatched, dead or
// gibbed. Then the map's wiring decides: only a wired egg can ever hatch.
inline int egg_state(const EggFacts& f) {
  if (f.hatched || !f.intact || !f.solid || !(f.health > 0.f)) return EGG_GONE;
  if (f.wired > 0) return EGG_ACTIVE;
  return f.wired == 0 ? EGG_INACTIVE : EGG_UNREAD;
}
// What silent aim and both Triggerbot modes may take: a live wired egg only.
inline int egg_targetable(int state) { return state == EGG_ACTIVE ? 1 : 0; }
inline const char* egg_label(int state) {
  if (state == EGG_INACTIVE) return "Roach Egg (inactive)";
  if (state == EGG_UNREAD) return "Roach Egg (unread)";
  return "Roach Egg";
}

// A wire's target "<entity>.<pin>" (pin_t::connect stores what follows the ':' of a world.scr `connect` line,
// prism3d!0x402066): 1 and the entity name when the pin is "hatch" and the name fits in cap.
inline int egg_wire_target(const char* wire, char* name, size_t cap) {
  static const char kPin[] = ".hatch";
  const size_t k = sizeof kPin - 1;
  if (!wire || !name || !cap) return 0;
  const size_t n = strlen(wire);
  if (n <= k || strcmp(wire + n - k, kPin) != 0 || n - k >= cap) return 0;
  memcpy(name, wire, n - k);
  name[n - k] = 0;
  return 1;
}
// The level the wire table belongs to: the player and his world (+0xE0). A player allocation reused by the next
// level still comes with a new world.
struct EggLevelKey { const void* player; const void* world; };
inline bool egg_level_same(const EggLevelKey& a, const EggLevelKey& b) {
  return a.player == b.player && a.world == b.world;
}

// Each egg's wired verdict, kept per entity until the table is rebuilt or the level changes, so the name compare
// runs once per egg, not every frame.
enum { EGG_CACHE_MAX = 256 };
struct EggVerdictCache { const void* ent[EGG_CACHE_MAX]; unsigned char wired[EGG_CACHE_MAX]; int n; unsigned full; };
inline void egg_cache_clear(EggVerdictCache* c) { c->n = 0; }
// 1 and *wired when ent has a verdict.
inline int egg_cache_get(const EggVerdictCache* c, const void* ent, int* wired) {
  for (int i = 0; i < c->n; ++i)
    if (c->ent[i] == ent) { *wired = c->wired[i]; return 1; }
  return 0;
}
// Past EGG_CACHE_MAX the verdict is not kept (counted in full); the caller compares names for that egg each frame.
inline void egg_cache_put(EggVerdictCache* c, const void* ent, int wired) {
  if (c->n >= EGG_CACHE_MAX) { c->full++; return; }
  c->ent[c->n] = ent;
  c->wired[c->n++] = (unsigned char)(wired ? 1 : 0);
}

// Whether eggName is among the n wired names (the scan's table). Case-blind: how the delivery resolves a
// wire's name was not read (open in docs/re/entities.md section 8), and the maps use one case throughout.
inline int egg_wired(const char (*names)[EGG_NAME_LEN], int n, const char* eggName) {
  if (!names || !eggName) return 0;
  for (int i = 0; i < n; ++i) if (_stricmp(names[i], eggName) == 0) return 1;
  return 0;
}
