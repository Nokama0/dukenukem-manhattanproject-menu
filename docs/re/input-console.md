# Input and console registry (prism3d.exe / duke_base.dll / sys/input.dll)

The console is fully usable through exports: cvars are a linked list
(`cvar_get_list` + next at +0xC4), commands run with `cmd_execute(const char*)` on the main thread.
Mouse deltas already reach the impulse system (triggers `mouseleft/right/up/down`), and the free
camera turns with impulses 201-204. The stock `input.cfg` never binds the mouse, so the camera flies
keyboard-only. The player reduces every impulse to a digital bitmask, so mouse look for the player
cannot come through impulses. To block game input under a panel, use `inp_set_active(0)` (see section 4).

All addresses: prism3d.exe (base 0x400000), duke_base.dll / input.dll / p3shared.dll (base 0x10000000).
The binaries are identical across Zoom Classic, Zoom Enhanced and Steam.
Method key: [D] decompile read, [X] xref, [S] string, [A] disassembly, [C] shipped config file.
Full name list: `dumps\console-registry.txt` in the Ghidra project folder. Trigger (key) names:
`dumps\input-triggers.txt`. Ghidra scripts used: `DumpConsoleRegistry.java`, `DecompileCmdHandlers.java`,
`XrefReport.java`, `DecompileAroundSites.java`.

## 1. Console registry

### cvar_t (0xD0 bytes)
Established from the cvar_t methods in p3shared (`set` 0x1000a7d0, `reset` 0x1000a6c0, `ptr` 0x1000a840,
`is_read_only` 0x1000a9a0, `link` 0x1000a980, `print` 0x1000a710) [D]. The same size appears in
`uset`'s `mem_sys_alloc(0xd0)` (prism3d!0x4245b0) and in the 0xD0 stride of the static cvars in duke_base [D].

| off | field |
|---|---|
| +0x00 | `char name[0x20]` |
| +0x20 | `char default[0x40]` (static initialiser; +0xB8 points here) |
| +0x60 | int kind: `toggle` refuses when it is non-zero (prism3d!0x425d60) [D] |
| +0x64 | `bool (*on_change)(cvar_t*, const char* newval)`: `set` stores only when it returns non-zero [D] |
| +0x69 | byte ptr/read-only mode (`cvar_t::ptr` sets it; `is_read_only` returns it) |
| +0x6C | int hash = sum of the name's (signed) chars (prism3d!0x406590) |
| +0x70 | byte modified; +0x71 byte "runtime-owned" (the `register` 2nd argument) |
| +0x72 | `char value[0x40]` (`set` copies at most 0x3F chars) |
| +0xB4 | `char* value` (points to +0x72); +0xB8 `char* default` |
| +0xBC | `cvar_t* link` (forward to this cvar); +0xC0 linked-from |
| +0xC4 | `cvar_t* next`; +0xC8 prev |

**Reading a value** (the engine's own pattern at prism3d!0x406740 and 0x426b20 [D]): follow +0xBC
until it is null, then use `*(char**)(cv+0xB8)` if byte +0x69 is set, else `*(char**)(cv+0xB4)`. The value
is always a string: readers such as duke_base's fly camera call `v_atof(*(char**)(cv+0xB4))` on every
read (duke_base!0x100739e0 [D]). **Writing:** `cvar_t::set(const char*)` (p3shared export, thiscall), or
`cmd_execute("uset name value")`.

**Registry:** `cvar_register_variable(cvar_t*, bool runtime)` (prism3d!0x406660 [D]) keeps one
doubly linked list, head `0x480d90`, pushed at the front (prism3d!0x4065c0). There is no hash table: the
+0x6C hash only speeds up the strcmp. If the name already exists:
- the old entry was created at runtime (by `uset` from the config, before the module loaded): the old
  value is copied in and the old entry is freed;
- the old entry is static: `cvar_link(old, new)` and the new one is **not** inserted into the list.

`cvar_get(name)` (0x406510) walks the list. `cvar_get_list()` (0x4065b0) returns the head
(`A1 90 0D 48 00 C3`) [D][A].

**Enumerate cvars:** `for (cv = cvar_get_list(); cv; cv = *(cvar_t**)(cv+0xC4))`. Resolve each value
through the +0xBC link. The engine's `cvars` command does exactly this walk (0x4260e0 [D]).

### Commands
- `struct cmd_t { const char* name; void (*handler)(void); int hash; cmd_t* next; }` (16 bytes).
- `cmd_register_command(cmd_t*)` (0x41a7c0 [D][A]) pushes onto a singly linked list at head `0x49bf5c`.
  That head is **not exported**; duplicate names are ignored.
- Handlers take no arguments. They read `cmd_argc()` / `cmd_argv()` (exports 0x41a930/0x41a920,
  globals 0x49bf50/0x49bf54), which are set just before the call.
- `bool cmd_execute(const char* line)` (0x41a610 [D]; all duke_base call sites pass one string, e.g.
  `"camera camera"` at duke_base!0x100e5b30):
  1. tokenises the line with `arg_t::separate` and echoes it through `message()`;
  2. looks up the command;
  3. if there is none, tries `name [value]` as a cvar print/set (0x406770);
  4. otherwise raises `error()`.
- `cmd_execute_block(text, len)` runs a text block line by line (0x41a730).
- `cmd_add(line)` (0x41a880) queues up to 100 lines of 127 chars at 0x498d50. The queue is drained
  at the end of the frame by 0x41a8e0, called from `engine_input_frame_update` (0x425bc0, renamed) [D].
- **Thread:** argc/argv are plain globals and nothing is locked, so call these from the game's main
  thread only. That thread runs the process loop (0x405ff0: process updates, then
  PeekMessage/DispatchMessage, so the WndProc runs on it too) and `engine_input_frame_update`, which
  calls `rend_begin_frame` / `rend_end_frame` / `rend_swapbuffers` [D]. A hook on the renderer's
  frame functions or a WndProc subclass is therefore on the right thread. A thread-id log agrees at runtime: all
  of it runs on one thread (render.md section 6).

**Enumerate commands through exports only:** register your own static `cmd_t` with
`cmd_register_command`. It becomes the list head, and its `next` (+0xC) points to every command
registered earlier (everything, because all modules register in DLL_register / init). This also gives
the panel console commands of its own. Fallback: the `cmds` command prints the list (0x4261d0).
For a direct read, the head address is the imm32 at `cmd_register_command+0x21` (`8B 15 <5C BF 49 00>`).

### sys / srv variable registries
- `sys_register_var_ptr(name, ptr)` (0x4028d0) and `sys_register_var_data(name, data, size)` (0x402990)
  append to the growable array at `0x476448` of `{void* p; int size (-1 = ptr); char hash; char name[]}`.
- `void* sys_get_var(name, void* default, int* size_out)` (0x402b60) returns p, or the default
  (size_out = -2 when not found) [D].
- **Only one entry exists:** `"game_globals"`, registered by duke_base DLL_register at 0x100de517: it
  first tries `sys_get_var` (name pointer at 0x10178280 -> "game_globals"), otherwise allocates 0xB4
  bytes and registers them [A]. The registry scan found no other caller in any module (0 sites in
  prism3d, gl, dx8, input, p3shared, client, skmod).
- So `sys_get_var("game_globals",0,0)` hands a mod the game-globals object with no offset. Fields seen
  in the handlers [D]:
  - +0x08 = game process (+0xC8 state, 7 = in game; +0xEC current world/map);
  - +0x10 = session data, whose +(+0x20)+0x24 byte is the "cheated" flag set by give/camera/overkill.
- `srv_get_var` (0x420b10) searches a per-server table at `server+0xDC`. `srv_register_var_*` has no
  caller in any scanned module [X], so it is unused.

## 2. Inventory (full list in dumps/console-registry.txt)

Totals: prism3d 34 commands + 54 cvars (2 dynamic sites unresolved: uset-created cvars at 0x424a31 and
0x424b50); duke_base 25 commands + 73 cvars + 1 sys var; gl 23 cvars; dx8 15 cvars; input, client,
skmod and p3shared register none [script]. Handlers are renamed `cmd_<name>` in Ghidra.

**Game commands (duke_base, handlers 0x100e5a50..0x100e5da0):**
- `give <item> [n]` calls player vtable +0x250 = `player_give_item` 0x1007dba0 [D][S]. Items:
  `all` (ammo+health+keys+weap+bomb), `ammo`, `health n`, `forcefield`, `berserk`, `jetpack`, `keys`,
  `weap n`, `nuke n`, `bomb n` (diffused bombs), `power`, `kill n`, `secret n`. Each prints
  "... provided. (cheater!)". `give life` in cheats.cfg matches no item.
- `kill` kills the player; `overkill` kills every monster (`srv_kill` over a spawn_search).
- `weap n`, `use`, `pause`, `camera <entity>` (section 3), `letterbox n`, `map <name> [difficulty]`,
  `load`, `new [difficulty]`, `save`, `quicksave`, `quickload`, `restart`, `skip` (cutscene),
  `credits`, `nuke_crash` (shows `menu_nuke`), `screenshot`, `record`/`play` (demo), `help`, `escape`,
  `exit`, `quit`.

**Engine commands:**
- config: `set`/`eset` (entity values via `spawn_set_value`), `uset` (cvar create/set), `toggle <cvar...>`,
  `exec`, `echo`;
- binds: `bind_cmd <key> "<cmd>" [always]`, `bind_imp <key> "<impulse>"`, `unbind_cmd`, `unbind_imp`,
  `imp_alias <n> <name>`;
- listings: `cvars`, `cmds`, `ps` (entity list), `maps`, `gps` (process list);
- engine: `signal <entity> <n>`, `wm`, `ui`, `r_restart`, `s_restart`, `s_info`, `mm_*`, `mount`,
  `umount`, `vfs_drivers`, `sleep`, `wakeup`, `hw_flush`, `ver`, `debug`.
- `debug` is an empty stub: 0x401260 is shared by several exports, see Other.

**Interesting cvars (duke_base unless noted):**
- cheats: `g_p_god`, `g_p_hover`;
- movement: `g_p_speed` 300, `g_p_jump` 600, `g_gravity` 1800, `g_p_jet_*`, `g_p_boost*`;
- camera: `g_cam_fov` 50, `g_cam_near`/`far`, `g_cam_fly_speed` 1000, `g_cam_fly_spin` 10, `g_cam_*`
  springs, `g_off_*` (side-scroll camera offsets);
- display/debug: `g_hud`, `g_map_info`, `g_debug`, `g_minicon`, `g_hint_*`, `g_dval0..9`; `g_fps` and `g_rpg` are not
  display cvars but the engine's hidden first-person / free-move player modes ([free-move.md](free-move.md));
- engine: `r_fps`, `r_timings`, `r_stats`, `r_mode`, `r_device`;
- mouse (engine): `m_sensitivity`, `m_invert`, `m_filter`, `i_grabpointer`, `i_joystick1/2`, `i_feedback`.

No timescale cvar exists.

**Shipped cheat binds:** `db_e01.zipfs:cheats.cfg` [C] binds F7 `camera camera`, F8 `camera player`,
G/H/J/F/L `give ...`, K `kill`, I `toggle g_p_god`, P `pause`, M `toggle g_map_info`, and MOUSELEFT/
RIGHT/UP/DOWN -> turnleft/turnright/pitchup/pitchdown. **init.cfg only runs `exec input.cfg` and
`exec config.cfg`** [C], so none of these are active by default.

## 3. Input path

**Devices** (input.dll, device class "dinput", vtable 0x10006188 [D]):
- `DirectInput8Create` (0x10002690);
- mouse: `c_dfDIMouse`, `SetCooperativeLevel(hwnd, 5 = EXCLUSIVE|FOREGROUND)`, buffered, 128 entries
  (0x100011a0 / 0x10001270);
- keyboard: `SetCooperativeLevel(hwnd, 0x16 = NONEXCLUSIVE|FOREGROUND|NOWINKEY)`, buffered, 20 entries
  (0x10001500 / 0x100015e0);
- joysticks with force feedback.

`set_active(bool)` (vtbl+0x1C, 0x10002790) acquires or unacquires every device. `poll` (vtbl+0x20,
0x10001160) reads `GetDeviceData` and turns it into `inp_event()` records (0x34 bytes; type 0/1 key
up/down, 2/3 mouse button, 5 relative mouse, 6/7 wheel, 8-10 joystick).

The input init at prism3d!0x406cb0 loads `sys/input` only when `i_grabpointer`="1" and the OS is not
NT 5.0-5.4 [D]. Otherwise it sets 0x48ac60=1, and `prism_wndproc` (0x426340, renamed) builds the same
events from WM_KEY*/WM_CHAR/WM_MOUSE* messages. In DirectInput mode the WndProc builds no key or mouse
events.

**Per frame** `engine_input_frame_update` (0x425bc0) runs `inp_reset_impulses()`, then `inp_process()`,
then `rend_begin_frame()` [D].
- `inp_process` (0x407b80) polls the devices and converts events to triggers (0x407290).
- There are 333 triggers of 0x240 bytes at 0x43d528:
  - index 0-255 = DIK scan code;
  - 256 mousex, 257 mousey, **258 mouseleft, 259 mouseright, 260 mouseup, 261 mousedown**, 262+ mouse1-8,
    270/271 wheel;
  - 280+ joystick.
- Each trigger has `+0x30 scale`, `+0x34 value`, `+0x38 down`, `+0x40 bound command`, `+0x44[4]` bound
  impulses.
- A relative mouse event (type 5) moves the engine's software cursor (0x486170/74 px, normalised through
  `inp_cursor_set`/`inp_cursor_info`). It also writes `|delta| * m_sensitivity` into triggers 258-261,
  with Y swapped by `m_invert`; `m_filter` averages two samples.
- The cvars reach those globals through their callbacks: `m_sensitivity` -> 0x43cf74 (0x406ac0),
  `m_invert` -> 0x48ac64 (0x406ae0), `m_filter` -> 0x48ac68 (0x406b00). **That is the only reader** [X].
- With 0x43cf70 ("game input enabled") set, each trigger adds its value to its bound impulses.
  `FUN_004068f0` accumulates and clamps the value to the impulse's max.
- On a press edge the bound command runs if game input is enabled or the bind carries the "always"
  flag (e.g. `bind_cmd ESC "escape" 1`).

**Impulses** (255 × 0x54 at 0x480d98: +0x10 `max[4]`, +0x20 `value[4]`, +0x40 alias, +0x50 bind count):
`inp_impulse_value(i, ch)` returns `value[ch]`, and the values are cleared every frame. Names from
input.cfg [C]:
- 101 left, 102 right, 103 up, 104 down, 105 jump, 106 attack, 107 kick, 108 pipebomb, 109 zoom,
  110 look, 111 prev, 112 next;
- 201 turnleft, 202 turnright, 203 pitchup, 204 pitchdown.

Default keys are the **arrow keys**, LALT attack, LCTRL jump, SPACE kick, RCTRL look. Nothing is bound
to W/A/S/D by default. The live `duke/base/config.cfg` (generated by the launcher) holds the WASD binds.

**Player** (duke_base `FUN_1007d250`, vtable slot at 0x10136bfc [X][D]) reads 101-112 with
`inp_impulse_value(i, ch)` and turns every non-zero value into one bit of the mask at `player+0xE00`:
1 left, 2 right, 4 jump, 8 up, 0x10 down, 0x20 attack, 0x40 kick, 0x80 pipebomb, 0x100 zoom,
0x200 look, 0x400 prev, 0x800 next. It `net_send`s the mask when it changes. **Analog magnitude is
discarded, and the player never reads 201-204.** Consumers of the mask: `FUN_1008a700`
(bits 1/2/8/0x10), `FUN_1009a460`, `FUN_1007e280`, `FUN_10099440` and 13 functions without names
(41 references to +0xE00). `FUN_1007e7c0` sends position +0x18C and angles
+0x1A8 over the network.

**Free camera `camera <name>`** (duke_base 0x100e48b0 [D]):
1. finds the player (spawn_search flag 0x200) and the entity named `<name>` in the current world;
2. calls `wm_input(0, ent)`, which makes that entity the input receiver (wm slot 0);
3. calls `FUN_1008d590(player, ent)` and `FUN_100e55d0(...)` to switch the view (exact semantics
   not read);
4. sets the cheated flag;
5. calls `inp_set_mouse_state(name == "player" ? 0 : 1)`.

`camera camera` needs an entity named "camera" in the level. `pause` also issues `camera camera` /
`camera player` (duke_base 0x100e5b30), but only when `player+0xB34` holds a player, which only `camera player`
sets (freecam-pause.md section 1). The freeze itself is `process_sleep` on each game server.

Fly think `FUN_100739e0` (vtable ref 0x101362d4) and a second class `FUN_1008c750` (0x10137304):
- translation from impulses 101-104 (the second class also reads 111/112) × `g_cam_fly_speed` × dt
  along the camera axes;
- rotation `angles(+0x1A8) += (204-203)` and `(+0x1AC) += (201-202)` (**analog**), then
  `set_transforms_dirty`.

So the camera supports mouse look; only the binds are missing. Fix, with no code: run
`bind_imp mouseleft "turnleft"`, `... mouseright "turnright"`, `... mouseup "pitchup"`,
`... mousedown "pitchdown"` through `cmd_execute`. The direction of the Y axis is not confirmed; flip it
with `m_invert`. `FUN_1007fc20` (vtable ref 0x101365c8) is a player-side state that also reads 101-104
and 201-204. Which state it is (hover/fly?) is not identified.

**Mouse look for the player:**
- Injecting through `inp_impulse_set(i, ch, v)` (accumulate + clamp) after `inp_process` in the same
  frame works mechanically. The frame order puts `rend_begin_frame` right after `inp_process`
  (0x425bc0), so a hook there is the injection point. But it can only press or release the digital
  buttons.
- Real mouse aim must write the player's aim or facing state (angles +0x1A8 or whatever the look/aim
  code keeps) after the player think.
- The shipped mouse aim works around this: it drives the engine's single analog aim channel, the eased
  "Bip01 Spine1" bend `player_spine_bend` duke!0x1007c7f0, and writes the facing yaw (mouse-aim.md). The engine's
  "animation blending for directional keyboard looking" code is among the +0xE00 bit-8/0x10/0x200 consumers above.

## 4. Blocking game input under a panel

- **How the engine does it:** showing a UI window (`ui_wshow`, 0x419870 -> 0x423600) that lacks window
  flag 4 (+0x208) increments `ui_mgr+0x440`. On 0 -> 1 it calls `FUN_00407df0(0)`
  (`8A 44 24 04 A2 70 CF 43 00 C3`), which clears the "game input enabled" byte 0x43cf70; hiding the
  window reverses it (0x423430 / 0x423b80) [D][A]. Effects: impulses are not fed, non-"always" binds
  are suppressed, and keys go to the UI key queue (0x486180). This is how the console and menus block
  gameplay. That setter is not exported.
- **Recommended for Nuke Panel: `inp_set_active(0)` + `inp_reset_impulses()` on open, and
  `inp_set_active(1)` on close.** Both are exports. `inp_set_active(0)` (0x407d60):
  - clears 0x43cf71, so key-down events are ignored and even "always" binds cannot fire;
  - unacquires every DInput device, which releases the exclusive mouse;
  - clears every trigger's down flags and the event counters;
  - when `mouse_state == 1`, restores the cursor position and calls `ReleaseCapture` + `ShowCursor(1)`.
- **Caveat:** `prism_wndproc` calls `inp_set_active(1)` on WM_ACTIVATE, WM_ACTIVATEAPP and
  WM_EXITMENULOOP. Re-assert it every frame while the panel is open, or filter those messages in the
  subclass.
- **Window messages:** in DirectInput mode the engine ignores keyboard window messages. It handles
  WM_ACTIVATE/ACTIVATEAPP, WM_CLOSE (-> `cmd_execute`), WM_SYSCOMMAND (blocks SC_SCREENSAVE/
  SC_KEYMENU), WM_WINDOWPOSCHANGED, WM_ENTER/EXITMENULOOP and WM_MOUSEWHEEL (both input modes) [D]. The DInput
  keyboard is non-exclusive, so WM_KEYDOWN/WM_CHAR still reach the window (the loop calls
  TranslateMessage). A WndProc subclass therefore sees every key the panel needs, and the game
  imports no GetAsyncKeyState or GetKeyState polling [X imports].
- **Is the console open? [A][D]** The console is the UI window
  `console:system_01` (config.cfg: `` bind_cmd ` "ui t console:system_01" 1 ``); the `ui` command 0x422740
  splits it into name `console`, class `system_01`. `ui_wget(cls, name, visibleOnly)` 0x4198e0 (cdecl, plain
  RET) runs thiscall 0x423770 on the UI manager `[0x4a1e48]`: class vs window+0x1e8, name vs window+4, and
  with visibleOnly=1 only the shown list (+0x3e0; hidden windows sit on +0x3f4). `ui_wfocused()` 0x4198d0
  returns `[[0x4a1e48]+0x41c]`, the focused top-level window; hiding the focused window (0x423430) moves it
  to the next shown one. Window +0x1e2 is the visible byte (0x41a130). input.cpp yields every hotkey while the
  console is shown and focused (or nothing is focused): `console_takes_keys` in ownership.h. It reads the
  manager slot out of `ui_wfocused`'s own bytes (`A1 <slot> 8B 80 1C 04 00 00 C3`, cross-checked against
  `ui_wget`'s `8B 0D <slot>`) and skips the calls while it is null. The focus value while
  typing is not confirmed (see Open questions).
- dxwrapper does not wrap DirectInput in the Enhanced build (`EnableDinput8Wrapper=0`,
  `Dinputto8=0` in `dxwrapper.ini` [C]), so input.dll talks to the system dinput8.dll.

## 5. Mouse cursor

- `inp_set_mouse_state(1)` (0x406f40 -> 0x406e90) calls `SetCapture`, `ShowCursor(0)` and recentres
  the cursor. `inp_cursor_reset_lazy` then recentres it each frame from the main loop (0x405ff0).
  `inp_set_mouse_state(0)` (0x406f00) restores the position, calls `ReleaseCapture` and `ShowCursor(1)`.
- The game sets state 1 only for the free camera and 0 for menus and play (duke_base 0x100e48b0,
  0x100e4f20).
- `ClipCursor` is never imported; `inp_cursor_reset` calls `SetCursor(NULL)`. The class cursor is
  resource #1 of prism3d.exe (`LoadCursorA(hInst, 1)`, 0x426b20).
- **The DInput mouse is EXCLUSIVE|FOREGROUND and acquired whenever the window is active**, regardless
  of mouse_state. DirectInput then hides the OS cursor and the window gets no mouse messages, as Microsoft
  documents for an exclusive mouse. The panel runs the engine mouse shared (`NONEXCLUSIVE|FOREGROUND`) and holds
  the cursor itself (mouse-lock.md "The fix").
- With the stock exclusive mouse the Windows cursor is usable for the panel only after `inp_set_active(0)`. The panel should
  then answer WM_SETCURSOR with an arrow: resource #1 may be blank. Without unacquiring,
  a panel could draw its own pointer from `inp_cursor_info()` (normalised -1..1, y up), but clicks
  would still reach the game.

## Other findings

- `rend_line` (and `rend_display_modes`, `spawn_replicate_entity`, `sys_unload_libs` ...) are all
  exported at 0x401260, an empty `ret` stub [D]. **ESP cannot draw through `rend_line`.**
- Ghidra edits (prism3d): created and renamed `prism_wndproc` 0x426340,
  `engine_input_frame_update` 0x425bc0 and the `cmd_*` handlers. In duke_base: `cmd_*` handlers,
  `player_give_item` 0x1007dba0, and unnamed functions at 0x100739e0, 0x1007d250, 0x1007fc20,
  0x1008c750, 0x100999d0.

## Open questions
- The semantics of `FUN_1008d590` and `FUN_100e55d0` in `camera <name>` (view switch).
- Which player state `FUN_1007fc20` is (hover/fly?), and the direction of the Y axis for mouse-bound pitch impulses.
- The focus value of the console while typing (the log line `input: console takes typing` settles it).
- Whether the blank class cursor (resource #1) needs an arrow from WM_SETCURSOR after `inp_set_active(0)`.
