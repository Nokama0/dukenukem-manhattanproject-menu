#pragma once
// Which game window the WndProc subclass should be on. On gl every rend_restart destroys and recreates it
// (core_destroy_context_gwindow 0x426e70, gl!0x10002671 mode_init -> core_create_context_gwindow 0x426db0) and GLDirect
// restores its saved proc first, so the old subclass never sees WM_NCDESTROY. Current window: hw_get_context()+4.

enum { WIN_KEEP = 0, WIN_REPLACED = 1, WIN_GONE = 2 };
// stored: the window we are on (NULL: none yet, the search path runs); storedAlive: IsWindow(stored);
// engineWnd: the engine's current game window, NULL when unknown or between shutdown and init.
inline int win_check(const void* stored, int storedAlive, const void* engineWnd) {
  if (!stored) return WIN_KEEP;
  if (engineWnd && engineWnd != stored) return WIN_REPLACED;
  if (!storedAlive) return WIN_GONE;
  return WIN_KEEP;
}

// A window created while our class proc was the Prism3D class's proc already runs through us:
// adopt it. Subclassing it again would put us in its chain twice (every key handled twice).
enum { WIN_SUBCLASS = 0, WIN_ADOPT = 1 };
inline int win_install_mode(int createdUnderClassProc) {
  return createdUnderClassProc ? WIN_ADOPT : WIN_SUBCLASS;
}
