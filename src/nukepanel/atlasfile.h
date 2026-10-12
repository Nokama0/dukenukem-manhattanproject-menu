#pragma once
// The runtime atlas as the two files the engine already loads the baked one from: font.png (RGBA, white, alpha =
// coverage) and font.shd. Prism3D gives the canvas no texture upload; a shader plus its PNG via mm_get_resource is
// the one path proven on screen (docs/re/render.md). Pure so the suite pins the encoder; canvas.cpp writes bytes.
#include <stddef.h>
#include <string.h>

// CRC-32 (ISO-HDLC, PNG's chunk check), bitwise: an atlas is encoded once per scale change.
inline unsigned atlas_crc32(const unsigned char* p, size_t n) {
  unsigned c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return c ^ 0xFFFFFFFFu;
}

// Adler-32, the zlib stream's trailer.
inline unsigned atlas_adler32(const unsigned char* p, size_t n) {
  unsigned a = 1, b = 0;
  for (size_t i = 0; i < n; i++) { a = (a + p[i]) % 65521u; b = (b + a) % 65521u; }
  return (b << 16) | a;
}

// Uncompressed ("stored") deflate blocks: every inflater reads them and the encoder stays a few lines. The file is
// bigger than a compressed one (2 MB at 1024x512), written once per scale change.
inline size_t atlas_png_raw_size(int w, int h) { return (size_t)h * (1u + 4u * (size_t)w); }
inline size_t atlas_png_size(int w, int h) {
  if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return 0;
  const size_t raw = atlas_png_raw_size(w, h);
  const size_t blocks = (raw + 65534u) / 65535u;
  const size_t zlib = 2u + raw + 5u * blocks + 4u;
  return 8u + (12u + 13u) + (12u + zlib) + 12u;
}

inline void atlas_put_be32(unsigned char* p, unsigned v) {
  p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

// w*h coverage bytes -> an RGBA PNG, white with the coverage as alpha (tools/gen_atlas.py's font.png layout).
// Returns the bytes written, or 0 (nothing written) on a bad argument or a buffer under atlas_png_size.
inline size_t atlas_png_encode(const unsigned char* alpha, int w, int h, unsigned char* out, size_t cap) {
  const size_t total = atlas_png_size(w, h);
  if (!alpha || !out || total == 0 || cap < total) return 0;
  static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
  unsigned char* p = out;
  memcpy(p, sig, 8); p += 8;
  atlas_put_be32(p, 13); memcpy(p + 4, "IHDR", 4);
  atlas_put_be32(p + 8, (unsigned)w); atlas_put_be32(p + 12, (unsigned)h);
  p[16] = 8; p[17] = 6; p[18] = 0; p[19] = 0; p[20] = 0;   // 8-bit RGBA, deflate, adaptive filters, no interlace
  atlas_put_be32(p + 21, atlas_crc32(p + 4, 17));
  p += 25;
  const size_t raw = atlas_png_raw_size(w, h);
  const size_t blocks = (raw + 65534u) / 65535u;
  const size_t idatLen = 2u + raw + 5u * blocks + 4u;
  unsigned char* idat = p;
  atlas_put_be32(p, (unsigned)idatLen); memcpy(p + 4, "IDAT", 4);
  p += 8;
  *p++ = 0x78; *p++ = 0x01;   // zlib: deflate, 32 KB window, no dictionary, FCHECK
  const size_t row = 1u + 4u * (size_t)w;
  unsigned a = 1, b = 0;
  size_t i = 0;
  while (i < raw) {
    const size_t len = raw - i < 65535u ? raw - i : 65535u;
    *p++ = (unsigned char)(i + len == raw ? 1 : 0);   // BFINAL on the last; BTYPE 00, stored
    p[0] = (unsigned char)len; p[1] = (unsigned char)(len >> 8);
    p[2] = (unsigned char)~len; p[3] = (unsigned char)(~len >> 8);
    p += 4;
    for (size_t k = 0; k < len; k++, i++) {
      const size_t col = i % row;
      unsigned char v;
      if (col == 0) v = 0;                         // filter: none
      else if ((col - 1) % 4 != 3) v = 0xFF;       // white
      else v = alpha[(i / row) * (size_t)w + (col - 1) / 4];
      *p++ = v;
      a = (a + v) % 65521u; b = (b + a) % 65521u;
    }
  }
  atlas_put_be32(p, (b << 16) | a); p += 4;
  atlas_put_be32(p, atlas_crc32(idat + 4, 4u + idatLen)); p += 4;
  atlas_put_be32(p, 0); memcpy(p + 4, "IEND", 4); atlas_put_be32(p + 8, 0xAE426082u); p += 12;
  return (size_t)(p - out);
}

// assets/nukepanel/font.shd with its source renamed (tools/gen_atlas.py writes that file; keep the two in step).
// Returns the length written, or 0 when it does not fit.
inline int atlas_shd_text(const char* pngName, char* out, size_t cap) {
  static const char head[] =
    "common {\n        ortho\n\tpriority\t0\n}\n\n"
    "pass {\n        cull            none\n        depthmask       false\n"
    "        blendfunc       SRC_ALPHA           ONE_MINUS_SRC_ALPHA\n        depthfunc       ALWAYS\n"
    "        colorgen\t\tselect 0\n        layer {\n                nomips\n"
    "                uvgen           select  0\n                source          ";
  static const char tail[] =
    "\n                minfilter       nearest\n                magfilter       nearest\n"
    "                tquality        high\n                clamp\n                texenv          modulate\n"
    "        }\n}\n";
  if (!pngName || !out) return 0;
  const size_t hn = sizeof(head) - 1, nn = strlen(pngName), tn = sizeof(tail) - 1;
  if (nn == 0 || hn + nn + tn + 1 > cap) return 0;
  memcpy(out, head, hn);
  memcpy(out + hn, pngName, nn);
  memcpy(out + hn + nn, tail, tn + 1);
  return (int)(hn + nn + tn);
}

// The UV flat fills sample: the centre of a 4x4 opaque block at (x, y), as the baked atlas's (508, 252) block gives
// (510/512, 254/256). A texel corner whose four neighbours are all white, so any filtering samples white.
inline void atlas_white_uv(int x, int y, int w, int h, float* u, float* v) {
  *u = w > 0 ? (float)(x + 2) / (float)w : 0.f;
  *v = h > 0 ? (float)(y + 2) / (float)h : 0.f;
}

// The read-back of a written file, chunk by chunk: bytes read so far and the first offset that differs from what
// was written (-1 none; n when the file runs past it). canvas.cpp logs both when the VFS does not serve the atlas.
struct AtlasReadBack { size_t at; long long firstDiff; };
inline void atlas_readback_begin(AtlasReadBack* r) { r->at = 0; r->firstDiff = -1; }
inline void atlas_readback_step(AtlasReadBack* r, const unsigned char* want, size_t n, const unsigned char* got,
                                size_t m) {
  for (size_t k = 0; k < m && r->firstDiff < 0; k++) {
    const size_t off = r->at + k;
    if (off >= n || got[k] != want[off]) r->firstDiff = (long long)off;
  }
  r->at += m;
}
inline bool atlas_readback_ok(const AtlasReadBack* r, size_t n) { return r->firstDiff < 0 && r->at == n && n > 0; }
