# Display fixes: borderless window, ultrawide HUD, floating text

(C) The floating pickup and damage text is a camera-facing 3D billboard with fixed 640x480 axes. The Enhanced
widescreen data replaced its script (`scr_actor_text.sc`) with a screen-style layout. Because child widgets are only
translated, never scaled, that script shifts the text box right by `1 - 640/(480*aspect)` of the half-width (0.44 at
21:9, 0.25 at 16:9, 0 at 4:3). It also squeezes the glyphs to 56 % width. The menu mounts the stock 4:3 script above
`db_wide`; no code. (B) The HUD maps its `csystem` onto the whole screen and sizes text by the same numbers, so a
`csystem` width of `768*aspect` is undistorted by construction; at 3440x1427 nothing in the HUD, the menus or the
letterbox looks stretched, so the HUD needs no fix. (A) In windowed mode the engine never tears the video mode down
and never minimizes on alt-tab. Borderless: stay windowed at desktop size and have our WndProc subclass rewrite the
style to the `WS_POPUP` values the engine itself writes for fullscreen. The first video init comes up borderless at
launch (section A "At creation"); the `rend_restart` path stays for mid-session flips and as the fallback.

Tags: **[D]** decompile, **[A]** disassembly, **[X]** xref/vtable read, **[S]** string or data file. Addresses: prism3d
base 0x400000, DLLs 0x10000000. Names set in Ghidra: `actor_popup_text_add` duke!0x1007b9a0,
`actor_popup_alloc` 0x1008bfc0, `actor_popup_update` 0x1007ba90, `actor_popup_remove` 0x1008c030, `ui_window_draw`
0x10115ec0, `ui_view_draw_children` 0x10114090; prism3d `ui_wdraw3d_push_matrix` 0x4193a0, `ui_wdraw3d_pop_matrix`
0x419380, `app_deactivate_fullscreen` 0x41ec50, `apply_window_style` 0x41ed80. The last two are named in [render.md](render.md)
§7.

## Data facts (Enhanced folder, read-only)

- The launcher `DukeNukemMP.exe` runs `WideGen.exe %d %d` with the `r_mode` size [S, UTF-16 strings]. It writes
  `duke/init_ex.cfg` (mounts `db_lang_*`, `exec ~GAME/init_widescreen.cfg` and `init_widescreen_fov.cfg`) and
  `uset gl_fullscreen/dx8_fullscreen "0|1"`, `r_mode`, `r_device` into `duke/base/config.cfg` [S].
- WideGen regenerates `duke/base/db_wide.zipfs` (29 `ui/scripts/*.sc`) and `db_zoom_wide.zipfs` (`scr_intro.sc`) at
  every launch; the file dates match the last launch. It also regenerates `init_widescreen_fov.cfg`
  (`uset g_cam_fov "79.7553406"`). The sources, with `$_aspect` as a variable, are in `duke/wide/db_wide/`. The
  generated copies start with the literal line `$$_aspect = 3440 / 1440` [S, python zipfile].
- `init_widescreen.cfg` mounts them at `p51`/`p52`. `init.cfg` mounts `"$GAME"/base/` **rw at p99**, so a loose file
  under `duke/base/ui/scripts/` overrides both. The panel already serves its shaders from `duke/base/nukepanel/` this
  way [S].
- 32 `ui/scripts` exist in the base archives. **Not widened: `scr_nuke.sc`** (main-menu nuke, a 3D `ui_model` in
  `0 0 640 480`), `scr_letterbox.sc` and `scr_shortcuts.sc`. `itm_console_mini.sc` has a typo, `_system480_x2`
  without `$` [S].
- The test machine ran windowed: `gl_fullscreen "0"`, `dx8_fullscreen "0"`, `r_mode "3440x1440x32"` [S config.cfg].
  GLDirect logged `Window: w=3440, h=1427 (windowed)`, so the captioned window is clamped to a **3440x1427 client**
  while the aspect and workspace come from 1440 [S the GL driver log].
- `dxwrapper.ini` [S]: `EnableWindowMode=0`, `WindowModeBorder=0`, `FullscreenWindowMode=0`, `EnableD3d9Wrapper=1`,
  `D3d8to9=1`. Its window options act only on D3D devices made through dxwrapper. They cannot restyle a window the
  game already creates windowed, and gl-fullscreen switches modes with GDI `ChangeDisplaySettingsA` (below), which
  dxwrapper does not see. No borderless lever for `gl` there.

## A. Window, fullscreen, alt-tab

- Window creation: [render.md](render.md) §7. `core_create_context_gwindow` prism3d!0x426db0 creates the game window
  `DAT_004a21ac`, shows it, then minimizes and hides the hidden context window `DAT_004a21a8` [D].
- `apply_window_style` 0x41ed80 [D] is called only from the export `hw_set_new_context` 0x41bd60 [X], which the device
  DLL calls from `mode_init`. The windowed flag is `[[DAT_004a0938+0x4c00]+0x52]`. Flag 0 (fullscreen):
  `SetWindowLongA(GWL_STYLE, 0x90000000 = WS_POPUP|WS_VISIBLE)`, `GWL_EXSTYLE 0`, and nothing else. Flag 1 (windowed):
  `0xC80000` (caption, sysmenu), ex `0x300`, centered via `AdjustWindowRectEx`, `SetWindowPos`, `ShowWindow(5)`. It is
  skipped entirely when cvar `HOST` (0x43c530) exists (embedding) [D].
- gl.dll `mode_init` gl!0x10002610 [A]: arg1 = windowed flag, stored at `+0x52`. Fullscreen: DEVMODE (w, h, bpp) ->
  `ChangeDisplaySettingsA(&dm, CDS_FULLSCREEN)` at 0x10002740, `ShowCursor(0)`,
  `SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, w, h, SWP_SHOWWINDOW)`, then `hw_set_new_context`. After that come GetDC,
  ChoosePixelFormat and the wgl context. The import names are inferred from IAT order and argument shapes. gl
  `mode_shutdown` (~0x10002560): ReleaseDC; if fullscreen, `ChangeDisplaySettingsA(NULL,0)` and a repaint; then the
  log line "`<gl> shutting down video`" [A]. **Neither gl.dll nor the host minimizes anything.** gl.dll imports no
  ShowWindow. The host's ShowWindow callers are only `apply_window_style`, window create and destroy, and the
  wndproc hiding the context window [X]. `CloseWindow` is used once, at context-window destruction 0x426f30 [D].
- WndProc `prism_wndproc` 0x426340 [D]: `WM_ACTIVATEAPP` (only while the guard `DAT_004a21b4 >= 0`):
  - Activate: `inp_set_active(1)`, then `app_deactivate_fullscreen(0)`, then focus and foreground calls.
  - Deactivate: `inp_set_active(0)`, `inp_reset_impulses()`, then `app_deactivate_fullscreen(1)`.

  `app_deactivate_fullscreen(1)` 0x41ec50 acts **only if `+0x52 == 0` (fullscreen)**: it sets `r_fullscreen "0"` and
  calls device `mode_shutdown` (`vt+0x48`). On reactivation it sets `r_fullscreen "1"` and calls `mode_init(0,0)`.
  In windowed mode it does nothing [D]. `WM_SYSCOMMAND` `SC_KEYMENU` and `SC_SCREENSAVE` are swallowed.
  `r_fullscreen` is a linked shadow of `gl_fullscreen` (`cvar_link`, gl!0x100021a6) [A].
- `rend_restart` (export 0x41f200) [D] re-reads r_device, r_renderer, `r_fullscreen` (windowed = `!atoi`) and r_mode.
  It then runs `mode_shutdown`, `mode_init(windowed,0)` (which re-enters `hw_set_new_context` and
  `apply_window_style`), sets the workspace to the mode size from `rend_get_params` and wraps it all in
  `rend_end_frame`/`rend_begin_frame`. `rend_desktop_res(int*,int*)` 0x41ebb0 returns the desktop size [D].
- **gl's `rend_restart` replaces the game window** (measured, then read in the code):
  The GL driver log shows `Input window` 905A6, then 7C0D0C after our apply, then B0114 after the revert. gl
  `mode_shutdown` ends with `hw_set_new_context({ctx, 0})` (gl!0x100025e8), and `hw_set_new_context` 0x41bd60 calls
  `core_destroy_context_gwindow` 0x426e70 (`DestroyWindow`) when the new window is 0. gl `mode_init` calls
  `core_create_context_gwindow` 0x426db0 (gl!0x10002671, a new `CreateWindowExA`), then `hw_set_new_context`, which
  runs `apply_window_style` on the new window. Our subclass saw no WM_NCDESTROY: GLDirect, whose proc was under
  ours (027ED650), puts its saved proc back before the destroy. The engine's current window is
  `hw_get_context()+4` (0x49c7a4). Fix: the input module follows that window every frame and installs our proc as
  the Prism3D class proc, so a window created inside a restart runs through us from WM_NCCREATE.
- **GLDirect's fullscreen test** (a 3440x1440 popup logged `Window: w=3440, h=1440
  (fullscreen)` and minimised on every focus loss; off, the captioned 3440x1427 client logs `(windowed)`). Read in
  the Enhanced `opengl32.dll` (GLDirect 5.x, loaded into the Ghidra project from the game folder):
  - `gldCreateContextBuffers` 0x1000ccf0 takes the drawable's size from `GetClientRect(WindowFromDC(hdc))`, calls
    the D3D9 driver's create function (table pointer 0x1012d600, set to 0x100042b0 by 0x1000dd40), then logs
    `Window: w=%i, h=%i (%s)` (0x100ee504) with `fullscreen` when drawable+0x38 is set.
  - 0x100042b0 sets +0x38 = 1 exactly when a config flag `DAT_10142dd4` is 0 (its default, 0x1000ddf0; set only
    for a list of other exe names, 0x1000c1b0, or from GLDirect's own settings, 0x1000c040) **and** the client
    width and height both equal `IDirect3D9::GetAdapterDisplayMode` (vtable +0x20) of the configured adapter.
    Then `D3DPRESENT_PARAMETERS.Windowed = 0`: an exclusive fullscreen device, which Direct3D 9 minimises when
    the app deactivates. The flag is fixed for the context; `gldResizeDrawable_DX` 0x100048a0 reuses it.
  - Lever: the borderless window is one row taller than the monitor, with the extra row above it (below
    when another monitor holds that row). The client never equals the display mode, so the device is windowed;
    the monitor is still covered with no frame. The engine renders its `r_mode` (the monitor size) from the
    bottom-left, so the unrendered top row is the off-screen one. No write to GLDirect code, data or settings.
    Each apply (and the launch's first frame) is checked by what we measure ourselves: the window is not iconic,
    its rect is the monitor plus the row, and the client is that size, so GLDirect's equality test cannot have
    passed. A GL driver log read-back is not used: the log is 0 bytes on the test machine, so it could never show
    `(fullscreen)`.
- **At creation** (verified in game; log: `borderless at creation: applied, no rend_restart`): the first video init
  comes up borderless, with no `rend_restart`.
  - Order [D]: `FUN_0041c190` calls `FUN_00426ea0` first, which creates the hidden context window
    (`core_create_context_window` 0x426c10: `CreateWindowExA(0, "Prism3D", title, style 0, 0,0,0,0)`, log
    "`<sys> create_window: OK`"). Only then does `FUN_0041bef0` call rend_init `FUN_0041ef00`, which reads
    r_device, r_renderer, `r_fullscreen` and `r_mode` (`rend_eval_modestring`) and calls `mode_init` (0x41e8c0,
    `vt+0x44`). The engine log agrees: `exec config.cfg` (`uset r_mode`, `uset gl_fullscreen`) and `init_ex.cfg` run
    before `create_window: OK`, then `<rnd> init mode : 3440x1440x32 w=1`.
  - gl `mode_init` then creates the game window (`core_create_context_gwindow` 0x426db0: the same
    `CreateWindowExA`, `ShowWindow(10)`), and `hw_set_new_context` runs `apply_window_style`, the same
    `SetWindowLongA` + `SetWindowPos(.., 0x40)` our filter rewrites after a restart. GLDirect sizes its context
    after that (GetDC, ChoosePixelFormat, wgl).
  - Lever: a thread-local `WH_CBT` hook set in DllMain (main thread, tid = frame tid in the log) sees
    `HCBT_CREATEWND` of the context window. There it loads our config, sets `r_mode` to the primary monitor through
    `cvar_t::set`, installs our proc as the Prism3D class proc, arms the filter (popup style, monitor rect + 1 row),
    and unhooks. The game window then runs through the filter from `WM_NCCREATE`.
  - `r_fullscreen` is linked to the device's cvar only inside rend_init, so the windowed test at creation reads
    `<r_device>_fullscreen` (set by config.cfg). Fullscreen, switch off, or an unreadable cvar: not armed.
  - Switch off (verified in game: off launches a captioned window, log shows `no launch hook`; on still logs
    `applied, no rend_restart`, `iconic 0`): DllMain reads `[display] borderless` itself with kernel32 only
    (`config_peek_borderless`: CreateFileA/ReadFile, the loader's own line rules in `ini.h`; the CRT's fopen is
    not proven safe under the loader lock) and installs no hook when it is 0. Log:
    `display: Borderless off: no launch hook`. If the ini exists but cannot be read whole, the hook is installed
    and returns after its config_load, before any cvar or class-proc work.
  - The first frame checks it like a restart apply (mode, popup style, rect, client, not iconic); a refusal
    hands over to the restart path below. If mode_init refuses the mode, rend_init resets `r_mode` to its default
    and retries (0x41ef00); the check then fails and the fallback runs. The launcher's r_mode is kept as stock
    (quit restore), with no stock window, so a revert keeps the window the engine centres itself.
  - Failed first frame: the stock r_mode is captured before ours is set, and until the
    fallback apply runs a pending re-apply counts as ours (`bl_mode_ours`): quitting puts the launcher's r_mode
    back for config.cfg, and switching off reverts (once windowed and a scene is up, as any revert). An r_mode the
    engine reset after a refused mode_init is logged and kept as ours, never adopted as the player's.
  - Open: the dx8 path (only gl is tested) and whether anything between creation and the first frame re-applies a
    caption. The first-frame log line settles both.
- **Restart mechanism (mid-session flips, and the fallback):**
  1. Keep the engine windowed (`gl_fullscreen 0`), so the alt-tab teardown never runs.
  2. Set `r_mode` to `rend_desktop_res`.
  3. Have our existing WndProc subclass answer `WM_STYLECHANGING`: set `GWL_STYLE` to `0x90000000` and `GWL_EXSTYLE`
     to `0`. These are exactly the values `apply_window_style` writes for fullscreen, so no new state is invented.
  4. Answer `WM_WINDOWPOSCHANGING` with pos 0,0 at the desktop size.
  5. Call `rend_restart` once on the main thread (between frames), so the style is applied before GetDC and
     wglCreateContext and GLDirect builds its swapchain at the full 3440x1440.

  This needs no engine memory writes and no code hooks. It also removes the 1427-row clamp: GLDirect builds a 3440x1440
  swapchain (GL driver log `Window: w=3440, h=1440 (windowed)`) that survives alt-tab without minimizing. Rejected alternatives:
  - Swallowing `WM_ACTIVATEAPP(0)` in fullscreen: it leaves a CDS_FULLSCREEN mode and a possibly exclusive device
    behind.
  - dxwrapper window options: they do not act on `gl`.
  - `HOST` embedding: we would have to own the window.

## B. HUD / UI canvas

- UI widgets are duke_base classes (`duke_base.classes.txt`): `ui_view` vtable duke!0x1014b268, `ui_window` 0x1014b3b8,
  `ui_text` 0x1014b7b8. Draw is `vt+0x118`: `ui_window_draw` 0x10115ec0 calls `ui_view_draw_children` 0x10114090,
  which scissors to `+0x1b0` and calls each child's `vt+0x118` [X, D].
- Fields [A]: `csystem` = +0xe4..+0xf0 (x0, x1, y0, y1), `coords` = +0xf4..+0x100, parent = +0x60.
  - `csys_to_ndc` (`vt+0x104`) 0x10114d90: `x' = 2(x-x0)/(x1-x0) - 1`, `y' = 1 - 2(y-y0)/(y1-y0)`.
  - `get_ndc_rect` (`vt+0x10c`) 0x10114b40: a root takes its own coords through its own csystem. A child gets
    `own_ndc + 1 + parent.left` (and the `y` analogue).
  - Children are **translated, never scaled**, so every csystem in a tree must be the same screen-sized system.
    That is why every widened script repeats `0 0 $_system768_x2 768` [S].
- Text size: `pml_set_ref(|csys w|, |csys h|)` (duke!0x1009e460 into prism3d!0x416000), then `pml_set_bounds` 0x416020:
  `scale = 2/(box_ndc_w * ref * 0.5)`. A glyph is `4g/ref_w` NDC wide and `4g/ref_h` NDC high, so on screen it is
  `g * 2W/ref_w` by `g * 2H/ref_h` pixels. **With `ref_w/ref_h = W/H`, text and boxes keep their aspect** [D].
- Conclusion: the widened HUD (`csystem 0 0 768*aspect 768`, the 4:3 block centered) is correct if `$_aspect` is
  2.389. If the preprocessor computes `3440 / 1440` as the integer 2, the canvas is 1536 wide and everything is
  **19 % too wide**, which matches "stretched horizontally". The preprocessor is in `sys/loaders.dll` ("`<script>
  preprocessor size limit hit`", "`referencing uninitialized variable %s`"; imports `v_atof`, `v_atoi`, `_ftol`)
  [S]. loaders.dll is not in the Ghidra project, so whether the aspect is an integer is open (U1). The non-widened `scr_nuke.sc`,
  `scr_letterbox.sc` and `scr_shortcuts.sc` stretch 640x480 over the whole screen regardless [S].
- **Lever, if a stretch ever shows:**
  - Generated data mounted above `db_wide`: loose files under `duke/base/ui/scripts/` (p99), or
    `mount -o ro,p60 <dir> /` through `cmd_execute` before the UI loads.
  - For U1 = integer, rewrite each generated script with **literal** float csystem and coords strings; those strings
    are parsed as floats at widget load.
  - Widen the three missing screens the same way.
  - No code lever is needed. The fallback, a detour of `get_ndc_rect` for roots to pillarbox, would shrink the
    widened HUD and is not recommended.

## C. Floating text ("10 AMMO", damage) offset right

- Producer: `actor_popup_text_add(text, …)` duke!0x1007b9a0 [D, A], 19 call sites [X], for example ammo in
  0x100796e0 (`"%d %s"` with `@@p_ammo@@`, S 0x10164df4).
  - It allocates a 0x128-byte popup through `actor_popup_alloc` 0x1008bfc0. That calls
    `ui_winsert3d_src(ui, "ui_handler_default", "ui/scripts/scr_actor_text.sc")` and appends the popup to the list at
    actor+0xa50.
  - Popup fields: position = actor-local `(0, [actor+0xec4]+48, -10)` (+0xec4 drops by 10 per popup, so popups stack) taken through the actor matrix; `+0x10 = 640.0`,
    `+0x14 = 480.0`; text = `<FONT FACE=help.font XSCALE=0.25…><ALIGN HSTYLE=CENTER VSTYLE=CENTER>%s` (S 0x10165060).
  - `actor_popup_update` 0x1007ba90 raises it by `80*dt/life` and fades it.
- Draw: `actor_draw_ui3d_texts` 0x1008c090 [A] calls
  `ui_wdraw3d(win, pos, (640,0,0), (0,480,0), billboard=1)`. prism3d `ui_wdraw3d` 0x419a50 uses
  `ui_wdraw3d_push_matrix` 0x4193a0 [D]: translate(pos), axes from the two vectors, billboard through
  `rend_camera_inverse`, scale by the axis lengths (640, 480, 1), `rend_push_mult`. Then the window's `vt+0x118` runs
  and `ui_wdraw3d_pop_matrix` pops. **The canvas is a fixed 4:3 quad in world space. Screen aspect reaches it only
  through the perspective projection, which is already correct.**
- Why it shifts [D + arithmetic]: `S = 480*aspect`. The widened window has `coords x1 = (S-640)/2`, so its left edge
  is `-640/S`. Its `ui_text` child spans `0..S`, so the child box is `[-640/S, 2-640/S]` with center `1-640/S`: 0.442
  at 21:9 (0.333 if U1 = integer), 0.25 at 16:9. The text is CENTER-aligned, so it lands right of Duke by that
  fraction of the quad half-width. `ref_w = S` narrows glyphs to `640/S`. The stock script (`0 0 640 480` for window
  and child, db_e01) gives center 0 and glyph ratio 1.
- **Fix (shipped, verified in game):** mount the stock `scr_actor_text.sc` above `db_wide` (loose file at p99, or our
  own mount at p>52). No hook, no engine write. Removing winmm.dll restores the widened script.

## Open questions

1. **U1** `$$_aspect = 3440 / 1440` integer or float. It would settle B's candidate cause (moot for what is seen).
   Measurement: `ui_wsearch`/`ui_wget` on the HUD root window, then log its csystem floats at +0xe4..+0xf0 once:
   1834.67 means float, 1536 integer.
4. **U4** Where the alt-tab minimize comes from in a fullscreen device. Engine and gl.dll contain no minimize call;
   the GLDirect finding (section A) points at the D3D9 runtime minimizing an exclusive device. Measurement: log
   `WM_SIZE(SIZE_MINIMIZED)`, `WM_SYSCOMMAND(SC_MINIMIZE)` and `WM_WINDOWPOSCHANGING` flags with the return address in
   our subclass, windowed and with `gl_fullscreen 1`.
6. **U6** Whether the workspace (r_mode 3440x1440) against the 1427-row client crops or squashes 13 rows in the
   captioned window. Measurement: log `rend_get_viewport` against `GetClientRect` once.
8. **U8** `rend_restart` with an `r_mode` the renderer does not list. `rend_eval_modestring` 0x41e9f0 may fail inside
   the engine (an `error()` path) before our post-restart check runs, so the revert would never happen. It is not
   reached today: borderless sets the monitor's desktop size, which every renderer lists. Measurement: set an
   unlisted `r_mode` by hand and `r_restart`; read the console and our log.
