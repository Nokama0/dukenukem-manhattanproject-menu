#pragma once
// CPU clipping for the canvas: the engine has no scissor we may set from the
// end-frame hook, so canvas_push_clip is honoured by trimming geometry before it
// is submitted. Pixel space, y down. Pure; pinned by the suite.

struct ClipRect { float x0, y0, x1, y1; };   // x0 < x1 and y0 < y1 for a non-empty clip

// Trims an axis-aligned rect to the clip and moves its UVs by the same
// fraction, so a clipped glyph keeps its texels where they were. Returns 0 when
// nothing is left (outside, touching an edge, or a zero-area clip or rect).
inline int clip_rect_uv(const ClipRect* c, float* x0, float* y0, float* x1, float* y1,
                        float* u0, float* v0, float* u1, float* v1) {
  if (!(c->x1 > c->x0) || !(c->y1 > c->y0)) return 0;
  const float w = *x1 - *x0, h = *y1 - *y0;
  if (!(w > 0.f) || !(h > 0.f)) return 0;
  if (*x1 <= c->x0 || *x0 >= c->x1 || *y1 <= c->y0 || *y0 >= c->y1) return 0;
  const float du = *u1 - *u0, dv = *v1 - *v0;
  if (*x0 < c->x0) { *u0 += du * (c->x0 - *x0) / w; *x0 = c->x0; }
  if (*x1 > c->x1) { *u1 -= du * (*x1 - c->x1) / w; *x1 = c->x1; }
  if (*y0 < c->y0) { *v0 += dv * (c->y0 - *y0) / h; *y0 = c->y0; }
  if (*y1 > c->y1) { *v1 -= dv * (*y1 - c->y1) / h; *y1 = c->y1; }
  return 1;
}

// A convex polygon (triangle or quad) against the clip, Sutherland-Hodgman,
// one edge at a time; UVs are interpolated along each cut edge. Every edge can
// add at most one vertex, so a quad never needs more than CLIP_POLY_MAX.
struct ClipVert { float x, y, u, v; };
const int CLIP_POLY_MAX = 8;

inline int clip_poly_edge(const ClipVert* in, int n, ClipVert* out, int axis, float bound, int keepBelow) {
  int m = 0;
  for (int i = 0; i < n; ++i) {
    const ClipVert& a = in[i];
    const ClipVert& b = in[(i + 1) % n];
    const float pa = axis ? a.y : a.x, pb = axis ? b.y : b.x;
    const bool ina = keepBelow ? pa <= bound : pa >= bound;
    const bool inb = keepBelow ? pb <= bound : pb >= bound;
    if (ina) out[m++] = a;
    if (ina != inb) {
      const float t = (bound - pa) / (pb - pa);
      ClipVert p;
      p.x = axis ? a.x + (b.x - a.x) * t : bound;
      p.y = axis ? bound : a.y + (b.y - a.y) * t;
      p.u = a.u + (b.u - a.u) * t;
      p.v = a.v + (b.v - a.v) * t;
      out[m++] = p;
    }
  }
  return m;
}

// Returns the clipped vertex count (a fan around out[0]), or 0 when fewer
// than three survive. n must be 3 or 4.
inline int clip_poly(const ClipRect* c, const ClipVert* in, int n, ClipVert* out) {
  if (n < 3 || n > 4 || !(c->x1 > c->x0) || !(c->y1 > c->y0)) return 0;
  ClipVert a[CLIP_POLY_MAX], b[CLIP_POLY_MAX];
  for (int i = 0; i < n; ++i) a[i] = in[i];
  n = clip_poly_edge(a, n, b, 0, c->x0, 0); if (n < 3) return 0;
  n = clip_poly_edge(b, n, a, 0, c->x1, 1); if (n < 3) return 0;
  n = clip_poly_edge(a, n, b, 1, c->y0, 0); if (n < 3) return 0;
  n = clip_poly_edge(b, n, a, 1, c->y1, 1); if (n < 3) return 0;
  for (int i = 0; i < n; ++i) out[i] = a[i];
  return n;
}
