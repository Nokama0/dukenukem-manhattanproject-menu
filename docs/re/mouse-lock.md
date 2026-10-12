# Mouse locked in other programs after leaving the game

Cause: dinput8's exclusive-mouse raw-input row `NOLEGACY|CAPTUREMOUSE` (`RIDEV_NOLEGACY|RIDEV_CAPTUREMOUSE`). dinput8 registers and removes it
asynchronously; when focus leaves mid-change (Escape menu, Start, alt-tab, quit), Windows keeps suppressing legacy
clicks for every window except the foreground one until a desktop switch (Ctrl+Alt+Del). The symptom is clicks dead
while the cursor and keyboard work. Fix: the engine mouse is always shared (`NONEXCLUSIVE|FOREGROUND`, level 6), so
dinput8 never registers `NOLEGACY`, and the mod's own cursor hold hides the OS cursor in play. Verified in game: all six
checks in "The fix" pass; the log shows mouse level 5 -> 6 once, `NOLEGACY` only before it, and 10 focus losses with
the hold released each time. Hypotheses (a), (b), (c), (d) are refuted (below). The logging-only watch (mousewatch) stays.

Method: the mod's own log (`nukepanel.log` and the engine log), the Ghidra MCP on input.dll, prism3d and dinput8
(10.0.26100.8521), and in-game runs of the lock recipe.

- Removed diagnostics: `[display] borderless_variant` 1 and 2, the NonRudeHWND tag and the
  `SHQueryUserNotificationState` (QUNS) logging. An old `borderless_variant` ini value is skipped. Variant 0, the
  monitor plus one row, is the borderless window. The raw-row change lines (`hotkeys: raw rows change N`) and
  mousewatch's `gui threads` line stay.

## The fix

- **Shared mouse, always.** hotkeys.cpp sets the engine mouse to `NONEXCLUSIVE|FOREGROUND` (6) on every device and
  window, with no panel row. The `[input] mouse_nonexclusive` key is gone; an old ini value is skipped by the loader.
  Log: `hotkeys: mouse <dev> window <hwnd>: level 0x05 -> 0x06 set; ...` per device/window.
- **Cursor hold (input.cpp, rule `cursor_hold_block` in ownership.h).**
  - **When:** our game window is in front and not minimised, the panel is closed, the freecam is off, and the engine
    is not in mouse_state 1.
  - **What:** `WM_SETCURSOR` over the client answers `SetCursor(NULL)`, and every frame the cursor goes back to the
    client centre (`cursor_hold_centre`) unless it is already there.
  - **Release:** at once on WM_ACTIVATE inactive, WM_ACTIVATEAPP 0 or WM_KILLFOCUS, on the panel opening, on a
    freecam toggle, and on a window replaced or destroyed; the frame rule catches the rest.
  - **Nothing persists:** no ClipCursor and no ShowCursor call; only SetCursor and SetCursorPos.
  - Log: `input: cursor hold on|off (<reason>); on N time(s), M warp(s) so far`, once per change, with cumulative
    counts.
- **What yields to the hold:**
  - **The engine's menus do not need the OS cursor.** prism3d imports GetCursorPos with one call site, FUN_00406e90,
    which is the mouse_state 1 capture (SetCapture, GetCursorPos, ShowCursor(0)) [MCP xrefs]. The UI reads the
    software cursor, `inp_cursor_info` (for example, FUN_00423600), which is fed by DirectInput relative events
    ([mouse-aim.md](mouse-aim.md) section 4). duke_base.dll, sys/input.dll and p3shared.dll import no cursor function
    (byte search of the Enhanced binaries for GetCursorPos, SetCursorPos, ShowCursor, SetCursor, ClipCursor,
    ScreenToClient).
  - **The panel** (its arrow; the crosshair preview is in the panel): the hold yields.
  - **The freecam** (its own hide and re-centre, `grab_set`; screenshot mode is a freecam state): the hold yields.
  - **The engine's mouse_state 1** (its own capture and hide, 0x406e90): the hold yields.
  - **Mouse aim** builds its cursor from `GetDeviceState` mickeys and `inp_cursor_info`, never the OS cursor: it
    keeps working under the hold. SetCursorPos makes no raw input, so the warp feeds no deltas to the engine.
- **Why shared is safe in dinput8:** the `WH_MOUSE_LL` hook proc never swallows (the exclusive count `+0x1c` stays 0)
  and raw mode uses the INPUTSINK row. The OS cursor is no longer hidden or held by dinput8; on two monitors it could
  leave the borderless window, which is why the mod hides and re-centres it itself.
- **Known gap:** between two frames (about 6 ms at 175 fps) a fast flick can carry the cursor past the window edge.
  A click in that moment lands on the other window. ClipCursor would close the gap but is not used.
- **The read path does not need exclusive mode** (input.dll, MCP): the mouse init 0x100011a0 sets the buffer size
  (0x10001270, `SetProperty(DIPROP_BUFFERSIZE, 0x80)`) and nothing else; the read 0x10001320 is `GetDeviceData`
  (vt+0x28) on relative buffered events (offsets 0 X, 4 Y, 0xc-0xe buttons), re-acquiring on INPUTLOST/NOTACQUIRED.
  Buffered relative data flows in nonexclusive foreground mode. prism3d's `inp_set_active` only touches the OS cursor
  (0x406f00: SetCursorPos, ReleaseCapture, ShowCursor(1)) when `mouse_state` is 1, the window-message mode.
- **Checks (all pass in game):**
  1. **Menus:** the Escape menu and the in-level menus. The menu pointer moves and clicks as before, and no Windows
     cursor shows over the game.
  2. **Panel:** open it; the arrow shows and works, including the crosshair preview; close it; the cursor hides
     again.
  3. **Freecam:** fly, use the screenshot mode and teleport. The steering and buttons are as before.
  4. **Mouse aim:** the reticle follows the mouse; firing goes where it points.
  5. **Recipe:** Escape, then the Windows key, then clicks on monitor 2; then alt-tab and click on monitor 2. Clicks
     work, with no Ctrl+Alt+Del. In play the cursor never reaches monitor 2.
  6. **Quit:** after quitting, clicks work everywhere.
  - In the log: `level 0x05 -> 0x06 set` for each mouse device, the mouse row `0x100 INPUTSINK` (`0x230` only between
    a new device and our set), and `cursor hold off (focus lost)` on each alt-tab.

## Hypothesis (e) CONFIRMED: dinput8's exclusive-mouse raw-input row

- **The stock mouse is measured with its raw row while focused.** dinput8's exclusive engine mouse has
  `mouse flags 0x230 NOLEGACY|CAPTUREMOUSE`, registered in our process.
- **With the mouse shared** (the former `[input] mouse_nonexclusive=1`, level 5 -> 6), the mouse row is `0x100 INPUTSINK` or none for the rest of the session,
  across 24 row changes. NOLEGACY never appears again.
- **Same session, no lock:** 11 focus losses (6 to Start, 4 by alt-tab, 4 within 1 s of the menu opening) and a quit.
  Mouse look and firing work, as input.dll reads only `GetDeviceData`.
- **In exclusive mode:** locks in 5 of 6 sessions, including the variant runs below.
- **Mechanism:**
  - **The symptom is legacy suppression.** Clicks are dead while the cursor moves, which is what `RIDEV_NOLEGACY`
    does: no WM_*BUTTON* messages, while cursor motion is unaffected. Clicks still reach the foreground app, and a
    desktop switch resets it.
  - **dinput8 runs in raw mode in this process.** hotkeys logs the keyboard row at start-up as 0x200 and then 0x100
    after our level change. The exclusive mouse registers `NOLEGACY|CAPTUREMOUSE` (0x230, table 0x100274d8) while
    it is acquired, and removes it on unacquire, asynchronously from its worker.
  - **Every trigger moves focus while the engine re-acquires:** the Escape menu, Start, alt-tab and quit.
  - The exact win32k state cannot be read from user mode. The single-variable test (shared mouse) removes the lock.
- **Borderless** only changed timing.
- **Raw rows are measured while focused.** `hotkeys: raw rows change N: mouse <row>; keyboard <row>` is logged from
  the frame whenever the process's rows change (polled once a frame, since dinput8's worker registers and removes
  them asynchronously). Each `hotkeys: mouse GetDeviceState -> 0x... (change N; ...)` line carries the rows too. A row
  reads `flags 0x230 NOLEGACY|CAPTUREMOUSE target ...`, `flags 0x100 INPUTSINK ...` or `none registered`.
- **The mouse state machine** (hotkeys.h, `HK_KIND_MOUSE`; the keyboard's treatment): Unacquire, then
  `SetCooperativeLevel(game window, 6)`, then Acquire only if it was acquired. Re-applied per new device and per new
  window. Line: `hotkeys: mouse <dev> window <hwnd>: level 0x05 -> 0x06 set; Unacquire ...,
  SetCooperativeLevel ... (dinput8.dll), Acquire ...; raw before: ...; after: ...`.

## Variant runs (refuted (d))

- The variants were an ini-only key `[display] borderless_variant` (0..2, default 0) in nukepanel.ini, logged as
  `display: borderless_variant N (...)`; variant 1 logged `display: NonRudeHWND on <hwnd> (at creation|followed
  window): set`; `SHQueryUserNotificationState` was logged on the first companion line (`; shell QUNS_...`) and once
  at the first frame (`mousewatch: hh:mm:ss first frame: shell QUNS_...`). `QUNS_RUNNING_D3D_FULL_SCREEN` or
  `QUNS_BUSY` would mean the shell counts the game as fullscreen; `QUNS_ACCEPTS_NOTIFICATIONS` means it does not.
- **Results:**
  - **Borderless off:** no lock (1 trial).
  - **Variant 0 with Win key + alt-tab:** lock.
  - **Variant 1, NonRudeHWND:** first run locked after quitting the game, cured by Ctrl+Alt+Del; second run, no lock
    at all.
  - **Variant 2, monitor minus a row:** locked with the recipe and again after quitting. During the second lock,
    clicks worked in the foreground app and nowhere else, until Ctrl+Alt+Del.
  - The test machine's taskbar is set to auto-hide.
- **Shell state:** `SHQueryUserNotificationState` reads `QUNS_ACCEPTS_NOTIFICATIONS` at the first frame in variants 1
  and 2, and on every watch line. It reads `QUNS_APP` only while Start is in front. The shell never treated the game
  as a fullscreen app, and variant 2 still locked.
- **Borderless A/B with the recipe (Escape or alt-tab, Windows key, click on monitor 2):** Borderless on locks (3 of
  3, cured by Ctrl+Alt+Del); Borderless off (captioned window) does not.
- **Hypothesis (d), the shell treats the borderless window as fullscreen: REFUTED.** The borderless window is
  WS_POPUP at 0,-1 3440x1441: it covers the monitor. The variants were:
  - **0:** the monitor plus one row above it.
  - **1:** the same rect, plus `SetPropA(hwnd, "NonRudeHWND", 1)` at WM_NCCREATE through the class proc and on every
    window the input module follows.
  - **2:** the monitor minus its bottom row (0,0 3440x1439). r_mode stays 3440x1440: the stock captioned window
    already runs a smaller client (3440x1427) than r_mode as GLDirect's windowed device, with nothing seen stretched
    ([display.md](display.md) "Data facts").
  - The first-frame and restart checks compare against the variant's rect, and refuse a client equal to the display
    mode (GLDirect's fullscreen test).

## Observed lock cases

- **Recipe:** Escape in game (the menu opens 359 ms before the loss), then the Windows key (Start =
  `Windows.UI.Core.CoreWindow` foreground), then move the mouse to the second monitor: clicks are dead. Ctrl+Alt+Del
  alone ends it. The wheel was not tried.
- **Reproduction with the watch (hypothesis (c) refuted):**
  - **Foreground:** the Start CoreWindow stays foreground while the cursor is over Chrome on monitor 2. After the
    alt-tab, Chrome is foreground, then none (the secure desktop during Ctrl+Alt+Del).
  - **GetGUIThreadInfo:** reads flags 0x0 and no hwndCapture on every thread measured: foreground (Start, then
    Chrome), under-cursor (Chrome), taskbar and ours. No capture and no menu or move-size mode anywhere they look.
- **Capture with the engine mouse exclusive (freecam session):**
  - Focus loss to `Windows.UI.Core.CoreWindow`, the game back about 3 s later; `escape` logged and the main menu
    opened 438 ms before the next loss (also CoreWindow). A loss followed a menu opening by 375 ms earlier; 3 of 3
    lock captures had the menu opening just before the focus loss.
  - Alt-tab (`ForegroundStaging`, held `A`), then the cursor moves over Chrome on the second monitor, then the secure
    desktop (cursor UNREAD, foreground none), then Chrome in front.
  - Every line, before and during: `raw none` (our process had no raw-input registration at that moment), the
    dinput8 worker's kbd/mouse count 0, exclusive 0, hook 0x0, engine mouse `0x8007000c` (not acquired), no capture,
    no clip, cursor shown, `our grab 0`, and the GetLastInputInfo tick advancing. Nothing in our process held the
    mouse by any measured channel at that point (hypothesis (b) refuted by its own kill criterion).
- **Capture with first person and borderless on:** alt-tab with `held A`, foreground `ForegroundStaging`.
  - The cursor stayed `still` for 24 lines, then moved again with Chrome still in front. Every line read
    `GetCapture 0`, no process captures, clip = the whole desktop, `cursor flags 0x1` (shown), engine mouse
    `0x8007000c`, `our grab 0`, held `-`. 175 fps throughout.
  - The engine logged `escape` in the same second, so the main menu opened as focus left (engine log); no other
    deactivation in the session coincided with `escape`.
  - After one renderer restart at start-up the log shows a new keyboard and a new mouse device; none followed later.
  - **`still` alone is not evidence of a lock.** Two other alt-tabs show the same shape (the cursor moves once, then
    sits, for 48 s and 22 s). The watch cannot tell a frozen cursor from an idle hand; only a report marks a lock.

## Hypothesis (a): the mouse keeps the destroyed window -- REFUTED by reading

- input.dll's device init 0x10002690 [A] calls the engine's `hw_get_context` (pfn 0x10009b5c) and passes `{ctx, hwnd}`
  to the mouse init 0x100011a0. That init runs `SetCooperativeLevel([arg+4] = hwnd, 5)` (0x100011ee-0x10001200).
- The input module's hw vtable is 0x10006188. Entry +8 is init 0x10002690 and +0xc is shutdown 0x10002730 (mouse
  0x10001240: Unacquire, then Release). +0x10 is suspend 0x100028b0 (set_active(0), shutdown) and +0x14 is revive
  0x100028d0 (init, set_active(1)).
- prism3d `hw_set_new_context` 0x41bd60 [D] suspends every live hw module (0x41c110 -> `hw_suspend`, vt+0x10). It
  then sets the window slot 0x49c7a4 and revives the modules (0x41c150 -> `hw_revive`, vt+0x14). **Every window change
  recreates the DirectInput devices with the window current at that moment.**
- gl's restart calls it twice:
  - first with hwnd 0 (mode_shutdown). The engine log records `<inp> cannot set mouse cooperative level` and
    `... keyboard ...`: SetCooperativeLevel(NULL, 5) fails;
  - then with the new window (mode_init). That pass logs no warning, so the device the game uses afterwards is bound
    to the live window.
- Re-setting the mouse's level after a restart (the hotkeys pattern) would therefore change nothing.

## Hypothesis (b): what in our process could freeze the cursor (dinput8 10.0.26100.8521)

dinput8 loaded via the MCP (the system SysWOW64 `dinput8.dll`, base 0x10000000, analysed in the headless
session, not saved). The mouse acquire/unacquire is 0x10011f90. It takes one of two paths.
- **Without internal flag 0x10000: the window-subclass path** 0x100123fb. It subclasses the cooperative window with
  0x10012210.
  - Acquire (0x10012485): SetCapture, ShowCursor(0), ClipCursor(rect), and SetCursorPos back to a saved point on
    every mouse message.
  - Release runs asynchronously on the window's thread: flag 2 is set, then `SendNotifyMessageW(hwnd, 0)`. It does
    ReleaseCapture, ShowCursor(1), ClipCursor(0) and unsubclasses.
  - Excluded in the captures: there is no capture, the clip is the whole desktop and the cursor is shown. The pinning
    needs mouse messages, which an uncaptured background window does not get.
- **With flag 0x10000: the worker path** 0x10011869. It keeps per-type counts in the worker context (global
  0x1003125c; for the mouse, +0x18 = acquired, +0x1c = exclusive-acquired, +0x20 = the `WH_MOUSE_LL` handle).
  - **Raw mode** (global 0x100310a4 != 0, the hidden window 0x100310a8 exists):
    - acquire registers the table row 0x100274d8. The exclusive mouse row is `NOLEGACY|CAPTUREMOUSE` ([hotkeys.md](hotkeys.md)
      section 2);
    - unacquire always registers `RIDEV_REMOVE` (0x1000b4dd), then registers the `INPUTSINK` row if the count is
      still nonzero.
  - **Hook mode** (0x100310a4 == 0): the switch is permanent for the process. It happens when the hidden window
    cannot be created (0x10011a10) or when any row registration fails (0x1000b48c returns E_FAIL, 0x1001193b).
    - The worker thread 0x10011a10 then installs `WH_MOUSE_LL` (table 0x100010f0: 13 -> 0x100127e0, 14 -> 0x10011df0)
      whenever the mouse count is nonzero (0x100119aa).
    - **The hook proc 0x10011df0 returns 1, which swallows the event, whenever the context is null or its
      exclusive count (+0x1c) is nonzero.** A leaked count or hook would freeze the cursor system-wide from dinput's
      worker thread while the keyboard keeps working.
  - **Why it is not the cause here:** zero counts and no LL hook during a lock; the process was in raw mode at
    start-up (the only raw rows logged); Ctrl+Alt+Del would not remove a persistent hook.
- **Outside our process** (consistent with the logged fields, not readable from the mod): another program's low-level
  mouse hook, or a raw-input sink/legacy-suppression state that win32k leaves stale. A desktop switch (Ctrl+Alt+Del)
  resets that state, which matches the cure.

## Hypothesis (c): a foreign capture or modal mode -- REFUTED

A mouse capture or modal mode held by a thread in another process (the Start menu host, the shell) would fit
every field. The watch's `GetCapture` and "process captures" see only our own threads, so the `gui threads` line
(below) reads `GetGUIThreadInfo` for the foreground thread, the thread under the cursor, the taskbar thread and our
main thread: hwndCapture (with its class and process), the flags (move/size, menu mode, popup menu, system menu) and
hwndMenuOwner. No foreign capture and no modal flag showed during a lock.

## Next capture: measurements built into the watch

Built into mousewatch.{h,cpp}. After every mousewatch line a companion line `mousewatch: hh:mm:ss   last input N ms
ago (tick T); raw ...; dinput8 ...` carries 1-3 (the main line is near the log's 1 KB cap). The dinput8 reads are on
only when its RT_VERSION fixed block reads exactly 10.0.26100.8521 (logged once per base: `mousewatch: dinput8 at ...
worker reads on/off`). The RVAs: hook proc 0x10011df0 returns 1 when `[0x1003125c]+0x1c` is nonzero and chains `+0x20`;
FUN_100119aa keeps type t's count at `+8+0x10t` and hook at `+0x10+0x10t` (t 0 keyboard, 1 mouse); worker 0x10011a10
clears 0x100310a4 when the hidden window fails. `HOOK mode` = 0x100310a4 is 0. Measurement 4 logs `main menu
opened/closed N ms before/after the focus loss` (menu = ui "screen"/"menu_main" shown; whether the in-game Escape menu
is that window is open).

1. **`GetLastInputInfo` tick.** If the tick advances while the cursor reads `still` and held reads `-`, input is
   arriving and the cursor is not following: a frozen cursor, not an idle hand. Whether swallowed LL-hook input
   updates that tick is open.
2. **`GetRegisteredRawInputDevices`.** Logs the mouse row's flags and target (0x230 = exclusive) and the keyboard row.
   A keyboard row while acquired means raw mode; none means hook mode.
3. **dinput8's worker state, read-only.** Reads 0x100310a4 (raw mode), 0x1003125c and its +0x18/+0x1c/+0x20 (mouse
   count, exclusive count, LL hook handle), plus +0x10 (keyboard hook).
   - These are hardcoded RVAs in a system DLL. They are justified only as gated diagnostics: enabled only when the
     loaded dinput8's file version is 10.0.26100.8521, with one guarded read per line.
   - A hook handle and exclusive count that are nonzero while the engine device reads NOTACQUIRED would show a leak
     (hook mode); raw mode or zero counts during a freeze put the cause outside the process.
4. Log `escape`/menu transitions within 1 s of a deactivation, to test the race of a menu opening just before a
   focus loss.

## What the log does and does not show

- Every alt-tab in play logs `mouse GetDeviceState 0x8007000c` (DIERR_NOTACQUIRED) on WM_ACTIVATE/WM_KILLFOCUS:
  dinput8 released the exclusive mouse as it should.
- At quit the same lines read `0x00000001`. That is **not** "still acquired": 1 is `hotkeys_mouse_probe`'s own
  "no device read" value (hotkeys.h), returned when the engine's mouse global is already null. input.dll's shutdown had
  run before the focus moved. A quit-time Unacquire would find no device, so none exists.
- The old watch logged after a focus loss only while a capture or a narrowed clip pointed at our process, so an
  alt-tab lock with neither left no line at all.

## The watch (mousewatch.{h,cpp})

Logging only; nothing is released or changed. Every line starts with the local wall-clock time (hh:mm:ss) so a report
can name the minute.
- On each deactivation message (WM_ACTIVATE inactive, WM_ACTIVATEAPP 0, WM_KILLFOCUS): one line.
- Then **once a second for 60 s after each focus loss, unconditionally** (`mw_due`), while no window of ours is in
  front: `mousewatch: hh:mm:ss inactive N s after the focus loss`. After 60 s, a line only while something points at
  us (capped at 600).
- Each line carries:
  - the engine mouse's `GetDeviceState` HRESULT (0 = acquired, 0x8007000c = released, 1 = no device);
  - every capture held by a thread of ours, `GetCapture`, and the clip against the virtual desktop (`RESTRICTED`);
  - `GetCursorPos` and whether it moved since the last line (`moved`/`still`);
  - `WindowFromPoint(cursor)`, its class and whether it is ours;
  - held buttons and modifiers from `GetAsyncKeyState` (`L R M A C S`, `-` for none): a logically stuck button or
    modifier is the classic symptom Ctrl+Alt+Del cures;
  - the foreground window and its process;
  - our game window's visible and topmost flags and its rect;
  - `W` in the held field for either Windows key.
- A second companion line, `mousewatch: hh:mm:ss gui threads:`, runs `GetGUIThreadInfo` on four threads: **F** the
  foreground window's, **U** the one under the cursor, **T** the taskbar's (`Shell_TrayWnd`, re-resolved if it dies),
  **G** our game window's. Per thread: `tid`, flags in hex with names (`CARET MOVESIZE MENU SYSMENU POPUP`),
  `capture` as `hwnd (class, exe, ours|FOREIGN)` or `-`, and `menu owner` / `move/size` when non-null (`active` /
  `focus` too, for F only). Identical threads collapse (`U=F`); `none` is a null window; a failed call prints `- (err N)`.

### Reading an occurrence

Read the lines of the minute the lock was noticed.
- `still` cursor with held `-` and no capture/clip: the lock is outside our process (nothing here holds it).
- A capture or `RESTRICTED` clip of ours: our process holds it; then read who (dinput8's window or our grab).
- A held `L`/`A`/`C`/`S` that was not pressed: a stuck key state; next, find who swallowed its key-up.
- `under it ... ours` while the game is not in front: our window covers the cursor (topmost/visible flags say why).
- GetDeviceState 0 while not in front: the exclusive mouse is still acquired.
- The `gui threads` line: a `FOREIGN` capture, or a `MENU`/`MOVESIZE` flag (or a menu owner / move-size window), on
  F, U or T during a lock would show hypothesis (c) and name the process.

## Open questions

- Whether a stale NOLEGACY row matters only while our process is in front (raw input without INPUTSINK goes to the
  foreground).
- Whether swallowed LL-hook input updates the `GetLastInputInfo` tick.
- Whether the in-game Escape menu is the `menu_main` window the menu-transition measurement watches.
