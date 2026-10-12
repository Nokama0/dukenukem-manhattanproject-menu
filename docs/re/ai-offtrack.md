# Monster AI with Duke off the track

Monsters target the real Duke (3D position, no same-track test except gloppoids), but three things fail when he
leaves the line. (1) Waking needs the seen flag `+0xB71`, which only the draw sets, while the monster is in view.
(2) Monsters are track actors: their move goal is Duke **projected onto their own track**, and every tick re-snaps
their yaw along it. (3) Their 23-degree sight cone and their shots follow that yaw: bullets fly flat along the
track line, and glopp lobs only take the distance along it. Keeping them awake is cheap: an engine-written field
(`+0xB71`/`+0xB74`, the draw's own pair). Letting them see and hit an off-track Duke needs a detour (sight test,
shot direction) or clearing the monster's `+0xB1C`, as the hovermech does. That is "move anywhere" for monsters, the
most effective lever and the riskiest.

**As shipped:** L1 + L2 + L3 (section 5) are built as `aiaware.{h,cpp}`, active only while First person or Move
anywhere is on, and verified in game (enemy awareness). Non-bullet weapons (glopp lob, rockets, lasers) are not
re-aimed (A5). L4 is not built.

Tags as in [[offscreen-and-track]]: **[D]** decompile, **[A]** disassembly, **[X]** xref/vtable scan. Base
duke_base 0x10000000. Read through the Ghidra MCP only. Renamed in Ghidra (saved):
`monster_tick` 0x1002d4e0, `monster_perception_timer` 0x1002dab0, `monster_ai_decide` 0x1002dc90,
`monster_combat_think` 0x1002e1f0, `monster_chase_think` 0x1002e4e0, `monster_patrol_think` 0x1002de20,
`monster_on_event` 0x10033ef0, `monster_draw` 0x10032bd0, `monster_can_see_target` 0x100314b0,
`monster_set_move_goal` 0x10035c20, `monster_goal_reached` 0x10035ba0, `monster_walk_to_goal` 0x10030310,
`monster_point_in_front` 0x100307c0, `pigoid_think_wake` 0x100241f0, `pigoid_attack_think` 0x100246f0,
`pigoid_chase_think` 0x10024910, `pigoid_fire_line_clear` 0x10025520, `roachoid_spawn_glopp_lob` 0x10028b70,
`gloppoid_chase_same_track` 0x1001fcf0.

## 1. Perception

Monster fields: `+0xBD4` target (= `get_local_player`, rewritten every tick in `monster_tick` and the wake test, so
always Duke), `+0xB7C` flags (0x20 = **alerted**, 0x8 = **sees target**, 0x200 = dead/off), `+0xBA0` alert seconds
left, `+0xB94` AI state (1 = asleep), `+0xB71` seen byte, `+0xB74` time last seen, `+0xBD8` "always seen" byte.

- **Seen flag** [D 0x1008d9e0, 0x10032bd0]: `actor_draw_mark_seen` sets `+0xB71 = 1, +0xB74 = now` when the
  3x-radius sphere passes `rend_sphere_visible`, and clears `+0xB71` once `now - +0xB74 > 10 s`. `monster_draw`
  (vt+0xB4 of 18 classes, plus 6 direct callers [X]) then forces `+0xB71 = 1` while `+0xBD8` is set. The engine
  sets `+0xBD8` at init when `+0xB7F & 1` (0x1002cc82) and for spawned children (0x10026032). Other writers of
  `+0xB71 = 1`: `monster_on_event` events 0 and 1, FUN_10092d20, FUN_100b66e0 [X, 22-site list].
- **Sleep gate** `monster_sleep_unless_seen` 0x1002d320 [D]: when `+0xB71 == 0` **and** not alerted, state = 1 and
  `monster_alert_if_seen(0)` clears the alert. An alerted monster keeps acting unseen until its alert runs out.
- **Alert** `monster_alert_if_seen(amount)` 0x1002ea30 [D]: sets 0x20 and `+0xBA0 = max(+0xBA0, amount)` **only if
  `+0xB71`**. `monster_ai_decide` counts `+0xBA0` down by dt and clears the alert at 0. Sources: damage (20),
  perception (20), events (10), a 2000 jump-in path (0x10024550).
- **Perception timer** `monster_perception_timer` = vt+0x1AC, re-armed every `+0xBE8` s [D]:
  - timer 1: alert 20 while "sees target" (0x8) holds;
  - timer 2: when `+0xB71 == 0`, clear 0x8 and stop; else 0x8 = `vt+0x284(Duke)` (unless Duke is dead or locked).
    If neither alerted nor seeing, and Duke is within **300** (3D), alert 20.
- **Sight** `monster_can_see_target` (vt+0x284, all 46 monster vtables [X]) [D]: needs `+0xB71`, distance <= 1000,
  and Duke inside the monster's facing cone. The cone is 3D dot > 0.5, then **horizontal dot >= 0.92 (about 23
  degrees)** against `ypr_2_cartes(+0x1A8)`. Then `phys_ray_test` from the monster's eye (`+0x190 + +0xC00`) to
  Duke's position at three heights (centre and ± height ratio x half-height). It uses Duke's real 3D position.
- **Same-track test:** only `gloppoid_chase_same_track` 0x1001fcf0 (gloppoid1/2 vtables) [D]. When it does not see
  Duke **and** `Duke+0xB20 != own +0xB20`, it wanders to patrol points instead of approaching. No other monster
  compares tracks (`CMP ..+0xB20` scan [X]).

## 2. Decision, chase, movement

- `monster_tick` 0x1002d4e0 [D] first runs `FUN_1008d920` → **vt+0x230 `actor_track_update_if_moved`** (track pick
  + `actor_snap_to_track`), then AI, then `vt+0x2E4 monster_walk_to_goal`. The wake path (pigoid vt+0x24C) runs
  `monster_ai_decide`: alerted → `monster_combat_think` (vt+0x258), else patrol (vt+0x268).
- `monster_combat_think` [D]: Duke behind (`monster_point_in_front`, the facing dot) → turn; Duke above/below by more
  than both half-heights (vt+0x270/0x274) → chase; **sees (0x8) → attack (vt+0x260), else chase (vt+0x25c)**.
- **Track-bound** [D/A]: `actor_game_fielder_ctor` sets `+0xB1C = 1` (0x1008d866). The spawn projects the
  position and every patrol point with `track->vt+0x1EC` (0x1002c6f0-0x1002c7f7). Movement is `actor_move_delta`
  (vt+0x204) → physics → `actor_constrain_move_to_track` (vt+0x14C, same slot as Duke). The per-tick snap
  re-snaps the yaw onto the track plane (`FUN_10096410`, called from `game_track_snap_actor`).
- **Goal** `monster_set_move_goal` (vt+0x2D8, 34 vtables) [D]: `+0xC48 = goal`. Then, **if `+0xB1C && +0xB20`,
  `track->vt+0x1EC(goal)`**: the goal becomes Duke's nearest point on the monster's track. It turns to face the
  goal and flags "arrived" within 1 unit (`monster_goal_reached`). The chase (`monster_chase_think`) re-aims the
  goal at Duke every 0.5-1.5 s. An off-track Duke therefore draws monsters to the nearest point of their line, where
  they stop and turn.
- **Attack** `pigoid_attack_think` [D]: the **XZ** distance to Duke between `+0xBB0` and `+0xBAC` → fire state 0xE
  when `+0xBD0` allows. So range uses the real position, not the track.
- Hovermech (`FUN_10022000`, 4 vtables) clears its own `+0xB1C` while spinning (0x10022097), and the porkchopper
  child path (0x10024550) clears it and sets it back. An off-track monster is something the engine already makes.

## 3. Shots

- `monster_fire_bullet_noclip` 0x1002b0a0 [D] (pigoid burst `FUN_1002b5e0`, two vtables): direction =
  `ypr_2_cartes(+0x1A8)` with **y forced to 0**, spawned at the monster's position + (`+0xC00` - 6) up, velocity
  `+0x1C0 = dir x 2000`, then `proj_set_attacker` 0x10039980. The bullet's hitscan ray follows `+0x1C0`
  ([[offscreen-and-track]] A1). So the shot is **flat, along the monster's track-locked yaw**.
- The pig cops' fire, `FUN_100264f0`, spawns its bullet without `proj_set_attacker`. Both it and
  `monster_fire_bullet_noclip` put the monster in the bullet's first ignore slot (`proj_add_ignore` 0x10039000:
  `+0xB34` on, `+0xB3C` entity) [D]. The other `actor_proj_bullet` spawners (0x1001d9a9, 0x1002687a, 0x10026c4a,
  0x1003b960, 0x100451b6) were not read; whether they fill slot 0 alike shows in the L3 `other` count beside Duke's
  shots (measured: `calls=32 other=32`, equal to Duke's own shots, Golden Eagle 7/7 at 7 calls [L]).
- The clear-shot test `pigoid_fire_line_clear` 0x10025520 casts the same flat facing ray (1000 long) and refuses
  when another monster is first.
- `roachoid_spawn_glopp_lob` 0x10028b70 [D]: the glopp's angles = facing pitched up by 45 + f(range). The range is
  Duke's offset turned into the monster's local frame, z (forward) only. The lob lands on the track line.
- Other monster weapons (laser bolt, smech rockets, grenades, bosses) were not read, see A5.

## 4. What breaks off the track

| Situation | Effect (by reading) |
|---|---|
| First person, monster behind Duke or out of the eye frustum | never drawn → never seen → stays asleep; shooting it does not alert it (alert needs seen) |
| Off-track Duke, monster in view | wakes. **Sees** him only if he is within about 23 degrees of its track direction, within 1000 and a clear ray (so far along the line: yes; to the side and close: no) |
| Alerted but not seeing | chases to Duke's projection on its track, stops there; alert decays (20 s) unless re-seen |
| Seeing and in XZ range | fires flat along its track: misses a Duke standing off the line (bullets); glopp lands on the line |
| Gloppoids | wander when Duke's `+0xB20` differs (stale in first person: the pick is a no-op while `+0xB1C = 0`) |

## 5. Levers

| # | Lever | Kind | Fixes | Risk |
|---|---|---|---|---|
| L1 | Each frame, for monsters within R of Duke (and optionally a clear `phys_ray_visible`), write `+0xB74 = now, +0xB71 = 1`, the exact pair the draw writes | engine-written field | waking, alert on damage, proximity alert, timer refresh | large R wakes whole sections (horde); behaviour of never-drawn monsters (animation LOD?) unread |
| L1b | Set `+0xBD8 = 1` once per monster (the engine's "always seen") and restore on toggle-off | engine-written field (init/spawn only) | as L1, no per-frame cost | wakes every monster of the level at once; no range; we must restore the old value |
| L1c | Call `monster_on_event(1)` (vt+0x33C; event "wake": seen = 1, state 3, alert 10) | engine call | one-shot wake | lasts at most 10 s: the next draw clears `+0xB71` (stale `+0xB74`); not a sustain |
| L2 | Detour `monster_can_see_target` 0x100314b0: when Duke is off the track (or the option is on), drop the cone test and keep range + ray | detour | attack state, alert refresh | ray only from the monster's eye; flat shots still miss without L3 |
| L3 | Detour `proj_bullet_hitscan_ray` 0x1003a9c0, the first think of every bullet (`+0xBEC`), and re-aim `+0x1C0` to `normalize(Duke chest - bullet+0x18C) x 2000` when slot 0 holds a CAT_ENEMY. A detour on `proj_set_attacker` 0x10039980 instead (thiscall ECX = bullet, arg = monster [A 0x1002b194], filtered to the return address 0x1002b19c in `monster_fire_bullet_noclip`; 0x1002b90a is a second unread monster caller) misses the pig cops, whose fire never calls it | detour writing a field the engine has just written on a new object | bullet aim in 3D | every other monster weapon needs its own hook (A5); difficulty jumps (monsters now always hit) |
| L3b | Swap the monster's yaw `+0x1AC` toward Duke around each fire call | detour per fire function | XZ aim for all facing-based weapons | height still flat for bullets; a yaw written between ticks is undone by the next tick's snap, so it must be inside the call |
| L4 | Clear the monster's `+0xB1C` while Duke is off the track (restore = set 1 + `actor_teleport`-style re-pick) | engine-written field (hovermech does it) | chase toward the real Duke, yaw faces him (the goal is no longer projected, no re-snap), so sight and facing-based aim work natively | monsters leave the playable band: fall off ledges, stuck, walk through cut-away zones; re-enabling snaps them to a track (a jump); bosses and scripted tracks (`track_queenroach`, utility tracks) must be excluded |
| - | Make the AI use a "track Duke": not needed. Every AI test reads Duke's real `+0x18C`/vt+0x160; only the gloppoid compares `+0xB20` |

Shipped: **L1** (range-gated, R = 1500, bosses excluded because their wake paths were not read, plus the ray, at
most 8 rays a frame nearest first) + **L2** + **L3**. L4 stays an unbuilt, experimental option. Awareness only wakes
monsters and brings them to the nearest point of their line; L2+L3 also let them hit Duke anywhere in range.

## Open questions

- **A1** The draw (vt+0xB4) runs for every monster each frame, so the 10 s clear really runs on off-screen monsters
  (pvs.md: one leaf). Log `+0xB71`/`+0xB74` of one monster behind Duke in first person for 15 s.
- **A2** L1 wakes: count state `+0xB94` 1 → other and the 0x20 alert bit, per monster in range, with the option on
  (the `aiaware:` line). A never-drawn monster in range must leave state 1 within 2 s.
- **A3** Sight with Duke off the line: log `vt+0x284` result bits (`+0xB7C & 8`) next to Duke's lateral distance from
  the monster's track (`vt+0x1EC` projection). Expect it false beyond about 23 degrees without L2.
- **A4** Monster yaw after a tick equals the track direction (snap), even when the goal is off the line: log
  `+0x1AC` against `game_track_get_dir` at the monster.
- **A5** Fire functions of the other monsters (securitymech, hovermech, gatoroid, femech, flies, bosses): which ones
  aim from facing and which aim at Duke. Read their vt+0x2E8 / fire event, one per class family.
- **A6** L4: a monster with `+0xB1C = 0` walks to Duke without falling. Watch one pigoid on a flat section; log its
  distance from its track and its `+0x190` (y).
