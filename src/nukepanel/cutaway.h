#pragma once
// Cut-away walls (View > CAMERA): every fader_entity held in its cut-away state
// (docs/re/cutaway.md). The pure rules; cutaway.cpp owns the walk, the writes and the undo.
#include <string.h>

// fader_entity_handler (vt+0x58) at +9: `mov eax,[esi+B70]; xor ecx,ecx; cmp eax,ecx; jz; cmp [esi+B74],ecx;
// jz; mov [esi+B88],2`: the pin flag/value and the state word, with the fade-out value 2 (cutaway.md 4).
enum { CUT_SIG_A_AT = 9, CUT_SIG_B_AT = 0x4D };
static const unsigned char kCutSigA[] = {
  0x8B, 0x86, 0x70, 0x0B, 0x00, 0x00, 0x33, 0xC9, 0x3B, 0xC1, 0x74, 0x24, 0x39, 0x8E, 0x74, 0x0B,
  0x00, 0x00, 0x74, 0x0C, 0xC7, 0x86, 0x88, 0x0B, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 };
// At +0x4D: fdiv [esi+B08] (fade_time), fsubr/fst [esi+B84] (alpha), fild [esi+B0C] (min_trans).
static const unsigned char kCutSigB[] = {
  0xD8, 0xB6, 0x08, 0x0B, 0x00, 0x00, 0xD8, 0xAE, 0x84, 0x0B, 0x00, 0x00,
  0xD9, 0x96, 0x84, 0x0B, 0x00, 0x00, 0xDB, 0x86, 0x0C, 0x0B, 0x00, 0x00 };
enum { CUT_SIG_SPAN = CUT_SIG_B_AT + (int)sizeof(kCutSigB) };

inline int cut_handler_sig_ok(const unsigned char* h) {
  if (!h) return 0;
  return !memcmp(h + CUT_SIG_A_AT, kCutSigA, sizeof kCutSigA) && !memcmp(h + CUT_SIG_B_AT, kCutSigB, sizeof kCutSigB)
             ? 1 : 0;
}

// The handler's own clamp value: fild min_trans, fmul 0.01f, fstp float (0x100bc1ef..0x100bc1fb).
inline float cut_min(int minPct) { return (float)minPct * 0.01f; }

enum { CUT_KEEP = 0, CUT_FORCE = 1, CUT_REASSERT = 2 };
// State 2 is what the handler writes on the switch's "on" signal; it then fades the wall down to min at the
// map's own speed. A write to an already-forced fader is a re-assert: the engine faded it back in.
inline int cut_decide(float alpha, int minPct, int state, int forcedBefore) {
  if (!(alpha > cut_min(minPct)) || state == 2) return CUT_KEEP;
  return forcedBefore ? CUT_REASSERT : CUT_FORCE;
}

// Undo touches only faders we forced that this frame's walk returned (so still alive); out gets their indexes.
inline int cut_undo_pick(const unsigned char* forced, const unsigned char* seen, int n, int* out) {
  int k = 0;
  for (int i = 0; i < n; ++i)
    if (forced[i] && seen[i]) out[k++] = i;
  return k;
}

// One fader the walk has returned. `seen`: returned by the latest walk.
struct CutFader {
  const void* ent;
  const void* vt;
  unsigned char forced, seen, loggedSettle;
  unsigned long long t0;
  float before;
};
// After an uncapped walk: entries the search no longer returns are dropped, order kept, so reloads that
// reallocate faders cannot fill the table. Returns the new count.
inline int cut_prune(CutFader* f, int n) {
  int k = 0;
  for (int i = 0; i < n; ++i)
    if (f[i].seen) f[k++] = f[i];
  return k;
}

// --- The glue (cutaway.cpp), main thread, frame path ---------------------------
// Once a frame; `player` is probe.cpp's validated player or NULL. Reads g_cfg.cutaway.
void cutaway_frame(const unsigned char* player);
// The handler signature or the pin_t::set export failed: the row shows unavailable.
bool cutaway_refused(void);
