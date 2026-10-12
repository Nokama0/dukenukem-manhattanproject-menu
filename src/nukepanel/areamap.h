#pragma once
// Cross-area secrets, part 4: the schematic map of the part's areas in the Progress tab's "This level" section. Pure
// layout (menu.cpp draws it); label widths come from the caller, which owns the fonts. Each box: its label, an S row
// and an N row of pips; a finished area a tick and "all found"; the current area a notch above it (AM_TOP).
#include "areas.h"

enum { AM_BOX_H = 52, AM_PAD_X = 6, AM_TICK = 10, AM_TICK_GAP = 4, AM_LABEL_MIN = 22, AM_GAP_X = 30, AM_GAP_Y = 10,
       AM_TOP = 10, AM_BOTTOM = 4, AM_BEND = 12, AM_LINKS_MAX = AR_AREAS_MAX * (AR_AREAS_MAX - 1) / 2 };
// A pip row: "S" or "N", a gap, then one 9 px square per secret or nuke, at most AM_PIPS_MAX.
enum { AM_PIP = 9, AM_PIP_GAP = 2, AM_PIP_PREFIX_GAP = 4, AM_PIPS_MAX = 10, AM_ROW_STEP = AM_PIP + 4 };

struct AmBox { int x, y, w, h, layer, row; };
// One link per pair of areas. from/to: the one-way link's direction; a two-way one runs from the lower index.
// Drawn as two segments, (x0,y0)-(xm,ym)-(x1,y1); the arrowhead sits at (x1,y1).
struct AmLink { int from, to, oneway, run, route; int x0, y0, xm, ym, x1, y1; };
struct AmMap {
  int ok, n, nlayers, boxW, w, h, nlinks, routeTo;
  AmBox box[AR_AREAS_MAX];
  AmLink link[AM_LINKS_MAX];
};

// A doorway from area i lands in area j (ar_route's edge).
inline bool am_edge(const ArPart* p, int i, int j) {
  if (i == j) return false;
  for (int k = 0; k < p->areas[i].ndoors; ++k)
    if (p->areas[i].doors[k].landing >= 0 && p->areas[i].doors[k].target == j) return true;
  return false;
}

// Breadth-first from the part's first area (its _a01), links taken both ways: an area reached only through a
// one-way link still gets a column, and linked areas differ by at most one layer, so no link crosses a column.
// Areas with no link to the first share one column after the last. The layer count.
inline int am_layers(const ArPart* p, int layer[AR_AREAS_MAX]) {
  const int n = p->nareas < AR_AREAS_MAX ? p->nareas : AR_AREAS_MAX;
  if (n <= 0) return 0;
  int queue[AR_AREAS_MAX], qh = 0, qt = 0, deepest = 0;
  for (int i = 0; i < n; ++i) layer[i] = -1;
  layer[0] = 0;
  queue[qt++] = 0;
  while (qh < qt) {
    const int a = queue[qh++];
    for (int b = 0; b < n; ++b) {
      if (layer[b] >= 0 || !(am_edge(p, a, b) || am_edge(p, b, a))) continue;
      layer[b] = layer[a] + 1;
      if (layer[b] > deepest) deepest = layer[b];
      queue[qt++] = b;
    }
  }
  int nlayers = deepest + 1;
  bool orphan = false;
  for (int i = 0; i < n; ++i)
    if (layer[i] < 0) { layer[i] = nlayers; orphan = true; }
  return nlayers + (orphan ? 1 : 0);
}

// Columns left to right by layer, a fixed gap between them; within a column by area name, centred on the tallest.
// Every box takes the widest label's width (plus the finished tick) or the widest pip row's (rowW), shrunk to fit
// `width` but never below AM_LABEL_MIN: ok 0 when even that does not fit. AM_BEND stays free right of the last column.
inline void am_place(const ArPart* p, const int layer[AR_AREAS_MAX], int nlayers, const int labelW[AR_AREAS_MAX],
                     int width, AmMap* m, int rowW = 0, int rows = 2) {
  const int n = p->nareas < AR_AREAS_MAX ? p->nareas : AR_AREAS_MAX;
  m->ok = 0;
  m->n = n;
  m->nlayers = nlayers;
  if (n <= 0 || nlayers <= 0) return;
  const int chrome = 2 * AM_PAD_X + AM_TICK + AM_TICK_GAP;
  int widest = AM_LABEL_MIN;
  for (int i = 0; i < n; ++i)
    if (labelW[i] > widest) widest = labelW[i];
  const int want = widest + chrome > rowW + 2 * AM_PAD_X ? widest + chrome : rowW + 2 * AM_PAD_X;
  const int fit = (width - (nlayers - 1) * AM_GAP_X - AM_BEND) / nlayers;
  const int boxW = want < fit ? want : fit;
  if (boxW < AM_LABEL_MIN + chrome) return;
  int count[AR_AREAS_MAX + 1] = { 0 }, tallest = 0;
  for (int i = 0; i < n; ++i) {
    int row = 0;
    for (int j = 0; j < n; ++j)
      if (j != i && layer[j] == layer[i] && (_stricmp(p->areas[j].map, p->areas[i].map) < 0 ||
                                             (_stricmp(p->areas[j].map, p->areas[i].map) == 0 && j < i)))
        row++;
    m->box[i].layer = layer[i];
    m->box[i].row = row;
    if (++count[layer[i]] > tallest) tallest = count[layer[i]];
  }
  const int boxH = AM_BOX_H + (rows > 2 ? (rows - 2) * AM_ROW_STEP : 0);   // a third pip row (ambushes)
  const int innerH = tallest * boxH + (tallest - 1) * AM_GAP_Y;
  for (int i = 0; i < n; ++i) {
    AmBox& b = m->box[i];
    const int k = count[b.layer];
    const int colH = k * boxH + (k - 1) * AM_GAP_Y;
    b.w = boxW;
    b.h = boxH;
    b.x = b.layer * (boxW + AM_GAP_X);
    b.y = AM_TOP + (innerH - colH) / 2 + b.row * (boxH + AM_GAP_Y);
  }
  m->boxW = boxW;
  m->w = nlayers * boxW + (nlayers - 1) * AM_GAP_X;
  m->h = AM_TOP + innerH + AM_BOTTOM;
  m->ok = 1;
}

// The links between placed boxes. Across columns: facing edges' midpoints. In one column: adjacent rows join
// bottom to top; rows further apart bend out right of the column, so the line misses the boxes between. A link
// is a depth run when any of its doorways has a run end (ar_link's d.run).
inline void am_links(const ArPart* p, AmMap* m) {
  m->nlinks = 0;
  for (int i = 0; i < m->n; ++i)
    for (int j = i + 1; j < m->n; ++j) {
      const bool ij = am_edge(p, i, j), ji = am_edge(p, j, i);
      if (!ij && !ji) continue;
      AmLink& L = m->link[m->nlinks++];
      memset(&L, 0, sizeof L);
      L.oneway = ij != ji;
      L.from = ij ? i : j;
      L.to = ij ? j : i;
      for (int e = 0; e < 2; ++e) {
        const int a = e ? j : i, b = e ? i : j;
        for (int k = 0; k < p->areas[a].ndoors; ++k) {
          const ArDoor& d = p->areas[a].doors[k];
          if (d.landing >= 0 && d.target == b && d.run[0]) L.run = 1;
        }
      }
      const AmBox &A = m->box[L.from], &B = m->box[L.to];
      const int ya = A.y + A.h / 2, yb = B.y + B.h / 2;
      if (A.layer != B.layer) {
        const bool right = A.layer < B.layer;
        L.x0 = right ? A.x + A.w : A.x;
        L.x1 = right ? B.x : B.x + B.w;
        L.y0 = ya;
        L.y1 = yb;
      } else if (A.row - B.row == 1 || B.row - A.row == 1) {
        const bool down = A.row < B.row;
        L.x0 = A.x + A.w / 2;
        L.x1 = B.x + B.w / 2;
        L.y0 = down ? A.y + A.h : A.y;
        L.y1 = down ? B.y : B.y + B.h;
      } else {
        L.x0 = A.x + A.w;
        L.x1 = B.x + B.w;
        L.y0 = ya;
        L.y1 = yb;
        L.xm = A.x + A.w + AM_BEND;
        L.ym = (ya + yb) / 2;
        continue;
      }
      L.xm = (L.x0 + L.x1) / 2;
      L.ym = (L.y0 + L.y1) / 2;
    }
}

// An area missing secrets: its state known and fewer found than it has (the list's own test for a route line).
inline bool am_missing(const ArArea* a, const ArFound* f) {
  return f->known && ar_found_count(a, f) < a->nsecrets;
}

// The current route, its links marked: the door label's cross-area choice when there is one, else ar_route to the
// nearest other area missing secrets (fewest hops, then file order). None when the current area is unknown, still
// has missing secrets, or nothing else is missing (and no choice). Returns the target area, or -1.
inline int am_route_mark(const ArPart* p, int cur, const ArFound* found, AmMap* m, const ArCross* cross = 0) {
  for (int l = 0; l < m->nlinks; ++l) m->link[l].route = 0;
  m->routeTo = -1;
  if (cur < 0 || cur >= m->n) return -1;
  ArHop best[AR_HOPS_MAX];
  int bestN = -1;
  if (cross) {
    bestN = ar_cross_hops(p, cur, cross, best, AR_HOPS_MAX);
    if (bestN > 0) m->routeTo = cross->target;
  } else if (am_missing(&p->areas[cur], &found[cur])) {
    return -1;
  }
  for (int i = 0; i < m->n && !cross; ++i) {
    if (i == cur || !am_missing(&p->areas[i], &found[i])) continue;
    ArHop hops[AR_HOPS_MAX];
    const int k = ar_route(p, cur, i, hops, AR_HOPS_MAX);
    if (k <= 0 || (bestN >= 0 && k >= bestN)) continue;
    bestN = k;
    memcpy(best, hops, sizeof(ArHop) * (size_t)k);
    m->routeTo = i;
  }
  for (int h = 0; h < bestN; ++h) {
    const int a = best[h].area, b = p->areas[a].doors[best[h].door].target;
    for (int l = 0; l < m->nlinks; ++l) {
      AmLink& L = m->link[l];
      if ((L.from == a && L.to == b) || (L.from == b && L.to == a)) L.route = 1;
    }
  }
  return m->routeTo;
}

// One pip row of a box or a list row: none when the area has none of them, "?" when its state is unknown, a pip
// each (filled found, hollow missing) when at most AM_PIPS_MAX and `fit` of them fit, else "n/m" text.
enum { AM_ROW_NONE = 0, AM_ROW_PIPS, AM_ROW_TEXT, AM_ROW_UNKNOWN };
struct AmRow { int mode, found, total; };
inline AmRow am_pip_row(int total, int found, int known, int fit) {
  AmRow r = { AM_ROW_NONE, found, total };
  if (total <= 0) return r;
  if (!known) r.mode = AM_ROW_UNKNOWN;
  else r.mode = total <= AM_PIPS_MAX && total <= fit ? AM_ROW_PIPS : AM_ROW_TEXT;
  return r;
}
// The pips that fit `room` px after a prefix `prefixW` px wide.
inline int am_pips_fit(int room, int prefixW) {
  const int left = room - prefixW - AM_PIP_PREFIX_GAP + AM_PIP_GAP;
  return left > 0 ? left / (AM_PIP + AM_PIP_GAP) : 0;
}
// The width of a row of n pips after its prefix.
inline int am_pips_w(int n, int prefixW) {
  return n > 0 ? prefixW + AM_PIP_PREFIX_GAP + n * AM_PIP + (n - 1) * AM_PIP_GAP : prefixW;
}

// Every secret and nuke of the area found, its state known: the box's tick and "all found". An area with neither
// is never finished.
inline bool am_finished(const ArArea* a, const ArFound* sf, const ArNukeFound* nf) {
  if (a->nsecrets + a->nukeTotal <= 0) return false;
  if (a->nsecrets > 0 && (!sf->known || ar_found_count(a, sf) < a->nsecrets)) return false;
  if (a->nukeTotal > 0 && (!nf->known || nf->count < a->nukeTotal)) return false;
  return true;
}

// The most pip rows a box needs: S, N and A (the ambushes that fire once) where the area has any.
inline int am_box_rows(const ArPart* p) {
  int best = 0;
  for (int i = 0; i < p->nareas && i < AR_AREAS_MAX; ++i) {
    const ArArea& a = p->areas[i];
    const int r = (a.nsecrets > 0) + (a.nukeTotal > 0) + (ar_once_ambushes(&a) > 0);
    if (r > best) best = r;
  }
  return best;
}

// The whole map; ok 0 when the part is empty or the boxes do not fit `width`.
inline void am_build(const ArPart* p, int cur, const ArFound* found, const int labelW[AR_AREAS_MAX], int width,
                     AmMap* m, const ArCross* cross = 0, int rowW = 0, int rows = 2) {
  memset(m, 0, sizeof *m);
  m->routeTo = -1;
  int layer[AR_AREAS_MAX];
  const int nlayers = am_layers(p, layer);
  am_place(p, layer, nlayers, labelW, width, m, rowW, rows);
  if (!m->ok) return;
  am_links(p, m);
  am_route_mark(p, cur, found, m, cross);
}
