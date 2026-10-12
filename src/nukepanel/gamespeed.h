#pragma once
// Game speed (Settings > GAME SPEED; docs/re/game-speed.md). Our winmm.dll serves timeGetTime itself: a call from
// inside prism3d.exe (tmr_read, the engine's only clock) gets the scaled clock of speedrule.h, every other caller
// the real time. Sound, the frame cap, GLDirect and the panel read other clocks and keep their speed.

// The switch, 0/1. Not saved: off at every launch. Written by its panel row and the key.
extern int g_gameSpeedOn;

// Main thread, once a frame after panel_frame: the key's presses, the speed the clock runs at, the 5 s log line.
void gamespeed_frame(void);

// The corner tag while the switch is on ("Speed 50%"); false and nothing written while off.
bool gamespeed_tag(char* out, int cap);
