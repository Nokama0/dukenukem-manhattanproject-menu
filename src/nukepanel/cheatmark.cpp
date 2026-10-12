// The cheat_cvar_check detour (cheatmark.h, save-awards.md section 4): the panel's three flagged cvars read their
// defaults for the length of the check, through cvar_t::set, and get their values back right after it. Main thread:
// the check runs inside game_proc_handler (render.md section 1), the arm on the frame path of the same loop.
#include "cheatmark.h"
#include "engine.h"
#include "entities.h"
#include "offscreen.h"   // offs_scan_duke
#include "hook.h"
#include "log.h"
#include <windows.h>
#include <string.h>

// cheat_cvar_check duke!0x100e13d0: __fastcall (ECX = the game process), no stack arguments, plain RET.
typedef void (__fastcall* CheckFn)(void* proc);

static CheckFn s_tramp;
static void* s_target;
static int s_refused, s_waitLogged;
static cvar_t* s_cv[CM_NCVARS];
static CmSlot s_slot[CM_NCVARS];

// Cumulative. held[i]: checks that ran with cvar i at its default instead of its value.
static unsigned s_checks, s_held[CM_NCVARS], s_setRefused, s_backRefused, s_layoutBad;
static unsigned s_firstLogged;   // bit i: cvar i's first hold logged with its strings
static unsigned s_loggedSum = 0xFFFFFFFFu;
static ULONGLONG s_nextLog;

static void __fastcall check_detour(void* proc) {
  s_checks++;
  for (int i = 0; i < CM_NCVARS; ++i) {
    const unsigned char* c = (const unsigned char*)s_cv[i];
    const char* value;
    const char* def;
    const void* link;
    memcpy(&value, c + CM_OFS_VALUE, sizeof value);   // the record was validated in duke_base's image at arm
    memcpy(&def, c + CM_OFS_DEFAULT, sizeof def);
    memcpy(&link, c + CM_OFS_LINK, sizeof link);
    const int ok = cm_layout_ok(c, value, def, link);
    if (!ok) { s_layoutBad++; s_slot[i].restored = 0; continue; }
    if (!cm_slot_begin(&s_slot[i], ok, value, def)) continue;
    if (!g_p3.cvar_set(s_cv[i], def)) { s_setRefused++; cm_slot_failed(&s_slot[i]); continue; }
    s_held[i]++;
    if (!(s_firstLogged & (1u << i))) {
      s_firstLogged |= 1u << i;
      np_log("cheatmark: %s \"%s\" read as its default \"%s\" during the check", kCmCvars[i], s_slot[i].saved, def);
    }
  }
  s_tramp(proc);
  for (int i = 0; i < CM_NCVARS; ++i) {
    const char* back = cm_slot_end(&s_slot[i]);
    if (back && !g_p3.cvar_set(s_cv[i], back)) s_backRefused++;
  }
}

// A static record in duke_base's image: a runtime one (uset before duke_base registers) is freed later.
static int resolve_cvars(void) {
  for (int i = 0; i < CM_NCVARS; ++i) {
    if (s_cv[i]) continue;
    cvar_t* cv = g_eng.cvar_get(kCmCvars[i]);
    const unsigned char* c = (const unsigned char*)cv;
    if (!cv || !entities_in_duke(c) || !entities_in_duke(c + CM_CVAR_SIZE - 1) || !guarded_readable(c, CM_CVAR_SIZE)) {
      if (!s_waitLogged++) np_log("cheatmark: %s is %p, not yet duke_base's own record; waiting", kCmCvars[i], (void*)cv);
      return 0;
    }
    s_cv[i] = cv;
  }
  return 1;
}

static void arm(void) {
  if (s_target || s_refused) return;
  const unsigned char* base = 0;
  unsigned size = 0;
  if (!entities_duke_image(&base, &size) || !g_eng.cvar_get) return;
  if (!engine_resolve_cvar_set()) return;   // retried each frame; engine.cpp logs a missing export once
  if (!resolve_cvars()) return;
  const unsigned char* at = 0;
  const int n = offs_scan_duke(kCmCheckFp, kCmCheckMask, CM_FP_LEN, &at);
  if (n < 0) return;   // the image is not readable yet
  if (n != 1) { s_refused = 1; np_log("cheatmark: cheat_cvar_check matched %d time(s) (need 1); not armed", n); return; }
  unsigned char live[CM_FP_LEN];
  memcpy(live, at, CM_FP_LEN);   // offs_scan_duke read it; the relocated operands are this image's own
  if (!hook_install_at((void*)at, live, CM_FP_LEN, CM_PROLOGUE, (void*)check_detour, (void**)&s_tramp)) {
    s_refused = 1;
    np_log("cheatmark: install at %p refused; the check sees the panel's cvars", (const void*)at);
    return;
  }
  s_target = (void*)at;
  np_log("cheatmark: armed at %p (duke_base+%X); g_fps %p, g_cam_fov %p, g_cam_far %p", s_target,
         (unsigned)(at - base), (void*)s_cv[0], (void*)s_cv[1], (void*)s_cv[2]);
}

void cheatmark_frame(void) {
  arm();
  if (!s_target) return;
  const ULONGLONG now = GetTickCount64();
  if (now < s_nextLog) return;
  s_nextLog = now + 5000;
  const unsigned sum = s_held[0] + s_held[1] + s_held[2] + s_setRefused + s_backRefused + s_layoutBad;
  if (sum == s_loggedSum) return;   // a new line only when a hold or a fault was counted
  s_loggedSum = sum;
  np_log("cheatmark: checks %u, held at default: g_fps %u, g_cam_fov %u, g_cam_far %u; set refused %u, "
         "put back refused %u, layout refused %u", s_checks, s_held[0], s_held[1], s_held[2], s_setRefused,
         s_backRefused, s_layoutBad);
}
