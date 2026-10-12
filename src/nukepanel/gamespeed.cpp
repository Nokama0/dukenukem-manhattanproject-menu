// Game speed (gamespeed.h, docs/re/game-speed.md). The timeGetTime export and the main thread's control of it.
#include "gamespeed.h"
#include "speedrule.h"
#include "proxy_count.h"   // NP_EXPORT_COUNT, NP_INDEX_timeGetTime
#include "config.h"
#include "input.h"         // input_gamespeed_take
#include "log.h"
#include <windows.h>
#include <psapi.h>         // K32GetModuleInformation (kernel32)
#include <intrin.h>        // _ReturnAddress
#include <stdint.h>

extern "C" void* g_np_real[NP_EXPORT_COUNT];
typedef DWORD (WINAPI* TimeFn)(void);

int g_gameSpeedOn;

// One snapshot for every thread that calls timeGetTime: the clock and the game module's range, written by the main
// thread only, under a sequence count (odd while a write is in progress). Starts as the real clock with no game range.
struct GsState { GsClock clk; GsRange game; };
static volatile LONG s_seq;
static GsState s_state = { { 0u, 0u, 100 }, { 0u, 0u } };
static volatile LONG s_callsGame, s_callsOther;   // cumulative since the last log window

static GsState snapshot(void) {
  for (;;) {
    const LONG a = s_seq;   // volatile: an acquire read under MSVC's x86 /volatile:ms
    if (!(a & 1)) {
      const GsState s = s_state;
      _ReadWriteBarrier();
      if (s_seq == a) return s;
    }
    YieldProcessor();
  }
}

// Main thread only: the one writer.
static void publish(const GsState& n) {
  InterlockedIncrement(&s_seq);
  s_state = n;
  InterlockedIncrement(&s_seq);
}

static DWORD real_now(void) { return ((TimeFn)g_np_real[NP_INDEX_timeGetTime])(); }

// The export (winmm.def: timeGetTime=np_time_get_time). cdecl with no arguments is stdcall with no arguments (a plain
// RET), and cdecl keeps the name undecorated for the .def. The real call goes through g_np_real, so the first call
// resolves winmm as every other export's first call does.
extern "C" __declspec(noinline) DWORD __cdecl np_time_get_time(void) {
  const DWORD real = real_now();
  const GsState s = snapshot();
  if (gs_in_range(s.game, (unsigned)(uintptr_t)_ReturnAddress())) {
    InterlockedIncrement(&s_callsGame);
    return gs_scaled(s.clk, real);
  }
  InterlockedIncrement(&s_callsOther);
  return real;
}

static int s_inited, s_wasOn;
static ULONGLONG s_nextLog;

static double k_of(int pct) { return (double)pct / 100.0; }

void gamespeed_frame(void) {
  GsState cur = s_state;   // the writer reads its own state directly
  if (!s_inited) {
    // Before the first speed change, which is what makes the range matter: until then game callers get real time.
    s_inited = 1;
    MODULEINFO mi;
    if (K32GetModuleInformation(GetCurrentProcess(), GetModuleHandleA(NULL), &mi, sizeof mi)) {
      cur.game.lo = (unsigned)(uintptr_t)mi.lpBaseOfDll;
      cur.game.hi = cur.game.lo + (unsigned)mi.SizeOfImage;
      publish(cur);
      np_log("speed: game-time callers: prism3d.exe [%08X, %08X)", cur.game.lo, cur.game.hi);
    } else {
      np_log("speed: REFUSED: the exe's range is unknown (%lu); Game speed changes nothing", GetLastError());
    }
  }
  const int presses = input_gamespeed_take();
  if (presses) {
    if (presses & 1) g_gameSpeedOn = !g_gameSpeedOn;
    np_log("speed: key pressed %d time(s), switch %s", presses, g_gameSpeedOn ? "on" : "off");
  }
  const int pct = g_gameSpeedOn ? gs_pct_snap(g_cfg.gameSpeedPct) : 100;
  const DWORD real = real_now();
  if (pct != cur.clk.pct || gs_needs_rebase(cur.clk, real)) {
    const int was = cur.clk.pct;
    cur.clk = gs_rebase(cur.clk, real, pct);
    publish(cur);
    if (was != pct)
      np_log("speed: k %.2f -> %.2f at real %lu ms, game clock %u ms (%+d ms from real)", k_of(was), k_of(pct),
             real, cur.clk.baseScaled, (int)(cur.clk.baseScaled - real));
  }
  const ULONGLONG now = GetTickCount64();
  if (g_gameSpeedOn != s_wasOn) {
    s_wasOn = g_gameSpeedOn;
    np_log("speed: Game speed %s", g_gameSpeedOn ? "on" : "off");
    InterlockedExchange(&s_callsGame, 0);
    InterlockedExchange(&s_callsOther, 0);
    s_nextLog = now + 5000;
  }
  if (g_gameSpeedOn && now >= s_nextLog) {
    s_nextLog = now + 5000;
    np_log("speed: k=%.2f callers game=%u other=%u", k_of(cur.clk.pct),
           (unsigned)InterlockedExchange(&s_callsGame, 0), (unsigned)InterlockedExchange(&s_callsOther, 0));
  }
}

bool gamespeed_tag(char* out, int cap) {
  if (!g_gameSpeedOn) return false;
  gs_tag_text(s_state.clk.pct, out, cap);
  return true;
}
