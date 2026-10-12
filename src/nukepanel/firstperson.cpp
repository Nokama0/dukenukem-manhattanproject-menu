// First person (firstperson.h): g_fps through cvar_t::set, as display.cpp sets r_mode; our mouse look on Duke's yaw
// +0x1AC (state 17 writes it every tick, free-move.md section 1c) and our pitch in the pushed view only; the exit's
// re-pick through player_teleport. Main thread only: frame path, push detour and tick share one loop (render.md s1).
#include "firstperson.h"
#include "freecam.h"     // FreecamPose and its matrices: the substituted view
#include "engine.h"
#include "cvar_read.h"
#include "teleport.h"    // teleport_repick_here, teleport_world_hit
#include "hotkeys.h"     // hotkeys_mouse_xy_take: the device's own deltas
#include "aim.h"         // aim_fire_origin: verifies the player's fire getters for the detour
#include "entities.h"    // entities_snapshot().atLadder: Duke's head in a ladder volume
#include "fpmoves.h"     // fpmoves_eye_drop: the crouched eye
#include "input.h"
#include "canvas.h"        // also canvas_quit_installed: the quit detour that restores g_fps
#include "config.h"
#include "log.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned kPlayerSpanFp = 0xED0;   // probe's kPlayerSpan: every player field read here ends inside it
static const unsigned kOfsYaw = 0x1AC, kOfsAngles = 0x1A8, kOfsState = 0xB7C, kOfsPs = 0xC10, kOfsTrackOn = 0xB1C;

static cvar_t* s_fps;
static cvar_t* s_rpg;
static int s_resolveTried, s_unavailLogged;
static FpState s_st;
static int s_active;          // state 17 under our g_fps, as of this frame's read
static int s_wantLast = -1, s_refuseLogged, s_applyFailed;
static unsigned s_applyFrames;   // frames in a level since the apply without state 17 (the cvar was not taken)
static int s_notTakenLogged;
static unsigned s_rpgCheck;
static int s_wantPrev;   // last frame's want (switch on, not paused): its rise applies at once
static int s_quitRefuseLogged;
static unsigned s_lookHeld;   // frames whose deltas were drained but not applied (world not live)
// Pause and resume (firstperson.h fp_pause_step): cumulative pauses per reason and resumes.
static FpPause s_pause;
static unsigned s_pausesNoPlayer, s_pausesLadder, s_resumes;
static ULONGLONG s_pausedAt;
static FpLadderGate s_ladderGate;   // the ladder reason's grab condition and limbo clock (fp_ladder_reason)
static FpLadderAir s_ladderAir;     // after a ladder state, held until grounded (fp_ladder_air)
static unsigned s_airLands;
// The track guard's hold (fp_track_guard): whose +0xB1C it holds at 0, keyed with his server id (a new
// player at a reused address is not ours).
static const unsigned char* s_trackOffPlayer;
static int s_trackOffSid;
static unsigned s_limboEnds;

// The view: the last substituted pose (this frame's push), our pitch, and the frame's centre point.
static FreecamPose s_pose;
static int s_poseValid, s_haveView, s_havePoint, s_firstSubLogged;
static float s_pitch;
static Vec3 s_point;

// Diagnostics (cumulative): the spec's entry/exit, yaw and pitch ranges, and every state change while on.
static const unsigned char* s_player;
static int s_lastState = -1, s_lastPs = -1, s_lastHealth = 0x7FFF, s_lastLetterbox = -1;
static unsigned s_frames, s_subs, s_yawWrites, s_rays, s_hits, s_noView, s_entries, s_exits, s_repicks, s_drops;
static unsigned s_stateChanges, s_unwritable, s_watchFrames;
static double s_mickX, s_mickY;
static float s_yawMin, s_yawMax, s_pitchMin, s_pitchMax;
static int s_rangeSeeded;
static ULONGLONG s_nextLog;

static int rdi(const unsigned char* p) { int v; memcpy(&v, p, sizeof v); return v; }
static float rdf(const unsigned char* p) { float f; memcpy(&f, p, sizeof f); return f; }

static bool resolve(void) {
  if (!s_resolveTried) {
    s_resolveTried = 1;
    if (g_eng.cvar_get) { s_fps = g_eng.cvar_get("g_fps"); s_rpg = g_eng.cvar_get("g_rpg"); }
    np_log("fp: g_fps %p, g_rpg %p, cvar_t::set %s", (void*)s_fps, (void*)s_rpg,
           engine_resolve_cvar_set() ? "resolved" : "UNRESOLVED");
  }
  return s_fps && s_rpg && engine_resolve_cvar_set();
}

static bool read_num(cvar_t* cv, double* v, char* text, size_t cap) {
  if (!cvar_read_string(cv, text, cap)) return false;
  *v = atof(text);
  return true;
}

// Through cvar_t::set (RET 4, follows the +0xBC link), then read back: a validator's refusal still returns 1.
static bool set_fps(const char* value) {
  const bool ok = g_p3.cvar_set(s_fps, value);
  char now[64] = "";
  const bool read = cvar_read_string(s_fps, now, sizeof now);
  const bool took = ok && read && strcmp(now, value) == 0;
  np_log("fp: g_fps \"%s\" %s (set=%d, now \"%s\")", value, took ? "set" : "REFUSED", ok ? 1 : 0, read ? now : "?");
  return took;
}

static int rpg_on(void) {
  char t[64];
  double v = 0.0;
  return read_num(s_rpg, &v, t, sizeof t) && v != 0.0 ? 1 : 0;
}

int firstperson_active(void) { return s_active; }
static int s_holdNow;   // mirrors s_hold for callers declared before it
int firstperson_view_held(void) { return s_holdNow; }
int firstperson_point(Vec3* p) {
  if (!s_havePoint) return 0;
  *p = s_point;
  return 1;
}

// The view hold (fp_view_hold): our view through a ladder or a use state, built from Duke's own position.
static int s_hold, s_lastFpPoseOk, s_yawRestore;
static const unsigned char* s_viewPlayer;   // whose eye the held view is built from (this frame's hold)
static int s_holdScene;                     // the hold is a game sequence (input elsewhere)
static int s_holdingNow;                    // this frame's hold, not a game sequence (the track guard's)
static unsigned s_sceneSearches, s_sceneFound, s_sceneRefused;
static float s_holdYaw, s_holdHand = 1.f;
static FreecamPose s_lastFpPose;   // the last state-17 view: where a hold starts from
static unsigned s_holds, s_holdFrames, s_holdLooks, s_holdLen;
static ULONGLONG s_holdReasonAt;   // the last frame the hold had a reason of its own
static int s_handbackLeft;         // fp_handback: frames the hold's yaw may still be shown in 17
static float s_handbackYaw;
static unsigned s_handbacks, s_handbackFrames;

bool firstperson_substitute(const float* inv, float outView[16], float outInv[16]) {
  if (!s_active && s_hold && s_viewPlayer) {
    FreecamPose h;
    const unsigned char* pl = s_viewPlayer;
    // The held player came from last frame (a sequence's from a search) and a level change may free it in the
    // tick between: one guarded read of the fields used, and the removed flag.
    // Also after frames the probe skipped (no firstperson_frame), so the read is guarded for every hold.
    if (!guarded_readable(pl + 0x90, 0x198 - 0x90) || rdi(pl + 0x90) != 0) return false;
    h.eye[0] = rdf(pl + 0x18C); h.eye[1] = rdf(pl + 0x190) + 40.f; h.eye[2] = rdf(pl + 0x194);   // 17's own eye
    h.yaw = s_holdYaw; h.pitch = s_pitch; h.hand = s_holdHand;
    freecam_matrices(h, outView, outInv);
    s_haveView = 1;
    s_subs++;
    return true;
  }
  if (!s_active) return false;
  // The engine's own push reads it unguarded; ours is checked once a push (one a frame).
  if (!guarded_readable(inv, 16 * sizeof(float))) return false;
  FreecamPose p = freecam_pose_from_inverse(inv);
  const float enginePitch = p.pitch;
  p.pitch = s_pitch;
  if (s_handbackLeft > 0) {   // 17's first pushes after a hold: the hold's yaw until the engine's view has it
    const float engineYaw = p.yaw;
    const int was = s_handbackLeft;
    p.yaw = fp_handback(&s_handbackLeft, s_handbackYaw, engineYaw);
    if (p.yaw == s_handbackYaw && engineYaw != s_handbackYaw) s_handbackFrames++;
    if (s_handbackLeft == 0 && was > 0)
      np_log("fp: handback #%u: the hold's yaw %.1f shown for %u frame(s); the engine's view now %.1f (%s)",
             s_handbacks, s_handbackYaw, s_handbackFrames, engineYaw, p.yaw == engineYaw ? "agrees" : "cap reached");
  }
  p.eye[1] -= fpmoves_eye_drop();   // the Crouch key: the eye comes down with the engine's height ratio
  freecam_matrices(p, outView, outInv);
  s_pose = p;
  s_poseValid = 1;
  s_lastFpPose = p;
  s_lastFpPoseOk = 1;
  s_haveView = 1;
  s_subs++;
  if (!s_firstSubLogged && s_player) {
    s_firstSubLogged = 1;
    // The yaw convention fp_yaw_sign relies on, against the camera the engine built from the same yaw.
    const float yaw = rdf(s_player + kOfsYaw);
    np_log("fp: first view: eye (%.1f,%.1f,%.1f) pos.y %.1f yaw %.1f engine pitch %.1f hand %+.0f, forward agrees "
           "with (-sin yaw, cos yaw) by %.3f", p.eye[0], p.eye[1], p.eye[2], rdf(s_player + 0x18C + 4), yaw,
           enginePitch, p.hand, fp_forward_agree(yaw, inv + 8));
  }
  return true;
}

static void range(float yaw) {
  if (!s_rangeSeeded) { s_yawMin = s_yawMax = yaw; s_pitchMin = s_pitchMax = s_pitch; s_rangeSeeded = 1; }
  if (yaw < s_yawMin) s_yawMin = yaw;
  if (yaw > s_yawMax) s_yawMax = yaw;
  if (s_pitch < s_pitchMin) s_pitchMin = s_pitch;
  if (s_pitch > s_pitchMax) s_pitchMax = s_pitch;
}

static void log_track_counts(const char* what);

static void log_ranges(const char* what) {
  np_log("fp: %s frames=%u views=%u yawWrites=%u unwritable=%u mickeys=(%.0f,%.0f) yaw %.1f..%.1f pitch %.1f..%.1f "
         "rays=%u hits=%u noview=%u stateChanges=%u entries=%u exits=%u repicks=%u dropped=%u lookHeld=%u | pauses "
         "noplayer=%u ladder=%u resumes=%u | view holds=%u frames=%u looks=%u scene searches=%u found=%u refused=%u", what,
         s_frames, s_subs, s_yawWrites, s_unwritable, s_mickX, s_mickY, s_yawMin, s_yawMax, s_pitchMin, s_pitchMax,
         s_rays, s_hits, s_noView, s_stateChanges, s_entries, s_exits, s_repicks, s_drops, s_lookHeld,
         s_pausesNoPlayer, s_pausesLadder, s_resumes, s_holds, s_holdFrames, s_holdLooks, s_sceneSearches,
         s_sceneFound, s_sceneRefused);
  log_track_counts(what);
}

// Every state change while the mode holds (death, cutscene, rope, jetpack...): these lines settle the scripted
// states question. Health and the letterbox are read every 10th frame (guarded reads).
static void watch_states(const unsigned char* player) {
  const int st = rdi(player + kOfsState), ps = rdi(player + kOfsPs);
  if (st != s_lastState || ps != s_lastPs) {
    if (s_lastState >= 0) {
      s_stateChanges++;
      np_log("fp: state %d -> %d, PS %d -> %d, +0xB1C %u, pos (%.1f,%.1f,%.1f)", s_lastState, st, s_lastPs, ps,
             player[kOfsTrackOn], rdf(player + 0x18C), rdf(player + 0x190), rdf(player + 0x194));
    }
    s_lastState = st; s_lastPs = ps;
  }
  if (++s_watchFrames % 10) return;
  const unsigned char* stats = 0;
  short health = 0x7FFF;
  if (guarded_readable(player + 0x111C, 4)) memcpy(&stats, player + 0x111C, sizeof stats);
  if (stats && guarded_readable(stats, 2)) memcpy(&health, stats, sizeof health);
  const int lb = input_ui_window("system_03", "letterbox") != 0;
  if (health != s_lastHealth && (health <= 0 || s_lastHealth <= 0))
    np_log("fp: health %d -> %d (state %d, PS %d)", s_lastHealth, health, st, ps);
  if (lb != s_lastLetterbox && s_lastLetterbox >= 0) np_log("fp: cutscene letterbox %s (state %d)", lb ? "up" : "down", st);
  s_lastHealth = health;
  s_lastLetterbox = lb;
}

// world+0xC8 bit 8, world+0x14 0 and no letterbox: mouse aim's own gate (mouseaim.h ma_face_world_live).
int fp_world_takes_input(const unsigned char* player) {
  const unsigned char* w = 0;
  memcpy(&w, player + 0xE0, sizeof w);
  unsigned char c8 = 0;
  int w14 = 1;
  if (w && guarded_readable(w + 0x14, 0xC8 + 1 - 0x14)) { c8 = w[0xC8]; memcpy(&w14, w + 0x14, sizeof w14); }
  return ma_face_world_live(c8, w14, input_ui_window("system_03", "letterbox") != 0) ? 1 : 0;
}

// Mouse look: the device's deltas turn Duke (yaw +0x1AC) and tilt our view only (s_pitch). Drained every active
// frame so no stale turn lands later; applied only by fp_look_applies (not under the pause menu or a
// cutscene).
static void look(const unsigned char* player) {
  int dx = 0, dy = 0;
  const bool read = hotkeys_mouse_xy_take(&dx, &dy);
  float yaw = rdf(player + kOfsYaw);
  const bool moved = read && (dx || dy);
  if (moved && !fp_look_applies(1, input_panel_open(), input_console_open(), input_freecam_on(), fp_world_takes_input(player)))
    s_lookHeld++;
  else if (moved) {
    s_mickX += (double)(dx < 0 ? -dx : dx);
    s_mickY += (double)(dy < 0 ? -dy : dy);
    s_pitch = fp_pitch_step(s_pitch, dy, g_cfg.mouseSens);
    if (dx && s_poseValid) {
      float r[3], u[3], f[3];
      freecam_axes(s_pose, r, u, f);
      const float set = fp_yaw_step(yaw, dx, g_cfg.mouseSens, fp_yaw_sign(yaw, r));
      unsigned char* at = const_cast<unsigned char*>(player) + kOfsYaw;
      if (guarded_writable(at, sizeof(float))) {
        memcpy(at, &set, sizeof set);   // a field state 17 itself writes every tick (playerstate_fps_think)
        yaw = set;
        s_yawWrites++;
      } else if (s_unwritable++ == 0) {
        np_log("fp: player +0x1AC at %p not writable; no mouse turn", (void*)at);
      }
    }
  }
  range(yaw);
}

// The view's centre ray, cast once a frame from this frame's substituted eye.
static void centre_point(const unsigned char* player, int haveView) {
  if (!haveView) { s_noView++; return; }
  Vec3 eye0;
  aim_fire_origin(player, &eye0);   // verifies the player's getters, which the fire-dir detour calls
  float r[3], u[3], f[3];
  freecam_axes(s_pose, r, u, f);
  const float to[3] = { s_pose.eye[0] + f[0] * FP_RAY_LEN, s_pose.eye[1] + f[1] * FP_RAY_LEN,
                        s_pose.eye[2] + f[2] * FP_RAY_LEN };
  float h[3];
  s_rays++;
  const int hit = teleport_world_hit(player, s_pose.eye, to, h) ? 1 : 0;
  if (hit) s_hits++;
  const Vec3 eye = { s_pose.eye[0], s_pose.eye[1], s_pose.eye[2] }, fwd = { f[0], f[1], f[2] },
             hv = { h[0], h[1], h[2] };
  s_point = fp_view_point(eye, fwd, hit, hv, FP_RAY_LEN);
  s_havePoint = 1;
}

static void enter_log(const unsigned char* player) {
  s_entries++;
  s_rangeSeeded = 0;
  if (!s_hold && !s_yawRestore) s_pitch = 0.f;   // a ladder or use hold hands its pitch back
  np_log("fp: on: state %d -> 17, yaw %.1f, angles (%.1f,%.1f,%.1f), +0xB1C %u, track 0x%x", s_lastState,
         rdf(player + kOfsYaw), rdf(player + kOfsAngles), rdf(player + kOfsAngles + 4), rdf(player + kOfsAngles + 8),
         player[kOfsTrackOn], (unsigned)rdi(player + 0xB20));
}

static const char* pause_name(int r) { return r == FP_PAUSE_LADDER ? "a ladder" : "no player (input elsewhere)"; }

// The pause for this frame: no player (level change, loading, a scripted `camera`) or a ladder. The world's input
// gate is read only while a pause waits to end (one guarded read).
static void pause_frame(const unsigned char* player, int want) {
  const int state = player ? rdi(player + kOfsState) : -1;
  const ULONGLONG now = GetTickCount64();
  const int pausedLadder = s_pause.paused && s_pause.reason == FP_PAUSE_LADDER;
  const int wasLatched = s_ladderGate.latched, wasAir = s_ladderAir.on;
  // The engine's grab: head in a ladder volume and Up (mask 8, +0xE00), or the ladder states 5/6, or airborne after
  // them (+0xB00 the grounded byte).
  const int atLadder = player && fp_ladder_reason_air(&s_ladderGate, &s_ladderAir, player[0xB00] != 0,
                                                      entities_snapshot().atLadder, (rdi(player + 0xE00) & 8) != 0,
                                                      state, pausedLadder, s_trackOffPlayer == player, now);
  if (wasAir && !s_ladderAir.on) {
    s_airLands++;
    np_log("fp: off the ladder: state %d, grounded %d; the air hold ends (lands %u)", state,
           player ? player[0xB00] != 0 : 0, s_airLands);
  }
  if (s_ladderGate.latched && !wasLatched) {
    s_limboEnds++;
    np_log("fp: ladder pause ends: state %d for %u ms with +0xB1C held 0 and no ladder state; first person resumes "
           "(limbo ends %u)", state, FP_LADDER_LIMBO_MS, s_limboEnds);
  }
  const int reason = fp_pause_reason(player != 0, atLadder);
  const int wasPaused = s_pause.paused, wasReason = s_pause.reason;
  const int live = s_pause.paused && !reason && player ? fp_world_takes_input(player) : 0;
  fp_pause_step(&s_pause, want, reason, live, now);
  if (s_pause.paused && (!wasPaused || s_pause.reason != wasReason)) {
    if (s_pause.reason == FP_PAUSE_LADDER) s_pausesLadder++; else s_pausesNoPlayer++;
    if (!wasPaused) s_pausedAt = now;
    np_log("fp: paused for %s (state %d, g_fps %s)", pause_name(s_pause.reason), state, s_st.applied ? "1" : "0");
  } else if (wasPaused && !s_pause.paused && want) {
    s_resumes++;
    np_log("fp: resumed after %llu ms (%s; state %d)", now - s_pausedAt, pause_name(wasReason), state);
  }
}

static void frame_core(const unsigned char* player);

static void note_player(const unsigned char* player) {
  if (player == s_player) return;
  s_player = player; s_lastState = -1; s_lastPs = -1; s_lastLetterbox = -1;
  s_lastFpPoseOk = 0; s_yawRestore = 0; s_handbackLeft = 0;   // a new player: no view to carry over
  s_hold = 0; s_holdNow = 0; s_holdReasonAt = 0;   // nor a hold
  s_ladderAir.on = 0;                              // nor a ladder's air hold (a level change)
}

// Sequences (fp_scene_hold): while the input is elsewhere the probe has no player; the search finds him from the
// input entity's server id, one search a frame during the pause only.
static const unsigned char* s_input;
static const void* s_playerVt;   // the player class's vtable, from the probe's player
void firstperson_note_input(const unsigned char* in) { s_input = in; }

typedef int (__cdecl* GetServerFn)(int sid);
static GetServerFn s_getServer;
static int s_getServerTried, s_lastSid, s_haveLastSid;

static const unsigned char* scene_player(void) {
  if (!s_input || !s_playerVt || !g_eng.spawn_search || !g_eng.spawn_search_get || !g_eng.spawn_search_stop) return 0;
  if (!s_getServerTried) {
    s_getServerTried = 1;   // prism3d's own export (ordinal 364): spawner_find_server on the global spawner
    s_getServer = (GetServerFn)GetProcAddress(GetModuleHandleA(NULL), "spawn_get_server");
    np_log("fp: spawn_get_server %p", (void*)s_getServer);
  }
  unsigned user;
  int killed, sid;
  memcpy(&user, s_input + 0x4C, sizeof user);   // both inside the probe's 0x80-byte read
  memcpy(&sid, s_input + 0x78, sizeof sid);
  killed = 1;   // the removed flag lies past that read: one guarded read
  if ((user & 0x200u) && guarded_readable(s_input + 0x90, sizeof killed)) memcpy(&killed, s_input + 0x90, sizeof killed);
  const int server = s_getServer && (user & 0x200u) && !killed && s_haveLastSid && sid == s_lastSid
                         ? (s_getServer(sid) != 0) : 0;
  if (!fp_scene_search_ok(user, killed, sid, s_haveLastSid, s_lastSid, server)) { s_sceneRefused++; return 0; }
  s_sceneSearches++;
  g_eng.spawn_search("s=%d?t=%d?Y=%d", sid, 2, 0x100);   // get_local_player's own query (duke!0x100e7060)
  const unsigned char* e;
  const unsigned char* found = 0;
  int n = 0;
  while ((e = (const unsigned char*)g_eng.spawn_search_get()) != 0 && n++ < 64) {
    if (found || !guarded_readable(e, kPlayerSpanFp)) continue;
    const void* vt;
    unsigned u;
    int k;
    memcpy(&vt, e, sizeof vt);
    memcpy(&u, e + 0x4C, sizeof u);
    memcpy(&k, e + 0x90, sizeof k);
    if (fp_scene_player_ok(u, k, vt == s_playerVt)) found = e;
  }
  g_eng.spawn_search_stop();   // start/stop paired on every path
  if (found) s_sceneFound++;
  return found;
}

// While the hold lasts: our view, the body hidden. On a ladder or use state mouse look turns the view only (the
// ladder states write Duke's yaw each tick: playerstate_climb_pretick 0x10083f40 sets +0x1A8/+0x1AC); in a sequence
// the view faces where the engine turns him plus the mouse's offset. Back in state 17, his yaw is turned to the view.
static float s_sceneOffset;

static void hold_frame(const unsigned char* player) {
  if (player && !rdi(player + 0x90)) {   // a live player: his class and his server, for the sequence search
    memcpy(&s_playerVt, player, sizeof s_playerVt);
    s_lastSid = rdi(player + 0x78);
    s_haveLastSid = 1;
  }
  const int switchOn = g_cfg.firstPerson != 0;
  const int pausedNoPlayer = s_pause.paused && s_pause.reason == FP_PAUSE_NO_PLAYER;
  const unsigned char* vp = player ? player : (switchOn && pausedNoPlayer && !s_active ? scene_player() : 0);
  const int state = vp ? rdi(vp + kOfsState) : -1;
  // A sequence only while the probe has no player; with him back the running hold waits view-only (fp_wait_hold).
  const int scene = !s_active && !player && fp_scene_hold(switchOn, pausedNoPlayer, vp != 0);
  const int reason = scene || (!s_active && fp_wait_hold(s_hold, pausedNoPlayer, player != 0)) ||
                     (!s_active && fp_view_hold(switchOn, s_pause.paused && s_pause.reason == FP_PAUSE_LADDER,
                                                s_st.applied, state, player != 0));
  const ULONGLONG now = GetTickCount64();
  if (reason) s_holdReasonAt = now;
  // Past its reason the hold bridges to state 17, so no game-camera frame shows in between.
  const int hold = reason || (!s_active && player && fp_view_bridge(s_hold, switchOn, state, now - s_holdReasonAt));
  if (hold && (!s_hold || scene != s_holdScene)) {
    if (!s_hold) { s_holds++; s_holdLen = 0; }
    if (scene) {   // a sequence starts facing where Duke faces
      s_sceneOffset = 0.f;
      if (s_lastFpPoseOk) s_holdHand = s_lastFpPose.hand;
    }
    else if (!s_hold) {   // never the stale pose of 17's first frame (fp_hold_seed_yaw)
      s_holdYaw = fp_hold_seed_yaw(state == FP_ENGINE_STATE, rdf(vp + kOfsYaw), s_lastFpPoseOk, s_lastFpPose.yaw);
      if (s_lastFpPoseOk) s_holdHand = s_lastFpPose.hand;
    }
    np_log("fp: view held (state %d, %s); yaw %.1f pitch %.1f", state,
           scene ? "game sequence, input elsewhere" : s_pause.paused ? "ladder pause" : "interact hold-off",
           scene ? fp_scene_yaw(rdf(vp + kOfsYaw), 0.f) : s_holdYaw, s_pitch);
  } else if (!hold && s_hold) {
    s_yawRestore = 1;
    np_log("fp: view hold ends (state %d, %u frames this hold; yaw %.1f pitch %.1f)", state, s_holdLen, s_holdYaw,
           s_pitch);
  }
  s_hold = hold;
  s_holdNow = hold;
  s_holdScene = hold && scene;
  s_holdingNow = hold && !scene && player;   // the track guard's hold: never a game sequence
  s_viewPlayer = hold ? vp : 0;
  fpmoves_hide_body(vp, hold);
  if (s_yawRestore && s_active && player) {
    s_yawRestore = 0;
    unsigned char* at = const_cast<unsigned char*>(player) + kOfsYaw;
    const float y = fp_engine_yaw_for_view(s_holdYaw, rdf(player + kOfsYaw));
    if (guarded_writable(at, sizeof y)) memcpy(at, &y, sizeof y);   // a field state 17 writes itself
    unsigned char* trk = const_cast<unsigned char*>(player) + kOfsTrackOn;
    if (fp_handback_track_off(rdi(player + kOfsState), *trk != 0) && guarded_writable(trk, 1)) {
      *trk = 0;   // 17's own value, a tick early: no snap re-faces him (fp_handback_track_off)
      np_log("fp: handback: +0xB1C 1 -> 0 with the yaw %.1f (engine %.1f), as 17's pre-tick writes it", s_holdYaw, y);
    }
    s_handbackLeft = FP_HANDBACK_MAX;   // this frame's push predates the write (fp_handback)
    s_handbackYaw = s_holdYaw;
    s_handbacks++;
    s_handbackFrames = 0;
  } else if (s_yawRestore && !switchOn) {
    s_yawRestore = 0;
  }
  if (!hold) return;
  s_holdFrames++;
  s_holdLen++;
  int dx = 0, dy = 0;
  const float deg = fp_deg_per_mickey(g_cfg.mouseSens);
  if (hotkeys_mouse_xy_take(&dx, &dy) && (dx || dy) &&
      fp_look_applies(1, input_panel_open(), input_console_open(), input_freecam_on(), fp_world_takes_input(vp))) {
    if (s_holdScene) s_sceneOffset += s_holdHand * (float)dx * deg;
    else s_holdYaw += s_holdHand * (float)dx * deg;
    s_pitch = fp_pitch_step(s_pitch, dy, g_cfg.mouseSens);
    s_holdLooks++;
  }
  if (s_holdScene) s_holdYaw = fp_scene_yaw(rdf(vp + kOfsYaw), s_sceneOffset);
}

// The track guard (fp_track_guard): +0xB1C back to 0 while our hold has Duke out of 17 over a stale track. Run on
// the frame path and, for the ladder pause's own exit, inside the tick right after player_tick's switch leaves 17
// (firstperson_left_17, from fpmoves' player_set_state detour), before any move of that tick can use the track.
static unsigned s_trackOffs, s_trackOffsInTick, s_trackClears, s_trackRestores, s_trackUnwritable, s_trackNear;
static FpTrail s_trail;
static int s_trailEvent;   // set by a guard action: the trail dumps this frame

static void track_guard(const unsigned char* player, int holding, const char* where) {
  if (s_trackOffPlayer && (s_trackOffPlayer != player || rdi(player + 0x78) != s_trackOffSid))
    s_trackOffPlayer = 0;   // a new player starts on a track
  const int switchOn = g_cfg.firstPerson != 0;
  const int state = rdi(player + kOfsState);
  const int on = player[kOfsTrackOn] != 0;
  const void* track = 0;
  float dist = 0.f;
  const int have = switchOn && holding && state != FP_ENGINE_STATE && on ? teleport_track_snap(player, &track, &dist) : 0;
  const int act = fp_track_guard(switchOn, s_trackOffPlayer != 0, holding, state, on, have, dist);
  unsigned char* b = const_cast<unsigned char*>(player) + kOfsTrackOn;
  if (act == FP_TRK_NONE && have) s_trackNear++;
  if (act == FP_TRK_OFF) {
    if (!guarded_writable(b, 1)) {
      if (s_trackUnwritable++ == 0) np_log("fp: track guard: +0xB1C at %p not writable", (void*)b);
      return;
    }
    *b = 0;   // the value state 17's pre-tick, the state 13 enter and state 4 write
    s_trackOffPlayer = player;
    s_trackOffSid = rdi(player + 0x78);
    s_trackOffs++;
    s_trailEvent = 1;
    char d[32] = "unmeasured";
    if (have) sprintf_s(d, "%.1f units", dist);
    np_log("fp: track guard (%s): +0xB1C 1 -> 0 in state %d, PS %d at (%.1f,%.1f,%.1f): the snap onto +0xB20 %p "
           "would move him %s (offs %u, in tick %u)", where, state, rdi(player + kOfsPs), rdf(player + 0x18C),
           rdf(player + 0x190), rdf(player + 0x194), track ? track : (const void*)(size_t)rdi(player + 0xB20), d,
           s_trackOffs, s_trackOffsInTick);
  } else if (act == FP_TRK_CLEAR) {
    s_trackOffPlayer = 0;
    s_trackClears++;
    np_log("fp: track guard ends: back in state 17 (+0xB20 %p kept, never snapped)", (const void*)(size_t)rdi(player + 0xB20));
  } else if (act == FP_TRK_RESTORE) {
    s_trackOffPlayer = 0;
    s_trackRestores++;
    s_trailEvent = 1;
    if (guarded_writable(b, 1)) *b = 1;   // the 17 exit's own value, which the guard held back
    const int stale = rdi(player + 0xB20) != 0;
    np_log("fp: track guard ends: switched off in state %d, +0xB1C back to 1%s", state,
           stale ? "; the stale +0xB20 re-picked first" : " (+0xB20 already clear: the next tick picks)");
    if (stale) teleport_repick_here(player, "fp");   // clears +0xB20, then picks by area or nearest
  }
}

int firstperson_hold_track_off(const unsigned char* player, const char* who) {
  unsigned char* b = const_cast<unsigned char*>(player) + kOfsTrackOn;
  if (!g_cfg.firstPerson || !guarded_writable(b, 1)) return 0;
  const unsigned was = *b;
  *b = 0;   // the value state 17's pre-tick writes; the next tick's switch returns him to 17
  s_trackOffPlayer = player;
  s_trackOffSid = rdi(player + 0x78);
  s_trackOffs++;
  s_trailEvent = 1;
  np_log("%s: track guard: +0xB1C %u -> 0 after the teleport (state %d): Duke stays at the target", who, was,
         rdi(player + kOfsState));
  return 1;
}

// The 17 exit's camera (fp_exit_view_fix): the view before the exit, then after it the camera that follows Duke put
// back when the exit's search found another. Runs on every exit from 17, the switch off and a console g_fps 0 too.
// set_view_entity is the exit's own callee (fp_find_view_call); a refusal is retried every FP_SETVIEW_RETRY_MS.
typedef void (__fastcall* SetViewFn)(void* ecx, void* edx, void* ent);   // thiscall(ent), RET 4, ECX unread
static SetViewFn s_setView;
static ULONGLONG s_setViewNext;   // the next lookup after a refusal (not one-shot)
static unsigned s_setViewRefusals;
static const void* s_viewBefore;
static unsigned s_viewExits, s_viewFixes, s_viewNoFollow;

static void exit_view_before(void) { s_viewBefore = g_eng.wm_get_object ? g_eng.wm_get_object(0) : 0; }

static void resolve_set_view(const unsigned char* player) {
  const ULONGLONG now = GetTickCount64();
  if (s_setView || now < s_setViewNext) return;
  s_setViewNext = now + FP_SETVIEW_RETRY_MS;
  const unsigned char* st = 0;
  memcpy(&st, player + 0xB80 + 4 * FP_ENGINE_STATE, sizeof st);
  const void* const* svt = 0;
  if (st && guarded_readable(st, 4)) memcpy(&svt, st, sizeof svt);
  const unsigned char* ex = svt && entities_in_duke(svt + 2) ? (const unsigned char*)svt[2] : 0;   // +0x08 exit
  int t = 0;
  const int r = ex && entities_in_duke(ex) && guarded_readable(ex, 0x40) ? fp_find_view_call(ex, 0x40, &t) : -1;
  const unsigned char* fn = r >= 0 ? ex + t : 0;
  if (fn && entities_in_duke(fn) && guarded_readable(fn, FP_SETVIEW_FP_LEN) && fp_fp_is_set_view(fn))
    s_setView = (SetViewFn)fn;
  if (s_setView || s_setViewRefusals++ < 4)
    np_log("fp: set_view_entity %p (17 exit %p)%s", (const void*)s_setView, (const void*)ex,
           s_setView ? "" : ": REFUSED for now, the exit's camera is left as it is (retried)");
}

// The camera on Duke's server that follows him, or NULL.
static const void* follow_camera(const unsigned char* player) {
  if (!g_eng.spawn_search || !g_eng.spawn_search_get || !g_eng.spawn_search_stop) return 0;
  g_eng.spawn_search("s=%d?t=%d?Y=%d", rdi(player + 0x78), 2, 0x200);   // the exit's own query
  const unsigned char* e;
  const void* found = 0;
  int n = 0;
  while ((e = (const unsigned char*)g_eng.spawn_search_get()) != 0 && n++ < 64) {
    if (found || !guarded_readable(e, 0xB38)) continue;
    unsigned type, user;
    int killed;
    const void* target;
    memcpy(&type, e + 0x48, 4); memcpy(&user, e + 0x4C, 4); memcpy(&killed, e + 0x90, 4);
    memcpy(&target, e + 0xB34, sizeof target);
    if (fp_cam_follows(type, user, killed, target, player)) found = e;
  }
  g_eng.spawn_search_stop();   // start/stop paired on every path
  return found;
}

static void exit_view_after(const unsigned char* player) {
  if (!g_eng.wm_get_object) return;
  const void* after = g_eng.wm_get_object(0);
  const void* follow = follow_camera(player);
  const void* fix = fp_exit_view_fix(s_viewBefore, player, after, follow);
  s_viewExits++;
  if (s_viewBefore == player && !follow) s_viewNoFollow++;
  if (fix) {
    resolve_set_view(player);
    if (s_setView) { s_setView(0, 0, const_cast<void*>(fix)); s_viewFixes++; }
  }
  if (s_viewExits <= 50 || fix)
    np_log("fp: 17 exit view: before %p, the exit chose %p, the camera following Duke %p%s (exits %u, fixes %u, "
           "no follower %u)", s_viewBefore, after, follow, fix ? (s_setView ? "; put back" : "; NOT put back") : "",
           s_viewExits, s_viewFixes, s_viewNoFollow);
}

// The hold has not started yet (the frame path sets it after this tick): the ladder pause or the Interact key's
// scripted state is its reason.
static int exit_holding(int useHold) {
  return g_cfg.firstPerson && fp_exit_holding(s_pause.paused && s_pause.reason == FP_PAUSE_LADDER, useHold);
}
static unsigned s_trackForgets;

void firstperson_leaving_17(const unsigned char* player, int useHold) {
  if (!player || player != s_player) return;
  exit_view_before();
  const void* track = 0;
  float dist = 0.f;
  const int set = rdi(player + 0xB20) != 0;
  const int have = set ? teleport_track_snap(player, &track, &dist) : 0;
  if (!fp_track_forget(exit_holding(useHold), set, have, dist)) return;
  unsigned char* p = const_cast<unsigned char*>(player);
  if (!guarded_writable(p + 0xB20, 0xB60 - 0xB20)) return;
  fp_track_clear_fields(p);   // actor_teleport's own reset of the track fields
  s_trackForgets++;
  s_trailEvent = 1;
  char d[32] = "unmeasured";
  if (have) sprintf_s(d, "%.1f units", dist);
  np_log("fp: track forgotten before the 17 exit (%s) at (%.1f,%.1f,%.1f): the snap onto +0xB20 %p would move him "
         "%s (forgets %u)", useHold ? "interact" : "ladder pause", rdf(player + 0x18C), rdf(player + 0x190),
         rdf(player + 0x194), track, d, s_trackForgets);
}

void firstperson_left_17(const unsigned char* player, int useHold) {
  if (!player || player != s_player) return;
  const unsigned before = s_trackOffs;
  track_guard(player, exit_holding(useHold), "exit tick");
  if (s_trackOffs != before) s_trackOffsInTick++;
  exit_view_after(player);
}

// The trail (Debug log): per-frame records; a ladder, state, track or guard event dumps the FP_TRAIL_N frames before
// it and logs the FP_TRAIL_N after. Position jumps are logged while the switch is on, Debug log or not.
static int s_trLadder = -1, s_trState = -1, s_trPaused = -1, s_trTrackOn = -1;
static unsigned s_trTrack, s_posJumps, s_trailDumps;
static Vec3 s_trPos;
static const unsigned char* s_trPlayer;

// Cumulative: guard writes (frame or exit tick), clears in 17, restores at switch-off, measured snaps left to the
// engine (short), and x/z jumps seen; offs == clears + restores + (one still held) + (dropped with a new player).
static void log_track_counts(const char* what) {
  np_log("fp: %s track guard offs=%u inTick=%u clears=%u restores=%u near=%u unwritable=%u held=%d limboEnds=%u "
         "forgets=%u airLands=%u | jumps=%u trailDumps=%u | exit views=%u fixes=%u noFollower=%u", what, s_trackOffs,
         s_trackOffsInTick, s_trackClears, s_trackRestores, s_trackNear, s_trackUnwritable, s_trackOffPlayer ? 1 : 0,
         s_limboEnds, s_trackForgets, s_airLands, s_posJumps, s_trailDumps, s_viewExits, s_viewFixes, s_viewNoFollow);
}

static void trail_line(char tag, int k, const FpTrailRec& r) {
  np_log("fp: trail %c%02d (%.1f,%.1f,%.1f) state %d PS %d +0xB1C %d +0xB20 %08x", tag, k, r.x, r.y, r.z, r.state, r.ps,
         r.trackOn, r.track);
}

static void trail_frame(const unsigned char* player) {
  if (!player || !g_cfg.firstPerson) { s_trPlayer = 0; return; }
  FpTrailRec r;
  r.x = rdf(player + 0x18C); r.y = rdf(player + 0x190); r.z = rdf(player + 0x194);
  r.state = rdi(player + kOfsState); r.ps = rdi(player + kOfsPs); r.trackOn = player[kOfsTrackOn];
  r.track = (unsigned)rdi(player + 0xB20);
  const Vec3 pos = { r.x, r.y, r.z };
  const int fresh = player != s_trPlayer;
  int event = s_trailEvent;
  s_trailEvent = 0;
  if (!fresh && fp_pos_jump(s_trPos, pos)) {
    s_posJumps++;
    event = 1;
    if (s_posJumps <= 100)
      np_log("fp: Duke moved %.1f units in one frame: (%.1f,%.1f,%.1f) -> (%.1f,%.1f,%.1f), state %d -> %d, +0xB1C %d, "
             "+0xB20 %08x -> %08x (jumps %u)", sqrtf((pos.x - s_trPos.x) * (pos.x - s_trPos.x) +
             (pos.z - s_trPos.z) * (pos.z - s_trPos.z)), s_trPos.x, s_trPos.y, s_trPos.z, pos.x, pos.y, pos.z,
             s_trState, r.state, r.trackOn, s_trTrack, r.track, s_posJumps);
  }
  const int ladder = entities_snapshot().atLadder ? 1 : 0, lstate = fp_ladder_state(r.state) ? 1 : 0;
  const int paused = s_pause.paused ? s_pause.reason : 0;
  if (!fresh && (ladder != s_trLadder || lstate != (fp_ladder_state(s_trState) ? 1 : 0) ||
                 paused != s_trPaused || r.trackOn != s_trTrackOn || r.track != s_trTrack))
    event = 1;
  s_trPlayer = player; s_trPos = pos; s_trLadder = ladder; s_trState = r.state; s_trPaused = paused;
  s_trTrackOn = r.trackOn; s_trTrack = r.track;
  if (!g_cfg.timing) return;   // the Debug log switch
  if (fresh) { s_trail.count = 0; s_trail.after = 0; }
  if (event) {
    s_trailDumps++;
    np_log("fp: trail #%u: ladder volume %d, ladder state %d, pause %d", s_trailDumps, ladder, lstate, paused);
    for (int i = 0; i < s_trail.count; ++i) trail_line('-', s_trail.count - i, fp_trail_at(&s_trail, i));
    s_trail.count = 0;
    s_trail.after = FP_TRAIL_N;
  }
  if (s_trail.after > 0) {   // logged live, so not kept for a later dump
    trail_line('+', FP_TRAIL_N - s_trail.after, r);
    s_trail.after--;
    return;
  }
  fp_trail_push(&s_trail, r);
}

void firstperson_frame(const unsigned char* player) {
  frame_core(player);
  hold_frame(player);
  if (player) track_guard(player, s_holdingNow, "frame");
  trail_frame(player);
}

static void frame_core(const unsigned char* player) {
  s_havePoint = 0;
  const int haveView = s_haveView;
  s_haveView = 0;   // the next push sets it again
  const int switchOn = g_cfg.firstPerson ? 1 : 0;
  pause_frame(player, switchOn);
  const int want = switchOn && !s_pause.paused ? 1 : 0;
  const int wantRose = want && !s_wantPrev;   // tracked before any early return
  s_wantPrev = want;
  if (switchOn != s_wantLast) {
    if (s_wantLast >= 0) np_log("fp: switch %s", switchOn ? "on" : "off");
    s_wantLast = switchOn;
    s_refuseLogged = 0;
    s_quitRefuseLogged = 0;
    s_rpgCheck = 0;
    s_applyFailed = 0;   // a refused write is tried again only after the switch moves
  }
  if (!want && !s_st.applied && !s_st.repick) { s_active = 0; note_player(player); return; }   // nothing read
  if (!resolve()) {
    s_active = 0;
    if (want && !s_unavailLogged++) np_log("fp: g_fps, g_rpg or cvar_t::set unresolved; First person unavailable");
    return;
  }
  // g_rpg is read every frame while ours holds, so a console `g_rpg` drops ours at once, and on a
  // switch-off; while a refusal holds it is read again once a second; a resume applies at once (fp_check_now).
  const int waiting = want && !s_st.applied;
  const int check = fp_check_now(s_st.applied, waiting, s_applyFailed, wantRose, &s_rpgCheck);
  const int quitHooked = canvas_quit_installed();
  const int act = check ? fp_step(s_st, want, (s_st.applied && !want) ? 0 : rpg_on(), quitHooked) : FP_NONE;
  if (act == FP_APPLY) {
    if (set_fps("1")) { fp_done(&s_st, FP_APPLY, 0); s_applyFrames = 0; s_notTakenLogged = 0; }
    else s_applyFailed = 1;
  } else if (act == FP_RESTORE) {
    set_fps("0");   // a refusal is logged; the re-pick still waits for the engine's own exit
    // A pause re-picks nothing: no player to move, or a ladder whose own state places him.
    fp_done(&s_st, FP_RESTORE, s_pause.paused ? 0 : player);
    s_exits++;
    log_ranges("off:");
  } else if (act == FP_REFUSE && !s_refuseLogged++) {
    np_log("fp: REFUSED: g_rpg is on, and g_fps is never set with it (user decision 2026-10-02)");
  } else if (act == FP_REFUSE_QUIT && !s_quitRefuseLogged++) {
    np_log("fp: REFUSED: the config-write detour is not installed, so a quit could archive g_fps 1 in config.cfg");
  }

  const int state = player ? rdi(player + kOfsState) : -1;
  const int r = fp_repick(s_st, player, state);
  if (r == FP_REPICK_NOW) {
    if (teleport_repick_here(player, "fp")) s_repicks++;
    fp_done(&s_st, FP_REPICKED, 0);
  } else if (r == FP_REPICK_DROP) {
    s_drops++;
    np_log("fp: exit re-pick dropped: %s", player ? "a new player (level change)" : "no player");
    fp_done(&s_st, FP_REPICKED, 0);
  }

  const int active = player && fp_active(s_st.applied, state);
  note_player(player);
  if (active && !s_active) enter_log(player);
  if (!active && s_active) np_log("fp: left state 17 (now %d, g_fps %s)", state, s_st.applied ? "1" : "0");
  s_active = active;
  if (s_st.applied && player && !active && ++s_applyFrames == 120 && !s_notTakenLogged++)
    np_log("fp: g_fps is 1 but Duke has been in state %d for 120 frames", state);
  if (player) watch_states(player);
  if (!active) { s_poseValid = 0; return; }
  s_frames++;
  look(player);
  centre_point(player, haveView);
  const ULONGLONG t = GetTickCount64();
  if (t >= s_nextLog) { s_nextLog = t + 5000; log_ranges("on:"); }
}

void firstperson_draw(int W, int H) {
  if (!(s_active || s_hold) || input_panel_open() || input_freecam_on() || input_console_open()) return;
  canvas_begin(W, H);
  mouseaim_draw_reticle(W / 2, H / 2, 0);
  canvas_end();
}

void firstperson_restore_for_quit(void) {
  if (fp_quit(s_st) != FP_RESTORE || !s_fps) return;
  const bool ok = g_p3.cvar_set(s_fps, "0");
  fp_done(&s_st, FP_RESTORE, 0);
  np_log("quit: g_fps back to \"0\" for config.cfg: %s", ok ? "ok" : "FAILED");
}
