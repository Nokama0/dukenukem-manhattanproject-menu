// Verbatim from duke3d-eduke32-aa/src/ekmod/ui/fontpack.h (itself Megaton's); kept identical on purpose -- do not edit here.
#pragma once
// Pure shelf packer for the runtime font atlas (header-only, Windows-free; fontgen.cpp is its only other user).
// SHELF, NOT A SORT: the glyph index IS the position in this array (gen_font.py packs the baked atlas alike), so
// reordering boxes by height would hand every glyph somebody else's rectangle. Wasting a little atlas is free.
struct PackBox { int w, h; };
struct PackPos { short x, y; };

// Shelf-packs `n` boxes left to right, top to bottom, `pad` pixels between boxes and from every atlas edge, IN INPUT
// ORDER. Boxes with w == 0 or h == 0 get {0,0} and take no space (a space has an advance and no pixels). Returns true
// when every box fits inside atlasW x atlasH.
inline bool fontpack_shelf(const PackBox* boxes, int n, int atlasW, int atlasH, int pad,
                           PackPos* out) {
  if (!boxes || !out || n < 0 || atlasW <= 0 || atlasH <= 0 || pad < 0) return false;
  int penX = pad, penY = pad, rowH = 0;
  for (int i = 0; i < n; i++) {
    const int w = boxes[i].w, h = boxes[i].h;
    if (w <= 0 || h <= 0) { out[i].x = 0; out[i].y = 0; continue; }
    if (penX + w + pad > atlasW) { penX = pad; penY += rowH + pad; rowH = 0; }
    if (penX + w + pad > atlasW) return false;     // wider than the atlas itself
    if (penY + h + pad > atlasH) return false;
    out[i].x = (short)penX; out[i].y = (short)penY;
    penX += w + pad;
    if (h > rowH) rowH = h;
  }
  return true;
}

// The size ladder, smallest first. 512x256 is what the baked atlas uses at 100%; 2048x2048 is the ceiling, a texture no
// GL 1.1 driver of this era refuses. Past it the answer is to fall back to the baked atlas rather than hand the driver
// something it will silently drop.
static const PackBox FONTPACK_LADDER[6] =
  { {512,256}, {512,512}, {1024,512}, {1024,1024}, {2048,1024}, {2048,2048} };

// Index of the FIRST rung the boxes fit (out and chosen filled for that rung),
// or -1 when none does. Smallest first, because the atlas is uploaded to the GPU
// and the rungs above are mostly empty space.
inline int fontpack_choose(const PackBox* boxes, int n, int pad, PackPos* out, PackBox* chosen) {
  for (int r = 0; r < 6; r++) {
    if (fontpack_shelf(boxes, n, FONTPACK_LADDER[r].w, FONTPACK_LADDER[r].h, pad, out)) {
      if (chosen) *chosen = FONTPACK_LADDER[r];
      return r;
    }
  }
  return -1;
}
