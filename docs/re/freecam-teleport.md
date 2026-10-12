# Free camera and teleport: write path, target ray, mouse, menu coverage

Teleport Duke by calling **the player's own virtual `vt+0x218` = `player_teleport(const vec3* pos, const vec3*
angles)`** duke!0x1007d830, exactly as the engine's `area_teleport` and map-change code do (no console command, no
exported setter). Pick the target with **`phys_ray_test(world, eye, eye+dir*R, m, m)`** and the camera's own
world-geometry-only `tmatcher_t` (p3shared exports); the record holds object, normal and hit point. Mouse look after
`inp_set_active(0)`: **WM_MOUSEMOVE deltas + SetCursorPos re-centring** in our WndProc subclass. The engine pushes the
camera only from `client.dll+0x2650` (play, in-game menus, mission complete). In normal play the teleport marker snaps
to the nearest track of the whole level (not only Duke's `+0xB20`), so depth lines are targets too (section 5); the
snap is off while First person or Move anywhere is on (`tp_track_locked`), where the marker places freely.

Tags: D decompile, A disassembly, X xref/scan, S string, L the mod's own `nukepanel.log`. Base prism3d 0x400000, DLLs
0x10000000. Every duke function named below has its name set in Ghidra. Decompile dumps in the Ghidra project:
`dumps/{tp,tp2,tp3,tp4,menu,menu2,menu3}.duke_base.dll.c` (script `scripts/DecompAt.java`).

## 1. Teleport write path

**(a) Engine setter: none by name.** The console has no setpos/goto/tp command (all 59 commands in
`dumps/console-registry.txt`, X). p3shared exports only `object_e::to_spawn_point` p3shared!0x10018390 (D). It copies
+0x18C and +0x1A8 from a random `player_start` and nothing else, so it cannot take a point.

**(b) What the engine itself does.** Three callers place actors through `vt+0x218(pos, angles)`:
- `area_teleport_do_teleport` duke!0x1009d890 (A 0x1009d8d9-0x1009d8ed): `push &target+0x1A8; eax = target->vt+0x160()`
  (world pos); `push eax; call [ent->vt+0x218]`. It does this only for objects with user bit 0x10 (actors). For
  anything else it writes +0x18C directly.
- `area_changemap_place_player` duke!0x1009ad00 (D): after a map change it calls
  `get_local_player(new world)->vt+0x218(dest+0x18C, dest+0x1A8)` with no class gate.
- `launcher_place_payload` duke!0x100afa50 (D) does the same for launched actors.

`player_teleport` = actor_player vtable 0x10136AA8 slot +0x218 (X, slot scan) = duke!0x1007d830 (D), in order:
1. `player_clear_motion(this,1)` 0x10077800: zero +0x1C0 velocity and +0xE34..+0xE68, `object_e::force_clear`.
2. **`actor_teleport(pos, ang)`** 0x1008e770: `+0x18C = pos` with **y + 0.01** (float 0x10126fa8); `+0x1A8 = ang`;
   `vt+0x200(-velocity)` (`player_add_velocity`, cancels residual velocity); `set_transforms_dirty`; `+0xB44 = ang`;
   `+0xB58/+0xB5C = -1`; **`+0xB20 (game_track) = 0`**, `+0xB24/+0xB28 = 0`; `vt+0x230`
   (`actor_track_update_if_moved` 0x1008dfa0); and, if the world's camera targets this actor,
   `camera_reset_and_retarget` 0x1008cc80, which snaps the actor_camera ([camera-culling.md](camera-culling.md) §1).
3. Unless `+0xC10 == 0x12`: `player_set_state(3)` 0x100775d0 (state machine +0xB7C/+0xB80[]) and `+0xC0C = 1`.
4. Reset a sub-object (`+0x1118`, 0x1008a510) and its input (0x1007db90).
5. **`player_retest_areas(this, 2)`** 0x1007d1e0: `spawn_search("s=%d?t=%d?Y=%d", sid, 0x20 /*area_e*/, 2)` and
   `vt+0x1BC(player)` on every armed area. This is how the new area, and with it the new `game_track`, is picked up.
6. `set_transforms_dirty`.

Physics itself moves objects the same way: `phys_move_object` prism3d!0x410290 ends with `+0x18C += delta`,
`+0x1C0 = vel`, `set_transforms_dirty` (D). The world matrix +0xF4 and the cached position +0x180 are **derived**
(`update_transforms` p3shared!0x10017e00 rebuilds them when the dirty byte +0x98C is set, D), so nobody writes them.

**Do NOT copy the rest of area_teleport:** its vt+0x1C4 0x1009e070 calls `player_save_checkpoint` 0x1007a3c0
(writes the progress checkpoint, "@@i_prgsaved@@" S); the `actor_teleffect` FX (0x1009d6e0) is cosmetic.

**Recipe:** on the main thread, outside the draw pass (our `rend_end_frame` detour; `area_teleport` does it from its
tick handler 0x1009d980):
```
if (!player || health(PS+0) <= 0) skip;        // player_is_dead_or_locked 0x1007e050 gate, see below
vec3 ang = player+0x1A8 (keep facing);  vec3 pos = target;
((void(__thiscall*)(void*,const vec3*,const vec3*))(*(void***)player)[0x218/4])(player,&pos,&ang);
```
The vtable slot is the one hardcoded constant, justified by the three engine call sites above (signature
`FF 97 18 02 00 00` at duke!0x1009d8ed). Copy the engine's gate `player_is_dead_or_locked`: refuse when health <= 0,
`player+0xC10` is 0x14-0x16, or `+0xAF0 == 0` (D).

**(c) Risks**
- **Stuck in geometry:** the engine never checks. Its targets are designer-placed points. Section 2 gives the
  clearance step. The player's box is +0x2B8/+0x2C4 ([hitboxes.md](hitboxes.md) §0), so centre y = floor y + dim.y + 1.
- **No game_track at the destination:** `actor_teleport` clears +0xB20. If no `area_*` covers the destination,
  `entity_camera_from_track` 0x1008e8a0 does nothing (D) and the engine camera freezes where it was. Track-relative
  controls are likely wrong too. Open (Q1). It is harmless while our freecam owns the view, but it matters when the freecam ends.
- **Physics sleep, network:** duke never calls `srv_sleep_entity`/`srv_wakeup_entity` (only resolver writes
  0x100df623/0x100df630, X). The player's position `net_send` is gated on a server flag (`world+200 & 8`,
  0x1007f2f0 D), and `srv_replicate` is not involved: single-player needs nothing.
- **Vis (PVS) sector links:** `map_vis_insert` (world vt+0x188) links an object into vtree sectors from its 8 sample
  points (player vt+0x174 = 8 corners at +0x320, A 0x1008ffd0). It runs at `post_init` (p3shared!0x10017f42, X).
  `srv_vis_update` (server vt+0x6C = remove+insert, prism3d!0x40a4c0 D) has **no caller** in duke_base or p3shared,
  and none of the engine's teleports call it. Whether a far-teleported player stays linked to its old
  sectors, which would make it invisible from the new leaf, is open (Q2). If the sector list is stale, the exported
  `srv_vis_update(player+0xE0, player)` would refresh it.

## 2. Target ray

Use **`void* phys_ray_test(world, const vec3* from, const vec3* to, tmatcher_t* block, tmatcher_t* touch)`**
prism3d!0x40d820 (D, cdecl). `world = player+0xE0`. It returns the first record of the global collision history
(`DAT_0049787c + 8`), or NULL. `phys_ray_collide` 0x40d6a0 (D) is the same query plus `out_hit`, but its bool
is "history non-empty", not "blocked". Prefer `_test`.
- Filters (`phys_push_query` 0x40e7c0, D): `block` goes to phys+0x48 (NULL means the default at phys+0), and
  `touch` goes to phys+0x4C (NULL means phys+0x24). The broadphase 0x4127c0 (D) skips objects with `+0x2E0 == 0`.
  An object that validates `block` is a blocking hit: `phys_ray_vs_objects` 0x40f1b0 keeps the nearest one and
  records it. An object that validates only `touch` is logged, but not as the hit.
- **World-geometry-only matcher:** copy the engine camera's own sweep (`game_track_place_camera` 0x10096c80, D):
  `tmatcher_t m; m.reset_rules(false); m.accept_superset(0x100,0,true); m.deny_superset(0x100,0x8000,false);`
  then the same for 0x10000, 0x20000 and 0x400. Engine type bit **0x100 = `world_e`** (`world_e` ctor ORs +0x48 with
  0x100, p3shared!0x1002e148, X). So actors, pickups and the player never match: the ray ignores all entities.
  `tmatcher_t::validate` p3shared!0x10026d20 (D) shows why the four denies must be separate calls: "superset"
  means every listed user bit must be present. All six functions are p3shared exports (ctor 0x100268c0, dtor
  0x100269f0). Pass `&m` as both `block` and `touch`, as the camera does.
- **Record** (colldesc, filled at 0x40f1b0; the camera reads the hit point at +0x14): `+0x00` object hit,
  `+0x04` normal vec3 (the vec4 plane from `ray_collide`), `+0x10` plane d, `+0x14` **hit point**, `+0x20` ray
  direction, `+0x30` sub-item, `+0x38` from, `+0x44` to, `+0x50` distance. Copy it at once: the next physics
  query overwrites it. Whether +0x04 is a unit normal facing the ray is open (Q3).
- **Placement:** take `hit + n*(max(dim.x,dim.z)+2)` for walls, or `hit.y + dim.y + 1` when `n.y > 0.7`. Then confirm
  with `phys_bound_test(world, from, to, &player+0x2B8, &m, &m)` 0x40d750 (same record) from a little above down to
  that point. Reject on a hit. The meaning of the bound-test record +0x14 (box centre at contact) is the same open
  point as [camera-culling.md](camera-culling.md) U5.

## 3. Mouse while game input is off

After `inp_set_active(0)` prism3d!0x407d60 (D) every DirectInput device is unacquired, so the engine produces no
mouse data. `inp_get_mouse_state` 0x406f30 is only the capture flag 0/1 (D), and `inp_cursor_info` 0x407e20 returns
the software cursor. That cursor is fed only by DInput relative events ([input-console.md](input-console.md) §3). So option (c) is dead.
- **Used: WM_MOUSEMOVE + SetCursorPos re-centring** in the WndProc subclass we already have. On freecam
  start: ShowCursor(FALSE) (balanced on exit) and warp to the client centre. On each WM_MOUSEMOVE: delta = pos -
  centre, then warp back and ignore the warp's own echo. DInput is unacquired, so the non-exclusive window gets
  every mouse message ([input-console.md](input-console.md) §5). This is also how the engine's own `camera` free-fly handles the pointer
  (`inp_set_mouse_state(1)`: SetCapture, hide, re-centre each frame, 0x406e90/0x405ff0).
  Conflicts: (1) `prism_wndproc` calls `inp_set_active(1)` on WM_ACTIVATE/ACTIVATEAPP/EXITMENULOOP
  ([input-console.md](input-console.md) §4), which re-acquires the EXCLUSIVE mouse and stops the messages: re-assert
  `inp_set_active(0)` every frame (as the panel does) and re-warp after reactivation. (2) Keep
  `inp_set_mouse_state` at 0, so the engine does not re-centre too. (3) Deltas are accelerated (pointer
  ballistics), which is acceptable for a camera.
- **Rejected: RegisterRawInputDevices.** Windows allows one raw-input target per device class per process, and
  DirectInput 8 builds its mouse on raw input. Whichever registers last takes the WM_INPUT stream, so every
  reacquire would silently steal ours, or ours would break DInput. The `nukepanel` input code already notes that
  the engine registers no raw input of its own (src/nukepanel/input.cpp:110). Not measured on this machine (Q4).

## 4. Menus and mission-complete screens

- **Single push site:** `rend_camera_push` has no duke_base caller (pfn 0x10184008 is only written, X). In both
  logs every non-empty interval shows `callers=[client.dll+0x2650 x1]` (120 intervals, L). That is
  `client_render_views`, driven by `wm_draw` from wm slot 0's object ([camera-culling.md](camera-culling.md) §1).
- **In-game menus (ESC, nuke menu, console) and HUD:** 2-D `ui` windows (`menu_main`, `menu_nuke`, `game_view`,
  S 0x10174508/0x10173b28/0x101744cc) drawn over the running world. The world still renders through the client
  push, so the substituted camera applies (this matches the zoom-out working in the menu).
- **Startup intro (no level):** zero pushes (`callers=[]` for the first 1,744 frames, L). The zoom-out substituted in the
  client.dll push does work in the main menu, so the main menu renders through the client push too.
- **Mission complete (`end_part`, vtable 0x1013caf0):** `end_part_activate` duke!0x100ad290 (D) looks up the
  `system_game2:partend` ui window (`ui_create_system_windows` 0x100e0b30 creates it). It then
  `spawn_search("s=%d?t=%d?n=%s", sid, 1, +0xA18)` for a **level-named view entity** and calls
  `set_view_entity(it)` (`wm_object(0,it)`, 0x100e55d0 D), saving the old view at +0xB38. That entity's
  `vt+0x164` builds the matrices, and the client pushes them. A substituted camera would replace that shot too.
- **Cutscenes:** `script_entity` (0x100b4140 via 0x100b3ff0, D) switches the view to entities with user bit 0x200
  (camera classes such as `actor_camera`) through `set_view_entity`. The render path is the same.
- `ui_wdraw3d` is not a menu path: its only duke caller `actor_draw_ui3d_texts` 0x1008c090 (D) draws world text.
- **Rule for the freecam:** it overrides every client push while it is on, so it works in menus, mission-complete
  screens and cutscenes. It is not dropped when the view object changes (end_part, cutscene).

Pause the game (freeze the world while the camera flies, teleport while frozen): [freecam-pause.md](freecam-pause.md).

## 5. Any track

Enumerate the level's tracks with `actor_pick_track`'s own query, snap the crosshair's world hit to the
nearest one in x/z, and hold the engine's re-pick across `player_teleport` (`+0xB1C` = 0), then set that track.
Verified in game.

- **Track lock only in normal play:** the marker snaps to Duke's track only while neither First person nor Move
  anywhere is on (`tp_track_locked`). With First person on, 8299 of 9828 casts were refused as off-track at Duke
  (5330,-976,128), because the marker stayed locked to the `+0xB20` line he no longer follows; in those modes it
  places freely.
- **Why +0xB20 can be far [L; D `actor_pick_track` 0x1008e0f0]:** on e01p01 the level's start already held track
  0x13DC07A0, whose polyline runs x 6028..6376 (the level's end), while Duke stood near x -300. The pick's first pass
  takes the touching track volume that is current, else the last one the search returns, so where two track volumes
  overlap a cleared `+0xB20` comes back as the far one. After Rescue's `player_teleport` (-277,-912) it came back
  0x13DC07A0 (`0x13b8fb90->0x0`, then 0x13DC07A0 at the next freecam start); the first person exit re-pick at
  (-313,-912) also kept 0x13DC07A0. State 17 never re-picks (`+0xB1C = 0` each tick returns the pick at once), so the
  stale track holds for the whole first person session. The exit re-pick does not land on the local track here: it is
  the engine's own choice where volumes overlap; normal walking switches back (0x13B8FB90 after a few steps).
- Log at each freecam start: `teleport: freecam start: placement track-locked|free ...; Duke (...), +0xB20 ...:
  N points, first (...) last (...)`.
- **Enumeration [D 0x1008e0f0, A vtables]:** `spawn_search("s=%d?t=%d?Y=%d", sid, 0x20, 1)` (the pick's query,
  0x1008e10b). The pick keeps an area when `vt+0x1E8(actor,0,0,&flag)` is true, `flag` is 0 and `+0xAE4` (volume) is
  set; game_track's `vt+0x1E8` is 0x10098af0 (`mov al,1; ret 0x10`, clears the outputs). We select by class name
  (`vt[1]`, as entities.cpp does): `game_track` (vt 0x10138688, `vt[1]` -> "game_track" 0x10166934) with `+0xAE4 != 0`.
  `game_track_auto` (vt 0x10138890, "game_track_auto" 0x10166c2c, `vt+0x1C0` returns 2) is skipped: see depth runs.
  Read once per freecam flight, and again when the player or his world changes, into one pool (32 tracks, 16384
  points). Log: `teleport: tracks: N cached (P points) of M game_track: no volume, unread, over the cap; auto
  skipped; areas walked`, then one line per track.
- **Marker:** the ray's world hit, x/z to the nearest point of every cached polyline (`trk_nearest_xz`); the nearest
  within 256 units wins (`trk_choose`); tracks within 8 units of it are a tie, settled by the floor (a down ray below
  each one's nearest point) nearest the hit's height. The winner is re-validated (header readable, `vt+0x1EC`
  fingerprint; a failure drops the cache, read again on the next cast), its `vt+0x1EC` snaps the hit onto the
  engine's line, then the floor drop and clearance as before. No track within the radius: red footprint at the hit.
- **Which track Duke gets [D 0x1007d830, 0x1008e770, 0x1008dfa0, 0x1008e0f0, A 0x100852b0]:** `actor_teleport` clears
  `+0xB20` (and `+0xB24`); `actor_track_update_if_moved` (vt+0x230) then runs the pick (vt+0x210) and the snap
  (vt+0x214 `actor_snap_to_track` 0x1008e310) at once, because `+0xB20 == 0`. Pass 1 keeps the **last** track whose
  volume holds him (`break` only on the current one, now 0), so inside overlapping volumes **the pick need not return
  the line he stands on**; the snap would move him onto the picked line, and `player_retest_areas(2)` (0x1007d1e0,
  later in the same call) would fire triggers there. **So** `+0xB1C` is held at 0 across the ONE call: the update
  returns at once (no pick, no snap), +0x18C stays the target through `player_set_state(3)` (exit 0x100ee2f0 is
  `ret 8`; the state 3 enter 0x100852b0 writes `+0xB1C = 1` at 0x100852bf and touches no position), so the area retest
  runs at the target. After the call: `+0xB24 = 0` (what the pick writes after actor_teleport: the cleared `+0xB20`),
  `+0xB58/+0xB5C = -3`, `+0xB20 = chosen` (the pick's own write, 0x1008e2da) and `+0xB1C` restored. The next tick's
  update finds him moved from its pose cache (not refreshed while held), picks (the current track wins while its
  volume holds him) and snaps. Not held when `+0xB1C` was already 0, under first person, or when the chosen track no
  longer reads (logged). Logs: `teleport: #N ... (engine's pick 0x0, chosen 0x.., set with +0xB1C held)` and the
  frame-later line `(the chosen track|NOT the chosen track 0x..; kept K, lost L)`. Side effect, read: actor_teleport's
  `camera_reset_and_retarget` runs with `+0xB20 = 0`, so the engine camera is placed from the track from the next tick.
- **Depth runs [D 0x1007f590, state 4 pre-tick]:** state 4 (vtable 0x10136930) is entered from the net message that
  sets `+0xB20` by id and clears `+0xB1C` (0x1007ece5-0x1007ed19). Its pre-tick walks the `game_track_auto` connector
  (it requires `+0xB20->vt+0x1C0() == 2`), and at the end clears `+0xB20` and sets state 3, whose pick lands him on the
  next line. A teleport in state 4 is refused and the marker is red with its reason (`tp_depth_refuse`):
  player_teleport would leave the run half done. Also, the pick never leaves a current track whose `vt+0x1C0` is 2,
  so landing on an auto track would hold him there.
- Open: (1) whether each line's own volume holds Duke anywhere on its line (else the next pick moves him to the
  track whose volume does; the frame-later `lost` count shows it); (2) stacked tracks (same x/z, other floors) tie
  only through the floor rule; (3) the 256-unit radius is a chosen value, not an engine one.


## Open questions

1. **Q1** Destination without a `game_track` area: log `player+0xB20` after a teleport into open space, then walk.
2. **Q2** Vis sector staleness after a far teleport: read `player+0x930` before and after; is Duke visible there?
3. **Q3** Ray record +0x04 = unit normal facing the ray: log it on a floor hit (expect (0,1,0) on a floor).
4. **Q4** Raw input versus DInput8 in one process: count WM_INPUT across an alt-tab (only if WM_MOUSEMOVE proves too
   coarse).
5. **Q5** The `end_part` view entity's class: log `vt[1]` of `wm_get_object(0)` on the mission-complete screen.
