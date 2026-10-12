#pragma once
// The save-freeze instrument's pure rules (docs/re/save-io.md): per-kind QPC accumulators, the table of files the
// engine opened for write, the one log line per closed file, and Fast saves' write coalescing. saveio.cpp owns the
// IAT hooks that feed them.
#include <stdio.h>
#include <string.h>

// One file-API call kind: calls, summed QPC ticks, longest single call.
struct SioAcc { unsigned calls; long long ticks, max; };
inline void sio_acc_add(SioAcc* a, long long dt) {
  a->calls++; a->ticks += dt;
  if (dt > a->max) a->max = dt;
}

// Fast saves (save-io.md section 6): the engine's 1-8 byte writes are copied here and reach WriteFile in large
// blocks, in the same order and at the same file positions.
struct SioBuf { unsigned char* data; unsigned len, cap; };

enum { SIO_NAME_CAP = 48, SIO_FILES = 8 };
// A handle opened for write: opened at QPC t_open (before CreateFile ran), create = CreateFile's own ticks.
// write counts the engine's WriteFile calls; real counts the WriteFile calls that reached kernel32.
// buffered: Fast saves owns buf for this handle. flushFails/flushErr: flushes that failed, the last error.
struct SioFile { void* h; char name[SIO_NAME_CAP]; long long t_open, create; SioAcc write, seek, flush, real;
                 unsigned long long bytes; SioBuf buf; int buffered; unsigned flushFails; unsigned long flushErr; };
// overflow: opens not tracked, the table full. stale: a handle reopened before we saw it closed.
struct SioTable { SioFile f[SIO_FILES]; unsigned overflow, stale; };

// The name after the last '/' or '\\'.
inline const char* sio_basename(const char* path) {
  const char* b = path;
  for (const char* p = path; *p; ++p) if (*p == '/' || *p == '\\') b = p + 1;
  return b;
}
inline SioFile* sio_find(SioTable* t, const void* h) {
  if (!h) return 0;
  for (int i = 0; i < SIO_FILES; ++i) if (t->f[i].h == h) return &t->f[i];
  return 0;
}
// Starts tracking h (0 is never a handle: it marks a free slot). Null when the table is full.
inline SioFile* sio_open(SioTable* t, void* h, const char* path, long long t_open, long long create) {
  if (!h) return 0;
  SioFile* f = sio_find(t, h);
  if (f) t->stale++;
  for (int i = 0; !f && i < SIO_FILES; ++i) if (!t->f[i].h) f = &t->f[i];
  if (!f) { t->overflow++; return 0; }
  memset(f, 0, sizeof *f);
  f->h = h; f->t_open = t_open; f->create = create;
  const char* b = sio_basename(path ? path : "");
  strncpy(f->name, b, SIO_NAME_CAP - 1);
  f->name[SIO_NAME_CAP - 1] = 0;
  return f;
}
inline void sio_drop(SioFile* f) { if (f) f->h = 0; }

// CreateFileA's access bits, so this header stays free of windows.h. Any read right rules a handle out: a read
// would not see the bytes still in the buffer.
enum : unsigned long { SIO_GENERIC_READ = 0x80000000ul, SIO_GENERIC_WRITE = 0x40000000ul,
                       SIO_GENERIC_ALL = 0x10000000ul, SIO_FILE_READ_DATA = 0x1ul };
// Whether Fast saves buffers this open: write-only, and a save file (.msv world state or .sav), any case.
inline bool sio_bufferable(const char* path, unsigned long access) {
  if (!path || !(access & SIO_GENERIC_WRITE)) return false;
  if (access & (SIO_GENERIC_READ | SIO_GENERIC_ALL | SIO_FILE_READ_DATA)) return false;
  const size_t n = strlen(path);
  if (n < 4) return false;
  return _stricmp(path + n - 4, ".msv") == 0 || _stricmp(path + n - 4, ".sav") == 0;
}

// One real write of up to n bytes: *wrote = the bytes taken; 0 on failure.
typedef int (*SioWriteOnce)(void* ctx, const unsigned char* p, unsigned n, unsigned* wrote);
// All n bytes, one real write after another. 0 on a failure or a write that takes nothing (it would never end).
inline int sio_write_all(SioWriteOnce w, void* ctx, const unsigned char* p, unsigned n) {
  while (n) {
    unsigned k = 0;
    if (!w(ctx, p, n, &k) || k == 0 || k > n) return 0;
    p += k; n -= k;
  }
  return 1;
}
// Writes out what the buffer holds. It is empty afterwards either way: a failed flush's bytes are lost, and the
// caller records the failure.
inline int sio_buf_flush(SioBuf* b, SioWriteOnce w, void* ctx) {
  const unsigned n = b->len;
  b->len = 0;
  return n ? sio_write_all(w, ctx, b->data, n) : 1;
}
enum { SIO_BUF_FAILED = -1, SIO_BUF_DIRECT = 0, SIO_BUF_TAKEN = 1 };
// The engine writes n bytes. TAKEN: copied into the buffer (flushed first when they do not fit). DIRECT: larger than
// the buffer; it was flushed and the caller writes these bytes itself. FAILED: the flush failed.
inline int sio_buf_write(SioBuf* b, const void* src, unsigned n, SioWriteOnce w, void* ctx) {
  if (n > b->cap) return sio_buf_flush(b, w, ctx) ? SIO_BUF_DIRECT : SIO_BUF_FAILED;
  if (n > b->cap - b->len && !sio_buf_flush(b, w, ctx)) return SIO_BUF_FAILED;
  if (n) memcpy(b->data + b->len, src, n);
  b->len += n;
  return SIO_BUF_TAKEN;
}

// The per-file line. total runs from before CreateFile to after CloseHandle; engine = total minus every file-API
// call inside it (the engine's own serialisation between the calls). real= is the kernel32 WriteFile time, already
// inside the write/seek/flush/close times (Fast saves flushes from those hooks), so engine does not subtract it.
inline int sio_format(char* out, int cap, const SioFile* f, long long close, long long t_end, double ticks_per_ms) {
  if (cap < 1) return 0;
  const double k = ticks_per_ms > 0.0 ? 1.0 / ticks_per_ms : 0.0;
  const long long total = t_end - f->t_open;
  const long long api = f->create + f->write.ticks + f->seek.ticks + f->flush.ticks + close;
  int n = _snprintf(out, (size_t)cap - 1,
      "saveio: %s total=%.1f ms create=%.1f write=%u calls %.1f ms (max %.1f) real=%u calls %.1f ms bytes=%llu "
      "close=%.1f ms seek=%u calls %.1f ms flush=%u calls %.1f ms engine=%.1f ms",
      f->name, total * k, f->create * k, f->write.calls, f->write.ticks * k, f->write.max * k, f->real.calls,
      f->real.ticks * k, f->bytes, close * k, f->seek.calls, f->seek.ticks * k, f->flush.calls, f->flush.ticks * k,
      (total - api) * k);
  int len = (n < 0 || n > cap - 1) ? cap - 1 : n;
  if (f->flushFails && len < cap - 1) {
    n = _snprintf(out + len, (size_t)(cap - 1 - len), " flushfail=%u err=%lu", f->flushFails, f->flushErr);
    len = (n < 0 || n > cap - 1 - len) ? cap - 1 : len + n;
  }
  out[len] = 0;
  return len;
}
