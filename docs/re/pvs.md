# PVS / vis tree: what still hides geometry and entities with the frustum off

There is **no effective PVS in this game**. Every shipped level's `vtree.bsp` has exactly
**one sector and one BSP leaf** (99 of 101 maps; the two exceptions are sky maps), measured offline by parsing every
`maps/*/vtree.bsp` in `db_e01..08.zipfs`. The leaf walk in `map_draw_geometry` / `map_vis_draw` therefore always
selects sector 0 = the whole level, wherever the eye is. **No PVS hook is needed.** The menu's no-culling option forces
the three frustum exports visible and raises `g_cam_far`; the remaining culls are the per-mesh distance limit inside
`rend_sphere_visible_dist` (defeated by the same detour) and the one-sided mesh test, which stays on.

Addresses: duke_base base 0x10000000. D = decompile, A = disassembly, X = xref/script, S = string/data,
M = measured offline (script `scripts/vtree_stats.py` in the Ghidra project, output `dumps/vtree_stats.txt`).
Names set in duke_base: `vtree_bsp_load` 0x100ed630, `vtree_bsp_relocate` 0x100ed580,
`vtree_read_sector_node` 0x100ed910, `vtree_read_bsp_node` 0x100ed9d0, `vtree_size_sector_node` 0x100ed890,
`vtree_size_bsp_node` 0x100ed810, `vtree_bsp_resource_new` 0x100eddf0, `mesh_gdt_resource_new` 0x100eddc0,
`map_sub_mark_visible` 0x100be7e0.

## 1. The vtree_bsp resource and the leaf walk (settles the vtree-layout question in [camera-culling.md](camera-culling.md))

Loader: `map_load_resources` 0x100ee0b0 asks for `<map>vtree_bsp` with creator `vtree_bsp_resource_new` 0x100eddf0,
whose resource vtable 0x10178808 slot 0 is `vtree_bsp_load` 0x100ed630 (D, A). The file is `maps/<map>/vtree.bsp`.
**File format** (from the readers 0x100ed910 / 0x100ed9d0, D; confirmed on all 101 files: the parse consumes every
byte, M):
```
u32 N                                   sector count
N x sector tree  node := u8 inner; f32 center[3]; f32 radius;
                         inner ? node(back) node(front) : u16 n; u16 mesh_index[n]
1 x BSP tree     node := u8 inner; inner ? f32 plane[4] node(back) node(front) : u16 n; u16 sector_index[n]
```
**In memory** (data = `*(res+8)`, the pointer `map_draw_geometry` reads as `puVar11+4`):
- `data+0` u32 N; `data+4` -> `sector*[N]` (at data+0xc); `data+8` -> BSP root.
- **sector** (0x18 bytes, zeroed then filled in `vtree_bsp_load`; `+0xc` store at 0x100ed7b2): `+0x00..0x13` a `vptrlist_t` of objects (head node
  at `+4`, node `{+4 next, +8 object}`; `+0xc` = `server+0x3e4`, the list allocator); `+0x14` -> sector tree root.
- **sector-tree node**: `+0` front, `+4` back (both 0 = leaf), `+8` sphere centre, `+0x14` radius; leaf `+0x18`
  u16 count, `+0x1a` u16 indices into the `mesh_gdt` table (`*(map+0xa24)+8`: u32 count, then `mesh*` from +4).
- **BSP node**: `+0` front, `+4` back, `+8` plane (vec4). Leaf: `+4 == 0`, `+8` u16 count, `+0xa` u16 sector
  indices. The first index is the leaf's own sector (`map_vis_insert` 0x100f0de0 links objects into that one only, D).

**Walk** (`map_draw_geometry` 0x100ee930, `map_vis_draw` 0x100f0f90, D): from the root, `point_right(pos, plane)` ->
`+0`, else `+4`, until `+4 == 0`. Geometry: for each listed sector, `map->vtable+0x1a0` = `map_draw_vtree_node`
0x100ee820 on `sector+0x14`. Entities: for each listed sector, walk its object list and call `obj->vtable+0xb4(pos)`
once per frame (stamp `obj+0x948` vs `map+0xacc`). The visible set is a **sector list per leaf** (not a bitset,
not portals).

**Measured content** (M, all 101 maps): N = 1 and one leaf `[0]` in 99 maps; `e01_sky` (2 sectors, 2 leaves) and
`e08_sky2` (3 sectors, 3 leaves) each list only their own sector. So in every playable map the BSP root **is** the
leaf and lists the only sector. The eye position cannot change what is drawn.

## 2. What still culls geometry (map_draw_vtree_node / map_draw_mesh)

- `map_draw_vtree_node` 0x100ee820 (D, A 0x100ee834): `rend_sphere_visible(node+8, node+0x14)` on every sector-tree
  node, recursive. Each leaf mesh (`mesh+8` stamp against `map+0xacc` dedups meshes listed in several nodes;
  8957 references for 2230 meshes in e01p01_a01, M) then gets `rend_sphere_visible(mesh+0x28, mesh+0x34)` if
  `mesh+4 == 0`, else **`rend_sphere_visible_dist(mesh+0x28, mesh+0x34, mesh+4)`: a per-mesh maximum draw distance**
  (call sites 0x100ee8c4 and, for the fallback path, 0x100eea58; D, X). Both are frustum exports (pfn labels in `dumps/duke_base.pfn.txt`: 0x1018411c, 0x10184184),
  so the frustum detours defeat both. How many meshes carry a distance is open (V1).
- **One-sided test O1** `map_draw_mesh` 0x100ee5b0 (D): only for meshes with `mesh+0x58 & 1` (planar). If the shader
  flag byte `& 0x18` and the eye (`rend_get_camera`) is on the left of the mesh plane `mesh+0x18`, skip; if `& 0x14`
  and the eye is on the right, skip. It culls **whole meshes by eye side**, from the real (pushed) eye. It hides
  only faces seen from behind, which the rasteriser would cull anyway if these bits are the shader's cull mode
  (open, V2). **Leave it on.** If it must go: detour
  `map_draw_mesh` (prologue `83 EC 10 55 8B 6C 24 1C` = SUB ESP,0x10; PUSH EBP; MOV EBP,[ESP+0x1C], clean, no rel32)
  is overkill; skip nothing.
- Meshes in `mesh.gdt` but in no sector node (6 of 2236 in e01p01_a01, up to 60 per map, M: gdt header count vs
  vtree indices) are never drawn by the vtree path in normal play either. Not a culling difference.
- No-vtree fallback (meshes with `+0x58 & 4`, 0x100ee9ef..) is dead: every map ships a vtree (M).

## 3. Entities (settles the PVS half of the entity-draw question in [camera-culling.md](camera-culling.md))

- `map_vis_draw` draws **every object linked into sector 0**, i.e. every object `srv_vis_insert` accepted
  (`obj->vtable+0x174` sample points > 0), regardless of the eye. No PVS, no radius at this level.
- Per-entity culls are inside each draw (+0xb4). All 24 duke sites use the **same exports**: the common pattern is
  `rend_sphere_visible(obj->vtable+0x160(obj+0x50c /*bound radius*/), ...)` (0x100919c6, 0x10107d1d, 0x101086bd,
  0x100f7da3, 0x10032cd3 ..., X+A); `rend_box_visible` has no duke caller. skmod.dll resolves all three by
  name (skmod!0x100015a0, S+X). So the frustum detours cover entity culling, **provided they patch the export code**
  (duke/skmod cache the pointers from GetProcAddress at load, e.g. duke!0x100def7b; a pointer swap would miss them).
- `vis_radius` is an evalue (S 0x101630c4, def 0x10162d44 -> field **+0xb2c**, float) of the class whose def chain
  also has `skins`, `attach`, `hide_parts`, `max_lights` (+0xb3c) (head 0x10163040, X). No sphere-test caller reads
  +0xb2c (X over all 25 `rend_sphere_visible` sites). Its consumer is unknown (open, V3).

## 4. How to draw everything, ranked

1. **(d) Nothing to do for the PVS.** Detour the three frustum exports and raise `g_cam_far` (shipped as the freecam's no-culling option).
   That is the whole job for geometry and entities in all 99 playable maps.
2. (a) "a leaf that sees everything": moot, the only leaf already does.
3. (c) Drawing all sectors ourselves: for a multi-sector map, after the original `map_draw_geometry` returns, call
   `map->vtable+0x1a0(sector[i]+0x14)` for every `i < N` (the mesh stamp already dedups). For entities, walk each
   sector's list and call `+0xb4(pos)` where `obj+0x948 != map+0xacc`, writing the stamp as the engine does.
   Hook point: `map_draw_geometry` 0x100ee930, `__thiscall` (ECX = map, [ESP+4] = `const vec3_t* pos`, RET 4),
   prologue `83 EC 0C 55 8B E9` (SUB ESP,0xC; PUSH EBP; MOV EBP,ECX; 6 bytes, no rel32), or its vtable slot
   `map+0x1a8` (vtable 0x101460c8). `map_vis_draw` 0x100f0f90 is the same ABI, prologue `83 EC 08 53 8B D9`.
   Only needed if a future map has N > 1; none ships.
4. (b) Patching the leaf test: rejected, there is nothing to patch in shipped data.

## 5. Cost

The PVS removes nothing today, so "whole level vs one leaf's set" = 100 %: turning culling off adds only what the
frustum and distance tests removed. A whole playable map is **65 to 6,352 meshes** (83 playable map sections, median 1,764; largest
`e05p02_a01` 6,352 and `e08p02_a02..a06` ~6,300, M), each queued as one `rend_proto` per frame
(`map_draw_mesh` -> `rend_alloc_proto`/`rend_queue_proto`), plus every vis-linked entity's model. Expect roughly
the in-view fraction's inverse (~3-5x mesh submissions; open, V4).

## Open questions

1. **V1** Per-mesh distance limits in use: count live meshes with `mesh+4 != 0` and their values.
2. **V2** O1 bits = shader cull mode: read gl.dll's shader bind for flag bits 0x04/0x08/0x10.
3. **V3** `vis_radius` (+0xb2c) consumer: data breakpoint on a model entity's +0xb2c read, or grep pmd.dll / skmod
   for the evalue name.
4. **V4** Cost: `map_draw_mesh` calls per frame (vtable slot `+0x1a4`), culling on vs off, plus frame time, in e05p02_a01.
5. **V5** Sky maps (`e01_sky`, `e08_sky2`, multi-sector): how they are drawn with the free camera.
