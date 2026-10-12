// Duke's damage log (docs/re/queen-burst.md): the player_take_damage detour records what was sent and by whom,
// and a once-a-frame health watcher records what Duke really lost. hurtlog.h owns every rule. The detour
// calls the original unchanged, except that Fix Queen swipe drops repeat swipe hits (section 5). Main thread only (render.md section 1).
#include "hurtlog.h"
#include "config.h"      // g_cfg.queenFix
#include "offscreen.h"   // offs_scan_duke
#include "engine.h"     // guarded_readable
#include "entities.h"   // entities_in_duke, entities_duke_image
#include "hook.h"
#include "log.h"
#include "ui/wallclock.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

typedef void (__thiscall* TakeFn)(void* self, int type, float amount, float* dir, void* inflictor, void* attacker);
typedef const char* (__thiscall* ClassnameFn)(const void* self);

static const unsigned kSlotTake = 0x198 / 4;
static const unsigned kOfsStats = 0x111C;   // health is the short at [player+0x111C] (teleport.h)

static TakeFn s_tramp;
static void* s_target;
static int s_refused;
static const void* s_loggedMismatch;

// Fix Queen swipe: block 1's return address (0 until the scan finds exactly one site), the gate, and its counts.
static unsigned s_qsRet;
static int s_qsScanned;
static QsGate s_qs;
static unsigned s_qsKept, s_qsDropped, s_qsUnread;

// Class names by vtable, so each attacker class is read once. Tags: -1 no object, -2 unreadable or table full.
enum { kClsMax = 32 };
static const void* s_clsVt[kClsMax];
static const char* s_clsName[kClsMax];
static int s_ncls;
static unsigned s_clsFull;

// Sends not yet matched to a health drop; a drop takes those sent within a window before it.
struct Pending { unsigned t; int type; float amount; int src; };
enum { kPendMax = 16 };
static Pending s_pend[kPendMax];
static int s_npend;

// Cumulative. sendsNoDrop: sends a window old with no drop after them (refused, locked out, or absorbed).
static unsigned s_sends, s_drops, s_sendsNoDrop, s_pendLost, s_bursts;
static int s_lost;
static HurtTally s_tally;
static HurtQuiet s_qSend, s_qHurt;
static HurtBurst s_burst;
static const unsigned char* s_prevPlayer;
static int s_prevHp = -1;
static unsigned s_loggedSum = 0xFFFFFFFFu;
static ULONGLONG s_nextLog;

static const char* tag_name(int tag) {
  if (tag == -1) return "none";
  if (tag < 0 || tag >= s_ncls) return "?";
  return s_clsName[tag] ? s_clsName[tag] : "?";
}

// The classname call is the walk's own (entities.cpp); actor_player's slot 1 returns a non-pointer.
static int class_tag(const void* obj) {
  if (!obj) return -1;
  if (!guarded_readable(obj, sizeof(void*))) return -2;
  const void* const* vt;
  memcpy(&vt, obj, sizeof vt);
  for (int i = 0; i < s_ncls; ++i) if (s_clsVt[i] == vt) return i;
  if (s_ncls >= kClsMax) { s_clsFull++; return -2; }
  const char* name = 0;
  if (entities_in_duke(vt) && entities_in_duke(vt + 2) && entities_in_duke(vt[1])) {
    name = ((ClassnameFn)vt[1])(obj);
    if (!entities_in_duke(name)) name = "player?";
  }
  s_clsVt[s_ncls] = vt;
  s_clsName[s_ncls] = name;
  return s_ncls++;
}

static int health_of(const unsigned char* player) {
  if (!player || !guarded_readable(player + kOfsStats, sizeof(void*))) return -1;
  const unsigned char* ps;
  memcpy(&ps, player + kOfsStats, sizeof ps);
  if (!ps || !guarded_readable(ps, sizeof(short))) return -1;
  short h;
  memcpy(&h, ps, sizeof h);
  return h;
}

static unsigned line_key(int a, float f, int b, int c) {
  unsigned k;
  memcpy(&k, &f, sizeof k);
  return k * 2654435761u ^ (unsigned)a * 40503u ^ (unsigned)(b + 7) * 9973u ^ (unsigned)(c + 3) * 131u;
}

// thiscall seen as fastcall: ECX = this, EDX unused, the 5 stack args and the callee's RET 0x14 are the same.
static void __fastcall take_detour(void* self, void* /*edx*/, int type, float amount, float* dir, void* inflictor,
                                   void* attacker) {
  const unsigned ret = (unsigned)(size_t)_ReturnAddress();   // the JMP into the detour pushed nothing
  const unsigned now = wallclock_ms();
  const int src = class_tag(attacker), inf = class_tag(inflictor);
  const int hp = health_of((const unsigned char*)self);
  s_sends++;
  hurt_tally_add(&s_tally, src, type, amount);
  // Block 1 of the Queen's running swipe: only the first hit of each attack goes on to the original.
  if (s_qsRet && ret == s_qsRet && type == 1 && src >= 0 && s_clsName[src] && !strcmp(s_clsName[src], "boss_queenroach")) {
    const unsigned char* q = (const unsigned char*)attacker;
    unsigned start = 0;
    const int read = guarded_readable(q + QS_OFS_START, sizeof start);
    if (read) memcpy(&start, q + QS_OFS_START, sizeof start); else s_qsUnread++;
    if (!qs_gate(&s_qs, g_cfg.queenFix, read, attacker, start)) {
      s_qsDropped++;
      unsigned held = 0;
      if (hurt_quiet_pass(&s_qSend, line_key(-type, amount, src, inf), now, &held))
        np_log("hurtlog: queen swipe repeat dropped t=%u amount=%.2f health=%d (held %u)", now, amount, hp, held);
      return;
    }
    s_qsKept++;
  }
  if (s_npend == kPendMax) { memmove(s_pend, s_pend + 1, sizeof(Pending) * (kPendMax - 1)); s_npend--; s_pendLost++; }
  Pending& p = s_pend[s_npend++];
  p.t = now; p.type = type; p.amount = amount; p.src = src;
  unsigned held = 0;
  if (hurt_quiet_pass(&s_qSend, line_key(type, amount, src, inf), now, &held))
    np_log("hurtlog: send t=%u type=%d amount=%.2f attacker=%s inflictor=%s health=%d (held %u)", now, type, amount,
           tag_name(src), tag_name(inf), hp, held);
  s_tramp(self, type, amount, dir, inflictor, attacker);
}

// Armed from the live player's own vt+0x198. A slot that does not match the stock entry is logged once per target
// and tried again next frame (the input entity may not be Duke yet); only a refused install is final.
static void arm(const unsigned char* player) {
  if (s_target || s_refused || !guarded_readable(player, sizeof(void*))) return;
  const unsigned char* base = 0;
  unsigned size = 0;
  if (!entities_duke_image(&base, &size)) return;
  const void* const* vt;
  memcpy(&vt, player, sizeof vt);
  const void* const* slot = vt + kSlotTake;
  if (!entities_in_duke(slot) || !entities_in_duke((const unsigned char*)(slot + 1) - 1)) return;
  void* const target = (void*)*slot;
  if (!entities_in_duke(target) || !guarded_readable(target, HURT_FP_LEN)) return;
  const unsigned char* t = (const unsigned char*)target;
  if (memcmp(t, kHurtSendFp, HURT_FP_LEN)) {
    if (s_loggedMismatch != target) {
      s_loggedMismatch = target;
      np_log("hurtlog: player vt+0x198 -> %p (duke_base+%X) is not player_take_damage's stock entry: "
             "%02X %02X %02X %02X %02X %02X; not arming from it", target, (unsigned)(t - base), t[0], t[1], t[2],
             t[3], t[4], t[5]);
    }
    return;
  }
  if (!hook_install_at(target, kHurtSendFp, HURT_FP_LEN, HURT_TRAMP_LEN, (void*)take_detour, (void**)&s_tramp)) {
    s_refused = 1;
    np_log("hurtlog: install at %p refused; only the health watcher runs", target);
    return;
  }
  s_target = target;
  np_log("hurtlog: armed at %p (duke_base+%X) from the player vt %p", target, (unsigned)(t - base), (const void*)vt);
}

// One header line, then the events eight to a line (a log line holds about a kilobyte).
static void log_burst(void) {
  s_bursts++;
  const HurtBurst& b = s_burst;
  const int first = b.n ? b.ev[0].before : -1, last = b.n ? b.ev[b.n - 1].after : -1;
  np_log("hurtlog: BURST #%u t=%u..%u (%u ms) lost=%d in %d drops (+%u not listed), health %d -> %d", s_bursts,
         b.tFirst, b.tLast, b.tLast - b.tFirst, b.total, b.n, b.dropped, first, last);
  char line[960];
  int len = 0;
  line[0] = 0;
  for (int i = 0; i < b.n; ++i) {
    const HurtEv& e = b.ev[i];
    const int w = sprintf_s(line + len, sizeof line - (size_t)len, "%s+%ums -%d (%d->%d) sends=%d type=%d amt=%.1f %s",
                            len ? "; " : "", e.t - b.tFirst, e.loss, e.before, e.after, e.sends, e.type, e.amount,
                            tag_name(e.src));
    if (w > 0) len += w;
    if ((i % 8) == 7 || i == b.n - 1) { np_log("hurtlog: BURST #%u events %s", s_bursts, line); len = 0; line[0] = 0; }
  }
}

static void log_totals(void) {
  const ULONGLONG tick = GetTickCount64();
  if (tick < s_nextLog) return;
  s_nextLog = tick + 5000;
  const unsigned sum = s_sends + s_drops + s_sendsNoDrop + s_bursts + s_qsKept + s_qsDropped;
  if (sum == s_loggedSum) return;   // cumulative: an unchanged line says nothing
  s_loggedSum = sum;
  np_log("hurtlog: armed=%d sends=%u drops=%u lost=%d sendsNoDrop=%u pendLost=%u bursts=%u classes=%d classesFull=%u "
         "tallyFull=%u | queenFix=%d site=%d kept=%u dropped=%u unread=%u", s_target != 0, s_sends, s_drops, s_lost, s_sendsNoDrop, s_pendLost, s_bursts, s_ncls,
         s_clsFull, s_tally.full, g_cfg.queenFix, s_qsRet != 0, s_qsKept, s_qsDropped, s_qsUnread);
  char line[960];
  int len = 0;
  line[0] = 0;
  for (int i = 0; i < s_tally.n; ++i) {
    const int w = sprintf_s(line + len, sizeof line - (size_t)len, "%s%s/type%d x%u =%.1f", len ? "; " : "",
                            tag_name(s_tally.src[i]), s_tally.type[i], s_tally.count[i], s_tally.amount[i]);
    if (w > 0) len += w;
    if ((i % 8) == 7 || i == s_tally.n - 1) { np_log("hurtlog: by attacker/type %s", line); len = 0; line[0] = 0; }
  }
}

// Once, after the detour is armed: block 1's call site in duke_base's code. Anything but one match leaves the
// fix off (logged); the log and the other sends are unaffected.
static void qs_scan(void) {
  if (s_qsScanned || !s_target) return;
  unsigned char mask[QS_SITE_LEN];
  memset(mask, 1, sizeof mask);
  const unsigned char* at = 0;
  const int n = offs_scan_duke(kQsSite, mask, QS_SITE_LEN, &at);
  if (n < 0) return;   // the image is not readable yet: next frame
  s_qsScanned = 1;
  if (n != 1) { np_log("hurtlog: queen swipe site matched %d times; Fix Queen swipe stays inactive", n); return; }
  s_qsRet = (unsigned)(size_t)at + QS_SITE_RET;
  np_log("hurtlog: queen swipe block 1 at %p, return %08X; Fix Queen swipe is %s", (const void*)at, s_qsRet,
         g_cfg.queenFix ? "on" : "off");
}

void hurtlog_frame(const unsigned char* player) {
  const unsigned now = wallclock_ms();
  qs_scan();
  if (hurt_close_due(s_burst, now)) { log_burst(); hurt_reset(&s_burst); }
  log_totals();
  // Sends a window old that no drop took.
  int keep = 0;
  while (keep < s_npend && (unsigned)(now - s_pend[keep].t) >= HURT_WINDOW_MS) keep++;
  if (keep) {
    s_sendsNoDrop += (unsigned)keep;
    memmove(s_pend, s_pend + keep, sizeof(Pending) * (size_t)(s_npend - keep));
    s_npend -= keep;
  }
  if (!player) { s_prevPlayer = 0; s_prevHp = -1; return; }
  arm(player);
  const int hp = health_of(player);
  if (player != s_prevPlayer || hp < 0 || s_prevHp < 0) { s_prevPlayer = player; s_prevHp = hp; return; }
  if (hp < s_prevHp) {
    HurtEv e = {};
    e.t = now; e.loss = s_prevHp - hp; e.before = s_prevHp; e.after = hp; e.sends = s_npend;
    e.type = s_npend ? s_pend[0].type : -1;
    e.amount = s_npend ? s_pend[0].amount : 0.f;
    e.src = s_npend ? s_pend[0].src : -1;
    s_npend = 0;
    s_drops++;
    s_lost += e.loss;
    unsigned held = 0;
    if (hurt_quiet_pass(&s_qHurt, line_key(e.type, e.amount, e.src, e.loss), now, &held))
      np_log("hurtlog: hurt t=%u health %d -> %d (-%d) sends=%d first: type=%d amount=%.2f attacker=%s (held %u)",
             now, e.before, e.after, e.loss, e.sends, e.type, e.amount, tag_name(e.src), held);
    hurt_feed(&s_burst, e);
  }
  s_prevHp = hp;
}
