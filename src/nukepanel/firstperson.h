#pragma once
// First person (evidence docs/re/free-move.md):
// the engine's own g_fps state 17 through the cvar, our mouse look on top. The pure rules; firstperson.cpp owns
// the engine calls.
#include <math.h>
#include <string.h>
#include "w2s.h"        // Vec3
#include "mouseaim.h"   // MA_SRC_*, MA_SENS_*

// player_tick enters state 17 while g_fps > 0 (free-move.md section 1c); +0xB7C holds it.
enum { FP_ENGINE_STATE = 17 };
const float FP_PITCH_MAX = 80.f;
// Degrees a mickey at 100% Sensitivity: the freecam's 0.1 per count, so both turn alike.
const float FP_DEG_PER_MICKEY = 0.1f;
const float FP_RAY_LEN = 8192.f;   // world units, teleport.cpp's ray length (about 160 m)

inline float fp_deg_per_mickey(int sensPct) {
  if (sensPct < MA_SENS_MIN) sensPct = MA_SENS_MIN;
  if (sensPct > MA_SENS_MAX) sensPct = MA_SENS_MAX;
  return FP_DEG_PER_MICKEY * (float)sensPct / 100.f;
}
inline float fp_clamp_pitch(float p) { return p > FP_PITCH_MAX ? FP_PITCH_MAX : (p < -FP_PITCH_MAX ? -FP_PITCH_MAX : p); }
// Mickeys run y down, so a mouse pushed away (dy < 0) looks up (pitch +, freecam.h's convention).
inline float fp_pitch_step(float pitch, int dy, int sensPct) {
  return fp_clamp_pitch(pitch - (float)dy * fp_deg_per_mickey(sensPct));
}
// The engine's forward for yaw Y is (-sin Y, 0, cos Y) (ypr_2_cartes, aim.md section 2). sign: +1 when raising Y
// swings that forward toward the view's right row, so mouse right always turns the view right whatever the
// camera's handedness.
inline int fp_yaw_sign(float yawDeg, const float right[3]) {
  const float r = yawDeg * 0.017453292f;
  const float dx = -cosf(r), dz = -sinf(r);   // d forward / dY
  return dx * right[0] + dz * right[2] >= 0.f ? 1 : -1;
}
// The yaw after dx mickeys, unwrapped: playerstate_fps_think adds to it unwrapped too, so a wrap would be a 360-degree
// jump the engine never makes.
inline float fp_yaw_step(float yawDeg, int dx, int sensPct, int sign) {
  return yawDeg + (float)sign * (float)dx * fp_deg_per_mickey(sensPct);
}
// Mouse look turns Duke only with the deltas read, the panel, console and freecam off the mouse, and the world
// taking input (ma_face_world_live: not paused, no menu, no cutscene letterbox). The deltas are drained regardless.
inline bool fp_look_applies(int read, int panelOpen, int consoleOpen, int freecamOn, int worldLive) {
  return read && !panelOpen && !consoleOpen && !freecamOn && worldLive;
}
// The diagnostic for that forward: the cosine between the engine's forward for yaw Y and a view forward, both
// flattened. Near 1 means the yaw convention above is the one the camera uses.
inline float fp_forward_agree(float yawDeg, const float fwd[3]) {
  const float r = yawDeg * 0.017453292f;
  const float l = sqrtf(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
  if (!(l > 1e-6f)) return 0.f;
  return (-sinf(r) * fwd[0] + cosf(r) * fwd[2]) / l;
}

// The mode. applied: we set g_fps 1 and have not set it back. repick: g_fps went back to 0 and Duke still needs
// player_teleport on his own spot (free-move.md section 1d), on the player that was in the mode.
enum { FP_NONE = 0, FP_APPLY, FP_RESTORE, FP_REFUSE, FP_REPICKED, FP_REFUSE_QUIT };
struct FpState { int applied, repick; const void* repickPlayer; };
// rpgOn: g_rpg reads non-zero. Never both: no apply under it, and ours goes if it
// comes on while ours holds. quitHooked: the config-write detour is in, the only path that puts g_fps back to "0"
// before config.cfg is written; without it a quit would archive our 1.
inline int fp_step(const FpState& s, int want, int rpgOn, int quitHooked) {
  if (want && !s.applied) return !quitHooked ? FP_REFUSE_QUIT : (rpgOn ? FP_REFUSE : FP_APPLY);
  if (s.applied && (!want || rpgOn)) return FP_RESTORE;
  return FP_NONE;
}
inline int fp_quit(const FpState& s) { return s.applied ? FP_RESTORE : FP_NONE; }
inline void fp_done(FpState* s, int act, const void* player) {
  if (act == FP_APPLY) { s->applied = 1; s->repick = 0; s->repickPlayer = 0; }
  else if (act == FP_RESTORE) { s->applied = 0; s->repick = player ? 1 : 0; s->repickPlayer = player; }
  else if (act == FP_REPICKED) { s->repick = 0; s->repickPlayer = 0; }
}
// The exit's re-pick waits for the engine to leave state 17 itself (proof it read g_fps 0), and is dropped for
// another player: a new level builds Duke in state 3, on a track.
enum { FP_REPICK_NONE = 0, FP_REPICK_WAIT, FP_REPICK_NOW, FP_REPICK_DROP };
// Also waits while a state holds him that player_teleport's player_set_state(3) would break or that keeps +0xB1C 0
// itself: the ladder and use states 5-11, attached 13, net 4 (switched off mid-climb).
inline bool fp_repick_defers(int state) {
  return state == 17 || state == 4 || state == 13 || (state >= 5 && state <= 11);
}
inline int fp_repick(const FpState& s, const void* player, int state) {
  if (!s.repick) return FP_REPICK_NONE;
  if (!player || player != s.repickPlayer) return FP_REPICK_DROP;
  return fp_repick_defers(state) ? FP_REPICK_WAIT : FP_REPICK_NOW;
}
inline bool fp_active(int applied, int state) { return applied && state == FP_ENGINE_STATE; }

// The ladder pause's reason: the engine's own grab condition, head in an area_ladder volume AND Up (mask 8;
// area_ladder_grab_test duke!0x1009a460), or a ladder state. In state 3 with +0xB1C held 0 (player_track_move_vector
// 0x1008803e), FP_LADDER_LIMBO_MS without a ladder state ends the reason, latched until Up or the volume lets go.
const unsigned FP_LADDER_LIMBO_MS = 300;
struct FpLadderGate { unsigned long long since; int latched; };
inline int fp_ladder_reason(FpLadderGate* g, int atLadder, int upHeld, int state, int pausedLadder, int trackHeldOff,
                            unsigned long long now) {
  if (state == 5 || state == 6) { g->since = now; return 1; }
  if (!(atLadder && upHeld)) { g->latched = 0; g->since = 0; return 0; }
  if (g->latched) return 0;
  if (!pausedLadder || !g->since) { g->since = now; return 1; }
  if (trackHeldOff && now - g->since > FP_LADDER_LIMBO_MS) { g->latched = 1; return 0; }
  return 1;
}

// Ladder to ladder: after a ladder state the pause holds while Duke is in the air, until he is
// grounded in 3 or 17; leaving for 17 between two ladders reset the view. Death, net or jetpack states end it.
struct FpLadderAir { int on; };
inline int fp_ladder_air(FpLadderAir* a, int state, int grounded) {
  if (state == 5 || state == 6) { a->on = 1; return 1; }
  if (!a->on) return 0;
  const int known = state == 3 || state == 17 || (state >= 5 && state <= 11);
  if (!known || ((state == 3 || state == 17) && grounded)) { a->on = 0; return 0; }
  return 1;
}
// The ladder reason with that air hold first; the limbo clock restarts while it holds.
inline int fp_ladder_reason_air(FpLadderGate* g, FpLadderAir* a, int grounded, int atLadder, int upHeld, int state,
                                int pausedLadder, int trackHeldOff, unsigned long long now) {
  if (fp_ladder_air(a, state, grounded)) { g->since = now; return 1; }
  return fp_ladder_reason(g, atLadder, upHeld, state, pausedLadder, trackHeldOff, now);
}

// Pause and resume (free-move.md section 6): g_fps goes to 0 while the switch stays on. No player (the input is not the
// player: loading, level change, scripted `camera`): the input leaving a player in state 17 hung level starts.
// Ladder: state 17 never runs state 3's touch scan, the only way onto a ladder (state 5).
enum { FP_PAUSE_NONE = 0, FP_PAUSE_NO_PLAYER, FP_PAUSE_LADDER };
const unsigned FP_RESUME_NOPLAYER_MS = 1000, FP_RESUME_LADDER_MS = 300;
struct FpPause { int paused, reason; unsigned long long liveSince; };
inline int fp_pause_reason(int havePlayer, int atLadder) {
  if (!havePlayer) return FP_PAUSE_NO_PLAYER;
  return atLadder ? FP_PAUSE_LADDER : FP_PAUSE_NONE;
}
// The ladder's states: 5 walks to the grab point (playerstate_ladder_pretick 0x10083b10), 6 climbs
// (playerstate_climb_pretick 0x10083f40 / playerstate_climb_think 0x100842e0, Up/Down along the ladder).
inline bool fp_ladder_state(int s) { return s == 5 || s == 6; }
// The states a ladder or the Interact key starts: ladder 5/6, use 7, hang 8, pull-up 9, bars 10, 11.
inline bool fp_scripted_state(int s) { return s >= 5 && s <= 11; }
// Our first-person view stays while the ladder pause holds, or while the interact hold-off keeps a scripted state
// (g_fps still applied) (switching to the game camera there was jarring).
inline bool fp_view_hold(int switchOn, int pauseLadder, int applied, int state, int havePlayer) {
  return switchOn && havePlayer && (pauseLadder || (applied && fp_scripted_state(state)));
}
// Game sequences: while the input is not the player (the no-player pause: a scripted `camera`, the
// level's run-out or run-in), g_fps stays 0 so the engine plays them, but our view stays at the player the search
// finds; with no player (loading) the game's own screen shows.
inline bool fp_scene_hold(int switchOn, int pausedNoPlayer, int haveViewPlayer) {
  return switchOn && pausedNoPlayer && haveViewPlayer;
}
// The player the search returns (`s=%d?t=2?Y=256`, as get_local_player duke!0x100e7060): the player user bit
// (0x100), not removed (+0x90), the player class's vtable.
inline bool fp_scene_player_ok(unsigned user, int killed, int vtMatches) {
  return (user & 0x100u) && !killed && vtMatches;
}
// The search may run only when safe: a spawn_search on a sid with no server faults in spawn_search_get
// (spawner_find_server prism!0x40ca70 returns 0). Only a live camera input entity (+0x4C & 0x200, +0x90 clear) on the
// last live player's server, which the engine's own spawn_get_server prism!0x422180 still finds.
inline bool fp_scene_search_ok(unsigned inUser, int inKilled, int inSid, int haveLastSid, int lastSid, int serverFound) {
  return (inUser & 0x200u) && !inKilled && haveLastSid && inSid == lastSid && serverFound;
}
// The resume wait (FP_RESUME_NOPLAYER_MS, the player back as the input): a hold already running continues as a
// view-only hold, so no game-camera second shows; the yaw is handed to Duke when 17 returns.
inline bool fp_wait_hold(int wasHolding, int pausedNoPlayer, int havePlayer) {
  return wasHolding && pausedNoPlayer && havePlayer;
}
// During a sequence the view faces where Duke faces (the engine moves him) plus the mouse's offset.
inline float fp_scene_yaw(float engineYaw, float offset) { return -engineYaw + offset; }

// A hold's starting yaw. Started while Duke is still in 17 (the pause frame), it is his yaw:
// our restore and mouse look write it, and the first 17 frame's push predates the restore (log: hold 84.9, one 17
// frame, next hold 90.0 = the ladder's facing). Otherwise the last 17 view.
inline float fp_hold_seed_yaw(int inFp17, float engineYaw, int haveLastPose, float lastPoseYaw) {
  return inFp17 || !haveLastPose ? -engineYaw : lastPoseYaw;
}

// After the hold's reason ends, the view stays ours until state 17 is entered, for at most FP_BRIDGE_MS: no
// game-camera frame in between. Gated on the switch, not on g_fps being set again: on the
// resume frame g_fps is still 0, and the hold otherwise ended there on every ladder exit.
const unsigned FP_BRIDGE_MS = 1000;
inline bool fp_view_bridge(int wasHolding, int switchOn, int state, unsigned long long sinceEndMs) {
  return wasHolding && switchOn && state != 17 && sinceEndMs <= FP_BRIDGE_MS;
}
// Whether this frame reads g_rpg and may apply g_fps. While ours holds, every frame; while an apply waits, once in
// FP_RPG_RECHECK frames (a refusal is retried about once a second); but at once when `want` rises (a resume or the
// switch going on). Left mid-cycle by the last apply, the counter held g_fps 0 for 59 frames after a ladder resume.
const unsigned FP_RPG_RECHECK = 60;
inline int fp_check_now(int applied, int waiting, int applyFailed, int wantRose, unsigned* n) {
  if (wantRose) *n = 0;
  return applied || (waiting && !applyFailed && (*n)++ % FP_RPG_RECHECK == 0);
}
// Duke's yaw (+0x1AC) that faces our view yaw on return to state 17: the engine's forward (-sin Y, 0, cos Y) is the
// view's (sin y, 0, cos y) at Y = -y, taken the whole turns nearest his current, unwrapped yaw.
inline float fp_engine_yaw_for_view(float viewYaw, float engineNow) {
  const float t = -viewYaw;
  return t + 360.f * floorf((engineNow - t) / 360.f + 0.5f);
}

// The handback's first frames (the view reset on landing after a ladder).
// The push of 17's first frame is built before our yaw restore lands, so it faced the ladder's way. The hold's yaw is
// shown until the engine's own view agrees with it (mod 360, within 1 degree), for at most FP_HANDBACK_MAX frames.
enum { FP_HANDBACK_MAX = 3 };
inline float fp_handback(int* left, float handbackYaw, float poseYaw) {
  if (*left <= 0) return poseYaw;
  float d = fmodf(poseYaw - handbackYaw, 360.f);
  if (d > 180.f) d -= 360.f; else if (d < -180.f) d += 360.f;
  if (fabsf(d) <= 1.f) { *left = 0; return poseYaw; }
  (*left)--;
  return handbackYaw;
}

// The handback's yaw only holds with +0xB1C 0 (a hold of -440.6 snapped to -90.0, the track's facing): player_tick
// enters 17 after the state's pre-tick (switch 0x1007b089), so +0xB1C is still 1 next tick; actor_track_update_if_moved
// (0x1007ae6c) reads our write as a turn over 1 degree and its snap re-faces Duke (FUN_10096410). We write 0.
inline bool fp_handback_track_off(int state, int trackOn) { return state == FP_ENGINE_STATE && trackOn; }

// The track guard (free-move.md section 8). State 17 keeps +0xB1C at 0, so +0xB20 is never re-picked and goes stale.
// Leaving 17 (exit 0x1007fb50 writes +0xB1C = 1) lets the next tick's actor_track_update_if_moved 0x1008dfa0 pick and
// snap x/z onto it: 1600 units across the level and off it in depth.
const float FP_SNAP_SAFE = 24.f;   // world units: a snap this short is the engine's own small correction
enum { FP_TRK_NONE = 0, FP_TRK_OFF, FP_TRK_CLEAR, FP_TRK_RESTORE };
// While our hold has him out of 17, +0xB1C returns to 0 (the value 17, state 13 enter and state 4 write) when that snap
// would move him more than FP_SNAP_SAFE or cannot be measured; back in 17 the flag clears. Switched off, +0xB1C goes
// back to 1 with the track re-picked first, in state 3 (ladder states keep 0: enter 0x10083a12, exit 1 0x10083e22).
inline int fp_track_guard(int switchOn, int heldOff, int holding, int state, int trackOn, int haveSnap,
                          float snapDist) {
  if (state == FP_ENGINE_STATE) return heldOff ? FP_TRK_CLEAR : FP_TRK_NONE;
  if (!switchOn) return heldOff && state == 3 ? FP_TRK_RESTORE : FP_TRK_NONE;   // never 1 in a ladder state
  if (holding && trackOn && (!haveSnap || snapDist > FP_SNAP_SAFE)) return FP_TRK_OFF;
  return FP_TRK_NONE;
}

// The ladder grab (playerstate_move_touch_scan 0x10086120) and state 11's enter (playerstate11_enter 0x10087330)
// project their walk target onto +0xB20 whatever +0xB1C says, so a stale track walked Duke 741 units to a ladder's
// projection, or (state 11, +0xB1C 1 from the 17 exit) along the line forever.
inline bool fp_exit_holding(int ladderPause, int useHold) { return ladderPause || useHold; }
// Leaving 17 under our hold, a track the snap would move him more than FP_SNAP_SAFE onto, or that cannot be measured,
// is forgotten with actor_teleport's field writes (0x1008e833-0x1008e84b: +0xB58/+0xB5C = -1, +0xB24/28/20 = 0).
// Its next call, actor_track_update_if_moved (0x1008e851), is skipped: it returns at once while +0xB1C is 0.
inline bool fp_track_forget(int holding, int trackSet, int haveSnap, float dist) {
  return holding && trackSet && (!haveSnap || dist > FP_SNAP_SAFE);
}
inline void fp_track_clear_fields(unsigned char* p) {
  const int z = 0, m = -1;
  memcpy(p + 0xB20, &z, 4); memcpy(p + 0xB24, &z, 4); memcpy(p + 0xB28, &z, 4);
  memcpy(p + 0xB58, &m, 4); memcpy(p + 0xB5C, &m, 4);
}

// The 17 exit's camera: playerstate_free_exit 0x1007fb50 hands the view to the first camera `s=%d?t=2?Y=512` finds; a
// fixed one found first (eye (922,-938,-768)) kept the view. If Duke held the view before the exit (17's pre-tick sets
// it each tick), the follow camera (type 2, user 0x200, live, +0xB34 == player, 0x1008e86d) is put back; scripts keep.
inline bool fp_cam_follows(unsigned type, unsigned user, int killed, const void* target, const void* player) {
  return (type & 2u) && (user & 0x200u) && !killed && target == player;
}
inline const void* fp_exit_view_fix(const void* before, const void* player, const void* after, const void* follow) {
  return before == player && follow && after != follow ? follow : 0;
}
// set_view_entity duke!0x100e55d0, reached from the exit's `call [spawn_search_get]; push eax; mov ecx,edi; call rel32`.
// The offset of the call's return address, or -1; *target: the call's target as an offset from code.
inline int fp_find_view_call(const unsigned char* code, int len, int* target) {
  for (int i = 0; i + 14 <= len; ++i) {
    if (code[i] != 0xFF || code[i + 1] != 0x15 || code[i + 6] != 0x50 || code[i + 7] != 0x8B || code[i + 8] != 0xCF ||
        code[i + 9] != 0xE8)
      continue;
    int rel;
    memcpy(&rel, code + i + 10, 4);
    *target = i + 14 + rel;
    return i + 14;
  }
  return -1;
}
// Its body: thiscall(entity), RET 4, ECX never read (push esi; mov esi,[esp+8]; ... three imported calls).
enum { FP_SETVIEW_FP_LEN = 49 };
const unsigned FP_SETVIEW_RETRY_MS = 5000;
inline int fp_fp_is_set_view(const unsigned char* b) {
  static const unsigned char k[FP_SETVIEW_FP_LEN] = {
    0x56, 0x8B, 0x74, 0x24, 0x08, 0x8B, 0x86, 0xE0, 0x00, 0x00, 0x00, 0x50, 0xFF, 0x15, 0, 0, 0, 0, 0x8B,
    0x8E, 0xE0, 0x00, 0x00, 0x00, 0x6A, 0x01, 0x51, 0xFF, 0x15, 0, 0, 0, 0, 0x56, 0x6A, 0x00, 0xFF, 0x15,
    0, 0, 0, 0, 0x83, 0xC4, 0x14, 0x5E, 0xC2, 0x04, 0x00 };
  if (!b) return 0;
  for (int i = 0; i < FP_SETVIEW_FP_LEN; ++i) {
    const int wild = (i >= 14 && i < 18) || (i >= 29 && i < 33) || (i >= 38 && i < 42);
    if (!wild && b[i] != k[i]) return 0;
  }
  return 1;
}

// The ladder trail (Debug log): Duke's position, states and track for FP_TRAIL_N frames before and after each
// ladder or track event, so the step that moves him shows in the log.
struct FpTrailRec { float x, y, z; int state, ps, trackOn; unsigned track; };
enum { FP_TRAIL_N = 30 };
struct FpTrail { FpTrailRec r[FP_TRAIL_N]; int next, count, after; };
inline void fp_trail_push(FpTrail* t, const FpTrailRec& rec) {
  t->r[t->next] = rec;
  t->next = (t->next + 1) % FP_TRAIL_N;
  if (t->count < FP_TRAIL_N) t->count++;
}
// The i-th kept record, 0 the oldest.
inline const FpTrailRec& fp_trail_at(const FpTrail* t, int i) {
  return t->r[(t->next - t->count + i + 2 * FP_TRAIL_N) % FP_TRAIL_N];
}
// A move in x/z no walk makes in one frame (300 u/s walk; gate 2's snaps moved 700-3000 units, falls move y only).
const float FP_JUMP_UNITS = 64.f;
inline bool fp_pos_jump(Vec3 a, Vec3 b) {
  const float dx = b.x - a.x, dz = b.z - a.z;
  return dx * dx + dz * dz > FP_JUMP_UNITS * FP_JUMP_UNITS;
}

// Resumes once the reason is gone and the world has taken input for the reason's whole wait.
inline void fp_pause_step(FpPause* p, int want, int reason, int worldLive, unsigned long long now) {
  if (!want) { p->paused = 0; p->reason = FP_PAUSE_NONE; p->liveSince = 0; return; }
  if (reason) { p->paused = 1; p->reason = reason; p->liveSince = 0; return; }
  if (!p->paused) return;
  if (!worldLive) { p->liveSince = 0; return; }
  if (!p->liveSince) { p->liveSince = now ? now : 1; return; }
  const unsigned wait = p->reason == FP_PAUSE_LADDER ? FP_RESUME_LADDER_MS : FP_RESUME_NOPLAYER_MS;
  if (now - p->liveSince >= wait) { p->paused = 0; p->reason = FP_PAUSE_NONE; p->liveSince = 0; }
}

// Who sets the shot's direction: silent aim's lock, else first person's view centre while the mode is active
// (stock when no view was captured this frame), else mouse aim (ma_fire_source).
enum { FP_SRC_VIEW = 3 };
inline int fp_fire_source(int silentLocked, int fpActive, int haveView, int mouseOn, int haveMouse) {
  if (silentLocked) return MA_SRC_SILENT;
  if (fpActive) return haveView ? FP_SRC_VIEW : MA_SRC_STOCK;
  return ma_fire_source(0, mouseOn, haveMouse);
}
// The centre ray's point: its first world hit, else `len` along it. The gun's origin is the eye bone, not the
// camera eye, so a point (not the ray's direction) keeps the parallax out of close shots.
inline Vec3 fp_view_point(Vec3 eye, Vec3 fwd, int haveHit, Vec3 hit, float len) {
  if (haveHit) return hit;
  const Vec3 p = { eye.x + fwd.x * len, eye.y + fwd.y * len, eye.z + fwd.z * len };
  return p;
}

// --- firstperson.cpp (DLL only; read, not run, by the suite) --------------------------------
// Frame path before aim_frame: the switch against g_fps, the exit's re-pick, mouse look, the state log and this
// frame's view point for next tick's shots.
void firstperson_frame(const unsigned char* player);
// The frame's input entity (wm_get_input(0), probe-validated for 0x80 bytes) or NULL: a sequence's player is found
// from its server id.
void firstperson_note_input(const unsigned char* in);
// view.cpp's push detour, the frame's first client.dll push: true and the pair to push while the mode is active
// (the engine's eye and yaw, our pitch).
bool firstperson_substitute(const float* inv, float outView[16], float outInv[16]);
// Under the panel: the centre crosshair in the reticle's style.
void firstperson_draw(int W, int H);
// Duke is in state 17 under our g_fps (as of this frame's read): mouse aim and the torso stand down.
int firstperson_active(void);
// Our view is held through a ladder or use state (fp_view_hold): mouse aim stands down then too.
int firstperson_view_held(void);
// This frame's view-centre point; 0 when there is none.
int firstperson_point(Vec3* p);
// The world takes input (ma_face_world_live: world+0xC8 bit 8, world+0x14 0, no letterbox), the mask reader's own
// gate. Guarded reads: once a frame at most.
int fp_world_takes_input(const unsigned char* player);
// Inside the tick, from fpmoves' player_set_state detour: this player has just left 17 (the exit wrote +0xB1C = 1),
// by player_tick's g_fps switch or a net message's state. The track guard runs before any move of the tick can use
// the stale track.
void firstperson_left_17(const unsigned char* player, int useHold);
// The same detour, before the call: under our hold (the ladder pause, or useHold: the Interact key's scripted state),
// a stale +0xB20 is forgotten before the exit and the next state's enter can use it (fp_track_forget).
void firstperson_leaving_17(const unsigned char* player, int useHold);
// After our player_teleport out of first person (tp_keep_off_track): +0xB1C back to 0, held by the track guard until
// state 17 returns (or written back to 1 at switch-off). 1 when written.
int firstperson_hold_track_off(const unsigned char* player, const char* who);
// The quit path, before config.cfg is written: g_fps is archived, so ours goes back to "0".
void firstperson_restore_for_quit(void);
