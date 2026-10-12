// The takeoff-disarm restore. Runs from the rend_end_frame detour (subsys prio -2), after the
// world process (prio 10, where player_tick runs) and before the next frame's tick reads E14.
#include "jumpfix.h"
#include "jumprule.h"
#include "engine.h"
#include "config.h"
#include "log.h"
#include <string.h>

static const unsigned char* s_player;
static float s_prevE14;
static int s_writeFails;

template <class T> static T at(const unsigned char* p, unsigned off) { T v; memcpy(&v, p + off, sizeof(T)); return v; }

int jumpfix_frame(unsigned char* pl) {
  if (!pl) { s_player = 0; return 0; }
  const float e10 = at<float>(pl, 0xE10), e14 = at<float>(pl, 0xE14);  // ground-jump / air-press stamps (A 0x10085b4a)
  if (pl != s_player) {
    // A new player (level load, respawn): no previous E14 to compare yet.
    s_player = pl; s_prevE14 = e14;
    return 0;
  }
  const float prevE14 = s_prevE14;
  s_prevE14 = e14;
  // The cheap part of the rule first: the world read below is guarded (~12 us) and the
  // disarm it screens for happens once per jump at most.
  if (e14 == prevE14 || e14 != e10 || !(prevE14 < e10)) return 0;
  // World process: player+0xE0, time double +0x40 (A 0x10085b2a..b30).
  const unsigned char* world = at<const unsigned char*>(pl, 0xE0);
  if (!guarded_readable(world, 0x48)) return 0;
  const double t = at<double>(world, 0x40);
  const float vy = at<float>(pl, 0x1C4);   // own velocity y; the getter vt+0x190 adds +0xE44 (A 0x10078993)
  if (!jr_takeoff_disarm(prevE14, e14, e10, t, vy)) return 0;
  if (!g_cfg.jumpFix) return JF_EV_TAKEOFF;
  float* slot = (float*)(pl + 0xE14);
  if (!guarded_writable(slot, sizeof(float))) {
    if (s_writeFails++ == 0) np_log("jumpfix: +0xE14 at %p not writable; no restore", (void*)slot);
    return JF_EV_TAKEOFF;
  }
  *slot = prevE14;   // the value the contact callback overwrote; E14 is a field the engine writes itself
  s_prevE14 = prevE14;
  return JF_EV_TAKEOFF | JF_EV_RESTORED;
}
