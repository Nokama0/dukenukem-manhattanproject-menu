# The save freeze: where the ~600 ms goes

The freeze is the number of unbuffered `WriteFile` calls (one per 1-8 byte `vfs_write`) times a per-call cost
that spikes. The engine's own serialisation is a few ms (section 5). Fast saves (Settings > SAVES, default on)
coalesces those writes (section 6).

Tags: [D] decompiled/disassembled in Ghidra (MCP), [L] nukepanel.log and the engine log.

## 1. Evidence that the hitch is the save [L]
- Hitch lines (dt=611 ms, panel=0.5 ms and dt=628 ms, panel=1.0 ms) each follow an engine log line
  `<game_base_proc> saving e04p01_a01 state...`. 17 of the 21 in-play hitches in that log follow a save. Our
  panel's share is about 1 ms.

## 2. The write path [D]
- `world_store_state_msv` duke!0x100e1a00 (its 0x100e1a46 logs `saving %s state...`) runs
  `vfs_open("state", 0)` and `vfs_mkdir` when that fails, then `vfs_open("state/<map>.msv", 0x101, 0x1b6)`
  (lowercased; O_WRONLY|O_CREAT, no O_TRUNC, no O_BINARY), then `srv_gs_store` and `vfs_close`. With world 0 it
  walks every loaded world (`game+0xF8` list).
- The checkpoint save `FUN_100e33e0` (the "quick save point" path) calls `world_store_state_msv(game, 0, 0)`:
  every loaded world is written at each checkpoint, and nothing in the path runs on another thread.
- `srv_gs_store` prism3d!0x420e80 -> `0x40b5f0` serialises with **many small `vfs_write` calls**: 1, 2, 4 or 8
  bytes per field (header, per entity id/parent/+0x74, class-name length and name, pins, then each entity's
  `vt+0xD8` and `vt+0xE0` state writers).
- `vfs_write` prism3d!0x403ea0 calls the device's slot `+0x60`. For the dir device it is 0x404b80:
  `_write(file+0x10, buf, len)` (CRT 0x428ee5 -> `_write_lk` 0x428f4a), **with no buffer of its own**. `_write_lk`
  calls `WriteFile` once per call. In text mode it calls it once per 1 KB chunk after LF->CRLF.
  vfs_open's mask `0x70b` drops O_BINARY, so the mode is `_fmode`'s (not read).
- File API imports [D]: prism3d.exe imports `CreateFileA`, `WriteFile` (slot 0x434074), `SetFilePointer`,
  `FlushFileBuffers` and `CloseHandle` from KERNEL32 (CreateFileA slot 0x43405c; no `CreateFileW` and no
  `SetFilePointerEx`). `vfs_open` reaches CreateFileA through the CRT `_open` 0x4288c7 -> 0x4288de.
  - p3shared.dll uses msvcrt's `fopen`/`fwrite`, not the VFS.
  - The engine log is written with `WriteFile` on a handle the engine opened at startup (0x425a30, global 0x4a1f6c).
    That handle predates our hooks, so it is never tracked.
- A ~300 KB file written a few bytes at a time takes tens of thousands of `WriteFile` syscalls (section 5). Each
  one can pass through the security software's file filter, which fits the spiking per-call cost.

## 3. The instrument (saveio.cpp, saveio_rule.h)
- At the first frame (the main thread), five import slots of **prism3d.exe only** are pointed at timers:
  `CreateFileA`, `WriteFile`, `SetFilePointer`, `FlushFileBuffers` and `CloseHandle`. The slots are found by
  walking the import descriptors by name. No engine code bytes change.
  - Install log: `saveio: <fn> slot <addr> was <target> (kernel32|not kernel32's export)`, then
    `saveio: N/5 ... tid=`.
  - Other threads pass straight through.
- A handle that `CreateFileA` opened with `GENERIC_WRITE` on the main thread is tracked (8 at once). Calls on it are
  counted with QPC per kind, along with the longest single call and the bytes written.
- At its `CloseHandle`, the instrument logs one line:
  `saveio: <name> total=... ms create=... write=N calls ... ms (max ...) bytes=N close=... ms seek=N calls ... ms flush=N
  calls ... ms engine=... ms`. Appended when nonzero: `untracked=` (table full) and `stale=` (handle reused unseen).
  - `total` runs from before CreateFile to after CloseHandle.
  - **`engine` = total minus every file-API call**, i.e. the engine's own serialisation between the calls.
- The hitch line gains `saveio=... ms`: the tracked file-API time inside the window its dt spans (taken every frame,
  like `log=`).
- Any `CreateFileA` slower than 100 ms logs `saveio: slow open` with name, access, share, disposition and flags
  (see [[stalls]]).
- The rules are pure tests: tb_0506 (accumulator), tb_0507 (table, basename, stale, overflow) and tb_0508 (line
  format).
- There is no detour on `world_store_state_msv`. Each file's CreateFile..CloseHandle window already holds the
  serialisation. The window misses the `state` directory open and the `saving` log line before it.

## 4. How to read the result
- `write` close to `total`: the cost is the syscall count. That is what section 5 measured; section 6 buffers
  the writes.
- `close` large: a scan on close (security software).
- `engine` large: serialisation; the file API is not the cost.

## 5. The measurement [L]
- Hitch dt=420 ms, saveio=403.7 ms: `e03p03_a02_r01.msv` write=6782 calls 363.0 ms, engine=2.7 ms.
- Hitch dt=628 ms, saveio=612.0 ms: `e04p01_a01.msv` write=17001 calls 601.0 ms (max 12.1), engine=5.5 ms.
- A normal save of the same file: 17893 calls, 40.2 ms. The call count is the same; the per-call cost spikes (the
  disk, or the security software in the path). The engine's serialisation is a few ms either way.

## 6. The fix: Fast saves (saveio.cpp, saveio_rule.h)
- Settings > SAVES > Fast saves, default on (`[saves] fast`). It is read at each open, so a change applies from
  the next save. Off: the engine's own unbuffered writes.
- The same five import hooks, main thread only. A handle qualifies when `CreateFileA` opened it with `GENERIC_WRITE`
  and no read right (`GENERIC_READ`, `GENERIC_ALL`, `FILE_READ_DATA`), and its name ends in `.msv` or `.sav` (any
  case). Any other file passes through unchanged. Buffering needs all five hooks installed.
- `WriteFile` on it copies into a 256 KB buffer and returns TRUE with `*written = n`, with no real call. A full
  buffer is written out first. An overlapped write, or one larger than the buffer, writes the buffer out and then
  goes straight through.
- `SetFilePointer`, `FlushFileBuffers` and `CloseHandle` write the buffer out first (real `WriteFile` calls until every
  byte is out), then make the real call. So every byte reaches the file in the engine's order and at its position.
- A failed write-out is logged, counted on the close line (`flushfail=N err=E`), and the handle stops buffering.
  The engine `WriteFile` that triggered it fails with that error, and so does the seek. `FlushFileBuffers` and
  `CloseHandle` return FALSE with it; `CloseHandle` still closes the handle.
- The `saveio:` line keeps counting the engine's calls (`write=`) and adds the kernel32 ones
  (`real=N calls ... ms`). `real` time is nested in the write/seek/flush/close times, so `engine` is unchanged.
- No engine memory is written. Tests: tb_0508 (line format, `real=`, `flushfail=`), tb_0509 (which files qualify,
  the default), tb_0510 (a save-shaped sequence of writes, seeks, flush and close gives the same bytes in the same
  places with 9 real writes for 18), tb_0511 (full buffer, oversize write, short writes, failure, stall).

## Open questions
- Whether a save is hitch-free in game with Fast saves on: a `saveio:` line with `real=` in the tens of calls for
  a file with `write=` in the thousands, and the save loading back.
