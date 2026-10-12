# e05p02_a01 puzzle: three coloured floor plates, order Yellow -> Blue -> Red

The puzzle is a row of four floor plates (blue, red, yellow, reset). Duke sets one off by stepping or jumping onto
it. The level wiring builds a 3-step sequence lock out of three toggle flip-flops. The only winning order is
Yellow, then Blue, then Red. It is fixed: no `rnd` or `1of*` entity takes part, and the wiring is static map data.
A wrong order cannot be repaired by pressing more plates (each plate fires once per round). The player must step
on the fourth plate (reset), which raises every plate and clears the flip-flops. The overlay reads each plate's
mover point index (`+0xE4C`) and each flip-flop's output (`+0x20C`), and finds every entity by name with
`spawn_search("s=%d?n=%s")`. The solved state is `mover_spike_platform +0xE4C == 1`. One more level has a similar
puzzle: e07p03_a01 (three cog levers, order 1-2-3; section 6 and [puzzle-e07p03.md](puzzle-e07p03.md)). The
Visuals row "Puzzle solutions" draws both (`puzzle.cpp`, rules in `puzzlerule.h`); the hints work in game.

Evidence tags: [S] map data (`maps/e05p02_a01/world.scr` and `avolume` files in `duke/base/db_e05.zipfs`, read with
Python zipfile; no other archive overrides them), [D] decompile read, [A] disassembly, [X] xref. Addresses are in
duke_base.dll (base 0x10000000) unless another module is named. Positions are `<x y z>` with **+Y up**
([entities.md](entities.md)). Ghidra functions named: `register_engine_classes` 0x100f95e0,
`flipflop_handler` 0x10104290, `and4_handler` 0x101001a0, `not_handler` 0x101039b0, `cmp_handler` 0x10103f30,
`mover_post_init` 0x100f2dc0, `mover_handler` 0x100f2ec0, `mover_pin_inputs` 0x100f3000, `area_switch_touch`
0x1009c9c0.

## 1. The entities [S]

| name | class | position | role |
|---|---|---|---|
| `area_switch_blue_button` | area_switch, `switch_type:7` | < 7784 4430 3086 > | plate trigger -> `mover_blue_button.run_to` |
| `area_switch_red_button` | area_switch, 7 | < 7978 4430 3085 > | -> `mover_red_button.run_to` |
| `area_switch_yellow_button` | area_switch, 7 | < 8170 4430 3086 > | -> `mover_yellow_button.run_to` |
| `area_switch_reset_button` | area_switch, 7 | < 8356 4430 3086 > | `signal` -> `mover_reset_button.run_to`; `on_exit` -> `not_reset1.input` |
| `mover_blue_button` / `_red_` / `_yellow_` / `_reset_` | mover_adv, 2 way points | origin x 7732 / 7924 / 8116 / 8308, y 4424, z ~3060 | the plate mesh; point 0 = up (y 4424), point 1 = sunk 32 units (y 4392) |
| `blue_button_light` / `red_` / `yellow_` | controlled_light | x 7760 / 7952 / 8144, y 4464, z 3072 | lit by the plate's own `pos_signal` (shows "pressed" only) |
| `flipflop1` / `flipflop2` / `flipflop3` | flip-flop | - | the three sequence bits |
| `and_four2` | and_4 | - | success gate |
| `and_four1` | and_4 | - | "wrong" gate -> `sound_wrong` (key_error.ogg) |
| `cmp1`, `not_reset1`, `not_reset2` | cmp, not, not | - | the reset path |
| `not_final`, `not_cover_pos` | not | - | disarm on success; the cover-closed term |
| `timer2` 1.0 s, `timer3` 1.2 s | timer, trigger_once | - | success -> camera, then the payoff |
| `mover_spike_platform` | mover_adv | < 6664 4764 3192 > -> point 1 at z 3000 | the payoff: slides 192 units along z |
| `sound_right` / `sound_wrong` | plainsound | ~< 7890 4560 2906 > | pu_key.ogg / key_error.ogg |

The plates sit in one row along x at the same height and depth. From left to right: **blue, red, yellow, reset**,
spaced ~192 units apart. Plate volumes (`avolume`, read as an AABB relative to the switch position [S]: blue
min -52 -90 -26 / max 4 2 26) put the plate centres at x **7760 / 7951 / 8143 / 8336**, top y 4432, z 3086. These
match the light positions and are always **mover origin + 28 in x** (also true for the reset plate). The overlay's
marker anchors use this offset and work in game.
`area_switch2` (< 7576 4508 3144 >, Auto Trigger Once) plays "wonder what this does" on the way in.

## 2. The wiring [S]

```
plate switch (Auto Trigger Always) --signal 1--> mover_X.run_to         (plate sinks to point 1)
mover_red.pos_signal    -> flipflop3.input, red_light.activate,    and_four1.input2
mover_blue.pos_signal   -> flipflop3.reset, flipflop2.input,  blue_light.activate,   and_four1.input3
mover_yellow.pos_signal -> flipflop3.reset, flipflop2.reset, flipflop1.input, yellow_light.activate, and_four1.input4
flipflop3 -> and_four2.input1 ; flipflop2 -> input2 ; flipflop1 -> input3 ; not_cover_pos -> input4
mover_spike_platform.pos_signal -> not_cover_pos.input
and_four2.output -> sound_right.active, not_final.input, timer2.active
not_final.output -> area_active of all four plate switches, and_four1.input1
timer2 -> security_camera1.active, timer3.active ; timer3 -> mover_spike_platform.run_to, voice_over3 ("sometimes I amaze myself")
reset plate: signal -> mover_reset.run_to ; on_exit -> not_reset1 -> mover_reset.run_to (back up)
mover_reset.pos_signal -> cmp1.input_B ; cmp1 (A==B, default_A 1, signal_type 1) -> flipflop1/2/3.reset, not_reset2
not_reset2.output -> mover_red/blue/yellow.run_to (back up)
```

## 3. What each class does with a signal [D]

- **Delivery.** `pin_t::send` (p3shared!0x1001be90) stores the value at pin+0x58, the sender ids at +0x5c/+0x60,
  and sets the flag at +0x54. `pin_t::reset` (p3shared!0x1001bd30) clears only +0x54. **So +0x58 keeps the last
  value** [A]. Delivery to the wired targets is deferred to `entity_t::pin_io` ([cutaway.md](cutaway.md) section 3). Each
  logic handler below runs only when one of its input flags is set.
- **area_switch `switch_type:7`** = **"Auto Trigger Always"**: it fires `signal` = on_value (1) each time Duke
  enters the volume. Activation needs no use key. Enum strings 0x10167c40.. are stored in reverse: 0 Manual Buzzer,
  1 Manual Toggle, 2 Manual Trigger Once, 3 Manual Trigger Always, 4 Auto Buzzer, 5 Auto Toggle, 6 Auto Trigger
  Once, 7 Auto Trigger Always [A]. Case 7 of `area_switch_touch` 0x1009c9c0 fires only when Duke is inside and was
  not in its occupant list (+0xC74) [D]. Leaving sends `on_exit` = 1 [D].
- **mover_adv** (vtable 0x1013ef30, ctor 0x100be050 -> base 0x100f2530, size 0xF94). Pins: `run_to` +0xB74,
  `active` +0xBDC, `pos_signal` +0xD7C [D ctor]. `mover_pin_inputs` 0x100f3000: on `run_to` with value v, if v
  differs from the current point (or the mover is running) it starts the mover toward point v (+0xE54 = v).
  Re-pressing a sunk plate therefore does nothing [D]. **`pos_signal` sends `+0xE4C`, the current way-point index**:
  once at `mover_post_init` 0x100f2dc0 (map start, so every plate reports 0 and `not_cover_pos` outputs 1), then on
  each arrival in `mover_handler` 0x100f2ec0 [D].
- **flip-flop** (factory 0x100fa7f0, ctor 0x10104130, vtable 0x10148750, size 0x21C). Pins: `input` +0xE4,
  `reset` +0x14C, `output` +0x1B4. `flipflop_handler` 0x10104290 [D]: when the reset flag is set and the value is
  non-zero, it sends 0. When the input flag is set and the value is non-zero, it **toggles**, sending
  `(+0x20C == 0)`. Reset is processed before input. **+0x20C (output pin + 0x58) = the current bit.**
- **and_4** (ctor 0x100fffe0, vtable 0x101479b8, size 0x2F0). Inputs +0xE8/+0x150/+0x1B8/+0x220, output +0x288,
  mode +0xE4 (0 = always send; 1 = only when true; 2 = only when false). `and4_handler` 0x101001a0: when any input
  flag is set, it sends the AND of the four **stored** values [D]. Output value: +0x2E0.
- **not** (ctor 0x10103870, size 0x1B4): `not_handler` 0x101039b0 sends `value == 0` when the input flag is set [D].
- **cmp** (ctor 0x10103d40, size 0x22C): `cmp_handler` 0x10103f30. compare_type at +0xE4 (descriptor at 0x10103b41:
  "compare_type" -> offset 0xE4 [A]); 2 = `A == B`. Signal mode +0xE8 (1 = send only when true) [D]. cmp1 therefore
  fires once, when the reset plate is fully down. It does not fire when the plate rises again.

## 4. The solution, derived

A plate's `pos_signal` is 1 only on arrival at point 1, and each plate arrives once per round. So:

- flipflop1 = 1 after Yellow is pressed. Nothing else touches it.
- flipflop2 = 1 only if **Blue is pressed after Yellow**, because Yellow resets it.
- flipflop3 = 1 only if **Red is pressed after both Blue and Yellow**, because both reset it.
- `not_cover_pos` = 1 while the spike platform is at point 0.

`and_four2` is true only for **Yellow -> Blue -> Red**. All other 5 orders end with a 0 bit. When all three plates
are down and not solved, `and_four1` plays the key-error sound, and the plates stay down. Stepping on the **reset
plate** (far right) sinks it. `cmp1` then clears the flip-flops, and `not_reset2` sends the three plates back to
point 0 (their `pos_signal` 0 neither toggles nor resets anything). Leaving the reset plate raises it. **Success:** the
chime plays, `not_final` disarms all four plates, and 1.0 s later `security_camera1` shows the platform for 5 s.
After another 1.2 s `mover_spike_platform` slides to point 1, and Duke says "sometimes I amaze myself". The timers
are `trigger_once`, so the payoff happens once per map load.

## 5. Runtime fields for an overlay

Find each entity once per map with `spawn_search("s=%d?n=%s", sid, name)` ([entities.md](entities.md): name at
+0x04, the `n=` filter). Check its class with vt[1] (`mover_adv`, `flip-flop`). Validate the span once per walk
(0xF94 for a mover, 0x21C for a flip-flop), then read:

| what | read | meaning |
|---|---|---|
| plate pressed (Y/B/R/reset) | `mover_<c>_button +0xE4C` (int) | 1 = sunk (pressed), 0 = up |
| sequence bits | `flipflop1/2/3 +0x20C` (int) | F1 yellow-done, F2 blue-after-yellow, F3 red-after-both |
| solved | `mover_spike_platform +0xE4C` == 1, or F1 & F2 & F3 all 1 | hide the hints |
| plate centre (marker anchor) | mover position `+0x18C` + (28, 8, 26) | rides down with the plate; static fallback x 7760/7951/8143/8336, y 4432, z 3086 |

The decision is a pure rule, so it can go in the unit-test host (inputs pY, pB, pR, F2, F3, solved):

```
if solved                                   -> nothing (or "done")
wrong = (pB && !(pY && F2)) || (pR && !(pY && pB && F3))
if wrong                                    -> mark RESET plate ("R" / arrow), optionally cross the others
else next = first of Yellow, Blue, Red not pressed -> highlight it; label the plates "1" "2" "3" in that order
```

The suggested cue is a number above each plate in its own colour (Yellow "1", Blue "2", Red "3"), with the next one
pulsing or carrying an arrow. When `wrong`, show "RESET" over the right-most plate. A step counts as done when its
plate's `+0xE4C` reads 1.

**Transients.** The flip-flop bits change one or more delivery passes after the mover arrives (section 3). For a moment
"Blue pressed" can be read before F2 has toggled, and the rule above would flash "wrong". Debounce: act on a
`wrong` state only after it has held for about 250 ms.

## 6. Other levels

A census of logic entities over all 129 world.scr files [S] (flip-flop 10, and_4 4, and_3 8, cmp 106, counter 15,
rnd 6, 1of4 4) found **one other puzzle**:

- **e07p03_a01, cog puzzle** ("You got them all!", string 07002 on `help_area_puzzle_solve`). It has three levers,
  `switch_type:3` (Manual Trigger Always, use key): `switch_1` < 204 672 -140 > (top), `switch_3` < 204 480 -140 >
  (middle) and `switch_2` < 204 288 -140 > (bottom). After a 0.7 s timer, each one turns cogs: switch_1 opens cog_2
  and closes cog_1 and cog_3; switch_2 opens cog_1 and closes cog_3; switch_3 opens cog_3. `and_three1` needs all
  three `hinged_door` cogs `opened`. Each lever disarms itself after use, and `switch_reset` < -8 240 -552 > closes
  everything and re-arms them. **The only order is switch_1 -> switch_2 -> switch_3 (top, bottom, middle); fixed.**
  [puzzle-e07p03.md](puzzle-e07p03.md): `opened` sends 0 when a cog starts to close, so only that order passes; the
  state fields are there too.
- Not puzzles: e07p02_a01/a02 flip-flops (drill and steam timing hazards), e02p01_a01 and e08p03_a03 (cutscene
  counters and random sounds), e08p03_a02 (the Mechnukem boss sequence), e03p01_a01, e05p01_a02, e05p03_a02
  (breakable or camera gates).

## Open questions

- The `avolume` layout (min xyz, max xyz, relative to the switch position) is read from the bytes. It agrees with the
  light positions, but no loader was read.
- When `+0xE4C` changes during travel (on arrival or at the start) was not traced inside vt+0x1B0/0x1B8. A 1 Hz log
  of the four movers' +0xE4C/+0xE54 while pressing a plate would settle it.
- `pin_io` delivery order and depth per tick were not traced. The wiring predicts two stray error sounds. (a) On the
  correct third press, `and_four1` sees red=1 while `not_final` is still 1, so it may sound together with the chime.
  (b) When the platform reaches point 1, `not_cover_pos` drops `and_four2`, `not_final` re-arms the plates, and
  `and_four1` (all plates down) fires again. Whether `plainsound.active 0` stops the sound is also open.
- That `default_A:1` loads into cmp1's input_A value (+0x14C): the string has no code xref. It is harmless either way:
  with A = 0 the reset would fire on the plate's way up instead.
- Whether a save/load restores flip-flop bits and mover points was not read.
