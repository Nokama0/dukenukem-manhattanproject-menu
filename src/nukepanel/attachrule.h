#pragma once
// Where an entity's world position comes from (docs/re/entities.md, "Attached entities"). +0x18C is local to the
// parent at +0x60: update_transforms p3shared!0x10017e00 copies the parent's world matrix to +0xF4 and translates it
// by +0x18C (update_local_transforms 0x10017de0); get_world_pos 0x10017e70 reads back via mult_zero_matrix 0x10016aa0.
#include "w2s.h"   // Vec3
#include <string.h>

// ATT_ROOT: no parent, +0x18C is world. ATT_MATRIX: the world matrix's translation. ATT_UNSET: a parent but the
// matrix was never computed (+0x984 is 0 from the object_e ctor until the first update_transforms). ATT_BAD: the
// translation is not finite. The last two keep +0x18C.
enum { ATT_ROOT = 0, ATT_MATRIX = 1, ATT_UNSET = 2, ATT_BAD = 3 };

// m is the 16-float row-vector world matrix at +0xF4 (translation in m[12..14], w in m[15]); stamp is +0x984.
inline int att_world_pos(const void* parent, unsigned stamp, const float* m, Vec3 local, Vec3* out) {
  *out = local;
  if (!parent) return ATT_ROOT;
  if (!stamp) return ATT_UNSET;
  Vec3 t = { m[12], m[13], m[14] };
  // mult_zero_matrix: divide by w unless it is exactly 1.0f (compared as bits) or 0.
  unsigned wb;
  memcpy(&wb, &m[15], sizeof wb);
  if (wb != 0x3F800000u && m[15] != 0.f) {
    const float r = 1.f / m[15];
    t.x *= r; t.y *= r; t.z *= r;
  }
  // x - x is 0 only for a finite x (NaN and infinities give NaN).
  if (!(t.x - t.x == 0.f && t.y - t.y == 0.f && t.z - t.z == 0.f)) return ATT_BAD;
  *out = t;
  return ATT_MATRIX;
}

inline const char* att_name(int how) {
  return how == ATT_ROOT ? "root" : how == ATT_MATRIX ? "matrix" : how == ATT_UNSET ? "unset" : "bad";
}
