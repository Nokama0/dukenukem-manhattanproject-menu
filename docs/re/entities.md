# Game objects and weapons

Every live entity can be enumerated with the engine's own exported query iterator
(`spawn_search` / `spawn_search_get` / `spawn_search_stop`). Each entity names its own class through
vtable slot 1 (`get_classname()`), with no RTTI and no offset tables needed. The local player comes from
`wm_get_input(0)`, or from the query `Y=0x100`. Player stats live in a persistent block at
`*(player+0x111C)`. Every shot, bullets included, is a spawned projectile entity. Damage goes through
entity vtable `+0x198`, and the real health change happens in monster vtable `+0x2FC`.

Binary: duke_base.dll (base 0x10000000), prism3d.exe (0x400000), p3shared.dll (0x10000000). These are identical in
every build. Method: Ghidra MCP. Evidence tags: **[D]** decompile read, **[A]** disassembly, **[X]** xref,
**[S]** string, **[G]** Ghidra script (kept in the Ghidra project's `scripts\` folder). Functions understood are named
in the Ghidra project (names used below).

## 1. Entity model

### Class hierarchy and base layout (p3shared exports, [D])
`entity_base_t` -> `entity_t` -> `object_e` (world objects) -> duke classes. There is no RTTI.

| off | field | evidence |
|---|---|---|
| +0x00 | vtable | ctors p3shared!0x1000de40 / 0x1000d2f0 |
| +0x04 | name, char[] (auto-named `"<class>%d"` when unnamed) | duke_base!spawn_entity_autoname 0x100e9120 sprintf to +4 [D]; spawn_search 'n' compare prism3d!0x40d170 [D] |
| +0x44 | v_strhash(name) | prism3d!0x40d170 compares it before the name [D] |
| +0x48 | engine type bits (bit 0x2 = duke dynamic objects, set only by `dobj_base_ctor`; areas get bit 0x20 from `area_e`, not bit 0x2) | entity_base_t::parent_type p3shared!0x1000e230 [D]; duke code tests `+0x48 & 2` before touching object fields; see [secrets-walk.md](secrets-walk.md) section 3 |
| +0x4C | user type bits: 0x100 player, 0x200 camera, 0x10 actor, 0x20 monster (set in FUN_1002bd80, monster base), 0x400 projectile | match_type_user p3shared!0x1000e250 [D]; actor_player_ctor 0x100763c0 `OR +0x4c,0x100` [D]; get_local_player 0x100e7060 [D] |
| +0x50 | refcount | add_child 0x1000e3c0, wm_input [D] |
| +0x58 | class deleter fn (from the script registry) | ent_spawn prism3d!0x4020b0 [D]; ent_kill 0x4021a0 calls it |
| +0x60 / +0x64 / +0x68 / +0x6C | parent / first child / next sibling / prev sibling | add_child p3shared!0x1000e3c0 [D] |
| +0x74 | entity id (`u=` search key; attacker id in damage messages) | prism3d!0x40d170 [D]; monster_damage_send [D] |
| +0x78 | server id (`s=` search key) | cmd_camera 0x100e48b0, get_local_player [D] |
| +0x90 | killed flag (nonzero = dead or removed) | entity_t::alive p3shared!0x1000d830 [D] |
| +0xE0 | owning world/server object (`param 1` of every srv_*/phys_ray_*) | spawn_entity_autoname(ent+0xe0,...), phys_ray_* call sites [D] |
| +0xF4 | object_e world matrix | object_e::get_world_pos p3shared!0x10017e70 [D] |
| +0x180 | cached world position vec3 (refreshed by get_world_pos) | same [D] |
| +0x18C | local position vec3 (x, y, z; y is up): **relative to the parent +0x60 when there is one** (Attached entities below) | set by map spawn code FUN_1000db90, bullet placement [D] |
| +0x984 / +0x988 / +0x98C / +0x98D | transform stamp (0 from the ctor until the first update) / parent's stamp last seen / local dirty / world-pos cache dirty | object_e ctor p3shared!0x100178a0, update_transforms 0x10017e00 [D] |
| +0x1A8 | orientation vec3 in degrees, fed to `ypr_2_cartes` / `quat_t::set_ypr` | playerstate_get_fire_dir 0x10088400 [D] |
| +0x1C0 | direction or velocity vec3 (projectiles) | weap_geagle_spawn_shot 0x10055623 [A] |
| +0x220, +0x244 | two `tmatcher_t` collision filters (passed to phys_ray_*) | weap_geagle_spawn_shot `deny_ptr(owner)` [A]; FUN_100d3710 [D] |
| +0x2B8 | half-extents vec3 ("dimensions") | object_e::compute_bbox p3shared!0x100182a0 [D] |
| +0x2F0 | bbox_t, local: min = -dim, max = +dim, then `expand` | same [D] |

Virtuals that matter (slot = byte offset into vtable) [G, `VtSlots`, `Slots*` scripts]:
- `+0x04` **get_classname()**: returns `const char*`. Every duke class overrides it. The engine compares these
  pointers for identity (`vt[1]() == PTR_s_actor_proj_pbomb`, in monster_damage_send 0x1002f3f0) [D].
- `+0xBC` alive() (entity_t::alive). `+0x160` get_world_pos() -> `const vec3*` (import thunk to
  object_e::get_world_pos). `+0x15C` update transforms.
- `+0x198` **damage(int type, float amount, vec3* dir, object* inflictor, object* attacker)** (section 6).

### Attached entities (Queen arena train pickups)
+0x18C is local to the parent at +0x60; the world position is the translation of the world matrix at +0xF4. The menu's
walk uses the matrix. The Queen-arena check (below) has not been recorded.
- `object_e::update_transforms` (vt+0x15C) p3shared!0x10017e00 [D]: when the parent's stamp (its own vt+0x15C, i.e.
  recursive) differs from +0x988 or +0x98C is set, `copy_parent_transforms` 0x10017d90 copies the parent's +0xF4 (and
  inverse +0x134) matrices (identity without a parent), then vt+0x158 `update_local_transforms` 0x10017de0 does
  `axial_t::translate(+0xF4, +0x18C)`; +0x984 is incremented. `get_world_pos` (vt+0x160) 0x10017e70 calls it, then
  `mult_zero_matrix` 0x10016aa0: the translation row m[12..14] (+0x124), divided by w (+0x130) unless w is 1.0 or 0.
- The matrix is lazy: refreshed only when something calls vt+0x15C/vt+0x160. The actor draw duke!0x10090260 calls
  vt+0x15C first, and the walk runs after the frame's world render, so a drawn entity's matrix is this frame's. One
  never drawn or asked may hold an older one; one never updated has +0x984 == 0.
- Map data [S]: e04p03_a02's world.scr nests `spawn ammo`/`spawn ego` blocks inside `spawn mover_sub_train_car`
  (e.g. `car2_ego` local (160,56,-96) in `train_car_2` at (9456,160,-16)). The panel's log read +0x18C as exactly
  those local values for the four unmarked pickups, so the nested spawn parents them and keeps +0x18C local.
- The walk (`ent_world_pos`, entities.cpp; rule `att_world_pos`, attachrule.h): no parent -> +0x18C (root, measured
  == vt+0x160, see Measured in game); a parent and +0x984 != 0 -> the matrix translation; +0x984 == 0 or a non-finite
  translation -> +0x18C. No engine call; +0x60 and +0xF4 are inside the walk's kObjSpan and +0x984 in the same
  validated region. The hitbox follows. Log: the `ents:` line's `pos root/matrix/unset/bad=`, and each
  `esp: item ... hidden by` line gives the parent, the source and the local +0x18C.
- **Check:** `map e04p03_a02`, Items on, the Queen fight: the Ego +10/+100, Bullets +50 and Pipebombs
  +10 on the train platforms to Duke's left draw markers that ride the cars; `ents:` shows `matrix=` > 0.

### Class registry (names <-> vtables)
The engine keeps a linked list of script classes (`script_register`, head `DAT_00475b90`, readable through the export
`script_get_list` prism3d!0x4021c0). A record holds: +0 factory, +4 deleter, +8 name[64], +0x48 category[64],
+0x8C next, +0x94 short hash [D script_register 0x4019e0, ent_spawn 0x4020b0]. duke_base registers 217 classes
through `register_entity_class` 0x100d7a70, called from `register_all_entity_classes` 0x100d7aa0.
**Full table (name, category, alloc size, vtable, get_classname string): `dumps\duke_base.classes.txt`**
(script `scripts\ListDukeClasses.java`) [G]. Its categories are the ones the editor uses:
`monsters.eNN`, `powerup`, `object`, `areas.*`, `internal` ...

At runtime, identify an entity with **`((const char*(__thiscall*)(void*))vtbl[1])(ent)`**. That is a pure getter
(`MOV EAX,[classname_ptr]; RET`, e.g. 0x100e02c0 [A]). It is safe to call on any entity and needs no offsets.

### Enumeration recipe (engine registry, all exports)
`spawn_search(fmt, ...)` prism3d!0x4222b0 → FUN_0040cf30 parses `?`-separated tags [D]:
`s=<server id>` (required: picks the world), `t=<engine type bits>` (walks the per-type list at server+0xE4+bit*0x14,
otherwise the all-entities list at server+0x364), `u=<entity id>`, `y|Y|g|G=<user bits>` (match modes 0..3 of
match_type_user), `n=<name>`. `spawn_search_get()` 0x4222f0 returns the **next** match each call (the iterator skips
entities whose alive() is false) and NULL at the end. `spawn_search_stop()` 0x422300 pops the iterator [D 0x40d170,
0x40d160]. The iterator is a global stack (`DAT_00493060`, 100-byte frames), so calls must be balanced and made only on
the game thread.

```
sid = player->+0x78;                       // or any live entity's +0x78
spawn_search("s=%d?t=%d", sid, 2);          // duke dynamic objects only (dobj_base_ctor, bit 0x2);
                                             // areas (incl. area_secret) are t=0x20, see secrets-walk.md
while ((e = spawn_search_get()) != NULL) { cls = vt1(e); pos = vt160(e); ... }
spawn_search_stop();
```
The game queries exactly this way: `get_local_player` 0x100e7060 uses `"s=%d?t=%d?Y=%d"` with (sid, 2, 0x100) [D].
Walking the entity tree (+0x64/+0x68) from the world is an alternative that needs no call, but its root is open
(reading `spawn_get_master_world_list` at runtime would settle it).

- **Still on +0x18C:** `dmgnum.cpp` (where a damage number appears) and `areas.cpp` (live nuke
  positions matched to world.scr) read +0x18C directly, so an attached enemy's number or a nuke nested in a mover
  would be misplaced there. Duke's own position is +0x18C too; the `ents:` line's `playerParented` counts frames he
  had a parent (expected 0; a train or lift would show it).

## 2. The player

- **Find it:** `wm_get_input(0)` (prism3d!0x423fd0). It returns the entity that receives input in window 0. When
  `+0x48&2` and `+0x4C&0x100` hold, that entity is the player. When it is the camera (`+0x4C&0x200`), look the
  player up with `spawn_search("s=%d?t=2?Y=256")` [D get_local_player 0x100e7060]. The console command `camera
  player` sets it (cmd_camera 0x100e48b0 → `wm_input(0,ent)`, and `wm_object(0,…)` = the view object) [D].
  wm slot layout: 0x4A1C70 + 0x18*i: +0 active, +8 view object, +0xC alt object, +0x10 input entity [D wm_*].
- Class `actor_player`: vtable 0x10136AA8, size 0x1120, ctor `actor_player_ctor` 0x100763c0 [D]. Its vt[1] reads a
  non-pointer (0x3d000000), so **match the player by user bit 0x100, not by class name** [G].
- **Persistent stats block PS = `*(player+0x111C)` = `game_globals->buf(+0x10) + 0x223D4`** [D ctor 0x100763c0]:

| PS off | type | meaning | evidence |
|---|---|---|---|
| +0x00 | short | health / "ego", clamped 0..999 | player_add_health 0x10079530 (CE lead +0x79573 `mov [edx],ax`) [D] |
| +0x10 | float | jetpack fuel (timed power 0; state 0xC = jetpack) | player_tick_timed_powerups 0x1007d980; CE +0x7DA7B [D/A] |
| +0x14, +0x18 | float | timed powers 1, 2. +0x18>0 switches the body shader (**forcefield**, CE +0x7DA54 writes `[ecx+edi*4+0x10]`) | same [D]; mapping of 1 vs 2 open |
| +0x38+2*t | short | ammo of type t | player_consume_ammo 0x1007d8c0 (CE +0x7D901) [A] |
| +0x48 | float | nukes collected | player_add_nukes 0x10079b90; cheat "%d nuke(s) provided" [D/S] |
| +0x4C | float | secrets found | area_secret_on_enter 0x1009be40; cheat "secret" at 0x1007dca0 [D/A] |
| +0x50 | float | kills | cheat "kill" at 0x1007dc48 [A] |
| +0x5C | float | nukes already converted into episode bonuses | player_add_nukes [D] |

- `player+0xED8` (byte): nonzero makes `player_consume_ammo` return without consuming (an existing infinite-ammo
  switch) [A 0x1007d8c0].
- Player position is object_e +0x18C (local) or vt+0x160 (world). The view/aim angles are **+0x1A8 vec3**
  (`ypr_2_cartes(out, +0x1A8)` builds the fire direction). +0x1AC is the angle about the vertical axis (yaw) in
  0x10088400. Which component is pitch and the degree convention are open (reading +0x1A8..+0x1B0 while turning would settle it).
- Current weapon: weapons are child entities of the player (weapon +0x60 = owner, weapon_get_owner 0x100510f0).
  Which child is active is open. Candidates are the player's 12-slot arrays at +0xC30/+0xC60/+0xD24…
  Enumerating the player's children with class `actor_weap_*` and seeing which one's +0xB14 time advances would settle it.
- **game_globals** (0xB4 bytes, registered by name through `sys_register_var_ptr("game_globals")`, readable with the
  export `sys_get_var("game_globals",0,0)`) [D DLL_register 0x100de370, game_globals_ctor 0x100e6760]:
  +0x08 session object (+0x548 timedelta, +0x56C flag, +0x5AC HUD-ish object used via vt+0x1C/+0x20),
  +0x0C process, **+0x10 persistent buffer (0x2A87C bytes; PS at +0x223D4; +0x20 episode index)**,
  +0x18/+0x28 per-world player cache (FUN_100e7020), +0x4C bmem_manager, +0x60..+0x80 def pools (0 player.def,
  1 monsters.def, 2 bosses.def, 3 exploders, 4 weapons.def, 5 episodes.def, 6 effects, 7 local, 8 gfx defaults)
  [D game_globals_get_defpool 0x100e6d10].
- CE nukes chain `[0x4A1C80]+48→+6E4→+A30→+60→+64`: 0x4A1C80 is wm slot 0's **input entity** [D wm_get_input].
  But +0x48 on an entity is the type-bit word, not a pointer, so the chain's hops do not map to the layout above
  (open). The menu uses PS+0x48.

## 3. Enemies

- Recognise an enemy by vt[1] class name: `monst_*` / `boss_*` / `mechnukem_crane` (41 classes, categories
  `monsters.e01..e08`, see dump). Monster bases also set user bit 0x20 (FUN_1002bd80) [G]; that this holds for the
  bosses is open. The game ships **no human-readable display name** (no localisation string was found).
  ESP labels should map class names to text in our own table.
- Health (monster base, loaded from the def key "health" in FUN_1002c170 at 0x1002c46f) [A/D]:
  **+0xB84 current health (float), +0xB8C max health (float, same initial value)**, +0xB90 secondary
  "stun" pool (damage types 3/4), +0xB88 health_reward, +0xB7C behaviour flags.
  **Dead ⇔ +0xB84 <= 0** (monster_apply_damage 0x1002f560) [D]; the entity stays in the list until killed (+0x90).
- Drops: **+0xCA0/+0xCA4**, the editor value `spawnscript` (char[64] script paths, count), spawned at the body by
  `monster_drop_all` 0x10035450 at death, then emptied; a nuke.sc entry is a carried nuke ([pickups.md](pickups.md) section 6).
- Bounding box: object_e +0x2B8 half-extents around the world position [D].
- Head point: the engine pattern is `idx = skmod_get_bone_index(model, "Bip01 Head")`,
  `skmod_get_bone_transform(model, idx, float m[16])`, then multiply by the entity world matrix +0xF4. The player does
  this in 0x10088400/0x10088530 and the pistol does it for bone "weapon" in 0x10053e70 [D]. The skmod model handle is
  at **+0xAE4** on actor-derived classes [D]. That monsters keep it at +0xAE4 and use the same Biped bone name is
  open (`skmod_get_bone_index` returning something other than -1 would show it).

## 4. Pickups, objectives, secrets

- Powerups (category `powerup`): `nuke`, `ammo`, `life`, `key`, `jetpack`, `gun`, `forcefield`, `ego` (vtables in the dump).
  Keycard colour is at **key+0xD14: 0 = blue, 1 = yellow, 2 = red** (key_get_name 0x1005f7c0; editor value
  "key_type" is registered at offset 0xD14) [A]. key_hole has its own key_type at +0xDA0 [A 0x1006e64f].
- Picked up: powerup_on_picked 0x1005d950 clears byte +0x2E0 and, when there are no respawn flags
  (+0xCB4&7 == 0), calls vt+0x254 (remove or kill; open); otherwise it starts a respawn timer at +0xCB8 [D].
  A collected item should therefore stop appearing in `spawn_search` (alive() false) or show +0x2E0 == 0.
- Babes (hostages): class `babe` (object, 0xE60 bytes, vt 0x10134020, ctor FUN_10060280). **Rescue state = dword
  +0xE10**: the ctor sets 0 (bound); `babe_activate` 0x10060ac0 (Duke uses her:
  the activator must be the player, +0x48&2 and +0x4C&0x100) gives the rewards (`powerup_give_send` types 5 and
  1) only while it is 0, then writes 1 (0x10060b56) and a quip timer at +0xE5C; `babe_handler` 0x10060800 moves 1
  or 3 to 2 (release animation), FUN_10060ea0 moves 2 to 3; nothing writes 0 again. Freed ⇔ +0xE10 != 0: the ESP
  hides her marker and arrow (`ent_babe_freed`), and the watch log keeps a `watch: Babe <p> +E10 0 -> 1` line. "YOU SAVED HER!" is `p_saved_babe` in local.def; no literal of that key is in duke_base (string
  search), so the text lookup was not traced; the state field does not depend on it. Watching
  +0x2E0 and +0xB80..+0xBBC showed no change across a rescue: none of them is the state.
- Secrets: trigger volume class **`area_secret`** (areas.events, vt 0x10139C50). area_secret_on_enter 0x1009be40:
  if +0xAE4 != 0 (armed) and the activator is the player inside the volume (vt+0x1B8), then PS+0x4C += 1, the score
  goes up and the sound plays [D]. **+0xAE4 is "not found yet" (measured in game):**
  `watch: Secret 02E9F0B8 +AE4 1 -> 0` when the secret was found, and after a restart the same secret re-registers
  with `new +AE4=0`. The ESP hides a secret with +0xAE4 == 0 (`ent_secret_found`).
  **Where the volume is:** Duke is "inside" when `area_e::inside`
  p3shared!0x100014d0 (called by the vt+0x1B8 test duke!0x10091840) finds his vt+0x160 position, moved by the
  inverse world matrix at +0x134, inside the `avolume` BSP. That resource is the item at **+0xA7C** (ushort flags,
  mask 0x8 (bit 3) = loaded; +8 data = min[3], max[3] in local space, then the BSP: `get_range` 0x10001580, `get_bsp`
  0x10001650). So the volume's world box is the local range through the world matrix at +0xF4, and +0x18C (the
  origin) need not be its centre. The marker and arrow use its centre (`area_bounds`); the box is drawn as the
  volume's own, possibly rotated, box (`area_obb`: the local range's corners through +0xF4), not the enclosing
  world AABB. The local range is read once per secret once loaded; the log gives +0x18C beside the centre when the
  volume first loads, and an unloaded volume keeps +0x18C (`boundsUnloaded`).
  There is no per-level total field: **total = count `area_secret` entities** with the enumeration recipe; found = PS+0x4C.
- Nukes: class `nuke`. Count = PS+0x48; episodes.def `nukecount` is the per-episode bonus threshold [D 0x10079b90].
- Editor/property registry: `entity_base_t::get_evalue_def(name)` / `get_value(name, idx, buf, len)` (p3shared
  exports) look up any registered property by name at runtime (for example "key_type", "health" on breakables). This is
  the preferred way to read class fields without offsets [D p3shared!0x1000df40/0x1000dfc0].

## 5. Weapons and the fire path

- Classes `actor_weap_{base,geagle,shotgun,x3000,arifle,pbomb,gray,pcannon,prpg}`. Projectiles are
  `actor_proj_{bullet,plasma,pbomb,pc_rocket,laser_bolt,glopp,gray,fm_bolt,prpg,*_grenade,…}`.
  **No hitscan: even the pistol spawns `actor_proj_bullet`** [D weap_geagle_spawn_shot 0x10055500].
- Pistol (Golden Eagle, vt 0x101317E8) fields [D weap_geagle_fire_logic 0x10055120]: +0xB14 now, +0xB18 last fire
  time, **+0xBF8 current refire/lockout delay: a shot is allowed when `now-last >= +0xBF8`**, +0xC2C configured refire
  (copied into +0xBF8 after each shot), **+0xC04 shots fired in the clip, +0xC3C clip size** (CE +0x552E8);
  exceeding it calls weap_geagle_start_reload 0x100553e0, which sets +0xBF8 = +0xC30 (reload time), +0xC38 = reloading,
  +0xC04 = 0. Ammo is checked through the owner's vt+0x224 = player_consume_ammo(type, n) [A].
  The other weapons' field offsets are open. They share the actor_weap_base layout (the shotgun and rifle have
  `+0xC04/+0xC3C` accesses to verify).
- Spread: the pistol uses `rand()` only for shell-case spin and sound choice. Its shot direction has **no random
  deviation** [D]. `sys_rand` is never used in weapon code [X]. rand() users that may be spread are FUN_10052bf0 and
  FUN_100578f0 (open).
- **Shot call chain** [D/A]: weapon think FUN_10054f80 → weap_geagle_fire_logic 0x10055120 (timer, clip, anim vt+0x21C)
  → weap_geagle_spawn_shot 0x10055500:
  1. `weap->vt+0x210(&dir)` = **weap_get_fire_dir 0x100508b0** → owner `vt+0x21C` = player_get_fire_dir 0x10078930
     → state object `player+0xB80[player+0xB7C]`->vt+0x28 = playerstate_get_fire_dir 0x10088400 (`ypr_2_cartes(out,
     player+0x1A8)`).
  2. `spawn_entity_autoname(world, "actor_proj_bullet")`, deny-collide with the owner (`tmatcher_t::deny_ptr` on +0x220/+0x244).
  3. bullet+0x18C = **weap_get_fire_origin 0x100508f0** → owner vt+0x220 → playerstate_get_fire_origin 0x10088530
     (head-bone based) [D].
  4. **bullet+0x1C0 = dir** (instructions 0x1005561f..0x1005563b) [A]; +0xBC0 = damage (from def "proj_damage");
     +0xB30 = attacker (FUN_10039980).
  5. Hitscan, not flight: on its first think the bullet casts one `phys_ray_test` along +0x1C0 and damages up to 3
     entities from the collide history (`proj_bullet_hitscan_ray` 0x1003a9c0, see hitboxes §2). 0x1003c2e0/0x1003c694 are actor_proj_fm_bolt code, not the bullet.
- **Silent-aim injection point:** the return of `weap_get_fire_dir` 0x100508b0 (or the player-state vt+0x28). Overwrite
  the out-vec3 with `normalize(target − origin)`. It applies to every weapon that asks its owner for a direction. An
  alternative is to rewrite bullet+0x1C0 right after the spawn. Whether other weapons call vt+0x210 is open
  (grep `CALL [reg+0x210]` in their spawn functions).

## 6. Damage

- Entry, one virtual for every damageable object: **vt+0x198 `void __thiscall damage(int type, float amount, vec3*
  dir, object_e* inflictor, object_e* attacker)`**. Evidence: the call site 0x1003c694 pushes (attacker, inflictor,
  &dir, amount, 0); FUN_1008b9e0 calls it with (2, 100000.0f, &zero, a, a) [A/D].
  Monster implementation: monster_damage_send 0x1002f3f0 only **serialises a net message 0x82** (type, amount,
  dir, flags, attacker +0x74). Player: player_take_damage 0x10078de0.
- Applied: **monster_apply_damage 0x1002f560 = monster vt+0x2FC** `(int type, float amount, float* dir,
  uint flags, object* attacker)`: `+0xB84 -= amount`, and a kill credits the player via player_give_net [D; vtable
  data refs X]. **Hook here for damage numbers:** victim = this, amount = arg2, position = victim vt+0x160 (arg3 is a
  direction, not the hit point). Also reached from FUN_1001e120, a thin vt+0x2FC override that tail-jumps into it.
  Player health changes all go through player_add_health 0x10079530 (delta in arg1).

## 7. Ray tests (prism3d exports, [D] entry + duke_base call sites)
`world` = entity+0xE0. Each call works on `world+0x4B8` (the collision system) and returns 0 if it is NULL.
- `bool phys_ray_visible(world, const vec3* from, const vec3* to, tmatcher_t* filter)` 0x40d8e0: **true = line
  clear**. `filter` is an entity filter (callers pass `ent+0x244`; NULL = default). Call site FUN_100d3710 (4 corner
  rays around an entity) [D].
- `bool phys_ray_collide(world, from, to, vec3* out_hit, tmatcher_t* f1, tmatcher_t* f2)` 0x40d6a0: true on a hit,
  and writes the hit point to out_hit. Suited to teleport.
- `void* phys_ray_test(world, from, to, tmatcher_t* f1, tmatcher_t* f2)` 0x40d820: non-NULL when blocked (the pointer
  is inside the hit record: `DAT_0049787c+8`). Many projectile call sites (FUN_10039430) [A].
- These rays collide with entities as well as the world (hence the tmatcher filters, as the bullet setup shows). Which
  entity classes block by default is open (a ray cast at a monster at runtime would show).

## 8. Roach eggs (`roach_pod`)

An egg hatches only when its `hatch` input pin is set, and only the map's wires set it: 35 of the
game's 120 eggs are wired (to an `area_switch` or to a `timer` an area_switch starts), the other 85 never hatch.
No track, distance or player test is involved. Every egg takes bullet damage (25 health) whether wired or not.
The ESP draws a wired egg as an enemy ("Roach Egg") and an unwired one dimmed ("Roach Egg (inactive)"); silent aim
and both Triggerbot modes take only wired eggs. Verified in game on e04p01_a01 (wired egg red and targeted, inactive
eggs dimmed and never targeted).

- **Class** [G `dumps/duke_base.classes.txt`]: `roach_pod`, category `object`, factory 0x100da4c0, alloc 0xC5C, vt
  0x10135f80. The bullet ray judges it destructible
  (aim.md section 4b); the walk classes it as an enemy egg, not `other`.
  Ctor 0x10072280 [D]: `actor_model_ctor` base (+0xAF0 = 1), user |= 0x10000000, three pins
  registered by name: `hatch` (+0xB00, input), `hatched` (+0xB68, output), `gibbed` (+0xBD0, output); **health
  +0xC4C = 25.0** (no property sets it); +0xC48 hatched = 0; half-extents (15, 23.5, 15). Init 0x10072490: model
  `models/e04/world/roach_egg`. Save/load 0x10072a80/0x10072ae0 keep +0xC48, +0xC4C, +0xC50.
- **Damage** vt+0x198 = 0x10072770 (`roach_pod_damage` in Ghidra) [A]: returns for **damage types 3 and
  4** (the G.L.O.P.P. beam's is 4, weapons.md section 8); returns unless `[+0xE0]+0xC8 & 8` (the world's running
  flag, the same test as monster_damage_send's); `+0xC4C -= amount`; at <= 0 with +0xAF0 set it clears +0xAF0 and
  `net_send`s message 1. **No per-egg damage gate**, so no boss-style "immune now" rule; the beam is refused
  (`GLOPP_IMM_EGG`, aim.h).
- **Messages** vt+0x1A8 0x10072ba0 [A]: 1 -> the gib 0x100728b0 (+0x2E0 = 0, +0xC58 = 1 so the tick 0x100726d0
  removes it next frame, sends `gibbed`, and when not hatched gives Duke **+1 Ego**: `powerup_give_send(player,
  1, 0, 1, 1, 0)`, type 1 as ego_on_touch, pickups.md); 2 -> the hatch 0x100727e0 (plays the hatch animation,
  sends its spawner's pin, sends `hatched`, +0xC48 = 1). The tick creates one `spawner_entity` per egg
  (0x10072570, spawnscript `scripts/monst/roach.sc`, held at +0xC54) that makes the Roach.
- **Activation** [D 0x10072680, the signal handler, vt+0x60]: when +0xC48 == 0 and the hatch pin's flag
  (+0xB54 = pin+0x54) and value (+0xB58 = pin+0x58) are set, it sets +0xC48 = 1 and sends message 2. The string
  `hatch` has one reference (the ctor's regp), so nothing in duke_base sets the pin by name. The world.scr files
  wire it with `connect signal:roach_podN.hatch` on the sender ([S], every `db_*.zipfs`): e.g. e04p01_a01 has 13
  eggs, of which `roach_pod17` (area_switch13) and `roach_pod20` (area_switch14) are wired.
- **Reading the wiring at runtime** [D p3shared]: an input pin keeps no list of its senders. `pin_t::connect`
  0x1001bd40 allocates a 0x44-byte wire, copies the target name (what follows the `:`, prism3d!0x402066) to
  +0x00 (at most 0x3F characters) and links it at **pin+0x64** (next at wire+0x40); `entity_t::pin_io` 0x1000d6d0
  delivers each set **output** pin (pin+0x04 == 2) to each of its wires. Pins chain from **entity+0x5C** through
  **pin+0x0C** (`regp` 0x1000e340, `next_pin` 0x1000def0); the name sits at pin+0x11. So `egg_scan`
  (entities.cpp) walks every entity once (`spawn_search("s=%d")` with no `t=` runs the server's whole list,
  prism3d!0x40cf30) and keeps each wire that ends in `.hatch`; an egg whose own name (+4) is in that table is
  wired. It runs once per level, again when more eggs appear, at most every 2 s. The level is the player and his
  world (+0xE0): a change drops the table and every cached verdict before that frame's walk, so an egg is never
  matched against the previous level's table (it reads unread until the scan). Each egg's verdict is cached per
  entity until the table is rebuilt (`eggCacheFull` counts eggs past the 256-entry cache).
- **States** (`egg_state`, eggrule.h, in the handler's order): gone (hatched +0xC48, dead +0xAF0 == 0, gibbed
  +0x2E0 == 0, or health <= 0: no marker, no target), active (wired), inactive (not wired), unread (the scan was
  capped or its table full: drawn as "Roach Egg (unread)", never targeted).
- **Log:** `eggs: wire scan #n: E entities, P pins (O output), W wires, H to a hatch pin naming N egg(s); unread,
  capped, dropped, us` per scan, and the `ents:` line's `eggs active/inactive/unread/gone=a/b/c/d eggScans=`.
  The aim's `immuneBy` gains `eggIdle=` (unwired eggs skipped) and the glopp line `egg=`.
- **Open:** that the wire delivery (`DAT_1004bf50`, not traced) matches names exactly; the table is matched
  case-blind. That every map wire to an egg exists on an entity the scan reaches (the log's `naming N` against
  the [S] count for the level settles it: e04p01_a01 should name 2).
- **Check:** `map e04p01_a01`, Enemies on. Near (3756, -5032, -3192) five eggs stand together:
  `roach_pod17` draws as "Roach Egg" in red, `roach_pod14/15/16/18` dimmed as "Roach Egg (inactive)". Silent aim
  or the Triggerbot locks only the bright one; shooting a dimmed one still breaks it. When the area switch hatches
  `roach_pod17` its marker goes; a broken egg's marker goes the next frame.

## Measured in game

- `spawn_search` + vtable slot 1 enumerate correctly: about 50 entities, about 20 `monst_*`, first seen `monst_pigoid2`.
- Monster position at +0x18C equals the vtable +0x160 world-position getter (-632.00, 363.88, -2528.00), so +0x18C
  is the world position for an entity with no parent (an attached one's is local: section 1, Attached entities).
  Crosses projected onto enemies correctly.
- **Up axis = +Y** (player y rises from 1209 to 1300 during a jump). Ground jump vy = 571.2 at 60 fps.
- The walk costs 10-29 us per frame with the region cache (about 10 VirtualQuery misses per frame).

## Open questions (and the measurement that settles each)
1. Which component of +0x1A8 is yaw or pitch: read it at runtime while turning.
2. The active-weapon pointer on the player: enumerate the player's children and watch the fire timers.
3. Monster skmod handle at +0xAE4 and the "Bip01 Head" bone: `skmod_get_bone_index` at runtime.
4. Powerup vt+0x254 = remove: breakpoint or read-after-pickup.
5. Spread and fire-dir use in weapons other than the pistol; whether the ray tests hit monsters by default.
6. The Queen-arena train pickups draw markers that ride the cars (section 1, Attached entities).
