# Off-screen bullets and Duke's track

(A) The only off-screen gate on Duke's shots is `game_clip_ray_to_view_sides` duke!0x100e8540. Its only
shot caller is `proj_bullet_hitscan_ray`. Detour the entry and return `len` (5000) when the return address is
duke+0x3AAA4 and `bullet+0xB30` (the attacker) is the local player. Rockets and plasma are never clipped, and damage
has no visibility test. (B) Duke's track is `player+0xB20` (a `game_track`, an `area_e`). It is a flat XZ Hermite spline
with no height, which the engine samples into a polyline at `track+0xB2C`. The engine's own world-point-to-track
function is **`track->vt+0x1EC` = `game_track_project_point(vec3* inout)`** duke!0x10096260. It snaps x/z and keeps y.
Tracks curve (85 of 449). A map has 0-19 tracks. The engine picks Duke's track from area volumes when he moves or
teleports. The menu's Off-screen targets switch (the entry detour) and its track-locked teleport marker both work in game.

Tags: **[D]** decompile, **[A]** disassembly, **[X]** xref/vtable scan, **[G]** inline Ghidra script, **[S]** string,
**[F]** map files (`db_*.zipfs`, read with Python). Base duke_base 0x10000000. Ghidra
names: `proj_set_attacker` 0x10039980, `proj_get_attacker_or_self` 0x100398a0,
`proj_find_attacker_weapon` 0x10039920, `player_spot_monsters_for_quips` 0x1007b730, `game_quip_event` 0x100c5140,
`actor_draw_mark_seen` 0x1008d9e0, `monster_sleep_unless_seen` 0x1002d320, `monster_alert_if_seen` 0x1002ea30,
`actor_damage_from_info` 0x1008b630, `monster_fire_bullet_noclip` 0x1002b0a0, `spawner_target_near_camera` 0x10038bc0,
`monster_damage_send` 0x1002f3f0, `game_track_{ctor,finalize,tessellate,get_segment,adjust_offset_walk,
find_actor_span,snap_actor,project_point,adjust_offset,get_dir,actor_facing_with}`, `actor_pick_track` 0x1008e0f0,
`actor_track_state_reset` 0x1008d8c0, `actor_game_fielder_ctor/dtor` 0x1008d7b0/0x1008d8b0. Functions had to be created at
0x100958a0, 0x100956a0, 0x10094aa0 and 0x1008e0f0, where Ghidra had none.

## A. Off-screen bullets

### A1. `game_clip_ray_to_view_sides` duke!0x100e8540: ABI [A]

- `float __thiscall clip(void* game_globals, const vec3* from, const vec3* dir, float len)`, `RET 0xC`, result in ST0.
  ECX is loaded with `[0x101869dc]` (game_globals) at both call sites (0x1003aa92, 0x1007b7a3). The body never reads
  ECX: 0x100e8559 overwrites it before any use. Stack args: `[esp+4]` from, `[esp+8]` dir, `[esp+0xC]` len.
  Both callers pass `len = 5000.0f` (`68 00 40 9C 45`).
- Body [D/A]: `wm_get_object(0)` (pfn 0x10183f4c) is the view object. `vt+0x164` builds its matrices, and two side planes
  (fov x 0.52, 1500 deep) are intersected with the ray. It returns the distance to the hit plane, or `len` when neither
  plane is hit (0x100e8985 `FLD [esp+0x138]` = arg len). It writes no engine memory.
- Callers (xref, complete) [X]:
  1. **duke!0x1003aa9f in `proj_bullet_hitscan_ray` 0x1003a9c0**: `from = EBX = bullet+0xBF0` (copy of +0x18C),
     `dir = EDI = bullet+0xBFC` (normalised +0x1C0), then the result scales dir (0x1003aaa4..0x1003aab5). The call is
     gated by `CMP byte [ESI+0xC08],1` (0x1003aa47, `clip_to_frustrum`, ctor default 1). **Return address
     0x1003aaa4.** ESI = the bullet at entry.
  2. duke!0x1007b7ae in `player_spot_monsters_for_quips` 0x1007b730 (via 0x1007b2e0): it clips the *player's* forward
     ray, then looks for monsters already drawn (`+0xB71`) within that distance and a 45° cone that a ray test sees,
     and fires `game_quip_event` (Duke's voice lines). This is cosmetic. Return address 0x1007b7b3.
- Nothing else in duke_base calls it (xref).

### A2. Telling Duke's bullet apart [D/A]

- `bullet+0xB30` = **attacker**, written only by `proj_set_attacker` 0x10039980 (it also links the proj into
  the attacker's list via 0x1008ba80). All three player guns call it with
  `weapon_get_owner(weapon)` = `weapon+0x60` gated on `+0x48&2`: geagle 0x100556b0/b8, shotgun 0x1005bcbf/c7, arifle
  0x10052eab/b3 [A]. So for Duke's bullets, `+0xB30 == local player` (`get_local_player` 0x100e7060, or our own
  cached player pointer).
- Monster bullets: `monster_fire_bullet_noclip` 0x1002b0a0 sets the attacker to the monster and **sets
  `clip_to_frustrum` = 0** (`vt+0x20c("clip_to_frustrum", 0)`, 0x1002b187), so monster shots never reach the clip.
  The boss chain-gun FUN_1000cab0 does not clear it, and its bullets are clipped with attacker = the boss. The
  +0xB30 test passes those through unchanged.
- The attacker is set before `vt+0x58` (post-init). The ray is cast on the first think (`+0xBEC` = 1), so +0xB30 is
  valid when the clip runs.

### A3. Hook point (recommended: entry detour, pass-through by default)

```
float __fastcall clip_hook(void* ecx_gg, void* edx, const float* from, const float* dir, float len)
{   // __fastcall with an unused EDX = this ABI: ECX in, 3 stack args, callee pops 12, float in ST0
    if (g_offscreen_hits && _ReturnAddress() == duke + 0x3AAA4) {
        char* bullet = (char*)from - 0xBF0;               // EBX = ESI+0xBF0 at 0x1003aa15
        if (*(void**)(bullet + 0xB30) == local_player) return len;
    }
    return orig(ecx_gg, edx, from, dir, len);
}
```
- Fingerprint at duke!0x100e8540: `81 EC 24 01 00 00 56 57 6A 00 FF 15 ?? ?? ?? ?? 8B 3D ?? ?? ?? ?? 83 C4 04`
  (unique in duke_base, [G] findBytes). The **first 10 bytes are relocation-free** (`sub esp,0x124; push esi; push
  edi; push 0`). Steal 6 (the whole `sub esp,imm32`) or 8. Bytes 10..15 hold `call [abs]` (relocated), so
  never copy past byte 10.
- Returning `len` is exactly the original's "no side plane hit" path, so the bullet gets the plain 5000-unit ray.
  Hits beyond 5000 units stay out of reach, as they are on screen.
- Alternative: re-target the rel32 of the call at 0x1003aa9f (`E8 9C DA 0A 00`). This touches bullets only and needs
  no return-address test. Fingerprint `8B 0D ?? ?? ?? ?? 68 00 40 9C 45 57 53 E8` at 0x1003aa92 (unique, [G]). It is
  ranked second only because the entry detour matches our existing detour pattern.
- Rejected: clearing `bullet+0xC08` on spawn. It is an engine field the engine does write (0x1002b187), but it
  needs a spawn hook and it is a write. The detour writes nothing.

### A4. Other off-screen gates on Duke's shots

- **Damage: none.** Bullet hit → `proj_bullet_damage_hit` 0x1003ad90 → `victim->vt+0x194` (`actor_damage_from_info`
  0x1008b630) → `vt+0x198` → `monster_damage_send` 0x1002f3f0 → `monster_apply_damage` 0x1002f560. No `+0xB71`
  (seen flag) or frustrum read anywhere on that path [D; complete +0xB71 reader list, G].
- **Projectiles:** rockets, plasma, pbomb and gray never call the clip (only two callers, A1), and no projectile
  code reads the view. `spawner_target_near_camera` 0x10038bc0 (called from 0x100389f3) is a monster-spawner range
  test (5000 from the camera), not a shot gate [D/A].
- **Collision:** the only visibility-driven writer of `+0x2E0` (collidable) is `map_sub_mark_visible` 0x100be7e0
  (map sub-objects, not monsters) [G: all 100+ `+0x2E0` byte writes listed, D].
- **Seen flag `actor+0xB71`** (`actor_draw_mark_seen` 0x1008d9e0): set when the actor's 3x-radius sphere passes
  `rend_sphere_visible` in its draw, cleared 10 s after it was last seen. It drives **AI wake-up only**:
  `monster_sleep_unless_seen` 0x1002d320 keeps an unseen monster in state 1, and `monster_alert_if_seen` 0x1002ea30
  (called with 20.0 from `monster_apply_damage`) only alerts a seen monster. So an off-screen monster **takes damage
  and dies**, but a never-seen one does not react to being shot. Interaction: the freecam no-culling option (forcing
  `rend_sphere_visible` true) sets +0xB71 on every drawn actor and wakes them (answers the question in [camera-culling.md](camera-culling.md)).
- **Our own filters:** [aim.md](aim.md) §4 and the triggerbot restrict targets to on-screen because of this clip. With the
  option on, that rule should follow the option (the bullet then reaches 5000 along the aim ray).

## B. Duke's track

### B1. Player fields [D/A]

- **`+0xB20` = current `game_track*`** (used by `entity_camera_from_track` 0x1008e8a0: `8B 81 20 0B 00 00 85 C0 74 12 …
  FF 92 DC 01 00 00` = `[ecx+0xB20]->vt+0x1DC`). `+0xB24` = previous track (0x1008e117). `+0xB58/+0xB5C` =
  (span, sample) cursor on the track, -3 = unknown (reset whenever the track changes, 0x1008e2e2). `+0xB1C` (byte) =
  "follows tracks" (checked first in `actor_pick_track`; the player clears it in some states, 0x1007ed12/0x100809e4).
  `+0xB38..+0xB4C` hold the pose cached by `actor_track_update_if_moved`. `actor_track_state_reset` 0x1008d8c0 zeroes
  them.

### B2. Track geometry (`game_track` vtable 0x10138688, `game_track_auto` 0x10138890; class is an `area_e`) [D/S/F]

- Script keys (S 0x10166940): `route_point`, `route_dir`, `camera_offset`, `no_offset`, `add_offset`, plus `looped`,
  `sync_cameras`. In memory (`game_track_ctor` 0x10094330):
  `+0xAF8` vec3* route_point[n], `+0xAFC` n; `+0xB00` vec3* route_dir[n] (tangent handle point, tangent =
  dir - point); `+0xB08` camera_offset[]; `+0xB10/+0xB18` no_offset/add_offset arrays; `+0xB24` byte looped.
- `game_track_finalize` 0x100944c0 **zeroes y of every route_point and route_dir** (`+4` = 0). The track is a curve
  in the **XZ plane only**. Duke's height is free (physics).
- `game_track_tessellate` 0x100945c0: for span i (i..i+1, or n-1..0 if looped) `smooth_curve(P_i, D_i-P_i, P_i+1,
  D_i+1-P_i+1, u)` (a Hermite curve) is sampled `+0xB28[i]` times (int array, from FUN_10094c50). The **sample
  array `+0xB2C`** holds `{x, 0, z, cumulative arc length}`, 16 bytes each, row stride `+0xB30` samples per span.
  Sample (i,j) = `+0xB2C + (i*[+0xB30] + j)*16`. Samples per span (`FUN_10094c50`): 1 when both handles lie within 1 of the chord, else
  at most 32 (ftol of the float 32.0 at 0x10138684, scaled by 0.25 under the 5.0 threshold), so **stride <= 33**;
  a 26-point curved track is at most 858 samples. The reader's cap is 1024 (512 refused a real track). `game_track_get_segment` 0x10095410 returns segment (i,j)→(i,j+1)
  and handles the -1/-2 "before start/after end" extensions (along route_dir). Coordinates are world space (compared
  directly with actor +0x18C).
- [F] every `world.scr` in all `db_*.zipfs`: 449 tracks (296 `game_track`, 153 `game_track_auto`). 85 have handles off the chord
  (**curved**), 2 looped, at most 26 points. Per map: 18 maps have none, 41 have one, and up to 19. Each has its own
  `avolume` resource (`maps/<map>/game_track1/avolume`). `utility_track` (53) is a separate camera/object path
  class with route_speed/orient. It is not Duke's.

### B3. How Duke is constrained and switched [D/A/X]

- Movement: the player move code calls `track->vt+0x1C4` = `game_track_adjust_offset` 0x100958a0 (duke!0x1007f919,
  also 0x1008e64e/0x1008e69a). It finds the span (`game_track_find_actor_span` 0x10094f30), walks the desired XZ
  offset along the polyline (`game_track_adjust_offset_walk` 0x10095900, "_adjust_offset … off track" S
  0x101669a0), and writes the actor's `+0xB58/+0xB5C`. `vt+0x1D0` = `game_track_snap_actor` 0x10096070 projects the
  actor's XZ onto the nearest segment, **writes actor +0x18C.x/.z** and its facing, and sets the cursor. `vt+0x1C8` =
  track direction at the actor (`game_track_get_dir`). `vt+0x1CC` = facing with/against the track.
- Switching: `actor_pick_track` 0x1008e0f0 = actor **vt+0x210**, called by `actor_track_update_if_moved` (vt+0x230)
  whenever the actor moved > 0.01 or turned. If `+0xB1C` = 0 → no-op. If the current track's `vt+0x1C0()` is 2
  (`game_track_auto`), it is kept. Otherwise `spawn_search("s=%d?t=%d?Y=%d", sid, 0x20, 1)` over areas. Pass 1:
  tracks with a volume (`+0xAE4`) whose `vt+0x1B8(actor)` (inside the area volume) is true; the current track wins if
  still inside, else the last one found. Pass 2 (inside none): keep the current track; else the only one if there is
  one; else **the nearest by `vt+0x1EC` projection distance**. On change: `+0xB58/+0xB5C = -3`, `+0xB20 = new`.
  `actor_teleport` clears `+0xB20` and then calls vt+0x230, so **a teleport re-picks a track at the destination by
  the same rule** (Duke gets the nearest track when no volume covers
  the point).

### B4. World point → track point

- **`void __thiscall game_track_project_point(game_track* t, vec3* p)`** = `t->vt+0x1EC`, duke!0x10096260, `RET 4`
  [A]. It builds a temporary `actor_game_fielder` (0xB88 bytes) on its own stack, sets its +0x18C = *p, +0xB20 = t,
  cursor -3, calls `t->vt+0x1D0` (snap) on it, copies the temp's +0x18C back to *p (x/z on the track, y
  unchanged), then destroys it. **It writes only its own stack object.** The engine calls it for every candidate
  track in `actor_pick_track` and at 15 other sites that load +0xB20 just before (monsters, player 0x1007f363, 0x10078326; G). If the track has
  fewer than 2 points, p comes back unchanged (0x10096070 `afc > 1`). The slot constant 0x1EC is justified by
  those call sites (`FF 92 EC 01 00 00`).
- Pure-read alternative (unit-testable): walk the sample polyline at `+0xB2C/+0xB28/+0xB30/+0xAFC` (one guarded
  read of `n*stride*16` bytes per use).

**Teleport-marker recipe.** Run it on the main thread; the track can be any of them, not only +0xB20.
1. `t = player+0xB20`; if NULL, fall back to free placement.
2. Crosshair ray `o + s*d`. In XZ, intersect the ray with the track polyline: for each sample segment, solve the 2D
   ray/segment intersection, take the nearest s > 0, and y = o.y + s*d.y. If there is no crossing (the view runs
   along the track), take the point on the ray closest in XZ and `vt+0x1EC` it.
3. `vt+0x1EC(&pt)` to land exactly on the engine's own line, then drop to the floor with `phys_ray_test` downward
   using the world-only matcher ([freecam-teleport.md](freecam-teleport.md) §2), then the clearance step, then `player_teleport`.
4. The marker at any height = the XZ track point with y from step 2 (or the drop result).

## Open questions

1. **U1** Never-seen monsters (+0xB71 = 0) killed off-screen: death and score work (`monster_apply_damage` has no seen
   gate, D), but whether the kill animation plays while asleep is unread. Watch one die off-screen, then pan to it.
2. **U2** Aim-ray length: pistol, shotgun and arifle rays should reach 5000 units after the skip. A second cap is not
   ruled out (log `bullet+0xBFC` length after the call at 0x1003aab5).
3. **U3** The `vt+0x1EC` call balance: the temp actor ctor/dtor must stay balanced over 10k calls (check process
   private bytes).
4. **U4** The world.scr value order (does the file list `route_point` last-to-first?) does not matter for the menu;
   index 0 can be confirmed by reading `+0xAF8[0]` against the file.
