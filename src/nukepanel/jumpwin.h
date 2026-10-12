#pragma once
// Per-frame logging window after a jump-key edge: long enough (60 frames) to
// cover a ground jump and a possible boost, short enough to keep the log small.
struct JumpWin { int left; };

inline void jw_update(JumpWin* w, int jumpEdge) {
  if (jumpEdge) w->left = 60;
  else if (w->left > 0) w->left--;
}
inline int jw_active(const JumpWin* w) { return w->left > 0; }
