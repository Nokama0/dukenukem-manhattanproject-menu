# Camera pipeline, culling and camera walls (duke_base / prism3d / client)

The view is built at draw time from the `actor_camera` entity named "camera" (`actor_camera_build_camera_matrices`
duke!0x1008c4c0). The eye is `camera+0x18c`, which a spring pulls each tick towards a target that the current
`game_track` supplies. The panel's zoom out swaps in pulled-back copies of the view and inverse in its
`rend_camera_push` detour. The frustum, PVS lookup, one-sided mesh tests, sound listener and the ESP all read the
pushed pair or `rend_get_camera`, so they follow the move (works in game: listener and ESP stay right). The PVS is
no risk: every playable map has one vis leaf ([[pvs.md]]). The engine's own camera **does collide**:
`game_track_place_camera` duke!0x10096c80 sweeps a 16-unit box with `phys_bound_test`; the panel's pull-back passes
through walls and does no sweep. The in-game zoom is a **distance** zoom, not FOV (`player+0xb50` = 0.5 scales the
track's eye offset). Culling has **no cvar**. It is disabled by forcing the three frustum exports to report
"visible"; the duke_base PVS walkers need no change, because every playable map is one sector and one leaf
([[pvs.md]]).

Addresses: prism3d base 0x400000, DLLs 0x10000000. D = decompile, A = disassembly, X = xref/script, S = string/data.
Ghidra functions named: duke `actor_camera_*`, `security_camera_*`, `game_track_place_camera`,
`player_update_zoom`, `player_build_camera_target`, `entity_camera_from_track`, `map_*`, `camera_effects_sum`,
`set_view_entity`, `spring_step`; prism3d `rend_build_frustum_planes`, `server_draw`, `server_load_world`; client
`client_proc_handler`, `client_render_views`, `client_node_set_target`. Decompile dumps (kept beside the Ghidra project): `dumps/{actor_camera,map_world,cam_player,cam_rays,game_track,zoom3,cull*.prism3d}.decomp.c`.

## 1. Camera pipeline

**Who is the camera.** On level entry `FUN_100e1e80` case 4 (duke!0x100e2295, S "avatar spawned. spawning camera and
attaching client view...") spawns `scripts/camera.sc` = `spawn actor_camera {}` (read from `db_e01.zipfs`) with name
"camera". It then calls `FUN_1008d590(camera, player)`, which sets `camera+0xb34 = target`, and then
`set_view_entity(camera)` duke!0x100e55d0, which calls `wm_object(0, camera)`, and `wm_input(0, player)` (D).
actor_camera vtable 0x101371b0 (class table, X). security_camera (vtable 0x10136180) is a second camera class that
takes the view the same way (duke!0x10073e90 -> `set_view_entity(this)`, D).

- `camera <name>` (duke!0x100e48b0, D): `this` = spawn_search flag 0x200 = **the camera entity**. Then
  `wm_input(0, ent)`, `FUN_1008d590(camera, ent)` (camera target := ent) and `set_view_entity(ent has flag 0x100 ? camera
  : ent)`. So `camera camera` gives input to the same actor_camera and makes it target itself. input-console.md calls the
  0x200 search "the player"; per this read it is the camera.
- Free-fly: `actor_camera_process_input` duke!0x1008c750 (vtable +0x154) moves `+0x18c` from impulses 101-104 and
  111/112 and turns `+0x1a8/+0x1ac` from 201-204 (D). While `target == self`, the handler skips the follow logic (below).

**Per tick (game proc, prio 98): `actor_camera_handler` duke!0x1008c920 (vtable +0x5c, D).**
1. +0x1b8 `FUN_1008c440` refreshes the g_cam_* table 0x101657b8 (12-byte `{cvar*, float, name}` entries; the springs
   are `g_cam_[vh]spring/[vh]damp`, `g_cam_rspring/rdamp`) (D).
2. If `+0xa14 == 0` and the target (`+0xb34`) is not self: it copies the 0x60-byte camera block at `+0xa64`
   (`+0xa64` desired eye, `+0xa70` desired look-at, ...) and calls **`target->vtable+0x18c(block)`**. For the player
   that is `player_build_camera_target` duke!0x10078700 -> `entity_camera_from_track` 0x1008e8a0 ->
   `player+0xb20` (the current `game_track`) `->vtable+0x1dc` = **`game_track_place_camera` duke!0x10096c80** (D). The
   track computes the desired eye and look-at and writes them back into the block.
3. +0x1ac `FUN_1008cca0` blends track transitions. +0x1b0 **`actor_camera_spring_position` duke!0x1008d260** writes
   **`camera+0x18c`** (world position) via `spring_step` 0x100e8ab0 towards `+0xa64`. On a target change it snaps.
   +0x1b4 `actor_camera_spring_angles` 0x1008d3b0 writes `+0x1a8` (yaw/pitch/roll) from `+0xa70 - +0xa64` (D).

**Per frame (client proc, prio 99).** `client_proc_handler` client!0x10001000 = `wm_draw()` then
`client_render_views` (D).
- `wm_draw` prism3d!0x424080 (A): for each of the 8 wm slots (table 0x4a1c70, stride 0x18) it takes the alt object
  (+8) or else the object (+4), requires `obj+0x90 == 0`, and calls `obj->vtable+0x164(&A, &B)` =
  build_camera_matrices. It then calls `client_render_target(node, obj+0xe0 /*server*/, &A, &B)` (import slot
  0x4a1e44, written at 0x4251d9).
- `client_node_set_target` client!0x10002720: `node+0 = server`, **`node+4 = A` (camera->world)**,
  **`node+0x44 = B` (world->camera)** (D).
- `client_render_views` client!0x10002620 (A 0x1000263c-0x100026bc), in order: `rend_clear_stack()`;
  **`rend_camera_push(node+0x44, node+4)`**; `rend_get_camera(&pos)`; `snd_listener(&pos, rend_camera_inverse())`;
  `spawn_draw_server(server, &pos)`; `rend_camera_pop()`; `node+0 = 0` (import names from client's resolver
  0x10002290 et al., X).
- `actor_camera_build_camera_matrices` duke!0x1008c4c0 (D) builds these from scratch every frame:
  `eye = get_world_pos() (camera+0x18c) + shake`; rotate `(0, +0xb24*k, 0)`, then `+0x1a8 + shake angles`, then the
  roll. The shake is `camera_effects_sum` 0x100e8fc0, a sum over the game_globals+0xa0 list of `camera_quake` /
  `camera_effect_explosion`. It also applies FOV: `g_cam_near/far/fov`, then a per-world override `[obj+0xf4] != -1`
  from `FUN_100e7020(game_globals, server)`. The generic `object_e::build_camera_matrices` p3shared!0x100184f0 does the
  same with position + angles + a one-shot `add_camera_displacement`.

**Single write point for a pull-back?** The engine writes the eye in two places: `camera+0x18c`, written each tick by
the spring (0x1008d260), and the desired eye `+0xa64`, written by the track. Writing `+0x18c` would feed back into the
spring and drift. Writing `+0xa64` is overwritten every tick. The matrices are the only per-frame, stateless point.
The engine's own lever for distance is `player+0xb50` (section 4).

**Does swapping only the pushed matrices move everything consistently?** Detour `rend_camera_push(view, inv)` and
call the original with pointers to **our own** 64-byte copies. The push copies them, so `node` is never written.
Pull back by distance d (row-vector matrices, camera looks along +z, render.md section 3):
`inv' = inv` with `inv'[12..14] -= d * inv[8..10]` (the forward row), and `view' = view` with `view'[14] += d`.
- Follows automatically (same frame, after the push): the frustum (section 2), the PVS walk and one-sided mesh tests (they
  use the `pos` from `rend_get_camera`, which comes from the pushed inverse, prism3d!0x4207a0 D), `snd_listener`
  (listener position and orientation from the pushed inverse), every `rend_camera_matrix/inverse/get_camera` user
  inside the world pass (16/4/22 duke sites; billboards, flares), and our ESP if it captures after the original push.
- Does not follow: the HUD, which is an ortho pass (render.md section 1) and does not need to. Gameplay code that rebuilds
  the camera's own matrices: `FUN_100e8540` duke (callers 0x1003a9c0, 0x1007b730) calls
  `wm_get_object()->vtable+0x164` and intersects planes (purpose unknown, U3). It keeps using the unpulled camera.

## 2. Culling map (camera -> draw)

The draw chain (D): `spawn_draw_server(server,&pos)` prism3d!0x422290 -> `server->vtable+0x4c` =
**`server_draw` prism3d!0x40a340** (server vtable 0x4346d0). In order:
(a) `draw_portals(pos)` for list +0x3b4, which is hierarchical child drawing, not a test (p3shared!0x10018ba0 jumps to
+0x104);
(b) `pre_draw` for list +0x3c8;
(c) `draw(pos)` (+0xb4) for every live list-+0x3c8 entity of `parent_type(1)`, with no vis test at this level;
(d) `world->vtable+0x190(pos)` = **`map_vis_draw`**;
then `rend_cache_flush/props_flush`. The world is the duke class **"map"** (vtable 0x101460c8, get_classname
duke!0x100edf30 -> "map"). `map_load_resources` duke!0x100ee0b0 loads `mesh_gdt` (+0xa24), **`vtree_bsp` (+0xa1c)** and
`aabb_bsp` (+0xa2c, collision) and sets `server+0x3e0 = map` (D).

| # | Test | Where | Camera data used | When |
|---|---|---|---|---|
| F1 | frustum planes | `rend_build_frustum_planes` prism3d!0x41fd10: 6 planes from **current modelview x top of view stack x projection**, rebuilt lazily when `DAT_004a0a5c` is set. `rend_frustum_changed` 0x4200e0 sets it from push/view/pop, projection change, push_mult/pop/load/mult/identity (X) | pushed view + projection | lazily, at the first test after the push |
| F2 | `rend_sphere_visible(c,r)` 0x41ff80 | every plane `dot+r >= 0`, else culled (D). Users: duke 24 sites; map geometry (below); pmd/psm/skmod/p3shared/loaders/gl/dx8/client all resolve it (S names in every module) | F1 | during the world pass |
| F3 | `rend_sphere_visible_dist(c,r,max)` 0x420080 | culled if the distance past plane 4 (built from rows 3+2: the near-side plane, i.e. distance from the camera plane) > `max`, else F2 (D). Per-mesh `max` = mesh+4; the model `vis_radius` (e.g. 4096 in world.scr) is a probable user (U6) | pushed camera | same |
| F4 | `rend_box_visible(8 corners)` 0x41ffe0 | all 8 corners outside one plane -> culled (D); no duke callers (X) | F1 | same |
| P1 | PVS for world geometry | `map_draw` duke!0x100eebf0 -> `map_draw_geometry` 0x100ee930: descends the `vtree_bsp` with **`pos`** to a leaf; for each vtree node the leaf lists, `map_draw_vtree_node` 0x100ee820 (F2 on the node sphere, recursive), and each mesh gets F2 or F3 before `map_draw_mesh` (D). If there is no vtree, only meshes flagged `+0x58 & 4` are drawn | `pos` = pushed eye | inside the push |
| P2 | PVS for entities | `map_vis_insert` 0x100f0de0 (world +0x188, called through `srv_vis_insert/update`) drops each object's `vis_samplepoints` down the same vtree and links it into the sector lists (obj+0x930). `map_vis_draw` 0x100f0f90 descends with **`pos`** and draws (+0xb4) the objects in the leaf's sector list once per frame (stamp obj+0x948 vs map+0xacc) (D) | object position at insert time; `pos` at draw | insert: on move (tick); draw: inside the push |
| O1 | one-sided mesh test | `map_draw_mesh` 0x100ee5b0: meshes whose shader has flag 0x18/0x14 are skipped when `rend_get_camera()` is left/right of the mesh plane (D) | pushed eye | inside the push |
| D1 | far clip / fog | `g_cam_far` (8000) through `rend_projection_change` in the camera builders, per frame from `wm_draw`, but from a cache only the camera's world tick refreshes (freecam-pause.md section 6). A world's own far (`+0xF4 != -1`, e.g. 1600 on e06p02_a01) replaces g_cam_far in the builder: the freecam push raises it again (`nocull_far_override`, works in game); fog is visual only (`rend_register_fog_area`, no culling) | projection | per frame |
| E1 | per-entity | duke draw methods call F2/F3 and `pmd_draw_culled` (pmd.dll) | F1 | inside the push |

**If we pull the pushed camera back:** F1-F4, P1 and P2 (the draw side), O1 and E1 all follow, because they use the
pushed pair or `pos`. Nothing still uses the old eye. P2's insert side uses object positions, not the camera.
**The risk is P1/P2 themselves.** A pulled-back eye that crosses into a different vtree leaf (behind a wall, outside
the playable volume) gets that leaf's PVS. Geometry and entities near the player can then vanish, or everything can
vanish if the leaf is empty or solid. **Void ([[pvs.md]]): every playable map has one leaf.**

**Disabling culling (free camera option), ranked by safety:**
1. **Frustum F2/F3/F4: detour the three prism3d exports** (`rend_sphere_visible` 0x41ff80, `_dist` 0x420080,
   `rend_box_visible` 0x41ffe0; resolve by GetProcAddress) to return 1 while the option is on. This is code
   patching, like our existing export detours, and writes no engine data. Every module resolves these by name
   (every module has the three names, S), so one entry detour covers all callers. Note `_dist` calls `rend_sphere_visible`
   internally (0x4200d0). The return is `char` in AL (the callers test AL; the decompiler shows `ushort`).
   Side effect: two sphere callers store the result in an object byte (`+0xbbc` at duke!0x100af12b, and
   `map_sub_mark_visible`'s `+0x2e0`). While forced these bytes say "visible" for everything. The seen flag
   `actor+0xB71` is set the same way and wakes every drawn monster (offscreen-and-track.md).
   All 29 duke_base call sites of `rend_sphere_visible*` (through pfn slots 0x1018411c and 0x10184184) test or
   store AL only, and the originals leave EAX bits 8..31 undefined (engine.h:103-110) (U7).
2. **Far clip: raise `g_cam_far`** through the cvar API (render.md section 4 path; the camera builders reapply it each frame).
3. **Planes to zero (`DAT_004a0a58 = 0`):** would make F2/F4 pass, but it is an engine global written to a value the
   engine never writes, `_dist` still tests plane 4, and the lazy rebuild restores 6. Rejected.
4. **PVS P1+P2: moot, see [[pvs.md]]: every playable map has one sector and one leaf, so the walk always draws the
   whole level.** For a map with several leaves: no switch exists (no vis/cull cvar in `dumps/console-registry.txt`, X). It would need detours of
   `map_draw_geometry` 0x100ee930 and `map_vis_draw` 0x100f0f90 (duke_base, non-exported: signature scan) that walk
   **every** vtree node and sector instead of one leaf's list. The vtree layout (`vtree+4` table, leaf `ushort` lists) is read in [[pvs.md]] section 1 (U4). Most invasive, lowest rank. Cheaper, partial
   alternative: in free-cam mode, hand `map_*` the **player's** eye for the vis lookup only. That needs a second `pos`,
   which the client call chain does not carry (whether a `server_draw` pre-hook can swap `pos` is unknown).

## 3. Walls: the engine camera collides

`game_track_place_camera` duke!0x10096c80 (vtable +0x1dc of `game_track` 0x10138688 and `game_track_auto`
0x10138890, D/A):
- It computes the eye from the track anchor + offset, with the offset **scaled by `player+0xb50`** (FLD
  duke!0x10096f1e).
- Unless block byte `+0x28` is set and when `FUN_100978d0()` exceeds `DAT_10126cc0`, it calls
  **`phys_bound_test(server, from, to, &{16,16,16}, m, m)`** (call duke!0x1009705c; prism3d!0x40d750; half-extents
  0x41800000 = 16.0).
- The `tmatcher_t` rules are `accept_superset(0x100,0,true)` and `deny_superset(0x100, 0x8000 | 0x10000 | 0x20000 |
  0x400, false)`.
- On a hit it moves the eye to the hit point (`rec+0x14`) and shifts the look-at by the same delta. With `g_debug > 0`
  it logs "CAMERA_COLLIDING with '%s' (%d)" (S 0x10166a24).

A pull-back that wanted wall collision would sweep the same box from the original eye to the pulled eye with `phys_bound_test` (exported;
duke resolves it). Stop at the hit, minus a margin, then build the matrices. The panel does not do this. The argument order is (server, from, to,
halfext, matcher, matcher); which of the two pushed vec3s is `from` is unknown (U5), as is whether a NULL matcher
is accepted. Build a `tmatcher_t` with the p3shared exports otherwise. It runs on the main thread inside the push
detour, which is allowed (render.md section 1).

## 4. In-game zoom (impulse 109, `g_zoom_type`)

- The player input reader sets mask bit 0x100 when impulse 0x6d (109) is non-zero (duke!0x1007d34e-0x1007d366, A).
- `player_update_zoom` duke!0x1007e200 (D) reads `g_zoom_type` from the player cvar table (entry 0x19 at
  0x10163fac, name S 0x10164970). **0 = hold** (zoomed while the bit is held); otherwise **toggle** on the press edge
  (`+0xe04` = previous mask). The zoom state is kept in `+0xebd`.
- Result: `player+0xb50 = 0.5` when zoomed or `+0xe31` is set, else `player+0xb50 = player+0xed4` (1.0, set at
  duke!0x1007710e).
- `game_track_place_camera` multiplies the camera offset by `+0xb50` **before** its wall sweep. So zoom-in is **camera
  distance x0.5, no FOV change**. The UI only reads/writes the cvar (duke!0x100a6c80 / 0x100a7290).
- **Mirror:** a scale > 1 at the same place would be an engine-native zoom-out with the engine's wall collision
  included. It needs a write to `player+0xed4`, or to `+0xb50` after `player_update_zoom` each tick. Both are fields
  the engine writes itself, but it is still an engine-memory write. It only acts on track cameras (`player+0xb20`
  set), and it scales the anchor->eye offset, which is close to but not exactly the view direction. Ranked below the
  matrix swap.

## What ships

Zoom out = matrix swap in the `rend_camera_push` detour (section 1 formulas), limited to pushes whose caller is
client (return address `client.dll+0x2650`), always the full distance (walls: pass through; PVS gaps behind walls
are accepted). Disable culling for the free camera = detour F2/F3/F4 to return visible and raise `g_cam_far`. PVS
(P1/P2) needs nothing: one leaf per map ([[pvs.md]]).

Checks that settled the open points:
- **U1** The pulled-back pair renders correctly: the listener pans correctly, the ESP crosses stay on the enemies,
  sound is fine, and it applies in the main menu too.
- **U2** PVS breakage is moot: one leaf per map, no eye can pick a wrong leaf ([[pvs.md]]).
- **U4** The vtree resource layout (sector count, `vtree+4` table size) was read from the loader callback
  `LAB_100eddf0`; all 101 maps were parsed offline ([[pvs.md]] section 1).
- **U7** See the side effect in section 2.

## Open questions

- **U3** The purpose of `FUN_100e8540` (camera-plane math, callers 0x1003a9c0, 0x1007b730), and whether aiming or
  screen-bounds logic misbehaves with a pulled camera (aim at d = 1000).
- **U5** `phys_bound_test` from/to order, whether a NULL matcher is accepted, and the result record (`+0x14` = hit
  point). `FUN_00412480`/`FUN_0040eb00`, or one logged call from `game_track_place_camera` with g_debug 1, would
  settle it.
- **U6** Which `parent_type(1)` entities `server_draw` draws without PVS (section 2c), and whether `vis_radius` feeds
  `rend_sphere_visible_dist`. Logging the classnames in a `server_draw` pre-hook once would show it.
