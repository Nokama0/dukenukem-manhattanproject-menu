#pragma once
// Free camera > Photo: an N x N grid of screen-sized tiles stitched into one PNG (docs/re/photo.md). Pure; the glue is
// photo.cpp.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>   // abs

// THE TILE LEVER IS THE FOV PLUS A VIEW SHEAR, NOT A PROJECTION MATRIX. gl's set_persp (gl!0x10003120) builds a
// symmetric glFrustum from (near, far, fov) and the workspace aspect; no export takes a matrix. A tile's projection is
// the full one scaled by n and offset: x_tile = n*x_ndc - ox (the rest: photo.md section 2).
enum { PHOTO_MULT_MIN = 2, PHOTO_MULT_MAX = 16, PHOTO_MULT_DEF = 4 };
// Frames: settled (frozen and hidden) before the first tile; arming given up; one tile's push never seen.
enum { PHOTO_SETTLE_FRAMES = 2, PHOTO_ARM_TIMEOUT = 120, PHOTO_TILE_TIMEOUT = 30 };
// Frames a tile is drawn before the screen is read. The capture reads the COMPOSED screen, behind up to three
// queued presents and one composition: older captures are dropped at DRAIN, the tile read from CAPTURE on.
enum { PHOTO_HOLD_DRAIN = 4, PHOTO_HOLD_CAPTURE = 6 };

inline int photo_mult_clamped(int m) { return m < PHOTO_MULT_MIN ? PHOTO_MULT_MIN : m > PHOTO_MULT_MAX ? PHOTO_MULT_MAX : m; }

// The fov whose half-angle tangent is the full one's over n: every tile magnifies by exactly n.
inline float photo_tile_fov(float fovDeg, int n) {
  const double half = (double)fovDeg * 0.5 * 0.017453292519943;
  return (float)(2.0 * atan(tan(half) / (double)n) * 57.29577951308232);
}

// The part of the client the monitor shows: client pixels (vx, vy, w, h) and the same rect on screen (sx, sy).
// The capture reads the composed screen, so rows off the monitor (the borderless window sits at y = -1) do not exist.
struct PhotoVis { int vx, vy, w, h, sx, sy; };
inline bool photo_visible(int clientX, int clientY, int W, int H, int monL, int monT, int monR, int monB, PhotoVis* v) {
  const int x0 = clientX > monL ? clientX : monL, y0 = clientY > monT ? clientY : monT;
  const int x1 = clientX + W < monR ? clientX + W : monR, y1 = clientY + H < monB ? clientY + H : monB;
  memset(v, 0, sizeof *v);
  if (x1 <= x0 || y1 <= y0) return false;
  v->vx = x0 - clientX; v->vy = y0 - clientY; v->w = x1 - x0; v->h = y1 - y0; v->sx = x0; v->sy = y0;
  return true;
}

// Tile (r, c), row 0 at the top, of a big image n*v.w x n*v.h that is the visible rect magnified n times. A tile's
// render pixel x shows big pixel c*v.w + (x - v.vx), i.e. full-render pixel v.vx + that/n; in NDC that is
// x_tile = n*x_ndc - ox. With the whole render visible, ox = 2c + 1 - n and oy = n - 2r - 1.
inline void photo_tile_offsets(int n, int r, int c, int W, int H, const PhotoVis& v, float* ox, float* oy) {
  *ox = (float)(1.0 - n + (2.0 * v.vx * (n - 1) + 2.0 * c * v.w) / (double)W);
  *oy = (float)(n - 1.0 - (2.0 * v.vy * (n - 1) + 2.0 * r * v.h) / (double)H);
}

// The pushed view sheared so the n-times projection (m0t, m5t: its [0] and [5], read back from the engine) shows
// x_tile = n*x_ndc - ox, y_tile = n*y_ndc - oy.
inline void photo_tile_matrices(const float view[16], const float inv[16], float m0t, float m5t, float ox, float oy,
                                float outView[16], float outInv[16]) {
  // The scale is a narrower fov (tan halved n times, both axes: m[0], m[5] grow by n); the offset is x_view' = x_view -
  // k*z_view (w = z_view, so it subtracts m0t*k from x_ndc). Depth is untouched, so the z-buffer matches; the inverse's
  // right and up rows are untouched, so billboards keep their size.
  const float k = ox / m0t, m = oy / m5t;
  for (int i = 0; i < 16; ++i) { outView[i] = view[i]; outInv[i] = inv[i]; }
  for (int i = 0; i < 4; ++i) {
    outView[i * 4 + 0] = view[i * 4 + 0] - k * view[i * 4 + 2];
    outView[i * 4 + 1] = view[i * 4 + 1] - m * view[i * 4 + 2];
  }
  // The inverse shear: forward row += k*right + m*up; right, up and eye unchanged.
  for (int j = 0; j < 4; ++j) outInv[8 + j] = inv[8 + j] + k * inv[j] + m * inv[4 + j];
}

// A 24-bit row padded to 4 bytes (the strips WIC encodes).
inline unsigned photo_stride(int w) { return ((unsigned)w * 3u + 3u) & ~3u; }

// One captured tile (BGRA rows, top first: Desktop Duplication's and the GDI DIB's) into tile column c of a strip of
// 24bppBGR rows. True when any pixel is not black: the per-tile check against a capture that only sees black.
inline bool photo_copy_bgra(const unsigned char* src, unsigned pitch, int w, int h, unsigned char* strip,
                            unsigned stripStride, int c) {
  unsigned any = 0;
  for (int y = 0; y < h; ++y) {
    const unsigned char* s = src + (size_t)y * pitch;
    unsigned char* d = strip + (size_t)y * stripStride + (size_t)c * (size_t)w * 3u;
    for (int x = 0; x < w; ++x, s += 4, d += 3) {
      d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
      any |= (unsigned)s[0] | s[1] | s[2];
    }
  }
  return any != 0;
}

// --- A strip in chunks (a 226 MB strip failed "no memory" in a fragmented 32-bit address space) ----
// A strip is never one block: it is up to PHOTO_CHUNKS_MAX chunks of whole rows, each at most PHOTO_CHUNK_BYTES
// unless the cap on their number forces more rows into each. WIC takes them as consecutive WritePixels line batches.
const unsigned PHOTO_CHUNK_BYTES = 16u << 20;
enum { PHOTO_CHUNKS_MAX = 64 };

// Rows per chunk for a strip of h rows of `stride` bytes: at least 1, at most h.
inline int photo_chunk_rows(unsigned stride, int h) {
  const int byBytes = stride ? (int)(PHOTO_CHUNK_BYTES / stride) : h;
  const int byCount = (h + PHOTO_CHUNKS_MAX - 1) / PHOTO_CHUNKS_MAX;
  int r = byBytes < byCount ? byCount : byBytes;
  if (r > h) r = h;
  return r < 1 ? 1 : r;
}
// Chunks a strip of h rows needs at rowsPer rows each.
inline int photo_chunk_count(int h, int rowsPer) { return h > 0 && rowsPer > 0 ? (h + rowsPer - 1) / rowsPer : 0; }
// Rows in chunk k (the last one may be shorter).
inline int photo_chunk_rows_in(int k, int h, int rowsPer) {
  const int left = h - k * rowsPer;
  return left < 0 ? 0 : left < rowsPer ? left : rowsPer;
}
// Strip row y: its chunk and its row inside that chunk.
inline void photo_chunk_of(int y, int rowsPer, int* k, int* yIn) { *k = y / rowsPer; *yIn = y % rowsPer; }
// photo_copy_bgra into a chunked strip: rows 0..h-1 of the tile go to rows 0..h-1 of the strip's chunks.
inline bool photo_copy_bgra_chunks(const unsigned char* src, unsigned pitch, int w, int h, unsigned char* const* chunks,
                                   int rowsPer, unsigned stripStride, int c) {
  bool any = false;
  for (int k = 0; k < photo_chunk_count(h, rowsPer); ++k)
    any |= photo_copy_bgra(src + (size_t)k * (size_t)rowsPer * pitch, pitch, w, photo_chunk_rows_in(k, h, rowsPer),
                           chunks[k], stripStride, c);
  return any;
}

// Calibration (the window can be a row taller than the render, display.md): a PHOTO_CAL_BAND-px band, PHOTO_CAL_INSET
// px in from each edge (ROW1 top, ROW2 bottom, COL1 left, COL2 right), found by coverage-weighted centroid while the
// world is hidden. Bands, not 1-px lines: a 1-px mark was once read a row up. Colours: 0xRRGGBB.
const unsigned PHOTO_CAL_ROW1 = 0xFF0000u, PHOTO_CAL_ROW2 = 0x00FF00u, PHOTO_CAL_COL1 = 0x0000FFu, PHOTO_CAL_COL2 = 0xFF00FFu;
// WIN: lines either side of a band's fullest line that count toward its centroid. TOL: per channel; a band is a
// saturated colour, the world under it rarely all three.
const int PHOTO_CAL_BAND = 4, PHOTO_CAL_INSET = 8, PHOTO_CAL_WIN = 6, PHOTO_CAL_TOL = 48;

// The first render row/column of the near (farBand 0) or far band on an axis of n pixels, and the band's centre.
inline int photo_cal_band_at(int n, int farBand) { return farBand ? n - PHOTO_CAL_INSET - PHOTO_CAL_BAND : PHOTO_CAL_INSET; }
inline double photo_cal_band_centre(int n, int farBand) { return photo_cal_band_at(n, farBand) + (PHOTO_CAL_BAND - 1) * 0.5; }

inline bool photo_cal_match(const unsigned char* p, unsigned rgb) {
  const int r = (int)((rgb >> 16) & 0xFF), g = (int)((rgb >> 8) & 0xFF), b = (int)(rgb & 0xFF);
  return abs((int)p[2] - r) <= PHOTO_CAL_TOL && abs((int)p[1] - g) <= PHOTO_CAL_TOL && abs((int)p[0] - b) <= PHOTO_CAL_TOL;
}

// The fraction of row (vertical 0) or column (vertical 1) i of a BGRA image that is `rgb`.
inline double photo_cal_cover(const unsigned char* bgra, unsigned pitch, int w, int h, unsigned rgb, int vertical, int i) {
  const int len = vertical ? h : w;
  int hits = 0;
  for (int j = 0; j < len; ++j) {
    const int x = vertical ? i : j, y = vertical ? j : i;
    hits += photo_cal_match(bgra + (size_t)y * pitch + (size_t)x * 4u, rgb) ? 1 : 0;
  }
  return len > 0 ? (double)hits / (double)len : 0.0;
}

// A band's position: the coverage-weighted mean of the lines within PHOTO_CAL_WIN of its fullest line, which must be
// more than half covered; -1 when none is. A band smeared into a neighbouring row moves it by a fraction of a pixel,
// and colour elsewhere on screen (outside the window) does not move it at all.
inline double photo_band_centroid(const unsigned char* bgra, unsigned pitch, int w, int h, unsigned rgb, int vertical) {
  const int lines = vertical ? w : h;
  int peak = -1;
  double best = 0.5;
  for (int i = 0; i < lines; ++i) {
    const double c = photo_cal_cover(bgra, pitch, w, h, rgb, vertical, i);
    if (c > best) { best = c; peak = i; }
  }
  if (peak < 0) return -1.0;
  double sum = 0.0, wsum = 0.0;
  for (int i = peak - PHOTO_CAL_WIN; i <= peak + PHOTO_CAL_WIN; ++i) {
    if (i < 0 || i >= lines) continue;
    const double c = photo_cal_cover(bgra, pitch, w, h, rgb, vertical, i);
    sum += c * i;
    wsum += c;
  }
  return sum / wsum;
}

// One axis: bands centred at render positions r1 < r2 found at screen positions s1, s2 (negative: not found). 1:1
// when the distances agree within half a pixel; then *origin, the screen position of render 0, is the rounded mean
// of the two offsets. A real stretch (1440 rows over 1441: the distance grows by ~1 px) is refused.
inline bool photo_cal_axis(double r1, double r2, double s1, double s2, int* origin) {
  if (s1 < 0.0 || s2 < 0.0 || fabs((s2 - s1) - (r2 - r1)) >= 0.5) return false;
  *origin = (int)floor(((s1 - r1) + (s2 - r2)) * 0.5 + 0.5);
  return true;
}

// "<level> <yyyy-mm-dd hh-mm-ss> <W>x<H>.png". Characters Windows refuses in a name become '_';
// no level gives "level". -1 and an empty string when it does not fit: never a cut name.
inline int photo_file_name(char* out, int cap, const char* level, int y, int mo, int d, int h, int mi, int s, int W,
                           int H) {
  if (!out || cap <= 0) return -1;
  char lv[64];
  int n = 0;
  for (const char* p = level; p && *p && n < (int)sizeof lv - 1; ++p) {
    const unsigned char ch = (unsigned char)*p;
    lv[n++] = (ch < 0x20 || strchr("<>:\"/\\|?*", ch)) ? '_' : (char)ch;
  }
  lv[n] = 0;
  const int len = _snprintf_s(out, (size_t)cap, _TRUNCATE, "%s %04d-%02d-%02d %02d-%02d-%02d %dx%d.png",
                              n ? lv : "level", y, mo, d, h, mi, s, W, H);
  if (len < 0) { out[0] = 0; return -1; }
  return len;
}

// A photo never overwrites a file: name as it is (n <= 1), else "-n" before its ".png" ("... 7680x4320-2.png"), tried
// up to PHOTO_NAME_TRIES. -1 and "" when it does not fit or name is not a .png: never a cut name.
enum { PHOTO_NAME_TRIES = 9 };
inline int photo_file_name_n(const char* name, int n, char* out, int cap) {
  if (!out || cap <= 0) return -1;
  out[0] = 0;
  const int len = name ? (int)strlen(name) : 0;
  if (len < 4 || _stricmp(name + len - 4, ".png") != 0) return -1;
  const int w = n <= 1 ? _snprintf_s(out, (size_t)cap, _TRUNCATE, "%s", name)
                       : _snprintf_s(out, (size_t)cap, _TRUNCATE, "%.*s-%d.png", len - 4, name, n);
  if (w < 0) { out[0] = 0; return -1; }
  return w;
}

// "10x 34400x14400": the multiplier and the photo's pixel size.
inline void photo_size_text(int mult, int W, int H, char* out, int cap) {
  if (!out || cap <= 0) return;
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "%dx %dx%d", mult, mult * W, mult * H);
}

// --- The capture, one step a frame (end of frame). ARMING waits for a whole frame drawn with the world frozen and
// everything hidden. TILE (r, c) is what the pushes render until the screen capture has read it (tileRead). A row's
// last tile sends its strip to the encoder (FLUSH); WAIT holds for a free strip buffer; DRAIN waits for the encoder.
enum { PH_IDLE = 0, PH_ARMING, PH_TILE, PH_WAIT, PH_DRAIN };
enum { PA_NONE = 0, PA_START, PA_FLUSH, PA_FINISH, PA_ABORT };
enum { PHR_NONE = 0, PHR_CANCELLED, PHR_CAMERA, PHR_PANEL, PHR_LEVEL, PHR_SCREEN, PHR_FOCUS, PHR_SIZE, PHR_NOT_FROZEN,
       PHR_THAWED, PHR_NO_TILE, PHR_ENCODER, PHR_START, PHR_CALIBRATION };
// flushRow: the row PA_FLUSH hands over (else -1). reason: the last abort's PHR_.
struct PhotoSm { int phase, n, r, c, arm, settled, miss, flushRow, reason; };
// press: Photo key presses this frame. cam: flying and seeded. world: the level the capture began in. screen: the
// engine's menu or console. sameSize: the render size at capture start. frozen/hidden: as of the frame just drawn.
// calibrated: render-to-screen mapping measured. tileRead: tile (r, c) was captured. bufferFree: a strip is free.
struct PhotoIn { int press, cam, panel, world, screen, focus, sameSize, frozen, hidden, tileRead, bufferFree,
                 encFailed, encDone, calibrated; };

inline PhotoSm photo_begin(int n) { PhotoSm s; memset(&s, 0, sizeof s); s.n = n; s.flushRow = -1; return s; }

inline int photo_abort(PhotoSm* s, int why) {
  s->phase = PH_IDLE;
  s->reason = why;
  return PA_ABORT;
}

inline int photo_step(PhotoSm* s, const PhotoIn& in) {
  s->flushRow = -1;
  if (s->phase == PH_IDLE) {
    if (!in.press || !in.cam) return PA_NONE;
    s->phase = PH_ARMING;
    s->r = s->c = s->arm = s->settled = s->miss = 0;
    s->reason = PHR_NONE;
    return PA_START;
  }
  if (in.encFailed) return photo_abort(s, PHR_ENCODER);
  if (s->phase == PH_DRAIN) {
    if (!in.encDone) return PA_NONE;
    s->phase = PH_IDLE;
    return PA_FINISH;
  }
  if (in.press) return photo_abort(s, PHR_CANCELLED);
  if (!in.cam) return photo_abort(s, PHR_CAMERA);
  if (in.panel) return photo_abort(s, PHR_PANEL);
  if (!in.world) return photo_abort(s, PHR_LEVEL);
  if (in.screen) return photo_abort(s, PHR_SCREEN);
  if (!in.focus) return photo_abort(s, PHR_FOCUS);
  if (!in.sameSize) return photo_abort(s, PHR_SIZE);
  if (s->phase == PH_ARMING) {
    s->arm++;
    s->settled = (in.frozen && in.hidden) ? s->settled + 1 : 0;
    if (s->settled >= PHOTO_SETTLE_FRAMES && in.calibrated) {
      s->phase = PH_TILE;
      s->r = s->c = s->miss = 0;
      return PA_NONE;
    }
    return s->arm >= PHOTO_ARM_TIMEOUT ? photo_abort(s, PHR_NOT_FROZEN) : PA_NONE;
  }
  if (!in.frozen || !in.hidden) return photo_abort(s, PHR_THAWED);
  if (s->phase == PH_WAIT) {
    if (in.bufferFree) s->phase = PH_TILE;
    return PA_NONE;
  }
  // PH_TILE
  if (!in.tileRead) return ++s->miss >= PHOTO_TILE_TIMEOUT ? photo_abort(s, PHR_NO_TILE) : PA_NONE;
  s->miss = 0;
  if (++s->c < s->n) return PA_NONE;
  s->flushRow = s->r;
  s->c = 0;
  s->r++;
  s->phase = s->r >= s->n ? PH_DRAIN : PH_WAIT;
  return PA_FLUSH;
}

// The glue could not start (memory, folder, thread): an abort with its reason. Nothing to do when idle.
inline int photo_fail(PhotoSm* s, int why) { return s->phase == PH_IDLE ? PA_NONE : photo_abort(s, why); }

inline const char* photo_why_text(int why) {
  switch (why) {
    case PHR_CANCELLED: return "the Photo key again (cancelled)";
    case PHR_CAMERA: return "the camera ended";
    case PHR_PANEL: return "the panel opened";
    case PHR_LEVEL: return "the level changed";
    case PHR_SCREEN: return "the game's menu or console is up";
    case PHR_FOCUS: return "the game window lost focus";
    case PHR_SIZE: return "the screen size changed";
    case PHR_NOT_FROZEN: return "the world did not freeze or the overlays did not hide";
    case PHR_THAWED: return "the world woke or the HUD came back mid-capture";
    case PHR_NO_TILE: return "the camera push stopped drawing the tile";
    case PHR_ENCODER: return "the PNG encoder failed (see its line above)";
    case PHR_START: return "it could not start (see the line above)";
    case PHR_CALIBRATION: return "the calibration marks were not found 1:1 on screen (see the line above)";
    default: return "none";
  }
}

// --- The glue (photo.cpp), main thread only unless said ------------------------------------------------------
// Frame path, first thing after the render size is known: the render size, and the visible rect now and then.
void photo_capture(int W, int H);
// Frame path, after panel_frame: reads the screen once a tile has been held long enough, steps the capture, and
// draws its progress line only between rows (the capture reads the screen, so never while a tile is up).
void photo_frame(int W, int H);
// view.cpp's freecam push, after nocull_push_projection: on a tile frame narrows the fov and shears the pair in place.
void photo_push(float view[16], float inv[16]);
// A capture is under way (arming to drain): the world stays frozen and the HUD and overlays hidden.
bool photo_busy(void);
// Arming to the last tile: the camera holds still and its other keys do nothing.
bool photo_capturing(void);
// The visible size of the game window the frame path saw last (the Photo size readout), 0 x 0 before it is known.
void photo_screen_size(int* W, int* H);
// The quit path: an unfinished photo is abandoned and its partial file deleted.
void photo_restore_for_quit(void);
