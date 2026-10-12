// The stuck-mouse watch (mousewatch.h). Main thread only; logging only, nothing is released or changed.
#include "mousewatch.h"
#include "hotkeys.h"   // hotkeys_mouse_probe: the engine mouse device's GetDeviceState result
#include "log.h"
#include "engine.h"   // guarded_readable: the dinput8 worker reads
#include <stdio.h>
#include <string.h>

static ULONGLONG s_nextMs, s_lostAt;
static unsigned s_events, s_inactiveLines, s_inactiveSuspect, s_afterLoss;
static POINT s_lastPt;
// Measurement 4: the main menu's last change (GetTickCount64, 0 = none) and the transition last logged.
static int s_menuShown = -1;
static ULONGLONG s_menuAt, s_menuLoggedAt;
static unsigned s_menuNear;
// Measurement 3: dinput8's base, and 1 = the read build (reads on), -1 = another build or unreadable (off).
static const unsigned char* s_dn8Base;
static int s_dn8State;

struct Caps { DWORD pid; DWORD tids[16]; int ntids; char text[400]; int len; int ours; };

static void class_of(HWND h, char* out, int cap) {
  out[0] = 0;
  if (!h || !GetClassNameA(h, out, cap)) strcpy_s(out, cap, h ? "?" : "-");
}

// Every thread of this process that owns a window, once: GetCapture answers for the calling thread only, so
// dinput8's own window thread is asked through GetGUIThreadInfo.
static BOOL CALLBACK caps_cb(HWND h, LPARAM p) {
  Caps* c = (Caps*)p;
  DWORD pid = 0;
  const DWORD tid = GetWindowThreadProcessId(h, &pid);
  if (pid != c->pid) return TRUE;
  for (int i = 0; i < c->ntids; ++i) if (c->tids[i] == tid) return TRUE;
  if (c->ntids >= 16) return FALSE;
  c->tids[c->ntids++] = tid;
  GUITHREADINFO gi;
  gi.cbSize = sizeof gi;
  if (GetGUIThreadInfo(tid, &gi) && (gi.hwndCapture || (gi.flags & GUI_INMOVESIZE))) {
    char cls[48];
    class_of(gi.hwndCapture, cls, sizeof cls);
    const int k = _snprintf_s(c->text + c->len, sizeof c->text - c->len, _TRUNCATE, " tid %lu capture %p (%s)%s",
                              tid, (void*)gi.hwndCapture, cls, (gi.flags & GUI_INMOVESIZE) ? " in move/size" : "");
    if (k > 0) c->len += k;
    c->ours = 1;
  }
  return TRUE;
}

// The local wall-clock time, so the user can name the minute it happened.
static void wall(char* out, size_t cap) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  _snprintf_s(out, cap, _TRUNCATE, "%02u:%02u:%02u", t.wHour, t.wMinute, t.wSecond);
}

// The cursor, what lies under it, the held buttons and modifiers, and our window's own state.
static void extra(char* out, size_t cap, HWND ourWnd) {
  POINT pt = { 0, 0 };
  const BOOL ptOk = GetCursorPos(&pt);
  const bool moved = ptOk && (pt.x != s_lastPt.x || pt.y != s_lastPt.y);
  if (ptOk) s_lastPt = pt;
  const HWND under = ptOk ? WindowFromPoint(pt) : NULL;
  DWORD underPid = 0;
  if (under) GetWindowThreadProcessId(under, &underPid);
  char underCls[48];
  class_of(under, underCls, sizeof underCls);
  static const int kVk[7] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_MENU, VK_CONTROL, VK_SHIFT, VK_LWIN };
  unsigned bits = 0;
  for (int i = 0; i < 7; ++i) if (GetAsyncKeyState(kVk[i]) & 0x8000) bits |= 1u << i;
  if (GetAsyncKeyState(VK_RWIN) & 0x8000) bits |= MW_K_WIN;
  char keys[8];
  mw_keys(bits, keys);
  RECT wr = { 0, 0, 0, 0 };
  const BOOL wrOk = ourWnd && GetWindowRect(ourWnd, &wr);
  const LONG_PTR ex = ourWnd ? GetWindowLongPtrA(ourWnd, GWL_EXSTYLE) : 0;
  _snprintf_s(out, cap, _TRUNCATE,
              "; cursor %s(%ld,%ld)%s, under it %p (%s, %s); held %s; game window visible %d topmost %d rect %s%ld,%ld-%ld,%ld",
              ptOk ? "" : "UNREAD ", pt.x, pt.y, moved ? " moved" : " still", (void*)under, underCls,
              !under ? "-" : underPid == GetCurrentProcessId() ? "ours" : "another process", keys,
              ourWnd && IsWindowVisible(ourWnd) ? 1 : 0, (ex & WS_EX_TOPMOST) ? 1 : 0, wrOk ? "" : "unread ",
              wr.left, wr.top, wr.right, wr.bottom);
}

// One line of state; returns whether anything still points at this process (a capture, a narrowed clip).
static bool snapshot(char* out, size_t cap, HWND ourWnd, int ourGrab) {
  Caps c;
  memset(&c, 0, sizeof c);
  c.pid = GetCurrentProcessId();
  EnumWindows(caps_cb, (LPARAM)&c);
  const HWND cap0 = GetCapture();
  RECT clip = { 0, 0, 0, 0 };
  GetClipCursor(&clip);
  const RECT desk = { GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                      GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                      GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
  const bool clipped = clip_is_restricted(clip, desk);
  CURSORINFO ci;
  ci.cbSize = sizeof ci;
  const BOOL ciOk = GetCursorInfo(&ci);
  const HWND fg = GetForegroundWindow();
  DWORD fgPid = 0;
  GetWindowThreadProcessId(fg, &fgPid);
  char fgCls[48];
  class_of(fg, fgCls, sizeof fgCls);
  const long hr = hotkeys_mouse_probe();
  _snprintf_s(out, cap, _TRUNCATE,
              "GetCapture %p; process captures:%s; clip %ld,%ld-%ld,%ld of desktop %ld,%ld-%ld,%ld%s; cursor flags "
              "0x%lx%s; our grab %d; game window %p; foreground %p (%s, %s); mouse GetDeviceState 0x%08lx",
              (void*)cap0, c.len ? c.text : " none", clip.left, clip.top, clip.right, clip.bottom, desk.left, desk.top,
              desk.right, desk.bottom, clipped ? " RESTRICTED" : "", ciOk ? ci.flags : 0ul, ciOk ? "" : " (unread)",
              ourGrab, (void*)ourWnd, (void*)fg, fgCls, fgPid == c.pid ? "ours" : "another process", (unsigned long)hr);
  return c.ours || cap0 || clipped;
}

// dinput8's version from its own RT_VERSION resource (kernel32 only: no version.lib), once per base.
static void dn8_resolve(const unsigned char* base) {
  s_dn8Base = base;
  s_dn8State = -1;
  unsigned ms = 0, ls = 0;
  HRSRC r = FindResourceA((HMODULE)base, MAKEINTRESOURCEA(1), MAKEINTRESOURCEA(16));   // RT_VERSION
  HGLOBAL g = r ? LoadResource((HMODULE)base, r) : NULL;
  const unsigned char* p = g ? (const unsigned char*)LockResource(g) : NULL;
  const DWORD n = r ? SizeofResource((HMODULE)base, r) : 0;
  const bool have = p && mw_fixed_version(p, n < 256 ? n : 256, &ms, &ls);
  // The two globals lie in the image's data, mapped as long as this base is loaded: checked once here.
  const bool mapped = guarded_readable(base + MW_DN8_RAW_RVA, MW_DN8_CTX_RVA + 4 - MW_DN8_RAW_RVA);
  if (have && mw_dn8_known(ms, ls) && mapped) s_dn8State = 1;
  np_log("mousewatch: dinput8 at %p version %s%u.%u.%u.%u: worker reads %s", (const void*)base, have ? "" : "UNREAD ",
         ms >> 16, ms & 0xFFFF, ls >> 16, ls & 0xFFFF,
         s_dn8State == 1 ? "on" : !mapped ? "off (globals unreadable)" : "off (not the build read, 10.0.26100.8521)");
}

static void dn8_state(char* out, size_t cap) {
  const unsigned char* base = (const unsigned char*)GetModuleHandleA("dinput8.dll");
  if (!base) { _snprintf_s(out, cap, _TRUNCATE, " dinput8 not loaded"); return; }
  if (base != s_dn8Base) dn8_resolve(base);
  if (s_dn8State != 1) { _snprintf_s(out, cap, _TRUNCATE, " dinput8 reads off"); return; }
  unsigned raw;
  const unsigned char* ctx;
  memcpy(&raw, base + MW_DN8_RAW_RVA, sizeof raw);
  memcpy(&ctx, base + MW_DN8_CTX_RVA, sizeof ctx);
  if (!ctx) { _snprintf_s(out, cap, _TRUNCATE, " dinput8 %s ctx 0", raw ? "raw" : "HOOK mode"); return; }
  if (!guarded_readable(ctx, MW_DN8_CTX_SPAN)) {   // the one guarded read per line
    _snprintf_s(out, cap, _TRUNCATE, " dinput8 %s ctx %p UNREAD", raw ? "raw" : "HOOK mode", (const void*)ctx);
    return;
  }
  unsigned v[MW_DN8_CTX_SPAN / 4];
  memcpy(v, ctx, sizeof v);
  _snprintf_s(out, cap, _TRUNCATE, " dinput8 %s ctx %p kbd count %u excl %u hook 0x%x, mouse count %u excl %u hook 0x%x",
              raw ? "raw" : "HOOK mode", (const void*)ctx, v[MW_DN8_CTX_KBD_COUNT / 4], v[MW_DN8_CTX_KBD_EXCL / 4],
              v[MW_DN8_CTX_KBD_HOOK / 4], v[MW_DN8_CTX_MOUSE_COUNT / 4], v[MW_DN8_CTX_MOUSE_EXCL / 4],
              v[MW_DN8_CTX_MOUSE_HOOK / 4]);
}

// Measurements 1-3 as a companion line (the main line is near the log's 1 KB cap): the last input tick, the
// process's raw-input rows, dinput8's worker state.
static void measure_line(const char* at) {
  LASTINPUTINFO li;
  li.cbSize = sizeof li;
  const BOOL liOk = GetLastInputInfo(&li);
  const DWORD tick = GetTickCount();
  RAWINPUTDEVICE rows[8];
  UINT total = 0;
  int n = -1, all = -1;
  if (GetRegisteredRawInputDevices(NULL, &total, sizeof(RAWINPUTDEVICE)) != (UINT)-1) {
    all = (int)total;
    UINT cap = 8;
    n = total ? (int)GetRegisteredRawInputDevices(rows, &cap, sizeof(RAWINPUTDEVICE)) : 0;
    if (n < 0) n = 0;   // more than 8 rows: the count still says how many
  }
  char raw[200], dn8[200];
  mw_raw_summary(rows, n, all, raw, sizeof raw);
  dn8_state(dn8, sizeof dn8);
  np_log("mousewatch: %s   last input %s%lu ms ago (tick %lu);%s;%s", at, liOk ? "" : "UNREAD ",
         liOk ? (unsigned long)(tick - li.dwTime) : 0ul, liOk ? (unsigned long)li.dwTime : 0ul, raw, dn8);
}

// The executable name of a window's process, "?" when it cannot be opened (limited rights suffice).
static void exe_of(DWORD pid, char* out, int cap) {
  strcpy_s(out, cap, "?");
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) return;
  char path[MAX_PATH];
  DWORD n = sizeof path;
  if (QueryFullProcessImageNameA(h, 0, path, &n)) {
    const char* b = strrchr(path, '\\');
    _snprintf_s(out, cap, _TRUNCATE, "%s", b ? b + 1 : path);   // long exe names truncate, never abort
  }
  CloseHandle(h);
}

// One thread's GetGUIThreadInfo as text: flags, capture (class, exe, ours/foreign), menu owner, move/size, and for
// the foreground thread the active and focus windows.
static int gui_text(char* out, size_t cap, DWORD tid, bool withFocus) {
  GUITHREADINFO gi;
  gi.cbSize = sizeof gi;
  if (!GetGUIThreadInfo(tid, &gi)) return _snprintf_s(out, cap, _TRUNCATE, "tid %lu - (err %lu)", tid, GetLastError());
  char names[48], cls[40], exe[40];
  mw_gui_flags(gi.flags, names, sizeof names);
  int len = _snprintf_s(out, cap, _TRUNCATE, "tid %lu 0x%lx %s capture ", tid, gi.flags, names);
  if (len < 0) return (int)cap - 1;
  if (gi.hwndCapture) {
    DWORD pid = 0;
    GetWindowThreadProcessId(gi.hwndCapture, &pid);
    class_of(gi.hwndCapture, cls, sizeof cls);
    exe_of(pid, exe, sizeof exe);
    const int k = _snprintf_s(out + len, cap - len, _TRUNCATE, "%p (%s, %s, %s)", (void*)gi.hwndCapture, cls, exe,
                              !pid ? "?" : pid == GetCurrentProcessId() ? "ours" : "FOREIGN");
    len = k < 0 ? (int)cap - 1 : len + k;
  } else {
    const int k = _snprintf_s(out + len, cap - len, _TRUNCATE, "-");
    len = k < 0 ? (int)cap - 1 : len + k;
  }
  const struct { const char* n; HWND h; } kW[4] = { { "menu owner", gi.hwndMenuOwner }, { "move/size", gi.hwndMoveSize },
                                                   { "active", withFocus ? gi.hwndActive : NULL },
                                                   { "focus", withFocus ? gi.hwndFocus : NULL } };
  for (int i = 0; i < 4 && len > 0 && (size_t)len < cap; ++i) {
    if (!kW[i].h) continue;
    const int k = _snprintf_s(out + len, cap - len, _TRUNCATE, " %s %p", kW[i].n, (void*)kW[i].h);
    len = k < 0 ? (int)cap - 1 : len + k;
  }
  return len;
}

// Hypothesis (c): another process holds the capture or a modal loop. A second companion line (the first is near the
// 1 KB log cap): GetGUIThreadInfo of F foreground, U under the cursor, T taskbar (re-resolved if it dies), G ours.
static void gui_line(const char* at, HWND ourWnd) {
  static HWND s_tray;
  if (!s_tray || !IsWindow(s_tray)) s_tray = FindWindowA("Shell_TrayWnd", NULL);
  POINT pt;
  const HWND under = GetCursorPos(&pt) ? WindowFromPoint(pt) : NULL;
  const HWND wins[4] = { GetForegroundWindow(), under, s_tray, ourWnd };
  static const char kN[4] = { 'F', 'U', 'T', 'G' };
  unsigned long tids[4];
  for (int i = 0; i < 4; ++i) tids[i] = wins[i] ? GetWindowThreadProcessId(wins[i], NULL) : 0;
  int plan[4];
  mw_gui_plan(tids, plan);
  char line[900];
  int len = _snprintf_s(line, sizeof line, _TRUNCATE, "mousewatch: %s gui threads:", at);
  for (int i = 0; i < 4 && len > 0 && (size_t)len < sizeof line; ++i) {
    char t[260];
    int k;
    if (plan[i] == MW_GT_SKIP) {
      k = _snprintf_s(line + len, sizeof line - len, _TRUNCATE, " %c none;", kN[i]);
    } else if (plan[i] >= 0) {
      k = _snprintf_s(line + len, sizeof line - len, _TRUNCATE, " %c=%c;", kN[i], kN[plan[i]]);
    } else {
      gui_text(t, sizeof t, tids[i], i == 0);
      k = _snprintf_s(line + len, sizeof line - len, _TRUNCATE, " %c %s;", kN[i], t);
    }
    len = k < 0 ? (int)sizeof line - 1 : len + k;
  }
  np_log("%s", line);
}

void mousewatch_menu(int menuShown) {
  if (s_menuShown < 0) { s_menuShown = menuShown; return; }   // the first frame is a state, not a change
  if (menuShown == s_menuShown) return;
  s_menuShown = menuShown;
  const ULONGLONG now = GetTickCount64();
  s_menuAt = now;
  if (!mw_menu_near(now, s_lostAt) || s_menuLoggedAt == now) return;
  s_menuLoggedAt = now;
  s_menuNear++;
  char at[16];
  wall(at, sizeof at);
  np_log("mousewatch: %s main menu %s %llu ms after the focus loss (#%u near a loss)", at, menuShown ? "opened" : "closed",
         now - s_lostAt, s_menuNear);
}

void mousewatch_event(const char* what, HWND ourWnd, int ourGrab) {
  char line[900], more[400], at[16];
  const bool suspect = snapshot(line, sizeof line, ourWnd, ourGrab);
  extra(more, sizeof more, ourWnd);
  wall(at, sizeof at);
  s_events++;
  np_log("mousewatch: %s %s (#%u)%s: %s%s", at, what, s_events, suspect ? " SUSPECT" : "", line, more);
  measure_line(at);
  gui_line(at, ourWnd);
  const ULONGLONG now = GetTickCount64();
  // Measurement 4, the other side: a menu change in the second before this loss (logged once per change).
  if (mw_menu_near(s_menuAt, now) && s_menuLoggedAt != s_menuAt) {
    s_menuLoggedAt = s_menuAt;
    s_menuNear++;
    np_log("mousewatch: %s main menu %s %llu ms before this focus loss (#%u near a loss)", at,
           s_menuShown ? "opened" : "closed", now - s_menuAt, s_menuNear);
  }
  s_nextMs = now + MW_PERIOD_MS;
  s_lostAt = now;   // the 60 s unconditional window starts (again) here
}

// Once a second while no window of this process is in front: every second of the 60 after a focus loss, then a
// line only while something still points here.
void mousewatch_frame(HWND ourWnd, int ourGrab) {
  const ULONGLONG now = GetTickCount64();
  if (now < s_nextMs) return;
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  const int ourFront = pid == GetCurrentProcessId() ? 1 : 0;
  const bool due = mw_due(now, s_lostAt, s_nextMs, ourFront);
  s_nextMs = now + MW_PERIOD_MS;
  if (ourFront) return;
  char line[900], more[400], at[16];
  const bool suspect = snapshot(line, sizeof line, ourWnd, ourGrab);
  if (due) {
    extra(more, sizeof more, ourWnd);
    wall(at, sizeof at);
    s_afterLoss++;
    np_log("mousewatch: %s inactive %llu s after the focus loss (#%u)%s: %s%s", at, (now - s_lostAt) / 1000,
           s_afterLoss, suspect ? " SUSPECT" : "", line, more);
    measure_line(at);
    gui_line(at, ourWnd);
    return;
  }
  if (!suspect) return;
  s_inactiveSuspect++;
  if (s_inactiveLines++ < 600) {
    extra(more, sizeof more, ourWnd);
    wall(at, sizeof at);
    np_log("mousewatch: %s inactive and SUSPECT (%u s so far): %s%s", at, s_inactiveSuspect, line, more);
    measure_line(at);
    gui_line(at, ourWnd);
  }
}
