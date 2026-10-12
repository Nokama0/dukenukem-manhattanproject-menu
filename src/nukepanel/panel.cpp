// The bridge between the WndProc subclass (input.h) and the carried menu (ui/menu.h), where AA's uibridge.cpp and
// overlay.cpp held it. Main thread only: the WndProc, the frame and this run on the engine's one thread (input.h).
#include "panel.h"
#include "input.h"
#include "config.h"
#include "canvas.h"
#include "log.h"
#include "ui/ui.h"
#include "ui/input.h"
#include "ui/menu.h"
#include "ui/wallclock.h"
#include "ui/entry.h"     // ENTRY_VK_V, ENTRY_PASTE_CAP
#include "clipboard.h"
#include "gamebind.h"     // gamebind_begin_session
#include "photofolder.h"  // photo_folder_frame: the folder picker
#include <windows.h>

UiInput g_ui;

// GetTickCount, as AA: the springs are millisecond-scale and every caller takes
// a difference, so the 49-day wrap is harmless.
unsigned wallclock_ms() { return (unsigned)GetTickCount(); }

static int s_was_open;

// ImGui's software arrow, the cursor AA draws (12x19, imgui_draw.cpp:2597-2615):
// 'X' outline, '.' fill.
static const char* const kArrow[] = {
  "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ",
  "X....X      ", "X.....X     ", "X......X    ", "X.......X   ", "X........X  ",
  "X.........X ", "X..........X", "X......XXXXX", "X...X..X    ", "X..X X..X   ",
  "X.X  X..X   ", "XX    X..X  ", "      X..X  ", "       XX   ",
};

// Every run of `want` cells in one colour.
static void arrow_pass(int x, int y, char want, unsigned rgba) {
  for (int row = 0; row < (int)(sizeof kArrow / sizeof kArrow[0]); row++) {
    const char* s = kArrow[row];
    for (int c = 0; s[c];) {
      int n = 1;
      while (s[c + n] && (s[c + n] == want) == (s[c] == want)) n++;
      if (s[c] == want) canvas_rect(x + c, y + row, n, 1, rgba);
      c += n;
    }
  }
}

// ImGui's four passes (imgui.cpp:4019-4022): the 'X' image twice as a shadow at
// +1 and +2 x in black alpha 48, then 'X' black, then '.' white.
static void draw_arrow(int x, int y) {
  arrow_pass(x + 1, y, 'X', 0x30000000u);
  arrow_pass(x + 2, y, 'X', 0x30000000u);
  arrow_pass(x, y, 'X', 0xFF000000u);
  arrow_pass(x, y, '.', 0xFFFFFFFFu);
}

// Queued keys go to an open entry field first (auto-repeat included, so a held Backspace deletes), then
// to an armed bind row (Escape cancels it); every other key the panel swallowed is dropped, as AA's
// WndProc drops them. The clipboard half of Ctrl+C/X/V is here: the field decides the text (entry.h).
static void route_keys(void) {
  int vk, repeat, mods;
  while (input_key_take(&vk, &repeat, &mods)) {
    if (menu_entry_open()) {
      const bool shift = (mods & INPUT_MOD_SHIFT) != 0, ctrl = (mods & INPUT_MOD_CTRL) != 0;
      if (ctrl && vk == ENTRY_VK_V) {
        char clip[ENTRY_PASTE_CAP];
        if (clipboard_read_text(clip, sizeof clip)) menu_entry_paste(clip);
        continue;
      }
      const char* copied = menu_entry_key_vk(vk, shift, ctrl);
      if (copied && !clipboard_write_text(copied)) np_log("menu: entry copy did not reach the clipboard");
      continue;
    }
    if (repeat || !menu_keycap_armed()) continue;
    if (vk == VK_ESCAPE) menu_keycap_cancel();
    else menu_keycap_bind(vk);
  }
  unsigned ch;
  while (input_char_take(&ch)) {}
}

void panel_frame(int W, int H) {
  const int open = input_panel_open() ? 1 : 0;
  if (s_was_open && !open) {
    menu_keycap_close();
    np_log("menu: close");
  } else if (!s_was_open && open) {
    np_log("menu: open");
    gamebind_begin_session();   // the game's binds may have changed since the last open
  }
  s_was_open = open;

  // Closed means no input at all: a held button carried into a closed frame
  // could drag a row nobody can see.
  int mx, my;
  input_cursor(&mx, &my);
  g_ui.mx = mx; g_ui.my = my;
  if (open) {
    // Press counts, not held-state edges: a click shorter than a frame still lands.
    const int lp = input_press_take(0), rp = input_press_take(1);
    g_ui.down = input_mouse_down(0);
    g_ui.rdown = input_mouse_down(1);
    g_ui.pressed = lp > 0;
    g_ui.rpressed = rp > 0;
    menu_set_wheel((double)input_wheel_take() / (double)WHEEL_DELTA);
    route_keys();
  } else {
    g_ui.down = g_ui.pressed = g_ui.rdown = g_ui.rpressed = false;
  }

  if (open || menu_closing_animation_active()) {
    canvas_begin(W, H);
    menu_draw(W, H);
    if (open && input_software_cursor()) {
      canvas_set_transform(0, 0, 1.0, 0, 0);   // the panel's slide is not the cursor's
      draw_arrow(mx, my);
    }
    canvas_end();
  }

  // After the canvas pass, before the save: a Photo folder click opens the picker here, which blocks until closed.
  photo_folder_frame();

  // Saved on every change: a no-op unless a row raised g_cfgDirty.
  config_write_changed();
  input_set_panel_key(panel_key_effective(config_bind_get(BIND_ROW_MENU)));
}
