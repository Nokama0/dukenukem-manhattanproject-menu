# Weapons: fire rate, reload, spread

Every gate is time-based, in seconds (world clock `world+0x40` or the skmod animation clock), so
scaling a delay scales the fire rate. Only the Golden Eagle has a clip and a reload. Only the assault rifle has
spread: a script-driven cone (`cone_ang_start/end/time`). The held weapon is **`player+0xDF0`**. Each weapon needs
its own fire-rate lever (table in §2). Four of them are fields the engine rewrites on every shot, so they are safe
to overwrite right after a shot. The menu ships the fire-rate, reload and no-spread levers of section 6, verified in game.

Binary: duke_base.dll (base 0x10000000). Tags: **[D]** decompile, **[A]** disassembly, **[X]** xref/scan, **[S]**
string/script file, **[G]** Ghidra inline script. The functions named below are named in the Ghidra project. Method: Ghidra MCP.
Data: the game mounts `duke\base\db_e01.zipfs` `scripts/weap/weapons.def`. The loose copy under `DeepFreeze\base\`
belongs to the level editor and has different pistol numbers (0.4 s/7 rounds/1.0 s), so ignore it [S, file diff].

## 1. Common weapon layout and clocks

- Shared fields (all `actor_weap_*`) [D weap_update_time_and_owner_state 0x100509f0, weap_tick_anim_and_events
  0x10050a70]: **+0xB04** input bits written by the player (bit 1 = attack held, set via `[player+0xDF0]+0xB04`),
  +0xB10 state (1 idle, 6 attacking), **+0xB14 now** = `(float)world+0x40` (s), **+0xB18 last-fire time**,
  +0xB1C idle_delay, **+0xB28 anim-done flag** (set when `skmod_get_time() >= +0xB2C`), **+0xB2C current anim end
  time** (skmod clock, written by every `weap_play_anim` 0x10050bb0 = vt+0x21C, from `skmod_get_anim_parm(..,1) - 0.05`).
- Timed events: 5 slots at +0xB90 (stride 0xC: active, id, due time). `weap_schedule_event` 0x10050f50 sets
  due = skmod_get_time()+t, and `weap_dispatch_timed_events` 0x10050fc0 calls vt+0x20C(id) [D].
- Script data: each class's vt+0x230 = `*_parse_key(name, def)` stores `<class>_desc` keys into fields [D/G, list in §3].
- The logic runs once per rendered frame: vt+0xB0 (think) = `entity_t::pre_draw` + fire logic [D].
- **Held weapon: `player+0xDF0`** [A]. The ctor zeroes it (0x10076402). The weapon switch FUN_1007a840 commits it
  from the pending weapon `+0xDF4` (0x1007a8f5). FUN_1007a300 clears it (0x1007a378). ~15 player-state functions
  write attack bits through it (e.g. 0x1007fe43, 0x100821a9). Identify the class by vtable (geagle 0x101317E8,
  shotgun 0x10132380, x3000 0x101325D0, arifle 0x10130EA8, pbomb 0x10131C80, gray 0x10131A30, pcannon 0x10131EE0,
  prpg 0x10132130; dumps\duke_base.classes.txt) or by vt+4 `get_classname()`.

## 2. Per-weapon table

| weapon | fire logic | refire gate (field, source) | clip / reload | spread | fire-rate lever (engine-written) |
|---|---|---|---|---|---|
| geagle (pistol) | weap_geagle_fire_logic 0x10055120 | `now-B18 >= +0xBF8`; +0xBF8 := +0xC2C `auto_fire_time` (0.2 s) after each shot (0x1005528e, 0x1005533a). Skipped when state==1 (idle) | clip 10 +0xC3C, count +0xC04, reload §4 | none [D entities §5] | **+0xBF8 := +0xC2C/m** after a shot |
| shotgun | weap_shotgun_fire_logic 0x1005b940 | **animation-gated**: fires when state!=6 and held; returns to idle when +0xB28 (anim end). `auto_fire_time` 0.6 is parsed into +0xC1C and **never read** [X] | none (pump = event 1 at +0.4 s, weap_shotgun_on_event 0x1005be90) | none: one `actor_proj_bullet`, dir straight from vt+0x210 (weap_shotgun_spawn_shot 0x1005bb30) [D] | **+0xB2C** (anim end) shortened after the shot, open |
| arifle | weap_arifle_fire_logic 0x10052800 | `now-B18 >= +0xCE8` (interval, 0x1005284d). Fires **ftol(int +0xC28 · elapsed)** rounds (0x10052870-0x10052888) and keeps the remainder with +0xCE8 again (0x1005289e). Entering state 6 back-dates B18 by +0xCE8+0.001 (FUN_10052440), so the first frame fires. **Burst:** a held frame sets +0xC2C = 5 (0x10052837), the gate also passes while +0xC2C > 0, and weap_arifle_think subtracts the rounds fired (0x10052358): a click fires 5 rounds | none (`spin_up_time` +0xBFC = 0) | **cone** §5 | **+0xC28 := round(rps·m), +0xCE8 := 1/new rps** (raised by an ulp until rps·interval ≥ 1). Both are parse-time only, so hold the stock values and restore them. Scaling only +0xCE8 broke the rifle: ftol(12 · 1/18) = 0 rounds takes the no-ammo branch (+0xC2C = 0, vt+0x204 weapon switch) |
| pbomb | weap_pbomb_fire_logic 0x100574e0 | **animation-gated** (state 1 → throw anim; launch event at `*_launch` 0.2-0.42 s) | none | direction none; throw speed 580+rand%40 (or 330+rand%40) and rand spin (weap_pbomb_launch 0x100578f0) | +0xB2C, clamped ≥ launch time, open |
| gray (G.L.O.P.P.) | weap_gray_fire_logic 0x100561e0 | continuous beam while held. Ammo accumulator +0xC00 += dt at `proj_ammo_rate` +0xC04 (30/s) | none | rand = view shake only | n/a (beam, **290 units long**; it turns monsters into animals, no health damage: section 8) |
| pcannon | weap_pcannon_fire_logic 0x100588a0 | fires **on release**: charge +0xBF4 += dt, charge = min(+0xBF4/4.0, 1); the first frame without the bit discharges (section 10). **No refire gate on a normal shot**: the 2.0 cooldown +0xCC8 (0x1005954a) is written only on discharge's +0xC41 branch (an overcharge that hurts Duke, type 7), and the fire logic subtracts dt (0x10058901) | none | rand = view shake only | +0xCC8 := 2.0/m after that branch only, so it **does not act on normal shots** (charge speed: optional, +0xBF4) |
| prpg | weap_prpg_fire_logic 0x1005a1e0 | `state==1 or now-B18 > 0.5` (**0.5 is the shared .rdata constant DAT_10126ce0**, never patch it). B18 := now at 0x1005a364. Two rockets per shot (FUN_1005aaf0 x2) | none | none in the shot path [D]; rand in FUN_1005abe0 is open (particles?) | **back-date +0xB18 := B14 - 0.5·(1-1/m)** after a shot |
| x3000 | weap_x3000_fire_logic 0x1005c8f0 | none: every frame while held it damages every drawn live monster (vt+0x198, 1000 dmg; section 11) | none, no ammo | rand = view shake | n/a |

`m` = fire-rate multiplier (1 = stock). A shot is detected by polling, once per frame, the held weapon's +0xB18
changing (geagle/arifle/prpg/gray/pbomb write it). For the shotgun, watch +0xB10 changing to 6. For the pcannon,
watch +0xCC8 jumping up to 2.0; that catches only the overcharge branch, so normal Pulse Cannon shots are **not
counted** (`weapons:` shots, the Triggerbot's `botShots`). Their mark is +0xCBC falling from > 0 to 0 (section 10).

## 3. Script keys → fields (from each class's parse_key, [D/G])

- geagle (weap_geagle_parse_key 0x100547b0): proj_damage +0xC28, **auto_fire_time +0xC2C**, auto_reload_timer +0xBFC,
  **reload_time +0xC30**, **reload_time_multiplier +0xC34**, **clip_amount +0xC3C** (ftol), idle_delay +0xB1C.
- shotgun (0x1005b4d0): proj_damage +0xC18, auto_fire_time +0xC1C (unused), idle_delay +0xB1C.
- arifle (0x10051b80): proj_damage +0xCE4, **cone_ang_start +0xD1C, cone_ang_end +0xD20, cone_ang_time +0xD24**,
  spin_up_time +0xBFC, **rounds_per_sec +0xC28 (int, ftol; ctor 1 at 0x1005196b) → +0xCE8 = 1.0/rps**
  (0x10051f1a-0x10051f38). In the arifle's code only the ctor, the parser and the fire logic's FILD (0x10052870)
  touch +0xC28 [X]; spin-up (+0xBF4/+0xBFC), sound and anim never read it, so a larger value is safe.
  The rounds land in +0xC24, which weap_arifle_think spawns (0x10052bf0) and subtracts from the burst count +0xC2C.
  At fps ≥ rps every frame fires at most one round, so a click still fires exactly 5 rounds at any rate, only faster;
  below that a frame can fire 2 or more and the burst can overshoot 5 (as stock does below 12 fps).
- pcannon (0x10058340): proj_damage +0xC44, proj_min_damage +0xC48, proj_ammo_min +0xC4C, proj_ammo_rate +0xC50.
- gray (0x10055c70): proj_damage +0xBF4, proj_ammo_rate +0xC04. pbomb (0x10056f00): `*_launch`/`*_return_visible`
  +0xC00..+0xC34, proj_damage +0xC44. prpg (0x10059ef0): proj_damage +0x4940. x3000: only base keys.
- Shipped values (db_e01 weapons.def) [S]: geagle auto_fire 0.2 / clip 10 / reload 0.55 / mult 1.0 / auto-reload 2.0;
  shotgun 0.6; arifle 12 rps, cone 10°→2° over 1 s; pcannon charge rate 50/s, min ammo 8.

## 4. Reload (Golden Eagle only)

- No other class parses `clip_amount` or `reload_time` [G parse_key scan]. The shotgun pump is part of its fire cycle.
- weap_geagle_init 0x10054b60 (vt+0x74): after the desc is read, **+0xC30 = reload_time / reload_time_multiplier**.
  The same code also multiplies the speed (+8) of every `reload_group` anim in the **global** anim table
  DAT_1017f9dc by the multiplier. That compounds on every geagle init, and is harmless only while the multiplier is 1.0 [D].
- Trigger: in the fire logic, count +0xC04 += 1. On the last round (count == clip) it schedules event 2 at +0.2 s
  (skmod clock). Past the clip it reloads immediately. weap_geagle_on_event 0x10055a00 (id 2) → **weap_geagle_start_reload
  0x100553e0**: `+0xBF8 = +0xC30` (0x100553f6), +0xC04 = 0, **+0xC38 = 1 (reloading)**, plays the reload anim at speed 1.0 [D].
- End: at the top of the fire logic, reloading clears when `now - B18 >= +0xBF8`. So the reload lock is timed **from
  the last shot**, not from the reload start (0.55 s in total, and the 0.2 s event delay sits inside it). The time is
  read from +0xBF8 on each reload, and the engine writes +0xBF8 on each reload [D].
- Idle auto-reload: weap_geagle_tick_autoreload 0x10054ec0 counts +0xC00 down from +0xBFC (2 s) while idle, then calls
  start_reload(1) (anim speed 0.75). In idle the fire gate is skipped, so this reload never blocks firing [D].
- **Reload lever:** on the rising edge of +0xC38, write **+0xBF8 := +0xC30 / r**. The reload animation keeps its speed
  and the next shot's anim overrides it. Changing the anim speed too would mean editing the global table: don't.

## 5. Spread (settles FUN_10052bf0 and FUN_100578f0)

- **FUN_10052bf0 = weap_arifle_spawn_shots_cone**: this is the one real spread [A 0x10052cb1-0x10052d5d]. Per bullet:
  `cone° = (+0xD24 > 0) ? (+0xD1C - +0xD20)·(+0xD28/+0xD24) + +0xD20 : 0`, then deflection = `rand() % int(cone·100) · 0.01`°
  at azimuth `rand() % 360`, applied to the vt+0x210 direction and stored in bullet+0x1C0. +0xD28 is a cone timer:
  firing start resets it to +0xD24 (0x10052547), weap_arifle_tick_cone_timer 0x100522a0 decrements it by dt, and it
  is reset on every shot while +0xB86 is set (0x10052cba, owner pose state 3). So the cone narrows from 10° to 2° over 1 s of held fire.
- **No-spread lever:** write +0xD1C = +0xD20 = 0 on the arifle (cone 0 → deflection 0). These are parse-time fields
  (0x10051df1/0x10051e20, ctor 0x100519be/0x100519c8), so keep the stock values and restore them. An alternative is
  +0xD24 = 0, which takes the zero branch before the divide. Silent aim that rewrites bullet+0x1C0 also overrides the cone.
- **FUN_100578f0 = weap_pbomb_launch**: spawns `scripts/proj/pbomb.sc`, direction from vt+0x210 (not randomised),
  speed ×(580+rand%40) or ×(330+rand%40) (±3 %), plus a random spin (rand%20, `rotate_delta`) [A]. That is throw
  jitter, not aim spread. Leave it.
- Other `rand()` sites in weapon code (0x10050000-0x1005d400, [G]): geagle 0x10055867/0x10055991 casing/sound;
  shotgun 0x1005bfd2 casing; arifle 0x10053095/0x1005336f casing/flare; gray 0x1005660e, pcannon 0x10058d58, x3000
  0x1005c7b2/0x1005cd65 → `game_add_view_shake` 0x100e89a0 (camera shake); prpg 0x1005ad38 open; actor_weap_base
  0x10053f6f (not a player weapon). `sys_rand` is never used [X, entities §5].

**Pipebomb strong or weak** [D]: `weap_pbomb_launch` throws along vt+0x210 at
580+rand%40 when the weapon's `+0xC3C` is 1, else lobs at 45 degrees up at 330+rand%40. `+0xC3D` set (crouched, PS 4)
places the bomb. `weap_pbomb_fire_logic` copies `+0xB84..+0xB87` into `+0xC3C..` at the throw's start, and
`weap_read_owner_pose` 0x10051584 sets `+0xB84` = (player `+0xC1C` == 2). `+0xC1C` is the upper-body state that
player_tick copies from `+0xC18` (0x1007c3e3). The walk state writes 2 there on Up without Down (FUN_10077620(2) at
0x10085a70), so strong means Up held. State 17 never writes it. First person writes `+0xC18 = 2` while the bomb is
held unless the Crouch key is held (fpmoves.h `fp_pbomb_upper`), so the throw follows the view's centre point.
The menu throws strong unless crouched.

## 6. The mechanism the menu uses

Run once per frame on the main thread, from the overlay hook. Read `player+0xDF0` through a guarded read of the
weapon block (≤0xD80 bytes, one VirtualQuery per frame). Then:
1. Fire rate `m`: on a detected shot, write the lever in §2 (geagle +0xBF8, arifle +0xC28 and +0xCE8 (held and restored),
   pcannon +0xCC8, prpg +0xB18, shotgun/pbomb +0xB2C only once the open items are settled). Every one of these
   fields is written by the engine itself (per shot, except the arifle interval, which is written at load).
2. Reload `r`: geagle only, +0xBF8 on the rising edge of +0xC38.
3. No spread: arifle +0xD1C/+0xD20 = 0 while the switch is on, stock values restored when it is off or the weapon changes.
Do not patch the shared constants (0.5 @ DAT_10126ce0, 2.0/4.0 immediates) or the global anim table.

## 7. Sibling UI (the Alien Armageddon port: `src\ekmod\ui\menu.cpp:2102-2280`, weaponspeed.h:25,966)

Weapons tab. Group "PERFECT ACCURACY" holds one switch per weapon (hint "Removes this weapon's own spread."; here only
"Assault rifle" is meaningful). "Fire rate" slider 100-400 %, step 50, default 100, value text `N%` (plus the
predicted multiplier). "Reload speed" slider 100-1000 % plus a "Max" stop, default 100 (hint "Faster reloads, up to
Max; ..."). "Weapon switch speed" 100-400 %.

## 8. G.L.O.P.P. Ray: reach and the transform

The beam is 290 units (5.6 m at 52 units a metre) from the muzzle, and its "damage" is a
separate morph counter: when it runs out, a morphable monster turns into its animal. Animals, bosses and
hyper-mode monsters are never turned. The handler check that skips animals accepts the monster handler behind
dmgnum's detour (aim.md section 8).

- **Beam object:** `actor_proj_gray` (vt 0x1012E998, alloc 0x10DC), the weapon's child, found by class name and
  stored at weapon **+0xBFC** by `weap_gray_init` 0x10055d70 [D]. Each frame `proj_gray_beam_tick` 0x10041940
  (vt+0x220) calls `weap_gray_update_beam` 0x10056710. That writes the direction (+0xBEC) and the muzzle
  (+0xBE0, weapon vt+0x214). The tick then sets **end = muzzle + dir · +0xCC8** [D].
- **Range = +0xCC8 = 290.0** (0x43910000), written by `proj_gray_init_beam` 0x100414e0 at 0x1004150e. Its
  init vt+0x74 0x10041210 calls it after the ctor 0x10040f30 has written 10000.0 at 0x10041002 [A, all
  `+0xcc8]` writers listed]. The vtable ends at +0x224, so no script key sets it. The panel reads the live
  value each frame and falls back to 290 (`glopp_beam_len`).
- **Hit test:** `phys_bound_test` (a box sweep, half-extents 10) with the beam's matchers. If it misses,
  `phys_ray_test` runs. The nearest history record that is neither the weapon, its owner nor ignored takes the
  beam, and the beam ends there. Touch +0x220 denies actor user bits 0x1000/0x400/0x800/0x100 and blockers
  0x10000/0x20000. Block +0x244 denies actor user 2 and 0x100 [D 0x10041940].
- **Effect:** every 0.2 s (+0xCD0) the nearest actor gets **vt+0x198(type 4, `+0xBC0`·0.2)**, where +0xBC0 =
  the weapon's `proj_damage` +0xBF4. `FUN_10042790` then spawns the hit effect. Monsters route type 4
  through `monster_damage_send` 0x1002f3f0 to vt+0x2FC = **`monster_apply_damage` 0x1002f560**. Its type-4
  branch, in order [D/A]:
  refuse if flags +0xB7C & **0x1000** (animal); refuse if health +0xB84 <= 0; refuse if `[+0xDC0]+0x71`;
  refuse if `[+0xDD4]+0x24` (hyper on). Otherwise **+0xB90 -= amount** (+0xB90 starts at `health`, desc reader
  0x1002c170: a morph counter, health is untouched). When it reaches 0 the player gets the reward
  (`powerup_give_send`). Then, with flags & **0x800**, it starts hyper mode (`monster_hyper_start`
  0x10034eb0) and does not transform. Otherwise, if `[+0xDC0]+0x70 == 0`, it calls vt+0x344 =
  **`monster_morph_start` 0x100347d0**, which spawns the `morph_target` class in its place and sets +0x70.
  `[+0xDC0]+0x70` is read **without a null check** (0x1002fbf6). The block +0xDC0 exists only with flags
  0x80000 (desc reader). The panel never fires at a monster without it.
- **Transform in progress:** `monster_morph_start` (server) sets **`[+0xDC0]+0x70`
  = 1** at once. It spawns the animal with `+0x2E0 = 0` and runs that animal's vt+0x34c 0x10034b60 (state
  +0xB94 = 0x10, its own +0x71 = 1, the scale-in). It also sends message 0x84 with the animal's id. The receive
  0x10031990 stores the id at +0xDD0. On the next `monster_tick` (0x1002d54a-0x1002d589) the old monster's
  vt+0x348 0x10034930 runs: state +0xB94 = 0x11, user |= 0x80, **`[+0xDC0]+0x71` = 1** (morph animation),
  timer `[+0xDC0]+0xC` = morph_time, and the two are linked through +0xDC4. The gate refuses +0x71 as
  `morphAnim` (the handler's own early test, 0x1002fd49) and +0x70 as `morphing`, so the enemy stops being a
  target in the same frame `monster_morph_start` runs.
  **It still absorbs the beam:** neither 0x100347d0 nor 0x10034930 clears its +0x2E0. The beam's matchers do not
  deny user 0x80, and `proj_gray_beam_tick` has no 0x80 skip (unlike the bullet walk). So it stays the nearest
  record and soaks the beam: the panel treats it as a blocker. When the old monster is removed is not read.
  0x10037780 (pig, rat, gator and roach ctors: 0x10037de0, 0x100380c0,
  0x100373c0, 0x100383e0) sets flags |= **0x81000** and user |= 0x40. `monst_fly`'s init 0x10036380 sets
  0x1000. So a turned enemy is a different class, and the beam is refused at the handler's first test. The beam
  still stops on it (user 0x40 is not denied). These are CAT_ENEMY in our walk ("Rat", "Pig", ...), so the
  ray filter has to skip them or the triggerbot keeps firing at animals.
- **Bosses and parts:** their vt+0x198 are their own (aim.md section 7) and none reaches the morph. Some take
  the type-4 amount as plain health damage: the Cockroach Queen 0x10013b00 ignores the type. They are never
  ray targets (below).
- **Other weapons with a hard range:** the bullets (Golden Eagle, Shotgun, Assault Rifle) stop at 5000 or at
  the view's sides (aim.md section 4, already enforced by the on-screen test). The X-3000 hits every
  monster that is drawn and in view (+0xAF1). It has no distance limit beyond the renderer's own (section 11). The rocket lives 1.0 s
  (800 units) and splashes 150; the plasma lives 5 s; the pipebomb waits for Duke's press (aim.md section 10). Crosshair mode ignores the Aim range for every weapon except the ray. That is no
  bug for bullets, whose clip is 5000; it is reported, not changed.
- **Bosses are never ray targets** (a menu rule), though the ray takes health from the
  Cockroach Queen.

## 9. Damage routes and types

Bullets reach a monster through vt+0x194 with type 0 and the info's splash byte +0x20 clear. The PRPG's and the
pipebomb's splash go the same way with type 9 and +0x20 set. The plasma (type 10) and the X-3000 (type 0) call
vt+0x198 directly. The ray's type 4 is the morph (section 8). What each monster class refuses is in aim.md section
11: only the SecurityMech (low bullet hits) and roachoid3/B (state 0xB) refuse anything.

## 10. Pulse Cannon: charge, release and the tap

The Triggerbot taps it: `tbot_weapon_fires_on_press` refuses the Pulse Cannon, and `tbot_frame` applies that test
after either mode picks its target (Crosshair **and** Locked), so a press for one frame while nothing is charged and
a release while a charge is held replace the press. Verified in game.
- **Charge** [D/A weap_pcannon_fire_logic 0x100588a0]: a frame takes the charging branch when the cooldown +0xCC8 is
  at most 0, the attack bit (+0xB04 & 2) is held, and the owner checks pass. Charge time +0xBF4 += dt. On the
  first frame (+0xCBC = 0) it takes `proj_ammo_min` (+0xC4C, 8) from the owner (vt+0x224) and sets **+0xCBC =
  max(min(+0xBF4/4, 1), +0xC54)**, where +0xC54 = 1/`proj_ammo_rate` = **0.02** (parse 0x10058449; ctor 0.025).
  Later held frames add +0xC54 per ammo unit. While +0xCBC > 0 the ball sits at the muzzle and gets
  `proj_damage` = (+0xC44 - +0xC48)·+0xCBC + +0xC48 (doubled under the power-up) and `scale` (+0xBD4 on the
  plasma = damage / +0xC44, at least 0.1). Without ammo it dry-fires and switches weapon (vt+0x204).
- **Release latch** [A 0x100588dd, 0x100590c2]: a local byte starts at 1 each frame and the charging branch
  clears it. At the end, `+0xCBC > 0 && (byte || +0xC41)` calls `weap_pcannon_discharge` 0x100590f0. So the
  **first frame without the bit fires** what is charged, and a held bit keeps charging. The discharge sets the
  velocity to dir·((1 - c)·200 + 300), sets state 6, B18 = now, and zeroes +0xCBC, +0xCC0 and +0xBF4.
- **Uncharged shot:** a one-frame press leaves c = 0.02 (6 ms of hold time gives 0.0015 < 0.02). Its damage is
  (70 - 10)·0.02 + 10 = **11.2** (weapons.def `proj_damage:70.0`, `proj_min_damage:10.0`), 22.4 under the power-up,
  and it flies at about 496 plus the owner's velocity. So a Pig Cop (20) takes two taps.
- **The tap** (`tbot_pcannon_press`, aim.h): press while +0xCBC is 0, release while it is > 0. The weapon's own
  charge plays the role the Golden Eagle's latch +0xC1E plays (`tbot_semi_press`). With nothing to read it
  alternates frames. No engine gate limits that rate (each tap costs 8 ammo, `proj_ammo_min`), so **a new tap
  waits 125 ms of wall time after the last release** (`tbot_pcannon_ready`, about 8 a second). A charge already held is always released.
- **Overkill:** an in-flight ball is pending with its own +0xBC0 (aim.md section 10), so a tap's 11.2 counts once
  it flies. The next press is decided only after it flies (press, release, then the frame after the discharge),
  so a second tap at an enemy the first one kills is withheld. A ball at the muzzle (velocity 0) is not pending.
  The log's `overkill: … plasma tapExpect= seen=min..max` compares the held cannon's expected tap damage with the
  balls seen.

## 11. X-3000: fire logic

A plain hold. Each frame with the attack bit held, it damages every drawn, living monster.
It has no edge, no refire gate and no ammo. The Triggerbot holds it (aim.md section 12).
- **Held frame** [A 0x1005c8f0-0x1005cbae]: when `+0xB04 & 2` is set, it sets +0xB08 |= 2 and plays the attack
  anim (vt+0x21C, state 6, anim chosen by pose +0xB84..+0xB8A). It then runs (tail at 0x1005ca30)
  `spawn_search("s=%d?t=%d?g=%d", +0x78, 2, 0x20)`. For each match with **byte +0xAF1 set and +0xB84 > 0.0**
  it calls **vt+0x198(type 0, 1000.0, dir (0,0,0), weapon, owner +0x60)** and draws the bolt (FUN_1005cbb0, to
  vt+0x160). After any hit it draws one more bolt upward (+1000 y) and plays sound +0xBF4. **No B18, no
  cooldown, no ammo read**, and `weap_x3000_think` 0x1005c800 is `pre_draw` plus this. So every held frame
  fires.
- **Released frame:** B18 := now. When the anim is done (+0xB28) in state 6, it goes back to idle (vt+0x228
  within +0xB1C, else vt+0x22C). The shot poll of section 2 (B18 changing) counts released frames, not
  shots.
- **Who it reaches:** `g=0x20` matches user bit 0x20, which only the monster base ctor 0x1002bd80 sets
  (callers 0x10035b50/0x10036c90; monst_* and `animal_base_ctor`). So no boss (actor base), helicopter part
  or roach egg. **+0xAF1** is written only by the actor draw FUN_10090260 (0x10090316): the result of
  `rend_sphere_visible_dist` (import name at 0x101718ec) while +0xAF0 is set. It means "drawn and in view".
  Walls do not stop it.
- **Damage:** 1000, type 0, straight into vt+0x198 (aim.md section 11): the SecurityMech's low-hit line is skipped,
  the roachoid shell in state 0xB zeroes it. It reaches health through `monster_damage_send`'s message, ~3
  frames later, so a monster held for several frames receives 1000 each frame until it dies.
- Data (db_e01 `scripts/weap/x3000.sc`, `weapons.def` `x3000_anims`): a model, two skins, a light and the
  anims. No damage, rate or ammo keys.

## Golden Eagle cadence

Holding fire does **not** repeat: the Golden Eagle is semi-automatic, one shot per press (a triggerbot log with
`holds=5003 presses=3147` fired once). **Edge** (weap_geagle_fire_logic 0x10055120): a frame with `+0xB04 & 2` clear
sets the latch +0xC1E; a held frame with the latch set queues a shot (+0xBF4); the shot fires when `state == 1` or
`now - B18 >= +0xBF8`, then clears the latch (not on the dry-fire branch). The Triggerbot presses on the latch
(`tbot_semi_press`); a blind pulse (press one frame, release the next; `tbot_pulse`) missed shots
(`gaps=2340 presses=2668`), and why is unmeasured: the tbot line counts `pressShot`, `pressNoShot` and `latchUnread`.
The tap rate with the idle bypass is unmeasured.

## Open questions (and the runtime measurement that settles each)

1. `player+0xDF0` is the held weapon at runtime: log its vtable while switching weapons and check it matches the HUD icon.
2. The Golden Eagle tap rate with the idle bypass (see Golden Eagle cadence above).
3. +0xB2C = anim end on the skmod clock (`skmod_get_anim_parm` parm 1): log +0xB2C - skmod_get_time() at a shotgun
   shot. It should equal the anim length (~0.6-0.9 s).
4. Shotgun/pbomb rate via a shortened +0xB2C: whether the pump event (+0.4 s) or the pbomb launch event (5 slots at
   +0xB90) misbehave at 2-4×. Count shots and bombs spawned against the ammo spent.
5. prpg back-dated +0xB18: the rate rises and the idle_delay return (`elapsed <= +0xB1C`) does not stall. Log shots/s.
6. prpg rand at 0x1005ad38 (FUN_1005abe0, rand%60 → struct+0x30): particles or trajectory? Read FUN_1005abe0's caller.
7. Shotgun bullet +0xBDC (20, or 30 when +0xB85) and +0xBE4 (4): hit radius or max targets? Not spread.
   Read proj_bullet_hitscan_ray 0x1003a9c0.
8. What the arifle's +0xB86 (owner +0xC10 == 3) means. It pins the cone at its widest.
