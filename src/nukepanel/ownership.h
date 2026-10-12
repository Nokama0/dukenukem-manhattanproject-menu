#pragma once
// Who owns input: the game, the panel or the free camera. Pure, so the suite
// pins every edge; input.cpp turns the returned action bits into engine calls.

enum { EV_TOGGLE = 1, EV_ACTIVATE = 2, EV_FRAME = 3, EV_FREECAM = 4 };
enum { ACT_INPUT_OFF = 1, ACT_INPUT_ON = 2, ACT_RESET_IMPULSES = 4 };
enum { OWNER_GAME = 0, OWNER_PANEL = 1, OWNER_FREECAM = 2 };

struct OwnState { int open; int freecam; };

inline void own_init(OwnState* s) { s->open = 0; s->freecam = 0; }

// The panel wins over a flying camera: a player working the rows is not steering.
inline int own_owner(const OwnState* s) {
  if (s->open) return OWNER_PANEL;
  return s->freecam ? OWNER_FREECAM : OWNER_GAME;
}

// The engine turns input back on for three window messages (EV_ACTIVATE, fed only when activation_turns_input_on),
// so while the panel or freecam holds input each of those and every frame re-asserts OFF. Impulses reset when input
// is taken. A panel/freecam hand-over returns no action (a second inp_set_active(0) bumps mouse_state 1's count).
inline int own_event(OwnState* s, int ev) {
  const bool held = s->open || s->freecam;
  if (ev == EV_TOGGLE) s->open = !s->open;
  else if (ev == EV_FREECAM) s->freecam = !s->freecam;
  else if (ev == EV_ACTIVATE || ev == EV_FRAME) return held ? ACT_INPUT_OFF : 0;
  else return 0;
  const bool now = s->open || s->freecam;
  if (now && !held) return ACT_INPUT_OFF | ACT_RESET_IMPULSES;
  if (!now && held) return ACT_INPUT_ON;
  return 0;
}

// The Free camera switch (panel clean-up 2026-10-02): it arms the camera hotkeys and starts no flight.
inline bool freecam_hotkeys_armed(int switchOn) { return switchOn != 0; }
// Switched off while flying: the flight ends through the same toggle the Camera key drives.
inline bool freecam_switch_ends_flight(int switchOn, bool flying) { return !switchOn && flying; }

// Whether the engine console takes typing, so every hotkey of ours yields (bind.h hotkey_swallows). `console` is the
// shown console:system_01 window or null, `focused` ui_wfocused's window. A menu focused over the console takes the
// keys itself; nothing focused leaves the keys with the console.
inline bool console_takes_keys(const void* console, bool consoleVisible, const void* focused) {
  return console && consoleVisible && (focused == console || !focused);
}

// Whether prism_wndproc has just turned game input ON for this message: it tests
// the low word of wParam for WM_ACTIVATE (0x4263a3) and WM_ACTIVATEAPP (0x426444)
// and always turns input on for WM_EXITMENULOOP (0x426a14).
inline bool activation_turns_input_on(unsigned msg, unsigned wparam) {
  if (msg == 0x06 || msg == 0x1C) return (wparam & 0xFFFFu) != 0;
  return msg == 0x212;
}

// ShowCursor calls (+ show, - hide) that bring the display count back to the one recorded at open; capped at 64.
// inp_set_active(0) bumps it unguarded in mouse_state 1; a mouse_state change while open means the engine moved it on
// purpose. Mouse_state 1 only: in state 0 the count follows dinput8's exclusive-mouse latch and undoing it left -4.
inline int cursor_restore_steps(int open_count, int now_count, int open_state, int now_state) {
  if (open_state != now_state || now_state != 1) return 0;
  const long long d = (long long)open_count - now_count;
  return d > 64 ? 64 : (d < -64 ? -64 : (int)d);
}

// The cursor hold (docs/re/mouse-lock.md): with the engine mouse shared, nothing hides the OS cursor in play, so we
// hide it over the client and re-centre it every frame. It yields to everything that shows or uses the OS cursor;
// the engine's menus use a software cursor (inp_cursor_info, ui 0x423600), and prism3d's GetCursorPos (0x406e90).
struct CursorHoldIn { int window, foreground, iconic, panelOpen, freecam, engineCursor; };
enum { HOLD_OK = 0, HOLD_NO_WINDOW, HOLD_BACKGROUND, HOLD_ICONIC, HOLD_PANEL, HOLD_FREECAM, HOLD_ENGINE_CURSOR };
// The first condition that stops the hold, HOLD_OK when it holds. Focus first: the log names alt-tab as alt-tab.
inline int cursor_hold_block(const CursorHoldIn& in) {
  if (!in.window) return HOLD_NO_WINDOW;
  if (!in.foreground) return HOLD_BACKGROUND;
  if (in.iconic) return HOLD_ICONIC;
  if (in.panelOpen) return HOLD_PANEL;
  if (in.freecam) return HOLD_FREECAM;
  if (in.engineCursor) return HOLD_ENGINE_CURSOR;
  return HOLD_OK;
}
inline const char* cursor_hold_name(int block) {
  static const char* const kN[7] = { "held", "no window", "not in front", "minimised", "panel open", "freecam",
                                     "engine cursor (mouse_state 1)" };
  return block >= 0 && block < 7 ? kN[block] : "?";
}
// The centre of a rect (the client area, in screen or client pixels); false, outputs untouched, when it is empty.
inline bool cursor_hold_centre(long l, long t, long r, long b, long* x, long* y) {
  if (r <= l || b <= t) return false;
  *x = l + (r - l) / 2;
  *y = t + (b - t) / 2;
  return true;
}

// A client-area coordinate mapped to render pixels, clamped to [0, render-1].
// In windowed mode the client area and the render size differ either way.
inline int cursor_scale(int c, int client, int render) {
  if (client <= 0 || render <= 0) return 0;
  const long long v = (long long)c * render / client;
  if (v < 0) return 0;
  if (v > render - 1) return render - 1;
  return (int)v;
}
