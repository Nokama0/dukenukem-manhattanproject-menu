// Save-freeze instrument and Fast saves (docs/re/save-io.md). A save writes state/<map>.msv via vfs_write -> prism3d!
// 0x404b80 -> CRT _write 0x428ee5 -> WriteFile, 1-8 bytes per call. Fast saves buffers them per handle and flushes
// in large blocks before any seek, flush or close. Only prism3d.exe's import slots are patched; no code byte changes.
#include "saveio.h"
#include "saveio_rule.h"
#include "config.h"
#include "engine.h"
#include "log.h"
#include <windows.h>
#include <string.h>

typedef HANDLE (WINAPI* CreateFileA_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef BOOL (WINAPI* WriteFile_t)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef DWORD (WINAPI* SetFilePointer_t)(HANDLE, LONG, PLONG, DWORD);
typedef BOOL (WINAPI* FlushFileBuffers_t)(HANDLE);
typedef BOOL (WINAPI* CloseHandle_t)(HANDLE);

static CreateFileA_t s_CreateFileA;
static WriteFile_t s_WriteFile;
static SetFilePointer_t s_SetFilePointer;
static FlushFileBuffers_t s_FlushFileBuffers;
static CloseHandle_t s_CloseHandle;

// Touched only on s_tid (every hook passes other threads straight through), so no interlocks. A buffered handle is
// therefore only coalesced on the main thread; the save path runs on no other (save-io.md section 2).
static DWORD s_tid;
static double s_ticksPerMs;
static SioTable s_tab;
static long long s_frameTicks;

// One buffer per table slot, allocated the first time that slot holds a save file and kept: a save is ~300 KB.
enum { SIO_BUF_BYTES = 256 * 1024 };
static unsigned char* s_bufMem[SIO_FILES];

static int s_hooked;   // import slots patched by saveio_install
static long long qpc(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

// A real kernel32 WriteFile for the coalescing rule, timed into real=; err keeps its failure's last error.
struct RealWrite { HANDLE h; SioFile* f; DWORD err; };
static int real_write_once(void* ctx, const unsigned char* p, unsigned n, unsigned* wrote) {
  RealWrite* c = (RealWrite*)ctx;
  DWORD w = 0;
  const long long t0 = qpc();
  const BOOL ok = s_WriteFile(c->h, p, n, &w, NULL);
  if (!ok) c->err = GetLastError();
  sio_acc_add(&c->f->real, qpc() - t0);
  *wrote = w;
  return ok ? 1 : 0;
}

// A flush that failed: counted for the close line, logged, and this handle is no longer buffered (its stream has
// already lost bytes; the CloseHandle hook reports the save as failed).
static void flush_failed(SioFile* f, unsigned pending, DWORD err) {
  f->flushFails++;
  f->flushErr = err ? err : ERROR_WRITE_FAULT;   // a write that took nothing has no error of its own
  f->buffered = 0;
  np_log("saveio: %s: Fast saves could not write %u buffered bytes (error %lu); the save is incomplete",
         f->name, pending, f->flushErr);
}

// Writes out f's buffer, if it has one. False on a failure (flush_failed has run).
static bool flush_buf(HANDLE h, SioFile* f) {
  if (!f->buffered || !f->buf.len) return true;
  const unsigned pending = f->buf.len;
  RealWrite c = { h, f, 0 };
  if (sio_buf_flush(&f->buf, real_write_once, &c)) return true;
  flush_failed(f, pending, c.err);
  return false;
}

static HANDLE WINAPI hk_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                    DWORD flags, HANDLE tmpl) {
  if (GetCurrentThreadId() != s_tid) return s_CreateFileA(name, access, share, sa, disp, flags, tmpl);
  const long long t0 = qpc();
  HANDLE h = s_CreateFileA(name, access, share, sa, disp, flags, tmpl);
  const DWORD err = GetLastError();   // the CRT reads it after a failed call
  const long long dt = qpc() - t0;
  // The ~1 s hitches sample here (stall: WINMM.dll+0x58a52 is this call, docs/re/stalls.md): name any slow open,
  // read or write, so the file and its flags show which open Windows holds up.
  if (s_ticksPerMs > 0.0 && (double)dt / s_ticksPerMs >= 100.0) {
    char shown[MAX_PATH];
    np_log("saveio: slow open %.0f ms: %s access=0x%08lX share=0x%lX disp=%lu flags=0x%08lX %s err=%lu",
           (double)dt / s_ticksPerMs, name ? np_log_path(name, shown, sizeof shown) : "?", access, share, disp, flags,
           h == INVALID_HANDLE_VALUE ? "failed" : "ok", h == INVALID_HANDLE_VALUE ? err : 0ul);
  }
  if (h != INVALID_HANDLE_VALUE && (access & GENERIC_WRITE)) {
    const SioFile* old = sio_find(&s_tab, h);
    if (old && old->buffered && old->buf.len)
      np_log("saveio: %s: %u buffered bytes never written; its handle was closed where we could not see it",
             old->name, old->buf.len);
    SioFile* f = sio_open(&s_tab, h, name, t0, dt);
    // Buffer only with all five hooks in: a missing close or seek hook would strand or misplace the bytes (review).
    if (f && g_cfg.fastSaves && s_hooked == 5 && sio_bufferable(name, access)) {
      const int slot = (int)(f - s_tab.f);
      if (!s_bufMem[slot]) s_bufMem[slot] = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, SIO_BUF_BYTES);
      if (s_bufMem[slot]) {
        f->buf.data = s_bufMem[slot]; f->buf.len = 0; f->buf.cap = SIO_BUF_BYTES;
        f->buffered = 1;
      } else {
        np_log("saveio: %s: no memory for the Fast saves buffer; written unbuffered", f->name);
      }
    }
    s_frameTicks += dt;
  }
  SetLastError(err);
  return h;
}

static BOOL WINAPI hk_WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD written, LPOVERLAPPED ov) {
  SioFile* f = GetCurrentThreadId() == s_tid ? sio_find(&s_tab, h) : 0;
  if (!f) return s_WriteFile(h, buf, n, written, ov);
  const DWORD err0 = GetLastError();
  const long long t0 = qpc();
  int r = SIO_BUF_DIRECT;
  if (f->buffered) {
    if (ov) {
      r = flush_buf(h, f) ? SIO_BUF_DIRECT : SIO_BUF_FAILED;   // overlapped: in order, then straight through
    } else {
      const unsigned pending = f->buf.len;
      RealWrite c = { h, f, 0 };
      r = sio_buf_write(&f->buf, buf, n, real_write_once, &c);
      if (r == SIO_BUF_FAILED) flush_failed(f, pending, c.err);
    }
  }
  BOOL ok;
  DWORD err;
  if (r == SIO_BUF_TAKEN) {
    // The engine sees the write it asked for, done; nothing it can observe changed the last error.
    if (written) *written = n;
    ok = TRUE; err = err0;
    f->bytes += n;
  } else if (r == SIO_BUF_FAILED) {
    // The bytes before this write are lost: the engine's write fails, as a full disk would make it.
    if (written) *written = 0;
    ok = FALSE; err = f->flushErr;
  } else {
    const long long t1 = qpc();
    ok = s_WriteFile(h, buf, n, written, ov);
    err = GetLastError();   // the CRT reads it after a failed call
    sio_acc_add(&f->real, qpc() - t1);
    if (ok && written) f->bytes += *written;
  }
  const long long dt = qpc() - t0;
  sio_acc_add(&f->write, dt);
  s_frameTicks += dt;
  SetLastError(err);
  return ok;
}

static DWORD WINAPI hk_SetFilePointer(HANDLE h, LONG lo, PLONG hi, DWORD method) {
  SioFile* f = GetCurrentThreadId() == s_tid ? sio_find(&s_tab, h) : 0;
  if (!f) return s_SetFilePointer(h, lo, hi, method);
  const long long t0 = qpc();
  // The position (and a seek back to patch a header) must see every byte so far; a failed write-out fails the
  // seek, so the CRT's _lseek returns -1 instead of a position the file never reached (review).
  if (!flush_buf(h, f)) {
    s_frameTicks += qpc() - t0;
    SetLastError(f->flushErr);
    return INVALID_SET_FILE_POINTER;
  }
  const DWORD r = s_SetFilePointer(h, lo, hi, method);
  const DWORD err = GetLastError();   // the CRT reads it after a failed call
  const long long dt = qpc() - t0;
  sio_acc_add(&f->seek, dt);
  s_frameTicks += dt;
  SetLastError(err);
  return r;
}

static BOOL WINAPI hk_FlushFileBuffers(HANDLE h) {
  SioFile* f = GetCurrentThreadId() == s_tid ? sio_find(&s_tab, h) : 0;
  if (!f) return s_FlushFileBuffers(h);
  const long long t0 = qpc();
  const bool flushed = flush_buf(h, f);
  BOOL ok = s_FlushFileBuffers(h);
  DWORD err = GetLastError();   // the CRT reads it after a failed call
  if (!flushed) { ok = FALSE; err = f->flushErr; }
  const long long dt = qpc() - t0;
  sio_acc_add(&f->flush, dt);
  s_frameTicks += dt;
  SetLastError(err);
  return ok;
}

static BOOL WINAPI hk_CloseHandle(HANDLE h) {
  SioFile* f = GetCurrentThreadId() == s_tid ? sio_find(&s_tab, h) : 0;
  if (!f) return s_CloseHandle(h);
  const long long t0 = qpc();
  flush_buf(h, f);
  BOOL ok = s_CloseHandle(h);
  const long long t1 = qpc();
  s_frameTicks += t1 - t0;
  // The CRT reads GetLastError after a failed close; np_log's own calls must not change it.
  DWORD err = GetLastError();
  // Any lost buffered bytes make the save a failed one, after the handle is closed (the engine sees it where it can).
  if (f->flushFails) { ok = FALSE; err = f->flushErr; }
  char line[320];
  sio_format(line, sizeof line, f, t1 - t0, t1, s_ticksPerMs);
  sio_drop(f);
  if (s_tab.overflow || s_tab.stale) np_log("%s untracked=%u stale=%u", line, s_tab.overflow, s_tab.stale);
  else np_log("%s", line);
  SetLastError(err);
  return ok;
}

// prism3d.exe's import slot for dll!fn, by walking its import descriptors by name (no offsets).
static void** iat_slot(unsigned char* base, const char* dll, const char* fn) {
  if (!guarded_readable(base, sizeof(IMAGE_DOS_HEADER))) return 0;
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
  if (!guarded_readable(nt, sizeof *nt) || nt->Signature != IMAGE_NT_SIGNATURE) return 0;
  const DWORD size = nt->OptionalHeader.SizeOfImage;
  const IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress || dir.VirtualAddress >= size || !guarded_readable(base, size)) return 0;
  // The whole image is readable (one VirtualQuery walk above); every RVA below is still bounded by its size.
  for (const IMAGE_IMPORT_DESCRIPTOR* d = (const IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress);
       (unsigned char*)(d + 1) <= base + size && d->Name; ++d) {
    if (d->Name >= size || _stricmp((const char*)base + d->Name, dll) != 0) continue;
    const DWORD names = d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk;
    if (names >= size || d->FirstThunk >= size) return 0;
    const IMAGE_THUNK_DATA32* nm = (const IMAGE_THUNK_DATA32*)(base + names);
    IMAGE_THUNK_DATA32* iat = (IMAGE_THUNK_DATA32*)(base + d->FirstThunk);
    for (int i = 0; (unsigned char*)(nm + i + 1) <= base + size && nm[i].u1.AddressOfData; ++i) {
      if (IMAGE_SNAP_BY_ORDINAL32(nm[i].u1.Ordinal) || nm[i].u1.AddressOfData >= size) continue;
      const IMAGE_IMPORT_BY_NAME* ibn = (const IMAGE_IMPORT_BY_NAME*)(base + nm[i].u1.AddressOfData);
      if (strcmp((const char*)ibn->Name, fn) == 0) return (void**)&iat[i].u1.Function;
    }
  }
  return 0;
}

// Saves the slot's current target in *orig before the slot points at hook. A shim (apphelp) may own the slot:
// the hook calls whatever was there, and the log says whether it was kernel32's own export.
static int patch(unsigned char* base, const char* fn, void* hook, void** orig) {
  void** slot = iat_slot(base, "KERNEL32.dll", fn);
  if (!slot) { np_log("saveio: %s: no import slot", fn); return 0; }
  DWORD old;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
    np_log("saveio: %s: VirtualProtect failed (%lu)", fn, GetLastError());
    return 0;
  }
  *orig = *slot;
  InterlockedExchangePointer(slot, hook);
  VirtualProtect(slot, sizeof(void*), old, &old);
  const void* k32 = (const void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), fn);
  np_log("saveio: %s slot %p was %p (%s)", fn, (void*)slot, *orig, *orig == k32 ? "kernel32" : "not kernel32's export");
  return 1;
}

int saveio_install(void) {
  LARGE_INTEGER f;
  QueryPerformanceFrequency(&f);
  s_ticksPerMs = (double)f.QuadPart / 1000.0;
  s_tid = GetCurrentThreadId();
  unsigned char* base = (unsigned char*)GetModuleHandleA(NULL);
  // CloseHandle last: a close is only ever timed for a handle the CreateFileA hook already saw.
  int n = 0;
  n += patch(base, "CreateFileA", (void*)hk_CreateFileA, (void**)&s_CreateFileA);
  n += patch(base, "WriteFile", (void*)hk_WriteFile, (void**)&s_WriteFile);
  n += patch(base, "SetFilePointer", (void*)hk_SetFilePointer, (void**)&s_SetFilePointer);
  n += patch(base, "FlushFileBuffers", (void*)hk_FlushFileBuffers, (void**)&s_FlushFileBuffers);
  n += patch(base, "CloseHandle", (void*)hk_CloseHandle, (void**)&s_CloseHandle);
  s_hooked = n;
  np_log("saveio: %d/5 prism3d.exe import slots timed, tid=%lu", n, s_tid);
  return n;
}

double saveio_take_frame_ms(void) {
  const double ms = s_ticksPerMs > 0.0 ? (double)s_frameTicks / s_ticksPerMs : 0.0;
  s_frameTicks = 0;
  return ms;
}
