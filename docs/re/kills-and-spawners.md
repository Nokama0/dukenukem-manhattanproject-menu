# Kills and spawners

The kill counter (+0x50 of the live stats record) goes up by one whenever a `monst_*` actor dies in the
world Duke is in, whoever killed it; bosses, eggs and crates never count. A part's `killcount` is roughly its placed
monsters plus one monster per monster spawner. On e05p02 the 4 kills a full clear misses are two never-fired
ambushes, each an "Auto Trigger Once" `area_switch` the player never walked into: 2 Rat Oids in `e05p02_a01_r01`
(`area_ambush`) and 2 Pig Cops in `e05p02_a02` (`area_ambush`). Walking into either volume spawns them (verified in
game). The menu's Enemies > Ambushes ESP and the Progress kills column are built on this (section 5).

Method: tags as [levels-and-stats.md](levels-and-stats.md): **[D]** decompile, **[A]** disassembly, **[X]**
instruction or xref search, **[C]** shipped data (effective `world.scr`/`episodes.def`, `db_mapfix`/`db_p101` p50
over `db_eNN` p1), **[L]** the panel's log (an e05p02 session). duke_base base 0x10000000 (duke!). Named in Ghidra:
`spawner_entity_ctor` 0x100b5770, `spawner_s_handler` 0x100b5b80, `game_local_player_in_world` 0x100e7060. Census
scripts (Python, data only) are kept in the Ghidra project's `scripts\kills-census\` folder (`extract.py` unpacks the
effective files; set `SCR` in `scrparse.py` to the folder it fills).

## 1. What counts as a kill

- **The only real writer** [X/D]: of every `FSTP [reg+0x50]` in duke_base, two touch a stats record:
  `monster_die` 0x1002fdb0 (`+0x50 += 1.0` at 0x1002feef) and `player_give_item` 0x1007dc7c (the `give kill` cheat). Every other `+0x50` float store belongs to an unrelated structure (0x10025b27, 0x10040bbf, 0x10063c4d,
  0x1008cc57, 0x100c7d2a, 0x100cfd83, 0x100e8e52 read).
- **The rule in `monster_die`** [D]: it returns early (no kill) when the monster is already dead (`+0xB7C & 0x200`,
  the dead flag it sets itself), so a corpse never counts twice. Otherwise, if **+0xBD4** (the monster's target
  player) is non-zero, `*(+0xBD4)+0x111C` is the player's stats record and its +0x50 gets +1. The attacker is not
  consulted, so explosions, other monsters and falls count as well as Duke's shots.
- **+0xBD4** is written by `monster_tick` 0x1002d537 (and `monster_sleep_unless_seen` 0x1002d337,
  `monster_perception_timer` 0x1002dac5) from `game_local_player_in_world(G, world)` 0x100e7060 [A/D]: the local
  player when that player is in the monster's world, else 0. The save/load reader 0x100321f0 restores it by uid. So a
  monster counts once it has ticked once with Duke in its world, which every monster in Duke's area has done.
- **Who reaches `monster_die`** [X]: the monster base ctor 0x1002bd80 is called only by 0x10035b50 (7 classes plus
  `animal_base_ctor` 0x10037783) and 0x10036c90 (3 classes), and 12 call sites and 9 vtable slots name
  `monster_die`. Animals (`monst_rat`, `monst_roach`, `monst_gator`, ...) are on the monster base and count: [L] the
  4 placed `monst_roach` in e05p02_a01 were killed and `a01` reached exactly its 30 placed + 4 spawned.
- **Never counted** [D/X]: bosses (`boss_*` are built on the actor base 0x10090b30, aim.md section 4b, and no other
  stats write exists), `roach_pod` eggs (their gib gives +1 Ego, entities.md section 8; the Roach an egg hatches is a
  `monst_roach` and does count), crates, barrels and breakables.
- **E1P3's `extrakills`** (6/9/12 by difficulty, "porkchopper pigoids") are the pigoids the Porkchopper spawns during
  the fight (attached by 0x10025ff0, its `+0x14C8` child list); they die through `monster_die` like any pigoid.
- **A death reload rolls the counter back** with the area [L]: in `e05p02_a01_r01` the live kills were 37 before the
  reload at line 25604 and still 37 after two more Duke kills (lines 25623/25628), the same two enemies killed again.
- **A kill without Duke** [L]: entering `a01_r01` took the counter 34 -> 35 with no `dmgnum` kill line, and the
  `ents:` line showed 5 enemies of the 6 placed. Which monster died, and how, is not known (Open questions 2).

## 2. e05p02: census and the missing four

**Totals** [C]: `killcount01` of episode05 = 70, no `extrakills` (difficulty 1 in the log). The world.scr files hold
**59 placed monsters and 11 monster spawners (one monster each, all fired at most once)**: 59 + 11 = 70 exactly.

| area | placed (class x count) | monster spawners: what, trigger | total |
|---|---|---|---|
| a01 | 30: ratoid1 x8, roachoid1 x4, roach x4 (animals), pigoid2 x3, pigoid3 x1, securitymech1 x2, ratoid2 x2, hovermech1 x2, hovermech2 x1, femech2 x2, femech1 x1 | `spawner_rat`, `_rat2`, `_rat3`, `_rat4` (ratoid2/3/2/3, drops sc5/lc5) <- `area_switch_rats` (6416,4636,3088) | 34 |
| a01_r01 | 6: securitymech1 x2, hovermech1 x2, ratoid1 x1, ratoid2 x1 | `spawner_ambush`, `spawner_ambush1` (ratoid1 x2) <- **`area_ambush` (1580,340,480)** | 8 |
| a02 | 23: pigoid2 x5, pigoid1 x3, ratoid1 x5, ratoid2 x1, ratoid3 x1, roachoid1 x3, roachoid2 x1, hovermech1 x4 | **`spawner_ambush01/02` (pigoid1 x2, each drops sbullet) <- `area_ambush` (3568,156,-4352)**; `spawner_entity1/2` (femech2 x2) <- `area_switch_ambush2` (3568,-2952,-7616); `exit_spawner` (securitymech2, drops lglopp) <- `area_switch9` (2064,-232,-672), armed by `key_blue.picked_up` | 28 |

Every trigger is an `area_switch` with `switch_type:6`. **Type 6 = "Auto Trigger Once"** [A]: the enum names sit in
reverse at 0x10167c74.. (7 Auto Trigger Always, 6 Auto Trigger Once, 5 Auto Toggle, 4 Auto Buzzer, 3..0 Manual
Trigger Always / Trigger Once / Toggle / Buzzer; index 4 = Auto Buzzer matches cutaway.md). `area_switch_touch`'s
jump table 0x1009cc18, case 6 = 0x1009cb82: if off and Duke inside -> on, and nothing turns it off. One send of
`signal` = 1 fires every connected spawner once.

**Where the 66 came from** [L] (live kills per area visit, `ents:` enemies alive):

| area | counter | alive at the end | counted | of total |
|---|---|---|---|---|
| a01 | 0 -> 34 | 0 (28 alive at the first line, 2 kills in) | 30 placed + 4 rats | 34/34 |
| a01_r01 | 34 -> 40 | 0 | 6 placed (one died at entry, see section 1) | 6/8 |
| a02 | 40 -> 66 | 0 | 23 placed + 2 Fem Mechs (kills 61 -> 63 while alive stayed 2) + the exit Security Mech (line 40352) | 26/28 |

The 4 missing are exactly the 4 monsters of the two `area_ambush` switches. A spawned monster has no record before
its spawner fires, so a plain enemy ESP has nothing to draw for them.

**How to get them:** walk Duke into the volumes. `a01_r01`: the switch is centred at (1580, 340, 480); the rats
appear at the spawners (944, 308, 512) and (1084, 312, 512) and run to (1100, 520, 512). Reached from `a01` by the
`room1` or `room2` doors. `a02`: the switch is centred at (3568, 156, -4352); the Pig Cops appear at (3568, 156,
-4140) and (3568, 156, -4368). The nearest sampled `pos:` lines were 719 and 352 units from the two centres (the
volumes' extents were not read; Open questions 1).

## 3. spawner_entity at runtime

**Class** [D/A]: ctor `spawner_entity_ctor` 0x100b5770 (object_e base, vtable 0x1013dea0, `+0x48 |= 0x200000`, the
same type bit as `fader_entity`, so `spawn_search("s=%d?t=%d", sid, 0x200000)` lists spawners among faders, lens
flares and movers; filter by the class name). vt+0x60 = `spawner_s_handler` 0x100b5b80, vt+0x74 = init 0x100b5970
(not a Ghidra function). Editor values from the static evalue_def records at 0x1016c670.. (name +0x04, type +0x10,
size +0x18, offset +0x20) and the spawn_dir block at 0x100b5321 [A]:

| offset | field | notes |
|---|---|---|
| +0xA14 / +0xA18 | `spawnscript` list (char[64] each) / count | what one fire spawns, one entity per entry |
| +0xA1C / +0xA20 | `monst_spawnscr` list / count | appended to each spawned monster's own drop list (pickups.md section 6) |
| +0xA24 | `spawn_dir` (vec3) | the spawned entity faces it |
| +0xA30 | `spawn_dest` (vec3) | where it heads; the init stores the distance to it at +0xA4C |
| +0xA3C | `spawn_type` | enum (0x1016c640): 0 Snap Launch, 1 Artillery, 2 Linear, 3 Spray (multiple); launch style only |
| +0xA40 | `spawn_speed` float | default 300 |
| +0xA44 / +0xA45 | `spawn_on_start` / `teleport` bytes | init 0x100b5992 passes +0xA44 as the `active` pin's initial value; teleport makes a teleport effect entity at +0xAE0 |
| +0xA54 | pin `active` (input) | flag +0xAA8 (pin+0x54), value +0xAAC (pin+0x58) |
| +0xAE4 | entities spawned so far | ctor zeroes it; +1 per entity spawned; the name suffix |

- **Firing** [D]: `spawner_s_handler` resets the pin and calls `spawner_fire` 0x100b5f70 when the flag is set and
  the value is non-zero, every time. There is no count limit and no "spent" flag: a spawner fires as often as its
  input is set. `spawner_fire` returns without spawning unless the world runs (`[+0xE0]+0xC8 & 8`). Per entry it
  names the entity `"%s-spwn%03d"` (0x1016c9e4) from the spawner's name (entity+4) and `+0xAE4 % 1000`, then
  increments +0xAE4. A spawned monster is therefore `spawner_ambush01-spwn000`, and a second fire of the same
  spawner makes `-spwn001`.
- **A quirk** [D]: `spawner_fire` reuses its loop counter for the `monst_spawnscr` append, so a spawner with two
  `spawnscript` entries and a non-empty `monst_spawnscr` skips its second entry. No shipped monster spawner has two
  entries (section 4), so this changes nothing in the game.
- **States the ESP can show**, from +0xAE4 plus the map wiring (an input pin keeps no list of its senders,
  entities.md section 8, so the trigger comes from the parsed world.scr, as for the lasers):
  - **will spawn**: +0xAE4 == 0. Its trigger, when it is an `area_switch`, is also readable live: `+0xAE4` armed,
    `+0xAF4` on, `+0xB7C` switch_type (cutaway.md section 3); an armed type-6 switch still off is the ambush to walk into.
  - **spent**: +0xAE4 >= +0xA18 and the trigger is one-shot (type 6 switch, `trigger_once` timer, `broken`,
    `picked_up`). Its monsters are then live `<name>-spwn*` entities or dead.
  - **can repeat**: the trigger is a `laser` (pickups.md section 7: laser31 fired twice) or a repeating switch type
    (0, 1, 3, 4, 5, 7). +0xAE4 counts the fires.

## 4. Game-wide census [C]

Effective world.scr of the 24 parts (`e01_intro` is part of E1P1; demo maps left out):

- **111 monster spawners, each spawning one monster per fire** (no monster spawner has two scripts or
  `spawn_on_start`, and none is unwired). 101 fire once: 92 straight from a one-shot trigger (area_switch type 6, a
  `breakable_adv.broken`, a pickup's `picked_up`, a key), 9 through a `trigger_once` timer that a type-6
  area_switch starts. **10 are laser-wired and can
  repeat**: e08p01_a01 `spawner_laser9/10`, e08p03_a01 `spawner_entity1..8`. Of the other 88 placed spawners, 73
  spawn projectiles (`glopp_perm.sc` and the like), 12 pickups, 2 particles (e01_intro), 1 nothing.
- **22 of 24 parts have monster spawners** (not e04p03, e07p03). **19 parts need them** to reach `killcount`
  (placed non-boss monsters < killcount): all but e02p03, e03p01, e07p01, e07p03 and e04p03.
- **Eggs:** e04p01/02/03 and e06p01/02 also need hatched Roaches (wired eggs: 6, 14, 3, 7, 5 of 30, 36, 13, 22, 19).

| part | killcount | placed (bosses) | spawners | wired eggs | most reachable | | part | killcount | placed (bosses) | spawners | wired eggs | most reachable |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| e01p01 | 29 | 21 | 8 | 0 | 29 | | e05p01 | 57 | 49 | 10 | 0 | 59 |
| e01p02 | 58 | 54 | 5 | 0 | 59 | | e05p02 | 70 | 59 | 11 | 0 | 70 |
| e01p03 | 28 (+extra) | 27 (1) | 2 | 0 | 28 (+pigoids) | | e05p03 | 22 | 21 | 1 | 0 | 22 |
| e02p01 | 48 | 45 | 3 | 0 | 48 | | e06p01 | **85** | 68 | 9 | 7 | **84** |
| e02p02 | 48 | 46 | 4 | 0 | 50 | | e06p02 | **53** | 39 | 8 | 5 | **52** |
| e02p03 | 28 | 28 | 1 | 0 | 29 | | e06p03 | 13 | 12 | 1 | 0 | 13 |
| e03p01 | 84 | 87 | 4 | 0 | 91 | | e07p01 | 55 | 55 | 1 | 0 | 56 |
| e03p02 | 54 | 50 | 6 | 0 | 56 | | e07p02 | 40 | 38 | 2 | 0 | 40 |
| e03p03 | 41 | 39 | 4 | 0 | 43 | | e07p03 | 29 | 29 | 0 | 0 | 29 |
| e04p01 | 82 | 71 | 10 | 6 | 87 | | e08p01 | 29 | 24 | 5 | 0 | 29 (+laser repeats) |
| e04p02 | 76 | 69 | 1 | 14 | 84 | | e08p02 | 63 | 57 | 6 | 0 | 63 |
| e04p03 | 16 | 15 (1) | 0 | 3 | 17 | | e08p03 | 16 | 11 (3) | 9 | 0 | 17 (+laser repeats) |

"Most reachable" = non-boss placed + one fire per monster spawner + every wired egg.

- **Two parts cannot reach their listed kills by one fire each: e06p01 (84 of 85) and e06p02 (52 of 53).** e06p02_a02
  has a `timer1` wired to `spawner_entity1.active`, and no `spawner_entity1` exists in that map: a deleted spawner
  the count still includes. e06p01 has no such dangling wire (Open questions 3). Other dangling spawner wires, in parts with
  slack: e01p02_a01 `life1 -> spawner_bitch1`, e01p02_a01_r01 `breaking_door -> bitch_spawner1`, e01p03_a02
  `brk_intro -> spawn_air1`; e02p01_a01 has an empty `spawner_gator` (no script).
- Whole-game "all kills" award: the target is the sum (1130/1133/1136, save-awards.md), so surplus parts
  (e03p01 +7, e04p02 +8, ...) more than cover E6's two.

## 5. ESP Ambushes and Progress kills (verified in game: e05p02 ambushes, re-entry, K column)

- **Parse** (areas.h `ar_parse_world`): every `connect <pin>:<target>.active|.hatch` line of any entity is kept with
  its source; a monster spawner (a `spawnscript` under `scripts/monst/`) joins the ambush of the entity its first
  `.active` wire leaves from. A zone is that `area_switch`, or for a `timer` the switch that starts it; its volume is
  read offline (`maps/<map>/<switch>/avolume`, as doorways) and drawn unrotated (none of the 41 zone switches has a
  `rotation` key). The class each script spawns is read from the script once per part (`gator2.sc` spawns
  `monst_gator`). A Python mirror of the rule over the 24 parts' effective world.scr gives **111 monster spawners
  (101 once, 10 laser) in 72 ambushes**, and per part placed + one fire each + wired eggs equal section 4's "most
  reachable" column for all 24 (a Python mirror kept with the census scripts).
- **Live** (entities.cpp `ambush_entries`, Enemies > Ambushes): each spawner found by name every frame as the nuke
  lasers' are; +0xAE4 == 0 on every found spawner is "not fired". Not fired: an orange box round the spawners'
  parsed positions ("Ambush: 2x Pig Cop + Bullets +10", a non-zone trigger's word in parentheses) and an orange
  box at the zone ("Ambush trigger"); the box takes an off-screen arrow (Enemies have no arrow switch). A laser's
  ambush: a red box "(laser, fired N)" while the laser's +0xB90 is set. Fired: nothing (the monsters show as
  enemies). Each state change logs one `ambush:` line; the 5 s line counts `ambushes:` here, waiting, armed, unknown.
- **Re-entry**: an area rebuilt from its .msv zeroes every spawner's +0xAE4 (the ctor does, and the file
  does not hold it), but the trigger keeps its state: `area_switch_rats` (fired) stores its on byte as 1 in
  `e05p02_a01.msv`, `area_ambush` of a01_r01 (waiting) as 0. So an ambush also reads fired when its trigger has gone
  off (areas.h `ar_trig_read`): a zone's or timer's switch on (+0xAF4, cutaway.md section 3, by name, span 0xAF8); a
  breakable, pickup, crate or barrel with +0x2E0 clear or gone; a monster dead (+0xB84 <= 0) or gone. A timer's
  ambush reads fired from its switch on, before the timer's delay runs out. A laser's "fired N" is still lost on
  re-entry (its spawners restart at 0, and the laser keeps no count). Once fired, an ambush is not searched again in
  that world (the 5 s `ambushes:` line counts the name searches of a frame).
- **K reads low while uncounted monsters live**: `found = total - alive` also subtracts live `monst_*` the census
  does not count (the E1P3 Porkchopper's pigoids, a laser's second and later fires, any animal the G.L.O.P.P. ray
  leaves). The clamp at 0 hides it; K is right again once they die.
- **Checks**: at part load the log's `areas:   <map> kills N = P placed + S from A ambush(es) + E wired
  egg(s)` line is the C++ census check (e05p02: a01 34, a01_r01 8, a02 28). In e05p02_a01_r01 and _a02 the ambushes
  draw until walked into, then `ambush: ... fired` logs. **Re-enter e05p02_a01** after its rats fired: the rats
  ambush must not show, and K reads 34/34.
- Ambushes have no teleport marker.
- **Progress kills** (section "SECRETS, NUKES AND KILLS", areas.cpp `walk_kills`, areas.h `ar_kills_*`): each area's
  total is placed + one fire per wired monster spawner + wired eggs. A loaded area (1 Hz, and into our record each
  second): total - monsters alive (user bit 0x20, health > 0) - wired eggs not hatched and intact - the monsters of
  ambushes not fired (a laser's until its first fire); an ambush whose spawners are not found makes it "?". An
  unloaded area: our record of the last live view, else the .msv only for an area with **no ambush and no wired
  egg** (total - its `monst_*` class records), else "?". **Why "?"**: a spawner's pass-2 record is u32 id, u32 1,
  u32 0 for the four fired rat spawners of `e05p02_a01.msv` and the two waiting ones of `e05p02_a01_r01.msv` alike
  (the saved state files), so +0xAE4 is not stored. The Total row's K is the game's tally (+0x50)
  over the areas' total; when the part's killcount + extrakills differs, the row shows "game N (+d)" and the log's
  `areas: kills of ...` line says why (the extrakills pigoids, the e06p01/e06p02 shortfall, or a surplus). The map
  gets an "A" pip row per area: its once-firing ambushes, hollow while one waits.

## Open questions

1. The volumes' extents (the `area_switch` `path` meshes were not read); only centres are given. The ambush
   spawners' live `+0xAF4` was not read either.
2. Which monster died at `a01_r01` entry (kills 34 -> 35 with no Duke kill, 5 of 6 alive). Measurement: a
   per-monster death log line (class, name, attacker) in `monster_die`'s caller chain, or the `ents:` list by name.
3. e06p01's missing kill: the data holds 84 at most. Measurement: a full e06p01 run with the `ents:` and `stats:`
   lines, or a tally of every death by class against this census.
4. That an egg-hatched Roach counts (it is a `monst_roach` through a spawner, entities.md section 8; no log line ties a
   kill to an egg yet).
5. That `spawn_on_start` fires once at load (the init passes it as the pin's initial value; the first handler pass
   was not traced). No monster spawner uses it.
6. That a gibbed wired egg reads as a kill had: its Roach never exists, so the Progress count is one high against the
   game's tally. Measurement: gib a wired egg with Debug log and compare the `areas: found` K against `stats:` kills.
7. That a living monster is stored as a `monst_*` class record in its area's .msv (only files whose monsters were all
   killed were read, and they hold none). Measurement: leave an area with a monster alive and read its file.
