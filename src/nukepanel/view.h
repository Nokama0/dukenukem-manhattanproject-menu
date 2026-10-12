#pragma once
// The player's view, captured during the world pass: at rend_end_frame the
// view stack is back at identity (render.md section 3).
#include "w2s.h"

// DllMain(PROCESS_ATTACH), after engine_resolve: detours rend_camera_push. Returns 1 when installed.
int view_install(void);

// 1 only when a client.dll push was captured this frame; fills all three outputs.
int view_get(Mat4* view, Mat4* proj, float* nearZ);
// The same push before zoom-out or the freecam replaced its view: the game's own camera, whose
// sides clip a bullet (re/offscreen-and-track.md A1). Equal to view_get when nothing was replaced.
int view_get_stock(Mat4* view, Mat4* proj, float* nearZ);

// The projection FOV of the last captured push (rend_projection_get), 0 before the first.
float view_last_fov(void);

// End of each frame-path call: clears this frame's capture, logs the push counts every 5 s.
void view_frame_end(void);
// Every other rend_end_frame (rend_restart, plugins): drops a capture so the next frame
// never uses it, and logs each of the first 4 distinct callers (`ret`) once.
void view_frame_discard(void* ret);
