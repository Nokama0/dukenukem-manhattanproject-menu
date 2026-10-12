// Rescue falls (rescue.h). Main thread, frame path, after the world tick: plain reads of the validated player
// (inside probe's kPlayerSpan), and player_teleport behind teleport.cpp's gate when a fall needs it.
#include "rescue.h"
#include "teleport.h"   // teleport_player_to
#include "freecam.h"    // freecam_pause_holding
#include "config.h"
#include "log.h"
#include <windows.h>
#include <string.h>

static RescueState s_r;
static const unsigned char* s_player;
static unsigned s_rescues, s_refused;

template <class T> static T at(const unsigned char* p, unsigned off) { T v; memcpy(&v, p + off, sizeof(T)); return v; }

void rescue_frame(const unsigned char* player) {
  if (player != s_player) { memset(&s_r, 0, sizeof s_r); s_player = player; }   // a new level: no safe spot yet
  if (!player) return;
  // Off, or the world frozen by the freecam pause: a teleport into mid-air must not count as a long fall.
  if (!g_cfg.rescueFalls || freecam_pause_holding()) { s_r.airSince = 0; return; }
  // Walking states only: 3 (walk, jump, fall), 16/17 (g_rpg, g_fps). PS 20-22 are the deaths (PS_death1/2/_pit):
  // a death in progress is the engine's, never ours.
  const int state = at<int>(player, 0xB7C), ps = at<int>(player, 0xC10);
  const int eligible = (state == 3 || state == 16 || state == 17) && !(ps >= 20 && ps <= 22);
  const Vec3 pos = at<Vec3>(player, 0x18C);
  const ULONGLONG now = GetTickCount64();
  const ULONGLONG airSince = s_r.airSince;
  const float top = s_r.airTopY;
  if (rescue_step(&s_r, eligible, player[0xB00] != 0, pos, at<float>(player, 0x1C4), now) != RS_RESCUE) return;
  np_log("rescue: fell %.1f s from y %.1f to %.1f (state %d, PS %d); back to (%.1f,%.1f,%.1f)",
         (double)(now - airSince) / 1000.0, top, pos.y, state, ps, s_r.safe.x, s_r.safe.y, s_r.safe.z);
  if (teleport_player_to(player, &s_r.safe.x, "rescue")) s_rescues++;
  else s_refused++;
  np_log("rescue: rescues=%u refused=%u", s_rescues, s_refused);
}
