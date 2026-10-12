# Explodables: which classes, which list, when one is broken

Explodable walls are the `breakable_adv` class (112 placed, each a `brk_*` mesh folder). They live in the `t=4`
search list (the breakable base ctor sets `+0x48` bit 4). One is broken once `+0x2E0` is 0, and takes damage only
while `+0xC44` (its `active` pin's value) is nonzero. The ESP hides both kinds.

Method: Ghidra MCP and the map data. Evidence tags as [secrets-walk.md](secrets-walk.md): [D] decompile,
[A] disassembly, [S] map data (world.scr in `duke\base\db_*.zipfs`, read with Python zipfile). Addresses are
duke_base.dll. Functions named in Ghidra: `breakable_ctor` 0x100f11e0, `breakable_break` 0x100f1600, `breakable_reset` 0x100f16d0,
`breakable_adv_ctor` 0x100ba670, `breakable_adv_break` 0x100bab50, `breakable_adv_damage` 0x100bab30.

## 1. Classes [S]

Census of every world.scr (132 files, each counted once):

| class | placed | what it is |
|---|---|---|
| `breakable_adv` | 112 | `path:"maps/<map>/brk_*"` mesh walls and boxes (`brk_explodbox`, `brk_door1`, `brk_air1`...); `connect broken:` drives effects and switches. 31 have `start_active:false`, 23 `damage_type:1` |
| `breakable_obstacle` | 5 | 2 in `demo_obstacle`, 3 in e01p03_a02 with `respawn_time:10` and `break_on_touch:true`: respawning track obstacles, **not marked** |
| `barrel` / `crate` | 241 / 254 | dobj props (`barrel_type`, `crate_type`, `contents`), not walls; not explodables, but one holding a nuke or item is marked as it ([pickups.md](pickups.md) section 5) |
| `obstacle` | 45 | `manager_name` track obstacles with `health`/`damage`; **not marked** |

## 2. The list [D/A]

`breakable_ctor` 0x100f11e0 (vtable 0x10146290) reads `+0x48`, ORs 4 and writes it back (0x100f129f-0x100f12a9),
sets health `+0xB0C` = 1.0, registers the pins `break` (+0xB10) and `broken` (+0xB78), and sets `+0x2E0` = 1.
`breakable_adv_ctor` 0x100ba670 calls it, then ORs `+0x48` with 0x200000 and registers `active` (+0xBEC). So a
`breakable_adv` is in the bit-2 list: `spawn_search("s=%d?t=4")`. The same list holds every class built by
0x100fe850 (the only other `OR [reg+0x48],4`, a "running"/"active" mover), so the walk classifies by name as the
others do. It runs only while the Explodables row is on.

## 3. Broken and damageable [D]

- `breakable_adv_damage` 0x100bab30 (vt+0x170, aim.md section 3): only while `[+0xC44] != 0`, health `+0xB0C` drops by
  the damage; below 0 it `net_send`s message 1.
- `breakable_adv_break` 0x100bab50 first calls `breakable_break` 0x100f1600: it sends the `broken` pin (unless
  `+0xBD0`) and **clears `+0x2E0`**. `breakable_reset` 0x100f16d0 restores health and sets `+0x2E0` = 1 again.
- `+0xC44` is the `active` pin's value: `breakable_adv_ctor` builds a
  `pin_t` at +0xBEC and registers it as "active", and a pin's value sits at pin+0x58 (cutaway.md: +0xB1C/+0xB74).
  `start_active:false` walls (31 placed) therefore start at 0.
- The ESP hides an explodable while `+0x2E0 == 0` (broken) or `+0xC44 == 0` (not damageable yet; the row hint says
  so); both are watched in the log (`watch: Explodable ... +2E0`/`+C44`) so a
  level shows when each moves. Switching the row off drops those watches with one line.

## Open questions

1. Whether `+0x18C` (the marker point) sits on the visible mesh: the world.scr `position` is the entity origin and
   the mesh is loaded from `path`. Settle by the `watch: Explodable ... new at (x,y,z)` lines against the screen.
   The ESP row has not been checked in game.
2. Whether nothing rewrites `+0x48` between the ctor and registration (the same assumption as secrets-walk.md section 3).
