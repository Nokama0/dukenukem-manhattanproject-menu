#pragma once
// The end-frame detour: the jump instruments and the panel run from it on the
// frame path.

// DllMain(PROCESS_ATTACH), after engine_resolve: verifies the frame-path call
// site and installs the rend_end_frame detour. Returns 1 when installed.
int probe_install(void);

// Runs from the detour on the frame path only, on the game's main thread.
void probe_frame(void);

// The frame-log's own running average, in fps, 0 before the first second is
// measured (framestats.h's fs_ready). Read by the FRAME RATE hint row: the
// same window this already logs, not a second tracker.
double probe_measured_fps(void);
