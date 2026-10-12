#pragma once
// Category ESP: markers on screen, edge arrows for off-screen objectives,
// secret volumes.

// Main thread, frame path, after entities_frame in the same frame and before the
// panel, so the panel covers it. Draws only in a frame with a captured view.
void esp_frame(int W, int H);
