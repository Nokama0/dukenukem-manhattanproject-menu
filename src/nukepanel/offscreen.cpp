// Off-screen hits: skips game_clip_ray_to_view_sides for Duke's own bullets while Silent aim or the
// Triggerbot is on (re/offscreen-and-track.md A). offscreen.h owns the rules; this file finds, hooks
// and counts. Main thread only: the detour runs in the game tick, the rest on the frame path.
#include "offscreen.h"
#include "engine.h"
#include "entities.h"
#include "hook.h"
#include "view.h"
#include "config.h"
#include "log.h"
#include <windows.h>
#include <intrin.h>
#include <math.h>
#include <string.h>

#pragma intrinsic(_ReturnAddress)

// thiscall (from, dir, float len), RET 0xC, float in ST0 (A1): fastcall with an unused EDX is the same
// ABI. ECX (game_globals) is passed through untouched; the body never reads it.
typedef float (__fastcall* ClipFn)(void* gg, void* edx, const float* from, const float* dir, float len);

static ClipFn s_tramp;
static void* s_target;
static unsigned s_ret;   // the bullet cast's return address, derived from its fingerprinted call site
static int s_refused;
static unsigned s_calls, s_why[OFFS_REASON_COUNT];
static int s_firstLogged;
static float s_lastFrom[3];
static OffsShot s_shots[OFFS_SHOT_RING];   // Duke's bullet casts by sequence number, and the victim each damaged
static unsigned s_shotSeq, s_frameNo;
// Cumulative: Duke's hits dmgnum saw, and those no open cast matched (hits == matched + orphan).
static unsigned s_hits, s_hitsOrphan;

static void record_shot(const float* from, const float* dir, float len) {
  OffsShot& r = s_shots[++s_shotSeq % OFFS_SHOT_RING];
  r.seq = s_shotSeq; r.frame = s_frameNo; r.len = len; r.victim = 0; r.killed = 0;
  memcpy(r.from, from, sizeof r.from);
  memcpy(r.dir, dir, sizeof r.dir);
}
// A kill by Duke after a bypass, settled on the next frame (the view is captured only there).
static int s_pending, s_killLogged;
static const char* s_pendName;
static float s_pendAt[3];
static unsigned s_killsOn, s_killsOff;
static unsigned s_loggedSum = 0xFFFFFFFFu;
static ULONGLONG s_nextLog;

int offscreen_armed(void) { return s_target != 0; }
int offscreen_refused(void) { return s_refused; }
int offscreen_active(void) { return offs_active(s_target != 0, g_cfg.aimOffscreen); }

static float __fastcall clip_detour(void* gg, void* edx, const float* from, const float* dir, float len) {
  s_calls++;
  const int isBullet = (unsigned)(size_t)_ReturnAddress() == s_ret;
  // The Off-screen targets switch folds into the two switches: off, every call is stock (OFFS_OFF).
  const int on = g_cfg.aimOffscreen != 0;
  const int sil = g_cfg.silentOn && on, tb = g_cfg.tbotOn && on;
  const void* attacker = 0;
  const void* local = 0;
  int mine = 0;
  // The attacker is read only for the bullet cast with a switch on: one guarded read per Duke shot.
  // Read with either raw switch on, so the shot replay (aim.cpp) also sees stock shots.
  if (isBullet && (g_cfg.silentOn || g_cfg.tbotOn) && from) {
    const unsigned char* slot = (const unsigned char*)from - OFFS_BULLET_FROM + OFFS_BULLET_ATTACKER;
    if (guarded_readable(slot, sizeof attacker)) memcpy(&attacker, slot, sizeof attacker);
    local = g_eng.wm_get_input ? g_eng.wm_get_input(0) : 0;
    mine = attacker != 0 && attacker == local;
  }
  const int r = offs_reason(isBullet, mine, sil, tb, 1);
  s_why[r]++;
  if (r != OFFS_BYPASS) {
    const float got = s_tramp(gg, edx, from, dir, len);
    if (isBullet && mine && dir) record_shot(from, dir, got);
    return got;
  }
  if (dir) record_shot(from, dir, len);
  memcpy(s_lastFrom, from, sizeof s_lastFrom);
  if (!s_firstLogged) {
    s_firstLogged = 1;   // the attacker, wm_get_input(0) and the walk's player are one object
    np_log("offscreen: first clip skipped: attacker %p, wm_get_input(0) %p, snapshot player %p, len %.1f, "
           "from (%.1f,%.1f,%.1f) dir (%.3f,%.3f,%.3f)", attacker, local, entities_snapshot().player, len,
           from[0], from[1], from[2], dir ? dir[0] : 0.f, dir ? dir[1] : 0.f, dir ? dir[2] : 0.f);
  }
  return len;   // the original's "no side plane hit" result: the plain 5000-unit ray
}

// Every executable section of duke_base, from its own PE headers.
int offs_scan_duke(const unsigned char* pat, const unsigned char* mask, size_t m, const unsigned char** out) {
  const unsigned char* base = 0;
  unsigned size = 0;
  if (!entities_duke_image(&base, &size) || !guarded_readable(base, sizeof(IMAGE_DOS_HEADER))) return -1;
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || (unsigned)dos->e_lfanew > size) return -1;
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
  if (!guarded_readable(nt, sizeof *nt) || nt->Signature != IMAGE_NT_SIGNATURE) return -1;
  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  const unsigned nsec = nt->FileHeader.NumberOfSections;
  if (!guarded_readable(sec, nsec * sizeof *sec)) return -1;
  int hits = 0;
  for (unsigned i = 0; i < nsec && hits < 2; ++i) {
    if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
    const unsigned va = sec[i].VirtualAddress, n = sec[i].Misc.VirtualSize;
    if (va >= size || n > size - va || !guarded_readable(base + va, n)) return -1;
    size_t at = 0;
    const int h = offs_find(base + va, n, pat, mask, m, &at);
    if (h && !hits) *out = base + va + at;
    hits += h;
  }
  return hits;
}

// Armed the first time either switch is on (aim_frame), never before: off, the clip is stock.
// Armed with either switch, whatever Off-screen targets says: off, the detour passes every call through, and
// the shot measurement (aim.cpp) still sees Duke's casts.
void offscreen_arm(void) {
  if (s_target || s_refused) return;
  const unsigned char* base = 0;
  unsigned size = 0;
  if (!entities_duke_image(&base, &size)) return;   // not resolved yet: try next frame
  const unsigned char* clip = 0;
  const unsigned char* site = 0;
  const int nc = offs_scan_duke(kOffsClipFp, kOffsClipMask, OFFS_CLIP_FP_LEN, &clip);
  const int ns = offs_scan_duke(kOffsSiteFp, kOffsSiteMask, OFFS_SITE_FP_LEN, &site);
  s_refused = 1;
  if (nc != 1 || ns != 1) {
    np_log("offscreen: refused: clip fingerprint matched %d time(s), bullet call site %d (need 1 each); "
           "aim keeps the on-screen filter", nc, ns);
    return;
  }
  if (!guarded_readable(site, OFFS_SITE_RET) || offs_site_target(site, (unsigned)(size_t)site) != (unsigned)(size_t)clip) {
    np_log("offscreen: refused: the call at %p does not reach the clip %p", (const void*)site, (const void*)clip);
    return;
  }
  if (!g_eng.wm_get_input) { np_log("offscreen: refused: wm_get_input unresolved"); return; }
  s_ret = offs_site_ret((unsigned)(size_t)site);   // before the patch: the detour compares against it
  unsigned char live[OFFS_CLIP_HOOK_FP_LEN];
  memcpy(live, clip, sizeof live);   // the relocation-free head, matched by the scan above
  if (!hook_install_at((void*)clip, live, OFFS_CLIP_HOOK_FP_LEN, OFFS_CLIP_TRAMP_LEN, (void*)clip_detour,
                       (void**)&s_tramp)) {
    np_log("offscreen: install at %p refused; aim keeps the on-screen filter", (const void*)clip);
    return;
  }
  s_refused = 0;
  s_target = (void*)clip;
  np_log("offscreen: armed at %p (duke_base+%X), bullet cast returns to duke_base+%X", (const void*)clip,
         (unsigned)(clip - base), s_ret - (unsigned)(size_t)base);
}

unsigned offscreen_shot_seq(void) { return s_shotSeq; }
unsigned offscreen_frame_no(void) { return s_frameNo; }
int offscreen_shot(unsigned seq, OffsShot* out) {
  if (!seq || seq > s_shotSeq || s_shotSeq - seq >= OFFS_SHOT_RING) return 0;
  const OffsShot& r = s_shots[seq % OFFS_SHOT_RING];
  if (r.seq != seq) return 0;
  *out = r;
  return 1;
}

// A monster's damage lands through a net message after its cast (offscreen.h, OFFS_SHOT_DELAY), perhaps after
// a later cast: the hit is matched by its direction, not taken as the latest cast's.
void offscreen_note_hit(const void* victim, const float at[3], int killed, const float dir[3]) {
  s_hits++;
  const int i = offs_hit_slot(s_shots, s_shotSeq, s_frameNo, dir);
  if (i < 0) { s_hitsOrphan++; return; }
  OffsShot& r = s_shots[i];
  r.victim = victim;
  memcpy(r.victimAt, at, sizeof r.victimAt);
  r.killed = killed;
}

void offscreen_note_kill(const char* name, const float at[3]) {
  if (!s_target || s_killLogged || s_pending || !s_why[OFFS_BYPASS]) return;
  s_pending = 1;
  s_pendName = name;
  memcpy(s_pendAt, at, sizeof s_pendAt);
}

void offscreen_frame(int W, int H) {
  s_frameNo++;
  Mat4 view, proj;
  float nearZ;
  if (s_pending && view_get_stock(&view, &proj, &nearZ)) {   // the game's own camera, as the clip uses
    s_pending = 0;
    const Vec3 at = { s_pendAt[0], s_pendAt[1], s_pendAt[2] };
    const ScreenPt sp = w2s_project(at, view, proj, nearZ, W, H);
    if (!sp.ok || sp.x < 0.f || sp.x > (float)W || sp.y < 0.f || sp.y > (float)H) {
      s_killsOff++;
      s_killLogged = 1;   // an off-screen kill by a bypassed shot
      const float dx = at.x - s_lastFrom[0], dy = at.y - s_lastFrom[1], dz = at.z - s_lastFrom[2];
      np_log("offscreen: first off-screen kill: %s at (%.1f,%.1f,%.1f), %.1f from the last bypassed shot",
             s_pendName ? s_pendName : "?", at.x, at.y, at.z, sqrtf(dx * dx + dy * dy + dz * dz));
    } else {
      s_killsOn++;
    }
  }
  const ULONGLONG tick = GetTickCount64();
  if (tick < s_nextLog) return;
  s_nextLog = tick + 5000;
  const unsigned sum = s_calls + s_killsOn + s_killsOff + s_hits;
  if (!s_target || sum == s_loggedSum) return;   // cumulative: an unchanged line says nothing
  s_loggedSum = sum;
  // calls == bypassed + other + off + notmine + unarmed; hits (Duke's, from dmgnum) == matched + orphan
  np_log("offscreen: calls=%u bypassed=%u other=%u off=%u notmine=%u unarmed=%u | kills checked on=%u off=%u | "
         "hits=%u matched=%u orphan=%u",
         s_calls, s_why[OFFS_BYPASS], s_why[OFFS_OTHER_CALLER], s_why[OFFS_OFF], s_why[OFFS_NOT_MINE],
         s_why[OFFS_UNARMED], s_killsOn, s_killsOff, s_hits, s_hits - s_hitsOrphan, s_hitsOrphan);
}
