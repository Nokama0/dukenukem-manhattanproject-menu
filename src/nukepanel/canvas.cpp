// AA's ui/canvas.cpp with its ImGui backend replaced by prim.cpp (prism3d's ortho pool). The device arithmetic, corner
// tessellation, glyph quads and UI scale are AA's; clipping is done on the CPU (canvas_clip.h) because the engine's
// end-frame hook has no scissor. The scaled atlas goes to the engine as files (render.md, "UI scale").
#include "canvas.h"
#include "canvas_clip.h"
#include "font.h"
#include "prim.h"
#include "engine.h"
#include "log.h"
#include "hook.h"
#include "nocull.h"
#include "zoom.h"      // fov_restore_for_quit
#include "wheelzoom.h" // wheelzoom_log_totals
#include "shot.h"      // shot_restore_for_quit
#include "photo.h"     // photo_restore_for_quit
#include "display.h"
#include "firstperson.h"   // firstperson_restore_for_quit
#include "fontgen.h"   // the scaled atlas
#include "atlasfile.h" // its PNG and .shd
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------ scale, transform
// The applied UI scale (canvas.h). Only ever a scale whose atlas is in hand: canvas_set_scale falls back to 100
// when the rasterised atlas could not be built, so nothing is drawn as stretched baked glyphs.
static int s_scale = UI_SCALE_DEFAULT;

static int    s_txDx = 0, s_txDy = 0, s_txCx = 0, s_txCy = 0;
static double s_txScale = 1.0;
static bool   s_txIdentity = true;

void canvas_set_transform(int dx, int dy, double scale, int cx, int cy) {
  s_txDx = dx; s_txDy = dy; s_txCx = cx; s_txCy = cy;
  s_txScale = scale;
  s_txIdentity = (dx == 0 && dy == 0 && scale == 1.0);
}

static int tx_round(double v) {
  v = v < 0.0 ? v - 0.5 : v + 0.5;
  if (!(v > -2147483648.0)) return -2147483647 - 1;
  if (v > 2147483647.0) return 2147483647;
  return (int)v;
}
// AA's composition: a device coordinate (already through the UI scale) moved and scaled by the panel transform,
// whose displacement and centre are logical and take the UI scale too.
static int tx_dev_x(int d) {
  if (s_txIdentity) return d;
  const double c = (double)ui_scale_px(s_txCx, s_scale);
  return tx_round(((double)d - c) * s_txScale + c + (double)ui_scale_px(s_txDx, s_scale));
}
static int tx_dev_y(int d) {
  if (s_txIdentity) return d;
  const double c = (double)ui_scale_px(s_txCy, s_scale);
  return tx_round(((double)d - c) * s_txScale + c + (double)ui_scale_px(s_txDy, s_scale));
}
static int tx_dev_len(int d) {
  if (s_txIdentity) return d;
  return tx_round((double)d * s_txScale);
}
// Logical -> device: the UI scale, then the transform. At 100% the scale is skipped, so the ESP's coordinates
// pass through untouched.
static int scale_px(int v) { return s_scale == UI_SCALE_DEFAULT ? v : ui_scale_px(v, s_scale); }
static int dev_x(int x)   { return tx_dev_x(scale_px(x)); }
static int dev_y(int y)   { return tx_dev_y(scale_px(y)); }
static int dev_len(int v) { return tx_dev_len(scale_px(v)); }

// Shaders: font.shd samples the baked atlas (tools/gen_atlas.py), whose opaque white block at (508..511, 252..255) serves every
// flat fill, so one shader keeps the ortho pool in submission order; white.shd stands in if the atlas does not load
// (text skipped). mm_get_resource hands us one reference (quit-path.md), kept until canvas_release_for_quit.
static c_item_t* s_font;
static c_item_t* s_white;
static bool s_loaded;
static bool s_live;
static bool s_released;
static float s_flatU, s_flatV;           // the current atlas's white texel (apply_atlas)
static float s_bakedFlatU, s_bakedFlatV;
static const float kAtlasW = 512.f, kAtlasH = 256.f;

static void load_shaders(void) {
  if (s_loaded || s_released) return;
  s_loaded = true;
  if (!g_eng.mm_get_resource) { np_log("canvas: mm_get_resource unresolved; nothing will draw"); return; }
  // Unverified: that a missing asset returns NULL. A .shd that loads while its PNG is
  // missing likely gets the notfound texture, and fills would sample garbage silently.
  s_font = g_eng.mm_get_resource("nukepanel/font.shd");
  if (!s_font) s_white = g_eng.mm_get_resource("nukepanel/white.shd");
  np_log("canvas: font.shd = %p, white.shd = %p (atlas %dx%d)", (void*)s_font, (void*)s_white,
         FONT_ATLAS_W, FONT_ATLAS_H);
  if (s_font) { s_bakedFlatU = 510.f / kAtlasW; s_bakedFlatV = 254.f / kAtlasH; }
  else        { s_bakedFlatU = 0.5f; s_bakedFlatV = 0.5f; }
  s_flatU = s_bakedFlatU; s_flatV = s_bakedFlatV;
}

// The scaled atlas: at any scale but 100 the three faces are rasterised at that size (fontgen.cpp) and reach the engine as a PNG and a
// .shd written through the VFS (nukepanel/font_s<pct>.*), loaded with mm_get_resource: the engine gives the canvas no
// texture upload. One slot, replaced (old shader's reference dropped) only when another scale is applied.
struct RuntimeAtlas {
  FontAtlas run;        // glyph metrics in device pixels; pixels freed once on disk
  c_item_t* shader;     // our one mm_get_resource reference, 0 for none
  int pct;              // the scale `shader` was built for, 0 for none
  int failedPct;        // the last scale refused (build or load), 0 for none: never retried every frame
  float flatU, flatV;
};
static RuntimeAtlas s_rt;

// Drops our reference when the count still includes it (canvas_release_mask's rule).
static void drop_reference(c_item_t* item, const char* what) {
  if (!item) return;
  const unsigned char* rc = (const unsigned char*)item + 0x44;
  int refs = 0;
  const bool readable = guarded_readable(rc, 4);
  if (readable) memcpy(&refs, rc, sizeof(refs));
  const void* const held[2] = { item, 0 };
  const bool rd[2] = { readable, false };
  const int rf[2] = { refs, 0 };
  if (!canvas_release_mask(held, rd, rf, false) || !g_eng.ci_dec_refs) {
    np_log("canvas: %s %p left held (readable %d, refs %d)", what, (void*)item, readable ? 1 : 0, refs);
    return;
  }
  np_log("canvas: %s %p released, %d reference(s) left", what, (void*)item, g_eng.ci_dec_refs(item));
}

// Writes `n` bytes to `name` through the engine's own VFS, not CreateFileA: it finds names only in the tree its mounts
// indexed at launch, so a file put on disk later is invisible to it and to mm_get_resource. VFS_O_CREAT opens a new
// name on the rw fallback device and registers it, as duke_base's savegame writer does (open 1, then 0x101).
static bool vfs_write_file(const char* name, const void* data, size_t n, int pct) {
  if (!g_eng.vfs_open || !g_eng.vfs_write || !g_eng.vfs_close) {
    np_log("canvas: UI scale %d%%: vfs_open/vfs_write/vfs_close unresolved; keeping 100%%", pct);
    return false;
  }
  void* h = g_eng.vfs_open(name, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, VFS_MODE_RW);
  if (!h) {
    np_log("canvas: UI scale %d%%: vfs_open(%s, write|create|trunc) refused; keeping 100%%", pct, name);
    return false;
  }
  const char* dev = g_eng.vfs_get_device_name ? g_eng.vfs_get_device_name(h) : "?";
  size_t at = 0;
  while (at < n) {
    const unsigned chunk = (unsigned)(n - at < 65536u ? n - at : 65536u);
    const int put = g_eng.vfs_write(h, (const unsigned char*)data + at, chunk);
    if (put <= 0) break;
    at += (size_t)put;
  }
  g_eng.vfs_close(h);
  if (at != n) {
    np_log("canvas: UI scale %d%%: wrote %u of %u bytes of %s (device \"%s\"); keeping 100%%", pct, (unsigned)at,
           (unsigned)n, name, dev ? dev : "?");
    return false;
  }
  return true;
}

// True when the VFS serves exactly `want` under `name`; when not, one line saying what it did serve.
static bool vfs_matches(const char* name, const unsigned char* want, size_t n, int pct) {
  if (!g_eng.vfs_open || !g_eng.vfs_read || !g_eng.vfs_close) return false;
  void* h = g_eng.vfs_open(name, VFS_O_RDONLY, VFS_MODE_RW);
  if (!h) {
    np_log("canvas: UI scale %d%%: read-back: vfs_open(%s) found nothing; keeping 100%%", pct, name);
    return false;
  }
  const char* dev = g_eng.vfs_get_device_name ? g_eng.vfs_get_device_name(h) : "?";
  AtlasReadBack rb;
  atlas_readback_begin(&rb);
  unsigned char buf[4096];
  for (;;) {
    const int got = g_eng.vfs_read(h, buf, sizeof buf);
    if (got <= 0) break;
    atlas_readback_step(&rb, want, n, buf, (size_t)got);
    if (rb.firstDiff >= 0) break;
  }
  g_eng.vfs_close(h);
  if (atlas_readback_ok(&rb, n)) return true;
  np_log("canvas: UI scale %d%%: read-back of %s (device \"%s\"): %u of %u bytes read, first difference at %lld; "
         "keeping 100%%", pct, name, dev ? dev : "?", (unsigned)rb.at, (unsigned)n, rb.firstDiff);
  return false;
}

// The built atlas into the engine through its VFS; the shader item, or 0 with one warning logged.
static c_item_t* runtime_upload(const FontAtlas& a, int pct) {
  const size_t size = atlas_png_size(a.w, a.h);
  unsigned char* png = size ? (unsigned char*)malloc(size) : 0;
  if (!png || atlas_png_encode(a.pixels, a.w, a.h, png, size) != size) {
    free(png);
    np_log("canvas: UI scale %d%%: no memory to encode a %dx%d atlas; keeping 100%%", pct, a.w, a.h);
    return 0;
  }
  char pngName[32], pngRes[64], shdRes[64];
  sprintf_s(pngName, "font_s%d.png", pct);
  sprintf_s(pngRes, "nukepanel/%s", pngName);
  sprintf_s(shdRes, "nukepanel/font_s%d.shd", pct);
  char shd[1024];
  const int shdLen = atlas_shd_text(pngName, shd, sizeof shd);
  const bool ok = shdLen > 0 && vfs_write_file(pngRes, png, size, pct) &&
                  vfs_write_file(shdRes, shd, (size_t)shdLen, pct) && vfs_matches(pngRes, png, size, pct) &&
                  vfs_matches(shdRes, (const unsigned char*)shd, (size_t)shdLen, pct);
  free(png);
  if (!ok) return 0;
  c_item_t* sh = g_eng.mm_get_resource ? g_eng.mm_get_resource(shdRes) : 0;
  if (!sh) np_log("canvas: UI scale %d%%: mm_get_resource(%s) failed; keeping 100%%", pct, shdRes);
  return sh;
}

// Builds and loads `pct`'s atlas, all or nothing; on success the old one's reference is dropped.
static bool runtime_build(int pct) {
  if (s_released || !s_font) { s_rt.failedPct = pct; return false; }
  FontAtlas built;
  memset(&built, 0, sizeof(built));
  if (!fontgen_build(pct, &built)) { s_rt.failedPct = pct; return false; }   // fontgen logged why
  c_item_t* sh = runtime_upload(built, pct);
  if (!sh) { fontgen_free(&built); s_rt.failedPct = pct; return false; }
  drop_reference(s_rt.shader, "scaled atlas");
  fontgen_free(&s_rt.run);
  s_rt.run = built;
  free(s_rt.run.pixels);   // on disk and in the engine now; only the metrics are read from here on
  s_rt.run.pixels = 0;
  s_rt.shader = sh;
  s_rt.pct = pct;
  atlas_white_uv(built.whiteX, built.whiteY, built.w, built.h, &s_rt.flatU, &s_rt.flatV);
  np_log("canvas: UI scale %d%%: atlas %dx%d loaded as nukepanel/font_s%d.shd (%p)", pct, built.w, built.h, pct,
         (void*)sh);
  return true;
}

static bool runtime_active(void) { return s_scale != UI_SCALE_DEFAULT && s_rt.shader && s_rt.pct == s_scale; }
static c_item_t* current_shader(void) { return runtime_active() ? s_rt.shader : (s_font ? s_font : s_white); }

// The current scale's white texel and, mid-frame, its shader (prim flushes on a change, keeping order).
static void apply_atlas(void) {
  if (runtime_active()) { s_flatU = s_rt.flatU; s_flatV = s_rt.flatV; }
  else                  { s_flatU = s_bakedFlatU; s_flatV = s_bakedFlatV; }
  if (s_live) prim_shader(current_shader());
}

void canvas_set_scale(int pct) {
  const int want = ui_scale_clamped(pct);
  switch (canvas_atlas_step(want, s_rt.pct, s_rt.failedPct)) {
    case CANVAS_ATLAS_HELD:  s_scale = want; break;
    case CANVAS_ATLAS_BUILD: s_scale = runtime_build(want) ? want : UI_SCALE_DEFAULT; break;
    default:                 s_scale = UI_SCALE_DEFAULT; break;   // 100 itself, or a refused scale
  }
  apply_atlas();
}
int canvas_scale() { return s_scale; }

// A held reference keeps loaders' __dl_guard process locked, so the quitting main loop never empties its process list
// (docs/re/exit-hang.md, quit-path.md). Only the shader is ours to drop: its free (loaders+0x2c70, ops +4) calls
// ci_dec_refs on each texture it holds, so font.png follows it.
void canvas_release_for_quit(void) {
  c_item_t* held[2] = { s_font, s_white };
  bool readable[2] = { false, false };
  int refs[2] = { 0, 0 };
  for (int i = 0; i < 2; i++) {
    if (!held[i]) continue;
    // The refcount is +0x44 of the item (quit-path.md section 3).
    const unsigned char* rc = (const unsigned char*)held[i] + 0x44;
    readable[i] = guarded_readable(rc, 4);
    if (readable[i]) memcpy(&refs[i], rc, sizeof(refs[i]));
  }
  const void* const heldv[2] = { held[0], held[1] };
  const bool repeat = s_released;
  const unsigned mask = canvas_release_mask(heldv, readable, refs, repeat);
  s_released = true;
  s_live = false;
  s_font = s_white = 0;
  for (int i = 0; i < 2; i++) {
    if (!held[i] || (mask & (1u << i))) continue;
    if (!readable[i]) np_log("quit: item %p is not readable; left held", (void*)held[i]);
    else np_log("quit: item %p holds %d reference(s), not ours to drop; left held", (void*)held[i], refs[i]);
  }
  if (mask && !g_eng.ci_dec_refs) {
    np_log("quit: ci_dec_refs unresolved; %d reference(s) stay held", (mask & 1) + (mask >> 1));
    s_rt.shader = 0;
    s_rt.pct = 0;
    return;
  }
  int released = 0;
  for (int i = 0; i < 2; i++) {
    if (!(mask & (1u << i))) continue;
    const int left = g_eng.ci_dec_refs(held[i]);
    np_log("quit: ci_dec_refs(%p) -> %d reference(s) left", (void*)held[i], left);
    released++;
  }
  np_log("quit: released %d cache reference(s)%s", released, repeat ? " (repeat call)" : "");
  // The scaled atlas's shader is held the same way and would block the quit the same way (exit-hang.md).
  c_item_t* rt = s_rt.shader;
  s_rt.shader = 0;
  s_rt.pct = 0;
  if (!repeat) drop_reference(rt, "quit: scaled atlas");
}

// process_delete_all's own prologue opens with `push esi; call rel32`, which the
// hook copies unrelocated, so the detour sits on that call's target (0x405f50, the
// config.cfg write), reached only through it: the same quit path, before any process is signalled.
static const unsigned char kDeleteAllFp[] = {
  0x56, 0xe8, 0x8a, 0xff, 0xff, 0xff, 0x8b, 0x35, 0x6c, 0x64, 0x47, 0x00 };
// 0x405f50: sub esp,0xc (3); mov ecx,[0x43cdc0] (6); mov eax,[0x43cdbc]. No relative operand in 9 bytes.
static const unsigned char kConfigWriteFp[] = {
  0x83, 0xec, 0x0c, 0x8b, 0x0d, 0xc0, 0xcd, 0x43, 0x00, 0xa1, 0xbc, 0xcd, 0x43, 0x00 };

// cdecl (), no arguments, plain RET (ADD ESP,0x14 balances its own pushes); only caller 0x405fc1.
static void (*s_config_write_tramp)(void);
static int s_quitInstalled;

static void config_write_detour(void) {
  canvas_release_for_quit();
  // Precaution, not a fix for an observed bug: FUN_004067e0 (config writer) only
  // saves cvars with byte +0x68 set, and g_cam_far's is 0 -- it is never archived
  // today. Keep the restore defensive in case that ever changes.
  nocull_restore_for_quit();
  wheelzoom_log_totals();
  shot_restore_for_quit();      // g_hud is archived (config.cfg): the player's own value, not our 0
  fov_restore_for_quit();       // not archived today; the stock string goes back all the same
  display_restore_for_quit();   // r_mode is archived (config.cfg): the player's own value, not ours
  firstperson_restore_for_quit();   // g_fps is archived (config.cfg): never left at our 1
  photo_restore_for_quit();     // an unfinished photo's encoder is stopped, bounded
  s_config_write_tramp();
}

int canvas_quit_install(void) {
  const unsigned char* pda = (const unsigned char*)g_eng.process_delete_all;
  if (!pda) { np_log("quit: process_delete_all unresolved; no detour"); return 0; }
  if (!guarded_readable(pda, sizeof(kDeleteAllFp)) || memcmp(pda, kDeleteAllFp, sizeof(kDeleteAllFp)) != 0) {
    np_log("quit: process_delete_all at %p does not match; no detour", (const void*)pda);
    return 0;
  }
  const unsigned char* target = pda + 6 + *(const int*)(pda + 2);
  s_quitInstalled = hook_install_at((void*)target, kConfigWriteFp, sizeof(kConfigWriteFp), 9,
                                    (void*)config_write_detour, (void**)&s_config_write_tramp);
  return s_quitInstalled;
}

int canvas_quit_installed(void) { return s_quitInstalled; }

// ------------------------------------------------------------------ clipping
static const int kClipMax = 16;
static ClipRect s_clip[kClipMax];
static int s_clipDepth;
static unsigned s_clipOverflow;   // pushes past kClipMax, cumulative

// Past the stack's depth the deepest stored clip stands in, so a runaway
// nesting still clips rather than drawing over the game.
static const ClipRect* clip_top(void) {
  if (s_clipDepth <= 0) return 0;
  return &s_clip[(s_clipDepth <= kClipMax ? s_clipDepth : kClipMax) - 1];
}

void canvas_push_clip(int x, int y, int w, int h) {
  if (s_clipDepth >= kClipMax) {
    if (s_clipOverflow++ == 0) np_log("canvas: clip stack deeper than %d", kClipMax);
    s_clipDepth++;
    return;
  }
  const int dx = dev_x(x), dy = dev_y(y);
  ClipRect c = { (float)dx, (float)dy, (float)(dx + dev_len(w)), (float)(dy + dev_len(h)) };
  s_clip[s_clipDepth++] = c;
}

void canvas_pop_clip() {
  if (s_clipDepth > 0) s_clipDepth--;
}

// ------------------------------------------------------------------ emitters
static unsigned lerp_colour(unsigned a, unsigned b, float t) {
  unsigned out = 0;
  for (int sh = 0; sh < 32; sh += 8) {
    const float ca = (float)((a >> sh) & 0xFF), cb = (float)((b >> sh) & 0xFF);
    int v = (int)(ca + (cb - ca) * t + 0.5f);
    v = v < 0 ? 0 : v > 255 ? 255 : v;
    out |= (unsigned)v << sh;
  }
  return out;
}

// An axis-aligned rect, textured, colour ramping top to bottom.
static void emit_rect(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                      unsigned top, unsigned bottom) {
  if (!s_live || !(x1 > x0) || !(y1 > y0)) return;
  const float oy0 = y0, oh = y1 - y0;
  const ClipRect* c = clip_top();
  if (c && !clip_rect_uv(c, &x0, &y0, &x1, &y1, &u0, &v0, &u1, &v1)) return;
  const unsigned ct = top == bottom ? top : lerp_colour(top, bottom, (y0 - oy0) / oh);
  const unsigned cb = top == bottom ? top : lerp_colour(top, bottom, (y1 - oy0) / oh);
  const PrimVert a = { x0, y0, u0, v0, ct }, b = { x1, y0, u1, v0, ct };
  const PrimVert d = { x1, y1, u1, v1, cb }, e = { x0, y1, u0, v1, cb };
  prim_tri(&a, &b, &d);
  prim_tri(&a, &d, &e);
}

// A flat convex polygon of 3 or 4 points, fanned from the first.
static void emit_poly(const float* px, const float* py, int n, unsigned rgba) {
  if (!s_live) return;
  ClipVert in[4], out[CLIP_POLY_MAX];
  for (int i = 0; i < n; ++i) { in[i].x = px[i]; in[i].y = py[i]; in[i].u = s_flatU; in[i].v = s_flatV; }
  const ClipVert* v = in;
  const ClipRect* c = clip_top();
  if (c) { n = clip_poly(c, in, n, out); v = out; }
  for (int i = 1; i + 1 < n; ++i) {
    const PrimVert a = { v[0].x, v[0].y, s_flatU, s_flatV, rgba };
    const PrimVert b = { v[i].x, v[i].y, s_flatU, s_flatV, rgba };
    const PrimVert d = { v[i + 1].x, v[i + 1].y, s_flatU, s_flatV, rgba };
    prim_tri(&a, &b, &d);
  }
}

static void flat_quad(float x0, float y0, float x1, float y1,
                      float x2, float y2, float x3, float y3, unsigned rgba) {
  const float px[4] = { x0, x1, x2, x3 }, py[4] = { y0, y1, y2, y3 };
  emit_poly(px, py, 4, rgba);
}

static void fill_device(int x, int y, int w, int h, unsigned rgba) {
  emit_rect((float)x, (float)y, (float)(x + w), (float)(y + h), s_flatU, s_flatV, s_flatU, s_flatV, rgba, rgba);
}

// ------------------------------------------------------------------ frame
// Every frame starts at 100% as well as at the identity transform: menu_draw restores 100 on its way out, and this
// keeps a scale left standing by any other path off the ESP.
void canvas_begin(int w, int h) {
  canvas_set_transform(0, 0, 1.0, 0, 0);
  s_clipDepth = 0;
  load_shaders();
  s_live = false;
  s_scale = UI_SCALE_DEFAULT;
  apply_atlas();
  if (!s_font && !s_white) return;
  if (!prim_begin(w, h)) return;
  prim_shader(current_shader());
  s_live = true;
}

void canvas_end() {
  if (s_clipDepth != 0) {
    static int said = 0;
    if (said < 3) { said++; np_log("canvas: %d clip(s) left pushed at frame end", s_clipDepth); }
    s_clipDepth = 0;
  }
  if (s_live) prim_end();
  s_live = false;
}

// ------------------------------------------------------------------ primitives
void canvas_rect(int x, int y, int w, int h, unsigned rgba) {
  fill_device(dev_x(x), dev_y(y), dev_len(w), dev_len(h), rgba);
}

void canvas_line(int x0, int y0, int x1, int y1, unsigned rgba) {
  const int dx0 = dev_x(x0), dy0 = dev_y(y0);
  const int dx1 = dev_x(x1), dy1 = dev_y(y1);
  // A quad half a pixel either side of the segment between pixel centres:
  // exactly GL's one-pixel line when axis aligned (AA canvas.cpp).
  const float ax = (float)dx0 + 0.5f, ay = (float)dy0 + 0.5f;
  const float bx = (float)dx1 + 0.5f, by = (float)dy1 + 0.5f;
  float nx = 0.5f, ny = 0.0f;
  const float ex = bx - ax, ey = by - ay;
  const float len = (float)sqrt((double)(ex * ex + ey * ey));
  if (len > 0.0f) { nx = -ey / len * 0.5f; ny = ex / len * 0.5f; }
  // A zero-length segment would be a zero-width quad; make it one pixel.
  if (len == 0.0f) { fill_device(dx0, dy0, 1, 1, rgba); return; }
  flat_quad(ax - nx, ay - ny, bx - nx, by - ny, bx + nx, by + ny, ax + nx, ay + ny, rgba);
}

void canvas_tri(int x0, int y0, int x1, int y1, int x2, int y2, unsigned rgba) {
  float px[3] = { (float)dev_x(x0), (float)dev_x(x1), (float)dev_x(x2) };
  float py[3] = { (float)dev_y(y0), (float)dev_y(y1), (float)dev_y(y2) };
  // Positive area with y down, as TL -> TR -> BR: every other canvas triangle's winding.
  const float area = (px[1] - px[0]) * (py[2] - py[0]) - (py[1] - py[0]) * (px[2] - px[0]);
  if (area == 0.f) return;
  if (area < 0.f) {
    const float tx = px[1], ty = py[1];
    px[1] = px[2]; py[1] = py[2]; px[2] = tx; py[2] = ty;
  }
  emit_poly(px, py, 3, rgba);
}

void canvas_bevel(int x, int y, int w, int h, unsigned fill, unsigned hi, unsigned lo) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  int e = dev_len(1);
  if (e < 1) e = 1;
  fill_device(dx,          dy,          dw, dh,         fill);
  fill_device(dx,          dy,          dw, e,          hi);  // top, full width
  fill_device(dx,          dy + dh - e, dw, e,          lo);  // bottom, full width
  fill_device(dx,          dy + e,      e,  dh - 2 * e, hi);  // left, between top and bottom
  fill_device(dx + dw - e, dy + e,      e,  dh - 2 * e, lo);  // right, between top and bottom
}

void canvas_hazard(int x, int y, int w, int h, unsigned stripe, unsigned ground, int period) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  const int dp = dev_len(period);
  if (!canvas_hazard_params_ok(dw, dh, dp)) return;
  if (!s_live) return;
  fill_device(dx, dy, dw, dh, ground);
  // 45-degree bars clipped to the strip, pushed without intersecting as AA's
  // PushClipRect(..., false) was. Device rect, so it bypasses the logical push.
  if (s_clipDepth >= kClipMax) return;
  const ClipRect strip = { (float)dx, (float)dy, (float)(dx + dw), (float)(dy + dh) };
  s_clip[s_clipDepth++] = strip;
  for (int i = -dh; i < dw + dh; i += dp * 2) {
    // TL, TR, BR, BL: the engine quad's winding (positive area, y down).
    flat_quad((float)(dx + i + dh),      (float)dy,
              (float)(dx + i + dp + dh), (float)dy,
              (float)(dx + i + dp),      (float)(dy + dh),
              (float)(dx + i),           (float)(dy + dh), stripe);
  }
  s_clipDepth--;
}

// AA's segment rule: grows with radius, floor 3, ceiling 12.
static int round_rect_segments(int r) {
  const int s = 3 + (r > 0 ? r / 2 : 0);
  return s < 3 ? 3 : s > 12 ? 12 : s;
}

static const int ROUND_FAN_MAX = 4 * 12;
static int round_fan_points(int dx, int dy, int dw, int dh, int dr, int seg, float* px, float* py) {
  if (seg < 3) seg = 3;
  if (seg > 12) seg = 12;
  const double pi = 3.14159265358979323846;
  const float cx[4] = { (float)(dx + dr), (float)(dx + dw - dr), (float)(dx + dw - dr), (float)(dx + dr) };
  const float cy[4] = { (float)(dy + dr), (float)(dy + dr), (float)(dy + dh - dr), (float)(dy + dh - dr) };
  int n = 0;
  for (int c = 0; c < 4; c++) {
    const double a0 = (180.0 + 90.0 * c) * pi / 180.0;
    for (int i = 0; i < seg; i++) {
      const double a = a0 + (pi / 2.0) * (double)i / (double)(seg - 1);
      px[n] = cx[c] + (float)(dr * cos(a));
      py[n] = cy[c] + (float)(dr * sin(a));
      n++;
    }
  }
  return n;
}

static void round_rect_device(int dx, int dy, int dw, int dh, int dr, int seg, unsigned rgba) {
  float px[ROUND_FAN_MAX], py[ROUND_FAN_MAX];
  const int n = round_fan_points(dx, dy, dw, dh, dr, seg, px, py);
  const float cx = (float)dx + (float)dw * 0.5f, cy = (float)dy + (float)dh * 0.5f;
  for (int i = 0; i < n; i++) {
    const int j = (i + 1 < n) ? i + 1 : 0;
    const float tx[3] = { cx, px[i], px[j] }, ty[3] = { cy, py[i], py[j] };
    emit_poly(tx, ty, 3, rgba);
  }
}

static int corner_radius_device(int dw, int dh, int r) {
  int dr = dev_len(r > 0 ? r : 0);
  const int half = (dw < dh ? dw : dh) / 2;
  if (dr > half) dr = half;
  return dr < 0 ? 0 : dr;
}

void canvas_round_rect(int x, int y, int w, int h, int r, unsigned rgba) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  const int dr = corner_radius_device(dw, dh, r);
  round_rect_device(dx, dy, dw, dh, dr, round_rect_segments(dr), rgba);
}

void canvas_round_rect_outline(int x, int y, int w, int h, int r, int thickness, unsigned rgba) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  int t = dev_len(thickness > 0 ? thickness : 0);
  if (t < 1) t = 1;
  const int dr = corner_radius_device(dw, dh, r);
  const int seg = round_rect_segments(dr);
  if (dw <= 2 * t || dh <= 2 * t) { round_rect_device(dx, dy, dw, dh, dr, seg, rgba); return; }
  float ox[ROUND_FAN_MAX], oy[ROUND_FAN_MAX], ix[ROUND_FAN_MAX], iy[ROUND_FAN_MAX];
  const int n = round_fan_points(dx, dy, dw, dh, dr, seg, ox, oy);
  const int ir = dr - t < 0 ? 0 : dr - t;
  round_fan_points(dx + t, dy + t, dw - 2 * t, dh - 2 * t, ir, seg, ix, iy);
  // Each rung pair as two triangles, o0 o1 i0 and i0 o1 i1: the engine quad's
  // winding, like every other canvas triangle, so `cull none` is not load-bearing.
  for (int i = 0; i < n; i++) {
    const int j = (i + 1 < n) ? i + 1 : 0;
    const float ax[3] = { ox[i], ox[j], ix[i] }, ay[3] = { oy[i], oy[j], iy[i] };
    const float bx[3] = { ix[i], ox[j], ix[j] }, by[3] = { iy[i], oy[j], iy[j] };
    emit_poly(ax, ay, 3, rgba);
    emit_poly(bx, by, 3, rgba);
  }
}

void canvas_gradient_rect(int x, int y, int w, int h, unsigned top, unsigned bottom) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  emit_rect((float)dx, (float)dy, (float)(dx + dw), (float)(dy + dh), s_flatU, s_flatV, s_flatU, s_flatV,
            top, bottom);
}

// Additive approximated with alpha, as AA does (its canvas.cpp gives the
// measured error); the ortho pool has one blend state per shader.
void canvas_glow_rect(int x, int y, int w, int h, unsigned rgba) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  fill_device(dx, dy, dw, dh, rgba);
}

// The Glass backdrop is stubbed exactly as AA's is: the tint alone.
bool canvas_backdrop_capture(int, int, int, int) {
  static int said = 0;
  if (!said) { said = 1; np_log("canvas: the Glass backdrop capture is not implemented; the style degrades to its tint"); }
  return false;
}

void canvas_backdrop_draw(int x, int y, int w, int h, int r, unsigned tint, double, double, int, int) {
  canvas_round_rect(x, y, w, h, r, tint);
}

void canvas_backdrop_release() {}

bool canvas_white_texel(float* u, float* v) {
  load_shaders();
  if (u) *u = s_flatU;
  if (v) *v = s_flatV;
  return s_font != 0;
}

// On the scaled atlas (AA's canvas_text) the metrics are DEVICE pixels: the pen starts at zero and every advance is a
// device advance, so nothing rounds per character and only the panel transform is applied. On the baked atlas the pen
// stays logical. Width and glyphs always come from the same atlas.
int canvas_text(int x, int y, const char* s, unsigned rgba, int font) {
  if (!s || font < 0 || font >= FONT_COUNT) return x;
  if (!s_live || !s_font) return x;
  const bool device = runtime_active();
  const Glyph* glyphs = device ? s_rt.run.glyphs[font] : font_glyphs[font];
  const float aw = device ? (float)s_rt.run.w : kAtlasW, ah = device ? (float)s_rt.run.h : kAtlasH;
  const int ox = device ? ui_scale_px(x, s_scale) : 0;
  const int oy = device ? ui_scale_px(y, s_scale) : 0;
  int pen = device ? 0 : x;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if (*p < 0x20 || *p > 0x7F) continue;
    const Glyph& g = glyphs[*p - 0x20];
    if (g.w > 0 && g.h > 0) {
      // Integer device pixels and texel-edge UVs: with nearest filtering each
      // glyph texel lands on exactly one screen pixel.
      const float gx = device ? (float)tx_dev_x(ox + pen + g.bx) : (float)dev_x(pen + g.bx);
      const float gy = device ? (float)tx_dev_y(oy + g.by)       : (float)dev_y(y + g.by);
      const float gw = device ? (float)tx_dev_len(g.w)           : (float)dev_len(g.w);
      const float gh = device ? (float)tx_dev_len(g.h)           : (float)dev_len(g.h);
      emit_rect(gx, gy, gx + gw, gy + gh,
                (float)g.x / aw, (float)g.y / ah,
                (float)(g.x + g.w) / aw, (float)(g.y + g.h) / ah, rgba, rgba);
    }
    pen += g.adv;
  }
  // Logical, like the x it was seeded from.
  return device ? x + ui_unscale_px(pen, s_scale) : pen;
}

// LOGICAL (canvas.h): the scaled atlas's device advances are unscaled once, for the whole string.
int canvas_text_width(const char* s, int font) {
  if (!s || font < 0 || font >= FONT_COUNT) return 0;
  const bool device = runtime_active();
  const Glyph* glyphs = device ? s_rt.run.glyphs[font] : font_glyphs[font];
  int w = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if (*p < 0x20 || *p > 0x7F) continue;
    w += glyphs[*p - 0x20].adv;
  }
  return device ? ui_unscale_px(w, s_scale) : w;
}
