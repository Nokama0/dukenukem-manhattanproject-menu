#include <windows.h>
#include "clipboard.h"

// See clipboard.h for why this file is DLL-only and why every path below
// fails closed.

bool clipboard_read_text(char* out, int cap) {
  if (!out || cap <= 0) return false;
  // TERMINATED FIRST, before anything that can fail: every early return below
  // then leaves the caller holding a defined empty string rather than
  // whatever was on its stack.
  out[0] = 0;
  // ASKED BEFORE THE CLIPBOARD IS OPENED: IsClipboardFormatAvailable needs no open clipboard, and asking first means
  // the common case (an image or nothing on it) never takes the clipboard from the desktop, even for an instant.
  if (!IsClipboardFormatAvailable(CF_TEXT)) return false;
  if (!OpenClipboard(NULL)) return false;
  int n = 0;
  const HANDLE h = GetClipboardData(CF_TEXT);
  if (h) {
    // THE HANDLE IS NOT OURS: GetClipboardData returns the clipboard's own handle, so lock and unlock it, never
    // free it; freeing would take the player's clipboard contents away as a side effect of pasting.
    const char* p = (const char*)GlobalLock(h);
    if (p) {
      // Bounded by the block too: a CF_TEXT block with no NUL is read to its end and no further.
      const SIZE_T size = GlobalSize(h);
      while (n < cap - 1 && (SIZE_T)n < size && p[n]) { out[n] = p[n]; n++; }
      out[n] = 0;
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return n > 0;
}

bool clipboard_write_text(const char* s) {
  if (!s) return false;
  int n = 0;
  while (s[n]) n++;
  if (!OpenClipboard(NULL)) return false;
  bool ok = false;
  if (EmptyClipboard()) {
    // GMEM_MOVEABLE, which is what SetClipboardData requires of the block it
    // is given: a fixed allocation is documented as invalid there.
    const HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)n + 1);
    if (h) {
      char* p = (char*)GlobalLock(h);
      if (p) {
        for (int i = 0; i < n; i++) p[i] = s[i];
        p[n] = 0;
        GlobalUnlock(h);
        // THE HANDLE BELONGS TO THE CLIPBOARD FROM HERE, on success only: freeing after a successful set frees memory
        // the system now owns, and not freeing after a failed one leaks it. Both halves of the rule live only here.
        if (SetClipboardData(CF_TEXT, h)) ok = true;
        else GlobalFree(h);
      } else {
        GlobalFree(h);
      }
    }
  }
  CloseClipboard();
  return ok;
}
