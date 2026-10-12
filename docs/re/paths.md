# Secret path and nuke path: depth-run connectors and floors

**The route feature is removed.** The "Secret path" and "Nuke path" rows, `paths.cpp`, `paths.h`, `pathfind.h` and
its tests are gone; only the cross-area door label remains (`doorlabel.cpp`, section 7). The engine facts below
(connectors, `area_teleport`) hold and are the reference; the solver sections record what was built and why it was
dropped.

A `game_track_auto` keeps its route points WITH height at `+0xB48` (copied before `game_track_finalize` zeroes y),
its `auto_type` at `+0xB38`, and starts when Duke stands in its own volume and either `auto_type` allows the end he
is nearer or he presses the use key; it runs him from the end he is nearer to the other end. **Its far end usually
has no track at all** (a `map_change` doorway or an alcove, section 4), and one track can serve several floors, so a
route has to run over a graph of floors under the tracks, with walls tested by world rays. Teleport pads
(`area_teleport`, section 5) are one-way edges (tests 0430-0431). The last built rule drew a route only when every
edge of it and both its end legs were verified, otherwise nothing (section 6, tests 0432-0439), and was removed
because most goals then had no route (section 7).

Tags: D decompile, A disassembly, X xref/vtable read, F map data (`world.scr` in `duke/base/db_e03.zipfs`, read with
Python zipfile), L `nukepanel.log`. duke_base base 0x10000000. Functions created in Ghidra (saved):
`game_track_auto_vt70` 0x10098e70, `game_track_auto_vt1E8` 0x10098ef0, `game_track_auto_vtE0` 0x10099170,
`game_track_auto_vtE4` 0x100991c0.

## 1. The connector's layout [X, D]

- The two vtables (game_track 0x10138688, game_track_auto 0x10138890) differ only in slots 0-2, +0x5C, +0x70, +0x80,
  +0xE0, +0xE4, +0x1C0 and +0x1E8; the polyline (+0xAF8, +0xAFC, +0xB28..+0xB30), `vt+0x1C4` walk and `vt+0x1EC`
  snap are the same code.
- `vt+0x70` 0x10098e70: allocates `n*12`, copies `route_point[]` (+0xAF8) into **`+0xB48`**, then calls
  `game_track_finalize` (which zeroes y and tessellates). So `+0xB48[0]` and `+0xB48[n-1]` are the run's two ends
  in 3-D. `vt+0xE0/+0xE4` save and load that copy; `vt+0x80` 0x10098ec0 frees it.
- `+0xB38` = **`auto_type`** (script key "auto_type" S 0x10166c3c, registered at 0x10098bef-0x10098c17 with offset 0xB38,
  4 bytes).

## 2. What starts a run [D 0x10098ef0, 0x10086120, 0x1007ece5]

- `vt+0x1E8(actor, &dFirst, &dLast, &flag)` 0x10098ef0: SQUARED distances from the actor to the first and last end
  (`+0xB48[0]`, `+0xB48[+0xAFC-1]`); returns true when `auto_type` is 1, or 2 and he is nearer the first end, or 3
  and nearer the last. It also returns true and sets `flag` when the actor (user bit 0x100, `+0xB00` set) newly
  presses input bit 8 at `+0xE00` (the bit the same scan's use areas test: the use key), or newly presses bit 0x10
  while facing along the run within 0.8.
- `playerstate_move_touch_scan` 0x10086120: for each area with `+0xAE4` set whose volume holds Duke (`vt+0x1B8`), a
  `vt+0x1C0() == 2` area whose `vt+0x1E8` is true sends **message 9**: the entry point (his x/z projected onto the run,
  `vt+0x1EC`), the far end (`route_point[0]` when he is nearer the last end, else `route_point[n-1]`), facing, cursor
  and the run's id.
- Receive 0x1007ece5-0x1007ed19: `+0xB20 = run`, `+0xB1C = 0`, `player_set_state(4)`; `+0xBE0` entry, `+0xBEC` far end.
  State 4's pre-tick 0x1007f590 walks him to the entry, snaps him on (`+0xB1C = 1`) and along (`vt+0x1C4`) until past
  the far end, then `+0xB20 = 0` and state 3: the next pick (actor_pick_track) lands him on whatever track holds him.
- **The game's own hint arrow** (`g_hint_arrows`, FUN_10078170): for a run it takes the end nearer Duke (by
  `vt+0x1E8`'s squared distances), projects it onto his line and drops it to the floor. Our marker was the run's
  attach node on the line: the same spot.

## 3. The graph (pathfind.h, removed)

- **Columns:** each `game_track` polyline (repeated joints dropped) plus points every 128 units plus attachments.
- **Joins:** a line's end joins another line when their volumes' height ranges overlap and the end lies in the other
  line's volume or within 64 units of it (x/z alone when a volume is unread). Stacked lines share x/z (four lines at
  z 192 overlapping in x on e03p01_a01 [L]), so heights are required.
- **Runs:** each end goes to the line whose volume holds it, else one whose height range holds it, nearest in x/z within
  256 (`pf_choose_line`). An end with no line gets a **spur** column there (section 4). Weight: the run's length plus
  each end's distance to its line. Undirected: the use key starts a run from either end.
- **Floors:** under each column, every floor from the top of its line's volume down (`pf_floor_stack`: a
  ray down, an up-facing hit is a floor, then on from just below it; at most 4). A floor node per floor. Neighbouring
  floors are one walk within 40 units, or up to 128 when the floor midway lies well between them (a stair, not a
  cliff: `pf_level_pair`). A stack stopped by the floor cap (4) or the ray cap (8) with height left below is counted
  (`stacksCut` on the staging line): a lower floor may be missing there, and Duke standing on it reads `no start`. A floor with no level partner drops one way onto the highest floor below (at most 1024).
  Every walk and drop edge is **blocked when a world ray at waist height (Duke's half height) between its two floor
  points meets geometry**. A run's edge joins the floors nearest its two ends' own heights.
- **Ladders:** every `area_ladder` (a second `t=0x20` search, class name) with its volume, once per staging. Logged
  volumes: 56..136 wide, 340..1276 tall, bottom at the lower floor [L]; one read before its world matrix was set came
  back at the origin, so a centre more than 512 from `+0x18C` falls back to the local range around `+0x18C`. A ladder
  joins the floor node at its foot (within 64 of the volume's bottom) to the highest floor node within 256 below its top
  that a waist-high world ray from the ladder's centre reaches (so a roof just past a wall cannot win over the landing;
  each refusal counts in `ladderTopsWalled`), over the columns within its footprint plus 96, both ways, for the climb
  height (`pf_add_ladders`, tests 0395 and 0401). Each ladder's foot is pinned as a column of its own on the line
  that holds it, and a foot node inside the footprint is preferred (section 6). Drawn as a leg up or down the ladder's
  centre with a "Ladder" marker where the climb starts (its foot going up, its top going down). Counted:
  `ladderEdges`, `laddersNoFloors`.
- **Teleport pads** (section 5): every `area_teleport` (the ladders' search, class name) with its volume and its
  target's position (one guarded read of the pad's span to `+0xD30` and one of the target's to `+0x198`), once per
  staging. A pad is a **one-way** edge from the floor nodes in its volume (x/z in the box at its own height, floor
  between its bottom less 64 and its top; when none, the floor node within its footprint plus 96 whose floor is
  nearest the volume's bottom, then the nearest, that a waist-high ray from the pad's centre reaches) to the
  floor node at its landing (the line that would hold the target's position, at the floor one ray below it, else a
  spur), at a constant cost of 64 (`pf_add_teleports`, tests 0430 and 0431). Drawn as a leg to the pad's centre with a
  "Teleport" marker; the route goes on from the landing with no line through the level. Counted: `teleportEdges`,
  `teleportsNoFloors` (no floor node in reach, or none at the landing), `teleportsUnread` (span, volume or target
  unread, or no target), plus `off` (a pad whose `+0xAE4` or `teleport_on` read 0 at staging: kept, see Open questions).
- **Line volumes:** a line's volume read before its world matrix is set gets the same fallback (the local range at
  `+0x18C`, unrotated), judged by the matrix's own translation still at the origin while `+0x18C` is not
  (`pf_matrix_unset`, test 0402): the ladders' "centre more than 512 from `+0x18C`" cannot judge a line, whose long
  volume starts at its origin. Counted as `line matrix fallback`; each line's log line gives its volume centre's
  distance from its polyline.
- **Duke and goals snap by their own floor:** Duke's feet; a goal's floor is one ray down from its centre (a secret's
  volume centre, a nuke's or its crate's box centre), cached per goal. The goal attaches to the floor node within 64
  of that floor on its line's two columns, else to a spur at that floor, else to nothing: no route is drawn for it and
  it counts in `goalsNoFloorLine`. Found secrets, collected nukes and opened crates are skipped (`pf_goal_live`).
- The route is the solve's own chain of floor nodes (start, reached nodes, goal); nothing else is drawn.
- **No world for rays at staging** (`teleport_ray_world` returns 0; never seen in 23 logged stagings): no floor
  graph, so the level is staged again every 5 s, at most 12 times (`pf_world_retry_due`), each logged with its retry
  number. The level keeps no route until a world is there.

## 4. Why the first routes failed, measured (e03p01_a03_r01 and the log)

- **Far run ends never attached** [L: every `run` line read `lines a -> -1`; F]: the far end of every depth run lies
  where no `game_track` exists. On e03p01_a03_r01 both runs go z 0 -> 384 and a `map_change` (to e03p01_a03) sits at
  z 300 on each; e03p01_a03's two runs mirror them (z 1024 -> 1348, `map_change` back at z 1288/1292); e03p01_a01's
  run [0] ends at a `map_change` (384, 432, 492) and runs [1], [2] at a bathroom alcove holding `area_switch12/13`. So
  a run's far end is a doorway out of the map or an alcove. The graph gives such an end a spur column.
- **The route crossed a wall, the upper-floor secret got no line** [F, L]: e03p01_a03_r01 has ONE `game_track` (x
  612..-4824 at z 0, volume y -320..256) and six `area_ladder`s; its goals sit on several floors (nuke4 y -256, the
  nuke barrel1 y 128, area_secret1 y 144). A graph with one node per column and one floor, spread from Duke's
  floor, routes along Duke's floor through the wall between rooms, and its goal point sits under the goal on
  another floor ("nothingness"). The secret snapped to the line (`len 12`, 2 points [L]) at Duke's floor. The floor
  graph and the wall rays (section 3) answer both.
- The `volume read y 0..2480` line is genuine [F, L]: e03p01_a01's line 5 runs along z from -10520 to -2088 while its
  rooms climb from y ~200 to ~1900 (its switches); one volume spans them.

## 5. area_teleport [D 0x1009d610, 0x1009d6e0, 0x1009d890, 0x1009d980, 0x1007f2f0; A 0x10087ed7; X vtable 0x10139ff8]

Counts per part are in [areas-and-secrets.md](areas-and-secrets.md) section 1.

- **Keys** (descriptors at 0x10167dd0..): `target` char[32] at `+0xCFC`, `start_on` byte `+0xD1C`, `effect_type`
  enum `+0xD20`, an enum at `+0xD24` (indexes the effect times at 0x10139fec: 0.2, 2.0, 0), a float `+0xD28`. Pins
  (ctor 0x1009d2b0): `teleport_on` `+0xB5C`, `force_trigger` `+0xBC4`, outputs `on_depart` `+0xC2C`, `on_arrive`
  `+0xC94`. `+0xD2C` the resolved target entity, `+0xD30` the actor in transit, `+0xD40` a list of actors inside.
- **Trigger: its volume, by itself, no use key.** The touch callback 0x1009d610 tests the pad's volume (`vt+0x1B8`)
  and keeps `+0xD40` (actors inside). An actor newly inside, with the area active (`+0xAE4`), starts the teleport:
  `effect_type` 1 at once (`area_teleport_begin`); `effect_type` 0 (with `force_trigger`'s `+0xC1C` set, a player)
  first walks Duke to the pad's centre (0x1007f2f0 sends message 0xB, the point projected on his track), and the
  player's state code calls `area_teleport_begin` on arrival (0x10087ed7). `begin` refuses while `teleport_on`'s
  value `+0xBB4` is 0, or with no target.
- **Destination: the target entity's position.** `area_teleport_do_teleport` 0x1009d890 calls the actor's
  `vt+0x218` (player_teleport) with the target's `vt+0x160` (`get_world_pos`, a p3shared import) and its `+0x1A8`
  angles (MSVC pushes the angles before the getter call). The target need not be a pad.
- **Duke is re-picked at the destination:** player_teleport re-picks his track (freecam-teleport.md 1b), and the
  handler 0x1009d980 ends with `player_set_state(3)` (the next pick) once the effect finishes (`+0xD68` 2 -> 0).
- **One way.** A pad sends only to its own target. `do_teleport` appends the actor to the TARGET's `+0xD40`, so a
  target that is itself a pad does not fire back until Duke leaves its volume and enters again: a two-way passage is
  two pads naming each other (123 of 130 are such pairs).

## 6. Route only when sure (the last rule before removal)

Verified in game before removal (e05p02_a01 screenshot spots: no wrong lines; ladder routes show).

**Rule:** draw the route only when every edge of it is a verified kind; otherwise draw nothing for the path (the
off-screen arrows, the nuke's box and the secret's volume already show where the target is). A beacon for the
no-route case was asked for and then dropped.

**Why:** three rounds of in-game testing found lines that were drawn but wrong. Round 1 (e03p01_a03_r01, section 4):
through a brick wall, and to the wrong floor. Round 2 (teleport pads): the small-pad fallback had no wall ray. Round 3
(e05p02_a01): a nuke path straight down through a walkway (a drop edge through a grate or a solid floor onto the
level below), ladder legs to the wrong place ("climb" where the way is through the ground, and the reverse on the
other side), no route during a climb, none for secrets reached by a raised platform or by a jump onto an archway,
none near some nukes. Each fix added a ray for one geometry; every unverified kind (a drop, a jump, a gap) kept
producing lines the world does not support. A wrong line costs more than no line.

**The rule** (`PfEdge::sure`, pathfind.h):
- **Walk** (and a join): the waist-high wall ray (as before) and a floor-continuity ray at the midpoint: the floor
  under it lies within 40 (the step) of both ends, or for a stair between them (`pf_mid_continuous`, tests 0432,
  0433). A walk over a gap stays in the graph, unsure (`gaps`).
- **Ladder:** the foot node stands in the ladder volume's footprint (each ladder's foot is pinned as a column, so a
  node is there when the line passes through it), the top node is the floor AT its top (within 32 of the volume's
  top, the node nearest the footprint, `PF_LADDER_TOP_DY`), and a waist-high ray from the ladder reaches both (tests
  0435, 0438). With no floor at the top the edge goes to the highest floor up to 256 below it, unsure (`topOff`).
  Without a ray nothing is checked: unsure. If real volumes reach more than 32 above their landing, every ladder
  reads unsure; the staging's `paths: ladder [k] ... top y T, D below the volume's top` lines measure it.
- **Teleport pad:** from a node in the volume, or a wall-checked fallback node, onto a landing whose floor ray hit
  (test 0436).
- **Depth run:** sure, as before.
- **Never sure:** drops and any other vertical travel. They stay in the graph only to tell "reachable but unsure" from
  unreachable.

- **End legs** (test 0439): the route leaves the graph twice, from Duke's point on his line to the first node and
  from the last node to the goal. Each must pass the walk test itself (`pf_leg_sure`: the waist-high wall ray and the
  midpoint floor), or nothing is drawn (`leg`). Two rays per leg at solve time, counted as `leg rays`.

**The solve:** two Dijkstras from one start every 250 ms, one over every edge and one over sure edges. The goal drawn
is the nearest by a sure route; with none, nothing is drawn and the solve is counted by reason (`pf_pick_sure`, tests
0434, 0439): `drop` (some goal reachable only through an unsure edge), `unreachable` (goals on the graph, none
reachable), `noLineFloor` (no live goal on a line or a floor at all), `leg` (the chosen route's end leg failed),
`noStart`. A nearer goal reached only by a drop loses to a farther one reached surely. When no secret here had a drawn
route, the secret path led to another area's exit doorway under the same rule (the cross-area door label of section 7
replaced it).

**The start node** (test 0437): a log of e05p02_a01 counted `no start` on 1740 of 7983 solves, including a minute
with Duke standing still on a line his staging read as `Duke line -1`, and on every solve while he was airborne. On a
ladder (states 5/6, `+0xB7C`) he starts from the nearest SURE ladder edge's two nodes, seeded by his height and x/z
distance to each (`pf_ladder_snap`); his first leg runs along that ladder and is not leg-tested. On an unsure ladder
he has no ladder start (test 0438). Otherwise his own line's floor, then a line within 64 (x/z) whose floor lies
within 64 of his feet though its volume misses him (`pf_start_by_floor`), then a spur. With none, his last start
serves for 1 s (`pf_start_hold`), so a jump or the ladder's ends do not blank the route; its first leg is drawn from
where Duke is now, and only when that leg passes `pf_leg_sure`. Counted every 5 s:
`paths: starts: on a ladder, by floor, held after a miss; no start: no line, no floor near his feet`.

Logs: the staging adds `paths: sure edges: walks over a gap G and drops D (unsure); ladders sure S of L (foot outside
the footprint F); teleports sure T of P (fallbacks walled W, landing rays missed M)`; the 5 s line adds per goal
`unsure` and `over`, and the cumulative `unsureRoutes: secret drop, noStart, unreachable; nuke ...`.

## 7. Removed

The Secret path and Nuke path rows and their config keys (`[esp] secretpath`, `nukepath`; an old ini's values are
skipped by the loader's unknown-key rule) are gone. The cross-area door labels stay, shown while Secret areas
(counting secrets) or Nukes (counting nukes) is on, as a label only.

**Why:** the sure-only routes were too limited. After section 6, a route was drawn only when every edge and both end
legs were verified, and in practice most goals had none (drops, jumps, platforms, unverified ladders), so the rows
rarely showed anything; the off-screen arrows, the secret volumes and the nuke boxes already show where a target is.
Four rounds on wrong or missing lines (sections 4 and 6) had not produced a route that could be relied on.

**What went:** the whole route solver (`pathfind.h`, its 29 tests 0371-0378, 0385-0389, 0395, 0398-0402, 0430-0439),
the staging and drawing (`paths.cpp`), and teleport.cpp's level-track read (`teleport_level_tracks`), which only the
paths used. Nothing else used them.

**What stayed:** the door label picks its door without a solve: fewest doorway hops, then the straight-line distance
from Duke to the doorway's volume centre, then file order (areas.h `ar_cross_pick`, tests 0450-0452, 0470). The
Progress tab's map and list follow the same choice.

## Logs

(No longer written.)

At staging: `paths: tracks N, connectors C, nodes V, edges E; joins J, runs attached R of C; ...; run ends with no line
S (spurs: ...), runs with no line Z; floor graph: nodes, edges (level, stairs, drops), edgesBlockedByWall B, columns with
no floor, runs (no floor), rays K; Duke line D; T ms`, then one line per line (columns, floor nodes) and per run
(`auto_type`, ends, lines a -> b, spur note). Every 5 s: `paths: ... secret goal <ent> ... (live, no line,
goalsNoFloorLine, unreachable; drawn segments, depth-run markers); nuke goal ...; edgesBlockedByWall, runEndsUnattached
(spurs, spursOver, runs with no line, runs with no floor); goal rays (missed); solves (no start, truncated, pops)`.
The 5 s line names the column graph's counts (`column nodes/edges`) apart from the floor graph's (`floor
nodes/edges`); `runEndsUnattached` = spurs + spursOver (a spur the node cap refused) + 2 x runs with no line. The
ladders' own `unread`/`over` are apart from the tracks', and a ladder the edge cap refuses counts in
`laddersOverEdges`, not `laddersNoFloors`. Both lines carry `teleportEdges`, `teleportsNoFloors` and
`teleportsUnread`; the staging line also `teleportsOverEdges`, the pads kept of those found, `off`, the caps,
`matrix fallback` and `landing rays missed`.

## Open questions

1. Bit 0x10 of `+0xE00` as "forward"; only bit 8 (use) matters for the route.
2. Whether the world ray passes from just below a floor's top to the next floor (one-sided floors); a back face hit
   is skipped by its normal either way. `columns with no floor` and the floor node counts would settle it.
3. Jumps, lifts and locked doors are not edges: a goal reached only that way reads `unreachable`, no route. The
   ladder bands (foot within 64 of the volume bottom, top floor within 256 of its top) are from six logged volumes.
   The top's wall ray runs at Duke's half height above each candidate floor from the ladder's centre: a landing whose
   lip stands higher than that would be refused (`ladderTopsWalled` rising with `laddersNoFloors` would show it).
4. Staging cost: up to 8 rays per column, up to 2 per pair of neighbouring floors (the midway ray and the wall ray:
   at most 32 per column edge) and 1 drop wall ray per floor with no level partner. By the caps alone that is ~150k
   rays; the largest level logged (577 columns) is ~4k rays, about 7 ms. The staging line's `rays` and ms settle it.
5. Teleport pads (section 5): whether `+0xBB4` is `teleport_on`'s value (pin +0x58) and whether a pad off at staging
   may turn on later; so pads were kept whatever it reads, and `off` counts them. One pair starts inactive
   ([areas-and-secrets.md](areas-and-secrets.md)): a route may use it before it is on. Also open: whether the
   target's world translation is `get_world_pos` (the matrix fallback to `+0x18C` as for lines). A pad smaller than
   the node spacing is reached from a fallback node a wall ray reaches (`fallbacks walled` counts the refusals);
   whether the waist-high ray is the right test for a booth is open.
