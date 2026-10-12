#pragma once
// Header-only and pure (no allocation, no I/O), so the tests pin this rule without linking log.cpp's Win32 calls.
// _CRT_SECURE_NO_WARNINGS (build.ps1) silences _vsnprintf's deprecation warning under /WX; the call is deliberate.
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

inline bool logfmt_sep(char c) { return c == '\\' || c == '/'; }
inline char logfmt_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

// path into out with a leading `profile` folder written as "%USERPROFILE%" (true), else unchanged (false): players
// attach the log to public issues, and the profile folder carries their Windows user name. ASCII case and '/' versus
// '\\' do not matter; "<profile>2" is another folder. Cut to cap, always terminated.
inline bool logfmt_redact_profile(const char* path, const char* profile, char* out, int cap) {
  if (!out || cap <= 0) return false;
  out[0] = 0;
  if (!path) return false;
  int n = profile ? (int)strlen(profile) : 0;
  while (n > 0 && logfmt_sep(profile[n - 1])) n--;
  bool match = n >= 3;
  for (int i = 0; match && i < n; ++i)
    match = path[i] && ((logfmt_sep(path[i]) && logfmt_sep(profile[i])) ||
                        logfmt_lower(path[i]) == logfmt_lower(profile[i]));
  if (match && path[n] && !logfmt_sep(path[n])) match = false;
  if (match) _snprintf_s(out, (size_t)cap, _TRUNCATE, "%%USERPROFILE%%%s", path + n);
  else _snprintf_s(out, (size_t)cap, _TRUNCATE, "%s", path);
  return match;
}

// Formats `fmt`/`ap` into `out` (capacity `cap` >= 2), guaranteeing out[n-1] == '\n' and no NUL before out[n]. Returns n.
// _vsnprintf, not snprintf: on truncation it returns -1 and leaves no NUL, so "how much fit" is read from `limit`.
// The content limit is cap-2 so the '\n' appended here never lands past out[cap-1].
inline int logfmt_line(char* out, int cap, const char* fmt, va_list ap) {
  if (cap < 2) return 0;
  int limit = cap - 2;
  int n = _vsnprintf(out, (size_t)limit, fmt, ap);
  int len = (n < 0 || n > limit) ? limit : n;
  // Strip a caller-supplied trailing \r\n/\n so it isn't doubled below.
  while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) len--;
  out[len] = '\n';
  return len + 1;
}
