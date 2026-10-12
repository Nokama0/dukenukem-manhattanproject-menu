#pragma once
// The save-freeze instrument (docs/re/save-io.md): prism3d.exe's kernel32 import slots for CreateFileA, WriteFile,
// SetFilePointer, FlushFileBuffers and CloseHandle point at timers, main thread only; one log line per file opened
// for write and closed. With Fast saves on (g_cfg.fastSaves, read at each open) the hooks coalesce small writes.

// On the main thread (the first frame): that thread is the one timed. Returns the number of slots patched.
int saveio_install(void);

// File-API time on tracked handles since the last call, then cleared: the hitch line's saveio= share.
double saveio_take_frame_ms(void);
