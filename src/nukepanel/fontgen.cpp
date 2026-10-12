// Ported from duke3d-eduke32-aa/src/ekmod/ui/fontgen.cpp (itself Megaton's): np_log for LOGE/LOGI, and the white block
// fontgen.h describes packed as one more box after the glyphs. The panel's fonts rasterized at the applied UI scale:
// the baked atlas (Bahnschrift 21/16, Cascadia Mono 18) blurs when stretched, so scaled glyphs come from the outline.

// ui.h before windows.h, matching canvas.cpp: common.h is pure and must keep compiling on its own terms.
#include "ui/ui.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "fontgen.h"
#include "fontpack.h"
#include "log.h"

namespace {

const int GLYPH_FIRST = 0x20;   // the first character the atlas carries
const int GLYPH_COUNT = 96;     // 0x20..0x7F, the same range gen_font.py bakes
const int GLYPH_TOTAL = FONT_COUNT * GLYPH_COUNT;
const int WHITE_BOX   = 4;      // the white block's side, as the baked atlas's (508..511, 252..255)
const int ATLAS_PAD   = 1;      // gen_font.py's PAD, for the same reason: GL_LINEAR
                                // samples half a texel outside a glyph's own box

// Same three faces gen_font.py bakes, in the same order, so FONT_UI/
// FONT_SMALL/FONT_MONO mean the same thing in both atlases. Sizes are
// logical; each goes through ui_scale_px before reaching GDI.
struct FaceSpec { const wchar_t* face; int basePx; };
const FaceSpec FACES[FONT_COUNT] = {
  { L"Bahnschrift", 21 }, { L"Bahnschrift", 16 }, { L"Cascadia Mono", 18 } };

// The line gen_font.py measures from, in device pixels: every runtime glyph rests on it. Pillow anchors at FreeType's
// scaled hhea ascender (rounded up) but GDI's otmMacAscent is a pixel off at 5 of 12 face/size combos, so the ascender
// is recovered in font units (face queried at one em) and scaled with FreeType's rounding (else off by a pixel).
static int ascender_px(const wchar_t* face, int px, const OUTLINETEXTMETRICW& otm) {
  const int em = (int)otm.otmEMSquare;
  if (em <= 0 || px <= 0) return (int)otm.otmMacAscent;
  HDC dc = CreateCompatibleDC(nullptr);
  if (!dc) return (int)otm.otmMacAscent;
  int units = 0;
  HFONT f = CreateFontW(-em, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, face);
  if (f) {
    HGDIOBJ old = SelectObject(dc, f);
    OUTLINETEXTMETRICW emOtm;
    memset(&emOtm, 0, sizeof(emOtm));
    if (GetOutlineTextMetricsW(dc, sizeof(emOtm), &emOtm)) units = (int)emOtm.otmMacAscent;
    SelectObject(dc, old);
    DeleteObject(f);
  }
  DeleteDC(dc);
  if (units <= 0) return (int)otm.otmMacAscent;
  return (int)(((long long)units * (long long)px + (long long)em - 1) / (long long)em);
}

// One face's scratch cell: a DC with a 32 bpp top-down DIB and the font selected in. Every glyph draws at pen (px, px)
// inside a cell 4px wide and 3px tall so a negative left bearing or a descender has room. RAII: seven failure points
// between the first CreateCompatibleDC and the last glyph, and a leaked GDI handle per scale change would add up.
struct Cell {
  HDC       dc;
  HBITMAP   bmp;
  HGDIOBJ   oldBmp;
  HFONT     font;
  HGDIOBJ   oldFont;
  unsigned* bits;
  int       w, h;       // cell size in pixels
  int       px;         // em size this face was asked for, and the pen origin
  int       height;     // tmHeight, device pixels
  int       topAdj;     // GDI's cell top -> the line gen_font.py measured from

  Cell() : dc(nullptr), bmp(nullptr), oldBmp(nullptr), font(nullptr),
           oldFont(nullptr), bits(nullptr), w(0), h(0), px(0), height(0), topAdj(0) {}
  ~Cell() { release(); }

  void release() {
    if (dc && oldFont) SelectObject(dc, oldFont);
    oldFont = nullptr;
    if (font) DeleteObject(font);
    font = nullptr;
    if (dc && oldBmp) SelectObject(dc, oldBmp);
    oldBmp = nullptr;
    if (bmp) DeleteObject(bmp);
    bmp = nullptr;
    if (dc) DeleteDC(dc);
    dc = nullptr;
    bits = nullptr;
  }

  // Opens the cell and selects the face; logs its own reason on failure.
  bool open(const FaceSpec& spec, int emPx) {
    px = emPx;
    w  = 4 * px;
    h  = 3 * px;
    dc = CreateCompatibleDC(nullptr);
    if (!dc) {
      np_log("font: no device context for %ls at %d px; keeping the baked atlas", spec.face, px);
      return false;
    }

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;          // NEGATIVE: top-down, so row 0 is the top row
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* raw = nullptr;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &raw, nullptr, 0);
    if (!bmp || !raw) {
      np_log("font: no %dx%d cell for %ls; keeping the baked atlas", w, h, spec.face);
      return false;
    }
    bits   = (unsigned*)raw;
    oldBmp = SelectObject(dc, bmp);

    // Negative height is the em size, matching gen_font.py's
    // ImageFont.truetype(path, size); positive would give the cell height,
    // visibly smaller than the baked face.
    font = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, spec.face);
    if (!font) {
      np_log("font: could not create %ls at %d px; keeping the baked atlas", spec.face, px);
      return false;
    }
    oldFont = SelectObject(dc, font);

    // CreateFontW never fails for a missing face: it substitutes silently. Asking the DC what it selected is the only
    // way to tell, and a mismatch fails the whole build: a panel half in Bahnschrift and half in a substitute is worse
    // than the baked one.
    wchar_t got[64];
    got[0] = 0;
    if (GetTextFaceW(dc, 64, got) == 0 || _wcsicmp(got, spec.face) != 0) {
      np_log("font: %ls is not available, GDI selected %ls instead; keeping the baked atlas",
           spec.face, got);
      return false;
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    SetTextAlign(dc, TA_LEFT | TA_TOP | TA_NOUPDATECP);

    TEXTMETRICW tm;
    memset(&tm, 0, sizeof(tm));
    if (!GetTextMetricsW(dc, &tm)) {
      np_log("font: no metrics for %ls at %d px; keeping the baked atlas", spec.face, px);
      return false;
    }
    height = (int)tm.tmHeight;
    // A line box smaller than the em, or more than twice it, is not a UI
    // face at the size asked for -- catches a same-name substitution too.
    if (px > height || height > 2 * px) {
      np_log("font: %ls at %d px reports an implausible height of %d; keeping the baked atlas",
           spec.face, px, height);
      return false;
    }

    // The anchor: TA_TOP puts the pen at the top of the character cell (baseline - tmAscent), but gen_font.py measured
    // baked bearings from the ascender line, so without correcting every runtime glyph would sit lower than the baked
    // one (4 device px at Bahnschrift 21, 6 at 141%, 9 at 200%). The baked atlas is the reference.
    OUTLINETEXTMETRICW otm;
    memset(&otm, 0, sizeof(otm));
    if (GetOutlineTextMetricsW(dc, sizeof(otm), &otm) == 0) {
      np_log("font: no outline metrics for %ls at %d px; keeping the baked atlas", spec.face, px);
      return false;
    }
    topAdj = (int)tm.tmAscent - ascender_px(spec.face, px, otm);
    return true;
  }

  // Clears the cell to black and renders one character at the pen. GdiFlush
  // because the DIB is read directly afterwards and GDI batches its drawing.
  void render(int c) const {
    memset(bits, 0, (size_t)w * (size_t)h * 4u);
    const wchar_t wc = (wchar_t)c;
    TextOutW(dc, px, px, &wc, 1);
    GdiFlush();
  }

  // Bounding box of everything drawn, or false when empty (a space, or a character this face renders as nothing).
  // The red byte is the coverage: cleared to black, text drawn white, so an antialiased edge lands as a grey level
  // in all three colour bytes; alpha is not meaningful (GDI text output does not write it).
  bool bbox(int* bx0, int* by0, int* bx1, int* by1) const {
    int lox = w, loy = h, hix = -1, hiy = -1;
    for (int y = 0; y < h; y++) {
      const unsigned* row = bits + (size_t)y * (size_t)w;
      for (int x = 0; x < w; x++) {
        if (((row[x] >> 16) & 0xFFu) == 0u) continue;
        if (x < lox) lox = x;
        if (x > hix) hix = x;
        if (y < loy) loy = y;
        if (y > hiy) hiy = y;
      }
    }
    if (hix < 0) return false;
    *bx0 = lox; *by0 = loy; *bx1 = hix; *by1 = hiy;
    return true;
  }

  // Two answers, not one: a failed call is the same all-or-nothing failure
  // as a missing face, but a face genuinely reporting zero for one code is a
  // fact the caller decides whether to accept.
  bool advance(int c, int* out) const {
    int adv = 0;
    if (!GetCharWidth32W(dc, (UINT)c, (UINT)c, &adv)) return false;
    *out = adv;
    return true;
  }

private:
  Cell(const Cell&);
  Cell& operator=(const Cell&);
};

}  // namespace

void fontgen_free(FontAtlas* a) {
  if (!a) return;
  if (a->pixels) free(a->pixels);
  memset(a, 0, sizeof(*a));
}

bool fontgen_build(int pct, FontAtlas* out) {
  if (!out) return false;
  // 100% is the baked atlas: the ESP draws at 100 every frame and must keep
  // drawing the bytes it always drew.
  if (pct == UI_SCALE_DEFAULT) return false;

  Cell    cells[FONT_COUNT];
  PackBox boxes[GLYPH_TOTAL + 1];
  PackPos pos[GLYPH_TOTAL + 1];

  FontAtlas built;
  memset(&built, 0, sizeof(built));
  built.pct = pct;

  // Pass 1: measure. Every cell stays open for pass 2, so the glyph packed
  // is rendered by the same DC that measured it.
  for (int i = 0; i < FONT_COUNT; i++) {
    const int px = ui_scale_px(FACES[i].basePx, pct);
    if (px <= 0) {
      np_log("font: %d%% puts %ls at %d px; keeping the baked atlas", pct, FACES[i].face, px);
      return false;
    }
    if (!cells[i].open(FACES[i], px)) return false;   // open() logged the reason
    built.height[i] = cells[i].height;
    for (int g = 0; g < GLYPH_COUNT; g++) {
      const int c = GLYPH_FIRST + g;
      int adv = 0;
      if (!cells[i].advance(c, &adv)) {
        np_log("font: %ls at %d px could not report an advance for 0x%02X; keeping the baked atlas",
             FACES[i].face, px, c);
        return false;
      }
      // A visible character with no advance would collapse the string it
      // sits in, a hard failure. 0x20 and 0x7F are exempt: a space and
      // .notdef may legitimately report zero.
      if (adv <= 0 && c >= 0x21 && c <= 0x7E) {
        np_log("font: %ls at %d px has no advance for 0x%02X; keeping the baked atlas",
             FACES[i].face, px, c);
        return false;
      }
      Glyph& gl = built.glyphs[i][g];
      gl.x = gl.y = gl.w = gl.h = gl.bx = gl.by = 0;
      // Clamped: a negative advance would walk the pen backwards and draw
      // the next letter on top of this one.
      gl.adv = (short)(adv > 0 ? adv : 0);
      cells[i].render(c);
      int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      if (cells[i].bbox(&x0, &y0, &x1, &y1)) {
        // Ink touching an edge means GDI clipped it: the cell has room for any UI face's bearing/descender, so a bbox at
        // the edge is a letter already shaved. Vertical is unreachable behind the tmHeight gate; horizontal is not
        // checked against tmMaxCharWidth, so it is caught here.
        if (x0 == 0 || y0 == 0 || x1 == cells[i].w - 1 || y1 == cells[i].h - 1) {
          np_log("font: %ls at %d px draws 0x%02X past its cell; keeping the baked atlas",
               FACES[i].face, px, c);
          return false;
        }
        gl.w  = (short)(x1 - x0 + 1);
        gl.h  = (short)(y1 - y0 + 1);
        gl.bx = (short)(x0 - px);                     // bearing FROM THE PEN, like the baked metrics
        gl.by = (short)(y0 - px - cells[i].topAdj);   // ...and from the baked atlas's own top line
      }
      boxes[i * GLYPH_COUNT + g].w = gl.w;
      boxes[i * GLYPH_COUNT + g].h = gl.h;
    }
  }

  // Last in the shelf order, so no glyph's position depends on it.
  boxes[GLYPH_TOTAL].w = WHITE_BOX;
  boxes[GLYPH_TOTAL].h = WHITE_BOX;
  PackBox chosen = { 0, 0 };
  const int rung = fontpack_choose(boxes, GLYPH_TOTAL + 1, ATLAS_PAD, pos, &chosen);
  if (rung < 0) {
    long long area = 0;
    for (int k = 0; k < GLYPH_TOTAL; k++) area += (long long)boxes[k].w * (long long)boxes[k].h;
    np_log("font: the %d%% glyphs (%lld px of coverage) do not fit %dx%d; keeping the baked atlas",
         pct, area, FONTPACK_LADDER[5].w, FONTPACK_LADDER[5].h);
    return false;
  }

  built.w = chosen.w;
  built.h = chosen.h;
  built.pixels = (unsigned char*)calloc((size_t)chosen.w * (size_t)chosen.h, 1);
  if (!built.pixels) {
    np_log("font: no memory for a %dx%d runtime atlas at %d%%; keeping the baked atlas",
         chosen.w, chosen.h, pct);
    return false;
  }

  built.whiteX = pos[GLYPH_TOTAL].x;
  built.whiteY = pos[GLYPH_TOTAL].y;
  for (int row = 0; row < WHITE_BOX; row++)
    memset(built.pixels + (size_t)(built.whiteY + row) * (size_t)built.w + (size_t)built.whiteX, 0xFF, WHITE_BOX);

  // Pass 2: render into the atlas, now that there is somewhere to put it.
  for (int i = 0; i < FONT_COUNT; i++) {
    // Read off the cell, not recomputed: two independent copies of pass 1's
    // arithmetic could drift.
    const int px = cells[i].px, topAdj = cells[i].topAdj;
    for (int g = 0; g < GLYPH_COUNT; g++) {
      Glyph& gl = built.glyphs[i][g];
      if (gl.w <= 0 || gl.h <= 0) continue;
      gl.x = pos[i * GLYPH_COUNT + g].x;
      gl.y = pos[i * GLYPH_COUNT + g].y;
      cells[i].render(GLYPH_FIRST + g);
      const int sx = gl.bx + px, sy = gl.by + px + topAdj;   // the bbox origin, in cell coordinates
      for (int row = 0; row < gl.h; row++) {
        const unsigned* src = cells[i].bits + (size_t)(sy + row) * (size_t)cells[i].w + (size_t)sx;
        unsigned char*  dst = built.pixels + (size_t)(gl.y + row) * (size_t)built.w + (size_t)gl.x;
        for (int col = 0; col < gl.w; col++) dst[col] = (unsigned char)((src[col] >> 16) & 0xFFu);
      }
    }
  }

  // Line heights logged here (FontAtlas::height[] is log-only, nothing
  // reads it): the more useful half when a face comes out wrong elsewhere,
  // since the em size is what was asked for and the line height is given.
  np_log("font: runtime atlas %d%% %dx%d, Bahnschrift %d/%d px (line %d/%d), "
       "Cascadia Mono %d px (line %d)",
       pct, built.w, built.h,
       ui_scale_px(FACES[0].basePx, pct), ui_scale_px(FACES[1].basePx, pct),
       built.height[0], built.height[1],
       ui_scale_px(FACES[2].basePx, pct), built.height[2]);
  // Freed here rather than trusting every caller to have freed it first.
  fontgen_free(out);
  *out = built;   // ownership of `pixels` moves to the caller
  return true;
}
