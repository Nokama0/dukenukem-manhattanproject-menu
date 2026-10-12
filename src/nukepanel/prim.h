#pragma once
// Batched triangle submission through prism3d's rend_cache_data into the
// "ortho" pool (render.md, "2D primitive submission"). Main thread only, from
// the end-frame detour, between prim_begin and prim_end.
typedef struct c_item_t c_item_t;

// Pixels (origin top-left, y down), texture UV, colour 0xAARRGGBB.
struct PrimVert { float x, y, u, v; unsigned argb; };

// Ortho projection + identity modelview for the frame; false (and nothing may
// be drawn) when an export this needs did not resolve.
bool prim_begin(int W, int H);
// Submits what is queued, flushes the pools and restores the projection.
void prim_end(void);

// The shader the following triangles use. A different one submits and flushes
// what is queued first: the ortho pool groups buckets by shader, so order across
// shaders only survives a flush between them.
void prim_shader(c_item_t* shader);
void prim_tri(const PrimVert* a, const PrimVert* b, const PrimVert* c);

// Cumulative since load, for the log: packets queued, vertices, pool flushes
// forced by a shader change or by the render-cache budget.
struct PrimStats { unsigned packets, verts, shader_flushes, budget_flushes, frames; };
PrimStats prim_stats(void);
