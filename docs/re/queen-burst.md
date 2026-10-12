# Cockroach Queen one-burst kills (phase 2)

A frame-rate bug, read from the binary. In phase 2 the Queen's running swipe (`FUN_10016110`) tests its claw
against Duke **on every think** from 0.5 s into the attack until the animation ends. Each test that hits sends
type-1 damage of `swipe_damage` (10/15/25 by difficulty). Every entity thinks once a frame ([[timing]] §1), and type
1 has no player-side lockout. So the hits per swipe scale with the frame rate. A captured burst showed sixteen
"-25" and one "-11" (411 health): about 17 frames of contact on Hard, roughly 0.1 s at 175 fps. The same contact
costs about 3 hits (75) at 30 fps. The panel's Fix Queen swipe switch (section 5) lets one block-1 hit per swipe
through; the `hurtlog` lines show both the bug and the fix.

Method: D = decompile, A = disassembly, X = xref, S = string, F = game data. All addresses are in duke_base.dll.
Def values come from `scripts/monst/bosses.def` in `duke/base/db_e01.zipfs` (F). Suffix 00/01/02 = difficulty.

## 1. Phases and dispatch

- Vtable base 0x10127230 (vt+0x198 = 0x10013b00, X).
  - init vt+0x74 = 0x10012aa0;
  - think vt+0x5C = 0x100139a0;
  - contact vt+0x168 = 0x10014160 (created `queen_contact`).
- The think dispatches on mode `+0x16CC` (D 0x100139a0):
  - 1 = phase 1, `FUN_10014530`;
  - 2 = phase 2, `FUN_10014770`: the train chase with the `car1..4_health` thresholds.
- The damage handler 0x10013b00 drops the boss gate `+0x16B8` at `chase_health` (mode 1) and at each car threshold
  (mode 2) ([[aim]] §7).
- The phase-2 attack choice (D 0x10014770): when `+0x16C8` (`attack_latency`, 3/2.5/2 s plus rand 0..1) expires:
  - far (horizontal distance² >= `+0x16E8` = 175000, about 418 u) -> `FUN_10016430`, spit;
  - near -> `FUN_10015e50`, running swipe.

## 2. Every damage path

| Attack | Code | Damage | How applied | Frame-rate? |
|---|---|---|---|---|
| Touch (any phase) | contact 0x10014160 (A) | `touch_damage` 2/3/4, **type 2** | Once per contact callback, when the contacted object is the player (`+0x1834`). The player's apply `FUN_10078f10` refuses a type while an effect of that type is live. Types 2/3/4/7/8 add a 1.5 s effect (`FUN_100cc360`, `FUN_100cc420(...,1.5)`), aged by world dt (`FUN_100cc7b0`, world+0x38). | **No**: at most one per 1.5 s |
| Phase-1 swipe | `FUN_10015200` (D) | `swipe_damage` 10/15/25, type 1 | Once per swipe: flag `+0x16E4`, at `swipe_coll_time` into the attack; 2 or 4 segment tests | No |
| Phase-1 barrel | `FUN_10015760` (A 0x100157e3) | `barrel_damage` 5/10/20 | Projectile `qr_barrel` (damage +0xBC0), on its explosion | No (one projectile) |
| Phase-1 / phase-2 spit | `FUN_10015c30` / `FUN_10016570` (A/D) | `spit_damage` 10/15/25 | One `qr_acid` projectile per attack, flag `+0x1728`, at `acid_spit_time` | No |
| **Phase-2 running swipe, block 1** | **`FUN_10016110` 0x10016118..0x10016230 (A)** | **`swipe_damage` 10/15/25, type 1**, knock 550 | **Every think while `now > start(+0x16C4) + 0.5`**, until the animation flag `+0x14F0` clears. The gate is the time compare alone: no once-flag. Up to 3 segment tests against the player's vt+0x11C; the first hit sends `player vt+0x198(1, swipe_damage, +0x178C, queen, queen)` (0x10016205). | **YES** |
| Phase-2 running swipe, block 2 | same, 1.5 s branch (D) | `swipe_damage`, type 1, knock 450 | Once per attack (flag `+0x16E4`) | No |
| Phase-2 end of track | `FUN_10014770` (D) | +100 health to Duke (`powerup_give_send(player,1,0,100)`) | Once | -- (a heal) |

- **No instant-kill volume or crush damage in her code.** Her vtable's handlers reach Duke only through the paths
  above. The capture's per-hit 25s rule out a kill
  volume: a kill volume sends one large amount.
- **Designed one hit, per the data.** The def's `swipe_run_coll_time:0.6` says "time into running swipe animation to
  check for collision", and so does `shoulder_hit_time:0.3`. **Neither string exists in the binary** (S: no match),
  so the code never reads them. The phase-1 swipe of the same shape is single-shot (`+0x16E4`).
- Knockback (`FUN_1007acb0`) **sets** Duke's push vector and never adds to it. A hit therefore does not push him out
  any faster, and the Queen lunges after him (`+0x178C` = facing × 300).
- Duke's damage path:
  - `player_take_damage` 0x10078de0 = actor_player vt+0x198 (created in Ghidra). It is thiscall `(type, amount, dir,
    inflictor, attacker)`, RET 0x14 (A). It `net_send`s message 4.
  - The apply is `FUN_10078f10`:
    - it halves the amount while `session+0x1C0 > 0` (meaning unknown);
    - it subtracts through `powerup_give_send(...,1,...)` -> `FUN_10079530`, which clamps to 0..999.
  - `FUN_10079530` draws the stock **"-%d" popup** above Duke for each loss from 1 to 99 (`actor_popup_text_add`,
    format at 0x10161b94).

## 3. What the capture's numbers were

The column of "-25" and "-11" is **the game's own health popup** (`FUN_10079530`), not our Damage numbers.
- The engine draws it through `scr_actor_text.sc`. Our floattext only mounts the stock 4:3 copy of that script
  ([[display]] section C), so **no dmgnum filter leaked and nothing in dmgnum needs a change**.
- dmgnum prints `%d` of a positive damage (`dmg_display(before - after)`) on monsters hit by Duke, so it can never print
  a minus.
- dmgnum keeps a hit only when attacker == the local player and the victim is CAT_ENEMY/CAT_BOSS
  (`dmg_pre_reason`), and `dmg_display` returns at least 1. Its log counts the Queen's hits as `attacker=`.
- "Too close" fits too: the running swipe is her phase-2 attack whenever Duke is within about 418 u. The touch
  damage is 2/3/4, at most once per 1.5 s.
- The "-11" is the last 11 health clamped at 0. 16 × 25 + 11 = 411 = Duke's health before the burst. Swipe damage 25 is
  Hard (`swipe_damage02`).

## 4. Instrument

`hurtlog.cpp`/`hurtlog.h`, always on; read-only except the Fix Queen swipe drop (section 5). If the game refuses
the first block-1 hit, the gate still counts it and the rest of that swipe is dropped (rare; accepted):
- **Detour on `player_take_damage`.** It is armed lazily from the live player's vt+0x198 and fingerprinted on 15 bytes
  (6 move to the trampoline). Each send logs:
  `hurtlog: send t= type= amount= attacker=<class> inflictor=<class> health=`.
  Identical lines are held for 1 s, and the next printed line says `(held N)`.
- **Health watcher**, once a frame after the world tick (probe.cpp). Each drop logs
  `hurtlog: hurt t= health a -> b (-n) sends= first: type= amount= attacker=`, attributed to the sends of the last
  250 ms.
- **Burst.** A burst is at least 50 lost inside 250 ms. It stays open while drops keep coming within 250 ms, and on
  closing it logs `hurtlog: BURST #k t=a..b (ms) lost= in N drops, health x -> y` and then every drop, eight to a line.
- **Every 5 s, when changed:** `hurtlog: armed= sends= drops= lost= sendsNoDrop= ... bursts=` and
  `hurtlog: by attacker/type <class>/typeT xN =sum`.
- **Expected for this bug:** a BURST whose drops are about 6 ms apart, each `type=1 amount=25.0 boss_queenroach`, one
  per frame.

## 5. Fix A: one block-1 hit per swipe

- **Switch.** Settings > FRAME RATE "Fix Queen swipe", ini `[framerate] queenswipe`, default on (`g_cfg.queenFix`).
- **Where.** In the `player_take_damage` detour (hurtlog.cpp). A type-1 send whose return address is block 1's call
  site, from attacker class `boss_queenroach`, passes only once per attack; the detour returns without calling the
  original for the rest.
  - The attack key is the Queen pointer plus her `+0x16C4` attack start, which `FUN_10015e50` writes once per swipe.
  - Rule: `qs_gate` in hurtlog.h, tests 0410-0411.
- **Untouched.** Block 2 (the once-only 1.5 s hit), the phase-1 swipe, touch damage, spit, barrels and every other
  attacker reach the original unchanged.
- **Block 1's call site** is found by a 42-byte scan of duke_base's code (`kQsSite`, `offs_scan_duke`); ret = site + 32.
  - The scan includes block 1's `push 0x226`, because block 2's identical sequence is followed by `push 0x1C2` (A
    0x1001633a).
  - Anything but one match leaves the fix inactive and is logged: `queen swipe site matched N times`.
- **Log.** At arm time: `hurtlog: queen swipe block 1 at <addr>, return <ret>; Fix Queen swipe is on`. Each dropped
  repeat logs `queen swipe repeat dropped` (held 1 s). The 5 s line adds `queenFix= site= kept= dropped= unread=`.
- **Verified in game:** in phase 2, when she swipes Duke at close range, Duke loses one swipe (25 on Hard), plus the
  1.5 s hit if it connects; `dropped` rises; no BURST line from the Queen. With the switch off, the burst returns.
- **Option B** (at most one hit per 33 ms) was not built.

## Open questions

- Train-car blockers and a world `area_kill` were not searched as other damage sources.
