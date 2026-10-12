// Writes as little as it can and refuses to write at all unless the bytes
// under the patch are the exact ones the analysis read.
#include "hook.h"
#include "log.h"
#include "engine.h"
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

#define HOOK_MAX_PROLOGUE 32
#define HOOK_JMP_LEN      5
// Past this many threads we refuse rather than write a partly protected patch.
#define HOOK_MAX_THREADS  128

static void hook_resume_all(HANDLE* held, int n) {
  for (int i = 0; i < n; i++) { ResumeThread(held[i]); CloseHandle(held[i]); }
}

// Suspends every other thread so no instruction pointer can sit inside the
// bytes being replaced. Returns the count, or -1 (nothing left suspended).
// No logging or allocation here: a suspended thread may hold any lock.
static int hook_suspend_others(HANDLE* held, int max) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return -1;
  const DWORD pid = GetCurrentProcessId();
  const DWORD self = GetCurrentThreadId();
  THREADENTRY32 te;
  te.dwSize = sizeof(te);
  int n = 0, overflow = 0;
  if (Thread32First(snap, &te)) {
    do {
      if (te.dwSize < FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(DWORD)) continue;
      if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
      if (n >= max) { overflow = 1; break; }
      HANDLE h = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
      if (!h) continue;                       // exited between snapshot and open
      if (SuspendThread(h) == (DWORD)-1) { CloseHandle(h); continue; }
      held[n++] = h;
    } while (te.dwSize = sizeof(te), Thread32Next(snap, &te));
  }
  CloseHandle(snap);
  if (overflow) { hook_resume_all(held, n); return -1; }
  return n;
}

static void hook_log_bytes(const char* what, const unsigned char* p, unsigned n) {
  char text[HOOK_MAX_PROLOGUE * 3 + 1];
  unsigned used = 0;
  text[0] = 0;
  for (unsigned i = 0; i < n && i < HOOK_MAX_PROLOGUE; i++)
    used += (unsigned)sprintf_s(text + used, sizeof(text) - used, i ? " %02X" : "%02X", p[i]);
  np_log("hook: %s %s", what, text);
}

int hook_install_at(void* targetv, const unsigned char* fp, unsigned fpLen,
                    unsigned prologueLen, void* replacement, void** trampolineOut) {
  if (trampolineOut) *trampolineOut = 0;
  unsigned char* target = (unsigned char*)targetv;
  if (!target || !fp || !replacement || fpLen == 0 || fpLen < prologueLen ||
      prologueLen < HOOK_JMP_LEN || prologueLen > HOOK_MAX_PROLOGUE) {
    np_log("hook: refusing %p, arguments make no sense (fp=%u prologue=%u)",
           targetv, fpLen, prologueLen);
    return 0;
  }
  // Covers the fingerprint compare and the 16 bytes logged on a mismatch.
  if (!guarded_readable(target, fpLen > 16 ? fpLen : 16)) {
    np_log("hook: refusing %p, not committed readable memory", targetv);
    return 0;
  }
  if (memcmp(target, fp, fpLen) != 0) {
    np_log("hook: fingerprint MISMATCH at %p; installing nothing", targetv);
    hook_log_bytes("expected", fp, fpLen);
    hook_log_bytes("found   ", target, 16);
    return 0;
  }

  unsigned char* tramp = (unsigned char*)VirtualAlloc(0, 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!tramp) { np_log("hook: no trampoline memory for %p", targetv); return 0; }
  memcpy(tramp, target, prologueLen);
  tramp[prologueLen] = 0xE9;
  *(int*)(tramp + prologueLen + 1) = (int)((target + prologueLen) - (tramp + prologueLen + HOOK_JMP_LEN));
  // Written read-write, then execute-read before anything can call it: never a writable executable page.
  DWORD trampWas = 0;
  if (!VirtualProtect(tramp, 64, PAGE_EXECUTE_READ, &trampWas)) {
    np_log("hook: trampoline for %p not made execute-read (err %lu); installing nothing", targetv, GetLastError());
    VirtualFree(tramp, 0, MEM_RELEASE);
    return 0;
  }
  FlushInstructionCache(GetCurrentProcess(), tramp, 64);

  unsigned char patch[HOOK_MAX_PROLOGUE];
  patch[0] = 0xE9;
  *(int*)(patch + 1) = (int)((unsigned char*)replacement - (target + HOOK_JMP_LEN));
  for (unsigned i = HOOK_JMP_LEN; i < prologueLen; i++) patch[i] = 0x90;

  DWORD old = 0;
  if (!VirtualProtect(target, prologueLen, PAGE_EXECUTE_READWRITE, &old)) {
    np_log("hook: %p is not writable (err %lu)", targetv, GetLastError());
    VirtualFree(tramp, 0, MEM_RELEASE);
    return 0;
  }
  HANDLE held[HOOK_MAX_THREADS];
  const int suspended = hook_suspend_others(held, HOOK_MAX_THREADS);
  if (suspended < 0) {
    DWORD back = 0;
    VirtualProtect(target, prologueLen, old, &back);
    VirtualFree(tramp, 0, MEM_RELEASE);
    np_log("hook: %p refused, could not hold the other threads (max %d)", targetv, HOOK_MAX_THREADS);
    hook_log_bytes("found   ", target, 16);
    return 0;
  }
  if (trampolineOut) *trampolineOut = tramp;
  memcpy(target, patch, prologueLen);
  FlushInstructionCache(GetCurrentProcess(), target, prologueLen);
  hook_resume_all(held, suspended);
  DWORD ignored = 0;
  VirtualProtect(target, prologueLen, old, &ignored);

  np_log("hook: installed at %p -> %p, trampoline %p, held %d other thread(s)",
         targetv, replacement, (void*)tramp, suspended);
  hook_log_bytes("was     ", tramp, prologueLen);
  hook_log_bytes("now     ", target, 16);
  return 1;
}
