# Free camera: high-resolution photo

4x, 10x and 16x photos are correct on `gl` (verified in game). Each tile is read from the **composed screen**: DXGI
Desktop Duplication of the window's output, GDI's screen DC as the fallback, never a black file. `rend_get_image`
(the engine's own readback) **returns zeros on `gl`** (section 1). Each tile is rendered with the **fov narrowed n
times through `rend_projection_change`** (prism3d 0x4202d0, which nocull already calls) plus a **shear of the view
matrix** in the camera-push detour: no export takes a projection matrix, and a viewport n times the screen is
refused by the hardware. The tile maps the window's visible rect (the borderless window sits at y = -1) so tiles
abut. Tiles are stitched in a WIC PNG on a worker thread, two strips at most.

Measured: the calibration finds the render 1:1 at screen (0,0). Desktop Duplication was refused on the test desktop
(format 0x0A, R16G16B16A16_FLOAT: an HDR/advanced-colour desktop), so every tile came through GDI; 16x took 26.9 s
(1536 tile pushes, peak 472 MB held). The calibration locates 4-px bands by their centroid (section 1b); it ran over
several photos in one session with none aborted.

Method: D decompile, A disassembly, X xref, S string (Ghidra; duke_base 0x10000000, prism3d 0x400000, gl.dll and
p3shared.dll 0x10000000).

## 1. Readback

**`rend_get_image` is dead on `gl`.** Reading tiles with it gives all-black 4x and 10x photos (files written,
16/100 pushes and reads logged). The engine's own save thumbnails, written
by this same call through the capture callback below, are all-zero in the Enhanced install:
`duke/base/state/game01.bmp`, `game02.bmp` and `game_cur.bmp` each have 24630 bytes and 0 nonzero pixel bytes. In the
Steam install (stock renderer), `game01.bmp` has 24511 nonzero pixel bytes and `game_cur.bmp` 24556. So under GLDirect (opengl32 over d3d8 over dxwrapper's d3d8to9), glReadPixels returns
zeros: the lever is dead, whatever the timing. What follows records the call itself.

- **The capture callback.** duke!0x100e5f40 is inside the screen-capture *callback* 0x100e5e40, which the
  `screenshot` command (0x100e47c0, D) registers through pfn [0x101841c0] = `rend_screen_capture` (prism3d 0x41fb70:
  appends to the list 0x4a0940, count 0x4a0950, D). The import at [0x10126588] is `hbmp_resample(hbmp_t*, const
  hbmp_t*)` (IAT name at 0x10157af0, S): the ((w+1)*3 & ~3) * h buffer is the *resampled* copy for a sized shot
  (game +0x5a0/+0x5a4), not the readback. The callback then writes a BMP with 0x100c5e60 (`%s/dnmp_%05d.bmp`).
- **The readback is prism3d 0x41fb90** (D/A), called at 0x425c73, i.e. right after subsys's `call rend_end_frame`
  (0x425c6e) and before `rend_swap_clear` / `rend_swapbuffers`: `rend_get_params(&w, &h, 0)`, `mem_sys_alloc(w*h*3)`,
  `rend_get_image(buf, rect)` with rect = `{0, (float)W, (float)H, 0}` (stack B+0x10..0x1c, A 0x41fbc4-0x41fbfc),
  then every registered callback with an hbmp `{w, h, ..., type 1, data at +0x1c}`.
- **`rend_get_image` 0x41f9a0** (A): cdecl, two arguments, plain RET; `this = [0x4a0938]` (the device),
  `call [vtable+0x90]` with `buf, rect`.
- **gl's +0x90 is gl!0x100032b0** (vtable 0x100081f8, slot read 0x10008288, A): `ftol(rect[1]-rect[0])` = w,
  `ftol(rect[2]-rect[3])` = h, then the call `[this+0x5000]` with `(ftol(rect[0]), ftol(rect[3]), w, h, 0x1907 GL_RGB, 0x1401
  GL_UNSIGNED_BYTE, buf)` (glReadPixels' signature; S "glReadPixels" in gl.dll) and `hbmp_flip(1, w, h, buf)`, `RET 8`.
  No glReadBuffer call there, so a double-buffered context reads **GL_BACK**: the frame being drawn.
- **Pixel layout:** duke's BMP writer 0x100c5e60 (D) reads type-1 rows **top first**, bytes **R, G, B** (it swaps to
  BMP's B, G, R) at stride `(w*3 + 3) & ~3`. So `photo_stride` pads to 4 and `photo_copy_tile` swaps to BGR.
- **Where we read:** the first thing of `probe_frame_body` after `rend_get_params`. That is inside our
  `rend_end_frame` entry detour: the world, the HUD and subsys's flushes (0x425c64/0x425c69) are done; gl's
  end_frame is a `ret` (render.md section 2), so it is the same back buffer 0x41fb90 reads one call later, without
  our overlays, which all draw after this point. Our own buffer, not a capture callback: a callback runs after our
  draws (they happen inside rend_end_frame), and the engine would allocate w*h*3 a frame.

## 1b. The screen capture (used instead of the readback)

- **Desktop Duplication** (`dd_open`, photo.cpp): dxgi.dll and d3d11.dll are loaded by name (system DLLs). The
  output whose `Monitor` is `MonitorFromWindow(game window)` is found, a D3D11 device is made on its adapter,
  `IDXGIOutput1::DuplicateOutput` is called once per photo, and a staging texture the size of the visible rect is
  made. Refused for a rotated output or a desktop that is not `DXGI_FORMAT_B8G8R8A8_UNORM` (HDR). Each read is
  `AcquireNextFrame(100 ms)`, `CopySubresourceRegion` of the visible rect (output-relative), `ReleaseFrame`, `Map`.
  The mouse pointer is not part of the duplicated image. Closed when the photo ends.
- **GDI fallback** (`gdi_grab`): `BitBlt` from the screen DC into a top-down 32-bit DIB kept for the photo. It is used
  per tile when duplication failed, timed out or saw only black, and for the whole photo once duplication is refused
  or loses access.
- **Black check:** `photo_copy_bgra` reports whether any pixel of the tile is not black. A capture that only sees
  black never finds the calibration marks (below), so it aborts (`PHR_CALIBRATION`) instead of writing a black file.
  Black tiles after that are kept (a void corner of the level is black) and counted in the end line.
- **Timing** (`PHOTO_HOLD_DRAIN` 4, `PHOTO_HOLD_CAPTURE` 6): the screen lags our push by up to three queued presents
  and one composition, so each tile is pushed for several frames. At the 4th, the duplication's pending frames are
  dropped. From the 6th, the next composition is read, and that composition shows this tile. Nothing of ours is
  drawn while a tile is up: the progress line draws only between rows (WAIT) and while the encoder drains.
- **Client and calibration** (without it every photo aborted at start: "the client is not the 3440x1440 render"). Our borderless window is one row taller than the mode on purpose (3440x1441 at (0, -1); display.md: it
  keeps GLDirect windowed). So the client may be the render size or one row taller, and where the render lands in it
  is measured, not assumed. On arming frames, once the world is frozen and the HUD hidden, our canvas draws 4-px bands
  in four saturated colours 8 px in from each edge of the render (rows 8..11 and H-12..H-9, columns likewise;
  `photo_cal_band_at`). After `PHOTO_HOLD_CAPTURE` frames, the client's visible rect is read (Desktop Duplication,
  else GDI). `photo_band_centroid` finds each band's fullest line (more than half that colour) and takes the
  coverage-weighted mean of the lines within 6 of it. `photo_cal_axis` accepts 1:1 when the two bands' distance on
  screen matches the render's within 0.5 px, and takes the rounded mean offset as the screen position of render 0.
  The `photo: calibration` log lines give the measured centroids, 1:1 or not, how far the distance is off and the
  render's screen origin; the end line's Desktop Duplication/GDI/black counts show which reader worked.
  - **Why bands:** 1-px marks at render rows 1 and H-2 worked three times in a session, then twice read the top
    mark one row up ("render rows 1,1438 at capture rows 0,1438") and refused a window that was 1:1 (columns and the bottom row exact, earlier photos seamless). A line that
    lands on two rows, or scene colour at the screen edge, moves a centroid by a fraction of a pixel; it moved the
    first-row rule by a whole row.
  - **A real stretch (1440 over 1441) is still refused, not resampled** (a chosen rule): the bands' distance
    grows by ~0.99 px, past the 0.5 px tolerance. A resampled tile is not the render's pixels and its seams would blur.
  - Only after the calibration are the strips sized, the file named and the encoder started (`prepare`).
- **Visible rect** (`photo_visible`): the render's on-screen rect (from the calibration) intersected with its
  monitor. If the GL viewport is the bottom 1440 rows of the 1441-row backbuffer, render rows 0..1439 are client rows
  1..1440, all on screen. A scaled or DPI-virtualised client aborts at start; a move or resize during the capture
  aborts (`PHR_SIZE`).
- **Tile maths with a visible rect** (`photo_tile_offsets`): the big image is the visible rect magnified n times,
  n*w x n*h. Tile (r, c)'s render pixel x shows big pixel c*w + (x - vx), so ox = 1 - n + (2*vx*(n-1) + 2*c*w)/W and
  oy = n - 1 - (2*vy*(n-1) + 2*r*h)/H; with the whole render visible these reduce to 2c+1-n and n-2r-1. tb_0356
  proves it for the 3440x1441 window at y = -1 (n = 10) and for a fully visible render (n = 2); tb_0360 pins the
  visible rect.

## 2. Off-axis tiles

- **No matrix lever.** prism3d exports `rend_projection`, `rend_projection_change`, `rend_projection_get`,
  `rend_get_proj_matrix`, `rend_set_projection_type`, `rend_viewport`, `rend_set_workspace` (exports dump); none takes a
  matrix. `rend_projection_change` 0x4202d0 (D) writes (near, far, fov) to the stack top and marks the device and the
  frustum dirty. The device rebuilds in `r_device_t::set_projection` p3shared 0x1001e7b0 (D): mode 1 reads
  `rend_projection_get` and `rend_get_workspace`, then vtable+0xa0. gl's +0xa0 is gl!0x10003120 (slot 0x10008298, D):
  `glMatrixMode(GL_PROJECTION); glLoadIdentity(); glFrustum(-x, x, -y, y, near, far)` with x, y from tan(fov/2)*near
  and the workspace aspect: **always symmetric**. `rend_get_proj_matrix` 0x4200f0 (D) is the same formula (l = -r).
- **Viewport trick refused:** `rend_viewport` 0x4208c0 (D) pushes an NDC rect onto 0x4a1bb0 and marks the frustum
  dirty. A tile would need a viewport n times the screen (55040 px wide at 16x on 3440), past GL_MAX_VIEWPORT_DIMS on
  common hardware, and GLDirect draws through D3D9, whose viewports must lie inside the render target.
- **The lever: fov plus a view shear** (`photo_tile_matrices`, photo.h). With row vectors and w = z_view (render.md
  section 3), x_ndc = m0 * x_view / z_view. Tile (r, c) needs x_tile = n * x_ndc - ox with ox = 2c + 1 - n (and
  y_tile = n * y_ndc - oy, oy = n - 2r - 1, row 0 at the top). The fov with tan(fov_t/2) = tan(fov/2)/n gives
  m0_t = n*m0 and m5_t = n*m5. The pushed view gets x' = x - k*z, y' = y - m*z with k = ox/m0_t, m = oy/m5_t, so
  m0_t*x'/z = n*x_ndc - ox. The inverse gets forward += k*right + m*up; right, up and the eye are unchanged.
  m0_t and m5_t are read back with `rend_get_proj_matrix` after the change, so the shear uses the engine's own numbers.
- **Where:** `photo_push` in view.cpp's freecam branch, after `nocull_push_projection`: after wm_draw's camera builder
  (duke!0x1008c4c0, which puts its cached projection back every frame) and before the frame's frustum and draw
  (freecam-pause.md section 6). The base (near, far, fov) is read during arming, after nocull's paused re-apply, so it
  is the view the player saw; the builder undoes the narrowed fov at the next frame's wm_draw.
- **Proof (pure):** tb_0356 projects eight world points through the full view at n*W x n*H and through their tile at
  W x H (n = 2 and 10, 3440x1440): every point lands within 0.05 px of its big-image pixel, and the neighbouring
  tile across the row and the column agrees on the same point; view * inverse stays the identity.
- **Consequences:** depth is untouched (z' = z), so the z-buffer and fog by depth match the full view; culling is
  forced visible while flying (nocull); billboards built from the inverse's right/up rows keep their size. Anything
  that uses the inverse's *forward* row gets the tile centre's direction.

## 3. Overlays, HUD and freeze

- **Freeze:** freecam.cpp's pause rule is armed while `photo_busy()` as well as with the row (process_sleep on the
  player's world, freecam-pause.md). **HUD and overlays:** shot.cpp's screenshot mode is active while `photo_busy()`
  (ui_whide on `screen/game_view`, our overlays skip). Both are restored by the same rules when the photo ends.
- **Arming** waits for two consecutive frames with the pause holding, screenshot mode on and the panel's closing
  fade over (`PHOTO_SETTLE_FRAMES`): the first counts the frame that set them, the second is the first frame drawn
  without them. Given up after 120 frames.
- **Our progress line** ("Photo n/N" between rows, then "writing the PNG, strip k/N") draws only in WAIT and DRAIN:
  the capture reads the screen, so nothing of ours may be on it while a tile is up (section 1b).
- **Held still:** while capturing, the camera ignores mouse and keys, and the Teleport, Copy view and Paste view keys
  are swallowed without acting (input.cpp), so every tile is from one pose with Duke where he was.

## 4. Encoder, threads and memory

- A strip is one row of tiles: n*W x H x 3 (148 MB at 10x on 3440x1440, 226 MiB at 16x). Two slots at most: the one the
  tiles fill and the one the encoder writes; the second is allocated on first need, and if that is refused each row
  waits for the encoder instead (one strip in flight, logged). Plus one readback (photo_stride(W) * H).
- **A strip is never one block.** One strip as a single block failed late in a session (log `photo: no memory for a 226 MB strip`, then `ABORTED after 0.1 s at tile 0/256`, for a 16x photo that had worked
  earlier): the 32-bit address
  space was fragmented, so no 226 MB hole was left, though the memory was there. Each strip is up to 64 chunks of
  whole rows, each at most 16 MiB (`photo_chunk_rows`, `photo_chunk_count`, `photo_chunk_of`; tb_0476): at 16x on
  3440x1440 that is 15 chunks of 101 rows (15.9 MiB), the last 26 rows. Tiles are copied across chunks
  (`photo_copy_bgra_chunks`) and the worker hands WIC one `WritePixels(rows, stride, rows*stride, chunk)` per chunk in
  order (WIC takes consecutive line batches), checking the stop between chunks. A strip's chunks are allocated all or
  none; the first strip refused aborts at start (`PHR_START`), naming the refused chunk; the second refused falls back
  to one strip in flight.
- `photo_worker` owns COM and WIC (ole32 loaded by name; CLSID_WICImagingFactory1, PNG container, 24bppBGR; GUIDs by
  value, no new import libraries). One `WritePixels(H, stride, bytes, strip)` per strip, rows in order, then the
  frame and encoder Commit (one call per chunk, above). Main and worker share only interlocked scalars (slot state and row, stop, failed, done,
  strip count, final size written once before `done`) and the strip bytes the slot state hands over.
- An abort sets stop; the worker checks it before every strip (also when the next strip is already
  queued), exits, and deletes its own partial file. Buffers are freed only once
  the worker has exited. The quit path waits for it at most 5 s.
- **Log:** `photo: address space: largest free block .. MB, .. MB free in all, .. regions walked in .. ms` (one
  VirtualQuery walk at the key), `photo: a .. MB strip in .. chunks of .. rows (.. MB each, the last .. rows)`,
  `photo: no memory for a .. MB strip: chunk k/K of .. MB refused` or `photo: a second .. MB strip was refused at chunk
  k/K; one strip in flight`, `photo: start ...` (size, tiles, strips, level, path), one `photo: strip k/n encoded in .. ms, process
  private .. MB` per strip, and `photo: done in .. s, .. bytes, peak held .. MB, process private peak .. MB, pushes,
  reads` or `photo: ABORTED after .. s at tile i/N: <reason>`.

## 5. The level's name

- The `map` command with no argument prints `current map: %s` (S 0x10174008) with `[game+0xEC] + 0x48` (A
  0x100e3b99-0x100e3bad): an inline string in the world object. `game+0xEC` is the current world, which is
  `player+0xE0` (freecam-pause.md section 2). Read guarded, 64 bytes, printable and NUL-terminated, else "level".

## 6. The Photo folder

- The save folder is a setting: `[freecam] photofolder=` (UTF-8; empty is the default `<Pictures>\Nuke Panel`). It is config.cpp's
  first string key (`kStrFields`: read, written only when changed, default "", a value over 259 bytes refused and
  logged rather than cut; one pair of surrounding quotes dropped as GetPrivateProfileString drops them). tb_0499-0501.
- View > FREE CAMERA, under Photo key: the **Photo folder** row, a well with the folder shortened from the left
  (`photo_folder_shorten`: whole leading components first, `...\Game Screenshots\Nuke Panel`), dim while it is the
  default; non-ASCII characters draw as `?` (the font is ASCII). A click opens `IFileOpenDialog` with
  `FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM`, owned by the game window and starting in the folder now in use. OK saves
  (choosing the default folder itself stores ""), Cancel changes nothing.
- The picker runs in `panel_frame` after the panel's canvas pass, before the save, and **blocks the main thread**
  until closed. Around `Show`, `input_modal_begin/end` let go of the cursor hold, the freecam grab and capture, any
  clip, and a hidden cursor display count, then put back what they changed and drop the panel's button state (the
  dialog took the release). COM: `CoInitializeEx` apartment-threaded; S_OK and S_FALSE are balanced by
  `CoUninitialize`, RPC_E_CHANGED_MODE is not. ole32 and shell32 are loaded by name (the build links no others).
- `build_path` takes `photo_folder_dir`: the configured folder, created with its parents (`SHCreateDirectoryExW`)
  when absent; if it cannot be created, `photo: the Photo folder .. could not be created (err); the default
  instead`. The path is built once at the capture's start, so a photo in progress keeps its folder.
- **Log:** `input: modal dialog: cursor let go (clip .., n show step(s), mouse_state .., panel ..)`, `input: modal
  dialog closed: ..`, then `photo folder: <path>` / `photo folder: the default, <path>` / `photo folder: cancelled;
  unchanged` / `photo folder: <step> failed (hr ..)`. The blocked frame shows as one `hitch:` line.

## Verified in game

- **Chunked strips:** a 16x photo late in a long session, which once refused the 226 MB strip, now succeeds. The
  `address space` line gives the largest free block (below 226 MB a single strip would have failed), the chunk line
  shows 15 chunks, and the file is seamless at the chunk boundaries (every 101 rows of a strip at 16x on 3440x1440).
- **Photo folder picker** on gl in the borderless window: the dialog shows over the game with a usable cursor, the
  game gets its mouse back after OK or Cancel (the panel still open, clicks working), and the engine takes the
  blocked frame (one long `hitch:` line) without harm.

## Open questions

- That six frames of holding (`PHOTO_HOLD_CAPTURE`) are always enough: a tile showing the previous tile's view would
  show as a repeated or shifted block.
- Seams from client-side effects that do not sleep with the world (particles, flares) or that size themselves in
  screen pixels (a flare drawn at tile size is n times smaller in the photo). Look along tile boundaries.
- Anything drawn over the game window by other programs (overlays, notifications) is in the composed screen too.
- That WIC's PNG encoder streams strips rather than holding the image: the per-strip `process private` lines show it.
- A raw image over 4 GiB (screens larger than 3440x1440 at 16x): WIC may refuse at SetSize or WritePixels. That path
  aborts and deletes the file.
- dx8: not tested; only `gl` is.
- The ini bytes of a non-ASCII folder: `WritePrivateProfileStringA` passed UTF-8 through unchanged on a single-byte
  code page (tb_0501); a double-byte system code page is untested.
