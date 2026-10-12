#pragma once
// Class name (vtable slot 1, get_classname) -> ESP category and display name. The game ships no readable names
// (docs/re/entities.md section 3), so this table is ours; the names are provisional.
#include <string.h>
#include <stdio.h>

enum { CAT_OTHER = 0, CAT_ENEMY = 1, CAT_BOSS = 2, CAT_ITEM = 3, CAT_KEY = 4, CAT_NUKE = 5, CAT_BABE = 6,
       CAT_SECRET = 7, CAT_EXPLODABLE = 8, CAT_AMBUSH = 9, CAT_COUNT = 10 };
// What a CAT_ITEM pickup gives; ITEM_NONE for every other category.
enum { ITEM_NONE = 0, ITEM_AMMO, ITEM_LIFE, ITEM_EGO, ITEM_GUN, ITEM_JETPACK, ITEM_FORCEFIELD };
// 1..3 are the class suffix digits (none reads as 1); B is the elite variant.
enum { TIER_NONE = 0, TIER_1 = 1, TIER_2 = 2, TIER_3 = 3, TIER_B = 4 };

struct EntClass { unsigned char cat; unsigned char tier; unsigned char kind; const char* name; };

// One base name per class, no tier suffix (class_name_with_variant appends it).
// Words are spaced. Names follow the wiki.
struct ClassRow { const char* key; unsigned char cat; const char* name; };
static const ClassRow kClassTable[] = {
  { "monst_rat",              CAT_ENEMY, "Rat" },
  { "monst_pig",              CAT_ENEMY, "Pig" },
  { "monst_fly",              CAT_ENEMY, "Fly" },
  { "monst_gator",            CAT_ENEMY, "Gator" },
  { "monst_roach",            CAT_ENEMY, "Roach" },
  { "monst_pigoid",           CAT_ENEMY, "Pig Cop" },
  { "monst_femech",           CAT_ENEMY, "Fem Mech" },
  { "monst_hovermech",        CAT_ENEMY, "Hover Mech" },
  { "monst_gatoroid",         CAT_ENEMY, "Gator Oid" },
  { "monst_ratoid",           CAT_ENEMY, "Rat Oid" },
  { "monst_roachoid",         CAT_ENEMY, "Roach Oid" },
  { "monst_gloppoid",         CAT_ENEMY, "Glopp Oid" },
  { "monst_securitymech",     CAT_ENEMY, "Security Mech" },
  { "boss_porkchopper",       CAT_BOSS,  "Helicopter" },
  { "boss_porkchopper_child", CAT_BOSS,  "Helicopter Pod" },   // not wiki-named; guess kept
  { "boss_queenroach",        CAT_BOSS,  "Cockroach Queen" },
  { "boss_mechnukem",         CAT_BOSS,  "Robo Duke" },
  { "boss_mechnukem_head",    CAT_BOSS,  "Robo Duke Head" },
  { "mechnukem_crane",        CAT_BOSS,  "Mechanical Crane" },
  { "boss_wozma",             CAT_BOSS,  "Wozma" },
};

// The variant text: "" for tier 1 and unclassified, the digit for 2/3, "Boss" for the elite suffix.
inline const char* class_variant_label(unsigned char tier) {
  if (tier == TIER_2) return "2";
  if (tier == TIER_3) return "3";
  if (tier == TIER_B) return "Boss";
  return "";
}

// Name plus " variant" on the same line when showLevel (always, since the 2026-10-02 clean-up removed the
// Level row); truncates to the buffer.
inline void class_name_with_variant(char* buf, size_t cap, const char* name, unsigned char tier, bool showLevel) {
  const char* v = showLevel ? class_variant_label(tier) : "";
  if (v[0]) _snprintf_s(buf, cap, _TRUNCATE, "%s %s", name, v);
  else _snprintf_s(buf, cap, _TRUNCATE, "%s", name);
}

// Pickups (category powerup), babes, secret volumes, explodables and roach eggs (dumps/duke_base.classes.txt).
// Exact names only: "key" must not claim "key_hole", nor "jetpack" "actor_jetpack".
struct ObjRow { const char* key; unsigned char cat, kind; const char* name; };
static const ObjRow kObjTable[] = {
  { "ammo",        CAT_ITEM,   ITEM_AMMO,       "Ammo" },
  { "life",        CAT_ITEM,   ITEM_LIFE,       "Double Damage" },   // pickups.md section 3 ruling
  { "ego",         CAT_ITEM,   ITEM_EGO,        "Ego" },
  { "gun",         CAT_ITEM,   ITEM_GUN,        "Weapon" },
  { "jetpack",     CAT_ITEM,   ITEM_JETPACK,    "Jetpack" },
  { "forcefield",  CAT_ITEM,   ITEM_FORCEFIELD, "Force Field" },
  { "key",         CAT_KEY,    ITEM_NONE,       "Key" },
  { "nuke",        CAT_NUKE,   ITEM_NONE,       "Nuke" },
  { "babe",        CAT_BABE,   ITEM_NONE,       "Babe" },
  { "area_secret", CAT_SECRET, ITEM_NONE,       "Secret" },
  // The map's brk_* walls (docs/re/explodables.md). Not breakable_obstacle: e01p03_a02's respawning,
  // break-on-touch track obstacles, no wall.
  { "breakable_adv", CAT_EXPLODABLE, ITEM_NONE, "Explodable" },
  // The roach eggs of episodes 4 and 6 (eggrule.h; docs/re/entities.md section 8): destroyable, they hatch Roaches.
  { "roach_pod",     CAT_ENEMY,      ITEM_NONE, "Roach Egg" },
};

// key+0xD14 values 0/1/2 (key_get_name duke!0x1005f7c0); anything else reads as a plain key.
inline const char* keycard_colour_name(int colour) {
  static const char* const kNames[3] = { "Blue Key", "Yellow Key", "Red Key" };
  return colour >= 0 && colour < 3 ? kNames[colour] : "Key";
}

// ammo/ego/gun +0xD14 (ammo_type, health_type, gun_type; pickups.md sections 1-2). A value
// the grant code has no case for reads as the class name, which may be NULL.
inline const char* item_label(unsigned char kind, int subtype, const char* className) {
  // The C5 pool's pickups are pipebombs in the game's own pickup text (local.def p_pipebomb(s)).
  static const char* const kAmmo[6] = { "Bullets +10", "Bullets +50", "Pipebomb +1", "Pipebombs +10", "Glopp +25",
                                        "Glopp +100" };
  static const char* const kEgo[2] = { "Ego +10", "Ego +100" };
  // gun_type 0..5 in the order of the pickup's model table (duke!0x1005f230, e01/weap/shotgun at 5), which is what
  // the player sees; the labels are the wiki's.
  static const char* const kGun[6] = { "Golden Eagle", "Assault Rifle", "G.L.O.P.P. Ray", "Pulse Cannon", "PRPG",
                                       "Shotgun" };
  if (kind == ITEM_AMMO && subtype >= 0 && subtype < 6) return kAmmo[subtype];
  if (kind == ITEM_EGO && subtype >= 0 && subtype < 2) return kEgo[subtype];
  if (kind == ITEM_GUN && subtype >= 0 && subtype <= 5) return kGun[subtype];
  return className;
}

// The suffix after a key must be empty, one digit 1-3 or "B": "monst_pig" must
// not claim "monst_pigoid1", and "monst_talker_mgr" matches no key at all.
inline int class_tier_of(const char* rest) {
  if (rest[0] == 0) return TIER_1;
  if (rest[1] != 0) return TIER_NONE;
  if (rest[0] >= '1' && rest[0] <= '3') return rest[0] - '0';
  return rest[0] == 'B' ? TIER_B : TIER_NONE;
}

inline EntClass class_lookup(const char* cls) {
  EntClass c = { CAT_OTHER, TIER_NONE, ITEM_NONE, 0 };
  if (!cls) return c;
  for (unsigned i = 0; i < sizeof(kObjTable) / sizeof(kObjTable[0]); ++i) {
    if (strcmp(cls, kObjTable[i].key) != 0) continue;
    c.cat = kObjTable[i].cat; c.kind = kObjTable[i].kind; c.name = kObjTable[i].name;
    return c;
  }
  for (unsigned i = 0; i < sizeof(kClassTable) / sizeof(kClassTable[0]); ++i) {
    const ClassRow& r = kClassTable[i];
    const size_t n = strlen(r.key);
    if (strncmp(cls, r.key, n) != 0) continue;
    const int tier = class_tier_of(cls + n);
    if (tier == TIER_NONE) continue;
    c.cat = r.cat; c.tier = (unsigned char)tier; c.name = r.name;
    return c;
  }
  return c;
}
