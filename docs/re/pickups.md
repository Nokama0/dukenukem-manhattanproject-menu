# Pickup value fields

`ammo_type`, `health_type` and `gun_type` all sit at the **same relative offset as
`key_type`: entity+0xD14, dword**, immediately after the common `powerup` base (alloc size 0xD14
for classes with no extra field). Found the same way as `key_type` (entities.md §4): each class's
editor-value registration block writes offset 0xD14 into its `evalue_def` record (+0x28 = offset,
+0x20 = size 4, +0x0C = name-string pointer), then the ctor default-inits `this+0xD14` from it.
Binary: duke_base.dll (base 0x10000000). Method: Ghidra MCP and Python on the map data. Evidence tags: **[D]** decompile,
**[A]** disassembly, **[X]** xref. Functions understood are named in the Ghidra project.

## 1. Field offsets

| class | field | offset | size | evidence |
|---|---|---|---|---|
| ammo | ammo_type | +0xD14 | dword | registration block duke_base!0x1005e365-0x1005e3a1 (string "ammo_type" 0x1015fdec at +0xC, 0xd14 at +0x28); ctor **ammo_ctor** 0x1005e3c0 writes `this+0xd14` [A/D] |
| ego | health_type | +0xD14 | dword | registration block 0x1005e985-0x1005e9c1 (string "health_type" 0x1015ff90); ctor **ego_ctor** 0x1005e9e0 [A/D] |
| gun | gun_type | +0xD14 | dword | registration block 0x1005ee45-0x1005ee81 (string "gun_type" 0x1016015c); ctor **gun_ctor** 0x1005eea0 [A/D] |
| (key) | key_type | +0xD14 | dword | same pattern, block 0x1005f605-0x1005f641 (string 0x10160370) [A] |

ammo's alloc size (0xD20) has two more fields past 0xD14: **+0xD18** dword = cached "no ammo" sound
handle, **+0xD1C** byte = "sound already played" flag (ammo_on_touch 0x1005e6c0, tail at 0x1005e7af) [D].

## 2. Effect on pickup

**ammo** (**ammo_get_name** 0x1005e658 gives the enum names; **ammo_on_touch** 0x1005e6c0 is the
jump-table grant, cases 0-5, `powerup_give_send`(player, type=2, pool, amount,1,1) at 0x1005e788) [D]:

| ammo_type | name | pool (PS+0x38+2*pool) | amount |
|---|---|---|---|
| 0 | small bullet | 0 (bullets) | 10 |
| 1 | large bullet | 0 (bullets) | 50 |
| 2 | small C5 | 1 (C5) | 1 |
| 3 | large C5 | 1 (C5) | 10 |
| 4 | small glopp | 2 (glopp) | 25 |
| 5 | large glopp | 2 (glopp) | 100 |

Pool 0/1/2 are the three ammo pools weapons draw from (player.def: "ammo_types: 0 bullets, 1 c5,
2 glopp"; geagle/shotgun/arifle use pool 0, pbomb/prpg use pool 1, gray/pcannon use pool 2) [S/D].
A pool already at its float cap (this+0x64/0x68/0x6c) grants nothing, plays "no ammo" sound once [D].

**ego** (**ego_on_touch** 0x1005eb30, `powerup_give_send`(player,type=1,0,amount,1,1) at 0x1005eb66) [D]:
`health_type==0` -> +10 ego; `health_type==1` -> +100 ego; any other value takes the no-grant branch
(`JNZ 0x1005eb70`) [A]. **That branch is never reached in play:** the ego's two init methods
clamp +0xD14 into 0..1 before any touch, `edit_init` vt+0x2C **0x1005eac0** and the runtime init vt+0x74
**0x1005eaf0** (ego vtable 0x10132d10; `<0 -> 0`, `>1 -> 1`, then the powerup base init) [A]. So `lhealth.sc`'s
`health_type:2` becomes 1, a +100. Confirmed in game: the Ego +100 from a crate is picked up and gives +100 (Ego near
750 at the time).

**gun** (**gun_get_weapon_def** 0x1005f290, called from spawn setup 0x1005f18c/0x1005f1a4, not a
touch handler — the actual grant is script-driven; the detail is open): switches `gun_type` to a
`def_pool_t::find(player.def, "weap", idx)` index, i.e. player.def's `weapNN` block [D/S "weap"
0x1015ff24, "script" 0x1015ff1c]:

**Label guns by the pickup's model table** (a table keyed by weapNN mislabels the shotgun as "Pipebomb"), `gun_get_model` duke!0x1005f230: 0 e01/weap/geagle, 1 e02/weap/arifle, 2 e03/weap/gray, 3 e05/weap/pcannon, 4 e04/weap/prpg, **5 e01/weap/shotgun**. The touch grant (0x1005f1f0) passes gun_type straight to FUN_100792f0(player, 3, gun_type, 1,1,1). The weapNN column below is only the script lookup and is not what the player sees or gets. The C5 pool's pickups are named PIPEBOMB/PIPEBOMBS by the game (db_e01 local.def p_pipebomb, p_pipebombs).

| gun_type | weapNN | weapon (player.def `name:`) |
|---|---|---|
| 0 | weap00 | golden_eagle (unreachable from level data's 1..5 range) |
| 1 | weap03 | assault_rifle |
| 2 | weap04 | glopp_ray |
| 3 | weap06 | pulse_cannon |
| 4 | weap05 | rocket_launcher (P.R.P.G.) |
| 5 | weap01 | pipe_bomb |

weap02 (shotgun) and weap07 (x3000) have no case in this script lookup, but gun_type 5 IS the shotgun pickup
(see the correction above); x3000 is not a normal pickup [D/S, scripts/weap/weapons.def + player.def
read from db_e01.zipfs].

## 3. `life` -- labelled "Double Damage"

Observed in game: this pickup is the damage power-up, and the HUD's third bar is `D` beside `J` (jetpack) and `F`
(forcefield). Together with the distinct grant type 4, that outweighs the "extra life" reading of the model name
below. The receive side is open.

vtable 0x10133950, ctor **life_ctor** 0x1005f7f0, touch handler **life_on_touch** 0x1005f8c0:
`powerup_give_send(player, type=4, 0, 0, 1, 1)` then `powerup_on_picked` — **message type 4**, amount
always 0, distinct from ego's type 1 and ammo's type 2 [D]. No quantity is transmitted: a discrete
grant, not a stat top-up. Model string `models/e01/powerups/life_dna_fx.shd` [S 0x10160494] — a
DNA-helix icon, the series' usual "extra life" pickup. The receive side of message type 4 (where the life count
would be incremented) is not traced; following net message dispatch for type 4 would settle it.

The double-damage-above-100-ego effect seen with Cheat Engine is a property of the ego
stat itself (see entities.md PS+0x00), unrelated to the `life` class — no code here ties `life` to
damage or to ego.

## 4. Can the player take it?

Every pickup is dispatched by **`powerup_touch`** duke!0x1005d93a: when the toucher is the player (`+0x48 & 2` and
`+0x4C & 0x100`) it jumps to the class's **vt+0x24C** with no other gate [D]. vt+0x24C per class, read from each
vtable [A]: ammo 0x1005e6c0, ego 0x1005eb30, gun 0x1005f1f0, key 0x1005f770, life 0x1005f8c0, jetpack 0x1005f400,
forcefield 0x1005ecd0.

| kind | the engine's own test | the ESP |
|---|---|---|
| ammo | `ammo_on_touch`: PS = `*(player+0x111C)`; pool cur short **PS+0x38/+0x3A/+0x3C**, cap float **PS+0x64/+0x68/+0x6C** read through `ftol` (truncation) [A 0x1005e6d8-0x1005e6f4]; type 0/1 -> pool 0, 2/3 -> 1, 4/5 -> 2, granted only while `cur < (int)cap`; a type past 5 takes the refusal path (jump table `JA 0x1005e7af`) | hidden at the cap; shown when the pools cannot be read |
| ego | `ego_on_touch`: types 0 and 1 grant +10/+100 and are picked; any other type calls vt+0x254 and returns without `powerup_on_picked` [A], but the init clamps every type into 0..1 first (section 2), so no ego reaches that path. Health past the max is clamped by `player_add_health` after the pickup is already used | always shown (a type that could not be read is shown too) |
| gun, key, life, jetpack, forcefield | grant (`FUN_100792f0` types 3, 7, -, 9, 8) then `powerup_on_picked`, unconditionally [A]. A gun already owned is still picked up | always shown |

Rule: `pickrule.h` `pickup_can_take`, pinned by `tb_0219`. The pools are read once a frame (two guarded reads) only
while the switch and Items and pickups are on; the 5 s `ents:` line carries `poolReads`, `poolUnread` and the
three `cur/cap` pairs, and `esp:` carries `cantTake` and `takeUnknown`. Open: whether +0x64/+0x68/+0x6C hold
the caps the HUD shows (read from the compare only).

## 5. Pickups inside crates and barrels

A `crate` or `barrel` stores what it drops as a list of small codes in its own memory; the pickups do
not exist until it breaks. The ESP draws each content's marker at the holder, labelled "<content> (crate)" or
"(barrel)", and drops it once the holder's +0x2E0 clears. Tags as section 1; [S] map data (world.scr in
`duke/base/db_*.zipfs`, read with Python zipfile). Named in Ghidra: `holder_base_ctor` 0x10067d90,
`holder_contents_enum` 0x10068370, `holder_content_script` 0x100684b0, `holder_build_spawner` 0x10068230,
`holder_damage` 0x100685a0.

- **Storage [A/D]:** both classes are built on the contents base ctor duke!0x10067d90 (vtable 0x10134af0; crate
  0x10061a00 sets +0xC10 = 2, barrel 0x10061430 sets 1). The editor value `contents` (registration 0x10067b5b..0x10067bd0)
  is an int array: **+0xB7C pointer, +0xB80 count**. **+0xBF8** is `crate_type`/`barrel_type`; **+0xBF4** is the holder
  kind the init writes (crate vt+0x74 0x10061a60 sets 1, barrel 0x100614a0 sets 0). Neither class is a breakable_adv:
  they are dobj (actor_model_ctor -> dobj_base_ctor), so the `t=2` walk returns them.
- **Codes [D]:** FUN_100684b0 maps a code to a spawn script; the editor's enum tables (FUN_10068370) give the same names.
  Code 0 is `scripts/pwrup/nuke.sc` in every holder. Crate type 0: 1 sbullet, 2 lbullet, 3 shealth, 4 lhealth; type 1:
  1 sc5, 2 lc5; type 2: 1 sglopp, 2 lglopp. Barrel type 0: 1 shealth, 2 lhealth; any other barrel type holds only
  nukes. Any other code spawns nothing. The scripts [S] spawn `ammo` with ammo_type 0..5 in that order, `ego`
  health_type 0 (shealth) and **health_type 2 (lhealth)**, which the ego's init clamps to 1: an Ego +100 the player
  takes (section 2). The menu models it as subtype 1, "Ego +100 (crate|barrel)", takeable (type 2 would read as an
  untakeable "Ego (crate)"). Holders with an
  lhealth [S]: e01p01_a01 `barrel6`, e02p02_a02 `crate4`, e03p02_a02 `barrel11`, e03p02_a02_r01 `crate1`,
  e06p01_a01 `crate7`, e06p03_a02 `crate1`, e07p01_a03 `crate5`, and demo_crate_barrel `barrel5`/`barrel33`. Rule: `craterule.h` `crate_content` (test `tb_0362`).
- **Spawn [D]:** the first think (0x10068640) builds a `spawner_entity` "exploder" at the holder (0x10068230) with one
  spawnscript per code (+0xC18 keeps it). Damage (vt+0x198 0x100685a0) lowers health +0xBFC and, at 0, clears +0x2E0 and
  sends message 1; its handler calls the explode path vt+0x240 0x10068600, which clears +0x2E0 and fires the spawner.
  So the contents are not hidden pickups in the entity lists: only the codes
  exist until the break.
- **ESP:** with Nukes on, a holder with code 0 is a CAT_NUKE entry; with Items and pickups on, each other content is a
  CAT_ITEM entry with its ammo/ego subtype. One entry per distinct content, at the holder's position and box, in the
  content's colour, label from `crate_content_label` (`tb_0363`). An entry hides once +0x2E0 is 0
  (`ent_crate_opened`, `tb_0364`); the freed pickup then shows as itself. A crate's nuke gets the Nukes arrow
  (`arrow_wanted_cat`). Reads: the holder span +0..+0xBFC under the walk's cache, the codes array under a second cache,
  only while Nukes or Items is on. Log: `crates: N with contents (nukes k, items m) of B breakables; unread u, broken o`
  each 5 s, `esp: ... opened=` for dropped entries, and `watch: crate|barrel <p> new at (...) +2E0=1 +B80=n` then its
  changes and `gone`.
- **Census [S]** (129 world.scr, db_mapfix overrides none of these maps): 254 crates (78 with contents) and 242 barrels
  (54). Nukes in a holder outside the demo maps: e01p02_a01 `crate11` at <3904 -1836 -2524>, e02p02_a03 `barrel14`,
  e02p03_a01 `crate1`, e02p03_a01_r01 `barrel1`, e03p01_a03_r01 `barrel1`, e03p02_a01 `crate2`, e04p02_a01 `crate2` and
  `crate3`, e06p02_a01 `barrel29`, e06p02_a02 `crate5`, e06p03_a01 `crate2`/`crate3`/`crate4`/`barrel4`/`barrel6`/
  `barrel10`, e07p01_a02 `barrel2`/`barrel5`. The test map `demo_crate_barrel` has 38 more.

+0x2E0 is 1 on an intact holder (every `watch: crate|barrel ... new` line in the log reads `+2E0=1`, and breaking reads
`1 -> 0`). Open: whether the spawner places the pickups at the holder (its spawn path 0x100b5f70 was not read). The
holders' `.msv` record: [areas-and-secrets.md](areas-and-secrets.md) section 4, "Nukes".

## 6. Nukes that enemies carry

A monster's drops are its own editor value `spawnscript`, a list of script paths kept in its memory; it spawns each
one at its body when it dies. 44 placed enemies carry 47 nukes, and with the spawners every part's world.scr sums
exactly to its `nukecount`. The enemy's label gets a green " + Nuke" (" + 2 Nukes"), the Nukes switch draws a small
nuke marker on it and its arrow points there, and the areas count the carried nukes (verified in game). Tags as
section 1; [S] the effective world.scr of all 129 maps (`db_eNN` p1 under `db_mapfix` p50). Named in Ghidra: `monster_die` 0x1002fdb0, `monster_drop_all` 0x10035450, `monster_drop_one`
0x1001efe0, `gatoroidB_apply_damage` 0x1001ef70, `spawner_fire` 0x100b5f70.

- **Storage [A/D]:** the monster base ctor duke!0x1002bd80 (user bit 0x20) zeroes **+0xCA0** (pointer to char[64]
  entries) and **+0xCA4** (count); `set_value_size(this, "spawnscript", n)` (string 0x1015a8c4) resizes the same
  value, so it is the editor key. world.scr writes it as one `spawnscript:"scripts/pwrup/<x>.sc"` line per drop
  (e07p02_a01 `monst_pigoid22`: sbullet, nuke). Every class with that base allocates past +0xCA8 (smallest 0xEE8).
- **Death [D]:** `monster_die` 0x1002fdb0 (kills +1 at PS+0x50, then) calls **vt+0x2F4**. The default,
  `monster_drop_all` 0x10035450, spawns every entry with the spawn-script import (0x10183d50, class tag
  "monst_drop"), at +0x18C with a small fanned velocity, names it `drop-<class>` (format "drop-%s" 0x1015c1ec, so a
  dropped nuke is **`drop-nuke`**), then `set_value_size("spawnscript", 0)`. So a dead enemy holds nothing, and the
  dropped pickup is an ordinary `nuke` the ESP already draws. When +0xB9C == 5 and +0xEE0 is set, the list goes to
  FUN_10010990 instead (not read; what that state is, is open).
- **The gator boss drops as it is hurt [A]:** `monst_gatoroidB` (vt 0x10128888) overrides vt+0x2FC with 0x1001ef70 (and vt+0x2F4 is `monster_drop_one`):
  after the damage it drops one entry (`monster_drop_one` 0x1001efe0 pops the last, count - 1) while health <
  count x +0x1068, where its init 0x1001ee50 sets +0x1068 = health / count. Its three nukes fall at two thirds, one
  third and death. Any enemy's list is therefore read as "the first +0xCA4 entries are still held".
- **Spawned monsters [D]:** `spawner_fire` 0x100b5f70 appends its own `monst_spawnscr` list (+0xA1C, count +0xA20) to
  each monster it spawns (user bit 0x20) through `set_value("spawnscript", ...)`. Those carriers exist only once the
  script fires, so the ESP shows them but the areas cannot place them.
- **Census [S]** (a Python count over every world.scr): nukes per part from pickups /
  holders / placed enemies / spawner scripts (`spawnscript` nuke.sc on a spawner, or `monst_spawnscr` nuke.sc):

  | part | pick | hold | enemy (carriers) | script | part | pick | hold | enemy (carriers) | script |
  |---|---|---|---|---|---|---|---|---|---|
  | e01p01 | 10 | 0 | 0 | 0 | e05p01 | 10 | 0 | 0 | 0 |
  | e01p02 | 8 | 1 | 2 (2) | 0 | e05p02 | 9 | 0 | 1 (1) | 0 |
  | e01p03 | 8 | 0 | 2 (2) | 0 | e05p03 | 7 | 0 | 3 (3) | 0 |
  | e02p01 | 7 | 0 | 2 (2) | 1 | e06p01 | 9 | 0 | 1 (1) | 0 |
  | e02p02 | 5 | 1 | 2 (2) | 2 | e06p02 | 7 | 2 | 1 (1) | 0 |
  | e02p03 | 5 | 2 | 3 (1) | 0 | e06p03 | 4 | 6 | 0 | 0 |
  | e03p01 | 9 | 1 | 0 | 0 | e07p01 | 5 | 2 | 3 (3) | 0 |
  | e03p02 | 9 | 1 | 0 | 0 | e07p02 | 6 | 0 | 4 (4) | 0 |
  | e03p03 | 8 | 0 | 2 (1) | 0 | e07p03 | 5 | 0 | 5 (5) | 0 |
  | e04p01 | 8 | 0 | 2 (2) | 0 | e08p01 | 10 | 0 | 0 | 0 |
  | e04p02 | 5 | 2 | 3 (3) | 0 | e08p02 | 0 | 0 | 4 (4) | 6 |
  | e04p03 | 5 | 0 | 5 (5) | 0 | e08p03 | 5 | 0 | 2 (2) | 3 |

  The script column is the remainder: `e02p01_a01.spawner_nuke` (a nuke spawner) and 11 spawned carriers
  (e02p02_a03_r01 `spawner_entity2` x2, e08p02_a02 x2, _a03 x2, _a04, _a06 `blue_key_bitch`, e08p03_a01
  `spawner_entity3/4/6`, the blue lasers' spawners: section 7). The two-nuke carriers: e02p03_a02 `monst_gatoroidb1` (3), e03p03_a03_r01
  `monst_roachoidb1` (2).
- **ESP:** entities.cpp `entities_carried_nukes` reads +0x4C, +0xCA0/+0xCA4 (span 0xCA8) and the list (count x 64,
  under its own read cache) for each monster while Enemies or Nukes is on; `carry_nukes` (carryrule.h, `tb_0473`)
  counts nuke.sc among the held entries. The enemy's entry keeps the count (label suffix `carry_suffix`, `tb_0474`);
  with Nukes on a CAT_NUKE entry at the enemy draws small green brackets at its box centre (with Enemies off, also
  "Nuke (enemy)" and the distance) and takes the Nukes arrow. Log: `carriers: N enemies carry M nukes; lists unread u`
  each 5 s, and `watch: <class> <p> new ... +CA4=n`, its changes (`n -> m` on the gator's partial drops) and `gone` (an ordinary carrier killed logs only `gone`).
- **Areas:** [areas-and-secrets.md](areas-and-secrets.md) section 4, "Enemy carriers".

## 7. Blue lasers that spawn a nuke carrier

Three blue lasers in e08p03_a01 each fire a spawner whose monster carries one nuke. These are the three "script"
nukes that section 6 leaves to e08p03. One of them, laser31, fired twice in one life, which is how the part reached
11 of its listed 10 and the Hard sum reached 242 of 240. Verified in game (laser31 on e08p03_a01: marker, arrow,
Security Mech spawned with "+ Nuke"; the beam stayed off). Tags as section 1; [L] the panel's log of a run through
e08p03_a01. Named in Ghidra: `laser_s_handler` 0x100af380, FUN_100af0d0 (the laser's draw).

- **The trap [A/D]:** class `laser` (alloc 0xBC0, vtable 0x1013d080, ctor 0x100aeae0). It has three pins: `obstructed`
  (+0xA68, value +0xAC0), `world_obstructed` (+0xAD0) and `active` (input, +0xB38, value **+0xB90**; vt+0xE0/E4 store
  and restore it). The think 0x100aee10 traces the beam each frame into **+0xBB0** (its world end). It acts only while
  +0xB90 is set and the laser was drawn (+0xBBC). It sends `obstructed` 1 when Duke is in the beam and 0 when he
  leaves, and only on a change. The shader alone tells red (`laser_red.shd`) from blue (`laser_blue.shd`). The game
  has 12 blue lasers: 6 in e08p01_a01, wired to `spawner_laser9/10`, and 6 in e08p03_a01 [S].
- **The wiring [S]:** each e08p03 blue laser's `obstructed` drives `spawner_entityN.active` and a `not` gate whose
  output is the laser's own `active`. The first crossing therefore fires the spawner and switches the laser off.
  `spawner_entity`'s input handler FUN_100b5b80 fires (`spawner_fire`) on every nonzero value its `active` pin
  receives. `spawner_fire` names each entity `"%s-spwn%03d"` (0x1016c9e4) from its count **+0xAE4** (the ctor
  0x100b5770 zeroes it), and appends `monst_spawnscr` to the monster's drop list (section 6). So the carrier shows
  " + Nuke" through the carrier feature, and its drop is `drop-nuke`.
- **Census [S]** (every effective world.scr, `laser` -> `obstructed` -> `spawner_entity`): of the 10 laser-to-spawner
  wires, all from blue lasers, 3 hand out a nuke, all in e08p03_a01:

  | laser | where | spawner | monster | at |
  |---|---|---|---|---|
  | laser22 | on pod `mover_laserpod7` | spawner_entity4 | securitymech3 | (6044,-256,-412) |
  | laser31 | on pod `mover_laserpod13` | spawner_entity3 | securitymech3 | (6528,-256,-416) |
  | laser38 | top level, (9276,3628,-836) | spawner_entity6 (also fires 5 and 7: sc5) | securitymech3 | (10432,1568,-836) |

  With one fire each, e08p03 sums to its listed 10: 5 pickups, 2 placed carriers and 3 lasers. The other script nukes
  of section 6 (e02p01, e02p02_a03_r01, e08p02) are not wired to lasers.
- **A laser can fire more than once [L].** After the respawn (log line 7172), the spawned carriers' `watch: ... new`
  lines read: (6044,-256) at 7615, **(6528,-256) at 8807 and again at 9106**, and (10432,1568) at 9848, each with
  `+CA4=1`. No stage, death or reload falls between 8807 and 9106. The four carriers are the "4 additional"
  nukes. The live tally then reached `nukes 11.0` against `totals ... nukes 10` and `Hard nukes 242/240` (line
  13558). Open: why laser31 re-armed. Its `not9` should hold `active` at 0. laser31 and laser22 ride movers
  that their own `obstructed` starts, which may hand `active` back to their children. The `watch: Laser ... +B90`
  and `watch: Spawner ... +AE4` lines settle it.
- **Where the corner's "11/11" came from:** our own `ls_line` showed max(total, found) as the denominator, copying the
  part-end screen ([levels-and-stats.md](levels-and-stats.md) section 4). The game's own total stays 10. The nukes lines read raw
  (`Nukes 11/10`, `Hard nukes 242/240`, and the Progress nukes column), because the listed total is what the Wozma
  unlock compares against. Kills and secrets keep the max (`tb_0484`).
- **Menu:** the parser (areas.h `ar_parse_world`, `tb_0481`) makes one `AR_NK_LASER` source per laser and nuke
  spawner pair. Its position is the laser's plus its parents' (a pod's) positions. Live (`ar_nukes_live`, `tb_0482`):
  the nuke is missing until the spawner's +0xAE4 shows a fire. After that it stays missing while a `<spawner>-spwn*`
  monster holds a nuke or a `drop-nuke` lies for it. Each fire past the first adds to `extra`, which
  `ar_nuke_tally` allows past the listed total. The `.msv` holds no laser state: those sources come from our record.
  ESP (entities.cpp `laser_entries`): with Nukes on, each laser of the current area is found by name. While +0xB90
  is set, a CAT_NUKE entry "Nuke (laser)" frames the beam from the matrix origin to +0xBB0 (`laser_beam_box`,
  `tb_0483`) and takes the Nukes arrow. Log: `lasers: <name> <p> fires <spawner> <p>: +18C ... matrix ... end ...,
  armed a, spawned s` once per laser, the watch lines, `lasers: N nuke lasers here, a armed; ...` each 5 s, and
  `L taken/listed (+extra)` in `areas: found in`.

## 8. Other destructibles that drop pickups

No other class holds a pickup list of its own; `breakable_adv` objects drop pickups only through a
spawner wired to their `broken` pin, and two of those hand out a pickup directly. [S] every effective world.scr
(129 maps). Classes with "break" in the name: `breakable_adv` (121, keys `health`, `debris`, `damage_type`,
`connect broken`, ...; no `contents`/`spawnscript`) and `breakable_obstacle` (5, `respawn_time`, no drop list). Of
the `connect broken` wires, 13 reach a `spawner_entity`:

- **A pickup at once:** e01p01_a01 `brk_air1` (at <1280 -448 112>) and `brk_explodbox` (<5752 -956 136>), whose
  `broken` pins drive the spawners `spawn_air1` and `spawn_box`, each `spawnscript` sbullet.sc (Bullets +10).
  `brk_explodbox` has six `broken` pins, the most in any map; `spawn_box` is the fifth.
- **A monster carrying a pickup** (`monst_spawnscr`, section 6): sbullet.sc on pigoid spawners in e01p01_a01,
  e01p01_a02 (x2), e01p02_a01_r01 (x2), e01p03_a01 (x2) and demo_crate_barrel; lglopp.sc on e06p01_a02 `spwn_crate`
  (a gloppoid).
- **A monster only:** e06p01_a02 `spawner_entity1` (roachoid), e08p01_a01 `spawner_cargobox1` (securitymech).

**Menu** (`breakrule.h`, `ArBreak` in areas.h, entities.cpp `breakable_drop_entries`): `ar_parse_world` keeps each `breakable_adv` whose `connect broken:<spawner>.active` reaches a
`spawner_entity` whose own `spawnscript` is a pickup script (sbullet, lbullet, sc5, lc5, sglopp, lglopp, shealth,
lhealth; not nuke.sc, no breakable hands one out). With Items and pickups on, each such breakable of the current area
gets a CAT_ITEM entry "<content> (breakable)" at the live object (found by name, as the lasers), hidden once +0x2E0
clears; the spawned pickup then shows as itself. The 5 s line `breakdrops:` counts them, broken and unfound.
Verified in game: in e01p01_a01 the breakables `brk_air1` and `brk_explodbox` show "Bullets +10 (breakable)" and
vanish when shot (the pin target is spelled `.active` there, as the parser needs). The carrier suffix does not extend past nukes to the ammo a monster carries. None of these is an Ego.

## Open questions
1. gun_type's weapon-spawn mechanism past `gun_get_weapon_def` (script-driven).
2. life message type 4's receive-side handler / life-count storage.
3. Crates and barrels (section 5): where the spawner places the contents.
4. Enemies (section 6): what +0xB9C == 5 with +0xEE0 set is, where monster_drop_all hands the list to FUN_10010990.
5. Lasers (section 7): why laser31 fired twice; the laser's world position (matrix origin vs +0x18C for a pod's child,
   logged once per laser).
