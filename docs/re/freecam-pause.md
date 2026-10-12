# Free camera: Pause the game

The menu freezes the world with the prism3d exports **`process_sleep(world)` / `process_wakeup(world)`** on the
player's world (`player+0xE0`). These are the same two calls duke's own pause makes on each game server, without
its `+0x53C` flag and without the `camera camera` the `pause` command can run. Every call is preceded by
`process_pid_get(pid) == world`, so a world the engine has removed is never touched. Verified in game: pause, far
clip and FOV while paused, and the map far override.

Tags: D decompile, A disassembly, X xref, S string (Ghidra MCP, duke_base 0x10000000, prism3d 0x400000).

## 1. The `pause` command and the state it sets

- Command table entry 0x101733b8: name `pause` (S 0x10173524), handler **0x100e5b30** (created as `cmd_pause`):
  `game = [[0x101869dc]+8]`; if non-null, tail-jump **0x100e4160** (A).
- 0x100e4160 (D) toggles on the game's byte **`+0x53C`** (paused):
  - not paused: `FUN_100e4a20(game, 1)`; then, if `wm_get_input(0)` is the local player **and** `player+0xB34` is an
    object with `+0x48 & 2` and user bit `+0x4C & 0x100` (a player), `cmd_execute("camera camera")`.
  - paused: `FUN_100e4a20(game, 0)`; then `camera player` if the input object's `+0xB34` is itself.
- **When does `camera camera` run?** `player+0xB34` is 0 after `actor_track_state_reset` 0x1008d8c0 (D) and is set
  only by `FUN_1008d590` (`this+0xB34 = arg`, D), which the `camera <name>` command 0x100e48b0 calls on the player
  (D). So in normal play it does not run; after any `camera player` it would. `camera <name>` re-targets wm input
  slot 0, sets the mouse state to 1 and sets the cheated flag (input-console.md section 3): it would fight our
  camera.
- **The freeze itself, `FUN_100e4a20(game, on)` (A 0x100e4a20):** for every node of the game's server list
  `+0xF8`, `process_sleep(node+8)` (on) or `process_wakeup(node+8)` (off), through the pfns 0x101841cc /
  0x10183f88 that `FUN_100df650` fills by `GetProcAddress("process_sleep" / "process_wakeup")` (D). Then
  `+0x53C = on` (only when `+0x104` is non-zero, or when unpausing).
- **No cvar or time scale:** no string `pause`/`freeze`/`timescale` names a cvar in prism3d or duke_base
  (`pause_game` S 0x10166e84 has no xref; `snd_pause` is sound only).

## 2. The lever and why

- `process_sleep` 0x405e80 (A): `if (p+0x14 == 0) p+0x14 = 1`. `process_wakeup` 0x405ea0 (A): `if (p+0x14 == 1)
  { p+0x14 = 0; sys_get_timedelta(p+0x28); p+0x38 = p+0x3C = 0; }`, so a woken process does not catch up.
  Boolean, not counted. Both cdecl, one stack argument, plain RET.
- `main_loop_iteration` (D, the loop at 0x406001): for each process, `update` and the think `vt+4` run only while
  `+0x14 == 0 && +0x18 == 0`; `vt+8` runs regardless. So a sleeping world skips its ticks: enemies, projectiles,
  timers and everything the world thinks.
- **Which process:** `game+0xEC` is the current server and `entity+0xE0 == game+0xEC` (`FUN_100e70e0`, D); the
  server-list nodes `+8` are the same objects (`FUN_100e1ca0` compares them to `+0xEC`, D). So `player+0xE0`, the
  world teleport.cpp already passes to `phys_ray_test`, is what the engine's pause sleeps.
- **Liveness:** `process_pid_get(pid)` 0x406180 (A) walks the live list and matches `process+0x20`. The glue reads
  the pid once when it takes a world and checks it every frame before a sleep or a wake.
- The three entries are fingerprinted at first use (freecam.cpp `pause_levers`): another build is refused, logged.
- **Not the command:** it toggles (we would have to read `+0x53C`, a raw offset off duke's global) and may run
  `camera camera` (above). **Not `FUN_100e4a20`:** unexported, and it writes the flag our release could not tell
  apart from a menu's.

## 3. Does our camera keep running?

- Yes, from the binary: only the game servers sleep; the client process that renders (and pushes the camera from
  `client.dll+0x2650`) keeps running, which is why the in-game menu (itself a pause, section 4) still draws the
  world and our substituted camera there (freecam-teleport.md section 4). Our frame hook is `rend_end_frame`, the
  mouse arrives through our WndProc, and the view is captured in the push detour: none is a world tick.

## 4. The engine's own pauses and level loads

- **Main menu in a level (`ui_hdl_mainmenu`, factory 0x100dd970, ctor 0x100a46b0, vtable 0x1013b278):** its slots
  at 0x1013b5f0 / 0x1013b5f4 are 0x100a4bf0 (`FUN_100e4a20(game, 1)`, D) and 0x100a4c90 (restores the game
  state and wakes when it is 7, D). So closing the menu wakes the world under our flight: the rule sleeps it again
  the next frame (process_sleep every frame while held). At most one world tick may run between (depends on the
  process order); the log counts these as `refreezes`.
- **Help screen (state 10):** `FUN_100e4360` (D) wakes on its close too.
- **Map load (`FUN_100e1e80`, D):** spawning a map pauses (`FUN_100e1dc0`), the connect step and state 7 wake
  (`FUN_100e4a20(game, 0)` unless `+0x558`). The new level is a new world: our rule lets the old one go and does
  not freeze the new one until the next flight (a level change resumes).
- **Release under an engine screen:** if the flight ends while `screen/menu_main` or `screen/menu_help` is shown,
  we leave the world asleep and let go; the screen wakes it when it closes (above). Otherwise we wake it.
- The `pause` cheat key (P) while we hold: the engine's toggle sets/clears its flag around our sleep; a wake it does
  is undone by our next frame's sleep.

## 5. Teleport while frozen

- `player_teleport` (freecam-teleport.md section 1) is a direct chain of calls: `actor_teleport` writes `+0x18C`,
  `set_transforms_dirty`, re-picks the track (`vt+0x230`), `player_retest_areas` runs `spawn_search` and the
  areas' `vt+0x1BC`. None needs a world tick. `player_set_state(3)` takes effect at the next tick (after resume).
- `update_transforms` (p3shared 0x10017e00) rebuilds `+0xF4`/`+0x180` when the dirty byte is set; its callers are
  not read (p3shared is not in the Ghidra project). Our "You" marker reads `+0x18C` and moves at once. teleport.cpp's
  "a frame later" line shows the same spot while frozen (no tick ran), as expected.

## 6. The far clip while frozen

- **Measured (nukepanel.log):** paused, every 5 s nocull line read `far=1000000 stock=8000
  projection-top near=16.0 far=8000`; unpaused flights read `view: near=16.000 far=1000000.000`.
- **Cause (D/A, Ghidra MCP):** `actor_camera_build_camera_matrices` duke!0x1008c4c0 runs every frame from
  `wm_draw` (client process, not the world): it compares `rend_projection_get` with three cached floats and, when
  they differ, calls `rend_projection_change(near, far, fov)` (pfn 0x101841e0; `push fov, far, near` at
  0x1008c5bc-0x1008c5d0) with `0x1016584c` / `0x10165858` / `0x10165864`. Those are the float fields of the g_cam
  table entries named `g_cam_near` / `g_cam_far` / `g_cam_fov` (S 0x10163cdc / 0x10163cd0 / 0x10163cc4). The cache
  is refreshed only by `FUN_1008c440` (re-reads a cvar when its `+0x70` modified byte is set, then clears it),
  which is actor_camera vtable `+0x1b8` (slot 0x10137368, its only xref) and is called only from
  `actor_camera_handler` 0x1008c93f, the camera's **world tick**. With the world asleep the cache keeps 8000, and
  the per-frame builder puts 8000 back every frame. The builder is the client's; the stale value is the world's.
- **Fix:** in the freecam branch of our push detour (after `wm_draw`, before the frame's frustum and draw),
  `nocull_push_projection` runs while our pause holds. It reads the projection and the live `g_cam_near` /
  `g_cam_fov` cvars (atof of the string, as the cache does) and calls the exported `rend_projection_change` (prism3d
  0x4202d0: writes the projection top, tells the device, tail-jumps `rend_frustum_changed`) when anything differs.
  The far it pushes is the raised one when nocull applied it, else the projection's. So the freecam FOV wheel (which
  writes `g_cam_fov`) also works while frozen. The builder undoes it at the next frame's `wm_draw`; we redo it at
  the next push. On resume the woken tick reads the cvars (their modified bytes were never cleared while frozen),
  so the builder applies them itself; nothing is restored. The 5 s nocull line prints the projection far read back
  and a cumulative `paused-proj-pushes`. Cost while paused: two cvar reads (~4 guarded reads) a frame.
- **Map far override:** a world with its own far (`FUN_100e7020(...)+0xf4 != -1`) overrides g_cam_far in the
  builder (0x1008c5dd-0x1008c601), paused or not. Measured on e06p02_a01 while flying unpaused: `nocull: far=1000000 stock=8000 projection-top near=16.0 far=1600`, so distance culling returned. Fix: `nocull_push_projection` also
  pushes the raised far, far alone, whenever the free camera flies, our raised far is applied and the projection's
  far is below it (`nocull_far_override`; the world's field is only inferred, never read or written). Log:
  cumulative `override-pushes=` on the 5 s line and one `the map's own far plane is N` line per flight and value.

## Open questions
- Whether one world tick runs between the engine's menu close and our re-sleep (log: `refreezes`).
- Whether Duke's model is drawn at the teleport target before the world resumes (section 5).
- Sounds: the engine's pause does not call `snd_pause`; looping sounds likely keep playing, as under its menu.
