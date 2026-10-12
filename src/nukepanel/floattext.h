#pragma once
// Floating pickup and damage text at any aspect (docs/re/display.md section C): the base game's
// 4:3 ui/scripts/scr_actor_text.sc, mounted by our DLL above the Enhanced widescreen data.
#include <string.h>

// Which script the head of a file is: WideGen's widened one ($_aspect), the stock 4:3 one
// (its window and child both "0 0 640 480"), or something else.
enum { FT_OTHER = 0, FT_STOCK43 = 1, FT_WIDE = 2 };
inline int ft_script_kind(const char* head) {
  if (!head) return FT_OTHER;
  if (strstr(head, "_aspect")) return FT_WIDE;
  if (strstr(head, "csystem:\"0 0 640 480\"")) return FT_STOCK43;
  return FT_OTHER;
}

// Once, on the first frame: the mount through the engine's own `mount` command, then a log line
// naming the device the engine now serves the script from.
void floattext_mount(void);
// The engine's device name for a vfs handle, copied only as far as it is readable ("?" when nothing is).
void vfs_device_name_copy(const char* src, char* out, size_t cap);
