# Double jump (air boost) at 175 Hz

The double-jump failure is frame-rate dependent. The boost is armed only while
`+0xE14 < +0xE10`. Any floor contact sets `+0xE14 = +0xE10` (contact callback, vt+0x168), which disarms it.
On the frame after a ground jump, the ground sweep (vt+0x208) still reaches the floor if the player rose less than
about 3 units during the jump frame. At 591 u/s, that happens when the jump frame's dt is **5 ms or less**. The next
frame then reports `gnd=1` and fires the callback, and the boost is gone for that jump.
- 175 Hz on whole-ms timeGetTime gives 5 ms frames about 34 % of the time. In the measured log, **4 of 5** ground
  jumps on a 5 ms frame were disarmed, versus **0 of 9** on 6-7 ms frames.
- **3 of the 6 failed air presses** are this case. The others are by design: a second press, a ceiling bump, and a
  non-air state.
- At 60 fps the jump frame rises about 9.5 units, so this can never happen. Uncapped (hundreds of fps), it happens on
  every jump. That matches Zoom's "fixed double-jumping for high framerates", which was a frame cap.

The menu's Settings > FRAME RATE > Fix double jump (default on, `jumpfix.cpp`) undoes the takeoff disarm; it was
verified in game (5 takeoff resets, 5 restores).

Method: D = decompile, A = disassembly, X = xref, L = the panel's log (`nukepanel.log`, gl renderer, about 175 fps,
1946 `jf` lines). duke_base base 0x10000000. Ghidra function: `playerstate_move_update` at 0x100855d0.

## 1. The decision (`playerstate_move_update` duke!0x100855d0)

- **Edge** (A 0x10085aef..0x10085afd): `E00 & 4` and not (`E04 & 4`). `E04` is copied at entry (A 0x1008563e).
  - Only one edge is needed per press. A press is seen if it is held across one tick (a 5-7 ms frame), far shorter
    than any keypress.
  - L: the engine's `E04` matched our previous-frame `E00` on every contiguous frame (0 mismatches). Every air press
    reached the air branch, since it stamped `E14`.
- **Grounded** (`+0xB00 != 0`) -> `player_try_ground_jump` 0x10088210 (D). It stamps `E10 = now` and
  `E70 = y` (the jump start), and applies the `g_p_jump` impulse.
- **Airborne** (A 0x10085b2a..0x10085d32), three gates:
  1. `now(double world+0x40) - (float)E10 > 0.2f` (constant 0x10126cf8 = 0.2f, A 0x10085b30..b44).
  2. `E14 < E10` (A 0x10085b4a..b5b, TEST AH,5 / JP), the "armed" test.
  3. `y >= E70 - 50` (constant 0x10126a74 = 50.0f, A 0x10085b61..b78).
- If all three pass, the boost runs (D):
  1. An impulse of -vy.
  2. Either `sqrt(...)` when `|vy| <= g_p_boost_sweet` (or E64 > 0 and vy < 0), or else `g_p_boost`.
  3. Sound and animation 1.
- **`E14 = now` is stored on every air edge, pass or fail** (A 0x10085d29..d32 is the target of all three gate
  failures). A refused press therefore also uses up the jump.
- **dt enters only through `now`.** The gates are time- and position-based, with no frame counts.

## 2. What disarms it: contact sets E14 = E10

- `FUN_1007a960` = player **vt+0x168**, the contact callback (X: vtable slot 0x10136c10, base 0x10136aa8; D).
  - It sets `E14 = E10` when the contacted entity passes the player's tmatcher `+0x244` (A 0x1007ac11..ac17).
  - Other writers of `+0xE14` are only the ctor and reset (0x10076709, 0x10077dcf) and the air store above (X).
- `FUN_10090bd0` (D) is the ground check. The player's override is **vt+0x208** = `FUN_1007d950` -> `FUN_1008da60`
  -> it (X: slot 0x10136cb0).
  - It clears `+0xB00`, then sweeps a box (half-height 0.1) from `centre.y - 0.1` down to `feet.y + 0.1 - 3.12`,
    using the same tmatcher `+0x244`.
  - On a hit it sets `+0xB00 = 1` **and calls vt+0x168** with the hit, which is the disarm.
  - The reach is about 3.0-3.1 units below the feet. The decompiled constants are literal. (Note: timing.md's
    "10-unit probe" is `FUN_10085dd0`, which only picks the float/fall state when `+0xB00 == 0`.)
- Collision response also calls vt+0x168. L: f22293 was disarmed in the jump frame itself, and f30365 was disarmed
  by a ceiling bump (vy 124.8 -> 0).
- **Frame-rate dependence.** The rise in the jump frame is about `0.2955 + 591*(dt - 1 ms)` (1 ms impulse, then
  coasting; timing.md section 2):
  - 2.66 u at dt = 5 ms, which is inside the reach, so the boost is disarmed;
  - 3.25 u at 6 ms, which is clear;
  - about 9.5 u at 16-17 ms.
  - L confirms the next frame's dy = 591*dt at gnd=1 (no gravity while grounded).
  - The rise scales with `g_p_jump`, so a higher jump cvar shrinks the window.
- **Open:** where vt+0x208 is called in the frame. No direct `CALL [reg+0x208]` reaches the player (X), so it is
  likely in code Ghidra has not disassembled.
  - Inferred from L: it runs before that frame's movement, because gnd=1 is reported on a frame whose logged
    (post-move) y is 6 u up.
  - Settle with an execute breakpoint on 0x1007d950 and a stack trace.

## 3. Ground-jump frames (L, st 5, gnd=1 at the jump)

| jump frame | dt (ms) | next frame gnd | E14 := E10 next frame | later air press |
|---|---|---|---|---|
| f9577 | 6 | 0 | no | f9665 boosted |
| f11008 | 6 | 0 | no | f11061 boosted |
| f13042 | **5** | **1** | **yes** | none |
| f16904 | **5** | **1** | **yes** | f16948 refused |
| f17717 | **5** | 0 | no | none (exception: mask 0x25, other floor spot; resting gap unknown) |
| f17920 | **5** | **1** | **yes** | f17974 refused |
| f18867 | 6 | 0 | no | (contact at f18877, state 15) |
| f21484 | **5** | **1** | **yes** | f21560 refused |
| f22293 | 6 | 0 | in the jump frame (collision) | none |
| f28965 | 6 | 0 | no | f29024 boosted |
| f30319 | 7 | 0 | no | f30385 refused (ceiling, §4) |
| f31363 | 6 | 0 | no | f31429 boosted |
| f31686 | 6 | 0 | no | f31758 boosted |

The dt histogram over all jf frames: 5 ms 34 %, 6 ms 56 %, 7 ms 7 %, ≤4 ms 2 %.

## 4. Every air press (L, `gnd=0`, EDGE without GROUND)

| press | since E10 (s) | armed before (E14<E10) | y vs start-50 | result | refused by |
|---|---|---|---|---|---|
| f9665 | 0.513 | yes | ok | boost (vy 373) | -- |
| f11061 | 0.304 | yes | ok | boost (+339.6) | -- |
| f16948 | 0.251 | **no** (E14=E10 since f16905) | ok | none (vy keeps -10.8/frame) | gate 2: takeoff contact, 5 ms jump frame |
| f17974 | 0.309 | **no** (since f17921) | ok | none | gate 2: takeoff contact, 5 ms jump frame |
| f18961 | 0.536 | no | below (y -176, start about -80) | none, E14 not stamped | not the air branch: st 15. The jump block is skipped when +0xC1C is 10/15/16 (D), which fits the missing E14 stamp; +0xC1C itself is not logged. The press at f19027 jumped from this state |
| f21560 | 0.434 | **no** (since f21485) | ok | none (vy -181) | gate 2: takeoff contact, 5 ms jump frame |
| f21616 | 0.753 | no (E14 = f21560's stamp) | ok | none | gate 2: second press of the jump (by design) |
| f29024 | 0.337 | yes | ok | boost (+399.0) | -- |
| f30385 | 0.376 | no (since f30365) | ok | none (vy -207) | gate 2: ceiling contact at f30365 (by design, fps-independent) |
| f31429 | 0.376 | yes | ok | boost (vy 398) | -- |
| f31758 | 0.411 | yes | ok | boost (vy 415) | -- |

Gate 1 (0.2 s) refused nothing in this log.

**Counter note:** the old `air_boosts` / ` BOOST` count was every non-reset E14 change. Because of the unconditional
store in §1, that includes refused presses: the log's "10 air boosts" is 5 real boosts plus 5 refused stamps. The
`jf` line now separates them (section 6).

## 5. Fix levers (least intrusive first)

Lever 1 is the shipped fix (`jumpfix.cpp`). The `jf` line carries `e70`, `armed`, `BOOST`/
`REFUSED(gate1|gate2|gate3|state|?)`, `TAKEOFFRESET` and `RESTORED`, and the 5 s line counts real boosts and refused
air presses apart (section 6).

1. **Undo the takeoff disarm** (writes only `+0xE14`, which the engine itself writes). Once per frame after the
   world tick, the condition is:
   - E14 changed this frame to equal E10;
   - `t - E10 <= 0.02 s` (a few frames after the jump);
   - vy > 0;
   - the previous E14 was below E10.

   When it holds, write the previous E14 back. This gives exactly the 60 fps behaviour.
   - There is 200 ms of slack before gate 1 can admit a press, so the timing is not critical.
   - A ceiling bump (vy = 0) and a real landing (vy <= 0) stay disarmed.
2. **Frame cap so that no jump frame is ≤ 5 ms**: about 144 fps or lower with even pacing, or 87.5 (175/2) for even
   vsync pacing.
   - Whether our capper ever emits a short catch-up frame is unknown.
   - It gives up 175 Hz.
3. Not the cause, so not useful here: holding the input edge (every edge was seen), or a min-dt clamp.

## 6. Measurement that confirms the fix

The jf line carries `E70` and an `ARMED` flag (E14 < E10), and tags each edge:
- `BOOST` when vy rises by more than 100 on the edge frame;
- `REFUSED(gate)` otherwise;
- `TAKEOFFRESET` / `RESTORED` events.

Procedure: 20 or more standing jumps with one air press 0.25-0.5 s after each, at 175 Hz.

Pass criteria (met in game):
- every jump frame of 5 ms that shows `TAKEOFFRESET` is followed by `RESTORED`;
- `REFUSED(gate 2)` occurs only after a second press or a mid-air contact;
- the boosts equal the first air presses.

Control without the fix, at a 120 fps cap: zero `TAKEOFFRESET`.
