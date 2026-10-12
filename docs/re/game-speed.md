# Game speed: who reads which clock (prism3d.exe and every module the Enhanced Edition loads)

The whole game runs on one clock, `tmr_read` prism3d!0x402c70, the only caller of winmm!timeGetTime in a game-time module. Our winmm.dll now serves timeGetTime itself and returns a
scaled clock to calls whose return address lies in prism3d.exe; every other caller gets real time. Sound (ds8.dll),
GLDirect, dxwrapper, granny and the panel read other clocks or no engine clock, so they keep their speed. The free
camera flies on QueryPerformanceCounter and needs no compensation.

Method: D = decompile, A = disassembly, X = xref (Ghidra MCP); I = PE import table, S = string/byte census of the
file (Python on the game files, data only). Install: the Enhanced Edition folder (`<game folder>`).

## 1. The caller census

**winmm!timeGetTime importers** (I, every exe and DLL in the install):

| Module | Imports | Reads the time for | Gets |
|---|---|---|---|
| prism3d.exe | WINMM timeGetTime, timeBeginPeriod | `tmr_read` 0x402c70 only (X on IAT 0x4341fc; A: `call [timeGetTime]; mul 1000; ret`) | scaled |
| sys/granny.dll | WINMM timeGetTime, QPC, QPF | its own clock; animation time comes from skmod (below) | real |
| everything else | no timeGetTime import | -- | -- |

No other module imports it: p3shared.dll, duke/sys/duke_base.dll, sys/{client,ds8,dx8,gl,input,loaders,pmd,psm,
skmod}.dll, ogg/vorbis/vorbisfile (I). GLDirect opengl32.dll imports kernel32 GetTickCount, QPC,
GetSystemTimeAsFileTime; dxwrapper d3d8.dll imports QPC; dxwrapper.dll imports GetTickCount(64) and QPC (I). Both
dxwrapper files hold the string "timeGetTime" (S), so they may look it up by name: those calls come from their own
address range and get real time.

**tmr_read's users** (it is exported, so the plug-in DLLs could call it; every call reaches timeGetTime from prism3d):
- prism3d: `sys_init` 0x402298 (random seed), `sys_frame` 0x4024f3 (the engine vtimer, timing.md section 1), and an
  exported `jmp tmr_read` thunk at 0x402580 (X).
- p3shared (slot 0x1004bf30, X): `vtimer_t::start/update` 0x1002de30/0x1002de60 (vtimer_t is used by prism3d only,
  S), the CPU-speed calibration `FUN_100093c0` (rdtsc over one second of tmr_read, at startup while the switch is
  still off) and `cd_mesh_t::build_end` (logs its build time). The one vtimer, `vtmr_new`, is made by `sys_init`
  and updated only by `sys_frame` (X).
- duke_base: the pfn slot 0x10184118 is written by the resolver and never read (X).
- The sys/ plug-ins resolve the engine API by name (`push name; push hmod; call GetProcAddress; mov [slot],eax`).
  For tmr_read, sys_get_time, sys_get_timedelta, sys_get_lastdelta and sys_set_time every slot in client, ds8,
  dx8, gl, input, loaders, psm and skmod has **no use besides its store** (S, byte census of `FF 15`/`A1`/`8B xx`
  references to the slot). The same census on duke_base finds sys_get_time's 11 and sys_get_timedelta's 4 users that
  Ghidra lists (timing.md section 1), which validates it.

So the engine's game time (g_frame_dt, g_time, every process dt, skmod's animation clock through
`sys_get_timedelta`) all derive from the one scaled source. **ds8.dll (DirectSound, the music streamer) reads no engine
clock and no winmm time**: sound plays at its normal speed; sounds the game starts come at the game's pace.

**Other clocks of ours** (unchanged, all real): the panel `wallclock_ms` (GetTickCount, panel.cpp), the frame cap
(QPC, framecap.cpp), frame stats and hitch lines (QPC, probe.cpp), timers (GetTickCount64). Nothing of ours calls
timeGetTime.

## 2. The change

- `winmm.def`: `timeGetTime=np_time_get_time` (generated: `OVERRIDES` in tools/gen_winmm_proxy.py; proxy_count.h
  names `NP_INDEX_timeGetTime` 149). `np_time_get_time` (gamespeed.cpp) calls the real one through `g_np_real`, then
  returns `gs_scaled` when `_ReturnAddress()` lies in prism3d.exe's image (K32GetModuleInformation, once).
- The clock (speedrule.h): `scaled = baseScaled + (real - baseReal) * pct / 100`, unsigned 32-bit, computed from the
  base every call (no lost fractions). A speed change rebases at the current real time, so the clock is continuous
  and never steps back; real's 49.7-day wrap reads as elapsed time. Off is pct 100 on the same clock: the offset
  built up while scaled stays (a jump back to real would run the game clock backwards).
- Threads: the main thread is the only writer (`gamespeed_frame`, after `panel_frame`). Readers take a seqlock
  snapshot of the clock and the range together.
- Settings > GAME SPEED: the switch (not saved), Speed 10-400 % in 5 % steps (`[game] speed`, default 50), Game speed
  key (`[game] speedkey`, unbound). A "Speed 50%" tag beside the level stats, or alone in that corner, under the
  readout's hiding rules. Log: `speed: k=0.50 callers game=N other=M` every 5 s while on, plus each change.
- Free camera: our flight integrates QPC time (freecam.cpp `freecam_frame`), so it keeps its speed. The engine's own
  console fly camera (`FUN_100739e0`, `sys_get_timedelta`, timing.md) does scale; the panel never uses it.

## 3. Open questions (with the measurement that settles each)

The feature is built; none of the following has been measured in game.


1. **granny.dll's timeGetTime is not animation time.** skmod drives granny with `GrannyAdvanceTime` from
   `sys_get_timedelta` (I: skmod imports AdvanceTime/GetCurrentTime). Measure: with the switch on, `other=` in the
   speed line is granny's (and system DLLs') call count; and at 50 % Duke's walk animation must match his speed.
2. **Sound streaming needs no engine clock.** ds8 reads none (section 1), so its refill must run on buffer positions
   or per frame. Measure: music at 10 % and 400 % plays at normal pitch and without gaps.
3. **Per-frame logic is not scaled.** Logic counted in frames, not seconds (player +0xECC, input edges, timing.md
   section 2) runs at the frame rate whatever the speed. At low speed and a high frame rate many frames get dt = 0
   (1 ms granularity: at 175 fps and 10 % a frame is 0.57 game ms). The engine already sees dt = 0 above 1000 fps
   (menus); nothing is known to divide by it. Measure: at 10 % play a level section, watch for frozen or jittering
   motion, and the jump.
4. **The scaled clock's own wrap.** At 400 % the scaled clock reaches 2^32 ms four times sooner than real; the
   engine's `tmr_read` (u64 = ms * 1000) then steps back, the same engine fault the real wrap has after 49.7 days of
   uptime. Not measured; it needs days of uptime with the switch on.
