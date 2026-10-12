#pragma once
// Rescue falls (View > MOUSE LOOK): a fall that no floor or death area ends puts Duke back on the last ground he stood
// on, through player_teleport. Pure rules; rescue.cpp is the glue. Measured: a fall tops out near 611 u/s (so
// RS_FALL_MS is about 2400 units of drop) and a jump lands in under a second; 4 s and 2000 units down means no floor.
#include "w2s.h"   // Vec3

const unsigned RS_SAFE_MS = 500;    // a grounded spot becomes the safe spot once it is this old
const unsigned RS_FALL_MS = 4000;
const float RS_FALL_DROP = 2000.f;
enum { RS_NONE = 0, RS_RESCUE };

// cand: the grounded spot being aged; safe: the last one that aged RS_SAFE_MS (so 0.5-1 s behind a ledge's edge).
struct RescueState {
  int haveSafe;
  Vec3 safe, cand;
  unsigned long long candAt, airSince;
  float airTopY;
};

// eligible: the switch on, a live player in a walking state (not dying, no jetpack or scripted state).
inline int rescue_step(RescueState* r, int eligible, int grounded, Vec3 pos, float vy, unsigned long long now) {
  (void)vy;
  if (!eligible) { r->airSince = 0; return RS_NONE; }
  if (grounded) {
    r->airSince = 0;
    if (!r->candAt) { r->cand = pos; r->candAt = now ? now : 1; }
    else if (now - r->candAt >= RS_SAFE_MS) { r->safe = r->cand; r->haveSafe = 1; r->cand = pos; r->candAt = now; }
    return RS_NONE;
  }
  r->candAt = 0;   // a spot only ages while he stands on it
  if (!r->airSince) { r->airSince = now ? now : 1; r->airTopY = pos.y; return RS_NONE; }
  if (pos.y > r->airTopY) r->airTopY = pos.y;
  if (!r->haveSafe || now - r->airSince < RS_FALL_MS) return RS_NONE;
  const float ref = r->airTopY < r->safe.y ? r->airTopY : r->safe.y;
  if (pos.y >= ref - RS_FALL_DROP) return RS_NONE;
  r->airSince = 0;   // once per fall; a new one starts from here
  return RS_RESCUE;
}

// --- rescue.cpp (DLL only; read, not run, by the suite) ---------------------------------------
// Frame path after moveanywhere_frame: the safe spot, the fall watch and the rescue.
void rescue_frame(const unsigned char* player);
