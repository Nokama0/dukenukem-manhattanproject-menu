// winmm.dll proxy, the load vector. winmm is not a KnownDLL, so a copy beside DukeNukemMP.exe is consulted before
// the real one. Stub table, not typed forwards: x86 MSVC has __declspec(naked), so the generated proxy_stubs.cpp holds
// one naked jump-thunk per export rather than 193 hand-written typed wrappers.

#include <windows.h>
#include <stdio.h>
#include "log.h"
#include "proxy_count.h"
#include "engine.h"
#include "probe.h"
#include "view.h"
#include "instruments.h"
#include "hostgate.h"
#include "input.h"
#include "canvas.h"
#include "nocull.h"
#include "display.h"
#include "config.h"
#include "stall.h"

extern "C" void* g_np_real[NP_EXPORT_COUNT];
extern "C" void* g_np_missing[NP_EXPORT_COUNT];
extern "C" const char* const g_np_names[NP_EXPORT_COUNT];
extern "C" const unsigned short g_np_ords[NP_EXPORT_COUNT];

static CRITICAL_SECTION g_np_cs;
static volatile LONG g_np_resolved = 0;

// Called only when a caller invokes a missing export (never at resolve time). It never returns rather than guess
// the export's calling convention: an untyped "return 0" would corrupt the stack of any stdcall export with parameters.
extern "C" void np_report_missing(int i) {
  const char* name = g_np_names[i] ? g_np_names[i] : "(NONAME)";
  np_log("winmm: MISSING export %s called", name);
  char msg[256];
  _snprintf_s(msg, sizeof(msg), _TRUNCATE,
              "Nuke Panel: winmm export %s is missing from the system winmm.dll; "
              "delete winmm.dll from the game folder.", name);
  FatalAppExitA(0, msg);
}

// Loads the real winmm.dll and fills every g_np_real[i] slot, once. Called
// only from a stub's first-call thunk, never from DllMain -- LoadLibrary
// under the loader lock is the classic way to deadlock a process.
extern "C" void np_resolve_all(int) {
  if (g_np_resolved) return;
  EnterCriticalSection(&g_np_cs);
  if (g_np_resolved) { LeaveCriticalSection(&g_np_cs); return; }

  // Absolute path, always: a bare LoadLibraryA("winmm.dll") would resolve back to this very file. Under WOW64
  // (this game is 32-bit), GetSystemDirectoryA + redirection lands on the real SysWOW64\winmm.dll.
  char sys[MAX_PATH];
  const UINT sn = GetSystemDirectoryA(sys, MAX_PATH);
  int resolved = 0;
  HMODULE real = NULL;
  if (sn != 0 && sn < MAX_PATH - 16) {
    strcat_s(sys, MAX_PATH, "\\winmm.dll");
    real = LoadLibraryA(sys);
  }

  if (real) {
    for (int i = 0; i < NP_EXPORT_COUNT; i++) {
      FARPROC p = g_np_names[i] ? GetProcAddress(real, g_np_names[i])
                                 : GetProcAddress(real, MAKEINTRESOURCEA(g_np_ords[i]));
      if (p) {
        g_np_real[i] = (void*)p;
        resolved++;
      } else {
        np_log("winmm: export not found: %s (ordinal %u)",
               g_np_names[i] ? g_np_names[i] : "(NONAME)", g_np_ords[i]);
        g_np_real[i] = g_np_missing[i];
      }
    }
  } else {
    np_log("winmm: failed to load real winmm.dll from %s", sn ? sys : "(GetSystemDirectoryA failed)");
  }

  np_log("winmm: real loaded, %d/%d resolved", resolved, NP_EXPORT_COUNT);
  InterlockedExchange(&g_np_resolved, 1);
  LeaveCriticalSection(&g_np_cs);
}

static int s_is_game;

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(inst);
    // Needed for winmm forwarding itself (np_resolve_all), regardless of host.
    InitializeCriticalSection(&g_np_cs);

    // DukeNukemMP.exe (Enhanced launcher) loads this proxy too and is not prism3d.exe: it gets forwarding only, no
    // log file, engine_resolve or detours (it would roll nukepanel.log to .prev on every launch).
    char path[MAX_PATH];
    s_is_game = GetModuleFileNameA(NULL, path, MAX_PATH) != 0 && host_is_game(path);
    if (!s_is_game) return TRUE;
    np_log_enable();

    np_log("attach pid=%lu tid=%lu", GetCurrentProcessId(), GetCurrentThreadId());
    // prism3d.exe is mapped and none of its code has run yet: resolving its
    // exports and patching rend_end_frame / rend_camera_push here needs no LoadLibrary and no engine call.
    engine_resolve();
    // The camera hook is useful only with the end-frame hook that clears its latch.
    const int end_frame = probe_install();
    const int camera = end_frame ? view_install() : 0;
    const int quit = canvas_quit_install();
    // Only with the end-frame hook: nocull_frame there is what ever turns the force off.
    const int nocull = end_frame ? nocull_install() : 0;
    np_log("detours: end_frame=%d camera=%d quit=%d nocull=%d/3", end_frame, camera, quit, nocull);
    // Borderless at launch needs the frame path that checks it and reverts it. Switch off: no hook at
    // all, so the launch is stock (-1, the ini unreadable: the hook's own config_load decides).
    if (end_frame) {
      if (config_peek_borderless() != 0) display_early_install();
      else np_log("display: Borderless off: no launch hook");
    }
  } else if (reason == DLL_PROCESS_DETACH) {
    if (!s_is_game) return TRUE;
    instruments_detach();
    stall_detach();
    const ULONGLONG now = GetTickCount64(), closed = input_close_tick();
    if (closed) np_log("exit: detach at %llu (%llu ms after WM_CLOSE)", now, now - closed);
    else np_log("exit: detach at %llu (no WM_CLOSE seen)", now);
    np_log("detach dropped=%ld", np_log_dropped());
  }
  return TRUE;
}
