**Kills.** The Load Game slot's whole-game total (1136 on Hard) is `killcount + extrakills[difficulty]` summed over
all 24 parts, exactly what `ls_totals` gives (1124 + 12). A Progress row (`pg_readouts`, menu.cpp) showing `max(total, found)` lifts its own
denominator when it has more banked kills than its total; added up, such rows overshoot the slot by the sum of those
excesses (1157 against 1136 on a complete Hard save, an excess of 21, inferred). The Progress kill readouts therefore
read over the listed total (`pg_column_raw`, test tb_0485).

**Awards: 16 glyphs in `fonts/awards.font`, 13 reachable.** The left panel shows per difficulty a
finished/in-progress radiation icon and the all-nukes / all-secrets / all-kills icons tinted by difficulty; the
stats box shows the three difficulty bonuses (Mighty Boot, Mighty Duke, X-3000), "beat Wozma" and "CHEATED!".
**The "?" is the all-secrets award**, not a locked one. Nothing is "unknown". The other two, B (all babes) and E
(double damage), are never earned (section 2). The menu's Progress tab has a SAVE AWARDS grid (section 3, tests
tb_0486-0488).

**CHEATED.** The cvar check runs every game tick, and the stock Enhanced widescreen FOV trips it. WideGen's
`g_cam_fov` "79.7553406" against the default "50" marks the session on the first tick, before the panel does anything
(the engine log). The panel's three flagged cvars read their defaults during the check (section 4).

**X-3000 on another difficulty.** It carries; no panel option is needed. The bonus flag `S+0x1C4` is in the save
file's header and only a new game clears it. Every map load gives the X-3000 to the player whenever the flag is set,
whatever difficulty is being played (section 5; static).

Binary: duke_base 0x10000000, the Zoom Enhanced build. Method: Ghidra MCP and the shipped data. Tags as
[levels-and-stats.md](levels-and-stats.md): **[D]** decompile, **[A]** disassembly, **[X]** xref/instruction search,
**[S]** string, **[C]** shipped data. Named in Ghidra: `loadgame_awards_panel` 0x100a40d0, `loadgame_awards_stats` 0x100a4370,
`session_all_parts_done` 0x100eb6d0, `session_any_part_done` 0x100eb790, `session_next_part` 0x100eb3b0,
`cheat_cvar_check` 0x100e13d0; for section 5: `game_map_load_step` 0x100e1e80,
`game_start_part_from_save` 0x100e2640, `cmd_load` 0x100e3d60, `session_read_file` 0x100eac50,
`stats_merge_inventory` 0x100ea1a0, `player_select_weapon` 0x1007a500 (player vt+0x240), `player_pickup_weapon`
0x100797b0, `player_apply_give` 0x100793a0, `player_give_item` 0x1007dba0, `player_net_recv` 0x1007e810.

## 1. The kill total

**The slot** (`loadgame_slots_fill` 0x100a3670 [A 0x100a3a3d-0x100a3a78]): `found = session_stats_sum(S, 2, diff,
&total)`; it prints `"%d/%d"` (0x101691f0) as `found / max(total, found)` (`FCOMP; TEST AH,0x41`), once, over the
whole game's sums.

**`session_stats_sum`** 0x100eb540 [A]:
- found: `+0x48 + field*4` summed over the 100 records of `diff` (0x100eb571-0x100eb590).
- total: for each `episodeNN` from 0 while the def has it, parts `0 .. partcount-1`: `get_float(fieldname, part)`
  (the table 0x10178548 gives `killcount` for field 2, so key `killcountPP`), and for kills only
  (`CMP [ESP+0x128],2`) `sprintf("extrakills%02d", part)` (0x10169fa4) then `get_float(that, S+0x20)`, i.e. key
  `extrakillsPPDD` with DD the **save's own** difficulty `S+0x20`, not the `diff` argument (0x100eb65d-0x100eb688).
- `partend_stats_text` 0x100a9000 [D] reads the same two keys for one part, also with `S+0x20`.

**The def** (`scripts/episodes.def`, effective copy `db_p101`) [C]: `extrakills` exists only for **E1P3**
(`extrakills0200:6`, `0201:9`, `0202:12`, "porkchopper pigoids"). Hard: killcounts 1124 + 12 = **1136**; Normal
1133; Easy 1130. So:
- extrakills counts on every difficulty, for E1P3 only; the Hard index (02) is not off by one;
- spawned enemies are not counted by any total: totals come from the def alone ([levels-and-stats.md](levels-and-stats.md)
  section 4), and `ls_parse_def`/`ls_totals` read both keys the same way (test tb_0392, tb_0485).

**Cause of 1157.** A `pg_readout(..., raw=false)` prints `max(total, found)` per part and per episode, as the
part-end screen does for one part. A part's banked kills can pass its `killcount` (spawners, `stats_merge_best`
keeping the best run), so those rows show found/found. The slot raises only the whole-game sum. 1157 - 1136 = 21 is
the inferred excess (the save file is packed; the banked records were not read).
`pg_column_raw` (progress.h) prints nukes and kills over the listed total, so the part rows and the episode totals
each add up to 1136 (test tb_0485 with a save banking 1133: rows 21 over and 24 short).
Secrets keep max(): every part's `area_secret` count equals its `secretcount` (levels-and-stats.md section 4).

**Quirk, copied by the awards grid:** the slot's left panel calls `session_stats_sum` for all three difficulties, but
extrakills always uses the save's own difficulty, so E1P3's 6/9/12 follows the save, not the row: a Hard save judges
every column's all-kills icon against 1136, an Easy save against 1130. The Progress tab's part and episode rows keep
each difficulty's own extrakills (their totals are the part-end screen's, for the save being played).

## 2. The awards

`loadgame_slots_fill` builds two texts per slot (window ids from `scr_loadgame.sc` [C], slot 0 shown): **window 800**
"icons" (over `ui/shd_loadgame/awards.shd`, the left panel) from `loadgame_awards_panel`, and **window 400**
"statistic values" (the stats box) whose last `%s` (format 0x10169158) is `loadgame_awards_stats`. Both wrap their
glyphs in `<FONT FACE=awards.font XSCALE=0.8 YSCALE=0.8>` (0x10169464). Glyph names are the font file's own comments
(`fonts/awards.font`, db_e01) [C]; the pictures are `fonts/awards.tga`.

| glyph | picture | font comment | where | condition (all per save) | field | evidence |
|---|---|---|---|---|---|---|
| J / K / L | green / orange / red radiation | easy / normal / hard difficulty | panel, line 1 | every part of every episode finished on d | bytes `S+0x28+d*100+ep*5+part` all set | `session_all_parts_done(S, d)` [D 0x100eb6d0], glyph `'J'+d` [D 0x100a40d0] |
| M / N / O | grey radiation, coloured ring | ... (in progress) | panel, line 1 | not all done, and d is the save's difficulty `S+0x20` or any part finished on d | same bytes | `session_any_part_done` [D 0x100eb790], `'M'+d` |
| A | hexagon (tinted) | all nukes | panel, line 2+d | `session_stats_sum(S,0,d)`: found >= total (240) | records +0x48 | [D 0x100a40d0], glyph 0x101694bc |
| C | "?" (tinted) | all secrets | panel, line 2+d | found >= total (55) | records +0x4C | glyph 0x101694b8 |
| D | robot skull (tinted) | all kills | panel, line 2+d | found >= total (1130/1133/1136 by the SAVE's difficulty, in every column: section 1 quirk) | records +0x50 | glyph 0x101694b4 |
| F | boot | mighty boot bonus | stats box | `S+0x1BC > 0` | float `S+0x1BC` | [D 0x100a4370]; set below |
| G | gold shield | mighty duke bonus | stats box | `S+0x1C0 > 0` | float `S+0x1C0` | as F |
| H | striped "X3K" | x3000 | stats box | `S+0x1C4 > 0` | float `S+0x1C4` | as F |
| W | green orb | beat wozma boss | stats box | `S+0x1C8 > 0` | float `S+0x1C8` | `wozma_damage` 0x10017633 writes 1.0 [A] |
| I | pink face | CHEATED! ;( | stats box | any of bytes `S+0x24`, `+0x25`, `+0x26` set | byte per difficulty `S+0x24+d` | [D 0x100a4370]; set below |
| E | red X | double damage | stats box | `S+0x1B8 > 0` | float `S+0x1B8` | read only: no writer in duke_base [X, every `0x1b8]` operand], only `session_init`'s clear (0x100ea46f); the string `double_damage` (0x10164e90) is the 30 s power-up (0x100799c0), not this flag |
| B | grey X | all babes | - | never emitted | - | no glyph `'B'` in either builder [D] |

**Nothing else.** `scr_loadgame.sc` (db_e01 and db_wide) [C] only places the windows (700 the panel image, 800 the
icons, 300/400 the stats box); every condition is in the two builders: the panel emits
J/K/L or M/N/O and A/C/D per difficulty, the stats box E, F, G, H, W and I. No per-episode or babe award exists.

**Colours per difficulty** (A/C/D): `<COLOR VALUE=%08X>` (0x1016949c) with 0xFF00EF28, 0xFF12AEFF, 0xFF1E1DFF
[D 0x100a40d0]. Read as AABBGGRR (the only reading that matches the red Hard row and the J/K/L ring colours):
Easy green, Normal orange, Hard red. Each difficulty row ends `" <br>"` even when empty, so Hard is always line 4.

**The bonuses F/G/H** are set by a part-end screen method at 0x100a98d0 (no Ghidra function; its vtable slot
0x1013bc2c sits beside `partend_stats_text`'s at 0x1013bc48) [A 0x100a991e-0x100a9b30]. For d = Easy, Normal, Hard
in turn, it sets `S+0x1BC+4d = 1.0` when all hold: not already set; the save's difficulty `S+0x20 == d`;
`session_stats_sum(S, nukes, d)` found >= total; `session_all_parts_done(S, d)`. It then shows
`@@b_mighty_boot@@` / `@@b_might_duke@@` / `@@b_x3000@@` (0x1016a02c / 0x10169ff4 / 0x10169fe8) in window 50 and
calls 0x100e1930. In words: **finish every part on d with all 240 nukes banked on d.** What each bonus does
(S reached as `*(game+0x10)`, `game` = `DAT_101869dc`):
- **Mighty Boot** (`S+0x1BC`): the kick's damage, 15, is doubled to 30 (`FUN_1007dfe0`, called by the kick code
  `FUN_10082b70`; the same function multiplies it by 10 more while `S+0x223EC > 0`, not traced).
- **Mighty Duke** (`S+0x1C0`): the player takes half damage (`FUN_10078f10`, `param_2 *= 0.5` at its entry).
- **X-3000** (`S+0x1C4`): every map load gives the player the X-3000 (weapon 7, inventory byte `S+0x223DF`), on
  whatever difficulty the save is played (section 5). Weapon cycling (`FUN_1007e280`) also runs over 9 slots
  instead of 8 while it is set; slot 8 has no weapon, so that part changes nothing (section 5).
The readers 0x1007acb0 and 0x1007d116 named earlier read `+0x1C4` of the player object, not S: unrelated.
**"Double damage" (E, `S+0x1B8`) is not the Mighty Boot**: it is its own field, next to it, with no writer.
The panel's row reads "Nuke bonus" and each tooltip names the effect.

**The cheated byte** `S + S+0x20 + 0x24` (the save's current difficulty) is set by:
- the `give`, `camera` (0x100e48b0) and `overkill` cheat commands;
- **`cheat_cvar_check`** 0x100e13d0 [D], called on **every game tick** (section 4) from `game_proc_handler`
  0x100e0c80 at 0x100e1320 [A]: it walks the cvar list and sets the byte when a cvar whose byte **+0x6A** is non-zero
  has a value string different from its default (`strcmp(*(cv+0xB4), *(cv+0xB8))`). Static duke_base cvars with
  +0x6A = 1 [read 0x10176730, 0x10176f50, 0x10177020, 0x10175070]: `g_p_god`, `g_cam_far`, `g_cam_fov`, `g_fps`;
  `g_hud` has 0. **The panel writes three of those:** Draw the whole map (`g_cam_far`, nocull.cpp), Custom FOV /
  freecam zoom (`g_cam_fov`, zoom.cpp) and First person (`g_fps`, firstperson.cpp). They read their defaults during the
  check (section 4).
Each sets the byte once and prints a random `@@cheat001..003@@` taunt (0x100e1470).

**A complete Hard save's slot**: panel line 1 L (Hard finished, red radiation); line 4 A + C in red (all
nukes, all secrets on Hard; the "red hexagon with ?" is the two glyphs side by side); no D (kills 1133/1136). Stats
box: H (X-3000: all Hard parts finished with 240 nukes), W (Wozma beaten), I (cheated on some difficulty).

## 3. The Progress tab's SAVE AWARDS grid

At the top of the Progress tab, under "This level" when present. Rules in `progress.h` (`aw_build`, `aw_cell_text`,
`aw_tip`, `aw_cheated_text`; tests tb_0486-0488), read with the records at 1 Hz (`progress_awards`), drawn by
`aw_section` (menu.cpp). Columns Easy/Normal/Hard in the A/C/D tints as ARGB (0xFF28EF00, 0xFFFFAE12, 0xFFFF1D1E).

| row | earned (the game's own test) | missing, played | glyph |
|---|---|---|---|
| Finished | J/K/L: completion bytes AND-ed over the def's parts | "2 / 24 parts" + bar | trefoil |
| All nukes | A: banked sum over the 100 records >= 240 | "14 / 240" + bar | hexagon |
| All secrets | C: banked sum >= 55 | "x / 55" + bar | "?" |
| All kills | D: banked sum >= the save's kill total (1130/1133/1136 by `S+0x20`) | "1133 / 1136" + bar | skull |
| Bonus weapon | F/G/H: `S+0x1BC+4d > 0` | the name, dim | the name in the tint |

- **Played** is the game's M/N/O condition: d is the save's difficulty `S+0x20`, or a part on d is finished. Not
  played: "not played" (Finished) and "-" (the panel fonts are ASCII, no em dash).
- **Shown counts** take the part in play as max(banked, live) on the game's difficulty; an award is earned only
  from the banked sums or flags, never from the live counts.
- **Kill totals** follow the slot exactly: every column uses the save's extrakills (`session_stats_sum`
  0x100eb65d-0x100eb688, section 1 quirk); the tooltip of a column that is not the save's difficulty says so.
- A missing count that the part in play has brought to its total reads "240 / 240 (at part end)" (the note on its
  own line when the slot is narrow): it is earned once the part's end banks it.
- **Footer:** green orb + "Wozma beaten" (W, `S+0x1C8 > 0`) or dim "Wozma not beaten" with the path's tooltip;
  "Cheated on ..." in Hard red for each set byte `S+0x24+d` (I); a dim "Unused by the game: All babes, Double
  damage" with a tooltip for B and E.
- The log line `progress: awards ...` every 5 s gives each cell's state (E earned, m missing, - not played).

## 4. When the cvar check runs, and keeping the panel's cvars out of it

**When** [A 0x100e0c80-0x100e138d]: `game_proc_handler` 0x100e0c80 is the game process's vtable +4 handler (data
ref 0x10145b14; quit-path.md section 2). `main_loop_iteration` calls it every iteration while the process is awake
(render.md section 1, prio 98: after the spawner, before the client's world pass and our `rend_end_frame` hook). The
call at 0x100e1320 is unconditional, after the state switch, in every state. The check itself returns at once while
the session's byte is set or `game_globals` is null; otherwise it reads every flagged cvar each tick.

**cvar_t::set** p3shared 0x1000a7d0 [D]: follows +0xBC, runs the +0x64 callback (null on all three cvars [read the
records 0x10175010, 0x10176fc0, 0x10176ef0]), sets +0x70 modified and copies at most 0x3F chars into +0x72. No
print, no notification. Defaults: `g_fps` "0", `g_cam_fov` "50", `g_cam_far` "8000".

**The stock Enhanced launch marks every session [S the engine log].** WideGen's
`init_widescreen_fov.cfg` runs `uset g_cam_fov 79.7553406` at line 290; `cvar_register_variable` (prism3d!0x406660
[D]) keeps the static default "50" at +0x20 and the value in +0x72. Line 1327, the first game tick, prints
`<game_base_proc> @@cheat001@@`, **before the panel's first line** (its mounts, 1328-1329) and its own
`uset g_cam_fov 85.0000` (1330). After the attract demo's `server_load` restores its session, line 1409 prints
`@@cheat003@@` again. `g_fps` is "0" in config.cfg and nothing else flagged is set, so `g_cam_fov` is the trigger.
**The I glyph** on a complete save is therefore explained by playing the Enhanced edition at a non-4:3 mode, panel or not;
whether a `give all` also marked it cannot be told apart (same byte).

**The fix (cheatmark.cpp):** a detour on `cheat_cvar_check`'s entry (59-byte masked fingerprint, unique
in duke_base, armed on the first frame once the three cvars resolve to duke_base's own static records). Around the
original call, each of `g_fps`, `g_cam_fov`, `g_cam_far` whose value differs from its default is set to its default
through `cvar_t::set`, and its value is set back right after. Nothing else runs in that window, so no feature sees
the default. WideGen's FOV is covered too: the rule is "off default", not "written by the panel". `g_p_god` is left
alone. The cheated byte is never written. A cvar that is linked (+0xBC) or whose pointers are not its own buffers
is left to the check and counted. Log: `cheatmark: armed`, the first hold of each cvar with its strings, a
cumulative counter line every 5 s when a count moved. Suite tb_0490-0493.
**A save already marked stays marked**: the byte is loaded with the save and nothing clears it.

## 5. The X-3000 on another difficulty of the same save (static)

**The weapon is slot 7, not 9** [C db_e01 `scripts/player.def`]: `weap00`..`weap07` are golden_eagle, pipe_bomb,
shotgun, assault_rifle, glopp_ray, rocket_launcher, pulse_cannon and `weap07` = `scripts/weap/x3000.sc`. It sets no
`ammo_type`/`ammo_min` of its own, so it inherits the base `weapon`'s 0 (bullets) and 1. The owned flag of weapon i is
the byte `PS+4+i` (PS = `S+0x223D4`, entities.md), so the X-3000's flag is **`S+0x223DF`**. The weapon precache
reads `S+0x223D8+i` for i = 0..9 (`FUN_100e4bd0` 0x100e4cd2 [A]). `player_select_weapon` 0x1007a500 [D] (player
vt+0x240) accepts only `0 <= i < 8` with `PS+4+i` set. So the 9-slot cycle in `FUN_1007e280` while `S+0x1C4 != 0`
only adds slot 8, which has no def (`find("weap", 8)` is null) and no owned flag. That part is a harmless stock quirk;
the grant below is what does the work.

**Who gives it: the map loader, on every map, on any difficulty.** `game_map_load_step` 0x100e1e80 [D] is the game
process's load state machine (step byte `+0x534`). Its step 2 runs `if (0.0 < S+0x1C4) S+0x223DF = 1`
(0x100e1fcb [A], `MOV byte ptr [ECX+0x223df],1`) and then precaches the owned weapons. Nothing reads `S+0x20` on that
path. It is the only immediate write of that byte in duke_base [X: every `MOV` to `+0xb]`/`+0xc]`, every
`0x223d` operand]. Everything else that grants a weapon misses it:
- `player_pickup_weapon` 0x100797b0 [D] (net message kind 3, through `player_net_recv` 0x1007e810 ->
  `player_apply_give` 0x100793a0) switches over pickup gun types 0..5 only (bytes +4, +6, +7, +8, +9, +10).
- `give weap 7` (`player_give_item` 0x1007dba0 [D]) checks `S+0x1C4 > 0` and then sends kind 3 with n = 7, which
  that switch drops. **The console `give` can never grant the X-3000**, and it would mark the save CHEATED anyway.
- `stats_merge_inventory` 0x100ea1a0 [D] ORs the 10 flags `+4..+0xD` from the previous part's banked record into
  PS at a part start. It carries a flag; it does not create one.

**How a save is replayed on another difficulty.** The episode screen (`ui/scripts/scr_episode.sc`: Easy/Normal/Hard
radio buttons, Replay/Play) issues `load "<save>" <ep> <part> <diff>` (format 0x101685f8, built at 0x100a0464 [A]
from the screen's save name `+0x1E8` and difficulty `+0x208`). `cmd_load` 0x100e3d60 [A] queues it as request kind 1
(`FUN_100e56f0`; the queue's jump table 0x100e5a30: 0 = `map` with `session_init`, 1 = load, 2 = demo, 3 = `new`).
`game_start_part_from_save` 0x100e2640 [D] then:
- reads the save with `FUN_100ea650` -> `session_read_file` 0x100eac50 [D]. Every mode reads the 0x2B8-byte
  header `S+0..+0x2B7` first, so `S+0x1BC..+0x1C8` (the bonuses and Wozma) come back from the file;
- sets `S+0x20` to the requested difficulty. With none given, it uses the save's own difficulty and moves past
  every difficulty whose parts are all done;
- builds PS: either the part-start copy `S+0x22278` (same part resumed), or `stats_init` (pistol + 25 bullets)
  merged with the previous part's banked record **on the new difficulty**; then it starts the map load.
It never calls `session_init`. Only a new game (`FUN_100e2fd0`, request 3) and the `map` command (request 0)
call it, and they clear `S+0x1C4` (0x100ea46f).

**Result:** a save that earned the X-3000 (all Hard parts with 240 nukes) keeps `S+0x1C4` across every load, and
step 2 hands the weapon to the player at every map load on Easy, Normal or Hard. It is selectable by `weap 7`, or
by cycling while the player has at least 1 bullet (`weap07` inherits `ammo_min:1`, ammo type 0;
`FUN_1007e280` checks that, `player_select_weapon` does not). The panel has no option for it, since it
carries.

## Open questions

1. The 21-kill excess: the save file is packed, so the per-part banked kills were not read. Check: the Progress tab
   on Hard shows rows like "K 33/29"; their excesses should sum to 21.
2. Whether a new game clears the byte the launch set (the attract demo's restore did clear it: `@@cheat003@@` fired
   again), and that the detour stops the taunt. Check: start a new game with the panel deployed; the engine log shows
   no `@@cheat00N@@` after `cheatmark: armed`, and the new save's slot has no I glyph.
3. The bonuses' in-game effects, and when the part-end method 0x100a98d0 runs (assumed: the part-end screen of
   every part, so the bonus lands at the first part end after the conditions hold).
4. `S+0x1B8` ("double damage") has no writer in duke_base; prism3d/p3shared were not searched (they do not know the
   session layout; only a whole-session copy such as a save load could carry a value in).
5. (Section 5.) Whether the X-3000 shows up on Easy/Normal of a save with the H glyph. The code is read, not run.
   Check: on the Load Game slot that shows H, pick a part on Easy in the episode screen and press Play. The X-3000
   should be in the weapon list right from the start (cycling with at least 1 bullet; key binding not read), and the
   save gets no new I glyph.
