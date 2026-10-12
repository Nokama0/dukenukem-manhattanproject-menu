# Mouse aim and mouse fire for Duke

Mouse fire already works: the config binds `mouse1` (left button) to `attack`, and the DirectInput buttons reach
the triggers. Mouse aim has four parts, none of which fights the engine, and all work in game:
1. **Reticle** from the engine's own software cursor (`inp_cursor_info`), which already sums the exclusive
   DirectInput mouse deltas during play (the panel keeps its own cursor on top, section 10).
2. **Shots** toward the world point under the cursor, through the silent-aim `weap_get_fire_dir` detour. Shots land
   under the reticle.
3. **Visible upper-body aim** through the engine's single analog aim channel: the eased **"Bip01 Spine1" bend**,
   `player_spine_bend` duke!0x1007c7f0 (+0xDEC degrees). Its target is hard-coded to 0/11/22 degrees, so it is
   driven by a detour (`torso.cpp`), not by a field write.
4. **Facing** by writing the yaw +0x1AC to yaw + 180 in PS_idle (section 9); Duke turns in place.

Player +0x1A8 is the whole body's orientation, not an aim angle: it is the wrong lever.

Method: **[D]** decompile, **[A]** disassembly, **[X]** xref/instruction scan, **[S]** string, **[C]** shipped or
config script file. Bases: duke_base (duke!) 0x10000000, prism3d (prism!) 0x400000, input.dll (input!)
0x10000000.

## 1. What the engine has for aiming

### 1a. The smooth transition: two eased layers, one analog angle

Looking with the direction keys blends smoothly instead of snapping. That blend has two sources:

- **Discrete look-up clip, cross-faded.** `player.def` [C, db_e01.zipfs `scripts/player.def`, block
  `player_anims_add`] defines the additive layer set (priority 150): `PAS_idle`, then **`PAS_look_up`** with one
  clip per weapon (`duke_idle_up.grn`, `duke_hold_up_shotgun.grn`, `duke_idle_up_hvywpn.grn`, ...).
  - **There is no look-down clip.** The `duke_40deg_*_up/_dwn` clips (`special u/d`, "easing is done manually")
    are *slope stances* of PS_idle, PS_step, PS_running and PS_crouching, not aim clips.
  - The up key requests the look-up layer: mask bit 8 (`TEST BL,0x8`) → `player_request_add_anim(2)`
    duke!0x10077620, which writes **+0xC18 = requested PAS** [A 0x10085a20-0x10085a70, D 0x10077620]. The PAS
    name table is at duke!0x10164328 (0 none, 1 idle, **2 look_up**, 3 kick, ...) [S/X, lookup loop 0x100762d0].
  - `player_anim_update` duke!0x1007bea0 (called from `player_tick` 0x1007ae50 at 0x1007aed9 [A]) switches layer
    sets when requested ≠ current:
    - base PS: +0xC0C requested / **+0xC10 current**, layers +0xD24[12];
    - additive PAS: +0xC18 / **+0xC1C**, layers +0xC60[12];
    - pain: +0xC24 / +0xC28, layers +0xD90[12].
  - The old layers move to +0xCF4 / +0xC30 and **fade out over 0.1 s** (`skmod_set_anim_parm(model, layer, 4,
    1 - 10·(now - t_switch))`). The new ones start through the state's vt+0x30/0x34/0x38 callbacks, with the
    def's `ease_in_time` [D]. The PS name table is at duke!0x101642c8: 1 idle, 2 step, **3 running**, 4 crouching,
    5-8 jumps [S/X]. So the smoothness is a **time cross-fade between fixed poses** (forward ↔ up). It is not
    angle-driven.
- **Procedural spine bend: the only analog aim angle.** `player_spine_bend` duke!0x1007c7f0 runs every tick from
  `player_tick` at 0x1007aed2, right before `player_anim_update` [A]. It:
  - picks a target: **22° while +0xC1C == 2 (PAS_look_up)**; 11° or 22° while +0xC10 == 3 (running) and the held
    weapon's +0xB08 has bit 2 (11° when PS+0x02 == 6); else 0;
  - moves **+0xDEC** up at **230°/s** toward it. It subtracts 100°/s when the target is 0, but its clamp to
    [0, target] then snaps the bend straight to the target: **the stock never eases down**;
  - lazily builds a pose: +0xDE4 = `skmod_open_pose(1)`, `skmod_set_pose_bone_name(pose, 0, "Bip01 Spine1")`,
    `skmod_copy_pose_bones(pose, model+0xAE4)`;
  - lazily adds a layer: +0xDE8 = `skmod_add_pose_animation(model, pose, <double FLT_MAX>)`,
    `skmod_anim_priority(model, layer, 1000.0)`;
  - each tick calls **`skmod_set_pose_rotation(pose, 0, &(0, +0xDEC, 0), 0)`**;
  - at 0° calls `skmod_rmv_pose_animation(model, +0xDE8)` and sets +0xDE8 = 0.

  Evidence: [D], with the `pfn_*` names from dumps/duke_base.pfn.txt (0x10183d70 open_pose, 0x10183c88
  set_pose_bone_name, 0x10184064 copy_pose_bones, 0x10183abc add_pose_animation, 0x10183c0c anim_priority,
  0x10183b9c set_pose_rotation, 0x10184080 rmv_pose_animation). A boss turret uses the same API for continuous
  aim: FUN_1000cab0 eases +0x155C toward the target yaw and sets the rotation `(angle, 0, 0)` [D]. So an
  **analog, per-frame pose rotation is a supported engine path.**
- **The "look" key (RCTRL + direction) moves the camera, not Duke.** The player camera helper `*(player+0x1118)`,
  FUN_1008a700, offsets the view by ±200 horizontally and ±320 vertically, then springs it back (`spring_step`,
  this+0x7C) [D, dumps/player_cam.decomp.c].

### 1b. Why +0x1A8 is the wrong lever (see the aim.md open questions)

- +0x1A8 is the body orientation (pitch, yaw, roll).
  - The rope/zipline code pitches the **whole body** to the rope slope: FUN_10081610 writes +0x1A8 = ±slope
    [D 0x10081930].
  - The wall-ahead probe FUN_1008a390 casts its ray along `ypr_2_cartes(+0x1A8)` [D]. A pitch write would tilt
    both the model and that probe.
- **Yaw is re-snapped onto the track** whenever the actor is snapped: `game_track_snap_actor` 0x10096070 →
  FUN_10096410 projects the current forward onto the track's vertical plane and writes **only +0x1AC** (yaw) [D].
  So a yaw write survives only as a sign (facing along or against the track).
- **Writers of +0x1A8** (74 `MOV/FSTP [reg+0x1a8]` sites [X]): in player code (0x10076d82-0x10085516) they are
  all state entries, teleports (`actor_teleport` 0x1008e7cd), the rope pitch (0x1008193e), and "negate pitch,
  yaw and roll" facing flips after a wall probe (0x1008343d, 0x100834e2, 0x10085516, gated on FUN_1008a390/
  FUN_1008a2b0) [A].
- **No per-tick pitch writer was found for idle, walk or run.** So a pitch write would probably stick, and tilt
  Duke like a plank. That is not an aim. Not tried at runtime.

### 1c. The three meanings of "mouse aim", mapped to engine state

| option | what changes | engine lever | sticks? |
|---|---|---|---|
| (a) aim toward the cursor (shots + visible up/down) | fire direction; torso bend angle; facing when the cursor is behind | fire dir: `weap_get_fire_dir` detour (§2); torso: detour `player_spine_bend` (§1d); facing: yaw write (§9) | detours run inside the tick, so there is no fight. Facing works in PS_idle only |
| (b) facing flips left/right by cursor side | yaw sign only (+0x1AC ≤ 0 test, FUN_10081610) | a direct yaw write in PS_idle (§9; the one-frame press was tried and dropped, §7) | a held movement key overrides it |
| (c) pitch only (Duke keeps facing, aims up/down) | fire dir + torso bend | as in (a) without the facing part | yes, by construction |

### 1d. Visible-aim lever: detour `player_spine_bend` duke!0x1007c7f0

- `void __fastcall(player* ecx)`, plain `RET` (0x1007c9b5, 0x1007c9df), one caller (`player_tick` 0x1007aed2,
  `MOV ECX,ESI; CALL`) [A]. It reads only player fields and the frame dt (`world+0x38`) [D]. Prologue
  `83 EC 10 56 8B F1 | 8B 86 E0 00 00 00` (sub esp,0x10; push esi; mov esi,ecx): the first 6 bytes hold no
  relative operand, so a 5-byte JMP plus a 6-byte trampoline is clean [A 0x1007c7f0]. Signature: include
  through `83 F8 03` at +0x15 (the `+0xC10 == 3` test).
- **When mouse aim is off:** call the original. **When on:** run a copy of the same logic with our target angle
  (cursor pitch, clamped to −35..+35) and our ease rate. Write +0xDE4/+0xDE8/+0xDEC exactly as the
  engine does (fields it writes itself) and call the same skmod exports in the same order.
- When our angle goes away (switch off, freecam, no point), `torso.cpp` keeps the bend and eases it to 0 at
  100°/s before handing back, since the original would snap it. At 0 the layer is removed. No stale state is left.
- **Sign/axis:** the engine uses +Y with positive = look-up (22° with PAS_look_up). Negative (down) is never
  produced by the engine; −35..+35 works in game. Rotation units are degrees by inference (11/22, and the turret's
  +0x155C wraps at 360) [D].
- The gun follows the bend (the weapon is attached at "Bip01 R Finger0", a child of the spine; player.def
  `bone_parent` [C]). The muzzle (weapon vt+0x214) therefore moves with it. The bullet origin
  (`weap_get_fire_origin` = eye) does not.
- Past about 30° up, the head's forward y can exceed 0.5, and `playerstate_get_fire_dir` then switches to its
  look-up constant (aim.md §1). The fire-dir detour overrides that anyway.

## 2. Fire direction toward the cursor

- **Reuse the silent-aim detour.** `weap_get_fire_dir` 0x100508b0 already rewrites `*out` for every player
  weapon (aim.md §1). Mouse aim sets `*out = normalize(P - origin)`, with origin = `weap_get_fire_origin` and
  P = the cursor's world point.
- **Cursor → world point:**
  1. Build the cursor ray from the view matrix captured during the world pass (render.md §3) and the cursor's
     NDC (§4).
  2. Intersect it with the **vertical plane through the fire origin** whose normal is the horizontal part of the
     camera's forward vector. On straight tracks that plane is Duke's track plane.
  3. On curved tracks, use the track tangent at Duke instead: `track->vt+0x1C8` `game_track_get_dir`, or the
     sample polyline (offscreen-and-track.md B2/B4).
- **Depth:** the plane point (pure 2D aim). A "sticky" pick of the monster under the cursor (from the ESP
  projection, reusing the silent-aim target filter) is the alternative; it is not built.
- The point is on screen by construction, so the bullet's view-side clip (aim.md §4) never cuts short a shot
  aimed at it.
- **Precedence:** silent aim and mouse aim write the same `*out`. A silent-aim lock wins over the cursor (§6).
- The arifle cone still deflects after the detour (weapons.md §5).

## 3. Mouse fire

- **It already exists.** `duke/base/config.cfg` [C] (the live config, which also holds the WASD binds)
  has `bind_imp mouse1 "attack"` and `bind_imp mouse3 "kick"`.
- **Button mapping** (input! buffered reader 0x10001320, jump tables 0x100014cc/0x100014e4 [A/read_memory]):
  - `DIMOFS_BUTTON0` (0xC) → button 0 = trigger 262 `mouse1` = **left**;
  - `DIMOFS_BUTTON1` (0xD) → button 2 = trigger 264 `mouse3` = **right**;
  - `DIMOFS_BUTTON2` (0xE) → button 1 = trigger 263 `mouse2` = **middle**;
  - event type = `((data & 0x80) | 0x100) >> 7` = 3 down / 2 up.
- Check: read `inp_trigger(262..264)+0x38` while clicking: 262 = left, 263 = middle, 264 = right.
- prism! FUN_00407290 cases 2/3 set trigger `262 + button` +0x38 (down) [D]. The bound impulse then flows as
  for keys. No code is needed.
- The engine's semantics apply: the Golden Eagle is semi-automatic (one shot per press, weapons.md U2). A
  "hold to auto-fire" option would pulse impulse 106 from the `rend_begin_frame` hook, as the triggerbot does,
  while `mouse1` is held.
- **Reading the button ourselves:** `*(u8*)(inp_trigger(262) + 0x38)`. `inp_trigger` prism!0x407110 is an
  export that returns `0x43d528 + i*0x240` [D]. The trigger layout is input-console.md §3. Our WndProc gets no
  WM_LBUTTONDOWN in play (exclusive + NOLEGACY, quick-wins §3).

## 4. Reticle and cursor position

- **Source: `inp_cursor_info(float* x, float* y)`** prism!0x407e20 (export, cdecl). It returns the software
  cursor in NDC: −1..1, y up [D].
  - In DInput mode every relative event (type 5) adds the raw dx/dy (mickeys) to 0x486170/74. The sum is
    clamped to `[0, w] × [0, h]` from `rend_get_params`, then normalised by `inp_cursor_set` [D FUN_00407290
    case 5].
  - The only writers of 0x486170 are the input init (0x406e02) and that event handler (cases 4/5) [X]. Nothing
    re-centres it during play: `inp_cursor_reset[_lazy]` move only the OS cursor and run under
    `inp_set_mouse_state(1)` (free camera) [D 0x406fe0/0x407080/0x406e90].
  - So **the engine keeps an on-screen aim cursor for us already**, with no GetDeviceState call and nothing
    taken from the engine's buffered stream.
- **Properties:**
  - 1:1 raw mickeys; `m_sensitivity` scales only triggers 258-261.
  - Clamped to the screen.
  - Frozen while the device is unacquired (panel open, freecam), since no events arrive.
  - It keeps moving while game input is disabled (menus), because case 5 has no 0x43cf70 gate.
  - The engine UI reads it too (`FUN_00423600` and `FUN_004236f0` [X]).
- **Drawing:** a reticle on our canvas at `px = (x+1)/2·W`, `py = (1-y)/2·H`, in the render size that
  `inp_cursor_set` used (display.md canvas maths). The engine cursor's NDC matches the 3D viewport's NDC on the
  3440x1440 window: shots land under the reticle. The log's `reprojErr` cannot settle this: it re-projects through the same view
  and projection, so it checks only the panel's inverse.
- **Own sensitivity:** the panel reads raw deltas with `GetDeviceState` (vt+0x24) on the engine device, as
  quick-wins §3 W1 does for the wheel (section 10). DirectInput documents immediate and buffered data as separate
  (quick-wins U3.2).

## 5. Hazards

- **Facing:** the states turn Duke from the movement keys and the wall probe. A cursor-driven flip while a key is
  held the other way oscillates, so the facing write is limited to PS_idle with no key held (section 9).
- **Spine detour:**
  - the skmod callee ABIs match the duke call sites and the skmod.dll entries (section 8).
  - A second pose layer or a fight with the stock layer is avoided only if we reuse +0xDE4/+0xDE8 exactly (same
    fields, same calls).
  - A model swap or level change frees the model under +0xAE4. The engine's own lazy-create rule (re-add when
    +0xDE8 == 0) must be copied, including the null check on +0xAE4.
- **Look-down** has no clip, so down-aim relies on the spine bend alone. Large negative angles may look broken
  (not tried beyond −35).
- **Precedence** with silent aim, triggerbot and freecam (the cursor is frozen while freecam runs, §4).
- The Golden Eagle is semi-automatic (§3).

## 6. What the panel does

1. **Reticle:** drawn at the cursor when "Mouse aim" is on and no panel or freecam is active.
2. **Shots:** in the `weap_get_fire_dir` detour, when mouse aim is on and silent aim has no lock, `*out` aims at
   the cursor's point on Duke's plane (section 2). Clamped to the facing half-plane: behind Duke goes horizontal
   forward.
3. **Fire:** nothing to build (`mouse1` = attack).
4. **Torso:** the `player_spine_bend` detour (sections 1d and 8).
5. **Facing:** the yaw write in PS_idle (section 9).

## 7. Facing by a one-frame press (tried, dropped)

- The idle state's think (duke!0x10080130, reads +0xE00 at 0x10080140) calls FUN_10087fd0 for the movement vector:
  mask bit 1 (left) gives -1 and bit 2 (right) +1 along `track->vt+0x1C8` (`game_track_get_dir`), negated when
  that call returns its flip flag [D]. A vector with length squared over 0.001 (0x10126a64) writes the facing
  through `[0x1012619c]` into +0x1A8 and requests **PS_running** (`player_request_state(3)`, 0x10080384) [A].
- So a one-frame press turns Duke and also starts a run: turning in place is **not expected**. Whether one
  frame of PS_running moves him is runtime. The trial pressed once, watched 30 frames and refused the
  row when a clean press moved him more than 2 units, or after 4 clean presses that did not turn him. A press
  is clean only from PS_idle with Duke still, with no move key in +0xE00 on any watched frame (our own bit is
  allowed for the first 2 frames), and with Duke in idle, walk or run throughout. Any other press is counted as
  tainted and never refuses. A refusal lasts until the next level (a new world at player +0xE0).
- Measured: presses #2-#5 (still, grounded, no keys) left the yaw at 270; press #6 turned Duke and walked him 16
  units, so the row refused itself. The press lever is gone; section 9 replaces it.

## 8. The spine-bend replay

- `player_spine_bend` read whole [A 0x1007c7f0-0x1007c9df]. The ease picks its direction from the target's
  sign, not from the current angle: target > 0 adds 230·dt, else subtracts 100·dt; the result is then clamped
  to [0, target]. The +0xDE8 layer is stored before `anim_priority` is called. `set_pose_rotation` is called
  with +0xDE4 even when `open_pose` returned 0. +0xAE4 is never null-checked.
- skmod.dll entries read [A]: all seven pose exports are cdecl with a plain RET, with the argument orders that
  engine.h records. The two doubles are real doubles: `anim_priority` does `FLD double [esp+0x14]`.
- `torso.cpp` replays that body with the cursor's angle and a two-way ease (230 up, 100 down, never past the
  target; the down ease is ours, the stock snaps). With no angle it eases its own bend to 0 first, then passes
  through to the original, as it does with a null model. The skmod ABIs are settled by reading: `skmod_add_pose_animation(model, pose, double)` and
  `skmod_anim_priority(model, layer, double)` take their doubles as two dwords, and the other `skmod_*` pose entries
  are cdecl too.

## 9. Facing by yaw

The lever is a direct write of the yaw +0x1AC to yaw + 180. In game Duke turns in place while idle.

Read before choosing it [X 145 operand matches for `+0x1ac]` in duke_base; D/A where cited]:
- **No separate facing flag.** Facing is the yaw. The track snap `FUN_10096410` [D] takes the forward from
  `ypr_2_cartes(+0x1A8)`, projects it onto the track plane and writes only +0x1AC, so a reversed yaw comes back
  as the reversed track direction. `actor_track_update_if_moved` 0x1008e000 [D] re-snaps when the yaw moved by
  more than 1. The camera helper `FUN_1008ac10` [D] leads the view from the yaw, so the camera swings with the
  facing, as it does after a key turn.
- **Idle leaves the yaw alone without input.** The idle think 0x10080130 writes the angles only through
  `cartes_2_ypr` of a non-zero movement vector (0x1008031e-0x1008032c [A]), and that vector is zero with no
  left/right bit (`FUN_10087fd0` [D]). The other writers in player code are state entries, the rope and zipline
  code (0x100809fd, 0x10080a94, `FUN_10081610`), the wall-probe flips and teleport.
- **Running is not safe to write.** Walk and run take the facing from the keys each tick; the jump, rope and
  ladder states were not read through. `mouseaim.cpp` therefore writes only in PS_idle, with no move key in
  +0xE00 and Duke still for a frame. It watches 30 frames for the side held, his movement and the keys. Two
  clean writes in a row that do not stick, or one that moves him, refuse the row until the next level; trials
  with a key or a state change are tainted and only counted. The write sticks in game.
- **Never while the game takes no input:** the write needs world+0xC8 bit 8 and
  world+0x14 == 0, the input reader's own gate (0x1007d27a-0x1007d28a [A]), and no shown "letterbox" window
  (a cutscene, shot.h). A track seen nearly end-on (Duke's forward under 4 px on screen) gives no trial.

## 10. Our cursor and Sensitivity

- Mouse aim keeps its own cursor in render pixels. It is seeded from `inp_cursor_info` and then moved by the
  engine mouse's `GetDeviceState` lX/lY times Sensitivity (10-400 %, 100 = the engine's own 1:1), and clamped to
  the render size. The reticle, the aim point and the facing side all use it.
- **The read leaves the engine alone, by reading:** input.dll reads the mouse only through `GetDeviceData`
  (vt+0x28, at input!0x10001367/0x10001398 and 0x100016b9/0x1000170a [X]); its one `GetDeviceState` (vt+0x24,
  0x10002020) is the joystick poll. DirectInput keeps the immediate state apart from the buffer, as the
  Scroll-wheel zoom's lZ read already relies on. The log line's `engineMoved` (the engine cursor's own travel)
  keeps rising while `own` does: that is the runtime check.
- lX/lY/lZ are changes since the previous `GetDeviceState`, so `hotkeys.cpp` sums every read and each
  consumer (the wheel, mouse aim, the stuck-mouse probe) takes its own axes; otherwise the 1 Hz probe would drop
  the wheel notches of the read it made.
- When the device cannot be read (panel, freecam, focus lost, WndProc input) our cursor holds, unless the
  engine's moved: then it follows the engine's, without the sensitivity.

## Open questions

- **M1** A pitch write to +0x1A8 at the end-of-frame hook in PS_idle/PS_running was not tried: log +0x1A8 at the next
  hook. Expected: it sticks and tilts the whole body, so it is not used.
- **M4** Spine bend beyond −35..+35 degrees: where the mesh breaks is unrecorded. Positive = up matches the stock 22°
  look-up.
- The muzzle blast is angled toward the redirected shot instead of the gun's direction (cosmetic).
- An auto-fire pulse while `mouse1` is held (for the semi-automatic Golden Eagle) is not built.
