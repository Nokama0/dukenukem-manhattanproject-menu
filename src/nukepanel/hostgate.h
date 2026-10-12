#pragma once
// True only for the exe this proxy is designed for. The Enhanced launcher DukeNukemMP.exe loads winmm.dll too and
// must get plain forwarding with no logging, or it rolls the log to .prev on every launch and loses the game's log.
// Pure rule: compares only the final path component, case-insensitively.
inline int host_is_game(const char* fullPath) {
  if (!fullPath || !*fullPath) return 0;
  const char* base = fullPath;
  for (const char* p = fullPath; *p; ++p) {
    if (*p == '\\' || *p == '/') base = p + 1;
  }
  const char* want = "prism3d.exe";
  size_t i = 0;
  for (; base[i] && want[i]; ++i) {
    char a = base[i], b = want[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
    if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
    if (a != b) return 0;
  }
  return base[i] == 0 && want[i] == 0;
}
