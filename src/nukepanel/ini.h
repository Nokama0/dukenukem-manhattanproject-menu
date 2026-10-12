#pragma once
// nukepanel.ini's line rules, shared by config_load_from and the DllMain peek (config_peek_borderless),
// so the switch read before WinMain is the one the loader reads later.
#include <stdlib.h>
#include <string.h>

enum { INI_LINE_MAX = 512, INI_SECTION_MAX = 32 };

inline char* ini_trim(char* s) {
  while (*s == ' ' || *s == '\t') s++;
  char* e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
  return s;
}

// A whole-string integer (decimal or 0x hex) inside [lo, hi], or false.
inline bool ini_parse_int(const char* s, int lo, int hi, int* out) {
  if (!*s) return false;
  char* end = NULL;
  const long v = strtol(s, &end, 0);
  if (end == s || *end != 0 || v < lo || v > hi) return false;
  *out = (int)v;
  return true;
}

// A string value into out (cap bytes with the terminator): one pair of surrounding double quotes is dropped, as
// GetPrivateProfileString drops them. False, out untouched, when it does not fit: never a cut path.
inline bool ini_parse_str(const char* s, char* out, int cap) {
  int n = (int)strlen(s);
  if (n >= 2 && s[0] == '"' && s[n - 1] == '"') { s++; n -= 2; }
  if (n >= cap) return false;
  memcpy(out, s, (size_t)n);
  out[n] = 0;
  return true;
}

// One line, edited in place: a [section] header updates `section` (a header with no ']' changes
// nothing); a key=value line sets *key and *val and returns true. Blank and ;/# lines return false.
inline bool ini_line(char* line, char* section, int scap, const char** key, const char** val) {
  char* p = ini_trim(line);
  if (*p == 0 || *p == ';' || *p == '#') return false;
  if (*p == '[') {
    char* close = strchr(p, ']');
    if (close) {
      *close = 0;
      const char* s = ini_trim(p + 1);
      int i = 0;
      for (; s[i] && i < scap - 1; i++) section[i] = s[i];
      section[i] = 0;
    }
    return false;
  }
  char* eq = strchr(p, '=');
  if (!eq) return false;
  *eq = 0;
  *key = ini_trim(p);
  *val = ini_trim(eq + 1);
  return true;
}

// What config_load_from leaves for [section] key, from the file's first n bytes: each valid value in turn, def when
// none. A line whose text (CR LF dropped, as text-mode fgets drops it) is over INI_LINE_MAX - 1 bytes is skipped whole,
// as the loader skips it: cut, its tail would be read as a line of its own.
inline int ini_peek_int(const char* text, int n, const char* section, const char* key, int lo, int hi, int def) {
  int v = def;
  char sec[INI_SECTION_MAX] = "", line[INI_LINE_MAX];
  int i = 0;
  while (i < n) {
    int len = 0;
    bool over = false;
    while (i < n) {
      const char c = text[i++];
      if (c == '\n') break;
      if (c == '\r' && i < n && text[i] == '\n') continue;
      if (len < INI_LINE_MAX - 1) line[len++] = c;
      else over = true;
    }
    line[len] = 0;
    if (over) continue;
    const char *k = NULL, *val = NULL;
    int got;
    if (ini_line(line, sec, sizeof sec, &k, &val) && _stricmp(sec, section) == 0 && _stricmp(k, key) == 0 &&
        ini_parse_int(val, lo, hi, &got))
      v = got;
  }
  return v;
}
