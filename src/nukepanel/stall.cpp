// The stall sampler (docs/re/stalls.md). The ~1 s hitches carry no panel, log or save share and no engine
// log line, so this samples where the main thread is while a frame runs long and logs the modules on its stack.
#include "stall.h"
#include "stallrule.h"
#include "log.h"
#include <windows.h>

// Published by the main thread only. Write order in stall_frame_end is start time, then id, then the sample count:
// a suspended main thread that still shows the id and start time read before the suspend is inside that frame.
static volatile LONG s_frameId;        // frames ended since the watchdog started
static volatile LONG s_frameStartMs;   // qpc_ms() when the current frame began (the last frame ended)
static volatile LONG s_frameSamples;   // samples taken in the current frame, for the hitch line

// Read at detach. The watchdog is their only writer. s_stalls is the cap's count (long frames give theirs back).
static volatile LONG s_stalls, s_long, s_samples, s_suspendFails, s_contextFails, s_frameMoved;

static LARGE_INTEGER s_qpf;
static HANDLE s_main, s_stop;
static unsigned s_stackBase;   // the main thread's NT_TIB StackBase (one past its highest stack byte)
static bool s_started;

static unsigned qpc_ms(void) {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return (unsigned)(t.QuadPart * 1000 / s_qpf.QuadPart);   // wraps after 49 days; only differences are used
}

// A plain read of the suspended thread's stack. The walk keeps it inside [ESP, StackBase), all committed, so the
// __except is a backstop, not the bounds check.
static int seh_read(unsigned addr, unsigned* out, void*) {
  __try {
    *out = *(const volatile unsigned*)(UINT_PTR)addr;
    return 1;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}

struct StallSample { unsigned eip, top; int hasTop; unsigned rets[STALL_MAX_FRAMES]; int n; };
static CONTEXT s_ctx;   // watchdog only; static so the suspended window touches no new stack page

// 1 sampled, 0 the frame ended before the suspend landed, -1 SuspendThread failed, -2 GetThreadContext failed. Between
// Suspend and Resume the only API call is GetThreadContext: no allocation, logging, VirtualQuery or ReadProcessMemory
// (security software hooks those, and a hook taking a lock the suspended thread holds would freeze the game).
static int sample_main(LONG id, LONG startMs, StallSample* s, DWORD* err) {
  if (SuspendThread(s_main) == (DWORD)-1) { *err = GetLastError(); return -1; }
  int r = 1;
  s_ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
  if (!GetThreadContext(s_main, &s_ctx)) {
    *err = GetLastError(); r = -2;
  } else if (s_frameId != id || s_frameStartMs != startMs) {
    // Checked after GetThreadContext: SuspendThread is asynchronous, and only a returned context proves the main
    // thread has stopped. A new start time with the old id is a suspend in the middle of stall_frame_end.
    r = 0;
  } else {
    s->eip = s_ctx.Eip;
    // [esp]: a frameless leaf's return address (an ntdll syscall stub has no EBP frame of its own).
    s->hasTop = s_ctx.Esp < s_stackBase && s_stackBase - s_ctx.Esp >= 4 && seh_read(s_ctx.Esp, &s->top, 0);
    s->n = stall_walk(s_ctx.Ebp, s_ctx.Esp, s_stackBase, seh_read, 0, s->rets, STALL_MAX_FRAMES);
    InterlockedIncrement(&s_frameSamples);   // an intrinsic, not a call; still suspended, so it lands in frame `id`
  }
  ResumeThread(s_main);
  return r;
}

// After ResumeThread: the loader lock GetModuleHandleExA takes may be the one the main thread is stuck in; then this
// waits for it, and the main thread never waits on the watchdog, so it cannot deadlock.
static int fmt_addr(char* out, int cap, unsigned addr) {
  HMODULE m = 0;
  char path[MAX_PATH];
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR)(UINT_PTR)addr, &m) && m) {
    const DWORD n = GetModuleFileNameA(m, path, MAX_PATH);
    if (n && n < MAX_PATH) return stall_fmt_addr(out, cap, path, addr, (unsigned)(UINT_PTR)m);
  }
  return stall_fmt_addr(out, cap, 0, addr, 0);
}

static void log_sample(LONG id, unsigned elapsed, const char* kind, const StallSample* s) {
  char eip[96], top[96], stack[800];
  fmt_addr(eip, sizeof(eip), s->eip);
  if (s->hasTop) fmt_addr(top, sizeof(top), s->top);
  else strcpy_s(top, sizeof(top), "?");
  int len = 0;
  stack[0] = 0;
  for (int i = 0; i < s->n && len < (int)sizeof(stack) - 2; ++i) {
    if (len) stack[len++] = ' ';
    len += fmt_addr(stack + len, (int)sizeof(stack) - len, s->rets[i]);
  }
  np_log("stall: frame %ld at %u ms%s: eip %s top %s ; stack(%d) %s", id, elapsed, kind, eip, top, s->n, stack);
}

static DWORD WINAPI watchdog(void*) {
  LONG seenId = -1;
  int taken = 0, longDone = 0;
  while (WaitForSingleObject(s_stop, 50) == WAIT_TIMEOUT) {
    const LONG id = s_frameId;
    const LONG startMs = s_frameStartMs;
    if (id != seenId) { seenId = id; taken = 0; longDone = 0; }
    const unsigned elapsed = qpc_ms() - (unsigned)startMs;
    const int kind = stall_decide(elapsed, taken, longDone, s_stalls);
    if (kind == STALL_SKIP) continue;
    StallSample s;
    DWORD err = 0;
    const int r = sample_main(id, startMs, &s, &err);
    if (r == 0) { InterlockedIncrement(&s_frameMoved); continue; }
    // A failed attempt still uses up the sample: never a retry every 50 ms.
    InterlockedExchange(&s_stalls, stall_count_after(s_stalls, kind, taken));
    if (kind == STALL_SAMPLE_LONG) { longDone = 1; InterlockedIncrement(&s_long); }
    else ++taken;
    const char* tag = kind == STALL_SAMPLE_LONG ? " long" : "";
    if (r == -1) {
      InterlockedIncrement(&s_suspendFails);
      np_log("stall: frame %ld at %u ms%s: SuspendThread failed err=%lu", id, elapsed, tag, err);
    } else if (r == -2) {
      InterlockedIncrement(&s_contextFails);
      np_log("stall: frame %ld at %u ms%s: GetThreadContext failed err=%lu", id, elapsed, tag, err);
    } else {
      InterlockedIncrement(&s_samples);
      log_sample(id, qpc_ms() - (unsigned)startMs, tag, &s);
    }
  }
  return 0;
}

static void start(void) {
  QueryPerformanceFrequency(&s_qpf);
  // On the main thread: its own TIB. Read once here so the suspended window needs no query.
  s_stackBase = (unsigned)(UINT_PTR)((NT_TIB*)NtCurrentTeb())->StackBase;
  const unsigned stackLimit = (unsigned)(UINT_PTR)((NT_TIB*)NtCurrentTeb())->StackLimit;
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &s_main,
                       THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0)) {
    np_log("stall: no main thread handle (err=%lu); no watchdog", GetLastError());
    return;
  }
  s_stop = CreateEventA(NULL, TRUE, FALSE, NULL);
  if (!s_stop) { np_log("stall: CreateEvent failed (err=%lu); no watchdog", GetLastError()); return; }
  InterlockedExchange(&s_frameStartMs, (LONG)qpc_ms());
  DWORD tid = 0;
  const HANDLE t = CreateThread(NULL, 64 * 1024, watchdog, NULL, 0, &tid);
  if (!t) { np_log("stall: CreateThread failed (err=%lu); no watchdog", GetLastError()); return; }
  CloseHandle(t);
  np_log("stall: watchdog tid=%lu watching tid=%lu (stack %08x-%08x): samples at %d and %d ms, one long at %d ms,"
         " at most %d stalls, %d frames deep", tid, GetCurrentThreadId(), stackLimit, s_stackBase, STALL_FIRST_MS,
         STALL_SECOND_MS, STALL_LONG_MS, STALL_MAX_STALLS, STALL_MAX_FRAMES);
}

void stall_frame_end(void) {
  if (!s_started) { s_started = true; start(); }
  if (!s_qpf.QuadPart) return;
  InterlockedExchange(&s_frameStartMs, (LONG)qpc_ms());
  InterlockedIncrement(&s_frameId);
  InterlockedExchange(&s_frameSamples, 0);
}

int stall_frame_samples(void) { return (int)s_frameSamples; }

// No wait for the thread: at process exit it is already gone, and under the loader lock its exit would block on
// that lock. The DLL is never unloaded earlier (its detours stay installed for the process's life).
void stall_detach(void) {
  if (!s_stop) return;
  np_log("stall: %ld stalls (cap count), %ld long frames, %ld samples, %ld suspend failures, %ld context failures,"
         " %ld ended before the suspend", s_stalls, s_long, s_samples, s_suspendFails, s_contextFails, s_frameMoved);
  SetEvent(s_stop);
}
