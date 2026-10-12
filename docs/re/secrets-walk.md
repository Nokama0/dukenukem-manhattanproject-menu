# Secrets walk: why `t=2` never returns `area_secret`

`t=` selects a per-engine-type list, and `area_secret` is never linked into the `t=2` list, because engine type bit
0x2 (`+0x48 & 2`) is set only by duke's `dobj_base_ctor` and the area chain never calls it. Areas live in the `t=0x20`
list (area_e bit). The menu therefore runs a second search `"s=%d?t=%d", sid, 0x20` with the same class-name
classification; the `t=2` walk and its counts are unchanged. Verified in game: secrets are counted and marked.

Method: Ghidra MCP; evidence tags [D] decompile read, [A] disassembly/instruction search, [X] xref/string. Other
terms follow [entities.md](entities.md) section 1.

## 1. The query parser and iterator (prism3d)

`spawn_search(fmt, ...)` prism3d!0x4222b0 vsnprintf's the format and calls **`spawn_search_parse`** prism3d!0x40cf30
(named in Ghidra; was FUN_0040cf30) on the spawner `DAT_004a1e4c` [D]. It pushes a 100-byte frame on the global stack
`DAT_00493060` and splits the string on `?`. Each tag is `<letter>=<value>` (value read from offset 2) [D]:

| tag | effect (frame field) | notes |
|---|---|---|
| `s=` | server = `spawner_find_server(sid)` prism3d!0x40ca70: walk spawner+0xDC, match server+0xD4 == sid, next at +0xE0 | a missing sid gives server 0 |
| `t=` | type mask (d48), "has type" flag (d41) | picks a **list**, it is not a filter |
| `u=` | entity id (d44), flag (d40) | iterator: `ent+0x74 == u` |
| `y` `Y` `g` `G` | user mask (d4c), mode 0/1/2/3 (d54), flag (d42) | iterator: `match_type_user(mask, mode)` |
| `n=` | name (d58, 63 chars) + `v_strhash` (d98), flag (d43) | iterator: `ent+0x44 == hash` and `strcmp(ent+4)` |
| other | `vprocess_t::error("<spawner> illegal search tag ...")` | |

**List choice** (end of 0x40cf30) [D]: with no `t=`, the list is `server+0x364` (all registered entities).
With `t=`, `i` = index of the **lowest set bit** of the mask and the list is `server+0xE4 + i*0x14`. Only one list
is walked, so `t=0x22` would walk the bit-1 list only. There is no OR of lists.

**match_type_user** p3shared!0x1000e250 on `+0x4C` [D]: mode 0 (`y`) = `u != 0 && (u & m) == u`; mode 1 (`Y`) =
`u != 0 && (u & m) == m` (all bits of m present); mode 2 (`g`) = `(u & m) != 0`; mode 3 (`G`) = `(u & m) == 0`.

**Iterator** `spawn_search_next` prism3d!0x40d170 (was FUN_0040d170) [D]: node = node->+4, entity =
node->+8. It returns the entity when `vt+0xBC` is true and each enabled filter passes (u, then user mask, then name).
For both area_secret and monst_rat, `vt+0xBC` is the thunk duke!0x100ebb1e, which jumps through `[0x101260cc]` =
**`entity_t::alive`** p3shared!0x1000d830. That returns `+0x90 == 0 && *(+0xE0)+0x18 == 0` [D/X]. So the
alive() check does not separate areas from monsters.

## 2. How the lists are filled: srv_link_entity_lists

The server object's ctor is prism3d!0x41b110, vtable 0x4347E0. It clears 32 list heads of 0x14 bytes at +0xE4, plus
+0x364 (all entities) and +0x3C4 (root entities) [D]. `srv_entity_register` 0x420b30 calls server vt+0x2C
(0x41b510) and then vt+0x30 (0x41b570) [D]:
- 0x41b510 assigns the entity id at `+0x74` from the id_sys at server+0xD8. It is set only when `+0x84 > 1 &&
  +0x88 > 1`; otherwise it is -1 [D].
- 0x41b570 locks the server, then writes `ent+0xE0 = server` and `ent+0x78 = server+0xD4` (this is the **sid**). It
  then calls server vt+0x50 = **`srv_link_entity_lists`** prism3d!0x41b320, then ent vt+0xA8 [D].
- `srv_link_entity_lists` [D, disassembly 0x41b320-0x41b39b]: when `ent+0xDC == 0` the entity is appended to
  +0x3C4. It is always appended to +0x364. Then, **for each bit i (0..31) set in `ent+0x48`**, it is inserted into
  list `+0xE4 + i*0x14`, except for bit 0 when `parent_type(0x40)` holds (`+0x48 & 0x40`).
  `srv_unlink_entity_lists` 0x41b3a0 (vt+0x54) mirrors it on removal.

So `t=N` means "entities whose `+0x48` held bit lowest(N) **when they were registered**".

## 3. Why area_secret is not in the t=2 list

The engine type word `+0x48` gets these bits from constructors. Source: an instruction search for `OR [reg+0x48],imm`
over both DLLs [A], and each ctor decompiled [D]:

| bit | set by |
|---|---|
| 0x1 | `object_e::object_e` p3shared!0x100178a0 (`+0x48 |= 1`) |
| **0x2** | **only** `dobj_base_ctor` duke!0x1008b090 (`or [esi+0x48],2` at duke!0x1008b0ee; it calls object_e first) |
| 0x20 | `area_e::area_e` p3shared!0x100013c0 (0x100013c8) |
| 0x100 | `world_e::world_e` p3shared!0x1002e140 (0x1002e148) |
| others | logic_e 0x10, texture_e 0x80, spawnpoint_e 0x200, startpos_e 0x400, camera_e 0x1000, particle_texture_e 0x2000, decal_e 0x4000, force_e 0x40000; duke: 0x4 (0x100fe8fd, and `breakable_ctor` 0x100f12a9 in register form; see [explodables.md](explodables.md) section 2), 0x800 (0x100ec0cc), 0x10000 (0x10113a79), 0x200000 (11 ctors) |

The area_secret ctor chain was walked for every call and every `+0x48`/`+0x4C` access [D/A]:
factory 0x100dbe10 → 0x1009bd30 (`+0x4C |= 8`) → 0x100997f0 → 0x10099240 (`+0x4C |= 2`) → 0x10091640 →
`area_e::area_e` → `world_e` → `object_e` → `entity_t` → `entity_base_t` (zeroes +0x48/+0x4C). None of these calls
`dobj_base_ctor`, and none writes bit 0x2 into +0x48.
- **area_secret `+0x48` = 0x121** (object 1 | area 0x20 | world 0x100); **`+0x4C` = 0x0A**.
- Lists it is linked into: bit 0 (+0xE4), **bit 5 (+0x148, `t=0x20`)** and bit 8 (+0x1E4, `t=0x100`). Bit 1
  (+0xF8, `t=2`) is not among them.
- The other walk exclusions do not apply. The `s=` sid is the same server: register writes +0x78 from the server,
  and the game queries areas with the player's +0x78, see section 4. `vt+0xBC` is `entity_t::alive` for both
  classes. `u=` is not used.

**So the list alone decides it.** Monsters, items, nukes, keys and babes come from `dobj_base_ctor` (bit 0x2).
Areas, and anything else built straight on object_e/world_e, never appear under `t=2`.
`t=2` does not mean "all object_e in that world" (as entities.md section 1 once read): it means duke dynamic objects
(dobj). `t=1` is nearest to "all object_e". Before the second search, secrets read 0 on every map, which matches.
Whether anything rewrites `+0x48` between the ctor and registration is open: the OR-immediate search found no such
write, but register-based writes were not scanned exhaustively.

## 4. How the game itself enumerates areas

The game walks areas with `t=0x20`, never `t=2`. From the push arguments at all 113 spawn_search format-string
sites [A, a dump of the call sites kept in the Ghidra project's dumps folder]:
- duke!0x1007d1e0 (player method, not a Ghidra function; called from 0x1007aef3 with 2, 0x1007d0a2 with 4,
  and `player_teleport` 0x1007d8aa) [A]:
  `spawn_search("s=%d?t=%d?Y=%d", player+0x78, 0x20, arg)`. For each hit, while `player+0xE0` is unchanged, if
  `area+0xAE4 != 0` it calls `area->vt[0x1BC]` with the player. With arg 2 it visits every area whose +0x4C has bit 2,
  which includes area_secret (0x0A). The same armed `+0xAE4` gate appears in area_secret_on_enter 0x1009be40.
- duke!0x1008e138: `"s=%d?t=%d?Y=%d", ent+0x78, 0x20, 1`. duke!0x1007e71e / 0x1007ecee / 0x10076dbf / 0x1007ef1c /
  0x1007ee4d and duke!0x10083ae4 / 0x10087cc7 use `t=0x20` with `g=1/0x40/0x80` and `u=` [A].
- Nothing counts area_secret entities. The cheat "secret" (strcmp at duke!0x1007dca0) only does
  `PS+0x4C += n` and prints "%d secrets given. (cheater!)" [A]. The level-stats code near duke!0x100a90ca
  ("@@i_secrets@@", "secretcount") and FUN_100eb540 read a **`secretcount` value per episode part from
  episodes.def** (`def_obj_t::get_real(name, part)`) [D/X]. So the game's own total is a def value, not an
  enumeration. The menu does read the def totals (levels-and-stats.md section 2); the exact key and index mapping in
  FUN_100eb540 / the table at 0x10178548 were not traced.

## 5. The enumeration the menu uses

The `t=2` walk is unchanged. After `spawn_search_stop()`, a second, balanced search runs:

```
spawn_search("s=%d?t=%d", sid, 0x20);     // area_e list (server+0x148): every area, area_secret included
while ((e = spawn_search_get()) != NULL) { /* same guards, vt[1] class name, class_lookup */ }
spawn_search_stop();
```

- **No overlap and no change to the other categories** [A]. Entities in the t=2 list carry +0x48 bit 2
  (dobj_base_ctor, derived from object_e), and entities in the t=0x20 list carry bit 0x20 (area_e, derived from
  world_e). These are disjoint constructor chains, so no entity is counted twice. The t=2 results (enemies, bosses,
  items, keys, nukes, babes, `alive`, `inactive`) do not change. Only `total`, `other` (non-secret areas: switch,
  ladder, damage, hide, cold, force, ice, resist, flashlight, funk, mutate, waypoint, teleport, rain) and `secrets`
  grow.
- Every guard in `walk()` holds for areas: vt/vt[1] are in duke (area_secret vt 0x10139C50), +0x90 is the killed
  flag, area_secret is 0x1078 bytes (>= kObjSpan 0x2C8 and kSecretSpan 0xAE8), and position +0x18C is a world_e
  field. `+0x4C & kUserMonster (0x20)` is 0 for area_secret (0x0A), and other areas are CAT_OTHER, which `continue`
  before that read.
- Share `kMaxWalk` and `s_snap.total` across both searches, so a corrupt list still caps the frame.
  `watch_sweep()` must run only after both walks: until now it never saw a secret.
- Secrets only would be `"s=%d?t=%d?Y=%d", sid, 0x20, 0x8` (mode 1: `+0x4C & 8` must be set; area_secret's own ctor
  0x1009bd30 sets it). Whether any other area class also sets +0x4C bit 8 is open, so the class-name check stays.
- Not used: a single `t=1` walk (all object_e). It would also return the world entity, lights and other
  object_e helpers, and it changes the `other`/`total` counts. `t=0x22` does not OR the two lists (section 1).
- Cost: one more guarded read per area per walk (a map has tens of areas). `walk_us` measured 25-56 us before the
  second search.

## 6. Runtime check

The 5 s line on e01p01_a01 (db_e01.zipfs, which contains `area_secret2` at <-448 -832 128>) shows
`ents: ... secrets=N` with N >= 1 (>= 2 if names are consecutive from 1). `other` grows by the non-secret areas
and `enemies/items/keys/nukes/babes/alive/inactive` stay the same on the same map. `watch: Secret <ptr>` first-seen
lines appear. A separate `areas=%d` counter (every hit of the second search) makes the check exact:
`areas == secrets + (other_after - other_before)`.
If `secrets` is 0 while `areas > 0`, suspect class_lookup ("area_secret", classes.h:65) or the vt[1] getter before
the list. If `areas == 0`, the sid or list theory is wrong: a breakpoint at prism3d!0x41b320 reading `[esi+0x48]`
for an area_secret would settle it.
