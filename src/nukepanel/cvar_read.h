#pragma once
// Shared cvar-string reader (input-console.md section 1): zoom.cpp (g_cam_fov) and framecap.cpp (gl_sync) both walk
// the same cvar_t chain, so the pattern is written once here.
#include "engine.h"
#include <stddef.h>

// Follows cv's link chain to the final cvar, then reads its live or default
// string per the +0x69 ptr/read-only byte. Bounds-checked each hop; refused
// if not terminated within 8 (real chains are far shorter).
bool cvar_read_string(cvar_t* cv, char* out, size_t cap);
