#pragma once
// Breakables that hand out a pickup when broken (re/pickups.md section 8): a breakable_adv whose `broken` pin drives
// a spawner_entity whose own `spawnscript` is a pickup script. Pure, so the suite pins it; areas.h parses world.scr,
// entities.cpp reads the engine.
#include "classes.h"
#include <string.h>

// The pwrup scripts craterule.h maps (section 5), by file name: what each spawns, as the crate table has it.
// nuke.sc is left out: a breakable never hands one out (census of all 129 maps), and nukes have their own tally.
inline bool brk_script_content(const char* script, unsigned char* kind, int* subtype) {
  if (!script) return false;
  const char* base = strrchr(script, '/');
  base = base ? base + 1 : script;
  static const struct { const char* file; unsigned char kind; int sub; } kTab[] = {
    { "sbullet.sc", ITEM_AMMO, 0 }, { "lbullet.sc", ITEM_AMMO, 1 }, { "sc5.sc", ITEM_AMMO, 2 },
    { "lc5.sc", ITEM_AMMO, 3 }, { "sglopp.sc", ITEM_AMMO, 4 }, { "lglopp.sc", ITEM_AMMO, 5 },
    { "shealth.sc", ITEM_EGO, 0 }, { "lhealth.sc", ITEM_EGO, 1 },
  };
  for (int i = 0; i < (int)(sizeof kTab / sizeof kTab[0]); ++i)
    if (_stricmp(base, kTab[i].file) == 0) { *kind = kTab[i].kind; *subtype = kTab[i].sub; return true; }
  return false;
}

// "<content> (breakable)", static strings: EntSnap.name never points at a buffer.
inline const char* brk_label(unsigned char kind, int subtype) {
  static const char* const kAmmo[6] = { "Bullets +10 (breakable)", "Bullets +50 (breakable)", "Pipebomb +1 (breakable)",
    "Pipebombs +10 (breakable)", "Glopp +25 (breakable)", "Glopp +100 (breakable)" };
  if (kind == ITEM_AMMO && subtype >= 0 && subtype < 6) return kAmmo[subtype];
  if (kind == ITEM_EGO && subtype == 0) return "Ego +10 (breakable)";
  if (kind == ITEM_EGO && subtype == 1) return "Ego +100 (breakable)";
  return 0;
}

// breakable_break duke!0x100f1600 clears +0x2E0; the spawned pickup then shows as itself.
inline bool brk_intact(unsigned char b2e0) { return b2e0 != 0; }
