#pragma once
// Puzzle solutions (Visuals WORLD; rules in puzzlerule.h): the next step of the e05p02_a01 floor plates and the
// e07p03_a01 cog buttons, drawn over the plates and buttons while Duke is near and the puzzle is unsolved.

// Main thread, after entities_frame in the same frame (it reads the snapshot's player). On any other map it costs
// one pointer compare.
void puzzle_frame(int W, int H);
