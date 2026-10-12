#pragma once
// Duke's track for the freecam teleport marker: the pure rules. teleport.cpp reads the track and
// calls the engine's own snap. Layout and addresses: re/offscreen-and-track.md B2-B4.
#include "w2s.h"   // Vec3
#include <math.h>

// One +0xB2C sample: x, 0, z and the running arc length (game_track_tessellate 0x100945c0).
// That these are world space and +0xB30 is the row stride is unproven.
struct TrkPt { float x, y, z, len; };
// At most this many samples (n * stride) are read; more is refused with a log line, never truncated
// (re/offscreen-and-track.md). FUN_10094c50 gives a span at most 32 samples, so stride <= 33, and world.scr tracks
// have up to 26 points: 858. 1024 covers them (512 refused a real track).
enum { TRK_MAX_PTS = 1024 };

// The polyline in order. Span i (i..i+1, or n-1..0 when looped) holds samples j = 0..counts[i];
// a non-looped track's last span holds its end point only (tessellate, get_segment 0x10095410).
// Returns the number written to out, or -1 when counts or stride disagree with the layout.
inline int trk_flatten(const TrkPt* s, int n, int stride, const int* counts, int looped, TrkPt* out, int cap) {
  if (!s || !counts || n < 2 || stride < 1 || n * stride > cap) return -1;
  int w = 0;
  for (int i = 0; i < n; ++i) {
    const int last = (i < n - 1 || looped) ? counts[i] : 0;
    if (last < 0 || last + 1 > stride) return -1;
    for (int j = 0; j <= last; ++j) out[w++] = s[i * stride + j];
  }
  return w;
}

// The nearest crossing, in XZ, of the ray o + s*d with the polyline, 0 < s <= maxS. y comes from
// the ray at that s: the track has no height (game_track_finalize zeroes it).
struct TrkHit { int ok; float s; Vec3 p; int seg; };
inline TrkHit trk_ray_polyline(Vec3 o, Vec3 d, const TrkPt* p, int n, float maxS) {
  TrkHit h = { 0, 0.f, { 0.f, 0.f, 0.f }, -1 };
  const float dl = sqrtf(d.x * d.x + d.z * d.z);
  if (!(dl > 1e-6f)) return h;   // straight up or down: no XZ direction to cross along
  for (int i = 0; i + 1 < n; ++i) {
    const float ex = p[i + 1].x - p[i].x, ez = p[i + 1].z - p[i].z;
    const float el = sqrtf(ex * ex + ez * ez);
    if (!(el > 1e-6f)) continue;   // span joints repeat a point
    const float den = d.x * ez - d.z * ex;
    if (fabsf(den) <= 1e-6f * dl * el) continue;   // parallel or collinear
    const float wx = p[i].x - o.x, wz = p[i].z - o.z;
    const float s = (wx * ez - wz * ex) / den;
    const float t = (wx * d.z - wz * d.x) / den;
    if (t < 0.f || t > 1.f || !(s > 0.f) || s > maxS) continue;
    if (h.ok && s >= h.s) continue;
    h.ok = 1; h.s = s; h.seg = i;
    h.p.x = o.x + s * d.x; h.p.y = o.y + s * d.y; h.p.z = o.z + s * d.z;
  }
  return h;
}

// Where a refused placement's red marker goes: the crossing when there is one,
// else the polyline point nearest, in XZ, to the ray's first maxS units, with y from the ray where it
// passes closest (s = 0 when the nearest ray point is the eye).
inline TrkHit trk_ray_nearest(Vec3 o, Vec3 d, const TrkPt* p, int n, float maxS) {
  TrkHit h = trk_ray_polyline(o, d, p, n, maxS);
  if (h.ok || n < 2) return h;
  const float dd = d.x * d.x + d.z * d.z;
  float best = 0.f;
  for (int i = 0; i + 1 < n; ++i) {
    const float ax = p[i].x, az = p[i].z, ex = p[i + 1].x - ax, ez = p[i + 1].z - az;
    const float ee = ex * ex + ez * ez;
    // Candidates: each end onto the ray, and the ray's two ends onto the segment.
    float cs[4], cx[4], cz[4];
    for (int k = 0; k < 2; ++k) {
      const float qx = k ? ax + ex : ax, qz = k ? az + ez : az;
      float s = dd > 1e-12f ? ((qx - o.x) * d.x + (qz - o.z) * d.z) / dd : 0.f;
      s = s < 0.f ? 0.f : (s > maxS ? maxS : s);
      cs[k] = s; cx[k] = qx; cz[k] = qz;
    }
    for (int k = 0; k < 2; ++k) {
      const float s = k ? (dd > 1e-12f ? maxS : 0.f) : 0.f;
      const float rx = o.x + s * d.x, rz = o.z + s * d.z;
      float t = ee > 1e-12f ? ((rx - ax) * ex + (rz - az) * ez) / ee : 0.f;
      t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
      cs[2 + k] = s; cx[2 + k] = ax + t * ex; cz[2 + k] = az + t * ez;
    }
    for (int k = 0; k < 4; ++k) {
      const float rx = o.x + cs[k] * d.x - cx[k], rz = o.z + cs[k] * d.z - cz[k];
      const float dist = rx * rx + rz * rz;
      if (h.ok && dist >= best) continue;
      h.ok = 1; best = dist; h.s = cs[k]; h.seg = i;
      h.p.x = cx[k]; h.p.y = o.y + cs[k] * d.y; h.p.z = cz[k];
    }
  }
  return h;
}

// The polyline point nearest q in x/z (any track): p.y = q.y, s the x/z distance, seg the span.
inline TrkHit trk_nearest_xz(Vec3 q, const TrkPt* p, int n) {
  TrkHit h = { 0, 0.f, { 0.f, 0.f, 0.f }, -1 };
  if (!p || n < 1) return h;
  float best = 0.f;
  for (int i = 0; i < n; ++i) {
    const int j = i + 1 < n ? i + 1 : i;   // one point: its own span
    const float ax = p[i].x, az = p[i].z, ex = p[j].x - ax, ez = p[j].z - az, ee = ex * ex + ez * ez;
    float t = ee > 1e-12f ? ((q.x - ax) * ex + (q.z - az) * ez) / ee : 0.f;
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    const float cx = ax + t * ex, cz = az + t * ez, d = (q.x - cx) * (q.x - cx) + (q.z - cz) * (q.z - cz);
    if (h.ok && d >= best) continue;
    h.ok = 1; best = d; h.seg = i;
    h.p.x = cx; h.p.y = q.y; h.p.z = cz;
    if (j == i) break;
  }
  h.s = sqrtf(best);
  return h;
}

// One track's claim on the crosshair's hit: d its x/z distance, dy |its floor's y - the hit's y| (filled only in the
// tie band; FLT_MAX-like when no floor was found). ok 0: unread, never chosen.
struct TrkCand { int ok; float d, dy; };
inline float trk_nearest_d(const TrkCand* c, int n) {
  float best = 3.4e38f;
  for (int i = 0; i < n; ++i)
    if (c[i].ok && c[i].d < best) best = c[i].d;
  return best;
}
// The nearest track within radius; among those within tie of the nearest, the floor nearest the hit's height (the
// tracks have no height, game_track_finalize). -1 when none is within radius.
inline int trk_choose(const TrkCand* c, int n, float radius, float tie) {
  const float best = trk_nearest_d(c, n);
  if (!(best <= radius)) return -1;
  int w = -1;
  for (int i = 0; i < n; ++i) {
    if (!c[i].ok || c[i].d > radius || c[i].d > best + tie) continue;
    if (w < 0 || c[i].dy < c[w].dy || (c[i].dy == c[w].dy && c[i].d < c[w].d)) w = i;
  }
  return w;
}

// game_track_project_point duke!0x10096260 = track vt+0x1EC (B4): the SEH prologue, `sub esp,0xb7c;
// push esi; push edi; mov edi,ecx; lea ecx,[esp+8]; call`. Bytes 3..6 are the relocated handler.
// Unique in duke_base (Ghidra, 2026-09-29); both track vtables hold it.
enum { TRK_PROJECT_FP_LEN = 36 };
static const unsigned char kTrkProjectFp[TRK_PROJECT_FP_LEN] = {
  0x6A, 0xFF, 0x68, 0x00, 0x00, 0x00, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x64, 0x89,
  0x25, 0x00, 0x00, 0x00, 0x00, 0x81, 0xEC, 0x7C, 0x0B, 0x00, 0x00, 0x56, 0x57, 0x8B, 0xF9, 0x8D,
  0x4C, 0x24, 0x08, 0xE8 };
inline int trk_fp_is_project(const unsigned char* b) {
  if (!b) return 0;
  for (int i = 0; i < TRK_PROJECT_FP_LEN; ++i)
    if ((i < 3 || i > 6) && b[i] != kTrkProjectFp[i]) return 0;
  return 1;
}
