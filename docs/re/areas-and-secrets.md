# Areas and secrets across a part

Every secret's found flag survives an area change, so a cross-area finder can be built (the menu has one). The game
keeps a world it left in memory (a `change_type:0` doorway) or stores it to `duke/base/state/<map>.msv` when it
unloads it. Both keep each `area_secret`'s `+0xAE4`. A part's area graph is static data: 187 `map_change` doorways
in 24 parts, 63 two-way pairs. In E5 most of the maze is inside an area: 60 `area_teleport`s, which the floor graph models as one-way edges.

Tags as [levels-and-stats.md](levels-and-stats.md): **[D]** decompile, **[A]** disassembly, **[X]** xref/instruction
search, **[C]** shipped data (effective `world.scr` per `duke/init.cfg` mounts: `db_mapfix`/`db_p101` p50 over
`db_eNN` p1), **[F]** files in the state folder, **[L]** the mod's own `nukepanel.log`, **[P]** a played part's state folder.
Script: `scripts\area_graph.py` in the Ghidra project (no args: one row per part; with part
prefixes: every doorway, teleport and secret). Its output is in `dumps\area_graph.summary.txt` and
`dumps\area_graph.parts.txt`. Names set in Ghidra: `world_store_state_msv` 0x100e1a00,
`world_load_or_restore_msv` 0x100e1b00, `world_release_store_msv` 0x100e39d0, `world_queue_unload` 0x100e3970,
`world_find_or_load` 0x100e2520, `state_msv_delete_except_prefix` 0x100eb230, `area_store_user_state` 0x10091b30,
`area_restore_user_state` 0x10091b70; created functions `area_vtE0_store` 0x10099c90 and `area_vtE4_restore` 0x10099cd0;
in prism3d, `srv_gs_store_impl` 0x40b5f0 and `srv_gs_restore_impl` 0x40b970.

## 1. The area graph

The graph comes from `world.scr` alone. Each doorway names its target area and a landing entity
there. 126 of 187 doorways form two-way pairs (A's door lands on B's door, and B's door leads back to A). 69 have a
depth-run end within 400 units in x/z, and 59 of those within 128.

**A doorway** [C]: `spawn map_change { change_type, target_map, target_dest, rotation, position, name }`.
- Its volume is `maps/<map>/<name>/avolume` (244 bytes). The first 24 bytes are six floats, a local min (x y z)
  then a max (x y z), offset from `position`. A plane list follows. Whether `rotation` turns the box is
  open: on `e05p01_a01_r01` both doors (rotation 90 and 270) only fit their track's two ends unrotated. The
  DLL reads volumes live (vt+0x1B8) for the loaded area, so this only matters for offline data.
- **Landing** [D `area_changemap_place_player` duke!0x1009ad00]: in the target world the game runs
  `spawn_search("s=%d?n=%s?t=%d?g=%d", world+0xD4, target_dest, 0x20, 0x10)` (string 0x10167370). It finds the
  area named `target_dest`, so the landing is the target's doorway of that name. Duke is put at its `+0x18C` and
  `+0x1A8` through `vt+0x218`. The landing's `+0xC60 = -1` and `+0xC68 = 2` keep it from firing back at once.
  If no entity has that name, the game reports "map change failed ... [2]" (0x10167334).
- **`change_type`** (descriptor at duke!0x10167194: key "change_type", offset `+0xBDC`) decides what happens to
  the world Duke leaves [D 0x1009af5d-0x1009afa9]:
  - `2` (78 doors): `world_queue_unload(game, target, 1)` queues every other loaded world.
  - `1` (76 doors): queues only the world Duke leaves.
  - Anything else, including no key (33 doors): Duke's old player entity is `srv_kill`ed, and the world
    **stays loaded**.
  - The default of a missing key is read as 0 from the data: the ctor default was not traced.
- **Forced doorways** [C]: a `connect finished:<door>.force_change` line (an `end_part`'s `finished` pin) fires the
  door without a walk-in. Each of the 24 parts has exactly one, its exit to the next part, plus Wozma's return in
  `e08p03_a04`.
- **Landing-only doorways** [C]: 10 have no `target_map`. They are arrival points: most are a `start` where a
  part or the previous area drops Duke (e.g. `e01p01_a01.start`, `e04p03_a01.e04p03_start`), plus
  `e02p02_a03.vent` and `e08p03_a02.start`.
- **Broken targets** [C]: `e05p02_a01_r01.room1` targets `e05p02_a01:mover_map_change`, `e05p03_a02.start`
  targets `e05p03_a01:e05_to_boss`, and `e03p03_a02.start` targets `e03p03_a01:map_change2`. No entity of those
  names exists in the shipped maps. Their volumes sit at odd offsets (e.g. y+876..+936), so they are likely unreachable (open).

**Depth runs at doorways** [C]: a `game_track_auto` route end lies at the doorway in x/z, which matches
[paths.md](paths.md) section 4 (a run's far end is a doorway or an alcove). Compare in x/z only:
`e05p02_a01.auto_track1` keeps its route at y 2944 while its door `room2` and its own `position` are at
y -7680/-7664. The 3-D distance would miss it.

**Teleports inside an area** [C][D]: `spawn area_teleport { target:"<entity>" }`.
`area_teleport_do_teleport` duke!0x1009d890 moves the actor to the target entity (`+0xD2C`) through `vt+0x218`.
There are 130 in the game: 123 are mutual pairs, 3 one-way, 3 with a missing target, and 1 pair starts inactive.
**E5 has 60** (E5P1 20, E5P2 24, E5P3 16), E4P1 20, E3P2 8 and E8 28. They are the "passages to different parts
of the level": e05p02_a01's `teleport_ladderroom1/2` land at y 3088, beside `area_secret2` (y 4472). The floor
graph of [paths.md](paths.md) models them as one-way edges.

**Per part** [C, script summary]:

| part | areas | doors | in-part | secrets (areas) | teleports | runs | part | areas | doors | in-part | secrets (areas) | teleports | runs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| e01p01 | 4 | 8 | 6 | 5 (3) | 0 | 4 | e05p01 | 4 | 10 | 8 | 3 (2) | 20 | 10 |
| e01p02 | 3 | 6 | 4 | 5 (2) | 0 | 2 | e05p02 | 3 | 8 | 6 | 7 (3) | 24 | 9 |
| e01p03 | 3 | 6 | 3 | 1 (1) | 0 | 2 | e05p03 | 2 | 4 | 2 | 2 (1) | 16 | 0 |
| e02p01 | 3 | 5 | 4 | 1 (1) | 0 | 9 | e06p01 | 4 | 8 | 6 | 2 (2) | 0 | 8 |
| e02p02 | 5 | 14 | 11 | 1 (1) | 2 | 13 | e06p02 | 3 | 6 | 4 | 3 (3) | 0 | 2 |
| e02p03 | 3 | 10 | 8 | 2 (2) | 0 | 12 | e06p03 | 2 | 4 | 2 | 1 (1) | 0 | 0 |
| e03p01 | 6 | 16 | 14 | 6 (4) | 4 | 9 | e07p01 | 3 | 6 | 4 | 2 (2) | 4 | 7 |
| e03p02 | 3 | 6 | 4 | 5 (3) | 8 | 2 | e07p02 | 2 | 6 | 4 | 0 | 0 | 4 |
| e03p03 | 5 | 12 | 9 | 3 (2) | 2 | 4 | e07p03 | 2 | 4 | 2 | 1 (1) | 2 | 11 |
| e04p01 | 3 | 6 | 4 | 2 (2) | 20 | 13 | e08p01 | 3 | 6 | 4 | 0 | 10 | 2 |
| e04p02 | 5 | 10 | 8 | 3 (3) | 0 | 13 | e08p02 | 7 | 14 | 12 | 0 | 10 | 6 |
| e04p03 | 2 | 4 | 1 | 0 | 0 | 1 | e08p03 | 4 | 8 | 5 | 0 | 8 | 2 |

The secrets column matches `secretcount` in all 24 parts (55 in all, [levels-and-stats.md](levels-and-stats.md)
section 4).

## 2. Worked example: e05p02 (3 areas, 7 secrets)

```
 e05p01_a02 --start/end--> [a01] <==end/start==> [a02] --end (forced by end_part1)--> e05p03_a01
                            |  ^
                 room1 (t0) |  | room2 (t1 there, t2 back; a depth run auto_track1 on both sides)
                            v  |
                           [a01_r01]   (its room1 exit targets a01:mover_map_change: missing)
```

| area | doors (type) | secrets (position as stored, y up) | teleports | runs |
|---|---|---|---|---|
| a01 | `start` (2) -> e05p01_a02:end, leaves the part; `end` (2) <-> a02:start; `room1` (0) -> r01:room1; `room2` (1) <-> r01:room2 | `area_secret1` (1896 550 -376), `area_secret3` (4592 572 -378), `area_secret2` (9800 4472 3084) | 12 = 6 mutual pairs | 2 |
| a01_r01 | `room1` (1) -> a01:mover_map_change (missing); `room2` (2) <-> a01:room2 | `area_secret1` (-560 952 528) | 0 | 1 |
| a02 | `start` (2) <-> a01:end; `end` (2, forced) -> e05p03_a01:start | `area_secret1` (3584 -2144 -10468), `area_secret2` (3568 -2240 -6300), `area_secret3` (-136 -912 -400) | 12 = 5 pairs + `teleport_exit` one-way to `teleport_return_target` | 6 |

The graph has 3 areas and 4 links, each a door pair or a one-way door. 6 of the 8 doors stay inside the part.
Secrets per area: a01 3, r01 1, a02 3. A route from a02 to r01's secret runs: a02 `start` -> a01, walk to `room1`
-> r01. The way back from r01 is `room2` (a depth run), which lands at a01's `room2`, the far end of a01's own
depth run `auto_track1`.

**A played part, read from the state files and confirmed by the log** [F][L]: the part ended 5/7 (`stats:
... live read ... secrets 5.0`). The `.msv` flags name the two missing secrets:
`e05p02_a01.area_secret1` (+AE4=1) and `e05p02_a01_r01.area_secret1` (+AE4=1). The other five read 0. The log's
`watch: Secret ... +AE4 1 -> 0` lines name the same five.

## 3. The secrets per area

55 `area_secret` entities, all at the top level of `world.scr`.** Each has `message:"@@g001@@"`,
optional `display_time`/`show_multiple`, `position`, `name` (`area_secretN`, unique per area only) and a volume
in `maps/<map>/<name>/avolume` (same format as section 1). The full list with volumes is in
`dumps\area_graph.parts.txt` [C].

## 4. Which secrets are already found

**Live tally**: the part's tally (`S+0x223D4+0x4C`) is a count only.

**Per-secret state survives, in two places:**

1. **In memory, in every loaded world.** `game = *(G+8)` keeps its worlds in a `vptrlist_t` at `+0xF4`. The first
   node is at `+0xF8`; a node holds `+4` next and `+8` world. A world holds `+0x48` name and `+0xD4` sid
   (`world_find_or_load` 0x100e2520 walks it by name; `world_load_or_restore_msv` appends to it) [D]. A world left
   through a type-0 door stays in this list. Measured [L]: on re-entering e05p02_a01 from r01
   the three secrets came back at the **same addresses** as before leaving (145BC4C8, 17487000, 174C2A90). One already
   showed `+AE4=0`. So the mod can walk another loaded world's areas with `spawn_search("s=%d?t=%d", world+0xD4,
   0x20)` and read `+0xAE4` exactly as it does for the current one.
2. **In `state/<map>.msv`, for a world that was unloaded.**
   - `world_release_store_msv` 0x100e39d0 runs when a queued world's reference count drops below 1, unless the game
     state `+0xCC` is 6 or a demo is playing (`+0x56C`). It calls `world_store_state_msv(game, world, 0)`
     [D], which creates `state/` and writes `sprintf("%s/%s.msv", "state", world+0x48)` (lowercased) through
     `srv_gs_store` (prism3d!0x420e80 -> `srv_gs_store_impl` 0x40b5f0).
   - `world_load_or_restore_msv` 0x100e1b00 opens that file first. When it exists, it spawns a server and runs
     `srv_gs_restore`; otherwise it loads the map fresh (`spawn_allocate_world`) [D].
   - The save path (0x100e33e0) stores every loaded world the same way (`world_store_state_msv(game, 0, 0)` walks
     the list) [D].
   - The folder lives under the rw `duke/base` mount (`init.cfg` p99). It held `e05p02_a01/_a01_r01/_a02.msv`
     (49-467 KB) after the run [F].

**Do they store each `area_secret`'s found flag? Yes** [D/A]:
- `srv_gs_store_impl` writes, in order:
  - a header: u16 length + map name, then four 8-byte world fields (+0x40, +0x30, +0x28, +0x38), then the u32 id
    of the view entity;
  - pass 1, per entity: u32 id (`+0x7C`, numbered 0.. just before), u32 parent id or -1, u32 `+0x74`, u8 length +
    class name, the class's script keys (`entity_t::store_state` p3shared!0x1000d870, a walk of the class
    descriptors, **not self-delimiting**), then per pin u32 `+0x58`, u32 `+0x54` and, for an output pin, its
    `connect` strings (u16 length each, 0 ends the list); -1 ends a group, and a second -1 ends pass 1;
  - pass 2, per entity: u32 id, then **`vt+0xE0`** (user state); -1 ends it.
- area_secret's vtable (0x10139C50) has `vt+0xE0` = `area_vtE0_store` 0x10099c90. It calls
  `area_store_user_state` 0x10091b30, which writes `entity_t::store_user_state` (an empty `RET 4`,
  p3shared!0x1002e2c0), **u32 1, u32 `+0xAE4`**. Then it writes **u32 1, u8 `+0xBFF`**. The restore side
  (`vt+0xE4` 0x10099cd0 -> 0x10091b70) reads the same.
- So an area_secret's pass-2 record is `id, 1, +0xAE4, 1, +0xBFF` (17 bytes).

**Our DLL reads them both ways.**
- From memory for loaded worlds (item 1).
- From the file for the others. Parse rule, verified on all 10 secret records in the state folder [F]:
  1. Find each `\x0barea_secret` class record.
  2. Take its id from the 12 bytes before it (id, parent, `+0x74`), and its name from the record's path string
     `maps/<map>/area_secretN`.
  3. Find the one later occurrence of `id, 1, X, 1, Y`. X = `+0xAE4` (1 = not found), Y = `+0xBFF`.
  Every record matched exactly once. Y was 1 on the 8 found secrets and 0 on the 2 unfound ones (its meaning is
  open).
  - Read with Win32 file reads of `<game>\duke\base\state\<map>.msv`, or with the engine's `vfs_open`/`vfs_read`
    (prism3d exports) on `state/<map>.msv` from the main thread.
  - Read once per area change, not per frame (the files are under 0.5 MB).
  - A guard: the matches must equal the area's secret count, with X and Y in {0, 1}. Otherwise report "unknown".
- **Which source wins**: a loaded world -> memory (its file is stale or missing). Otherwise, the file. No file ->
  the area was not visited in this play: every secret is unfound (it loads fresh from `world.scr`).
- **Lifetime of the files** [D]:
  - `state_msv_delete_except_prefix(prefix)` 0x100eb230 deletes `state/*.msv` except names that start with
    `prefix`. With 0 it deletes all: game start (`FUN_100e2640` 0x100e2c17) and save load (`FUN_100eac50`
    0x100eaec7, before it unpacks the save's own state files).
  - `FUN_100e1930` 0x100e19bb, which the save path 0x100e33e0 calls, keeps only the current part's files (the
    first 6 chars of the map name).
  - So files from another part can linger (`e04p02_*` did, [F]), but they never share a name with this part's
    areas. A `load` of a not-done part unpacks the save's current-position block before checking its part, so
    another part's `.msv` can stay in `state/` until reached ([state-files.md](state-files.md); measured on the E4P3
    restore, [P][L]). Guard: trust a file only if its mtime is at or after the DLL's first staging of this part.
- **Nukes.** [D/A/F/L] Read in Ghidra and checked on a played state folder.
  - **A pickup has a record only while uncollected.** `nuke`'s vtable is 0x10133bb0 (its on-touch slot +0x24C
    0x1005fb50 calls `powerup_on_picked` 0x1005d950). `powerup_on_picked` clears `+0x2E0` and, unless the respawn
    flags `+0xCB4 & 7` are set, calls vt+0x254 = FUN_1005daa0, which sets `+0xD10 = 1`; the think FUN_1005d640 counts
    `+0xD10` down and calls `srv_kill` (the import named at 0x10172158, filled by FUN_100df490) when it runs out.
    `srv_gs_store_impl` walks only the world's live entity lists, so a collected nuke has no pass-1 record. Pass 1
    names a nuke as `u8 4 "nuke"`, its keys, then its name as u16 length + chars + NUL 72 bytes after the class
    (e06p01_a02.msv: `nuke1`, uncollected). Measured: e05p02 and e05p03 ended 10/10 [L] and their `.msv` files hold
    no `nuke` record; e06p01_a02's one record names its one pickup.
  - **A crate or barrel stores `+0x2E0` at a fixed offset.** Both vtables (crate 0x10134470, barrel 0x10134228)
    have vt+0xE0 = `holder_vtE0_store` 0x10068ac0 (function created in Ghidra) -> FUN_1008eaa0 ->
    FUN_10091320 -> FUN_10090a40 -> `entity_t::store_user_state` (empty). The pass-2 record is: u32 id, u32 1,
    `+0xAF0`, `+0xAF2`, `+0xAF3` (u8 each), u32 `+0xAFC`, u32 1, `+0xB01`, `+0xB00` (u8), u32 `+0xB04`, **u8 `+0x2E0`
    at offset 25**, u8 `+0x2E3`, `+0x1C0`/`+0x18C`/`+0x1A8` (12 bytes each), u32 count and count x 0x14 bytes, then
    FUN_1008eaa0's and the holder's tails (`+0xC14`, `+0xBFC`). Matched by (id, 1 at +4, 1 at +15): exactly one match
    for each of the 16 crates of e06p01_a01/_a02.msv, their `+0x18C` at the crates' positions. All read 0; the log's
    watch lines show the five crates with contents there going `+2E0 1 -> 0` when broken, and `+2E0=1` when new.
  - **Rule** (areas.h `ar_nukes_from_msv`): a pickup no record names is collected; a holder whole (1)
    keeps its nuke; a holder broken (0) or gone has spawned it: collected when no `nuke` record lacks a pickup's name
    (an uncollected spawned nuke would have a record), else unknown for that holder. A name twice is "not clean".
  - **Enemy carriers ([pickups.md](pickups.md) section 6).** A monst_*/boss_* entity whose
    `spawnscript` lines name `scripts/pwrup/nuke.sc` is one source per nuke (`AR_NK_ENEMY`, its `slot` among that
    enemy's nukes). Live: slot < the nukes its list still holds is missing; a dead or removed enemy holds
    none, and each active `drop-nuke` pickup (the name both drop paths give) keeps one dropped slot missing, in
    source order, the rest collected (`ar_nukes_live`). `.msv`: the monster's pass-1 record holds the list
    as u16 count, then u16 length + chars each, before its name (e07p02_a01.msv: `monst_pigoid22` lists sbullet and
    nuke, name 183 bytes after the class; e07p01_a01/_a03.msv hold no monster records, all killed and removed).
    `ar_msv_enemy_held` searches each record of the class up to the next one of the same class; no record means it
    died and dropped everything; `drop-nuke` records keep dropped slots missing as live does. A `drop-nuke` never
    counts as a broken crate's nuke, live or in the file. **Caveat:** a drop from a spawner-made carrier in the same
    area (only e08p03_a01 has both) is also `drop-nuke`, so it can hold a placed carrier's slot missing.
  - **Laser carriers ([pickups.md](pickups.md) section 7).** A laser whose `obstructed` pin
    fires a spawner that hands its monster a nuke is one `AR_NK_LASER` source (e08p03_a01's laser22, laser31 and
    laser38), counted in its area. A laser that fires again adds an `extra` nuke past the part's listed total; the
    tally check allows it. The `.msv` holds no laser state, so the area falls back on our record.
  - **Not counted:** nukes from other spawner scripts (a spawner's `spawnscript` nuke.sc, or `monst_spawnscr` nuke.sc on the
    monster it spawns): 12 in the game ([pickups.md](pickups.md) section 6). With them every part's world.scr sums to its
    `nukecount` (10), e01p02 to 11. The Progress tab's line reads "N nukes come from scripts" (`ar_unplaced_text`);
    e08p03 no longer has one, since its three are the lasers'.
- **A record of the mod's own is worse** for secrets, and is used for nukes only after the file. On death the game
  reloads its checkpoint and forgets finds made since; the log shows it: secret (9800 4472 3084) went 1 -> 0, then
  came back `+AE4=1`. A
  record of our own would disagree with the engine and with the tally. The engine's own state is the right source.

## 5. The current area and the entry Duke came through

- **Current area**: `*(game+0xEC)` world, `+0x48` its map name ([levels-and-stats.md](levels-and-stats.md)
  section 4). The player's own world `*(player+0xE0)` gives the same, and is what `player_save_checkpoint` reads.
- **Entry**: `area_changemap_place_player` copies the target map name to `S+0x2268C` (char[32]), `target_dest`
  to `S+0x226AC` (char[32]) and sets `S+0x226CC = -1` [D 0x1009ae09-0x1009ae43]. But this is the respawn record,
  not a pure "last door". `player_save_checkpoint` 0x1007a3c0 overwrites it with each checkpoint (map, name, and
  `+0x226CC` = run id). `session_init`, `FUN_100e2640` and `partend_commit_stats` clear it [X].
  Reading it at our staging of the new area (when `S+0x2268C` equals the new map name) gives the doorway Duke came
  through, before any checkpoint. Whether a checkpoint can fire between the change and our first staging is open.

## 6. What the menu builds on it

All three shipped and work in game.
1. **Progress-tab list.** For each area of the current part: name, found/total and the missing secrets' names, with
   nukes and kills per area. One line per in-part doorway gives the route as text, e.g. "r01: 1 missing - from a01
   via room1". Needs a static part table generated from `world.scr` (areas, doors with type, landing and pair, secret
   names and positions), the msv reader and the loaded-world walk. Self-check: the per-area flags must sum to the
   live tally `+0x4C`. At an area change the stage demands the live record (`levelstats_demand`) and refreshes a
   frame later, so the check never reads a tally up to 5 s old (a false `MISMATCH` line on an area change would show
   it).
2. **Cross-area secret path.** A BFS over the part's areas: doors are edges, a pair is both ways, a broken target is no
   edge. It picks the nearest missing secret by hops, then by in-area route length. In the current area it routes to
   the doorway that starts that path, labelled with the next area ("-> a01_r01"). The in-level side is a doorway as a
   run-end spur or a floor goal ([paths.md](paths.md)). Teleport edges in the floor graph were a prerequisite: without
   them most E5 secrets read `unreachable` inside their own area.
3. **Schematic map.** At most 7 nodes per part: boxes and links in the panel, with the areas that have missing
   secrets and the current area marked. Layout: the main route left to right, rooms below.

## Open questions

1. Whether `avolume` boxes are rotated by `rotation` (section 1). Live volumes make this matter only offline.
2. The default of a missing `change_type` (read as 0) and the trigger path of a walk-in door before
   `area_changemap_place_player` (0x1009abe0 sets `+0xC50 = 1` after a fade timer). Neither was traced.
3. Whether the `+0x108` unload queue is what calls `world_release_store_msv` (reached through the vtable slot at
   0x10145b24). The file times support it: `e05p02_a01.msv` was written when Duke left by a type-2 door,
   `e05p02_a02.msv` at part end.
4. The meaning of `+0xBFF` (1 on found secrets in all 10 records).
5. Whether `S+0x226AC` still holds the doorway at our first staging of the new area (section 5).
6. The broken targets (`mover_map_change`, `e05_to_boss`, `map_change2`): unreachable, or an error the player can
   trigger.
7. Teleport targets that are not `area_teleport` (3 missing, e.g. `teleport_return`): they may name another entity
   class. The script only looks up teleports.
