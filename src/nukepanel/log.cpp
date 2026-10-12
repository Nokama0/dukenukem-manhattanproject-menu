#include <windows.h>
#include <stdio.h>
#include "log.h"
#include "logfmt.h"

// ONE SESSION PER FILE (a stale multi-session log misled a crash investigation on the AA port). Kept as
// nukepanel.log.prev so a crash survives one more launch. Interlocked: two first calls racing must roll once.
static void log_roll_once(const char* path) {
  static volatile LONG s_rolled = 0;
  if (InterlockedCompareExchange(&s_rolled, 1, 0) != 0) return;
  char prev[MAX_PATH];
  if (strlen(path) + 6 >= MAX_PATH) return;
  strcpy_s(prev, MAX_PATH, path);
  strcat_s(prev, MAX_PATH, ".prev");
  MoveFileExA(path, prev, MOVEFILE_REPLACE_EXISTING);
}

static volatile LONG s_dropped = 0;
static volatile LONG s_dropped_reported = 0;

long np_log_dropped(void) { return s_dropped; }

// The one place that opens the file; takes an already-terminated line so the drop-note write can call it directly
// (no path back into np_log, so no recursion). FILE_SHARE_WRITE avoids a sharing violation against a second
// appender; FILE_APPEND_DATA makes each call atomic.
static volatile LONG s_wCalls = 0, s_wTotalUs = 0, s_wMaxUs = 0;

static bool np_log_raw_write_impl(const char* path, const char* line, int len);
static bool np_log_raw_write(const char* path, const char* line, int len) {
  LARGE_INTEGER f, a, b;
  QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
  const bool ok = np_log_raw_write_impl(path, line, len);
  QueryPerformanceCounter(&b);
  const LONG us = (LONG)((b.QuadPart - a.QuadPart) * 1000000 / f.QuadPart);
  InterlockedIncrement(&s_wCalls);
  InterlockedExchangeAdd(&s_wTotalUs, us);
  for (;;) {
    const LONG m = s_wMaxUs;
    if (us <= m || InterlockedCompareExchange(&s_wMaxUs, us, m) == m) break;
  }
  return ok;
}

void np_log_take_write_stats(unsigned* calls, unsigned* total_us, unsigned* max_us) {
  *calls = (unsigned)InterlockedExchange(&s_wCalls, 0);
  *total_us = (unsigned)InterlockedExchange(&s_wTotalUs, 0);
  *max_us = (unsigned)InterlockedExchange(&s_wMaxUs, 0);
}

static bool np_log_raw_write_impl(const char* path, const char* line, int len) {
  const HANDLE h = CreateFileA(path, FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    InterlockedIncrement(&s_dropped);
    return false;
  }
  DWORD written = 0;
  const BOOL ok = WriteFile(h, line, (DWORD)len, &written, NULL);
  CloseHandle(h);
  if (!ok || written != (DWORD)len) {
    InterlockedIncrement(&s_dropped);
    return false;
  }
  return true;
}

static volatile LONG s_enabled = 0;
void np_log_enable(void) { InterlockedExchange(&s_enabled, 1); }

static bool log_path(char* path) {
  if (!s_enabled) return false;
  const DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return false;
  char* slash = strrchr(path, '\\');
  if (!slash) return false;
  slash[1] = 0;
  if (strlen(path) + 14 >= MAX_PATH) return false;
  strcat_s(path, MAX_PATH, "nukepanel.log");
  log_roll_once(path);
  return true;
}

const char* np_log_path(const char* path, char* out, int cap) {
  wchar_t w[MAX_PATH];
  char prof[MAX_PATH * 3];
  const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", w, MAX_PATH);
  // A path may be UTF-8 (the Photo folder) or the ANSI code page (module and file names): the profile in each form.
  const UINT kCp[2] = { CP_UTF8, CP_ACP };
  for (int i = 0; n && n < MAX_PATH && i < 2; ++i)
    if (WideCharToMultiByte(kCp[i], 0, w, -1, prof, sizeof prof, NULL, NULL) > 0 &&
        logfmt_redact_profile(path, prof, out, cap))
      return out;
  logfmt_redact_profile(path, 0, out, cap);
  return out;
}

void np_log_block(const char* text, int len) {
  char path[MAX_PATH];
  if (len > 0 && log_path(path)) np_log_raw_write(path, text, len);
}

void np_log(const char* fmt, ...) {
  char path[MAX_PATH];
  if (!log_path(path)) return;

  char line[1024];
  va_list ap;
  va_start(ap, fmt);
  int len = logfmt_line(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (len <= 0) return;

  // Open and close per line: no handle survives across a crash mid-frame.
  const bool wrote_ok = np_log_raw_write(path, line, len);

  // Self-limiting drop note, at most once per rise (InterlockedExchange
  // reads-and-stores atomically, so only a thread observing cur > last
  // fires), and only after this write succeeded.
  if (wrote_ok) {
    const long cur = s_dropped;
    const long last = InterlockedExchange(&s_dropped_reported, cur);
    if (cur > last) {
      char note[128];
      const int nlen = _snprintf_s(note, sizeof(note), _TRUNCATE,
                                    "log: %ld line(s) lost to a failed or short write so far this session", cur);
      if (nlen > 0) {
        note[nlen] = '\n';
        np_log_raw_write(path, note, nlen + 1);
      }
    }
  }
}
