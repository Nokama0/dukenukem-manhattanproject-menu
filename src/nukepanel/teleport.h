#pragma once
// The freecam teleport's placement rules and the engine's gate, pure so the suite
// pins them (freecam-teleport.md sections 1-2). teleport.cpp casts the ray, runs
// the clearance sweep and calls player_teleport.
#include "w2s.h"   // Vec3
#include <math.h>

enum { TP_OK = 0, TP_NO_HIT, TP_BAD_NORMAL, TP_STEEP, TP_BLOCKED, TP_NO_PLAYER, TP_NO_FLOOR, TP_OFF_TRACK,
       TP_TRACK_UNREAD, TP_DEPTH_RUN, TP_REASON_COUNT };

// A floor is n.y above this; a wall within +-TP_WALL_NY of level. Between them, and
// every ceiling, is refused: the siblings refuse rather than clamp.
const float TP_FLOOR_NY = 0.7f;
const float TP_WALL_NY = 0.3f;
const float TP_MARGIN = 1.f;   // units of clearance beyond the box
const float TP_DROP_LEN = 2000.f;   // how far below a wall placement a floor is looked for

inline const char* tp_reason_text(int r) {
  switch (r) {
    case TP_OK:         return "ok";
    case TP_NO_HIT:     return "the ray hit nothing";
    case TP_BAD_NORMAL: return "the hit normal is not a unit vector facing the ray";
    case TP_STEEP:      return "too steep (neither floor nor wall)";
    case TP_BLOCKED:    return "no room for Duke there";
    case TP_NO_PLAYER:  return "no player";
    case TP_NO_FLOOR:   return "no floor below the wall spot";
    case TP_OFF_TRACK:  return "no track within reach of the crosshair's hit";
    case TP_TRACK_UNREAD: return "no track of the level could be read";
    case TP_DEPTH_RUN:  return "Duke is in a depth run (state 4)";
    default:            return "unknown";
  }
}

// Any track: the hit snaps to the nearest track within this x/z radius (about 5 m; the engine's
// depth lines run hundreds of units apart), and tracks this close to the nearest are a tie decided by floor height.
const float TP_ANYTRACK_RADIUS = 256.f;
const float TP_TRACK_TIE = 8.f;

// State 4 is the depth run (duke!0x1007f590: it walks a game_track_auto to the next line, then clears +0xB20 and
// sets state 3): a teleport there is refused, never landed mid-run.
inline bool tp_depth_run(unsigned state) { return state == 4; }
// The marker's reason while Duke is in state `state`: a placement otherwise fine is red during a depth run.
inline int tp_depth_refuse(int reason, unsigned state) {
  return reason == TP_OK && tp_depth_run(state) ? TP_DEPTH_RUN : reason;
}

// The marker snaps to a track only in normal play: First person and Move anywhere take the track's hold
// off him, so there it places freely as before the track lock.
inline bool tp_track_locked(int hasTrack, int firstPerson, int moveAnywhere) {
  return hasTrack && !firstPerson && !moveAnywhere;
}

// After player_teleport out of first person (state 17 or our ladder/use hold), +0xB1C goes back to 0 so Duke stays
// at the target (free-move.md section 8): player_teleport (duke!0x1007d830) writes it while +0xB1C is 0, then the
// state-17 exit writes +0xB1C = 1 and the next tick snaps x/z onto a track. The exit's re-pick (repick) wants that.
inline bool tp_keep_off_track(int fpEngaged, int repick) { return fpEngaged && !repick; }

// pos is Duke's +0x18C to pass to player_teleport; dim his half-extents (+0x2B8). wall: pos is
// stepped back from a wall and still needs tp_drop.
struct TpPlace { int reason; Vec3 pos; int wall; };

// hit is the ray record's +0x14 point or NULL; normal its +0x04 (a unit normal facing the
// ray, unverified: freecam-teleport.md Open questions 3; anything else is refused); dir the ray direction.
inline TpPlace tp_place(const Vec3* hit, Vec3 n, Vec3 dir, Vec3 dim) {
  TpPlace p = { TP_NO_HIT, { 0.f, 0.f, 0.f }, 0 };
  if (!hit) return p;
  const float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
  if (!(len > 0.9f && len < 1.1f) || !(n.x * dir.x + n.y * dir.y + n.z * dir.z < 0.f)) {
    p.reason = TP_BAD_NORMAL;
    return p;
  }
  if (n.y > TP_FLOOR_NY) {
    // The box's bottom is +0x18C.y - dim.y (hitboxes.md section 0, o >= 0).
    p.pos.x = hit->x; p.pos.y = hit->y + dim.y + TP_MARGIN; p.pos.z = hit->z;
    p.reason = TP_OK;
  } else if (fabsf(n.y) <= TP_WALL_NY) {
    // The box never rotates (hitboxes.md section 0): its support distance along n clears any wall angle.
    const float step = dim.x * fabsf(n.x) + dim.y * fabsf(n.y) + dim.z * fabsf(n.z) + TP_MARGIN;
    p.pos.x = hit->x + n.x * step; p.pos.y = hit->y + n.y * step; p.pos.z = hit->z + n.z * step;
    p.reason = TP_OK;
    p.wall = 1;
  } else {
    p.reason = TP_STEEP;
  }
  return p;
}

// player_is_dead_or_locked duke!0x1007e050 inverted: health is the short at [+0x111C],
// state the +0xC10 dword, unlocked the +0xAF0 byte.
inline bool tp_gate(short health, unsigned state, unsigned char unlocked) {
  return health > 0 && !(state >= 0x14 && state <= 0x16) && unlocked != 0;
}

// A wall placement stood on the floor below it: downHit is a world-only ray straight down from
// wall.pos (TP_DROP_LEN), NULL when it found nothing. Other placements pass through unchanged.
inline TpPlace tp_drop(TpPlace wall, const Vec3* downHit, Vec3 downN, Vec3 dim) {
  if (wall.reason != TP_OK || !wall.wall) return wall;
  if (!downHit) { wall.reason = TP_NO_FLOOR; return wall; }
  const Vec3 down = { 0.f, -1.f, 0.f };
  TpPlace f = tp_place(downHit, downN, down, dim);
  if (f.reason == TP_OK && f.wall) { f.reason = TP_STEEP; f.wall = 0; }   // not a floor to stand on
  return f;
}

// --- The glue (teleport.cpp), main thread, frame path ------------------------
// After the entity walk and before freecam_draw: casts while the freecam flies and acts on
// the Teleport key (input_teleport_take).
void teleport_frame(void);
// The landing marker from this frame's cast, under the crosshair.
void teleport_draw(int W, int H);
// Duke's current track as the flattened polyline (track.h), for mouse aim's plane: the point count, 0 with
// no track (free placement), -1 when it could not be read. *pts lives until the next call or teleport_frame.
struct TrkPt;
int teleport_track_polyline(const unsigned char* player, const TrkPt** pts);
// How far (x/z) the engine's snap onto Duke's current track (+0xB20) would move him, through the verified
// game_track_project_point (its own stack actor). 1 and *dist when computed; 0 with no track or an unverified one.
// *track: +0xB20 as read.
int teleport_track_snap(const unsigned char* player, const void** track, float* dist);
// player_teleport on Duke's own spot and facing, behind the same gate and entry check as the Teleport key: the
// engine re-picks his track (free-move.md section 1d). `who` prefixes the log lines. 1 when it was called.
int teleport_repick_here(const unsigned char* player, const char* who);
// player_teleport to pos with his own facing, behind the same gate and entry check (Rescue falls). 1 when called.
int teleport_player_to(const unsigned char* player, const float pos[3], const char* who);
// One world-only ray (the camera's matcher) from -> to: true and the hit point when it met the world.
bool teleport_world_hit(const unsigned char* player, const float from[3], const float to[3], float hit[3]);
// The same ray, the world validated once by the caller: teleport_ray_world checks the exports and the player's world
// (one guarded read) and returns it, or 0; teleport_world_hit_in then casts with no read of its own.
void* teleport_ray_world(const unsigned char* player);
// ny (optional): the y of the hit normal (the record's +0x04 vector), > 0.7 on a floor seen from above.
bool teleport_world_hit_in(void* world, const float from[3], const float to[3], float hit[3], float* ny = 0);
