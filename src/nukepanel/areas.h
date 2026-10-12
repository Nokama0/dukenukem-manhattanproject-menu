#pragma once
// Cross-area secrets (evidence re/areas-and-secrets.md): a part's areas, doorways, secrets and nukes from each area's
// world.scr, which are found (loaded worlds from memory, unloaded ones from state/<map>.msv; nukes also from our own
// record and the live tally) and the doorway route between areas as text. Pure rules; areas.cpp reads engine and files.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "carryrule.h"
#include "laserrule.h"
#include "breakrule.h"

enum { AR_AREAS_MAX = 8, AR_DOORS_MAX = 16, AR_SECRETS_MAX = 8, AR_RUNS_MAX = 32, AR_NAME = 32, AR_HOPS_MAX = 8,
       AR_ROUTE_CAP = 160, AR_NUKES_MAX = 16 };

// --- The part's areas: every eNNpNN_aNN[_rNN] map that has a world.scr (research dump, area_graph.py) ------
struct ArPartMaps { const char* prefix; const char* suffixes; };
static const ArPartMaps kArParts[] = {
  { "e01p01", "a01 a01_r01 a02 a02_r01" }, { "e01p02", "a01 a01_r01 a02" }, { "e01p03", "a01 a01_r01 a02" },
  { "e02p01", "a01 a02 a03" }, { "e02p02", "a01 a02 a02_r01 a03 a03_r01" }, { "e02p03", "a01 a01_r01 a02" },
  { "e03p01", "a01 a01_r01 a02 a02_r01 a03 a03_r01" }, { "e03p02", "a01 a02 a02_r01" },
  { "e03p03", "a01 a02 a02_r01 a03 a03_r01" }, { "e04p01", "a01 a02 a03" }, { "e04p02", "a01 a01_r01 a02 a03 a03_r01" },
  { "e04p03", "a01 a02" }, { "e05p01", "a01 a01_r01 a02 a02_r01" }, { "e05p02", "a01 a01_r01 a02" },
  { "e05p03", "a01 a02" }, { "e06p01", "a01 a02 a03 a04" }, { "e06p02", "a01 a02 a02_r01" }, { "e06p03", "a01 a02" },
  { "e07p01", "a01 a02 a03" }, { "e07p02", "a01 a02" }, { "e07p03", "a01 a02" }, { "e08p01", "a01 a02 a02_r01" },
  { "e08p02", "a01 a01_r01 a02 a03 a04 a05 a06" }, { "e08p03", "a01 a02 a03 a04" },
};

// The part prefix ("e05p02") of an area map ("e05p02_a01_r01"); false for anything not in the table (the
// stats maps, the skies, the intro).
inline bool ar_map_part(const char* map, char prefix[8]) {
  if (!map || strlen(map) < 10 || map[6] != '_') return false;
  for (size_t i = 0; i < sizeof kArParts / sizeof kArParts[0]; ++i)
    if (_strnicmp(map, kArParts[i].prefix, 6) == 0) {
      memcpy(prefix, kArParts[i].prefix, 6);
      prefix[6] = 0;
      return true;
    }
  return false;
}

// The part's map names in the table's order; the count (0 for an unknown prefix).
inline int ar_part_maps(const char* prefix, char out[][AR_NAME], int cap) {
  for (size_t i = 0; i < sizeof kArParts / sizeof kArParts[0]; ++i) {
    if (strcmp(prefix, kArParts[i].prefix) != 0) continue;
    int n = 0;
    for (const char* s = kArParts[i].suffixes; *s && n < cap;) {
      int k = 0;
      while (s[k] && s[k] != ' ') ++k;
      _snprintf_s(out[n++], AR_NAME, _TRUNCATE, "%s_%.*s", prefix, k, s);
      s += k;
      while (*s == ' ') ++s;
    }
    return n;
  }
  return 0;
}

// --- One area's world.scr: `spawn <class> {` opens an entity, `}` closes it, `key:value` goes to the innermost one.

// Kept: map_change (doorways), area_secret, game_track_auto (depth runs, their route ends) and the nukes: `nuke`
// pickups, crates and barrels whose `contents` hold code 0, enemies whose `spawnscript` names nuke.sc, lasers whose
// `obstructed` pin fires a nuke-carrying spawner (one source per nuke; re/pickups.md 5-7). Other spawners: not counted.
struct ArDoor {
  char name[AR_NAME], tmap[AR_NAME], tdest[AR_NAME], run[AR_NAME];
  float pos[3], vol[6];
  int changeType, hasType, volOk;
  int target, landing, pair;   // ar_link: the target area and its landing doorway (-1 none), the way back exists
};
struct ArSecret { char name[AR_NAME]; float pos[3], vol[6]; int volOk; };
struct ArRun { char name[AR_NAME]; float a[3], b[3]; };
// A nuke source: a pickup (count 1), a crate or barrel holding `count` nuke codes (holder values match craterule.h's
// HOLDER_*), or one nuke an enemy carries (`slot` its place among its nukes, cls its class). A laser: name the laser,
// cls its spawner, count the nukes and slot the entities one fire spawns. AR_NK_SPAWNED (live only): laser-spawned.
enum { AR_NK_PICKUP = 0, AR_NK_CRATE = 1, AR_NK_BARREL = 2, AR_NK_ENEMY = 3, AR_NK_LASER = 4, AR_NK_SPAWNED = 5 };
struct ArNuke { char name[AR_NAME], cls[AR_NAME]; float pos[3]; int holder, count, slot; };
// A breakable that hands out a pickup when broken (re/pickups.md section 8): name the breakable, spawner the one its
// `broken` pin drives, kind/subtype the pickup (breakrule.h).
struct ArBreak { char name[AR_NAME], spawner[AR_NAME]; unsigned char kind; int subtype; };
enum { AR_BREAKS_MAX = 8 };
// An ambush (re/kills-and-spawners.md s. 3-4): the monster spawners one entity's pin (trig) fires. zone: the
// area_switch whose volume starts it, directly or via its timer, with position and offline volume (read as a
// doorway's); a laser fires it again. Census: at most 7 ambushes in an area, 6 spawners on one trigger, 5 wired eggs.
enum { AR_AMBUSH_MAX = 12, AR_AMB_SP = 8, AR_AMB_KINDS = 4, AR_EGGS_MAX = 8, AR_CLS = 24 };
enum { AR_TRIG_OTHER = 0, AR_TRIG_ZONE, AR_TRIG_TIMER, AR_TRIG_LASER, AR_TRIG_BREAKABLE, AR_TRIG_PICKUP, AR_TRIG_KILL };
struct ArAmbush {
  char trig[AR_NAME], trigCls[AR_NAME], zone[AR_NAME];
  int kind;                       // AR_TRIG_*
  float zonePos[3], vol[6];
  int volOk;
  int nsp, monsters;              // its spawners; the monsters one fire spawns
  char sp[AR_AMB_SP][AR_NAME];
  float spPos[AR_AMB_SP][3];
  int spScripts[AR_AMB_SP];       // entities per fire of each spawner (laser_fires)
  int nmon, monN[AR_AMB_KINDS];
  char mon[AR_AMB_KINDS][AR_CLS]; // per fire, from spawnscript lines under scripts/monst/ ("monst_"+name until read)
  int ndrop;
  const char* drop[AR_AMB_KINDS]; // distinct labels of what they carry (monst_spawnscr); static (item_label, "Nuke")
};
struct ArArea {
  char map[AR_NAME];
  int parsed, ndoors, nsecrets, nruns, dropped;   // dropped: entities past a cap
  ArDoor doors[AR_DOORS_MAX];
  ArSecret secrets[AR_SECRETS_MAX];
  ArRun runs[AR_RUNS_MAX];
  int nnukes, nukeTotal;                          // sources, and the nukes they give
  ArNuke nukes[AR_NUKES_MAX];
  int nbreaks;
  ArBreak breaks[AR_BREAKS_MAX];
  // Kills (re/kills-and-spawners.md section 1): placed monst_* (bosses never count), the monsters one fire of each
  // wired monster spawner gives, the eggs a `.hatch` wire reaches (their Roach counts); killTotal their sum.
  int placed, spawned, neggs, killTotal, nambush;
  char eggs[AR_EGGS_MAX][AR_NAME];
  ArAmbush ambush[AR_AMBUSH_MAX];
};
struct ArPart { char prefix[8]; int nareas; ArArea areas[AR_AREAS_MAX]; };

enum { AR_K_OTHER = 0, AR_K_DOOR, AR_K_SECRET, AR_K_RUN, AR_K_NUKE, AR_K_CRATE, AR_K_BARREL, AR_K_ENEMY, AR_K_LASER,
       AR_K_SPAWNER, AR_K_BREAKABLE, AR_K_EGG };
// AR_LASER_TARGETS: e01p01_a01 brk_explodbox has 6 `.active` pins, the most in any world.scr (census).
enum { AR_LASER_TARGETS = 8, AR_LASERS_MAX = 16, AR_SPAWNERS_MAX = 64 };
struct ArPending {
  int kind, hasPos, changeType, hasType, nrp, zeros;   // zeros: `contents:0` lines (a holder's nuke codes)
  int drops;                                           // an enemy's `spawnscript` lines naming nuke.sc
  char name[AR_NAME], tmap[AR_NAME], tdest[AR_NAME], cls[AR_NAME];
  float pos[3], rp0[3], rp1[3];
  // A laser: the entities its `obstructed` pin sets `active` on. A spawner: its monsters' nuke lines (monst_spawnscr),
  // and its `spawnscript` lines that are monsters and that are nukes.
  char targets[AR_LASER_TARGETS][AR_NAME];
  int ntargets, mnukes, monsters, own;
  int hasPick, pickKind, pickSub;   // a spawner: its first `spawnscript` that is a pickup (breakrule.h)
  int src;                          // its index among the file's wire sources, -1 while it has no wire
  int switchType;                   // an area_switch's switch_type
  int nmon, monN[AR_AMB_KINDS];     // a spawner: its monster classes (from the script names) and their counts
  char mon[AR_AMB_KINDS][AR_CLS];
  int ndrop;
  const char* drop[AR_AMB_KINDS];   // a spawner: the distinct labels of its monst_spawnscr lines
};
// The lasers and spawners of one file, matched once it is read (the spawners come after the lasers).
struct ArLaserTmp { char name[AR_NAME], targets[AR_LASER_TARGETS][AR_NAME]; float pos[3]; int ntargets; };
struct ArSpawnerTmp {
  char name[AR_NAME];
  int nukes, perFire, hasPick, pickKind, pickSub;
  float pos[3];
  int mons, nmon, monN[AR_AMB_KINDS], ndrop;
  char mon[AR_AMB_KINDS][AR_CLS];
  const char* drop[AR_AMB_KINDS];
};
// Every `connect <pin>:<target>.active` or `.hatch` line of a file (sources with up to 56 wires exist, so they are
// kept flat), and the entities they leave from (at most 107 in a file, by census).
enum { AR_WIRES_MAX = 1024, AR_SRCS_MAX = 256, AR_EGG_NAMES_MAX = 64 };
struct ArWireTmp { int src, hatch; char target[AR_NAME]; };
struct ArSrcTmp { char name[AR_NAME], cls[AR_NAME]; float pos[3]; int switchType; };

// A spawnscript under scripts/monst/ spawns a monster (every one of the 111 monster spawners' scripts lives there, and
// no other script does: by census); cls gets "monst_" + its file name, the class until the glue reads it.
inline bool ar_monst_script(const char* sc, char* cls, int cap) {
  const char* d = sc ? strstr(sc, "/monst/") : 0;
  if (!d) return false;
  const char* base = d + 7;
  if (strchr(base, '/')) return false;
  const char* dot = strrchr(base, '.');
  const int n = dot ? (int)(dot - base) : (int)strlen(base);
  if (n <= 0) return false;
  _snprintf_s(cls, (size_t)cap, _TRUNCATE, "monst_%.*s", n, base);
  return true;
}

// What a monster's drop script hands out, as the item ESP names it; 0 for a script it does not know.
inline const char* ar_drop_label(const char* sc) {
  if (carry_is_nuke_script(sc)) return "Nuke";
  unsigned char k;
  int sub;
  return brk_script_content(sc, &k, &sub) ? item_label(k, sub, 0) : 0;
}

// The first class a script spawns ("spawn monst_gator {" in scripts/monst/gator2.sc); false when it spawns none.
inline bool ar_script_class(const char* text, int len, char* cls, int cap) {
  for (int i = 0; i + 6 < len; ++i) {
    if ((i && text[i - 1] != '\n' && text[i - 1] != ' ' && text[i - 1] != '\t') || strncmp(text + i, "spawn ", 6) != 0)
      continue;
    int s = i + 6, e;
    while (s < len && (text[s] == ' ' || text[s] == '\t')) ++s;
    for (e = s; e < len && text[e] != ' ' && text[e] != '\t' && text[e] != '{' && text[e] != '\r' && text[e] != '\n';) ++e;
    if (e == s) continue;
    _snprintf_s(cls, (size_t)cap, _TRUNCATE, "%.*s", e - s, text + s);
    return true;
  }
  return false;
}

// The kind of an ambush's trigger, by the class of the entity whose pin fires it.
inline int ar_trig_kind(const char* cls) {
  if (!strcmp(cls, "area_switch")) return AR_TRIG_ZONE;
  if (!strcmp(cls, "timer") || !strcmp(cls, "trigger_timer")) return AR_TRIG_TIMER;
  if (!strcmp(cls, "laser")) return AR_TRIG_LASER;
  if (!strcmp(cls, "breakable_adv")) return AR_TRIG_BREAKABLE;
  if (!strncmp(cls, "monst_", 6)) return AR_TRIG_KILL;
  static const char* const kPick[] = { "nuke", "life", "ego", "key", "ammo", "gun", "jetpack", "forcefield" };
  for (size_t i = 0; i < sizeof kPick / sizeof kPick[0]; ++i)
    if (!strcmp(cls, kPick[i])) return AR_TRIG_PICKUP;
  return AR_TRIG_OTHER;
}

inline bool ar_vec(const char* v, float out[3]) {
  while (*v == ' ' || *v == '\t') ++v;
  if (*v == '<') ++v;
  return sscanf_s(v, "%f %f %f", &out[0], &out[1], &out[2]) == 3;
}

// A value as a string: blanks and quotes trimmed.
inline void ar_str(const char* v, char* out, int cap) {
  while (*v == ' ' || *v == '\t') ++v;
  int n = (int)strlen(v);
  while (n > 0 && (v[n - 1] == ' ' || v[n - 1] == '\t')) --n;
  if (n >= 2 && v[0] == '"' && v[n - 1] == '"') { ++v; n -= 2; }
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "%.*s", n, v);
}

inline void ar_emit(ArArea* a, const ArPending& e) {
  if (e.kind == AR_K_DOOR) {
    if (a->ndoors >= AR_DOORS_MAX) { a->dropped++; return; }
    ArDoor& d = a->doors[a->ndoors++];
    memset(&d, 0, sizeof d);
    strcpy_s(d.name, sizeof d.name, e.name);
    strcpy_s(d.tmap, sizeof d.tmap, e.tmap);
    strcpy_s(d.tdest, sizeof d.tdest, e.tdest);
    memcpy(d.pos, e.pos, sizeof d.pos);
    d.changeType = e.changeType;
    d.hasType = e.hasType;
    d.target = d.landing = -1;
  } else if (e.kind == AR_K_SECRET) {
    if (a->nsecrets >= AR_SECRETS_MAX) { a->dropped++; return; }
    ArSecret& s = a->secrets[a->nsecrets++];
    memset(&s, 0, sizeof s);
    strcpy_s(s.name, sizeof s.name, e.name);
    memcpy(s.pos, e.pos, sizeof s.pos);
  } else if (e.kind == AR_K_NUKE || ((e.kind == AR_K_CRATE || e.kind == AR_K_BARREL) && e.zeros > 0)) {
    if (a->nnukes >= AR_NUKES_MAX) { a->dropped++; return; }
    ArNuke& k = a->nukes[a->nnukes++];
    memset(&k, 0, sizeof k);
    strcpy_s(k.name, sizeof k.name, e.name);
    memcpy(k.pos, e.pos, sizeof k.pos);
    k.holder = e.kind == AR_K_CRATE ? AR_NK_CRATE : e.kind == AR_K_BARREL ? AR_NK_BARREL : AR_NK_PICKUP;
    k.count = k.holder == AR_NK_PICKUP ? 1 : e.zeros;
    a->nukeTotal += k.count;
  } else if (e.kind == AR_K_ENEMY) {
    for (int k = 0; k < e.drops; ++k) {
      if (a->nnukes >= AR_NUKES_MAX) { a->dropped++; continue; }
      ArNuke& n = a->nukes[a->nnukes++];
      memset(&n, 0, sizeof n);
      strcpy_s(n.name, sizeof n.name, e.name);
      strcpy_s(n.cls, sizeof n.cls, e.cls);
      memcpy(n.pos, e.pos, sizeof n.pos);
      n.holder = AR_NK_ENEMY;
      n.count = 1;
      n.slot = k;
      a->nukeTotal++;
    }
  } else if (e.kind == AR_K_RUN && e.nrp > 0) {
    if (a->nruns >= AR_RUNS_MAX) { a->dropped++; return; }
    ArRun& r = a->runs[a->nruns++];
    strcpy_s(r.name, sizeof r.name, e.name);
    memcpy(r.a, e.rp0, sizeof r.a);
    memcpy(r.b, e.rp1, sizeof r.b);
  }
}

// The kept entities of the text into a (its map name is the caller's); false when no spawn was seen.
inline bool ar_parse_world(const char* text, int len, ArArea* a) {
  enum { kDepth = 16 };
  static ArPending stack[kDepth];   // static: the suite and the glue parse on one thread
  static ArLaserTmp lasers[AR_LASERS_MAX];
  static ArSpawnerTmp spawners[AR_SPAWNERS_MAX];
  static ArLaserTmp brks[AR_LASERS_MAX];   // breakables whose `broken` pin has a target, kept like the lasers
  static ArWireTmp wires[AR_WIRES_MAX];
  static ArSrcTmp srcs[AR_SRCS_MAX];
  static char eggs[AR_EGG_NAMES_MAX][AR_NAME];
  int nlasers = 0, nspawners = 0, nbrks = 0, nwires = 0, nsrcs = 0, neggs = 0;
  int depth = 0, spawns = 0;
  a->ndoors = a->nsecrets = a->nruns = a->dropped = a->nnukes = a->nukeTotal = a->nbreaks = 0;
  a->placed = a->spawned = a->neggs = a->killTotal = a->nambush = 0;
  for (int i = 0; i < len;) {
    int j = i;
    while (j < len && text[j] != '\n') ++j;
    char line[256];
    int n = j - i < (int)sizeof line - 1 ? j - i : (int)sizeof line - 1;
    memcpy(line, text + i, (size_t)n);
    line[n] = 0;
    i = j + 1;
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = 0;
    char* s = line;
    while (*s == ' ' || *s == '\t') ++s;
    if (strncmp(s, "spawn ", 6) == 0) {
      spawns++;
      char cls[48] = { 0 };
      sscanf_s(s + 6, "%47s", cls, (unsigned)sizeof cls);
      if (depth < kDepth) {
        ArPending& e = stack[depth];
        memset(&e, 0, sizeof e);
        e.kind = strcmp(cls, "map_change") == 0 ? AR_K_DOOR : strcmp(cls, "area_secret") == 0 ? AR_K_SECRET
               : strcmp(cls, "game_track_auto") == 0 ? AR_K_RUN : strcmp(cls, "nuke") == 0 ? AR_K_NUKE
               : strcmp(cls, "crate") == 0 ? AR_K_CRATE : strcmp(cls, "barrel") == 0 ? AR_K_BARREL
               : strcmp(cls, "breakable_adv") == 0 ? AR_K_BREAKABLE : strcmp(cls, "laser") == 0 ? AR_K_LASER : strcmp(cls, "spawner_entity") == 0 ? AR_K_SPAWNER
               : carry_class(cls) ? AR_K_ENEMY : strcmp(cls, "roach_pod") == 0 ? AR_K_EGG : AR_K_OTHER;
        _snprintf_s(e.cls, sizeof e.cls, _TRUNCATE, "%s", cls);
        e.src = -1;
      }
      if (strncmp(cls, "monst_", 6) == 0) a->placed++;   // bosses never count (kills-and-spawners.md section 1)
      depth++;
      continue;
    }
    if (strcmp(s, "}") == 0) {
      if (depth > 0) {
        depth--;
        if (depth < kDepth) {
          const ArPending& e = stack[depth];
          float at[3];   // its world position: a child sits at its parents' positions plus its own
          for (int k = 0; k < 3; ++k) {
            at[k] = e.pos[k];
            for (int d = 0; d < depth; ++d) at[k] += stack[d].pos[k];
          }
          if (e.src >= 0) {
            ArSrcTmp& w = srcs[e.src];
            memcpy(w.name, e.name, sizeof w.name);
            memcpy(w.cls, e.cls, sizeof w.cls);
            memcpy(w.pos, at, sizeof w.pos);
            w.switchType = e.switchType;
          }
          if (e.kind == AR_K_EGG) {
            if (neggs < AR_EGG_NAMES_MAX) memcpy(eggs[neggs++], e.name, AR_NAME);
            else a->dropped++;
            continue;
          }
          if (e.kind == AR_K_LASER) {
            if (!e.ntargets) continue;
            if (nlasers >= AR_LASERS_MAX) { a->dropped++; continue; }
            ArLaserTmp& l = lasers[nlasers++];
            memcpy(l.name, e.name, sizeof l.name);
            memcpy(l.targets, e.targets, sizeof l.targets);
            l.ntargets = e.ntargets;
            for (int k = 0; k < 3; ++k) {   // a child (a pod's laser) sits at its parents' positions plus its own
              l.pos[k] = e.pos[k];
              for (int d = 0; d < depth; ++d) l.pos[k] += stack[d].pos[k];
            }
          } else if (e.kind == AR_K_BREAKABLE) {
            if (!e.ntargets) continue;
            if (nbrks >= AR_LASERS_MAX) { a->dropped++; continue; }
            ArLaserTmp& b = brks[nbrks++];
            memcpy(b.name, e.name, sizeof b.name);
            memcpy(b.targets, e.targets, sizeof b.targets);
            b.ntargets = e.ntargets;
          } else if (e.kind == AR_K_SPAWNER) {
            if (nspawners >= AR_SPAWNERS_MAX) { a->dropped++; continue; }
            ArSpawnerTmp& sp = spawners[nspawners++];
            memcpy(sp.name, e.name, sizeof sp.name);
            sp.nukes = e.mnukes * e.monsters + e.own;
            sp.perFire = e.monsters + e.own;
            sp.hasPick = e.hasPick; sp.pickKind = e.pickKind; sp.pickSub = e.pickSub;
            memcpy(sp.pos, at, sizeof sp.pos);
            sp.mons = 0;
            for (int k = 0; k < e.nmon; ++k) sp.mons += e.monN[k];
            sp.nmon = e.nmon;
            memcpy(sp.monN, e.monN, sizeof sp.monN);
            memcpy(sp.mon, e.mon, sizeof sp.mon);
            sp.ndrop = e.ndrop;
            memcpy(sp.drop, e.drop, sizeof sp.drop);
          } else {
            ar_emit(a, e);
          }
        }
      }
      continue;
    }
    if (depth <= 0 || depth > kDepth) continue;
    ArPending& e = stack[depth - 1];
    char* colon = strchr(s, ':');
    if (!colon) continue;
    *colon = 0;
    const char* key = s;
    const char* v = colon + 1;
    // Any entity's `.active` and `.hatch` wires: what fires a spawner (FUN_100b5b80) and hatches an egg.
    if (strncmp(key, "connect ", 8) == 0) {
      char t[2 * AR_NAME];
      ar_str(v, t, sizeof t);
      char* dot = strrchr(t, '.');
      const int hatch = dot && strcmp(dot + 1, "hatch") == 0;
      if (dot && (hatch || strcmp(dot + 1, "active") == 0)) {
        if (e.src < 0 && nsrcs < AR_SRCS_MAX) {
          e.src = nsrcs++;
          memset(&srcs[e.src], 0, sizeof srcs[e.src]);
        }
        if (e.src < 0 || nwires >= AR_WIRES_MAX) {
          a->dropped++;
        } else {
          ArWireTmp& w = wires[nwires++];
          w.src = e.src;
          w.hatch = hatch;
          _snprintf_s(w.target, sizeof w.target, _TRUNCATE, "%.*s", (int)(dot - t), t);
        }
      }
    }
    if (e.kind == AR_K_OTHER || e.kind == AR_K_EGG) {   // its position (for its children's), name and switch type
      if (strcmp(key, "position") == 0) e.hasPos = ar_vec(v, e.pos);
      else if (strcmp(key, "name") == 0) ar_str(v, e.name, AR_NAME);
      else if (strcmp(key, "switch_type") == 0) e.switchType = atoi(v);
      continue;
    }
    if (strcmp(key, "name") == 0) ar_str(v, e.name, AR_NAME);
    else if (strcmp(key, "position") == 0) e.hasPos = ar_vec(v, e.pos);
    else if (strcmp(key, "change_type") == 0) { e.changeType = atoi(v); e.hasType = 1; }
    else if (strcmp(key, "target_map") == 0) ar_str(v, e.tmap, AR_NAME);
    else if (strcmp(key, "target_dest") == 0) ar_str(v, e.tdest, AR_NAME);
    else if (strcmp(key, "contents") == 0) {
      while (*v == ' ' || *v == '\t') ++v;
      if (*v >= '0' && *v <= '9' && atoi(v) == 0) e.zeros++;
    }
    else if (strcmp(key, "spawnscript") == 0 && e.kind == AR_K_ENEMY) {
      char sc[CARRY_SCRIPT_LEN];
      ar_str(v, sc, sizeof sc);
      if (carry_is_nuke_script(sc)) e.drops++;
    }
    else if (e.kind == AR_K_SPAWNER && (strcmp(key, "spawnscript") == 0 || strcmp(key, "monst_spawnscr") == 0)) {
      char sc[CARRY_SCRIPT_LEN];
      ar_str(v, sc, sizeof sc);
      const bool nuke = carry_is_nuke_script(sc);
      unsigned char pk; int ps;
      if (key[0] != 'm' && !e.hasPick && brk_script_content(sc, &pk, &ps)) { e.hasPick = 1; e.pickKind = pk; e.pickSub = ps; }
      if (key[0] == 'm') e.mnukes += nuke;
      else if (nuke) e.own++;
      else e.monsters++;
      char mc[AR_CLS];
      if (key[0] == 'm') {
        const char* d = ar_drop_label(sc);
        bool seen = !d;
        for (int k = 0; k < e.ndrop && !seen; ++k) seen = strcmp(e.drop[k], d) == 0;
        if (!seen && e.ndrop < AR_AMB_KINDS) e.drop[e.ndrop++] = d;
      } else if (ar_monst_script(sc, mc, sizeof mc)) {
        int k = 0;
        while (k < e.nmon && strcmp(e.mon[k], mc) != 0) ++k;
        if (k < e.nmon) e.monN[k]++;
        else if (e.nmon < AR_AMB_KINDS) { memcpy(e.mon[e.nmon], mc, sizeof mc); e.monN[e.nmon++] = 1; }
      }
    }
    else if ((e.kind == AR_K_LASER && strcmp(key, "connect obstructed") == 0) ||
             (e.kind == AR_K_BREAKABLE && strcmp(key, "connect broken") == 0)) {
      // "spawner_entity3.active": a spawner fires on its `active` input (FUN_100b5b80).
      char t[2 * AR_NAME];
      ar_str(v, t, sizeof t);
      char* dot = strrchr(t, '.');
      if (dot && strcmp(dot + 1, "active") == 0 && e.ntargets < AR_LASER_TARGETS) {
        *dot = 0;
        _snprintf_s(e.targets[e.ntargets++], AR_NAME, _TRUNCATE, "%s", t);
      }
    }
    else if (strcmp(key, "route_point") == 0) {
      float p[3];
      if (ar_vec(v, p)) {
        if (!e.nrp) memcpy(e.rp0, p, sizeof p);
        memcpy(e.rp1, p, sizeof p);
        e.nrp++;
      }
    }
  }
  // One source per (laser, spawner) whose fire spawns a nuke, after the file's other sources.
  for (int i = 0; i < nlasers; ++i)
    for (int t = 0; t < lasers[i].ntargets; ++t)
      for (int j = 0; j < nspawners; ++j) {
        if (strcmp(spawners[j].name, lasers[i].targets[t]) != 0 || spawners[j].nukes <= 0) continue;
        if (a->nnukes >= AR_NUKES_MAX) { a->dropped++; break; }
        ArNuke& k = a->nukes[a->nnukes++];
        memset(&k, 0, sizeof k);
        strcpy_s(k.name, sizeof k.name, lasers[i].name);
        strcpy_s(k.cls, sizeof k.cls, spawners[j].name);
        memcpy(k.pos, lasers[i].pos, sizeof k.pos);
        k.holder = AR_NK_LASER;
        k.count = spawners[j].nukes;
        k.slot = spawners[j].perFire;
        a->nukeTotal += k.count;
        break;
      }
  // One entry per (breakable, spawner) whose spawner's own script is a pickup; the breakable stays where it is.
  for (int i = 0; i < nbrks; ++i)
    for (int t = 0; t < brks[i].ntargets; ++t)
      for (int j = 0; j < nspawners; ++j) {
        if (strcmp(spawners[j].name, brks[i].targets[t]) != 0 || !spawners[j].hasPick) continue;
        if (a->nbreaks >= AR_BREAKS_MAX) { a->dropped++; break; }
        ArBreak& b = a->breaks[a->nbreaks++];
        memset(&b, 0, sizeof b);
        strcpy_s(b.name, sizeof b.name, brks[i].name);
        strcpy_s(b.spawner, sizeof b.spawner, spawners[j].name);
        b.kind = (unsigned char)spawners[j].pickKind;
        b.subtype = spawners[j].pickSub;
        break;
      }
  // The wired eggs: a `.hatch` wire reaches them (their Roach counts, entities.md section 8).
  for (int i = 0; i < neggs; ++i) {
    bool wired = false;
    for (int w = 0; w < nwires && !wired; ++w) wired = wires[w].hatch && strcmp(wires[w].target, eggs[i]) == 0;
    if (!wired) continue;
    if (a->neggs < AR_EGGS_MAX) memcpy(a->eggs[a->neggs], eggs[i], AR_NAME);
    else a->dropped++;
    a->neggs++;
  }
  // The ambushes: each monster spawner under the entity its first `.active` wire leaves from; the spawners one entity
  // fires make one ambush. An unwired one never fires and is not counted (none ships, kills-and-spawners.md section 4).
  for (int j = 0; j < nspawners; ++j) {
    const ArSpawnerTmp& sp = spawners[j];
    if (!sp.mons) continue;
    int w = 0;
    while (w < nwires && (wires[w].hatch || strcmp(wires[w].target, sp.name) != 0)) ++w;
    if (w >= nwires) continue;
    a->spawned += sp.mons;
    const ArSrcTmp& src = srcs[wires[w].src];
    int m = 0;
    while (m < a->nambush && strcmp(a->ambush[m].trig, src.name) != 0) ++m;
    if (m >= a->nambush) {
      if (a->nambush >= AR_AMBUSH_MAX) { a->dropped++; continue; }
      ArAmbush& n = a->ambush[a->nambush++];
      memset(&n, 0, sizeof n);
      memcpy(n.trig, src.name, sizeof n.trig);
      memcpy(n.trigCls, src.cls, sizeof n.trigCls);
      n.kind = ar_trig_kind(src.cls);
      const ArSrcTmp* zone = n.kind == AR_TRIG_ZONE ? &src : 0;
      for (int k = 0; k < nwires && n.kind == AR_TRIG_TIMER && !zone; ++k)   // the switch that starts the timer
        if (!wires[k].hatch && strcmp(wires[k].target, src.name) == 0 && !strcmp(srcs[wires[k].src].cls, "area_switch"))
          zone = &srcs[wires[k].src];
      if (zone) {
        memcpy(n.zone, zone->name, sizeof n.zone);
        memcpy(n.zonePos, zone->pos, sizeof n.zonePos);
      }
    }
    ArAmbush& am = a->ambush[m];
    if (am.nsp >= AR_AMB_SP) { a->dropped++; continue; }
    memcpy(am.sp[am.nsp], sp.name, AR_NAME);
    memcpy(am.spPos[am.nsp], sp.pos, sizeof sp.pos);
    am.spScripts[am.nsp++] = sp.perFire;
    am.monsters += sp.mons;
    for (int k = 0; k < sp.nmon; ++k) {
      int c = 0;
      while (c < am.nmon && strcmp(am.mon[c], sp.mon[k]) != 0) ++c;
      if (c < am.nmon) am.monN[c] += sp.monN[k];
      else if (am.nmon < AR_AMB_KINDS) { memcpy(am.mon[am.nmon], sp.mon[k], AR_CLS); am.monN[am.nmon++] = sp.monN[k]; }
    }
    for (int k = 0; k < sp.ndrop; ++k) {
      bool seen = false;
      for (int c = 0; c < am.ndrop && !seen; ++c) seen = strcmp(am.drop[c], sp.drop[k]) == 0;
      if (!seen && am.ndrop < AR_AMB_KINDS) am.drop[am.ndrop++] = sp.drop[k];
    }
  }
  a->killTotal = a->placed + a->spawned + a->neggs;
  a->parsed = spawns > 0;
  return a->parsed != 0;
}

// An avolume's first 24 bytes: local min (x y z), then max (re/areas-and-secrets.md section 1).
inline bool ar_volume(const unsigned char* b, int n, float out[6]) {
  if (!b || n < 24) return false;
  memcpy(out, b, 24);
  for (int k = 0; k < 3; ++k)
    if (!(out[k] == out[k]) || !(out[k + 3] == out[k + 3]) || out[k] > out[k + 3] || out[k] < -1e6f || out[k + 3] > 1e6f)
      return false;
  return true;
}

// --- Ambush state and label (ESP; re/kills-and-spawners.md section 3) ------------------------------------------
enum { AR_AMB_UNKNOWN = 0, AR_AMB_WAITING, AR_AMB_FIRED, AR_AMB_ARMED, AR_AMB_IDLE };
// What tells that an ambush's trigger has gone off, kept when its area is rebuilt from its .msv (the spawners' +0xAE4
// is not): a zone's or timer's switch is on (+0xAF4); a breakable, pickup, crate or barrel has cleared +0x2E0 or is
// gone; a monster is dead or gone. A laser's and any other trigger's state is not read.
enum { AR_TRD_NONE = 0, AR_TRD_SWITCH, AR_TRD_HOLDER, AR_TRD_MONSTER };
inline int ar_trig_read(const ArAmbush* m) {
  switch (m->kind) {
    case AR_TRIG_ZONE: case AR_TRIG_TIMER: return m->zone[0] ? AR_TRD_SWITCH : AR_TRD_NONE;
    case AR_TRIG_BREAKABLE: case AR_TRIG_PICKUP: return AR_TRD_HOLDER;
    case AR_TRIG_KILL: return AR_TRD_MONSTER;
    case AR_TRIG_OTHER: return !strcmp(m->trigCls, "crate") || !strcmp(m->trigCls, "barrel") ? AR_TRD_HOLDER : AR_TRD_NONE;
    default: return AR_TRD_NONE;
  }
}

// spawned: each spawner's +0xAE4 (-1 name search failed); laserArmed: the laser's `active` pin +0xB90 (-1 unread);
// *fires: most fires any found spawner made. trigDone: ar_trig_read's result (1 gone off, 0 not, -1 unread). A timer's
// switch turns on before it fires the spawners, so a timer's ambush reads fired for that delay.
inline int ar_ambush_state(const ArAmbush* m, const int* spawned, int laserArmed, int trigDone, int* fires) {
  int found = 0;
  *fires = 0;
  for (int k = 0; k < m->nsp; ++k) {
    if (spawned[k] < 0) continue;
    found++;
    const int f = laser_fires(spawned[k], m->spScripts[k]);
    if (f > *fires) *fires = f;
  }
  // A laser's ambush is ARMED while its laser can fire, else IDLE; any other is FIRED once a spawner spawned or its
  // trigger went off, else WAITING.
  if (m->kind == AR_TRIG_LASER) return !found ? AR_AMB_UNKNOWN : laserArmed > 0 ? AR_AMB_ARMED : AR_AMB_IDLE;
  if (*fires || trigDone > 0) return AR_AMB_FIRED;
  return found ? AR_AMB_WAITING : AR_AMB_UNKNOWN;
}

// The word after the label for a trigger that is no zone ("" for a zone, which draws its own box).
inline const char* ar_trig_word(const ArAmbush* m) {
  switch (m->kind) {
    case AR_TRIG_ZONE: return "";
    case AR_TRIG_TIMER: return "timer";
    case AR_TRIG_LASER: return "laser";
    case AR_TRIG_BREAKABLE: return "breakable";
    case AR_TRIG_PICKUP: return "pickup";
    case AR_TRIG_KILL: return "kill";
    default: return !strcmp(m->trigCls, "actor_explo") ? "explosion" : m->trigCls;
  }
}

// "Ambush: 2x Pig Cop + Bullets +10", the trigger's word in parentheses when it is no zone ("(timer)"), a laser's
// fires with it ("(laser, fired 2)"). Names as the enemy ESP gives them (classes.h).
inline void ar_ambush_label(const ArAmbush* m, int fires, char* out, int cap) {
  int len = _snprintf_s(out, (size_t)cap, _TRUNCATE, "Ambush:");
  for (int k = 0; k < m->nmon && len >= 0; ++k) {
    const EntClass c = class_lookup(m->mon[k]);
    char nm[48];
    if (c.name) class_name_with_variant(nm, sizeof nm, c.name, c.tier, true);
    else _snprintf_s(nm, sizeof nm, _TRUNCATE, "%s", m->mon[k]);
    const int n = _snprintf_s(out + len, (size_t)(cap - len), _TRUNCATE, "%s %dx %s", k ? "," : "", m->monN[k], nm);
    len = n < 0 ? -1 : len + n;
  }
  for (int k = 0; k < m->ndrop && len >= 0; ++k) {
    const int n = _snprintf_s(out + len, (size_t)(cap - len), _TRUNCATE, "%s%s", k ? ", " : " + ", m->drop[k]);
    len = n < 0 ? -1 : len + n;
  }
  const char* w = ar_trig_word(m);
  if (len < 0) return;
  if (m->kind == AR_TRIG_LASER) _snprintf_s(out + len, (size_t)(cap - len), _TRUNCATE, " (laser, fired %d)", fires);
  else if (w[0]) _snprintf_s(out + len, (size_t)(cap - len), _TRUNCATE, " (%s)", w);
}

// --- The graph -------------------------------------------------------------------------------------------
inline int ar_area_index(const ArPart* p, const char* map) {
  for (int i = 0; i < p->nareas; ++i)
    if (_stricmp(p->areas[i].map, map) == 0) return i;
  return -1;
}
inline int ar_door_index(const ArArea* a, const char* name) {
  for (int i = 0; i < a->ndoors; ++i)
    if (strcmp(a->doors[i].name, name) == 0) return i;
  return -1;
}

// A depth run's end this close to a doorway in x/z names the doorway's run: the research's link (a run's far end
// is a doorway, paths.md section 4). x/z only: e05p02_a01's auto_track1 keeps its route at y 2944, its door at -7680.
static const float AR_RUN_LINK = 400.f;

// Each doorway's target area and landing (the target's doorway of that name: area_changemap_place_player
// duke!0x1009ad00 searches it by name), whether the landing leads straight back (a two-way pair), and the
// nearest depth run. A target outside the part or a landing that does not exist is no edge.
inline void ar_link(ArPart* p) {
  for (int i = 0; i < p->nareas; ++i) {
    ArArea& a = p->areas[i];
    for (int k = 0; k < a.ndoors; ++k) {
      ArDoor& d = a.doors[k];
      d.target = d.tmap[0] ? ar_area_index(p, d.tmap) : -1;
      d.landing = d.target >= 0 ? ar_door_index(&p->areas[d.target], d.tdest) : -1;
      d.run[0] = 0;
      float best = AR_RUN_LINK;
      for (int r = 0; r < a.nruns; ++r)
        for (int e = 0; e < 2; ++e) {
          const float* q = e ? a.runs[r].b : a.runs[r].a;
          const float dx = q[0] - d.pos[0], dz = q[2] - d.pos[2];
          const float dist = sqrtf(dx * dx + dz * dz);
          if (dist <= best) { best = dist; strcpy_s(d.run, sizeof d.run, a.runs[r].name); }
        }
    }
  }
  for (int i = 0; i < p->nareas; ++i)
    for (int k = 0; k < p->areas[i].ndoors; ++k) {
      ArDoor& d = p->areas[i].doors[k];
      d.pair = 0;
      if (d.landing < 0) continue;
      const ArDoor& back = p->areas[d.target].doors[d.landing];
      d.pair = back.target == i && back.landing >= 0;
    }
}

// The part's totals for the log: doorways, those with an edge inside the part, two-way pairs, secrets.
struct ArTotals { int doors, edges, pairs, secrets, runs, dropped, nukes; };
inline ArTotals ar_totals(const ArPart* p) {
  ArTotals t = { 0, 0, 0, 0, 0, 0, 0 };
  for (int i = 0; i < p->nareas; ++i) {
    const ArArea& a = p->areas[i];
    t.doors += a.ndoors;
    t.secrets += a.nsecrets;
    t.nukes += a.nukeTotal;
    t.runs += a.nruns;
    t.dropped += a.dropped;
    for (int k = 0; k < a.ndoors; ++k) {
      t.edges += a.doors[k].landing >= 0;
      t.pairs += a.doors[k].pair;
    }
  }
  t.pairs /= 2;
  return t;
}

// The short label of an area: its map name after the part prefix ("a01_r01").
inline const char* ar_label(const ArArea* a) { return strlen(a->map) > 7 ? a->map + 7 : a->map; }

// One doorway hop: the area it starts in and the doorway.
struct ArHop { int area, door; };

// Breadth-first over the doorways from `from` to `to`, areas and doorways in file order; the hop count (0 when
// from == to), or -1 when no route exists or it is longer than cap.
inline int ar_route(const ArPart* p, int from, int to, ArHop* hops, int cap) {
  if (from < 0 || to < 0 || from >= p->nareas || to >= p->nareas) return -1;
  if (from == to) return 0;
  ArHop via[AR_AREAS_MAX];
  int seen[AR_AREAS_MAX] = { 0 }, queue[AR_AREAS_MAX], qh = 0, qt = 0;
  seen[from] = 1;
  queue[qt++] = from;
  while (qh < qt) {
    const int a = queue[qh++];
    for (int k = 0; k < p->areas[a].ndoors; ++k) {
      const ArDoor& d = p->areas[a].doors[k];
      if (d.landing < 0 || seen[d.target]) continue;
      seen[d.target] = 1;
      via[d.target].area = a;
      via[d.target].door = k;
      if (d.target == to) {
        int n = 0;
        for (int x = to; x != from; x = via[x].area) n++;
        if (n > cap) return -1;
        int i = n;
        for (int x = to; x != from; x = via[x].area) hops[--i] = via[x];
        return n;
      }
      queue[qt++] = d.target;
    }
  }
  return -1;
}

// "a02 -> door start -> a01 -> door room1 -> a01_r01"; a doorway with a depth run reads "run <name>".
// n 0 is "here", n < 0 "no route".
inline void ar_route_text(const ArPart* p, int from, const ArHop* hops, int n, char* out, int cap) {
  if (n < 0) { _snprintf_s(out, (size_t)cap, _TRUNCATE, "no route"); return; }
  if (n == 0) { _snprintf_s(out, (size_t)cap, _TRUNCATE, "here"); return; }
  int len = _snprintf_s(out, (size_t)cap, _TRUNCATE, "%s", ar_label(&p->areas[from]));
  for (int i = 0; i < n && len >= 0; ++i) {
    const ArDoor& d = p->areas[hops[i].area].doors[hops[i].door];
    const int k = _snprintf_s(out + len, (size_t)(cap - len), _TRUNCATE, " -> %s %s -> %s", d.run[0] ? "run" : "door",
                              d.run[0] ? d.run : d.name, ar_label(&p->areas[d.target]));
    if (k < 0) break;
    len += k;
  }
}

// --- Which secrets are found (state/<map>.msv, areas-and-secrets.md section 4). area_secret pass-1 record: u32 id, u32
// parent, u32 +0x74, u8 11 "area_secret", its keys (path "maps/<map>/area_secretN": u16 length + chars + NUL); pass-2:
// u32 id, u32 1, u32 +0xAE4, u32 1, u8 +0xBFF (area_vtE0_store duke!0x10099c90). +0xAE4 is 1 until Duke finds it.
struct ArMsvRec { char name[AR_NAME]; unsigned id; int matches, ok, armed, bff; };

inline unsigned ar_u32(const unsigned char* b) { unsigned v; memcpy(&v, b, 4); return v; }

// Every area_secret class record; each is ok when its name was read and exactly one pass-2 record with both
// values in {0, 1} follows. The count of records (at most cap kept, the rest counted in *dropped).
inline int ar_msv_parse(const unsigned char* b, int n, ArMsvRec* out, int cap, int* dropped) {
  static const char kCls[] = "\x0b" "area_secret";
  const int cl = 12;
  int count = 0;
  if (dropped) *dropped = 0;
  for (int i = 12; i + cl <= n; ++i) {
    if (b[i] != 0x0b || memcmp(b + i, kCls, (size_t)cl) != 0) continue;
    if (count >= cap) { if (dropped) (*dropped)++; continue; }
    ArMsvRec& r = out[count++];
    memset(&r, 0, sizeof r);
    r.id = ar_u32(b + i - 12);
    // The path key: u16 length, then "maps/..." of that many chars and a NUL, within the record's first keys.
    const int end = i + cl + 160 < n ? i + cl + 160 : n;
    for (int k = i + cl + 2; k + 5 <= end; ++k) {
      if (memcmp(b + k, "maps/", 5) != 0) continue;
      const int plen = b[k - 2] | (b[k - 1] << 8);
      if (plen <= 5 || k + plen >= n || b[k + plen] != 0) continue;
      int s = k + plen;
      while (s > k && b[s - 1] != '/') --s;
      _snprintf_s(r.name, sizeof r.name, _TRUNCATE, "%.*s", k + plen - s, (const char*)b + s);
      break;
    }
    for (int k = i + cl; k + 17 <= n; ++k) {
      if (ar_u32(b + k) != r.id || ar_u32(b + k + 4) != 1 || ar_u32(b + k + 12) != 1) continue;
      const unsigned x = ar_u32(b + k + 8), y = b[k + 16];
      if (x > 1 || y > 1) continue;
      if (!r.matches) { r.armed = (int)x; r.bff = (int)y; }
      r.matches++;
    }
    r.ok = r.name[0] && r.matches == 1;
  }
  return count;
}

// RECORD and DERIVED are the nukes' alone: our own last live view of the area, and the one count the tally leaves.
// SETTLING: a just-staged or empty world, unknown for a moment.
enum { AR_SRC_NONE = 0, AR_SRC_LIVE, AR_SRC_FILE, AR_SRC_FRESH, AR_SRC_STALE, AR_SRC_BAD, AR_SRC_RECORD,
       AR_SRC_DERIVED, AR_SRC_SETTLING };
struct ArFound { int src, known; unsigned char armed[AR_SECRETS_MAX]; };

// Name-matched found flags for an area: known only when the n sources are exactly the area's secrets, each
// matched once (the guard of section 4: the matches must equal the area's secret count).
inline void ar_found_match(const ArArea* a, const char (*names)[AR_NAME], const int* armed, int n, int src,
                           ArFound* f) {
  memset(f, 0, sizeof *f);
  f->src = src;
  if (n != a->nsecrets) { f->src = AR_SRC_BAD; return; }
  for (int s = 0; s < a->nsecrets; ++s) {
    int hit = -1, hits = 0;
    for (int k = 0; k < n; ++k)
      if (strcmp(names[k], a->secrets[s].name) == 0) { hit = k; hits++; }
    if (hits != 1 || armed[hit] < 0 || armed[hit] > 1) { f->src = AR_SRC_BAD; return; }
    f->armed[s] = (unsigned char)armed[hit];
  }
  f->known = 1;
}

// From the parsed .msv records: every record must be ok.
inline void ar_found_from_msv(const ArArea* a, const ArMsvRec* recs, int n, ArFound* f) {
  char names[AR_SECRETS_MAX][AR_NAME];
  int armed[AR_SECRETS_MAX];
  if (n > AR_SECRETS_MAX) n = AR_SECRETS_MAX + 1;   // never equal to a capped secret count
  for (int k = 0; k < n && k < AR_SECRETS_MAX; ++k) {
    if (!recs[k].ok) { memset(f, 0, sizeof *f); f->src = AR_SRC_BAD; return; }
    strcpy_s(names[k], AR_NAME, recs[k].name);
    armed[k] = recs[k].armed;
  }
  ar_found_match(a, names, armed, n, AR_SRC_FILE, f);
}

// An area never visited this play (no world loaded, no file) loads fresh from world.scr: nothing found.
inline void ar_found_fresh(const ArArea* a, ArFound* f) {
  memset(f, 0, sizeof *f);
  f->src = AR_SRC_FRESH;
  f->known = 1;
  for (int s = 0; s < a->nsecrets; ++s) f->armed[s] = 1;
}

inline int ar_found_count(const ArArea* a, const ArFound* f) {
  int n = 0;
  for (int s = 0; s < a->nsecrets; ++s) n += !f->armed[s];
  return n;
}

// The self-check: the areas' found secrets must sum to the live tally (+0x4C). UNKNOWN when an area with
// secrets is unknown.
enum { AR_TALLY_OK = 0, AR_TALLY_MISMATCH, AR_TALLY_UNKNOWN };
inline int ar_tally(const ArPart* p, const ArFound* found, int live, int* sum) {
  int s = 0, unknown = 0;
  for (int i = 0; i < p->nareas; ++i) {
    if (!p->areas[i].nsecrets) continue;
    if (!found[i].known) { unknown = 1; continue; }
    s += ar_found_count(&p->areas[i], &found[i]);
  }
  if (sum) *sum = s;
  if (unknown) return AR_TALLY_UNKNOWN;
  return s == live ? AR_TALLY_OK : AR_TALLY_MISMATCH;
}

// The level total as a folded heading reads it, like the Total row: "S 3/7  N 2/4"; "?" where a count is unread.
inline void ar_summary_total(char* out, int cap, int tally, int foundSum, int secretTotal, int liveOk, int nukeLive,
                             int nukeTotal) {
  char s[16], n[16];
  if (tally == AR_TALLY_OK) _snprintf_s(s, sizeof s, _TRUNCATE, "S %d/%d", foundSum, secretTotal);
  else _snprintf_s(s, sizeof s, _TRUNCATE, "S ?/%d", secretTotal);
  if (liveOk) _snprintf_s(n, sizeof n, _TRUNCATE, "N %d/%d", nukeLive, nukeTotal);
  else _snprintf_s(n, sizeof n, _TRUNCATE, "N ?/%d", nukeTotal);
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "%s  %s", s, n);
}

// The missing secrets' names, short ("secret1, secret3": the "area_" prefix dropped); "" when none.
inline void ar_missing_text(const ArArea* a, const ArFound* f, char* out, int cap) {
  int len = 0;
  out[0] = 0;
  for (int s = 0; s < a->nsecrets && len >= 0; ++s) {
    if (!f->armed[s]) continue;
    const char* nm = a->secrets[s].name;
    if (strncmp(nm, "area_", 5) == 0 && nm[5]) nm += 5;
    const int k = _snprintf_s(out + len, (size_t)(cap - len), _TRUNCATE, "%s%s", len ? ", " : "", nm);
    if (k < 0) break;
    len += k;
  }
}

// Each area's missing secrets (armed ones); -1 when its found state is unknown (stale or unreadable file).
inline void ar_missing_counts(const ArPart* p, const ArFound* found, int* out) {
  for (int i = 0; i < AR_AREAS_MAX; ++i) out[i] = -1;
  for (int i = 0; i < p->nareas && i < AR_AREAS_MAX; ++i)
    if (found[i].known) out[i] = p->areas[i].nsecrets - ar_found_count(&p->areas[i], &found[i]);
}

// --- Which nukes are collected: got[k]: source k's nukes are collected, read when have[k]. count: the nukes collected
// in all (a derived area has only the count). known: every source read. extra: nukes the area's lasers spawned past
// their one listed fire each (a laser that fired again, re/pickups.md section 7), which the part's listed total omits.
struct ArNukeFound { int src, known, count, extra; unsigned char got[AR_NUKES_MAX], have[AR_NUKES_MAX]; };

inline int ar_nukes_sum(const ArArea* a, const unsigned char* got) {
  int n = 0;
  for (int k = 0; k < a->nnukes; ++k) n += got[k] ? a->nukes[k].count : 0;
  return n;
}

// An area never visited this play loads fresh from world.scr: nothing collected.
inline void ar_nukes_fresh(const ArArea* a, ArNukeFound* f) {
  memset(f, 0, sizeof *f);
  f->src = AR_SRC_FRESH;
  f->known = 1;
  for (int k = 0; k < a->nnukes; ++k) f->have[k] = 1;
}

// A loaded world's nuke, crate, barrel and carrier entities (killed ones left out). holder: AR_NK_*; active: +0x2E0,
// cleared by powerup_on_picked duke!0x1005d950 (pickup) or damage/explode 0x100685a0, 0x10068600 (break). held: nukes
// an enemy's drop list names (carry_nukes). A laser: active its `active` pin (+0xB90), held its spawns (+0xAE4).
struct ArLiveNuke { char name[AR_NAME]; float pos[3]; int holder, active, held; };
// A broken holder's nuke lies within this of it (the spawner is built at the holder, 0x10068230; where it places the
// pickup is an open question in docs/re/pickups.md).
const float AR_HOLDER_NEAR = 256.f;

// A monster one of the area's nuke lasers' spawners made.
inline bool ar_laser_spawned(const ArArea* a, const char* name) {
  for (int k = 0; k < a->nnukes; ++k)
    if (a->nukes[k].holder == AR_NK_LASER && laser_spawned_by(name, a->nukes[k].cls)) return true;
  return false;
}

inline bool ar_pickup_name(const ArArea* a, const char* name) {
  for (int k = 0; k < a->nnukes; ++k)
    if (a->nukes[k].holder == AR_NK_PICKUP && strcmp(a->nukes[k].name, name) == 0) return true;
  return false;
}

// Live: a pickup is collected unless its entity is there with +0x2E0 set (a picked nuke is srv_killed a think later,
// FUN_1005daa0 then FUN_1005d640). A holder's nuke is not collected while the holder is whole, or, once broken or
// gone, while an active nuke with no pickup's name lies near it.
inline void ar_nukes_live(const ArArea* a, const ArLiveNuke* e, int n, ArNukeFound* f) {
  memset(f, 0, sizeof *f);
  f->src = AR_SRC_LIVE;
  f->known = 1;
  int lying = 0;
  for (int i = 0; i < n; ++i) lying += e[i].holder == AR_NK_PICKUP && e[i].active && !strcmp(e[i].name, CARRY_DROP_NAME);
  for (int k = 0; k < a->nnukes; ++k) {
    const ArNuke& s = a->nukes[k];
    bool there = false;
    // An enemy's nuke is not collected while its list still names it (slot < held) or a dropped one lies (each active
    // drop-nuke stands for one dropped slot, in source order). A laser's nuke is not collected until its spawner fired
    // (unfound: not fired), then while a spawned monster holds a nuke or a drop lies; each later fire adds to extra.
    if (s.holder == AR_NK_ENEMY) {
      int held = 0;
      for (int i = 0; i < n; ++i)
        if (e[i].holder == AR_NK_ENEMY && strcmp(e[i].name, s.name) == 0) { held = e[i].held; break; }
      if (s.slot < held) there = true;
      else if (lying > 0) { lying--; there = true; }
    } else if (s.holder == AR_NK_LASER) {
      int fires = 0;
      for (int i = 0; i < n; ++i)
        if (e[i].holder == AR_NK_LASER && strcmp(e[i].name, s.name) == 0) { fires = laser_fires(e[i].held, s.slot); break; }
      if (fires > 1) f->extra += (fires - 1) * s.count;
      for (int i = 0; i < n && fires && !there; ++i)
        there = laser_spawned_by(e[i].name, s.cls) &&
                ((e[i].holder == AR_NK_SPAWNED && e[i].held > 0) || (e[i].holder == AR_NK_PICKUP && e[i].active));
      if (!fires) there = true;
      else if (!there && lying > 0) { lying--; there = true; }
    } else if (s.holder == AR_NK_PICKUP) {
      for (int i = 0; i < n && !there; ++i)
        there = e[i].holder == AR_NK_PICKUP && e[i].active && strcmp(e[i].name, s.name) == 0;
    } else {
      const float* at = s.pos;
      for (int i = 0; i < n; ++i)
        if (e[i].holder == s.holder && strcmp(e[i].name, s.name) == 0) {
          there = e[i].active != 0;
          at = e[i].pos;
          break;
        }
      for (int i = 0; i < n && !there; ++i) {
        if (e[i].holder != AR_NK_PICKUP || !e[i].active || ar_pickup_name(a, e[i].name) ||
            !strcmp(e[i].name, CARRY_DROP_NAME))
          continue;
        const float dx = e[i].pos[0] - at[0], dy = e[i].pos[1] - at[1], dz = e[i].pos[2] - at[2];
        there = dx * dx + dy * dy + dz * dz <= AR_HOLDER_NEAR * AR_HOLDER_NEAR;
      }
    }
    f->got[k] = !there;
    f->have[k] = 1;
  }
  f->count = ar_nukes_sum(a, f->got);
}

// state/<map>.msv (re/areas-and-secrets.md section 4). Pass 1 holds a record per live entity: u32 id, u32 parent, u32
// +0x74, u8 length + class, then its keys, the name among them as u16 length + chars + NUL (72 bytes after "nuke",
// 95-99 after "crate" in the shipped files). The pickup, crate and enemy rules are in ar_nukes_from_msv.
enum { AR_MSV_NAME_WIN = 128, AR_MSV_HOLDER_2E0 = 25, AR_MSV_ENEMY_WIN = 2048 };

// The name key `name` within the window after a class name ending at `from`.
inline bool ar_msv_name_at(const unsigned char* b, int n, int from, const char* name) {
  const int l = (int)strlen(name), to = from + AR_MSV_NAME_WIN < n ? from + AR_MSV_NAME_WIN : n;
  for (int j = from; j + 2 + l + 1 <= to; ++j)
    if ((b[j] | (b[j + 1] << 8)) == l && memcmp(b + j + 2, name, (size_t)l) == 0 && b[j + 2 + l] == 0) return true;
  return false;
}

// The first class record of `cls` (u8 length + chars) whose name key is `name`; its offset, or -1. *dup: another.
inline int ar_msv_named(const unsigned char* b, int n, const char* cls, const char* name, int* dup) {
  const int cl = (int)strlen(cls);
  int at = -1;
  *dup = 0;
  for (int i = 12; i + 1 + cl <= n; ++i) {
    if (b[i] != cl || memcmp(b + i + 1, cls, (size_t)cl) != 0 || !ar_msv_name_at(b, n, i + 1 + cl, name)) continue;
    if (at >= 0) *dup = 1;
    else at = i;
  }
  return at;
}

// A holder's +0x2E0 from its pass-2 record after its class record at i: 1 or 0, -1 when no single match.
inline int ar_msv_holder_whole(const unsigned char* b, int n, int i) {
  const unsigned id = ar_u32(b + i - 12);
  int v = -1, hits = 0;
  for (int k = i + 1; k + AR_MSV_HOLDER_2E0 + 1 <= n; ++k) {
    if (ar_u32(b + k) != id || ar_u32(b + k + 4) != 1 || ar_u32(b + k + 15) != 1) continue;
    if (b[k + AR_MSV_HOLDER_2E0] > 1) continue;
    v = b[k + AR_MSV_HOLDER_2E0];
    hits++;
  }
  return hits == 1 ? v : -1;
}

// The nukes an enemy's record still lists: its class record (u8 length + class) whose name key is `name`, searched up
// to the next record of the same class (AR_MSV_ENEMY_WIN at most); -1 when no record names it. *dup: two do.
inline int ar_msv_enemy_held(const unsigned char* b, int n, const char* cls, const char* name, int* dup) {
  const int cl = (int)strlen(cls), l = (int)strlen(name), sl = (int)sizeof CARRY_NUKE_SCRIPT - 1;
  int held = -1;
  *dup = 0;
  for (int i = 12; i + 1 + cl <= n; ++i) {
    if (b[i] != cl || memcmp(b + i + 1, cls, (size_t)cl) != 0) continue;
    const int from = i + 1 + cl;
    int to = from + AR_MSV_ENEMY_WIN < n ? from + AR_MSV_ENEMY_WIN : n;
    for (int j = from; j + 1 + cl <= to; ++j)
      if (b[j] == cl && memcmp(b + j + 1, cls, (size_t)cl) == 0) { to = j; break; }
    int at = -1;
    for (int j = from; j + 2 + l + 1 <= to && at < 0; ++j)
      if ((b[j] | (b[j + 1] << 8)) == l && memcmp(b + j + 2, name, (size_t)l) == 0 && b[j + 2 + l] == 0) at = j;
    if (at < 0) continue;
    if (held >= 0) *dup = 1;
    held = 0;
    for (int j = from; j + 2 + sl <= at; ++j)
      if ((b[j] | (b[j + 1] << 8)) == sl && memcmp(b + j + 2, CARRY_NUKE_SCRIPT, (size_t)sl) == 0) held++;
  }
  return held;
}

// Clean: no pickup, holder or enemy named twice, one pass-2 match per holder. records: "nuke" class records;
// unnamed: those naming no pickup (drop-nukes among them).
inline void ar_nukes_from_msv(const unsigned char* b, int n, const ArArea* a, ArNukeFound* f, int* records,
                              int* unnamed) {
  memset(f, 0, sizeof *f);
  f->src = AR_SRC_FILE;
  int recs = 0, un = 0, drops = 0;
  for (int i = 12; i + 5 <= n; ++i) {
    if (b[i] != 0x04 || memcmp(b + i + 1, "nuke", 4) != 0) continue;
    recs++;
    bool named = false;
    for (int k = 0; k < a->nnukes && !named; ++k)
      named = a->nukes[k].holder == AR_NK_PICKUP && ar_msv_name_at(b, n, i + 5, a->nukes[k].name);
    if (named) continue;
    un++;
    drops += ar_msv_name_at(b, n, i + 5, CARRY_DROP_NAME);
  }
  if (records) *records = recs;
  if (unnamed) *unnamed = un;
  const int spawned = un - drops;   // a broken holder's nukes, or a spawner's
  int all = 1;
  for (int k = 0; k < a->nnukes; ++k) {
    const ArNuke& s = a->nukes[k];
    int dup;
    if (s.holder == AR_NK_LASER) { all = 0; continue; }   // no laser or spawner state is parsed: our record has it
    // An enemy (re/pickups.md section 6): its record lists its drops as u16 count, then u16 length + chars each, before
    // its name (e07p02_a01.msv: monst_pigoid22's name 183 bytes after the class). Gone, it died and dropped them all.
    // A dropped slot is collected unless a drop-nuke record (an uncollected drop) is left for it, in source order.
    if (s.holder == AR_NK_ENEMY) {
      const int held = ar_msv_enemy_held(b, n, s.cls, s.name, &dup);
      if (dup) { memset(f, 0, sizeof *f); f->src = AR_SRC_BAD; return; }
      f->have[k] = 1;
      if (s.slot < held) f->got[k] = 0;
      else if (drops > 0) { drops--; f->got[k] = 0; }
      else f->got[k] = 1;
      continue;
    }
    const int i = ar_msv_named(b, n, s.holder == AR_NK_PICKUP ? "nuke" : s.holder == AR_NK_CRATE ? "crate" : "barrel",
                               s.name, &dup);
    if (dup) { memset(f, 0, sizeof *f); f->src = AR_SRC_BAD; return; }
    // A pickup: a collected nuke is srv_killed a think later (powerup_on_picked duke!0x1005d950 -> vt+0x254
    // FUN_1005daa0 sets +0xD10 -> FUN_1005d640 srv_kill) and srv_gs_store_impl prism3d!0x40b5f0 walks only the live
    // entity lists, so a pickup no record names is collected.
    if (s.holder == AR_NK_PICKUP) {
      f->got[k] = i < 0;
      f->have[k] = 1;
      continue;
    }
    // A crate or barrel: its pass-2 record (holder_vtE0_store duke!0x10068ac0) is u32 id, u32 1, 3 bytes, u32, u32 1, 2
    // bytes, u32, then +0x2E0 at offset 25: 1 whole, its nuke inside. Broken (0, or no record) its nuke was spawned:
    // collected when no nuke record lacks a pickup's name (an uncollected spawned nuke would have one), else unknown.
    const int whole = i < 0 ? 0 : ar_msv_holder_whole(b, n, i);
    if (whole < 0) { memset(f, 0, sizeof *f); f->src = AR_SRC_BAD; return; }
    if (whole) { f->got[k] = 0; f->have[k] = 1; }
    else if (!spawned) { f->got[k] = 1; f->have[k] = 1; }
    else all = 0;
  }
  f->known = all;
  f->count = ar_nukes_sum(a, f->got);
}

// An unloaded area: a clean file first; the sources it read with the rest from our record of the area (the last live
// view, rec, 0 for none); the record alone; else unknown. No file at all reads "nothing collected" only when we have
// no record: a visited area whose file is missing keeps what we saw.
inline void ar_nukes_merge(const ArArea* a, const ArNukeFound* file, const ArNukeFound* rec, ArNukeFound* out) {
  if (file->known && !(file->src == AR_SRC_FRESH && rec && rec->known)) { *out = *file; return; }
  memset(out, 0, sizeof *out);
  if (!rec || !rec->known) { out->src = file->src == AR_SRC_NONE ? AR_SRC_BAD : file->src; return; }
  *out = *rec;
  out->src = AR_SRC_RECORD;
  if (file->src != AR_SRC_FRESH)
    for (int k = 0; k < a->nnukes; ++k)
      if (file->have[k]) out->got[k] = file->got[k];
  out->count = ar_nukes_sum(a, out->got);
}

// The self-check against the live tally (+0x48). The tally also counts nukes outside the areas' sources (spawner
// scripts, the monsters they spawn, a laser's fires past its first); the part's total (episodes.def nukecount,
// partTotal; -1 unknown) less the areas' sources bounds them.
inline int ar_nuke_tally(const ArPart* p, ArNukeFound* nf, int live, int liveOk, int partTotal, int* sum,
                         int* derived) {
  int s = 0, unknown = 0, which = -1, areaTotal = 0, extra = 0;
  for (int i = 0; i < p->nareas; ++i) {
    areaTotal += p->areas[i].nukeTotal;
    if (!p->areas[i].nukeTotal) continue;
    if (!nf[i].known) { unknown++; which = i; continue; }
    s += nf[i].count;
    extra += nf[i].extra;
  }
  // A laser that fired again spawned nukes no source lists: the tally may hold them too.
  const int other = (partTotal >= 0 ? (partTotal > areaTotal ? partTotal - areaTotal : 0) : 1 << 20) + extra;
  *derived = -1;
  // With exactly one area unknown and the tally read, that area's count is derived when the bounds leave one value
  // (*derived its index, else -1). OK: the sum is at most the tally and the excess within those other nukes.
  if (unknown == 1 && liveOk) {
    const int d = live - s, total = p->areas[which].nukeTotal;
    const int lo = d - other > 0 ? d - other : 0, hi = d < total ? d : total;
    if (lo == hi && hi >= 0) {
      memset(&nf[which], 0, sizeof nf[which]);
      nf[which].src = AR_SRC_DERIVED;
      nf[which].known = 1;
      nf[which].count = lo;
      s += lo;
      unknown = 0;
      *derived = which;
    }
  }
  if (sum) *sum = s;
  if (unknown || !liveOk) return AR_TALLY_UNKNOWN;
  return s <= live && live - s <= other ? AR_TALLY_OK : AR_TALLY_MISMATCH;
}

// The part's nukes the areas cannot place (spawner scripts and the monsters they spawn; re/pickups.md section 6): n =
// the part's total (episodes.def, -1 unknown) less the areas' sources; *missing = n less those taken (the live tally
// less the placed ones found), clamped to 0..n, or -1 when the tally is unread or an area is unknown. False if n is 0.
inline bool ar_unplaced(int partTotal, int placedTotal, int live, int liveOk, int placedSum, int placedKnown, int* n,
                        int* missing) {
  *n = partTotal > placedTotal ? partTotal - placedTotal : 0;
  *missing = -1;
  if (*n <= 0) return false;
  if (liveOk && placedKnown) {
    int taken = live - placedSum;
    if (taken < 0) taken = 0;
    if (taken > *n) taken = *n;
    *missing = *n - taken;
  }
  return true;
}
// "2 nukes come from scripts (1 still missing)"; "(all taken)"; no parentheses when unknown.
inline void ar_unplaced_text(int n, int missing, char* out, int cap) {
  const int len = _snprintf_s(out, (size_t)cap, _TRUNCATE, "%d nuke%s from scripts", n,
                              n == 1 ? " comes" : "s come");
  if (len < 0 || missing < 0) return;
  if (missing == 0) _snprintf_s(out + len, (size_t)(cap - len), _TRUNCATE, " (all taken)");
  else _snprintf_s(out + len, (size_t)(cap - len), _TRUNCATE, " (%d still missing)", missing);
}

// --- Kills per area (Progress; re/kills-and-spawners.md section 5) ----------------------------------------------
// found: of the area's killTotal, the kills had (known when read); waiting: its ambushes that fire once and have not.
struct ArKills { int src, known, found, waiting; };

// The ambushes that fire once (a laser's fires again): the area map's A pips.
inline int ar_once_ambushes(const ArArea* a) {
  int n = 0;
  for (int k = 0; k < a->nambush; ++k) n += a->ambush[k].kind != AR_TRIG_LASER;
  return n;
}

// A loaded area: its total less the monsters alive (placed or spawned, wired eggs not hatched) and those of ambushes
// not fired yet (a laser's until its first fire); state/fires per ar_ambush_state, unknown state: unknown count. A kill
// is any death (section 1), so a corpse counts however it died; a gibbed egg's Roach reads as had (Open questions 6).
inline void ar_kills_live(const ArArea* a, int alive, const int* state, const int* fires, ArKills* k) {
  memset(k, 0, sizeof *k);
  k->src = AR_SRC_LIVE;
  int pending = 0;
  for (int m = 0; m < a->nambush; ++m) {
    if (state[m] == AR_AMB_UNKNOWN) return;
    if (a->ambush[m].kind == AR_TRIG_LASER) {
      if (!fires[m]) pending += a->ambush[m].monsters;
    } else if (state[m] == AR_AMB_WAITING) {
      pending += a->ambush[m].monsters;
      k->waiting++;
    }
  }
  const int f = a->killTotal - alive - pending;
  k->found = f < 0 ? 0 : f > a->killTotal ? a->killTotal : f;
  k->known = 1;
}

// An area never visited this play loads fresh: nothing killed, every ambush waiting.
inline void ar_kills_fresh(const ArArea* a, ArKills* k) {
  memset(k, 0, sizeof *k);
  k->src = AR_SRC_FRESH;
  k->known = 1;
  k->waiting = ar_once_ambushes(a);
}

// state/<map>.msv: a monster's pass-1 class record (u8 length + "monst_..."); a killed one is not stored (e05p02_a01
// holds none after 34 kills, section 5). No spawner record holds +0xAE4 (fired and waiting spawners store the same
// pass-2 bytes) and no egg's hatch state is read, so only an area with neither reads: its total less the records.
inline void ar_kills_from_msv(const unsigned char* b, int n, const ArArea* a, ArKills* k, int* records) {
  memset(k, 0, sizeof *k);
  k->src = AR_SRC_FILE;
  int recs = 0;
  for (int i = 12; i + 7 <= n; ++i) {
    const int l = b[i];
    if (l < 7 || l > 40 || i + 1 + l > n || memcmp(b + i + 1, "monst_", 6) != 0) continue;
    bool word = true;
    for (int c = 7; c <= l && word; ++c)
      word = (b[i + c] >= 'a' && b[i + c] <= 'z') || (b[i + c] >= '0' && b[i + c] <= '9') || b[i + c] == '_';
    if (!word) continue;
    recs++;
    i += l;
  }
  if (records) *records = recs;
  if (a->nambush || a->neggs) return;
  const int f = a->killTotal - recs;
  k->found = f < 0 ? 0 : f;
  k->known = 1;
}

// An unloaded area, as the nukes merge: a clean file first (a fresh one yields to our record of a visited area), then
// our record (the last live view), else unknown.
inline void ar_kills_merge(const ArKills* file, const ArKills* rec, ArKills* out) {
  if (file->known && !(file->src == AR_SRC_FRESH && rec && rec->known)) { *out = *file; return; }
  if (rec && rec->known) {
    *out = *rec;
    out->src = AR_SRC_RECORD;
    return;
  }
  *out = *file;
  out->known = 0;
  if (out->src == AR_SRC_NONE) out->src = AR_SRC_BAD;
}

// "K 3/8"; "K ?/8" when unknown.
inline void ar_kills_text(char* out, int cap, int found, int total, int known) {
  if (known) _snprintf_s(out, (size_t)cap, _TRUNCATE, "K %d/%d", found, total);
  else _snprintf_s(out, (size_t)cap, _TRUNCATE, "K ?/%d", total);
}

// The Total row's note when the part's listed kills (killcount + extrakills, the corner readout's) differ from what
// the areas place: "game 85 (+1)"; "" when they agree.
inline void ar_kills_game_note(int areas, int game, char* out, int cap) {
  if (game == areas) { out[0] = 0; return; }
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "game %d (%+d)", game, game - areas);
}

// The doorway Duke came in by is one of the area's doorways (the respawn record names a checkpoint after a load or a
// checkpoint respawn, a landing doorway after area_changemap_place_player).
inline bool ar_entry_is_door(const ArArea* a, const char* entry) {
  return entry && entry[0] && ar_door_index(a, entry) >= 0;
}
// Our nuke record goes stale on a new session record, a new player entity in the same
// world (a respawn in place), or an area staged without a doorway arrival (a load, a checkpoint respawn).
inline bool ar_records_stale(int sessionChanged, int sameWorldNewPlayer, int staged, int enteredByDoor) {
  return sessionChanged || sameWorldNewPlayer || (staged && !enteredByDoor);
}

// A live nuke verdict needs a populated world: an area with sources whose dobj list came
// back empty, or one staged less than AR_SETTLE_MS ago, reads unknown rather than "all collected".
const unsigned long long AR_SETTLE_MS = 1000;
inline bool ar_live_settled(int nsources, int walked, unsigned long long msSinceStage) {
  if (nsources <= 0) return true;
  return walked > 0 && msSinceStage >= AR_SETTLE_MS;
}

// --- The cross-area door label (a label only: secrets and nukes). A doorway's label point: its offline volume's centre
// (position plus the box's middle, unrotated: rotation is an open question, areas-and-secrets.md; e05p02_a01_r01's
// two doors fit their run's ends unrotated) at the volume's bottom; its position without a volume.
inline void ar_door_centre(const ArDoor* d, float q[3], float* bottom) {
  for (int k = 0; k < 3; ++k) q[k] = d->pos[k] + (d->volOk ? 0.5f * (d->vol[k] + d->vol[k + 3]) : 0.f);
  *bottom = d->pos[1] + (d->volOk ? d->vol[1] : 0.f);
}

// What the label counts per area: missing secrets while Secret areas is on, missing nukes while Nukes is on; -1
// when that state is unknown, 0 when off or none.
struct ArWant { int secrets[AR_AREAS_MAX], nukes[AR_AREAS_MAX]; };

// w per area, and missing[i] their known sum: -1 when nothing wanted there is known to be missing and some of it is
// unknown (never a target, as before).
inline void ar_want_counts(const ArPart* p, const ArFound* sf, const ArNukeFound* nf, int wantS, int wantN, ArWant* w,
                           int* missing) {
  memset(w, 0, sizeof *w);
  for (int i = 0; i < AR_AREAS_MAX; ++i) missing[i] = 0;
  for (int i = 0; i < p->nareas && i < AR_AREAS_MAX; ++i) {
    const ArArea& a = p->areas[i];
    const int s = wantS && a.nsecrets > 0 ? (sf[i].known ? a.nsecrets - ar_found_count(&a, &sf[i]) : -1) : 0;
    const int n = wantN && a.nukeTotal > 0 ? (nf[i].known ? a.nukeTotal - nf[i].count : -1) : 0;
    w->secrets[i] = s;
    w->nukes[i] = n;
    const int sum = (s > 0 ? s : 0) + (n > 0 ? n : 0);
    missing[i] = !sum && (s < 0 || n < 0) ? -1 : sum;
  }
}

// The chosen other area, the current area's doorway that starts the route there, the hop count and the next area.
struct ArCross { int target, door, hops, next; };

// The area with something wanted missing (missing[i] > 0; -1 unknown is never chosen) fewest doorway hops away,
// over the current area's doors that land in the part. Ties: the door nearer Duke (doorDist[k], straight-line to its
// volume centre), then file order (no route solve).
inline bool ar_cross_pick(const ArPart* p, int cur, const int* missing, const float* doorDist, ArCross* out) {
  if (cur < 0 || cur >= p->nareas) return false;
  const ArArea& a = p->areas[cur];
  bool have = false;
  for (int k = 0; k < a.ndoors; ++k) {
    const ArDoor& d = a.doors[k];
    if (d.landing < 0 || d.target == cur) continue;
    // Hops from this door's area to every area (breadth-first), plus the door itself.
    int dist[AR_AREAS_MAX], queue[AR_AREAS_MAX], qh = 0, qt = 0;
    for (int i = 0; i < AR_AREAS_MAX; ++i) dist[i] = -1;
    dist[d.target] = 1;
    queue[qt++] = d.target;
    while (qh < qt) {
      const int u = queue[qh++];
      for (int j = 0; j < p->areas[u].ndoors; ++j) {
        const ArDoor& e = p->areas[u].doors[j];
        if (e.landing < 0 || dist[e.target] >= 0) continue;
        dist[e.target] = dist[u] + 1;
        queue[qt++] = e.target;
      }
    }
    for (int t = 0; t < p->nareas; ++t) {
      if (t == cur || dist[t] < 0 || missing[t] <= 0) continue;
      if (have && (dist[t] > out->hops || (dist[t] == out->hops && doorDist[k] >= doorDist[out->door]))) continue;
      out->target = t; out->door = k; out->hops = dist[t]; out->next = d.target;
      have = true;
    }
  }
  return have;
}

// The exit's label: "to a01_r01 (1 secret, 2 nukes)"; through another area first, "to a01, then a01_r01 (1 secret)".
inline void ar_cross_label(const ArPart* p, const ArCross* c, const ArWant* w, char* out, int cap) {
  char what[48] = "";
  const int s = w->secrets[c->target], n = w->nukes[c->target];
  if (s > 0) _snprintf_s(what, sizeof what, _TRUNCATE, "%d secret%s", s, s == 1 ? "" : "s");
  if (n > 0) {
    const size_t len = strlen(what);
    _snprintf_s(what + len, sizeof what - len, _TRUNCATE, "%s%d nuke%s", len ? ", " : "", n, n == 1 ? "" : "s");
  }
  if (c->next == c->target)
    _snprintf_s(out, (size_t)cap, _TRUNCATE, "to %s (%s)", ar_label(&p->areas[c->target]), what);
  else
    _snprintf_s(out, (size_t)cap, _TRUNCATE, "to %s, then %s (%s)", ar_label(&p->areas[c->next]),
                ar_label(&p->areas[c->target]), what);
}

// The chosen route as doorway hops: the chosen door first, then the doorway route on from the next area (ar_route).
// The hop count, or -1 when it does not lead to the target within cap.
inline int ar_cross_hops(const ArPart* p, int cur, const ArCross* c, ArHop* hops, int cap) {
  if (cur < 0 || cur >= p->nareas || c->door < 0 || c->door >= p->areas[cur].ndoors || cap < 1) return -1;
  hops[0].area = cur;
  hops[0].door = c->door;
  const int n = ar_route(p, c->next, c->target, hops + 1, cap - 1);
  return n < 0 ? -1 : n + 1;
}

// The list's route text from the current area to every area; the cross pick's target (c, may be 0) by its chosen
// route, so the list, the map and the exit's label name the same way.
inline void ar_route_texts(const ArPart* p, int cur, const ArCross* c, char (*out)[AR_ROUTE_CAP]) {
  for (int i = 0; i < p->nareas && i < AR_AREAS_MAX; ++i) {
    ArHop hops[AR_HOPS_MAX];
    const int n = c && i == c->target && i != cur ? ar_cross_hops(p, cur, c, hops, AR_HOPS_MAX)
                                                  : ar_route(p, cur, i, hops, AR_HOPS_MAX);
    ar_route_text(p, cur, hops, n, out[i], AR_ROUTE_CAP);
  }
}

// --- The glue (areas.cpp), main thread only. Frame path, after levelstats_frame: a new area stages (the part's
// world.scr files once per part, the entry doorway); the found state refreshes a frame after an area change (the stage
// demands the live tally first) and once a second while areas_demand holds; the current area's nukes go to our record.
void areas_frame(const unsigned char* player);
// The Progress tab asks for the 1 Hz refresh (held 1.5 s, as levelstats_demand).
void areas_demand(void);
unsigned areas_msv_reads(void);   // cumulative .msv re-reads, for the frame hitch line
struct ArView {
  int ok;                          // a part is staged and parsed
  const ArPart* part;
  int cur;                         // the current area's index, or -1
  char entry[AR_NAME];             // the doorway Duke came in by, or ""
  ArFound found[AR_AREAS_MAX];
  char route[AR_AREAS_MAX][AR_ROUTE_CAP];   // from the current area
  int tally, foundSum, live, liveOk, secretTotal;
  ArNukeFound nfound[AR_AREAS_MAX];
  // The nukes' self-check (ar_nuke_tally): the areas' sum and sources, the live tally (+0x48), episodes.def's
  // nukecount for the part (-1 unknown), and the area whose count the tally gave (-1 none).
  int nukeTally, nukeSum, nukeTotal, nukeLive, nukePart, nukeDerived;
  // Kills (re/kills-and-spawners.md section 5): each area's, the areas' total, the game's for the part (killcount +
  // extrakills at the difficulty, the corner readout's; -1 unknown), the live tally (+0x50) and the known areas' sum.
  ArKills kills[AR_AREAS_MAX];
  int killTotal, killGame, killLive, killSum;
  const void* foundWorld;          // the world (player +0xE0) current at the last refresh: found[] and cur are for it
  int crossOk;                     // the door label's cross-area choice for this area (areas_set_cross)
  ArCross cross;
};
const ArView* areas_view(void);
// The door label's cross-area choice (doorlabel.cpp), or 0 for none: the map and the list follow it. Ignored
// unless world is the area the view's found state is for.
void areas_set_cross(const void* world, const ArCross* c);
