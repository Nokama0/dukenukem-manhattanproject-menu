// Windows key and global hotkeys glue (hotkeys.h, docs/re/hotkeys.md section 4). Main thread only: the
// frame and the WndProc share the engine's one thread (input.h), and DirectInput is called from it.
#include "hotkeys.h"
#include "offscreen.h"   // offs_find: the unique-match scan
#include "engine.h"      // guarded_readable
#include "log.h"
#include "wheelzoom.h"   // the DIMOUSESTATE layout
#include <stdio.h>
#include <string.h>
#include <psapi.h>     // K32EnumProcessModules (kernel32, no psapi.lib)

static HMODULE s_mod;             // input.dll once a slot in it resolved
static void* const* s_slot;       // &g_kbd inside it
static void* const* s_mouseSlot;  // the mouse device global inside it (Scroll-wheel zoom)
static int s_scanDone, s_saidMissing;   // scanDone: both signatures refused in the loaded input.dll
static unsigned s_frames;
static HkState s_hk;
static const void* s_seenDev;     // the last device logged as seen
static HkState s_hkMouse;         // the same rules for the engine mouse, always wanted free (mouse-lock.md)
static const void* s_seenMouse;   // the last mouse device logged as seen by hotkeys_frame

typedef HRESULT(__stdcall* DevFn)(void* self);
typedef HRESULT(__stdcall* CoopFn)(void* self, HWND wnd, DWORD flags);
typedef HRESULT(__stdcall* StateFn)(void* self, DWORD cb, void* data);
static const char* owner_of(const void* p, char* buf, size_t cap);
static void raw_rows(char* out, size_t cap);

// The signature over input.dll's executable sections, from its own PE headers; -1 when unreadable.
static int scan(const unsigned char* base, const unsigned char* sig, const unsigned char** out,
                unsigned* imageSize) {
  if (!guarded_readable(base, sizeof(IMAGE_DOS_HEADER))) return -1;
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) return -1;
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
  if (!guarded_readable(nt, sizeof *nt) || nt->Signature != IMAGE_NT_SIGNATURE) return -1;
  const unsigned size = nt->OptionalHeader.SizeOfImage;
  *imageSize = size;
  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  const unsigned nsec = nt->FileHeader.NumberOfSections;
  if (!guarded_readable(sec, nsec * sizeof *sec)) return -1;
  int hits = 0;
  for (unsigned i = 0; i < nsec && hits < 2; ++i) {
    if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
    const unsigned va = sec[i].VirtualAddress, n = sec[i].Misc.VirtualSize;
    if (va >= size || n > size - va || !guarded_readable(base + va, n)) return -1;
    size_t at = 0;
    const int h = offs_find(base + va, n, sig, kHkKbdMask, HK_SIG_LEN, &at);
    if (h && !hits) *out = base + va + at;
    hits += h;
  }
  return hits;
}

// The loaded module whose path ends in sys/input.dll (hk_is_engine_input). Never by bare name: Windows
// ships its own SysWOW64 input.dll, and GetModuleHandle picks either when both are loaded.
static HMODULE engine_input_module(char* path, DWORD cap) {
  HMODULE mods[512];
  DWORD need = 0;
  if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need)) return NULL;
  const DWORD n = need / sizeof mods[0] < 512 ? need / sizeof mods[0] : 512;
  for (DWORD i = 0; i < n; i++) {
    if (GetModuleFileNameA(mods[i], path, cap) && hk_is_engine_input(path)) return mods[i];
  }
  return NULL;
}

static const void* s_mouseDev;         // the mouse device whose vtable was checked
static void* const* s_mouseVt;          // its vtable, NULL when unreadable
static unsigned s_lzReads, s_lzFails, s_lzChanges;
static long s_lzLastHr = 1;             // the last GetDeviceState result logged (1: none yet)
static long s_lzHrNow = 1;              // this call's result, 1 when no device was read
// lX/lY/lZ are changes since the previous GetDeviceState, so a read by one consumer would rob the others:
// every read adds into these, and each consumer takes its own axes.
static long s_accX, s_accY, s_accZ;

// One device global by its init signature: the slot, or NULL with the reason logged (what = "keyboard"...).
static void* const* find_slot(const unsigned char* base, const unsigned char* sig, const char* what,
                              const char* path) {
  const unsigned char* match = NULL;
  unsigned size = 0;
  char shown[MAX_PATH];
  np_log_path(path, shown, sizeof shown);
  const int hits = scan(base, sig, &match, &size);
  if (hits < 0) { np_log("hotkeys: %s OFF: %s %p: PE headers or code unreadable", what, shown, base); return NULL; }
  if (hits != 1) {
    np_log("hotkeys: %s OFF: init signature matched %d time(s) in %s %p (need exactly 1)", what, hits, shown, base);
    return NULL;
  }
  const unsigned slot = hk_sig_slot(match);
  if (!hk_slot_in_image(slot, (unsigned)(size_t)base, size) || !guarded_readable((const void*)(size_t)slot, 4)) {
    np_log("hotkeys: %s OFF: slot 0x%08x outside input.dll %p (+0x%x) or unreadable", what, slot, base, size);
    return NULL;
  }
  np_log("hotkeys: %s %p, %s init match %p (+0x%x, unique), device slot 0x%08x", shown, base, what,
         (const void*)match, (unsigned)(match - base), slot);
  return (void* const*)(size_t)slot;
}

// input.dll is loaded by the engine's input init only in DirectInput mode (i_grabpointer 1, input-console.md section
// 3): until then this retries every 60 frames. Both device slots come from one scan of the one module; a refused one
// stays refused until that module unloads.
static void resolve(int scan = 1) {
  // scan = 0 only checks that the resolved module is still loaded (hotkeys_mouse_lz), so the 60-frame retry counts
  // hotkeys_frame's calls alone.
  if (s_mod) {
    // One loader lookup a frame, no VirtualQuery: a slot is never read from an unloaded image.
    const void* anchor = s_slot ? (const void*)s_slot : (const void*)s_mouseSlot;
    HMODULE now = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)anchor, &now) && now == s_mod) return;
    np_log("hotkeys: input.dll %p is gone (slot now in %p); device slots dropped", (void*)s_mod, (void*)now);
    s_slot = NULL; s_mouseSlot = NULL; s_mod = NULL; s_frames = 0;
    memset(&s_hk, 0, sizeof s_hk);   // a reloaded input.dll may put its device at the same address
    memset(&s_hkMouse, 0, sizeof s_hkMouse);
    s_seenDev = NULL; s_seenMouse = NULL; s_mouseDev = NULL; s_mouseVt = NULL;
    return;
  }
  if (!scan || s_scanDone || s_frames++ % 60) return;
  char path[MAX_PATH];
  const HMODULE m = engine_input_module(path, sizeof path);
  if (!m) {
    if (!s_saidMissing++) np_log("hotkeys: sys/input.dll not loaded (WndProc input mode, or not yet); retrying");
    return;
  }
  const unsigned char* base = (const unsigned char*)m;
  s_slot = find_slot(base, kHkKbdSig, "keyboard", path);
  s_mouseSlot = find_slot(base, kHkMouseSig, "mouse", path);
  if (!s_mouseSlot) np_log("hotkeys: Scroll-wheel zoom reads WM_MOUSEWHEEL only");
  if (s_slot || s_mouseSlot) s_mod = m;
  else s_scanDone = 1;
}

// One GetDeviceState on the engine mouse; its deltas go into the accumulators. False when unread.
static bool read_mouse(void) {
  s_lzHrNow = 1;
  resolve(0);
  if (!s_mouseSlot) return false;
  void* dev = *s_mouseSlot;
  if (!dev) return false;
  if (dev != s_mouseDev) {
    // Checked once per device, not per frame (guarded reads are slow).
    s_mouseDev = dev;
    void* const* vt = guarded_readable(dev, sizeof(void*)) ? *(void* const* const*)dev : NULL;
    s_mouseVt = vt && guarded_readable(vt, (HK_VT_GETSTATE + 1) * sizeof(void*)) ? vt : NULL;
    char mod[MAX_PATH];
    np_log("hotkeys: mouse device %p vtable %p GetDeviceState %p (%s)", dev, (const void*)vt,
           s_mouseVt ? vt[HK_VT_GETSTATE] : NULL, s_mouseVt ? owner_of(vt[HK_VT_GETSTATE], mod, sizeof mod) : "unreadable");
  }
  if (!s_mouseVt) return false;
  unsigned char st[WZ_DIMOUSESTATE_SIZE];
  const HRESULT hr = ((StateFn)s_mouseVt[HK_VT_GETSTATE])(dev, WZ_DIMOUSESTATE_SIZE, st);
  s_lzReads++;
  s_lzHrNow = (long)hr;
  if ((long)hr != s_lzLastHr) {   // each change of result once: acquired, then DIERR_NOTACQUIRED under the panel...
    s_lzLastHr = (long)hr;
    // The rows dinput8 registered at this acquire change (mouse-lock.md (e)); its worker may still be catching up.
    char raw[192];
    raw_rows(raw, sizeof raw);
    np_log("hotkeys: mouse GetDeviceState -> 0x%08lx (change %u; read %u, failed %u so far); raw %s",
           (unsigned long)hr, ++s_lzChanges, s_lzReads, s_lzFails, raw);
  }
  if (FAILED(hr)) { s_lzFails++; return false; }
  long v[3];
  memcpy(v, st, sizeof v);   // DIMOUSESTATE lX, lY, lZ (wheelzoom.h)
  s_accX += v[0]; s_accY += v[1]; s_accZ += v[2];
  return true;
}

bool hotkeys_mouse_lz(int* lz) {
  *lz = 0;
  if (!read_mouse()) return false;
  *lz = (int)s_accZ;
  s_accZ = 0;
  return true;
}

bool hotkeys_mouse_xy_take(int* dx, int* dy) {
  *dx = 0; *dy = 0;
  if (!read_mouse()) return false;
  *dx = (int)s_accX; *dy = (int)s_accY;
  s_accX = 0; s_accY = 0;
  return true;
}

// The process's raw-input mouse and keyboard rows. dinput8 registers them only while a device is acquired:
// keyboard 0x200 NOHOTKEYS with NOWINKEY, else 0x100 INPUTSINK; mouse 0x230 NOLEGACY|CAPTUREMOUSE exclusive, else
// 0x100 (hotkeys.md section 2).
static void raw_rows(char* out, size_t cap) {
  RAWINPUTDEVICE d[16];
  UINT n = 16;
  const UINT got = GetRegisteredRawInputDevices(d, &n, sizeof d[0]);
  const unsigned long err = got == (UINT)-1 ? GetLastError() : 0;
  char m[96], k[96];
  hk_raw_row(d, got == (UINT)-1 ? -1 : (int)got, err, HK_RAW_MOUSE, m, sizeof m);
  hk_raw_row(d, got == (UINT)-1 ? -1 : (int)got, err, HK_RAW_KEYBOARD, k, sizeof k);
  _snprintf_s(out, cap, _TRUNCATE, "mouse %s; keyboard %s", m, k);
}

// The rows polled once a frame, logged on each change with a cumulative count: dinput8's worker registers and
// removes them asynchronously, so a line at an acquire change alone can read them a moment early.
static unsigned s_rawChanges;
static char s_rawLast[192];
static void raw_watch(void) {
  char now[192];
  raw_rows(now, sizeof now);
  if (strcmp(now, s_rawLast) == 0) return;
  strcpy_s(s_rawLast, sizeof s_rawLast, now);
  np_log("hotkeys: raw rows change %u: %s", ++s_rawChanges, now);
}

static const char* owner_of(const void* p, char* buf, size_t cap) {
  HMODULE m = NULL;
  buf[0] = 0;
  if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCSTR)p, &m) || !GetModuleFileNameA(m, buf, (DWORD)cap)) return "?";
  const char* slash = strrchr(buf, 0x5C);
  return slash ? slash + 1 : buf;
}

// Unacquire, SetCooperativeLevel, then Acquire only if the Unacquire released an acquired device, so a device
// the panel or a focus loss released stays released. The engine's later set_active calls keep the new level.
static bool set_level(const char* what, void* dev, HWND wnd, int level, int before) {
  void* const* vt = guarded_readable(dev, sizeof(void*)) ? *(void* const* const*)dev : NULL;
  if (!vt || !guarded_readable(vt, (HK_VT_SETCOOP + 1) * sizeof(void*))) {
    np_log("hotkeys: %s %p vtable %p unreadable; left alone", what, dev, (const void*)vt);
    return false;
  }
  char mod[MAX_PATH], rawBefore[192], rawAfter[192];
  const char* owner = owner_of(vt[HK_VT_SETCOOP], mod, sizeof mod);
  raw_rows(rawBefore, sizeof rawBefore);
  const HRESULT hrU = ((DevFn)vt[HK_VT_UNACQUIRE])(dev);
  if (FAILED(hrU)) {
    np_log("hotkeys: %s %p Unacquire 0x%08lx; level left at 0x%02x", what, dev, (unsigned long)hrU, before);
    return false;
  }
  const HRESULT hrC = ((CoopFn)vt[HK_VT_SETCOOP])(dev, wnd, (DWORD)level);
  const bool again = hk_reacquire((long)hrU);
  const HRESULT hrA = again ? ((DevFn)vt[HK_VT_ACQUIRE])(dev) : 0;
  raw_rows(rawAfter, sizeof rawAfter);
  char acq[32];
  if (again) sprintf_s(acq, sizeof acq, "0x%08lx", (unsigned long)hrA);
  else strcpy_s(acq, sizeof acq, "skipped (was released)");
  np_log("hotkeys: %s %p window %p: level 0x%02x -> 0x%02x %s; Unacquire 0x%08lx, SetCooperativeLevel "
         "0x%08lx (%s), Acquire %s; raw before: %s; after: %s", what, dev, (void*)wnd, before,
         SUCCEEDED(hrC) ? level : before, SUCCEEDED(hrC) ? "set" : "REFUSED", (unsigned long)hrU,
         (unsigned long)hrC, owner, acq, rawBefore, rawAfter);
  return SUCCEEDED(hrC);
}

// One device through the shared state machine: logs a new device once, then sets the level hk_step asks for.
static void apply(HkState* st, const void** seen, int kind, void* dev, HWND wnd, int want) {
  const char* what = kind == HK_KIND_MOUSE ? "mouse" : "keyboard";
  if (dev != *seen) {
    *seen = dev;
    char raw[192];
    raw_rows(raw, sizeof raw);
    np_log("hotkeys: %s device %p is new (its init set 0x%02x; not set by us yet); raw %s; switch %s", what, dev,
           hk_stock_level(kind), raw, want ? "on" : "off");
  }
  const int act = hk_step(st, want, dev, wnd);
  if (act == HK_NONE) return;
  const int before = st->level ? st->level : hk_stock_level(kind);
  hk_done(st, act, wnd, set_level(what, dev, wnd, hk_level_for(kind, act), before) ? 1 : 0, kind);
}

long hotkeys_mouse_probe(void) {
  read_mouse();   // its deltas stay in the accumulators for the wheel and mouse aim
  return s_lzHrNow;
}

void hotkeys_frame(HWND wnd, int want) {
  raw_watch();
  resolve();
  if (s_slot) apply(&s_hk, &s_seenDev, HK_KIND_KEYBOARD, *s_slot, wnd, want);
  if (s_mouseSlot) apply(&s_hkMouse, &s_seenMouse, HK_KIND_MOUSE, *s_mouseSlot, wnd, 1);
}
