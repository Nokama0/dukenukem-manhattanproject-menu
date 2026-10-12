#pragma once
// Targeting and silent aim: the pure rules. aim.cpp owns the detour, the engine calls and the marker. Every address is
// from docs/re/aim.md.
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "w2s.h"       // Vec3
#include "weapons.h"   // WK_*

enum { AIM_AT_BODY = 0, AIM_AT_HEAD = 1 };
// AA's ranges and defaults (its menu.cpp:2015-2096, config.cpp:223-251). Aim range is AA's
// 512..32768 Build units at 512 a metre, shown in metres at this engine's 52 units a metre.
enum { AIM_RANGE_MIN_M = 1, AIM_RANGE_MAX_M = 64, AIM_LEAD_DEF = 70 };
// At most this many candidates reach a visibility ray each frame (plan review focus 3).
enum { AIM_CAND_MAX = 64 };
const float AIM_UNITS_PER_METRE = 52.f;   // snd_distance_factor (esp_layout.h ESP_UNITS_PER_METRE)

inline const char* aim_point_label(int v) { return v ? "Head" : "Body"; }

inline float aim_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float aim_len(Vec3 a) { return sqrtf(aim_dot(a, a)); }
inline Vec3 aim_sub(Vec3 a, Vec3 b) { Vec3 r = { a.x - b.x, a.y - b.y, a.z - b.z }; return r; }
// Whether the segment from -> to crosses the axis-aligned box (centre c, half-extents h), and where it enters
// as a fraction of the segment (0 when from is inside). The slab test; a degenerate segment is a point test.
inline bool aim_seg_box_hit(Vec3 from, Vec3 to, Vec3 c, Vec3 h, float* tEnter) {
  const float f[3] = { from.x, from.y, from.z }, d[3] = { to.x - from.x, to.y - from.y, to.z - from.z };
  const float lo[3] = { c.x - h.x, c.y - h.y, c.z - h.z }, hi[3] = { c.x + h.x, c.y + h.y, c.z + h.z };
  float t0 = 0.f, t1 = 1.f;
  for (int a = 0; a < 3; ++a) {
    if (d[a] == 0.f) { if (f[a] < lo[a] || f[a] > hi[a]) return false; continue; }
    float ta = (lo[a] - f[a]) / d[a], tb = (hi[a] - f[a]) / d[a];
    if (ta > tb) { const float x = ta; ta = tb; tb = x; }
    if (ta > t0) t0 = ta;
    if (tb < t1) t1 = tb;
    if (t0 > t1) return false;
  }
  if (tEnter) *tEnter = t0;
  return true;
}

// normalize(to - from) at the original's length: the look-up case returns a scaled vector
// (aim.md section 1), and the weapons multiply by it. 0 when either length is zero.
inline int aim_redirect(Vec3 orig, Vec3 from, Vec3 to, Vec3* out) {
  const float lo = aim_len(orig);
  const Vec3 d = aim_sub(to, from);
  const float ld = aim_len(d);
  if (!(lo > 0.f) || !(ld > 0.f)) return 0;
  const float k = lo / ld;
  out->x = d.x * k; out->y = d.y * k; out->z = d.z * k;
  return 1;
}

// Straight-line projectile speed, units/s; 0 = instant or not led (aim.md section 3): bullets
// are hitscan, the beam is continuous, the pipebomb flies an arc. PRPG 800; Pulse Cannon 300..500
// by charge, 400 the mid charge.
inline float aim_proj_speed(int kind) {
  switch (kind) {
    case WK_PRPG:    return 800.f;
    case WK_PCANNON: return 400.f;
    default:         return 0.f;
  }
}
// p + vel * (dist / speed) * pct %.
inline Vec3 aim_lead_point(Vec3 p, Vec3 vel, float dist, float speed, int pct) {
  if (!(speed > 0.f) || pct <= 0) return p;
  const float t = dist / speed * (float)pct / 100.f;
  Vec3 r = { p.x + vel.x * t, p.y + vel.y * t, p.z + vel.z * t };
  return r;
}

// One candidate that passed every filter but the ray. idx is its snapshot index.
struct AimCand { int idx; float dist; Vec3 pt; };

// Ties by the walk's order, so the pick never flickers between equals at the same distance.
inline int aim_cand_cmp_dist(const void* pa, const void* pb) {
  const AimCand* a = (const AimCand*)pa; const AimCand* b = (const AimCand*)pb;
  if (a->dist != b->dist) return a->dist < b->dist ? -1 : 1;
  return a->idx < b->idx ? -1 : (a->idx > b->idx ? 1 : 0);
}
// Moves the candidate whose idx is `idx` to the front, the rest keeping their order: the rifle's committed burst
// keeps its target first in the ray walk (overkill.h). Returns 1 when it was there.
inline int aim_cand_front(AimCand* c, int n, int idx) {
  for (int i = 0; i < n; ++i) {
    if (c[i].idx != idx) continue;
    const AimCand x = c[i];
    for (int j = i; j > 0; --j) c[j] = c[j - 1];
    c[0] = x;
    return 1;
  }
  return 0;
}
// Sorts nearest-first and keeps the `cap` nearest: the ray walk always tries the nearest enemy first, in any direction
// (no Pick target/Cone/Max redirect).
inline int aim_keep_nearest(AimCand* c, int n, int cap) {
  qsort(c, (size_t)n, sizeof(AimCand), aim_cand_cmp_dist);
  return n <= cap ? n : cap;
}

// "Bip01 Head" index per class (keyed by vtable). skmod_get_bone_index prints a console error
// on a miss (aim.md section 3), so a miss is cached too, and a full cache answers "no head".
enum { AIM_BONE_SLOTS = 64 };
enum { AIM_BONE_HIT = 0, AIM_BONE_ASK, AIM_BONE_FULL };
struct AimBoneCache { const void* key[AIM_BONE_SLOTS]; int idx[AIM_BONE_SLOTS]; int n; unsigned full; };
inline int aim_bone_lookup(AimBoneCache* c, const void* key, int* idx) {
  for (int i = 0; i < c->n; ++i) if (c->key[i] == key) { *idx = c->idx[i]; return AIM_BONE_HIT; }
  if (c->n >= AIM_BONE_SLOTS) { *idx = -1; return AIM_BONE_FULL; }
  return AIM_BONE_ASK;
}
inline void aim_bone_put(AimBoneCache* c, const void* key, int idx) {
  if (c->n >= AIM_BONE_SLOTS) { c->full++; return; }
  c->key[c->n] = key; c->idx[c->n] = idx; c->n++;
}

// p times a row-vector matrix (render.md section 3): the engine's world transform at +0xF4.
inline Vec3 aim_row_xform(Vec3 p, const float* m) {
  Vec3 r = { p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12],
             p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13],
             p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14] };
  return r;
}

// weap_get_fire_dir duke!0x100508b0, all 0x31 bytes through its RET 4 (aim.md section 1). No
// relative or absolute operand in it; weap_get_fire_origin 0x100508f0 differs only at +0x29.
enum { AIM_FIRE_DIR_FP_LEN = 0x31, AIM_FIRE_DIR_TRAMP_LEN = 6 };   // 6: push esi; mov esi,[ecx+60]; test esi,esi
static const unsigned char kAimFireDirFp[AIM_FIRE_DIR_FP_LEN] = {
  0x56, 0x8B, 0x71, 0x60, 0x85, 0xF6, 0x74, 0x25, 0xF6, 0x46, 0x48, 0x02, 0x74, 0x1F, 0xF6, 0x46,
  0x4C, 0x10, 0x74, 0x19, 0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x90, 0x5C, 0x01, 0x00, 0x00, 0x8B, 0x44,
  0x24, 0x08, 0x8B, 0x16, 0x50, 0x8B, 0xCE, 0xFF, 0x92, 0x1C, 0x02, 0x00, 0x00, 0x5E, 0xC2, 0x04,
  0x00 };
inline int aim_fp_is_fire_dir(const unsigned char* b) {
  return b && memcmp(b, kAimFireDirFp, AIM_FIRE_DIR_FP_LEN) == 0 ? 1 : 0;
}

// player_get_fire_dir duke!0x10078930 (player vt+0x21C) and player_get_fire_origin 0x10078960
// (vt+0x220), each thiscall(vec3* out), RET 4; they differ only in the state slot at +0x22 (0x28 / 0x2C).
// Read in Ghidra: weap_get_fire_origin is in no vtable, so the origin is reached here.
enum { AIM_PLAYER_FP_LEN = 0x27, AIM_PLAYER_FP_SLOT = 0x22 };
static const unsigned char kAimPlayerFp[AIM_PLAYER_FP_LEN] = {
  0x56, 0x8B, 0xF1, 0x8B, 0x06, 0xFF, 0x90, 0x5C, 0x01, 0x00, 0x00, 0x8B, 0x8E, 0x7C, 0x0B, 0x00,
  0x00, 0x8B, 0x44, 0x24, 0x08, 0x8B, 0x8C, 0x8E, 0x80, 0x0B, 0x00, 0x00, 0x8B, 0x11, 0x50, 0x56,
  0xFF, 0x52, 0x28, 0x5E, 0xC2, 0x04, 0x00 };
inline int aim_fp_player(const unsigned char* b, unsigned char slot) {
  if (!b) return 0;
  for (int i = 0; i < AIM_PLAYER_FP_LEN; ++i) {
    const unsigned char want = i == AIM_PLAYER_FP_SLOT ? slot : kAimPlayerFp[i];
    if (b[i] != want) return 0;
  }
  return 1;
}
inline int aim_fp_is_player_dir(const unsigned char* b) { return aim_fp_player(b, 0x28); }
inline int aim_fp_is_player_origin(const unsigned char* b) { return aim_fp_player(b, 0x2C); }

// The calibration marker, AA's (its esp_layout.h:211-241): a hollow square at the raw point,
// a filled one at the led point. It needs its own switch and an assist that selects targets.
const int AIM_MARKER_HALF = 4;
const int AIM_MARKER_T = 1;
const char* const AIM_MARKER_LABEL = "Aim";
inline bool aim_marker_on(int silenton, int tboton, int aimdebug) {
  return (silenton != 0 || tboton != 0) && aimdebug != 0;
}

// Why a candidate was dropped, one bucket each: seen == locked-or-passed + the rest. IMMUNE: its own damage gate
// refuses now (bossrule.h). GLOPP: the G.L.O.P.P. beam cannot turn it now (glopp_gate). DOOMED: damage already on its
// way kills it (overkill.h). WIMMUNE: the ray reaches it but the held weapon cannot hurt it there (weapimm.h).
enum { AIM_REJ_DEAD = 0, AIM_REJ_RANGE, AIM_REJ_SCREEN, AIM_REJ_CAPPED, AIM_REJ_HIDDEN, AIM_REJ_IMMUNE,
       AIM_REJ_GLOPP, AIM_REJ_DOOMED, AIM_REJ_WIMMUNE, AIM_REJ_COUNT };
// What silent aim and both Triggerbot modes may shoot: a live enemy or boss whose own damage gate does not
// refuse now. immuneWhy is a BOSS_IMM_* reason (bossrule.h), 0 (BOSS_OPEN) when nothing read refuses it.
inline int aim_target_ok(int enemyOrBoss, int alive, int immuneWhy) {
  return enemyOrBoss && alive && !immuneWhy ? 1 : 0;
}

// --- The G.L.O.P.P. Ray (weapons.md section 8) -------------------------------------------------
// The beam ends at muzzle + dir * proj_gray +0xCC8 (proj_gray_beam_tick duke!0x10041940). +0xCC8 is 290,
// written by proj_gray_init_beam 0x100414e0 (at 0x1004150e) from the init vt+0x74, after the ctor's 10000.
const float GLOPP_RANGE_STOCK = 290.f;
// Where the bot may fire the ray: the Aim range, never past the beam. beam: the live +0xCC8, <= 0 if unread.
inline float glopp_reach(float aimRange, float beam) {
  const float b = beam > 0.f ? beam : GLOPP_RANGE_STOCK;
  return aimRange < b ? aimRange : b;
}
// monster_apply_damage duke!0x1002f560's checks on the beam (type 4), in order: why a target cannot morph, GLOPP_OK if
// it can. Only a read refusal excludes. BOSS: boss or helicopter part, no morph path (vt+0x198; aim.md 7).
// MORPH_ANIM: [+0xDC0]+0x71 (0x10034930 old monster, 0x10034b60 animal); MORPHING: [+0xDC0]+0x70, set by 0x100347d0.
enum { GLOPP_OK = 0, GLOPP_UNREAD_CLASS, GLOPP_UNREAD, GLOPP_IMM_BOSS, GLOPP_IMM_ANIMAL, GLOPP_IMM_DEAD,
       GLOPP_IMM_MORPH_ANIM, GLOPP_IMM_HYPER_ON, GLOPP_IMM_HYPER, GLOPP_IMM_NO_MORPH, GLOPP_IMM_MORPHING,
       GLOPP_IMM_EGG, GLOPP_IMM_COUNT };
inline int glopp_refuses(int g) { return g >= GLOPP_IMM_BOSS && g < GLOPP_IMM_COUNT; }
// Monster flags +0xB7C as that handler and the morph code test them.
const unsigned GLOPP_FLAG_ANIMAL = 0x1000;    // animal_base_ctor 0x10037780 (|= 0x81000), monst_fly init 0x10036380
const unsigned GLOPP_FLAG_HYPER = 0x800;      // the beam starts hyper mode (vt+0x354 = monster_hyper_start 0x10034eb0)
const unsigned GLOPP_FLAG_MORPHABLE = 0x80000;   // the desc reader 0x1002c170 allocates the morph block +0xDC0
struct GloppFacts {
  int boss;        // CAT_BOSS or a boss part
  int egg;         // a roach egg: its vt+0x198 duke!0x10072770 drops damage types 3 and 4 (the beam's is 4)
  int read;        // the fields below were read
  int monster;     // vt+0x198 is monster_damage_send 0x1002f3f0 and vt+0x2FC is monster_apply_damage 0x1002f560
  unsigned flags;  // +0xB7C
  float health;    // +0xB84
  int hasMorph;    // +0xDC0 != 0
  int morphed;     // [+0xDC0]+0x70: monster_morph_start 0x100347d0 has run (the transform has started)
  int morphBusy;   // [+0xDC0]+0x71: the morph animation runs
  int hyperOn;     // +0xDD4 != 0 and [+0xDD4]+0x24
};
inline int glopp_gate(const GloppFacts& f) {
  if (f.boss) return GLOPP_IMM_BOSS;
  if (f.egg) return GLOPP_IMM_EGG;
  if (!f.monster) return GLOPP_UNREAD_CLASS;
  if (!f.read) return GLOPP_UNREAD;
  if (f.flags & GLOPP_FLAG_ANIMAL) return GLOPP_IMM_ANIMAL;
  if (!(f.health > 0.f)) return GLOPP_IMM_DEAD;
  if (f.morphBusy) return GLOPP_IMM_MORPH_ANIM;
  if (f.hyperOn) return GLOPP_IMM_HYPER_ON;
  if (f.flags & GLOPP_FLAG_HYPER) return GLOPP_IMM_HYPER;
  // The handler reads [+0xDC0]+0x70 unchecked (0x1002fbf6): without the block (no 0x80000) it is never a target.
  if (!(f.flags & GLOPP_FLAG_MORPHABLE) || !f.hasMorph) return GLOPP_IMM_NO_MORPH;
  if (f.morphed) return GLOPP_IMM_MORPHING;
  return GLOPP_OK;
}
inline const char* glopp_imm_label(int r) {
  static const char* const k[GLOPP_IMM_COUNT] = { "ok", "unreadClass", "unread", "boss", "animal", "dead",
                                                  "morphAnim", "hyperOn", "hyper", "noMorph", "morphing",
                                                  "egg" };
  return r >= 0 && r < GLOPP_IMM_COUNT ? k[r] : "?";
}
// The triggerbot's verdict on the ray's target: out of reach first (no read needed), then the gate.
enum { GLOPP_FIRE = 0, GLOPP_REFUSE_RANGE, GLOPP_REFUSE_IMMUNE };
inline int glopp_verdict(float dist, float reach, int gate) {
  if (!(dist <= reach)) return GLOPP_REFUSE_RANGE;
  return glopp_refuses(gate) ? GLOPP_REFUSE_IMMUNE : GLOPP_FIRE;
}
// monster_apply_damage duke!0x1002f560 from +7 to +0x41: up to `mov ebp,0x1000` (the animal flag it tests first) and
// the +0xB84 health read. Bytes 0..6 (`push -1; push <SEH handler>`) are skipped: the handler's address is absolute,
// and dmgnum's detour overwrites them with a JMP (7 bytes copied), so with Damage numbers armed they never match stock.
enum { GLOPP_APPLY_SKIP = 7, GLOPP_APPLY_FP_LEN = 58 };
static const unsigned char kGloppApplyFp[GLOPP_APPLY_FP_LEN] = {
  0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x64, 0x89, 0x25, 0x00, 0x00, 0x00, 0x00, 0x83, 0xEC, 0x28, 0x53, 0x8A,
  0x5C, 0x24, 0x48, 0x55, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x48, 0x8B, 0xF1, 0x8B, 0x86, 0x84, 0x0B, 0x00, 0x00, 0xF6,
  0xD3, 0x80, 0xE3, 0x01, 0x83, 0xFF, 0x01, 0x89, 0x44, 0x24, 0x18, 0x88, 0x5C, 0x24, 0x1C, 0xBD, 0x00, 0x10, 0x00,
  0x00 };
// b: GLOPP_APPLY_SKIP + GLOPP_APPLY_FP_LEN readable bytes. The head is the stock `6A FF 68` or a detour's `E9`.
inline int glopp_fp_monster_apply(const unsigned char* b) {
  if (!b || !((b[0] == 0x6A && b[1] == 0xFF && b[2] == 0x68) || b[0] == 0xE9)) return 0;
  return memcmp(b + GLOPP_APPLY_SKIP, kGloppApplyFp, GLOPP_APPLY_FP_LEN) == 0 ? 1 : 0;
}

// --- Triggerbot ----------------------------------------------------------------------------
const int TBOT_WHEN_CROSSHAIR = 0;   // the unredirected line of fire meets an enemy first
const int TBOT_WHEN_LOCKED = 1;      // silent aim holds a target
const int TBOT_DELAY_MS_MIN = 0, TBOT_DELAY_MS_MAX = 500, TBOT_DELAY_MS_DEF = 0;   // AA's tbot.h
const int TBOT_FRAME_MS_MAX = 133;   // AA's four-tic ceiling: a longer gap is a pause, not a hold
const int TBOT_HELD_MS_CAP = 1000000;
// "Locked" was "Silent aim locked", 17 cells against the slider readout column's 9.
inline const char* tbot_when_label(int v) { return v == TBOT_WHEN_LOCKED ? "Locked" : "Crosshair"; }

// The delay machine, stepped once a frame. Ints, not bools (AA's tbot.h: MSVC /O2 lost a bool member).
struct TbotState { int holding; int heldMs; const void* target; };
// 1 while the condition has held on the same target for the (clamped) delay; the first frame
// counts as zero held, so a zero delay fires at once. A break or a new target starts it again.
inline int tbot_step(TbotState* s, int holds, const void* target, int dtMs, int delayMs) {
  if (!holds) { s->holding = 0; s->heldMs = 0; s->target = 0; return 0; }
  if (dtMs < 0) dtMs = 0;
  if (dtMs > TBOT_FRAME_MS_MAX) dtMs = TBOT_FRAME_MS_MAX;
  if (delayMs < TBOT_DELAY_MS_MIN) delayMs = TBOT_DELAY_MS_MIN;
  if (delayMs > TBOT_DELAY_MS_MAX) delayMs = TBOT_DELAY_MS_MAX;
  if (!s->holding || s->target != target) {
    s->holding = 1; s->heldMs = 0; s->target = target;
  } else {
    s->heldMs = s->heldMs + dtMs > TBOT_HELD_MS_CAP ? TBOT_HELD_MS_CAP : s->heldMs + dtMs;
  }
  return s->heldMs >= delayMs ? 1 : 0;
}
// Never while the panel, the freecam or the engine console owns the keys (spec, Triggerbot).
inline bool tbot_may_press(int on, int panelOpen, int freecamOn, int consoleOpen) {
  return on != 0 && !panelOpen && !freecamOn && !consoleOpen;
}
inline int tbot_when_holds(int when, int crosshairEnemy, int silentLocked) {
  return when == TBOT_WHEN_LOCKED ? (silentLocked != 0) : (crosshairEnemy != 0);
}

// Weapons the Triggerbot fires (AA's tbot.h:131-141 reasoning): the pipebomb throws on release, so a held bit
// would throw it when the condition breaks. The Pulse Cannon also fires on release, so it is tapped instead of
// held (tbot_pcannon_press). The X-3000 fires on every held frame: a plain hold.
inline int tbot_weapon_fires_on_press(int kind) {
  return kind == WK_GEAGLE || kind == WK_SHOTGUN || kind == WK_ARIFLE || kind == WK_PRPG || kind == WK_GRAY ||
         kind == WK_PCANNON || kind == WK_X3000;
}
// What the X-3000 hurts (weap_x3000_fire_logic duke!0x1005c8f0, weapons.md section 11): each held frame, every
// spawn_search "t=2?g=0x20" match (user bit 0x20 = the monster base ctor 0x1002bd80: no boss, part or egg) whose
// +0xAF1 is set (in view when last drawn, FUN_10090260) and health +0xB84 > 0. drawn < 0 (unread) stays a target.
inline int x3000_hurts(int monsterBase, int drawn, float hp) { return monsterBase && drawn != 0 && hp > 0.f; }
// With the X-3000 held the target is the first snapshot entity it hurts, in view now (x3000_view_gate), that
// overkill does not doom: no ray, no lock, no line of sight (it reaches through walls). Returns its index, or -1.
inline int x3000_pick(const int* hurts, const int* doomed, int n) {
  for (int i = 0; i < n; ++i) if (hurts[i] && !doomed[i]) return i;
  return -1;
}
// +0xAF1 is written only inside the actor draw and only when it changes, so a monster the renderer stops drawing
// (another area, culled, or what the freecam drew) keeps its last value. The bot's target must also have been drawn in
// the last world pass: X3kDrawn records the pass of each actor's last draw.
enum { X3K_DRAWN_SLOTS = 1024, X3K_DRAWN_PROBE = 32 };   // a power of two; a chain is walked at most PROBE slots
struct X3kDrawn { const void* key[X3K_DRAWN_SLOTS]; unsigned pass[X3K_DRAWN_SLOTS]; unsigned full; };
inline void x3000_drawn_clear(X3kDrawn* t) { memset(t, 0, sizeof *t); }
// Heap objects are 8-aligned on MSVC x86 and monsters are 0xEE8+ bytes apart, so the low bits spread them.
inline unsigned x3000_drawn_slot(const void* ent) { return (unsigned)((size_t)ent >> 4) & (X3K_DRAWN_SLOTS - 1); }
// The first slot of the chain that holds ent, is empty, or holds an older pass takes it. A lookup meets ent's own
// slot before any older duplicate, since a put stops at ent's slot when it comes first. 0 (full++) when the whole
// chain holds this pass's other actors.
inline int x3000_drawn_put(X3kDrawn* t, const void* ent, unsigned pass) {
  unsigned s = x3000_drawn_slot(ent);
  for (int i = 0; i < X3K_DRAWN_PROBE; ++i, s = (s + 1) & (X3K_DRAWN_SLOTS - 1)) {
    if (t->key[s] == ent || !t->key[s] || t->pass[s] != pass) { t->key[s] = ent; t->pass[s] = pass; return 1; }
  }
  t->full++;
  return 0;
}
inline int x3000_drawn_get(const X3kDrawn* t, const void* ent, unsigned* pass) {
  unsigned s = x3000_drawn_slot(ent);
  for (int i = 0; i < X3K_DRAWN_PROBE; ++i, s = (s + 1) & (X3K_DRAWN_SLOTS - 1)) {
    if (t->key[s] == ent) { *pass = t->pass[s]; return 1; }
    if (!t->key[s]) return 0;
  }
  return 0;
}
// Drawn in the world pass just completed, and that pass was Duke's camera: not a freecam frame, nor the first frame
// after one (its pass may still have drawn from the freecam).
inline int x3000_fresh(int haveRec, unsigned rec, unsigned pass, int freecamNow, int freecamPrev) {
  return haveRec && rec == pass && !freecamNow && !freecamPrev ? 1 : 0;
}
// rend_sphere_visible passes a sphere that only touches the frustum; the bot wants the box centre on screen.
inline int x3000_on_screen(ScreenPt sp, int W, int H) {
  return sp.ok && sp.x >= 0.f && sp.x <= (float)W && sp.y >= 0.f && sp.y <= (float)H ? 1 : 0;
}
// Why a monster the X-3000 hurts is not the bot's target (one counter each), X3K_SEEN when it is in view now.
enum { X3K_SEEN = 0, X3K_STALE, X3K_OFFSCREEN };
inline int x3000_view_gate(int fresh, int onScreen) { return !fresh ? X3K_STALE : (!onScreen ? X3K_OFFSCREEN : X3K_SEEN); }
// The actor draw FUN_10090260 (thiscall, one stack arg, RET 4), entry to 0x100902e6, just before `call
// [rend_sphere_visible]` (absolute operand): tests +0xA14, +0xAF0, pushes +0x4AC (the sphere); its rel32 calls
// (0x1008c090, 0x10090720) do not move. The 6-byte head `sub esp,0x264` is the trampoline's; the rest is compared.
enum { X3K_DRAW_HEAD = 6, X3K_DRAW_FP_LEN = 135 };
static const unsigned char kX3kDrawFp[X3K_DRAW_FP_LEN] = {
  0x81, 0xEC, 0x64, 0x02, 0x00, 0x00, 0x8B, 0x84, 0x24, 0x68, 0x02, 0x00, 0x00, 0x53, 0x55, 0x56, 0x57, 0xB3, 0x01,
  0x50, 0x8B, 0xE9, 0x88, 0x5C, 0x24, 0x14, 0xE8, 0x11, 0xBE, 0xFF, 0xFF, 0x8B, 0x55, 0x00, 0x8B, 0xCD, 0xFF, 0x92,
  0x5C, 0x01, 0x00, 0x00, 0x8A, 0x85, 0x14, 0x0A, 0x00, 0x00, 0x84, 0xC0, 0x75, 0x6A, 0x8B, 0x45, 0x60, 0x85, 0xC0,
  0x74, 0x17, 0x8B, 0x48, 0x48, 0x84, 0xEB, 0x75, 0x10, 0x6A, 0x00, 0x8D, 0x44, 0x24, 0x30, 0x50, 0x8B, 0xCD, 0xE8,
  0x70, 0x04, 0x00, 0x00, 0xEB, 0x1F, 0x8B, 0x55, 0x00, 0x8B, 0xCD, 0xFF, 0x92, 0x60, 0x01, 0x00, 0x00, 0x8B, 0x08,
  0x89, 0x4C, 0x24, 0x2C, 0x8B, 0x50, 0x04, 0x89, 0x54, 0x24, 0x30, 0x8B, 0x40, 0x08, 0x89, 0x44, 0x24, 0x34, 0x8A,
  0x85, 0xF0, 0x0A, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x1D, 0x8B, 0x8D, 0xAC, 0x04, 0x00, 0x00, 0x51, 0x8D, 0x54, 0x24,
  0x30, 0x52 };
inline int x3000_fp_actor_draw(const unsigned char* b) {
  if (!b || !(b[0] == 0xE9 || memcmp(b, kX3kDrawFp, X3K_DRAW_HEAD) == 0)) return 0;
  return memcmp(b + X3K_DRAW_HEAD, kX3kDrawFp + X3K_DRAW_HEAD, X3K_DRAW_FP_LEN - X3K_DRAW_HEAD) == 0 ? 1 : 0;
}
// The signature scan over [b, b+n) (duke_base's code; no export or vtable reaches the function directly). Returns
// the number of matches, *at the first one's offset; the caller arms only on exactly one.
inline int x3000_find_actor_draw(const unsigned char* b, size_t n, size_t* at) {
  int found = 0;
  for (size_t i = 0; b && i + X3K_DRAW_FP_LEN <= n; ++i) {
    if ((b[i] != 0x81 && b[i] != 0xE9) || b[i + X3K_DRAW_HEAD] != kX3kDrawFp[X3K_DRAW_HEAD]) continue;
    if (!x3000_fp_actor_draw(b + i)) continue;
    if (!found) *at = i;
    found++;
  }
  return found;
}
// Semi-automatic: one shot per press (docs/re/weapons.md; measured holds=5003 presses=3147 fired once). Only the Golden
// Eagle is verified; every other weapon keeps the hold.
inline int tbot_weapon_semi_auto(int kind) { return kind == WK_GEAGLE; }
// This frame's press: a semi-automatic weapon is pressed one frame and released the next while fire
// holds (the mask is level and the weapon wants an edge, aim.md section 5); automatics hold.
inline int tbot_pulse(int fire, int semiAuto, int* pressedLast) {
  const int press = fire && !(semiAuto && *pressedLast);
  *pressedLast = press;
  return press;
}
// weap_geagle_fire_logic duke!0x10055120 queues a shot only on a held frame after a frame with the attack bit
// clear (its latch +0xC1E, cleared by each shot). So the press follows that latch: pressed while it is set,
// released until the weapon has seen the release. latch < 0 (unread) falls back to alternate frames.
inline int tbot_semi_press(int fire, int latch, int* pressedLast) {
  if (latch < 0) return tbot_pulse(fire, 1, pressedLast);
  const int press = fire && latch;
  *pressedLast = press;
  return press;
}
// The Pulse Cannon's tap (weap_pcannon_fire_logic duke!0x100588a0): a held frame with cooldown +0xCC8 at 0 charges
// (+0xCBC); the first frame without the bit discharges (0x100590f0). So press while nothing is charged, release while
// something is: uncharged shots. One tap per TBOT_PCANNON_GAP_MS (about 8/s; no engine gate). chargeRead 0 alternates.
const unsigned TBOT_PCANNON_GAP_MS = 125;
inline int tbot_pcannon_ready(int haveRelease, unsigned nowMs, unsigned lastReleaseMs) {
  return !haveRelease || nowMs - lastReleaseMs >= TBOT_PCANNON_GAP_MS;
}
inline int tbot_pcannon_press(int fire, int chargeRead, float charge, int* pressedLast) {
  if (!chargeRead) return tbot_pulse(fire, 1, pressedLast);
  const int press = fire && !(charge > 0.f);
  *pressedLast = press;
  return press;
}
// What a Golden Eagle press did, read on the frame it landed: a shot only when +0xB18 moved and the latch
// is clear, since only the normal shot path clears it (0x10055120); reload and dry fire move B18 and keep it.
enum { TBOT_PRESS_SHOT = 0, TBOT_PRESS_RELOAD_DRY, TBOT_PRESS_NOSHOT, TBOT_PRESS_UNREAD, TBOT_PRESS_KINDS };
inline int tbot_press_outcome(int readOk, int b18Moved, int latchAfter) {
  if (!readOk) return TBOT_PRESS_UNREAD;
  if (!b18Moved) return TBOT_PRESS_NOSHOT;
  return latchAfter ? TBOT_PRESS_RELOAD_DRY : TBOT_PRESS_SHOT;
}
// Four channels per impulse slot: value[4]/max[4] at +0x20/+0x10 (slot init prism3d!0x4068c0, inp_process ch < 4).
inline int tbot_channel_ok(int ch) { return ch >= 0 && ch < 4; }
// inp_reset_impulses clears a slot only while its bind count (+0x50) is non-zero, so an unbound attack
// would keep our press for good: it is never pressed then.
inline int tbot_bound(int bindCount) { return bindCount > 0; }
// wm_get_input prism3d!0x423fd0: `mov eax,[esp+4]; lea eax,[eax+eax*2]; mov cl,[eax*8+T]; test cl,cl;
// lea eax,[eax*8+T]`. T is the wm slot table (stride 0x18: +0 live byte, +0x10 entity, +0x14 channel,
// wm_dispatch_process_input 0x424050). Both immediates must agree. 1 with *table = T.
inline int aim_wm_table(const unsigned char* b, unsigned* table) {
  static const unsigned char head[10] = { 0x8B, 0x44, 0x24, 0x04, 0x8D, 0x04, 0x40, 0x8A, 0x0C, 0xC5 };
  static const unsigned char mid[5] = { 0x84, 0xC9, 0x8D, 0x04, 0xC5 };
  if (!b || memcmp(b, head, sizeof head) || memcmp(b + 14, mid, sizeof mid) || memcmp(b + 10, b + 19, 4)) return 0;
  *table = (unsigned)b[10] | ((unsigned)b[11] << 8) | ((unsigned)b[12] << 16) | ((unsigned)b[13] << 24);
  return 1;
}

// --- The bullet's own ray (docs/re/aim.md section 4b): what proj_bullet_damage_hit duke!0x1003ad90 does ---
// to a record. PASS: nothing (a skipped or dying monster, or neither type bit), so not a blocker. DESTRUCTIBLE:
// breakable; the bullet stops but firing on destroys it, so it does not block. BLOCK: stops it, nothing breaks.
enum { AIM_OBS_PASS = 0, AIM_OBS_DESTRUCTIBLE, AIM_OBS_BLOCK };
// The two class names proj_bullet_damage_hit compares on its object branch, and `blocker` (below).
enum { AIM_OBJ_OTHER = 0, AIM_OBJ_BREAKABLE, AIM_OBJ_BREAKABLE_ADV, AIM_OBJ_BLOCKER };
// User bit 0x1000: powerup_base_ctor duke!0x1005d051 (every pickup class); FUN_101190b0 also sets it on a
// non-object class, which the (type & 2) gate below excludes.
const unsigned AIM_USER_POWERUP = 0x1000;
// vt+0x170 (object_e::damage slot) as read: unread, a bare RET 8, or the breakable handler 0x100bab30.
enum { AIM_H_UNKNOWN = 0, AIM_H_NOOP, AIM_H_BREAKABLE };
struct AimObsFacts {
  unsigned type, user;   // +0x48 engine type bits, +0x4C user bits
  int dyingMonster;      // monster (user 0x20) with [+0xB94] >= 0x14: the actor branch skips its damage
  int actorDamages;      // vt+0x194 is actor_damage_from_info and its vt+0x198 is not a bare RET 0x14
  int objClass;          // AIM_OBJ_*
  int advArmed;          // breakable_adv: [+0xBE4] != 0, the damage path's own gate
  int handler;           // AIM_H_*
  int breakArmed;        // [+0xC44] != 0, the breakable handler's own gate before it takes health
  int blockerActive;     // blocker: +0x2E0 != 0 (its ctor sets it; unverified that a lowered one clears it)
  int immuneNow;         // an actor whose own damage gate refuses now (bossrule.h): it takes the budget, no health
};
// The blocker's per-instance flags (property handler duke!0x100b9af0, init 0x100b9a80): world.scr creatures_only ->
// +0xB0A -> user 0x10000, player_only -> +0xB0B -> user 0x20000. With neither, the blocker stops everything, Duke's
// bullets included (measured at the doorway barrier, blocker2).
const unsigned AIM_USER_BLOCK_CREATURES = 0x10000, AIM_USER_BLOCK_PLAYER = 0x20000;
inline int aim_obstacle_kind(const AimObsFacts& f) {
  // The walk's skip (0x1003ab60-0x1003ab89): actor, monster, user bit 0x80, budget <= 1.
  if ((f.type & 2) && (f.user & 0x20) && (f.user & 0x80)) return AIM_OBS_PASS;
  // Real Duke bullets pass creatures_only blockers (railings) and pickups (docs/re/aim.md section 4b); a blocker for
  // everyone stops them.
  if (f.objClass == AIM_OBJ_BLOCKER) {
    // A creatures_only blocker passed a real shot; player_only is taken to pass as well (unmeasured); a blocker for
    // everyone stopped them (the doorway barrier): it blocks while up.
    const bool forAll = !(f.user & (AIM_USER_BLOCK_CREATURES | AIM_USER_BLOCK_PLAYER));
    return forAll && f.blockerActive ? AIM_OBS_BLOCK : AIM_OBS_PASS;
  }
  if ((f.type & 2) && (f.user & AIM_USER_POWERUP)) return AIM_OBS_PASS;
  if (f.type & 0x100) return AIM_OBS_BLOCK;   // world_e: the budget goes to 0
  if (f.type & 2) {
    if ((f.user & 0x20) && f.dyingMonster) return AIM_OBS_PASS;   // no damage, no budget spent
    if (f.immuneNow) return AIM_OBS_BLOCK;   // vt+0x194 runs and the budget drops, but its gate takes no health
    return f.actorDamages ? AIM_OBS_DESTRUCTIBLE : AIM_OBS_BLOCK;   // the budget drops by 1 either way
  }
  if (f.type & 1) {   // vt+0x170, then the budget goes to 0
    if (f.objClass == AIM_OBJ_BREAKABLE) return AIM_OBS_BLOCK;
    if (f.objClass == AIM_OBJ_BREAKABLE_ADV && !f.advArmed) return AIM_OBS_BLOCK;
    return f.handler == AIM_H_BREAKABLE && f.breakArmed ? AIM_OBS_DESTRUCTIBLE : AIM_OBS_BLOCK;
  }
  return AIM_OBS_PASS;   // neither bit: damage_hit returns with the budget unchanged
}
// Only a blocking world_e ends the ray as the nearest wall does: a passed one is no wall.
inline int aim_obstacle_cuts(const AimObsFacts& f, int kind) { return (f.type & 0x100) && kind == AIM_OBS_BLOCK; }
// world: a hit that cuts the ray (aim_obstacle_cuts). tag: the caller's own index, carried through the sort.
struct AimRayRec { float dist; int target; int kind; int world; int tag; };
// The history phys_ray_test builds with block = world-only and touch = everything else: every touch hit
// (phys_ray_vs_objects prism!0x40f1b0 records them all, past the wall too) and only the nearest blocking (world) hit.
// Sorts by distance and returns how many records reach the first world hit inclusive: nothing past the wall counts.
inline int aim_ray_sort_cut(AimRayRec* r, int n) {
  for (int i = 1; i < n; ++i) {
    const AimRayRec x = r[i];
    int j = i - 1;
    while (j >= 0 && r[j].dist > x.dist) { r[j + 1] = r[j]; --j; }
    r[j + 1] = x;
  }
  for (int i = 0; i < n; ++i) if (r[i].world) return i + 1;
  return n;
}
// The nearest target record that no BLOCK record lies in front of, or -1 with *blocker the nearest
// BLOCK in front of every target (-1 when there is none: the ray met no target at all).
inline int aim_ray_first_target(const AimRayRec* r, int n, int* blocker) {
  int t = -1, b = -1;
  for (int i = 0; i < n; ++i) {
    if (r[i].target) { if (t < 0 || r[i].dist < r[t].dist) t = i; }
    else if (r[i].kind == AIM_OBS_BLOCK && (b < 0 || r[i].dist < r[b].dist)) b = i;
  }
  *blocker = -1;
  if (t >= 0 && (b < 0 || r[t].dist < r[b].dist)) return t;
  *blocker = b;
  return -1;
}
// actor_damage_from_info duke!0x1008b630, whole (no relocations): forwards to vt+0x198.
static const unsigned char kAimDamageFromInfo[36] = {
  0x8B, 0x44, 0x24, 0x0C, 0x8B, 0x11, 0x56, 0x8B, 0x70, 0x1C, 0x56, 0x8B, 0x70, 0x18, 0x56, 0x50, 0x8B, 0x44,
  0x24, 0x18, 0x50, 0x8B, 0x44, 0x24, 0x18, 0x50, 0xFF, 0x92, 0x98, 0x01, 0x00, 0x00, 0x5E, 0xC2, 0x0C, 0x00 };
inline int aim_fp_is_damage_from_info(const unsigned char* b) {
  return b && memcmp(b, kAimDamageFromInfo, sizeof kAimDamageFromInfo) == 0 ? 1 : 0;
}
// The breakable handler duke!0x100bab30: `mov eax,[ecx+0xc44]; test eax,eax; jz ret; jmp take_health`
// then `ret 8`. Bytes 19..22 are the jmp's rel32.
enum { AIM_BREAK_FP_LEN = 26 };
inline int aim_fp_is_breakable_handler(const unsigned char* b) {
  static const unsigned char head[19] = { 0x8B, 0x81, 0x44, 0x0C, 0x00, 0x00, 0x85, 0xC0, 0x74, 0x0D, 0x8B, 0x44,
                                          0x24, 0x08, 0x89, 0x44, 0x24, 0x08, 0xE9 };
  static const unsigned char tail[3] = { 0xC2, 0x08, 0x00 };
  return b && !memcmp(b, head, sizeof head) && !memcmp(b + 23, tail, sizeof tail) ? 1 : 0;
}
// breakable_obstacle's vt+0x170 (0x100bb6c0): the same two args, then `jmp rel32`.
enum { AIM_THUNK_LEN = 13 };
inline int aim_thunk_target(const unsigned char* b, unsigned at, unsigned* target) {
  static const unsigned char head[9] = { 0x8B, 0x44, 0x24, 0x08, 0x89, 0x44, 0x24, 0x08, 0xE9 };
  if (!b || memcmp(b, head, sizeof head)) return 0;
  const unsigned rel = (unsigned)b[9] | ((unsigned)b[10] << 8) | ((unsigned)b[11] << 16) | ((unsigned)b[12] << 24);
  *target = at + AIM_THUNK_LEN + rel;
  return 1;
}
// A bare `ret imm16`: object_e::damage (p3shared!0x1001e6d0, RET 8) and the actor default vt+0x198
// (duke!0x10001c40, RET 0x14) do nothing.
inline int aim_is_ret(const unsigned char* b, unsigned imm) {
  return b && b[0] == 0xC2 && b[1] == (unsigned char)(imm & 0xFF) && b[2] == (unsigned char)(imm >> 8) ? 1 : 0;
}

// --- aim.cpp (DLL only; read, not run, by the suite) ---------------------------------------
// Main thread, frame path, after weapons_frame: arms the detour once Silent aim is on and
// selects this frame's target. `player` is probe.cpp's validated player or NULL; W, H the screen.
void aim_frame(const unsigned char* player, int W, int H);
// After the ESP, under the panel: the calibration marker.
void aim_draw(int W, int H);
// Silent aim holds a target: the rifle's no-spread lever applies (weapons.cpp).
int aim_rifle_nospread(void);
// Top of every frame-path call: a frame that returns before aim_frame must not keep last frame's lock.
void aim_lock_clear(void);
int aim_armed(void);
// The triggerbot's detour and the DIAGNOSTICS numbers.
int aim_tbot_armed(void);
int aim_tbot_refused(void);
int aim_tbot_channel(void);
unsigned aim_tbot_presses(void);
unsigned aim_tbot_holds(void);
int aim_refused(void);
unsigned aim_calls(void);
unsigned aim_rewrites(void);
// Shots the detour sent to mouse aim's point.
unsigned aim_mouse_rewrites(void);
// The triggerbot's rend_begin_frame detour, armed on first use; 1 once installed.
int aim_begin_hook(void);
// The fire origin weap_get_fire_origin returns for `player` (its verified state getter); 0 when unread.
int aim_fire_origin(const unsigned char* player, Vec3* eye);
unsigned aim_locks(void);
// The ESP's mark: the held weapon cannot hurt this enemy now (weapimm.h), judged once a frame by aim_frame.
int aim_weapon_immune(const void* ent);
unsigned aim_rejected(int reason);
// Rays for overkill.cpp, from this frame's context (aim_frame's; 0 hits outside it). replica 1: the bullet's own
// filter, so only its nearest object; 0: every object on the ray and the nearest wall. Sorted nearest first,
// with each object's engine type (+0x48) and user (+0x4C) bits; read 0 when they could not be read.
struct AimProbeHit { const void* obj; float dist; Vec3 at; unsigned type, user; int dying, read; };
int aim_probe_ray(Vec3 from, Vec3 to, int replica, AimProbeHit* out, int max);
// The world-only line of sight (walls, no actors), as silent aim's prefilter: 1 when clear.
int aim_probe_visible(Vec3 from, Vec3 to);
