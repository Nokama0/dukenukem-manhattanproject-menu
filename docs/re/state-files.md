# State files: why entering E4P3 restored an old E4P3

Stock behaviour, not the panel. A `load` of a part that is not done on the chosen difficulty unpacks the save's
"current position" block into `state/` *before* the engine checks whether that block belongs to the requested
part. When the block belongs to another part, the engine starts the requested part fresh but leaves the unpacked
`.msv` files in `state/`. Nothing in normal play removes them: a part end writes only the save header. In the case
studied (sections 2 and 5), the block held E4P3 states from an earlier Normal run. They were unpacked at an E1P1
Easy load and restored when Duke reached E4P3 76 minutes later. Fast saves only change how the bytes reach the
disk. They cannot create, keep or choose a state file (section 3).

Tags: [D] decompiled, [A] disassembly, [X] xref/instruction search, [L] engine log, [P] nukepanel.log,
[F] files on disk. duke_base 0x10000000, prism3d 0x400000 (the Zoom Enhanced Edition build). Earlier
names: [areas-and-secrets.md](areas-and-secrets.md) section 4, [save-awards.md](save-awards.md) section 5,
[save-io.md](save-io.md).
Renamed in this note only (not in Ghidra): `state_prune_and_save` = `FUN_100e1930`, `session_write_file` =
`FUN_100ea6d0`, `session_save` = `FUN_100ea570`, `session_load` = `FUN_100ea650`, `game_quicksave` =
`FUN_100e33e0`, `game_new` = `FUN_100e2fd0`, `game_play_demo` = `FUN_100e31f0`.

## 1. The state lifecycle

**Files.** `state/<map>.msv` holds one unloaded world (writer `world_store_state_msv` 0x100e1a00). A world is restored
from that file whenever it exists: `world_load_or_restore_msv` 0x100e1b00 logs `restoring from saved file`.
Otherwise it loads fresh (`loading.`). **No flag marks a new part's areas as fresh. The only signal is whether the
file exists** [D].

**Delete.** `state_msv_delete_except_prefix(prefix)` 0x100eb230 [D]:
- It opens the `state` directory with `vfs_open` and reads its name list (`vfs_read`, at most 0x3FF bytes).
- For each `*.msv` whose name does not start with `prefix`, it calls `vfs_unlink("state/<name>")`. With prefix 0 it
  deletes every `.msv`.
- `vfs_unlink` prism3d!0x403fd0 goes through device slot +0x70 (0x404c90), then CRT 0x4295ba. prism3d imports
  `DeleteFileA` [D/A/X]. The 0x4295ba -> `DeleteFileA` step was not traced.
- Nothing truncates a state file. `vfs_open` passes no O_TRUNC, and `SetEndOfFile` is imported only for the CRT.

Callers [X]:

| Caller | When | Prefix |
|---|---|---|
| `game_start_part_from_save` 0x100e2c17 | every `load` (before reading) | 0 (all) |
| `session_read_file` 0x100eaec7 | when the read mode has bit 2 or 4 (unpack) | 0 (all) |
| `game_new` 0x100e3014 | `new` game | 0 |
| `game_play_demo` 0x100e3212 | attract demo | 0 |
| `state_prune_and_save` 0x100e19bb | see below | first 6 chars of the current world (`e04p03`) |
| 0x100e5871 (request queue) | not read | 0 |

**The `.sav` file** (`save/<name>.sav`, written by `session_write_file` 0x100ea6d0, read by `session_read_file`
0x100eac50; mode bits [D/A]):
- **Bit 1:** header, session `S+0..0x2B7` + three 0x148 records + 400 per-part records, then the thumbnail at
  0x20710 and the flag byte at 0x2870F. **157,519 bytes in all.**
- **Bit 2:** the **current-position block** at 0x28710: magic, version, current map `S+0x2268C`, entry
  `S+0x226AC`, difficulty, part-start inventory `S+0x22278`, the list of loaded worlds (restored into
  `game+0x130`), then every `state/*.msv`, packed.
- **Bit 4:** the same layout at 0x328710, the quicksave block (`game_quicksave` 0x100e33e0; read by `load ... q`).
- The file is opened without O_TRUNC (`session_save` 0x100ea570), so a bit-1 save leaves both blocks as they were.

**Who writes which mode.** `state_prune_and_save(game, quick)` 0x100e1930 [D]:
- If the current part is done on this difficulty and `quick` == 0, it writes **mode 1 only** and deletes nothing.
- Otherwise it deletes all `.msv` except the current part's, then writes mode 3 (`quick` 0) or mode 5 (`quick` 1).

Its callers:
- `partend_commit_stats` 0x100acef0 sets the part's done flag, then calls `state_prune_and_save(game, 0)`. **So every
  part end writes the header only.**
- `game_proc_confirm_leave` 0x100e18b0 (leaving play for the menu, load or quit, in a saved game): mode 3 while the
  part is not done.
- The quicksave 0x100e33e0: mode 5.
- `FUN_100a4e80`: not read.

**Load** (`game_start_part_from_save` 0x100e2640 [D]; args from `cmd_load` 0x100e3d60: name, ep, part, diff, `r`
replay, `q` quicksave):
1. Read the header (mode 0), then `delete_except_prefix(0)`.
2. `fresh = r || done[diff][ep][part]`.
3. If not fresh and no `q`: `session_load(S, 3)`. This reads the header and the current-position block. It **deletes
   all `.msv` and unpacks the block's files**, and it fills `game+0x130` with the block's world list.
4. Then it compares the block's map (`S+0x2268C`) with the requested ep/part. If they differ, it sets `fresh`, reads
   mode 1 again and clears `S+0x2268C`. **The files unpacked in step 3 stay in `state/`.**
5. With `fresh`, `stats_init` and the previous part's banked record build the new part's inventory. Otherwise it
   resumes from `S+0x22278`.

## 2. The stock sequence, reconstructed

Clock times are from the logs of one sitting that crossed midnight.

1. **Earlier, on Normal:** the player left play (to the menu, a load or a quit) while in E4P3, which was not done
   yet. `game_proc_confirm_leave` -> mode 3 packed `e04p03_a01.msv` and `e04p03_a02.msv`, with the current map in
   E4P3, into the block at 0x28710. E4P3 was finished later. Its part end wrote mode 1 only, so **the block stayed
   E4P3's**.
2. **21:52:27 `load My Game 2 4 1 1` (E5P2 Normal).**
   - Step 3 unpacked the block: [P] lines 327-328, `e04p03_a01.msv ... write=18 calls ... bytes=289510`, and
     `e04p03_a02.msv ... bytes=71628`.
   - The block's world list made the engine allocate `e04p03_a02` beside `e05p02_a01`:
     [L] 1604 `world_alloc('e04p03_a02') ... restoring from saved file`.
3. **21:52 to 23:02:**
   - Every `my game 2.sav` write was 157,519 bytes ([P], 13 lines from 21342 on), i.e. mode 1. The block was never
     rewritten.
   - 23:02:42: the attract demo deleted all `.msv` (`game_play_demo`).
4. **23:03:26 `load My Game 2 0 0 0` (E1P1 Easy, not done): the same unpack happened again.**
   - [P] 157043-157044: the same 289,510 and 71,628 bytes.
   - [L] 6881-6899: `e04p03_a02` was restored again beside `e01_intro`.
   - Step 4 then started E1P1 fresh. `state/` kept both E4P3 files.
5. **23:03 to 00:19, E1P1 to E4P2 in one sitting:**
   - No load and no leave to the menu ([L]: no `load` line between 6855 and 12537).
   - Every `.sav` write was 157,519 bytes, i.e. part ends ([P] 167906 ... 324810).
   - So no prune ran. Every other world loaded `loading.` on first entry ([L], all 50 worlds from e01p01_a01 to
     e04p02_a03_r01). They had no files, because the demo had deleted them and the block holds only E4P3's.
   - `e04p03_a02.msv` was rewritten at 23:03:41, when its world unloaded ([P] 157336/157339).
   - `e04p03_a01.msv` was never written again. It is the 289,510-byte file from the block.
6. **00:19:25:** room2 (change_type 2) -> `world_find_or_load('e04p03_a01')` found the file -> `restoring from
   saved file`.
   - The restored world already contains a player entity, so the client `attaching to existing avatar` (a fresh
     world logs `requesting avatar spawn`).
   - Duke stood where the old Normal run had left him.

**The engine clears E4P3 neither at the load nor at E4P2's end.** Step 4 of the load never deletes what step 3
unpacked, and a part end never prunes. The stock game only prunes on a leave or a quicksave.
**Why it is rarely seen:** it takes all three of these together:
- the save's block belongs to a part other than the one being loaded;
- the loaded part is not done on the chosen difficulty;
- the player reaches the block's part in one sitting, with no leave to the menu and no load in between.

## 3. Can the panel change that outcome? No (evidence)

- **Fast saves** (saveio.cpp) only buffers `WriteFile` on handles that `CreateFileA` opened write-only for
  `.msv`/`.sav`. On a seek, flush or close it writes the buffer out first, then makes the real call
  ([save-io.md](save-io.md) section 6).
  - It never calls `SetEndOfFile`, `DeleteFileA` or `CreateFileA` with other flags. Unlinks pass straight through
    (not hooked).
  - Byte counts and offsets match the engine's: the three unpacks of `e04p03_a01.msv` are all 289,510 bytes, the
    same size as the 21:52 copy.
  - It changes neither the save mode (`state_prune_and_save` picks it from the done flags) nor the unpack (the
    engine's `session_read_file`).
  - No `flushfail` appears in [P].
- **Freecam, Pause, Game speed (k=2) and Teleport** were used in E4P2 ([P] near the end: freecam on/off, `teleport:
  freecam start`, `speed: k=2.00`). None of them touches files, the session record or `game+0x130`.
  - The restore depends only on `state/e04p03_a01.msv` existing at 00:19:25.
  - The file was put there by the engine's unpack 76 minutes earlier, and nothing deleted it.
- The stale data itself is a valid state: it restored and played. It is not corrupt; it is the wrong part's.
- The panel log starts at the 21:52 load, so step 1 is read only from the engine's behaviour. Even with Fast saves
  on then, it would only have changed how the bytes were written, not which block they went into.

## 4. `change_type:2` and "map change failed"

- `area_changemap_place_player` 0x1009ad00 (string xref 0x1009af07) [D]:
  - First, `world_find_or_load` loads or restores the target world.
  - Then it searches that world for the door's target entity (`s=%d?n=%s?t=0x20?g=0x10`).
  - When none is found, it logs `map change failed ... [2]` and skips the placement (vt+0x218). Duke stays where
    the new world puts him.
  - change_type 2 then queues a world unload with `cVar7 = 1` (type 0 does not unload; see
    [areas-and-secrets.md](areas-and-secrets.md)).
- **Benign at part transitions.** The same `[2]` line appears at the fresh part starts 23:17:05 (e01p03 -> e02p01)
  and 00:07:31 (e03 -> e04p01) [L], with `requesting avatar spawn`.
- At 00:19:25 the line is the same. The difference is only `restoring from saved file` / `attaching to existing
  avatar`: the player came from the stale file.

## 5. Recovery and what the files are now

- After the stale restore, `state/` held only `e04_stats.msv` and the thumbnails [F, 00:20:02]. `load ... r`
  (Replay) at 00:20:01 deleted everything and started E4P3 fresh ([L] `server_load('e04p03_a01') : loading.`).
- **But `my game 2.sav`'s block was still E4P3-from-the-stale-state.** It was rewritten by the leave saves at 00:19:49,
  00:19:57 and 00:20:01 ([P] 325676/325842/325942, `write=450`), from the stale files then in `state/`. Its map is
  `e04p03_a02`.
- **Recovery:** Load Game -> the slot -> episode screen -> E4P3, Easy -> **Replay** (the `r` load). Then leave play
  once from inside E4P3 (Esc -> main menu, or quit), so that the block is rewritten from the fresh run.
- Play/Continue on E4P3 before that resumes the stale block.
- Deleting files in `state/` does not help: every load deletes them and unpacks the block again.

## Open questions
- When step 1 happened; the log shows only "before the 21:52 load, while in E4P3 not done".
- Whether CRT 0x4295ba is `_unlink` -> `DeleteFileA` (not traced).
- `FUN_100a4e80`'s use of `state_prune_and_save` and the request-queue delete at 0x100e5871.
- The 0x3FF-byte name list in `state_msv_delete_except_prefix`: with about 70 or more files, later names would
  escape a delete. It did not matter in the case studied: `state/` held a handful of files after the demo's delete.
- A panel guard (after a `load` whose block map differs from the requested part, list `state/` and warn about
  `.msv` files of other parts) is not built. Deleting them would be writing the engine's files, which the panel
  does only where the engine itself deletes.
