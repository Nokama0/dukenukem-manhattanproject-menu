// cvar_t chain walk, shared by zoom.cpp and framecap.cpp (gl_sync) so there is one copy of the pattern.
#include "cvar_read.h"
#include <string.h>

// The value buffer is char[0x40] in cvar_t (docs/re/input-console.md); a margin over that covers both the +0xB4
// (live) and +0xB8 (default) targets.
static const size_t kCvarBuf = 0x40;

bool cvar_read_string(cvar_t* cv, char* out, size_t cap) {
  const unsigned char* cur = (const unsigned char*)cv;
  int hops = 0;
  for (; hops < 8 && cur; hops++) {
    if (!guarded_readable(cur, 0xD0)) return false;
    unsigned char* link;
    memcpy(&link, cur + CVAR_OFS_LINK, sizeof link);
    if (!link) break;
    cur = link;
  }
  if (hops >= 8) return false;   // did not terminate within 8 hops: corrupt, refuse
  if (!guarded_readable(cur, 0xD0)) return false;
  unsigned char ptrMode;
  memcpy(&ptrMode, cur + CVAR_OFS_PTR_MODE, 1);
  const char* strPtr;
  memcpy(&strPtr, cur + (ptrMode ? CVAR_OFS_DEFAULT : CVAR_OFS_VALUE), sizeof strPtr);
  if (!strPtr || !guarded_readable(strPtr, kCvarBuf)) return false;
  strncpy_s(out, cap, strPtr, _TRUNCATE);
  return true;
}
