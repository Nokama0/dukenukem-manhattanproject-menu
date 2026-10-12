// Skip intro glue (skipintro.h, docs/re/quick-wins.md). Main thread only, frame path.
#include "skipintro.h"
#include "floattext.h"   // vfs_device_name_copy
#include "engine.h"
#include "config.h"
#include "input.h"       // input_ui_window
#include "log.h"
#include <windows.h>

// init.cfg's own mount shape, one priority above floattext's p60 and db_zoom_wide's p52. The directory holds
// ui/scripts/scr_intro.sc, installed by deploy.ps1 under duke/base/nukepanel/.
static const char kMountLine[] = "mount -o ro,p61 \"$GAME\"/base/nukepanel/skipintro/ /";
static const char kScript[] = "ui/scripts/scr_intro.sc";

static const char* kind_name(int k) {
  return k == SI_SKIP ? "ours (no logo groups)" : k == SI_STOCK ? "STOCK (logo groups)" : "unrecognised";
}

void skipintro_mount(void) {
  static int done;
  if (done++) return;
  if (!g_cfg.skipIntro) { np_log("skipintro: switch off; stock intro"); return; }
  if (!g_eng.cmd_execute) { np_log("skipintro: cmd_execute unresolved; not mounted"); return; }
  const bool found = g_eng.cmd_execute(kMountLine);
  np_log("skipintro: %s -> %s", kMountLine, found ? "run" : "command not found");
  if (!g_eng.vfs_open || !g_eng.vfs_read || !g_eng.vfs_close || !g_eng.vfs_get_device_name) {
    np_log("skipintro: vfs exports unresolved; cannot tell which %s is served", kScript);
    return;
  }
  void* f = g_eng.vfs_open(kScript, 0, 0x1b6);
  if (!f) { np_log("skipintro: %s does not open", kScript); return; }
  char dev[160];
  vfs_device_name_copy(g_eng.vfs_get_device_name(f), dev, sizeof dev);
  // The widened stock script's first group sits ~500 bytes in, past its $$ header.
  static char head[2048];
  const int n = g_eng.vfs_read(f, head, sizeof head - 1);
  head[n > 0 ? n : 0] = 0;
  g_eng.vfs_close(f);
  np_log("skipintro: %s served from %s: %s (%d bytes read)", kScript, dev, kind_name(si_script_kind(head)), n);
}

// The intro window's life, with and without the switch: how many frames it was shown, and when the main menu came.
// "ui s intro:screen" names it intro, class screen (the `ui` command's name:class order).
void skipintro_frame(void) {
  static int state;              // 0 watching for the intro, 1 intro shown, 2 finished
  static unsigned frames, shown;
  static ULONGLONG t0, tShown;
  if (state == 2) return;
  const ULONGLONG now = GetTickCount64();
  if (!t0) t0 = now;
  frames++;
  const bool intro = input_ui_window("screen", "intro") != NULL;
  const bool menu = input_ui_window("screen", "menu_main") != NULL;
  if (state == 0 && intro) {
    state = 1; tShown = now;
    np_log("skipintro: intro screen shown at +%llu ms (frame %u)", now - t0, frames);
  }
  if (state == 1 && intro) shown++;
  if (state == 1 && !intro) {
    np_log("skipintro: intro screen gone at +%llu ms after %u frame(s) (%llu ms)%s", now - t0, shown, now - tShown,
           shown <= 2 ? ": straight to the menu" : "");
    state = 0;   // the menu may still be a frame away; a second show would be logged again
    if (menu) state = 2;
  }
  if (menu && state != 1) {
    np_log("skipintro: main menu shown at +%llu ms (frame %u)", now - t0, frames);
    state = 2;
  } else if (now - t0 > 120000) {
    np_log("skipintro: no main menu within 120 s; watch stopped (intro %s)", state == 1 ? "still shown" : "not shown");
    state = 2;
  }
}
