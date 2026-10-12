# Windows key and global hotkeys dead while the game has focus

Stock behaviour, fixed by the panel and verified in game. The engine opens its DirectInput keyboard with
`DISCL_NOWINKEY` (input.dll!0x10001568, `SetCooperativeLevel(hwnd, 0x16)`). On Windows 11, dinput8 turns that flag
into `RegisterRawInputDevices(keyboard, RIDEV_NOHOTKEYS)` while the keyboard is acquired. That flag suppresses the
Windows key and every `RegisterHotKey` hotkey, including ShareX's Scroll Lock. The panel's WndProc is not the
cause. Fix: on the engine's own keyboard device, Unacquire, `SetCooperativeLevel(hwnd, NONEXCLUSIVE|FOREGROUND =
6)`, Acquire. It ships as Settings > DISPLAY "Windows key and hotkeys" (section 6). With it, the Windows key and
Scroll Lock work with the panel closed, game keys are unaffected, alt-tab and panel open/close keep input, and the
switch off restores the stock block.

Method: Ghidra on input.dll and dinput8.dll; tags as in input-console.md: [D] decompile, [A] disassembly, [X] xref,
[S] string, [C] config. The input.dll addresses are at its preferred base 0x10000000. The dinput8.dll analysed is
the system SysWOW64 copy, 10.0.26100.8521 (base 0x10000000, no symbols; not saved to the
project).

## 1. What the engine asks for (input.dll)

- The keyboard init 0x10001500 [A] does three things:
  - `CreateDevice(GUID_SysKeyboard)`: the GUID at 0x100061d8 is `6F1D2B61-D5A0-11CF-BFC7-444553540000`. The device
    pointer is stored in the global **0x100099f0** (an `IDirectInputDevice8A*`).
  - `SetDataFormat(c_dfDIKeyboard @0x1000668c)` (vtbl+0x2C).
  - `SetCooperativeLevel(ctx->hwnd, 0x16)` (vtbl+0x34) at 0x10001568. The value 0x16 is
    `DISCL_NONEXCLUSIVE 2 | DISCL_FOREGROUND 4 | DISCL_NOWINKEY 0x10`.
- The mouse init 0x100011a0 is `SetCooperativeLevel(hwnd, 5 = EXCLUSIVE|FOREGROUND)` (pointer in 0x100099ec) [A].
  It only affects the mouse (section 2).
- The device init 0x10002690 runs both inits once, from 0x100026e6 and 0x100026f2 [X]. The shutdown path is
  0x10002730. Keyboard Acquire is 0x10001640 (vtbl+0x1C) and Unacquire is 0x10001660 (vtbl+0x20). Both are called
  from `set_active` 0x10002790 [A].
- input.dll imports only `DirectInput8Create` from outside the engine [X imports]. prism3d.exe, p3shared,
  duke_base, gl.dll and dx8.dll import no `SetWindowsHookEx`, `RegisterRawInputDevices`, `RegisterHotKey` or
  `SystemParametersInfo` (import dumps) [X].
- GLDirect's `opengl32.dll` subclasses the window and has a `bHotKeySupport` string [S]. It imports no hook,
  raw-input or hotkey API. A window procedure cannot block the Windows key or `RegisterHotKey`: the system acts on
  those before any message is posted. So GLDirect is excluded.

## 2. What dinput8 does with it (Windows 11 26100)

- **The raw-input registration table** is at dinput8!0x100274d8. Each entry is a 12-byte `RAWINPUTDEVICE`,
  indexed by `mode + 2*device` [D FUN_1000b48c]. Only the keyboard (usage page 1, usage 6) has a NOWINKEY row:

  | index | device | mode | dwFlags |
  |---|---|---|---|
  | 0 | mouse | non-exclusive | 0x100 INPUTSINK |
  | 1 | mouse | exclusive | 0x230 NOLEGACY\|CAPTUREMOUSE |
  | 2 | keyboard | non-exclusive | 0x100 INPUTSINK |
  | 3 | keyboard | exclusive | 0x230 NOLEGACY\|NOHOTKEYS |
  | 4 | keyboard | **NOWINKEY** | **0x200 RIDEV_NOHOTKEYS** |

- The function pointer 0x10026304 holds `RegisterRawInputDevices`, resolved at run time: it takes (table,
  count 1, size 0xC).
- Acquire and unacquire go through `FUN_10012590(flags, on)` [D]:
  - internal flag 1 means exclusive; internal flag 0x20 without flag 1 means NOWINKEY.
  - On the emulated path (flag 0x20000), `FUN_10011869` picks mode 2 for a non-exclusive keyboard with 0x20, which
    selects index 4, `RIDEV_NOHOTKEYS`.
  - Unacquire registers `RIDEV_REMOVE` from 0x100274c0 [D FUN_1000b4dd, table read].
  - The name "0x20 = NOWINKEY" comes from how the code behaves (next bullet), not from symbols.
- **Fallback paths do not match the symptom:**
  - The low-level hook proc 0x100127e0 (`WH_KEYBOARD_LL`, table 0x100010f0) swallows only VK 0x5B/0x5C (the
    Windows keys) when NOWINKEY is set [D].
  - The thread hook proc 0x100126d0 (`WH_KEYBOARD`, 0x100125ec) swallows only DIK 0xDB/0xDC [D].
  - Neither can block Scroll Lock. Only the raw-input `RIDEV_NOHOTKEYS` row explains both symptoms.
- **Microsoft's documentation of `RIDEV_NOHOTKEYS`:** application-defined hotkeys are not handled while the
  registrant has focus (e.g. ShareX's `RegisterHotKey`); system hotkeys such as Alt+Tab and Ctrl+Alt+Del still are.
  The Windows key being among the suppressed keys matches the symptom, and the fix (section 6) removes both.
- **Only while focused:** with `DISCL_FOREGROUND`, focus loss unacquires the keyboard, which registers
  `RIDEV_REMOVE`.

## 3. Our code (src/nukepanel/input.cpp, read only)

With the panel closed and the free camera off, `handle()` swallows only the panel key, the camera bind and the
teleport bind (lines 288-310). Everything else goes to `CallWindowProcA`. Even a swallowed WM_KEYDOWN cannot
matter: the Windows key and `RegisterHotKey` are acted on before the message reaches our thread. **Not ours.**
Builds before the fix had the same symptom, which is consistent with this.

## 4. Fix lever (least intrusive; nothing written to engine memory)

Take the engine's own keyboard object and change only its cooperative level. Make all three calls on the main
thread (the frame path), because DirectInput is single-threaded here:

1. **Find the device pointer by signature in input.dll's code** (no hardcoded address). Scan for
   `8B 54 24 08 8B 52 04 A1 ?? ?? ?? ?? 8B 08 6A 16 52 50 FF 51 34`, which is 0x1000155a-0x1000156e in the
   keyboard init. The imm32 at +8 is `&g_kbd` (0x100099f0).
   - Check that address lies inside input.dll's image, then read the pointer with one guarded read.
   - If the pointer is null, input.dll is not loaded or is in WndProc mode (`i_grabpointer 0`). Do nothing.
2. `hr = dev->Unacquire()` (vtbl+0x20). DI_OK means it was acquired; DI_NOEFFECT (1) means it was not.
3. `dev->SetCooperativeLevel(engine_hwnd, DISCL_NONEXCLUSIVE|DISCL_FOREGROUND)` (vtbl+0x34, value 6).
   - This call fails with DIERR_ACQUIRED if step 2 is skipped.
   - Pass the engine's top-level window (our `s_wnd`).
4. If step 2 returned DI_OK, call `dev->Acquire()` (vtbl+0x1C). The engine's later Acquire/Unacquire calls from
   `set_active` keep the new level.
5. Apply the fix once per distinct device pointer. Re-apply when the pointer changes (input re-init) and after the
   window is recreated: the device holds the hwnd it was given.

**Risks:**
- The Windows key works in game. Pressing it opens Start and takes focus.
- Game keys are unaffected: row 2 (INPUTSINK) feeds the same buffered data.
- If the renderer restart recreates the window, re-applying the level with the new hwnd is required. The
  state machine re-applies on a new window (section 6).
- On the `dx8` renderer the lever is the same; dx8 is untested.

**Rejected alternatives:**
- Hooking `SetCooperativeLevel` or the IAT: that writes to another module's memory, which the house rule forbids.
- `uset i_grabpointer "0"` ([C] duke/base/config.cfg:95; the engine then builds input from window messages):
  - it drops DirectInput entirely;
  - it changes mouse feel and cursor capture;
  - the panel's DInput-mode assumptions (mouse-edge polling, freecam grab) break.

## 5. Checks

- **Cause (confirmed):**
  1. With the panel open (which calls `inp_set_active(0)` and so unacquires the keyboard), Scroll Lock and the
     Windows key work.
  2. With the panel closed and no fix, both are dead.
  3. The stock game (`winmm.dll` removed) shows the same symptom, since the flag is in stock input.dll.
- **Instrument (stays):**
  - `GetRegisteredRawInputDevices` is called from the frame path when the keyboard pointer changes and after the
    fix. The log shows the keyboard row's `dwFlags`: 0x200 before the fix, 0x100 after.
  - The three HRESULTs are logged too.
- **After the fix**, with the game focused and the panel closed:
  - Windows key opens Start;
  - Scroll Lock fires ShareX;
  - arrows, Ctrl, Alt and Space still drive Duke;
  - Alt+Tab and focus return re-acquire (the arrows work again).

## 6. What is built

- **Switch:** Settings > DISPLAY "Windows key and hotkeys", default on, ini key `[display] winkeys`
  (appended to config.cpp's table). Off puts `0x16` back, only on a device we changed.
- **Code:** `src/nukepanel/hotkeys.h` (pure rules: signature, slot bounds, re-acquire rule, the
  `hk_step`/`hk_done` state machine; tests tb_0168-0170) and `hotkeys.cpp` (glue). `input_frame` calls
  `hotkeys_frame(s_wnd, g_cfg.winKeys)` every frame after the window follow, on the main thread.
- **Device:** section 4's signature, unique in input.dll (Ghidra byte search: one hit, 0x1000155a) and
  scanned at run time over input.dll's executable sections; 0 or 2+ hits, or unreadable headers, refuse
  for the session (logged). The module is the loaded one whose path ends in `sys/input.dll`
  (`hk_is_engine_input`, via `K32EnumProcessModules`), never by bare name: Windows ships its own
  `SysWOW64/input.dll`. The imm32 must lie inside that image and pass one guarded
  read. Afterwards the slot is read plainly each frame, behind a check that
  `GetModuleHandleExA(FROM_ADDRESS, slot)` still names that module (no VirtualQuery). A dropped slot
  resets the state machine.
- **Vtable slots, verified [D/A]:** in dinput8 10.0.26100.8521 the `IDirectInputDevice8A` vtable is the
  table after the IID_IDirectInputDevice8A pointer at 0x10026598, starting at 0x100265a0 (32 slots).
  Slot 13 (+0x34) is 0x1000cdd0: it takes (this, hwnd, flags), returns DIERR_ACQUIRED while acquired, and
  validates the flags in 0x1000ceaf, where 6 is legal. Slots 7 (+0x1C Acquire) and 8 (+0x20 Unacquire) are
  the ones the engine calls (input.dll 0x1000164b, 0x1000166b).
- **Only one SetCooperativeLevel in the engine [X]:** of the 16 references to 0x100099f0, only
  0x1000156c calls vtbl+0x34. `set_active` 0x10002790 and the keyboard read's re-acquire (0x100016db, on
  DIERR_INPUTLOST/NOTACQUIRED) only Acquire/Unacquire. So the panel's `inp_set_active(0/1)` never brings
  back 0x16, since dinput8 keeps the flags in the device (0x1000cdd0 stores them).
- **Order:** Unacquire -> SetCooperativeLevel -> Acquire only on `DI_OK` from the Unacquire
  (`hk_reacquire`). If the panel has the keyboard released, Unacquire gives DI_NOEFFECT and the keyboard
  stays released; closing the panel acquires it with the new level.
- **Re-apply:** when the device pointer changes (new device: engine level again) or the followed window
  changes (gl's `rend_restart`, display.md section A), the level is set again with the new window.
  A failed set waits for the switch, the device or the window to change. A device freed and recreated
  at the same address would be missed (the log's device line would show no new device).
- **Log lines:** `hotkeys: <path>\sys\input.dll ..., keyboard init match ... (unique), device slot ...`;
  `hotkeys: keyboard device ... is new (its init set 0x16; not set by us yet); raw ...`; then one line per set:
  `level 0x16 -> 0x06 set; Unacquire ..., SetCooperativeLevel ... (dinput8.dll), Acquire ...; raw before: mouse
  ...; keyboard flags 0x200 NOHOTKEYS ...; after: ... keyboard flags 0x100 INPUTSINK ...`. The raw rows are read
  with `GetRegisteredRawInputDevices`. They exist only while the device is acquired.
- **The mouse (mouse-lock.md "The fix"):** the same state machine always sets the engine mouse
  (stock level 5) to 6, with `hotkeys: mouse ...` lines of the same shape. There is no switch: the
  `[input] mouse_nonexclusive` key is gone and an old value is skipped. The keyboard's switch is unchanged.
  With the shared mouse nothing hides the OS cursor in play; input.cpp's cursor hold does (mouse-lock.md).

## Open questions

- Whether input.dll's keyboard device survives `rend_restart` (the level is re-applied if the window changes).
- A device freed and recreated at the same address would not be noticed.
