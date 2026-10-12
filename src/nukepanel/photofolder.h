#pragma once
// Free camera > Photo folder: where photos are saved. [freecam] photofolder= in UTF-8; empty is the default,
// <Pictures>\Nuke Panel. The pure rules are here; the picker and the folder are photofolder.cpp.
#include <stdio.h>
#include <string.h>

// The ini value names the default folder when it is empty.
inline bool photo_folder_is_default(const char* cfg) { return !cfg || !cfg[0]; }

inline bool photo_folder_sep(char c) { return c == '\\' || c == '/'; }

// A configured folder is used only as a drive-letter absolute path (letter, colon, separator): a UNC path would open an SMB connection
// (sending the user's NTLM hash) at the first photo, \\host@80\ a WebDAV request, and \\?\ or \\.\ reach devices.
// A second ':' names a stream.
inline bool photo_folder_path_ok(const char* p) {
  if (!p || !((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) || p[1] != ':' || !photo_folder_sep(p[2]))
    return false;
  return !strchr(p + 2, ':');
}

// The folder as the panel draws it: the font has ASCII only, so each non-ASCII UTF-8 character becomes one '?';
// trailing separators are dropped ("Shots\" draws as "Shots").
inline void photo_folder_display(const char* utf8, char* out, int cap) {
  if (!out || cap <= 0) return;
  int n = 0;
  for (const unsigned char* p = (const unsigned char*)utf8; p && *p && n < cap - 1; ++p) {
    if (*p < 0x80) out[n++] = (char)*p;
    else if ((*p & 0xC0) != 0x80) out[n++] = '?';   // a lead byte; its continuation bytes draw nothing
  }
  while (n > 0 && photo_folder_sep(out[n - 1])) n--;
  out[n] = 0;
}

// Text widths in the panel's font; the suite measures characters.
typedef int (*PhotoMeasure)(const char* s, void* ctx);

// s shortened from the left to fit maxW: whole leading components go first ("...\Game Screenshots\Nuke Panel"); a
// last component too wide on its own keeps as many of its last characters as fit. "" when not even "..." fits.
inline void photo_folder_shorten(const char* s, int maxW, PhotoMeasure m, void* ctx, char* out, int cap) {
  if (!out || cap <= 0) return;
  out[0] = 0;
  if (!s) return;
  if (m(s, ctx) <= maxW && (int)strlen(s) < cap) { strcpy_s(out, (size_t)cap, s); return; }
  const int len = (int)strlen(s);
  int last = 0;   // where the last component's own characters may start being cut
  for (int i = 1; i < len; ++i) {
    if (!photo_folder_sep(s[i])) continue;
    last = i;
    if (_snprintf_s(out, (size_t)cap, _TRUNCATE, "...%s", s + i) >= 0 && m(out, ctx) <= maxW) return;
  }
  for (int i = last; i < len; ++i)
    if (_snprintf_s(out, (size_t)cap, _TRUNCATE, "...%s", s + i) >= 0 && m(out, ctx) <= maxW) return;
  out[0] = 0;
}

// --- The glue (photofolder.cpp), main thread only ---------------------------------------------------------------
// The folder photos go to now, created (with any missing parents) when absent: the configured one, or the default
// when none is set or the configured one cannot be created (logged). False (logged) when neither is available.
bool photo_folder_dir(wchar_t* out, int cap);
// The row's text source: the folder now, UTF-8, and whether it is the default. Not created here.
void photo_folder_shown(char* out, int cap, bool* isDefault);
// The Photo folder row was clicked: the picker opens at the end of this frame's panel pass.
void photo_folder_request(void);
// panel_frame, after the panel drew: runs a requested picker (it blocks this thread until the player closes it).
void photo_folder_frame(void);
