# The ~1 s stall: which module holds the main thread

Some frames take 1020-1046 ms. The panel's share is 0.6-2.6 ms, log writes are ~0, `saveio=0.0`, and the engine log
has no line near them. A stall watchdog samples the main thread's stack: the stack of every 1 s hitch ends in the
real `CreateFileA`, on a read-only open of a save-slot thumbnail held for exactly 1000 ms. The cause is
`prism3d.exe`'s own CreateFileA call, held by the security software's file filter (Malwarebytes is installed).

Tags: [L] nukepanel.log and the engine log. Related: [[save-io]] (the save freeze, a different hitch), [[timing]].

## 1. Evidence [L]
- 12 hitches of 1020-1046 ms (`hitch:` lines, probe.cpp). Panel share 0.6-2.6 ms, log writes ~0, `saveio=0.0`, and
  no engine log line near any of them.
- The same ~1.03 s hitches occur before Game speed existed (1026, 1018 and 1034 ms in an earlier session), so
  nothing the panel added to the frame causes them.
- The near-constant length points to a 1 s timeout (a wait, a Sleep, or a driver, audio or DirectInput call). The
  samples in section 4 name the call.

## 2. The instrument (stall.cpp, stallrule.h)
- `stall_frame_end` runs last in `probe_frame`, after the frame cap's sleep. It publishes the frame's start time and
  a frame counter with interlocked writes. Its first call records the main thread's stack bounds (`NT_TIB`
  StackBase/StackLimit, read on that thread) and starts the watchdog thread.
- The watchdog polls every 50 ms. When the current frame passes **300 ms** it samples the main thread, and again at
  **800 ms**. That is at most 2 samples per stall and at most **40 stalls** per session. A failed attempt uses up
  its sample.
- A frame past **3 s** (a level load, a minimised window) gets one sample marked `long`, even past the cap, and
  nothing after it. It gives back its count, so loads do not use up the 40.
- The rules are pure tests: tb_0527 (policy), tb_0528 (EBP walk), tb_0529 (address format) and tb_0530 (long
  frames and the cap count).
- A sample: `SuspendThread(main)`, `GetThreadContext`, then the check that the frame has not ended (id and start
  time unchanged). The check comes after the context because SuspendThread is asynchronous and only a returned
  context proves the thread stopped. Then read `[esp]` and walk the EBP chain, at most 24 frames, between ESP and
  StackBase, with plain reads inside `__try/__except`. Then `ResumeThread`.
  - **GetThreadContext is the only API call in the suspended window.** No VirtualQuery or ReadProcessMemory,
    because security software hooks them in this process, and a hook that takes a lock the suspended thread holds
    would freeze the game for good. Nothing there allocates or logs. The main thread holds no lock of ours on the
    frame path.
- The addresses are resolved after the resume with `GetModuleHandleExA(FROM_ADDRESS)` and `GetModuleFileNameA`.
  If the stall is the main thread holding the loader lock, the resolve waits for it and the second sample is lost.
  It cannot deadlock, because the main thread never waits on the watchdog.
- Log lines:
  - `stall: watchdog tid=... watching tid=... (stack lo-hi)` at start.
  - `stall: frame N at T ms[ long]: eip <mod>+0x... top <mod>+0x... ; stack(n) <mod>+0x... ...` per sample. `top` is
    `[esp]`, the return address of a frameless leaf (an ntdll syscall stub has no EBP frame of its own).
  - `... SuspendThread failed err=` or `... GetThreadContext failed err=` when a sample fails.
  - At detach: `stall: S stalls (cap count), L long frames, N samples, ... suspend failures, ... context failures, ...
    ended before the suspend`.
- The hitch line gains `stall=<n>`: the samples taken in the frame whose dt it reports.
- At detach the watchdog is told to stop but not waited for. At process exit it is already gone. Under the loader
  lock its exit would block.

## 3. How to read the result
- Match each `hitch: ... stall=1|2` to the `stall: frame ...` lines just before it. The first non-system module up
  the stack (prism3d.exe, duke_base.dll, p3shared.dll, a driver or wrapper DLL) is the caller. Its offset goes to
  Ghidra (MCP) to name the function.
- A level load or a minimised window also passes 300 ms; past 3 s its sample is marked `long`. Read those samples
  against what the player was doing.

## 4. Samples (all hitches 1020-1034 ms)

Every 1 s hitch sampled twice (300 and ~830 ms) with the same stack: `eip ntdll.dll+0x7a0fc top KERNELBASE.dll+0x14e8bf ;
KERNELBASE.dll+0x14e2d0 KERNELBASE.dll+0x16daf1 WINMM.dll+0x58a52`. `WINMM.dll` here is **our proxy** (the system
winmm.dll image is 0x32000 bytes; ours is 0x1E4000): dumpbin of build/winmm.dll shows 0x10058a4c `call [0x1018C568]`
with seven pushed arguments, then `GetLastError`: saveio.cpp `hk_CreateFileA`'s call to the real `CreateFileA`.
So the main thread is inside Windows' CreateFileA for ~1 s, on a file prism3d.exe opens. The hitches predate the
hook, so the hook is only where the call is seen. `saveio: slow open` lines (any CreateFileA >= 100 ms, with name,
access, share, disposition, flags) name the file.

Named file, seen while quitting through the menu:
`saveio: slow open 1000 ms: duke/base/state/game01.bmp access=0x80000000 share=0x3 disp=3 flags=0x00000080 ok`:
a read-only open of a 24 KB save-slot thumbnail, last written 21 min earlier, held for exactly 1000 ms; the
same stack as every 1 s hitch (`WINMM.dll+0x58a4f`, that build's offset of the same call). Malwarebytes
(MBAMService.exe, Malwarebytes.exe) is running.

## Open questions
- Whether the in-play hitches open thumbnails too, or other files. One more named in-play hitch would settle it.
