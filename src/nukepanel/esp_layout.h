#pragma once
// ESP marker layout, ported from the Alien Armageddon port's ekmod/esp_layout.h (brackets, bar, labels, the marker
// pipeline) with its aimsolve.h range cull. World Tour's geometry, so a marker reads the same in every port.
// Every function widens to long long before adding and clamps back.
#include <stdio.h>
#include <math.h>
#include "ui/common.h"   // clamp_ll_to_int
#include "espbox.h"      // EkScreenBox
#include "canvas.h"       // canvas_text, canvas_with_alpha, C_KEYLINE

const int ESP_BRACKET_T   = 2;   // arm thickness
const int ESP_BRACKET_DIV = 3;   // arm length is edge/this
const int ESP_BRACKET_MIN = 3;   // floor, when it fits

// Health bar, outside the box to its left so it keeps the box's full height.
const int ESP_BAR_W   = 3;
const int ESP_BAR_GAP = 3;

// ESP_LABEL_GAP is font_height[FONT_SMALL], so a descender never clips the box.
const int ESP_LABEL_GAP = 17;
const int ESP_DIST_GAP  = 2;

// Cap (dim-1)/2, not dim/2: two opposing arms of dim/2 on an even edge would meet
// and close the brackets into an outline, which merges markers into a grid in a crowd.
inline int esp_bracket_arm(int dim) {
  if (dim <= 0) return 0;
  int a = dim / ESP_BRACKET_DIV;
  if (a < ESP_BRACKET_MIN) a = ESP_BRACKET_MIN;
  const int cap = (dim - 1) / 2;
  if (a > cap) a = cap;
  return a < 0 ? 0 : a;
}

// Nukes, the level objective, draw twice the weight.
const int ESP_NUKE_BRACKET_T = 2 * ESP_BRACKET_T;

inline int esp_bracket_thickness(int armX, int armY, int want = ESP_BRACKET_T) {
  int t = want;
  if (t > armX) t = armX;
  if (t > armY) t = armY;
  return t < 0 ? 0 : t;
}

inline int esp_label_x(int boxX, int boxW, int textW) {
  return clamp_ll_to_int((long long)boxX + (long long)boxW / 2 - (long long)textW / 2);
}

// gap is the label font's height: ESP_LABEL_GAP for FONT_SMALL.
inline int esp_name_label_y(int boxY, int gap = ESP_LABEL_GAP) {
  return clamp_ll_to_int((long long)boxY - gap);
}

inline int esp_dist_label_y(int boxY, int boxH) {
  return clamp_ll_to_int((long long)boxY + (long long)boxH + ESP_DIST_GAP);
}

// The bar fills bottom-up; canvas_bar_fill bounds the fill to the bar.
inline int esp_bar_x(int boxX) {
  return clamp_ll_to_int((long long)boxX - ESP_BAR_GAP - ESP_BAR_W);
}

// Name in FONT_SMALL, number in FONT_MONO so a column of enemies keeps its digits
// aligned, centred as one pair.
const int ESP_LABEL_RUN_GAP = 4;
struct EspLabelRuns { int nameX, numX, totalW; };
inline EspLabelRuns esp_label_runs(int boxX, int boxW, int nameW, int numW) {
  EspLabelRuns r = { 0, 0, 0 };
  const int gap = (nameW > 0 && numW > 0) ? ESP_LABEL_RUN_GAP : 0;
  r.totalW = clamp_ll_to_int((long long)nameW + gap + numW);
  r.nameX = esp_label_x(boxX, boxW, r.totalW);
  r.numX = clamp_ll_to_int((long long)r.nameX + nameW + gap);
  return r;
}

// canvas_text takes a line-box top and the atlas has no per-face ascent, so the
// runs share their bottom edge.
inline int esp_label_mono_y(int nameY, int smallH, int monoH) {
  return clamp_ll_to_int((long long)nameY + smallH - monoH);
}

// Max 0 means unknown: "%d" alone, never "%d/0". hp <= 0 has no honest number.
const int ESP_HEALTH_NUM_BUFSIZE = 32;
inline bool esp_health_number(char* numBuf, int hp, int maxhp) {
  if (hp <= 0) return false;
  if (maxhp > 0) sprintf_s(numBuf, ESP_HEALTH_NUM_BUFSIZE, "%d/%d", hp, maxhp);
  else           sprintf_s(numBuf, ESP_HEALTH_NUM_BUFSIZE, "%d", hp);
  return true;
}

// Engine health is a float (+0xB84): rounded up, so a live monster below 1 hp
// never reads 0.
inline int esp_hp_int(float hp) {
  if (!(hp > 0.f)) return 0;
  if (hp > 1e9f) return 1000000000;
  return (int)ceilf(hp);
}

// The only unit scale the engine states: it passes 52.0 to snd_distance_factor
// (render.md section 3).
const float ESP_UNITS_PER_METRE = 52.f;

// Draw distance in metres: AA's range (512..131072 Build units at 512 a metre)
// and its default, the maximum, since a cull shipped active reads as broken.
const int ESP_DRAWDIST_MIN_M = 1;
const int ESP_DRAWDIST_MAX_M = 256;

// Horizontal plane only (world up is +Y): a low target close by is never culled
// by height alone.
inline float esp_dist_m(float dx, float dz) {
  return sqrtf(dx * dx + dz * dz) / ESP_UNITS_PER_METRE;
}

// Compared against the 2D distance, never depth; `unlimited` is its own term so the
// slider keeps meaning metres.
inline bool ekm_within_draw_distance(int dist2d, int limit, int unlimited) {
  if (unlimited) return true;
  if (dist2d < 0) return false;
  return dist2d <= limit;
}

// Nuke/babe/secret point marker: a screen-space cross, AA's shape and numbers. Both bars share one centre pixel;
// labels use the bracket box's two gaps, so a cross and a box read the same way in one room.
const int ESP_SECRET_ARM = 5;   // half-length of each cross arm, in pixels
const int ESP_SECRET_T   = 2;   // bar thickness; matches ESP_BRACKET_T's weight

inline int esp_secret_bar_span(int arm = ESP_SECRET_ARM) { return arm * 2 + 1; }
inline int esp_secret_hbar_x(int cx, int arm = ESP_SECRET_ARM) { return clamp_ll_to_int((long long)cx - arm); }
inline int esp_secret_hbar_y(int cy, int t = ESP_SECRET_T) { return clamp_ll_to_int((long long)cy - t / 2); }
inline int esp_secret_vbar_x(int cx, int t = ESP_SECRET_T) { return clamp_ll_to_int((long long)cx - t / 2); }
inline int esp_secret_vbar_y(int cy, int arm = ESP_SECRET_ARM) { return clamp_ll_to_int((long long)cy - arm); }

inline int esp_secret_name_label_y(int cy, int arm = ESP_SECRET_ARM) {
  return clamp_ll_to_int((long long)cy - arm - ESP_LABEL_GAP);
}
inline int esp_secret_dist_label_y(int cy, int arm = ESP_SECRET_ARM) {
  return clamp_ll_to_int((long long)cy + arm + 1 + ESP_DIST_GAP);
}

// AA's explodable x (its esp_layout.h esp_saltire): the two diagonals, `arm` out along each axis.
struct EspSaltire { int x0, y0, x1, y1, x2, y2, x3, y3; };
inline EspSaltire esp_saltire(int cx, int cy, int arm) {
  const int l = clamp_ll_to_int((long long)cx - arm), r = clamp_ll_to_int((long long)cx + arm);
  const int t = clamp_ll_to_int((long long)cy - arm), b = clamp_ll_to_int((long long)cy + arm);
  EspSaltire s = { l, t, r, b, l, b, r, t };
  return s;
}

// ---------------------------------------------------------------- markers
// Collected before any is drawn, so there is a painter order and a way to choose
// which to keep when a frame has more than it should draw.
struct EkScreenPt { int x, y, dist2d; };
// `index` is the slot in this frame's entity snapshot (entities.h).
struct EkMarker { int kind, index, dist2d, seq; EkScreenPt p; EkScreenBox box; };

// Two ceilings, and they must differ: with gather == draw, ekm_keep_nearest's
// `keep >= count` guard is always true and the cap becomes first-come, not nearest.
const int EK_MARKER_COLLECT_MAX = 1024;   // what one frame may gather
const int EK_MARKER_MAX         = 256;    // what one frame may draw
enum { EK_MARKER_ENEMY = 0 };

// Keep the nearest `keep`, then let the caller order them: far-to-near is the
// painter order, and a cap applied to it would drop the near markers.
inline void ekm_keep_nearest(EkMarker* m, int count, int keep) {
  if (!m || count <= 0 || keep <= 0 || keep >= count) return;
  for (int i = 0; i < keep; i++) {
    int best = i;
    for (int j = i + 1; j < count; j++)
      if (m[j].dist2d < m[best].dist2d) best = j;
    if (best != i) { const EkMarker t = m[i]; m[i] = m[best]; m[best] = t; }
  }
}

// Farthest first, so a near marker draws over a far one. Takes the caller's count
// as given: the caller clamps to EK_MARKER_MAX first.
inline void ekm_sort_far_to_near(EkMarker* m, int count) {
  if (!m || count < 2) return;
  for (int i = 1; i < count; i++) {
    const EkMarker key = m[i];
    int j = i - 1;
    // Strict `<`: equal distances keep collection order and do not swap frame to frame.
    while (j >= 0 && m[j].dist2d < key.dist2d) { m[j + 1] = m[j]; j--; }
    m[j + 1] = key;
  }
}

// --- Outlined world-overlay labels, always on ------------------------------
// Ported from AA's esp_keyline_colour, esp_draw_label, esp_draw_outlined_text and esp_outline_offsets, outlined form
// only: there is no menu switch for high-visibility overlays here, so every world text draws with the outline.
const int ESP_LABEL_OUTLINE_R = 2;   // AA's ESP_CORNER_OUTLINE; logical px

// The outline's stamps: the 8 neighbours at radius r, never (0,0), the fill's own.
inline int esp_outline_offsets(int r, int dx[8], int dy[8]) {
  if (r <= 0 || !dx || !dy) return 0;
  int n = 0;
  for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) {
      if (i == 0 && j == 0) continue;
      dx[n] = i * r; dy[n] = j * r; n++;
    }
  return n;
}

// The outline takes the fill's own alpha, so a fading label (damage numbers)
// fades its edge with it rather than leaving a black ghost.
inline unsigned esp_keyline_colour(unsigned fill) {
  return canvas_with_alpha(C_KEYLINE, (int)(fill >> 24));
}

// The outline stamped at the 8 offsets, then the fill: one plain canvas_text
// call each, matching AA's esp_draw_outlined_text at r ESP_LABEL_OUTLINE_R.
inline void esp_draw_label(int x, int y, const char* s, unsigned fill, int font) {
  int dx[8], dy[8];
  const int n = esp_outline_offsets(ESP_LABEL_OUTLINE_R, dx, dy);
  const unsigned outline = esp_keyline_colour(fill);
  for (int i = 0; i < n; i++) canvas_text(x + dx[i], y + dy[i], s, outline, font);
  canvas_text(x, y, s, fill, font);
}
