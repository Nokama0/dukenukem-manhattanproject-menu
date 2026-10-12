#pragma once
// Pure rules for the double-jump fix and its instruments (double-jump.md sections 1-2). Times are world+0x40
// seconds; E10/E14 are the player's float stamps, vy the player's +0x1C4 (up is positive).

// The ground sweep's contact callback (duke!0x1007a960, A 0x1007ac11..ac17) copies E10 into E14 on the frame after a
// ground jump of 5 ms or less, disarming the boost. That disarm alone qualifies: E14 just became E10, was armed (below
// E10) before, the jump is under 20 ms old and Duke is rising. Landings, ceiling bumps and air-press contacts do not.
static const double kJrTakeoffWindowS = 0.02;

inline int jr_takeoff_disarm(float prevE14, float e14, float e10, double t, float vy) {
  if (e14 == prevE14 || e14 != e10) return 0;
  if (!(prevE14 < e10)) return 0;
  // E10 is a float copy of the time, so it can sit a rounding step after t on the jump frame.
  const double since = t - (double)e10;
  if (since < -0.001 || since >= kJrTakeoffWindowS) return 0;
  return vy > 0.f ? 1 : 0;
}

enum PressResult { PR_NONE, PR_BOOST, PR_REFUSED_TIME, PR_REFUSED_ARMED, PR_REFUSED_HEIGHT,
                   PR_REFUSED_STATE, PR_REFUSED_OTHER };

// One air press (an edge, no ground jump, gnd 0), read after the frame. A boost replaces vy, so a rise over 100 is one.
// A refusal still stamps E14 with now (A 0x10085d29..d32): unchanged E14 means the air branch never ran. Else the first
// failing gate (A 0x10085b2a..b78): 0.2 s after E10, E14 below E10, y not over 50 below E70.
inline PressResult jr_classify_press(int airEdge, float prevVy, float vy, float prevE14, float e14,
                                     float e10, double t, float y, float e70) {
  if (!airEdge) return PR_NONE;
  if (vy - prevVy > 100.f) return PR_BOOST;
  if (e14 == prevE14) return PR_REFUSED_STATE;
  if (!(t - (double)e10 > (double)0.2f)) return PR_REFUSED_TIME;
  if (!(prevE14 < e10)) return PR_REFUSED_ARMED;
  if (!(y >= e70 - 50.f)) return PR_REFUSED_HEIGHT;
  return PR_REFUSED_OTHER;
}

inline const char* jr_press_tag(PressResult r) {
  switch (r) {
    case PR_BOOST:          return " BOOST";
    case PR_REFUSED_TIME:   return " REFUSED(gate1)";
    case PR_REFUSED_ARMED:  return " REFUSED(gate2)";
    case PR_REFUSED_HEIGHT: return " REFUSED(gate3)";
    case PR_REFUSED_STATE:  return " REFUSED(state)";
    case PR_REFUSED_OTHER:  return " REFUSED(?)";
    default:                return "";
  }
}
