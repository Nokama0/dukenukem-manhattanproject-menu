// Player and world fields logged around each jump. Counters are cumulative
// (never reset) so a missing or extra jump shows as a total that disagrees.
#include "instruments.h"
#include "jumpwin.h"
#include "boostrule.h"
#include "jumprule.h"
#include "jumpfix.h"
#include "engine.h"
#include "log.h"
#include "config.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int s_enabled = -1;   // -1 until the first frame logs the state
static ULONGLONG s_next_pos, s_next_count;
static const unsigned char* s_last_player;
static unsigned s_prev_mask;
static float s_prev_e10, s_prev_e14, s_prev_vy;
static float s_prev_pos[3];
static JumpWin s_win;
static unsigned s_frame;
// air_boosts counts real boosts only; a refused air press also stamps E14 (double-jump.md section 4).
static int s_jump_presses, s_ground_jumps, s_air_boosts, s_air_refused, s_takeoff_resets, s_restores;

// The window's `jf` lines, written once when it closes: one file open per line
// inside the measured frames would perturb the dt being measured.
static char s_jbuf[32768];
static int s_jlen;

static void jf_flush(void) {
  if (s_jlen > 0) np_log_block(s_jbuf, s_jlen);
  s_jlen = 0;
}

static void log_counts(const char* when) {
  np_log("jumps(%s): jump_presses=%d ground_jumps=%d air_boosts=%d air_refused=%d takeoff_resets=%d restores=%d",
         when, s_jump_presses, s_ground_jumps, s_air_boosts, s_air_refused, s_takeoff_resets, s_restores);
}

template <class T> static T at(const unsigned char* p, unsigned off) { T v; memcpy(&v, p + off, sizeof(T)); return v; }

void instruments_frame(const unsigned char* pl, int jfEv) {
  const int on = g_cfg.timing != 0;
  if (on != s_enabled) {
    // Switched off mid-window: what is buffered is written, not lost.
    if (s_enabled == 1) { jf_flush(); log_counts("off"); }
    s_enabled = on;
    np_log("instruments: timing=%d", s_enabled);
  }
  if (!s_enabled) return;
  s_frame++;
  const ULONGLONG now = GetTickCount64();
  if (now >= s_next_count) { s_next_count = now + 5000; log_counts("5s"); }
  if (!pl) { jf_flush(); return; }

  const float pos[3] = { at<float>(pl, 0x18C), at<float>(pl, 0x190), at<float>(pl, 0x194) };  // entities.md section 1
  const unsigned mask = at<unsigned>(pl, 0xE00), prev = at<unsigned>(pl, 0xE04);  // impulse masks, timing.md section 2
  const float e10 = at<float>(pl, 0xE10), e14 = at<float>(pl, 0xE14);            // jump / boost stamps, timing.md section 2
  const float vy = at<float>(pl, 0x1C4), e70 = at<float>(pl, 0xE70);             // velocity y; jump-start y (duke!0x10088210)
  if (pl != s_last_player) {
    // A new player (level load, respawn) restarts the edge and stamp baselines, not the counters.
    s_last_player = pl; s_prev_mask = mask; s_prev_e10 = e10; s_prev_e14 = e14; s_prev_vy = vy;
    memcpy(s_prev_pos, pos, sizeof(pos));
    np_log("instruments: player %p", (const void*)pl);
  }
  // The jump impulse is mask value 4 (`mask & 4` in playerstate_move_update, timing.md section 2).
  const int edge = (mask & 4) && !(s_prev_mask & 4);
  const int ground = e10 != s_prev_e10;
  // A contact sets +0xE14 back to +0xE10's value (E14RESET); a restored takeoff
  // disarm is already undone here, so it shows only as TAKEOFFRESET RESTORED.
  const BoostChange e14change = boost_classify(e14, s_prev_e14, e10);
  jw_update(&s_win, edge);
  // World process: player+0xE0; dt double +0x38, time double +0x40 (timing.md section 1).
  // Read only inside a jump window (an edge opens one): a guarded read costs ~12 us.
  double dt = -1.0, t = -1.0;
  if (jw_active(&s_win)) {
    const unsigned char* world = at<const unsigned char*>(pl, 0xE0);
    if (guarded_readable(world, 0x48)) { dt = at<double>(world, 0x38); t = at<double>(world, 0x40); }
  }
  const int gnd = at<unsigned char>(pl, 0xB00);   // grounded byte (timing.md section 2; char test in 0x10085dd0)
  const PressResult press = jr_classify_press(edge && !ground && !gnd, s_prev_vy, vy, s_prev_e14, e14, e10, t,
                                              pos[1], e70);
  s_jump_presses += edge; s_ground_jumps += ground;
  s_air_boosts += press == PR_BOOST;
  s_air_refused += press != PR_NONE && press != PR_BOOST;
  s_takeoff_resets += (jfEv & JF_EV_TAKEOFF) != 0;
  s_restores += (jfEv & JF_EV_RESTORED) != 0;
  s_prev_mask = mask; s_prev_e10 = e10; s_prev_e14 = e14; s_prev_vy = vy;

  if (now >= s_next_pos) {
    s_next_pos = now + 1000;
    // Deltas over the second show which component is vertical while jumping or falling.
    np_log("pos: (%.2f, %.2f, %.2f) d=(%.2f, %.2f, %.2f)", pos[0], pos[1], pos[2],
           pos[0] - s_prev_pos[0], pos[1] - s_prev_pos[1], pos[2] - s_prev_pos[2]);
    memcpy(s_prev_pos, pos, sizeof(pos));
  }
  if (!jw_active(&s_win)) { jf_flush(); return; }

  char line[512];
  // One byte kept back so a truncated line can end in '~' instead of being dropped.
  int n = _snprintf_s(line, sizeof(line) - 1, _TRUNCATE, "jf %u dt=%.4f t=%.4f v=(%.2f,%.2f,%.2f) y=%.3f e70=%.3f gnd=%u st=%d/%d b7c=0x%X m=%X/%X e10=%.4f e14=%.4f armed=%d ecc=%d%s%s%s%s%s%s",
         s_frame, dt, t,
         at<float>(pl, 0x1C0), at<float>(pl, 0x1C4), at<float>(pl, 0x1C8),  // velocity, force_process (timing.md section 2)
         pos[1], e70, (unsigned)gnd,
         at<int>(pl, 0xC10), at<int>(pl, 0xC0C),   // current / requested move state (timing.md section 2)
         at<unsigned>(pl, 0xB7C),                  // "state" in timing.md section 3, type unpinned
         mask, prev, e10, e14, e14 < e10 ? 1 : 0,   // armed: gate 2 of the air branch (A 0x10085b4a..b5b)
         at<int>(pl, 0xECC),                       // per-frame counter (timing.md section 2)
         edge ? " EDGE" : "", ground ? " GROUND" : "", jr_press_tag(press),
         e14change == BOOST_RESET ? " E14RESET" : "",
         (jfEv & JF_EV_TAKEOFF) ? " TAKEOFFRESET" : "", (jfEv & JF_EV_RESTORED) ? " RESTORED" : "");
  if (n < 0) { n = (int)strlen(line); line[n++] = '~'; }
  if (n <= 0) return;
  if (s_jlen + n + 1 > (int)sizeof(s_jbuf)) jf_flush();   // a window re-opened by new presses
  memcpy(s_jbuf + s_jlen, line, (size_t)n);
  s_jlen += n;
  s_jbuf[s_jlen++] = 0x0A;   // newline
}

void instruments_detach(void) {
  if (s_enabled != 1) return;
  jf_flush();
  log_counts("detach");
}
