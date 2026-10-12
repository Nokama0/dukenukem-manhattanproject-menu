"""Generate the canvas's texture assets from src/nukepanel/font_data.cpp.

font.png  512x256 RGBA: white, alpha = the baked 8-bit coverage of all three
          faces (they share one atlas, so one shader serves every glyph), plus
          an opaque WHITE_BLOCK that flat fills sample. One shader for the whole
          canvas keeps submission order in the engine's ortho pool (render.md,
          "2D primitive submission").
font.shd  probe.shd with that source, nearest filtering, no mips, no culling,
          texenv modulate (vertex colour tints the texel); nocompress available but off.
white.png / white.shd  a 4x4 opaque white texture: the fallback flat-fill
          shader when font.shd does not load.
"""
import os
import re
import struct
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = os.path.join(ROOT, "src", "nukepanel", "font_data.cpp")
OUT = os.path.join(ROOT, "assets", "nukepanel")
PROBE_SHD = os.path.join(OUT, "probe.shd")
WHITE_BLOCK = (508, 252, 4, 4)   # x, y, w, h; canvas.cpp samples its centre texel (510, 254)


def png(path, w, h, rgba_rows):
    raw = b"".join(b"\x00" + row for row in rgba_rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def shd(path, source, nocompress):
    # Keyword spellings from the shipped shaders in duke/base/db_cor.zipfs:
    # `texenv modulate` (fonts/default.shd), `cull none` in the pass
    # (specials/notfound.shd). `nocompress` is in loaders.dll's keyword table
    # among the layer flags but no shipped shader uses it.
    text = open(PROBE_SHD, newline="").read()
    text = text.replace("probe.png", source)
    text = re.sub(r"minfilter\s+linear", "minfilter       nearest", text)
    text = re.sub(r"magfilter\s+linear", "magfilter       nearest", text)
    text = text.replace("pass {\n", "pass {\n        cull            none\n", 1)
    layer_extra = "                texenv          modulate\n"
    if nocompress:
        layer_extra += "                nocompress\n"
    text = text.replace("                clamp\n", "                clamp\n" + layer_extra, 1)
    assert "nomips" in text and source in text and "linear" not in text
    assert "cull            none" in text and "texenv          modulate" in text
    with open(path, "w", newline="\n") as f:
        f.write(text)


def main():
    src = open(SRC).read()
    w = int(re.search(r"FONT_ATLAS_W\s*=\s*(\d+)", src).group(1))
    h = int(re.search(r"FONT_ATLAS_H\s*=\s*(\d+)", src).group(1))
    body = src[src.index("font_atlas[] = {") + len("font_atlas[] = {"):]
    body = body[:body.index("};")]
    cov = [int(t) for t in body.replace("\n", "").split(",") if t.strip()]
    assert len(cov) == w * h, (len(cov), w, h)
    glyphs = src[src.index("font_glyphs[FONT_COUNT][96] = {"):src.index("font_atlas[]")]
    rects = [tuple(int(v) for v in m.groups()) for m in re.finditer(r"\{(-?\d+),(-?\d+),(-?\d+),(-?\d+),-?\d+,-?\d+,-?\d+\}", glyphs)]
    assert len(rects) == 3 * 96, len(rects)
    bx, by, bw, bh = WHITE_BLOCK
    for gx, gy, gw, gh in rects:   # the block must be free space, not someone's glyph
        assert gw == 0 or gh == 0 or gx + gw <= bx or gy + gh <= by or gx >= bx + bw or gy >= by + bh
    for y in range(by, by + bh):
        for x in range(bx, bx + bw):
            assert cov[y * w + x] == 0
            cov[y * w + x] = 255
    rows = [bytes(b for a in cov[y * w:(y + 1) * w] for b in (255, 255, 255, a)) for y in range(h)]
    png(os.path.join(OUT, "font.png"), w, h, rows)
    # nocompress off: no shipped shader uses it, and a parse failure would lose the whole font (2026-09-27).
    shd(os.path.join(OUT, "font.shd"), "font.png", False)
    png(os.path.join(OUT, "white.png"), 4, 4, [b"\xff" * 16] * 4)
    shd(os.path.join(OUT, "white.shd"), "white.png", False)
    print("wrote font.png %dx%d, font.shd, white.png, white.shd" % (w, h))


if __name__ == "__main__":
    main()
