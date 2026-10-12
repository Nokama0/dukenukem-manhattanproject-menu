#pragma once
// World point -> screen pixels with the engine's matrices (render.md section 3):
// 16 floats row-major, row vectors (p' = p * M), camera looks down +z, w_clip = z_view.
struct Vec3 { float x, y, z; };
struct Mat4 { float m[16]; };
struct ScreenPt { float x, y; int ok; };

inline void w2s_mul(const float in[4], const Mat4& M, float out[4]) {
  for (int j = 0; j < 4; ++j)
    out[j] = in[0] * M.m[j] + in[1] * M.m[4 + j] + in[2] * M.m[8 + j] + in[3] * M.m[12 + j];
}

// ok = 0 at or behind the near plane, and at w <= 0 whatever near says: the divide by w
// would flip or explode there.
inline ScreenPt w2s_project(Vec3 p, const Mat4& view, const Mat4& proj, float nearZ, int W, int H) {
  const float wp[4] = { p.x, p.y, p.z, 1.f };
  float v[4], c[4];
  w2s_mul(wp, view, v);
  w2s_mul(v, proj, c);
  ScreenPt s = { 0.f, 0.f, 0 };
  if (!(c[3] > nearZ) || !(c[3] > 0.f)) return s;
  s.x = (c[0] / c[3] + 1.f) * 0.5f * (float)W;
  s.y = (1.f - c[1] / c[3]) * 0.5f * (float)H;
  s.ok = 1;
  return s;
}
