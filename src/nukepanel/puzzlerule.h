#pragma once
// Puzzle solutions (Visuals WORLD): the next step of the two fixed-order puzzles, whether a wrong step needs the
// reset, and when to show the hints. Pure, so the suite runs it; puzzle.cpp reads the engine and draws.
// Evidence: docs/re/puzzle-e05p02.md (floor plates) and docs/re/puzzle-e07p03.md (cog buttons).
#include "w2s.h"   // Vec3
#include <string.h>

enum { PZ_NONE = 0, PZ_E05 = 1, PZ_E07 = 2 };
enum { PZ_STEPS = 3, PZ_RESET = 3 };
const float PZ_NEAR_UNITS = 1500.f;          // "a few metres"
const unsigned PZ_WRONG_HOLD_MS = 250;       // flip-flop/cog transients settle within a few delivery passes

// The map name at world+0x48, exactly: the other parts of e05p02 and e07p03 have no puzzle.
inline int pz_map_puzzle(const char* map) {
  if (!map) return PZ_NONE;
  if (!strcmp(map, "e05p02_a01")) return PZ_E05;
  if (!strcmp(map, "e07p03_a01")) return PZ_E07;
  return PZ_NONE;
}

// e05p02_a01, steps 0 Yellow, 1 Blue, 2 Red. down[i]: the plate's mover +0xE4C == 1; f1..f3 the flip-flop bits
// (+0x20C); platform: mover_spike_platform +0xE4C.
struct PzE05 { int down[PZ_STEPS]; int f1, f2, f3; int platform; };
inline bool pz_e05_solved(const PzE05& s) { return s.platform == 1 || (s.f1 && s.f2 && s.f3); }
// Blue counts only after Yellow (F2), Red only after both (F3): puzzle-e05p02.md section 5.
inline bool pz_e05_wrong(const PzE05& s) {
  const int y = s.down[0], b = s.down[1], r = s.down[2];
  return (b && !(y && s.f2)) || (r && !(y && b && s.f3));
}

// e07p03_a01, steps 0 switch_1 (top), 1 switch_2 (bottom), 2 switch_3 (middle). armed[i]: the button's +0xAE4;
// cogs 0..2 are cog_1..cog_3: opened +0xCAC, motion +0xF4C (1 opening, 0 closing, -1 at rest).
struct PzE07 { int armed[PZ_STEPS]; int opened[PZ_STEPS]; int motion[PZ_STEPS]; };
inline int pz_cog_target(int opened, int motion) { return motion == 1 ? 1 : motion == 0 ? 0 : opened != 0; }
// and_three1's own condition: all three cogs fully open.
inline bool pz_e07_solved(const PzE07& s) { return s.opened[0] && s.opened[1] && s.opened[2]; }
// The used buttons must be a prefix of the order, and the cogs must head where that prefix leaves them
// (puzzle-e07p03.md section 4).
inline bool pz_e07_wrong(const PzE07& s) {
  int k = 0;
  while (k < PZ_STEPS && !s.armed[k]) ++k;
  for (int i = k; i < PZ_STEPS; ++i)
    if (!s.armed[i]) return true;
  static const unsigned char want[PZ_STEPS + 1][PZ_STEPS] = { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 1, 1 } };
  for (int c = 0; c < PZ_STEPS; ++c)
    if (pz_cog_target(s.opened[c], s.motion[c]) != want[k][c]) return true;
  return false;
}

// What to draw: next is a step 0..2, PZ_RESET, or -1 for nothing; done ticks the steps already made. Only a pressed
// prefix is ticked, so a wrong step still waiting out the hold is never ticked.
struct PzView { int next; unsigned char done[PZ_STEPS]; };
inline PzView pz_view(const int* pressed, bool wrongShown) {
  PzView v = { -1, { 0, 0, 0 } };
  if (wrongShown) { v.next = PZ_RESET; return v; }
  for (int i = 0; i < PZ_STEPS; ++i) {
    if (!pressed[i]) { v.next = i; break; }
    v.done[i] = 1;
  }
  return v;
}

// `wrong` is acted on only after it has held PZ_WRONG_HOLD_MS; it drops at once.
struct PzDebounce { int raw; unsigned long long since; };
inline bool pz_wrong_shown(PzDebounce* d, bool raw, unsigned long long nowMs) {
  if ((d->raw != 0) != raw) { d->raw = raw ? 1 : 0; d->since = nowMs; }
  return raw && nowMs - d->since >= PZ_WRONG_HOLD_MS;
}

inline Vec3 pz_centre(const Vec3* p, int n) {
  Vec3 c = { 0.f, 0.f, 0.f };
  if (n <= 0) return c;
  for (int i = 0; i < n; ++i) { c.x += p[i].x; c.y += p[i].y; c.z += p[i].z; }
  c.x /= (float)n; c.y /= (float)n; c.z /= (float)n;
  return c;
}
inline bool pz_near(Vec3 player, Vec3 centre) {
  const float dx = player.x - centre.x, dy = player.y - centre.y, dz = player.z - centre.z;
  return dx * dx + dy * dy + dz * dz <= PZ_NEAR_UNITS * PZ_NEAR_UNITS;
}
inline bool pz_show(int rowOn, bool nearby, bool solved) { return rowOn != 0 && nearby && !solved; }

// The next step's outline alpha: a 1 s triangle wave between 0x50 and 0xFF.
inline int pz_pulse_alpha(unsigned long long ms) {
  const int t = (int)(ms % 1000ull);
  const int up = t <= 500 ? t : 1000 - t;
  return 0x50 + (0xFF - 0x50) * up / 500;
}
