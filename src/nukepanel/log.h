#pragma once
// Crude on purpose: raw kernel32, no CRT startup dependency, no allocation, because it runs during loader
// initialisation and from arbitrary threads. The file lands beside the host exe as nukepanel.log.
// Ported from the Alien Armageddon port's ekmod/log.
void np_log(const char* fmt, ...);

// path for a log line, into out: a path under the profile folder starts "%USERPROFILE%" instead (logfmt.h), so a log
// posted publicly does not name the Windows user. Returns out. Any thread.
const char* np_log_path(const char* path, char* out, int cap);

// Off until DllMain confirms the host is prism3d.exe: any other host (the
// launcher) must never touch or roll nukepanel.log.
void np_log_enable(void);

// Writes pre-terminated lines in one open/write/close, for bursts logged
// inside measured frames (the jump window) and written after them.
void np_log_block(const char* text, int len);

// Lines this process failed to write in full since load (failed open, failed WriteFile, short write). Interlocked
// because np_log runs on every thread; read once at DLL_PROCESS_DETACH to report what the log cannot show is missing.
long np_log_dropped(void);

// Write cost since the last call, then cleared: calls, total and longest single call in microseconds.
// Interlocked (np_log runs on any thread); read by the frame hitch line.
void np_log_take_write_stats(unsigned* calls, unsigned* total_us, unsigned* max_us);
