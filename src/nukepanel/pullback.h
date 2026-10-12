#pragma once
// Zoom out: the camera pulled back along its own forward row, as a pair of
// matrices handed to rend_camera_push in place of the engine's (render.md
// section 3: row vectors, p' = p * M, the camera looks down +z).
#include <string.h>

// view = world->camera, inv = camera->world (rows right, up, forward, eye); d in
// world units. Only the eye row of inv and view's z translation change.
inline void pullback(const float view[16], const float inv[16], float d, float outView[16], float outInv[16]) {
  memcpy(outView, view, 16 * sizeof(float));
  memcpy(outInv, inv, 16 * sizeof(float));
  for (int k = 0; k < 3; ++k) outInv[12 + k] = inv[12 + k] - d * inv[8 + k];
  // view is inv's inverse and f (inv's third row) is A's own row, so f*A^-1 = (0,0,1):
  // view's z translation grows by exactly d.
  outView[14] = view[14] + d;
}
