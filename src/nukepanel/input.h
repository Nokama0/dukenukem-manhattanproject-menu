#pragma once
// The game window's WndProc subclass: panel and freecam keys, input ownership via the engine's inp_set_active
// (input-console.md section 4), the cursor. Plain statics, not interlocked: a window procedure runs on the thread that
// created the window, the engine's main thread, which also runs the frame (measured; input.cpp logs a THREAD MISMATCH).
#include <windows.h>   // HWND for input_window; cheap under WIN32_LEAN_AND_MEAN

// Frame path, every frame before anything draws. Installs the subclass on the
// first frame that finds the game window (never in DllMain), re-asserts the
// panel's ownership and maps the cursor to the render size W x H.
void input_frame(int W, int H);

bool input_panel_open(void);

// The freecam is on (the panel may be open over it); toggled by BIND_ROW_CAMERA.
bool input_freecam_on(void);
// The engine console takes typing (console_takes_keys), as of the last input_frame.
bool input_console_open(void);
// ui_wget(cls, name, 1) once the UI manager exists (the check console detection makes), else NULL.
void* input_ui_window(const char* cls, const char* name);
// The same over every window, shown or hidden (ui_wget's visibleOnly 0).
void* input_ui_window_any(const char* cls, const char* name);
// It steers right now: on, the panel closed, the window in front, the cursor grabbed.
bool input_freecam_steers(void);
// WM_MOUSEMOVE counts (client pixels) since the last call, 0 while not grabbed.
void input_freecam_mouse_take(int* dx, int* dy);
// Teleport key presses (BIND_ROW_TELEPORT) since the last call, taken only while the freecam steers.
int input_teleport_take(void);
// Copy view / Paste view presses (BIND_ROW_CAMCOPY/CAMPASTE) since the last call, taken only while the freecam steers.
int input_camcopy_take(void);
int input_campaste_take(void);
// Photo key presses (BIND_ROW_PHOTO) since the last call, taken only while the freecam steers.
int input_photo_take(void);
// Game speed key presses (BIND_ROW_GAMESPEED) since the last call, taken with the panel shut, flying or not.
int input_gamespeed_take(void);

// The subclassed game window, or NULL before it is found (input_frame retries
// every 30 frames). framecap.cpp reads this; nothing here searches for it again.
HWND input_window(void);
// Checks now whether the engine replaced its game window (gl does on every rend_restart) and moves
// the subclass to the new one; returns the window we are on, NULL if none yet. Main thread.
HWND input_window_resync(void);
// At launch (display.cpp's CBT hook, the engine's first Prism3D window): our proc becomes the class
// proc, so the game window gl creates next runs through the display filter from WM_NCCREATE.
bool input_class_proc_early(HWND anyPrism3D);

// The cursor in render pixels, as of the last input_frame.
void input_cursor(int* x, int* y);
// 0 left, 1 right: held right now.
bool input_mouse_down(int button);
// 0 left, 1 right: button-down messages since the last call, so a click that
// goes down and up between two frames is still seen.
int input_press_take(int button);
// Wheel movement since the last call, in WHEEL_DELTA units (120 a notch).
int input_wheel_take(void);
// WM_MOUSEWHEEL deltas that arrived while the panel was closed (Scroll-wheel zoom's fallback), since the last call.
int input_play_wheel_take(void);
// Cumulative WM_MOUSEWHEEL messages by who owned input: [0] play, [1] panel, [2] freecam.
void input_wheel_counts(unsigned out[3]);
// The oldest queued key-down while the panel is open (the panel key excluded);
// false when the queue is empty. `repeat` is the auto-repeat bit.
// `mods` is INPUT_MOD_* as held when the message arrived.
enum { INPUT_MOD_SHIFT = 1, INPUT_MOD_CTRL = 2 };
bool input_key_take(int* vk, int* repeat, int* mods);
// The oldest queued WM_CHAR while the panel is open; false when empty.
bool input_char_take(unsigned* ch);

// 1 when GetCursorInfo reported the OS cursor hidden after the first open: the
// UI then draws AA's software cursor itself.
int input_software_cursor(void);

// The panel key, a virtual key; panel_key_effective (ui/menu.h) decides the
// fallback, so the status line and the toggle cannot name different keys.
void input_set_panel_key(int vk);

// Around a modal system dialog on this thread (the photo folder picker). Begin lets go of what we hold on the cursor:
// the hold, the freecam grab and capture, a clip, a hidden display count. End puts back what begin changed and
// drops the panel's buttons and keys, since the dialog took the button's release.
void input_modal_begin(void);
void input_modal_end(void);

// GetTickCount64 at the first WM_CLOSE, 0 when none arrived (docs/re/exit-hang.md).
unsigned long long input_close_tick(void);
