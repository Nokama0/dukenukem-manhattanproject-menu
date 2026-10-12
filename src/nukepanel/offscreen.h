#pragma once
// Off-screen hits: the pure rules for skipping the bullet's view-side clip on Duke's own shots.
// offscreen.cpp owns the scan, the detour and the counters. Every address: re/offscreen-and-track.md A.
#include <stddef.h>

// game_clip_ray_to_view_sides duke!0x100e8540 (A3): `sub esp,0x124; push esi; push edi; push 0;
// call [abs]; mov edi,[abs]; add esp,4`. Unique in duke_base's text (Ghidra findBytes, 2026-09-29).
// The first 10 bytes are relocation-free and are all the hook compares; 6 (sub esp,imm32) move.
enum { OFFS_CLIP_FP_LEN = 25, OFFS_CLIP_HOOK_FP_LEN = 10, OFFS_CLIP_TRAMP_LEN = 6 };
static const unsigned char kOffsClipFp[OFFS_CLIP_FP_LEN] = {
  0x81, 0xEC, 0x24, 0x01, 0x00, 0x00, 0x56, 0x57, 0x6A, 0x00, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,
  0x8B, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x04 };
static const unsigned char kOffsClipMask[OFFS_CLIP_FP_LEN] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 1 };

// The bullet ray cast's call, proj_bullet_hitscan_ray duke!0x1003aa92 (A3): `mov ecx,[game_globals];
// push 5000.0f; push edi; push ebx; call rel32`. Unique (Ghidra, 2026-09-29); the voice-line caller at
// 0x1007b799 pushes other registers. The call's rel32 must land on the clip found above.
enum { OFFS_SITE_FP_LEN = 14, OFFS_SITE_CALL = 13, OFFS_SITE_RET = 18 };
static const unsigned char kOffsSiteFp[OFFS_SITE_FP_LEN] = {
  0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x68, 0x00, 0x40, 0x9C, 0x45, 0x57, 0x53, 0xE8 };
static const unsigned char kOffsSiteMask[OFFS_SITE_FP_LEN] = { 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 };

// `from` is the bullet's +0xBF0 (EBX at 0x1003aa15); the attacker is its +0xB30, written only by
// proj_set_attacker 0x10039980 (A2).
const unsigned OFFS_BULLET_FROM = 0xBF0;
const unsigned OFFS_BULLET_ATTACKER = 0xB30;

// Where [b, b+n) holds pat under mask (1 = compare). Counts at most 2: a second match is a refusal.
inline int offs_find(const unsigned char* b, size_t n, const unsigned char* pat, const unsigned char* mask,
                     size_t m, size_t* at) {
  int hits = 0;
  if (!b || m == 0 || n < m) return 0;
  for (size_t i = 0; i + m <= n; ++i) {
    if (mask[0] && b[i] != pat[0]) continue;
    size_t k = 1;
    for (; k < m; ++k) if (mask[k] && b[i + k] != pat[k]) break;
    if (k < m) continue;
    if (hits == 0 && at) *at = i;
    if (++hits >= 2) break;
  }
  return hits;
}

// The call at `site` (its first byte at address siteAddr): the address it calls and the return address.
inline unsigned offs_site_target(const unsigned char* site, unsigned siteAddr) {
  const unsigned rel = (unsigned)site[OFFS_SITE_CALL + 1] | ((unsigned)site[OFFS_SITE_CALL + 2] << 8) |
                       ((unsigned)site[OFFS_SITE_CALL + 3] << 16) | ((unsigned)site[OFFS_SITE_CALL + 4] << 24);
  return siteAddr + OFFS_SITE_RET + rel;
}
inline unsigned offs_site_ret(unsigned siteAddr) { return siteAddr + OFFS_SITE_RET; }

// One outcome per clip call: calls == the sum of all five.
enum { OFFS_BYPASS = 0, OFFS_OTHER_CALLER, OFFS_OFF, OFFS_NOT_MINE, OFFS_UNARMED, OFFS_REASON_COUNT };
// Unclipped only for the bullet cast's return address, Duke's own bullet, with Silent aim or the
// Triggerbot on and the hook armed; everything else runs the original.
inline int offs_reason(int isBulletCast, int attackerIsLocal, int silentOn, int tbotOn, int armed) {
  if (!armed) return OFFS_UNARMED;
  if (!isBulletCast) return OFFS_OTHER_CALLER;
  if (!silentOn && !tbotOn) return OFFS_OFF;
  if (!attackerIsLocal) return OFFS_NOT_MINE;
  return OFFS_BYPASS;
}

// A replayed shot (the bullet's own filter, cast a frame later) against the victim its cast damaged:
// PASSED -- the replay's first object lies in front of where the victim was hit (by more than the margin),
// so the real bullet went through it; KILLED/BEHIND prove nothing (a dead or moved victim left the ray).
enum { OFFS_SHOT_AGREE = 0, OFFS_SHOT_PASSED, OFFS_SHOT_KILLED, OFFS_SHOT_BEHIND, OFFS_SHOT_NOHIT, OFFS_SHOT_KINDS };
const float OFFS_SHOT_MARGIN = 32.f;   // world units: a box centre lies this deep behind its front face
inline int offs_shot_verdict(int hasVictim, int firstIsVictim, int killed, float firstDist, float victimDist) {
  if (!hasVictim) return OFFS_SHOT_NOHIT;
  if (firstIsVictim) return OFFS_SHOT_AGREE;
  if (killed) return OFFS_SHOT_KILLED;
  return firstDist < victimDist - OFFS_SHOT_MARGIN ? OFFS_SHOT_PASSED : OFFS_SHOT_BEHIND;
}

// The bypass and the aim's dropped on-screen filter need the hook armed and the Off-screen targets switch
// on; off, the clip is stock and the filters judge the game's own screen.
inline int offs_active(int armed, int switchOn) { return armed && switchOn ? 1 : 0; }

// --- offscreen.cpp (DLL only; read, not run, by the suite) ------------------------------------
// Frame path, main thread: arms the detour the first time Silent aim or the Triggerbot is on.
void offscreen_arm(void);
// 1 once the detour is installed.
int offscreen_armed(void);
int offscreen_refused(void);
// pat under mask (offs_find) over duke_base's executable sections: the match count (at most 2, a second is a
// refusal) with *out the first, or -1 when the image is not readable yet. Main thread.
int offs_scan_duke(const unsigned char* pat, const unsigned char* mask, size_t m, const unsigned char** out);
// offs_active(armed, Off-screen targets): the aim filters then drop their on-screen test.
int offscreen_active(void);
// Frame path, every frame: settles a pending kill (off screen or not) and writes the 5 s counter line.
void offscreen_frame(int W, int H);
// dmgnum's hook, after a kill by Duke: the victim's class and box centre at kill time.
void offscreen_note_kill(const char* name, const float at[3]);
// Measurement: each Duke bullet cast the detour sees (ring of 16, by sequence number), the length it used, and the
// first monster Duke's damage hit in that cast (dmgnum, whatever the Damage numbers switch), its box centre at the
// hit and whether it killed. frame: offscreen_frame's counter when the cast ran.
struct OffsShot { unsigned seq, frame; float from[3], dir[3], len; const void* victim; float victimAt[3]; int killed; };
enum { OFFS_SHOT_RING = 16 };
// A monster's damage is a net message (monster_damage_send duke!0x1002f3f0 sends 0x82; the receive
// FUN_10031990 calls vt+0x2FC at 0x10031b17), applied after the cast returns: replaying at the next frame
// found no victim in 47 of 47 casts. A cast is replayed, and closed, this many frames later.
enum { OFFS_SHOT_DELAY = 3 };
inline int offs_shot_due(unsigned castFrame, unsigned nowFrame) { return nowFrame - castFrame >= (unsigned)OFFS_SHOT_DELAY; }
// Which ring slot a hit belongs to, or -1. The damage's dir is the shot's (end - from) times the frame time
// (proj_bullet_damage_hit 0x1003ad90), so: the oldest open cast (not yet due) without a victim along that
// direction; with no direction, the newest open one.
inline int offs_hit_slot(const OffsShot* ring, unsigned lastSeq, unsigned nowFrame, const float d[3]) {
  const float l2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
  int best = -1;
  for (unsigned k = 0; k < (unsigned)OFFS_SHOT_RING && k < lastSeq; ++k) {
    const unsigned seq = lastSeq - k;
    const OffsShot& r = ring[seq % OFFS_SHOT_RING];
    if (r.seq != seq) continue;
    if (offs_shot_due(r.frame, nowFrame)) break;   // older casts are closed too
    if (r.victim) continue;
    if (!(l2 > 1e-12f)) return (int)(seq % OFFS_SHOT_RING);
    const float dot = r.dir[0] * d[0] + r.dir[1] * d[1] + r.dir[2] * d[2];
    const float rl2 = r.dir[0] * r.dir[0] + r.dir[1] * r.dir[1] + r.dir[2] * r.dir[2];
    if (dot > 0.f && dot * dot >= 0.998f * rl2 * l2) best = (int)(seq % OFFS_SHOT_RING);
  }
  return best;
}
unsigned offscreen_shot_seq(void);   // the last cast's number, 0 before any
unsigned offscreen_frame_no(void);   // counts offscreen_frame calls: OffsShot.frame's clock
int offscreen_shot(unsigned seq, OffsShot* out);
// dmgnum's hook, for a hit by Duke: dir is the damage's own direction (offs_hit_slot).
void offscreen_note_hit(const void* victim, const float at[3], int killed, const float dir[3]);
