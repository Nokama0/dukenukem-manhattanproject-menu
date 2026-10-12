# Quit path and the main loop's exit condition

The menu Quit and WM_CLOSE run the same console command (`quit`), so the engine gives no reason for them to differ.
The main loop ends only when the process list is empty. A process list stuck at two processes whose lock counts never
reach 0 is what a mod causes by holding cache items: in a hung-exit dump the two locks came from the two
`nukepanel/probe.*` cache items the mod had taken with `mm_get_resource` and never released. The menu's canvas
`font.shd` reference is dropped at the engine's quit (section 7), and all three quit paths (menu Quit, WM_CLOSE,
Alt+F4) exit in game. Method: code reading in Ghidra plus a cdb read of a hung-exit dump.

## 1. What the menu's Quit runs

- Main menu: `ui/scripts/scr_mmenu.sc` (db_e01 / db_wide / db_lang_pl), `ui_menuitem id:"5"` with
  `commands:"" #"!quit"`. The direct `!quit` is commented out, so the button goes through the menu handler, which
  opens the confirmation screen `ui s quit:screen:ui_hdl_quit ui/scripts/scr_quit.sc`
  (duke_base!0x100a4782, found from the string and its xref).
- `scr_quit.sc` (db_e01, db_p101, db_wide): id 2 = No, id 3 = Yes; there are no `commands`.
- Handler `ui_hdl_quit` is registered at duke_base!0x100d8f3f (factory 0x100ddc70, vtable 0x1013bc50).
  Its event method `ui_hdl_quit_event` duke_base!0x100aa010 (vtable +0x100): on event 5,
  id 3 -> `cmd_execute("quit")` (0x100aa03a); id 2 -> `cmd_execute("escape")` (read in the disassembly).
- The engine's log (prism3d's log) from a menu quit shows the same sequence:
  `ui s quit:screen:ui_hdl_quit ...`, then `quit`, then `ui h console:system_01`.
- `quit` is a duke_base command (registered at 0x100e05e1, dumps/console-registry.txt). `cmd_quit`
  duke_base!0x100e5a50 tail-jumps to `game_proc_quit` 0x100e17e0 on the game process (`game_globals+8`), which
  sets requested state +0xcc=8, then next state +0xd0=0xc, and runs `ui h console:system_01`
  (read in the disassembly).
- State machine (`game_proc_handler` 0x100e0c80 = vtable+4, read in the decompile): the switch to 8 calls
  `game_proc_confirm_leave` 0x100e18b0. If no game is in progress it goes straight to 0xc. Otherwise it saves
  (0x100e1930) or opens the save-name dialog `ui_hdl_newgame`, then goes to 0xc.
- `game_proc_system` 0x100e1750 (vtable+8, runs every frame even when the process is asleep): in state 0xc it stops
  the music and closes the servers (`game_proc_close_servers` 0x100e3b00, which also calls `wm_close(0)`). Once the
  master world list is empty (`spawn_get_master_world_list()->count == 0`) it calls `game_quit_teardown`
  0x100e1790 -> **`process_delete_all()`** (prism3d!0x405fc0), then signals itself.

## 2. What WM_CLOSE runs

- `prism_wndproc` prism3d!0x426340 : case 0x10, if `hwnd == DAT_004a21ac` (the main
  window), `cmd_execute("quit")` at 0x426536 (push 0x471894 = "quit"; 0x41a610 = `cmd_execute`), then it
  returns 0. It never reaches DefWindowProc, so WM_CLOSE never calls DestroyWindow.
- `cmd_execute` runs its command immediately (it looks the command up and calls it; decompile). **So both quit paths
  run the same handler, `cmd_quit` 0x100e5a50.** The only difference is when it runs: in the menu path it runs inside
  the `__ui_proc` walk (prio -3); for WM_CLOSE it runs in the message pump after the walk. Either way
  `process_delete_all` runs later, from `game_proc_system`.

## 3. The main loop's exit condition

- `main_loop` prism3d!0x406100: `do {} while (main_loop_iteration());`. `main_loop_iteration` 0x405ff0 returns 0 in only two cases:
  1. `WM_QUIT` peeked (0x4060c0 loop). **This never happens in practice**: no module imports PostQuitMessage (checked
     in the dumps/*.imports.txt files).
  2. It returns `DAT_00476474` (0x4060e5), which is the count field (+0xc) of the process list `vdatalist_t` at
     0x476468 (`vdatalist_t::append` / `remove_pos` inc/dec [this+0xc], p3shared!0x1002872b / 0x100288df).
     **The loop ends only when every process has been removed.**
- How a process gets removed: at the top of each iteration, processes with `+0x18 == 2` are passed to
  `process_remove` (0x405ff0 first loop). `process_delete_all` calls `signal(1)` on every process (vtable +0xc,
  0x405fd7). The default `vprocess_t::signal` p3shared!0x10027d00 stores the argument at +0x18. The default
  `vprocess_t::system` p3shared!0x1002d5d0 calls `signal(2)` only when `+0x18 == 1 && +0x8 (lock count) == 0`. A
  dying process whose lock count stays above 0 is never removed, and the loop never ends.
- Lock holders (read in the code): `sys_library` 0x4027ab locks the host "mm" process (`DAT_00475bf0` =
  `DAT_004a1e54`) once per loaded DLL, and `sys_unload_library` 0x40287e releases that lock. loaders.dll locks its own
  process (`__dl_guard`, thunk loaders+0x33f0) at the end of every resource load (11 call sites). The unlock
  (loaders+0x3410) sits in every loader ops table at +0x14 (bmp/jpg/pcx/png/scr/tga/ld_/pla ... at
  0x00d79024..0x00d7a520) and runs when an item is destroyed. While `__dl_guard` is dying, its `system`
  (loaders+0x3460) calls `mm_hw_free_unreferenced_ops` / `mm_mem_free_unreferenced_ops` /
  `mm_remove_unreferenced_ops` for each loader every frame, and only then `vprocess_t::system`. That per-frame
  clean-up is the stack the hang samples show. **It only frees UNREFERENCED items.**
- `mm_get_resource` 0x41c620 -> `cache_get_resource` 0x413810: on a hit it increments the item's refcount +0x44; a
  new item starts at refcount 1 (`MOV [EAX+0x44],1` at 0x41401d). The caller owns one reference in either case.
  It is released by `ci_dec_refs` 0x41ca60 (frees at 0 via 0x413080) or by `mm_release_item` 0x41c720.

### Measured in a hung-exit dump (cdb)

- Process list 0x476468: count **2**. `mm` (proc 0x024c18f8, the host sys process) has state18=1 and **lock 1**;
  `__dl_guard` (proc 0x024c1ae8, loaders.dll, vtable 0x00d78110) has state18=1 and **lock 2**. Both have been told
  to die and neither can.
- Library table 0x475bf0 (count 9): every library proc is 0 (unloaded) **except sys/loaders**. Its one
  `sys_library` lock is the `mm` process's lock 1.
- Items in the cache 0x024c19d8 (block 0) with refcount > 0: **exactly two**, `nukepanel/probe.shd` (refs 1, ops
  0x00d793cc) and `nukepanel/probe.png` (refs 1, ops 0x00d792a0, loaded as the shader's texture). **Two held items
  match loaders' lock 2.** These are our probe's `mm_get_resource` reference, which was never released
  ([exit-hang.md](exit-hang.md)).

## 4. Why the stock engine exits on both paths

- In the engine the two paths do not differ: the same `cmd_quit`, the same state 8 -> 0xc, the same
  `process_delete_all`, and the same exit condition. The stock game releases every cache reference during the
  teardown, so `__dl_guard` unlocks, loaders unloads, `mm` unlocks, the list empties and `host_main` 0x424c90 carries
  on into `sys_shutdown` and the rest of the teardown. A menu Quit in the engine log reaches
  `unloading library sys/loaders ... <sys> shutting down`, a clean exit.
- The window disappears on both paths (it closes during teardown via `core_destroy_context_gwindow` 0x426e70 while the
  loop keeps running), so a clean-looking exit can still leave a process running. Check with `Get-Process prism3d`
  (MainWindowHandle 0) after an exit.

## 5. What in the mod could block the exit condition

- **The cache reference: yes** (section 3, measured). The canvas holds `nukepanel/font.shd` (or `white.shd`) for the
  whole session and drops it at the engine's quit (section 7).
- Subclass: no. The exit condition is not message-based. WM_CLOSE reaches the engine unchanged
  (CallWindowProcA; our only WM_CLOSE handling is a log line), WM_QUIT is never posted, and
  WM_DESTROY/NCDESTROY only reset our window handle. Our previous proc is GLDirect's subclass (0x0935D650 in
  nukepanel.log), not prism_wndproc; this is harmless here.
- End-frame detour, frame cap and waitable timer: no. `rend_end_frame` is called only from the subsys handler
  (0x425c6e) and `rend_restart`. Once the subsys process is signalled its handler no longer runs, so our detour,
  our timer wait and our draws stop during the teardown. The loop then runs uncapped, which is why a stray process
  sits at 100% CPU.
- winmm proxy (timeGetTime/timeEndPeriod): no. Nothing in the exit condition reads time.

## 6. Diagnosing a stray process

- Take a `.dump /ma` of the stray. Read the process list at 0x476468 (a mod-caused hang shows `mm` lock 1,
  `__dl_guard` lock N) and the referenced items in cache `poi(0x4a1e54)+0xe0`. Process nodes: +4 next, +8 proc;
  proc +8 lock, +0x18 state, +0x48 name. Cache: [+0x14] block array, [+0x18] block count, 256 items of 0x98
  bytes per block; item +0 flags (0x40 = in use), +0x28 ops, +0x44 refs, +0x58 name.
- The rule for the mod: **release every cache reference it holds before the loaders process's clean-up needs it**
  (section 7). Making the menu quit "take the WM_CLOSE path" is moot: it already runs the same command.
- Releasing is `ci_dec_refs(item)` once per `mm_get_resource` (`mm_release_item` 0x41c720 goes straight to the item
  destroy 0x414130, so it is not a plain decref). A renderer restart also passes the window teardown, where the canvas
  re-acquires lazily by clearing `s_loaded`. A frame-scoped acquire/release instead of a session-long hold would also
  work, but costs a cache lookup every frame (the canvas flushes itself, prim.cpp:92).

## 7. The fix (verified in game: all three quit paths exit)

- Hook point: `process_delete_all` itself starts `push esi; call 0x405f50`, and our hook copies prologue bytes
  unrelocated, so the detour sits on 0x405f50 (it opens `config.cfg` for writing; clean 9-byte prologue). Its only
  caller is 0x405fc1, so it runs only where `process_delete_all` does: duke_base `game_quit_teardown` and host_main
  0x42510a (the "can't initialize gfx subsystem" exit), both quits, on the main thread, before any process is
  signalled. The address is derived from the export's verified bytes, not hardcoded.
- Release call: `ci_dec_refs` pairs with `mm_get_resource` (decrements +0x44; at 0, 0x413080 frees the item only if
  it is unloaded or flagged 0x10, otherwise the unreferenced sweep frees it). `mm_release_item` destroys the item
  outright (0x414130) and is not a decref.
- Texture: the shader loader's free (loaders+0x2c70, ops +4; read from a dump with cdb) calls `ci_dec_refs`
  (import 0xd0acf4) on every texture the shader holds, so releasing `font.shd` also releases `font.png`.
- Log: `quit: released N cache reference(s)`. An item whose +0x44 count reads below 1 is left held and logged,
  since a decrement would underflow and the item would never be freed.

Ghidra names: prism3d `prism_wndproc`, `main_loop`, `main_loop_iteration`, `host_main`, `process_list_reset`
0x405d00, `cache_get_resource`, labels `process_list` 0x476468 / `process_list_count` 0x476474. duke_base
`cmd_quit`, `game_proc_quit`, `game_proc_handler`, `game_proc_system`, `game_quit_teardown`,
`game_proc_confirm_leave`, `game_proc_close_servers`, `game_proc_cmd_exit` 0x100e4040 (`exit` = leave the
server), `ui_hdl_quit_event`. loaders.dll is not in the Ghidra project; its code was read from the dump.
