// Verbatim from Megaton's src/duke3dmod/font.h; kept identical on purpose -- do not edit here.
#pragma once

enum { FONT_UI = 0, FONT_SMALL = 1, FONT_MONO = 2, FONT_COUNT = 3 };

struct Glyph {
  short x, y, w, h;   // position and size in the atlas
  short bx, by;       // bearing: offset from the pen position to the top-left
  short adv;          // pen advance
};

extern const int FONT_ATLAS_W;
extern const int FONT_ATLAS_H;
extern const unsigned char font_atlas[];        // FONT_ATLAS_W * FONT_ATLAS_H, 8-bit alpha
extern const Glyph font_glyphs[FONT_COUNT][96]; // ASCII 0x20..0x7F
extern const int font_height[FONT_COUNT];
