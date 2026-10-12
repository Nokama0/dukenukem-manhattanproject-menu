#pragma once
// Whether a boss target can take damage now, by each boss's own damage gate (docs/re/aim.md section 7).
// No engine flag is shared by the bosses: each reason below is one handler's own test, read in Ghidra
// 2026-10-02. A target is only ruled out when its handler is read to refuse; unread means open.
#include <string.h>

// Why a candidate is skipped as "immune now". BOSS_OPEN: nothing read refuses it.
enum { BOSS_OPEN = 0, BOSS_IMM_NOHANDLER, BOSS_IMM_PORK_CORE, BOSS_IMM_PORK_PART, BOSS_IMM_QUEEN,
       BOSS_IMM_MECHHEAD, BOSS_IMM_WOZMA, BOSS_IMM_EGG, BOSS_IMM_COUNT };

inline const char* boss_imm_label(int why) {
  static const char* const k[BOSS_IMM_COUNT] = { "open", "noHandler", "porkCore", "porkPart", "queen",
                                                 "mechHead", "wozma", "eggIdle" };
  return why >= 0 && why < BOSS_IMM_COUNT ? k[why] : "?";
}

// The helicopter's three boss_porkchopper_child parts. porkchild_think duke!0x10010ed0 switches on the
// name's 13th character (+0x10: "porkchopper_engine" / "_vulcan" / "_wing", names set by the boss init
// 0x10006e30).
enum { PORK_PART_ENGINE = 'e', PORK_PART_VULCAN = 'v', PORK_PART_WING = 'w' };
enum { PORK_PART_LETTER_AT = 12 };   // index into the name (+4), so the engine's own +0x10 byte

// boss_porkchopper fields the parts' gates read (all from porkchild_think 0x10010ed0).
struct PorkFacts {
  float health;        // +0x145C, shared: the wing and the engine take it down
  int active;          // byte +0x1468
  int state;           // +0x146C
  float dropPigHealth; // +0x14B4 (def drop_pig_health)
  int pigsLeft;        // +0x14BC (def drop_pig_num, counted down)
  float vulcanHealth;  // +0x1534 (def vulcan_health)
  int vulcanOut;       // byte +0x1570
};

// 1 when porkchild_think would subtract this part's damage now: wing (0x10010f3c) active and health > drop_pig_health;
// engine (0x10011bfc) active, no pigs left and health > 0; vulcan (0x100116ae) vulcanOut, active, health > 0, state 3.
// An unknown letter is not a part the think handles: 0.
inline int pork_part_open(int letter, const PorkFacts& p) {
  if (!p.active) return 0;
  if (letter == PORK_PART_WING) return p.dropPigHealth < p.health ? 1 : 0;
  if (letter == PORK_PART_ENGINE) return p.pigsLeft <= 0 && p.health > 0.f ? 1 : 0;
  if (letter == PORK_PART_VULCAN) return p.vulcanOut && p.vulcanHealth > 0.f && p.state == 3 ? 1 : 0;
  return 0;
}

// The helicopter core's own vt+0x198 (0x100094a0) never takes health: only its parts do.
inline int pork_core_open(void) { return 0; }

// boss_queenroach vt+0x198 0x10013b00: health +0x1690 > 0 and the byte +0x16B8 set (it clears it at each
// phase threshold).
inline int queen_open(float health, int vulnerable) { return health > 0.f && vulnerable ? 1 : 0; }

// boss_mechnukem_head vt+0x198 0x10005760: refuses in states +0xF4C 2, 4, 5, 7, 8 and while +0x10C0 == 2.
inline int mechhead_open(int state, int mode) {
  if (state == 2 || state == 4 || state == 5 || state == 7 || state == 8) return 0;
  return mode != 2 ? 1 : 0;
}

// boss_wozma vt+0x198 0x10017520: takes health only while the byte +0xB4C is set (its attacker test,
// +0xB5C == get_local_player 0x100e7060 at 0x100177f6, passes for Duke).
inline int wozma_open(int armed) { return armed ? 1 : 0; }

// The part's display name by its letter; NULL for an unknown letter.
inline const char* pork_part_name(int letter) {
  if (letter == PORK_PART_ENGINE) return "Helicopter Engine";
  if (letter == PORK_PART_WING) return "Helicopter Wing";
  if (letter == PORK_PART_VULCAN) return "Helicopter Gun";
  return 0;
}

// A boss whose damage lands only on separate part entities: its slots hold the parts, each part points back at the
// boss. Only the helicopter has any (docs/re/aim.md): the queen (init 0x10012aa0), Wozma (0x10017330), the Robo Duke
// body (0x100013e0, ray only) and head (0x10002330, jet_exhaust only) spawn none that take damage.
struct BossPartsSpec {
  const char* cls;     // the boss's get_classname
  unsigned firstSlot;  // the first part pointer in the boss (boss init 0x10006e30)
  int count;           // consecutive pointer slots
  unsigned backLink;   // part -> boss (child ctor 0x10010d80's layout, written by the boss init)
  unsigned partSpan;   // the part's alloc (dumps/duke_base.classes.txt): every read stays inside it
};
// One row today. entities.cpp's boss_parts still reads the helicopter's own gate fields (porkchild think),
// so a second row needs its own gate there before it can be added here.
inline const BossPartsSpec* boss_parts_spec(const char* cls) {
  static const BossPartsSpec k[] = { { "boss_porkchopper", 0x15B8, 3, 0xB1C, 0xB38 } };
  if (!cls) return 0;
  for (unsigned i = 0; i < sizeof k / sizeof k[0]; ++i) if (!strcmp(cls, k[i].cls)) return &k[i];
  return 0;
}

// One counter row per boss entity and helicopter part (the aim's per-boss log, as immuneBy is per reason).
enum { BOSS_K_NONE = 0, BOSS_K_PORK_CORE, BOSS_K_PORK_WING, BOSS_K_PORK_ENGINE, BOSS_K_PORK_GUN, BOSS_K_QUEEN,
       BOSS_K_MECH_BODY, BOSS_K_MECH_HEAD, BOSS_K_CRANE, BOSS_K_WOZMA, BOSS_K_COUNT };
inline int boss_kind_of(const char* cls) {
  static const char* const k[BOSS_K_COUNT] = { 0, "boss_porkchopper", 0, 0, 0, "boss_queenroach", "boss_mechnukem",
                                               "boss_mechnukem_head", "mechnukem_crane", "boss_wozma" };
  if (!cls) return BOSS_K_NONE;
  for (int i = 1; i < BOSS_K_COUNT; ++i) if (k[i] && !strcmp(cls, k[i])) return i;
  return BOSS_K_NONE;
}
inline int boss_kind_of_part(int letter) {
  if (letter == PORK_PART_WING) return BOSS_K_PORK_WING;
  if (letter == PORK_PART_ENGINE) return BOSS_K_PORK_ENGINE;
  if (letter == PORK_PART_VULCAN) return BOSS_K_PORK_GUN;
  return BOSS_K_NONE;
}
inline const char* boss_kind_label(int k) {
  static const char* const n[BOSS_K_COUNT] = { "none", "porkCore", "wing", "engine", "gun", "queen", "roboBody",
                                               "roboHead", "crane", "wozma" };
  return k >= 0 && k < BOSS_K_COUNT ? n[k] : "?";
}
