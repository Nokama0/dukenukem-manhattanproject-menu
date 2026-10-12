// Cut-away walls: while the switch is on, every fader_entity's state word is set to 2 (fade out) whenever it is above
// min_trans, as its own handler does on the room switch's "on" signal, so the engine fades the wall at the map's
// speed. The pin is never touched; the undo raises it (pin_t::set); docs/re/cutaway.md sections 2 and 4.
#include "cutaway.h"
#include "engine.h"
#include "entities.h"
#include "config.h"
#include "log.h"
#include <windows.h>
#include <string.h>

typedef const char* (__thiscall* ClassnameFn)(const void* self);

// Fader fields (cutaway.md section 2), all inside the class's 0xB8C allocation.
static const unsigned kFaderSpan = 0xB8C;
static const unsigned kOfsFadeTime = 0xB08, kOfsMin = 0xB0C, kOfsPin = 0xB1C, kOfsAlpha = 0xB84, kOfsState = 0xB88;
static const unsigned kOfsPinFlag = 0x54;   // the field pin_t::set writes (engine.h)
static const int kFaderType = 0x200000;     // the fader ctor's +0x48 bit: its spawn_search list
static const int kVtHandler = 0x58 / 4;
static const int kMaxWalk = 4096;           // a corrupt list must not spin the frame forever
enum { kFaderMax = 256 };                   // 82 faders over all 29 shipped maps

typedef CutFader Fader;
static Fader s_f[kFaderMax];
static int s_n;
static const void* s_player;   // the level the table belongs to: a new player or sid is a new map
static int s_sid;
static int s_lastFound = -1;

static const void* s_faderVt;   // the fader vtable whose handler passed the signature check
static const void* s_badVt;     // one that failed: its faders are skipped without re-reading the bytes
static int s_refused;
// Cumulative, never reset: the 5 s line prints them when any moved.
static unsigned s_forced, s_reasserts, s_undone, s_writeRefused, s_full, s_capped, s_undoGone;
static unsigned s_misses;   // region-cache misses of the walks, per 5 s window
static unsigned s_loggedSum = 0xFFFFFFFFu;
static ULONGLONG s_nextLog;

bool cutaway_refused(void) { return s_refused != 0; }

static float rdf(const unsigned char* p) { float f; memcpy(&f, p, sizeof f); return f; }
static int rdi(const unsigned char* p) { int v; memcpy(&v, p, sizeof v); return v; }

static void refuse(const char* why, const void* at) {
  if (s_refused) return;
  s_refused = 1;
  np_log("cutaway: DISABLED, %s (%p)", why, at);
}

// The handler behind vt+0x58 must be the one cutaway.md read; checked once per vtable, before any write.
static bool handler_ok(void* const* vt) {
  if (vt == s_faderVt) return true;
  if (vt == s_badVt) return false;
  const unsigned char* h = (const unsigned char*)vt[kVtHandler];
  if (!entities_in_duke(h) || !guarded_readable(h, CUT_SIG_SPAN)) {
    s_badVt = vt;
    refuse("fader handler not in duke_base", h);
    return false;
  }
  if (!cut_handler_sig_ok(h)) {
    s_badVt = vt;
    refuse("fader handler bytes differ from cutaway.md section 4", h);
    return false;
  }
  s_faderVt = vt;
  np_log("cutaway: fader vtable %p handler %p signature ok", (const void*)vt, (const void*)h);
  return true;
}

static Fader* fader_slot(const unsigned char* e, const void* vt) {
  for (int i = 0; i < s_n; ++i)
    if (s_f[i].ent == e) {
      if (s_f[i].vt != vt) { memset(&s_f[i], 0, sizeof s_f[i]); s_f[i].ent = e; s_f[i].vt = vt; }   // reused
      return &s_f[i];
    }
  if (s_n >= kFaderMax) {
    if (!s_full++) np_log("cutaway: fader table full (%d); later faders are skipped", kFaderMax);
    return 0;
  }
  Fader* f = &s_f[s_n++];
  memset(f, 0, sizeof *f);
  f->ent = e; f->vt = vt;
  return f;
}

// Our one write: +0xB88 = 2, after re-reading the vtable (a freed object's is rewritten by its dtor chain).
static bool write_state(const Fader* f) {
  const void* now;
  memcpy(&now, f->ent, sizeof now);
  unsigned char* p = (unsigned char*)f->ent + kOfsState;   // ent is const only in the shared record
  if (now != f->vt || !guarded_writable(p, sizeof(int))) { s_writeRefused++; return false; }
  const int v = 2;
  memcpy(p, &v, sizeof v);
  return true;
}

static void force_one(Fader* f) {
  const unsigned char* e = (const unsigned char*)f->ent;
  const float alpha = rdf(e + kOfsAlpha);
  const int minPct = rdi(e + kOfsMin);
  const int state = rdi(e + kOfsState);
  const int d = cut_decide(alpha, minPct, state, f->forced);
  if (d != CUT_KEEP && write_state(f)) {
    if (d == CUT_REASSERT) {
      s_reasserts++;
    } else {
      s_forced++;
      f->forced = 1;
      f->before = alpha;
      f->t0 = GetTickCount64();
      np_log("cutaway: fader %p min=%d%% fade=%.2fs alpha %.3f state %d -> 2", (const void*)e, minPct,
             rdf(e + kOfsFadeTime), alpha, state);
    }
  }
  // Once per fader: the alpha it reached and how long it took (cutaway.md section 6, item 2).
  if (f->forced && !f->loggedSettle && alpha <= cut_min(minPct)) {
    f->loggedSettle = 1;
    np_log("cutaway: fader %p alpha %.3f -> %.3f (min) after %llu ms", (const void*)e, f->before, alpha,
           GetTickCount64() - f->t0);
  }
}

// One balanced search of the fader list; marks each table entry the engine still returns. `force` applies
// the rule to each. Returns the faders found.
static int walk(int sid, bool force) {
  for (int i = 0; i < s_n; ++i) s_f[i].seen = 0;
  ReadCache rc = {};   // fresh each walk: a region freed since last frame is never trusted
  int total = 0, found = 0;
  bool capped = false;
  g_eng.spawn_search("s=%d?t=%d", sid, kFaderType);
  const unsigned char* e;
  while ((e = (const unsigned char*)g_eng.spawn_search_get()) != 0) {
    if (++total > kMaxWalk) { s_capped++; capped = true; break; }
    if (!guarded_readable_cached(&rc, e, kFaderSpan)) continue;
    void* const* vt;
    memcpy(&vt, e, sizeof vt);
    if (!entities_in_duke(vt) || !entities_in_duke(vt + kVtHandler + 1) || !entities_in_duke(vt[1])) continue;
    if (rdi(e + 0x90)) continue;   // killed flag (entities.md section 1)
    if (vt != s_faderVt) {
      const char* cls = ((ClassnameFn)vt[1])(e);
      if (!entities_in_duke(cls) || strcmp(cls, "fader_entity") != 0) continue;   // 10 other classes share the bit
      if (!handler_ok(vt)) continue;   // this fader only: the rest of the list still walks
    }
    Fader* f = fader_slot(e, vt);
    if (!f) continue;
    f->seen = 1;
    found++;
    if (force) force_one(f);
  }
  g_eng.spawn_search_stop();
  s_misses += rc.misses;
  // A capped walk missed faders: it cannot say one is gone.
  if (!capped) s_n = cut_prune(s_f, s_n);
  if (found != s_lastFound) {
    s_lastFound = found;
    np_log("cutaway: faders=%d (search hits=%d) sid=%d", found, total, sid);
  }
  return found;
}

// The switch went off: raise each forced fader's `active` pin so its handler rebuilds the state from the
// switch's recorded signal next tick (Duke's room stays cut away, the rest fade back in). Then forget them.
static void undo(int sid) {
  int nForced = 0;   // before the walk, which prunes faders the search no longer returns
  for (int i = 0; i < s_n; ++i) nForced += s_f[i].forced;
  walk(sid, false);
  unsigned char forced[kFaderMax], seen[kFaderMax];
  int idx[kFaderMax];
  for (int i = 0; i < s_n; ++i) { forced[i] = s_f[i].forced; seen[i] = s_f[i].seen; }
  const int k = cut_undo_pick(forced, seen, s_n, idx);
  int done = 0;
  for (int j = 0; j < k; ++j) {
    const Fader& f = s_f[idx[j]];
    const void* now;
    memcpy(&now, f.ent, sizeof now);
    unsigned char* pin = (unsigned char*)f.ent + kOfsPin;
    if (now != f.vt || !guarded_writable(pin + kOfsPinFlag, sizeof(int))) { s_writeRefused++; continue; }
    g_p3.pin_set(pin);
    done++;
  }
  s_undone += (unsigned)done;
  s_undoGone += (unsigned)(nForced - k);
  np_log("cutaway: off, undone %d of %d forced (%d no longer returned)", done, nForced, nForced - k);
  s_n = 0;
}

static void log_counters(void) {
  const ULONGLONG tick = GetTickCount64();
  if (tick < s_nextLog) return;
  s_nextLog = tick + 5000;
  const unsigned misses = s_misses;
  s_misses = 0;
  const unsigned sum = s_forced + s_reasserts + s_undone + s_writeRefused + s_full + s_capped + s_undoGone;
  if (sum == s_loggedSum) return;
  s_loggedSum = sum;
  np_log("cutaway: on=%d faders=%d forced=%u reasserts=%u undone=%u undoGone=%u writeRefused=%u full=%u capped=%u "
         "misses=%u", g_cfg.cutaway, s_lastFound, s_forced, s_reasserts, s_undone, s_undoGone, s_writeRefused, s_full,
         s_capped, misses);
}

void cutaway_frame(const unsigned char* player) {
  log_counters();
  if (player && rdi(player + 0x90)) player = 0;   // being torn down: no server for its sid (entities.cpp)
  if (!player || !g_eng.spawn_search || !g_eng.spawn_search_get || !g_eng.spawn_search_stop) return;
  const int sid = rdi(player + 0x78);   // server id, the `s=` key (entities.md section 1)
  // The table is kept across a new player or sid: each walk prunes what the search no longer returns, and
  // undo touches only returned faders, so a respawn on the same map keeps its forced marks.
  if (player != s_player || sid != s_sid) {
    np_log("cutaway: player %p sid %d (was %p sid %d), %d fader(s) kept until the next walk", (const void*)player, sid,
           s_player, s_sid, s_n);
    s_player = player;
    s_sid = sid;
    s_lastFound = -1;
  }
  const bool want = g_cfg.cutaway != 0 && !s_refused;
  if (want) {
    // Nothing is forced until the undo is known to exist.
    if (!engine_resolve_pin_set()) {
      if (GetModuleHandleA("p3shared.dll")) refuse("p3shared has no pin_t::set export", 0);
      return;
    }
    walk(sid, true);
  } else if (s_n) {
    bool any = false;
    for (int i = 0; i < s_n; ++i) any = any || s_f[i].forced;
    if (any && g_p3.pin_set) undo(sid);
    else s_n = 0;
  }
}
