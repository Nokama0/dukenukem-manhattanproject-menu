#pragma once
// The panel on the frame path: fills the menu's input from the WndProc subclass,
// draws the menu and the software cursor, saves changed settings.

// Frame path only, after input_frame and after config_load (probe.cpp's first frame).
void panel_frame(int W, int H);
