# Levels, secret exits and the stats the game keeps

**Secret levels/exits: one, and it is conditional.** Every shipped map is reached by the normal flow
except backgrounds (sky/stats) and the 28 `demo_*` test maps. The one hidden branch is **`e08p03_a04`, the Wozma
boss**: in the final map `e08p03_a02` a down-track to the `wozma` exit opens only while the cvar `g_dval6` is 1, and
the game sets it to 1 when, **on Hard, every nuke of the game has been banked** and Wozma has not been killed yet.
No other map has an exit whose target is off the episode path, and no other exit is gated on secrets or nukes.

**Stats outside mission complete: only the Load Game slots.** Kills/secrets/nukes N/M appear on the
part-end screen and, as **whole-game totals per save**, on each Load Game slot. The pause/Escape menu (= main menu),
episode select, F1 help (it shows only "location: episode : part n : A<area>") and the HUD (nukes found, no total)
show no N/M. No console command prints them; `g_map_info` shows four empty debug lines.

Binary: duke_base 0x10000000 (duke!), the Zoom Enhanced build. Method: Ghidra MCP and Python on the shipped data.
Tags: **[D]**
decompile, **[A]** disassembly, **[X]** xref/instruction search, **[S]** string, **[C]** shipped data (`world.scr`,
`episodes.def`, ui scripts, read from `duke/base/*.zipfs` with Python zipfile, effective file = highest mount
priority: `db_p101`/`db_mapfix` p50 over `db_e0N` p1, per `init.cfg`). Named in Ghidra:
`partend_stats_text` 0x100a9000, `partend_commit_stats` 0x100acef0, `session_stats_sum` 0x100eb540,
`loadgame_slots_fill` 0x100a3670, `stats_init` 0x100ea0c0, `stats_reset_part_tallies` 0x100ea370,
`stats_merge_best` 0x100ea210, `session_init` 0x100ea450, `map_episode_part_area` 0x100e9240,
`map_name_parse_epa` 0x100e9270, `player_add_nukes` 0x10079b90, `area_secret_on_enter` 0x1009be40,
`hud_update` 0x100a13c0, `hud_map_info_clear` 0x100a1350, `help_screen_fill` 0x100a0ad0.

## 1. Words: episode, part, area

- **Episode** = `episodeNN` in `scripts/episodes.def` (8). **Part** = what the player calls a level (3 per episode;
  `partNN:` gives its first map). **Area** = one map file; a part spans 2..7 areas joined by `map_change` entities.
  The part-end ("mission complete") screen comes once per part, from its `end_part` entity [C].
- The engine derives (episode, part, area) from the **map name** alone: `map_name_parse_epa` reads `eEE pPP _aAA`
  as chars 1-2, 4-5, 8-9, each minus 1, clamped at 0 [D]. So `e01_intro` = E1P1, and an `_rNN` map belongs to the
  same part and area as its parent.
- **`_rNN` maps are separately loaded rooms**, not revisits: exits named `room`, `room2`, `secret1/2`, `goodies`,
  `djbooth`, `platform`, `vent` lead into them [C]. Of the 21, 13 are dead-end side rooms (one entry, exits back
  only, e.g. `e01p01_a01_r01`, `e02p02_a02_r01` via `secret1`/`secret2`); 8 lie on the main route (e.g. `e03p03_a03_r01` and
  `e04p02_a03_r01` hold their part's `end_part`; `e05p01_a01_r01` joins a01 to a02).

## 2. Question 1: secret levels and exits

**Map census (129 maps)** [C]: 84 playable areas + `e01_intro` are reachable by following `map_change.target_map`
from the 24 `partNN` starts. The 16 unreached non-demo maps are backgrounds: `eNN_sky*` are pulled in by `skylink`
(`world_name:`), `eNN_stats*` by `end_part.background_map` (the part-end screen's 3D backdrop). 28 `demo_*` are
developer test maps; only `demo_stats` links to a shipped map (`e05p01_a01`).

**Branches that are not the straight line** [C]:

| map | exits | what it is |
|---|---|---|
| `e08p03_a02` | `end` -> `e08p03_a03` (forced by `end_part.finished`), **`wozma` -> `e08p03_a04`** | final map; see below |
| `e08p03_a04` | back to `e08p03_a02` dest `wozma`, forced 7 s after `boss_wozma1.dead` | **hidden boss level** |
| `e08p03_a03` | none | credits map (`credits` entity), entered after the last part-end |
| `e06p01_a03` | `end` -> `e06p01_a04` (walk-in), `end2` -> `e06p02_a01` (forced by `end_part1`) | `e06p01_a04` is a dead-end side area (1 secret, 2 nukes, 11 monsters) |
| `e07p02_a01` | `e07p02_a02_in` / `_out` -> `e07p02_a02` | side loop (3 nukes + 2 scripted nukes) |
| `e08p02_a01` | `lab1..5_change` -> `e08p02_a02..a06` | hub of five labs, each returns to the hub |
| `e02p02_a03` | `end` -> `_r01`, `end2` -> `e02p03_a01` (forced by `end_part1`) | `_r01` is a vent room |

Every part's `end_part` is activated by a switch, a `cmp` or a timer, and its `finished` pin forces the exit to the
next part [C, `connect` lines]. **No exit is wired to a nuke count, a secret count or a counter** other than Wozma's.

**The Wozma path** [C][D][A]:
- `e08p03_a02` holds `spawn cvar { cvar:"g_dval6"; connect get_value:auto_track1.area_active }`, polled by a
  1 s `timer6`. `auto_track1` is a `game_track_auto` (`area_start_active:false`) whose route runs from
  (15968, 520, -4800) down to -5376; the `wozma` map_change sits at (15968, 512, -5288) on it.
- **Sets 1:** `player_add_nukes` 0x10079b90, after every nuke pickup [D/A 0x10079c9d-0x10079cf8]: if difficulty
  `S+0x20 == 2` (Hard), and `session_stats_sum(S, nukes, Hard)` >= the sum of `nukecount` over all 24 parts (240),
  and `S+0x1C8 <= 0.0` (`FCOMP` vs 0.0 at 0x10126cbc, `TEST AH,0x41; JP` skip), it runs `g_dval6 "1"`.
- **Sets 0:** `wozma_damage` 0x10017633 on Wozma's death writes `S+0x1C8 = 1.0` and `g_dval6 "0"`; a
  `game_proc_handler` state at 0x100e5963 also writes "0" (which state was not traced).
- No code reads `g_dval6` (xrefs: the three writers only) [X]; the map's cvar entity is its only reader.
- The banked sum only counts **finished** parts (section 4), so the final part's nukes are banked only after the
  first win: a first Hard run cannot open the path at `e08p03_a02` (inferred, see Open questions).

**E8P3 reads 9/10 at Hard 240/240: the totals are right; an earlier part banked 11.**
- Measured in a Hard run (the panel's log): the Hard sum read 221 entering E8P2 (live 0, 22 parts banked), 231 once E8P2 banked
  10, and 240 at E8P3 with 9 live. 22 parts at `nukecount` 10 make 220, so one banked part holds 11: `e01p02`, the
  only part whose world.scr holds more nukes (11) than its `nukecount` ([areas-and-secrets.md](areas-and-secrets.md)
  section 4, "Not counted").
- `e08p03` holds exactly 10 [C, db_e08 world.scr]: `a01` 8 (3 placed `nuke`, 2 carried by `monst_femech32` and
  `monst_securitymech31`, 3 from `spawner_entity3/4/6` `monst_spawnscr` nuke.sc), `a02` 2 placed; `a03` (credits)
  and `a04` (Wozma) none. The log has a01 5/5 placed and carried, a02 2/2, tally 9: the missing one is a spawner's.
- So the 240 is the sum of `nukecount`, not of what each part holds, and an extra banked nuke anywhere lets it be
  reached with one missing elsewhere. The panel keeps `nukecount` per part (the part-end screen's own total); no
  Wozma-area nuke exists to count.

Dead data, not secrets [C]: `e02p03_a01.start` targets `e02p02_a04`, which does not exist; `e03p03_a03.start`
targets `e03p02_a02`; `e04p03_a01` has an `end_part1` that nothing activates (the part ends in `e04p03_a02`);
`e08p03_a01.start` is `area_start_active:false` and nothing activates it.

## 3. Question 2: where N/M is shown

| place | shows | evidence |
|---|---|---|
| Part-end screen (`scr_part_end.sc`, windows 30/40) | "PART n COMPLETE", TIME hh:mm:ss, KILLS/SECRETS/NUKES `found / total` for this part; `- / total` for a row not counted up yet | `partend_stats_text` 0x100a9000 [D], strings 0x10169ed8/0x10169ef4/0x10169f10 [S] |
| **Load Game slots** (`scr_loadgame.sc`) | per save: EPISODE n or COMPLETE, PART n, TIME, KILLS/SECRETS/NUKES `found/total` summed over the **whole game** for the save's difficulty; "NO PARTS COMPLETED" otherwise | `loadgame_slots_fill` 0x100a3670 [D/A 0x100a388b-0x100a3a5f], "%d/%d<br>" 0x101691f0 [S] |
| HUD | nuke counter = the part's nukes found (window 10, green once `+0x5C > 0`); no total | `hud_update` [D] |
| F1 help | "LOCATION: <episode title> : PART n : A<area>" | `help_screen_fill` 0x100a0ad0 -> string 0x10168c48 [D/A] |
| main menu (= Escape in game), episode select | nothing (episode select shows art, story text, PLAY/REPLAY) | scr_mmenu.sc [C], 0x100a0660 [D] |
| `g_map_info 1` (a cheat-config cvar) | shows HUD texts id 1000-1003 ("map info - for debug purposes only", scr_hud.sc); nothing writes them except `hud_map_info_clear`, which sets "" | `hud_update` 0x100a1423/0x100a14f4 [A], 0x100a1350 [D], PUSH 0x3e8 scan [X] |

The save files are `duke/base/save/*.sav` (not `.msv`); the slot list reads each save's session record.

## 4. The fields (what an in-level readout reads)

Roots [D/A]: `G = *(duke!0x101869dc)` (the game_globals object). **Session `S = *(G+0x10)`**
(record 0x2A87C bytes; also the save-game header). The game state the cheat list calls "game" is `*(G+8)`; its +0xEC is the
current world, whose +0x48 is the map name fed to `map_episode_part_area`.

**Stats record** (87 dwords, 0x15C bytes; `stats_init` zeroes it and sets +0x04 = 1, the bullet pool +0x38 = 25
(short) and +0x60..+0x6C = 100, 200, 50, 400; the last three are the ammo caps of [pickups.md](pickups.md) §4) [D]:

| field | meaning | evidence |
|---|---|---|
| +0x48 float | nukes found | `player_add_nukes` `+= n`; table 0x10178548 {"nukecount",0} [D/A] |
| +0x4C float | secrets found | `area_secret_on_enter` `+= 1` and +5 health [D] |
| +0x50 float | kills | the `give kill` cheat; table {"killcount",2}; part-end reads it; real kills are added only by `monster_die` 0x1002fdb0 ([kills-and-spawners.md](kills-and-spawners.md) section 1) [D] |
| +0x54 float | bombs/babes (exit objective) | pickups and the `give` cheats; cleared per part [D] |
| +0x58 float | part time (seconds) | load-screen field 4 printed hh:mm:ss; best = smallest non-zero (`stats_merge_best`) [D/A] |
| +0x5C float | nukes already converted into a `power` bonus | `player_add_nukes` [D] |

**Live record = `S+0x223D4`**: the player's `+0x111C` points at it (`FUN_100763c0` 0x1007642f [A]). Copies:
`S+0x22530` (written each `player_tick`, 0x1007b4e8 [A]) and `S+0x22278` (role not traced).

**Lifetime: per part.** `partend_commit_stats` (called from `end_part_activate` 0x100ad290) is the only caller of
`stats_reset_part_tallies`, which zeroes +0x48..+0x5C, +0x70, +0x74 and the keys +0x42..+0x47 [D/X]. Moving between
areas (`map_change`) does not touch them. A full `stats_init` runs from `session_init` and from the game-start path
`FUN_100e2640` 0x100e2d02 [A] (condition not traced).

**Banked per-part records** (written at part end) [D]: `S + 0x2B8 + (((diff*20 + ep)*5 + part) * 0x15C)`; first
completion copies the live record, a replay keeps the best of each field (`stats_merge_best`). Completion bytes:
`S + 0x28 + diff*100 + ep*5 + part`. Difficulty: `S+0x20` (0 easy, 1 normal, 2 hard). `session_stats_sum(S, field,
diff, &total)` sums `+0x48 + field*4` over the 100 records of one difficulty (fields 0 nukes, 1 secrets, 2 kills,
4 time).

**Totals: `episodes.def`, never counted from the world** [D][C]. The def pool is `*(G+0x60+5*4)` +8, loaded lazily by `0x100e6d10(G, 5)` (`mm_precache("scripts/episodes.def")`); `def_pool_t::find` and `def_obj_t::get_int` are imports [A]. `partend_stats_text` reads the episode def object
(`def_pool` 5, `find("episode", ep)`): `killcount<part>` + `extrakills<part>[difficulty]`, `secretcount<part>`,
`nukecount<part>`; the screen shows `max(total, found)` as the denominator. Our corner readout copies that for kills and
secrets, but shows nukes raw over the listed total (e.g. 11/10 after a laser fired twice, [pickups.md](pickups.md) section 7);
the Progress tab shows kills raw too, so its rows add up to the Load Game slot's total ([save-awards.md](save-awards.md) section 1). Patch values (`db_p101` overrides
`db_e01`: secrets E3P1 7->6, E4P2 4->3, E5P1 6->3; kills E5P3 23->22):

| | E1 | E2 | E3 | E4 | E5 | E6 | E7 | E8 |
|---|---|---|---|---|---|---|---|---|
| kills P1/P2/P3 | 29/58/28 (+6/9/12 by difficulty) | 48/48/28 | 84/54/41 | 82/76/16 | 57/70/22 | 85/53/13 | 55/40/29 | 29/63/16 |
| secrets | 5/5/1 | 1/1/2 | 6/5/3 | 2/3/0 | 3/7/2 | 2/3/1 | 2/0/1 | 0/0/0 |
| nukes | 10 each part | | | | | | | |

Cross-check against the maps [C]: `area_secret` entities per part equal `secretcount` in all 24 parts (55). Nukes
(`nuke` entities + crate/barrel code 0 + `spawnscript`/`monst_spawnscr` "scripts/pwrup/nuke.sc") are 10 per part,
except E1P2 with 11. Placed monsters do not match `killcount` (spawners add more), so a kill total must come from the
def, not an entity count.

## Open questions

1. The Wozma path in game: that `auto_track1` is the only way to the `wozma` exit (geometry not checked), and that a
   first Hard run cannot open it (needs the final part banked, e.g. by replaying E8P3 on Hard after a win).
   Shortcut to test: `g_dval6 1` in the console, then `map e08p03_a02` (the cvar is archived, so reset it after).
2. The HUD nuke counter reads +0x48 (from a lossy `hud_update` decompile).
3. Whether a part replayed from episode select starts with a cleared live record (`FUN_100e2640` condition).
4. `g_map_info`: that nothing else (a script, the `ui` command) fills texts 1000-1003; it was not tried in game.
5. Whether the walk-in exits to side areas (`e06p01_a03.end`, `e07p02_a01` in/out) are behind locked doors.
