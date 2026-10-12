#pragma once
// Inline detour, x86 only. Ported from duke3d-wt/src/wtmod/hook.cpp with an
// absolute target (engine functions are resolved by export name, not RVA).

// JMPs `target` to `replacement` only if its first fpLen bytes equal `fp` (fpLen >= prologueLen >= 5).
// The trampoline is stored to *trampolineOut before the patch goes live: a thread entering at
// once must never jump through a null pointer. Returns 1 when installed.
int hook_install_at(void* target, const unsigned char* fp, unsigned fpLen,
                    unsigned prologueLen, void* replacement, void** trampolineOut);
