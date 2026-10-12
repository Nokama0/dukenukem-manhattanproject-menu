// Screenshot mode glue (shot.h): the HUD screen, the cutscene letterbox and the level-end stats through the
// engine's own ui_wget/ui_whide/ui_wshow exports, as the letterbox does it. Main thread only (frame path). No
// g_hud write any more; the stash from the first build's g_hud writes is still cleaned up once per launch.
#include "shot.h"
#include "engine.h"
#include "cvar_read.h"
#include "config.h"
#include "input.h"
#include "log.h"
#include "photo.h"   // photo_busy: a photo hides the HUD and overlays as this mode does
#include "offscreen.h" // offs_find: the handler fingerprints
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_active, s_hid, s_recovered, s_saidOff;
static unsigned s_hides, s_shows;

// --- The letterbox and the level-end stats (shot.h shot_win_act) ---
// Each handler is at window+0x21c (duke 0x100e7d32, end_part_activate 0x100ad290) and keeps the field we need at
// +0x1d4. Neither offset is trusted alone: the handler's vtable slot must hold the code whose bytes name +0x1d4.
static const unsigned kWinHandler = 0x21C, kHdlField = 0x1D4;
// ui_hdl_letterbox's tick duke 0x100a2e50 (vt 0x1013b010 +0x58): `push ecx/ebx/esi; mov esi,ecx; call rel32;
// mov ecx,[esi+0x1d4]; cmp ecx,2; jz; cmp ecx,3` -- its state. The call's rel32 is masked.
static const unsigned char kLbPat[24] = { 0x51, 0x53, 0x56, 0x8B, 0xF1, 0xE8, 0, 0, 0, 0, 0x8B, 0x8E,
                                          0xD4, 0x01, 0x00, 0x00, 0x83, 0xF9, 0x02, 0x74, 0x09, 0x83, 0xF9, 0x03 };
static const unsigned char kLbMask[24] = { 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
// ui_hdl_part_end vt 0x1013baf8 +0x14c = duke 0x100a9ee0, the close end_part's destroy calls: `if
// ([esi+0x1d4] == arg) { [esi+0x1d8] = 0; ui_whide([esi+0xe4]); [esi+0x1d4] = 0 }` -- +0x1d4 is the owning
// end_part entity, 0 once the page is closed (its own close 0x100a9e00 clears it too).
static const unsigned char kPePat[28] = { 0x56, 0x8B, 0xF1, 0x8B, 0x86, 0xD4, 0x01, 0x00, 0x00, 0x3B,
                                          0x44, 0x24, 0x08, 0x75, 0x24, 0x8B, 0x8E, 0xE4, 0x00, 0x00,
                                          0x00, 0x51, 0xC7, 0x86, 0xD8, 0x01, 0x00, 0x00 };
static const unsigned char kPeMask[28] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                           1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

struct SysWin {
  const char *cls, *name, *what;
  unsigned slot;
  const unsigned char *pat, *mask;
  size_t n;
  int hid, refused;
  const void* vtOk;   // the handler vtable whose slot matched once: later reads skip the code compare
  unsigned hides, shows, forgets;
};
static SysWin s_lb = { "system_03", "letterbox", "letterbox", 0x58, kLbPat, kLbMask, sizeof kLbPat, 0, 0, 0, 0, 0, 0 };
static SysWin s_pe = { "system_game2", "partend", "level-end stats", 0x14C, kPePat, kPeMask, sizeof kPePat, 0, 0, 0, 0, 0, 0 };

static bool read_u32(const void* p, unsigned* out) {
  if (!p || !guarded_readable(p, 4)) return false;
  memcpy(out, p, 4);
  return true;
}

// The handler's +0x1d4 for window w, or false when unreadable or the handler is not the read build's (then the
// window is refused for the launch, logged once).
static bool handler_field(SysWin* k, void* w, unsigned* out) {
  unsigned h = 0, vt = 0, fn = 0;
  if (!read_u32((const unsigned char*)w + kWinHandler, &h) || !h || !read_u32((const void*)(uintptr_t)h, &vt)) return false;
  if ((const void*)(uintptr_t)vt != k->vtOk) {
    size_t at = 0;
    const unsigned char* code = 0;
    const bool ok = read_u32((const unsigned char*)(uintptr_t)vt + k->slot, &fn) &&
                    (code = (const unsigned char*)(uintptr_t)fn, guarded_readable(code, k->n)) &&
                    offs_find(code, k->n, k->pat, k->mask, k->n, &at) == 1;
    if (!ok) {
      k->refused = 1;
      np_log("shot: %s handler %08X (vt %08X slot +0x%X -> %08X) differs from the read build; never hidden",
             k->what, h, vt, k->slot, fn);
      return false;
    }
    k->vtOk = (const void*)(uintptr_t)vt;
    np_log("shot: %s handler verified (vt %08X slot +0x%X -> %08X)", k->what, vt, k->slot, fn);
  }
  return read_u32((const unsigned char*)(uintptr_t)h + kHdlField, out);
}

// The letterbox state for the HUD's rule, or -2 when refused (the HUD then falls back to its visibility).
static int lb_state(void) {
  if (s_lb.refused) return -2;
  void* w = input_ui_window_any(s_lb.cls, s_lb.name);
  unsigned v = 0;
  if (!w || !handler_field(&s_lb, w, &v)) return s_lb.refused ? -2 : -1;
  return (int)v;
}

static void sys_frame(SysWin* k, int active) {
  if (k->refused || (!active && !k->hid)) return;
  void* shown = active ? input_ui_window(k->cls, k->name) : NULL;
  if (active && !shown) return;   // the common frame: no reads
  void* w = shown ? shown : input_ui_window_any(k->cls, k->name);
  unsigned v = 0;
  const bool read = w && handler_field(k, w, &v);
  if (k->refused) return;
  const int wanted = read && (k == &s_lb ? shot_lb_wanted((int)v) : v != 0);
  const int act = shot_win_act(active && read, k->hid, shown != NULL, wanted);
  if (act == SHOT_HIDE) {
    g_eng.ui_whide(shown);
    k->hid = 1;
    if (k->hides++ < 16) np_log("shot: %s %p hidden (field %08X, %u)", k->what, shown, v, k->hides);
  } else if (act == SHOT_RESTORE) {
    g_eng.ui_wshow(w, 0);
    k->hid = 0;
    k->shows++;
    np_log("shot: %s %p shown back (field %08X, %u)", k->what, w, v, k->shows);
  } else if (act == SHOT_FORGET) {
    k->hid = 0;
    k->forgets++;
    np_log("shot: %s %p left hidden: the game %s meanwhile (%u)", k->what, w,
           read ? "closed it" : "window or handler unreadable", k->forgets);
  }
}

static void uset_hud(const char* v) {
  char cmd[96];
  _snprintf_s(cmd, sizeof cmd, _TRUNCATE, "uset g_hud \"%s\"", v);
  g_eng.cmd_execute(cmd);
}

// Once per launch: a g_hud 0 left by the first build's crash is put back (shot_recover), and the stash cleared.
static void recover(void) {
  s_recovered = 1;
  if (g_cfg.hudStash < 0) return;
  cvar_t* cv = g_eng.cvar_get ? g_eng.cvar_get("g_hud") : NULL;
  char cur[64];
  if (!cv || !g_eng.cmd_execute || !cvar_read_string(cv, cur, sizeof cur)) {
    np_log("shot: g_hud unreadable; stash %d dropped", g_cfg.hudStash);
  } else {
    const int put = shot_recover(g_cfg.hudStash, atoi(cur));
    if (put >= 0) {
      char v[16];
      _snprintf_s(v, sizeof v, _TRUNCATE, "%d", put);
      uset_hud(v);
    }
    np_log("shot: stash %d from an earlier build, g_hud \"%s\"%s", g_cfg.hudStash, cur,
           put >= 0 ? ": written back" : ": nothing to put back");
  }
  g_cfg.hudStash = -1;
  g_cfgDirty = 1;
}

void shot_frame(void) {
  if (!s_recovered) recover();
  const int active = shot_active(input_freecam_on() ? 1 : 0, g_cfg.freecamShot || photo_busy());
  if (active != s_active) np_log("shot: screenshot mode %s", active ? "on" : "off");
  s_active = active;
  if (!g_eng.ui_whide || !g_eng.ui_wshow) {
    if (!s_saidOff++) np_log("shot: ui_whide/ui_wshow unresolved; only our overlays hide");
    return;
  }
  // The letterbox first: the HUD's rule below reads its state, as shown back.
  sys_frame(&s_lb, active);
  sys_frame(&s_pe, active);
  if (!active && !s_hid) return;   // the common frame: no ui_wget
  // Visible-only lookups: ui_wget scans the shown list (+0x3e0) for these (engine.h).
  void* shown = active ? input_ui_window("screen", "game_view") : NULL;
  int lbUp = 0;
  if (!active && s_hid) {
    // The letterbox end shows game_view only from states 1 and 2; a refused handler falls back to visibility.
    const int st = lb_state();
    lbUp = st == -2 ? input_ui_window("system_03", "letterbox") != NULL : shot_lb_holds_hud(st);
  }
  const int act = shot_hud_act(active, s_hid, shown != NULL, lbUp);
  if (act == SHOT_HIDE) {
    g_eng.ui_whide(shown);
    s_hid = 1;
    if (s_hides++ < 16) np_log("shot: HUD screen game_view %p hidden (%u)", shown, s_hides);
  } else if (act == SHOT_RESTORE) {
    void* w = input_ui_window_any("screen", "game_view");
    if (w) g_eng.ui_wshow(w, 0);
    s_hid = 0;
    s_shows++;
    np_log("shot: HUD screen game_view %p %s (%u)", w, w ? "shown back" : "gone (level change); nothing to show",
           s_shows);
  } else if (act == SHOT_FORGET) {
    s_hid = 0;
    np_log("shot: a cutscene letterbox is up; its end shows the HUD back itself");
  }
}

bool shot_hides_overlays(void) { return s_active != 0; }

void shot_restore_for_quit(void) {
  s_active = 0;
  if (s_hid || s_lb.hid || s_pe.hid)
    np_log("shot: quit with HUD %d, letterbox %d, level-end stats %d hidden by us; no visibility is archived", s_hid,
           s_lb.hid, s_pe.hid);
}
