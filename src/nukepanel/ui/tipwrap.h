#pragma once
// Tooltip text wrapped to a width (a long hint ran off the screen on one line). Kept beside
// tooltip.h, which is verbatim Megaton's; tooltip_box_place still flips and clamps the wrapped box.
#include "tooltip.h"
#include <string.h>

enum { TIP_MAX_W = 420, TIP_MIN_W = 80, TIP_LINES_MAX = 8, TIP_LINE_GAP = 2 };

// The widest a hint's text may run: TIP_MAX_W, narrowed to the panel and to the screen less the box's padding.
inline int tip_max_w(int panelW, int screenW) {
  int w = TIP_MAX_W;
  if (panelW < w) w = panelW;
  if (screenW - 2 * TOOLTIP_PAD < w) w = screenW - 2 * TOOLTIP_PAD;
  return w < TIP_MIN_W ? TIP_MIN_W : w;
}

// Width of s[0..n), logical px.
typedef int (*TipMeasure)(const char* s, int n, void* ctx);

// Greedy lines at spaces, each no wider than maxW; a word wider than maxW is cut where it overflows. The spaces at
// a break are dropped. start/len per line, at most cap lines (the rest is dropped); *widest the widest line.
inline int tip_wrap(const char* s, int maxW, TipMeasure measure, void* ctx, int* start, int* len, int cap,
                    int* widest) {
  *widest = 0;
  int n = 0, i = 0;
  const int total = s ? (int)strlen(s) : 0;
  while (n < cap) {
    while (i < total && s[i] == ' ') ++i;
    if (i >= total) break;
    int end = i, fit = -1;   // fit: the end of the last whole word that fits
    for (int j = i; j <= total; ++j) {
      if (j < total && s[j] != ' ') continue;
      if (measure(s + i, j - i, ctx) > maxW) break;
      fit = j;
      while (j < total && s[j + 1] == ' ') ++j;
    }
    if (fit > i) {
      end = fit;
    } else {   // the first word alone is too wide: as many characters as fit, at least one
      end = i + 1;
      while (end < total && s[end] != ' ' && measure(s + i, end + 1 - i, ctx) <= maxW) ++end;
    }
    start[n] = i;
    len[n] = end - i;
    const int w = measure(s + i, end - i, ctx);
    if (w > *widest) *widest = w;
    ++n;
    i = end;
  }
  return n;
}
