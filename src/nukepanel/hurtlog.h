#pragma once
// Duke's damage log (docs/re/queen-burst.md): the logging rules plus the Fix Queen swipe gate, nothing else.
// hurtlog.cpp owns the player_take_damage detour, the health reads and the log lines.
#include <string.h>

// player_take_damage duke!0x10078de0 = actor_player vt+0x198, thiscall (type, amount, vec3* dir, inflictor,
// attacker), RET 0x14. Stock entry: sub esp,0xC; push esi; mov esi,ecx; mov eax,[esi+0x111C]; fld [eax+0x14].
// The first 6 bytes are three whole instructions with no relative operand: they move to the trampoline.
enum { HURT_FP_LEN = 15, HURT_TRAMP_LEN = 6 };
static const unsigned char kHurtSendFp[HURT_FP_LEN] = {
  0x83, 0xEC, 0x0C, 0x56, 0x8B, 0xF1, 0x8B, 0x86, 0x1C, 0x11, 0x00, 0x00, 0xD9, 0x40, 0x14 };

// A burst: Duke loses at least HURT_BURST_LOSS health inside HURT_WINDOW_MS. It stays open while drops keep
// coming less than a window apart, and closes (one log line, every event in it) after a quiet window.
enum { HURT_KEEP = 64 };
const unsigned HURT_WINDOW_MS = 250;
const int HURT_BURST_LOSS = 50;

// One health drop seen by the frame watcher, with the sends since the previous frame (the first one's
// type, amount and attacker tag; src indexes hurtlog.cpp's class table, -1 none).
struct HurtEv { unsigned t; int loss; int before, after; int sends; int type; float amount; int src; };
struct HurtBurst { HurtEv ev[HURT_KEEP]; int n; int open; int total; unsigned tFirst, tLast, dropped; };

inline void hurt_reset(HurtBurst* b) { if (b) memset(b, 0, sizeof *b); }

// Feeds one drop. Closed: the list is the sliding window (ages are unsigned differences, exact across the
// clock's wrap) and opens when its losses reach HURT_BURST_LOSS. Open: every drop is kept, up to HURT_KEEP
// (the rest counted in dropped, their loss still in total).
inline void hurt_feed(HurtBurst* b, const HurtEv& e) {
  if (!b) return;
  b->tLast = e.t;
  if (b->open) {
    b->total += e.loss;
    if (b->n < HURT_KEEP) b->ev[b->n++] = e; else b->dropped++;
    return;
  }
  int keep = 0;
  while (keep < b->n && (unsigned)(e.t - b->ev[keep].t) >= HURT_WINDOW_MS) keep++;
  if (keep) { memmove(b->ev, b->ev + keep, sizeof(HurtEv) * (size_t)(b->n - keep)); b->n -= keep; }
  if (b->n == HURT_KEEP) { memmove(b->ev, b->ev + 1, sizeof(HurtEv) * (HURT_KEEP - 1)); b->n--; }
  b->ev[b->n++] = e;
  b->total = 0;
  for (int i = 0; i < b->n; ++i) b->total += b->ev[i].loss;
  if (b->total >= HURT_BURST_LOSS) { b->open = 1; b->tFirst = b->ev[0].t; }
}

// 1 once an open burst has had a quiet window; the caller logs it and resets.
inline int hurt_close_due(const HurtBurst& b, unsigned now) {
  return b.open && (unsigned)(now - b.tLast) >= HURT_WINDOW_MS ? 1 : 0;
}

// Identical lines (same key) within HURT_QUIET_MS print once; the next printed line reports how many were held
// since the last print, whatever their key.
const unsigned HURT_QUIET_MS = 1000;
struct HurtQuiet { unsigned key; unsigned t; unsigned held; int any; };
inline int hurt_quiet_pass(HurtQuiet* q, unsigned key, unsigned now, unsigned* heldOut) {
  if (q->any && q->key == key && (unsigned)(now - q->t) < HURT_QUIET_MS) { q->held++; return 0; }
  if (heldOut) *heldOut = q->held;
  q->key = key; q->t = now; q->held = 0; q->any = 1;
  return 1;
}

// Cumulative sends per (attacker class tag, damage type): count and amount. A full table counts in full.
enum { HURT_TALLY_MAX = 32 };
struct HurtTally { int src[HURT_TALLY_MAX], type[HURT_TALLY_MAX]; unsigned count[HURT_TALLY_MAX];
                   float amount[HURT_TALLY_MAX]; int n; unsigned full; };
inline void hurt_tally_add(HurtTally* t, int src, int type, float amount) {
  if (!t) return;
  for (int i = 0; i < t->n; ++i)
    if (t->src[i] == src && t->type[i] == type) { t->count[i]++; t->amount[i] += amount; return; }
  if (t->n >= HURT_TALLY_MAX) { t->full++; return; }
  t->src[t->n] = src; t->type[t->n] = type; t->count[t->n] = 1; t->amount[t->n] = amount; t->n++;
}

// Fix Queen swipe (queen-burst.md section 5). Phase-2 swipe FUN_10016110 sends its block-1 hit every think in contact;
// only the first per attack (Queen + attack start +0x16C4, set per swipe by FUN_10015e50) reaches Duke. Block 2 (1.5 s
// hit, knock 0x1C2) and other sends pass. Block 1's site (duke!0x100161eb) is the bytes below, ending `push 0x226`.
enum { QS_SITE_LEN = 42, QS_SITE_RET = 32 };
static const unsigned char kQsSite[QS_SITE_LEN] = {
  0x8B, 0x86, 0xEC, 0x16, 0x00, 0x00, 0x8B, 0x8E, 0x34, 0x18, 0x00, 0x00, 0x8B, 0x11, 0x56, 0x56,
  0x8D, 0xBE, 0x8C, 0x17, 0x00, 0x00, 0x57, 0x50, 0x6A, 0x01, 0xFF, 0x92, 0x98, 0x01, 0x00, 0x00,
  0x8B, 0x17, 0x8B, 0x47, 0x04, 0x68, 0x26, 0x02, 0x00, 0x00 };
const unsigned QS_OFS_START = 0x16C4;
struct QsGate { const void* queen; unsigned start; int passed; };
// 1 = the send reaches Duke, 0 = dropped. block1: the return address is block 1's and the attacker is the Queen.
inline int qs_gate(QsGate* g, int on, int block1, const void* queen, unsigned startBits) {
  if (!on || !block1 || !queen) return 1;
  if (g->queen != queen || g->start != startBits) { g->queen = queen; g->start = startBits; g->passed = 0; }
  if (g->passed) return 0;
  g->passed = 1;
  return 1;
}

// Once a frame from probe.cpp after the world tick, player or not (a burst closes after Duke dies).
void hurtlog_frame(const unsigned char* player);
