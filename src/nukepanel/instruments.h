#pragma once
// Timing instruments for the double-jump question (timing.md section 3,
// "Settling measurement"). On by default; the Settings row (config.h `timing`,
// [debug] timing in nukepanel.ini) turns them off and on live.

// Once per frame from probe_frame. `player` is the input entity already
// validated readable for kPlayerSpan bytes, or NULL when there is none.
// `jumpfixEvents` is jumpfix_frame's JF_EV_* bits for this frame.
void instruments_frame(const unsigned char* player, int jumpfixEvents);
// DllMain(PROCESS_DETACH): the cumulative counters one last time.
void instruments_detach(void);

// Covers every player field the instruments read (the last is +0xECC).
static const unsigned kPlayerSpan = 0xED0;
