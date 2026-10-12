#pragma once
// A secret trigger volume's box on screen: its 8 corners projected, and which
// of its 6 faces and 12 edges can be drawn. Pure, so the suite runs it.
#include "w2s.h"
#include "espbox.h"   // esp_box_from_corners, the on-screen test

// Corner i is centre +- half on each axis: bit 0 x, bit 1 y, bit 2 z (espbox.h's order).
// Faces are listed as a loop, so (0,1,2) and (0,2,3) cover each one.
const unsigned char VOL_FACE[6][4] = {
  { 0, 2, 6, 4 }, { 1, 3, 7, 5 },   // -x, +x
  { 0, 1, 5, 4 }, { 2, 3, 7, 6 },   // -y, +y
  { 0, 1, 3, 2 }, { 4, 5, 7, 6 },   // -z, +z
};
const unsigned char VOL_EDGE[12][2] = {
  { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },   // along x
  { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },   // along y
  { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },   // along z
};

struct VolumeGeom { ScreenPt c[8]; int faceOk[6], edgeOk[12]; int faces, edges, onScreen; };

// Corner i of a box with centre c and half-axis vectors ax[0..2] (bit 0 ax[0], bit 1 ax[1], bit 2 ax[2]).
inline Vec3 volume_corner(Vec3 c, const Vec3* ax, int i) {
  Vec3 q = c;
  for (int a = 0; a < 3; ++a) {
    const float s = (i >> a) & 1 ? 1.f : -1.f;
    q.x += s * ax[a].x; q.y += s * ax[a].y; q.z += s * ax[a].z;
  }
  return q;
}

// A face or edge with any corner at or behind the near plane is skipped: its
// projection would flip through the camera. Clipping can come later.
inline void volume_project_axes(Vec3 c, const Vec3* ax, const Mat4& view, const Mat4& proj, float nearZ,
                                int W, int H, VolumeGeom* g) {
  for (int i = 0; i < 8; ++i) g->c[i] = w2s_project(volume_corner(c, ax, i), view, proj, nearZ, W, H);
  g->faces = g->edges = 0;
  for (int f = 0; f < 6; ++f) {
    g->faceOk[f] = g->c[VOL_FACE[f][0]].ok && g->c[VOL_FACE[f][1]].ok &&
                   g->c[VOL_FACE[f][2]].ok && g->c[VOL_FACE[f][3]].ok;
    g->faces += g->faceOk[f];
  }
  for (int e = 0; e < 12; ++e) {
    g->edgeOk[e] = g->c[VOL_EDGE[e][0]].ok && g->c[VOL_EDGE[e][1]].ok;
    g->edges += g->edgeOk[e];
  }
  esp_box_from_corners(g->c, W, H, &g->onScreen);
}
inline void volume_project(Vec3 pos, Vec3 half, const Mat4& view, const Mat4& proj, float nearZ,
                           int W, int H, VolumeGeom* g) {
  const Vec3 ax[3] = { { half.x, 0.f, 0.f }, { 0.f, half.y, 0.f }, { 0.f, 0.f, half.z } };
  volume_project_axes(pos, ax, view, proj, nearZ, W, H, g);
}
