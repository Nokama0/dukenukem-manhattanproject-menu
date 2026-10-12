#pragma once
#include "ui.h"
// The carried menu's view of input, as in AA's ui/input.h: one struct, filled
// once a frame by panel.cpp from the WndProc subclass (../input.h), in render
// pixels. Nothing outside ui/ and panel.cpp includes this file.
extern UiInput g_ui;
