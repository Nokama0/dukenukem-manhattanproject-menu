# Free movement and first-person mode

The engine already ships both features, switched off. Causes and fixes found in play: sections 6-9. Two archived
cvars, **`g_fps`** and **`g_rpg`** (both default "0", listed in `config.cfg`), put the player into hidden states 17 and 16, and `player_tick`
re-checks them every tick [D duke!0x1007ae50, 0x1007b089-0x1007b10d].
- **`g_fps 1` = first person plus free movement.**
  - The view entity becomes Duke himself (eye = position + 40 up, angles = `+0x1A8`).
  - His model is not drawn.
  - The track constraint is off (`+0xB1C = 0` every tick).
  - Up/down keys walk forward/back along the body heading, left/right strafe.
  - Impulses 201-204 turn and pitch him (analog).
  - Gravity, collision, jump and fire all stay on.
- **`g_rpg <h>` = top-down free movement.** A camera looks straight down from height h. Duke moves along the world
  X/Z axes and faces his movement. His model is drawn.

First person sets `g_fps` through the cvar API (`cvar_set`). That is a console action, so no engine memory is
written and nothing is hooked for the switch itself. Mouse look needs only a yaw write that the state itself makes
every tick. The risks, handled in sections 3 and 6-9:
- the state overrides every scripted state (death, jetpack, ropes, cutscene views);
- leaving the mode snaps Duke onto a track;
- the cvar persists into `config.cfg`.

The "move anywhere" lever outside these states is the byte `+0xB1C`. All three track layers are gated on it. But the
stock walk states make no sideways motion without a track, so clearing it alone gives no free walking.

Tags: **[D]** decompile, **[A]** disassembly, **[X]** xref/byte or instruction scan, **[S]** string/data read,
**[C]** game config file. Base: duke_base 0x10000000 (duke!), prism3d 0x400000. All work was done through the Ghidra
MCP.

Created and renamed in Ghidra (saved):
- `player_tick` 0x1007ae50, `player_draw` 0x1007d4d0, `actor_snap_to_track` 0x1008e310;
- `playerstate_fps_think` 0x1007fc20, `playerstate_rpg_think` 0x10087150, `playerstate_free_{enter,exit,pretick,gravity}`
  0x10087120/0x1007fb50/0x1007fba0/0x1007fbe0;
- `player_build_camera_matrices` 0x10077b20, `actor_constrain_move_to_track` 0x1008e360, `actor_move_delta` 0x10090f20;
- `player_refresh_cvars` 0x1007c9e0, `player_cvar_value` 0x100775b0, `player_track_move_vector` 0x10087fd0.

Correction to input-console.md: `g_fps` is not an fps counter. It selects the first-person state (§2).

## 1. How Duke is held to the track (move anywhere)

### 1a. The player state machine [D 0x100763c0, S vtables]

- `player+0xB80[18]` holds the state objects, and `+0xB7C` is the current index. `player_set_state` 0x100775d0 calls the old
  state's slot +0x08 (exit), stores the new index, then calls the new state's slot +0x04 (enter).
- `player_tick` calls the current state's slot +0x0C each tick (pre-tick, which calls +0x40 think and +0x44).
- Vtables, stride 0x48, with the +0x40 think of each:

| # | vtable | think (+0x40) | known role |
|---|---|---|---|
| 0 | 0x10136a60 | 0x10087b50 | death/none (set by `FUN_10079e70`, `FUN_10077720`) |
| 3 | 0x10136978 | 0x100855d0 `playerstate_move_update` | normal walk/run/jump (ctor, teleport) |
| 4 | 0x10136930 | 0x1007fa20 | entered from a net message that sets `+0xB20` by id and clears `+0xB1C` (0x1007ece5-0x1007ed19) |
| 8 | 0x10136810 | 0x10080130 | idle-like think (mouse-aim §7) |
| 12 | 0x101366f0 | 0x10081d20 | jetpack (`FUN_1007d980` sets 0xC while fuel > 0, 0x1007dae1 [A]) |
| 13 | 0x101366a8 | 0x10081120 | attached to the object `+0xBD8` (exit 0x10080af0 detaches it). Enter clears `+0xB1C` (0x100809e4) |
| **16** | 0x101365d0 | **0x10087150 `playerstate_rpg_think`** | `g_rpg` |
| **17** | 0x10136588 | **0x1007fc20 `playerstate_fps_think`** | `g_fps` |

States 16 and 17 share slots +0x04..+0x3C and +0x44, and differ only in the think.

### 1b. Three track layers, all gated on `+0xB1C` [D/A]

1. **Input → direction.** `player_track_move_vector` 0x10087fd0 builds the walk vector from mask bits 1/2
   (left/right). It does so **only while `+0xB1C != 0`** and along `track->vt+0x1C8` (`game_track_get_dir`). With the
   byte clear, left/right produce no motion in the normal states.
2. **Physics delta → track.** `phys_move_object` prism!0x411a09 calls the object's **vt+0x14C** on the move delta
   [A]. For actors that is `actor_constrain_move_to_track` 0x1008e360 (player vtable slot 0x10136bf4 [X]). If
   `+0xB1C && +0xB20`, it calls `track->vt+0x1C4` (`game_track_adjust_offset`), which bends the XZ delta along the
   polyline, with up to 5 wall-sweep retries.
3. **Snap and re-pick.** `actor_track_update_if_moved` 0x1008dfa0 (vt+0x230, called from `FUN_1008d920` at the start
   of `player_tick`) returns at once when `+0xB1C == 0`. Otherwise, after a move > 0.01 or a turn > 1, or when
   `+0xB20 == 0`, it runs:
   - vt+0x210 `actor_pick_track`;
   - vt+0x214 = **`actor_snap_to_track` 0x1008e310**, which calls `track->vt+0x1D0` only if `+0xB20 && +0xB1C` [D].
   The yaw re-snap `FUN_10096410` lives inside `vt+0x1D0` (mouse-aim §9).

So `+0xB1C` is the single switch. The engine writes it to (byte stores, [A]):
- 0 in state 4 (net, 0x1007ed12), the state 13 enter (0x100809e4), the ladder enter (0x10083a12), the state 7
  enter `playerstate7_enter` (0x10087bb9) and the state 16/17 pre-tick (every tick);
- 1 in the state 16/17 exit (0x1007fb50), the ladder exit (0x10083e22), at 0x1007f79e, 0x100852bf and 0x10087bf4,
  and in `player_tick` while `buf+0x2a774 > 0` and state ≠ 4 ([D] `player_tick`).
- Read by `actor_track_update_if_moved`, `actor_constrain_move_to_track`, `actor_pick_track` and
  `player_track_move_vector` (left/right zeroed without it, 0x1008803e/0x10088059), which the walk, idle, air and
  climb thinks call. `playerstate_move_touch_scan` 0x10086120 itself has no `+0xB1C` access (its callees were not
  checked).

### 1c. Engine-supported off-track modes

- **State 17 (`g_fps`), `playerstate_fps_think`** [A, read whole]:
  - Local move vector `v = ((imp102 - imp101), 0, (imp103 - imp104)) · 300 · dt`. That is right-left strafe, up-down
    forward/back, and +z local = forward.
  - `mult_vector_matrix(+0xF4)` turns it into world space through the body matrix. Pitch is included, so looking
    down slows walking.
  - `+0x1A8 += (imp204 - imp203)·50·dt` (pitch, **no clamp**). `+0x1AC += (imp201 - imp202)·50·dt` (yaw).
    Then `set_transforms_dirty`.
  - If |v|² > 0.001: `vt+0x204` = `actor_move_delta` 0x10090f20 (ground-slope projection, then vt+0x108 physics)
    and `player_request_state(3)` (run animation).
  - Weapon trigger from bits 0x20/0x80. Jump on the bit-4 edge when grounded (`FUN_10088210`).
  - No kick, crouch, air boost, ladder or rope.
- **State 16 (`g_rpg`), `playerstate_rpg_think`** [D]: bits 8/0x10/1/2 → world (0,0,+1)/(0,0,-1)/(-1,0,0)/(+1,0,0).
  `cartes_2_ypr` writes the facing into +0x1A8, and the move is the same 300·dt `vt+0x204`. Fire as above, no jump.
- **Shared slots:**
  - enter 0x10087120 zeroes `+0xE34..+0xE48`;
  - **pre-tick 0x1007fba0** runs think, then `vt+0x1EC(1.0)` (`actor_set_height_ratio`: stand), then +0x44, then
    `+0xB1C = 0`, then **`set_view_entity(player)`**;
  - +0x10 0x1007fbe0 applies gravity (`FUN_100e7e00`: `g_gravity` × `+0x2D8` × world scale →
    `object_e::force_insert`);
  - **exit 0x1007fb50**: `spawn_search(Y=0x200)` → `set_view_entity(camera)`, `+0xB1C = 1`.
- **Selection:** `player_refresh_cvars` 0x1007c9e0 (first call in `player_tick`, xref 0x1007ae73) re-reads the
  27-entry table at 0x10163e80 (`{cvar*, float, name*}`; entry 0x16 `g_p_hover`, 0x17 `g_rpg`, 0x18 `g_fps`) when
  the cvar's modified byte +0x70 is set [D]. Then, in `player_tick`:
  - `g_rpg > 0` and state ≠ 16 → state 16;
  - else `g_fps > 0` and state ≠ 17 → state 17;
  - both 0 and state ∈ {16, 17} → state 3 [D/A 0x1007b089-0x1007b10d].
  - There is **no gate** on death, cutscene or current state.
- Leads not followed: `g_p_hover` (entry 0x16) is read in `playerstate_move_update` 0x10085787 and in 0x100835d5. It is a
  movement modifier of the normal state, not an off-track mode.

### 1d. What breaks when the player is off the track

- **Camera:** the actor_camera follows `game_track_place_camera` of the stale `+0xB20` (the pick is a no-op). It is
  invisible in g_fps (the view is the player) and wrong in a third-person free mode, so our own camera must replace
  it there.
- **Return to normal:** the 16/17 exit sets `+0xB1C = 1`. The next move runs the pick, then the **snap writes x/z onto
  a track line**.
  - If no area volume covers Duke, the pick keeps the **old** `+0xB20` (offscreen-and-track B3, pass 2), so he can be
    snapped back to a far-away track.
  - y is kept, so he may fall, or land inside geometry where the track runs on another floor.
  - Mitigation: on leaving, call `player_teleport(vt+0x218)` to his own position. It clears `+0xB20`, re-picks by
    area, then the nearest track, and retests areas (freecam-teleport §1).
- **Triggers:** `player_retest_areas(this, 2)` runs every tick in `player_tick`, whatever the state [D]. So doors,
  pickups, secrets, area_teleport and changemap still fire.
- **AI:**
  - Monsters keep their own tracks.
  - Wake-up needs the seen flag `+0xB71`, which is set only when drawn in view (offscreen-and-track A4). In first
    person, monsters behind Duke never wake (ai-offtrack.md lifts this).
  - Monster shots aim at his position: unaffected by reading. Whether any monster logic projects the *player* onto a
    track is open (F6).
- **Collision:** the same physics path (`actor_move_delta` → vt+0x108 → `phys_move_object`) with the track bend
  skipped. Walls still block.

### 1e. 2D input → 3D

g_fps does it natively: up/down = forward/back along the yaw (and pitch), left/right = strafe. For a third-person free
mode there are two ways:
- g_rpg's fixed world axes, possibly remapped by injecting mask bits from the camera yaw (8 directions);
- a detour of `playerstate_rpg_think` that builds `v` from the camera yaw (analog), keeping the engine's `vt+0x204`
  call.

## 2. First-person

- **View:** `player_build_camera_matrices` 0x10077b20 (player vt+0x164, slot 0x10136c0c [S]) [D]:
  - state 16: eye = pos + (0, `g_rpg`, 0), angles (-90, 0, 0), looking straight down;
  - otherwise: eye = `vt+0x160()` + (0, **40**, 0), angles = **`+0x1A8`**;
  - FOV, near and far from the `g_cam_*` cvars (`g_cam_near` 16), with the per-world override.
  - The client push uses it because the pre-tick makes the player wm slot 0's view object every tick.
  - Our `rend_camera_push` capture (view.cpp) keeps working (same client push). Zoom-out would pull the eye back behind
    a body that is not drawn.
- **Own model hidden:** `player_draw` 0x1007d4d0 (vt+0xB4, slot 0x10136b5c [S]) returns at once when `+0xB7C == 0x11`
  (`CMP [ESI+0xB7C],0x11` at 0x1007d4e3 [A]). That is the only state-17 test outside `player_tick` and the camera
  builder (instruction scan [X]). There is no near-clip trick and no flag to write.
- **Weapon:** Manhattan Project has no view model. The held weapon is a child actor drawn by `actor_draw` 0x10090260 at its parent's
  bone (`actor_get_bone_world_pos`) when its `+0xAF0` is set and its sphere passes the frustum [D]. Whether it floats
  in view at the hand while the body is not drawn, and whether the bones still animate, is open (F2).
- **Mouse look:**
  - Engine route: `bind_imp mouseleft "turnleft"` (and the other three), as input-console §3. But the state multiplies
    the impulse by 50·dt, so the turn per mickey falls with frame rate, and the impulse max may clamp it (F3).
  - Better: our `GetDeviceState` deltas (mouse-aim §10). Write **yaw** `+0x1AC += dx·k` before the tick; the state
    itself writes it every tick.
  - Keep pitch out of the body: put pitch only into our substituted view matrix (eye from the engine's push, yaw from
    +0x1AC, our clamped pitch).
  - This avoids the unclamped body tilt, the slowed walking (§1c), and the head-bone look-up branch of
    `playerstate_get_fire_dir` (aim.md §1, head forward y > 0.5).
- **Firing:** `mouse1` = attack already reaches mask bit 0x20, which state 17 handles. Fire dir = `ypr_2_cartes(+0x1A8)`
  (shared slot +0x28 0x10088400), and the origin is the eye bone, not pos + 40. Use the existing `weap_get_fire_dir`
  detour: `*out = normalize(P - origin)`, P = the first world hit of the view's centre ray (`phys_ray_test`,
  world-only matcher). Draw a centre crosshair on our canvas.
- **Movement:** native (§1c). Jump works; kick, crouch, air boost and jetpack do not exist in state 17.

## 3. Interactions

- **Culling/PVS:** one leaf per map (pvs.md). The frustum follows the pushed view. Levels are built for a side view,
  so one-sided meshes (O1), backdrops and cut-away walls may show holes from inside (look, F4).
- **Cutscenes / mission complete:** they switch the view with `set_view_entity` (freecam-teleport §4). The state 16/17
  pre-tick re-asserts `set_view_entity(player)` **every tick**, so a cutscene would play from Duke's eyes for as long as
  the player ticks. Drop the cvar while a letterbox/cutscene window shows (the mouse-aim §9 gate) or when
  `wm_get_object(0)` is a camera-class entity that is not the actor_camera.
- **Scripted states:** the `player_tick` switch overrides every other state in the same tick. Affected:
  - death (state 0 from `FUN_10079e70`);
  - jetpack 12, attached 13, security-camera monitor (`FUN_10073e90`) and babe rescue (`babe_activate` 0x10060b4b);
  - teleport's state 3.
  A death in g_fps may leave Duke dead but walking (F5); clearing the cvar when health ≤ 0 or
  `player_is_dead_or_locked` would be the remedy. The panel pauses first person around scripted sequences (section 6
  FP1, section 7).
- **Save/load and level changes:**
  - A new level builds a new player in state 3, and the first tick re-enters 16/17 from the cvar. `player_teleport`
    and changemap set state 3 and return the same way.
  - The **cvars are archived** (`uset g_fps "0"`, `uset g_rpg "0"` in `duke/base/config.cfg` [C]). If left on at quit,
    the next session starts in that mode. The panel restores 0 on toggle-off and at quit
    (`firstperson_restore_for_quit`, before config.cfg is written).
  - Checkpoint saves were not read.
- **Track-dependent features of ours:** the teleport marker (in normal play it snaps to any track, freecam-teleport.md
  section 5) and the facing/mouse-aim plane use `+0xB20`. They are
  meaningless in g_fps. Mouse aim's yaw write and torso bend must stand down while the state is 16/17.

## 4. Design as built

1. **First person** (no hooks for the switch): `cvar_set(g_fps, "1"/"0")`; mouse yaw from our `GetDeviceState` deltas
   written to `+0x1AC` on the main thread; pitch only in our view substitution (clamped ±80°); a centre crosshair,
   and fire dir to the centre ray's hit through the existing detour; the cvar is cleared on level change or quit and
   when the toggle goes off (scripted sequences: sections 6 and 7); on leaving,
   `player_teleport(own pos, own angles)` gives a clean track re-pick.
2. **Move anywhere** is not `g_rpg`: it keeps the game's own camera and controls and stops the track holding Duke
   (section 5). `g_rpg` with a large height plus a chase camera and camera-relative input (a detour of
   `playerstate_rpg_think` 0x10087150, keeping its `vt+0x204` call) was the alternative.
3. Jetpack, ropes and death are not let through: that would need a detour of `player_set_state` 0x100775d0 or of the
   `player_tick` switch, so that a scripted state suspends the mode instead of being overridden (section 7 does this
   for the Interact key's states).

## 5. Move anywhere: the lever, the camera and the keys

The lever and depth on Up/Down are built (`moveanywhere.cpp`): Up/Down replace look-up and crouch while the switch is
on, and the camera follows Duke. All read through the Ghidra MCP.
- **Constrain** `actor_constrain_move_to_track` 0x1008e360: thiscall(delta), RET 4 (0x1008e6e6) [A]; entry
  `sub esp,0x60; push ebx; push esi` [A]; call site prism!0x411a03-0x411a09 (`push ecx; mov ecx,ebx; call
  [eax+0x14c]`) [A]. Shared by every actor vtable (61 data xrefs [X]), so the detour gates on the local player.
  Skipping it is the engine's own +0xB1C = 0 path.
- **Snap** `actor_snap_to_track` 0x1008e310: ECX only, plain RET [A]; reached through vt+0x214 from
  `actor_track_update_if_moved` 0x1008dfa0 [D]. Its target `game_track_snap_actor` 0x10096070 (track vt+0x1D0,
  vtable 0x10138688 [S]) finds the nearest span, writes **x/z only** (+0x18C, +0x194), re-snaps the facing
  (FUN_10096410) and stores the span in **+0xB58/+0xB5C** [D]. Player vtable slots +0x14C and +0x214 hold exactly
  these two entries (0x10136bf4, 0x10136cbc [S]).
- **Why the snap runs and only its x/z is undone:** `game_track_get_dir` 0x100956a0 (vt+0x1C8), which gives
  `player_track_move_vector` its left/right direction, reads the span from +0xB58/+0xB5C, not the position [D]. A
  skipped snap would freeze the walk direction and the camera's span.
- **Camera (by reading):** `game_track_place_camera` 0x10096c80 starts from Duke's own position (vt+0x160) and adds
  the track offset walked from +0xB58/+0xB5C [D]. So it should follow him off the line at the same offset; F10
  measures it (`mv:` lines: camera-Duke distance and Duke's screen point beside the undone snap distance).
- **Keys:** in `playerstate_move_update` 0x100855d0 [D] up (mask 8, without 0x10) requests the look-up layer (PAS 2,
  mouse-aim.md) and down (mask 0x10) is crouch, which also stops the walk (the move needs `(mask & 0x10) == 0`).
  A walk also needs `+0xB20 != 0`.
- **Depth (built):** `player_read_input_mask` 0x1007d250 (player vt+0x154, RET 4) sets mask bit 8 from impulse 0x67
  and 0x10 from 0x68 through `inp_impulse_value` (prism!0x406a30, cdecl, ST0; import slot [0x10183e94], the
  GetProcAddress at duke!0x100dfe1a) at 0x1007d3ef/0x1007d40d [A]. The mask then goes out through `net_send`, so
  +0xE00 is not written there. Our `inp_impulse_value` detour returns 0 to exactly those two return addresses while
  we act, so nothing downstream sees up or down (look-up, crouch, the idle ledge climb at 0x100801e3, ladders, and the
  four-way mover FUN_1008a700, +-320 vertical on mask 8/0x10 and +-200 on 1/2, likely the jetpack). Switching off
  gives the keys back at the next read. The reader returns before both reads while the world takes no input
  (0x1007d281/0x1007d28a), so our held keys are cleared on the frame path then.
- `player_track_move_vector` 0x10087fd0: thiscall (ECX the state, unused) with (out, player, bool, angles), RET 0x10,
  EAX = out [A]; 7 callers (idle 0x100802a2, move 0x100856b3/0x1008580f, jetpack, air states [X]). Each multiplies
  the unit result by the walk speed (`player_cvar_value(0)`) and turns Duke to face it (cartes_2_ypr into +0x1A8);
  idle requests running from it alone (0x10080384) [A/D]. Our detour adds `away` (the camera forward without the
  track's direction) times the depth axis and renormalises, so a diagonal walks at the walk speed.
- The snap's facing re-snap (FUN_10096410) turns the forward by 10 degrees before projecting it onto the track plane,
  so a depth-facing Duke gets a definite side back at the next tick's start [D]; the state then faces his move
  again.

## 6. First fixes in play: causes and fixes

Logs: `nukepanel.log` and `.prev` [L]. Read through the Ghidra MCP; functions created or renamed
there: `playerstate_move_touch_scan` 0x10086120, `area_ladder_grab_test` 0x1009a460, `playerstate_ladder_enter`
0x10083a00, `playerstate_hang_think` 0x10080130, `playerstate_pullup_pretick` 0x10080630, `playerstate7_enter`
0x10087bb0.

- **FP1 level-start hang: pause and resume.** [L] Both hangs (sid 3 -> 4 and sid 8 -> 10 in `.prev`) have the
  same shape: the new player is first seen already in state 17 (g_fps stayed 1 through the load), the next frame the
  input entity (`wm_get_input(0)`) is no longer a player, and the client pushes no camera from then on
  (`view: pushes/frame=0` for minutes). The level that loaded fine (sid 1 -> 3) kept the input on the player.
  The engine hands the input elsewhere through the `camera <entity>` script command (FUN_100e48b0: `wm_input(0, ent)`,
  `set_view_entity`), while the 16/17 pre-tick re-asserts `set_view_entity(player)` every tick (section 1c).
  Fix: `fp_pause_step` drops g_fps whenever the input entity is not the player, so the next level's player starts in
  state 3, and puts it back once the world has taken input (`ma_face_world_live`) for 1 s. Log: `fp: paused for no
  player`, `fp: resumed after N ms`. A pause in the middle of a
  scripted `camera` runs the 17 exit (`set_view_entity(actor camera)`), which may show that cutscene from the game
  camera.
- **FP2/MA1 ladders.** The ladder is **states 5 (walk to the grab point) and 6 (climb)**, section 7: state 3's extra slot +0x48, `playerstate_move_touch_scan`,
  finds an `area_ladder` (user bit 0x40) whose volume holds Duke's head (pos + `+0x2BC`) while Up (mask 8) is held
  (`area_ladder_grab_test`), and sends local net message 10, whose handler (jump table 0x1007ef8c, case 10 at
  0x1007ede1) calls `player_set_state(5)` [D/A]. State 17 never runs that scan; Move anywhere ate Up. Hanging is
  state 8 (message 5), where Up pulls up (state 9) and Down drops. Fix: the entity walk tests Duke's head against
  every `area_ladder` volume (`ent_point_in_obb`, `ents: Duke's head entered/left a ladder volume`).
  - First person pauses while the head is in a ladder volume with Up held, or Duke is in state 5 or 6, and resumes
    300 ms after (Up added with the track guard, section 8 G2-2).
  - Move anywhere gives Up/Down back to the game in states 5, 6 and 8 and in a ladder volume (`mv_keys_to_game`).
  - Staying in first person on a ladder (section 7, "Ladders in the first-person view"): states 5/6 run
    under our own eye-height view with the body hidden.
- **FP3 double jump: no boost in state 17** (section 7 adds one). `playerstate_fps_think` 0x1007fc20 [D, read whole] jumps only on
  the bit-4 edge while grounded (`+0xB00`, `FUN_10088210`); it has no airborne branch. The boost lives in
  `playerstate_move_update`'s air branch (double-jump.md section 1), which state 17 replaces. The jumpfix restore is
  not involved.
- **FP4 falling forever: Rescue falls (View > MOUSE LOOK, default on, `rescue.h`).** [L] The death area does
  fire in first person (`PS 3 -> 22` = `PS_death_pit` at y -940.6), but the death is undone (`PS 22 -> 3`) and Duke
  keeps falling: to y -5009 and -7120 in `.prev`. The level's bounds are not read: area volumes cover scripted zones
  only. The rule: a spot is safe after 0.5 s grounded; a fall of 4 s and 2000 units under both the safe spot and the
  fall's top (terminal speed about 611 u/s [L jf], so about 2400 units) calls `player_teleport` to the safe spot.
  States 3/16/17 only, never during PS 20-22. Log: `rescue: fell ...`.
- **MA2 camera snaps. Cause by reading; instrument only.** `game_track_place_camera`
  0x10096c80 [D] turns the camera offset (`+0xB2C..+0xB34`) by Duke's yaw `+0x1AC` (FUN_10067a40) before walking it
  along the track. A depth step makes the caller face the move (section 5), a 90-degree yaw change, and the next
  tick's snap turns him back to a side: the offset swings with each flip. The `mv: camera snap #n` lines (eye move
  against Duke's move, yaw change, keys) and `snaps=/withYawFlip=` on the 5 s line confirm or kill it. The fix if confirmed (not built): hold Duke's yaw on the track side while the move is pure depth.
- **Crouch key: the engine resets it every tick** (section 7 builds it with a detour). Crouch is `actor_set_height_ratio` (vt+0x1EC) with 0.5
  in `playerstate_move_update` (0x100859c6-0x100859d7); the 16/17 pre-tick calls vt+0x1EC(1.0) after its think every
  tick (0x1007fbb7). A ratio we set between ticks would hold only for the next think's own move, then stand again.
  Alternatives: a view-only crouch (our substituted eye lowered, collision unchanged), or a detour of the pre-tick's
  vt+0x1EC call (one call site, engine memory untouched) that passes 0.5 while the key is held.
- **Interact key: not possible by an impulse in first person** (section 7 builds it). The engine's "use" is Up itself: `cmd_use` 0x100e5da0 sets `+0xEBE`,
  which `player_read_input_mask` turns into one tick of mask 8 only in state 3 (0x1007d453-0x1007d46b). Doors,
  switches and ladders are taken by state 3's touch scan, which state 17 never runs, so pressing the up impulse in
  `rend_begin_frame` does nothing there. Alternatives: an Interact key for Move anywhere (one frame of the up impulse
  let through our depth detour), or, in first person, calling state 3's touch scan ourselves with mask 8.

## 7. First person extras

Built in `fpmoves.{h,cpp}`. The Crouch hook is a detour of the callee filtered by return address, not a patch of
the call site: the call is `call [edx+0x1EC]`, an indirect vtable call that a 5-byte jump cannot replace in place,
and the callee's entry is a clean 7-byte prologue. Bind rows appended to bind.h (`BIND_ROW_FP_CROUCH`,
`BIND_ROW_INTERACT`); like the camera keys they refuse the fixed freecam movement keys.
- **Crouch key (default C).** Ctrl is refused: it is a fixed freecam movement key, and the game binds lctrl to
  jump. A detour of `actor_set_height_ratio` 0x1008fc90 (player vt+0x1EC, thiscall(ratio), RET 4; entry
  `fld [esp+4]; sub esp,0x30`, 7-byte prologue) passes 0.5 instead of 1.0 only for our player, only from the 16/17
  pre-tick's stand call (return address found by its bytes `push 1.0; mov ecx,esi; call [edx+0x1EC]` in state 17's
  pre-tick, 0x1007fbb0-0x1007fbbd), only while the key is held in first person. The function's own sweep
  (0x1008fe7a) refuses to grow into a ceiling, so letting go under a low roof keeps him down. Our eye drops by
  `(1 - +0xAFC) * (40 + half.y)`, the engine's actual ratio. The eye eases there over about 150 ms (`fp_ease_drop`); the collision ratio stays instant, as the walk state's own crouch is. Log: `fp: crouch armed`, `fp: crouch held/released`.

- **Move anywhere Interact key (default E).** A use queued just before Up/Down go back to the game is dropped. The engine's use is Up itself (cmd_use 0x100e5da0 -> one tick of
  mask 8 in state 3), taken by state 3's touch scan. A press makes our `inp_impulse_value` detour return 1.0 to the
  mask reader's up read once (`mv_up_read`), so the scan sees one Up edge; depth ignores it. Log: `mv: interact`.

- **First person Interact key (default E).** Measured callable outside state 3 [D/A]: the touch scan
  `playerstate_move_touch_scan` 0x10086120 is thiscall(state, player), RET 4 (0x10086969); ECX is only passed on to
  FUN_10086970, which never reads it; it reads player +0xE00/+0xE04/+0xB00/+0x18C, the world time and a
  `spawn_search`, and acts through net messages and the touched entities' own calls. It also calls FUN_10078170 for
  every touched entity whatever the masks; with `g_hint_arrows` on, that spawns hint-arrow objects (FUN_100ce0a0)
  into player +0xEB0 from the frame path, as state 3 does every tick (not skippable without another
  hook). On a press we run it once
  from the frame path with an Up edge in the masks (`fp_use_masks`), then put both masks back. The state the message
  starts (use 7, ladder 5, grab 8, 11) would be undone the same tick by player_tick's g_fps switch, so a detour of
  `player_set_state` 0x100775d0 (found as the target of that switch's call 0x1007b10d, unique by bytes) skips the
  switch's call for 10 s after the press while Duke is in 5/7/8/9/10/11 (`fp_hold_scripted`). Back in state 3 the
  switch returns him to 17 by itself. While the state plays the view stays ours (the view hold, below). Log:
  `fp: interact #n`, `fp: interact: state N plays`. One scan per E press: the key's edge is tracked while first
  person is inactive too, so E held through the use does not fire again. Open: whether the
  local net message is handled in the same frame or the next tick (either is covered); whether the 17 exit's
  `+0xB1C = 1` snaps him to a track after the use; whether hint arrows spawned by our extra scan go stale.

- **First person double jump.** State 17 has no air branch, so the walk state's rule is mirrored after each tick
  (`fpj_press`, `fpj_velocity`): an air press (jump bit 4's edge while `+0xB00` is 0) more than 0.2 s after the ground
  jump's stamp E10, armed (E14 < E10), not more than 50 below the jump start E70, gets the engine's two velocity adds
  through the player's own vt+0x200 (0x10078a50): `(0, -vy, 0)`, then `(0, v, 0)`. `v` is g_p_boost, or on the sweet
  path (|vy| <= g_p_boost_sweet, or E64 > 0 while falling) `sqrt(|J^2 + B^2 - 2 g dy|)` as at 0x10085c58-0x10085cf9.
  The engine's g is `g_gravity` times the map's scale (FUN_100e7e00); we measure it from consecutive airborne frames
  (1800 u/s^2 default). E14 is stamped on every air press, pass or fail, as the engine does; a
  press within 0.05 s of E10 is the ground jump's own. Log: `fp: air jump BOOST/REFUSED(gateN)`.
  The upward speed is scaled by **Double jump strength** (First person sub-row, 100-150 %, default 120 %: first
  person cannot grab the ledges a vanilla short jump reaches); the ground jump is unchanged.
  Open: vt+0x200 is `force_insert` of mass*v*1000 over 0.001 s (p3shared, not in the project).
  Whether a force inserted between ticks applies in full on the next step is unread. So is whether the engine's sweet
  test at 0x10085c04, which reads vy after its cancel force, sees the old vy: if the force were immediate the engine
  would always take the sweet path and our `boost` branch would differ.

- **Ladders in the first-person view.** Measured [D]: the ladder is two states. State 5
  (`playerstate_ladder_pretick` 0x10083b10) walks Duke to the ladder's grab point (+0xBEC), facing it, then calls
  `player_set_state(6)`. State 6 climbs: `playerstate_climb_think` 0x100842e0 moves along the ladder on Up/Down
  (mask 8/0x10) at half walk speed and turns with left/right; `playerstate_climb_pretick` 0x10083f40 writes
  +0x1A8/+0x1AC from the ladder's own mode every tick and leaves to state 3 at the foot (grounded + Down) or when the
  ladder (+0xBCC) no longer touches him. Section 6 reads states 5/6 everywhere (`fp_ladder_state`,
  `mv_keys_to_game`). While the ladder pause or the interact hold-off holds (`fp_view_hold`), the first push is
  still ours: eye at his position + 40 (17's eye), yaw and pitch view-only (state 6 writes his yaw itself), the
  crosshair, and the body skipped by a pass-through detour of `player_draw` 0x1007d4d0 (the same early return state
  17 takes). Mouse aim stands down. Back in 17, his yaw is turned to face the view (`fp_engine_yaw_for_view`).
  After its reason ends the hold bridges until state 17 is
  actually entered (`fp_view_bridge`, at most 1 s, gated on the switch: section 8, G2-1), so no
  game-camera frame shows; the held pitch carries into 17;
  a new player object drops the hold; the interact hold-off also covers state 6. Log: `fp: view held`,
  `fp: view hold ends (N frames this hold)`, `fp: body hiding armed`. Open: whether the held weapon (a
  child actor) still draws while the body is skipped (F2).

- **First person through game sequences.** [D/L]: a switch's use animation is state
  7 (net message 11 from the touch scan's FUN_1007f2f0), already held in our view by the interact hold-off. Level
  run-outs/run-ins and intros are `camera <entity>` script sequences (FUN_100e48b0: `wm_input(0, ent)` and
  `set_view_entity`), during which the input entity is not the player: the FP1 pause (g_fps 0) lets the engine
  play them. Built (`fp_scene_hold`): during that pause our view stays at the player, found each frame by
  `spawn_search("s=%d?t=%d?Y=%d", sid, 2, 0x100)` (get_local_player's own query; `u=` is the entity id) and checked
  (player bit, +0x90 clear, the player vtable); eye at his position + 40, facing where the engine turns him plus the
  mouse's offset, body hidden. The search runs only when the input entity is a live camera (+0x4C & 0x200, +0x90
  clear) on the last live player's server and prism3d's own `spawn_get_server` (0x422180, ordinal 364) still finds
  that server: a sid with no server faults `spawn_search_get` (`fp_scene_search_ok`). So an
  intro at a new level's start, before its player was ever live, keeps the game camera. When the player is back,
  a running hold waits view-only through the 1 s resume (`fp_wait_hold`) and hands its yaw to Duke in 17. The push
  substitution re-checks any held player with one guarded read, since a level change may free it.
  Stays the game's: the load screen (no camera push to replace), menus (no player found), the letterbox and HUD.
  Mouse look is frozen while the world takes no input (cutscene letterbox). Open: that every run-out is a
  `camera` sequence rather than a player state that the g_fps switch would override.

## 8. Second round in play: causes and fixes

Logs: `nukepanel.log` and `.prev` [L].

- **G2-1 Ladder exit shows the game camera.** [L] Each ladder resume logs `fp: resumed`, then `fp: view hold
  ends (state 3, ...)`, and only then `fp: g_fps "1" set`. Two causes. The bridge (`fp_view_bridge`) required
  g_fps applied, but on the resume frame it is still 0, so the hold ended at once. The apply waited for the g_rpg
  throttle (`s_rpgCheck++ % 60`), which the previous apply had left mid-cycle, so g_fps came back 59 frames later.
  In the first case of `nukepanel.log` the buffered `jf` trace puts state 17 at least 0.63 s after the ladder jump.
  Fix: a rise of `want` (resume or switch on) reads g_rpg and applies at once (`fp_check_now`), and the bridge holds
  on the switch until state 17 is entered (at most 1 s).
- **G2-2 Jumping onto a ladder moves Duke across the level: the track guard.** [L] In every case the head
  enters a ladder volume, the pause sets g_fps 0, and while the pause holds Duke's x/z jumps onto another track's
  line: `.prev` head (1278.5,-672,33.1) at the ladder at x 1280, then (-320,-672,33.1), the stale track 1456B5A0
  that runs in depth at x -320 (1600 units); `.prev` (613,-296,-66.6) -> (-128,-296,-807) (off the map in depth);
  `nukepanel.log` (7640.4,-885.9,143.9) -> x 4400 and (6041.3,-1152,109.3) -> (4400,-1152,-1514.9), the last
  followed by a 4 s fall and the Rescue. y is kept and only x/z move: the snap's signature. Rescue and Move
  anywhere did not run before any of them (Move anywhere was off all session).
  Cause, by reading [D]: state 17 holds `+0xB1C` at 0, so `+0xB20` is never re-picked and goes stale. The 17 exit
  `playerstate_free_exit` 0x1007fb50 writes `+0xB1C = 1`. The next tick's `actor_track_update_if_moved` 0x1008dfa0
  (from FUN_1008d920 at the start of `player_tick`, gated only on `+0xB1C` and `+0xA14`) picks and snaps. The pick
  keeps the current track when no volume holds Duke (offscreen-and-track B3), and the snap writes x/z onto it.
  Fix (`fp_track_guard`): while our hold has Duke out of 17 (ladder pause, interact hold-off, bridge), `+0xB1C` goes
  back to 0 when `game_track_project_point` on `+0xB20` says the snap would move him more than 24 units or cannot
  measure it. The engine writes 0 there itself in 17, the ladder enter, state 7, the state 13 enter and state 4
  (section 1b), so the ladder runs with 0 anyway: `playerstate_climb_think` reads it only through
  `player_track_move_vector`, and `playerstate_climb_pretick` reads `+0xB20` (null-checked) for the facing [D]. The guard runs
  inside the exit tick, right after the switch call (fpmoves' `player_set_state` detour, now armed with the mode),
  and again on the frame path. Back in 17 it clears. Switched off, it writes `+0xB1C = 1` back, and re-picks first
  when `+0xB20` is still set. Log: `fp: track guard ...`, the counts on the 5 s line, and `fp: Duke moved N units in
  one frame`. With Debug log on, `fp: trail` holds 30 frames before and after each ladder, state, track or guard
  event. The touch scan has no `+0xB1C` access of its own (section 1b), so the grab should not need it; its callees
  are unread. Open: whether a physics move in the exit tick runs the constrain (vt+0x14C) on the stale track
  before the guard. The trail shows it.
  - The pause starts on the engine's own grab condition, head in a ladder
    volume AND Up (mask 8, `area_ladder_grab_test` 0x1009a460), or a ladder state (`fp_ladder_reason`). Walking past
    a ladder or dropping off its foot no longer pauses. In state 3 the guard's 0 zeroes left/right, so 300 ms there
    without a ladder state ends the reason. The reason stays latched until Up or the volume lets go, and first
    person resumes (`fp: ladder pause ends`). Switched off mid-climb, the restore of `+0xB1C = 1` and both re-picks
    wait for a free state: RESTORE only in state 3, and `fp_repick` waits in 4, 5-11 and 13. The engine's ladder
    exit writes 1 itself. The guard's hold is keyed to the player and his server id (+0x78).
- **G2-3 Freecam teleport in first person lands on a track.** [L] Every first-person teleport logs the
  target reached (`#1 ... -> (-445.1,289.0,-183.5) asked (-445.1,289.0,-183.5) track 0x14390548->0x0`). A frame
  later Duke stands at z 64 on track 143812C0 (`Duke (-445.1,289.0,64.0)`), and likewise in `.prev` #3, #4, #6, #7
  and #8. The Rescue lines show the same in state 17 (`track ->0x0, +0xB1C 0->1, state 3`). Order in
  `player_teleport` 0x1007d830 [D]: `player_clear_motion`, then `actor_teleport`, which writes the target while
  `+0xB1C` is still 0, so its own `actor_track_update_if_moved` does nothing. Then `player_set_state(3)` runs the
  17 exit (`+0xB1C = 1`), and the next tick picks a track and snaps x/z. Fix (`tp_keep_off_track`): after a
  teleport out of first person (state 17, or under our hold), `+0xB1C` goes back to 0 and the track guard holds it
  until 17 returns at the next tick's switch. This covers the freecam teleport and the Rescue; the exit's re-pick
  is left alone. Move anywhere was not measured (off all session). By reading, its snap detour already keeps x/z
  through `player_teleport`'s own track update and the next tick's, so it is unchanged. Log: `teleport: #n a frame
  later at ..., N from the target in x/z` measures both modes.

## 9. Third round in play: causes and fixes

Logs: `nukepanel.log` [L]; Ghidra MCP [D/A]. Functions created there: `FUN_100264f0` (pig cop fire),
`playerstate11_enter` 0x10087330, `playerstate11_pretick` 0x10087560.

- **G3-1 Ladder to ladder resets the view.** [L] Pause, resume, then a new pause one frame later: hold yaw
  84.9, one state-17 frame (17 yaw range `270.0..270.0`), next hold 90.0, the ladder's facing (lines 22641-22810).
  The new hold seeded from the last 17 pose, and the push of 17's first frame predates our yaw restore. Fix: a hold
  that starts while Duke is still in 17 seeds from his yaw, which the restore and mouse look wrote
  (`fp_hold_seed_yaw`). After a ladder state, the pause also holds while he is airborne until he is grounded in 3 or
  17 (`fp_ladder_air`, `+0xB00`), so a jump between ladders never returns to 17. Log: `fp: off the ladder ...`.
- **G3-1b Landing after a ladder jump still reset the view.** The air hold handed
  back on landing, but 17's first push is built before our yaw restore lands (the stale-pose finding of G3-1), so
  that frame faced the ladder's way. The log has no yaw at the hold's end, so the size of the jump is not measured.
  Fix: the hold's yaw is shown in 17's pushes until the engine's own view agrees with it, at most 3 frames
  (`fp_handback`). Pitch was already ours throughout. Log: `fp: view hold ends (... yaw, pitch)` and
  `fp: handback #n ...` give the before and after.
  - **Measured: the first handback never took** [L]: hold yaw -440.6, then `the engine's view now -90.0
    (cap reached)`; the same for 8.8. The entry line shows `+0xB1C 1`: player_tick enters 17 after the state's
    pre-tick (switch 0x1007b089, pre-tick call 0x1007aec6), so the next tick's `actor_track_update_if_moved`
    (0x1007ae6c, section 1b) still runs. It takes our yaw write as a turn and its snap re-faces Duke along the track
    (`FUN_10096410`). Fix: the handback writes `+0xB1C = 0` with the yaw, the value 17's pre-tick writes every
    tick (`fp_handback_track_off`). `fp_handback` stays for the one stale first push and ends once the engine's
    view agrees. The resume after landing waits `FP_RESUME_LADDER_MS` (300 ms) of a live world. That wait exists
    for ladder exits in mid-climb, so a grounded landing could resume at once; it is left as it is.
- **G3-2 Jumping onto a ladder sideways walks Duke to a fixed point.** [L/D] The trail's state-5 run at line
  8615 walks 740.9 units, (613.6,-344,-48.6) to (-127.1,-364.5,-64), and the guard measured the snap onto the stale
  track at 741.3. The touch scan's ladder grab projects the ladder's position onto `+0xB20` (track vt+0x1EC) whatever
  `+0xB1C` says, and state 5 walks to that point. Grabs with `+0xB20` already 0 (after a teleport) walked 9-19 units.
- **G3-2b Interact (E) sideways: Duke runs along the line forever (same cause).** [L/D] E on a button
  started state 11 (net message 6). `playerstate11_enter` projects the button onto `+0xB20` (no `+0xB1C` test), and
  the 17 exit wrote `+0xB1C = 1` outside the switch, where our guard did not run. The next tick picked track
  030029D0 and snapped z 101.5 to 128 (line 30310). State 11 then walked toward the button with every move bent
  along the line: x 8315 to 8452, then stuck in state 11 for good. Fix for both: the `player_set_state` detour now
  sees every exit from 17, not only the switch's. Under our hold (the ladder pause, or the Interact key's scripted
  state), a track the snap would move Duke more than 24 units onto, or that cannot be measured, is forgotten before
  the exit: actor_teleport's own reset (0x1008e833-0x1008e84b: `+0xB58/+0xB5C = -1`, `+0xB20/+0xB24/+0xB28 = 0`,
  `fp_track_forget`). Its next call, vt+0x230 `actor_track_update_if_moved`, is not made: it returns at once while
  `+0xB1C` is 0 (section 1b), which 17 holds before the exit and the guard after it. Open: whether anything else
  needs that call; the `forgets=` count and the trail show it. After the exit the guard holds `+0xB1C` at 0
  (unmeasured). Log: `fp: track forgotten ...`,
  `forgets=` on the 5 s line.
- **G3-camera After that, First person off leaves the camera at a fixed point.** [L/D] From the state-11
  exit on, every engine camera seed reads eye (922,-938,-768), whatever Duke's position and through two
  `player_teleport`s, which snap the following camera (`camera_reset_and_retarget`). So the view is another camera.
  `playerstate_free_exit` 0x1007fb50 hands the view to the first camera `s=%d?t=2?Y=512` finds. Fix: after every 17
  exit whose prior view was Duke, the camera with `+0xB34 == player` (actor_teleport's test) is put back through
  the exit's own `set_view_entity` (0x100e55d0, found from the exit's call, retried every 5 s if refused;
  `fp_exit_view_fix`). This runs on every exit from 17, including the switch off and a console `g_fps 0`. A
  script's camera is left alone. Every exit logs `fp: 17 exit view: before, the exit chose, the camera following Duke`, which settles
  the reading. Open: whether 922,-938,-768 is a second camera rather than the follower in a broken state; the
  line shows it.

## Open questions

- **F2** Is the held weapon visible at the hand, and do the bones animate? Look, and log `actor_get_bone_world_pos`
  of "Bip01 R Hand" over 30 frames while walking.
- **F3** Impulse max of 201-204 (`0x480d98 + i·0x54 + 0x10`) and the turn rate with mouse binds at 60 vs 175 fps.
- **F4** Level art from inside: holes, cut-away walls, backdrops. Look in three episodes.
- **F5** Die in g_fps: does the state-0 death survive the per-tick switch? Log `+0xB7C` and health around the death.
- **F6** Monster AI with Duke off every track volume: do monsters still path and attack? Watch one level section.
- **F7** Leaving the mode: does Duke's XZ snap to a track (expected), and where to with and without the teleport
  re-pick? Log `+0x18C` and `+0xB20` before and after. Assumption: one world tick per frame. The re-pick runs on
  the frame path, and the next tick's `actor_track_update_if_moved` (0x1007ae6c) snaps him before the 17->3 switch
  (0x1007b089). If a second tick ran before our frame, the snap would come first and the re-pick would land on the
  snapped spot. The `fp:`/`mv:` re-pick lines log the distance moved, which shows it.
- **F8** Eye height: pos + 40 against Duke's half-height `+0x2B8.y` (is the eye inside the head?). Read it once.
- **F9** Does a cutscene really show from the eyes (the pre-tick re-assert)? Trigger one with `g_fps 1`.
