// Borderless window glue (display.h, docs/re/display.md section A). Main thread only: the WndProc
// and the frame share the engine's one thread (input.h), so the statics below are plain.
#include "display.h"
#include "engine.h"
#include "cvar_read.h"
#include "config.h"
#include "input.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BlState s_bl;
static BlStock s_stock;
static BlRect s_win;            // the borderless window rect (bl_window_rect), read by the filter
static int s_filterOn;          // borderless applied: the subclass rewrites style and position
static int s_fullscreenSeen;    // the switch wanted borderless while the game was fullscreen
static cvar_t* s_modeCv;
static cvar_t* s_fsCv;
static int s_modeTried, s_fsTried;
static unsigned s_styleRewrites, s_posRewrites, s_minLogs;
static unsigned s_frames, s_nextCheck;   // the waiting path's 30-frame throttle
static char s_setMode[40];      // the r_mode we set, for telling the player's own change from ours
static HHOOK s_cbt;             // the launch hook (display_early_install), until the first Prism3D window
static int s_early;             // 1: armed at creation, the first frame checks it; 2: checked
static BlRect s_earlyMon;       // the monitor armed at creation

static bool read_cvar(cvar_t** slot, int* tried, const char* name, char* out, size_t cap) {
  if (!*slot && !*tried && g_eng.cvar_get) {
    *tried = 1;
    *slot = g_eng.cvar_get(name);
    if (!*slot) np_log("display: cvar %s not found", name);
  }
  return *slot && cvar_read_string(*slot, out, cap);
}

// rend_restart reads the windowed flag as !atoi(r_fullscreen) (0x41f28a).
static bool game_windowed(void) {
  char b[64];
  return read_cvar(&s_fsCv, &s_fsTried, "r_fullscreen", b, sizeof b) && atoi(b) == 0;
}

static bool read_mode(char* out, size_t cap) { return read_cvar(&s_modeCv, &s_modeTried, "r_mode", out, cap); }

// Through cvar_t::set, as duke_base's video menu does; read back, since the validator's refusal
// still returns 1.
static bool set_mode(const char* mode) {
  if (!s_modeCv || !engine_resolve_cvar_set()) return false;
  const bool ok = g_p3.cvar_set(s_modeCv, mode);
  char now[64] = "";
  const bool read = read_mode(now, sizeof now);
  if (!ok || !read || strcmp(now, mode) != 0) {
    np_log("display: r_mode \"%s\" refused (set=%d, now \"%s\")", mode, ok ? 1 : 0, read ? now : "?");
    return false;
  }
  return true;
}

struct MonList { BlRect r[16]; int n, primary; };

static BOOL CALLBACK mon_cb(HMONITOR m, HDC, LPRECT, LPARAM p) {
  MonList* ml = (MonList*)p;
  MONITORINFO mi;
  mi.cbSize = sizeof mi;
  if (GetMonitorInfoA(m, &mi)) {
    const BlRect r = { mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom };
    if (mi.dwFlags & MONITORINFOF_PRIMARY) ml->primary = ml->n;
    ml->r[ml->n++] = r;
  }
  return ml->n < 16;
}

static BlRect window_rect(HWND h) {
  RECT wr = { 0, 0, 0, 0 };
  GetWindowRect(h, &wr);
  const BlRect r = { wr.left, wr.top, wr.right, wr.bottom };
  return r;
}

static bool same_rect(BlRect a, BlRect b) { return a.l == b.l && a.t == b.t && a.r == b.r && a.b == b.b; }

// A mode other than ours is the player's latest choice (video menu, r_restart): it is the one a
// revert or the quit restore must write, never the older capture.
static void adopt_player_mode(const char* why) {
  char now[64] = "";
  if (!s_setMode[0] || !read_mode(now, sizeof now) || !strcmp(now, s_setMode) || !strcmp(now, s_stock.mode)) return;
  np_log("display: r_mode \"%s\" is the player's (%s); stock mode was \"%s\"", now, why, s_stock.mode);
  strcpy_s(s_stock.mode, sizeof s_stock.mode, now);
}

// gl's rend_restart destroys the game window and creates a new one (winwatch.h): everything after
// a restart acts on the window the subclass follows now, never on the handle from before.
static HWND restart_and_follow(HWND before) {
  g_eng.rend_restart();
  const HWND after = input_window_resync();
  np_log("display: rend_restart: window %p (IsWindow=%d now) -> %p", (void*)before, IsWindow(before) ? 1 : 0,
         (void*)after);
  return after;
}

// Borderless means a popup with no caption or sizing frame; the system adds WS_CLIPSIBLINGS itself.
static bool is_borderless_style(unsigned style) {
  return (style & (WS_POPUP | WS_CAPTION | WS_THICKFRAME)) == WS_POPUP;
}

// Nothing of ours is left applied: the filter is off, r_mode is the player's, and the renderer
// rebuilt with the engine's own windowed style. The frame the engine centred is then moved back
// to where the player had it, with the style it had.
static void revert(HWND h, const char* why) {
  adopt_player_mode(why);
  s_filterOn = 0;
  s_setMode[0] = 0;
  if (s_stock.valid && !set_mode(s_stock.mode)) np_log("display: revert could not restore r_mode");
  h = restart_and_follow(h);
  if (!h) { np_log("display: no game window after the revert's restart"); return; }
  const unsigned style = (unsigned)GetWindowLongA(h, GWL_STYLE), ex = (unsigned)GetWindowLongA(h, GWL_EXSTYLE);
  const BlRect now = window_rect(h);
  // Armed at launch there was no stock window: the captioned one the engine just centred is it.
  if (bl_stock_has_window(&s_stock) &&
      (style != s_stock.style || ex != s_stock.exstyle || !same_rect(now, s_stock.rect))) {
    SetWindowLongA(h, GWL_STYLE, (LONG)s_stock.style);
    SetWindowLongA(h, GWL_EXSTYLE, (LONG)s_stock.exstyle);
    SetWindowPos(h, NULL, s_stock.rect.l, s_stock.rect.t, s_stock.rect.r - s_stock.rect.l,
                 s_stock.rect.b - s_stock.rect.t, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
  const BlRect fin = window_rect(h);
  np_log("display: borderless reverted (%s): r_mode \"%s\", style 0x%08x->0x%08x ex 0x%x->0x%x, "
         "window %d,%d %dx%d (stock %d,%d %dx%d)", why, s_stock.mode, style,
         (unsigned)GetWindowLongA(h, GWL_STYLE), ex, (unsigned)GetWindowLongA(h, GWL_EXSTYLE),
         fin.l, fin.t, fin.r - fin.l, fin.b - fin.t, s_stock.rect.l, s_stock.rect.t,
         s_stock.rect.r - s_stock.rect.l, s_stock.rect.b - s_stock.rect.t);
}

static bool apply(HWND h) {
  char cur[64] = "";
  // A failed re-apply leaves our monitor mode set: revert it so config.cfg never keeps it.
  if (!read_mode(cur, sizeof cur)) {
    np_log("display: r_mode unreadable; borderless not applied");
    if (s_bl.reapply) revert(h, "re-apply: r_mode unreadable");
    return false;
  }
  const BlRect wr = window_rect(h);
  bl_capture(&s_stock, s_bl.applied || s_bl.reapply, (unsigned)GetWindowLongA(h, GWL_STYLE),
             (unsigned)GetWindowLongA(h, GWL_EXSTYLE), wr, cur);

  MonList ml;
  memset(&ml, 0, sizeof ml);
  EnumDisplayMonitors(NULL, NULL, mon_cb, (LPARAM)&ml);
  const int pick = bl_pick_monitor(ml.r, ml.n, wr, ml.primary);
  if (pick < 0) {
    np_log("display: no monitor found; borderless not applied");
    if (s_bl.reapply) revert(h, "re-apply: no monitor");
    return false;
  }
  const BlRect mon = ml.r[pick];
  const int mw = mon.r - mon.l, mh = mon.b - mon.t;

  int w = 0, hh = 0, bpp = 0;
  if (!bl_parse_mode(cur, &w, &hh, &bpp) || bpp <= 0) bpp = 32;
  char want[40];
  if (!bl_mode_string(want, sizeof want, mw, mh, bpp)) {
    if (s_bl.reapply) revert(h, "re-apply: mode string");
    return false;
  }
  np_log("display: applying borderless: monitor %d of %d at %d,%d %dx%d, r_mode \"%s\" -> \"%s\", "
         "stock style 0x%08x ex 0x%x window %d,%d %dx%d", pick, ml.n, mon.l, mon.t, mw, mh, cur, want,
         s_stock.style, s_stock.exstyle, wr.l, wr.t, wr.r - wr.l, wr.b - wr.t);
  if (!set_mode(want)) {   // nothing else changed yet, unless this is a re-apply
    if (s_bl.reapply) revert(h, "re-apply refused");
    return false;
  }

  strcpy_s(s_setMode, sizeof s_setMode, want);
  const BlRect win = bl_window_rect(mon, ml.r, ml.n);
  const int ww = win.r - win.l, wh = win.b - win.t;
  s_win = win;
  s_filterOn = 1;
  const HWND before = h;
  h = restart_and_follow(h);
  if (!h) {
    np_log("display: no game window after the restart; borderless not applied");
    revert(before, "no window");
    return false;
  }
  // The class proc restyles a window the engine creates inside the restart; one that came up
  // captioned anyway is restyled here, after its GL context (the swapchain keeps the old size).
  if (!is_borderless_style((unsigned)GetWindowLongA(h, GWL_STYLE))) {
    np_log("display: new window %p came up with style 0x%08x; restyling it after the fact", (void*)h,
           (unsigned)GetWindowLongA(h, GWL_STYLE));
    SetWindowLongA(h, GWL_STYLE, (LONG)BL_STYLE_POPUP);
    SetWindowLongA(h, GWL_EXSTYLE, (LONG)BL_EXSTYLE_POPUP);
    SetWindowPos(h, NULL, win.l, win.t, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }

  int W = 0, H = 0;
  g_eng.rend_get_params(&W, &H, NULL);
  const unsigned style = (unsigned)GetWindowLongA(h, GWL_STYLE);
  const BlRect now = window_rect(h);
  RECT cr = { 0, 0, 0, 0 };
  GetClientRect(h, &cr);
  const int iconic = IsIconic(h) ? 1 : 0;
  const BlCheck chk = { W, H, mw, mh, style, now, (int)cr.right, (int)cr.bottom, iconic };
  const bool ok = bl_applied_ok(&chk, win);
  np_log("display: rend_restart done: mode %dx%d, style 0x%08x, window %d,%d %dx%d, client %dx%d, iconic %d, "
         "rewrites style %u pos %u -> %s", W, H, style, now.l, now.t, now.r - now.l, now.b - now.t,
         (int)cr.right, (int)cr.bottom, iconic, s_styleRewrites, s_posRewrites, ok ? "applied" : "FAILED");
  if (!ok) revert(h, "apply failed");
  return ok;
}

// The first frame after an arm at creation: the engine's first video init is checked like a restart
// apply. A refusal hands over to the restart path (bl_early_failed), which waits for a scene as before.
static void early_check(HWND h) {
  s_early = 2;
  int W = 0, H = 0;
  g_eng.rend_get_params(&W, &H, NULL);
  const unsigned style = (unsigned)GetWindowLongA(h, GWL_STYLE);
  const BlRect now = window_rect(h);
  RECT cr = { 0, 0, 0, 0 };
  GetClientRect(h, &cr);
  const int iconic = IsIconic(h) ? 1 : 0;
  const BlCheck chk = { W, H, s_earlyMon.r - s_earlyMon.l, s_earlyMon.b - s_earlyMon.t, style, now,
                        (int)cr.right, (int)cr.bottom, iconic };
  const bool ok = game_windowed() && bl_applied_ok(&chk, s_win);
  np_log("display: first frame: mode %dx%d, style 0x%08x, window %d,%d %dx%d, client %dx%d, iconic %d, rewrites "
         "style %u pos %u, windowed=%d -> %s", W, H, style, now.l, now.t, now.r - now.l, now.b - now.t,
         (int)cr.right, (int)cr.bottom, iconic, s_styleRewrites, s_posRewrites, game_windowed() ? 1 : 0,
         ok ? "borderless at creation: applied, no rend_restart" :
              "borderless at creation FAILED; fallback: the rend_restart path");
  if (ok) return;
  s_filterOn = 0;
  // A mode_init refusal resets r_mode to its default (rend_init 0x41ef00): that value is the engine's,
  // not the player's, so the quit restore and a revert must not adopt it over the launch capture.
  char mode[64] = "";
  if (read_mode(mode, sizeof mode) && strcmp(mode, s_setMode) != 0 && strlen(mode) < sizeof s_setMode) {
    np_log("display: first frame: r_mode is \"%s\" (we set \"%s\"); kept as ours, stock stays \"%s\"", mode,
           s_setMode, s_stock.mode);
    strcpy_s(s_setMode, sizeof s_setMode, mode);
  }
  bl_early_failed(&s_bl);
}

void display_frame(int inLevel) {
  const int want = g_cfg.borderless ? 1 : 0;
  if (s_early == 1) {
    HWND h = input_window();
    if (!h || !g_eng.rend_get_params) return;   // input_frame finds the window first
    early_check(h);
  }
  // Applied: r_mode is read every frame (one cvar read), so a mode the player picks in the video
  // menu is adopted before the next frame is drawn stretched.
  if (want && s_bl.applied) {
    char now[64] = "";
    if (read_mode(now, sizeof now)) {
      const int d = bl_mode_drift(&s_bl, &s_stock, s_setMode, now);
      if (d == BL_DRIFT_REAPPLY) {
        np_log("display: player chose r_mode \"%s\" (we set \"%s\"); it is the stock mode now; re-applying (%d)",
               now, s_setMode, s_bl.drifts);
        s_nextCheck = 0;   // the re-apply runs this frame, not after the throttle
      } else if (d == BL_DRIFT_GIVEUP) {
        np_log("display: r_mode changed again (\"%s\") after %d re-applies; borderless gives up", now, s_bl.drifts);
        HWND h = input_window();
        if (h) revert(h, "r_mode keeps changing");
        bl_done_revert(&s_bl);
        return;
      }
    }
  }
  BlState probe = s_bl;
  if (bl_step(&probe, want, 1) == BL_NONE) { s_bl = probe; return; }   // the cheap common frame
  // Waiting on a window, a scene or a windowed game: those reads cost guarded reads, so every 30 frames.
  if (++s_frames < s_nextCheck) return;
  s_nextCheck = s_frames + 30;

  HWND h = input_window();
  const bool apis = g_eng.rend_restart && g_eng.cvar_get && g_eng.rend_get_params && engine_resolve_cvar_set();
  const bool windowed = h && apis && game_windowed();
  const int fsNow = h && apis && !windowed;
  if (want && fsNow != s_fullscreenSeen)
    np_log("display: game %s; borderless %s", fsNow ? "fullscreen" : "windowed", fsNow ? "waits" : "may apply");
  s_fullscreenSeen = fsNow;

  if (!s_bl.scene && h) {
    const int menu = input_ui_window("screen", "menu_main") != NULL;
    const int sc = bl_note_scene(&s_bl, menu, inLevel);
    if (sc != BL_SCENE_NONE)
      np_log("display: first scene: %s; borderless may apply", sc == BL_SCENE_MENU ? "main menu shown" : "player entity");
  }

  const int act = bl_step(&s_bl, want, windowed && s_bl.scene ? 1 : 0);
  if (act != BL_NONE) s_nextCheck = 0;   // a flip right after is acted on at once
  if (act == BL_APPLY) bl_done_apply(&s_bl, apply(h) ? 1 : 0);
  else if (act == BL_REVERT) { revert(h, "switch off"); bl_done_revert(&s_bl); }
}

// Up to 8 return addresses outside user32/win32u/ntdll and this DLL, as module+offset: who sent
// the message when it was sent on this thread.
static void caller_chain(char* out, size_t cap) {
  void* frames[32];
  const USHORT n = RtlCaptureStackBackTrace(1, 32, frames, NULL);
  HMODULE self = NULL;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     (LPCSTR)&caller_chain, &self);
  size_t len = 0;
  out[0] = 0;
  int shown = 0;
  for (USHORT i = 0; i < n && shown < 8; i++) {
    HMODULE m = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)frames[i], &m) || m == self) continue;
    char path[MAX_PATH];
    if (!GetModuleFileNameA(m, path, MAX_PATH)) continue;
    const char* base = strrchr(path, 0x5C);
    base = base ? base + 1 : path;
    if (!_stricmp(base, "user32.dll") || !_stricmp(base, "win32u.dll") || !_stricmp(base, "ntdll.dll")) continue;
    const int k = _snprintf_s(out + len, cap - len, _TRUNCATE, "%s%s+0x%x", shown ? " < " : "", base,
                              (unsigned)((const char*)frames[i] - (const char*)m));
    if (k < 0) break;
    len += (size_t)k;
    shown++;
  }
  if (!shown) strcpy_s(out, cap, "(none outside user32/ntdll)");
}

void display_wndproc_filter(HWND h, UINT msg, WPARAM w, LPARAM l) {
  if (msg == WM_STYLECHANGING && s_filterOn && l) {
    STYLESTRUCT* ss = (STYLESTRUCT*)l;
    const unsigned to = bl_style_for(1, (int)w, (unsigned)ss->styleNew);
    if (to != (unsigned)ss->styleNew) {
      if (s_styleRewrites++ < 16)
        np_log("display: style %d 0x%08x -> 0x%08x", (int)w, (unsigned)ss->styleNew, to);
      ss->styleNew = to;
    }
  } else if (msg == WM_WINDOWPOSCHANGING && s_filterOn && l && game_windowed()) {
    WINDOWPOS* wp = (WINDOWPOS*)l;
    BlPos p = { wp->x, wp->y, wp->cx, wp->cy };
    if (bl_fix_pos(1, wp->flags, &p, s_win)) {
      if (s_posRewrites++ < 16)
        np_log("display: window pos %d,%d %dx%d flags 0x%x -> %d,%d %dx%d", wp->x, wp->y, wp->cx, wp->cy,
               wp->flags, p.x, p.y, p.cx, p.cy);
      wp->x = p.x; wp->y = p.y; wp->cx = p.cx; wp->cy = p.cy;
      // The engine's SetWindowPos (0x40) follows its SetWindowLong; the frame is recomputed only with this.
      wp->flags |= SWP_FRAMECHANGED;
    }
  }
  if (bl_minimise_msg(s_filterOn, msg, (unsigned)w) == BL_MSG_LOG) {
    ++s_minLogs;
    if (s_minLogs <= 64) {
      char chain[400];
      caller_chain(chain, sizeof chain);
      np_log("display: msg 0x%x wParam 0x%x iconic=%d foreground=%d (#%u) from %s", msg, (unsigned)w,
             IsIconic(h) ? 1 : 0, GetForegroundWindow() == h ? 1 : 0, s_minLogs, chain);
    }
  }
}

// At the engine's first Prism3D window (the hidden context window, core_create_context_window via 0x426ea0): config.cfg
// has run and rend_init (0x41ef00) has not read r_mode yet. Set r_mode to the monitor and put our proc on the class, so
// the window gl's mode_init creates next runs through the filter from WM_NCCREATE and comes out borderless early.
static void early_arm(HWND ctx) {
  config_load();
  if (!g_cfg.borderless) {   // reached only when the DllMain peek could not read the ini: no cvar work
    np_log("display: at creation (window %p): switch off; nothing armed", (void*)ctx);
    return;
  }
  char cur[64] = "", dev[32] = "", fsName[48] = "", fsVal[16] = "";
  const int apis = g_eng.cvar_get && g_eng.rend_get_params && g_eng.rend_restart && engine_resolve_cvar_set() &&
                   read_mode(cur, sizeof cur) ? 1 : 0;
  cvar_t* devCv = g_eng.cvar_get ? g_eng.cvar_get("r_device") : NULL;
  cvar_t* fsCv = NULL;
  int fs = -1;
  if (devCv && cvar_read_string(devCv, dev, sizeof dev) && bl_fs_cvar_name(fsName, sizeof fsName, dev) &&
      (fsCv = g_eng.cvar_get(fsName)) != NULL && cvar_read_string(fsCv, fsVal, sizeof fsVal))
    fs = atoi(fsVal) != 0 ? 1 : 0;
  MonList ml;
  memset(&ml, 0, sizeof ml);
  EnumDisplayMonitors(NULL, NULL, mon_cb, (LPARAM)&ml);
  const BlRect none = { 0, 0, 0, 0 };
  const int pick = bl_pick_monitor(ml.r, ml.n, none, ml.primary);   // the engine centres on the primary
  static const char* const kWhy[] = { "arming", "switch off", "cvar API or r_mode unreadable",
                                      "the game is fullscreen", "no monitor" };
  const int d = bl_early_decide(g_cfg.borderless, apis, fs, pick >= 0);
  np_log("display: at creation (window %p): r_device \"%s\" %s=\"%s\", r_mode \"%s\": %s", (void*)ctx, dev,
         fsName[0] ? fsName : "?_fullscreen", fsVal, cur, kWhy[d]);
  if (d != BL_EARLY_ARM) return;

  const BlRect mon = ml.r[pick];
  int w = 0, h = 0, bpp = 0;
  if (!bl_parse_mode(cur, &w, &h, &bpp) || bpp <= 0) bpp = 32;
  char want[40];
  // The launcher's r_mode is captured before ours is set, so the quit restore has it from that moment.
  bl_capture_mode_only(&s_stock, cur);
  if (!bl_mode_string(want, sizeof want, mon.r - mon.l, mon.b - mon.t, bpp) || !set_mode(want)) {
    np_log("display: at creation: r_mode not set; the restart path applies after the first scene");
    return;
  }
  if (!input_class_proc_early(ctx)) {
    set_mode(cur);
    np_log("display: at creation: class proc not installed; r_mode back to \"%s\"; the restart path applies", cur);
    return;
  }
  strcpy_s(s_setMode, sizeof s_setMode, want);
  s_win = bl_window_rect(mon, ml.r, ml.n);
  s_earlyMon = mon;
  s_filterOn = 1;
  bl_arm_early(&s_bl);
  s_early = 1;
  np_log("display: borderless at creation: monitor %d of %d at %d,%d %dx%d, r_mode \"%s\" -> \"%s\", window "
         "%d,%d %dx%d; the first frame checks it", pick, ml.n, mon.l, mon.t, mon.r - mon.l, mon.b - mon.t, cur, want,
         s_win.l, s_win.t, s_win.r - s_win.l, s_win.b - s_win.t);
}

static LRESULT CALLBACK cbt_proc(int code, WPARAM w, LPARAM l) {
  bool done = false;
  if (code == HCBT_CREATEWND && s_cbt) {
    char cls[32];
    if (GetClassNameA((HWND)w, cls, sizeof cls) && !strcmp(cls, "Prism3D")) {
      early_arm((HWND)w);
      done = true;
    }
  }
  const LRESULT r = CallNextHookEx(NULL, code, w, l);
  if (done && s_cbt) { UnhookWindowsHookEx(s_cbt); s_cbt = NULL; }
  return r;
}

void display_early_install(void) {
  s_cbt = SetWindowsHookExA(WH_CBT, cbt_proc, NULL, GetCurrentThreadId());
  np_log("display: launch hook %s (tid=%lu)%s", s_cbt ? "installed" : "NOT installed", GetCurrentThreadId(),
         s_cbt ? "" : "; borderless uses the restart path");
}

void display_restore_for_quit(void) {
  if (!bl_mode_ours(&s_bl) || !s_stock.valid) return;
  adopt_player_mode("quit");
  const bool ok = set_mode(s_stock.mode);
  np_log("quit: r_mode back to \"%s\" for config.cfg: %s", s_stock.mode, ok ? "ok" : "FAILED");
}

const char* display_note(void) {
  if (!g_cfg.borderless) return NULL;
  if (s_bl.failed) return "Could not apply; see nukepanel.log";
  if (s_fullscreenSeen) return "The game is fullscreen: this acts in windowed mode";
  return NULL;
}
