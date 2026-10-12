#pragma once
// The per-frame entity snapshot: rebuilt from the engine's own search iterator
// every frame, so no entity pointer outlives the frame that read it.
#include "w2s.h"
#include "classes.h"
#include "espbox.h"   // Hitbox
#include "pickrule.h" // PlayerPools
#include "eggrule.h"  // EGG_*
#include "attachrule.h"  // ATT_*

// The ESP collect cap (plan: EK_MARKER_COLLECT_MAX = 1024).
enum { ENT_SNAPSHOT_MAX = 1024 };
// Boss parts (only the helicopter has any: three, aim.md section 7), and the entities whose own damage gate
// refuses now.
enum { ENT_PARTS_MAX = 8, ENT_IMMUNE_MAX = 16 };

struct EntSnap {
  void* ent;
  unsigned char cat, tier, kind;
  unsigned char hashp;   // 0: not a monster-base object, hp/maxhp hold no health
  const char* name;      // classes.h's table, never engine memory; items by subtype (item_label)
  Vec3 pos;              // world position: +0x18C, or the world matrix's for an attached entity (attachrule.h)
  unsigned char att;     // ATT_* (attachrule.h): where pos came from
  const void* parent;    // +0x60, the entity this one rides (a train car), or NULL
  Vec3 local;            // +0x18C as read: local to `parent` when there is one
  Vec3 half;             // half-extents as read (+0x2B8)
  Hitbox hb;             // the collision box every marker but a secret's draws (hitbox_from)
  float hp, maxhp;
  int keyColour;         // CAT_KEY: key+0xD14 as read (0 blue, 1 yellow, 2 red); -1 elsewhere
  unsigned char active;  // CAT_ITEM/CAT_KEY/CAT_NUKE: 0 once picked (+0x2E0 cleared); CAT_EXPLODABLE: 0 once broken
                         // (the same byte, cleared by breakable_break duke!0x100f1600); 1 elsewhere
  unsigned char armed;   // CAT_SECRET: +0xAE4 != 0, cleared when found (entities.md section 4); CAT_EXPLODABLE: the
                         // damage gate +0xC44 != 0 (breakable_adv_damage duke!0x100bab30); 0 elsewhere
  unsigned char freed;   // CAT_BABE: +0xE10 != 0, set by babe_activate (entities.md section 4); 0 elsewhere
  const void* vt;        // the entity's vtable, in duke_base (the walk checked it)
  void* skel;            // CAT_ENEMY/CAT_BOSS: the skmod handle +0xAE4 (aim.md section 3), or NULL
  Vec3 axes[3];          // CAT_SECRET with obb: the volume's half-axis vectors (area_obb); pos is its centre
  unsigned char obb;     // 1 when axes hold the engine's own volume
  int subtype;           // CAT_ITEM ammo/ego/gun: the +0xD14 type dword (pickups.md section 1); -1 elsewhere or unread
  unsigned char immune;  // CAT_BOSS: BOSS_IMM_* when the boss's own damage gate refuses now (bossrule.h); 0 elsewhere
  int bone;              // a part: its centre bone in its own model (+0xB2C, set by the boss init 0x10006e30); -1 elsewhere
  unsigned char bossKind;  // BOSS_K_* (bossrule.h) for a boss entity or part, the per-boss log's row; 0 elsewhere
  unsigned char holder;  // HOLDER_* (craterule.h): this nuke or item is still inside the crate or barrel `ent`
  unsigned char egg;     // EGG_* (eggrule.h) for a roach egg; EGG_NONE elsewhere
  signed char drawn;     // monster base: +0xAF1 != 0 (drawn and in view, the X-3000's gate), read once in the walk
                         // under kMonsterSpan; -1 when not a monster base
  unsigned char carry;   // CAT_ENEMY/CAT_BOSS: the nukes its drop list still names (carryrule.h); CAT_NUKE: this
                         // entry is those nukes, drawn on the enemy `ent`; 0 elsewhere
};

// CAT_AMBUSH entries (areas.h's ambushes; re/kills-and-spawners.md section 3), in EntSnap.kind: one not fired yet
// (orange, at its spawners), one a laser can fire again (red), and the zone that starts a waiting one.
enum { AMB_MARK_WAIT = 0, AMB_MARK_ARMED = 1, AMB_MARK_ZONE = 2 };
// The marker's least half-extent: a spawner has no body of its own.
const float AMB_BOX_MIN = 24.f;

struct EntSnapshot {
  EntSnap ents[ENT_SNAPSHOT_MAX];
  int count;                   // entries in ents: every category but CAT_OTHER
  const void* player;          // NULL in menus and loading: no walk ran
  int ladders;                 // area_ladder volumes walked this frame
  int atLadder;                // Duke's head (ent_ladder_head) inside one of them: Up would grab it
  Vec3 playerPos;
  int total;                   // every entity the search returned
  int cats[CAT_COUNT];         // of those, per category
  int skipped;                 // dropped before classification: total == skipped + sum(cats)
  int alive;                   // enemy and boss entries that ent_alive accepts
  int inactive;                // item, key and nuke entries with active 0
  int areas;                   // every hit of the t=0x20 (area_e) search, incl. non-secret areas
                                // (secrets-walk.md section 6: areas == secrets + delta(other))
  int breakables;              // every hit of the t=4 search (breakables and movers, re/explodables.md)
  PlayerPools pools;           // the player's ammo pools and caps, read once a frame (pickrule.h); valid 0 if not
  // Boss parts, outside ents[] so the ESP does not draw them: the aim's extra candidates.
  EntSnap parts[ENT_PARTS_MAX];
  int nparts;
  int partsUnread;             // part slots that failed validation this frame
  const void* immuneEnt[ENT_IMMUNE_MAX];   // ents and parts with immune != 0, for the bullet ray's obstacles
  unsigned char immuneWhy[ENT_IMMUNE_MAX];
  int nimmune;
  int eggs[EGG_GONE + 1];      // roach eggs walked this frame by EGG_* state (eggs[0] unused)
  int att[4];                  // walked entities, crate and breakable entries by ATT_* this frame (attachrule.h)
};

// Mech Nukem, Wozma and the Crane have no monster health (hashp 0): they count as
// alive and draw without a bar or number. An egg is alive until it hatches, dies or is gibbed (eggrule.h).
inline bool ent_alive(const EntSnap& s) {
  if (s.egg) return s.egg != EGG_GONE;
  return !s.hashp || s.hp > 0.f;
}
// The world box of an area's volume: the avolume's local range (min/max, area_e::get_range
// p3shared!0x10001580) through the row-vector world matrix at +0xF4 (area_e::inside p3shared!0x100014d0
// tests a point through its inverse at +0x134). The axis-aligned box of the 8 moved corners.
inline void area_bounds(const float mn[3], const float mx[3], const float* m, Vec3* centre, Vec3* half) {
  float lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
  for (int k = 0; k < 8; ++k) {
    const float p[3] = { (k & 1) ? mx[0] : mn[0], (k & 2) ? mx[1] : mn[1], (k & 4) ? mx[2] : mn[2] };
    for (int a = 0; a < 3; ++a) {
      const float v = p[0] * m[a] + p[1] * m[4 + a] + p[2] * m[8 + a] + m[12 + a];
      if (!k || v < lo[a]) lo[a] = v;
      if (!k || v > hi[a]) hi[a] = v;
    }
  }
  centre->x = (lo[0] + hi[0]) * 0.5f; centre->y = (lo[1] + hi[1]) * 0.5f; centre->z = (lo[2] + hi[2]) * 0.5f;
  half->x = (hi[0] - lo[0]) * 0.5f; half->y = (hi[1] - lo[1]) * 0.5f; half->z = (hi[2] - lo[2]) * 0.5f;
}
// The same volume as its own (possibly rotated) box: centre, and the three half-axis vectors (the local
// half extents along the matrix rows), so it draws exactly (volume_project_axes), not as the AABB.
inline void area_obb(const float mn[3], const float mx[3], const float* m, Vec3* centre, Vec3* ax) {
  const float lc[3] = { (mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, (mn[2] + mx[2]) * 0.5f };
  centre->x = lc[0] * m[0] + lc[1] * m[4] + lc[2] * m[8] + m[12];
  centre->y = lc[0] * m[1] + lc[1] * m[5] + lc[2] * m[9] + m[13];
  centre->z = lc[0] * m[2] + lc[1] * m[6] + lc[2] * m[10] + m[14];
  for (int a = 0; a < 3; ++a) {
    const float h = (mx[a] - mn[a]) * 0.5f;
    ax[a].x = h * m[4 * a]; ax[a].y = h * m[4 * a + 1]; ax[a].z = h * m[4 * a + 2];
  }
}
// p inside the box of centre c and half-axis vectors ax (area_obb).
inline bool ent_point_in_obb(Vec3 p, Vec3 c, const Vec3 ax[3]) {
  const float d[3] = { p.x - c.x, p.y - c.y, p.z - c.z };
  for (int a = 0; a < 3; ++a) {
    const float l2 = ax[a].x * ax[a].x + ax[a].y * ax[a].y + ax[a].z * ax[a].z;
    const float t = d[0] * ax[a].x + d[1] * ax[a].y + d[2] * ax[a].z;
    if (t > l2 || t < -l2) return false;
  }
  return true;
}
// The point area_ladder_grab_test duke!0x1009a460 tests against a ladder volume: Duke's position raised by his
// half-height (+0x2BC, the y of the +0x2B8 half-extents).
inline Vec3 ent_ladder_head(Vec3 pos, Vec3 half) {
  const Vec3 h = { pos.x, pos.y + half.y, pos.z };
  return h;
}
// A found secret: +0xAE4 went 1 -> 0 when Duke entered it (watch log, 2026-09-29).
inline bool ent_secret_found(const EntSnap& s) { return s.cat == CAT_SECRET && !s.armed; }
// An explodable wall worth marking: intact (+0x2E0) and taking damage now (+0xC44).
inline bool ent_explodable_shown(const EntSnap& s) { return s.cat == CAT_EXPLODABLE && s.active && s.armed; }
// A crate or barrel entry once its holder broke: +0x2E0 cleared by the explode path duke!0x10068600; the freed
// pickup then draws as itself.
inline bool ent_crate_opened(const EntSnap& s) { return s.holder != 0 && !s.active; }
// A freed babe: babe_activate duke!0x10060ac0 moves +0xE10 from 0 (bound) to 1, and it never returns to 0.
inline bool ent_babe_freed(const EntSnap& s) { return s.cat == CAT_BABE && s.freed; }

struct ReadCache;
// The nukes a monster's drop list still names (re/pickups.md section 6), 0 for an entity that is no monster, -1 when
// the list does not read. rc covers the entity, listRc the list's heap block. Main thread.
int entities_carried_nukes(const unsigned char* e, ReadCache* rc, ReadCache* listRc);

// Main thread, frame path, once a frame. `player` is probe.cpp's validated
// player entity or NULL.
void entities_frame(const unsigned char* player);

const EntSnapshot& entities_snapshot(void);

// duke_base.dll's image [base, base+size): vtables, their methods and class names
// live there. Resolved on first use; false/0 until it is.
bool entities_duke_image(const unsigned char** base, unsigned* size);
bool entities_in_duke(const void* p);
