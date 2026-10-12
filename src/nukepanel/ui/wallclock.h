// Verbatim from Megaton's src/duke3dmod/wallclock.h; kept identical on purpose -- do not edit here.
#pragma once

// A free-running millisecond counter to date events by. UNSIGNED AND IT WRAPS, by design: consumers measure an AGE
// as the unsigned difference `now - t0` (dmgnum.h), exact across the wrap. wallclock.cpp is DLL-only (one
// performance-counter call), so the suite never links it; tests hand their own `now` to the pure rules.
unsigned wallclock_ms();
