#pragma once
// Classifies a change of player+0xE14 against the ground-jump stamp +0xE10. The landing reset sets +0xE14 back to
// +0xE10's value and is not a boost; only a change to something else is a real air boost.
enum BoostChange { BOOST_NONE, BOOST_AIR, BOOST_RESET };

inline BoostChange boost_classify(float e14, float prev_e14, float e10) {
  if (e14 == prev_e14) return BOOST_NONE;
  return (e14 == e10) ? BOOST_RESET : BOOST_AIR;
}
