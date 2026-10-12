#pragma once
// Screenshot mode (docs/re/quick-wins.md): with the freecam flying and the switch on, the game's HUD, our overlays,
// the cutscene letterbox and level-end stats all go. The HUD is one UI screen, "game_view" (ui_hdl_hud, duke
// 0x101744cc), hidden/shown with ui_whide/ui_wshow like a cutscene does; the g_hud stash only cleans up an old build.
#include <stdlib.h>

inline int shot_active(int freecamOn, int switchOn) { return (freecamOn && switchOn) ? 1 : 0; }

// This frame's act on the HUD screen. hidByUs: we hid it and have not shown it back. Hidden again whenever
// the game shows it while active (a new level's `ui s:l game_view...`). Shown back only if we hid it, and
// not under a cutscene's letterbox, whose own end shows it (then the claim is just dropped).
enum { SHOT_NONE = 0, SHOT_HIDE = 1, SHOT_RESTORE = 2, SHOT_FORGET = 3 };
inline int shot_hud_act(int active, int hidByUs, int hudVisible, int letterboxShown) {
  if (active) return hudVisible ? SHOT_HIDE : SHOT_NONE;
  if (!hidByUs) return SHOT_NONE;
  return letterboxShown ? SHOT_FORGET : SHOT_RESTORE;
}

// The cutscene letterbox and level-end stats are system windows too (ui_create_system_windows duke 0x100e0b30):
// `system_03:letterbox` (scr_letterbox.sc: two black ui_text bars, no viewport change) and `system_game2:partend`
// (scr_part_end.sc). Same acts as the HUD; wanted: the game still wants it up, read from its handler.
inline int shot_win_act(int active, int hidByUs, int shown, int wanted) {
  if (active) return shown ? SHOT_HIDE : SHOT_NONE;
  if (!hidByUs) return SHOT_NONE;
  return wanted ? SHOT_RESTORE : SHOT_FORGET;
}
// ui_hdl_letterbox +0x1d4 (tick duke 0x100a2e50): 0 idle, 1 held, 2 sliding in, 3 sliding out (its tick hides
// the window at the end). -1: unreadable.
inline int shot_lb_wanted(int state) { return state >= 1 && state <= 3; }
// Letterbox end (duke 0x100e7d70) shows game_view only from states 1 and 2: then the HUD is the game's to show.
inline int shot_lb_holds_hud(int state) { return state == 1 || state == 2; }

// The first frame of a launch: the stash (the stock g_hud an earlier build recorded while hidden, -1 when none)
// against the HUD as config.cfg set it. Only a 0 that build may have left is put back. The stash is the stock as the
// HUD handler reads it (atoi): 0..SHOT_STASH_MAX as itself, anything else that shows the HUD as SHOT_STASH_ODD.
enum { SHOT_STASH_MAX = 9999, SHOT_STASH_ODD = 10000 };
inline int shot_stash_of(const char* stock) {
  const long v = stock ? strtol(stock, 0, 10) : 0;   // atoi's value, without atoi's overflow
  return (v >= 0 && v <= SHOT_STASH_MAX) ? (int)v : SHOT_STASH_ODD;
}
inline int shot_recover(int stash, int curHud) {
  if (stash < 0 || curHud != 0 || stash == 0) return -1;
  return stash == SHOT_STASH_ODD ? 1 : stash;
}

// --- The glue (shot.cpp), main thread only -------------------------------------------------------
// Frame path, after freecam_frame: the HUD screen's hide and show; the launch's stash cleanup once.
void shot_frame(void);
// Screenshot mode is on right now: the ESP, markers, damage numbers and freecam overlays skip drawing.
bool shot_hides_overlays(void);
// The quit path: nothing of the HUD screen is archived; logs whether it was still hidden by us.
void shot_restore_for_quit(void);
