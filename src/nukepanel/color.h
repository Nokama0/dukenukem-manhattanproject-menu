#pragma once
// prism3d's rend_line_plain takes 0xAABBGGRR; our constants are the familiar 0xAARRGGBB, so each routes through this
// swap before the call (unswapped, "cyan" drew yellow and "red" drew blue). rend_bitmap's 0xFFFFFFFF tint is
// symmetric under the swap and is left alone.
inline unsigned engine_color(unsigned argb) {
  const unsigned a = argb & 0xFF000000u;
  const unsigned r = (argb >> 16) & 0xFFu;
  const unsigned g = argb & 0x0000FF00u;
  const unsigned b = argb & 0x000000FFu;
  return a | (b << 16) | g | r;
}
