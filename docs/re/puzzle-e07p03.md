# e07p03_a01 puzzle: three cog buttons, order switch_1 -> switch_2 -> switch_3 (top, bottom, middle)

Three use-key buttons turn three cogs (`hinged_door`). The puzzle is solved when all three cogs are fully open
(`and_three1`). Each button works once per round: 0.7 s after the press its timer turns the cogs and disarms the
button (`area_active` 0, read at `+0xAE4`). Any press arms `switch_reset`, which closes the cogs and re-arms the
buttons. The only winning order is switch_1, switch_2, switch_3, and it is fixed (static wiring, no random entity). A
cog's `opened` pin does send 0 when it starts closing, which settles section 7's open question in
[puzzle-e05p02.md](puzzle-e05p02.md). The menu's puzzle hints for this map work in game. The overlay reads the button
`+0xAE4` and the cog `+0xCAC` (opened), `+0xF4C` (motion); the pure rule is `pz_e07_wrong` in
`src/nukepanel/puzzlerule.h`.

Evidence tags as in [puzzle-e05p02.md](puzzle-e05p02.md): [S] map data (`maps/e07p03_a01/world.scr` and `avolume`
files in `duke/base/db_e07.zipfs`, read with Python zipfile), [D] decompile read, [A] raw bytes. Addresses are in
duke_base.dll. Ghidra function names: `hinged_door_slot18` 0x10111480 (the handler) and `hinged_door_fn_119e0`
0x101119e0 (the save writer).

## 1. The entities [S]

| name | class | position | role |
|---|---|---|---|
| `switch_1` | area_switch, `switch_type:3` (Manual Trigger Always, use key) | < 204 672 -140 > | top button -> `timer5` |
| `switch_3` | area_switch, 3 | < 204 480 -140 > | middle button -> `timer6` |
| `switch_2` | area_switch, 3 | < 204 288 -140 > | bottom button -> `timer4` |
| `switch_reset` | area_switch, 3, `area_start_active:false` | < -8 240 -552 > | closes everything, re-arms the three |
| `mdl_switch3` / `mdl_switch5` / `mdl_switch4` / `mdl_switch2` | model `button.grn` | < 224 656 -160 > / < 224 464 -160 > / < 224 272 -160 > / < 48 272 -552 > | the visible buttons of switch_1 / 3 / 2 / reset: switch position + (20, -16, -20), reset + (56, 32, 0); red skin, green once pressed |
| `cog_1` / `cog_2` / `cog_3` | hinged_door, `open_angle:180`, speed 80 | x -288, y 768 / 576 / 384, z 352 | the cogs; `opened` -> `and_three1` input 3 / 2 / 1 |
| `and_three1` | and_3, `signal_type:1` | - | solved gate -> `help_area_puzzle_solve` ("You got them all!"), `puzzle_solve_sound`, `not7` |

The switch volumes (`avolume`, local min/max) are min (-28 -64 -76) max (84 64 36) for the three buttons, so each
volume's centre is the switch position + (28, 0, -20), beside its button model. The overlay's marker anchor is the
button model position (switch + (20, -16, -20)) from map data, not mesh bounds.

## 2. The wiring [S]

```
switch_1.signal -> timer5.active, switch_reset.area_active (arms it), timer7 (camera)
timer5 (0.7 s, once) -> cog_2.open, cog_1.close, cog_3.close, not1.input -> switch_1.area_active 0
switch_2.signal -> timer4.active, switch_reset.area_active, timer7
timer4 (0.7 s, once) -> cog_1.open, cog_3.close, not2.input -> switch_2.area_active 0
switch_3.signal -> timer6.active, switch_reset.area_active, timer7
timer6 (0.7 s, once) -> cog_3.open, not3.input -> switch_3.area_active 0
switch_reset.signal -> cog_1/2/3.close, switch_1/2/3.area_active (re-arm), not8.input -> switch_reset.area_active 0
and_three1.output (all three opened) -> help_area_puzzle_solve.area_active, puzzle_solve_sound.active,
                                        not7.input -> switch_reset.area_active 0 (no reset after a solve)
```

The ramps (`ramp1a`..`ramp3b`) follow their cog; `and_allopen1` and `cog_a1` are named in wires but never spawned.

## 3. What the classes do [D]

- **area_switch.** `area_switch_touch` 0x1009c9c0 returns at once while `+0xAE4 == 0`. That is the stored value of the
  `area_active` input pin (pin at +0xA8C, registered by the area base ctor 0x10091640; value = pin + 0x58). The same
  dword is the secrets' armed flag ([entities.md](entities.md) section 4). **A button is used up (pressed this round)
  when `+0xAE4 == 0`.** It drops 0.7 s after the press, when the timer fires `notN`.
- **hinged_door** (factory 0x100fb190, alloc 0xF74; ctor 0x10110f30, vtable 0x1014a390, classname "hinged_door").
  Pins: `running` +0xBEC, `opened` +0xC54, `closed` +0xCBC, `open` +0xD8C, `close` +0xDF4, `locked` +0xE5C,
  `toggle` +0xEC4. So the values are **opened +0xCAC, closed +0xD14**; init 0x10111700 writes them as (0, 1) for a
  door that starts closed. **+0xF4C is the motion**: 1 opening, 0 closing, -1 at rest (ctor -1).
  Handler `hinged_door_slot18` 0x10111480:
  - `open` is honoured only while `closed` (+0xD14) is 1, i.e. fully closed;
  - `close` only while `opened` (+0xCAC) is 1, i.e. fully open. **A close that arrives while a cog is still turning
    is dropped.**
  - Starting to move (0x10111380) sends running 1, closed 0, **opened 0**. Arriving open (0x101112d0) sends opened 1
    and sets +0xF4C = -1. Arriving closed (0x10111220) sends closed 1, opened 0, +0xF4C = -1.
  - 180 degrees at 80 deg/s: about 2.25 s per turn.
- **Save/load.** The save writer 0x101119e0 and the loader 0x10111cc0 keep +0xCAC, +0xD14, +0xF4C and the angle. So
  a loaded game keeps the cog state.

## 4. The solution, derived

Cog targets (cog_1, cog_2, cog_3) after each correct prefix: none 000; switch_1 010; then switch_2 110; then
switch_3 111 = solved. All other orders end with a closed cog. Each button disarms itself, so a wrong order cannot be
repaired by pressing more buttons. **Wrong** is therefore: the used buttons are not a prefix of (1, 2, 3), or the cog
targets differ from that prefix's row. A cog's target is open while +0xF4C is 1, closed while it is 0, and its
`opened` value at rest. The fix is the reset button (armed by any press). As on e05, one or two delivery passes
separate a button's disarm from its cogs' motion, so the overlay acts on `wrong` only after it has held 250 ms.

## Open questions

- Whether `area_start_active:false` loads `+0xAE4 = 0` for `switch_reset` (the loader was not read). The puzzle log
  line prints the four buttons' values when the map is recognised.
- A press made while a cog is still turning: its `close` is dropped (section 3), so a fast wrong order can leave a cog
  open, and `switch_reset` cannot close a cog that is still turning either. The rule then keeps showing "Reset" until
  the cogs come to rest. Not seen in game.
