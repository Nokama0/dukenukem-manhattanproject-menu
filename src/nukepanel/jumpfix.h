#pragma once
// Fix double jump (Settings > FRAME RATE): undoes the takeoff disarm of the air boost
// (jumprule.h, double-jump.md section 5 lever 1) by writing the player's +0xE14 back.

enum { JF_EV_TAKEOFF = 1, JF_EV_RESTORED = 2 };

// Once per frame from probe_frame, after the world update and before the instruments.
// `player` is this frame's input entity, readable for kPlayerSpan bytes, or NULL.
// Returns the JF_EV_* bits for this frame: a takeoff disarm seen, and whether it was undone.
int jumpfix_frame(unsigned char* player);
