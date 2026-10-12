# Rendering, camera, frame loop, window (prism3d.exe / client.dll / gl.dll / dx8.dll)

One single-threaded cooperative loop runs a priority-sorted process list; the world is
drawn by `client.dll` (prio 99), the HUD by the host `__ui_proc` (prio -3), and `subsys` (prio -2) then flushes the
render queue, calls `rend_end_frame` (EndScene) and `rend_swapbuffers` (Present/SwapBuffers). **Best overlay hook:
the entry of the exported `rend_end_frame` (prism3d!0x41fa70) when called from subsys (prism3d!0x425c6e)**, drawing
through the engine's own renderer-neutral 2D API: one code path for `gl` and `dx8`. The camera stack is popped by
then, so ESP must **capture the view matrix during the world pass** (hook the export `rend_camera_push`,
prism3d!0x4206d0, or `spawn_draw_server`, prism3d!0x422290). FOV is the duke_base cvar **`g_cam_fov`** (horizontal
degrees, default "50"), reapplied every frame.

All addresses: prism3d.exe base 0x400000; DLLs base 0x10000000. Methods: D = decompile, A = disassembly,
X = xref/script, S = string. The scripts used are in the Ghidra project's `scripts` folder
(DumpVtable, DisRange, CallSites, PfnUsers, LabelPfns, FindEntry).

## 1. Frame loop

- `main_loop` prism3d!0x406100 = `do {} while (main_loop_iteration());` (D). It is called from
  `host_main` prism3d!0x424c90 via FUN_0041c190 after all init (D).
- `main_loop_iteration` prism3d!0x405ff0 (D), per iteration:
  1. `sys_frame()` prism3d!0x4024f0: `tmr_read` + `vtmr_update`, sets frame dt `DAT_00476430` (s) and total
     time `DAT_00476438` (D).
  2. Walks the process list `DAT_0047646c` (node: +4 next, +8 proc, +0xc phase arg, +0x10 priority). For each
     proc: `proc+0x1c = node phase`; if not sleeping (`+0x14==0 && +0x18==0`): `vprocess_t::update(dt)` and
     vtable **+4 (handler)**; then always vtable **+8 (system)** (D).
  3. FUN_0041bec0 (per-device vtable +0x18 polls), then a `PeekMessage/Translate/Dispatch` pump; `WM_QUIT` ends (D).
- **Threads:** none. No module imports CreateThread (X on dumps\*.imports.txt); window messages, game update and
  rendering all run on the main thread. (ds8.dll/DirectSound/DirectInput internal threads not checked.)
- `process_add(proc, prio, phase, fn)` prism3d!0x405e10 inserts sorted by **unsigned** prio, ascending, equal
  prios append (D). Registered processes (A of every call site, X):

| prio (u32) | process | who / where | per-frame work |
|---|---|---|---|
| 0 | `subsys` phase 0 | host_main @0x424f3d | handler prism3d!0x425bc0: input `handler`, `inp_reset_impulses`, `inp_process`, **`rend_begin_frame`** (tail jmp 0x425bf0) |
| 1 | `spawner` (FUN_0040bde0) | @0x424f81 | server/entity sim (object used by `spawn_draw_server`) |
| 10 | FUN_00409e00 obj | FUN_0040c810 @0x40c832 | (world/net, not analysed) |
| 98 (0x62) | duke `game_proc_t` | duke_base FUN_100e6950 @0x100e69ae | game logic |
| 99 | `client` | client.dll FUN_100013d0 @0x10001428 | **world render**: `client_proc_handler` client!0x10001000 = `wm_draw()` + `client_render_views` client!0x10002620 |
| 0xfffffffd (-3) | `__ui_proc` | host_main @0x424fba, ctor `ui_proc_ctor` 0x422550 | **HUD/UI**: handler 0x422d70 (vtable 0x4348b0 +4) processes cursor input, then calls UI entities' vtable **+0x118** (A: 0x4230d5, 0x423105, 0x423136) |
| 0xfffffffe (-2) | `subsys` phase 1 | host_main @0x424f4f | handler 0x425bc0 else-branch: r_fps text, `rend_cache_flush`, `rend_props_flush`, **`rend_end_frame`** (0x425c6e), screenshot callbacks FUN_0041fb90, `rend_swap_clear(3,0)`, **`rend_swapbuffers`** (0x425c84), deferred `cmd_execute` FUN_0041a8e0 |
| 0xffffffff (-1) | memory/"sys" proc (FUN_00415610), renderer DLL procs (gl/dx8 DLL_register), duke `game_globals`+0xc (duke_base 0x100de609), FUN_0041e4b0/FUN_00421d90/FUN_00409190 objs | | run after Present |

- `rend_begin_frame/rend_end_frame/rend_swapbuffers` are only called by the host (subsys handler, and
  `rend_restart` 0x41f200 around a mode change). duke_base resolves but never calls them (X: pfn only written in
  its resolver FUN_100decb0).
- **World vs HUD boundary:** world = client prio 99 (`rend_camera_push` -> `spawn_draw_server(world, &campos)` ->
  `rend_camera_pop`, client!0x10002620, D). HUD = `__ui_proc` prio -3. duke_base UI widgets draw from vtable
  +0x118 (e.g. text widget duke!0x1009e460 = `pml_set_ref/pml_set_bounds/pml_render/pml_buffer_get/..flush`,
  group duke!0x10114090 `ui_group_draw_children` iterates children +0x118, frame duke!0x10114a60 =
  `rend_set_projection_type(2)` + `rend_line_strip`) (D). A 3D HUD widget (duke!0x10113670) renders a model with
  its own `spawn_draw_server`, then `rend_cache_flush`, `rend_clear(2,0)` (A 0x10113790..0x101137a6).
- **Queued vs immediate:** bitmaps/text/sprites are queued protos (`rend_sub_bitmap` 0x41d2a0 builds a packet and
  `rend_queue_proto`, D) and reach the device at the next `rend_cache_flush`; lines are **immediate**
  (`rend_line_plain` 0x41f910 -> device vtable +0x94; dx8 +0x94 0x10004130 forwards to +0x98 which draws at once, A).

## 2. Overlay hook candidates (ranked)

Device object: `DAT_004a0938` = current `r_device_t*` (every `rend_*` wrapper dereferences it, D).
r_device_t vtable (base p3shared!0x10035648; dx8 derived 0x10007230; gl derived 0x100081f8; DumpVtable):
`+0x44 mode_init, +0x48 mode_shutdown, +0x6c projection_get, +0x74 clear, +0x78 swap_clear, +0x7c swap_buffers,
+0x90 get_image, +0x94 line_plain, +0x98 line_strip, +0xa0/+0xa4 set persp/ortho, +0xc0 set_projection,
+0xd8 begin_frame, +0xdc end_frame`.

1. **(a) Engine 2D API from a detour on `rend_end_frame` entry prism3d!0x41fa70** — USED (the overlay draws here).
   Bytes `8b 0d 38 09 4a 00  8b 01  ff a0 dc 00 00 00` (`mov ecx,[0x4a0938]; mov eax,[ecx]; jmp [eax+0xdc]`), a clean 6-byte
   first instruction; it is exported, so resolve by `GetProcAddress(prism3d, "rend_end_frame")`, no signature scan.
   At this point every queued packet has been flushed (0x425c64/0x425c69), the HUD is drawn, dx8 is inside
   BeginScene (dx8 begin_frame 0x10003ea0 calls BeginScene +0x88; end_frame 0x10003ff0 calls EndScene +0x8c, A),
   and gl's begin/end_frame are `ret` (gl!0x10004010, A) so GL is simply current. Present follows
   (0x425c84). Skip our drawing when the call does not come from 0x425c73 (return address), i.e. from `rend_restart`
   0x41f2ca. Recipe inside the hook:
   `rend_set_projection_type(2)` (ortho = rect -1..1, near -1, far 1: p3shared `set_projection` 0x1001e7b0, D) ->
   `rend_push(); rend_identity();` -> immediate lines `rend_line_plain(ARGB, const vec3* a, const vec3* b)`,
   `rend_line_strip(ARGB, const vec3* pts, int n)` (vec3 = 3 floats, stride 12; duke!0x10114a60 passes
   `color, pts, 5`) -> for text: `pml_set_ref(refW, refH); pml_set_bounds(rect); pml_render(str);
   pml_buffer_flush(...)` (queued) followed by our own `rend_cache_flush(); rend_props_flush();` -> `rend_pop();`
   restore `rend_set_projection_type(1)`. NDC: x_ndc = 2*px/W - 1, y_ndc = 1 - 2*py/H (y up; from the ortho rect
   (-1,1,1,-1)). Font: `fonts/default.font` (S prism3d!0x46d4f0), chosen by pml markup (`<font=...>`, S "/font").
   There is no separate engine console renderer: "console" is a UI script (`itm_console.sc`, see section 5).
   One code path for both renderers. W/H from `rend_get_params(&w,&h,&bpp)` 0x41ebd0 (ints, D).
2. **(b) Device vtable slot +0xdc (end_frame) or +0x7c (swap_buffers)** of the object at `DAT_004a0938`.
   Same timing as (1) for +0xdc but requires writing a vtable in engine data (house rule) and must be redone
   after every renderer switch (`r_device`, DLL_register creates a new device object). +0x7c is later: dx8
   swap_buffers 0x10004020 does GetBackBuffer/CopyRects/Present (+0x3c) after EndScene, so drawing there needs its
   own BeginScene; gl 0x10002fd0 = optional glFinish then GDI `SwapBuffers(hdc)` or `wglSwapBuffers`.
3. **(c) Raw IDirect3DDevice8::Present / GDI SwapBuffers** — two code paths (dxwrapper d3d8->d3d9 vs GLDirect
   opengl32), must draw with raw D3D/GL state. Worst; only if (1) fails.

## 3. Camera and world-to-screen

Signatures (entry + call site read):
- `rend_camera_push(const matrix_t* view, const matrix_t* inv)` 0x4206d0 (cdecl, D): pushes 64-byte copies onto
  stacks `DAT_004a1278` (view) and `DAT_004a0a64` (inverse). Called only by client!0x1000264a with
  `(node+0x44, node+4)` (X: p3shared/duke_base never call it).
- `rend_camera_view(view, inv)` 0x420720 replaces the top; `rend_camera_pop()` 0x420760.
- `rend_camera_matrix()` 0x420840 returns `float*` = top of view stack; `rend_camera_inverse()` 0x420850 returns
  top of inverse stack; `rend_get_matrix()` 0x4206b0 = current modelview `DAT_004a1a84`.
- `rend_get_camera(vec3* out)` 0x4207a0 = origin transformed through the inverse (camera->world) = camera position.
- `rend_get_proj_matrix(float out[16])` 0x4200f0 takes **one** argument (A: arg read at [esp+0x28] after the
  0x24 frame; the decompiler's first param is spurious).
- `rend_projection(near, far, fov)` 0x4202a0 pushes; `rend_projection_change(near, far, fov)` 0x4202d0 edits the
  top in place; `rend_projection_get(&near,&far,&fov)` 0x420350 (D). Stack top ptr `PTR_DAT_0046efe0`, base
  0x4a09f8, 12-byte entries.
- `rend_get_viewport(float out[4])` 0x420970 converts the NDC viewport to workspace pixels; workspace set by
  `rend_restart` to `{0, W, H, 0}` (x0,x1,y_top?,y_bottom) (D).

**Matrix layout:** 16 floats, row-major, **row-vector** convention (p' = p * M, translation in m[12..14]):
`mult_zero_matrix` p3shared!0x10016aa0 returns (m[12],m[13],m[14])/m[15] as the transformed origin (D).
Projection (`rend_get_proj_matrix`, and the identical device `projection_get` p3shared!0x1001e590, D/A):
t = tan(fov/2), landscape (W>H): m[0] = 1/t, m[5] = (W/H)/t, m[10] = (f+n)/(f-n), **m[11] = 1**,
m[14] = -2fn/(f-n), rest 0. So camera space looks down **+z**, w_clip = z_view, NDC z in [-1,1], and
**fov is horizontal** (vertical when H >= W).

**Recipe (world point P -> pixels):**
1. During the world pass, capture `V = *rend_camera_matrix()` (64 bytes) and `rend_get_proj_matrix(Pm)`, e.g. in a
   hook on `rend_camera_push` exit or `spawn_draw_server` entry (0x422290: `(entity*, const vec3* campos)`, A).
   Take the push whose caller is client.dll (return address) — several views per frame are possible.
2. c = [Px Py Pz 1] * V * Pm (row vector). Reject if c.w <= near (behind camera).
3. nx = c.x/c.w, ny = c.y/c.w; sx = (nx+1)/2*W, sy = (1-ny)/2*H with W,H from `rend_get_params`.
Valid window: only between client's `rend_camera_push` and `rend_camera_pop` (prio 99). At `rend_end_frame` the
view stack is back at its identity base (`rend_reset_camera` 0x4203e0 sets base 0x4a0e78/0x4a0a68 to identity, D).
The projection stack top still holds the game values at end of frame (g_cam code edits it in place), but capture it
together with V to be safe.

**Axes / units:** `DLL_register` duke!0x100de531..0x100de596 builds `rotate(-90,0,0)` then
`scale(s,s,-s)` with `s = DAT_101869d0 = 52 * 0.1 = 5.2` (write at duke!0x100e671c: `DAT_10145b90 * DAT_10145b94`)
and passes it to **`skmod_base_transform`** (A). That is the skinned-model import transform: model Z-up rotated to
engine **Y-up**, z negated (handedness flip), 5.2 world units per model unit. The following pfn is
`snd_distance_factor(52.0)` and `snd_def_mm_distances(350, 2000)` (sound min/max distances) — **not** FOV/near/far.

## 4. FOV / zoom

- duke_base cvars (struct name at +0, default string at +0x20; X/D): `g_cam_fov` @duke!0x10176fc0 default "50",
  `g_cam_near` @0x10176e20 "16", `g_cam_far` @0x10176ef0 "8000", `g_zoom_type` @0x10175350 "0". Registered in
  FUN_100e6200.
- Applied every frame by the camera code at duke!0x10073c77..0x10073d94 (and a twin at 0x10077b61..0x10077c61)
  (A): `rend_projection_get`; atof each cvar's string at **cvar+0xb4**; if any differs,
  `rend_projection_change(near, far, fov)`. Then if the camera object has `[obj+0xf4] != -1`, it overrides:
  `rend_projection_change(near, far, (float)[obj+0xf4])` — the game's own per-camera/zoom FOV.
- Widescreen: WideGen.exe (Enhanced, .NET) writes `.\duke\init_widescreen_fov.cfg` = `uset g_cam_fov "<x>"`
  (ComputeWideFOV) and packs `duke\wide\db_wide|db_zoom_wide` scripts into `base\*.zipfs` substituting
  `$$_aspect = W / H` (S in WideGen user-string heap). Until the launcher has run WideGen (it does at
  every launch, [display.md](display.md) "Data facts") the zipfs and the fov cfg do **not exist** (only the source folders and `duke\init_widescreen.cfg` / `duke\base\widescreen.cfg` exist).
  Because FOV is horizontal, a wider screen at fov 50 shows less vertically (Vert-); WideGen raises g_cam_fov.
- **Zoom feature:** set `g_cam_fov` at runtime (cvar API, e.g. `cvar_get("g_cam_fov")` + `cvar_t::set`, or
  `cmd_execute`), restore on release; the next frame's camera code applies it. No engine memory writes.

## 5. HUD on ultrawide

HUD/UI are data-driven `ui_window` scripts (`ui/scripts/*.sc`, e.g. `scr_hud.sc`) with
`csystem:"0 0 1024 768"` / `"0 0 640 480"` virtual coordinates mapped onto the ortho [-1,1] screen, so on non-4:3
they stretch. The widescreen versions set `csystem` width to `768 * $_aspect` and center 4:3 blocks
(`duke\wide\db_wide\ui\scripts\scr_hud.sc` lines 1-16). Text scale follows `pml_set_ref(|csys x1-x0|,|csys y1-y0|)`
(duke!0x1009e460). Fix location = these scripts (regenerate with the right aspect), not code.

## 6. Frame time / FPS limiting

- `tmr_read` 0x402c70 = `timeGetTime() * 1000` (D): **1 ms granularity**; no QueryPerformanceCounter import.
  `sys_frame` derives dt from the p3shared virtual timer (`DAT_00475b9c+0x18`, usec). `sys_get_timedelta(double*
  last)` 0x4025d0 returns now - *last and updates it; `sys_get_time` 0x4025c0 returns total seconds (D).
- **No frame cap** in the engine: prism3d imports no `Sleep` (X). Caps come from vsync (`gl_sync` cvar,
  S "wglSwapIntervalEXT"/"WGL_EXT_swap_control" in gl.dll) and dxwrapper `LimitPerFrameFPS`. `r_fps`/`r_timings`
  are display-only cvars (subsys ctor 0x425b4e..0x425b66; `%-5.1f ms / %-5.1f fps`).
- Limiter site: detour `sys_frame` entry (0x4024f0, `83ec08 e8...`, first thing of each iteration) or after
  `rend_swapbuffers`; sleep/spin there, then let the engine sample the clock.

## 7. Window

- Class "Prism3D" registered at 0x426bf8 (RegisterClassA, FUN_00426b20); WndProc = `prism_wndproc` 0x426340.
  `core_create_context_window` 0x426c10: `CreateWindowExA(0, "Prism3D", TITLE cvar or "Prism3D Engine", style 0,
  0,0,0,0, ...)`, or adopts an HWND from cvar `HOST` (embedding). Two windows: `DAT_004a21a8` (context, hidden)
  and `DAT_004a21ac` (game, `core_create_context_gwindow` 0x426db0) (D).
- `apply_window_style` 0x41ed80: renderer windowed flag = `[[DAT_004a0938+0x4c00]+0x52]`. Windowed ->
  `WS_CAPTION|WS_SYSMENU` (0xC80000), ex 0x300, centered via AdjustWindowRectEx + SetWindowPos; fullscreen ->
  `WS_POPUP|WS_VISIBLE` (0x90000000), ex 0 (D). Mode size from `[[dev+0x4c00]+0xc]` (w,h,bpp).
  Fullscreen cvars: `r_fullscreen` (host), `gl_fullscreen` / `dx8_fullscreen` (device DLLs, S).
- Alt-tab: `WM_ACTIVATEAPP(0)` (case 0x1c) -> `app_deactivate_fullscreen(1)` 0x41ec50: if fullscreen,
  sets `r_fullscreen "0"` and calls device `mode_shutdown` (+0x48) (D); `WM_ACTIVATEAPP(1)` restores
  `r_fullscreen "1"` and `mode_init(0,0)` (+0x44). So leaving a fullscreen game tears the video mode down and the
  popup window is minimized. `WM_SYSCOMMAND` SC_KEYMENU/SC_SCREENSAVE are swallowed.
- Borderless needs windowed mode (`*_fullscreen 0`) at desktop size with the 0xC80000/0x300 styles replaced by
  `WS_POPUP`, positioned at 0,0. It is built without a hook on `apply_window_style`: the WndProc subclass rewrites the
  style and position messages, then `rend_restart` ([display.md](display.md) section A).

## 8. Renderer DLLs

- **dx8.dll**: static import `Direct3DCreate8` only (dumps). Device at `this+0x4d44`, IDirect3D8 at `this+0x4d40`.
  CreateDevice call dx8!0x100037be: HAL, focus hwnd from the host context, **BehaviorFlags = MIXED_VP (0x80) if
  `dx8_tnl` != "0" else SOFTWARE_VP (0x20)**; no `D3DCREATE_MULTITHREADED`, no `FPU_PRESERVE` (A 0x100036a2..ae).
  So D3D sets the x87 FPU to single precision on this thread (matters for any double math in the mod).
  begin_frame 0x10003ea0: TestCooperativeLevel, Reset on DEVICENOTRESET, BeginScene; end_frame 0x10003ff0:
  EndScene; swap_buffers 0x10004020: GetBackBuffer, CopyRects from `this+0x4c28`, Present(0,0,0,0) (A).
  In Enhanced, `d3d8.dll` = dxwrapper (d3d8to9).
- **gl.dll**: no opengl32 import; `LoadLibraryA("opengl32.dll")` + GetProcAddress of gl*/wgl* (S), GDI
  `SwapBuffers/ChoosePixelFormat/SetPixelFormat`, `ChangeDisplaySettingsA` for fullscreen. swap_buffers
  0x10002fd0: optional glFinish (`[this+0x4db0]`, when a cvar is not "0"), then GDI `SwapBuffers(hdc)` if
  `[[this+0x4c00]+0x50]` else `wglSwapBuffers` (`[this+0x52cc]`) (A). In Enhanced opengl32.dll = GLDirect.

## Open questions

1. The `rend_end_frame` overlay on `dx8`: it draws and blends correctly on `gl` (the "Measured in game" section); `dx8`
   is not tested.
2. Viewport / modelview / scissor stacks are at their base at `rend_end_frame`: read `DAT_004a1bb0`, `DAT_004a1a80`
   in the hook (log once), or always push identity/full viewport ourselves (the overlay does the latter).
3. World up axis = +Y and 1 m ~ 52 units are inferred from `skmod_base_transform` / `snd_distance_factor`: log the
   player position while jumping to confirm.
4. Whether anyone calls timeBeginPeriod (dt granularity at high fps).
5. Exact reason the window minimizes on alt-tab inside `mode_shutdown` (dx8 +0x48 / gl +0x48 not read; the
   GLDirect fullscreen test in [display.md](display.md) section A explains the windowed case).

## Measured in game (`gl`, windowed 3440x1440)

- The `rend_end_frame` detour at the frame path (return address 0x425c73) draws visibly after the world and HUD:
  `rend_line_plain`, `rend_bitmap` with our own `.shd`/PNG served from loose files in `duke/base/nukepanel/`, and
  engine text via `pml_flush` all appeared. The quad's rect order {x_left, x_right, y_top, y_bottom} is correct:
  the "N" was upright and unmirrored.
- **Engine colours are ABGR (0xAABBGGRR):** 0xFF2FE0D0 drew yellow and 0xFFD8402F drew blue.
- Exactly one `rend_camera_push` per frame, from `client.dll+0x2650`, on the same thread as the frame
  (`foreign_tid_pushes=0`). At the level: near 16, far 8000, fov 79.755 (the widescreen value). World-to-screen
  per section 3 put the crosses on the enemies. Everything ran on one thread,
  confirming section 1.
- **Frame rate: exactly 16.67 ms (60.0 fps)** with `gl_sync 1` on a 175 Hz monitor. `sys/sys.ini` loads `sys/dx8.dll`
  on `gl` too, which pulls in dxwrapper's `d3d8.dll` (`LimitPerFrameFPS = 60`).

**Frame cap:** on `gl` the process loads
`d3d8.dll` + `dxwrapper.dll` (from the game folder; `sys/dx8.dll` is always loaded), and GLDirect uses system
`d3d9.dll`. Changing only `dxwrapper.ini` `LimitPerFrameFPS` from 60 to 0 took `gl` from 60.0 fps to 175 fps
(5.71 ms, vsync `gl_sync 1` on a 175 Hz monitor). So dxwrapper's frame limiter throttles the `gl` path too, not only `dx8`.

## 2D primitive submission

The canvas submits non-indexed triangle lists through `rend_cache_data` into the **"ortho" pool**
with **one shader** (the font atlas, whose white block serves flat fills), so every proto lands in one bucket and
draws in submission order; a shader change flushes (`rend_cache_flush`) before the next submission.

- `rend_cache_data(uint* desc, int* pool)` prism3d!0x41cd80, cdecl, plain RET (A: desc at [esp+0x1c] after
  `sub esp,0xc` + 3 pushes). Descriptor (D): `desc[0]` format (bit0 pos 3f, bit1
  normals 3f, bits2-5 UV sets 2f each, bits6-7 colour sets 1 uint, bit15 uint16 indices); `desc[1]` bit0 copies
  `desc[0xf..0x11]` (bounding-sphere centre) and `desc[0x12]` (radius) into the packet, else they are cache garbage;
  `desc[2]` -> proto+0x38 flags, `desc[3]` -> proto+0x44, `desc[4]` -> proto+0x40 **topology**, `desc[5]` shader,
  `desc[6]` vertex count, `desc[7]` index count, `desc[8]` pos, `desc[9]` colours, `desc[0xb]` UV0, `desc[0xd]`
  normals, `desc[0xe]` indices, `desc[0x13]` -> proto+0x20. The packet's data lives in the linear render cache
  (`rend_cache_alloc` 0x41ccb0: 0x40000 bytes at start, `DAT_0046daa0`; an overflowing packet is dropped and the
  cache grows 1.5x at the next flush, 0x41cc50). Packet size = 92 + 2*indices + 24*vertices bytes for format 0x45.
- The one caller building a quad (FUN_00417e20 0x418b..: `desc = {0x45, 1, 0, -1.0f, 1, shader, 4, 0, pos, col, -,
  uv, -, -, 0, centre[3], radius, 0}`), vertices BL, TL, BR, TR: that order is a strip, not a fan or a quad.
- **Topology** (gl!0x10003b40, device vtable +0xe0, D): proto+0x40 = 0 -> `GL_TRIANGLES`, 1 -> `GL_TRIANGLE_STRIP`,
  2 -> `GL_TRIANGLE_FAN`, anything else logs "<gl> wrong geometry type". Without indices it is
  `glDrawArrays(mode, 0, vcount)`; with indices `glDrawRangeElements`/`glDrawElements(mode, n, GL_UNSIGNED_SHORT)`,
  where the count comes from proto+0x2c when non-zero (zeroed by `rend_alloc_proto` 0x41f520). dx8.dll not read
  (not open in the project; only `gl` is tested).
- r_cache packet ops `PTR_s_r_cache_0046db78` (A): +4 indices, +8 pos[i], +0xc normals[i], +0x10 colours[i],
  +0x18 UV[i], +0x28 bounding sphere (returns centre, radius out), +0x40 vertex count, +0x44 index count.
- **Pools** (type table 0x46ed00, 6 dwords each `{insert fn, 0, 0, description, name, priority}`, S): "decal" 20,
  "semitranslucent" 30, "translucent" 40 (`rend_translucent_insert` 0x41e230), **"ortho" 50**
  (`rend_ortho_insert` 0x41e0e0, "Ortho [2D] geometry pool"). `rend_get_pool(name)` 0x41f480 (cdecl) walks the
  registered list `DAT_004a0890`; `rend_flush_pools` 0x41f790 renders pools in list order, buckets in list order
  (`r_device_t::render_bucket` p3shared!0x1001ee80), protos in a bucket in append order.
- **Ordering, ortho pool** (0x41e0e0, D): buckets sorted by shader priority (shader data +8), then by shader
  **address**, and a proto whose shader and matrix id (`DAT_004a1a90`) match an existing bucket is **appended to
  that bucket** even if other buckets were queued since. So order is kept for one shader and lost across shaders.
  The bucket's projection type is set to 2 (ortho), which render_bucket applies (p3shared!0x1001f6f0).
- **Translucent pool is unusable for 2D:** it sorts by priority then back-to-front depth of the sphere centre (stable
  for equal keys), but `rend_alloc_bucket` 0x41f840 creates buckets with projection type 1 and only the ortho insert
  overrides it, so render_bucket would switch to perspective. (The pml path that uses it runs only when
  `DAT_004981c6` is set.)
- **Chosen:** pool = `rend_get_pool("ortho")`, `desc = {0x45, 1, 0, -1.0f, 0 (triangle list), shader, n, 0, pos,
  col, 0, uv, 0, 0, 0, {0,0,0}, 2.0f, 0}`, positions in ortho NDC, colours ABGR via `engine_color`. Flat fills
  sample a white block in the atlas, so the whole canvas is one shader; `prim` still flushes on a shader change,
  and keeps its packets under the cache budget with a mid-frame `rend_cache_flush`.
- **Shader state the canvas sets** (spellings from shipped `duke/base/db_cor.zipfs`): texture x vertex colour is a
  per-layer texenv (gl!0x100040f0: replace / modulate / add / combine), so the layer says `texenv modulate` as
  `fonts/default.shd` does. Cull is per pass (gl!0x10004c30 @0x10004e9e), so the pass says `cull none` as
  `specials/notfound.shd` does, and every canvas triangle also winds like the engine's quad (positive area in
  pixels, y down). `nocompress` is in the parser's layer keywords (sys/loaders.dll string 0xa93c) but no shipped
  shader uses it; the atlas layer sets it with no argument and the panel renders.

## UI scale

Verified in game: 75-200% render sharp. The panel, its hints and tooltips draw at the Settings > PANEL "UI scale"; text at any scale but 100 is
rasterised at that size (AA's `fontgen.cpp`, GDI) and reaches the engine as files, because Prism3D gives the canvas
no texture upload of its own. The ESP and the other overlays stay at 100%.

- **Coordinates** (`canvas.cpp`, AA's composition): the panel is laid out in logical pixels; `dev_x = tx(ui_scale_px(x))`,
  the panel transform's displacement and centre scaled too. `canvas_text_width` stays logical. `menu_draw` sets the
  scale on entry and restores 100 on exit, and `canvas_begin` starts every frame at 100, so the ESP never inherits it.
  `menu_panel_rect` hands the ESP the panel box in device pixels for the edge-arrow suppression.
- **Range:** the row runs 75-200% in steps of 5 (`ui_scale_step`) on every
  screen. The applied scale is capped by `ui_scale_ceiling(screenW, screenH)`, the largest scale at which the least
  panel fits: `PANEL_CHROME_MIN_H` 277 (bevel, strip 15, status bar 30, the five 46 px rail entries; the body beside
  the rail holds five rows) with the 2x30 inset, and `PANEL_MIN_W` wide. That is 200% on every screen 674 px tall or
  more (1080: 540 logical, the panel 480; 1440: 720 logical, the panel 660), 195% at 673, 75% under 270; a 1024 px
  wide screen stops at 180% (PANEL_MIN_W 560). AA's rule (PANEL_MIN_H 790 here) stopped at 165% on 1440 and 125% on
  1080. A panel taller than fits draws capped at the applied scale (`ui_panel_h_cap`, `menu_panel_drawn`) and its
  content scrolls. The drag lays out with the same cap (`panel_layout`'s `hCap`), so a capped panel moves
  freely and a click that moves nothing stores nothing; under a cap below PANEL_MIN_H a resize changes the width
  only. The stored box is written only by a resize (a move stores neither size).
- **Atlas**: `fontgen_build(pct)` (all or nothing; faces Bahnschrift 21/16 and Cascadia Mono 18 at `ui_scale_px`) packs
  the 288 glyphs and a 4x4 white block (flat fills sample it, so the panel stays one shader and keeps submission
  order) on the smallest `FONTPACK_LADDER` rung; 200% fits 1024x512. `atlasfile.h` encodes it as an RGBA PNG
  (white, alpha = coverage, stored deflate blocks) and a `.shd` identical to `font.shd` but for its source.
- **Upload**: `nukepanel/font_s<pct>.{png,shd}` are written THROUGH THE VFS (`vfs_open(name, O_WRONLY|O_CREAT|
  O_TRUNC)` + `vfs_write`), read back with `vfs_open/vfs_read` and compared byte for byte, then
  `mm_get_resource("nukepanel/font_s<pct>.shd")` loads it exactly as `font.shd` is. Any failure logs one line (what
  the VFS returned: opened or not, device, bytes read, first differing offset) and the scale draws at 100
  (`canvas_atlas_step` REFUSED, never retried every frame).
- **Why not CreateFileA** (it logged "does not serve" at every scale): `vfs_open`
  (0x403b80) looks a name up only in an in-memory tree (FUN_004031f0 -> hash walks 0x4030c0/0x403150, no disk
  access). The osfs driver ("osfs_wrapper", ops at 0x43caf0; open 0x404a60 = `_open("%s%s" device dir + path)`,
  read 0x404b50 = `_read`) fills that tree once per mount by FindFirstFile recursion (0x404e10). A file created on
  disk after the mount is never found (-2) until the next launch. The one way in is `O_CREAT` (0x100): a missing
  name is `_open`ed on the fallback device (`DAT_00476454[0]`, the rw root mount: "fallback device must be mounted
  on root"/"rdwr") and registered in the tree (0x4032a0); duke_base's savegame writer does the same (flags 1, then
  0x101, duke!0x100ea5f4/0x100ea612). Flags are the CRT's `_O_*`, masked 0x70b before `_open`; the VFS init sets
  0x8000 (`_O_BINARY`) at 0x4a220c.
- **Lifetime**: one slot, built when a new scale is applied (the row applies on release: `ui_scale_effective` holds the
  pre-press scale while the button is down), replaced (old shader's reference dropped with `ci_dec_refs`) only by
  another scale, kept across a return to 100, and dropped by `canvas_release_for_quit` with the baked shader
  ([exit-hang.md](exit-hang.md): a held reference blocks the quit).
- Verified by the in-game check: the engine's PNG loader inflates the stored deflate blocks (any inflater does; only
  compressed PNGs ship), and `mm_get_resource` of a freshly written `.shd` resolves its `source` beside it as
  `font.shd` does. Open: whether the fallback device is the `"$GAME"/base/` rw mount (the read-back proves the VFS
  serves the bytes wherever they land). `rend_max_texture_size` (0x41f970, tail jump to device vtable +0x9c, 0x100
  without a device) is not called: its device implementation was not read; the ladder tops out at 2048 and 200% needs
  1024x512.
