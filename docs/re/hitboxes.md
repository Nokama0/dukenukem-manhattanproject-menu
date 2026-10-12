# Hitboxes: pickup and hit volumes

Every gameplay collision is one engine box per object: **half-extents `+0x2B8`, centred on the world
position (`vt+0x160`), shifted down by `+0x2C4`, and axis-aligned in world space** (translation only, never rotated by
`+0xF4`/`+0x1A8`). Pickups fire on physics contact (`touch`, vt+0x168) between Duke's box and the powerup's box.
Bullets are a one-shot hitscan ray, tested against the same box. For ordinary monsters that box is the whole hit
test. Four boss classes add a per-triangle ray test against the posed Granny mesh, and a ray must pass both tests.
The menu's ESP draws an 8-corner box from `+0x18C`/`+0x2B8`/`+0x2C4`: it is exact for pickups and for all `monst_*`.

Tags: D decompile, A disassembly, X xref/vtable scan, S string. Base prism3d 0x400000, DLLs 0x10000000. Ghidra
names: prism3d `phys_move_object`, `phys_sweep_vs_objects`, `phys_clip_segment_vs_object`, `phys_gather_candidates`,
`phys_ray_vs_objects`, `phys_push_query`. duke `actor_ray_collide_aabb_then_skmod`, `actor_exact_collision_skmod`,
`dobj_clip_vector_obb_if_a15`, `object_set_dimensions`, `actor_set_height_ratio`, `actor_dims_from_skmod`,
`proj_bullet_hitscan_ray`, `powerup_touch`, `powerup_base_ctor`, `babe_activate`, `babe_handler`. Decompile dumps:
`dumps/hitbox_*.decomp.c` in the Ghidra project.

## 0. The box (all four questions rest on this)

- object_e vtable (p3shared ctor 0x100178a0, vt 0x100351d8, X) gives these slots: +0x11C ray_collide, +0x124 clip_vector,
  +0x128 clip_point, +0x130 exact_collision, +0x134 bv_collision, +0x138 build_collision_data, +0x140 compute_bbox,
  +0x144 link_clist, **+0x168 touch(colldesc_t*)** (empty stub 0x1002e2c0; export name `object_e::touch`).
- `object_e::build_collision_data` p3shared!0x100181d0 (D): the collision data holds 6 planes with unit normals ±X/±Y/±Z,
  which is an axis box. `object_e::ray_collide` p3shared!0x10018630 (D) moves the ray into object space by **subtracting
  the world position only** (`create_translation_matrix`), then clips it against extents
  `(dim.x, dim.y - |o|, dim.z)` with the ray's y shifted by `+o`, where `dim = +0x2B8` and `o = +0x2C4`.
  `object_e::clip_vector` 0x10018870 (D) uses the same convention for swept boxes. It adds the mover's own extents
  (a Minkowski sum).
- **World box** = x `pos.x ± dim.x`, z `pos.z ± dim.z`, y centre `pos.y - o`, half-height `dim.y - |o|`.
  With o >= 0 that is y from `pos.y - dim.y` to `pos.y + dim.y - 2o`. `pos` is the vt+0x160 world position, which equals
  +0x18C for top-level entities ([entities.md](entities.md)).
- `+0x2C4` is a height offset: `actor_set_height_ratio` duke!0x1008fc90 (vt+0x1EC, D) stores r in [-1, 1] at +0xAFC and
  sets `+0x2C4 = (1-r)*dim.y` (or `-(1+r)*dim.y` when r < 0). The default r is 1, so o = 0 (`actor_model_ctor`
  0x1008f170 writes +0xAFC = 1.0, D). Monster AI calls vt+0x1EC from about 40 sites, for example the def loader
  0x1002c170 (X), so **read +0x2C4 every frame**.
- `+0x2B8` is written only by `object_set_dimensions` duke!0x1008b300 (vt+0x188, D), which rebuilds the planes, the bbox
  and the sphere. Sources: class constructors, monster def key "dimensions" (0x1002cd56 S), and the editor property
  `dimensions` (p3shared `object_e::evd_dimensions`, offset 0x2B8, S/D). **Do not use `+0x2F0` bbox_t**: it is ±dim
  with no `+0x2C4` shift (compute_bbox p3shared!0x100182a0), and the collision code never reads it.
- Collidable gate: the broadphase `phys_gather_candidates` prism3d!0x412480 (D) skips any object whose byte `+0x2E0` is 0,
  then culls by bounding sphere (+0x4A0 centre, +0x4AC radius). `powerup_on_picked` 0x1005d950 clears +0x2E0 (D).
  **ESP: skip objects with +0x2E0 == 0.**

## 1. Pickup volume (powerups)

- Path: `phys_move_object` prism3d!0x410290 → `phys_sweep_vs_objects` 0x40eb00 sweeps the mover's box (its own
  +0x2B8) from the old to the new position. For each candidate it calls `phys_clip_segment_vs_object` 0x40e8d0 →
  candidate vt+0x124 clip_vector. Every hit, blocking or not, goes into the collision history, and **both sides then get
  vt+0x168 touch** (0x4109ee / 0x410a25, D).
- `powerup_touch` duke!0x1005d920 is vt+0x168 of all 8 powerup classes (X scan). If the other object has type bit 2
  and user bit 0x100 (the player), it calls vt+0x24C = the pickup (D).
- **Pickups use no sphere, radius or separate trigger.** The pickup fires when Duke's box and the powerup's box overlap
  along Duke's movement that frame. The powerup's own volume is the §0 box. Powerups never set +0xA15, so it is
  axis-aligned. `+0x1CC` = (0, 150, 0) (powerup_base_ctor, D) is possibly a spin rate (unverified); the box ignores it
  either way.
- Default half-extents (constructor immediates before `object_set_dimensions`, A): base `powerup_base_ctor` 0x1005cff0
  = 16 (so nuke 0x1005f9d0 and key 0x1005f660 = 16, since they never call set_dimensions). ammo, ego, life = 24.
  gun, jetpack (0x1005f330), forcefield (0x1005ebc0) = 32. Level files can override these, so read +0x2B8.
- **"Pick-up-able region" for Duke's centre** = the powerup box grown by Duke's half-extents (the Minkowski sum from
  clip_vector). Drawing the powerup's own box is the faithful per-item volume.

## 2. Enemy hit volume

- **Bullets are hitscan.** `proj_bullet_hitscan_ray` duke!0x1003a9c0 (bullet vt+0x5C think via 0x1003a990, D) runs once
  (flag +0xBEC). It casts `phys_ray_test(world, spawn pos, spawn pos + dir(+0x1C0)×range, +0x244, +0x220)`, reads every
  entity the ray crossed from `phys_collide_history`, sorts by distance, keeps up to 3, and damages them in order
  (FUN_1003ad90). Bullet touch 0x10039cb0 only places decals.
  **Correction to [entities.md](entities.md) §5:** 0x1003c2e0/0x1003c694 belong to `actor_proj_fm_bolt` (touch at vt+0x168
  0x1003c610, X), not the bullet.
- `phys_ray_vs_objects` prism3d!0x40f1b0 calls each candidate's vt+0x11C (D). For every duke actor class (monsters,
  bosses, projectiles, pickups) that slot is `actor_ray_collide_aabb_then_skmod` duke!0x1008fff0 (X, A):
  1. `object_e::ray_collide`, the §0 box.
  2. Only if (**box hit OR byte +0xAF3**) AND skmod handle `+0xAE4` != 0 AND byte **`+0xAF2`** != 0: transform the ray by
     the inverse world matrix `+0x134`, then `skmod_ray_collide(model, from, to, &hit, &normal)` (pfn 0x10183eb0).
     That runs Granny `LockSequenceForRayIntersection`/`GetNextIntersection` on the **posed mesh triangles**
     (skmod!0x100031f0, D). The result replaces the box result, so a hit needs both tests.
- Other projectiles (rockets, bolts: physics movers with user bit 0x400, +0x2E0 = +0x2E1 = 1, ctor 0x10038c90 D) hit
  in a box-vs-box sweep. If the victim has +0x2E1 set, `actor_exact_collision_skmod` 0x10090120 then applies the same
  mesh test, but only for projectile movers (D).
- Who sets `+0xAF2` (all writers, A scan): the init functions of boss_mechnukem (0x100011c0), boss_porkchopper and
  children (0x10006e30), boss_queenroach (0x10012aa0), and boss_wozma (0x10017330, stores BL: 0 or 1, open).
  `actor_model_ctor` zeroes it. **No `monst_*` class sets it, so for every ordinary enemy the §0 box is the complete,
  exact hit volume.** `+0xAF3` (skip the box gate) is written only by 0x10015e50 (class unknown).
- **ESP rule:** box = §0 box for all enemies, read each frame (+0x18C, +0x2B8, +0x2C4; 3 floats + 1, one
  guarded read of the 0x2C8-byte prefix). For the four mesh-tested bosses the box is the outer gate: a hit is always
  inside it, but not every point inside it hits. A tighter per-frame shape there is `skmod_calc_dimensions(+0xAE4,
  &min, &max)` (skmod!0x100039b0: AABB of the current rendered vertices in model space), with its 8 corners transformed
  by `+0xF4`. Its cost (it locks Granny rendering state) and whether it returns the posed or the bind mesh are open.
  Per-bone boxes do not exist in the hit path: no bone or part collision data is read (D, both functions).

## 3. Axis-aligned or rotating

- **Axis-aligned in world space** for rays always (ray_collide never reads a rotation, D) and for sweeps unless byte
  **`+0xA15`** is set. `dobj_clip_vector_obb_if_a15` duke!0x1008b660 (vt+0x124, D) then rotates the segment by
  `axial_t::rotate(+0x1A8)` into an oriented box. Only boss_mechnukem (0x100011c0) and boss_mechnukem_head (0x10001dc0)
  set +0xA15 (all writers, A). A projected 8-corner AABB is therefore exact for everything else. For those two, rotate
  the box by +0x1A8 when drawing their projectile-sweep volume (their bullet volume is still the AABB).

## 4. Nukes, keycards, babes

- `nuke` and `key` are powerups: same `powerup_touch` at vt+0x168, same base ctor and filters, half-extents 16, same box
  (X scan, A). Keycards and nukes need no special case.
- `babe` (activated_item family, ctor 0x10060280) has half-extents **(20, 56, 20)** (A). Post-init 0x10068050 drops it
  onto the floor (`phys_bound_collide`) so the box bottom rests there, which confirms pos = box centre (D). Its vt+0x168
  is the empty `object_e::touch` (thunk 0x100ebc08 → import `touch`, X), so **bumping into a babe does not rescue her**.
  Rescue is `babe_activate` duke!0x10060ac0 (vt+0x204, D): if the activator is the player it grants powerups
  (types 5 and 1), sets state +0xE10 = 1, and `babe_handler` 0x10060800 then plays "rescue". What calls
  babe vt+0x204 is unknown (a use key, an area_switch, or an "activate" pin; no caller was found by scanning
  `CALL [reg+0x204]`).

## Open questions

1. Babe rescue trigger volume: the caller of 0x10060ac0 (breakpoint at 0x10060ac0 recording the return address during one rescue).
2. boss_wozma +0xAF2 value, and the class that sets +0xAF3: read both bytes at runtime on those bosses.
3. skmod_calc_dimensions cost and posed-vs-bind: time one call and compare min/max across two animation frames.
4. Whether dead monsters clear +0x2E0: read it on a corpse (decides whether corpses still stop bullets).
