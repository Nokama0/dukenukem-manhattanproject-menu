#pragma once
// The stuck-mouse watch (after alt-tab the mouse is dead elsewhere until Ctrl+Alt+Del resets clip and capture).
// Instrumentation only. dinput8's emulated exclusive mouse (dinput8!0x10012485) does SetCapture, ShowCursor(0) and a
// small ClipCursor on acquire and the reverse on release; prism3d imports no ClipCursor (SetCapture: 0x406e90).
#include <windows.h>
#include <stdio.h>
#include <string.h>

// A clip that leaves part of the virtual desktop unreachable.
inline bool clip_is_restricted(const RECT& clip, const RECT& desktop) {
  return clip.left > desktop.left || clip.top > desktop.top || clip.right < desktop.right ||
         clip.bottom < desktop.bottom;
}

// After each focus loss: one line a second for MW_WINDOW_MS whatever it shows (the lock
// came on an alt-tab in play, so the moment must be on record even when nothing points at this process).
const unsigned long long MW_WINDOW_MS = 60000, MW_PERIOD_MS = 1000;
inline bool mw_due(unsigned long long now, unsigned long long lostAt, unsigned long long next, int ourFront) {
  return !ourFront && lostAt && now >= next && now - lostAt <= MW_WINDOW_MS;
}
// GetAsyncKeyState's held bits as letters (L R M buttons, Alt, Ctrl, Shift), "-" for none: a logically stuck
// button or modifier is a classic Ctrl+Alt+Del-cured symptom.
enum { MW_K_LBUTTON = 1, MW_K_RBUTTON = 2, MW_K_MBUTTON = 4, MW_K_ALT = 8, MW_K_CTRL = 16, MW_K_SHIFT = 32,
       MW_K_WIN = 64 };   // W: either Windows key (a stuck Win key swallows clicks' meaning for the shell)
inline void mw_keys(unsigned bits, char out[8]) {
  static const char kL[] = "LRMACSW";
  int n = 0;
  for (int i = 0; i < 7; ++i) if (bits & (1u << i)) out[n++] = kL[i];
  if (!n) out[n++] = '-';
  out[n] = 0;
}

// --- Next-capture measurements (docs/re/mouse-lock.md "Next capture") ---------------------------------
// (3) dinput8's worker state, read-only, only from the build read in the Ghidra MCP: 10.0.26100.8521
// (SysWOW64). The fixed block of its RT_VERSION resource, found by its 0xFEEF04BD signature at a dword boundary.
inline bool mw_fixed_version(const unsigned char* res, size_t n, unsigned* ms, unsigned* ls) {
  for (size_t i = 0; i + 16 <= n; i += 4) {
    unsigned sig;
    memcpy(&sig, res + i, 4);
    if (sig != 0xFEEF04BDu) continue;
    memcpy(ms, res + i + 8, 4);
    memcpy(ls, res + i + 12, 4);
    return true;
  }
  return false;
}
inline bool mw_dn8_known(unsigned ms, unsigned ls) { return ms == 0x000A0000u && ls == 0x65F42149u; }
// Module-relative (base 0x10000000 in the MCP): the raw-mode flag 0x100310a4 (worker 0x10011a10 clears it when
// the hidden window fails) and the worker context global 0x1003125c (hook proc 0x10011df0 reads its +0x1C and
// +0x20). Per type t (0 keyboard, 1 mouse, FUN_100119aa): count +8+0x10t, exclusive +0xC+0x10t, LL hook +0x10+0x10t.
const unsigned MW_DN8_RAW_RVA = 0x310A4u, MW_DN8_CTX_RVA = 0x3125Cu;
enum { MW_DN8_CTX_KBD_COUNT = 0x8, MW_DN8_CTX_KBD_EXCL = 0xC, MW_DN8_CTX_KBD_HOOK = 0x10,
       MW_DN8_CTX_MOUSE_COUNT = 0x18, MW_DN8_CTX_MOUSE_EXCL = 0x1C, MW_DN8_CTX_MOUSE_HOOK = 0x20,
       MW_DN8_CTX_SPAN = 0x24 };
// (2) GetRegisteredRawInputDevices: n rows in the buffer, total as the call reported (-1: the call failed).
// Mouse (page 1, usage 2) and keyboard (1, 6) rows by flags and target; others counted.
inline void mw_raw_summary(const RAWINPUTDEVICE* r, int n, int total, char* out, size_t cap) {
  if (total < 0) { _snprintf_s(out, cap, _TRUNCATE, " raw UNREAD"); return; }
  if (total == 0) { _snprintf_s(out, cap, _TRUNCATE, " raw none"); return; }
  int len = _snprintf_s(out, cap, _TRUNCATE, " raw %d of %d:", n, total);
  int other = 0;
  for (int i = 0; i < n && len > 0 && (size_t)len < cap; ++i) {
    const char* kind = r[i].usUsagePage == 1 && r[i].usUsage == 2 ? "mouse" :
                       r[i].usUsagePage == 1 && r[i].usUsage == 6 ? "keyboard" : 0;
    if (!kind) { other++; continue; }
    const int k = _snprintf_s(out + len, cap - len, _TRUNCATE, " %s 0x%lx->%p", kind, (unsigned long)r[i].dwFlags,
                              (void*)r[i].hwndTarget);
    if (k < 0) return;
    len += k;
  }
  if (other && len > 0 && (size_t)len < cap) _snprintf_s(out + len, cap - len, _TRUNCATE, " other %d", other);
}
// (4) A main-menu transition within 1 s of a focus loss, either side (the 17:07:46 race). 0 = none yet.
const unsigned long long MW_MENU_NEAR_MS = 1000;
inline bool mw_menu_near(unsigned long long transAt, unsigned long long lostAt) {
  if (!transAt || !lostAt) return false;
  return (transAt > lostAt ? transAt - lostAt : lostAt - transAt) <= MW_MENU_NEAR_MS;
}

// (c) GetGUIThreadInfo of the threads that could hold the mouse: F foreground, U under the cursor, T taskbar, G our
// game window. mw_gui_plan: per slot MW_GT_SKIP (no thread), MW_GT_PRINT, or the index of the earlier slot with the
// same thread id (printed as "U=F").
enum { MW_GT_SKIP = -2, MW_GT_PRINT = -1 };
inline void mw_gui_plan(const unsigned long tids[4], int out[4]) {
  for (int i = 0; i < 4; ++i) {
    out[i] = tids[i] ? MW_GT_PRINT : MW_GT_SKIP;
    for (int j = 0; j < i && tids[i]; ++j) if (tids[j] == tids[i]) { out[i] = j; break; }
  }
}
// The GUI_* bits by name, "-" for none (the caller prints the hex beside it).
inline void mw_gui_flags(unsigned f, char* out, size_t cap) {
  static const struct { unsigned bit; const char* name; } kF[5] = { { 0x1, "CARET" }, { 0x2, "MOVESIZE" },
      { 0x4, "MENU" }, { 0x8, "SYSMENU" }, { 0x10, "POPUP" } };   // GUI_CARETBLINKING .. GUI_POPUPMENUMODE
  size_t len = 0;
  out[0] = 0;
  for (int i = 0; i < 5; ++i) {
    if (!(f & kF[i].bit)) continue;
    const int k = _snprintf_s(out + len, cap - len, _TRUNCATE, "%s%s", len ? "|" : "", kF[i].name);
    if (k < 0) return;
    len += k;
  }
  if (!len) _snprintf_s(out, cap, _TRUNCATE, "-");
}

// input.cpp: one line on every deactivation message; for 60 s after it one line a second (mw_due), and after
// that, while the game is not in front, once a second while the capture or the clip still points at this process.
void mousewatch_event(const char* what, HWND ourWnd, int ourGrab);
void mousewatch_frame(HWND ourWnd, int ourGrab);
// input.cpp, every frame: whether the main menu (ui "screen"/"menu_main", shown list) is up. A change within
// 1 s of a focus loss is logged (measurement 4).
void mousewatch_menu(int menuShown);
