#pragma once
// Game speed's pure rules (docs/re/game-speed.md): the scaled clock prism3d's tmr_read reads through our timeGetTime
// export, and which callers get it. gamespeed.cpp owns the state; everything here is the suite's.
#include <stdio.h>

enum { GS_PCT_MIN = 10, GS_PCT_MAX = 400, GS_PCT_STEP = 5, GS_PCT_DEF = 50 };

// scaled = baseScaled + (real - baseReal) * pct / 100, all in timeGetTime's 32-bit milliseconds. Computed from the
// base on every call, so no fraction of a millisecond is ever dropped and the clock never steps back between rebases.
struct GsClock { unsigned baseReal, baseScaled; int pct; };

// The real clock exactly: what game callers get until the first speed change.
inline GsClock gs_clock_start(void) {
  GsClock c = { 0u, 0u, 100 };
  return c;
}

// Unsigned subtraction, so the real clock's 49.7-day wrap reads as the time that passed.
inline unsigned gs_scaled(const GsClock& c, unsigned real) {
  const unsigned elapsed = real - c.baseReal;
  return c.baseScaled + (unsigned)(((unsigned long long)elapsed * (unsigned)c.pct) / 100u);
}

// A new speed from `real` on, starting where the old one stands then: the clock stays continuous.
inline GsClock gs_rebase(const GsClock& c, unsigned real, int pct) {
  GsClock n = { real, gs_scaled(c, real), pct };
  return n;
}

// The elapsed time must never reach 2^32 ms or it would read as short; rebasing at the same speed is free.
inline bool gs_needs_rebase(const GsClock& c, unsigned real) { return real - c.baseReal >= 0x40000000u; }

// The row's value on the 5 % steps, inside 10-400 %.
inline int gs_pct_snap(int pct) {
  if (pct < GS_PCT_MIN) pct = GS_PCT_MIN;
  if (pct > GS_PCT_MAX) pct = GS_PCT_MAX;
  return (pct + GS_PCT_STEP / 2) / GS_PCT_STEP * GS_PCT_STEP;
}

// A module's image, [lo, hi): a caller whose return address lies inside gets game time. The empty range claims none.
struct GsRange { unsigned lo, hi; };
inline bool gs_in_range(const GsRange& r, unsigned addr) { return addr >= r.lo && addr < r.hi; }

// The corner tag next to the level stats readout.
inline void gs_tag_text(int pct, char* out, int cap) {
  if (!out || cap <= 0) return;
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "Speed %d%%", pct);
}
