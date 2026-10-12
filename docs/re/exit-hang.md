# Exit hang: a held cache reference blocked every quit

The game process stayed alive after the window closed because the panel's canvas held one cache reference each on
`nukepanel/font.shd` and `nukepanel/font.png` (taken by `mm_get_resource`, never released). That kept the loaders'
`__dl_guard` process at lock 2, so the quitting engine's process list never emptied (quit-path.md). The canvas now
drops its shader reference in the quit teardown (quit-path.md section 7). In-level Quit, main-menu Quit and Alt+F4
all end the process. The stock game (no `winmm.dll`) exits cleanly, so the hang was caused by the panel.

Method: Ghidra for function bounds, cdb attach and `.dump /ma` (non-invasive; a dump was 216 MB) for stacks and the
cache pool, Python for parsing the exported pool.

## 1. Symptom

- After a long session (about 1 h at 175 fps with an early probe build) the game window closed but `prism3d.exe`
  kept running: main thread at 100% CPU, 45 min of CPU time, 5 threads, no window, and it held `winmm.dll` (the
  next deploy timed out).
- A stray process had no window, 570 s of CPU, `opengl32.dll` in the unloaded-module list and the main thread in the
  main loop pass running the `loaders` cache clean-up (two samples).
- A lingering process is one with **MainWindowHandle 0**. A process that still has a window is just a running game.
- Quit from the main menu or the in-game Quit button left the process running. Alt+F4, the taskbar's "Close
  window" and "End task" all ended it.
- A missing `detach` log line is not evidence by itself: whether it is logged depends on how the engine exits.

## 2. Stack

The stack of the stray process is the engine's NORMAL per-frame loop, not a shutdown path: `process_delete_all+0xb4`
(0x406074) lies inside the main loop pass `FUN_00405ff0` and `wm_close_inst+0x1004` (0x425114) inside
`FUN_00424c90` (Ghidra function bounds). cdb named them after the nearest export. A running game sampled with a
window shows the same stack. So the main loop kept running after the window and GLDirect were gone and never left
the loop.

- Main thread (the thread every panel hook runs on):
  `wm_close_inst+0x1004 -> process_delete_all+0x145/+0xb4 -> loaders.dll+0x34d1 -> mm_remove_unreferenced_ops+0x16
  -> prism3d 0x414700 (cache pool walk) -> 0x414130 (item destroy, itself looping near 0x414633)`.
- `opengl32.dll` (GLDirect) was already in the unloaded-module list.
- `loaders.dll` shutdown (0x3490..) is straight code: for each loader ops table (0x00d79260, 0x00d7932c,
  0x00d793cc, 0x00d7942c, 0x00d792a0, 0x00d79194, 0x00d7908c ...) it calls three cache functions, the third being
  `mm_remove_unreferenced_ops`. There is no outer loop.
- Two snapshots minutes apart showed the walk on loader 0x00d79260 and then on 0x00d793cc: it was advancing.
- The pool walked (edi = 0x024c19d8) has `[+0x18] = 0x11` blocks of 256 items (about 4.3 k items): the cache is not
  huge. The early probe build had taken one `mm_get_resource("nukepanel/probe.shd")` reference and never released
  it, and drew engine text through `pml_render`/`pml_flush` every frame.
- Seeing `loaders` / `mm_*_unreferenced_ops` on the main thread is NOT evidence of a hang: it is the loaders
  process's per-frame cache clean-up.

## 3. Mechanism

- At the hang, the process list held 2 entries: `__dl_guard` at lock 2 and `mm` at lock 1, both dying.
- The cache (0x023c19d8, 23 blocks, exported with cdb `.writemem` and parsed in Python) held exactly two genuinely
  referenced items: `nukepanel/font.shd` (refs 1, ops 0xd093cc) and `nukepanel/font.png` (refs 1, ops 0xd092a0).
- Every quit (main menu, in-level, Alt+F4) runs the same `quit` command, and the held font references block all of
  them (quit-path.md).
- Stock A/B: with our `winmm.dll` renamed away (`winmm.dll.nukepanel-off`), in-level Quit and main-menu Quit both close the process.
- Short runs (about 3 min at 60 fps; about 11 min at 175 fps with a held `font.shd`) exited normally: the hang
  needs a quit that goes through the process list, not session length.

## 4. Instruments

- The log has a line at the start of shutdown and at DLL detach, with timestamps, so the log shows how long an
  exit took.
- To check a test run: a lingering `prism3d.exe` with MainWindowHandle 0 is a hang. Take two stack samples 10 s
  apart (`cdb -pv -p <pid> -c "~0kb; q"`) and one dump.
