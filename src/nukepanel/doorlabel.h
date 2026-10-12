#pragma once
// The cross-area door label: at the doorway that starts the fewest-hops way to another area with something still
// missing, "to a01_r01 (1 secret, 2 nukes)". Secrets count while Secret areas is on, nukes while Nukes is on.
// A label only; the route lines are gone (re/paths.md section 7). The rules are areas.h's.

// Main thread, frame path, after areas_frame: chooses the door 4 times a second and shares the choice with the
// Progress tab's map and list (areas_set_cross).
void doorlabel_frame(const unsigned char* player);
// The label, under the ESP markers. Not called in screenshot mode.
void doorlabel_draw(int W, int H);
