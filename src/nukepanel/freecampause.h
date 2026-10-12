#pragma once
// Free camera > Pause the game: when to freeze the world and when to let it run again. Pure; the glue is in
// freecam.cpp. The lever is the engine's own pause without its flag: process_sleep / process_wakeup on the
// player's world, the calls duke's pause makes on each game server (docs/re/freecam-pause.md).

enum { FCP_NONE = 0, FCP_SLEEP = 1, FCP_WAKE = 2, FCP_LET_GO = 3 };
enum { FCP_WHY_NONE = 0, FCP_WHY_ENDED = 1, FCP_WHY_SCREEN = 2, FCP_WHY_GONE = 3, FCP_WHY_NEW_WORLD = 4 };

// world: the world this flight froze. blocked: a level change or a lost world ended the pause for the rest of
// this flight (it resumes on a level change). why: the reason of the last WAKE or LET_GO.
struct FcPause { const void* world; int held, blocked, wasArmed, why; };

// Ends the hold. A world no longer in the engine's process list is never touched. Under the engine's own menu
// or help screen the world is left asleep: those screens wake it when they close (0x100a4c90, 0x100e4360).
inline int fcp_release(FcPause* s, int alive, int screen, int why) {
  const int held = s->held;
  s->world = 0;
  s->held = 0;
  if (!held) return FCP_NONE;
  s->why = alive && screen ? FCP_WHY_SCREEN : !alive && why == FCP_WHY_ENDED ? FCP_WHY_GONE : why;
  return alive && !screen ? FCP_WAKE : FCP_LET_GO;
}

// Once a frame. armed: the row and the flight both on. cur: the player's world (0 without one). alive: s->world (else
// cur) is a live engine process. screen: the engine's menu or help screen is up. SLEEP comes every frame while held:
// process_sleep is a no-op on a sleeping world and re-freezes one the engine's menu woke on closing.
inline int fcp_step(FcPause* s, int armed, const void* cur, int alive, int screen) {
  s->why = FCP_WHY_NONE;
  if (armed && !s->wasArmed) s->blocked = 0;
  s->wasArmed = armed;
  if (s->world && cur && cur != s->world) {
    s->blocked = 1;
    return fcp_release(s, alive, screen, FCP_WHY_NEW_WORLD);
  }
  if (armed && !s->blocked) {
    if (!s->world) {
      if (!cur || !alive) return FCP_NONE;
      s->world = cur;
    } else if (!alive) {
      s->blocked = 1;
      return fcp_release(s, 0, screen, FCP_WHY_GONE);
    }
    s->held = 1;
    return FCP_SLEEP;
  }
  return s->world ? fcp_release(s, alive, screen, FCP_WHY_ENDED) : FCP_NONE;
}

inline const char* fcp_why_text(int why) {
  switch (why) {
    case FCP_WHY_ENDED: return "the flight or the row ended";
    case FCP_WHY_SCREEN: return "the engine's menu is up and wakes it on closing";
    case FCP_WHY_GONE: return "the world left the engine's process list";
    case FCP_WHY_NEW_WORLD: return "a new level";
    default: return "none";
  }
}
