# Aim: silent aim, triggerbot, aimbot targeting

Every player weapon asks the same function for its shot direction, **`weap_get_fire_dir` duke!0x100508b0** (vt+0x210
of all 12 `actor_weap_*` vtables). Silent aim is one post-call rewrite of that function's out-vec3, gated on
`weapon+0x60 == local player`. There is no separate view or aim angle: the only aim state is the player's
orientation `+0x1A8` = **(pitch, yaw, roll) in degrees**, and the camera follows the track, not that orientation. So
a visible aimbot can at most turn Duke's body; mouse-aim.md covers the torso bend and the idle yaw write that the
panel uses (a pitch write was not tried). Fire is pressed by injecting impulse 106 after `inp_process`. Head
points come from the skmod exports. Monster velocity comes from vt+0x190. A bullet is a clipped hitscan that stops at
the first thing it hits. Bosses are targeted only while their own damage gate is open; the helicopter's core never
is (section 7). Sections 8-12 cover the G.L.O.P.P. Ray, roach eggs, overkill, weapon immunity and the X-3000.

Method: **[D]** decompile, **[A]** disassembly, **[X]** xref/vtable scan, **[G]** inline Ghidra script, **[S]** string.
Bases: duke_base 0x10000000, prism3d 0x400000, p3shared 0x10000000, skmod 0x10000000. Named in Ghidra:
duke `weap_get_fire_dir`, `weap_get_fire_origin`, `player_get_fire_dir/_origin` (0x10078930/60),
`playerstate_get_fire_dir/_origin` (0x10088400/0x10088530), `actor_get_bone_{model,world}_pos[_by_name]`
(0x100906a0/0x100906f0/0x10090720/0x100907c0), `actor_get_world_velocity` 0x1008b420, `monster_get_world_velocity`
0x10030520, `player_read_input_mask` 0x1007d250, `proj_bullet_think` 0x1003a990, `actor_proj_bullet_ctor` 0x1003a790,
`proj_bullet_parse_key` 0x1003b610, `proj_bullet_damage_hit` 0x1003ad90, `proj_is_ignored` 0x10039860,
`proj_add_ignore` 0x10039000, `game_clip_ray_to_view_sides` 0x100e8540, `weap_prpg_spawn_rocket` 0x1005a4f0,
`weap_pcannon_spawn_plasma` 0x10059790, `weap_gray_update_beam` 0x10056710, `weap_project_owner_velocity`
0x100599c0; prism `wm_dispatch_process_input` 0x424050.

## 1. Silent aim lever

**Chain** [A]: `weap_get_fire_dir` duke!0x100508b0 → owner vt+0x15C (update transforms) → owner **vt+0x21C** =
`player_get_fire_dir` 0x10078930 (the only vtable slot holding it: 0x10136cc4 = player vt 0x10136AA8+0x21C [X]) →
state object `player+0xB80[player+0xB7C]`->vt+0x28 = `playerstate_get_fire_dir` 0x10088400 (in 17 of the 19 state vtables
0x10136588..0x10136a60, stride 0x48 [X]; two override it, below):
- normal case: `ypr_2_cartes(out, player+0x1A8)` (0x10088506-0x10088512) [A];
- **look-up case:** the "Bip01 Head" bone's forward vector has y > 0.5 (head tilted up, `PAS_look_up` [S
  0x101644c0]). The direction is then the constant (0, 0.95, 0.05)·1.0512, rotated by yaw `+0x1AC` (0x10088477-0x100884fb)
  [A]. The detour overrides both cases.
- **Two state overrides:** vt 0x101366a8 -> dir 0x100810d0 / origin 0x10080f80,
  and vt 0x101368a0 -> 0x10084730 / 0x10084790 (negates ypr; origin from the +0xBCC object). Both are pure getters
  that null-check +0xBD8 / +0xBCC, so in those states the unredirected direction differs from the normal case.

**ABI** (entry read, 0x100508b0-0x100508de) [A]: `void __thiscall weap_get_fire_dir(actor_weap* this /*ECX*/, vec3* out)`,
`RET 4`. It saves ESI only. If `this+0x60` (owner) is NULL, or fails `+0x48&2` / `+0x4C&0x10`, **out is left untouched**.
Prologue `56 8B 71 60 85 F6 | 74 25`: the first 6 bytes (push esi; mov esi,[ecx+0x60]; test esi,esi) hold no relative
operand, so a 5-byte JMP plus a trampoline that copies 6 bytes and resumes at +6 (the JZ) is clean. **Signature caution:**
`weap_get_fire_origin` 0x100508f0 is byte-identical except for `FF 92 20 02` against `FF 92 1C 02` at +0x27. Include
through +0x2B. Better, resolve it at runtime from the held weapon's vtable: `(*(void***)[player+0xDF0])[0x210/4]`,
then byte-check it. That uses no offset table. `weap_get_fire_origin` itself is in no vtable (xrefs: only the three
direct calls above); its chain ends in the player's vt+0x220 `player_get_fire_origin` 0x10078960
(`56 8B F1 8B 06 FF 90 5C 01 ..`, RET 4), which differs from vt+0x21C 0x10078930 only in the state slot (+0x2C/+0x28).

**Who calls vt+0x210 and what uses the result** (all `CALL [reg+0x210]` sites in 0x10050000-0x1005d400 [G]; all
12 weapon vtables hold 0x100508b0 at +0x210 [X]):

| weapon | site | use of the direction | projectile origin |
|---|---|---|---|
| geagle | weap_geagle_spawn_shot 0x10055512 | bullet+0x1C0 = dir (entities §5) | `weap_get_fire_origin` 0x100508f0 (eye) |
| shotgun | weap_shotgun_spawn_shot 0x1005bb40 | bullet+0x1C0 = dir (0x1005bc2f-0x1005bc4a) [A] | 0x100508f0 (0x1005bc2a) [A] |
| arifle | weap_arifle_spawn_shots_cone 0x10052c02 | cone deflection applied **on top**, then +0x1C0 (weapons §5) | 0x100508f0 (0x10052e03) [X] |
| prpg | weap_prpg_fire_logic 0x1005a37e, weap_prpg_spawn_rocket 0x1005a55f | `cartes_2_ypr` → rotate (two rockets); rocket **+0x1C0 += dir·800** (0x1005a653-0x1005a6a4) [A] | weapon vt+0x214 (muzzle, 0x1005a573) |
| pcannon | weap_pcannon_fire_logic 0x10058932 → discharge(…, &dir) 0x100590d4 | plasma velocity = dir·((1-charge)·200+300) + owner forward velocity (weap_project_owner_velocity) [D 0x100590f0] | vt+0x214 (0x10059790) |
| pbomb | weap_pbomb_launch 0x10057986 | dir·(580 or 330 + rand%40) (weapons §5) | vt+0x214 (0x1005799a) |
| gray | weap_gray_update_beam 0x10056738 | beam object (+0xBFC)->+0xBEC..+0xBF4 = dir, every frame [A 0x10056795] | vt+0x214 |
| x3000 | none | damages every live monster, so it has no direction (weapons §2) | n/a |

Also cosmetic: arifle casing/flare FUN_10053300 (0x10053323), shotgun casing (0x1005be9f), the pcannon charge-ball
placement (0x100598c5). Redirecting them is harmless. The `CALL [reg+0x210]` sites outside weapon code (for example
0x1007f2b4, 0x1008e091) have no argument and belong to other classes [A].

**Hook:** detour 0x100508b0. Call the original, then if `this+0x60 == local player` and a target is
locked, set `*out = normalize(aim_point - origin)`. For **origin**, call `weap_get_fire_origin(this, &o)` (0x100508f0,
same ABI; it is a pure getter chain: owner vt+0x15C then vt+0x220 = eye point `playerstate_get_fire_origin`). That is
exact for the three bullet weapons. prpg/pcannon/pbomb/gray start at the muzzle bone (weapon vt+0x214, per-class,
different ABIs), so aiming from the eye leaves a small parallax (size unmeasured). The menu has no "Max redirect"
limit (side-scroller: the lock takes the nearest visible enemy in any direction). The arifle cone still deflects after the detour: pair silent
aim with the no-spread lever (weapons §5).

**Post-spawn rewrite (rejected):** bullet+0x1C0 is written by the engine, so rewriting it breaks no
rule. But each class needs its own field (bullet +0x1C0 dir; prpg +0x1C0 velocity; plasma via vt+0x200; gray
per-frame beam field), and it only works if our hook runs between the spawn (weapon think) and the bullet's first
`proj_bullet_think`. The detour is one point for all of them.

## 2. Aim angles, turning, visible aimbot

- **The angle order (open in entities.md) is settled by the math:** `ypr_2_cartes` p3shared!0x10028ed0 [D]: `in[0]` = pitch,
  `in[1]` = yaw (degrees): `x = -sin(yaw)·cos(pitch)`, `y = sin(pitch)`, `z = cos(yaw)·cos(pitch)`, normalised.
  So **+0x1A8 = pitch (+ = up, since +Y is up), +0x1AC = yaw, +0x1B0 = roll.** The inverse is `cartes_2_ypr`
  p3shared!0x10028d80 [D]: pitch = asin(y), yaw = 360 - atan(x/z) (+180 when z < 0), roll = 0. Use these two exports
  for every angle conversion. playerstate_get_fire_dir's use of +0x1AC as yaw agrees [A]. Caveat: the free-fly
  handlers add `(pitchdown - pitchup)` to +0x1A8 (camera-culling §1; 0x1007fd48 [A]), so the camera's matrix sign may
  differ. For aim, only ypr_2_cartes matters.
- **How the player turns:** not by angle impulses. `player_read_input_mask` 0x1007d250 (vt+0x154, called per wm slot
  by `wm_dispatch_process_input` prism!0x424050 with the slot's channel) turns impulses 101-112 into bits at +0xE00,
  and keeps the previous mask at +0xE04 (for edge detection) [A]. The player never reads 201-204. Only one player
  state, the code around 0x1007fc20 (a free-look/debug state, [A] 0x1007fcfc-0x1007fda5), adds `(201-202)·50·dt` to yaw
  and `(204-203)·50·dt` to pitch. About 50 absolute writes of +0x1A8/+0x1AC/+0x1B0 are spread over the PS_* state code
  0x10076d82-0x10085532 [G]: facing flips (the yaw sign decides facing, as FUN_10081610 tests `+0x1AC <= 0`), slope pitch
  from the track (FUN_10081610 sets pitch = the track's slope), and teleport. It is a 2.5D side-view player: yaw = facing
  along the track, and "aim up" is the look-up head-bone branch in §1.
- **Camera:** `player_build_camera_target` → `game_track_place_camera` places eye and look-at from the track and the
  player position (camera-culling §1). A grep of the decompile dumps (game_track, player_cam) finds no read of the
  player's +0x1A8 [G]. **The camera does not follow the aim.**
- **Visible aimbot feasibility:** the only lever is writing player +0x1A8 (pitch, and yaw keeping its facing sign)
  from `cartes_2_ypr(target - eye)`. That is a field the engine writes, but the state code rewrites it on many ticks, and
  a yaw off the track axis may disturb facing and movement logic (a pitch write is untried; the yaw write in
  PS_idle works, mouse-aim.md section 9). The menu ships no Aimbot rows (ui/menu.cpp, TARGETING group): the
  camera follows the track, not Duke's aim. Its targeting rows serve silent aim and the Triggerbot's Locked mode.

## 3. Targets: head point, body point, velocity, projectile speed

- **Skmod handle +0xAE4 on monsters** [X/D]: monster base FUN_1002bd80 → FUN_1008d7b0 → FUN_10090b30 → `actor_model_ctor`
  0x1008f170, which initialises +0xAE4 (0x1008f20d). All `monst_*` and actor bosses inherit it. Model loads write it
  (FUN_1008f900 0x1008f92c).
- **Engine recipe** (`actor_get_bone_world_pos` 0x10090720 [D], `…_by_name` 0x100907c0 = index lookup then that):
  `if (+0xAE4) { m = skmod_get_bone_transform(h, idx); p = (0,0,0)·m; p = p·M(+0xF4) } else p = vt+0x160()`.
  Only boss code calls it (xrefs 0x1000xxxx/0x10012xxx [X]). Nothing in monster code names "Bip01 Head": its only
  users are player code (0x10088400/0x10088530/0x10080f90/0x10091e73) [S xrefs].
- Exports (skmod!, cdecl, [D]): `int skmod_get_bone_index(h, const char* name)` 0x100049c0 returns **-1 on failure
  and prints a console error** (`<skmod> get_bone_index: cannot g…`), so cache the index per class and never retry a
  miss each frame. `bool skmod_get_bone_transform(h, int idx, float m[16])` 0x10004bc0 returns a model-space bone
  matrix (Granny bone state, translation in m[12..14]). Call site: 0x10088408-0x1008842d [A] (push m, idx, h; add esp,0x14).
  World head = `(m12,m13,m14)` transformed by the row-vector world matrix at +0xF4 (render.md §3 convention).
- **Body point fallback** = hitbox centre (hitboxes.md §0): `(pos.x, pos.y - o, pos.z)` with pos = vt+0x160,
  o = +0x2C4. Head fallback = the box top minus a margin: `pos.y + dim.y - 2o - k`.
- **Target velocity:** call **vt+0x190** `vec3* __thiscall get_world_velocity(vec3* out)` (`RET 4`, returns out) [A].
  Monsters: `monster_get_world_velocity` 0x10030520 = **+0xC3C** + the parent's vt+0x190 (all `monst_*` vtables checked
  [G]). Actor default 0x1008b420 = **+0x1C0** + parent (boss_wozma, boss_mechnukem). porkchopper has its own
  0x1000fe70. Player 0x10078990 = +0x1C0 + +0xE40. Monster FUN_100305b0 zeroes +0x1C0, so do not read +0x1C0 on
  monsters. Units per second and whether it matches the animated motion are unmeasured. The fallback is the
  per-frame position delta / `sys_get_timedelta`.
- **Projectile speeds for lead** (no script keys; these are immediates/constants): **geagle/shotgun/arifle =
  hitscan (no lead)**; prpg **800** + inherited (float 0x10126ff0) [A]; pcannon plasma **300..500** by charge
  (`(1-c)·200+300`) plus the owner's forward velocity [D 0x100590f0]; pbomb **580/330 + rand%40**, a thrown arc (lead
  needs gravity: skip it); gray beam is continuous (treat it as instant). The rocket flies straight at constant speed (section 10); the plasma's
  path is unread.

## 4. Visibility: what a bullet actually hits

- **Bullet** [D proj_bullet_hitscan_ray 0x1003a9c0, A 0x1003aa47-0x1003ab0f]: on its first think (`+0xBEC` = 1 from
  `actor_proj_bullet_ctor` 0x1003a790) it takes `from` = spawn pos and `dir` = +0x1C0. If **+0xC08 `clip_to_frustrum`**
  (ctor default 1; script key in `proj_bullet_parse_key` 0x1003b610 [S]) is set, the length is
  `game_clip_ray_to_view_sides(from, dir, 5000.0)`: the distance to the **left/right planes of the view** (camera
  fov·0.52, planes 1500 deep) and at most 5000 [D 0x100e8540]. **Bullets only reach the screen edge.** Aim range and
  target selection must reject off-screen targets (unless the clip bypass is armed, re/offscreen-and-track.md A).
  The screen that matters is the **game's own camera** (the view object the clip reads), not a zoomed-out or freecam
  substitute: the fallback filter projects with `view_get_stock`. Then `phys_ray_test(world, from, to, +0x244, +0x220)`, walk
  `phys_collide_history()` (prism!0x40d9b0 = global list 0x497878; node+8 = record: +0 object, +0x14 hit point),
  skip ignored entities (`proj_is_ignored`: the owner, added for 0.2 s by `proj_add_ignore`), keep the nearest 3,
  qsort by distance, and damage in order (`proj_bullet_damage_hit` 0x1003ad90). A non-object hit (world) sets the
  budget +0xBCC = 0. An object hit costs 1 of the budget (default 1.0, proj base ctor 0x10038c90). **So the first
  thing along the ray takes the shot: the wall, or the monster in front.**
- **`bool phys_ray_visible(world, from, to, tmatcher_t* block)`** prism!0x40d8e0 [D]: pushes `block` as the blocking
  filter (NULL = default phys+0), walks every object with +0x2E0 set that `validate(block)` accepts, applies a
  bounding-sphere test, then calls vt+0x120, and returns **true when nothing blocked** (early exit, cheap). With the
  **world-only matcher** (freecam-teleport §2: `reset_rules(false); accept_superset(0x100,0,true); deny_superset(0x100,
  0x8000|0x10000|0x20000|0x400 …)`, all p3shared tmatcher exports) it is a walls-only line of sight that ignores
  monsters. It misses doors and force fields, so the lock uses the bullet's own ray (§4b) and runs this test only as a
  cheap first filter.
- **Triggerbot test that matches the bullet:** `phys_ray_test(world, eye, eye + dir·len, &m, &m)` with `m`
  accepting all types and `deny_ptr(player)`, and `len` from the same view-clip rule (or ≤ 5000 plus an on-screen
  check). Then take the **nearest** history record by distance: the shot lands iff `record.object == target`. That is
  the sibling lesson "test the hit's identity, not unblocked". The bullet's own filters are the object_e defaults at
  +0x220/+0x244 plus deny_ptr(owner). They equal "accept all" (§4b). `tmatcher_t::inherit`
  p3shared!0x10026c80 can copy a live object's filter if needed.
- **§4b. The bullet's own filter and what stops it** (settled by reading):
  - Filters: the bullet passes `block = +0x244`, `touch = +0x220` to `phys_ray_test`. Both are `mass_t` matchers
    (object_e+0xF4 is a `light_t`, whose ctor p3shared!0x10013fc0 first runs the `mass_t` ctor
    p3shared!0x10016370, which calls `reset_rules(true)` on +0x12C/+0x150, i.e.
    **accept everything**: `tmatcher_t::validate` p3shared!0x10026d20 returns the byte +0x20 when no rule matches).
    `proj_bullet_hitscan_ray` adds `deny_ptr(attacker's weapon)` on both (0x1003a9f2/0x1003a9fb) and
    `weap_geagle_spawn_shot` `deny_ptr(owner)` (0x100555c6/0x100555d2). No duke code narrows a bullet's filter
    further. So "accept all + deny Duke + deny his gun" is the bullet's filter, **as block and as touch**.
  - **How the history is built (prism3d):** `phys_ray_test` 0x40d820
    calls `phys_push_query` 0x40e7c0, which stores its 4th argument (block) at phys+0x48 and its 5th (touch) at
    +0x4C. The gather `FUN_004127c0` takes each collidable (+0x2E0) object that either matcher validates and flags
    it blocking (node+0x14 = 1) when the **block** matcher takes it, touch-only (0) otherwise.
    `phys_ray_vs_objects` 0x40f1b0 adds **every touch-only hit** to the history (`add(.., false)`) but keeps only
    the **nearest blocking hit** and adds that one last (`add(.., true)`). Touch hits are not cut at the blocker.
  - So the real bullet, whose block matcher accepts everything, sees **one record: the nearest object**. Its walk
    (0x1003ab60-0x1003ab89: skip an actor monster with user bit 0x80 while the budget is <= 1, and
    `proj_is_ignored`) and its "nearest 3" array never hold more than that one. That object takes the shot.
  - Our ray (`bullet_ray`): **block = world-only** (freecam-teleport §2), **touch = accept-all minus Duke and his
    gun**. The history then holds every object on the ray plus the nearest wall; `aim_ray_sort_cut` sorts by
    distance and drops everything past the wall, and `aim_obstacle_kind` judges what lies in front of the target.
  - `proj_bullet_damage_hit`: **actor** (+0x48&2): unless it is a monster with [+0xB94] >= 0x14, it calls vt+0x194
    and the budget drops by 1. vt+0x194 is `actor_damage_from_info` 0x1008b630, which forwards to vt+0x198. The
    default vt+0x198 is duke!0x10001c40, a bare `RET 0x14` (babe, model, key_hole, forcefield powerup: no damage);
    barrel/crate 0x100685a0, obstacle 0x100711f0, roach_pod 0x10072770 and monsters take health. **Object**
    (+0x48&1, not actor): except class `breakable` (never) and `breakable_adv` with [+0xBE4] == 0, it calls
    vt+0x170 and sets the budget to 0. The default vt+0x170 is `object_e::damage` p3shared!0x1001e6d0, a bare
    `RET 8` (doors/movers, platforms' base, water...); `force_field` has its own bare `RET 8` 0x100ee2f0;
    `breakable_adv` 0x100bab30 and `breakable_obstacle` (thunk 0x100bb6c0 → 0x100bab30) take health at +0xB0C
    only while [+0xC44] != 0, and at <= 0 break (net_send). Neither bit: no effect, budget unchanged.
  - **Destructible** (decision: such obstacles do not block) = the engine would take health from it:
    an actor whose vt+0x194 is `actor_damage_from_info` and whose vt+0x198 is not a bare `RET 0x14`, or an object
    whose vt+0x170 is the breakable handler (directly or through the thunk) with [+0xC44] != 0 (and [+0xBE4] != 0
    for `breakable_adv`). World (`+0x48&0x100`) and anything unread block. Pure rule `aim_obstacle_kind`; the
    target is reached when no blocker lies in front of it (`aim_ray_first_target`). The cheap world-only
    `phys_ray_visible` runs first and the full ray only for candidates it passes. Not read: `platform_entity`'s
    vt+0x170 0x100c18d0 (a state/timer at +0xB70/+0xB80 that can clear +0x2E0) is treated as a blocker; the log names each class met once.
  - **Two kinds of blocker (measured):** a `blocker` with `creatures_only` (property handler
    duke!0x100b9af0 -> +0xB0A, init 0x100b9a80 -> user 0x10000) passed a real shot (log: real shot #4 damaged the
    Hover Mech behind it), while one with neither `creatures_only` nor `player_only` (+0xB0B, user 0x20000) -- the
    doorway barrier, `e01p02_a01_r01` blocker2 `invisible:false` -- stopped Duke's shots (no hit). `aim_obstacle_kind`
    blocks the latter while its +0x2E0 is set (verified in game: the raised doorway barrier refuses the lock, the railing blocker still locks, and the lowered barrier locks again); `player_only` is taken
    to pass (unmeasured). Of 217 blockers in the game's world.scr files: 133 creatures_only, 16 player_only, 68
    neither. Each instance is logged once with its flags (`aim: blocker ...`).
  - **Blockers and pickups (the PASS rule is verified in game: the bot kills a Pig Cop behind a railing; the engine
    mechanism that lets the bullet through stays unread):** real bullets kill a Pig Cop behind a railing that the bot
    had refused. Reading finds no path by which a real bullet skips an active `blocker` or a pickup: both rays are the same `phys_ray_test` (duke's import at
    0x1018400c is resolved from the name "phys_ray_test", 0x100df359); the bullet's matchers stay accept-all
    (`mass_t` ctor via `light_t` at object_e+0xF4; the bullet ctors 0x1003a790/0x10038c90/0x10090b30/0x1008f170/
    0x1008af50/`dobj_base_ctor` add only deny_ptr(self); `weap_geagle_spawn_shot` only deny_ptr(owner)); the gather
    FUN_004127c0 tests only +0x2E0 and the two matchers (the per-object +0x268/+0x28c tests need a query self,
    and `phys_ray_test` passes 0); `blocker`'s vt+0x11c 0x100efe00 (named `worldent_ray_test`) is a
    two-sided triangle test with no flag; `proj_is_ignored` 0x10039860 is only the 5-slot ignore list; a pickup
    (actor branch) and a blocker (object branch) both end the bullet's budget in `proj_bullet_damage_hit`.
    What `blocker` is: world_e (type 0x200101), ctor 0x100b9930 sets user 0x8000, its init 0x100b9a80 adds user
    0x10000 or 0x20000 from +0xB0A/+0xB0B, and +0x2E0 follows its "active" pin (handler 0x100b9b90). The engine
    itself leaves world_e with user 0x10000/0x20000 out of its decal ray (FUN_1003b680) and the camera's sight
    matcher denies 0x8000|0x10000|0x20000: clip volumes, not scenery. **Rule:** class
    `blocker` and any actor with user bit 0x1000 (set by `powerup_base_ctor` 0x1005d051; FUN_101190b0 also sets it on a non-object class, excluded by the type-bit-2 gate) PASS, and only a
    blocking world_e cuts the ray (`aim_obstacle_cuts`); force fields, doors and the map still block. The
    shot replay below measures it; the log line `aim: refused by class` names every class that refused a target.
    Another possible cause of the railing case: the bot aims at the head, the manual shot flies at body height
    through a gap in the railing mesh.
  - **Shot replay (measures the rule):** each real Duke cast is replayed with the bullet's own
    filter (block = touch) and its first object compared with the monster that cast damaged
    (`offs_shot_verdict`); only `shotPassed` proves a pass. The replay cannot read the victim inside the cast (a
    version that did recorded none: 47 of 47 `shotNoHit` while dmgnum kept 35 of Duke's hits), because a monster's
    damage is not applied inside the cast: `proj_bullet_damage_hit` calls vt+0x198 = `monster_damage_send` 0x1002f3f0, which only
    `net_send`s message 0x82 (p3shared `entity_t::net_send` 0x1000d7a0 appends to the entity's +0xBC list); the
    receive FUN_10031990 calls vt+0x2FC at 0x10031b17 later, after the replay (next frame) had copied the cast.
    A cast is replayed `OFFS_SHOT_DELAY` (3) frames later, and a hit is matched to the oldest open cast along
    the damage's own dir (the shot's end - from times the frame time), not to the latest cast
    (`offs_hit_slot`; `offscreen:` line: hits == matched + orphan). Preconditions: the clip hook armed (Silent
    aim or Triggerbot on; `shotUnhooked` counts presses without it), victims from the monster damage hook
    whatever the Damage numbers switch.
  - Consequence, by design: a destructible in front takes each bullet until it breaks; the bot keeps
    firing. A PASS object in front (a dying or skipped monster, an object with neither type bit) also takes the
    real bullet without effect; it is transient or rare, so it is not counted as a blocker.
- Aim at a point inside the §0 box: for `monst_*` the box is the whole hit test (hitboxes §2). For the four
  mesh-tested bosses the ray must also cross the posed mesh, so prefer bone points (§3) there.

## 5. Pressing fire (triggerbot)

- The player turns **impulse 106 (`attack`) into mask bit 0x20** (0x1007d2af-0x1007d2c7 [A]), and the PS_* states copy
  it into `[player+0xDF0]+0xB04` bit 1 (weapons §1). Writing +0xE00 or +0xB04 ourselves is overwritten on the next
  read, so neither is the lever.
- **Order per frame** (render.md §1 + this read): subsys prio 0 = `inp_reset_impulses`, `inp_process`,
  **`rend_begin_frame`** → prio 1: the process handler prism!0x40c7a0 (vtable 0x434744) calls `wm_dispatch_process_input`
  0x424050 → each wm slot's input entity vt+0x154(channel = slot+0x14) = `player_read_input_mask` [A 0x40c7c7, D].
  **So inject in a `rend_begin_frame` hook: `inp_impulse_set(106, ch, 1.0f)`** (prism!0x406aa0, cdecl, accumulate +
  clamp [D]). It is read the same frame, on the main thread. Injecting from the end-of-frame overlay hook is too late:
  the next frame resets impulses before the read. For a tap: press for one frame and release for at least one frame
  (the mask is level, +0xE04 gives the edge). The Golden Eagle needs that tap on every shot (weapons.md open questions): the triggerbot pulses it and holds the automatics. The Pulse Cannon fires on release, so it is tapped
  on its own charge +0xCBC (weapons.md section 10). The X-3000 fires on every held frame, so it is held (section 12).
- The reader returns early unless `world+0xC8 & 8` and `world+0x14 == 0` (0x1007d27a-0x1007d28a [A]; whether that holds when paused or in a menu is unmeasured). The channel is `wm slot+0x14` (expected 0).
- **Golden Eagle presses** (log `semiPresses=284 pressShot=6 pressNoShot=278 latchUnread=0`): `pressNoShot` counts
  presses the weapon accepted while its refire gate (+0xBF8) was still closed; none were unbound or blocked. Fire
  was steady, so this is expected, not a failure count. Shot-replay caveat: a second victim of one pierce cast can be
  matched to another open cast in the same direction within 3 frames; it only shifts `shotPassed`/`noHit`.

## 6. Triggerbot and one-hit kills: no double fire

Reported as "the Triggerbot double fires on a one-hit kill" (Shotgun, Fire rate 400%). It does not: every Pig Cop
took two Shotgun hits because one hit does not kill it. The suspicion was that a monster's damage lands a tick or
more after the cast (section 4b), so the lock would keep a target that is already dying and fire again. The game's
log of that session (Shotgun, `fire=400`, When = Silent aim locked) refutes it:
- `dmgnum: first hit kept -- Pig Cop (monst_pigoid1), damage 15, health 20.00 -> 5.00, kill=0`: the first bot
  shell left the Pig Cop alive. It needs ceil(20/15) = 2 hits.
- Locks #1-#4 were on four different Pig Cops. During those holds there were 8 Shotgun shots (`holds` rose
  only in those windows), with `offscreen: hits=8 matched=8` and `dmgnum: entered=8 kept=8 dead=0`. So it
  was exactly 2 hits each, and no hit landed on a monster at health <= 0. The other 2 shots of the session
  (shots 5 and 10, `holds` unchanged, `shotNoHit`) were Duke's own.
- Timing: each lock lasted about 34 frames of about 5.7 ms (194 ms) for its 2 shots. The damage latency is
  about 3 frames (section 4b). The second shell left well after the first hit had landed and the lock had
  read health 5.
- Reading [A/D]: `weap_shotgun_think` spawns **one** `actor_proj_bullet` per shot
  (`weap_shotgun_spawn_shot` 0x1005bb30; its only call is at 0x1005b824, once per +0xC0C flag). The damage is the bullet key
  `proj_damage` (string 0x1015b11c) = weapon **+0xC18**, doubled while the game's float +0x223ec (on
  `[0x101869dc]+0x10`) is > 0 (0x1005bc69-0x1005bcad; the same test is in every weapon's spawn). This looks
  like a damage power-up timer (not established). `proj_bullet_damage_hit` 0x1003ad90 passes bullet +0xBC0
  (times the budget when it is < 1), and `monster_apply_damage` 0x1002f560 subtracts it from +0xB84 with no
  location or distance factor. **A manual shell deals the same 15**, unless that doubling is active (30 >= 20:
  a one-hit kill).
- Diagnostics: with **Debug log** on, `aim: shot t=.. by triggerbot|Duke, <weapon>, target <p> hp=..` (one line per
  shot, while the Triggerbot runs) and `dmgnum: hit t=.. <class> <p> health a -> b (damage d), kill=k` (one line per
  hit of Duke's, whatever the switches). Two bot shots on one target with a first `kill=0` mean the second shot was
  needed. A bot shot logged with `hp <= 0`, or a hit with `dead` rising, would be the late-damage refire. The
  triggerbot line ends with `botShots`/`dukeShots`.
- No "pending until the hit lands" rule exists: it would not change this case, because the second shot comes ~190 ms
  after the first, long after the hit landed.

## 7. Bosses: what can take damage now

**The helicopter's core never takes damage; its three parts do, each behind its own gate.** The walk sees only the
core as a `boss_*`, so locking it would waste every shot: the candidates are the parts while their gate is open,
never the core. There is **no generic per-actor "can take damage now" flag**: every boss has its own
vt+0x198 with its own test, so each is read and special-cased below (pure rules in `bossrule.h`).

- **The parts** [D 0x10006e30, the boss init]: three `boss_porkchopper_child` entities (factory 0x100db990,
  ctor 0x10010d80: actor base 0x10090b30, vt 0x10127008, alloc 0xB38), spawned with `spawn_entity_autoname`
  and named `porkchopper_vulcan`, `porkchopper_wing`, `porkchopper_engine`; parent slots **+0x15B8 / +0x15BC
  / +0x15C0**, back link **child+0xB1C = parent**. Each gets +0xAF2 = 1 and +0x2E1 = 1 (mesh-tested, hitboxes
  §2), the boss's own "dimensions" (all four the same size), and **+0xB2C = its centre bone** in its own model
  (`bone_child_{vulcan,wing,engine}_center` through skmod_get_bone_index). **Their get_classname is
  "actor_mover"** (vt slot 1 = 0x10012610 -> string 0x10165d00; `dumps/duke_base.classes.txt`), so the class
  table never names them: the `ents:` line's `bosses=1` is the core, the parts count as `other`. The ESP does
  not draw them (not built; proposal below).
- **The core is immune, always** [A 0x100094a0]: boss_porkchopper vt+0x198 never touches health; it only
  freezes the gun in state 2. Its health **+0x145C** (def `health`) is lowered only by the parts.
- **The parts' gates** [D/A `porkchild_think` 0x10010ed0, created in Ghidra; `porkchild_damage` 0x10010e70]:
  the part's vt+0x198 accumulates the damage at +0xB34 (amount > 0, explosions of class `bdo_ex` excluded);
  the think switches on the name byte **+0x10** ('e', 'v', 'w') and applies it only when:
  - **wing** (0x10010f3c): parent byte +0x1468 set and **health > drop_pig_health** (+0x14B4); health -= damage;
  - **engine** (0x10011bfc): +0x1468 set, **pigs left +0x14BC <= 0** and health > 0; health -= damage;
  - **gun** (`vulcan`, 0x100116ae): byte +0x1570 set, +0x1468 set, **gun health +0x1534 > 0 and state
    +0x146C == 3**; it has its own health and revives (+0x1590 `vulcan_revive_time`).
  So: wing first (down to drop_pig_health), the Pig Cops drop, then the engine. The gun is optional.
- **Other bosses** (each vt+0x198 read):
  - `boss_mechnukem` (Robo Duke body) and `mechnukem_crane`: vt+0x198 = 0x10001c40, a bare `RET 0x14`: **never
    damaged** by anything that calls it. Caught generically: a target whose vt+0x194 is
    `actor_damage_from_info` forwarding to a bare RET 0x14 is skipped (`noHandler`).
  - `boss_mechnukem_head` 0x10005760 (created in Ghidra as `mechhead_damage`): refuses in states **+0xF4C 2,
    4, 5, 7, 8** and while **+0x10C0 == 2**; health +0x1090.
  - `boss_queenroach` 0x10013b00: takes health +0x1690 only while it is > 0 **and the byte +0x16B8 is set**;
    each phase threshold clears +0x16B8 (an immune phase between stages).
  - `boss_wozma` 0x10017520 (`wozma_damage`): only while the byte **+0xB4C** is set, and only from the
    attacker +0xB5C = `get_local_player` (0x100177f6).
- **Every boss, per entity.** **Only the helicopter has parts. No boss has a
  shield actor; a boss "blocks bullets" while its own gate refuses** (the bullet still spends its budget on it,
  `proj_bullet_damage_hit` 0x1003ad90), plus the arena `blocker`s below. Every boss class's vt+0x194 is
  `actor_damage_from_info` 0x1008b630. Bullet damage info (0x1003ad90): +0x18 = the bullet, +0x1C = its
  attacker (`proj_get_attacker_or_self`), the two fields the queen's and Wozma's tests read.

  | Entity (class) | Takes damage? (vt+0x198) | Gate, read | Links | Our walk |
  |---|---|---|---|---|
  | Helicopter core `boss_porkchopper` | never (0x100094a0) | -- | parts in +0x15B8/+0x15BC/+0x15C0, back link part+0xB1C | CAT_BOSS, `porkCore` |
  | Wing / Engine / Gun `boss_porkchopper_child` ("actor_mover") | yes, into the core's health (gun: own) | the three part gates above | from the core's slots | `parts[]`, `porkPart` |
  | Cockroach Queen `boss_queenroach` | yes (0x10013b00) | health > 0, byte +0x16B8, inflictor name not `qr_*` (her own barrels). +0x16B8 is cleared at `chase_health` and at each `car1..4_health` threshold (0x10013e8b..0x10014145, 0x10014919) and set again only by 0x10014d12 / 0x10016ac2 | none: her init 0x10012aa0 spawns no entity | CAT_BOSS, `queen` |
  | Robo Duke body `boss_mechnukem` | never (bare RET 0x14 0x10001c40) | -- | ctor 0x100011c0 sets user 0x800000 and +0x2E0 = 0; its track follow 0x10001630 sets +0x2E0 = 1 five seconds into the final transition | CAT_BOSS, `noHandler` |
  | Robo Duke head `boss_mechnukem_head` | yes (0x10005760) | not state +0xF4C 2/4/5/7/8 (5 = docked in the body, +0x18C zeroed by the state setter 0x10002cd0; 7 = dead), not mode +0x10C0 == 2. Mode 1 (linked to a `mover_elect`, +0xF54) takes damage; `cancel_link` damage breaks it | +0xF30 = the body (search `t=2 Y=0x800000`, init 0x10002330); spawns 3 `jet_exhaust` | CAT_BOSS, `mechHead` |
  | Mechanical Crane `mechnukem_crane` | never (bare RET 0x14) | -- | init 0x10018170: four ropes, no damage path | CAT_BOSS, `noHandler` |
  | Wozma `boss_wozma` | yes (0x10017520) | attacker == +0xB5C (the local player, written each think 0x100177e0) and byte +0xB4C, set by the init 0x10017330 and cleared only at death | init spawns none | CAT_BOSS, `wozma` |

  - **Forwarders:** of the 219 registered classes only `boss_porkchopper_child` and `actor_proj_explo`
    answer a classname other than their own (`actor_mover`, `dumps/duke_base.classes.txt`); no other class
    carries a boss's damage. `boss_parts_spec` (bossrule.h) holds the helicopter as the one parts row.
  - **Shields:** none spawned by a boss. `actor_forcefield` is Duke's powerup effect; the "shield" mesh groups
    of `monst_pigoid1` (0x10023920) and `monst_securitymech*` (0x10029c50) are cosmetic, and
    `monster_apply_damage` 0x1002f560 has no direction test. The boss maps' barriers ([S], world.scr):
    e01p03_a02 `blocker1` (no flag: blocks everything while up); e04p03_a02 five `player_only` blockers along
    the train (`blocker_first`, `blocker_car1..4`); e08p03_a02 `blocker1` (no flag, `start_active:false`,
    raised by an area_switch at the fight) and `blocker2` (`player_only`). These already classify by section
    4b: a for-everyone blocker BLOCKS while +0x2E0 is set, `player_only` passes (**unmeasured**).
  - **What already blocks:** an immune boss or part (`immuneNow`), the body and the crane (their vt+0x198 is
    a bare RET, so `actorDamages` is 0), any for-everyone blocker. Silent aim locks nothing behind them.
- **Rule:** both Triggerbot modes use silent aim's test (`aim_target_ok`); the walk's `immune` alone would fire
  at Robo Duke's body or the crane.
- **ESP (parts not drawn):** the parts it would draw are the helicopter's Wing, Engine and Gun at
  their centre bones, labelled open/immune. No other boss has parts; the Robo Duke head is already drawn as a
  boss of its own (in state 5 its +0x18C is the docked, local position).
- **Rule:** silent aim (and so the Triggerbot's Locked mode, which fires on its lock) skips any
  candidate whose gate refuses now, by reason, and adds the helicopter's parts (read from the parent's slots
  and validated by the back link) as candidates aimed at their centre bone. A boss or part that refuses now
  also **blocks** the bullet ray (`AimObsFacts::immuneNow`): `proj_bullet_damage_hit` still spends the budget
  on it. Only a refusal that was read excludes a target; anything unread stays a candidate. Crosshair mode never counts an immune boss as a target and counts an open part as one (`crosshair_enemy`).
- **Log:** `aim: ... immune=N` (seen == dead + immune + range + screen + capped + cands) and `aim: immuneBy
  noHandler= porkCore= porkPart= queen= mechHead= wozma= | partPoints= partFallback= partFar=`, all cumulative;
  `ents: ... parts= immune= partSlots= partBad=`; and one `ents: Helicopter Wing <p> takes damage now|immune
  now (health, drop_pig_health, pigs, state, gun health, gun out, active)` line each time a part's gate flips.
- **Boss fight, verified in game:** in the helicopter fight with Silent aim and the Triggerbot (Locked) on, the
  lock goes to the wing, then (after the Pig Cops drop) the engine, never the core; the boss's health falls.
  The log should show `porkCore` rising, `partPoints` rising with `partFallback` and `partFar` at 0, and the
  `ents:` flips in the order wing open -> wing immune -> engine open. A `refused by class boss_porkchopper`
  count means the core's mesh stood in front of a part. Per boss: `aim: bosses
  seen/immune/locks/crosshair porkCore=a/b/0/0 wing=... engine=... gun=... queen=... roboBody=... roboHead=...
  crane=... wozma=...` (cumulative; a row's immune share is its gate). `locks` and `crosshair` stay 0 on
  porkCore, roboBody and crane. Queen: `queen` immune rises between phases and locks stop until she is
  open again. Robo Duke: locks only on roboHead, never while it is docked. Wozma: locks until it dies.

## 8. G.L.O.P.P. Ray: reach and transform filter

The triggerbot must not fire the ray at enemies beyond its 290-unit beam or at animals it already made (weapons.md
section 8).
- **Range for every other weapon:** silent aim (and so the Locked mode) compares the eye-to-box-centre distance with
  the Aim range slider (`aimRangeM` · 52, `select_target`). Crosshair mode applies no range at all: the bullet ray
  runs 5000 plus the on-screen test (`crosshair_enemy`). There is no per-weapon range.
- **Rule (ray held only):** reach = `glopp_reach(Aim range, live +0xCC8)` = the smaller of the two (290 when
  the beam read fails). A target must be within reach and pass `glopp_gate` (aim.h). That gate is
  monster_apply_damage's type-4 tests in its own order: monster handlers fingerprinted (vt+0x198
  monster_damage_send, vt+0x2FC monster_apply_damage), not an animal (0x1000), alive, morph not busy, hyper
  not on, not a hyper monster (0x800), morph block present (0x80000 and +0xDC0), and no transform started
  (`morphing`, [+0xDC0]+0x70, weapons.md section 8). Bosses
  and parts fail the handler test (`notMonster`). Silent aim's pick applies range then gate (bucket `glopp`).
  Crosshair mode refuses a hit past the reach (`gloppRange`). An enemy the ray cannot turn is no target
  and **blocks** the ray in both paths, because the beam acts on the nearest object only (`forced`). That
  includes an enemy that is transforming: it keeps absorbing the beam (weapons.md section 8).
- **Bosses are never ray targets.**
- **The handler check accepts a detour's head.** A first version refused every candidate as `notMonster`; the log's
  only class was `vt 0A1B1D28` = duke_base 0x0A070000 + 0x141D28 = **`monst_pigoid2`** (vt 0x10141D28). Its
  vt+0x198 is 0x1002f3f0 and its vt+0x2FC is 0x1002f560, exactly the fingerprinted pair [read]. The class was
  fine; the check was wrong. dmgnum had detoured that same 0x1002f560 (`dmgnum: armed at 0A09F560
  (duke_base+2F560)`): its JMP replaces bytes 0..6, and the gate wanted the stock `6A FF 68` head. The
  apply fingerprint (`glopp_fp_monster_apply`, aim.h) checks +7..+0x41 and accepts the stock head or a
  detour's `E9`. A class whose handlers are not recognised stays a candidate, counted
  `unreadClass`, as are unreadable fields (`unread`). Only a read refusal excludes (`glopp_refuses`). Bosses
  and parts are `boss` (their handlers read, section 7).
- **Reach is measured from the eye**, but the beam starts at the muzzle (vt+0x214). The gap is unmeasured
  (the muzzle offset). A target just inside 290 from the eye may be just outside from the muzzle.
- **Log:** `aim: glopp frames= beam= beamUnread= | pick gloppRange= gloppImmune= | triggerbot gloppRange=
  gloppImmune= | forced= classes= classesFull= | immuneBy unread= notMonster= animal= dead= morphAnim= hyperOn=
  hyper= noMorph= morphing=` (cumulative; reasons are `unreadClass unread boss animal dead morphAnim hyperOn
  hyper noMorph morphing`). Pick refusals are the Locked mode's; triggerbot ones are the
  Crosshair mode's. `aim: G.L.O.P.P. beam <p> reaches 290` once per beam object. `aim: glopp class vt <p>:
  monster handlers|other handlers` once per class. The `aim:` line gains `glopp=` (seen == dead + immune +
  glopp + range + screen + capped + cands).
- **Ray range, verified in game:** with the ray held and the Triggerbot on (each mode): no fire at an enemy farther
  than about 5.6 m, or than the Aim range if shorter. A monster in reach is held until it turns, then never
  fired at again (the animal). The log shows `pick gloppRange` / `triggerbot gloppRange` rising while enemies
  stand beyond the beam, and `immuneBy animal` rising once the first one turns.

## 9. Roach eggs as targets

A wired roach egg (one the map can hatch) is a target for silent aim and both Triggerbot modes; an unwired one is
not, though it still breaks under fire. Verified in game. Facts and citations: [entities.md section 8](entities.md).
- The walk names `roach_pod` CAT_ENEMY "Roach Egg" with its own health (+0xC4C of 25) and `egg` state. An unwired
  or unread egg carries `immune = BOSS_IMM_EGG` (bossrule.h, `eggIdle`), so `aim_target_ok` refuses it in the pick
  and the Crosshair mode. It is **not** an `immuneNow` obstacle: its vt+0x198 takes bullet damage, so the ray
  still classes it destructible (section 4b) and a target behind it stays reachable.
- With the G.L.O.P.P. Ray held, no egg is a target (`GLOPP_IMM_EGG`: 0x10072770 returns for type 4) and an egg on
  the ray soaks the beam (`forced`).
- The egg is aimed at its box centre (no skmod handle is read for it: no head bone); dmgnum and AI awareness skip it.

## 10. Overkill: doomed enemies are no target

**Verified in game** (PRPG, Pulse Cannon, Assault Rifle burst, Shotgun).

Silent aim's pick and both Triggerbot modes skip an enemy whose death is already guaranteed by
damage on its way: pending >= health, with no margin, because `monster_apply_damage` 0x1002f560 subtracts the
amount unscaled (no armour, no difficulty factor; the receive 0x10031990 passes it straight through, [A
0x10031ad9-0x10031b17]) and a monster dies at health <= 0. Rules:
`overkill.h`; engine side: `overkill.cpp`. Monsters only (CAT_ENEMY with monster health, no egg): bosses and
parts keep their own handlers (section 7).

How each weapon delivers its damage:
- **Bullets** (Golden Eagle 5, Shotgun 15, Assault Rifle 4 per round; weapons.def): one `actor_proj_bullet`
  per shot, cast on its first think (byte +0xBEC, 1 from the ctor, cleared at 0x1003a9d7), damage +0xBC0 to
  the nearest object (section 4b). The damage is a net message (`monster_damage_send` 0x1002f3f0 sends 0x82:
  type, amount, dir, flags, attacker id) applied about 3 frames later (section 4b). This is the "damage already
  dealt" window.
- **PRPG rocket** (`actor_proj_prpg`, ctor 0x1004f220, vt 0x101303B8): damage +0xBC0 = 30 (proj_damage,
  doubled by the same power-up test as every weapon, `weap_prpg_spawn_rocket` 0x1005a4f0), **radius +0xBC4 =
  150**, life +0xC00 = 1.0 s, velocity +0x1C0 += dir·800. Its think 0x1004f480 counts the life down, casts
  `phys_ray_test(pos, pos + vel·dt, +0x220, +0x244)` and explodes at the first record unless it is a dying
  monster, then moves pos += vel·dt itself: **straight, constant speed (settled for the rocket)**. The
  explosion vt+0x220 0x1004f770 calls vt+0x21C = **0x10039080, splash only**: every actor (type 2, user 1)
  whose distance `FUN_1008bc30` is under the radius and that one of seven world rays sees (`FUN_10039430`)
  takes **(r - d)/r · damage**, type 9, through vt+0x194. The distance is 0 inside the box, else |d| minus the
  nearest box corner's projection on d (`ovk_splash_dist`). A direct hit is a splash at d of about 0.
- **Plasma** (`actor_proj_plasma`, ctor 0x1004c630): direct only, no splash. `proj_plasma_collide` 0x1004e260
  (vt+0x168, created in Ghidra) gives the actor it touches +0xBC0 (type 10); a full charge (+0xBD4 >= 1) not
  yet passed through a monster gives it at most 100 and flies on (`ovk_plasma_direct`). State +0xBE0 is 0 while
  flying. Its think 0x1004ced0 (created in Ghidra) adds a ray only for mesh-tested actors; the physics touch
  does the rest. Life: destroy_time = now + 5 s (`weap_pcannon_discharge` 0x100590f0). Its flight path and
  matchers are **not read** (taken as straight, with the rocket's stop rule).
- **Pipebomb** (`actor_proj_pbomb`): **remote**. Duke's press runs `FUN_1007f090` (from `weap_pbomb_fire_logic`
  0x100574e0), which finds every live bomb by the key `s=%d?t=%d?Y=%d` (1, 0x400), merges near ones and calls
  vt+0x210 0x1004b210 (byte +0xBFC = 1); the bomb's think 0x1004aa60 detonates the same frame (`FUN_1004b220`:
  the same splash 0x10039080). So no damage is on its way until Duke presses, and a lying bomb is **not**
  pending (it is counted, `pbomb=`).
- **Burst:** the rifle's +0xC2C (5 per held frame, 0x10052837) minus the rounds fired; times proj_damage
  +0xCE4. The power-up doubling is not read here (the rounds' +0xBC0 is), so the burst may undercount. Rounds
  fire only in the attack state (+0xB10 == 6, `weap_arifle_fire_logic` 0x10052800); out of ammo the logic zeroes
  +0xC2C itself (0x100528f4) and switches weapon; the rifle has no reload. So the committed rounds are +0xC2C
  only while +0xB10 is 6 (`ovk_burst_left`), and none once the weapon leaves the hand.

Mechanism:
- **Dealt:** a detour on `monster_damage_send` (armed from a live monster's vt+0x198 when silent aim or the
  Triggerbot first runs; fingerprint `kOvkSendFp`, 6-byte trampoline) notes each send that will take health
  (`ovk_send_counts`) in a ledger with the health at the run's first send. It notes **every attacker's** send
  (Duke, monsters, barrels): any of them is still a guaranteed death. monst_fly (0x10027870) and the ratoids and
  roachoids (0x100290b0) reach 0x1002f3f0 through a wrapper that ends in `JMP 0x1002f3f0`:
  a monster whose slot is not the stock entry is skipped and the next one tried (`ovk_send_slot_kind`), so the
  detour still covers them; only a refused install is final. Unapplied = sent - (health then -
  health now) (`ovk_unapplied`); a run ends when all of it landed or 500 ms after its last send. The G.L.O.P.P.
  handler check (section 8) accepts the detour's head (`ovk_fp_monster_send`).
- **In flight:** once a frame, Duke's live projectiles (the engine's own key above; attacker +0xB30 = Duke, set
  by `proj_set_attacker` 0x10039980 from every weapon's spawn) are walked: at most 256 read, at most 32 of
  Duke's get rays. An uncast bullet takes the
  bullet's own ray; a rocket and a plasma ball take the full ray to their end of life and stop at the first
  record `ovk_proj_stops` accepts (the rocket's block matcher, its init 0x1004f300). Splash sight: one
  world-only ray per enemy (the engine tries seven), at most 32 a frame.
- **Order and causes:** dealt, then inFlight, then splash, then burst; the cause is where the sum reaches health.
  The burst's rounds are credited to the target the Triggerbot pressed the rifle at, and only the Triggerbot
  counts them: **silent aim keeps that lock**: while the burst has rounds left its target goes first in the pick's
  ray walk (`aim_cand_front`, `burstSticky=`), else the committed rounds would fly at the next target and the
  first would live. Duke's own clicks credit no burst.
- **Log:** `aim: ... doomed=` (pick skips) and `overkill: armed= sends= sendSkipped= sendExpired= ledger= | proj
  bullet= rocket= plasma= pbomb= fresh= predicted= noVictim= los= losHidden= losCapped= | doomed dealt= inFlight=
  splash= burst= | withheld= (by cause) pickSkipped= burstFrames=`, all cumulative. `doomed` counts enemies when
  they turn doomed; `withheld` counts shots the Triggerbot would have fired at a doomed target: its fire
  decision on a shadow delay machine fed the target before the doom test (`ovk_withheld_step`: every press of a
  semi-automatic, one per hold of an automatic).
- **What it generalises:** section 6 found no double fire (the second Shotgun shell was needed); this rule keeps
  that: 15 sent against 20 health is not doomed, so the second shell still fires.
- **Checks:** PRPG: fire at a Pig Cop group; once a rocket is on its way to a kill, the lock
  moves to the next one (`doomed inFlight`/`splash` rise). Pulse Cannon: the same with a charged ball
  (`inFlight`). Assault Rifle: one burst per Pig Cop (20 health = 5 rounds of 4), `withheld burst` rises, and the
  lock moves on after the burst. Shotgun: still two shells per Pig Cop (15 + 15 against 20), never a third.

## 11. Enemies the held weapon cannot hurt

**Verified in game** (SecurityMech immune label and targeting; Pulse Cannon taps in both modes).

No monster ignores a weapon by damage type. The SecurityMech ignores **bullets that hit it low**:
its own vt+0x194 drops a non-splash hit unless the hit point, in its own frame, lies above `10 · scale`.
Splash, plasma and the X-3000 always hurt it. The other refusal is roachoid3/B, which takes no damage at all in
state 0xB. Silent aim and both Triggerbot modes skip such an enemy, and the ESP marks it "(immune)". Rules: `weapimm.h`; engine side: `aim.cpp`.

How each weapon's damage enters a monster [D]:
- **Bullets** (Golden Eagle, Shotgun, Assault Rifle): `proj_bullet_damage_hit` 0x1003ad90 calls **vt+0x194(type 0,
  amount, info)**. The info is +0 dir, +0xC hit point (the collision record's +0x14), +0x18 inflictor, +0x1C attacker
  and **+0x20 splash byte = 0**.
- **Splash** (PRPG rocket, pipebomb): 0x10039080 calls **vt+0x194(type 9, …)** with **+0x20 = 1**.
- **Plasma** (Pulse Cannon): `proj_plasma_collide` 0x1004e260 calls **vt+0x198(type 10, …)**, skipping vt+0x194.
- **X-3000**: `weap_x3000_fire_logic` 0x1005c8f0 calls **vt+0x198(type 0, 1000)** on every monster.
- **G.L.O.P.P.**: vt+0x198 type 4, the morph counter (section 8, its own gate).
- vt+0x194 is `actor_damage_from_info` 0x1008b630 (forwards to vt+0x198) on every monster but the SecurityMech.
  vt+0x198 is `monster_damage_send` 0x1002f3f0, which filters nothing. It reaches vt+0x2FC =
  `monster_apply_damage` 0x1002f560, whose only type rules are: flag 0x200 ignores types 6/7/8 (no weapon of Duke's
  deals them); types 3/4 go to the morph counter; every other type takes health.

Per class (all 31 monster vtables and the bosses read: vt+0x194/0x198/0x2FC):

| class | bullets | splash | plasma, X-3000 | handler |
|---|---|---|---|---|
| `monst_securitymech1..3` | **only above the line** | yes | yes | vt+0x194 `smech_damage_from_info` 0x1002a9b0 (created in Ghidra) |
| `monst_roachoid3`, `monst_roachoidB` | none in state 0xB | none in state 0xB | none in state 0xB | vt+0x198 0x100290b0 |
| `monst_gatoroid1..3/B` | yes | yes | yes | vt+0x2FC 0x1001e120/0x1001ef70 wrap apply, no filter |
| `monst_fly`, `monst_ratoid1..3/B` | yes | yes | yes | vt+0x198 0x10027870: a pass-through `JMP` to the send |
| every other `monst_*` | yes | yes | yes | stock |
| `roach_pod` (egg) | yes | yes | yes | drops types 3/4 only (section 9) |
| bosses | their own gates | | | section 7 |

- **The SecurityMech's line** [A 0x1002a9b0-0x1002a9f6]: the handler runs the info's hit point through the inverse
  world matrix **+0x134** (p3shared `mult_point_matrix`, the pointer selected at 0x10009c9e to 0x10016ec0:
  row-vector, out[j] = Σ p[i]·m[4i+j] + m[12+j]). It then sends the damage on if **byte +0x20 is set**, or if
  **`[+0xC08]·10.0 < local.y`** (+0xC08 = the def's `scale`, desc reader 0x1002c58a; 10.0 at 0x10126cd4, read
  through the handler's own operand at runtime). Otherwise it only alerts and turns the mech (state 0xD). Defs
  (monsters.def): dimensions <35 45 35>, scale 1.0 / 1.12 / 1.25 for tiers 1/2/3. Where its origin lies within its
  box is **not read**, so how much of its body is above the line is unmeasured.
- **The roachoid shell** [A 0x100290b0]: `cmp [ecx+0xc68],0xb; jnz; mov [esp+8],0`. The amount is zeroed, so
  nothing reaches health. What sets state 0xB is not read.
- **The gate** (`wimm_gate`): the held weapon's route against the class, in the engine's order (vt+0x194, vt+0x198,
  vt+0x2FC). The classes are recognised by their handlers' bytes, read once per vtable (`aim: wimm class vt …`).
  Anything unread stays a candidate: `unreadClass`, `unread`, and `noHit` (a SecurityMech with no hit point).
  The SecurityMech's verdict needs the hit point. Silent aim takes it from the target's own record on the
  bullet's ray (`shot_reaches`), and the Crosshair mode from its line of fire. A candidate the ray reaches but the
  weapon cannot hurt is skipped (`wimmune`), and the next nearest is tried. An immune monster in front of a target
  **blocks** that weapon's shot (`forced`), since the bullet, the rocket and the plasma all stop on it.
- **ESP:** each frame (Silent aim and Triggerbot on or off), every live monster is judged for the held weapon. A
  SecurityMech's hit point is where the eye-to-box-centre line enters its box. A refused enemy's label reads
  "(immune)" (alone with Names off) in the dim label colour.
- **Log:** `aim: weaponImmune pick= triggerbot= forced= esp= classes= classesFull= rowsFull= | by lowHit= shell=
  type= … <class>/<weapon>=n` (cumulative). The `aim:` line gains `wimmune=` (cands == hidden + wimmune + locks +
  untried). `aim: wimm <name> <p> met at (…) with the <weapon>: local y a against scale s * k 10 = t -> ok|lowHit`
  (the first 16 bullet hits on a SecurityMech, the measurement for the origin question). Check: with Silent aim and the
  Triggerbot on, face a SecurityMech with the Golden Eagle. If its body is below the line, no lock, no fire, and an
  "(immune)" label. Switch to the PRPG or the Pulse Cannon: the label goes and the bot fires. The `aim: wimm … met at` lines give where bullets met it against the line. Bullets that do hurt it (`-> ok`)
  show which part of it lies above the line.
- **Pulse Cannon taps** (weapons.md section 10): with the Triggerbot on (each mode) and the Pulse Cannon held,
  enemies under the condition take quick uncharged shots, small balls about 8 a second (`pcCadence` rises). The
  `aim: triggerbot` line shows `pcPresses` = `pcCharged` (+ `pcNoCharge`, the cooldown or no ammo) and
  `pcReleases` = `pcShots` (`pcStuck` 0). `overkill: … plasma tapExpect=11.2 seen=11.2..` (22.4 under the damage
  power-up). A Pig Cop takes two taps and is never tapped a third time.

## 12. X-3000 and the Triggerbot

**Verified in game.**

Both Triggerbot modes hold the X-3000. `tbot_weapon_fires_on_press` includes it, and `tbot_frame` applies that
test after either mode picks its target, as with the Pulse Cannon (section 11).
- **How it fires** (weapons.md section 11): on every frame with the attack bit held, 1000 damage to every
  monster that is drawn and in view. There is no edge, no refire gate and no ammo, so the press is a plain
  hold (`tbot_pulse`, not semi-automatic). There is no human-rate cap, because a human also holds the button
  and the engine fires at the same per-frame rate. Overkill ends the hold: the first held frame's sends go
  through `monster_damage_send` (type 0, 1000 counts, `ovk_send_counts`), so the dealt ledger dooms the
  target in that same frame and the bot releases. No overkill row is needed.
- **Target rule** (`x3000_hurts`, aim.h): the X-3000 only reaches entities with user bit 0x20 (the snapshot's
  `hashp`, eggs excepted) that have +0xAF1 set and health > 0. So with it held, a boss, a helicopter part, an
  egg or an undrawn monster is no target (`x3000Refused`), and an unread +0xAF1 keeps the target
  (`x3000Unread`). Weapon immunity already had its row (`wimm_spec`: type 0 straight into vt+0x198). It
  skips the SecurityMech's low-hit line, and the roachoid shell zeroes it.
- The menu hint for Crosshair names only the Pipebomb.
- **No line of sight:** the weapon ignores walls, so with it held, in either
  mode, `x3000_scan` (aim.cpp) takes the first snapshot entity that `x3000_hurts` accepts and overkill does not
  doom (`x3000_pick`, aim.h); no crosshair ray, no lock. +0xAF1 is read once per walk into `EntSnap::drawn`
  (inside the walk's validated `kMonsterSpan` 0xB90), so no guarded read per entity. Counters: `x3000Refused`
  = held frames with no candidate, `x3000Unread` = monster entries whose `drawn` is unread (an invariant 0),
  `x3000Scan` = frames the scan found a target. The Crosshair hint says so.
- **Drawn this frame and on screen:** trusting +0xAF1 alone, the bot fired after freecam at far monsters Duke could
  not see, and at monsters just outside the screen. +0xAF1 is written only inside the actor
  draw FUN_10090260 (at 0x10090316), and only when it changes: `(+0xAF0 && rend_sphere_visible(+0x4AC sphere))`.
  There is no draw timestamp, so a monster the renderer stops drawing keeps its last value, and what the freecam
  drew stays "in view". `rend_sphere_visible` tests a bounding sphere, so a monster whose sphere touches the frustum
  counts while its centre is off screen. A detour on FUN_10090260 records the pass each actor was last drawn in
  (`X3kDrawn`, an open-addressed table keyed by the pointer, cleared on a new player or world; it writes no engine
  memory). It is found by a one-time scan of duke_base's executable sections for its first 135 bytes
  (`kX3kDrawFp`, up to the absolute `call [rend_sphere_visible]`; the 6-byte head is compared as the stock bytes
  or a detour's E9; exactly one match arms it). It is armed the first time the Triggerbot runs with the X-3000 held,
  and records only while the Triggerbot is on. A candidate also needs: drawn in the pass just completed, not during
  freecam or on the first frame after it (`x3000_fresh`), and its box centre projected with the game's camera (the
  Triggerbot's Ctx) inside the viewport (`x3000_on_screen`). Without a Ctx there is no target, and the clip bypass
  does not widen this. Walls stay ignored. Counters: `x3000Stale`, `x3000Offscreen`, `x3000NoCtx`, `drawArmed`,
  `drawRecords`, `drawFull`.
- **Not changed:** when the bot or the player fires, the game's own X-3000 still hits every monster with a stale
  +0xAF1, including ones out of view. That is the game's behaviour, and the bot only chooses when to press.
- **Eggs: the X-3000 cannot hurt them** (a log showed the bot holding fire on an egg with nothing happening;
  `x3000Eggs=2469` picks). The game's search takes user bit 0x20 and health +0xB84 (weapons.md section 11); a roach
  egg is built on `actor_model_ctor`, sets only user bit 0x10000000 (ctor 0x10072280) and keeps its health at +0xC4C,
  so the X-3000 never reaches it. The panel's walk marks eggs `hashp = 1` with +0xC4C as health for the ESP and aim
  (entities.cpp `egg_fill`), which is why a scan could pick one. There is no "X-3000 fires at eggs" option;
  `x3000_hurts` excludes eggs.

## Open questions

- **U1** A pitch write to player +0x1A8 at the frame hook is untried: log +0x1A8 at the next hook plus the dir the
  detour receives from the original. Does it stick, does any state overwrite it per tick? (The yaw write in PS_idle
  works, mouse-aim.md section 9.)
- **U2** Whether the detour covers every shot: count detour hits against the shots detected by weapons.md §2 polling
  (+0xB18, pcannon +0xCC8) per weapon. Expected equal (gray: once per frame while held).
- **U3** Muzzle parallax: for prpg/pcannon/pbomb/gray, the distance between `weap_get_fire_origin` and the spawned
  projectile's +0x18C.
- **U4** Spawn-to-hitscan window: a new bullet's +0xBEC at the end-of-frame hook (1 = not yet cast). Matters only
  for the rejected post-spawn rewrite.
- **U5** Monster velocity: vt+0x190 against (pos_now - pos_prev)/dt for walking monsters over 60 frames.
- **U6** The plasma's flight path is unread: log +0x18C of one plasma over 10 frames.
- **U8** The input channel and gates: wm slot 0 +0x14 (0x4A1C70+0x14) is expected 0; the reader gate fields when
  paused or in a menu.
- **U9** Head bone on monsters: `skmod_get_bone_index(+0xAE4, "Bip01 Head")` once per class is expected >= 0 for
  Biped rigs; the classes that return -1 use the box fallback and are not listed. One `skmod_get_bone_transform`
  call is not timed.
- **U10** That the head-forward-y > 0.5 look-up branch fires only while `PAS_look_up` plays (log which branch the
  original takes, from the out value).
- Bosses (section 7): that a `player_only` blocker (the queen's train) passes Duke's bullets, and where the Robo Duke
  head's +0x18C points outside state 5 (the aim uses its box centre).
- G.L.O.P.P. Ray (section 8): the eye-to-muzzle gap, and whether every turnable monster class passes the handler
  fingerprints (`aim: glopp class vt` lines).
- **U13** Overkill (section 10): whether an uncast bullet is ever seen at the frame hook (`fresh=`), the plasma's
  path, and how often the one sight ray misses a splash the engine's seven rays would see (`losHidden=`).
- **U14** The SecurityMech's origin within its box, so how much of its body is above the line (section 11).
- Whether the +0x223ec doubling is a damage power-up timer (section 6), and what was seen as a manual one-hit kill
  in the double-fire report.
- The engine mechanism that lets a real bullet pass a `blocker` or a pickup (section 4b).
