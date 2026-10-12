"""Generate assets/nukepanel/probe.png: the stage 0 probe's textured quad.

64x64 RGBA, 8x8 checks (cyan / near-black) with a hazard-yellow "N" 32 px
tall, centred. The "N" is asymmetric on purpose: mirrored or upside down on
screen means the quad's rect or uv order is swapped.
"""
import os
import struct
import zlib

SIZE = 64
CYAN = (0x2F, 0xE0, 0xD0, 255)
DARK = (0x0E, 0x11, 0x14, 255)
YELLOW = (0xF2, 0xB0, 0x1E, 255)


def pixels():
    px = [[CYAN if ((x // 8) + (y // 8)) % 2 == 0 else DARK for x in range(SIZE)] for y in range(SIZE)]
    top, h, left, w, stroke = 16, 32, 20, 24, 6
    for y in range(top, top + h):
        for x in range(left, left + stroke):              # left upright
            px[y][x] = YELLOW
        for x in range(left + w - stroke, left + w):      # right upright
            px[y][x] = YELLOW
        t = (y - top) / (h - 1)                           # diagonal: top-left to bottom-right
        cx = left + stroke / 2 + t * (w - stroke)
        for x in range(int(cx - stroke / 2), int(cx + stroke / 2)):
            px[y][x] = YELLOW
    return px


def write_zlib(path, px):
    raw = b"".join(b"\x00" + bytes(c for p in row for c in p) for row in px)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "nukepanel", "probe.png")
    px = pixels()
    try:
        from PIL import Image
        img = Image.new("RGBA", (SIZE, SIZE))
        img.putdata([p for row in px for p in row])
        img.save(out)
    except ImportError:
        write_zlib(out, px)
    print("wrote", os.path.normpath(out))


if __name__ == "__main__":
    main()
