// Silent aim: the weap_get_fire_dir detour, its arming, the once-a-frame target selection and the calibration marker.
// aim.h owns every rule; this file owns the engine calls. Main thread only: the detour runs inside the game tick and
// the rest on the frame path of the same cooperative loop (docs/re/render.md section 1), so no locking.
#include "aim.h"
#include "bossrule.h"
#include "overkill.h"   // doomed targets: the pick and the Triggerbot skip them
#include "weapimm.h"    // enemies the held weapon cannot hurt
#include "engine.h"
#include "entities.h"
#include "weapons.h"
#include "instruments.h"   // kPlayerSpan
#include "hook.h"
#include "offscreen.h"   // offscreen_active: the on-screen filters follow the clip bypass
#include "view.h"
#include "canvas.h"
#include "font.h"
#include "esp_layout.h"    // esp_draw_label, esp_label_x, ESP_LABEL_GAP
#include "config.h"
#include "log.h"
#include "ui/common.h"     // clamp_double_to_int
#include "ui/wallclock.h"
#include "input.h"         // input_panel_open, input_freecam_on, input_console_open: the triggerbot's gate
#include "mouseaim.h"      // ma_fire_source, mouseaim_point: the detour also serves mouse aim
#include "firstperson.h"   // fp_fire_source, firstperson_point: and first person's view centre
#include <windows.h>
#include <stdio.h>
#include <string.h>

// weap_get_fire_dir and the player getters: thiscall (vec3* out), RET 4 (aim.md section 1).
typedef void (__thiscall* VecOutFn)(void* self, float* out);
// vt+0x190 get_world_velocity: thiscall (vec3* out), RET 4, EAX = out (aim.md section 3).
typedef float* (__thiscall* VelFn)(void* self, float* out);

static const unsigned kSlotFireDir = 0x210 / 4;       // every actor_weap_* vtable (aim.md section 1)
static const unsigned kSlotPlayerDir = 0x21C / 4;     // player_get_fire_dir 0x10078930
static const unsigned kSlotPlayerOrigin = 0x220 / 4;  // player_get_fire_origin 0x10078960
static const unsigned kSlotVelocity = 0x190 / 4;      // aim.md section 3
static const unsigned kOfsOwner = 0x60;       // weapon -> owner, the original's own read (0x100508b1)
static const unsigned kOfsWorld = 0xE0;       // object -> world (teleport.cpp, proj_bullet_hitscan_ray)
static const unsigned kOfsWorldMatrix = 0xF4; // row-vector world matrix (actor_get_bone_world_pos 0x10090720)
static const unsigned kOfsStateIdx = 0xB7C;   // player state index, then the state table at +0xB80
static const unsigned kOfsStates = 0xB80;     //   (player_get_fire_dir 0x1007893b-0x10078945)
static const unsigned kStateSlotEnd = 0x2C + 4;   // the state's get_fire_dir +0x28 / get_fire_origin +0x2C
// The player's skmod handle: both state getters pass it to skmod unchecked (0x10088408, 0x10088530).
static const unsigned kOfsSkel = 0xAE4;
static const char kHeadBone[] = "Bip01 Head";

// vt+0x190 entries this code will call: monster_get_world_velocity 0x10030520, the actor default
// 0x1008b420 and porkchopper's 0x1000fe70 (docs/re/aim.md section 3). Anything else gets no lead.
static const unsigned char kVelMonster[9] = { 0x83, 0xEC, 0x18, 0x56, 0x8B, 0xF1, 0x8B, 0x4E, 0x60 };
static const unsigned char kVelActor[9] = { 0x83, 0xEC, 0x18, 0x8D, 0x81, 0xC0, 0x01, 0x00, 0x00 };
static const unsigned char kVelPork[9] = { 0x8B, 0x44, 0x24, 0x04, 0x81, 0xC1, 0x2C, 0x14, 0x00 };

// Detour outcomes, one per call: calls == the sum of s_why. R_MOUSE: sent to the cursor's point; R_NOCURSOR:
// mouse aim on, no lock and no point this frame (stock).
// R_VIEW: sent to first person's view-centre point; R_NOVIEW: first person active, no lock and no view this frame.
enum { R_REWRITTEN = 0, R_OFF, R_NOTMINE, R_GATE, R_NOLOCK, R_NOORIGIN, R_DEGENERATE, R_MOUSE, R_NOCURSOR, R_VIEW,
       R_NOVIEW, R_COUNT };

struct Lock { int have; const void* ent; Vec3 raw, led; float dist; const char* name; };
static Lock s_lock;

static VecOutFn s_tramp;
static void* s_target;
static int s_refused;          // a refused fingerprint or install is not retried every frame
static unsigned s_calls, s_why[R_COUNT];

// The player getters, verified once per player vtable.
static const void* s_playerVt;
static VecOutFn s_dirFn, s_originFn;
static int s_playerFnsBad;

static AimBoneCache s_bones;
static unsigned s_seen, s_rej[AIM_REJ_COUNT], s_cands, s_locks, s_lockChanges, s_untried;
static unsigned s_headFallback, s_headFar, s_velRefused, s_noEye, s_noView, s_leads, s_noPhys;
static int s_farLogged;
// Every skmod_get_bone_transform call and their total time: a lazy model load shows up here.
static unsigned s_boneCalls;
static double s_boneUs;
static LARGE_INTEGER s_qpf;
static const void* s_lastLockEnt;
static unsigned s_loggedSum = 0xFFFFFFFFu;
static ULONGLONG s_nextLog;

// Weapon vtables whose first redirect has been logged, and the calls' first vtables.
enum { kSeenMax = 16 };
static const void* s_redirVt[kSeenMax];
static int s_nredir;

// The snapshot's ents, then its boss parts at ENT_SNAPSHOT_MAX + j (cand_ent).
static AimCand s_cand[ENT_SNAPSHOT_MAX + ENT_PARTS_MAX];
static unsigned s_burstSticky;   // cumulative: frames the rifle's burst target was put first (overkill.h)
// Cumulative: candidates skipped because their own damage gate refuses now, by BOSS_IMM_* reason; and the
// part points (the part's centre bone), with the box-centre fallbacks and the far-from-box refusals.
static unsigned s_immune[BOSS_IMM_COUNT], s_partPoints, s_partFallback, s_partFar;
// Cumulative, per boss entity or part (BOSS_K_*): seen by silent aim, skipped as immune, locked, and taken
// as the Triggerbot's crosshair target. seen == immune + the rest of that boss's buckets.
static unsigned s_bossSeen[BOSS_K_COUNT], s_bossImm[BOSS_K_COUNT], s_bossLocks[BOSS_K_COUNT],
                s_bossCross[BOSS_K_COUNT];
static int boss_row(unsigned char k) { return k < BOSS_K_COUNT ? k : BOSS_K_NONE; }

static const EntSnap& cand_ent(const EntSnapshot& snap, int idx) {
  return idx >= ENT_SNAPSHOT_MAX ? snap.parts[idx - ENT_SNAPSHOT_MAX] : snap.ents[idx];
}

int aim_armed(void) { return s_target != 0; }
int aim_refused(void) { return s_refused; }
unsigned aim_calls(void) { return s_calls; }
unsigned aim_rewrites(void) { return s_why[R_REWRITTEN]; }
unsigned aim_mouse_rewrites(void) { return s_why[R_MOUSE]; }
unsigned aim_locks(void) { return s_locks; }
unsigned aim_rejected(int reason) { return reason >= 0 && reason < AIM_REJ_COUNT ? s_rej[reason] : 0u; }
// Only with the detour armed: a refused hook must not leave the rifle's lever on with no redirect.
int aim_rifle_nospread(void) { return g_cfg.silentOn && s_target && s_lock.have; }
void aim_lock_clear(void) { s_lock.have = 0; }

static Vec3 rdv(const unsigned char* p) { Vec3 v; memcpy(&v, p, sizeof v); return v; }

// The first redirect per weapon class logs the eye-to-weapon distance: the muzzle (vt+0x214) has a
// per-class ABI, so the weapon's own +0x18C stands in for it (a loose parallax measure).
static void log_first_redirect(const unsigned char* w, Vec3 o, Vec3 orig, Vec3 nd) {
  const void* vt;
  memcpy(&vt, w, sizeof vt);
  for (int i = 0; i < s_nredir; ++i) if (s_redirVt[i] == vt) return;
  if (s_nredir >= kSeenMax) return;
  s_redirVt[s_nredir++] = vt;
  float d = -1.f;
  if (guarded_readable(w + 0x18C, sizeof(Vec3))) d = aim_len(aim_sub(rdv(w + 0x18C), o));
  np_log("aim: first redirect for weapon vt %p: eye (%.1f,%.1f,%.1f), eye-to-weapon %.1f, dir (%.3f,%.3f,%.3f) -> "
         "(%.3f,%.3f,%.3f), target %s", vt, o.x, o.y, o.z, d, orig.x, orig.y, orig.z, nd.x, nd.y, nd.z,
         s_lock.name ? s_lock.name : "?");
}

// After the original: `out` is rewritten only for the local player's own weapon, toward silent aim's lock or else
// mouse aim's point (ma_fire_source). No max-redirect refusal: the new direction is used whatever its angle, so shots
// may fly toward a target behind Duke. Monsters that call vt+0x210 pass straight through (R_NOTMINE).
static int redirect(const unsigned char* w, float* out) {
  const int mouseOn = mouseaim_on();
  const int fpOn = firstperson_active();
  if (!g_cfg.silentOn && !mouseOn && !fpOn) return R_OFF;
  const unsigned char* owner;
  memcpy(&owner, w + kOfsOwner, sizeof owner);
  const void* local = g_eng.wm_get_input ? g_eng.wm_get_input(0) : 0;
  if (!owner || owner != local || !out) return R_NOTMINE;
  // The original's own gate (0x100508b8-0x100508c2): when it fails, out was left untouched.
  if (!(owner[0x48] & 2) || !(owner[0x4C] & 0x10)) return R_GATE;
  Vec3 mp, vp;
  const int haveView = fpOn && firstperson_point(&vp);
  const int src = fp_fire_source(g_cfg.silentOn && s_lock.have, fpOn, haveView, mouseOn,
                                 mouseOn && mouseaim_point(&mp));
  if (src == MA_SRC_STOCK) return fpOn ? R_NOVIEW : (mouseOn ? R_NOCURSOR : R_NOLOCK);
  if (src == FP_SRC_VIEW) mp = vp;
  const void* ovt;
  memcpy(&ovt, owner, sizeof ovt);
  const void* skel;
  memcpy(&skel, owner + kOfsSkel, sizeof skel);
  if (!s_originFn || ovt != s_playerVt || !skel) return R_NOORIGIN;
  float of[3];
  s_originFn((void*)owner, of);   // what weap_get_fire_origin returns for the three bullet weapons
  const Vec3 o = { of[0], of[1], of[2] };
  const Vec3 orig = { out[0], out[1], out[2] };
  Vec3 nd;
  if (!aim_redirect(orig, o, src == MA_SRC_SILENT ? s_lock.led : mp, &nd)) return R_DEGENERATE;
  out[0] = nd.x; out[1] = nd.y; out[2] = nd.z;
  if (src == MA_SRC_MOUSE) return R_MOUSE;
  if (src == FP_SRC_VIEW) return R_VIEW;
  log_first_redirect(w, o, orig, nd);
  return R_REWRITTEN;
}

// thiscall seen as fastcall: ECX = this, EDX unused, one stack arg popped by RET 4 either way.
static void __fastcall fire_dir_detour(void* self, void* /*edx*/, float* out) {
  s_tramp(self, out);
  s_calls++;
  s_why[redirect((const unsigned char*)self, out)]++;
}

// Armed from the held weapon's vt+0x210 once Silent aim or Mouse aim is first switched on, never
// before: with both off this build runs the game's code unchanged.
static void arm(void) {
  if (s_target || s_refused) return;
  const void* vt;
  int kind;
  const unsigned char* w = weapons_held(&vt, &kind);
  if (!w || !vt) return;
  const void* const* slot = (const void* const*)vt + kSlotFireDir;
  if (!entities_in_duke(slot) || !entities_in_duke((const unsigned char*)(slot + 1) - 1)) return;
  void* const target = (void*)*slot;
  const unsigned char* base = 0;
  unsigned size = 0;
  if (!entities_duke_image(&base, &size)) return;
  if (!entities_in_duke(target) || !guarded_readable(target, AIM_FIRE_DIR_FP_LEN)) {
    s_refused = 1;
    np_log("aim: %s vt+0x210 -> %p is outside duke_base or unreadable; the fire-dir detour stays unarmed", weap_name(kind),
           target);
    return;
  }
  unsigned char live[AIM_FIRE_DIR_FP_LEN];
  memcpy(live, target, sizeof live);
  if (!aim_fp_is_fire_dir(live)) {
    s_refused = 1;
    char hex[AIM_FIRE_DIR_FP_LEN * 3 + 1];
    for (int i = 0; i < AIM_FIRE_DIR_FP_LEN; ++i) sprintf_s(hex + 3 * i, sizeof(hex) - 3 * i, "%02X ", live[i]);
    np_log("aim: %s vt+0x210 -> %p fingerprint refused: %s", weap_name(kind), target, hex);
    return;
  }
  if (!hook_install_at(target, live, AIM_FIRE_DIR_FP_LEN, AIM_FIRE_DIR_TRAMP_LEN, (void*)fire_dir_detour,
                       (void**)&s_tramp)) {
    s_refused = 1;
    np_log("aim: install at %p refused; the fire-dir detour stays unarmed", target);
    return;
  }
  s_target = target;
  np_log("aim: armed at %p (duke_base+%X) from %s vt %p, fingerprint matched", target,
         (unsigned)((const unsigned char*)target - base), weap_name(kind), vt);
}

// Verifies the player's vt+0x21C/+0x220 once per player vtable (the fingerprints in aim.h).
static bool player_fns(const unsigned char* player) {
  const void* vt;
  memcpy(&vt, player, sizeof vt);
  if (vt == s_playerVt) return s_dirFn != 0;
  s_playerVt = vt;
  s_dirFn = 0; s_originFn = 0;
  const void* const* v = (const void* const*)vt;
  if (!entities_in_duke(v + kSlotPlayerDir) || !entities_in_duke(v + kSlotPlayerOrigin)) return false;
  const unsigned char* d = (const unsigned char*)v[kSlotPlayerDir];
  const unsigned char* o = (const unsigned char*)v[kSlotPlayerOrigin];
  if (!entities_in_duke(d) || !entities_in_duke(o) || !guarded_readable(d, AIM_PLAYER_FP_LEN) ||
      !guarded_readable(o, AIM_PLAYER_FP_LEN) || !aim_fp_is_player_dir(d) || !aim_fp_is_player_origin(o)) {
    s_playerFnsBad++;
    np_log("aim: player vt %p: +0x21C %p / +0x220 %p are not player_get_fire_dir/_origin; no targeting", vt, d, o);
    return false;
  }
  s_dirFn = (VecOutFn)d;
  s_originFn = (VecOutFn)o;
  np_log("aim: player vt %p getters verified: fire dir %p, fire origin %p", vt, d, o);
  return true;
}

// The unredirected fire direction and the eye, as weap_get_fire_dir/_origin would return them.
// Both getters index the player's state table; the state object is checked first (one guarded read).
static bool eye_and_dir(const unsigned char* player, Vec3* eye, Vec3* dir) {
  if (!player_fns(player)) return false;
  const void* skel;
  memcpy(&skel, player + kOfsSkel, sizeof skel);   // inside kPlayerSpan
  if (!skel) return false;
  int idx;
  memcpy(&idx, player + kOfsStateIdx, sizeof idx);
  if (idx < 0 || kOfsStates + 4u * (unsigned)idx + 4u > kPlayerSpan) return false;
  const unsigned char* st;
  memcpy(&st, player + kOfsStates + 4u * (unsigned)idx, sizeof st);
  if (!st || !guarded_readable(st, 4)) return false;
  const void* svt;
  memcpy(&svt, st, sizeof svt);
  if (!entities_in_duke(svt) || !entities_in_duke((const unsigned char*)svt + kStateSlotEnd - 1)) return false;
  float o[3], d[3];
  s_originFn((void*)player, o);
  s_dirFn((void*)player, d);
  eye->x = o[0]; eye->y = o[1]; eye->z = o[2];
  dir->x = d[0]; dir->y = d[1]; dir->z = d[2];
  return aim_len(*dir) > 0.f;
}

// Bone idx of e's model through its world matrix, as actor_get_bone_world_pos 0x10090720 does; false when
// skmod refuses or the point lies far outside the box (a wrong matrix reading, not the bone).
static bool bone_world(const EntSnap& e, int idx, Vec3* out, unsigned* farCount) {
  float m[16];
  LARGE_INTEGER t0, t1;
  if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
  QueryPerformanceCounter(&t0);
  const bool ok = g_sk.skmod_get_bone_transform(e.skel, idx, m);
  QueryPerformanceCounter(&t1);
  s_boneCalls++;
  s_boneUs += (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)s_qpf.QuadPart;
  if (!ok) return false;
  float world[16];
  memcpy(world, (const unsigned char*)e.ent + kOfsWorldMatrix, sizeof world);   // inside the walk's validated span
  const Vec3 bone = { m[12], m[13], m[14] };
  const Vec3 p = aim_row_xform(bone, world);
  const float off = aim_len(aim_sub(p, e.hb.c));
  if (!(off <= 2.f * aim_len(e.hb.half) + 64.f)) {
    (*farCount)++;
    if (!s_farLogged) {
      s_farLogged = 1;
      np_log("aim: bone %d of %s at (%.1f,%.1f,%.1f) is %.1f from its box centre (%.1f,%.1f,%.1f); box centre used",
             idx, e.name ? e.name : "?", p.x, p.y, p.z, off, e.hb.c.x, e.hb.c.y, e.hb.c.z);
    }
    return false;
  }
  *out = p;
  return true;
}

// A boss part: its centre bone (+0xB2C, "bone_child_*_center", set by the boss init 0x10006e30). The part's
// box is the whole helicopter's size (the same "dimensions" for all four), so its centre is no aim.
static Vec3 part_point(const EntSnap& e) {
  Vec3 p;
  if (e.skel && e.bone >= 0 && engine_resolve_skmod_bones() && bone_world(e, e.bone, &p, &s_partFar)) {
    s_partPoints++;
    return p;
  }
  s_partFallback++;
  return e.hb.c;
}

// Head: the "Bip01 Head" bone through the world matrix, as actor_get_bone_world_pos 0x10090720
// does. Anything unusable keeps the body point (the box centre, hitboxes.md section 0).
static Vec3 head_point(const EntSnap& e) {
  if (!e.skel || !engine_resolve_skmod_bones()) { s_headFallback++; return e.hb.c; }
  int idx;
  const int r = aim_bone_lookup(&s_bones, e.vt, &idx);
  if (r == AIM_BONE_ASK) {
    idx = g_sk.skmod_get_bone_index(e.skel, kHeadBone);
    aim_bone_put(&s_bones, e.vt, idx);
    np_log("aim: head bone of %s (vt %p) = %d%s", e.name ? e.name : "?", e.vt, idx, idx < 0 ? ", body point used" : "");
  }
  if (idx < 0) { s_headFallback++; return e.hb.c; }
  Vec3 p;
  const unsigned farBefore = s_headFar;
  if (!bone_world(e, idx, &p, &s_headFar)) { if (s_headFar == farBefore) s_headFallback++; return e.hb.c; }
  return p;
}

// vt+0x190 on the locked target only, and only through an entry this code has read.
static bool velocity(const EntSnap& e, Vec3* v) {
  const void* const* slot = (const void* const*)e.vt + kSlotVelocity;
  if (!entities_in_duke(slot)) return false;
  const unsigned char* fn = (const unsigned char*)*slot;
  if (!entities_in_duke(fn) || !guarded_readable(fn, sizeof kVelMonster) ||
      (memcmp(fn, kVelMonster, sizeof kVelMonster) && memcmp(fn, kVelActor, sizeof kVelActor) &&
       memcmp(fn, kVelPork, sizeof kVelPork))) {
    s_velRefused++;
    return false;
  }
  float out[3] = { 0.f, 0.f, 0.f };
  ((VelFn)fn)(e.ent, out);
  v->x = out[0]; v->y = out[1]; v->z = out[2];
  return true;
}

// --- The bullet's own ray (aim.md section 4b) -----------------------------------------------
// Sized past the ctor's 0x24 bytes, as teleport.cpp's.
struct Matcher { __declspec(align(8)) unsigned char b[0x40]; };
static const int kHistoryMax = 32;       // a corrupt list must not spin the frame
static const unsigned kNodeSpan = 0x8 + 0x20;   // node +4 next, +8 record: object +0, hit point +0x14..+0x20
static const float kPastTarget = 32.f;   // the ray runs this far past the aim point, into the target's box
// The damage path's slots (proj_bullet_damage_hit 0x1003ad90): object vt+0x170, actor vt+0x194 -> +0x198.
static const unsigned kSlotClassname = 1, kSlotObjDamage = 0x170 / 4, kSlotDamageInfo = 0x194 / 4,
                      kSlotActorDamage = 0x198 / 4;
static const unsigned kOfsDying = 0xB94, kOfsAdvArmed = 0xBE4, kOfsBreakArmed = 0xC44;
typedef const char* (__thiscall* ClassnameFn)(const void* self);

struct Hit { const void* obj; float dist; Vec3 at; };
// Per vtable: what its damage slots are, read once (a vtable outside duke_base reads nothing). blocked: cumulative, how
// often this class was the blocker in front of a refused target. noDamage: positively read as actor_damage_from_info
// forwarding to a bare RET 0x14 (duke!0x10001c40: Robo Duke's body, the crane); an unread slot is not noDamage.
struct VtFacts { const void* vt; int actorDamages, noDamage, handler, objClass; const char* name; unsigned blocked; };
enum { kVtFactsMax = 64 };
static VtFacts s_vtf[kVtFactsMax];
static int s_nvtf, s_vtfFullLogged;
static unsigned s_vtfFull, s_rayThrough, s_rayBlocked, s_rayMissed, s_rayUnread, s_blockedSum, s_blockedLogged;
// Cumulative: bullet rays cast and their total time; world-only prefilter rays and the candidates they hid.
static unsigned s_bulletRays, s_visRays, s_visHidden, s_replayRays;
static double s_bulletUs, s_replayUs;   // the shot replay (measurement) is timed apart from the bot's rays
static ReadCache s_rc;   // one per frame, zeroed by aim_frame: history nodes and obstacle fields

// Engine type bit 0x100 = world_e; the denies are the camera's own (freecam-teleport.md section 2).
static const unsigned kDenyUser[4] = { 0x8000, 0x10000, 0x20000, 0x400 };
static void world_matcher(Matcher* m) {
  g_p3.tm_ctor(m->b);
  g_p3.tm_reset_rules(m->b, false);
  g_p3.tm_accept_superset(m->b, 0x100, 0, true);
  for (int i = 0; i < 4; ++i) g_p3.tm_deny_superset(m->b, 0x100, kDenyUser[i], false);
}
// Everything but Duke and his gun: the bullet's own filter (aim.md section 4b).
static void touch_matcher(Matcher* m, const unsigned char* player) {
  g_p3.tm_ctor(m->b);
  g_p3.tm_reset_rules(m->b, false);
  g_p3.tm_accept_superset(m->b, 0, 0, true);
  g_p3.tm_deny_ptr(m->b, player);
  const void* wvt;
  int kind;
  const unsigned char* held = weapons_held(&wvt, &kind);
  if (held) g_p3.tm_deny_ptr(m->b, held);
}

// block = world-only, touch = everything else but Duke and his gun. phys_ray_test's gather (prism!0x4127c0) flags a
// candidate blocking when the block matcher takes it, and phys_ray_vs_objects (0x40f1b0) records every touch-only hit
// but only the nearest blocking one, so this sees every object and the wall. replica: block = touch = bullet filter.
static int bullet_ray(const unsigned char* player, void* world, Vec3 from, Vec3 to, Hit* out, bool replica = false) {
  if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  Matcher blk, tch;
  if (replica) touch_matcher(&blk, player); else world_matcher(&blk);
  touch_matcher(&tch, player);
  const void* first = g_eng.phys_ray_test(world, &from.x, &to.x, blk.b, tch.b);
  int n = 0;
  if (first) {
    const unsigned char* head = (const unsigned char*)g_eng.phys_collide_history();
    const unsigned char* node = 0;
    if (head && guarded_readable_cached(&s_rc, head, 8)) memcpy(&node, head + 4, sizeof node);
    for (int i = 0; node && i < kHistoryMax; ++i) {
      if (!guarded_readable_cached(&s_rc, node, kNodeSpan)) break;
      const void* obj;
      memcpy(&obj, node + 8, sizeof obj);
      if (!obj) break;   // the engine's own walk ends at a NULL object (duke!0x1003ac89)
      out[n].obj = obj;
      out[n].at = rdv(node + 8 + 0x14);
      out[n].dist = aim_len(aim_sub(out[n].at, from));
      n++;
      memcpy(&node, node + 4, sizeof node);
    }
  }
  g_p3.tm_dtor(tch.b);
  g_p3.tm_dtor(blk.b);
  QueryPerformanceCounter(&t1);
  const double us = (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)s_qpf.QuadPart;
  if (replica) { s_replayRays++; s_replayUs += us; } else { s_bulletRays++; s_bulletUs += us; }
  return n;
}

// A slot's code with one `jmp [import]` followed (the default vt+0x170 is such a thunk to
// p3shared's object_e::damage). NULL when unreadable.
static const unsigned char* code_at(const unsigned char* f, unsigned n) {
  if (!f || !guarded_readable(f, 6)) return 0;
  if (f[0] == 0xFF && f[1] == 0x25) {
    const unsigned char* const* slot;
    memcpy(&slot, f + 2, sizeof slot);
    if (!guarded_readable(slot, sizeof *slot)) return 0;
    f = *slot;
  }
  return f && guarded_readable(f, n) ? f : 0;
}

static void read_vt_facts(const unsigned char* obj, const void* vt, VtFacts* o) {
  o->vt = vt; o->actorDamages = 0; o->noDamage = 0; o->handler = AIM_H_UNKNOWN; o->objClass = AIM_OBJ_OTHER;
  o->name = "?";
  o->blocked = 0;
  const void* const* v = (const void* const*)vt;
  if (!entities_in_duke(v) || !entities_in_duke(v + kSlotActorDamage + 1) ) return;
  if (entities_in_duke(v[kSlotClassname])) {
    const char* cls = ((ClassnameFn)v[kSlotClassname])(obj);
    if (entities_in_duke(cls) && guarded_readable(cls, 1)) {
      o->name = cls;
      if (!strcmp(cls, "breakable")) o->objClass = AIM_OBJ_BREAKABLE;
      else if (!strcmp(cls, "breakable_adv")) o->objClass = AIM_OBJ_BREAKABLE_ADV;
      else if (!strcmp(cls, "blocker")) o->objClass = AIM_OBJ_BLOCKER;   // ctor duke!0x100b9930
    }
  }
  const unsigned char* dfi = (const unsigned char*)v[kSlotDamageInfo];
  const unsigned char* act = (const unsigned char*)v[kSlotActorDamage];
  if (entities_in_duke(dfi) && guarded_readable(dfi, sizeof kAimDamageFromInfo) && aim_fp_is_damage_from_info(dfi)) {
    const unsigned char* a = code_at(act, 3);
    o->actorDamages = a && !aim_is_ret(a, 0x14);
    o->noDamage = a && aim_is_ret(a, 0x14);
  }
  const unsigned char* d = code_at((const unsigned char*)v[kSlotObjDamage], 3);
  if (!d) return;
  if (aim_is_ret(d, 8)) { o->handler = AIM_H_NOOP; return; }
  if (!entities_in_duke(d) || !guarded_readable(d, AIM_BREAK_FP_LEN)) return;
  unsigned t = 0;
  if (aim_thunk_target(d, (unsigned)(size_t)d, &t)) {
    d = (const unsigned char*)(size_t)t;
    if (!entities_in_duke(d) || !guarded_readable(d, AIM_BREAK_FP_LEN)) return;
  }
  if (aim_fp_is_breakable_handler(d)) o->handler = AIM_H_BREAKABLE;
}

static const char* kKindName[3] = { "passes", "destructible", "BLOCKS" };

static unsigned rd_u32(const unsigned char* p) {
  unsigned u = 0;
  if (guarded_readable_cached(&s_rc, p, 4)) memcpy(&u, p, 4);
  return u;
}

// Each blocker instance once (the class line below logs only the first of a class): its flags and verdict.
static const void* s_blockersSeen[64];
static int s_nBlockersSeen;
static void log_blocker(const void* obj, const AimObsFacts& f, const unsigned char* b) {
  for (int i = 0; i < s_nBlockersSeen; ++i) if (s_blockersSeen[i] == obj) return;
  if (s_nBlockersSeen < 64) s_blockersSeen[s_nBlockersSeen++] = obj;
  else return;
  const Vec3 p = guarded_readable_cached(&s_rc, (const unsigned char*)obj + 0x18C, 12) ? rdv((const unsigned char*)obj + 0x18C)
                                                                                       : Vec3{ 0.f, 0.f, 0.f };
  np_log("aim: blocker %p at (%.0f,%.0f,%.0f): user 0x%X +0x2E0=%d start_active=%u invisible=%u creatures_only=%u "
         "player_only=%u -> %s", obj, p.x, p.y, p.z, f.user, f.blockerActive, b[0], b[1], b[2], b[3],
         aim_obstacle_kind(f) == AIM_OBS_BLOCK ? "BLOCKS" : "passes");
}

// The entity walk's own verdict for obj (bossrule.h), 0 when it holds none: an immune boss or part.
static int immune_of(const void* obj) {
  const EntSnapshot& snap = entities_snapshot();
  for (int i = 0; i < snap.nimmune; ++i) if (snap.immuneEnt[i] == obj) return snap.immuneWhy[i];
  return 0;
}

// What the bullet would do to obj (aim_obstacle_kind); each cached class is logged once with its verdict.
// *isWorld: the hit cuts the ray as a wall does (aim_obstacle_cuts); *vtf: its cached class, or NULL.
static int classify(const void* objv, int* isWorld, VtFacts** vtf = 0) {
  const unsigned char* obj = (const unsigned char*)objv;
  *isWorld = 0;
  if (!guarded_readable_cached(&s_rc, obj, 0x50)) { s_rayUnread++; return AIM_OBS_BLOCK; }
  AimObsFacts f;
  memset(&f, 0, sizeof f);
  const void* vt;
  memcpy(&vt, obj, sizeof vt);
  memcpy(&f.type, obj + 0x48, 4);
  memcpy(&f.user, obj + 0x4C, 4);
  if (vtf) *vtf = 0;
  int i = 0;
  while (i < s_nvtf && s_vtf[i].vt != vt) ++i;
  VtFacts tmp;
  VtFacts* vf = &tmp;
  bool logIt = false;
  if (i < s_nvtf) vf = &s_vtf[i];
  else {
    read_vt_facts(obj, vt, &tmp);
    if (s_nvtf < kVtFactsMax) { s_vtf[s_nvtf] = tmp; vf = &s_vtf[s_nvtf++]; logIt = true; }
    else {
      s_vtfFull++;
      if (!s_vtfFullLogged) { s_vtfFullLogged = 1; np_log("aim: class cache full (%d); later classes are read each time, unlogged", kVtFactsMax); }
    }
  }
  f.actorDamages = vf->actorDamages; f.handler = vf->handler; f.objClass = vf->objClass;
  if ((f.type & 2) && (f.user & 0x20)) f.dyingMonster = (int)rd_u32(obj + kOfsDying) >= 0x14;
  if (f.objClass == AIM_OBJ_BREAKABLE_ADV) f.advArmed = rd_u32(obj + kOfsAdvArmed) != 0;
  if (f.handler == AIM_H_BREAKABLE) f.breakArmed = rd_u32(obj + kOfsBreakArmed) != 0;
  if (f.type & 2) f.immuneNow = immune_of(objv) != 0;
  if (f.objClass == AIM_OBJ_BLOCKER) {
    unsigned char b[4] = { 0, 0, 0, 0 };   // +0xB08 start_active, +0xB09 invisible, +0xB0A/+0xB0B the two flags
    f.blockerActive = guarded_readable_cached(&s_rc, obj + 0x2E0, 1) && obj[0x2E0] != 0;
    if (guarded_readable_cached(&s_rc, obj + 0xB08, 4)) memcpy(b, obj + 0xB08, 4);
    log_blocker(objv, f, b);
  }
  const int k = aim_obstacle_kind(f);
  *isWorld = aim_obstacle_cuts(f, k);
  if (vtf && vf != &tmp) *vtf = vf;
  if (logIt) {
    const unsigned char* base = 0;
    unsigned size = 0;
    entities_duke_image(&base, &size);
    const unsigned rva = entities_in_duke(vt) ? (unsigned)((const unsigned char*)vt - base) : 0;
    np_log("aim: bullet ray meets %s (vt %p = duke_base+%X): %s -- type 0x%X user 0x%X actorDamages=%d handler=%d "
           "class=%d advArmed=%d breakArmed=%d dying=%d", vf->name, vt, rva, kKindName[k], f.type, f.user,
           f.actorDamages, f.handler, f.objClass, f.advArmed, f.breakArmed, f.dyingMonster);
  }
  return k;
}

// recs[i].target is set by the caller for hit i. Classifies the rest, sorts, cuts at the wall and returns the reached
// hit's index, or -1 with *blockerHit (-1: none) and *blockerWorld. forceBlock[i] (optional): hit i stops the shot
// whatever its class (an enemy the G.L.O.P.P. beam cannot turn soaks it: proj_gray_beam_tick 0x10041940, nearest only).
static int evaluate(const Hit* hits, AimRayRec* recs, int n, int* blockerHit, int* blockerWorld,
                    const unsigned char* forceBlock = 0) {
  VtFacts* vf[kHistoryMax];
  for (int i = 0; i < n; ++i) {
    recs[i].dist = hits[i].dist;
    recs[i].tag = i;
    recs[i].world = 0;
    vf[i] = 0;
    if (recs[i].target) recs[i].kind = AIM_OBS_PASS;
    else if (forceBlock && forceBlock[i]) recs[i].kind = AIM_OBS_BLOCK;
    else recs[i].kind = classify(hits[i].obj, &recs[i].world, &vf[i]);
  }
  const int m = aim_ray_sort_cut(recs, n);
  int blk;
  const int t = aim_ray_first_target(recs, m, &blk);
  *blockerHit = blk >= 0 ? recs[blk].tag : -1;
  *blockerWorld = blk >= 0 ? recs[blk].world : 0;
  // Which class refused a target that lay behind it (a ray that met no target names no blocker).
  if (t < 0 && blk >= 0 && vf[recs[blk].tag]) { vf[recs[blk].tag]->blocked++; s_blockedSum++; }
  if (t < 0) return -1;
  int destructibles = 0;
  for (int i = 0; i < t; ++i) destructibles += recs[i].kind == AIM_OBS_DESTRUCTIBLE;
  if (destructibles) s_rayThrough++;
  return recs[t].tag;
}

static const char* class_of(const void* obj) {
  const void* vt = 0;
  if (!guarded_readable_cached(&s_rc, obj, 4)) return "?";
  memcpy(&vt, obj, sizeof vt);
  for (int i = 0; i < s_nvtf; ++i) if (s_vtf[i].vt == vt) return s_vtf[i].name;
  return "?";
}

// Measurement: replay Duke's last real shot with the bullet's own filter (block = touch = accept-all minus Duke and his
// gun, so the history holds the nearest object) and compare it with what Duke's damage hit in that tick (dmgnum). A
// disagreement means the real bullet passed an object the replica stops at.
static unsigned s_shotSeq, s_shotTraced, s_shotSkipped, s_shotVerdict[OFFS_SHOT_KINDS], s_shotUnhooked;
static const unsigned kShotReplayMax = 8;   // replays per frame; the rest wait for the next frame
static void replay_shot(const unsigned char* player, void* world, const OffsShot& sh) {
  const Vec3 from = { sh.from[0], sh.from[1], sh.from[2] };
  const Vec3 to = { sh.from[0] + sh.dir[0] * sh.len, sh.from[1] + sh.dir[1] * sh.len, sh.from[2] + sh.dir[2] * sh.len };
  Hit hits[kHistoryMax];
  const int n = bullet_ray(player, world, from, to, hits, true);
  int ni = -1;
  for (int i = 0; i < n; ++i) if (ni < 0 || hits[i].dist < hits[ni].dist) ni = i;
  s_shotTraced++;
  const void* first = ni >= 0 ? hits[ni].obj : 0;
  if (first) { int w; classify(first, &w); }   // logs the class once, with its verdict
  // The victim's distance along the shot, from where it was when the cast damaged it.
  const float vd = (sh.victimAt[0] - sh.from[0]) * sh.dir[0] + (sh.victimAt[1] - sh.from[1]) * sh.dir[1] +
                   (sh.victimAt[2] - sh.from[2]) * sh.dir[2];
  const int v = offs_shot_verdict(sh.victim != 0, first != 0 && first == sh.victim, sh.killed,
                                  ni >= 0 ? hits[ni].dist : 1e30f, vd);
  s_shotVerdict[v]++;
  if (v == OFFS_SHOT_PASSED && s_shotVerdict[v] <= 8)
    np_log("aim: real shot #%u damaged %p at %.0f but its replica ray stops first at %p (%s) at %.0f -- the bullet "
           "passed it", sh.seq, sh.victim, vd, first, class_of(first), hits[ni].dist);
}
// Each cast once it is OFFS_SHOT_DELAY frames old (its victim's damage message has landed), in order, up to
// kShotReplayMax a frame; casts == traced + skipped + the few still pending.
static void trace_shots(const unsigned char* player, void* world) {
  const unsigned last = offscreen_shot_seq();
  if (last == s_shotSeq) return;
  if (last - s_shotSeq > OFFS_SHOT_RING) {   // fell out of the ring unreplayed
    s_shotSkipped += last - OFFS_SHOT_RING - s_shotSeq;
    s_shotSeq = last - OFFS_SHOT_RING;
  }
  const unsigned now = offscreen_frame_no();
  unsigned done = 0;
  while (s_shotSeq != last && done < kShotReplayMax) {
    const unsigned q = s_shotSeq + 1;
    OffsShot sh;
    if (!offscreen_shot(q, &sh)) { s_shotSkipped++; s_shotSeq = q; continue; }
    if (!offs_shot_due(sh.frame, now)) break;
    replay_shot(player, world, sh);
    done++;
    s_shotSeq = q;
  }
}

// Whether a bullet from `from` toward pt reaches target: no blocker in front of it on the bullet's own
// ray, destructibles allowed. SHOT_IMMUNE: it reaches a target the held weapon cannot
// hurt where the ray meets it (weapimm.h).
static int glopp_refused_hit(const void* obj, int* why);
static const EntSnap* wimm_monster(const void* obj);
static int wimm_gate_of(const EntSnap& e, int kind, const Vec3* at, int fromRay = 1);
static void wimm_note(const EntSnap& e, int kind, int why);
static int s_kind;   // this frame's held weapon (WK_*), set by aim_frame
enum { SHOT_HIDDEN = 0, SHOT_REACHED, SHOT_IMMUNE };
static unsigned s_wimmForced;   // cumulative: ray hits on an enemy the held weapon cannot hurt, made to block
static int shot_reaches(const unsigned char* player, void* world, Vec3 from, Vec3 pt, const void* target) {
  const Vec3 d = aim_sub(pt, from);
  const float l = aim_len(d);
  if (!(l > 0.f)) return SHOT_HIDDEN;
  const Vec3 to = { pt.x + d.x / l * kPastTarget, pt.y + d.y / l * kPastTarget, pt.z + d.z / l * kPastTarget };
  Hit hits[kHistoryMax];
  AimRayRec recs[kHistoryMax];
  unsigned char force[kHistoryMax];
  const int n = bullet_ray(player, world, from, to, hits);
  const bool gray = s_kind == WK_GRAY;
  for (int i = 0; i < n; ++i) {
    recs[i].target = hits[i].obj == target;
    force[i] = 0;
    if (recs[i].target) continue;
    int why;
    if (gray) { force[i] = (unsigned char)glopp_refused_hit(hits[i].obj, &why); continue; }
    // An enemy this weapon cannot hurt stops the shot all the same (the bullet spends its budget on it, the rocket
    // and the plasma stop at it): it blocks, for this weapon only.
    const EntSnap* m = wimm_monster(hits[i].obj);
    if (m && wimm_refuses(wimm_gate_of(*m, s_kind, &hits[i].at))) { force[i] = 1; s_wimmForced++; }
  }
  int bh, bw;
  const int t = evaluate(hits, recs, n, &bh, &bw, force);
  if (t >= 0) {
    // The target itself, judged where this ray meets it (the SecurityMech's test reads the hit point).
    const EntSnap* m = gray ? 0 : wimm_monster(target);
    const int why = m ? wimm_gate_of(*m, s_kind, &hits[t].at) : WIMM_OK;
    if (wimm_refuses(why)) { wimm_note(*m, s_kind, why); return SHOT_IMMUNE; }
    return SHOT_REACHED;
  }
  if (bh >= 0) s_rayBlocked++; else s_rayMissed++;
  return SHOT_HIDDEN;
}

// A bullet's ray stops at the view's sides (docs/re/aim.md section 4): off screen is out of reach, unless the clip
// bypass is armed (docs/re/offscreen-and-track.md A), when every point passes. No cone test: any direction, including
// behind Duke, is reachable. Fills c's point and distance; -1 when pt passes, else AIM_REJ_SCREEN.
static int screen_test(Vec3 pt, Vec3 eye, const Mat4& view, const Mat4& proj, float nearZ, int W, int H,
                       AimCand* c) {
  if (offscreen_active()) { c->pt = pt; c->dist = aim_len(aim_sub(pt, eye)); return -1; }
  const ScreenPt sp = w2s_project(pt, view, proj, nearZ, W, H);
  if (!sp.ok || sp.x < 0.f || sp.x > (float)W || sp.y < 0.f || sp.y > (float)H) return AIM_REJ_SCREEN;
  c->pt = pt; c->dist = aim_len(aim_sub(pt, eye));
  return -1;
}

// What both the selection and the triggerbot's crosshair ray need, gathered once a frame.
struct Ctx { const unsigned char* player; void* world; Mat4 view, proj; float nearZ; Vec3 eye, dir; int W, H; };

static bool frame_ctx(const unsigned char* player, int W, int H, Ctx* c) {
  if (entities_snapshot().player != player || !g_eng.phys_ray_test || !g_eng.phys_collide_history ||
      !g_eng.phys_ray_visible || !engine_resolve_p3shared() || !g_p3.tm_deny_ptr) return false;
  c->player = player;
  memcpy(&c->world, player + kOfsWorld, sizeof c->world);
  // A NULL physics slot makes the physics queries answer nothing (0x40d820, 0x40d8e0).
  const void* phys = 0;
  if (c->world && guarded_readable((const unsigned char*)c->world + 0x4B8, 4))
    memcpy(&phys, (const unsigned char*)c->world + 0x4B8, sizeof phys);
  if (!phys) { s_noPhys++; return false; }
  // The on-screen fallback must use the game's own camera: its sides clip the bullet, not zoom-out's. In first
  // person the game's camera is the pushed one with our pitch: the targets are the ones on screen.
  const bool viewOk = firstperson_active() ? view_get(&c->view, &c->proj, &c->nearZ) != 0
                                           : view_get_stock(&c->view, &c->proj, &c->nearZ) != 0;
  if (!viewOk) { s_noView++; return false; }
  if (!eye_and_dir(player, &c->eye, &c->dir)) { s_noEye++; return false; }
  c->W = W; c->H = H;
  return true;
}

// Lock probe (measurement only): on a new lock (at most every 250 ms) the real bullet's nearest object on the
// eye-to-target ray, and every type-bit-0x200000 entity (force_field ctor 0x100bc8d0, laser 0x100aeae0; blocker too)
// the segment crosses, with class, box, type/user, +0x2E0 and its pins' first bytes (is the barrier up?).
static const int kBarrierType = 0x200000;
static ULONGLONG s_nextProbeMs;
static unsigned s_probes, s_probeBarriers;

static void hex16(const unsigned char* p, char* out, size_t cap) {
  out[0] = 0;
  if (!guarded_readable_cached(&s_rc, p, 16)) { strcpy_s(out, cap, "unread"); return; }
  for (int i = 0; i < 16; ++i) _snprintf_s(out + i * 2, cap - i * 2, _TRUNCATE, "%02x", p[i]);
}

static void lock_probe(const Ctx& cx, const void* target, Vec3 pt, float dist) {
  const ULONGLONG now = GetTickCount64();
  if (now < s_nextProbeMs || !g_eng.spawn_search || !g_eng.spawn_search_get || !g_eng.spawn_search_stop) return;
  s_nextProbeMs = now + 250;
  s_probes++;
  const Vec3 d = aim_sub(pt, cx.eye);
  const float l = aim_len(d);
  if (!(l > 0.f)) return;
  Hit hits[kHistoryMax];
  const Vec3 to = { pt.x + d.x / l * kPastTarget, pt.y + d.y / l * kPastTarget, pt.z + d.z / l * kPastTarget };
  const int n = bullet_ray(cx.player, cx.world, cx.eye, to, hits, true);
  int ni = -1;
  for (int i = 0; i < n; ++i) if (ni < 0 || hits[i].dist < hits[ni].dist) ni = i;
  if (ni >= 0) { int w; classify(hits[ni].obj, &w); }   // names the class once, with its verdict
  np_log("aim: lock #%u on %s at %.0f: the real bullet's ray meets %s %p at %.0f%s", s_probes, class_of(target),
         dist, ni >= 0 ? class_of(hits[ni].obj) : "nothing", ni >= 0 ? hits[ni].obj : 0, ni >= 0 ? hits[ni].dist : 0.f,
         ni >= 0 && hits[ni].obj == target ? " (the target itself)" : "");
  int sid;
  memcpy(&sid, cx.player + 0x78, sizeof sid);   // the walk's own server key (entities.cpp)
  int total = 0, crossed = 0;
  g_eng.spawn_search("s=%d?t=%d", sid, kBarrierType);
  const unsigned char* e;
  while ((e = (const unsigned char*)g_eng.spawn_search_get()) != 0 && total < 256) {
    total++;
    if (!guarded_readable_cached(&s_rc, e, 0xB48)) continue;   // through the last pin read (+0xB38, 16 bytes)
    const Vec3 c = rdv(e + 0x18C), h = rdv(e + 0x2B8);
    float t;
    if (!aim_seg_box_hit(cx.eye, pt, c, h, &t)) continue;
    crossed++;
    int w;
    classify(e, &w);
    unsigned type, user;
    memcpy(&type, e + 0x48, 4);
    memcpy(&user, e + 0x4C, 4);
    char a[40], b[40], p2[40];
    hex16(e + 0xB08, a, sizeof a);
    hex16(e + 0xB38, b, sizeof b);
    hex16(e + 0xA68, p2, sizeof p2);
    if (s_probeBarriers++ < 256)
      np_log("aim:   crosses %s %p at %.0f: centre (%.0f,%.0f,%.0f) half (%.0f,%.0f,%.0f) type 0x%X user 0x%X +0x2E0=%u "
             "+0xB08=%s +0xB38=%s +0xA68=%s", class_of(e), e, t * l, c.x, c.y, c.z, h.x, h.y, h.z, type, user,
             (unsigned)e[0x2E0], a, b, p2);
  }
  g_eng.spawn_search_stop();
  np_log("aim:   %d entity(ies) of type 0x%X in the level, %d crossed by the lock's segment", total, kBarrierType, crossed);
}

// Why a target cannot take damage now: the walk's boss gate, else a damage slot read as a bare RET.
// BOSS_OPEN when nothing read refuses it. Each class's slots are read once (s_vtf).
static int target_immune(const EntSnap& e) {
  if (e.immune) return e.immune;
  int i = 0;
  while (i < s_nvtf && s_vtf[i].vt != e.vt) ++i;
  if (i < s_nvtf) return s_vtf[i].noDamage ? BOSS_IMM_NOHANDLER : BOSS_OPEN;
  VtFacts f;
  read_vt_facts((const unsigned char*)e.ent, e.vt, &f);
  if (s_nvtf < kVtFactsMax) {
    s_vtf[s_nvtf++] = f;
    np_log("aim: target class %s (vt %p): noDamage=%d actorDamages=%d", f.name, e.vt, f.noDamage, f.actorDamages);
  } else {
    s_vtfFull++;
  }
  return f.noDamage ? BOSS_IMM_NOHANDLER : BOSS_OPEN;
}

// The G.L.O.P.P. Ray (docs/re/weapons.md section 8): the beam's damage (type 4) reaches monster_apply_damage
// 0x1002f560 through vt+0x198 = monster_damage_send 0x1002f3f0 (net message 0x82, receive 0x10031990 -> vt+0x2FC),
// both fingerprinted once per vtable; a class with other handlers (every boss, helicopter part) has no morph path.
static const unsigned kSlotSend = 0x198 / 4, kSlotApply = 0x2FC / 4;
// Monster fields the handler reads, through +0xDD4 (every monst_* alloc is >= 0xEE8, classes.txt).
static const unsigned kOfsMFlags = 0xB7C, kOfsMHealth = 0xB84, kOfsMorph = 0xDC0, kOfsHyper = 0xDD4, kGloppSpan = 0xDD8;
// The held gray's actor_proj_gray (weap_gray_init 0x10055d70 stores its child at +0xBFC) and the beam length.
static const unsigned kOfsBeamObj = 0xBFC, kOfsBeamLen = 0xCC8, kBeamSpan = 0xCCC;
static const char kBeamClass[] = "actor_proj_gray";

struct GloppVt { const void* vt; int monster; };
enum { kGloppVtMax = 64, kGloppMemoMax = 64 };
static GloppVt s_gvt[kGloppVtMax];
static int s_ngvt;
// Per frame: the gate of every entity already asked (the pick, its rays and the crosshair ask the same ones).
struct GloppMemo { const void* ent; int gate; };
static GloppMemo s_gmemo[kGloppMemoMax];
static int s_ngmemo;
static float s_beamLen;         // this frame's live +0xCC8, 0 when unread
static const void* s_beamLogged;
// Cumulative: frames with the ray held, beam reads refused; refusals by the pick and by the Crosshair mode,
// split range/immune; the gate's reasons over both; hits forced to block a ray.
static unsigned s_gloppFrames, s_beamUnread, s_pickGloppRange, s_pickGloppImm, s_crossGloppRange, s_crossGloppImm,
                s_gloppBy[GLOPP_IMM_COUNT], s_gloppForced, s_gloppVtFull;

// The slot's code, readable for len bytes, or NULL.
static const unsigned char* glopp_slot_code(const void* const* v, unsigned slot, unsigned len) {
  if (!entities_in_duke(v + slot) || !entities_in_duke((const unsigned char*)(v + slot + 1) - 1)) return 0;
  const unsigned char* f = (const unsigned char*)v[slot];
  return entities_in_duke(f) && guarded_readable(f, len) ? f : 0;
}

static int glopp_monster_vt(const void* vt) {
  for (int i = 0; i < s_ngvt; ++i) if (s_gvt[i].vt == vt) return s_gvt[i].monster;
  const void* const* v = (const void* const*)vt;
  const unsigned char* send = glopp_slot_code(v, kSlotSend, OVK_SEND_FP_LEN);
  const unsigned char* apply = glopp_slot_code(v, kSlotApply, GLOPP_APPLY_SKIP + GLOPP_APPLY_FP_LEN);
  const int m = ovk_fp_monster_send(send) && glopp_fp_monster_apply(apply);
  if (s_ngvt < kGloppVtMax) {
    s_gvt[s_ngvt].vt = vt; s_gvt[s_ngvt].monster = m; s_ngvt++;
    // Unrecognised: logged with both slots' first bytes, and left a candidate (unreadClass).
    np_log("aim: glopp class vt %p: %s (vt+0x198 %p %02X%02X%02X, vt+0x2FC %p %02X%02X%02X)", vt,
           m ? "monster handlers (the beam can turn it)" : "handlers not recognised, kept a candidate",
           (const void*)send, send ? send[0] : 0, send ? send[1] : 0, send ? send[2] : 0, (const void*)apply,
           apply ? apply[0] : 0, apply ? apply[1] : 0, apply ? apply[2] : 0);
  } else {
    s_gloppVtFull++;
  }
  return m;
}

// glopp_gate for one entity, read once a frame. The morph block +0xDC0 and the hyper block +0xDD4 are their
// own allocations (the desc reader 0x1002c170), so each is checked before its bytes are read.
static int glopp_gate_of(const void* entv, const void* vt, int boss, int egg) {
  for (int i = 0; i < s_ngmemo; ++i) if (s_gmemo[i].ent == entv) return s_gmemo[i].gate;
  const unsigned char* e = (const unsigned char*)entv;
  GloppFacts f;
  memset(&f, 0, sizeof f);
  f.boss = boss;
  f.egg = egg;
  f.monster = !boss && !egg && entities_in_duke(vt) && glopp_monster_vt(vt);
  f.read = 1;
  if (f.monster) {
    f.read = guarded_readable_cached(&s_rc, e, kGloppSpan);
    if (f.read) {
      memcpy(&f.flags, e + kOfsMFlags, 4);
      memcpy(&f.health, e + kOfsMHealth, 4);
      const unsigned char* m;
      const unsigned char* h;
      memcpy(&m, e + kOfsMorph, sizeof m);
      memcpy(&h, e + kOfsHyper, sizeof h);
      f.hasMorph = m != 0;
      if (m) {
        if (guarded_readable_cached(&s_rc, m, 0x72)) { f.morphed = m[0x70] != 0; f.morphBusy = m[0x71] != 0; }
        else f.read = 0;
      }
      if (h) {
        if (guarded_readable_cached(&s_rc, h, 0x25)) f.hyperOn = h[0x24] != 0;
        else f.read = 0;
      }
    }
  }
  const int g = glopp_gate(f);
  if (s_ngmemo < kGloppMemoMax) { s_gmemo[s_ngmemo].ent = entv; s_gmemo[s_ngmemo].gate = g; s_ngmemo++; }
  return g;
}

// A live enemy or boss (or a part) on a ray that the beam cannot turn: it soaks the beam. *why = its reason.
static int glopp_refused_hit(const void* obj, int* why) {
  const EntSnapshot& snap = entities_snapshot();
  for (int e = 0; e < snap.count; ++e) {
    const EntSnap& es = snap.ents[e];
    if (es.ent != obj) continue;
    if ((es.cat != CAT_ENEMY && es.cat != CAT_BOSS) || !ent_alive(es)) return 0;
    *why = glopp_gate_of(es.ent, es.vt, es.cat == CAT_BOSS, es.egg != EGG_NONE);
    if (glopp_refuses(*why)) s_gloppForced++;
    return glopp_refuses(*why);
  }
  for (int p = 0; p < snap.nparts; ++p)
    if (snap.parts[p].ent == obj) { *why = GLOPP_IMM_BOSS; s_gloppForced++; return 1; }
  return 0;
}

// The live beam length of the held gray, 0 when anything is off. Logged once per beam object.
static float glopp_beam_len(const unsigned char* w) {
  const unsigned char* b;
  memcpy(&b, w + kOfsBeamObj, sizeof b);   // inside the gray's 0xCC0 span (weapons_held)
  if (!b || !guarded_readable_cached(&s_rc, b, kBeamSpan)) return 0.f;
  const void* const* vt;
  memcpy(&vt, b, sizeof vt);
  if (!entities_in_duke(vt) || !entities_in_duke(vt + 2) || !entities_in_duke(vt[1])) return 0.f;
  const char* cls = ((ClassnameFn)vt[1])(b);
  if (!entities_in_duke(cls) || !guarded_readable(cls, sizeof kBeamClass) || strcmp(cls, kBeamClass)) return 0.f;
  float len;
  memcpy(&len, b + kOfsBeamLen, sizeof len);
  if (!(len > 0.f && len < 100000.f)) return 0.f;
  if (b != s_beamLogged) {
    s_beamLogged = b;
    np_log("aim: G.L.O.P.P. beam %p reaches %.0f (stock %.0f)", (const void*)b, len, GLOPP_RANGE_STOCK);
  }
  return len;
}

// --- Enemies the held weapon cannot hurt (aim.md section 11, weapimm.h) -------------------------------------
// Monster fields the gate reads: the inverse world matrix +0x134 (16 floats), scale +0xC08, state +0xC68, flags
// +0xB7C; every monst_* alloc is >= 0xEE8 (classes.txt).
static const unsigned kOfsInvWorld = 0x134, kOfsMScale = 0xC08, kOfsMState = 0xC68, kWimmSpan = 0xC6C;
struct WimmVt { const void* vt; int cls; float k; };
enum { kWimmVtMax = 64, kWimmRows = 16, kWimmEspMax = 64, kWimmHitLogMax = 16 };
static WimmVt s_wvt[kWimmVtMax];
static int s_nwvt;
// Cumulative refusals by class (vtable) and held weapon; rows past kWimmRows are counted in rowsFull.
struct WimmRow { const void* vt; const char* name; unsigned n[WK_COUNT]; };
static WimmRow s_wrow[kWimmRows];
static int s_nwrow;
static unsigned s_wimmBy[WIMM_COUNT], s_wimmRowsFull, s_wimmVtFull, s_wimmPick, s_wimmCross, s_wimmEsp, s_wimmHitLogs;
// This frame's ESP verdicts (aim_weapon_immune): the enemies marked "(immune)".
static const void* s_wesp[kWimmEspMax];
static int s_nwesp;

// The class's handlers, read once per vtable. A SecurityMech handler whose constant cannot be read stays unread.
static int wimm_class_of(const void* vt, float* k) {
  for (int i = 0; i < s_nwvt; ++i) if (s_wvt[i].vt == vt) { *k = s_wvt[i].k; return s_wvt[i].cls; }
  const void* const* v = (const void* const*)vt;
  const unsigned char* dfi = glopp_slot_code(v, kSlotDamageInfo, WIMM_SMECH_FP_LEN);
  const unsigned char* send = glopp_slot_code(v, kSlotSend, WIMM_SHELL_LEN);
  unsigned kAddr = 0, tgt = 0;
  float kv = 0.f;
  int smech = dfi && wimm_fp_smech(dfi, &kAddr);
  if (smech) {
    const unsigned char* kp = (const unsigned char*)(size_t)kAddr;
    smech = entities_in_duke(kp) && guarded_readable(kp, 4);
    if (smech) memcpy(&kv, kp, sizeof kv);
    smech = smech && kv > 0.f && kv < 1000.f;
  }
  int shell = send && wimm_fp_shell(send, (unsigned)(size_t)send, &tgt);
  if (shell) {
    const unsigned char* t = (const unsigned char*)(size_t)tgt;
    shell = entities_in_duke(t) && guarded_readable(t, OVK_SEND_FP_LEN) && ovk_fp_monster_send(t);
  }
  const int cls = wimm_class(dfi && aim_fp_is_damage_from_info(dfi), smech, shell);
  *k = kv;
  if (s_nwvt < kWimmVtMax) {
    s_wvt[s_nwvt].vt = vt; s_wvt[s_nwvt].cls = cls; s_wvt[s_nwvt].k = kv; s_nwvt++;
    static const char* const kCls[4] = { "handlers not recognised, kept a candidate", "monster", "SecurityMech (bullets "
                                         "hurt only above scale * k)", "roachoid shell (no damage in state 0xB)" };
    np_log("aim: wimm class vt %p: %s, k=%.2f", vt, kCls[cls], kv);
  } else {
    s_wimmVtFull++;
  }
  return cls;
}

// A live monster of the walk (an enemy with monster health, no egg: bosses keep their own gates), or NULL.
static const EntSnap* wimm_monster(const void* obj) {
  if (!obj || !guarded_readable_cached(&s_rc, obj, 0x50)) return 0;
  unsigned type, user;
  memcpy(&type, (const unsigned char*)obj + 0x48, 4);
  memcpy(&user, (const unsigned char*)obj + 0x4C, 4);
  if (!(type & 2) || !(user & 0x20)) return 0;   // an actor with the monster user bit (aim.md section 4b)
  const EntSnapshot& snap = entities_snapshot();
  for (int i = 0; i < snap.count; ++i) {
    const EntSnap& e = snap.ents[i];
    if (e.ent != obj) continue;
    return e.cat == CAT_ENEMY && e.hashp && e.egg == EGG_NONE && ent_alive(e) ? &e : 0;
  }
  return 0;
}

// wimm_gate for monster e and held weapon `kind`, hit at *at (NULL: no hit point known). fromRay: *at is where a
// bullet ray met it (logged for the SecurityMech), not the ESP's estimate.
static int wimm_gate_of(const EntSnap& e, int kind, const Vec3* at, int fromRay) {
  const WimmSpec s = wimm_spec(kind);
  if (!s.known) return WIMM_OK;
  WimmFacts f;
  memset(&f, 0, sizeof f);
  f.cls = entities_in_duke(e.vt) ? wimm_class_of(e.vt, &f.k) : WIMM_CLASS_UNREAD;
  const unsigned char* p = (const unsigned char*)e.ent;
  if (f.cls != WIMM_CLASS_UNREAD && guarded_readable_cached(&s_rc, p, kWimmSpan)) {
    f.read = 1;
    memcpy(&f.flags, p + kOfsMFlags, 4);
    memcpy(&f.state, p + kOfsMState, 4);
    memcpy(&f.scale, p + kOfsMScale, 4);
    if (at) {
      float m[16];
      memcpy(m, p + kOfsInvWorld, sizeof m);
      f.hit = 1;
      f.hitLocalY = wimm_local_y(*at, m);
    }
  }
  const int g = wimm_gate(s, f);
  // The measurement for the SecurityMech's line: where bullets met it, against the line (docs/re/aim.md section 11, Open questions).
  if (fromRay && f.cls == WIMM_CLASS_SMECH && f.hit && s.via194 && !s.splash && s_wimmHitLogs < kWimmHitLogMax) {
    s_wimmHitLogs++;
    np_log("aim: wimm %s %p met at (%.0f,%.0f,%.0f) with the %s: local y %.1f against scale %.2f * k %.1f = %.1f -> %s "
           "(box centre y %.0f, half y %.0f)", e.name ? e.name : "?", e.ent, at->x, at->y, at->z, weap_name(kind),
           f.hitLocalY, f.scale, f.k, f.scale * f.k, wimm_label(g), e.hb.c.y, e.hb.half.y);
  }
  return g;
}

// One refusal of the pick or the Triggerbot, by class and weapon (the log's weaponImmune line).
static void wimm_note(const EntSnap& e, int kind, int why) {
  if (why >= 0 && why < WIMM_COUNT) s_wimmBy[why]++;
  if (kind < 0 || kind >= WK_COUNT) return;
  int i = 0;
  while (i < s_nwrow && s_wrow[i].vt != e.vt) ++i;
  if (i == s_nwrow) {
    if (s_nwrow >= kWimmRows) { s_wimmRowsFull++; return; }
    memset(&s_wrow[i], 0, sizeof s_wrow[i]);
    s_wrow[i].vt = e.vt; s_wrow[i].name = e.name;
    s_nwrow++;
  }
  s_wrow[i].n[kind]++;
}

// The ESP's "(immune)" marks for this frame, whatever Silent aim and the Triggerbot: each live monster judged for
// the held weapon. A hit point (the SecurityMech's bullet test) is where the eye-to-box-centre line enters its box,
// as a shot at the box centre would meet it; with no eye, no mark.
static void wimm_esp_frame(const unsigned char* player) {
  s_nwesp = 0;
  if (!player || !g_cfg.espOn) return;
  const void* wvt;
  int kind;
  weapons_held(&wvt, &kind);
  if (!wimm_spec(kind).known) return;
  const EntSnapshot& snap = entities_snapshot();
  int haveEye = -1;
  Vec3 eye = { 0.f, 0.f, 0.f };
  for (int i = 0; i < snap.count && s_nwesp < kWimmEspMax; ++i) {
    const EntSnap& e = snap.ents[i];
    if (e.cat != CAT_ENEMY || !e.hashp || e.egg != EGG_NONE || !ent_alive(e)) continue;
    float k;
    const int cls = entities_in_duke(e.vt) ? wimm_class_of(e.vt, &k) : WIMM_CLASS_UNREAD;
    Vec3 at;
    const Vec3* pat = 0;
    if (cls == WIMM_CLASS_SMECH) {
      if (haveEye < 0) haveEye = aim_fire_origin(player, &eye) ? 1 : 0;
      float t;
      if (haveEye && aim_seg_box_hit(eye, e.hb.c, e.hb.c, e.hb.half, &t)) {
        at.x = eye.x + (e.hb.c.x - eye.x) * t; at.y = eye.y + (e.hb.c.y - eye.y) * t; at.z = eye.z + (e.hb.c.z - eye.z) * t;
        pat = &at;
      }
    }
    if (wimm_refuses(wimm_gate_of(e, kind, pat, 0))) { s_wesp[s_nwesp++] = e.ent; s_wimmEsp++; }
  }
}

int aim_weapon_immune(const void* ent) {
  for (int i = 0; i < s_nwesp; ++i) if (s_wesp[i] == ent) return 1;
  return 0;
}

static void select_target(const Ctx& cx) {
  const EntSnapshot& snap = entities_snapshot();
  void* const world = cx.world;
  const Mat4& view = cx.view;
  const Mat4& proj = cx.proj;
  const float nearZ = cx.nearZ;
  const Vec3 eye = cx.eye;
  const int W = cx.W, H = cx.H;
  const float aimRange = (float)g_cfg.aimRangeM * AIM_UNITS_PER_METRE;
  const bool gray = s_kind == WK_GRAY;
  const float range = gray ? glopp_reach(aimRange, s_beamLen) : aimRange;
  int n = 0;
  // The ents, then the boss parts (ENT_SNAPSHOT_MAX + j): a part is aimed at by its centre bone.
  for (int j = 0; j < snap.count + snap.nparts; ++j) {
    const int i = j < snap.count ? j : ENT_SNAPSHOT_MAX + (j - snap.count);
    const EntSnap& e = cand_ent(snap, i);
    if (e.cat != CAT_ENEMY && e.cat != CAT_BOSS) continue;
    s_seen++;
    const int bk = boss_row(e.bossKind);
    if (bk) s_bossSeen[bk]++;
    if (!ent_alive(e)) { s_rej[AIM_REJ_DEAD]++; continue; }
    // Only what can take damage now (else every shot went into the helicopter's core).
    const int imm = target_immune(e);
    if (!aim_target_ok(1, 1, imm)) { s_rej[AIM_REJ_IMMUNE]++; s_immune[imm]++; if (bk) s_bossImm[bk]++; continue; }
    // Already dying from damage on its way; the rifle's burst still needs this lock.
    const int ovk = overkill_cause(e.ent, 0);
    if (ovk) { s_rej[AIM_REJ_DOOMED]++; overkill_pick_skip(ovk); continue; }
    // The box centre first: no skmod call for an enemy the screen already rules out (a part's box is no aim).
    const Vec3 pt = i >= ENT_SNAPSHOT_MAX ? part_point(e) : e.hb.c;
    const float d = aim_len(aim_sub(pt, eye));
    if (d > range) { s_rej[AIM_REJ_RANGE]++; if (gray && d <= aimRange) s_pickGloppRange++; continue; }
    if (gray) {   // only what the beam can turn into an animal now
      const int g =
          i >= ENT_SNAPSHOT_MAX ? GLOPP_IMM_BOSS : glopp_gate_of(e.ent, e.vt, e.cat == CAT_BOSS, e.egg != EGG_NONE);
      if (glopp_refuses(g)) { s_rej[AIM_REJ_GLOPP]++; s_pickGloppImm++; s_gloppBy[g]++; continue; }
      if (g != GLOPP_OK) s_gloppBy[g]++;   // unreadClass / unread: kept, counted
    }
    const int why = screen_test(pt, eye, view, proj, nearZ, W, H, &s_cand[n]);
    if (why >= 0) { s_rej[why]++; continue; }
    s_cand[n++].idx = i;
  }
  // Nearest first, any direction (no Pick target/Cone rows).
  int kept = aim_keep_nearest(s_cand, n, AIM_CAND_MAX);
  s_rej[AIM_REJ_CAPPED] += (unsigned)(n - kept);
  // Head for at most the 64 kept, then the same screen test on the point actually aimed at.
  if (g_cfg.aimPoint == AIM_AT_HEAD) {
    int w = 0;
    for (int k = 0; k < kept; ++k) {
      const int idx = s_cand[k].idx;
      if (idx >= ENT_SNAPSHOT_MAX) { s_cand[w++] = s_cand[k]; continue; }   // a part keeps its centre bone
      const int why = screen_test(head_point(snap.ents[idx]), eye, view, proj, nearZ, W, H, &s_cand[w]);
      if (why >= 0) { s_rej[why]++; continue; }
      s_cand[w++].idx = idx;
    }
    kept = w;
    // The head pass can reorder distances; keep nearest-first for the ray walk below.
    if (kept > 1) qsort(s_cand, (size_t)kept, sizeof(AimCand), aim_cand_cmp_dist);
  }
  // The rifle's committed burst keeps its target first while it can be reached (overkill.h): its rounds are
  // counted against it, so the lock must not hand them to the next enemy.
  const void* burst = overkill_burst_ent();
  for (int k = 0; burst && k < kept; ++k)
    if (cand_ent(snap, s_cand[k].idx).ent == burst) { aim_cand_front(s_cand, kept, s_cand[k].idx); s_burstSticky++; break; }
  s_cands += (unsigned)kept;
  if (!kept) return;
  // The bullet's own ray from the fire origin: the world-only line of
  // sight missed doors and force fields, which stop bullets. It stays as a cheap prefilter: a wall
  // stops the bullet too, and phys_ray_visible exits early.
  Matcher wm;
  world_matcher(&wm);
  int k = 0;
  for (; k < kept; ++k) {
    s_visRays++;
    if (!g_eng.phys_ray_visible(world, &eye.x, &s_cand[k].pt.x, wm.b)) { s_visHidden++; s_rej[AIM_REJ_HIDDEN]++; continue; }
    const int sr = shot_reaches(cx.player, world, eye, s_cand[k].pt, cand_ent(snap, s_cand[k].idx).ent);
    if (sr == SHOT_REACHED) break;
    // The held weapon cannot hurt it there: skipped until the player switches weapon.
    if (sr == SHOT_IMMUNE) { s_rej[AIM_REJ_WIMMUNE]++; s_wimmPick++; continue; }
    s_rej[AIM_REJ_HIDDEN]++;
  }
  g_p3.tm_dtor(wm.b);
  if (k >= kept) return;
  s_untried += (unsigned)(kept - k - 1);
  const AimCand& c = s_cand[k];
  const EntSnap& e = cand_ent(snap, c.idx);
  s_lock.have = 1;
  s_lock.ent = e.ent;
  s_lock.raw = c.pt;
  s_lock.led = c.pt;
  s_lock.dist = c.dist;
  s_lock.name = e.name;
  s_locks++;
  if (boss_row(e.bossKind)) s_bossLocks[boss_row(e.bossKind)]++;
  if (e.ent != s_lastLockEnt) { s_lockChanges++; s_lastLockEnt = e.ent; lock_probe(cx, e.ent, c.pt, c.dist); }
  const void* wvt;
  int kind;
  weapons_held(&wvt, &kind);
  const float speed = aim_proj_speed(kind);
  Vec3 v;
  if (g_cfg.aimLead && speed > 0.f && velocity(e, &v)) {
    s_lock.led = aim_lead_point(c.pt, v, c.dist, speed, g_cfg.aimLeadPct);
    s_leads++;
  }
}

// --- Probe rays for overkill.cpp ---------------------------------------------------------------
// Only inside aim_frame, between frame_ctx and its end: the context lives on its stack.
static const Ctx* s_probeCx;

int aim_probe_ray(Vec3 from, Vec3 to, int replica, AimProbeHit* out, int max) {
  if (!s_probeCx || max <= 0) return 0;
  Hit hits[kHistoryMax];
  AimProbeHit all[kHistoryMax];
  const int n = bullet_ray(s_probeCx->player, s_probeCx->world, from, to, hits, replica != 0);
  for (int i = 0; i < n; ++i) {
    AimProbeHit& h = all[i];
    memset(&h, 0, sizeof h);
    h.obj = hits[i].obj; h.dist = hits[i].dist; h.at = hits[i].at;
    const unsigned char* o = (const unsigned char*)hits[i].obj;
    h.read = guarded_readable_cached(&s_rc, o, 0x50);
    if (h.read) {
      memcpy(&h.type, o + 0x48, 4);
      memcpy(&h.user, o + 0x4C, 4);
      if ((h.type & 2) && (h.user & 0x20)) h.dying = (int)rd_u32(o + kOfsDying) >= 0x14;
    }
    for (int j = i; j > 0 && all[j - 1].dist > all[j].dist; --j) {   // insertion, nearest first
      const AimProbeHit t = all[j]; all[j] = all[j - 1]; all[j - 1] = t;
    }
  }
  const int m = n < max ? n : max;
  memcpy(out, all, sizeof(AimProbeHit) * (size_t)m);
  return m;
}

int aim_probe_visible(Vec3 from, Vec3 to) {
  if (!s_probeCx) return 0;
  Matcher wm;
  world_matcher(&wm);
  const int ok = g_eng.phys_ray_visible(s_probeCx->world, &from.x, &to.x, wm.b) ? 1 : 0;
  g_p3.tm_dtor(wm.b);
  return ok;
}

// --- Triggerbot ------------------------------------------------------------------------------
// rend_begin_frame prism3d!0x41fa60 (engine.h): the device global is absolute in the exe, as the
// end-frame detour's fingerprint is (probe.cpp). 6 bytes (mov ecx,[abs]) move to the trampoline.
static const unsigned char kBeginFrameFp[14] = {
  0x8B, 0x0D, 0x38, 0x09, 0x4A, 0x00, 0x8B, 0x01, 0xFF, 0xA0, 0xD8, 0x00, 0x00, 0x00 };
static const int kImpulseAttack = 106;   // mask bit 0x20 (player_read_input_mask 0x1007d2af, aim.md section 5)
static const float kCrossLen = 5000.f;   // a bullet reaches at most 5000 (game_clip_ray_to_view_sides, aim.md 4)
static const unsigned kImpulseBinds = 0x50;      // impulse slot bind count (inp_impulse_get, engine.h)

static void (*s_beginTramp)(void);
static int s_beginArmed, s_beginRefused;   // the frame-start hook
static int s_tbArmed, s_tbRefused;
static int s_channel, s_channelRead;
static TbotState s_tb;
static unsigned s_tbLastMs;
static int s_tbHaveMs;
static int s_fireNext;   // set by the end-of-frame decision, taken by the next rend_begin_frame
static int s_pulseLast;  // tbot_pulse's state: last frame pressed
static TbotState s_tbShadow;   // the same delay machine fed the target before the doom test (overkill)
static int s_withheldPrev;     // ovk_withheld_step's state
// Shots by who pressed (guards a "double fire"): weapons_shots() rising on a frame whose start we
// pressed is the bot's shot, else Duke's own. Lines per shot under Debug log; dmgnum logs each hit beside.
static int s_botPressed;   // set by tbot_begin_frame when it pressed this frame
static unsigned s_shotsSeen, s_botShots, s_manualShots;
static int s_shotsPrimed;
// weap_geagle_fire_logic duke!0x10055120: +0xC1E is set by a frame with the attack bit clear and cleared by
// a shot; +0xB18 is the last-fire time (weapons.md section 1). Read once a frame, semi-automatic only.
static const unsigned kOfsSemiLatch = 0xC1E, kOfsLastFire = 0xB18;
// The Pulse Cannon's charge +0xCBC (weap_pcannon_fire_logic 0x100588a0; zeroed by each discharge, 0x100590f0).
static const unsigned kOfsPcCharge = 0xCBC;
// Its taps, cumulative: presses, and on the next frame whether the press charged (charged/noCharge); releases of a
// charge we pressed, and on the next frame whether it left (shots: charge back to 0) or not (stuck); unread frames.
static unsigned s_pcPresses, s_pcCharged, s_pcNoCharge, s_pcReleases, s_pcShots, s_pcStuck, s_pcUnread;
static int s_pcPrevPress, s_pcPrevRelease;
// The last release of a charge we pressed (wall clock) and frames a tap waited for the cadence, cumulative.
static unsigned s_pcLastReleaseMs, s_pcCadence;
static int s_pcHaveRelease;
static int s_pressedPrev, s_haveB18;
static float s_lastB18;
static unsigned s_semiPresses, s_pressOut[TBOT_PRESS_KINDS], s_latchUnread;
static unsigned s_tbFrames, s_tbHolds, s_tbFires, s_tbPresses, s_tbBlocked, s_tbUnbound, s_tbWeapon, s_tbGaps;
// X-3000 targets it cannot hurt (boss, part, egg, not drawn), and those whose +0xAF1 could not be read; cumulative.
static unsigned s_tbX3000, s_tbX3000Unread, s_tbX3000Scan;
static const unsigned char* s_attackSlot;   // impulse 106's slot, validated once
static int s_unboundLogged;
static unsigned s_rays, s_rayEnemy, s_rayWorld, s_rayOther, s_rayNone, s_rayOffscreen;

int aim_tbot_armed(void) { return s_tbArmed; }
int aim_tbot_refused(void) { return s_tbRefused; }
int aim_tbot_channel(void) { return s_channel; }
unsigned aim_tbot_presses(void) { return s_tbPresses; }
unsigned aim_tbot_holds(void) { return s_tbHolds; }

// On the main thread, before the input handler reads this frame's impulses: the press lands in
// this frame's mask, and inp_reset_impulses clears it before the next one (aim.md section 5).
static void tbot_begin_frame(void) {
  if (!s_fireNext) return;
  s_fireNext = 0;   // rend_restart's extra call finds nothing to press
  if (!tbot_may_press(g_cfg.tbotOn, input_panel_open(), input_freecam_on(), input_console_open())) {
    s_tbBlocked++;
    return;
  }
  // Skip rather than undo: with no bind the engine never clears the slot, and not adding is the one
  // choice that leaves nothing of ours behind if the Triggerbot is switched off mid-press.
  int binds = 0;
  if (s_attackSlot) memcpy(&binds, s_attackSlot + kImpulseBinds, sizeof binds);
  if (!tbot_bound(binds)) {
    s_tbUnbound++;
    if (!s_unboundLogged++) np_log("aim: triggerbot: attack (impulse 106) has no bind (%d); not pressing", binds);
    return;
  }
  g_eng.inp_impulse_set(kImpulseAttack, s_channel, 1.0f);
  s_tbPresses++;
  s_botPressed = 1;
  if (!offscreen_armed()) s_shotUnhooked++;   // this press's shot is not seen by the replay
}

// cdecl, no args, reached by a tail jump or a call: registers and the stack go to the trampoline as they came.
static void __declspec(naked) begin_frame_detour(void) {
  __asm {
    pushad
    call tbot_begin_frame
    popad
    jmp dword ptr [s_beginTramp]
  }
}

// Armed when the Triggerbot is first switched on: until then the frame start is stock. A refusal is final.
int aim_begin_hook(void) {
  if (s_beginArmed || s_beginRefused) return s_beginArmed;
  const unsigned char* fn = (const unsigned char*)g_eng.rend_begin_frame;
  if (!fn || !g_eng.inp_impulse_set || !guarded_readable(fn, sizeof kBeginFrameFp) ||
      memcmp(fn, kBeginFrameFp, sizeof kBeginFrameFp) != 0) {
    s_beginRefused = 1;
    np_log("aim: frame-start hook refused: rend_begin_frame %p / inp_impulse_set %p missing or not the read bytes",
           (const void*)fn, (const void*)g_eng.inp_impulse_set);
    return 0;
  }
  if (!hook_install_at((void*)fn, kBeginFrameFp, sizeof kBeginFrameFp, 6, (void*)begin_frame_detour,
                       (void**)&s_beginTramp)) {
    s_beginRefused = 1;
    np_log("aim: frame-start hook install at %p refused", (const void*)fn);
    return 0;
  }
  s_beginArmed = 1;
  np_log("aim: frame-start hook armed at rend_begin_frame %p", (const void*)fn);
  return 1;
}

// Armed once the Triggerbot is first switched on, as Silent aim is: off, the frame start is stock.
static void tbot_arm(void) {
  if (s_tbArmed || s_tbRefused) return;
  const unsigned char* slot = g_eng.inp_impulse_get ? g_eng.inp_impulse_get(kImpulseAttack) : 0;
  if (!slot || !guarded_readable(slot, kImpulseBinds + 4)) {
    s_tbRefused = 1;
    np_log("aim: triggerbot refused: impulse 106's slot %p unreadable", (const void*)slot);
    return;
  }
  s_attackSlot = slot;
  if (!aim_begin_hook()) {
    s_tbRefused = 1;
    np_log("aim: triggerbot refused: no frame-start hook");
    return;
  }
  s_tbArmed = 1;
  np_log("aim: triggerbot armed");
}

// wm slot 0's channel (+0x14), found through wm_get_input's own instructions. 0 on any doubt.
static void tbot_channel(void) {
  if (s_channelRead) return;
  s_channelRead = 1;
  const unsigned char* f = (const unsigned char*)g_eng.wm_get_input;
  unsigned table = 0;
  if (!f || !guarded_readable(f, 23) || !aim_wm_table(f, &table) || !guarded_readable((const void*)(size_t)table, 0x18)) {
    np_log("aim: triggerbot channel: wm_get_input bytes not the read ones; channel 0");
    return;
  }
  const unsigned char* slot = (const unsigned char*)(size_t)table;
  const void* ent;
  int ch;
  memcpy(&ent, slot + 0x10, sizeof ent);
  memcpy(&ch, slot + 0x14, sizeof ch);
  const void* input = g_eng.wm_get_input(0);
  if (ent != input || !tbot_channel_ok(ch)) {
    np_log("aim: triggerbot channel: slot 0 %p entity %p, input %p, channel %d disagree; channel 0",
           (const void*)slot, ent, input, ch);
    return;
  }
  s_channel = ch;
  np_log("aim: triggerbot channel %d (wm slot table %p, slot 0 live=%u entity %p)", ch, (const void*)slot, slot[0], ent);
}

int aim_fire_origin(const unsigned char* player, Vec3* eye) {
  Vec3 dir;
  return player && eye_and_dir(player, eye, &dir);
}

// Crosshair: the unredirected line of fire on the bullet's own ray (aim.md section 4b). The nearest
// live enemy that no blocker precedes is the target; destructibles in front do not block.
// On screen unless the clip bypass is armed (re/offscreen-and-track.md A).
static const void* crosshair_enemy(const Ctx& cx) {
  s_rays++;
  const float l = aim_len(cx.dir);
  const Vec3 to = { cx.eye.x + cx.dir.x / l * kCrossLen, cx.eye.y + cx.dir.y / l * kCrossLen,
                    cx.eye.z + cx.dir.z / l * kCrossLen };
  Hit hits[kHistoryMax];
  AimRayRec recs[kHistoryMax];
  unsigned char force[kHistoryMax];
  int why[kHistoryMax], wimm[kHistoryMax];
  const int n = bullet_ray(cx.player, cx.world, cx.eye, to, hits);
  if (!n) { s_rayNone++; return 0; }
  const bool gray = s_kind == WK_GRAY;
  const EntSnapshot& snap = entities_snapshot();
  for (int i = 0; i < n; ++i) {
    recs[i].target = 0;
    force[i] = 0;
    why[i] = GLOPP_OK;
    wimm[i] = WIMM_OK;
    // A monster this weapon cannot hurt where the line of fire meets it: no target, and it stops the shot.
    const EntSnap* wm = gray ? 0 : wimm_monster(hits[i].obj);
    if (wm && wimm_refuses(wimm[i] = wimm_gate_of(*wm, s_kind, &hits[i].at))) {
      force[i] = 1;
      s_wimmForced++;
      continue;
    }
    bool found = false;
    for (int e = 0; e < snap.count; ++e) {
      const EntSnap& es = snap.ents[e];
      if (es.ent != hits[i].obj) continue;
      // An immune boss soaks the shot like a wall (aim.md section 7): never a crosshair target. The same test
      // as silent aim's, so the Robo Duke body and the crane (no damage handler) are refused here too.
      const bool eob = es.cat == CAT_ENEMY || es.cat == CAT_BOSS;
      recs[i].target = aim_target_ok(eob, ent_alive(es), eob ? target_immune(es) : BOSS_OPEN);
      // With the ray, an enemy it cannot turn is no target and soaks the beam (proj_gray_beam_tick 0x10041940).
      if (gray && eob && ent_alive(es) &&
          glopp_refuses(why[i] = glopp_gate_of(es.ent, es.vt, es.cat == CAT_BOSS, es.egg != EGG_NONE))) {
        recs[i].target = 0;
        force[i] = 1;
        s_gloppForced++;
      }
      found = true;
      break;
    }
    // Helicopter parts live outside ents (the ESP never draws them); an open one is a target.
    for (int p = 0; !found && p < snap.nparts; ++p)
      if (snap.parts[p].ent == hits[i].obj) {
        recs[i].target = aim_target_ok(1, 1, snap.parts[p].immune);
        if (gray) { recs[i].target = 0; force[i] = 1; why[i] = GLOPP_IMM_BOSS; s_gloppForced++; }
        found = true;
      }
  }
  int bh, bw;
  const int t = evaluate(hits, recs, n, &bh, &bw, force);
  if (t < 0 && gray && bh >= 0 && force[bh]) { s_crossGloppImm++; s_gloppBy[why[bh]]++; }
  if (t < 0 && !gray && bh >= 0 && wimm_refuses(wimm[bh])) {
    s_wimmCross++;
    const EntSnap* wm = wimm_monster(hits[bh].obj);
    if (wm) wimm_note(*wm, s_kind, wimm[bh]);
  }
  if (t < 0) {
    if (bh < 0) s_rayNone++;
    else if (bw) s_rayWorld++;   // world_e (freecam-teleport.md 2)
    else s_rayOther++;
    return 0;
  }
  const ScreenPt sp = w2s_project(hits[t].at, cx.view, cx.proj, cx.nearZ, cx.W, cx.H);
  if (!offscreen_active() && (!sp.ok || sp.x < 0.f || sp.x > (float)cx.W || sp.y < 0.f || sp.y > (float)cx.H)) {
    s_rayOffscreen++;
    return 0;
  }
  // The beam stops at +0xCC8 from the muzzle; the Aim range too, for this weapon.
  if (gray && glopp_verdict(hits[t].dist, glopp_reach((float)g_cfg.aimRangeM * AIM_UNITS_PER_METRE, s_beamLen),
                            GLOPP_OK) == GLOPP_REFUSE_RANGE) {
    s_crossGloppRange++;
    return 0;
  }
  s_rayEnemy++;
  // Which boss or part the crosshair target is (the per-boss log).
  for (int e = 0; e < snap.count; ++e)
    if (snap.ents[e].ent == hits[t].obj) s_bossCross[boss_row(snap.ents[e].bossKind)]++;
  for (int p = 0; p < snap.nparts; ++p)
    if (snap.parts[p].ent == hits[t].obj) s_bossCross[boss_row(snap.parts[p].bossKind)]++;
  return hits[t].obj;
}

// The shot this frame's tick fired, if any, and who pressed for it. The press came from last frame's
// decision, so its target is s_tb.target; its health is what the lock reads now (a monster's damage lands
// a tick or more after the cast, aim.md section 4b), which shows whether a shot went at a dying target.
static void note_shots(unsigned now) {
  const unsigned shots = weapons_shots();
  const int bot = s_botPressed;
  s_botPressed = 0;
  if (s_shotsPrimed && shots != s_shotsSeen) {
    const unsigned k = shots - s_shotsSeen;
    if (bot) s_botShots += k; else s_manualShots += k;
    if (g_cfg.timing) {
      const void* t = bot ? s_tb.target : 0;
      float hp = -1.f;
      int found = 0;
      const EntSnapshot& snap = entities_snapshot();
      for (int i = 0; t && i < snap.count; ++i)
        if (snap.ents[i].ent == t) { hp = snap.ents[i].hp; found = 1; break; }
      np_log("aim: shot t=%u by %s, %s, target %p %s hp=%.1f", now, bot ? "triggerbot" : "Duke",
             weapons_held_name(), t, t ? (found ? "in snapshot" : "gone") : "-", hp);
    }
  }
  s_shotsSeen = shots;
  s_shotsPrimed = 1;
}

// --- X-3000: only what the last world pass drew, on screen (docs/re/aim.md section 12) -------
// The actor draw FUN_10090260 (thiscall, one stack arg, RET 4) records each actor's pass in s_drawn while the
// Triggerbot is on. It only reads our table; the engine's memory is untouched.
typedef void (__thiscall* ActorDrawFn)(void* self, void* arg);
static ActorDrawFn s_drawTramp;
static int s_drawArmed, s_drawRefused;
static X3kDrawn s_drawn;
static unsigned s_drawPass;   // what the draws record now; aim_frame moves it on after each world pass
static unsigned s_x3kPass;    // the pass aim_frame found just completed
static int s_x3kFreecamNow, s_x3kFreecamPrev;
static const void* s_x3kPlayer;
static const void* s_x3kWorld;
// Cumulative: draws recorded; monsters x3000_hurts accepted but not drawn in the last pass (stale) or with the box
// centre off screen (offscreen); held frames without a frame context (noCtx: no target then).
static unsigned s_drawRecords, s_tbX3000Stale, s_tbX3000Offscreen, s_tbX3000NoCtx;

static void __fastcall actor_draw_detour(void* self, void* /*edx*/, void* arg) {
  if (g_cfg.tbotOn) { x3000_drawn_put(&s_drawn, self, s_drawPass); s_drawRecords++; }
  s_drawTramp(self, arg);
}

// No export or vtable slot reaches FUN_10090260 directly, so duke_base's executable sections are scanned once for
// its bytes (x3000_find_actor_draw), the first time the Triggerbot runs with the X-3000 held. A refusal is final.
static void x3000_draw_arm(void) {
  if (s_drawArmed || s_drawRefused) return;
  s_drawRefused = 1;   // until installed
  const unsigned char* base = 0;
  unsigned size = 0;
  if (!entities_duke_image(&base, &size) || !guarded_readable(base, sizeof(IMAGE_DOS_HEADER))) {
    np_log("aim: x3000 draw hook refused: duke_base image unreadable");
    return;
  }
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || (unsigned)dos->e_lfanew >= size ||
      !guarded_readable(nt, sizeof *nt) || nt->Signature != IMAGE_NT_SIGNATURE) {
    np_log("aim: x3000 draw hook refused: duke_base headers not read");
    return;
  }
  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  const unsigned nsec = nt->FileHeader.NumberOfSections;
  if (!guarded_readable(sec, nsec * sizeof *sec)) { np_log("aim: x3000 draw hook refused: section table unreadable"); return; }
  LARGE_INTEGER t0, t1;
  if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
  QueryPerformanceCounter(&t0);
  int found = 0;
  unsigned char* fn = 0;
  for (unsigned i = 0; i < nsec; ++i) {
    if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
    const unsigned va = sec[i].VirtualAddress, n = sec[i].Misc.VirtualSize;
    if (va >= size || n > size - va || !guarded_readable(base + va, n)) continue;
    size_t at = 0;
    const int k = x3000_find_actor_draw(base + va, n, &at);
    if (k && !found) fn = (unsigned char*)base + va + at;
    found += k;
  }
  QueryPerformanceCounter(&t1);
  const double us = (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)s_qpf.QuadPart;
  if (found != 1) {
    np_log("aim: x3000 draw hook refused: %d match(es) of FUN_10090260's bytes in duke_base (scan %.0f us)", found, us);
    return;
  }
  // hook_install_at copies the head to the trampoline as it is: a JMP of someone else's there would not survive it.
  if (fn[0] != kX3kDrawFp[0]) {
    np_log("aim: x3000 draw hook refused: FUN_10090260 at %p is already detoured (%02X)", (void*)fn, fn[0]);
    return;
  }
  if (!hook_install_at(fn, kX3kDrawFp, X3K_DRAW_FP_LEN, X3K_DRAW_HEAD, (void*)actor_draw_detour, (void**)&s_drawTramp)) {
    np_log("aim: x3000 draw hook install at %p refused", (void*)fn);
    return;
  }
  s_drawRefused = 0;
  s_drawArmed = 1;
  np_log("aim: x3000 draw hook armed at %p (duke_base+%X), scan %.0f us", (void*)fn, (unsigned)(fn - base), us);
}

// The X-3000 target: the first snapshot entity x3000_hurts accepts, drawn in the last world pass with its box centre on
// the game's own screen (x3000_view_gate), that overkill does not doom; through walls. +0xAF1 was read once by the
// entity walk (EntSnap::drawn) and the draw table is ours, so no guarded read per entity. No context: no target.
static const void* x3000_scan(const Ctx* cx) {
  if (!cx) { s_tbX3000NoCtx++; s_tbX3000++; return 0; }
  const EntSnapshot& snap = entities_snapshot();
  for (int i = 0; i < snap.count; ++i) {
    const EntSnap& es = snap.ents[i];
    if (es.hashp && es.drawn < 0) s_tbX3000Unread++;
    int hurts = x3000_hurts(es.hashp && es.egg == EGG_NONE, es.drawn, es.hp);
    if (hurts) {
      unsigned rec = 0;
      const int have = x3000_drawn_get(&s_drawn, es.ent, &rec);
      const int fresh = x3000_fresh(have, rec, s_x3kPass, s_x3kFreecamNow, s_x3kFreecamPrev);
      const int on = fresh && x3000_on_screen(w2s_project(es.hb.c, cx->view, cx->proj, cx->nearZ, cx->W, cx->H),
                                              cx->W, cx->H);
      const int gate = x3000_view_gate(fresh, on);
      if (gate == X3K_STALE) s_tbX3000Stale++;
      else if (gate == X3K_OFFSCREEN) s_tbX3000Offscreen++;
      hurts = gate == X3K_SEEN;
    }
    const int doomed = hurts && overkill_cause(es.ent, 1) != OVK_ALIVE;
    const int pick = x3000_pick(&hurts, &doomed, 1);
    if (pick == 0) { s_tbX3000Scan++; return es.ent; }
  }
  s_tbX3000++;
  return 0;
}

static void tbot_frame(const unsigned char* player, const Ctx* cx) {
  const unsigned now = wallclock_ms();
  const int dt = s_tbHaveMs ? (int)(now - s_tbLastMs) : 0;
  s_tbLastMs = now;
  s_tbHaveMs = 1;
  tbot_arm();
  if (!s_tbArmed) return;
  tbot_channel();
  note_shots(now);
  const void* wvt;
  int kind;
  const unsigned char* w = weapons_held(&wvt, &kind);
  const void* target = 0;
  const void* doomed = 0;
  int ovk = OVK_ALIVE;
  if (kind == WK_X3000) {
    // Either mode: no crosshair, no lock, no line of sight (the weapon reaches through walls).
    // The scan already dropped the doomed.
    x3000_draw_arm();
    // A new level (player or world): the table's actors are gone.
    const void* world = 0;
    memcpy(&world, player + kOfsWorld, sizeof world);   // inside kPlayerSpan, as frame_ctx reads it
    if (player != s_x3kPlayer || world != s_x3kWorld) {
      x3000_drawn_clear(&s_drawn);
      s_x3kPlayer = player;
      s_x3kWorld = world;
    }
    target = x3000_scan(cx);
  } else {
    if (g_cfg.tbotWhen == TBOT_WHEN_LOCKED) {
      if (g_cfg.silentOn && s_target && s_lock.have) target = s_lock.ent;
    } else if (cx) {
      target = crosshair_enemy(*cx);
    }
    // Don't waste shots: a target that damage on its way already kills is no target.
    ovk = target ? overkill_cause(target, 1) : OVK_ALIVE;
    if (ovk) { doomed = target; target = 0; }
  }
  const int semi = tbot_weapon_semi_auto(kind);
  const int pcannon = kind == WK_PCANNON;
  int pcRead = 0;
  float pcCharge = 0.f;
  if (pcannon && w) { memcpy(&pcCharge, w + kOfsPcCharge, sizeof pcCharge); pcRead = 1; }   // inside weapons_held's span
  if (s_pcPrevPress) { if (pcRead && pcCharge > 0.f) s_pcCharged++; else s_pcNoCharge++; }
  if (s_pcPrevRelease) { if (pcRead && !(pcCharge > 0.f)) s_pcShots++; else s_pcStuck++; }
  // Measurement: did last frame's press produce a shot (+0xB18 moved)? Cumulative, in the log line.
  int latch = -1;
  float b18 = 0.f;
  const bool readOk = semi && w && guarded_readable(w + kOfsLastFire, kOfsSemiLatch + 1 - kOfsLastFire);
  if (readOk) { memcpy(&b18, w + kOfsLastFire, sizeof b18); latch = w[kOfsSemiLatch] != 0; }
  if (s_pressedPrev) {
    s_semiPresses++;
    s_pressOut[tbot_press_outcome(readOk && s_haveB18, b18 != s_lastB18, latch > 0)]++;
  }
  s_lastB18 = b18;
  s_haveB18 = readOk;
  if (target && !tbot_weapon_fires_on_press(kind)) { target = 0; s_tbWeapon++; }
  const int holds = tbot_when_holds(g_cfg.tbotWhen, target != 0, target != 0);
  s_tbFrames++;
  if (holds) s_tbHolds++;
  const int fire = tbot_step(&s_tb, holds, target, dt, g_cfg.tbotDelay);
  if (fire) s_tbFires++;
  const int want = fire && tbot_may_press(g_cfg.tbotOn, input_panel_open(), input_freecam_on(), input_console_open());
  if (semi) {
    if (want && latch < 0) s_latchUnread++;
    s_fireNext = tbot_semi_press(want, latch, &s_pulseLast);
  } else if (pcannon) {
    // Taps, never a hold: a held bit would charge and fire only when the condition broke.
    if (want && !pcRead) s_pcUnread++;
    // The cadence only holds back a new press: a charge we hold is always released.
    const int ready = tbot_pcannon_ready(s_pcHaveRelease, now, s_pcLastReleaseMs);
    if (want && !ready && !(pcCharge > 0.f)) s_pcCadence++;
    s_fireNext = tbot_pcannon_press(want && ready, pcRead, pcCharge, &s_pulseLast);
  } else {
    s_fireNext = tbot_pulse(want, 0, &s_pulseLast);
  }
  if (want && !s_fireNext) s_tbGaps++;   // a released frame of a semi-automatic weapon
  // The decision without the doom test, on its own delay machine: what it would have fired is `withheld`.
  const void* st = doomed ? doomed : target;
  if (st && !tbot_weapon_fires_on_press(kind)) st = 0;
  const int would = tbot_step(&s_tbShadow, st != 0, st, dt, g_cfg.tbotDelay) &&
                    tbot_may_press(g_cfg.tbotOn, input_panel_open(), input_freecam_on(), input_console_open());
  if (ovk_withheld_step(would, doomed != 0, semi || pcannon, &s_withheldPrev)) overkill_withheld(ovk);
  if (s_fireNext && kind == WK_ARIFLE) overkill_burst_target(target);   // this press commits a burst at it
  s_pressedPrev = semi && s_fireNext;
  // A press of ours, then the release that discharges it (the press was the Triggerbot's, so the shot is its own).
  s_pcPrevRelease = pcannon && pcRead && s_pcPrevPress && !s_fireNext && pcCharge > 0.f;
  s_pcPrevPress = pcannon && s_fireNext;
  if (s_pcPrevPress) s_pcPresses++;
  if (s_pcPrevRelease) { s_pcReleases++; s_pcLastReleaseMs = now; s_pcHaveRelease = 1; }
}

static void aim_log(void) {
  const ULONGLONG tick = GetTickCount64();
  if (tick < s_nextLog) return;
  s_nextLog = tick + 5000;
  unsigned sum = s_calls + s_seen + s_locks + s_headFallback + s_headFar + s_velRefused + s_noEye + s_noView + s_noPhys +
                 s_tbFrames + s_tbPresses + s_tbBlocked + s_tbUnbound + s_tbWeapon + s_gloppFrames;   // not s_wimmEsp: a marked enemy would re-log every 5 s
  if (sum == s_loggedSum) return;   // cumulative: an unchanged line says nothing
  s_loggedSum = sum;
  // calls == rewritten + off + notmine + gate + nolock + noorigin + degenerate + mouse + nocursor + view + noview;
  // seen == dead + immune + glopp + doomed + range + screen + capped + cands (screen counts the head pass too);
  // cands == hidden + wimmune + locks + untried; immune == the immuneBy reasons; through: reached, destructible ahead.
  np_log("aim: armed=%d calls=%u rewritten=%u off=%u notmine=%u gate=%u nolock=%u noorigin=%u degenerate=%u "
         "mouse=%u nocursor=%u view=%u noview=%u | seen=%u dead=%u immune=%u glopp=%u doomed=%u range=%u screen=%u capped=%u cands=%u hidden=%u wimmune=%u locks=%u untried=%u "
         "changes=%u leads=%u velRefused=%u headFallback=%u headFar=%u boneCalls=%u boneUs=%.0f noEye=%u noView=%u "
         "noPhys=%u | visRays=%u visHidden=%u rayBlocked=%u rayMissed=%u rayThrough=%u rayUnread=%u bulletRays=%u "
         "bulletUs=%.0f classes=%d classesFull=%u burstSticky=%u",
         s_target != 0, s_calls, s_why[R_REWRITTEN], s_why[R_OFF], s_why[R_NOTMINE], s_why[R_GATE], s_why[R_NOLOCK],
         s_why[R_NOORIGIN], s_why[R_DEGENERATE], s_why[R_MOUSE], s_why[R_NOCURSOR], s_why[R_VIEW], s_why[R_NOVIEW], s_seen, s_rej[AIM_REJ_DEAD], s_rej[AIM_REJ_IMMUNE], s_rej[AIM_REJ_GLOPP], s_rej[AIM_REJ_DOOMED], s_rej[AIM_REJ_RANGE],
         s_rej[AIM_REJ_SCREEN], s_rej[AIM_REJ_CAPPED], s_cands, s_rej[AIM_REJ_HIDDEN], s_rej[AIM_REJ_WIMMUNE], s_locks,
         s_untried, s_lockChanges, s_leads, s_velRefused, s_headFallback, s_headFar, s_boneCalls, s_boneUs, s_noEye,
         s_noView, s_noPhys, s_visRays, s_visHidden, s_rayBlocked, s_rayMissed, s_rayThrough, s_rayUnread, s_bulletRays,
         s_bulletUs, s_nvtf, s_vtfFull, s_burstSticky);
  // Boss targets skipped as immune now, by the gate that refused (bossrule.h; cumulative). partPoints +
  // partFallback: part aim points from the centre bone and from the box; partFar: bone points refused as far.
  if (s_rej[AIM_REJ_IMMUNE] || s_partPoints || s_partFallback)
    np_log("aim: immuneBy %s=%u %s=%u %s=%u %s=%u %s=%u %s=%u %s=%u | partPoints=%u partFallback=%u partFar=%u",
           boss_imm_label(BOSS_IMM_NOHANDLER), s_immune[BOSS_IMM_NOHANDLER], boss_imm_label(BOSS_IMM_PORK_CORE),
           s_immune[BOSS_IMM_PORK_CORE], boss_imm_label(BOSS_IMM_PORK_PART), s_immune[BOSS_IMM_PORK_PART],
           boss_imm_label(BOSS_IMM_QUEEN), s_immune[BOSS_IMM_QUEEN], boss_imm_label(BOSS_IMM_MECHHEAD),
           s_immune[BOSS_IMM_MECHHEAD], boss_imm_label(BOSS_IMM_WOZMA), s_immune[BOSS_IMM_WOZMA],
           boss_imm_label(BOSS_IMM_EGG), s_immune[BOSS_IMM_EGG], s_partPoints,
           s_partFallback, s_partFar);
  // Per boss entity and part, cumulative: seen/immune/locks/crosshair (only rows silent aim or the crosshair met).
  {
    char line[512];
    int len = _snprintf_s(line, sizeof line, _TRUNCATE, "aim: bosses seen/immune/locks/crosshair");
    int rows = 0;
    for (int k = 1; k < BOSS_K_COUNT && len > 0; ++k) {
      if (!s_bossSeen[k] && !s_bossCross[k]) continue;
      const int w = _snprintf_s(line + len, sizeof line - len, _TRUNCATE, " %s=%u/%u/%u/%u", boss_kind_label(k),
                                s_bossSeen[k], s_bossImm[k], s_bossLocks[k], s_bossCross[k]);
      if (w < 0) break;
      len += w;
      rows++;
    }
    if (rows) np_log("%s", line);
  }
  // G.L.O.P.P. Ray, cumulative. pick (silent aim, the Locked mode): gloppRange = in the Aim range but past the beam,
  // gloppImmune == the pick's glopp bucket. triggerbot = the Crosshair mode's refusals; immuneBy sums both. forced =
  // ray hits on an enemy the beam cannot turn, made to block (both paths).
  if (s_gloppFrames) {
    char by[256];
    int len = 0;
    by[0] = 0;
    for (int r = 1; r < GLOPP_IMM_COUNT; ++r) {
      const int w = _snprintf_s(by + len, sizeof by - len, _TRUNCATE, " %s=%u", glopp_imm_label(r), s_gloppBy[r]);
      if (w < 0) break;
      len += w;
    }
    np_log("aim: glopp frames=%u beam=%.0f beamUnread=%u | pick gloppRange=%u gloppImmune=%u | triggerbot "
           "gloppRange=%u gloppImmune=%u | forced=%u classes=%d classesFull=%u | immuneBy%s", s_gloppFrames, s_beamLen,
           s_beamUnread, s_pickGloppRange, s_pickGloppImm, s_crossGloppRange, s_crossGloppImm, s_gloppForced, s_ngvt,
           s_gloppVtFull, by);
  }
  // Enemies the held weapon cannot hurt, cumulative: pick (silent aim, so the Locked mode) == the aim line's wimmune;
  // triggerbot = Crosshair-mode rays whose first enemy was one; forced = ray hits made to block (both); esp = frames x
  // enemies marked "(immune)"; by = the gate's reasons over pick + triggerbot. Then per class and weapon.
  if (s_wimmPick || s_wimmCross || s_wimmForced || s_wimmEsp) {
    char line[640];
    int len = _snprintf_s(line, sizeof line, _TRUNCATE, "aim: weaponImmune pick=%u triggerbot=%u forced=%u esp=%u "
                          "classes=%d classesFull=%u rowsFull=%u | by", s_wimmPick, s_wimmCross, s_wimmForced, s_wimmEsp,
                          s_nwvt, s_wimmVtFull, s_wimmRowsFull);
    for (int r = 1; r < WIMM_COUNT && len > 0; ++r) {
      const int w = _snprintf_s(line + len, sizeof line - len, _TRUNCATE, " %s=%u", wimm_label(r), s_wimmBy[r]);
      if (w < 0) break;
      len += w;
    }
    for (int i = 0; i < s_nwrow && len > 0; ++i)
      for (int k = 0; k < WK_COUNT; ++k) {
        if (!s_wrow[i].n[k]) continue;
        const int w = _snprintf_s(line + len, sizeof line - len, _TRUNCATE, " %s/%s=%u",
                                  s_wrow[i].name ? s_wrow[i].name : "?", weap_name(k), s_wrow[i].n[k]);
        if (w < 0) break;
        len += w;
      }
    np_log("%s", line);
  }
  // Per class: how often it was the blocker that refused a target (cumulative; sum == blockedSum).
  if (s_blockedSum != s_blockedLogged) {
    s_blockedLogged = s_blockedSum;
    char line[512];
    int len = _snprintf_s(line, sizeof line, _TRUNCATE, "aim: refused by class (sum %u):", s_blockedSum);
    for (int i = 0; i < s_nvtf && len > 0; ++i) {
      if (!s_vtf[i].blocked) continue;
      const int k = _snprintf_s(line + len, sizeof line - len, _TRUNCATE, " %s=%u", s_vtf[i].name, s_vtf[i].blocked);
      if (k < 0) break;
      len += k;
    }
    np_log("%s", line);
  }
  // rays == enemy + world + other + none + offscreen; fires == presses + blocked + unbound + gaps (+ one pending);
  // casts == traced + skipped + pending; traced == agree + passed + killed + behind + noHit; pcPresses == pcCharged +
  // pcNoCharge, pcReleases == pcShots + pcStuck (each + one pending). Every other counter: docs/re/aim.md.
  if (s_tbFrames || s_tbArmed)
    np_log("aim: triggerbot armed=%d channel=%d frames=%u holds=%u fires=%u presses=%u blocked=%u unbound=%u weapon=%u gaps=%u "
           "semiPresses=%u pressShot=%u pressReloadDry=%u pressNoShot=%u pressUnread=%u latchUnread=%u "
           "shotHooked=%d shotTraced=%u shotSkipped=%u shotAgree=%u shotPassed=%u shotKilled=%u shotBehind=%u "
           "shotNoHit=%u shotUnhooked=%u replayRays=%u replayUs=%.0f "
           "rays=%u enemy=%u world=%u other=%u none=%u offscreen=%u when=%d delay=%d botShots=%u dukeShots=%u "
           "pcPresses=%u pcCharged=%u pcNoCharge=%u pcReleases=%u pcShots=%u pcStuck=%u pcUnread=%u pcCadence=%u "
           "x3000Refused=%u x3000Unread=%u x3000Scan=%u x3000Stale=%u x3000Offscreen=%u x3000NoCtx=%u drawArmed=%d "
           "drawRecords=%u drawFull=%u",
           s_tbArmed, s_channel, s_tbFrames, s_tbHolds, s_tbFires, s_tbPresses, s_tbBlocked, s_tbUnbound, s_tbWeapon, s_tbGaps,
           s_semiPresses, s_pressOut[TBOT_PRESS_SHOT], s_pressOut[TBOT_PRESS_RELOAD_DRY], s_pressOut[TBOT_PRESS_NOSHOT],
           s_pressOut[TBOT_PRESS_UNREAD], s_latchUnread, offscreen_armed(), s_shotTraced, s_shotSkipped,
           s_shotVerdict[OFFS_SHOT_AGREE], s_shotVerdict[OFFS_SHOT_PASSED], s_shotVerdict[OFFS_SHOT_KILLED],
           s_shotVerdict[OFFS_SHOT_BEHIND], s_shotVerdict[OFFS_SHOT_NOHIT], s_shotUnhooked, s_replayRays, s_replayUs,
           s_rays, s_rayEnemy,
           s_rayWorld, s_rayOther, s_rayNone, s_rayOffscreen, g_cfg.tbotWhen, g_cfg.tbotDelay, s_botShots,
           s_manualShots, s_pcPresses, s_pcCharged, s_pcNoCharge, s_pcReleases, s_pcShots, s_pcStuck, s_pcUnread,
           s_pcCadence, s_tbX3000, s_tbX3000Unread, s_tbX3000Scan, s_tbX3000Stale, s_tbX3000Offscreen, s_tbX3000NoCtx,
           s_drawArmed, s_drawRecords, s_drawn.full);
}

void aim_frame(const unsigned char* player, int W, int H) {
  // Every frame, before any return: the world pass just drawn recorded s_drawPass, the next records the next one.
  s_x3kPass = s_drawPass++;
  s_x3kFreecamPrev = s_x3kFreecamNow;
  s_x3kFreecamNow = input_freecam_on() ? 1 : 0;
  aim_log();
  memset(&s_rc, 0, sizeof s_rc);   // a region freed since last frame is never trusted
  wimm_esp_frame(player);          // before the ESP draws, whatever Silent aim and the Triggerbot
  offscreen_frame(W, H);
  s_lock.have = 0;   // a lock lives one frame: the detour never aims at last level's entity
  s_fireNext = 0;
  if (player && (mouseaim_on() || firstperson_active())) arm();   // it also carries mouse aim's and first person's shots
  if (!player || (!g_cfg.silentOn && !g_cfg.tbotOn)) {
    s_tb = TbotState();
    s_tbShadow = TbotState();
    s_withheldPrev = 0;
    s_tbHaveMs = 0;
    s_pulseLast = 0;
    s_pressedPrev = 0;
    s_pcPrevPress = 0; s_pcPrevRelease = 0;
    s_shotsPrimed = 0;   // shots made while the triggerbot is off are nobody's
    return;
  }
  offscreen_arm();   // before the filters read offscreen_active
  {
    const void* wvt;
    const unsigned char* w = weapons_held(&wvt, &s_kind);
    s_ngmemo = 0;
    s_beamLen = 0.f;
    if (s_kind == WK_GRAY) {
      s_gloppFrames++;
      if (w) s_beamLen = glopp_beam_len(w);
      if (!(s_beamLen > 0.f)) s_beamUnread++;
    }
  }
  Ctx cx;
  const bool haveCtx = frame_ctx(player, W, H, &cx);
  if (haveCtx) trace_shots(player, cx.world);
  s_probeCx = haveCtx ? &cx : 0;
  overkill_frame(player, haveCtx);   // before the pick and the Triggerbot read its verdicts
  if (g_cfg.silentOn) {
    arm();
    // A refused or not-yet-armed hook selects nothing, so nothing else acts on a lock.
    if (s_target && haveCtx) select_target(cx);
  }
  if (g_cfg.tbotOn) tbot_frame(player, haveCtx ? &cx : 0);
  else {
    s_tb = TbotState(); s_tbShadow = TbotState(); s_withheldPrev = 0;
    s_tbHaveMs = 0; s_pulseLast = 0; s_pressedPrev = 0; s_shotsPrimed = 0;
    s_pcPrevPress = 0; s_pcPrevRelease = 0;
  }
  s_probeCx = 0;   // cx dies with this call
}

// AA's calibration marker (its esp.cpp aim_draw_marker): hollow at the raw point, filled at the led one.
void aim_draw(int W, int H) {
  if (!aim_marker_on(g_cfg.silentOn, 0, g_cfg.aimMarker) || !s_lock.have) return;
  Mat4 view, proj;
  float nearZ;
  if (!view_get(&view, &proj, &nearZ)) return;
  const ScreenPt r = w2s_project(s_lock.raw, view, proj, nearZ, W, H);
  const ScreenPt l = w2s_project(s_lock.led, view, proj, nearZ, W, H);
  if (!r.ok && !l.ok) return;
  const int half = AIM_MARKER_HALF, t = AIM_MARKER_T, span = 2 * half + 1;
  canvas_begin(W, H);
  if (r.ok) {
    const int cx = clamp_double_to_int((double)r.x + 0.5), cy = clamp_double_to_int((double)r.y + 0.5);
    const int bx = cx - half, by = cy - half;
    canvas_rect(bx, by, span, t, C_AIM);
    canvas_rect(bx, cy + half + 1 - t, span, t, C_AIM);
    canvas_rect(bx, by, t, span, C_AIM);
    canvas_rect(cx + half + 1 - t, by, t, span, C_AIM);
  }
  if (l.ok) {
    const int cx = clamp_double_to_int((double)l.x + 0.5), cy = clamp_double_to_int((double)l.y + 0.5);
    canvas_rect(cx - half, cy - half, span, span, C_AIM);
    esp_draw_label(esp_label_x(cx, 0, canvas_text_width(AIM_MARKER_LABEL, FONT_SMALL)), cy - half - ESP_LABEL_GAP,
                   AIM_MARKER_LABEL, C_AIM, FONT_SMALL);
  }
  canvas_end();
}
