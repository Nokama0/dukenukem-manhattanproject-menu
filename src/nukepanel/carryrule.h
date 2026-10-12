#pragma once
// Enemies that carry a nuke (re/pickups.md section 6): a monster's `spawnscript` list is what it drops, and
// monster_drop_all duke!0x10035450 spawns each entry at its body when it dies, then empties the list. Pure, so the
// suite pins it; entities.cpp and areas.cpp read the engine.
#include <string.h>
#include <stdio.h>

// The list: char[64] entries, +0xCA0 pointer and +0xCA4 count (monster base ctor duke!0x1002bd80; the count is what
// monster_drop_one 0x1001efe0 pops from the end). More than any shipped monster lists (3, e02p03_a02's boss).
enum { CARRY_SCRIPT_LEN = 64, CARRY_DROPS_MAX = 8 };
static const char CARRY_NUKE_SCRIPT[] = "scripts/pwrup/nuke.sc";
// The name a dropped pickup gets: v_snprintf("drop-%s", its class) in both drop paths.
static const char CARRY_DROP_NAME[] = "drop-nuke";

inline bool carry_is_nuke_script(const char* s) { return s && _stricmp(s, CARRY_NUKE_SCRIPT) == 0; }

// The world.scr classes built on the monster base (their `spawnscript` lines are drops). A spawner_entity's
// `spawnscript` is what it spawns and its `monst_spawnscr` the spawned monster's list: scripted, not counted.
inline bool carry_class(const char* cls) {
  return cls && (strncmp(cls, "monst_", 6) == 0 || strncmp(cls, "boss_", 5) == 0);
}

// The nukes among the first n entries of a drop list; an entry with no NUL inside its 64 bytes is no script.
inline int carry_nukes(const char* list, int n) {
  if (!list) return 0;
  int k = 0;
  for (int i = 0; i < n; ++i) {
    const char* e = list + (size_t)i * CARRY_SCRIPT_LEN;
    if (memchr(e, 0, CARRY_SCRIPT_LEN) && carry_is_nuke_script(e)) ++k;
  }
  return k;
}

// The enemy label's suffix (drawn green): " + Nuke", " + 2 Nukes"; "" for none.
inline const char* carry_suffix(int n, char* buf, int cap) {
  if (n <= 0) buf[0] = 0;
  else if (n == 1) _snprintf_s(buf, (size_t)cap, _TRUNCATE, " + Nuke");
  else _snprintf_s(buf, (size_t)cap, _TRUNCATE, " + %d Nukes", n);
  return buf;
}

// The small nuke marker's label when the enemy's own label is off.
inline const char* carry_marker_label(int n, char* buf, int cap) {
  if (n <= 1) _snprintf_s(buf, (size_t)cap, _TRUNCATE, "Nuke (enemy)");
  else _snprintf_s(buf, (size_t)cap, _TRUNCATE, "%d Nukes (enemy)", n);
  return buf;
}
