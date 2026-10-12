#pragma once
// The stall sampler (docs/re/stalls.md): a watchdog thread that samples the main thread's EIP and EBP chain when a
// frame runs past 300 ms, to name the module behind the ~1 s hitches. Measurement only.

// Main thread, last thing on the frame path (probe_frame): publishes "a frame ended now". The first call starts the
// watchdog, so it never runs before the engine's first frame.
void stall_frame_end(void);

// Main thread: the samples the watchdog took since the last stall_frame_end, for the hitch line's stall=.
int stall_frame_samples(void);

// DLL_PROCESS_DETACH: logs the session's counters and tells the watchdog to stop.
void stall_detach(void);
