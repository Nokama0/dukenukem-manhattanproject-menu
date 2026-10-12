// The stock 4:3 scr_actor_text.sc above db_wide (p51) and db_zoom_wide (p52): mounted by us, so
// removing winmm.dll restores the widened script (docs/re/display.md section C). No hook, no engine write.
#include "floattext.h"
#include "engine.h"
#include "log.h"
#include <stdint.h>

// The line has init.cfg's own shape ("$GAME"/base/... mounted on /); the directory holds
// ui/scripts/scr_actor_text.sc, installed by deploy.ps1 under duke/base/nukepanel/.
static const char kMountLine[] = "mount -o ro,p60 \"$GAME\"/base/nukepanel/aspect43/ /";
static const char kScript[] = "ui/scripts/scr_actor_text.sc";

// The engine's device name, copied only as far as it is readable: each page it enters is checked
// before its first byte is read, and the copy stops at the NUL or the buffer's end.
void vfs_device_name_copy(const char* src, char* out, size_t cap) {
  size_t i = 0;
  for (; src && i + 1 < cap; i++) {
    if ((i == 0 || ((uintptr_t)(src + i) & 0xFFF) == 0) && !guarded_readable(src + i, 1)) break;
    if (!src[i]) break;
    out[i] = src[i];
  }
  out[i] = 0;
  if (!i) strcpy_s(out, cap, "?");
}

static const char* kind_name(int k) {
  return k == FT_STOCK43 ? "stock 4:3" : k == FT_WIDE ? "WIDENED" : "unrecognised";
}

void floattext_mount(void) {
  static int done;
  if (done++) return;
  if (!g_eng.cmd_execute) { np_log("floattext: cmd_execute unresolved; not mounted"); return; }
  const bool found = g_eng.cmd_execute(kMountLine);
  np_log("floattext: %s -> %s", kMountLine, found ? "run" : "command not found");

  // Which script the engine serves now, read the way it will be loaded. vfs_open returns 0 on a miss.
  if (!g_eng.vfs_open || !g_eng.vfs_read || !g_eng.vfs_close || !g_eng.vfs_get_device_name) {
    np_log("floattext: vfs exports unresolved; cannot tell which %s is served", kScript);
    return;
  }
  void* f = g_eng.vfs_open(kScript, 0, 0x1b6);
  if (!f) { np_log("floattext: %s does not open", kScript); return; }
  char dev[160];
  vfs_device_name_copy(g_eng.vfs_get_device_name(f), dev, sizeof dev);
  char head[256];
  const int n = g_eng.vfs_read(f, head, sizeof head - 1);
  head[n > 0 ? n : 0] = 0;
  g_eng.vfs_close(f);
  np_log("floattext: %s served from %s: %s (%d bytes read)", kScript, dev, kind_name(ft_script_kind(head)), n);
}
