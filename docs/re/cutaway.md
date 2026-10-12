# Cut-away walls: `fader_entity`, driven by an "Auto Buzzer" `area_switch`

Every cut-away wall is a `fader_entity` (82 in the shipped maps, 29 maps). Its alpha lives
at `+0xB84`, and its own per-frame handler moves it toward `min_trans` or `max_trans` according to a state word at
`+0xB88`. An `area_switch` with `switch_type:4` ("Auto Buzzer": on while Duke is inside the volume) drives that state
through the fader's `active` pin. To force every fader into its cut-away state, enumerate with
`spawn_search("s=%d?t=%d", sid, 0x200000)` plus the class name, and each frame write `+0xB88 = 2` (fade out) whenever
`+0xB84` is above `min_trans`. The handler writes that same field with the same value. Leave the pin alone. To undo,
call `pin_t::set` on the fader's `active` pin (p3shared export): the handler then re-derives the state from the
switch's last signal, which it kept. The engine re-fades a wall when Duke leaves its room, so the write must be
re-asserted every frame. The menu does exactly this (View > CAMERA switch, all faders, animated fade at the map's own
`fade_time`), verified in game.

Method: Ghidra MCP and Python on the map data. Evidence tags: [D] decompile read, [A] disassembly, [X] xref/string, [S] map-script data (world.scr in
`duke\base\db_*.zipfs`, read with Python zipfile). Addresses are duke_base.dll (base 0x10000000) unless a module is
named. Functions named in Ghidra: `fader_entity_ctor/_init/_handler/_draw_mesh`, `area_switch_ctor`,
`area_switch_touch`, `area_switch_net_apply`.

## 1. How levels mark cut-away walls [S]

- A cut-away wall is a **`fader_entity`** with its own mesh (`maps/<map>/<name>/mesh.gdt`, aabb.bsp, lightmap).
  Properties: `fade_time` (s), `min_trans` (%, 0..45 in the data), rarely `max_trans`, `start_active`,
  `alpha_mask`, `outside_lit`. The names `room`/`room_track1` in e01p01_a01 are unrelated: `room` is a
  `map_change` and `room_track1` is a `game_track`.
- The "room" is an **`area_switch`** volume (`avolume`) with `switch_type:4`, usually `sound_type:1` (silent), and
  `connect signal:<fader>.active`. Census over every world.scr:

| driver of `<fader>.active` | count | notes |
|---|---|---|
| `area_switch` (all `switch_type:4`) `signal` | 77 | 70 always armed; 7 armed only by an event (6 by a `breakable_adv` `broken` -> `area_active`, e.g. e01p01_a01 `fader_entity1` next to `area_secret3` behind `breakable_adv1`; 1 by another switch) |
| `area_secret` `output` | 1 | demo_area_secret |
| `fader_entity` `signal`, `breakable_adv` `broken`, `not` `output` | 1 each | e01p03_a01_r01, e03p03_a02, e07p03_a02 |
| nothing | 1 | e04_stats `fader_pillar8` |

- Property values in the data: `min_trans` 0/5/10/15/20/25/45 (default 30), `fade_time` 0.5..5.0 (default 1.0),
  `alpha_mask:1` once, `start_active:true` once, `outside_lit:false` once.

## 2. The fader class

Class table entry `FUN_100d7aa0` registers `fader_entity` (category "world", size 0xB8C, factory 0x100d9b60) [D].
Vtable **0x1013EBA8**. `vt[1]` 0x100bc430 is `mov eax,[0x1016d828]; ret`, which returns the name string
"fader_entity" 0x1016da60 [A]. That is the same classname getter the entity walk already calls.

**Ctor** `fader_entity_ctor` 0x100bbf80 [D]: base chain 0x100ec580 -> 0x100ede80 -> `world_e::world_e`. No
`dobj_base_ctor`, so the fader is in **neither the t=2 nor the t=0x20 list** ([secrets-walk.md](secrets-walk.md)).
It sets `+0x48 |= 0x200000`, `+0xB08 = 1.0f`, `+0xB0C = 30`, `+0xB10 = 100`, `+0xB14 = 0`, `+0xB18 = 0`,
`+0xB84 = 255.0f` (overwritten by init), `+0xB88 = 0`, `+0xB04 = 1`, `+0x2E0 = 1`. It builds `pin_t` at **+0xB1C**
(assign 1 = input, type 0) and registers it as **"active"** via `entity_base_t::regp`.

| field | meaning | evidence |
|---|---|---|
| +0xB08 float | fade_time (s); init clamps <=0 to 1e-4 | D, init + handler divisor |
| +0xB0C int | min_trans %, clamped 0..100 | D, init; handler target when fading out |
| +0xB10 int | max_trans %, clamped 0..100 | D, init; handler target when fading in |
| +0xB14 byte | start_active | D: init starts alpha at min if set and sends it on the pin |
| +0xB18 int | alpha_mask: 0 = "A---" (alpha byte), 1 = "-RGB" (colour scale), 2 = "ARGB" | A, draw switch; enum strings 0x1016da40.. (stored in reverse) |
| +0xB70 / +0xB74 | the `active` pin's `+0x54` "set" flag and `+0x58` value | D, `pin_t::receive` p3shared!0x1001be90 |
| **+0xB84 float** | **current opacity 0..1** | D, handler + draw |
| **+0xB88 int** | **state: 0 idle, 1 fading in (to max), 2 fading out (to min)** | D/A, handler |

**Init** `fader_entity_init` 0x100bc0d0 (vt+0x74) [D]: clamps the fields and sets
`+0xB84 = (start_active ? min : max) * 0.01`. It then calls `pin_t::send(+0xB1C, sid, id, start_active)`.

**Handler** `fader_entity_handler` 0x100bc190 (vt+0x58) [A]. It runs once per frame for every active entity, per
[timing.md](timing.md) §2 step 2:
```
world_e::handler
if pin.set (+0xB70): +0xB88 = pin.value (+0xB74) ? 2 : 1 ; +0xB70 = 0     // 0x100bc199..0x100bc1c3
if +0xB88 == 2: +0xB84 -= dt/fade_time; if <= min*0.01: +0xB84 = min*0.01, +0xB88 = 0   // ..0x100bc214
if +0xB88 == 1: +0xB84 += dt/fade_time; if >= max*0.01: +0xB84 = max*0.01, +0xB88 = 0   // ..0x100bc262
```
`dt` is the double at `server+0x38` (`[ent+0xE0]+0x38`). **Signal value 1 = fade out to min (cut-away), 0 = fade
back in.** The handler writes `+0xB88` with the values 0, 1 and 2, and it writes `+0xB84`.

**Draw.** `fader_entity_draw_mesh` 0x100bc2a0 (vt+0x1A4, a per-mesh callback) [A]. For each mesh with `+0x10 != 0`
and `(+0x58 & 2) == 0`, it computes `a = ftol(+0xB84 * 255.0)` (constant 0x10127e28 = 255.0f). Then, for all
`+0x50` entries of the colour array `+0x38`, it does `c = (c & mask) | a-pattern`, with the mask chosen by alpha_mask.
It then calls `map_draw_mesh` 0x100ee5b0. The alpha is re-baked into the vertex colours on **every draw** from
`+0xB84`, on the CPU, so it is renderer-neutral (gl/dx8). `vt+0x19C` 0x100bc270 picks a render-pass table
(0x1013EB68 when mesh `+0x58 & 2`, otherwise 0x1013EB40) [D]. The blend states in those tables were not decoded.

**Save/load** [D]. vt+0xE0 0x100bc360 stores `+0xB74`, `+0xB84` and `+0xB88`. vt+0xE4 0x100bc3c0 restores them, then
`pin_t::send(+0xB1C, sid, id, +0xB74)`, so after a load the handler re-derives the state from the pin value.

## 3. The trigger: area_switch

`area_switch_ctor` 0x1009c5a0 [D]. Vtable 0x10139E18, size 0xC9C. It builds the output pin **"signal" at +0xB94**
(assign 2, type 1) and "on_exit" at +0xBFC, and sets `+0x4C |= 0x20`. Base 0x10099240 sets `+0x4C |= 2`. Fields and
defaults: `+0xB7C` switch_type (1), `+0xB84` activator (1 = "Player"; enum 0 All(auto), 1 Player, 2 Monsters(auto),
3 Obstacles(auto), strings at 0x10167c10 stored in reverse), `+0xB8C` on_value (1), `+0xB90` off_value (0),
`+0xB78` sound_type (0), `+0xAF4` current on/off byte, `+0xAE4` armed (the `area_active` gate).

- **Who tests Duke against the volume.** The player's area walk duke!0x1007d1e0 runs
  `spawn_search("s=%d?t=%d?Y=%d", sid, 0x20, 2)` and calls `area->vt[0x1BC]` with the player for every armed area
  ([secrets-walk.md](secrets-walk.md) §4). Every area_switch has `+0x4C` bit 2, so it is visited every tick whether
  Duke is inside or not. Switches with activator "Player" are skipped by the switch's own poll 0x1009c950 [D].
- **`area_switch_touch`** 0x1009c9c0 (vt+0x1BC) [A]. It returns unless armed (`+0xAE4`). `inside = vt+0x1B8(ent)`
  (0x10091840). The switch_type jump table is at 0x1009cc18. **Case 4 (0x1009cb57): if off and inside -> on; if on
  and not inside -> off.** The names are stored in reverse at 0x10167c40.., which makes index 4 "Auto Buzzer". When
  the state changes it calls `entity_t::net_send(state + 2)`.
- **`area_switch_net_apply`** 0x1009cdb0 (vt+0xFC) [A]: on -> `pin_t::send(+0xB94, sid, id, on_value)` at
  0x1009ce0a, off -> `send(..., off_value)` at 0x1009ce3f, sound via vt+0x1D0 when sound_type == 0, then
  `+0xAF4 = state`.
- **Delivery** [D]. `pin_t::send` only stores the value and sets the flag (p3shared!0x1001be90).
  `entity_t::pin_io` p3shared!0x1000d6d0 walks **output** pins (assign 2) that have the flag set. For each connection
  (+0x64 list) it calls `DAT_1004bf50(conn, sid, id, value)`, then `pin_t::reset`. The target's input pin
  receives it through `pin_t::receive` (vt+4, the same code as send). The routine behind `DAT_1004bf50` was not traced.
  `entity_base_t::set_pin` p3shared!0x1000e200 and `spawn_set_pin("ent.pin", sid, id, v)` prism3d!0x422200 reach
  the same vt+4 by name. **An input pin's flag is cleared only by its owner:** pin_io skips assign 1, and the fader
  handler clears +0xB70 itself.

So the chain is: Duke enters -> the switch sends 1 -> the fader state becomes 2 -> alpha fades to min_trans over
`(1-min)*fade_time`. When Duke leaves: the switch sends 0 -> state 1 -> the alpha fades back to max.

## 4. The mechanism the menu uses

**Enumerate (once per map, and again on a level change, like the existing walk):**
`spawn_search("s=%d?t=%d", sid, 0x200000)`. Lowest bit 21 selects list `server+0xE4+21*0x14`. The fader's `+0x48` is
0x200101: object 0x1 and world 0x100 come from the base ctors, per secrets-walk §3. 11 duke ctors set 0x200000
(secrets-walk §3), so keep the entries whose `vt[1]()` returns "fader_entity". Apply the same guards as `walk()`:
vt and vt[1] inside duke, a span of at least 0xB8C validated once per walk.

**Force (each frame while the switch is on; main thread, from the existing frame hook):**
```
min = (float)(+0xB0C) * 0.01f
if (+0xB84 > min && +0xB88 != 2) +0xB88 = 2;   // the engine's own "fade out" state
```
- It writes one field, and only with a value the handler itself stores there. The pin (+0xB70/+0xB74) is never
  touched, so the switch's latest signal stays recorded.
- The wall fades out at the map's own `fade_time`, exactly as if Duke had walked in, and stops at `min_trans`
  (the handler clamps it and sets state 0).
- When a switch fires while the switch is on, the handler reads the pin flag and sets state 1 before the draw. For
  one frame the alpha rises by `dt/fade_time`: about 3 % at 60 fps with fade 0.5 s. The next frame's re-assert
  brings it back. If that one-frame blip is visible, also snap `+0xB84 = min` (the handler writes that value too).

**Undo (once, when the switch goes off):** for each cached fader, `pin_t::set(fader + 0xB1C)`. That is the
p3shared export `?set@pin_t@@QAEXXZ` 0x1001beb0 (`+0x54 = 1`). A pin-offset-free equivalent is
`entity_base_t::get_pin(ent, "active")` p3shared!0x1000e190 followed by `pin_t::set`. On the next frame the handler
sets the state from the recorded value: 1 (Duke inside) keeps the wall at min, 0 fades it back to max. Do not call
`pin_t::receive` or `send` to undo: they would overwrite the switch's value.

**Offsets that cannot be avoided** (fader fields): `+0xB84`, `+0xB88` and `+0xB0C`. To check them before any write,
read the instructions through the fader's own vtable. `vt[0x58/4]` must be the handler. At handler+9 (0x100bc199)
the bytes must be `8B 86 70 0B 00 00 33 C9 3B C1 74 24 39 8E 74 0B 00 00 74 0C C7 86 88 0B 00 00 02 00 00 00`
([A], read_memory). Their displacements hold +0xB70, +0xB74 and +0xB88. At handler+0x4D (0x100bc1dd) the bytes must
be `D8 B6 08 0B 00 00 D8 AE 84 0B 00 00`, the fdiv and fsubr that use +0xB08 and +0xB84 ([A], read_memory). A mismatch
disables the feature. The +0xB0C read is at 0x100bc1ef (`DB 86 0C 0B 00 00`, fild).

## 5. Interactions

- **Re-fade on leaving a room:** The player's area walk visits every armed switch every tick. On exit the switch
  sends `off_value` and the fader fades back in, so the force must be re-asserted each frame (§4).
- **Armed gate:** 7 faders are driven by switches that stay unarmed (`+0xAE4 == 0`) until an event: a breakable
  wall, often in front of a secret room. Forcing every fader reveals those rooms early.
- **Culling:** the alpha is baked at draw time from `+0xB84` and does not depend on visibility. Maps are one sector
  and one leaf ([pvs.md](pvs.md)), so a forced wall seen from anywhere, the free camera included, is at `min_trans`.
- **Collision:** the fader only changes vertex colour. A cut-away wall keeps whatever collision it had, forced or not.
- **Savegames:** safe. A save made while forced stores the forced `+0xB84`/`+0xB88`. The load sends the stored pin
  value, which resets the state to what the switch last said.
- **AA "Cutaway sky":** unrelated, only the word is shared. In the AA port `freecamcutawaysky` (config.h:236) lets
  EDuke32's parallax sky draw while that port's free-camera cutaway paints the Build map. It is a Build-renderer
  feature with no counterpart here.

## 6. Open questions (with the measurement that settles each)

1. The `t=0x200000` search returns every fader: log `faders=N` per map. On e01p01_a01 it must be 2 (`fader_entity1`,
   `fdr_door1`), and 0 on maps without faders.
2. The handler runs for faders at any distance (timing.md says every active entity): after forcing, log `+0xB84` for
   each fader until it equals `min_trans*0.01` within `fade_time`.
3. Undo restores the right state: with Duke inside a cut-away room, turn the switch off. That wall must stay at min
   and the others must return to `max_trans*0.01`. Log `+0xB74`, `+0xB88` and `+0xB84` per fader for 2 s after the
   toggle.
4. The one-frame blip when a switch fires under force is invisible: log the maximum `+0xB84 - min` seen per fader
   over a room crossing.
5. `+0xB04` = `outside_lit`, and the meaning of the pass tables 0x1013EB40/68 (blend on or off): not needed for
   the lever. Read the property registration or the pass bytes if they ever matter.
6. The routine behind `DAT_1004bf50` (connection delivery): not traced, and the lever does not use it.
Scope and look (the menu's choices, in section 4's mechanism): all faders are forced, which also reveals the 7
event-gated walls, secret rooms included. Forcing only faders whose driving switch is armed (`+0xAE4`) would need a
switch-to-fader mapping (a walk of each area_switch's `signal` connections), which is not mapped. The fade is animated
at the map's `fade_time`; an instant snap would also write `+0xB84 = min`.
