#pragma once
// Off-screen edge arrows for nukes, babes and secrets: direction, the point on
// the inset rect, fan-out and the panel test. Screen pixels, y down; pure, so
// the suite runs every rule here.
#include "w2s.h"
#include "ui/ui.h"   // UiRect
#include "classes.h" // CAT_*
#include <math.h>

const int   ARROW_MARGIN    = 40;   // inset from every screen edge, at 100% UI scale
const int   ARROW_LEN       = 18;   // tip to base
const int   ARROW_BASE      = 14;   // base width
const int   ARROW_FAN_STEP  = 24;   // spacing along the edge inside one fan
const int   ARROW_LABEL_GAP = 3;    // base to the distance label
const int   ARROW_MAX       = 64;   // per frame; a level has a handful of objectives
const float ARROW_FAN_DEG   = 6.f;  // arrows closer than this in angle fan out

// (x, y) is the tip, on the inset rect; (dx, dy) the unit direction it points;
// tag the caller's index, carried through the fan's sort.
struct EdgeArrow { float x, y, dx, dy, angle; int tag; };

inline float arrow_margin(int scalePct) { return (float)ARROW_MARGIN * (float)scalePct / 100.f; }

// The categories with an arrow sub-row, each gated by its own. A nuke still in a crate is a CAT_NUKE entry, so it
// gets the Nukes arrow too.
inline bool arrow_wanted_cat(unsigned char cat, bool nukes, bool babes, bool secrets) {
  if (cat == CAT_NUKE) return nukes;
  if (cat == CAT_BABE) return babes;
  if (cat == CAT_SECRET) return secrets;
  return false;
}

// The inset rect and the clockwise perimeter walk the fan spreads along: it
// starts at the left edge's middle (angle -pi), so the walk grows with angle.
struct ArrowInset { float L, T, R, B, cx, cy, a1, iw, ih, P; };

inline bool arrow_inset(int W, int H, float margin, ArrowInset* r) {
  r->L = margin; r->T = margin; r->R = (float)W - margin; r->B = (float)H - margin;
  r->cx = (float)W * 0.5f; r->cy = (float)H * 0.5f;
  r->iw = r->R - r->L; r->ih = r->B - r->T;
  r->a1 = r->cy - r->T;
  r->P = 2.f * (r->iw + r->ih);
  return r->iw > 0.f && r->ih > 0.f;
}

inline float arrow_perimeter_s(const ArrowInset& r, float x, float y) {
  const float eps = 0.01f;
  if (x <= r.L + eps) return y < r.cy ? r.cy - y : r.a1 + 2.f * r.iw + r.ih + (r.B - y);
  if (y <= r.T + eps) return r.a1 + (x - r.L);
  if (x >= r.R - eps) return r.a1 + r.iw + (y - r.T);
  return r.a1 + r.iw + r.ih + (r.R - x);
}

inline void arrow_perimeter_point(const ArrowInset& r, float s, float* x, float* y) {
  s = fmodf(s, r.P);
  if (s < 0.f) s += r.P;
  if (s < r.a1) { *x = r.L; *y = r.cy - s; return; }
  s -= r.a1;
  if (s < r.iw) { *x = r.L + s; *y = r.T; return; }
  s -= r.iw;
  if (s < r.ih) { *x = r.R; *y = r.T + s; return; }
  s -= r.ih;
  if (s < r.iw) { *x = r.R - s; *y = r.B; return; }
  s -= r.iw;
  *x = r.L; *y = r.B - s;
}

// In front, from the screen centre toward the projected point. At or behind the near plane the projection flips, so
// camera-space (x, -y) is used instead (an aspect-correct projection scales both axes alike, so the angle stays
// continuous across the side planes). Dead behind has no side: it points down.
inline bool arrow_make(Vec3 p, const Mat4& view, const Mat4& proj, float nearZ, int W, int H,
                       float margin, int tag, EdgeArrow* a) {
  ArrowInset r;
  if (!arrow_inset(W, H, margin, &r)) return false;
  float dx, dy;
  const ScreenPt sp = w2s_project(p, view, proj, nearZ, W, H);
  if (sp.ok) { dx = sp.x - r.cx; dy = sp.y - r.cy; }
  else {
    const float wp[4] = { p.x, p.y, p.z, 1.f };
    float v[4];
    w2s_mul(wp, view, v);
    dx = v[0]; dy = -v[1];
  }
  const float len = sqrtf(dx * dx + dy * dy);
  if (!(len > 1e-6f)) { dx = 0.f; dy = 1.f; }
  else { dx /= len; dy /= len; }
  // The nearer side of the inset rect along the ray is where it leaves.
  float t = 1e30f;
  if (dx > 0.f) t = (r.R - r.cx) / dx;
  else if (dx < 0.f) t = (r.L - r.cx) / dx;
  if (dy > 0.f) { const float ty = (r.B - r.cy) / dy; if (ty < t) t = ty; }
  else if (dy < 0.f) { const float ty = (r.T - r.cy) / dy; if (ty < t) t = ty; }
  float x = r.cx + dx * t, y = r.cy + dy * t;
  x = x < r.L ? r.L : x > r.R ? r.R : x;
  y = y < r.T ? r.T : y > r.B ? r.B : y;
  a->x = x; a->y = y; a->dx = dx; a->dy = dy; a->angle = atan2f(dy, dx); a->tag = tag;
  return true;
}

// Sorts by angle, groups neighbours closer than ARROW_FAN_DEG and spreads each
// group ARROW_FAN_STEP apart along the perimeter walk, so an arrow never leaves
// the inset rect; groups whose spreads would touch merge. Direction is kept.
inline void arrows_fan_out(EdgeArrow* a, int n, int W, int H, float margin) {
  ArrowInset r;
  if (!a || n < 2 || n > ARROW_MAX || !arrow_inset(W, H, margin, &r)) return;
  const float pi = 3.14159265f, fan = ARROW_FAN_DEG * pi / 180.f, step = (float)ARROW_FAN_STEP;
  for (int i = 1; i < n; ++i) {
    const EdgeArrow k = a[i];
    int j = i - 1;
    while (j >= 0 && a[j].angle > k.angle) { a[j + 1] = a[j]; j--; }
    a[j + 1] = k;
  }
  // Start after the widest gap, so a group straddling +-pi (dead left) stays whole.
  int start = 0;
  float widest = -1.f;
  for (int i = 0; i < n; ++i) {
    const float g = i ? a[i].angle - a[i - 1].angle : a[0].angle + 2.f * pi - a[n - 1].angle;
    if (g > widest) { widest = g; start = i; }
  }
  EdgeArrow o[ARROW_MAX];
  float s[ARROW_MAX];
  int grp[ARROW_MAX];   // grp[g] = first member of group g
  int groups = 0;
  for (int j = 0; j < n; ++j) {
    o[j] = a[(start + j) % n];
    s[j] = arrow_perimeter_s(r, o[j].x, o[j].y);
    if (j && s[j] < s[j - 1] - r.P * 0.5f) s[j] += r.P;
    float gap = j ? o[j].angle - o[j - 1].angle : 0.f;
    if (gap < 0.f) gap += 2.f * pi;
    if (!j || !(gap < fan)) grp[groups++] = j;
  }
  // Merge until no two neighbouring spreads come closer than one step.
  float centre[ARROW_MAX];
  for (bool merged = true; merged;) {
    merged = false;
    for (int g = 0; g < groups; ++g) {
      const int b = grp[g], e = g + 1 < groups ? grp[g + 1] : n;
      float sum = 0.f;
      for (int j = b; j < e; ++j) sum += s[j];
      centre[g] = sum / (float)(e - b);
    }
    for (int g = 0; g + 1 < groups; ++g) {
      const int b = grp[g], m = grp[g + 1], e = g + 2 < groups ? grp[g + 2] : n;
      const float lastA = centre[g] + ((float)(m - b) - 1.f) * 0.5f * step;
      const float firstB = centre[g + 1] - ((float)(e - m) - 1.f) * 0.5f * step;
      if (firstB - lastA < step) {
        for (int k = g + 1; k + 1 < groups; ++k) grp[k] = grp[k + 1];
        groups--;
        merged = true;
        break;
      }
    }
  }
  for (int g = 0; g < groups; ++g) {
    const int b = grp[g], e = g + 1 < groups ? grp[g + 1] : n;
    if (e - b < 2) continue;   // a lone arrow keeps its exact exit point
    for (int j = b; j < e; ++j)
      arrow_perimeter_point(r, centre[g] + ((float)(j - b) - (float)(e - b - 1) * 0.5f) * step, &o[j].x, &o[j].y);
  }
  for (int j = 0; j < n; ++j) a[j] = o[j];
}

// Tip first, then the two base corners.
inline void arrow_triangle(const EdgeArrow& a, float px[3], float py[3]) {
  const float bx = a.x - a.dx * (float)ARROW_LEN, by = a.y - a.dy * (float)ARROW_LEN;
  const float hw = (float)ARROW_BASE * 0.5f;
  px[0] = a.x; py[0] = a.y;
  px[1] = bx - a.dy * hw; py[1] = by + a.dx * hw;
  px[2] = bx + a.dy * hw; py[2] = by - a.dx * hw;
}

// The label's top-left: beyond the base, toward the screen centre, pushed out
// by its own half extent along the arrow so it never covers the arrowhead.
inline void arrow_label_pos(const EdgeArrow& a, int tw, int th, int* lx, int* ly) {
  const float push = (float)(ARROW_LEN + ARROW_LABEL_GAP) + fabsf(a.dx) * (float)tw * 0.5f +
                     fabsf(a.dy) * (float)th * 0.5f;
  const float cx = a.x - a.dx * push, cy = a.y - a.dy * push;
  *lx = (int)floorf(cx - (float)tw * 0.5f + 0.5f);
  *ly = (int)floorf(cy - (float)th * 0.5f + 0.5f);
}

// The arrowhead and its label together.
inline UiRect arrow_bounds(const EdgeArrow& a, int tw, int th) {
  float px[3], py[3];
  arrow_triangle(a, px, py);
  int lx, ly;
  arrow_label_pos(a, tw, th, &lx, &ly);
  float x0 = (float)lx, y0 = (float)ly, x1 = (float)(lx + tw), y1 = (float)(ly + th);
  for (int i = 0; i < 3; ++i) {
    if (px[i] < x0) x0 = px[i];
    if (px[i] > x1) x1 = px[i];
    if (py[i] < y0) y0 = py[i];
    if (py[i] > y1) y1 = py[i];
  }
  const int ix = (int)floorf(x0), iy = (int)floorf(y0);
  UiRect b = { ix, iy, (int)ceilf(x1) - ix, (int)ceilf(y1) - iy };
  return b;
}

// Any overlap with the open panel hides the arrow: the panel draws after the
// ESP and an arrow half under it reads as part of the panel.
inline bool arrow_suppressed(const UiRect& b, bool panelOpen, const UiRect& panel) {
  if (!panelOpen) return false;
  return b.x < panel.x + panel.w && panel.x < b.x + b.w && b.y < panel.y + panel.h && panel.y < b.y + b.h;
}
