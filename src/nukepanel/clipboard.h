#pragma once
// The Windows clipboard, and nothing else: AA's clipboard.h/.cpp (from World Tour, from Megaton). DLL-only, never in
// the suite (a live global). Fail-closed: a busy clipboard, absent format or refused lock answers false and changes
// nothing; every opened path closes it. Main thread only. What a copy writes or a paste may be: campos.h, ui/entry.h.

// CF_TEXT into `out`, always NUL-terminated (empty on every failure); `cap` counts the terminator.
// Answers whether anything was read.
bool clipboard_read_text(char* out, int cap);

// `s` onto the clipboard as CF_TEXT. Answers whether it landed.
bool clipboard_write_text(const char* s);
