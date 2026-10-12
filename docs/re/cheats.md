# Cheats and hidden console commands

Every cheat found in the game is a console command or cvar; no typed key-sequence cheat was found (a reading result, not a proof of absence). The console
opens with the backtick key and needs no unlock. The gameplay cheats are `give` (13 items), `g_p_god`, `g_p_hover`,
`g_fps`, `g_rpg`, `kill`, `overkill`, `camera`, `pause` and `map` (warp, including 28 developer test maps). Online lists
miss most of them. `give life` (in the shipped `cheats.cfg`) matches no item, and `g_ignore_duke` is read by no code.

Verified in game: `give all` (all weapons, full ammo, 200 health, keys, babes) and
`g_p_hover 1` (flying, arrow keys raise and lower Duke).

Registry: 59 commands and 174 cvars over five modules (appendix). Binary: Zoom Enhanced (the same build in the Classic and
Steam editions). Base: duke_base 0x10000000 (duke!), prism3d 0x400000.

Tags: **read** = handler decompiled; **name-only** = not read. Evidence: **[D]** decompile, **[A]** disassembly,
**[X]** xref or byte/instruction scan, **[S]** string, **[C]** shipped or engine-written config file. All Ghidra
work was done through the MCP.
"Archived" means the engine saves the cvar to `duke/base/config.cfg` at quit. The writer prism!0x4067e0 stores a
cvar when its byte +0x68 is set and +0x71 (runtime-created) is clear [D]. The archived set below is read from the
config.cfg that the engine wrote [C]. Related: [input-console.md](input-console.md) (registry, binds),
[free-move.md](free-move.md) (`g_fps`, `g_rpg`), [pickups.md](pickups.md) (grant types).

## 1. Opening the console

- **Key: `` ` `` (backtick, the key under Esc on US layouts).** `ui_create_system_windows` duke!0x100e0b30 creates the
  window `console:system_01` and runs `` bind_cmd ` "ui t console:system_01" 1 `` [D][S 0x10173698]. This happens on
  every launch, so the bind exists without any cvar, switch or launcher option. The trailing `1` ("always") lets it
  open in menus too.
- The stock `input.cfg` does not bind it. The bind reaches `config.cfg` only because the engine saves all binds at
  quit [C].
- **`exec cheats.cfg`** runs the developers' bind file shipped in `db_e01.zipfs` [C], which nothing executes by
  default. It binds:
  - F2 `toggle r_timings`, F3 `toggle r_stats`, F4 `toggle g_debug`;
  - F7 `camera camera`, F8 `camera player`;
  - G `give all`, H `give ammo`, J `give jetpack`, F `give forcefield`, L `give life` (no effect), K `kill`;
  - I `toggle g_p_god`, P `pause`, M `toggle g_map_info`;
  - the mouse axes to the fly-camera turn impulses.

  These binds are saved to `config.cfg` at quit, so they persist.
- `toggle <cvar>...` writes "1" when the value parses as 0, otherwise "0" (prism!0x425d60 [D]). `uset <cvar> <value>`
  sets a cvar, and `<cvar> <value>` alone does the same (input-console §1).

## 2. Gameplay cheats

"Level" means a game world with a player must be loaded. In the handlers this is `game+0xEC != 0` plus a player found
by `spawn_search`, or the input entity (`wm_get_input(0)`) being the player (flags `+0x48&2`, `+0x4C&0x100`).

**The cheated flag.** `give`, `camera` and `overkill` set a per-session "cheated" byte (`session+[+0x20]+0x24`). The
first time, they print one of three taunts at random (`@@cheat001..003@@`, for example "Come on! Duke doesn't need
to cheat!" [S local.def]). `give` sets the flag on every call, even a bare or failed one [D 0x100e5b50]. What else
reads the flag was not traced.

### God and health

| Cheat | Syntax | Effect (evidence) | Default | Archived | Level |
|---|---|---|---|---|---|
| `g_p_god` | `g_p_god 1` / `toggle g_p_god` | **read.** The player damage handler duke!0x10078f10 reads `player_cvar_value(0x15)` (= `g_p_god`). When it is > 0, damage takes no health, starts no pain and no death [D]. Not protected: damage type 5, which kills outright before the check (0x10078f10 tail [D]; which hazards use type 5 is not established); `kill`; and view shake still plays. | "0" | **no** | no (applies on the next hit) |
| `give health [n]` | n default 100 | **read.** Grant type 1 (`player_add_health`) of n [D 0x1007dba0, 0x100793a0]. | | | yes |
| `give forcefield` | | **read.** Timed power +0x14 := 30 s. The damage handler ignores damage while it is > 0 [D 0x10079dd0, 0x10078f10]. | | | yes |
| `give berserk` | | **read.** Grant type 4: timed power +0x18 := 30 s and the voice "double_damage" [D 0x100799c0]. This is the **Double Damage** power-up (pickups.md §3); the console calls it "berserk". | | | yes |
| `give power` | | **read.** Grant type 10: stats `+0x60 += 2`, `+0x64 += 5`, `+0x68 += 1`, `+0x6C += 10` [D 0x10079a10]. The game grants the same thing when the nukes collected reach the episode's `nukecount` (0x10079b90). The meaning of the four fields is not established. | | | yes |
| `kill` | | **read.** Sets health to -100 and runs the death routine (`FUN_1007a2d0` → 0x10079e70) [D]. It ignores `g_p_god`. | | | yes |

### Weapons, ammo and items (`give <item> [n]`)

The handler `cmd_give` duke!0x100e5b50 calls player vt+0x250 `player_give_item` 0x1007dba0 [D]. Item names are
compared exactly and case-sensitively. Every grant goes through `powerup_give_send`, then the receive switch
0x100793a0 [D].

| Item | Effect (read) |
|---|---|
| `all` | Runs `ammo`, `health`, `keys`, `weap` (all) and `bomb` in that order. |
| `ammo` | +500 to each of the three ammo pools (bullets, C5/pipebombs, GLOPP), capped by the pool maximum (0x100796e0). |
| `weap [n]` | n = pickup `gun_type`: 0 golden eagle, 1 assault rifle, 2 GLOPP ray, 3 pulse cannon, 4 PRPG, 5 shotgun (0x100797b0, pickups.md). With n missing or outside 0..9, it gives 0..9, which covers all six. **n 6..9 grant nothing** (no case in the receiver), but the message still prints. n = 7 is further gated on `session+0x1C4 > 0`. |
| `keys` | All three keys (`+0x42/+0x44/+0x46 := 1`, 0x10079d10). |
| `bomb [n]` | n (default 1) diffused bombs, `+0x54`. These are the level-exit objective count (0x10079ad0), shown on the HUD as rescued babes (each babe is strapped to a bomb). |
| `nuke [n]` | n (default 10) nuke symbols, `+0x48`. Reaching the episode's `nukecount` also triggers the `power` grant (0x10079b90). |
| `jetpack` | Jetpack fuel `+0x10 := 20` (0x10079e00). |
| `forcefield`, `berserk`, `health`, `power` | See the table above. |
| `kill [n]` | Adds n (default 1) to the level's kill tally `+0x50`. It kills nothing. |
| `secret [n]` | Adds n (default 1) to the secrets tally `+0x4C`. |
| `life` | **No match.** Nothing is given, but the cheated flag is still set. |

### Movement and camera

| Cheat | Syntax | Effect (evidence) | Default | Archived | Level |
|---|---|---|---|---|---|
| `g_fps` | `g_fps 1` | **read.** First person with free movement (player state 17): view from the eyes, body not drawn, track constraint off, arrows walk and strafe, turn impulses 201-204 (free-move.md §1c, §2 [A][D]). | "0" | **yes** (persists across sessions) | no |
| `g_rpg` | `g_rpg <height>` | **read.** Top-down free movement (state 16): the camera looks straight down from `<height>`, and Duke moves along world X/Z (free-move.md §1c [D]). It takes priority over `g_fps`. | "0" | **yes** | no |
| `g_p_hover` | `g_p_hover 1` | **read.** Hover. The gravity slot `FUN_100835d0` (in 4 player state vtables, including walk state 3) skips gravity while it is > 0 [D]. In `playerstate_move_update` 0x100855d0, the down key alone applies a downward move of 300·dt and the up key alone an upward move [D] (the upward magnitude is garbled in the decompile). Down no longer blocks walking. | "0" | **yes** | no |
| `g_p_speed` | `g_p_speed <v>` | **read.** Walk speed: the track move vector × `player_cvar_value(0)` [D 0x100855d0]. | "300" | no | no |
| `g_p_jump` | `g_p_jump <v>` | **read.** Jump velocity term (`player_cvar_value(1)`, with `g_p_boost` (2) in the same formula) [D 0x100855d0]. | "600" | no | no |
| `g_gravity` | `g_gravity <v>` | **read.** World gravity: force (0, -g, 0) × object mass factor +0x2D8 × world percent, for every object that uses `FUN_100e7e00` [D]. 0 = no gravity. | "1800" | no | no |
| `camera <entity>` | `camera camera` / `camera player` | **read.** Makes the named entity the input receiver (`wm_input`) and switches the view. `camera camera` = free-fly camera (the level's `actor_camera`, camera-culling.md); `player` returns. The mouse is captured unless the target is `player`. Sets the cheated flag [D 0x100e5b70]. | | | yes |
| `pause` | `pause` | **read.** Developer pause: toggles the game pause (`FUN_100e4a20`: `process_sleep`/`process_wakeup` on each game server, flag +0x53C). It runs `camera camera` only when the player has input **and** `player+0xB34` holds a player (set by an earlier `camera player`; 0 in normal play); unpausing runs `camera player` likewise [D 0x100e5b30 -> 0x100e4160; freecam-pause.md section 1]. | | | yes |
| `g_cam_fly_speed`, `g_cam_fly_spin` | | **read** (input-console §3): fly-camera translation speed and turn rate. | "1000", "10" | **yes** | |

### Level and warp

| Cheat | Syntax | Effect (evidence) | Level |
|---|---|---|---|
| `map` | `map <name> [difficulty]` | **read.** Lower-cases the name and checks that `maps/<name>` exists in the VFS. It stores the name at `game+0x540` and the difficulty (0..2, default 1) at +0x544, then hides the console [D 0x100e5a70]. `game_proc_handler` sees the name and enters state 5 (loads it) [A 0x100e0d5a]. No argument prints the current map. **129 maps** ship: e01p01_a01 ... e08p03_a04, the `_rNN` variants, sky/stats/intro maps, and **28 developer test maps `demo_*`** (`demo_room`, `demo_babe`, `demo_sentinel`, ... from `db_demo_maps.zipfs` [C]). The editor help calls them example maps. | no (works from the menu) |
| `new [difficulty]` | | **read.** Starts a new game, difficulty clamped to 0..2 [D 0x100e5ab0]. | no |
| `restart [1]` | | **read.** Restarts the level. With `1`, it skips the confirmation and reloads the checkpoint through `load ... r` [D 0x100e5d20]. | yes |
| `skip` | | **read.** Ends the running cutscene (`game+0x5A8`) when the game is in state 7 [D 0x100e5bd0]. | yes |
| `quickload [1]`, `load <game> ...`, `save`, `quicksave` | | **read** (handlers dumped). `quickload 1` skips the confirmation. | |

### Enemies and AI

| Cheat | Syntax | Effect (evidence) | Default | Archived | Level |
|---|---|---|---|---|---|
| `overkill` | `overkill` | **read.** Runs `spawn_search` over list t=2 group 0x20 and calls `srv_kill` on every hit, i.e. removes the level's monsters. It refuses while `game+0x56C` is set. Sets the cheated flag [D 0x100e5d80]. | | | yes |
| `g_ignore_duke` | | **No effect found.** No code reads it: the only reference to its cvar object is the registration (pointer byte scan and xrefs [X]), there is no second copy of the name string, and prism3d has no such string [S]. | "0" | no | |

## 3. Debug and display (player-interesting, mostly name-only)

| Name | Kind | Notes |
|---|---|---|
| `g_hud` | cvar "1", archived | HUD style; the stock binds `-` = 2, `=` = 1 [C]. name-only |
| `g_map_info` | cvar "0", archived | Read by the HUD updater 0x100a13c0 [X]; effect not settled. cheats.cfg binds M. name-only |
| `g_debug` | cvar "0", archived | Polled twice a second (0x100e6eb0) and used by `game_track_place_camera` [X]. name-only |
| `g_minicon` | cvar "0", archived | Mini console ("mini console enabled [%d row(s)]" [S]). name-only |
| `g_cam_fov`, `g_cam_near`, `g_cam_far` | cvars 50/16/8000 | **read**: camera FOV/clip (free-move.md §2); Enhanced sets fov in `init_widescreen_fov.cfg` [C] |
| `g_p_boost*`, `g_p_stair`, `g_p_grab`, `g_p_aa/da/ag/dg`, `g_p_fall`, `g_p_hang1/2`, `g_p_jet_i/v/h/max`, `g_p_cam_vtol/htol` | cvars | Player tuning table entries 2-20 (duke!0x10163e80 [S]); name-only |
| `g_off_*`, `g_cam_*spring/damp`, `g_cam_speed`, `g_cam_accel_f/decel_f`, `g_cam_trans` | cvars | Side-scroll camera tuning; name-only |
| `g_dval0..9` | cvars, archived | Developer values; the nuke-grant code writes `g_dval6` (0x10079b90 [D]); name-only |
| `g_gore`, `g_gore_pass`, `g_effects`, `g_shadows`, `g_zoom_type`, `g_demo` | cvars, archived | name-only (`g_demo` = attract demo, quick-wins.md) |
| `r_fps`, `r_timings`, `r_stats` | cvars "0" | Frame counters; cheats.cfg F2/F3. name-only |
| `gl_wireframe`, `dx8_wireframe`, `r_wireframe` | cvars | name-only |
| `letterbox <n>` | cmd | **read**: n > 0 shows the cinematic bars and hides `game_view`; n <= 0 reverses [D 0x100e5d60] |
| `nuke_crash` | cmd | **read**: shows the UI window `menu_nuke` [D 0x100e5bb0] |
| `screenshot [w h name]`, `credits`, `help`, `escape`, `use`, `weap n`, `record`/`play <demo>` | cmds | **read** (dumped handlers): `use` sets player +0xEBE; `weap n` selects slot n (player vt+0x240) |
| `cvars`, `cmds`, `maps`, `ps`, `gps`, `ver` | cmds | Lists cvars / commands / map folders / entities / processes [D] |
| `set`/`eset <path> <value>`, `signal <entity> <n>` | cmds | Engine `spawn_set_value` / `spawn_set_pin` on entities; name-only beyond the call [D] |

## 4. Command-line and ini switches

Parsed by prism3d `host_main` 0x424c90 and `FUN_004246c0`/`FUN_00424aa0` [D]:

- **`-map <name> [difficulty]`** queues `map name [difficulty]` after the modules load: boot straight into any map,
  `demo_*` included.
- **`-lenv name=value`** creates or sets any cvar after `init.cfg` (and so after `config.cfg`). It wins over the
  saved config, for example `-lenv g_p_god=1`. Values up to 63 characters.
- `-env name=value` does the same but runs before `init.cfg`, so archived cvars are overwritten by `config.cfg`.
  Values are truncated to 3 characters (`v_stpncpy(..., 3)`).
- `-game <dir>`, `-cd <path>`, `-icon`, `-title`, `-host` set the matching upper-case cvars; `-log <file>`,
  `-nosound`.
- `-editui` loads `sys/uiedit` and sets `i_grabpointer` (developer UI editor).
- No switch or ini key enables or disables cheats. `prism3d.ini` holds only the game folder `duke`, and
  `duke/launch.ini` holds `ugc_dir`/`language` [C].

## 5. Open questions

- **Typed cheats:** none found. There are no code-like strings (search for cheat/god/noclip/give/iddqd-style words
  [S]), and duke_base imports nothing from user32. The `inp_trigger` readers are joystick polling, the bind lookup and
  three menu handlers comparing the key name with "Y"/"y" (yes/no dialogs) [X][A]. That is a reading result, not a
  proof of absence.
- `zoom/Manual.pdf` is scanned images. Only its credits page has text, so it could not be checked for an official
  cheat list. The Prism help file (`.chm`) is editor help, with no cheats.
- Unknown: which damage sources use type 5 (the hole in god mode); the four `give power` fields; what the cheated flag
  changes.
- `ds8.dll` and `loaders.dll` are not in the Ghidra project. The ds8 cvars come from strings [S] and config.cfg [C].
  The `skmod` and `client` registries did not resolve (no cvar strings [S]; `client` only mentions `r_screen_size`).

## Appendix: full registry

Source: the console-registry dump in the Ghidra project (static scan of every
`cvar_register_variable`/`cmd_register_command` call) plus ds8 [S]. "A" = archived. Defaults are the static
initialisers. Totals: **59 commands, 174 cvars (96 archived)**. input.dll, client.dll, skmod.dll, p3shared.dll and
loaders.dll register none. Two runtime sites in prism3d (0x424a31, 0x424b50) are the `-env`/`-lenv` creators (§4).

**prism3d.exe: 34 commands, 53 cvars (27 archived).**

- Commands: `mount`, `umount`, `vfs_drivers`, `gps`, `sleep`, `wakeup`, `imp_alias`, `mm_dump`, `mm_flush`, `mm_defrag`, `mm_sys`, `r_restart`, `hw_flush`, `s_restart`, `s_info`, `ui`, `wm`, `exec`, `echo`, `bind_cmd`, `bind_imp`, `unbind_cmd`, `unbind_imp`, `set`, `uset`, `ps`, `maps`, `cvars`, `cmds`, `ver`, `signal`, `eset`, `debug`, `toggle`.
- Cvars: `GAME` "", `sys_info` "<unknown>", `m_filter` "0" A, `m_sensitivity` "1.0" A, `m_invert` "0" A, `i_grabpointer` "1" A, `i_joystick1` "1" A, `i_joystick2` "0" A, `i_feedback` "1" A, `net_ping_interval` "1", `net_tick` "0.03" A, `r_cache_stats` "0", `r_mode` "640x480x16" A, `r_device` "dx8" A, `r_renderer` "default" A, `r_fullscreen` "1", `r_displayrefresh` "0", `r_gamma` "1.0", `r_tex_quality` "0" A, `r_tex_detail` "0" A, `r_tex_filter` "4" A, `r_sync` "0", `r_screen_size` "0", `r_stats` "0", `r_merge` "50", `r_hw` "<unknown>", `r_vendor` "<unknown>", `r_version` "<unknown>", `r_extensions` "<unknown>", `r_wireframe` "<unknown>", `r_acc_envmap` "0", `r_acc_tmatrix` "0", `s_device` "ds8" A, `s_reverse_stereo` "0" A, `s_music_volume` "1.0" A, `s_sfx_volume` "1.0" A, `s_music_scheme` "0" A, `s_sfx_scheme` "0" A, `s_balance` "0.5" A, `s_music_enabled` "1" A, `s_sfx_enabled` "1" A, `s_3d_emulated` "0" A, `s_mix_rate` "44100" A, `s_mix_depth` "16" A, `s_mix_channels` "2" A, `log` "1", `CD` "", `ICON` "", `TITLE` "Prism3D Engine", `HOST` "", `r_timings` "0", `r_fps` "0", `p_verbose` "".

**duke_base.dll: 25 commands, 73 cvars (29 archived).**

- Commands: `quit`, `map`, `load`, `new`, `record`, `play`, `exit`, `weap`, `kill`, `pause`, `give`, `camera`, `letterbox`, `overkill`, `escape`, `help`, `save`, `quicksave`, `quickload`, `restart`, `screenshot`, `use`, `credits`, `nuke_crash`, `skip`.
- Cvars: `g_debug` "0" A, `g_minicon` "0" A, `g_hud` "1" A, `g_gravity` "1800", `g_demo` "0" A, `g_gore` "1" A, `g_gore_pass` "" A, `g_hint_arrows` "1" A, `g_hint_level` "1" A, `g_map_info` "0" A, `g_rpg` "0" A, `g_fps` "0" A, `g_ignore_duke` "0", `g_shadows` "1" A, `g_effects` "2" A, `g_zoom_type` "0" A, `g_autoswitch` "1" A, `g_cd_drive` "" A, `g_p_speed` "300", `g_p_jump` "600", `g_p_boost` "400", `g_p_boost_up` "400", `g_p_boost_down` "300", `g_p_boost_sweet` "200", `g_p_stair` "15", `g_p_grab` "20", `g_p_aa` "1500", `g_p_da` "10", `g_p_ag` "1400", `g_p_dg` "2000", `g_p_fall` "600", `g_p_hang1` "700", `g_p_hang2` "100", `g_p_jet_i` "1600", `g_p_jet_v` "2100", `g_p_jet_h` "600", `g_p_jet_max` "400", `g_p_cam_vtol` "1.00", `g_p_cam_htol` "0.01", `g_p_god` "0", `g_p_hover` "0" A, `g_off_window` "0.4", `g_off_hspeed` "300", `g_off_htol` "0.4", `g_off_hfactor` "2", `g_off_hidle` "0.2", `g_off_uline` "0.8", `g_off_dline` "1.0", `g_cam_near` "16", `g_cam_far` "8000", `g_cam_fov` "50", `g_cam_vspring` "2.500", `g_cam_vdamp` "0.100", `g_cam_hspring` "5.000", `g_cam_hdamp` "0.100", `g_cam_rspring` "5.000", `g_cam_rdamp` "0.300", `g_cam_fly_speed` "1000" A, `g_cam_fly_spin` "10" A, `g_cam_speed` "600", `g_cam_accel_f` "4", `g_cam_decel_f` "5", `g_cam_trans` "1.5", `g_dval0` "0" A, `g_dval1` "0" A, `g_dval2` "0" A, `g_dval3` "0" A, `g_dval4` "0" A, `g_dval5` "0" A, `g_dval6` "0" A, `g_dval7` "0" A, `g_dval8` "0" A, `g_dval9` "0" A.

**gl.dll: 0 commands, 23 cvars (18 archived).**

- Cvars: `gl_hw` "<unknown>", `gl_vendor` "<unknown>", `gl_version` "<unknown>", `gl_extensions` "<unknown>", `gl_fullscreen` "1" A, `gl_gamma` "1.0" A, `gl_tex_quality` "0" A, `gl_tex_detail` "0" A, `gl_tex_filter` "4" A, `gl_displayrefresh` "0" A, `gl_sync` "0" A, `gl_wireframe` "0", `gl_ext_multitexture` "1" A, `gl_cva` "1" A, `gl_automip` "1" A, `gl_compress` "0" A, `gl_compress_s3tc` "0" A, `gl_ext_edge_clamp` "1" A, `gl_ext_env_add` "1" A, `gl_range_elements` "0" A, `gl_lod_bias` "1" A, `gl_acc_tmatrix` "1" A, `gl_acc_envmap` "1" A.

**dx8.dll: 0 commands, 15 cvars (12 archived).**

- Cvars: `dx8_fullscreen` "0" A, `dx8_gamma` "1.0" A, `dx8_wireframe` "0", `dx8_tex_quality` "0" A, `dx8_tex_detail` "0" A, `dx8_tex_filter` "4" A, `dx8_hw` "<unknown>", `dx8_version` "<unknown>", `dx8_pfog` "1" A, `dx8_rfog` "1" A, `dx8_tnl` "1" A, `dx8_lod_bias` "1" A, `dx8_tex_square` "0" A, `dx8_displayrefresh` "0" A, `dx8_acc_tmatrix` "1" A.

**ds8.dll: 0 commands, 10 cvars (10 archived).**

- Cvars: `ds8_3d_emulated` A, `ds8_balance` A, `ds8_mix_channels` A, `ds8_mix_depth` A, `ds8_mix_rate` A, `ds8_music_enabled` A, `ds8_music_volume` A, `ds8_reverse_stereo` A, `ds8_sfx_enabled` A, `ds8_sfx_volume` A.
