#pragma once
// An entity's screen box from its 8 bbox corners, projected with w2s.h.
// Corners behind the near plane have no screen point, so they never enter the
// min/max: the side they would reach runs to the screen edge instead.
#include "w2s.h"
#include <math.h>

struct EkScreenBox { int x, y, w, h; };

// ok = 0 when no corner is in front or the box lies wholly off screen. The
// caller skips an entity whose centre is behind (esp_project_box does).
inline EkScreenBox esp_box_from_corners(const ScreenPt c[8], int W, int H, int* ok) {
  EkScreenBox b = { 0, 0, 0, 0 };
  *ok = 0;
  if (W <= 0 || H <= 0) return b;
  float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
  int front = 0, behind = 0;
  for (int i = 0; i < 8; ++i) {
    if (!c[i].ok) { behind++; continue; }
    if (!front) { x0 = x1 = c[i].x; y0 = y1 = c[i].y; }
    else {
      if (c[i].x < x0) x0 = c[i].x;
      if (c[i].x > x1) x1 = c[i].x;
      if (c[i].y < y0) y0 = c[i].y;
      if (c[i].y > y1) y1 = c[i].y;
    }
    front++;
  }
  if (!front) return b;
  // A behind corner projects outward from the screen centre: extend each side
  // that already reaches past the centre toward its edge.
  const float fW = (float)W, fH = (float)H;
  if (behind) {
    if (x0 < fW * 0.5f) x0 = 0.f;
    if (x1 > fW * 0.5f) x1 = fW;
    if (y0 < fH * 0.5f) y0 = 0.f;
    if (y1 > fH * 0.5f) y1 = fH;
  }
  if (!(x1 > 0.f) || !(y1 > 0.f) || !(x0 < fW) || !(y0 < fH)) return b;
  if (x0 < 0.f) x0 = 0.f;
  if (y0 < 0.f) y0 = 0.f;
  if (x1 > fW) x1 = fW;
  if (y1 > fH) y1 = fH;
  b.x = (int)floorf(x0);
  b.y = (int)floorf(y0);
  const int ex = (int)ceilf(x1), ey = (int)ceilf(y1);
  b.w = ex - b.x < 1 ? 1 : ex - b.x;
  b.h = ey - b.y < 1 ? 1 : ey - b.y;
  *ok = 1;
  return b;
}

// The engine's collision box, world-axis-aligned (hitboxes.md section 0): pos is the
// world position (+0x18C), dim the half-extents (+0x2B8), o the height offset (+0x2C4).
// Bullets and pickups test this box, not the +0x2F0 bbox, which ignores o.
struct Hitbox { Vec3 c, half; };
inline Hitbox hitbox_from(Vec3 pos, Vec3 dim, float o) {
  const float hy = dim.y - fabsf(o);
  Hitbox h = { { pos.x, pos.y - o, pos.z }, { dim.x, hy > 0.f ? hy : 0.f, dim.z } };
  return h;
}

// pos is a box centre and half its half-extents: for an entity, hitbox_from's c and half.
inline EkScreenBox esp_project_box(Vec3 pos, Vec3 half, const Mat4& view, const Mat4& proj,
                                   float nearZ, int W, int H, int* ok) {
  *ok = 0;
  if (!w2s_project(pos, view, proj, nearZ, W, H).ok) { EkScreenBox none = { 0, 0, 0, 0 }; return none; }
  ScreenPt c[8];
  for (int i = 0; i < 8; ++i) {
    const Vec3 q = { pos.x + ((i & 1) ? half.x : -half.x), pos.y + ((i & 2) ? half.y : -half.y),
                     pos.z + ((i & 4) ? half.z : -half.z) };
    c[i] = w2s_project(q, view, proj, nearZ, W, H);
  }
  return esp_box_from_corners(c, W, H, ok);
}
