#pragma once
// Borderless window (docs/re/display.md section A): pure rules, then the glue in display.cpp. The game stays
// windowed; r_mode takes the monitor's size and the WndProc subclass turns the windowed style into the fullscreen one.
// At launch a CBT hook arms this before the first video init; mid-session (and as the fallback) rend_restart rebuilds.

struct BlRect { int l, t, r, b; };
struct BlPos { int x, y, cx, cy; };

enum { BL_GWL_STYLE = -16, BL_GWL_EXSTYLE = -20 };
enum { BL_SWP_NOSIZE = 0x1, BL_SWP_NOMOVE = 0x2 };
// What apply_window_style prism3d!0x41ed80 writes for fullscreen (WS_POPUP|WS_VISIBLE, ex 0).
static const unsigned BL_STYLE_POPUP = 0x90000000u;
static const unsigned BL_EXSTYLE_POPUP = 0u;

// Overlap area of two rects, 0 when disjoint. 64-bit: two 32k-pixel sides overflow an int.
inline long long bl_overlap(BlRect a, BlRect b) {
  const int l = a.l > b.l ? a.l : b.l, r = a.r < b.r ? a.r : b.r;
  const int t = a.t > b.t ? a.t : b.t, bo = a.b < b.b ? a.b : b.b;
  if (r <= l || bo <= t) return 0;
  return (long long)(r - l) * (long long)(bo - t);
}

// The monitor holding most of the window; none overlapping -> primary (when valid), else 0; -1 if n <= 0.
inline int bl_pick_monitor(const BlRect* mons, int n, BlRect win, int primary) {
  if (n <= 0) return -1;
  int best = -1;
  long long bestArea = 0;
  for (int i = 0; i < n; i++) {
    const long long a = bl_overlap(mons[i], win);
    if (a > bestArea) { bestArea = a; best = i; }
  }
  if (best >= 0) return best;
  return primary >= 0 && primary < n ? primary : 0;
}

// The borderless window's rect. GLDirect (opengl32.dll) runs a fullscreen D3D9 device, which minimises on every focus
// loss, when the client rect equals the display mode at creation (gld_create_drawable 0x100042b0). One extra row off
// the monitor fails that test while it stays covered: above, unless another monitor holds that row, then below.
inline BlRect bl_window_rect(BlRect mon, const BlRect* mons, int n) {
  const BlRect above = { mon.l, mon.t - 1, mon.r, mon.t };
  bool taken = false;
  for (int i = 0; i < n; i++) if (bl_overlap(mons[i], above) > 0) taken = true;
  BlRect r = mon;
  if (taken) r.b += 1;
  else r.t -= 1;
  return r;
}

// The style WM_STYLECHANGING leaves: the fullscreen values while on, the engine's request while off.
inline unsigned bl_style_for(int on, int which, unsigned requested) {
  if (!on) return requested;
  if (which == BL_GWL_STYLE) return BL_STYLE_POPUP;
  if (which == BL_GWL_EXSTYLE) return BL_EXSTYLE_POPUP;
  return requested;
}

// WM_WINDOWPOSCHANGING while on: moved to the monitor origin and sized to it, each part only
// when the call moves or sizes. A minimise (-32000,-32000) is left alone. True when p changed.
inline bool bl_fix_pos(int on, unsigned swpFlags, BlPos* p, BlRect mon) {
  if (!on || (p->x == -32000 && p->y == -32000)) return false;
  bool changed = false;
  if (!(swpFlags & BL_SWP_NOMOVE) && (p->x != mon.l || p->y != mon.t)) {
    p->x = mon.l; p->y = mon.t; changed = true;
  }
  const int w = mon.r - mon.l, h = mon.b - mon.t;
  if (!(swpFlags & BL_SWP_NOSIZE) && (p->cx != w || p->cy != h)) {
    p->cx = w; p->cy = h; changed = true;
  }
  return changed;
}

// Minimise diagnostics: which messages the subclass logs. Nothing is swallowed; that would be justified only once
// the log shows GLDirect minimising.
enum { BL_MSG_PASS = 0, BL_MSG_LOG = 1 };
inline int bl_minimise_msg(int on, unsigned msg, unsigned wparam) {
  if (!on) return BL_MSG_PASS;
  if (msg == 0x1C) return BL_MSG_LOG;                                    // WM_ACTIVATEAPP, either way
  if (msg == 0x5 && wparam == 1) return BL_MSG_LOG;                      // WM_SIZE, SIZE_MINIMIZED
  if (msg == 0x112 && (wparam & 0xFFF0u) == 0xF020u) return BL_MSG_LOG;  // WM_SYSCOMMAND, SC_MINIMIZE
  return BL_MSG_PASS;
}

// The switch's state machine, stepped once a frame between our draws and the present.
struct BlState { int applied, failed, lastWant, seen, reapply, drifts, scene; };
enum { BL_NONE = 0, BL_APPLY = 1, BL_REVERT = 2 };
// A failed apply waits for the switch to move instead of retrying (and restarting the
// renderer) every frame; a revert waits for `ready` too (windowed, window known).
inline int bl_step(BlState* s, int want, int ready) {
  want = want ? 1 : 0;
  if (!s->seen || want != s->lastWant) { s->seen = 1; s->lastWant = want; s->failed = 0; }
  if (!ready) return BL_NONE;
  if (want && !s->applied && !s->failed) return BL_APPLY;
  if (!want && (s->applied || s->reapply)) return BL_REVERT;
  return BL_NONE;
}
inline void bl_done_apply(BlState* s, int ok) {
  if (ok) s->applied = 1;
  else s->failed = 1;
  s->reapply = 0;
}
inline void bl_done_revert(BlState* s) { s->applied = 0; s->reapply = 0; }

// The first apply waits for a scene the engine exposes: the main menu shown (ui_wget("screen", "menu_main", 1);
// duke_base's "ui s menu_main:screen:..." 0x10159e80) or a player entity. A restart during the intro or a load is
// unproven (the video menu, its only precedent, runs from a menu). Latched, so later flips never wait.
enum { BL_SCENE_NONE = 0, BL_SCENE_MENU = 1, BL_SCENE_LEVEL = 2 };
inline int bl_note_scene(BlState* s, int menuShown, int inLevel) {
  if (s->scene || (!menuShown && !inLevel)) return BL_SCENE_NONE;
  s->scene = 1;
  return menuShown ? BL_SCENE_MENU : BL_SCENE_LEVEL;
}

// The stock window and r_mode, taken only while nothing of ours is applied (the glue passes
// applied || reapply, so a re-apply never takes the borderless window as stock).
struct BlStock { unsigned style, exstyle; BlRect rect; char mode[64]; int valid; };
inline bool bl_capture(BlStock* s, int applied, unsigned style, unsigned exstyle, BlRect rect, const char* mode) {
  if (applied) return false;
  s->style = style; s->exstyle = exstyle; s->rect = rect;
  int i = 0;
  for (; mode && mode[i] && i < (int)sizeof(s->mode) - 1; i++) s->mode[i] = mode[i];
  s->mode[i] = 0;
  s->valid = 1;
  return true;
}

// While applied, an r_mode other than ours is the player's (video menu, r_restart): it becomes the stock mode, so a
// revert or the quit restore writes it, and borderless re-applies at the monitor size. Past BL_DRIFT_MAX re-applies
// the engine is taken to be fighting the mode: GIVEUP, and the glue reverts.
enum { BL_DRIFT_NONE = 0, BL_DRIFT_REAPPLY = 1, BL_DRIFT_GIVEUP = 2 };
enum { BL_DRIFT_MAX = 8 };
inline int bl_mode_drift(BlState* s, BlStock* st, const char* setMode, const char* nowMode) {
  if (!s->applied || !setMode || !nowMode || !*nowMode) return BL_DRIFT_NONE;
  int i = 0;
  while (setMode[i] && setMode[i] == nowMode[i]) i++;
  if (setMode[i] == nowMode[i]) return BL_DRIFT_NONE;
  for (i = 0; nowMode[i] && i < (int)sizeof(st->mode) - 1; i++) st->mode[i] = nowMode[i];
  st->mode[i] = 0;
  if (s->drifts >= BL_DRIFT_MAX) { s->failed = 1; return BL_DRIFT_GIVEUP; }
  s->drifts++;
  s->applied = 0;
  s->reapply = 1;
  return BL_DRIFT_REAPPLY;
}

// r_mode is "WxHxBPP", the format duke_base's video menu writes (FUN_100a83c0, "%dx%dx%d").
// Up to three positive decimal fields split by 'x'; bpp is 0 when absent.
inline bool bl_parse_mode(const char* s, int* w, int* h, int* bpp) {
  if (!s) return false;
  int v[3] = { 0, 0, 0 }, n = 0;
  while (n < 3) {
    if (*s < '0' || *s > '9') return false;
    int x = 0;
    while (*s >= '0' && *s <= '9') { if (x > 100000) return false; x = x * 10 + (*s++ - '0'); }
    v[n++] = x;
    if ((*s == 'x' || *s == 'X') && n < 3) { s++; continue; }
    break;
  }
  if (*s || n < 2 || v[0] <= 0 || v[1] <= 0) return false;
  *w = v[0]; *h = v[1]; *bpp = n == 3 ? v[2] : 0;
  return true;
}
// "WxHxBPP" into out; false (out untouched past its first byte) when it does not fit.
inline bool bl_mode_string(char* out, int cap, int w, int h, int bpp) {
  char tmp[40];
  int n = 0;
  const int vals[3] = { w, h, bpp };
  for (int k = 0; k < 3; k++) {
    char digits[12];
    int d = 0;
    unsigned x = vals[k] > 0 ? (unsigned)vals[k] : 0u;
    do { digits[d++] = (char)('0' + x % 10); x /= 10; } while (x);
    if (k) tmp[n++] = 'x';
    while (d) tmp[n++] = digits[--d];
  }
  if (!out || cap <= n) { if (out && cap > 0) out[0] = 0; return false; }
  for (int i = 0; i < n; i++) out[i] = tmp[i];
  out[n] = 0;
  return true;
}

// The stock r_mode only, with no window: at launch the engine has not made its game window yet.
// style 0 marks "no stock window", so a revert keeps the captioned window the engine centres itself.
inline void bl_capture_mode_only(BlStock* s, const char* mode) {
  s->style = 0; s->exstyle = 0;
  s->rect.l = s->rect.t = s->rect.r = s->rect.b = 0;
  int i = 0;
  for (; mode && mode[i] && i < (int)sizeof(s->mode) - 1; i++) s->mode[i] = mode[i];
  s->mode[i] = 0;
  s->valid = 1;
}
inline bool bl_stock_has_window(const BlStock* s) { return s->valid && s->style != 0; }

// Borderless at launch (display.md section A). The engine creates its hidden context window (0x426ea0) after
// config.cfg and before rend_init (0x41ef00) reads r_fullscreen and r_mode; a CBT hook sees that. r_fullscreen links
// to the device's cvar only inside rend_init, so the windowed test reads "<r_device>_fullscreen", set by config.cfg.
inline bool bl_fs_cvar_name(char* out, int cap, const char* device) {
  static const char kSuffix[] = "_fullscreen";
  if (!out || cap <= 0) return false;
  out[0] = 0;
  if (!device || !device[0]) return false;
  int n = 0;
  while (device[n]) n++;
  if (n + (int)sizeof(kSuffix) > cap) return false;
  for (int i = 0; i < n; i++) out[i] = device[i];
  for (int i = 0; i < (int)sizeof(kSuffix); i++) out[n + i] = kSuffix[i];
  return true;
}
// deviceFullscreen: 1 fullscreen, 0 windowed, -1 unreadable.
enum { BL_EARLY_ARM = 0, BL_EARLY_OFF = 1, BL_EARLY_NOAPI = 2, BL_EARLY_FULLSCREEN = 3, BL_EARLY_NOMONITOR = 4 };
inline int bl_early_decide(int want, int apis, int deviceFullscreen, int haveMonitor) {
  if (!want) return BL_EARLY_OFF;
  if (!apis || deviceFullscreen < 0) return BL_EARLY_NOAPI;
  if (deviceFullscreen) return BL_EARLY_FULLSCREEN;
  if (!haveMonitor) return BL_EARLY_NOMONITOR;
  return BL_EARLY_ARM;
}
// Armed at creation: applied with no restart. The scene latch stays clear, so a later revert (which
// restarts) still waits for the menu or a level.
inline void bl_arm_early(BlState* s) { s->seen = 1; s->lastWant = 1; s->applied = 1; s->failed = 0; }
// The first frame found it not applied: the restart path takes over, as a re-apply, so the launch
// capture (mode only) is kept and the half-applied window is never taken as stock.
inline void bl_early_failed(BlState* s) { s->applied = 0; s->reapply = 1; }
// Our r_mode is set: applied, or a re-apply pending (a failed first frame, a drift). The quit restore and
// a switch-off revert act on either, so the monitor mode never reaches config.cfg before the fallback runs.
inline bool bl_mode_ours(const BlState* s) { return s->applied || s->reapply; }

// What an apply is checked against, at creation or after a restart, all measured by us: GLDirect's fullscreen test
// needs client == display mode and its exclusive device minimises on focus loss, so a window at bl_window_rect's
// rect, with that client, not the mode's size and not iconic, is a windowed device.
struct BlCheck { int modeW, modeH, monW, monH; unsigned style; BlRect rect; int clientW, clientH, iconic; };
inline bool bl_applied_ok(const BlCheck* c, BlRect win) {
  const bool popup = (c->style & (0x80000000u | 0x00C00000u | 0x00040000u)) == 0x80000000u;   // WS_POPUP, no caption or sizing frame
  const bool gldFull = c->clientW == c->modeW && c->clientH == c->modeH;
  return c->modeW == c->monW && c->modeH == c->monH && popup && c->rect.l == win.l && c->rect.t == win.t &&
         c->rect.r == win.r && c->rect.b == win.b && c->clientW == win.r - win.l && c->clientH == win.b - win.t &&
         !gldFull && !c->iconic;
}

// ------------------------------------------------------------------ engine glue (display.cpp)
#include <windows.h>
// Once a frame, last in the end-frame detour after our own drawing: steps the state machine and,
// on a change, sets r_mode and calls rend_restart (which ends this frame and begins the next).
// inLevel: a player entity this frame (probe.cpp), one of the scenes the first apply waits for.
void display_frame(int inLevel);
// First thing in the WndProc subclass: rewrites WM_STYLECHANGING and WM_WINDOWPOSCHANGING in place
// while borderless is applied and the game windowed, and logs the minimise messages. Never swallows.
void display_wndproc_filter(HWND h, UINT msg, WPARAM w, LPARAM l);
// The config.cfg write at quit: puts the player's r_mode back so config.cfg keeps it.
void display_restore_for_quit(void);
// From DllMain (main thread, before WinMain): a thread-local WH_CBT hook that arms borderless when the
// engine creates its first Prism3D window, so the first video init comes up borderless. Unhooks itself.
void display_early_install(void);
// A line for the Settings > DISPLAY row, or NULL when there is nothing to say.
const char* display_note(void);
