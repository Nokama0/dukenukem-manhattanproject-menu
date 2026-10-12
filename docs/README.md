# Docs index

Reverse-engineering notes for the Manhattan Project mod menu ("Nuke Panel"), one note per topic, in `re/`. Each note
opens with a summary of what is known, then the facts by topic, and ends with the open questions. Addresses are for
the Steam Zoom Enhanced build; `Name.dll!0x...` names the module.

- [re/render.md](re/render.md) -- frame loop, overlay hook, camera and world-to-screen, FOV, window, UI scale.
- [re/input-console.md](re/input-console.md) -- cvar and command registry, console inventory, input path, free camera, input blocking.
- [re/cheats.md](re/cheats.md) -- every cheat and console command or cvar: `give`, god, hover, `map` warp (28 developer maps), the opening key, command-line switches, full registry appendix.
- [re/entities.md](re/entities.md) -- entity enumeration, player/enemy/pickup layout, fire path, damage, ray tests, roach eggs (hatch wiring).
- [re/timing.md](re/timing.md) -- frame time path, physics step, jump gates, 175 Hz candidates.
- [re/game-speed.md](re/game-speed.md) -- who reads which clock; Game speed scales `timeGetTime` for prism3d's `tmr_read` only.
- [re/double-jump.md](re/double-jump.md) -- the boost lost when the jump frame is <= 5 ms: next-frame ground sweep, contact, and the restore fix.
- [re/camera-culling.md](re/camera-culling.md) -- camera pipeline (`actor_camera`, `game_track`), culling map, camera collision, zoom mechanism.
- [re/pvs.md](re/pvs.md) -- `vtree_bsp` format; every playable map is one sector and one leaf, so no PVS hook is needed for a draw-everything free camera.
- [re/exit-hang.md](re/exit-hang.md) -- the process that outlived its window: a held cache reference blocked every quit.
- [re/quit-path.md](re/quit-path.md) -- both quits run `cmd_quit`; the loop waits for process locks; the held cache reference.
- [re/hitboxes.md](re/hitboxes.md) -- pickup and hit volumes: one axis-aligned box, boss mesh test, babe rescue.
- [re/freecam-teleport.md](re/freecam-teleport.md) -- teleport via the player's vt+0x218, world-only ray, mouse after `inp_set_active(0)`, menu coverage.
- [re/freecam-pause.md](re/freecam-pause.md) -- Pause the game: `process_sleep`/`process_wakeup` on the player's world; menu and level-load interplay.
- [re/photo.md](re/photo.md) -- high-resolution freecam photo: tiles read from the composed screen, narrowed fov plus a view shear, WIC PNG by strips on a worker, the Photo folder.
- [re/paths.md](re/paths.md) -- depth runs: `+0xB48` ends with height, `auto_type`, use-key start; `area_teleport`; why the route feature was removed.
- [re/pickups.md](re/pickups.md) -- ammo, health and gun subtype fields (`+0xD14`) and names; life = Double Damage.
- [re/weapons.md](re/weapons.md) -- fire gates per weapon, pistol-only reload, arifle-only spread cone, held weapon `player+0xDF0`.
- [re/aim.md](re/aim.md) -- silent aim (the `weap_get_fire_dir` detour), `+0x1A8` pitch/yaw/roll, head bones, the bullet hit rule, firing by impulse 106, boss gates, G.L.O.P.P. Ray, eggs, overkill, weapon immunity, X-3000.
- [re/mouse-aim.md](re/mouse-aim.md) -- mouse fire (already bound), the engine cursor `inp_cursor_info`, shots to the cursor, the `player_spine_bend` torso detour, facing by yaw, own sensitivity.
- [re/secrets-walk.md](re/secrets-walk.md) -- `t=` picks one per-type list; `t=2` is dobj only, `area_secret` lives in `t=0x20`.
- [re/levels-and-stats.md](re/levels-and-stats.md) -- the one secret branch (Wozma), N/M stats only on part-end and Load Game slots, per-part tallies, totals from `episodes.def`.
- [re/kills-and-spawners.md](re/kills-and-spawners.md) -- what counts as a kill, killcount = placed + spawners, e05p02's unfired ambush switches, spawner fields.
- [re/save-io.md](re/save-io.md) -- the save freeze: each save `vfs_write` is one unbuffered `WriteFile`; the `saveio:` line times the file API.
- [re/state-files.md](re/state-files.md) -- a `load` of a not-done part unpacks the save's position block before checking its part and keeps the files; recovery.
- [re/stalls.md](re/stalls.md) -- the ~1 s hitches: a watchdog samples the main thread's EIP and EBP chain and logs `stall:` lines.
- [re/save-awards.md](re/save-awards.md) -- Load Game slot awards (16 glyphs), their conditions, cheat flags, whole-game kills, the SAVE AWARDS grid.
- [re/offscreen-and-track.md](re/offscreen-and-track.md) -- off-screen bullet clip, `game_track` geometry, track pick, `vt+0x1EC` point projection.
- [re/display.md](re/display.md) -- floating text offset (the 3D billboard script), HUD canvas maths, borderless via windowed restyle and `rend_restart`.
- [re/hotkeys.md](re/hotkeys.md) -- Windows key and global hotkeys dead in game: `DISCL_NOWINKEY` becomes `RIDEV_NOHOTKEYS`; the cooperative-level fix.
- [re/quick-wins.md](re/quick-wins.md) -- skip intro, custom FOV, wheel zoom, freecam extras, slider typing.
- [re/cutaway.md](re/cutaway.md) -- cut-away walls are `fader_entity` (alpha and state fields) driven by Auto-Buzzer `area_switch`.
- [re/free-move.md](re/free-move.md) -- first person and free movement via the cvars `g_fps` and `g_rpg`; `+0xB1C` gates all track layers; ladders, Rescue falls.
- [re/ai-offtrack.md](re/ai-offtrack.md) -- monster AI against an off-track Duke: the seen flag gates waking; the levers.
- [re/mouse-lock.md](re/mouse-lock.md) -- clicks dead in other programs after leaving the game: cause is dinput8's exclusive-mouse raw-input row `NOLEGACY|CAPTUREMOUSE`; fixed by a shared engine mouse (verified).
- [re/queen-burst.md](re/queen-burst.md) -- Queen phase-2 one-burst kill: the running swipe hits every think, so hits scale with fps; `hurtlog` and the Fix Queen swipe switch.
- [re/areas-and-secrets.md](re/areas-and-secrets.md) -- a part's area graph from `world.scr`, per-secret state in memory and in `state/<map>.msv`, nukes, `area_teleport` mazes.
- [re/explodables.md](re/explodables.md) -- explodable walls are `breakable_adv`, in the `t=4` list; broken when `+0x2E0 == 0`.
- [re/puzzle-e05p02.md](re/puzzle-e05p02.md) -- e05p02_a01 floor-plate puzzle: fixed order Yellow, Blue, Red; the fields the overlay reads.
- [re/puzzle-e07p03.md](re/puzzle-e07p03.md) -- e07p03_a01 cog buttons: fixed order switch_1, switch_2, switch_3; state fields.
