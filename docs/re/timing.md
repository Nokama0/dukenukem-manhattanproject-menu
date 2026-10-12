# Frame timing and frame-rate dependence (prism3d.exe / p3shared.dll / duke_base.dll)

One variable step per frame, no fixed substeps. dt = `timeGetTime` difference in whole milliseconds
(timeBeginPeriod(1)), converted to double seconds, clamped only from above (0.5 s) by `vprocess_t::update`.
Movement integrates in closed form (constant force over the step), and velocity changes are 1 ms impulses. At
175 Hz, gravity and jumps are therefore frame-rate independent. The 1 ms quantisation causes per-frame jitter only,
never drift. **Zoom never fixed the double jump in code or game data.** Every .zipfs is byte-identical to the 2009
Steam data. The 1.4 "fix" is a `dxwrapper.ini` dated 30 Jun 2023 with `EnableVSync = 1` (a frame cap on the dx8
path). The 1.9 fix is `LimitPerFrameFPS = 60`, which also throttles the `gl` path (section 3). The actual
double-jump failure is not a dt error: a 5 ms takeoff frame lets the ground sweep re-contact the floor and disarm
the boost ([double-jump.md](double-jump.md)).

Methods: D = decompile, A = disassembly, X = xref/script, S = string, F = file diff. Addresses: prism3d base 0x400000,
DLLs 0x10000000.

## 1. How frame time flows

- **Clock:** `sys_init` prism3d!0x402250 calls `sys_timer_period_1ms` prism3d!0x402c50 = `timeBeginPeriod(1)` (D).
  This is the only reference to timeBeginPeriod (X). `tmr_read` prism3d!0x402c70 = `timeGetTime()*1000` (usec, D).
  No QueryPerformanceCounter anywhere (X on imports). `tmr_get_cycles` (rdtsc) 0x402c80 is used only by
  `collision_solve` prism3d!0x411d90 to time its own work (profiling-shaped: the results only feed `tmr_cycles_to_usec`).
- **sys_frame** prism3d!0x4024f0 (D): `vtmr_update(tmr_read())` (p3shared!0x1002df40 updates every vtimer). It then reads the
  engine vtimer `DAT_00475b9c` +0x18 (last delta, u64 usec), and sets
  `g_frame_dt DAT_00476430 = delta_usec / 1e6` (double seconds) and `g_time DAT_00476438 += dt`.
  **No clamp, no minimum, no int truncation of seconds.** Because the source is whole ms, dt is always a multiple of 0.001 s.
  At 175 Hz, frames alternate 5 ms and 6 ms (about +-10 % jitter). The sum of dt stays exact, so there is no drift.
  If two frames fall in the same ms (only above 1000 fps: menus, loading), dt = 0.
- **Readers:** `sys_get_lastdelta` 0x402590 returns g_frame_dt. Its only engine callers are `main_loop_iteration`
  0x405ff0 and `spawner_handler` 0x40c7a0 (X); duke_base never calls it (pfn unused, X). `sys_get_time` 0x4025c0 returns g_time.
  `sys_get_timedelta(double* last)` 0x4025d0 returns `g_time - *last` and stores it (unclamped).
- **Per-process dt:** `main_loop_iteration` calls `vprocess_t::update(proc, sys_get_lastdelta())` for every awake
  process (D). `vprocess_t::update` p3shared!0x1002d570 (D): `+0x30 = old time; dt = min(dt, 0.5); +0x38 = dt
  (double); +0x40 += dt (process time, double)`. Minimum dt is 0. `process_wakeup` 0x405ea0 zeroes +0x38 (D).
- **The world is a process.** `FUN_00409e00` sets vtable 0x4346d0 (A 0x409e39), which render.md lists as the prio-10
  process. Every entity reaches it as `entity+0xE0` = world, and **all game logic reads `world+0x38` (dt) and
  `world+0x40` (time)**: player_tick, gravity, jump timers (D, section 2).
- **Other clocks in duke_base** (X on pfn labels, dumps\duke_base.pfn.txt):
  - `sys_get_timedelta` has 4 users. `FUN_100e0c80` (+0x548 last-time) drives `skmod_update_time`, the animation
    clock: real time, **not** clamped at 0.5. `FUN_100739e0` is the free camera (`g_cam_fly_speed`); `FUN_1008c750`
    and `cmd_camera` 0x100e48b0 only reset that timestamp.
  - `sys_get_time` has 10 users: timers such as `FUN_100c5140` and `FUN_100999d0` compare `now - (float)stamp`.
  - Time is often stored as **float**: player +0xE10/+0xE14, stats +0x226D0. At 1 h of process time the float ulp is
    0.24 ms, which is harmless at 5.7 ms frames. It only gets near 5 ms after about 11 h.
- **Leftover 60 Hz design trace:** `world_proc_handler` prism3d!0x40a160 does `+0x3D8 = (+0x3D8 - dt) + 1/60` every
  frame (constant 0x434740 = 0.0166667, A 0x40a22c..0x40a248). No reader was found outside that function (X on
  `+ 0x3d8]` in prism3d and duke_base; the other hits are `__ui_proc` fields). At 175 Hz it grows by about 0.011 per frame
  without bound.

## 2. Physics and player movement

**World update order** (`world_proc_handler` 0x40a160, D; once per frame, one step):
1. If world+0x488 is set: `spawn_get_iface(3)->vt+8(1, &dt)`.
2. Every active entity (vt+0xBC): vt+0x58, then (flags&8) vt+0x60, then vt+0xC0. After that `FUN_0040a810` and `FUN_0040ae80`.
3. Every active entity with world flags&4 runs **vt+0x5C = think**. For the player that is `player_tick` duke!0x1007ae50.
4. The +0x3D8 accumulator (above).
5. `world_physics_step` 0x40a260 (D). For each object: vt+0x114 (player: `player_physics` duke!0x1007d090), then
   **`object_e::force_process(world dt)`**, then `accel_table_t::eval/flush`. Next, vt+0x118 for flagged objects,
   then `collision_solve(world+0x4B8, dt)` 0x411d90.

The `phys_simulate_step` export 0x40d940 has **no callers**: duke_base resolves it but never uses it (X), and the
engine only reaches it from the export table. It is not the simulation path.

**Integrator.** In p3shared, `object_e::force_insert(F, duration)` 0x100104f0 (D) keeps a list of forces sorted by
remaining duration (list at +0xE4). `object_e::force_process(dt)` 0x10010560 (D) sub-steps at each force expiry. For
each sub-step of length t, with a = sum(F)/mass (mass = +0x2D8):
`delta(+0x1D8) += v*t + a*t^2/2` and `v(+0x1C0) += a*t`. After the forces run out, the rest of dt adds `v*t`. This is
**exact for piecewise-constant forces**, so it does not depend on dt except for collision.
- **Gravity:** `get_gravity_force` duke!0x100e7e00 (D) returns `(0, -g_gravity*mass, 0) * world_pct/100`, with Y up and
  g_gravity cached once a second. Each frame, the player move-state handlers insert it for **duration = this frame's dt**:
  `FUN_1007fbe0` and `FUN_10081cc0` (D), and `player_try_ground_jump` when grounded.
- **Velocity changes are 1 ms impulses:** player vt+0x200 = `FUN_10078a50` -> `object_add_velocity_impulse`
  duke!0x10091120 (D). It calls `force_insert(mass*dv*1000, 0.001)`, i.e. dv delivered over exactly 1 ms. That is
  exact whenever dt >= 1 ms (true at 175 Hz). When dt < 1 ms, part of the impulse carries into the next frame.
- **Terminal velocity:** `player_physics` 0x1007d090 (D/A 0x1007d10e..0x1007d190): if `vy <= -g_p_fall`, it adds
  impulses that set vy = -g_p_fall. This does not depend on dt.
- **Player cvars:** `player_refresh_cvars` 0x1007c9e0 re-reads the table at duke!0x10163e80 every tick. Entries are
  {cvar*, float, name}, and `player_cvar_value(i)` 0x100775b0 returns entry i. Indices: 0 g_p_speed, 1 g_p_jump,
  2 g_p_boost, 5 g_p_boost_sweet, 12 g_p_fall, 0x16 g_p_hover... (X/D).

**Jump logic** (`playerstate_move_update` duke!0x100855d0, a player-state vt method at 0x101369b8; D):
- Jump fires on the **edge** of impulse bit 4: `mask(+0xE00)&4 && !(prev +0xE04)&4`.
- **Grounded (+0xB00 != 0)** -> `player_try_ground_jump` 0x10088210 (D). It refuses when `vy > 0`, when the platform
  vector +0xE4C..E54 has magnitude > 0.001, or when `now - lastJump(+0xE10) < 0.1 s`. Otherwise it records the jump
  start position (+0xE6C.. and +0xE70 = y), sets +0xE10 = now, and applies impulse (0, **g_p_jump**, 0) plus platform
  velocity. It also adds one frame of gravity (`force_insert(g, dt)`).
- **Airborne (+0xB00 == 0) = the double jump / boost.** Conditions: `now - (float)+0xE10 > 0.2`, `+0xE14 < +0xE10`
  (not yet boosted in this jump), and `y > jumpStartY - 50`. It cancels vy with an impulse, then adds
  `v = sqrt(2*g*|jump^2/2g + boost^2/2g - (y - y0)|)` if `|vy| <= g_p_boost_sweet`, otherwise `v = g_p_boost`. It
  then sets +0xE14 = now and plays jump sound/animation 1.
- **Ground detection** `player_ground_probe_anim` 0x10085dd0 (D): `phys_bound_collide` from the position to 10 units
  below. With no hit, the state becomes 6 (float), and state 7 (fall) once `vy < -0.5*g_p_jump`. The window is a
  fixed distance, i.e. the same time at any fps (10 u / 600 u/s = 17 ms).
- **Move state (player +0xC0C / +0xC10, int):** `FUN_10077610`
  duke!0x10077610 is thiscall(state) = `mov eax,[esp+4]; mov [ecx+0xc0c],eax; ret 4` (A), so +0xC0C is the
  **requested** state. `player_ground_probe_anim` 0x10085dd0 requests 6 and 7 through it and tests both +0xC0C and
  **+0xC10** against 5/8 and the range 5..7 (D). That +0xC10 is the current state is inferred (nothing seen writing it).
  The "+0xB7C (state)" in section 3 is unconfirmed for the player (on monsters +0xB7C is behaviour flags,
  entities.md section 3). The probe logs all three.

**Per-frame (not per-second) logic found**:
- `player_tick` 0x1007ae50: `if (!+0xA14 && +0xECC > 0) +0xECC -= 1` is a **frame counter** (A 0x1007afae..afb9).
  The ctor sets it to 4 (A 0x100769be), and it is tested for -1 on the respawn path (A 0x1007b4be). Other entities
  also use +0xECC (monster_apply_damage etc.), probably for a different field.
- Input is sampled per frame: impulses are reset each frame by subsys (render.md), and edge detection compares
  +0xE00 with +0xE04 (copied at entry of `playerstate_move_update`, double-jump.md section 1).
- `playerstate_move_update` keeps an idle accumulator in state+4 (`+= dt`, zeroed on input). It is dt-based.
- Everything else read in player_tick is dt-scaled (`+0xB04 += 2*dt`, `+0xEC4 += 50*dt`, `+0x10F4/+0x1110 -= dt`).

## 3. Zoom's fixes, the data diff and the double-jump probe

- **Game data is untouched** (F, zipfile CRC per entry). All of db_cor, db_e01..e08 and db_p101 are identical across
  Classic, Enhanced and **Steam 2009**. Enhanced-only archives:
  - `db_mapfix` rebuilds map e01p01_a02 and edits the world.scr of e05p01_a01_r01/e05p01_a02. This is the changelog
    1.7 map work.
  - `db_zoom` contains scr_intro.sc and logo_zoom.shd.
  - `db_demo_maps` contains the editor sample maps; db_lang_* are the translations; root.zipfs is a dummy.
- Enhanced `init.cfg` only adds mounts and `exec ~GAME/init_ex.cfg`. That file is generated by the launcher, whose
  strings (S) show that it writes r_mode, r_device, dx8_/gl_ refresh, tmatrix, envmap, cva and fullscreen, and
  **nothing about timing**. The launcher imports no WriteProcessMemory (X on the PE import table), so it patches no memory.
  Both config.cfg files set `gl_sync "1"`.
- **The 1.4 fix is dxwrapper.** Classic `dxwrapper.ini` is dated 30 Jun 2023, the 1.4 release date. It has
  `D3d8to9 = 1` and `EnableVSync = 1` (F), and Steam has no wrapper at all. So "Fixed double-jumping for high
  framerates" = forcing vsync on the dx8 path. 1.9 (8 Sep 2026) adds `LimitPerFrameFPS = 60`.
  - Both fixes are frame caps: the bug grows with fps, and the caps hide it rather than fix it.
  - On `r_device gl` the process loads dxwrapper's `d3d8.dll` too (`sys/dx8.dll` is always loaded), and GLDirect uses
    system `d3d9.dll`. Setting `LimitPerFrameFPS` from 60 to 0 took `gl` from 60.0 fps to 175 fps, so the limiter
    throttles `gl` as well (render.md section 6). GLDirect's `opengl32.dll` imports d3dx9_35 and loads `D3D9.DLL`
    (S/X), never d3d8.dll: that static reading alone predicted the opposite.
- **Double-jump mechanism: settled.** Static reading shows no dt-scaled jump error. The mechanism is the takeoff
  ground re-contact on frames of 5 ms or less ([double-jump.md](double-jump.md)). The other suspects checked:
  1. **Impulse bit edge.** One edge per press is enough; a held key does not re-edge (double-jump.md section 1).
  2. **Ground contact after takeoff.** This is the mechanism. For the first frames after a ground jump the player
     is still inside the 10-unit probe, so +0xB00 may stay set. `player_try_ground_jump` blocks a second jump only
     when vy > 0 or within 0.1 s of +0xE10. The impulse is applied in the *same* frame's physics, so vy > 0 from the
     next frame. The airborne double-jump gate `now - E10 > 0.2` is float-time based, so it is independent of fps.
  3. **dt = 0 frames.** Above 1000 fps (menus, or uncapped with vsync off), velocity impulses are delivered only
     partly (1 ms duration > dt) and gravity forces with duration 0 are added.
  4. **Frame counter +0xECC.** Per-frame, but not a jump gate.

  **The probe** (instruments.cpp, `jf` lines) logs one line per frame:
  - world+0x38 (dt) and world+0x40;
  - player +0x1C0..0x1C8 (velocity) and +0x190 (y);
  - +0xB00 (grounded), +0xB7C (state), +0xE00/+0xE04 (masks), +0xE10/+0xE14 (jump stamps), +0xECC.

  It also counts calls that return 1 from `player_try_ground_jump` 0x10088210, and boosts (the vt+0x200 impulses
  from 0x100855d0).

## 4. Options for 175 Hz-safe play

The menu ships the targeted fix (option b's shape: restore the takeoff disarm, "Fix double jump", double-jump.md).
The others stay as the known alternatives.

1. **(d) Plain frame cap.** Sleep and spin at the `sys_frame` entry 0x4024f0 (render.md 6), before the engine
   samples the clock. This is exactly what Zoom relied on (vsync, then 60), so it hides the bug. Unlike dxwrapper,
   it covers both gl and dx8.
   - Against: it gives up 175 Hz.
   - A cap of 87.5 (=175/2) or 116.7 (=175/1.5) keeps pacing even.
2. **(c) Fixed-rate world, render every frame.** The structure allows it. The whole simulation (think vt+0x5C,
   gravity inserts, force_process, collision) lives in one process handler, `world_proc_handler` 0x40a160 (prio 10).
   It reads only world+0x38/+0x40, which `vprocess_t::update` sets. Render is `client` prio 99, HUD prio -3.
   A wrapper could run the handler 0..n times per frame with +0x38 = 1/60 (and +0x40 advanced) from an accumulator.
   Needed:
   - **Latch input impulses.** They are reset every frame by subsys prio 0, so a frame with zero world ticks would
     drop an edge.
   - Keep duke `game_proc` (prio 98), `spawner` (prio 1) and the skmod animation clock (`FUN_100e0c80`, real time)
     consistent with the world clock.
   - Interpolate: store previous/current world transforms of drawn objects and the camera (entity pos +0x18C/vt+0x160,
     view angles +0x1A8) and blend at render. Without it, the view and mouse-look update at 60 Hz, i.e. judder.
     This is the largest cost.
   - For: a single entry point and a single dt source; +0x3D8 shows a 60 Hz notion.
   - Against: many subsystems read `sys_get_time` directly (section 1). Mouse look through the camera entity
     would lag unless it is split out.
3. **(b) Clamp or fix dt for specific consumers.** This helps only where one gate leaks, e.g. a minimum dt of 1 ms
   against the dt = 0 impulse loss, or the takeoff disarm. No general dt misuse was found: gravity/jump are
   closed-form.
4. **(a) Higher-resolution timer (QPC in `tmr_read` 0x402c70, 3 callers).** Quantisation exists (1 ms, 5/6 ms
   alternation at 175 Hz). But dt sums exactly and the integration is closed-form, so the effect would be smoother
   motion, not bug removal.
   - Cheap and low risk: one export, u64 usec contract.
   - dt = 0 frames would disappear.

## 5. Measured in game

- At 60.0 fps on `gl`: dt alternates 0.016/0.017 s (1 ms quantisation). A ground jump stamps +0xE10 = t and gives
  vy 569-573. A second press in the air stamps +0xE14 = t (boost, vy about 373-400). On landing +0xE14 is reset to
  the +0xE10 value, so a boost counter must not count that reset. The frame rate was capped at exactly 60.0 fps
  (the dxwrapper limiter, section 3; render.md "Measured in-game").
- At 175 fps (dxwrapper limit 0, vsync on): dt 0.005-0.008 s. 118 jump presses gave 43 ground jumps. Air presses
  restamp +0xE14 on every press (66 stamps), as they do at 60 fps, and the velocity keeps falling smoothly through
  later presses, so there is no extra impulse. Play at 175 fps showed no obvious frame-rate bug; the occasional
  takeoff disarm shows only in the per-frame log (double-jump.md).
- The Steam build's bugs happened uncapped (hundreds of fps). A frame cap (vsync, plus a cap of our own as a safety
  net) hides them; the targeted fix and the fixed-rate world (section 4 b/c) are needed only where a bug shows at or
  below the cap.

## Open questions
- Whether world+0x3D8 is read anywhere (hardware read-watch on it).
- Whether `collision_solve` 0x411d90 uses rdtsc as a budget or only for profiling (read it fully).
- The meaning of player +0xECC (per-frame counter): watch its writes during play.
