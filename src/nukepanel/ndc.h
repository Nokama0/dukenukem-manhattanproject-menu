#pragma once
// Pixel -> the engine's ortho space (rend_set_projection_type(2): x -1..1
// left to right, y +1 at the top; render.md section 2).
inline float ndc_x(float px, int W) { return 2.f * px / (float)W - 1.f; }
inline float ndc_y(float py, int H) { return 1.f - 2.f * py / (float)H; }
// The inverses, so a pixel can be checked after a trip through NDC.
inline float ndc_to_px_x(float nx, int W) { return (nx + 1.f) * (float)W * 0.5f; }
inline float ndc_to_px_y(float ny, int H) { return (1.f - ny) * (float)H * 0.5f; }
