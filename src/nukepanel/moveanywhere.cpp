// Move anywhere (moveanywhere.h): detours armed on first use and fingerprinted, acting only on the local player: the
// track constrain and snap (shared by every actor class, so others pass through) and, for depth, player_read_input_mask
// and player_track_move_vector's result. Main thread only: the tick (all four detours) and frame path share one loop.
#include "moveanywhere.h"
#include "firstperson.h"   // firstperson_active: state 17 owns the player then
#include "fpmoves.h"       // fpmoves_key_down: the Interact key
#include "teleport.h"      // teleport_repick_here: the exit's re-pick
#include "mouseaim.h"      // ma_inv3, ma_row3: the camera eye for the follow log
#include "engine.h"
#include "entities.h"
#include "hook.h"
#include "view.h"
#include "config.h"
#include "log.h"
#include "track.h"         // TrkPt: the tangent that keeps Up pure depth
#include <windows.h>
#include <intrin.h>
#include <math.h>
#include <string.h>

#pragma intrinsic(_ReturnAddress)

static const unsigned kSlotConstrain = 0x14C / 4, kSlotSnap = 0x214 / 4, kSlotReadMask = 0x154 / 4;
// The state table: player_track_move_vector is found from state 3's think (playerstate_move_update, slot +0x40).
static const unsigned kOfsStates = 0xB80, kStateMove = 3, kSlotThink = 0x40 / 4;
static const int kThinkScan = 0x200;   // its first call is at +0xE3 (0x100856b3)
static const unsigned kOfsPos = 0x18C;

typedef void (__thiscall* ConstrainFn)(void* self, float* delta);
typedef void (__fastcall* SnapFn)(void* self);
static ConstrainFn s_constrainTramp;
static SnapFn s_snapTramp;
typedef float (__cdecl* ImpValFn)(int impulse, int channel);
typedef float* (__thiscall* MoveVecFn)(void* state, float* out, const unsigned char* player, int flag, float* angles);
static ImpValFn s_impTramp;
static MoveVecFn s_moveTramp;
static const void* s_retUp;     // the return addresses of player_read_input_mask's two reads (0x67 up, 0x68 down)
static const void* s_retDown;
static int s_armed, s_refused, s_on, s_depthArmed, s_depthRefused;
// The exit's re-pick, deferred until Duke is grounded (mv_repick), on the player that was in the mode.
static int s_repickPending;
static const unsigned char* s_repickPlayer;
static unsigned s_repickWaits;
// Depth: the keys as the engine read them this tick, and away from the camera (frame path).
static int s_upHeld, s_downHeld, s_haveAway;
static Vec3 s_away;
static unsigned s_impEaten, s_depthVectors, s_depthSkipped, s_noAway, s_keysCleared;

// Set on the frame path, read in the tick: the player the detours act on and whether they act this tick.
static const unsigned char* s_player;
static int s_acting;
// MA1: Up/Down belong to the game this frame (mv_keys_to_game: ladder, hanging, head in a ladder volume).
static int s_giveKeys;
// Interact: a press waiting for the mask reader's next up read; presses seen and reads sent.
static int s_usePending, s_useLast;
static unsigned s_usePresses, s_useSent;
static unsigned s_giveFrames, s_giveEdges;
// MA2: the camera eye, Duke's position and yaw of the previous acting frame, and the snaps seen.
static int s_camPrev;
static Vec3 s_eyePrev, s_posPrev;
static float s_yawPrev, s_camMoveMax;
static unsigned s_camFrames, s_snaps, s_snapsYaw, s_snapsLogged;

// Cumulative: constrain calls for the player and those skipped; snaps kept; the farthest a snap would have moved him.
static unsigned s_constrainPlayer, s_constrainSkipped, s_snapPlayer, s_snapKept;
static float s_undoneMax, s_undoneLast;
static ULONGLONG s_nextLog;

static Vec3 rdv(const unsigned char* p) { Vec3 v; memcpy(&v, p, sizeof v); return v; }

int moveanywhere_refused(void) { return s_refused; }

// thiscall seen as fastcall: ECX = this, EDX unused, the delta popped by RET 4 either way.
static void __fastcall constrain_detour(void* self, void* /*edx*/, float* delta) {
  if (self == (const void*)s_player && s_player) {
    s_constrainPlayer++;
    if (s_acting) { s_constrainSkipped++; return; }   // the delta stays as the walk state made it (+0xB1C 0's path)
  }
  s_constrainTramp(self, delta);
}

static void __fastcall snap_detour(void* self) {
  if (self != (const void*)s_player || !s_player || !s_acting) { s_snapTramp(self); return; }
  s_snapPlayer++;
  unsigned char* p = (unsigned char*)self;
  const Vec3 before = rdv(p + kOfsPos);
  s_snapTramp(self);   // +0xB58/+0xB5C and the facing follow the nearest span
  const Vec3 after = rdv(p + kOfsPos);
  const Vec3 keep = mv_snap_keep(before, after);
  memcpy(p + kOfsPos, &keep, sizeof keep);   // the x/z the snap itself just wrote, put back
  const float dx = after.x - before.x, dz = after.z - before.z;
  s_undoneLast = sqrtf(dx * dx + dz * dz);
  if (s_undoneLast > s_undoneMax) s_undoneMax = s_undoneLast;
  s_snapKept++;
}

// While we act, the two reads behind mask bits 8 and 0x10 return 0 to the mask reader; the keys are kept for depth.
static float __cdecl impval_detour(int impulse, int channel) {
  const float v = s_impTramp(impulse, channel);
  if (!s_acting || s_giveKeys) return v;   // MA1: the ladder and the ledge read their keys as stock
  const void* ret = _ReturnAddress();
  if (ret == s_retUp) {   // the reader's own test: != 0.0
    s_upHeld = v != 0.f;
    s_impEaten++;
    const float r = mv_up_read(0, s_usePending, v);
    if (s_usePending) { s_usePending = 0; s_useSent++; }
    return r;
  }
  if (ret == s_retDown) { s_downHeld = v != 0.f; s_impEaten++; return 0.f; }
  return v;
}

// thiscall seen as fastcall: ECX the state, EDX unused, four stack args popped by RET 0x10 either way.
static float* __fastcall movevec_detour(void* state, void* /*edx*/, float* out, const unsigned char* player, int flag,
                                        float* angles) {
  float* r = s_moveTramp(state, out, player, flag, angles);
  if (!s_acting || !s_player || player != s_player || !r) return r;
  const int depth = mv_depth_axis(s_upHeld, s_downHeld);
  if (!depth) return r;
  if (!s_haveAway) { s_noAway++; return r; }
  // The engine's own first gate: while +0xE4C (a slide) is non-zero it moves nothing, and neither do we.
  float slide[3];
  memcpy(slide, player + 0xE4C, sizeof slide);
  if (slide[0] * slide[0] + slide[1] * slide[1] + slide[2] * slide[2] >= 0.001f) { s_depthSkipped++; return r; }
  unsigned mask;
  memcpy(&mask, player + 0xE00, sizeof mask);
  unsigned track;
  memcpy(&track, player + 0xB20, sizeof track);
  const int lr = mv_lr_walks(mask, player[0xB1C], track != 0);   // the engine's own walk branch
  const Vec3 in = { r[0], r[1], r[2] };
  const Vec3 o = mv_move_vector(in, lr, depth, s_away);
  r[0] = o.x; r[1] = o.y; r[2] = o.z;   // the caller's own local: every caller copies it out at once
  s_depthVectors++;
  return r;
}

// The depth detours: inp_impulse_value by export name, its two call sites inside player_read_input_mask (player
// vt+0x154), and player_track_move_vector from state 3's think. A refusal leaves the track part working.
static void arm_depth(const unsigned char* player, const void* const* vt) {
  const char* why = 0;
  unsigned char* rim = (unsigned char*)vt[kSlotReadMask];
  unsigned char* iv = (unsigned char*)GetProcAddress(GetModuleHandleA(NULL), "inp_impulse_value");
  unsigned char* mvf = 0;
  if (!entities_in_duke(vt + kSlotReadMask) || !entities_in_duke(rim) || !guarded_readable(rim, MV_READMASK_SPAN) ||
      !mv_fp_is_readmask(rim)) why = "player vt+0x154 is not player_read_input_mask";
  else if (!iv || !guarded_readable(iv, MV_IMPVAL_FP_LEN) || !mv_fp_is_impval(iv)) why = "inp_impulse_value unresolved or unknown";
  if (!why) {
    // The import slot of `push 0x67; call [slot]`, which must hold inp_impulse_value.
    unsigned slot = 0;
    for (int i = 0; i + 8 <= MV_READMASK_SPAN && !slot; ++i)
      if (rim[i] == 0x6A && rim[i + 1] == 0x67 && rim[i + 2] == 0xFF && rim[i + 3] == 0x15) memcpy(&slot, rim + i + 4, 4);
    const void* held = 0;
    if (slot && guarded_readable((const void*)(size_t)slot, 4)) memcpy(&held, (const void*)(size_t)slot, 4);
    const int up = mv_find_impulse_call(rim, MV_READMASK_SPAN, 0x67, slot);
    const int down = mv_find_impulse_call(rim, MV_READMASK_SPAN, 0x68, slot);
    if (!slot || held != iv || up < 0 || down < 0) why = "the up/down reads in player_read_input_mask were not found";
    else { s_retUp = rim + up; s_retDown = rim + down; }
  }
  if (!why) {
    const unsigned char* st = 0;
    memcpy(&st, player + kOfsStates + 4 * kStateMove, sizeof st);
    const void* const* svt = 0;
    if (st && guarded_readable(st, 4)) memcpy(&svt, st, sizeof svt);
    const unsigned char* think = svt && entities_in_duke(svt + kSlotThink) ? (const unsigned char*)svt[kSlotThink] : 0;
    if (think && entities_in_duke(think) && guarded_readable(think, kThinkScan + 8)) {
      for (int i = 0; i + 7 <= kThinkScan && !mvf; ++i) {
        if (think[i] != 0x8B || think[i + 1] != 0xCD || think[i + 2] != 0xE8) continue;   // mov ecx,ebp; call rel32
        int rel;
        memcpy(&rel, think + i + 3, 4);
        unsigned char* t = (unsigned char*)(think + i + 7) + rel;
        if (entities_in_duke(t) && guarded_readable(t, MV_MOVEVEC_FP_LEN) && mv_fp_is_movevec(t)) mvf = t;
      }
    }
    if (!mvf) why = "player_track_move_vector was not found from state 3's think";
  }
  if (!why) {
    unsigned char ivfp[MV_IMPVAL_FP_LEN], mvfp[MV_MOVEVEC_FP_LEN];
    memcpy(ivfp, iv, sizeof ivfp);
    memcpy(mvfp, mvf, sizeof mvfp);
    // The vector first: with only it in place the keys still read as look-up and crouch, as before.
    if (!hook_install_at(mvf, mvfp, sizeof mvfp, MV_MOVEVEC_PROLOGUE, (void*)movevec_detour, (void**)&s_moveTramp) ||
        !hook_install_at(iv, ivfp, sizeof ivfp, MV_IMPVAL_PROLOGUE, (void*)impval_detour, (void**)&s_impTramp))
      why = "install refused";
  }
  if (why) {
    s_depthRefused = 1;
    np_log("mv: depth on Up/Down REFUSED: %s (left/right off the line still works)", why);
    return;
  }
  s_depthArmed = 1;
  np_log("mv: depth armed: inp_impulse_value %p (reads return to %p up, %p down), player_track_move_vector %p",
         (void*)iv, s_retUp, s_retDown, (void*)mvf);
}

// Both slots from the player's vtable, fingerprinted; both installed or the row is refused.
static void arm(const unsigned char* player) {
  if (s_armed || s_refused) return;
  const void* const* vt;
  memcpy(&vt, player, sizeof vt);
  if (!entities_in_duke(vt + kSlotConstrain) || !entities_in_duke(vt + kSlotSnap + 1)) {
    s_refused = 1;
    np_log("mv: player vtable %p outside duke_base; Move anywhere REFUSED", (const void*)vt);
    return;
  }
  unsigned char* c = (unsigned char*)vt[kSlotConstrain];
  unsigned char* s = (unsigned char*)vt[kSlotSnap];
  if (!entities_in_duke(c) || !entities_in_duke(s) || !guarded_readable(c, MV_CONSTRAIN_FP_LEN) ||
      !guarded_readable(s, MV_SNAP_FP_LEN) || !mv_fp_is_constrain(c) || !mv_fp_is_snap(s)) {
    s_refused = 1;
    np_log("mv: vt+0x14C %p / vt+0x214 %p are not actor_constrain_move_to_track / actor_snap_to_track; Move "
           "anywhere REFUSED", (void*)c, (void*)s);
    return;
  }
  if (!guarded_writable(player + kOfsPos, sizeof(Vec3))) {
    s_refused = 1;
    np_log("mv: player +0x18C not writable; Move anywhere REFUSED");
    return;
  }
  unsigned char cfp[MV_CONSTRAIN_FP_LEN], sfp[MV_SNAP_FP_LEN];
  memcpy(cfp, c, sizeof cfp);
  memcpy(sfp, s, sizeof sfp);
  if (!hook_install_at(c, cfp, sizeof cfp, MV_CONSTRAIN_PROLOGUE, (void*)constrain_detour, (void**)&s_constrainTramp) ||
      !hook_install_at(s, sfp, sizeof sfp, MV_SNAP_PROLOGUE, (void*)snap_detour, (void**)&s_snapTramp)) {
    // A constrain detour left alone passes everything through: s_acting stays 0 without s_armed.
    s_refused = 1;
    np_log("mv: install refused (constrain %s, snap %s); Move anywhere REFUSED", s_constrainTramp ? "in" : "out",
           s_snapTramp ? "in" : "out");
    return;
  }
  s_armed = 1;
  np_log("mv: armed: constrain %p, snap %p (from player vt %p)", (void*)c, (void*)s, (const void*)vt);
  arm_depth(player, vt);
}

// Away from the camera for the next ticks: the game camera's forward without the track's direction at Duke.
static void away_frame(const unsigned char* player) {
  s_haveAway = 0;
  Mat4 view, proj;
  float nearZ;
  Vec3 fwd, t = { 0.f, 0.f, 0.f };
  if (!view_get_stock(&view, &proj, &nearZ) || !ma_view_forward(view, &fwd)) return;
  const TrkPt* pts = 0;
  const int n = teleport_track_polyline(player, &pts);
  const int haveT = n >= 2 && ma_track_tangent(pts, n, rdv(player + kOfsPos), &t);
  s_haveAway = mv_away(fwd, haveT, t, &s_away);
}

// MA2: every acting frame, how far the game camera's eye moved against how far Duke did. A snap (mv_cam_snap) is
// logged with Duke's yaw change, since game_track_place_camera duke!0x10096c80 turns the camera offset (+0xB2C) by
// his yaw (+0x1AC) before walking it along the track: a facing flip from a depth step would swing the camera.
static void camera_frame(const unsigned char* player) {
  Mat4 view, proj;
  float nearZ, inv[9];
  if (!view_get_stock(&view, &proj, &nearZ) || !ma_inv3(view, inv)) { s_camPrev = 0; return; }
  const Vec3 eye = ma_row3(-view.m[12], -view.m[13], -view.m[14], inv);
  const Vec3 pos = rdv(player + kOfsPos);
  float yaw;
  memcpy(&yaw, player + 0x1AC, sizeof yaw);
  if (s_camPrev) {
    const float ex = eye.x - s_eyePrev.x, ey = eye.y - s_eyePrev.y, ez = eye.z - s_eyePrev.z;
    const float px = pos.x - s_posPrev.x, py = pos.y - s_posPrev.y, pz = pos.z - s_posPrev.z;
    const float cam = sqrtf(ex * ex + ey * ey + ez * ez), duke = sqrtf(px * px + py * py + pz * pz);
    const float dyaw = mv_yaw_diff(yaw, s_yawPrev);
    s_camFrames++;
    if (cam > s_camMoveMax) s_camMoveMax = cam;
    if (mv_cam_snap(cam, duke)) {
      s_snaps++;
      if (fabsf(dyaw) > 20.f) s_snapsYaw++;
      if (s_snapsLogged++ < 60)
        np_log("mv: camera snap #%u: eye moved %.1f, Duke %.1f; yaw %.1f -> %.1f (%+.1f); keys up=%d down=%d "
               "lr=%d; camera-Duke %.1f", s_snaps, cam, duke, s_yawPrev, yaw, dyaw, s_upHeld, s_downHeld,
               (*(const unsigned*)(player + 0xE00)) & 3u,
               sqrtf((eye.x - pos.x) * (eye.x - pos.x) + (eye.y - pos.y) * (eye.y - pos.y) +
                     (eye.z - pos.z) * (eye.z - pos.z)));
    }
  }
  s_camPrev = 1;
  s_eyePrev = eye; s_posPrev = pos; s_yawPrev = yaw;
}

// The camera question: the game camera's eye and where Duke lands on screen, beside how far off the
// line he stands. A camera that keeps him on screen while undone grows follows him.
static void follow_log(const unsigned char* player, int W, int H) {
  const ULONGLONG t = GetTickCount64();
  if (t < s_nextLog) return;
  s_nextLog = t + 5000;
  Mat4 view, proj;
  float nearZ;
  float camDist = -1.f, sx = -1.f, sy = -1.f;
  const Vec3 pos = rdv(player + kOfsPos);
  float inv[9];
  if (view_get_stock(&view, &proj, &nearZ) && ma_inv3(view, inv)) {
    const Vec3 eye = ma_row3(-view.m[12], -view.m[13], -view.m[14], inv);
    const float dx = eye.x - pos.x, dy = eye.y - pos.y, dz = eye.z - pos.z;
    camDist = sqrtf(dx * dx + dy * dy + dz * dz);
    const ScreenPt sp = w2s_project(pos, view, proj, nearZ, W, H);
    if (sp.ok) { sx = sp.x; sy = sp.y; }
  }
  // constrainPlayer == skipped while on; snapPlayer == kept. undone: x/z the snaps would have moved him (off the line).
  // depth: reads eaten from the mask reader, vectors given depth, skipped while sliding, no away direction, held keys
  // cleared while the world took no input.
  np_log("mv: constrainPlayer=%u skipped=%u snapPlayer=%u kept=%u undone last %.1f max %.1f | depth armed=%d eaten=%u "
         "vectors=%u slideSkips=%u noAway=%u keysCleared=%u keys up=%d down=%d away (%.2f,%.2f) | camera-Duke %.1f, Duke "
         "on screen (%.0f,%.0f) of %dx%d, track 0x%x +0xB1C %u span %d/%d | keys to game frames=%u edges=%u | camera "
         "frames=%u maxMove %.1f snaps=%u withYawFlip=%u", s_constrainPlayer, s_constrainSkipped,
         s_snapPlayer, s_snapKept, s_undoneLast, s_undoneMax, s_depthArmed, s_impEaten, s_depthVectors, s_depthSkipped,
         s_noAway, s_keysCleared, s_upHeld, s_downHeld, s_away.x, s_away.z, camDist, sx, sy, W, H,
         *(const unsigned*)(player + 0xB20), player[0xB1C], *(const int*)(player + 0xB58),
         *(const int*)(player + 0xB5C), s_giveFrames, s_giveEdges, s_camFrames, s_camMoveMax, s_snaps, s_snapsYaw);
  s_camMoveMax = 0.f;
}

void moveanywhere_frame(const unsigned char* player, int W, int H) {
  const int want = g_cfg.moveAnywhere && !s_refused ? 1 : 0;
  s_player = player;
  const int act = mv_step(want, s_on);
  if (act == MV_ON) {
    s_on = 1;
    s_undoneMax = 0.f;
    np_log("mv: on: Up/Down move in depth; look-up, crouch, ledge climb, ladders and the four-way mover "
           "FUN_1008a700 (+-320 up/down, likely the jetpack) lose them while it is on");
  } else if (act == MV_OFF) {
    s_on = 0;
    s_acting = 0;
    np_log("mv: off (undone max %.1f units)", s_undoneMax);
    // Back onto a track the engine's way, once he stands (mv_repick); in first person the mode's own exit does it.
    s_repickPending = s_armed && player && mv_left_line(s_undoneMax) ? 1 : 0;   // never left the line: nothing to do
    if (s_armed && player && !s_repickPending) np_log("mv: no exit re-pick: Duke never left the line");
    s_repickPlayer = player;
    s_repickWaits = 0;
  }
  if (s_on) s_repickPending = 0;   // switched back on before he landed: nothing to re-pick
  const int ps = s_repickPending && player ? *(const int*)(player + 0xC10) : -1;   // read only while one waits
  const int r = mv_repick(s_repickPending, player && player == s_repickPlayer, ps, firstperson_active());
  if (r == MV_REPICK_NOW) {
    s_repickPending = 0;
    np_log("mv: exit re-pick after %u frame(s) waiting (PS %d)", s_repickWaits, ps);
    teleport_repick_here(player, "mv");
  } else if (r == MV_REPICK_DROP) {
    s_repickPending = 0;
    np_log("mv: exit re-pick dropped (%s)", firstperson_active() ? "first person owns the exit" : "another player");
  } else if (r == MV_REPICK_WAIT) {
    s_repickWaits++;
  }
  if (s_on && player) arm(player);
  s_acting = s_on && s_armed && mv_acts(1, player != 0, firstperson_active()) ? 1 : 0;
  if (!s_acting) {
    s_upHeld = 0; s_downHeld = 0; s_haveAway = 0; s_giveKeys = 0; s_camPrev = 0; s_usePending = 0;
    return;
  }
  const int state = *(const int*)(player + 0xB7C);
  const int give = mv_keys_to_game(state, entities_snapshot().atLadder) ? 1 : 0;
  if (give != s_giveKeys) {
    if (give) s_giveEdges++;
    np_log("mv: Up/Down %s (state %d, head in a ladder volume %d)", give ? "back to the game" : "move in depth again",
           state, entities_snapshot().atLadder);
    s_giveKeys = give;
  }
  if (give) { s_giveFrames++; s_upHeld = 0; s_downHeld = 0; }   // no depth while the game has the keys
  s_usePending = mv_use_pending(s_usePending, give);
  const int use = fpmoves_key_down(g_cfg.interactKey) ? 1 : 0;
  if (use && !s_useLast && !give) {   // with the keys given back, Up itself already uses
    s_usePending = 1;
    s_usePresses++;
    np_log("mv: interact: one tick of Up/use to the game (presses %u, sent %u)", s_usePresses, s_useSent);
  }
  s_useLast = use;
  camera_frame(player);
  const int live = fp_world_takes_input(player);
  if ((s_upHeld || s_downHeld) && !live) s_keysCleared++;
  s_upHeld = mv_key_held(s_upHeld, live);
  s_downHeld = mv_key_held(s_downHeld, live);
  away_frame(player);
  follow_log(player, W, H);
}
