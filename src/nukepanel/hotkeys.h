#pragma once
// Windows key and global hotkeys in game (docs/re/hotkeys.md): pure rules, then the glue in hotkeys.cpp.
// The engine's keyboard asks for DISCL_NOWINKEY, which dinput8 turns into RIDEV_NOHOTKEYS; we set the
// same device's cooperative level again without that flag. Nothing is written to engine memory.
#include <stddef.h>

// input.dll keyboard init 0x1000155a-0x1000156e: `mov edx,[esp+8]; mov edx,[edx+4]; mov eax,[g_kbd];
// mov ecx,[eax]; push 0x16; push edx; push eax; call [ecx+0x34]`. Unique in input.dll (Ghidra byte search,
// 2026-10-01); the imm32 at +8 is &g_kbd (0x100099f0).
enum { HK_SIG_LEN = 21, HK_SIG_SLOT = 8, HK_SIG_LEVEL = 15 };
static const unsigned char kHkKbdSig[HK_SIG_LEN] = {
  0x8B, 0x54, 0x24, 0x08, 0x8B, 0x52, 0x04, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x08, 0x6A, 0x16,
  0x52, 0x50, 0xFF, 0x51, 0x34 };
static const unsigned char kHkKbdMask[HK_SIG_LEN] = {
  1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

// input.dll mouse init 0x100011ee-0x10001202: the same shape, `push 5` (DISCL_EXCLUSIVE|DISCL_FOREGROUND) and the
// imm32 &g_mouse (0x100099ec, which the buffered read 0x10001320 loads). Unique in input.dll with the level byte
// pinned (Ghidra byte search, 2026-10-01; masked, it also matches the keyboard at 0x1000155a).
static const unsigned char kHkMouseSig[HK_SIG_LEN] = {
  0x8B, 0x54, 0x24, 0x08, 0x8B, 0x52, 0x04, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x08, 0x6A, 0x05,
  0x52, 0x50, 0xFF, 0x51, 0x34 };

// DISCL_NONEXCLUSIVE|DISCL_FOREGROUND, with (the engine's) and without DISCL_NOWINKEY 0x10.
enum { HK_LEVEL_STOCK = 0x16, HK_LEVEL_FREE = 0x06 };
// IDirectInputDevice8A slots, read in dinput8 10.0.26100.8521: the vtable after IID_IDirectInputDevice8A
// (0x100265a0), slot 13 = 0x1000cdd0 (validates the flags, DIERR_ACQUIRED while acquired). The engine
// calls the same slots (input.dll 0x1000164b, 0x1000166b, 0x1000156c).
enum { HK_VT_ACQUIRE = 7, HK_VT_UNACQUIRE = 8, HK_VT_SETCOOP = 13 };
// GetDeviceState(this, cbData, lpvData), stdcall: input.dll's joystick poll calls it at 0x10002020 (push buf,
// push 0x110, push dev; call [vt+0x24]); the mouse itself is only read buffered (vt+0x28, 0x10001367).
enum { HK_VT_GETSTATE = 9 };

// &g_kbd, the imm32 at +HK_SIG_SLOT of a match.
inline unsigned hk_sig_slot(const unsigned char* match) {
  const unsigned char* b = match + HK_SIG_SLOT;
  return (unsigned)b[0] | ((unsigned)b[1] << 8) | ((unsigned)b[2] << 16) | ((unsigned)b[3] << 24);
}
// All four bytes of the slot inside [base, base+size).
inline bool hk_slot_in_image(unsigned slot, unsigned base, unsigned size) {
  return slot >= base && size >= 4 && slot - base <= size - 4;
}
// The engine's module: a path ending in \sys\input.dll (the engine logs `linking sys/input`), never
// Windows' own SysWOW64\input.dll. Case-insensitive; either slash.
inline bool hk_is_engine_input(const char* path) {
  static const char kTail[] = "sys/input.dll";
  const int tl = (int)sizeof(kTail) - 1;
  int n = 0;
  while (path && path[n]) n++;
  if (n < tl + 1) return false;
  const char* p = path + n - tl;
  for (int i = 0; i < tl; i++) {
    char c = p[i];
    if (c == 0x5C) c = '/';
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c != kTail[i]) return false;
  }
  return p[-1] == '/' || p[-1] == 0x5C;
}
// Acquire again only when our Unacquire released an acquired device (DI_OK). DI_NOEFFECT means the
// engine had it released (the panel's inp_set_active(0), focus loss): it stays released.
inline bool hk_reacquire(long hrUnacquire) { return hrUnacquire == 0; }

// The two engine devices share the state machine below; only their engine (stock) level differs. The mouse init
// asks for 5 = DISCL_EXCLUSIVE|DISCL_FOREGROUND (input.dll 0x100011ee); both are set free at HK_LEVEL_FREE.
enum { HK_KIND_KEYBOARD = 0, HK_KIND_MOUSE = 1 };
enum { HK_LEVEL_MOUSE_STOCK = 0x05 };
inline int hk_stock_level(int kind) { return kind == HK_KIND_MOUSE ? HK_LEVEL_MOUSE_STOCK : HK_LEVEL_STOCK; }

// One device at a time per state. level: 0 = the engine's own (never touched), else the level we last
// set on dev with setWnd. A failed set waits for the switch, the device or the window to change.
struct HkState { const void* dev; const void* wnd; const void* setWnd; int level, failed, lastWant, seen; };
enum { HK_NONE = 0, HK_SET_FREE = 1, HK_SET_STOCK = 2 };
inline int hk_step(HkState* s, int want, const void* dev, const void* wnd) {
  want = want ? 1 : 0;
  if (!s->seen || want != s->lastWant) { s->seen = 1; s->lastWant = want; s->failed = 0; }
  if (dev != s->dev) { s->dev = dev; s->setWnd = 0; s->level = 0; s->failed = 0; }
  if (wnd != s->wnd) { s->wnd = wnd; s->failed = 0; }
  if (!dev || !wnd || s->failed) return HK_NONE;
  // The device keeps the window it was given: a recreated window needs our level set again with it.
  if (want && (s->level != HK_LEVEL_FREE || s->setWnd != wnd)) return HK_SET_FREE;
  if (!want && s->level == HK_LEVEL_FREE) return HK_SET_STOCK;
  return HK_NONE;
}
// The level act sets on a device of this kind; 0 for HK_NONE.
inline int hk_level_for(int kind, int act) {
  return act == HK_SET_FREE ? HK_LEVEL_FREE : act == HK_SET_STOCK ? hk_stock_level(kind) : 0;
}
inline void hk_done(HkState* s, int act, const void* wnd, int ok, int kind = HK_KIND_KEYBOARD) {
  if (!ok) { s->failed = 1; return; }
  s->level = hk_level_for(kind, act);
  s->setWnd = wnd;
}

// ------------------------------------------------------------------ engine glue (hotkeys.cpp)
#include <windows.h>
#include <stdio.h>
#include <string.h>
// Raw-input rows on usage page 1: the mouse (2) and the keyboard (6).
enum { HK_RAW_MOUSE = 2, HK_RAW_KEYBOARD = 6 };
// The row for usage among GetRegisteredRawInputDevices' got rows (-1: the call failed with err), with the flags
// dinput8's table uses named (hotkeys.md section 2): 0x230 is the exclusive mouse, 0x100 the nonexclusive one.
inline void hk_raw_row(const RAWINPUTDEVICE* rows, int got, unsigned long err, int usage, char* out, size_t cap) {
  if (got < 0) { _snprintf_s(out, cap, _TRUNCATE, "unread (err %lu)", err); return; }
  for (int i = 0; i < got; i++) {
    if (rows[i].usUsagePage != 1 || rows[i].usUsage != usage) continue;
    const unsigned long f = rows[i].dwFlags;
    char names[96] = "";   // every named bit set needs 68 + NUL
    const struct { unsigned long bit; const char* name; } kNames[] = {
      { 0x1, "REMOVE" }, { 0x30, "NOLEGACY" }, { 0x100, "INPUTSINK" },
      { 0x200, usage == HK_RAW_MOUSE ? "CAPTUREMOUSE" : "NOHOTKEYS" }, { 0x400, "APPKEYS" },
      { 0x1000, "EXINPUTSINK" }, { 0x2000, "DEVNOTIFY" } };
    for (size_t k = 0; k < sizeof kNames / sizeof kNames[0]; k++) {
      if ((f & kNames[k].bit) != kNames[k].bit) continue;
      if (names[0]) strcat_s(names, sizeof names, "|");
      strcat_s(names, sizeof names, kNames[k].name);
    }
    _snprintf_s(out, cap, _TRUNCATE, "flags 0x%lx%s%s target %p", f, names[0] ? " " : "", names,
                (void*)rows[i].hwndTarget);
    return;
  }
  _snprintf_s(out, cap, _TRUNCATE, "none registered (%d rows)", got);
}

// Once a frame on the main thread, from input_frame. want is the Settings > DISPLAY switch; off restores HK_LEVEL_STOCK
// only on a device we changed. The engine mouse always gets level 6: its exclusive raw row NOLEGACY|CAPTUREMOUSE left
// clicks dead in other programs after a focus change (docs/re/mouse-lock.md); input.cpp hides the OS cursor.
void hotkeys_frame(HWND wnd, int want);
// The engine mouse's wheel change since the last call (DIMOUSESTATE.lZ through GetDeviceState, summed over every
// read since: see hotkeys_mouse_xy_take). False while the device is missing, unacquired (panel, freecam, focus
// lost) or its read fails. Main thread.
bool hotkeys_mouse_lz(int* lz);
// The same device's lX/lY (mickeys, y down) since the last take, for mouse aim's own cursor. GetDeviceState reads
// the immediate state; input.dll reads the mouse only through GetDeviceData (input!0x10001320/0x10001690, vt+0x28),
// so the engine's buffered events and its cursor are left alone. False while unread.
bool hotkeys_mouse_xy_take(int* dx, int* dy);
// The same read for the stuck-mouse watch: its HRESULT (DIERR_NOTACQUIRED 0x8007000C when released), 1 when
// there is no device. Its deltas are kept for the other readers.
long hotkeys_mouse_probe(void);
