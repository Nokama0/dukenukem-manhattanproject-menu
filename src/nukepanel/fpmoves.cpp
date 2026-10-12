// First person extras (fpmoves.h). Main thread only: the frame path and the tick, where the detour runs, share
// the engine's one cooperative loop (render.md section 1).
#include "fpmoves.h"
#include "firstperson.h"   // firstperson_active
#include "engine.h"
#include "entities.h"      // entities_in_duke
#include "weapons.h"       // weapons_held: the pipebomb
#include "hook.h"
#include "input.h"
#include "config.h"
#include "log.h"
#include "cvar_read.h"
#include <stdlib.h>
#include "ui/bind.h"       // bind_key_is_live
#include <windows.h>
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

static const unsigned kSlotRatio = 0x1EC / 4, kOfsStates = 0xB80, kStateFps = 17, kSlotPretick = 0x0C / 4;
static const unsigned kOfsRatio = 0xAFC, kOfsHalf = 0x2B8;

static const unsigned char* s_player;
static int s_fpActive;

// A key of ours is down for the game: bound and live, this process in front, no panel, console or free camera.
bool fpmoves_key_down(int vk) {
  if (!vk || !bind_key_is_live(vk)) return false;
  if (input_panel_open() || input_console_open() || input_freecam_on()) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  if (pid != GetCurrentProcessId()) return false;
  return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// --- Crouch -----------------------------------------------------------------------------------------------------
typedef void (__thiscall* RatioFn)(void* self, float ratio);
static RatioFn s_ratioTramp;
static const void* s_standRet;   // the pre-tick's stand call's return address
static int s_crouchArmed, s_crouchRefused, s_crouchHeld, s_crouchLast;
static unsigned s_crouchCalls, s_crouchSet;
static float s_eyeDrop;
static LARGE_INTEGER s_easeAt;   // the last eased frame; zero after first person was inactive

// thiscall seen as fastcall: ECX = this, EDX unused, the float popped by RET 4 either way.
static void __fastcall ratio_detour(void* self, void* /*edx*/, float ratio) {
  const int mine = self == (const void*)s_player && s_player;
  const int fromStand = _ReturnAddress() == s_standRet;
  const float r = fp_crouch_arg(mine, fromStand, s_fpActive, s_crouchHeld, ratio);
  if (mine && fromStand) { s_crouchCalls++; if (r != ratio) s_crouchSet++; }
  s_ratioTramp(self, r);
}

// actor_set_height_ratio from the player's vtable, the stand call from state 17's pre-tick; both fingerprinted.
static void arm_crouch(const unsigned char* player) {
  if (s_crouchArmed || s_crouchRefused) return;
  const char* why = 0;
  const void* const* vt;
  memcpy(&vt, player, sizeof vt);
  unsigned char* fn = entities_in_duke(vt + kSlotRatio) ? (unsigned char*)vt[kSlotRatio] : 0;
  if (!fn || !entities_in_duke(fn) || !guarded_readable(fn, FP_RATIO_FP_LEN) || !fp_fp_is_ratio(fn))
    why = "player vt+0x1EC is not actor_set_height_ratio";
  const unsigned char* pre = 0;
  if (!why) {
    const unsigned char* st = 0;
    memcpy(&st, player + kOfsStates + 4 * kStateFps, sizeof st);
    const void* const* svt = 0;
    if (st && guarded_readable(st, 4)) memcpy(&svt, st, sizeof svt);
    pre = svt && entities_in_duke(svt + kSlotPretick) ? (const unsigned char*)svt[kSlotPretick] : 0;
    const int off = pre && entities_in_duke(pre) && guarded_readable(pre, FP_PRETICK_SCAN)
                        ? fp_find_stand_call(pre, FP_PRETICK_SCAN) : -1;
    if (off < 0) why = "the stand call was not found in state 17's pre-tick";
    else s_standRet = pre + off;
  }
  if (!why) {
    unsigned char fp[FP_RATIO_FP_LEN];
    memcpy(fp, fn, sizeof fp);
    if (!hook_install_at(fn, fp, sizeof fp, FP_RATIO_PROLOGUE, (void*)ratio_detour, (void**)&s_ratioTramp))
      why = "install refused";
  }
  if (why) {
    s_crouchRefused = 1;
    np_log("fp: Crouch key REFUSED: %s", why);
    return;
  }
  s_crouchArmed = 1;
  np_log("fp: crouch armed: actor_set_height_ratio %p, stand call returns to %p (pre-tick %p)", (void*)fn, s_standRet,
         (const void*)pre);
}

float fpmoves_eye_drop(void) { return s_fpActive ? s_eyeDrop : 0.f; }

static void crouch_frame(const unsigned char* player) {
  s_crouchHeld = s_crouchArmed && fpmoves_key_down(g_cfg.crouchKey) ? 1 : 0;
  float ratio, half;
  memcpy(&ratio, player + kOfsRatio, sizeof ratio);
  memcpy(&half, player + kOfsHalf + 4, sizeof half);
  // The engine's own ratio, eased: a blocked stand keeps the eye low.
  LARGE_INTEGER t, f;
  QueryPerformanceCounter(&t);
  QueryPerformanceFrequency(&f);
  const float dtMs = s_easeAt.QuadPart ? (float)((double)(t.QuadPart - s_easeAt.QuadPart) * 1000.0 / (double)f.QuadPart)
                                       : 1e6f;
  s_easeAt = t;
  s_eyeDrop = fp_ease_drop(s_eyeDrop, fp_crouch_eye_drop(ratio, half), dtMs);
  if (s_crouchHeld != s_crouchLast) {
    s_crouchLast = s_crouchHeld;
    np_log("fp: crouch %s (ratio %.2f, eye drop %.1f; stand calls %u, crouched %u)", s_crouchHeld ? "held" : "released",
           ratio, s_eyeDrop, s_crouchCalls, s_crouchSet);
  }
}

// --- Interact -----------------------------------------------------------------------------------------------------
typedef void (__thiscall* SetStateFn)(void* self, int state);
typedef void (__thiscall* ScanFn)(void* state, const unsigned char* player);
static SetStateFn s_setStateTramp;
static const void* s_switchRet;    // player_tick's g_fps switch call's return address
static ScanFn s_scan;
static void* s_scanState;          // state 3's object, the scan's ECX (never read)
static int s_useArmed, s_useRefused, s_useLast;
static ULONGLONG s_useAt;          // the last Interact press; 0 = none
static unsigned s_usePresses, s_holds, s_useStates;
static int s_lastHeldState = -1;

static void __fastcall setstate_detour(void* self, void* /*edx*/, int st) {
  const int fromSwitch = _ReturnAddress() == s_switchRet;
  const int mine = self == (const void*)s_player && s_player;
  // Duke leaving 17, by the g_fps switch (our ladder pause set g_fps 0) or a net message's state (the Interact key's
  // use, gate 3): a stale track is forgotten before the exit and the new state's enter, and the track guard runs
  // right after the exit's +0xB1C = 1, inside this tick (free-move.md sections 8 G2-2 and 9).
  const int leaving17 = mine && st != 17 && *(const int*)((const unsigned char*)self + 0xB7C) == 17;
  const int useHold = leaving17 && s_useAt && fp_scripted_state(st) && GetTickCount64() - s_useAt <= FP_USE_HOLD_MS;
  if (mine && s_useAt) {
    const int cur = *(const int*)((const unsigned char*)self + 0xB7C);
    const ULONGLONG since = GetTickCount64() - s_useAt;
    if (fp_hold_scripted(fromSwitch, st, cur, since)) {
      s_holds++;
      if (cur != s_lastHeldState) {
        s_lastHeldState = cur;
        s_useStates++;
        np_log("fp: interact: state %d plays (%llu ms after the press); g_fps held off", cur, since);
      }
      return;   // the switch's own call skipped: the scripted state keeps Duke this tick
    }
    if (st == 17) s_lastHeldState = -1;
  }
  if (leaving17) firstperson_leaving_17((const unsigned char*)self, useHold);
  s_setStateTramp(self, st);
  if (leaving17) firstperson_left_17((const unsigned char*)self, useHold);
}

// The unique switch call in duke_base's executable sections, from its own PE headers.
static const unsigned char* find_switch(const unsigned char** target) {
  const unsigned char* base = 0;
  unsigned size = 0;
  if (!entities_duke_image(&base, &size) || !guarded_readable(base, 0x400)) return 0;
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
  if (!guarded_readable(nt, sizeof *nt)) return 0;
  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  const unsigned nsec = nt->FileHeader.NumberOfSections;
  if (!guarded_readable(sec, nsec * sizeof *sec)) return 0;
  const unsigned char* ret = 0;
  int hits = 0;
  for (unsigned i = 0; i < nsec; ++i) {
    if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
    const unsigned va = sec[i].VirtualAddress, n = sec[i].Misc.VirtualSize;
    if (va >= size || n > size - va || !guarded_readable(base + va, n)) return 0;
    const unsigned char* p = base + va;
    int left = (int)n;
    while (left > 0) {
      int t = 0;
      const int r = fp_find_switch_call(p, left, &t);
      if (r < 0) break;
      if (++hits == 1) { ret = p + r; *target = p + t; }
      p += r; left -= r;
    }
  }
  return hits == 1 ? ret : 0;
}

static void arm_use(const unsigned char* player) {
  if (s_useArmed || s_useRefused) return;
  const char* why = 0;
  const unsigned char* st = 0;
  memcpy(&st, player + kOfsStates + 4 * 3, sizeof st);   // state 3, the walk state
  const void* const* svt = 0;
  if (st && guarded_readable(st, 4)) memcpy(&svt, st, sizeof svt);
  unsigned char* scan = svt && entities_in_duke(svt + 0x48 / 4) ? (unsigned char*)svt[0x48 / 4] : 0;
  if (!scan || !entities_in_duke(scan) || !guarded_readable(scan, FP_SCAN_FP_LEN) || !fp_fp_is_scan(scan))
    why = "state 3 vt+0x48 is not playerstate_move_touch_scan";
  const unsigned char* target = 0;
  if (!why && !(s_switchRet = find_switch(&target))) why = "player_tick's g_fps switch call not found once";
  if (!why && (!entities_in_duke(target) || !guarded_readable(target, FP_SETSTATE_FP_LEN) || !fp_fp_is_setstate(target)))
    why = "the switch's call target is not player_set_state";
  if (!why) {
    unsigned char fp[FP_SETSTATE_FP_LEN];
    memcpy(fp, target, sizeof fp);
    if (!hook_install_at((void*)target, fp, sizeof fp, FP_SETSTATE_PROLOGUE, (void*)setstate_detour,
                         (void**)&s_setStateTramp))
      why = "install refused";
  }
  if (!why && !guarded_writable(player + 0xE00, 8)) why = "player +0xE00 not writable";
  if (why) {
    s_useRefused = 1;
    np_log("fp: Interact key REFUSED: %s", why);
    return;
  }
  s_scan = (ScanFn)scan;
  s_scanState = (void*)st;
  s_useArmed = 1;
  np_log("fp: interact armed: touch scan %p (state 3 %p), player_set_state %p, switch returns to %p", (void*)scan,
         (const void*)st, (const void*)target, s_switchRet);
}

// One run of the touch scan with an Up edge, the masks put back after: the fields the mask reader rewrites every tick.
static void use_now(const unsigned char* player) {
  unsigned char* p = const_cast<unsigned char*>(player);
  unsigned e00, e04, n00, n04;
  memcpy(&e00, p + 0xE00, 4);
  memcpy(&e04, p + 0xE04, 4);
  fp_use_masks(e00, e04, &n00, &n04);
  memcpy(p + 0xE00, &n00, 4);
  memcpy(p + 0xE04, &n04, 4);
  const int before = *(const int*)(player + 0xB7C);
  s_useAt = GetTickCount64();
  s_scan(s_scanState, player);
  memcpy(p + 0xE00, &e00, 4);
  memcpy(p + 0xE04, &e04, 4);
  s_usePresses++;
  np_log("fp: interact #%u: touch scan run (state %d -> %d, +0xBDC %p); holds so far %u, scripted states %u",
         s_usePresses, before, *(const int*)(player + 0xB7C), *(void* const*)(player + 0xBDC), s_holds, s_useStates);
}

static void interact_frame(const unsigned char* player) {
  if (fp_key_edge(fpmoves_key_down(g_cfg.interactKey) ? 1 : 0, &s_useLast)) {
    arm_use(player);
    if (s_useArmed) use_now(player);
  }
}

// --- Air jump -----------------------------------------------------------------------------------------------------
typedef void (__thiscall* AddVelFn)(void* self, const float* v);
static AddVelFn s_addVel;
static int s_jumpArmed, s_jumpRefused, s_prevMask = -1;
static cvar_t* s_cvJump;
static cvar_t* s_cvBoost;
static cvar_t* s_cvSweet;
static float s_g = FPJ_G_DEFAULT, s_vyPrev;
static double s_tPrev;
static int s_airPrev;
static unsigned s_presses, s_boosts, s_refused[FPJ_TAKEOFF + 1];

static float cvar_num(cvar_t* cv, float fallback) {
  char t[64];
  return cv && cvar_read_string(cv, t, sizeof t) ? (float)atof(t) : fallback;
}

static void arm_jump(const unsigned char* player) {
  if (s_jumpArmed || s_jumpRefused) return;
  const char* why = 0;
  const void* const* vt;
  memcpy(&vt, player, sizeof vt);
  unsigned char* fn = entities_in_duke(vt + 0x200 / 4) ? (unsigned char*)vt[0x200 / 4] : 0;
  if (!fn || !entities_in_duke(fn) || !guarded_readable(fn, FPJ_ADDVEL_FP_LEN) || !fp_fp_is_addvel(fn))
    why = "player vt+0x200 is not the velocity add";
  else if (!g_eng.cvar_get || !(s_cvJump = g_eng.cvar_get("g_p_jump")) || !(s_cvBoost = g_eng.cvar_get("g_p_boost")) ||
           !(s_cvSweet = g_eng.cvar_get("g_p_boost_sweet")))
    why = "g_p_jump, g_p_boost or g_p_boost_sweet unresolved";
  else if (!guarded_writable(player + 0xE14, 4)) why = "player +0xE14 not writable";
  if (why) {
    s_jumpRefused = 1;
    np_log("fp: air jump REFUSED: %s", why);
    return;
  }
  s_addVel = (AddVelFn)fn;
  s_jumpArmed = 1;
  np_log("fp: air jump armed: velocity add %p; g_p_jump %.1f g_p_boost %.1f g_p_boost_sweet %.1f", (void*)fn,
         cvar_num(s_cvJump, 0.f), cvar_num(s_cvBoost, 0.f), cvar_num(s_cvSweet, 0.f));
}

static const char* const kFpjName[] = { "none", "BOOST", "REFUSED(gate1)", "REFUSED(gate2)", "REFUSED(gate3)",
                                        "takeoff" };

// After the tick: an air press (jump bit 4's edge while not grounded) runs the walk state's boost rule.
static void jump_frame(const unsigned char* player) {
  unsigned mask;
  memcpy(&mask, player + 0xE00, sizeof mask);
  const int edge = (mask & 4u) && s_prevMask >= 0 && !((unsigned)s_prevMask & 4u);
  s_prevMask = (int)mask;
  const int air = player[0xB00] == 0;
  float vy;
  memcpy(&vy, player + 0x1C4, sizeof vy);   // own velocity y, as the boost reads it (0x10085bd9)
  if (!air && !edge) { s_airPrev = 0; return; }
  const unsigned char* world = 0;
  memcpy(&world, player + 0xE0, sizeof world);
  if (!world || !guarded_readable(world + 0x40, 8)) { s_airPrev = 0; return; }
  double now;
  memcpy(&now, world + 0x40, sizeof now);
  if (air && s_airPrev) {
    float g;
    if (fpj_measure_g(s_vyPrev, vy, now - s_tPrev, &g)) s_g = g;
  }
  s_airPrev = air; s_vyPrev = vy; s_tPrev = now;
  if (!edge || !air) return;
  arm_jump(player);
  if (!s_jumpArmed) return;
  float e10, e14, e70, e64, y;
  memcpy(&e10, player + 0xE10, 4); memcpy(&e14, player + 0xE14, 4); memcpy(&e70, player + 0xE70, 4);
  memcpy(&e64, player + 0xE64, 4); memcpy(&y, player + 0x190, 4);
  const int r = fpj_press(now, e10, e14, y, e70);
  if (r == FPJ_TAKEOFF) return;   // the ground jump's own press
  s_presses++;
  if (r == FPJ_BOOST) {
    const float jump = cvar_num(s_cvJump, 0.f), boost = cvar_num(s_cvBoost, 0.f), sweet = cvar_num(s_cvSweet, 0.f);
    const float v = fpj_scaled(fpj_velocity(vy, e64, jump, boost, sweet, s_g, y - e70), g_cfg.fpJumpPct);
    const float cancel[3] = { 0.f, -vy, 0.f }, up[3] = { 0.f, v, 0.f };
    s_addVel((void*)player, cancel);   // as the engine: cancel the fall, then the boost (0x10085bfe, 0x10085d18)
    s_addVel((void*)player, up);
    s_boosts++;
    np_log("fp: air jump BOOST #%u (%s, %d%%): vy %.1f -> %.1f, dy %.1f, g %.0f, %.3f s after the jump", s_boosts,
           fpj_sweet(vy, e64, sweet) ? "sweet" : "boost", g_cfg.fpJumpPct, vy, v, y - e70, s_g, now - e10);
  } else {
    s_refused[r]++;
    np_log("fp: air jump %s: since jump %.3f s, armed %d, y %.1f vs start %.1f", kFpjName[r], now - e10,
           e14 < e10 ? 1 : 0, y, e70);
  }
  const float stamp = (float)now;   // the engine stamps E14 on every air press, pass or fail (0x10085d29-0x10085d32)
  memcpy(const_cast<unsigned char*>(player) + 0xE14, &stamp, 4);
}

// --- Body ---------------------------------------------------------------------------------------------------------
typedef void (__thiscall* DrawFn)(void* self, int arg);
static DrawFn s_drawTramp;
static const unsigned char* s_hidePlayer;
static int s_hide, s_drawArmed, s_drawRefused;
static unsigned s_hidden;

static void __fastcall draw_detour(void* self, void* /*edx*/, int arg) {
  if (s_hide && self == (const void*)s_hidePlayer && s_hidePlayer) { s_hidden++; return; }   // as state 17 does
  s_drawTramp(self, arg);
}

void fpmoves_hide_body(const unsigned char* player, int hide) {
  s_hidePlayer = player;
  s_hide = hide && player ? 1 : 0;
  if (!s_hide || s_drawArmed || s_drawRefused) return;
  const void* const* vt;
  memcpy(&vt, player, sizeof vt);
  unsigned char* fn = entities_in_duke(vt + 0xB4 / 4) ? (unsigned char*)vt[0xB4 / 4] : 0;
  const char* why = 0;
  if (!fn || !entities_in_duke(fn) || !guarded_readable(fn, FP_DRAW_FP_LEN) || !fp_fp_is_draw(fn))
    why = "player vt+0xB4 is not player_draw";
  if (!why) {
    unsigned char fp[FP_DRAW_FP_LEN];
    memcpy(fp, fn, sizeof fp);
    if (!hook_install_at(fn, fp, sizeof fp, FP_DRAW_PROLOGUE, (void*)draw_detour, (void**)&s_drawTramp))
      why = "install refused";
  }
  if (why) {
    s_drawRefused = 1;
    s_hide = 0;
    np_log("fp: body hiding REFUSED: %s (the ladder view shows the body)", why);
    return;
  }
  s_drawArmed = 1;
  np_log("fp: body hiding armed: player_draw %p", (void*)fn);
}

// --- Pipebomb throw -------------------------------------------------------------------------------------------------
static const unsigned kOfsUpper = 0xC18;
static int s_upperOurs, s_upperWas;
static const unsigned char* s_upperPlayer;
static unsigned s_upperWrites, s_upperRestores;

static void pbomb_frame(const unsigned char* player, int fpActive) {
  if (player != s_upperPlayer) { s_upperOurs = 0; s_upperPlayer = player; }   // a new player: nothing of ours
  if (!player) return;
  // Out of 17 the state's own code writes +0xC18 (the walk state on Up): ours is dropped, not restored stale.
  if (!fpActive) { s_upperOurs = 0; return; }
  int kind = -1;
  const void* vt = 0;
  weapons_held(&vt, &kind);
  const int want = fp_pbomb_want(fpActive, kind == WK_PBOMB, s_crouchHeld);
  int cur;
  memcpy(&cur, player + kOfsUpper, sizeof cur);
  const int act = fp_pbomb_upper(want, s_upperOurs, cur);
  if (act == FP_UP_NONE) {
    if (!want) s_upperOurs = 0;
    return;
  }
  unsigned char* at = const_cast<unsigned char*>(player) + kOfsUpper;
  if (!guarded_writable(at, 4)) return;
  if (act == FP_UP_WRITE) {
    if (!s_upperOurs) s_upperWas = cur;
    memcpy(at, &FP_UPPER_THROW, sizeof FP_UPPER_THROW);   // the walk state's Up value
    s_upperOurs = 1;
    if (s_upperWrites++ < 8) np_log("fp: pipebomb strong throw on: +0xC18 %d -> 2 (writes %u)", cur, s_upperWrites);
  } else {
    memcpy(at, &s_upperWas, sizeof s_upperWas);
    s_upperOurs = 0;
    s_upperRestores++;
    np_log("fp: pipebomb strong throw off: +0xC18 2 -> %d (restores %u)", s_upperWas, s_upperRestores);
  }
}

void fpmoves_frame(const unsigned char* player) {
  s_player = player;
  s_fpActive = player && firstperson_active() ? 1 : 0;
  pbomb_frame(player, s_fpActive);   // runs inactive too: drops our write after first person
  if (!s_fpActive) {
    s_crouchHeld = 0; s_eyeDrop = 0.f; s_easeAt.QuadPart = 0; s_prevMask = -1; s_airPrev = 0;
    s_useLast = fpmoves_key_down(g_cfg.interactKey) ? 1 : 0;   // the edge is tracked while inactive too
    return;
  }
  arm_crouch(player);
  arm_use(player);   // armed with the mode, not on the first E: its player_set_state detour carries the track guard
  crouch_frame(player);
  interact_frame(player);
  jump_frame(player);
}
