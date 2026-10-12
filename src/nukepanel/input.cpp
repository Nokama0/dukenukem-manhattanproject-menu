// WndProc subclass of the game window. The DInput keyboard is non-exclusive
// and the loop calls TranslateMessage, so every key the panel needs arrives
// here as a window message even in DirectInput mode (input-console.md section 4).
#include "input.h"
#include "ownership.h"
#include "engine.h"
#include "log.h"
#include "config.h"    // config_bind_get: the freecam toggle; g_cfg.winKeys
#include "ui/menu.h"   // menu_keycap_armed, panel_key_effective
#include "ui/bind.h"   // bind_swallows, bind_is_mouse
#include "display.h"   // display_wndproc_filter: the borderless style and position
#include "winwatch.h"  // win_check, win_install_mode: following the engine's game window
#include "hotkeys.h"   // hotkeys_frame: the engine keyboard without DISCL_NOWINKEY
#include "mousewatch.h" // the stuck-mouse instrumentation
#include "photo.h"      // photo_capturing: the camera's other keys wait
#include <windows.h>
#include <string.h>

static HWND s_wnd;
static WNDPROC s_orig;                  // the proc under ours on s_wnd
// Every window we instance-subclassed, with the proc under ours there: a replaced window that still
// lives keeps forwarding to its own proc, never to the new window's.
struct Subclassed { HWND h; WNDPROC orig; };
static Subclassed s_subs[8];
// The Prism3D class's own proc (prism_wndproc) once ours is the class proc: windows the engine
// creates later (gl's rend_restart) run through us from WM_NCCREATE, before apply_window_style.
static WNDPROC s_class_orig;
static int s_class_tried;
static unsigned s_class_created;
static const char kClassProp[] = "NukePanel.classproc";   // set on windows created under the class proc
static unsigned s_find_frames;
static OwnState s_own;
static int s_key = VK_INSERT;
static HCURSOR s_arrow;

static int s_client_x, s_client_y;       // client-area pixels, from the last mouse message
static int s_cx, s_cy;                   // mapped to render pixels at the last frame
static bool s_btn[2];
static int s_press[2];                   // button-down messages since the last take
static int s_wheel;
static int s_play_wheel;                 // WM_MOUSEWHEEL with the panel closed, for Scroll-wheel zoom
static unsigned s_wheel_msgs[3];         // play, panel, freecam
static int s_soft_cursor, s_cursor_checked;
static unsigned s_skipped_frames;
static ULONGLONG s_close_tick;
static int s_open_count, s_open_state;   // cursor display count and mouse_state at open
static unsigned s_installs;
static DWORD s_wnd_tid, s_frame_tid;
static int s_tid_logged, s_tid_mismatches;

// The freecam's cursor: hidden and parked at the client centre while it steers,
// so WM_MOUSEMOVE carries deltas (freecam-teleport.md section 3).
static bool s_grab;
static int s_fc_dx, s_fc_dy;             // mouse counts since the last take
static bool s_toggle_mouse_down;         // a mouse-bound toggle's last state, for its edge
static int s_tp_presses;                 // teleport presses, taken by the frame path
static int s_copy_presses, s_paste_presses;   // Copy view / Paste view presses, likewise
static int s_photo_presses;                   // Photo key presses, likewise
static int s_speed_presses;                   // Game speed key presses, taken by gamespeed_frame

// The cursor hold (ownership.h cursor_hold_block): hidden over the client and back at its centre every frame
// while we are in front and nothing wants the OS cursor. Cumulative counts for the log.
static bool s_hold;
static unsigned s_hold_ons, s_hold_warps;

// The engine console takes typing (console_takes_keys), refreshed once a frame.
static bool s_console_keys;
static void* const* s_uimgr_slot;        // the UI manager global both ui_* exports dereference
static int s_console_ready_logged, s_console_seen_logged;

struct KeyEv { int vk, repeat, mods; };
static KeyEv s_keys[32];
static unsigned s_chars[32];
static int s_key_head, s_key_tail, s_char_head, s_char_tail;   // full rings drop the newest

static void clear_ui_state(void) {
  s_btn[0] = s_btn[1] = false;
  s_press[0] = s_press[1] = 0;
  s_wheel = 0;
  s_key_head = s_key_tail = s_char_head = s_char_tail = 0;
}

static void apply(int acts) {
  if ((acts & ACT_RESET_IMPULSES) && g_eng.inp_reset_impulses) g_eng.inp_reset_impulses();
  if ((acts & ACT_INPUT_OFF) && g_eng.inp_set_active) g_eng.inp_set_active(0);
  if ((acts & ACT_INPUT_ON) && g_eng.inp_set_active) g_eng.inp_set_active(1);
}

static int mouse_state(void) { return g_eng.inp_get_mouse_state ? g_eng.inp_get_mouse_state() : -1; }

// ShowCursor returns the new display count; one show and one hide leave it unchanged.
static int cursor_count(void) {
  const int c = ShowCursor(TRUE) - 1;
  ShowCursor(FALSE);
  return c;
}

// Both tids once known; a mismatch would break the plain-statics argument in input.h.
static void note_tid(DWORD* mine, DWORD other, const char* who) {
  const DWORD tid = GetCurrentThreadId();
  if (!*mine) *mine = tid;
  if (other && tid != other && s_tid_mismatches++ < 3)
    np_log("input: THREAD MISMATCH %s tid=%lu, other side tid=%lu", who, tid, other);
  if (!s_tid_logged && s_wnd_tid && s_frame_tid) {
    s_tid_logged = 1;
    np_log("input: WndProc tid=%lu frame tid=%lu", s_wnd_tid, s_frame_tid);
  }
}

// The client centre in client and screen coordinates; false when minimised.
static bool client_centre(POINT* client, POINT* screen) {
  RECT rc;
  if (!s_wnd || !GetClientRect(s_wnd, &rc) || !cursor_hold_centre(0, 0, rc.right, rc.bottom, &client->x, &client->y))
    return false;
  *screen = *client;
  return ClientToScreen(s_wnd, screen) != 0;
}

// Back to the client centre unless already there; each warp is counted.
static void hold_centre(void) {
  POINT c, sc, now;
  if (!client_centre(&c, &sc)) return;
  if (GetCursorPos(&now) && now.x == sc.x && now.y == sc.y) return;
  SetCursorPos(sc.x, sc.y);
  s_hold_warps++;
}

// SetCursor and SetCursorPos only: no ShowCursor count and no ClipCursor, so nothing is left behind when the hold
// lets go. Whoever takes the cursor next sets its own shape (the panel's arrow, the
// freecam's hide, the window under it on a focus loss).
static void hold_set(bool on, const char* why) {
  if (on == s_hold) return;
  s_hold = on;
  if (on) {
    s_hold_ons++;
    hold_centre();
    SetCursor(NULL);
  }
  np_log("input: cursor hold %s (%s); on %u time(s), %u warp(s) so far", on ? "on" : "off", why, s_hold_ons,
         s_hold_warps);
}

// One ShowCursor each way, so the display count is left as found. No ClipCursor: the re-centring holds the cursor, and
// nothing is left clipped after a release. The capture keeps WM_MOUSEMOVE coming when a flick leaves the client area,
// as the engine's own free-fly does (input-console.md section 5); every release path ends here.
static void grab_set(bool on, const char* why) {
  if (on == s_grab) return;
  s_grab = on;
  ShowCursor(on ? FALSE : TRUE);
  s_fc_dx = s_fc_dy = 0;
  POINT c, sc;
  if (on) {
    SetCapture(s_wnd);
    if (client_centre(&c, &sc)) SetCursorPos(sc.x, sc.y);
  } else if (GetCapture() == s_wnd) {
    ReleaseCapture();
  }
  np_log("input: freecam cursor %s (%s)", on ? "grabbed" : "released", why);
}

// Only while the freecam owns input and the window is in front: alt-tab gives the cursor back.
static bool grab_wanted(void) {
  return own_owner(&s_own) == OWNER_FREECAM && s_wnd && GetForegroundWindow() == s_wnd && !IsIconic(s_wnd);
}

// Every hotkey the subclass takes asks this: its row's key, yielding to the engine
// console and never taken while the panel is open (the panel owns every key then).
static bool hotkey_pressed(int pressedVk, int row) {
  return !s_own.open && hotkey_swallows(pressedVk, config_bind_get(row), true, s_console_keys, s_own.open);
}
// The camera's four keys act only while the panel's Free camera switch arms them; disarmed, the key
// reaches the game as if unbound.
static bool cam_hotkey_pressed(int pressedVk, int row) {
  return freecam_hotkeys_armed(g_cfg.freecamOn) && hotkey_pressed(pressedVk, row);
}

// ui_wfocused's bytes (A1 <slot> 8B 80 1C 04 00 00 C3) name the UI manager global; ui_wget
// must load the same one (8B 0D <slot> at +0xE). Checked once, so the manager's null
// before the UI exists is tested here instead of dereferenced inside the engine.
static bool console_probe_ready(void) {
  if (s_uimgr_slot) return true;
  static int tried;
  if (tried || !g_eng.ui_wget || !g_eng.ui_wfocused) return false;
  tried = 1;
  const unsigned char* f = (const unsigned char*)g_eng.ui_wfocused;
  const unsigned char* g = (const unsigned char*)g_eng.ui_wget;
  static const unsigned char kTail[7] = { 0x8B, 0x80, 0x1C, 0x04, 0x00, 0x00, 0xC3 };
  if (!guarded_readable(f, 12) || !guarded_readable(g, 20) || f[0] != 0xA1 || memcmp(f + 5, kTail, 7) != 0 ||
      g[0xE] != 0x8B || g[0xF] != 0x0D || memcmp(g + 0x10, f + 1, 4) != 0) {
    np_log("input: console detection OFF: ui_wfocused/ui_wget bytes differ from the read build");
    return false;
  }
  void* const* slot;
  memcpy(&slot, f + 1, sizeof slot);
  if (!guarded_readable(slot, sizeof(void*))) {
    np_log("input: console detection OFF: slot %p unreadable", (void*)slot);
    return false;
  }
  s_uimgr_slot = slot;
  return true;
}

// Once a frame on the main thread: the shown console:system_01 window, its visible
// byte (+0x1e2, read by 0x41a130) and the focused window.
static void console_frame(void) {
  if (!console_probe_ready() || !*s_uimgr_slot) { s_console_keys = false; return; }
  if (!s_console_ready_logged++) np_log("input: console detection ready: ui manager slot %p", (void*)s_uimgr_slot);
  void* con = g_eng.ui_wget("system_01", "console", 1);
  const unsigned char* cb = (const unsigned char*)con;
  const bool visible = cb && guarded_readable(cb + 0x1e2, 1) && cb[0x1e2] != 0;
  void* focused = g_eng.ui_wfocused();
  const bool keys = console_takes_keys(con, visible, focused);
  if (con && !s_console_seen_logged++)
    np_log("input: console window %p shown (visible byte %d, focused %p)", con, visible ? 1 : 0, focused);
  if (keys != s_console_keys)
    np_log("input: console %s typing; hotkeys %s (window %p focused %p)", keys ? "takes" : "releases",
           keys ? "yield" : "act", con, focused);
  s_console_keys = keys;
}

static void freecam_toggle(void) {
  hold_set(false, "freecam toggled");
  const int acts = own_event(&s_own, EV_FREECAM);
  apply(acts);
  grab_set(grab_wanted(), s_own.freecam ? "freecam on" : "freecam off");
  np_log("input: freecam %s, acts=0x%x mouse_state=%d", s_own.freecam ? "on" : "off", acts, mouse_state());
}

// A delta from the centre, then back to it; the warp's own echo arrives at the centre as 0.
static void freecam_mouse(LPARAM l) {
  POINT c, sc;
  if (!client_centre(&c, &sc)) return;
  const int dx = (short)LOWORD(l) - c.x, dy = (short)HIWORD(l) - c.y;
  if (!dx && !dy) return;
  s_fc_dx += dx; s_fc_dy += dy;
  SetCursorPos(sc.x, sc.y);
}

static void toggle(void) {
  const bool opening = !s_own.open;
  if (opening) hold_set(false, "panel open");
  // Before the count is recorded, so the panel's restore never undoes the grab's hide.
  if (opening) grab_set(false, "panel open");
  if (opening) { s_open_count = cursor_count(); s_open_state = mouse_state(); }
  // Asked again at every open, not once a launch: the OS cursor's visibility is dinput8's and the engine's to
  // change between sessions, and the software cursor must stand in whenever it is hidden.
  if (opening) s_cursor_checked = 0;
  const int acts = own_event(&s_own, EV_TOGGLE);
  apply(acts);
  clear_ui_state();
  if (!opening) {
    const int now = cursor_count(), state = mouse_state();
    const int steps = cursor_restore_steps(s_open_count, now, s_open_state, state);
    for (int i = 0; i < steps; ++i) ShowCursor(TRUE);
    for (int i = 0; i > steps; --i) ShowCursor(FALSE);
    np_log("input: cursor count open=%d close=%d mouse_state %d->%d, %d restore step(s)",
           s_open_count, now, s_open_state, state, steps);
  }
  if (s_own.open) {
    // Seed the virtual cursor where the OS cursor is: after the unacquire it is
    // the cursor the player sees.
    POINT pt;
    if (GetCursorPos(&pt) && ScreenToClient(s_wnd, &pt)) { s_client_x = pt.x; s_client_y = pt.y; }
    SetCursor(s_arrow);
  }
  np_log("input: panel %s, acts=0x%x mouse_state=%d", s_own.open ? "open" : "closed", acts, mouse_state());
}

static void push_key(int vk, int repeat) {
  const int next = (s_key_head + 1) % 32;
  if (next == s_key_tail) return;
  // GetKeyState inside the WndProc is the state as of this message, not as of the frame that reads it.
  const int mods = (GetKeyState(VK_SHIFT) < 0 ? INPUT_MOD_SHIFT : 0) | (GetKeyState(VK_CONTROL) < 0 ? INPUT_MOD_CTRL : 0);
  s_keys[s_key_head].vk = vk; s_keys[s_key_head].repeat = repeat; s_keys[s_key_head].mods = mods;
  s_key_head = next;
}

static void push_char(unsigned ch) {
  const int next = (s_char_head + 1) % 32;
  if (next == s_char_tail) return;
  s_chars[s_char_head] = ch;
  s_char_head = next;
}

// The binding a mouse button message means (bind.h's Windows codes), 0 for anything else.
static int mouse_message_vk(UINT m, WPARAM w) {
  switch (m) {
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK: return VK_BIND_RBUTTON;
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK: return VK_BIND_MBUTTON;
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
      return GET_XBUTTON_WPARAM(w) == XBUTTON1 ? VK_BIND_XBUTTON1 : VK_BIND_XBUTTON2;
    default: return 0;
  }
}

// Keyboard and mouse messages, the ones swallowed while the panel is open.
// WM_INPUT is not among them: the engine registers no raw input.
static bool is_input_message(UINT m) {
  return (m >= WM_KEYFIRST && m <= WM_KEYLAST) || (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST);
}

static void feed(UINT msg, WPARAM w, LPARAM l) {
  switch (msg) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN: push_key((int)w, (l & (1 << 30)) ? 1 : 0); return;
    case WM_CHAR: push_char((unsigned)w); return;
    case WM_MOUSEWHEEL: s_wheel += GET_WHEEL_DELTA_WPARAM(w); return;   // screen coordinates: no position
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: s_btn[0] = true; s_press[0]++; break;
    case WM_LBUTTONUP:   s_btn[0] = false; break;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: s_btn[1] = true; s_press[1]++; break;
    case WM_RBUTTONUP:   s_btn[1] = false; break;
    case WM_MOUSEMOVE:   break;
    default: return;
  }
  s_client_x = (short)LOWORD(l);
  s_client_y = (short)HIWORD(l);
}

// Every message for s_wnd, whichever proc of ours received it; `next` is the proc under ours there.
static LRESULT handle(HWND h, UINT msg, WPARAM w, LPARAM l, WNDPROC next) {
  note_tid(&s_wnd_tid, s_frame_tid, "WndProc");
  display_wndproc_filter(h, msg, w, l);   // rewrites in place, then the engine sees the message as usual
  if (msg == WM_CLOSE) {
    const ULONGLONG now = GetTickCount64();
    if (!s_close_tick) s_close_tick = now;
    np_log("exit: WM_CLOSE at %llu", now);
  } else if (msg == WM_DESTROY) {
    np_log("exit: WM_DESTROY at %llu", GetTickCount64());
  }

  // An armed bind row gets the panel key too (AA), so the current key can be
  // chosen again; it arrives through the queue below.
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && (int)w == s_key &&
      !(s_own.open && menu_keycap_armed())) {
    if (!(l & (1 << 30))) toggle();       // bit 30: auto-repeat of a held key
    return 0;                             // never reaches the game
  }

  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && cam_hotkey_pressed((int)w, BIND_ROW_CAMERA)) {
    if (!(l & (1 << 30))) freecam_toggle();
    return 0;
  }
  // A mouse-bound toggle: input_frame polls its edge, as it did under the exclusive mouse, which sent no
  // messages; the shared mouse's button messages are swallowed here so the game never sees them.
  const int mvk = mouse_message_vk(msg, w);
  if (mvk && cam_hotkey_pressed(mvk, BIND_ROW_CAMERA)) return 0;
  // The Game speed key, in play and in flight alike; its repeats and button-ups are swallowed too.
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && hotkey_pressed((int)w, BIND_ROW_GAMESPEED)) {
    if (!(l & (1 << 30))) s_speed_presses++;
    return 0;
  }
  if (mvk && hotkey_pressed(mvk, BIND_ROW_GAMESPEED)) {
    if (msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN) s_speed_presses++;
    return 0;
  }

  // The teleport acts in the frame path (player_teleport is not called from a WndProc); the
  // grab keeps DInput unacquired, so a mouse-bound teleport arrives here as a message too.
  const bool keyDown = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && !(l & (1 << 30));
  const bool btnDown = msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN;
  if (s_own.freecam && s_grab && (keyDown || btnDown) &&
      cam_hotkey_pressed(keyDown ? (int)w : mvk, BIND_ROW_TELEPORT)) {
    if (!photo_capturing()) s_tp_presses++;   // swallowed either way: Duke must not move between tiles
    return 0;
  }
  if (s_own.freecam && s_grab && (keyDown || btnDown)) {
    const int pvk = keyDown ? (int)w : mvk;
    if (cam_hotkey_pressed(pvk, BIND_ROW_PHOTO)) { s_photo_presses++; return 0; }
    if (cam_hotkey_pressed(pvk, BIND_ROW_CAMCOPY)) { if (!photo_capturing()) s_copy_presses++; return 0; }
    if (cam_hotkey_pressed(pvk, BIND_ROW_CAMPASTE)) { if (!photo_capturing()) s_paste_presses++; return 0; }
  }

  // The shared mouse lets these through in play too; counted by owner either way.
  if (msg == WM_MOUSEWHEEL) {
    s_wheel_msgs[s_own.open ? 1 : s_own.freecam ? 2 : 0]++;
    if (!s_own.open) s_play_wheel += GET_WHEEL_DELTA_WPARAM(w);
  }

  const bool alt_f4 = msg == WM_SYSKEYDOWN && w == VK_F4 && (l & (1 << 29));
  if (s_own.open) {
    if (msg == WM_SETCURSOR && LOWORD(l) == HTCLIENT) { SetCursor(s_arrow); return TRUE; }
    if (is_input_message(msg) && !alt_f4) { feed(msg, w, l); return 0; }
  } else if (s_own.freecam) {
    if (msg == WM_SETCURSOR && LOWORD(l) == HTCLIENT && s_grab) { SetCursor(NULL); return TRUE; }
    if (msg == WM_MOUSEMOVE && s_grab) { freecam_mouse(l); return 0; }
    // Every key and button is the freecam's. Alt's WM_SYSKEYUP and WM_SYSCHAR never
    // reach DefWindowProc, so Alt (Slower) cannot open the system menu.
    if (is_input_message(msg) && !alt_f4) return 0;
  } else if (s_hold && msg == WM_SETCURSOR && LOWORD(l) == HTCLIENT) {
    SetCursor(NULL);
    return TRUE;
  }

  const LRESULT r = CallWindowProcA(next, h, msg, w, l);

  // The engine switched its input inside the call above (0x4263af and siblings).
  // Take it back only after an ON: after an OFF a second inp_set_active(0) would
  // just bump the cursor count again in mouse_state 1.
  if (msg == WM_ACTIVATE || msg == WM_ACTIVATEAPP || msg == WM_EXITMENULOOP) {
    const bool on = activation_turns_input_on(msg, (unsigned)w);
    const int acts = on ? own_event(&s_own, EV_ACTIVATE) : 0;
    apply(acts);
    if (s_own.open || s_own.freecam)
      np_log("input: msg 0x%x wParam 0x%x: engine turned input %s; %s", msg, (unsigned)w,
             on ? "on" : "off", acts ? "re-asserted off" : "no re-assert");
  }

  if ((msg == WM_ACTIVATEAPP && !w) || (msg == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE) || msg == WM_KILLFOCUS) {
    hold_set(false, "focus lost");   // before the next frame: Start or alt-tab gets the cursor at once
    grab_set(false, "focus lost");   // input_frame grabs again once the window is in front
    // After the engine's own handling and our release: what still holds the mouse (mousewatch.h).
    mousewatch_event(msg == WM_KILLFOCUS ? "WM_KILLFOCUS" : msg == WM_ACTIVATE ? "WM_ACTIVATE inactive" : "WM_ACTIVATEAPP 0",
                     h, s_grab ? 1 : 0);
  }

  // The engine may recreate its window (unproven); the next frame subclasses the new one.
  if (msg == WM_NCDESTROY && h == s_wnd) {
    np_log("input: window %p destroyed; subclass will reinstall", (void*)h);
    hold_set(false, "window destroyed");
    s_wnd = NULL;
    s_find_frames = 0;
  }
  return r;
}

static Subclassed* sub_find(HWND h) {
  for (int i = 0; i < 8; i++) if (s_subs[i].h == h) return &s_subs[i];
  return NULL;
}

// stdcall, 4 args, like prism_wndproc (0x426340 ends in RET 0x10); the class is
// registered with RegisterClassA (0x426b20), so the subclass stays ANSI.
static LRESULT CALLBACK np_wndproc(HWND h, UINT msg, WPARAM w, LPARAM l) {
  Subclassed* e = sub_find(h);
  const WNDPROC next = e ? e->orig : NULL;
  if (msg == WM_NCDESTROY && e) e->h = NULL;   // the slot is free; `next` is already read
  if (!next) return DefWindowProcA(h, msg, w, l);
  if (h == s_wnd) return handle(h, msg, w, l, next);
  return CallWindowProcA(next, h, msg, w, l);
}

// The class proc: every Prism3D window created after install_class_proc. The display filter acts
// from the first message, so a window gl creates inside rend_restart is restyled before its GL context.
static LRESULT CALLBACK np_class_wndproc(HWND h, UINT msg, WPARAM w, LPARAM l) {
  if (msg == WM_NCCREATE) {
    SetPropA(h, kClassProp, (HANDLE)1);
    if (s_class_created++ < 32)
      np_log("input: Prism3D window %p created under our class proc (#%u)", (void*)h, s_class_created);
  }
  if (h == s_wnd) return handle(h, msg, w, l, s_class_orig);
  display_wndproc_filter(h, msg, w, l);
  if (msg == WM_NCDESTROY) RemovePropA(h, kClassProp);
  return CallWindowProcA(s_class_orig, h, msg, w, l);
}

static void install_class_proc(HWND h) {
  if (s_class_tried) return;
  s_class_tried = 1;
  SetLastError(0);
  const LONG_PTR prev = SetClassLongPtrA(h, GCLP_WNDPROC, (LONG_PTR)np_class_wndproc);
  if (!prev) { np_log("input: class proc not installed (%lu)", GetLastError()); return; }
  s_class_orig = (WNDPROC)prev;
  np_log("input: Prism3D class proc %p -> ours: windows created from now on run through us", (void*)prev);
}

bool input_class_proc_early(HWND anyPrism3D) {
  install_class_proc(anyPrism3D);
  return s_class_orig != NULL;
}

// The engine's current game window, hw_get_context()+4. The context is a static in the exe's
// .data (0x49c7a0), so one guarded check covers every later read.
static HWND engine_window(void) {
  static const HWND* slot;
  static int tried;
  if (!slot) {
    if (tried || !g_eng.hw_get_context) return NULL;
    tried = 1;
    const unsigned char* ctx = (const unsigned char*)g_eng.hw_get_context();
    if (!ctx || !guarded_readable(ctx, 8)) {
      np_log("input: render context %p unreadable", (const void*)ctx);
      return NULL;
    }
    slot = (const HWND*)(ctx + 4);
    np_log("input: engine game window slot %p = %p", (const void*)slot, (void*)*slot);
  }
  return *slot;
}

struct FindCtx { DWORD pid; HWND found; int count; };

static BOOL CALLBACK find_cb(HWND h, LPARAM p) {
  FindCtx* f = (FindCtx*)p;
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  if (pid != f->pid || !IsWindowVisible(h)) return TRUE;
  char cls[32];
  if (!GetClassNameA(h, cls, sizeof cls) || strcmp(cls, "Prism3D") != 0) return TRUE;
  if (!f->found) f->found = h;
  f->count++;
  return TRUE;
}

// The visible top-level "Prism3D" window of this process. The engine's second
// window of that class (0x4a21a8) is hidden at creation (0x426db0).
static void try_install(HWND preferred) {
  FindCtx f = { GetCurrentProcessId(), NULL, 0 };
  if (preferred && IsWindow(preferred)) { f.found = preferred; f.count = 1; }
  else EnumWindows(find_cb, (LPARAM)&f);
  static int said;
  if (!f.found) {
    if (!said++) np_log("input: no visible Prism3D window yet; retrying every 30 frames");
    return;
  }
  const DWORD tid = GetWindowThreadProcessId(f.found, NULL);
  s_arrow = LoadCursor(NULL, IDC_ARROW);
  install_class_proc(f.found);
  LONG_PTR prev;
  const bool adopt = win_install_mode(s_class_orig && GetPropA(f.found, kClassProp) != NULL) == WIN_ADOPT;
  if (adopt) {
    prev = (LONG_PTR)s_class_orig;   // already ours from its first message
  } else {
    Subclassed* slot = sub_find(NULL);
    if (!slot) { np_log("input: subclass table full; %p not subclassed", (void*)f.found); return; }
    SetLastError(0);
    prev = SetWindowLongPtrA(f.found, GWLP_WNDPROC, (LONG_PTR)np_wndproc);
    if (!prev) {
      np_log("input: subclass of %p failed (%lu)", (void*)f.found, GetLastError());
      return;
    }
    slot->h = f.found;
    slot->orig = (WNDPROC)prev;
  }
  s_orig = (WNDPROC)prev;
  s_wnd = f.found;
  np_log("input: %s %p (%d candidate(s)), previous proc %p, window tid=%lu frame tid=%lu, key=0x%02X",
         adopt ? "adopted" : "subclassed", (void*)s_wnd, f.count, (void*)prev, tid, GetCurrentThreadId(), s_key);
  if (s_installs++) {
    // A new window: the engine turned its input back on when it activated, before we were on it.
    const int acts = own_event(&s_own, EV_ACTIVATE);
    apply(acts);
    if (s_own.open) SetCursor(s_arrow);
    np_log("input: REINSTALL #%u; ownership re-applied, acts=0x%x (panel %s, freecam %s)", s_installs - 1, acts,
           s_own.open ? "open" : "closed", s_own.freecam ? "on" : "off");
  }
}

// The subclass follows the engine's window (winwatch.h): cheap enough for every frame (IsWindow and
// one read of a static). Returns the window we are on now.
static HWND follow_window(void) {
  if (!s_wnd) return NULL;
  const HWND eng = engine_window();
  const int alive = IsWindow(s_wnd) != 0;
  const int v = win_check(s_wnd, alive, eng);
  if (v == WIN_KEEP) return s_wnd;
  np_log("input: window %p %s (IsWindow=%d), engine window %p; following it", (void*)s_wnd,
         v == WIN_REPLACED ? "replaced" : "gone", alive, (void*)eng);
  grab_set(false, "window replaced");
  hold_set(false, "window replaced");
  s_wnd = NULL;
  s_find_frames = 0;
  if (v == WIN_REPLACED) try_install(eng);
  return s_wnd;
}

HWND input_window_resync(void) {
  if (!follow_window()) try_install(engine_window());
  return s_wnd;
}

void input_frame(int W, int H) {
  note_tid(&s_frame_tid, s_wnd_tid, "frame");
  follow_window();
  if (!s_wnd) {
    if (s_find_frames++ % 30 == 0) try_install(engine_window());
    if (!s_wnd) return;
  }
  // With the window we follow now: a window recreated by rend_restart gets the levels set again.
  hotkeys_frame(s_wnd, g_cfg.winKeys);
  mousewatch_frame(s_wnd, s_grab ? 1 : 0);   // 1 Hz, only while no window of ours is in front
  mousewatch_menu(input_ui_window("screen", "menu_main") != NULL);   // a menu change near a focus loss

  // Nothing but the three messages turns the engine's input back on, so the
  // per-frame re-assert is a backstop. With mouse_state 1 inp_set_active(0)
  // also warps the cursor and bumps ShowCursor (0x406f00): never per frame.
  if (own_event(&s_own, EV_FRAME) & ACT_INPUT_OFF) {
    if (mouse_state() == 1) {
      if (s_skipped_frames++ == 0) np_log("input: mouse_state 1; per-frame re-assert skipped");
    } else {
      apply(ACT_INPUT_OFF);
    }
  }

  console_frame();
  // The switch went off mid-flight (the panel row or an ini edit): the flight ends the way the key ends it.
  if (freecam_switch_ends_flight(g_cfg.freecamOn, s_own.freecam != 0)) {
    np_log("input: Free camera switch off while flying; ending the flight");
    freecam_toggle();
  }
  // A mouse-bound toggle has no key message under DInput: its edge is polled here.
  const int tvk = config_bind_get(BIND_ROW_CAMERA);
  const bool tdown = bind_is_mouse(tvk) && cam_hotkey_pressed(tvk, BIND_ROW_CAMERA) &&
                     GetForegroundWindow() == s_wnd && (GetAsyncKeyState(tvk) & 0x8000) != 0;
  if (tdown && !s_toggle_mouse_down) freecam_toggle();
  s_toggle_mouse_down = tdown;
  const bool want = grab_wanted();
  grab_set(want, want ? "freecam steers" : "freecam not steering");
  // After the grab: the hold never runs on a frame the freecam owns the cursor.
  const CursorHoldIn hin = { 1, GetForegroundWindow() == s_wnd, IsIconic(s_wnd) != 0, s_own.open, s_own.freecam,
                             mouse_state() == 1 };
  const int block = cursor_hold_block(hin);
  hold_set(block == HOLD_OK, cursor_hold_name(block));
  if (s_hold) hold_centre();

  if (!s_own.open) return;
  RECT rc;
  if (GetClientRect(s_wnd, &rc)) {
    s_cx = cursor_scale(s_client_x, rc.right - rc.left, W);
    s_cy = cursor_scale(s_client_y, rc.bottom - rc.top, H);
  }
  if (!s_cursor_checked) {
    s_cursor_checked = 1;
    CURSORINFO ci;
    ci.cbSize = sizeof ci;
    const BOOL ok = GetCursorInfo(&ci);
    const int showing = ok && (ci.flags & CURSOR_SHOWING);
    s_soft_cursor = showing ? 0 : 1;
    np_log("input: GetCursorInfo ok=%d flags=0x%lx showing=%d count=%d -> software_cursor=%d",
           ok ? 1 : 0, ok ? ci.flags : 0ul, showing, cursor_count(), s_soft_cursor);
  }
}

bool input_panel_open(void) { return s_own.open != 0; }
bool input_freecam_on(void) { return s_own.freecam != 0; }
bool input_console_open(void) { return s_console_keys; }
void* input_ui_window(const char* cls, const char* name) {
  return console_probe_ready() && *s_uimgr_slot ? g_eng.ui_wget(cls, name, 1) : NULL;
}
void* input_ui_window_any(const char* cls, const char* name) {
  return console_probe_ready() && *s_uimgr_slot ? g_eng.ui_wget(cls, name, 0) : NULL;
}
bool input_freecam_steers(void) { return s_grab && own_owner(&s_own) == OWNER_FREECAM; }
void input_freecam_mouse_take(int* dx, int* dy) {
  *dx = s_fc_dx; *dy = s_fc_dy;
  s_fc_dx = s_fc_dy = 0;
}
int input_teleport_take(void) { const int n = s_tp_presses; s_tp_presses = 0; return n; }
int input_camcopy_take(void) { const int n = s_copy_presses; s_copy_presses = 0; return n; }
int input_campaste_take(void) { const int n = s_paste_presses; s_paste_presses = 0; return n; }
int input_photo_take(void) { const int n = s_photo_presses; s_photo_presses = 0; return n; }
int input_gamespeed_take(void) { const int n = s_speed_presses; s_speed_presses = 0; return n; }
HWND input_window(void) { return s_wnd; }
void input_cursor(int* x, int* y) { *x = s_cx; *y = s_cy; }
bool input_mouse_down(int button) { return button >= 0 && button < 2 && s_btn[button]; }
int input_press_take(int button) {
  if (button < 0 || button > 1) return 0;
  const int n = s_press[button];
  s_press[button] = 0;
  return n;
}
int input_wheel_take(void) { const int w = s_wheel; s_wheel = 0; return w; }
int input_play_wheel_take(void) { const int w = s_play_wheel; s_play_wheel = 0; return w; }
void input_wheel_counts(unsigned out[3]) { for (int i = 0; i < 3; i++) out[i] = s_wheel_msgs[i]; }

bool input_key_take(int* vk, int* repeat, int* mods) {
  if (s_key_tail == s_key_head) return false;
  *vk = s_keys[s_key_tail].vk; *repeat = s_keys[s_key_tail].repeat; *mods = s_keys[s_key_tail].mods;
  s_key_tail = (s_key_tail + 1) % 32;
  return true;
}

bool input_char_take(unsigned* ch) {
  if (s_char_tail == s_char_head) return false;
  *ch = s_chars[s_char_tail];
  s_char_tail = (s_char_tail + 1) % 32;
  return true;
}

int input_software_cursor(void) { return s_soft_cursor; }

void input_set_panel_key(int vk) {
  const int key = panel_key_effective(vk);
  if (key != s_key) np_log("input: panel key 0x%02X (asked 0x%02X)", key, vk);
  s_key = key;
}

unsigned long long input_close_tick(void) { return s_close_tick; }

// What input_modal_begin changed, for input_modal_end.
static int s_modal_shows;
static RECT s_modal_clip;
static bool s_modal_clipped;

void input_modal_begin(void) {
  hold_set(false, "modal dialog");
  grab_set(false, "modal dialog");
  if (s_wnd && GetCapture() == s_wnd) ReleaseCapture();
  // A clip smaller than the virtual screen is someone's (the engine's, or dinput8's): lifted, and not put back:
  // its owner re-applies it when it re-acquires, and a restored stale clip is the mouse-lock bug class.
  const RECT vs = { GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                    GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                    GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
  s_modal_clipped = GetClipCursor(&s_modal_clip) && memcmp(&s_modal_clip, &vs, sizeof vs) != 0;
  if (s_modal_clipped) ClipCursor(NULL);
  // The software-cursor case: the OS cursor is hidden by count, and the dialog needs it shown.
  s_modal_shows = 0;
  for (int c = cursor_count(); c < 0 && s_modal_shows < 64; ++s_modal_shows) c = ShowCursor(TRUE);
  SetCursor(s_arrow ? s_arrow : LoadCursor(NULL, IDC_ARROW));
  np_log("input: modal dialog: cursor let go (clip %s, %d show step(s), mouse_state %d, panel %s)",
         s_modal_clipped ? "lifted" : "none", s_modal_shows, mouse_state(), s_own.open ? "open" : "closed");
}

void input_modal_end(void) {
  for (int i = 0; i < s_modal_shows; ++i) ShowCursor(FALSE);
  clear_ui_state();
  POINT pt;
  if (s_wnd && GetCursorPos(&pt) && ScreenToClient(s_wnd, &pt)) { s_client_x = pt.x; s_client_y = pt.y; }
  if (s_own.open) SetCursor(s_arrow);
  np_log("input: modal dialog closed: %d show step(s) undone, clip %s, mouse_state %d", s_modal_shows,
         s_modal_clipped ? "left to its owner" : "none", mouse_state());
  s_modal_shows = 0;
  s_modal_clipped = false;
}
