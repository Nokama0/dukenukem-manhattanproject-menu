#pragma once
// Crates and barrels that hold a pickup (re/pickups.md section 5): their `contents` codes, what each spawns, and
// the label the ESP draws at the holder. Pure, so the suite pins it; entities.cpp reads the codes.
#include "classes.h"
#include <string.h>

enum { HOLDER_NONE = 0, HOLDER_CRATE = 1, HOLDER_BARREL = 2 };
// More codes than any placed holder has (the most is 5, demo_crate_barrel's crate3); a corrupt count reads no further.
enum { CRATE_CONTENTS_MAX = 8 };

struct CrateContent { unsigned char cat, kind; int subtype; };

// Exact class names: the two dobj classes built on the contents base duke!0x10067d90.
inline int crate_holder_of(const char* cls) {
  if (!cls) return HOLDER_NONE;
  if (!strcmp(cls, "crate")) return HOLDER_CRATE;
  if (!strcmp(cls, "barrel")) return HOLDER_BARREL;
  return HOLDER_NONE;
}

// FUN_100684b0 picks a spawn script by code, holder (+0xBF4: crate 1, barrel 0) and type (+0xBF8, crate_type or
// barrel_type); the scripts (scripts/pwrup/*.sc) spawn the pickup with the subtype given here. A code it has no
// script for spawns nothing: CAT_OTHER. lhealth.sc writes health_type 2, which the ego's init clamps to 1 (+100).
inline CrateContent crate_content(int holder, int type, int code) {
  CrateContent c = { CAT_OTHER, ITEM_NONE, -1 };
  if (holder == HOLDER_NONE) return c;
  if (code == 0) { c.cat = CAT_NUKE; return c; }   // nuke.sc, whatever the holder and type
  int kind = ITEM_NONE, sub = -1;
  if (holder == HOLDER_BARREL) {
    if (type == 0 && code == 1) { kind = ITEM_EGO; sub = 0; }        // shealth.sc
    else if (type == 0 && code == 2) { kind = ITEM_EGO; sub = 1; }   // lhealth.sc: 2, clamped to 1
  } else if (type == 0) {
    static const int kKind[4] = { ITEM_AMMO, ITEM_AMMO, ITEM_EGO, ITEM_EGO }, kSub[4] = { 0, 1, 0, 1 };
    if (code >= 1 && code <= 4) { kind = kKind[code - 1]; sub = kSub[code - 1]; }   // s/lbullet, s/lhealth
  } else if ((type == 1 || type == 2) && (code == 1 || code == 2)) {
    kind = ITEM_AMMO; sub = (type == 1 ? 2 : 4) + code - 1;   // s/lc5, s/lglopp
  }
  if (kind == ITEM_NONE) return c;
  c.cat = CAT_ITEM; c.kind = (unsigned char)kind; c.subtype = sub;
  return c;
}

// item_label's text (or the nuke's) plus the holder, as static strings: EntSnap.name never points at a buffer.
inline const char* crate_content_label(int holder, const CrateContent& c) {
  static const char* const kLabel[2][9] = {
    { "Nuke (crate)", "Bullets +10 (crate)", "Bullets +50 (crate)", "Pipebomb +1 (crate)", "Pipebombs +10 (crate)",
      "Glopp +25 (crate)", "Glopp +100 (crate)", "Ego +10 (crate)", "Ego +100 (crate)" },
    { "Nuke (barrel)", "Bullets +10 (barrel)", "Bullets +50 (barrel)", "Pipebomb +1 (barrel)",
      "Pipebombs +10 (barrel)", "Glopp +25 (barrel)", "Glopp +100 (barrel)", "Ego +10 (barrel)", "Ego +100 (barrel)" },
  };
  if (holder != HOLDER_CRATE && holder != HOLDER_BARREL) return 0;
  const char* const* row = kLabel[holder - 1];
  if (c.cat == CAT_NUKE) return row[0];
  if (c.cat != CAT_ITEM) return 0;
  if (c.kind == ITEM_AMMO && c.subtype >= 0 && c.subtype < 6) return row[1 + c.subtype];
  if (c.kind == ITEM_EGO) return c.subtype == 0 ? row[7] : row[8];   // only 0 and 1 are ever spawned
  return 0;
}
