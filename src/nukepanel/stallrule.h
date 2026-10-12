#pragma once
// The stall sampler's pure rules (docs/re/stalls.md): when the watchdog samples a long frame, how it walks the main
// thread's EBP chain, and how one address is written. stall.cpp owns the thread and the suspend.
#include <stdio.h>
#include <string.h>

enum { STALL_FIRST_MS = 300, STALL_SECOND_MS = 800, STALL_LONG_MS = 3000, STALL_MAX_SAMPLES = 2,
       STALL_MAX_STALLS = 40, STALL_MAX_FRAMES = 24 };
enum { STALL_SKIP = 0, STALL_SAMPLE = 1, STALL_SAMPLE_LONG = 2 };

// What the watchdog does for a frame elapsed_ms long, with `taken` normal samples, (longDone) its long sample taken,
// and `stalls` counted against the cap. A stall counts at its first sample, so the last stall under the cap still gets
// its second. A frame past 3 s (level load, minimised window) gets one long sample, cap or not, then nothing.
inline int stall_decide(unsigned elapsed_ms, int taken, int longDone, int stalls) {
  if (longDone) return STALL_SKIP;
  if (elapsed_ms >= STALL_LONG_MS) return STALL_SAMPLE_LONG;
  if (taken <= 0) return stalls < STALL_MAX_STALLS && elapsed_ms >= STALL_FIRST_MS ? STALL_SAMPLE : STALL_SKIP;
  return taken < STALL_MAX_SAMPLES && elapsed_ms >= STALL_SECOND_MS ? STALL_SAMPLE : STALL_SKIP;
}

// The cap count after a sample of `kind` in a frame that had `taken` normal samples before it: the first normal
// sample counts the frame, and its long sample gives that back (a frame past 3 s is not the hitch being hunted).
inline int stall_count_after(int stalls, int kind, int taken) {
  if (kind == STALL_SAMPLE && taken == 0) return stalls + 1;
  if (kind == STALL_SAMPLE_LONG && taken > 0) return stalls - 1;
  return stalls;
}

// Reads one 4-byte word at addr into *out; 0 when the address cannot be read.
typedef int (*StallRead)(unsigned addr, unsigned* out, void* ctx);

// The return addresses up the EBP chain from ebp, at most max. A frame ([ebp] = caller's ebp, [ebp+4] = return
// address) must be 4-aligned and lie whole inside [lo, hi); a zero return address or an unreadable word ends the
// walk, and so does a caller frame that is not above the current one (the stack grows down).
inline int stall_walk(unsigned ebp, unsigned lo, unsigned hi, StallRead rd, void* ctx, unsigned* rets, int max) {
  int n = 0;
  while (n < max && (ebp & 3) == 0 && ebp >= lo && ebp < hi && hi - ebp >= 8) {
    unsigned next, ret;
    if (!rd(ebp, &next, ctx) || !rd(ebp + 4, &ret, ctx) || ret == 0) break;
    rets[n++] = ret;
    if (next <= ebp) break;
    ebp = next;
  }
  return n;
}

// "<module basename>+0x<addr - base>", or "0x<addr>" (8 digits) when no module holds it. Always NUL-terminated;
// returns the length written.
inline int stall_fmt_addr(char* out, int cap, const char* modpath, unsigned addr, unsigned base) {
  if (cap <= 0) return 0;
  int n;
  if (modpath) {
    const char* b = modpath;
    for (const char* p = modpath; *p; ++p) if (*p == '/' || *p == '\\') b = p + 1;
    n = snprintf(out, (size_t)cap, "%s+0x%x", b, addr - base);
  } else {
    n = snprintf(out, (size_t)cap, "0x%08x", addr);
  }
  if (n < 0) { out[0] = 0; return 0; }
  return n < cap ? n : cap - 1;
}
