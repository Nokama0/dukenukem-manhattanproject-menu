// Ported from duke3d-eduke32-aa/src/ekmod/ui/fontgen.h (itself Megaton's). One change: the atlas
// also carries a 4x4 opaque white block (whiteX/whiteY), because our canvas draws every flat fill from the
// atlas's own white texel through one shader (render.md, "2D primitive submission").
#pragma once
// The panel's fonts, rasterized at the applied UI scale. DLL LINE ONLY: this is GDI, so it cannot link into test_host,
// which is why the packing decision lives in fontpack.h (pure, pinned by the suite). What is left here can only be
// checked in front of a real font: ask Windows for a face at a size and read the pixels back.
#include "font.h"   // Glyph, FONT_COUNT

// One atlas, built for one scale. `pixels` is 8-bit coverage in the same layout
// font_data.cpp's baked `font_atlas` uses, so the canvas uploads it the same way.
struct FontAtlas {
  int w, h;                       // atlas size, one of FONTPACK_LADDER
  unsigned char* pixels;          // w*h alpha bytes, owned (free with fontgen_free)
  Glyph glyphs[FONT_COUNT][96];   // DEVICE pixels: bx/by/w/h/adv at this scale
  int height[FONT_COUNT];         // tmHeight per face, device pixels (for the log)
  int pct;                        // the scale it was built for
  int whiteX, whiteY;             // top-left of the 4x4 white block, packed after the last glyph
};

// Rasterizes the three faces at ui_scale_px(base px, pct). ALL OR NOTHING: any face unavailable, implausible or
// unpackable returns false with `*out` untouched and one warning logged, and the caller keeps the baked atlas. Also
// false for pct == UI_SCALE_DEFAULT (100% IS baked). `out` must be ZEROED OR PREVIOUSLY BUILT: old contents are freed.
bool fontgen_build(int pct, FontAtlas* out);

// Frees `pixels` and zeroes the struct. Safe on a zeroed struct, so a caller
// can free unconditionally before every rebuild.
void fontgen_free(FontAtlas* a);
