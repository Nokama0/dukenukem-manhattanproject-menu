// See prim.h. Descriptor values are the engine's own text-quad caller's
// (prism3d FUN_00417e20 @0x418b..), with topology 0 = GL_TRIANGLES (gl!0x10003b40).
#include "prim.h"
#include "engine.h"
#include "color.h"
#include "ndc.h"
#include "log.h"
#include <string.h>

// 1536 vertices = 512 triangles, a 36 KB packet (92 + 24 bytes a vertex).
static const int kMaxVerts = 1536;
// The render cache starts at 0x40000 bytes and drops a packet that overflows it
// (rend_cache_alloc 0x41ccb0); flushing past this keeps well clear of that.
static const unsigned kCacheBudget = 160 * 1024;

static float s_pos[kMaxVerts * 3];
static float s_uv[kMaxVerts * 2];
static unsigned s_col[kMaxVerts];
static int s_n;
static int s_W, s_H;
static bool s_live;
static c_item_t* s_shader;
static bool s_queued;          // a packet is in the pool since the last flush
static unsigned s_cache_bytes;
static void* s_pool;
static bool s_pool_tried;
static PrimStats s_stats;

PrimStats prim_stats(void) { return s_stats; }

static void pool_flush(void) {
  g_eng.rend_cache_flush();
  s_queued = false;
  s_cache_bytes = 0;
}

static void submit(void) {
  if (s_n == 0) return;
  const unsigned bytes = 92u + 24u * (unsigned)s_n;
  if (s_cache_bytes + bytes > kCacheBudget) { pool_flush(); s_stats.budget_flushes++; }
  const float minus_one = -1.f, radius = 2.f, zero = 0.f;
  unsigned d[0x14];
  memset(d, 0, sizeof(d));
  d[0] = 0x45;                          // positions, UV set 0, colour set 0
  d[1] = 1;                             // copy the bounding sphere below
  d[2] = 0;                             // proto+0x38 flags
  memcpy(&d[3], &minus_one, 4);         // proto+0x44, as the engine's caller
  d[4] = 0;                             // proto+0x40 topology: triangle list
  d[5] = (unsigned)(size_t)s_shader;
  d[6] = (unsigned)s_n;
  d[7] = 0;                             // no indices
  d[8] = (unsigned)(size_t)s_pos;
  d[9] = (unsigned)(size_t)s_col;
  d[0xb] = (unsigned)(size_t)s_uv;
  memcpy(&d[0xf], &zero, 4); memcpy(&d[0x10], &zero, 4); memcpy(&d[0x11], &zero, 4);
  memcpy(&d[0x12], &radius, 4);         // covers the whole -1..1 ortho screen
  g_eng.rend_cache_data(d, s_pool);     // copies the arrays into the render cache
  s_cache_bytes += bytes;
  s_queued = true;
  s_stats.packets++;
  s_stats.verts += (unsigned)s_n;
  s_n = 0;
}

bool prim_begin(int W, int H) {
  s_live = false;
  if (!g_eng.rend_cache_data || !g_eng.rend_cache_flush || !g_eng.rend_props_flush ||
      !g_eng.rend_set_projection_type || !g_eng.rend_push || !g_eng.rend_identity || !g_eng.rend_pop ||
      W <= 0 || H <= 0) return false;
  if (!s_pool_tried) {
    s_pool_tried = true;
    s_pool = g_eng.rend_get_pool ? g_eng.rend_get_pool("ortho") : 0;
    // NULL falls back to the shader's own pool, which `ortho` in the .shd selects.
    np_log("prim: rend_get_pool(ortho) = %p", s_pool);
  }
  s_W = W; s_H = H;
  s_n = 0;
  s_shader = 0;
  s_queued = false;
  s_cache_bytes = 0;   // subsys flushed (and so reset) the cache just before rend_end_frame
  g_eng.rend_set_projection_type(2);
  g_eng.rend_push();
  g_eng.rend_identity();
  s_live = true;
  s_stats.frames++;
  return true;
}

void prim_end(void) {
  if (!s_live) return;
  submit();
  g_eng.rend_cache_flush();
  g_eng.rend_props_flush();
  g_eng.rend_pop();
  g_eng.rend_set_projection_type(1);
  s_live = false;
}

void prim_shader(c_item_t* shader) {
  if (!s_live || shader == s_shader) return;
  submit();
  if (s_queued) { pool_flush(); s_stats.shader_flushes++; }
  s_shader = shader;
}

static void put(const PrimVert* p) {
  float* pos = s_pos + s_n * 3;
  pos[0] = ndc_x(p->x, s_W);
  pos[1] = ndc_y(p->y, s_H);
  pos[2] = 0.f;
  s_uv[s_n * 2] = p->u;
  s_uv[s_n * 2 + 1] = p->v;
  s_col[s_n] = engine_color(p->argb);
  s_n++;
}

void prim_tri(const PrimVert* a, const PrimVert* b, const PrimVert* c) {
  if (!s_live || !s_shader) return;
  if (s_n + 3 > kMaxVerts) submit();
  put(a); put(b); put(c);
}
