#pragma once
// The free camera's viewpoint as a line of text, for the Copy view and Paste view keys. Pure: clipboard.cpp is the
// syscall, freecam.cpp the two keys. Own tag, not Megaton's line: the Build games' "cam x= y= z= ang= pitch=" is in
// Build units and means nothing here, so a line without "dnmp cam" is refused. Units: world units (52 a metre).
#include "freecam.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// Past the freecam's own far plane (nocull.h's NOCULL_FAR) nothing draws at all, so a paste beyond it is a
// bad line, not a place. No map extent is read (whether one would be tighter is unverified; not needed for v1).
const float CAMPOS_MAX_UNITS = 1000000.f;

// False, with `out` left empty, when the whole line does not fit: half a pose would parse to somewhere else.
inline bool campos_format(const FreecamPose& p, char* out, int cap) {
  if (!out || cap <= 0) return false;
  out[0] = 0;
  const int n = _snprintf_s(out, (size_t)cap, _TRUNCATE, "dnmp cam x=%.1f y=%.1f z=%.1f yaw=%.2f pitch=%.2f",
                            p.eye[0], p.eye[1], p.eye[2], p.yaw, p.pitch);
  if (n < 0) { out[0] = 0; return false; }
  return true;
}

// The tag anywhere in the text, then five numbers, all finite and in range. On success the eye, yaw and
// pitch are written and `hand` is kept (it is the engine camera's, not the viewpoint's); on refusal `io`
// is not touched.
inline bool campos_accept(const char* text, FreecamPose* io) {
  if (!text || !io) return false;
  const char* p = strstr(text, "dnmp cam x=");
  if (!p) return false;
  float x, y, z, yaw, pitch;
  if (sscanf_s(p, "dnmp cam x=%f y=%f z=%f yaw=%f pitch=%f", &x, &y, &z, &yaw, &pitch) != 5) return false;
  const float v[5] = { x, y, z, yaw, pitch };
  for (int i = 0; i < 5; ++i) if (!isfinite(v[i])) return false;
  for (int i = 0; i < 3; ++i) if (fabsf(v[i]) > CAMPOS_MAX_UNITS) return false;
  if (fabsf(yaw) > 360.f || fabsf(pitch) > FREECAM_PITCH_MAX) return false;
  if (yaw > 180.f) yaw -= 360.f; else if (yaw <= -180.f) yaw += 360.f;
  io->eye[0] = x; io->eye[1] = y; io->eye[2] = z;
  io->yaw = yaw; io->pitch = pitch;
  return true;
}
