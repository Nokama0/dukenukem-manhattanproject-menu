#pragma once
// Hide what you can't take: each pickup kind's own touch test, read in duke_base (re/pickups.md section 4).
// Pure, so the suite pins it; entities.cpp fills PlayerPools once a frame.
#include "classes.h"   // ITEM_*

enum { TAKE_YES = 0, TAKE_NO = 1, TAKE_UNKNOWN = 2 };

// The player's ammo pools from the stats block PS = *(player+0x111C): current shorts PS+0x38/+0x3A/+0x3C and the
// float caps PS+0x64/+0x68/+0x6C that ammo_on_touch duke!0x1005e6c0 compares them with. valid 0 when unread.
struct PlayerPools { int valid; int cur[3]; float cap[3]; };

// The hide decision: the switch gates the verdict, so switched off nothing is hidden for being untakeable.
inline bool item_hidden_by_take(int switchOn, int take) { return switchOn && take == TAKE_NO; }

// ammo_on_touch: ammo_type 0/1 fill pool 0, 2/3 pool 1, 4/5 pool 2, while the pool is below its cap (as an int); else
// refused. ego_on_touch 0x1005eb30 grants health_type 0/1 only, but the ego's init (0x1005eaf0) clamps the type to
// 0..1, so every ego is taken. subtype -1 (unread type, entities.cpp) is never judged: shown.
inline int pickup_can_take(int kind, int subtype, const PlayerPools* pp) {
  if ((kind == ITEM_AMMO || kind == ITEM_EGO) && subtype == -1) return TAKE_UNKNOWN;
  if (kind == ITEM_AMMO) {
    if (subtype < 0 || subtype > 5) return TAKE_NO;
    if (!pp || !pp->valid) return TAKE_UNKNOWN;
    const int pool = subtype / 2;
    const float cap = pp->cap[pool];
    if (!(cap > -2147483648.f && cap < 2147483648.f)) return TAKE_UNKNOWN;   // NaN or past an int: no answer
    return pp->cur[pool] < (int)cap ? TAKE_YES : TAKE_NO;
  }
  // gun 0x1005f1f0, key 0x1005f770, life 0x1005f8c0, jetpack 0x1005f400, forcefield 0x1005ecd0: picked with no test.
  return TAKE_YES;
}
