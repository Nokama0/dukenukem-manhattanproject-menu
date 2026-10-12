// From Megaton's src/duke3dmod/bind.h. DIVERGED in the enum below
// and nowhere else: this port grew rows Megaton does not have. Everything
// else is still that file line for line and is not to be edited here.
#pragma once
#include "common.h"   // vk_to_sdl_scancode, the one keyboard table this DLL has

// Every binding this panel owns is a WINDOWS VIRTUAL KEY, mouse buttons included: one capture/label/save/lookup for all

// The mouse codes are the REAL Windows ones, outside vk_to_sdl_scancode's table (0x01/02/04/05/06), so no keyboard code
// is also a mouse code (suite sweeps every byte). SDL numbers buttons 1 left/2 mid/3 right/4 X1/5 X2 but Windows has
// right 0x02, middle 0x04: a straight cast would swap them, hence bind_from_sdl_mouse/bind_mouse_sdl_button.
const int VK_BIND_LBUTTON = 0x01, VK_BIND_RBUTTON = 0x02;
const int VK_BIND_MBUTTON = 0x04, VK_BIND_XBUTTON1 = 0x05, VK_BIND_XBUTTON2 = 0x06;

// The rows, in config_bind_get/set's order (config.cpp). BIND_ROW_COUNT is load-bearing: everything taking a row
// rejects one outside 0..COUNT-1. APPENDED, NEVER INSERTED: the row indexes config.cpp's kBindFields, so a middle
// insert would silently move every binding after it to another key.
enum { BIND_ROW_MENU = 0, BIND_ROW_AIM, BIND_ROW_CAMERA, BIND_ROW_TELEPORT,
       BIND_ROW_CAMCOPY, BIND_ROW_CAMPASTE, BIND_ROW_HOLDZOOM, BIND_ROW_HOLDZOOMOUT,
       BIND_ROW_FC_FORWARD, BIND_ROW_FC_BACK, BIND_ROW_FC_LEFT, BIND_ROW_FC_RIGHT,
       BIND_ROW_FC_UP, BIND_ROW_FC_DOWN, BIND_ROW_FC_FASTER, BIND_ROW_FC_SLOWER,
       BIND_ROW_FP_CROUCH, BIND_ROW_INTERACT,   // First person / Move anywhere keys
       BIND_ROW_PHOTO,                          // Free camera > Photo key
       BIND_ROW_GAMESPEED,                      // Settings > Game speed key
       BIND_ROW_COUNT };

// Whether a binding is one of the mouse buttons this panel accepts. MOUSE 1 IS NOT ONE OF THEM: VK_BIND_LBUTTON still
// exists as a name (keycap_step refuses it, hooked_poll guards `btn != 1`, gamecfg_key_name names it since the game
// binds it to Fire), but nothing here treats it as a binding: it is the click that arms a capture.
inline bool bind_is_mouse(int vk) {
  // The suite sweeps every byte: accepted iff bind_mouse_sdl_button has a real button for it, holdable iff bind_label
  // names it.
  return vk == VK_BIND_RBUTTON || vk == VK_BIND_MBUTTON
      || vk == VK_BIND_XBUTTON1 || vk == VK_BIND_XBUTTON2;
}

// The SDL button number a mouse binding means, or -1 for anything that is not one (key, unbound 0, mouse 1, or 0x03
// VK_CANCEL, a gap between right/middle). VK_BIND_LBUTTON is -1 here too, so input_bind_held's mouse arm
// (input_mouse_button_down(bind_mouse_sdl_button(vk))) refuses it independently of bind_is_mouse.
inline int bind_mouse_sdl_button(int vk) {
  switch (vk) {
    case VK_BIND_MBUTTON:  return 2;   // SDL 2 is MIDDLE
    case VK_BIND_RBUTTON:  return 3;   // SDL 3 is RIGHT
    case VK_BIND_XBUTTON1: return 4;
    case VK_BIND_XBUTTON2: return 5;
    default:               return -1;
  }
}

// The exact inverse: the binding an SDL button number means, or 0 (unbound)
// for a button this DLL does not track. A button outside the range is DROPPED
// rather than clamped, matching hooked_poll's own recording.
inline int bind_from_sdl_mouse(int sdlButton) {
  switch (sdlButton) {
    // Button 1 produces no binding: neither the capture in hooked_poll nor
    // the legacy `aimbutton` migration can ever produce mouse 1.
    case 2: return VK_BIND_MBUTTON;
    case 3: return VK_BIND_RBUTTON;
    case 4: return VK_BIND_XBUTTON1;
    case 5: return VK_BIND_XBUTTON2;
    default: return 0;
  }
}

// The binding an SDL SCANCODE means, or 0 for one this DLL has no key for. A sweep over vk_to_sdl_scancode rather than
// a second table, so a key added to common.h and forgotten here cannot go unnoticed. Runs once per captured key press,
// never per frame.
inline int bind_from_sdl_scancode(int sc) {
  for (int vk = 0; vk <= 255; vk++) {
    int s = -1;
    if (vk_to_sdl_scancode(vk, &s) && s == sc) return vk;
  }
  return 0;
}

// Whether a binding matches an incoming keyboard event, computed now rather than resolved once at install, so a key
// rebound from the panel takes effect on the next press, not the next launch. Unbound (0) and mouse bindings both match
// no key.
inline bool bind_matches_key(int vk, int sdlScancode) {
  if (vk == 0 || bind_is_mouse(vk)) return false;
  int sc = -1;
  return vk_to_sdl_scancode(vk, &sc) && sc == sdlScancode;
}

// The same for a mouse button. An unbound 0 and a keyboard binding both match
// no button.
inline bool bind_matches_mouse(int vk, int sdlButton) {
  return bind_is_mouse(vk) && bind_mouse_sdl_button(vk) == sdlButton;
}

// The panel must stay reachable: a menukey with no bindable mouse button and no SDL scancode behind it falls back to
// Insert (config_load clamps menukey to 1..254, so `menukey=0` lands on VK_BIND_LBUTTON). Every OTHER binding is left
// unbound: an unreachable off-by-default feature is a nuisance, an unreachable panel a broken mod.
inline int bind_menu_key_effective(int vk) {
  int sc = -1;
  if (bind_is_mouse(vk) || vk_to_sdl_scancode(vk, &sc)) return vk;
  return 0x2D;   // VK_INSERT
}

// Whether a binding can actually be acted on: the ONE predicate the panel, input.cpp's swallows and a hand-edited ini
// all agree with, so a row cannot show "Unbound" while still eating a key. It is bind_menu_key_effective's own test,
// named and reused, minus that function's Insert fallback: every OTHER row is left unbound instead of substituted.
inline bool bind_key_is_live(int vk) {
  int sc = -1;
  return bind_is_mouse(vk) || vk_to_sdl_scancode(vk, &sc);
}

// The aimbot's hold gate, pulled out of aim.cpp so the rule can be pinned: a hold row that is not live never reports
// held, whatever the OS key state says (aim.cpp once compared the vk to 0, so an unmappable key drew "Unbound" while
// GetAsyncKeyState still fired it).
inline bool bind_hold_is_active(int vk, bool keyDown) {
  return bind_key_is_live(vk) && keyDown;
}

// ek_WndProc's swallow test, pulled out so it can be pinned. The "window" a bind is swallowed for: the toggle's own row
// and camera_available(), the other three's camera_owns_input(); neither gate can run in the suite, so the caller reads
// it and hands in the answer.
inline bool bind_swallows(int pressedVk, int boundVk, bool windowOwned) {
  return windowOwned && bind_key_is_live(boundVk) && pressedVk == boundVk;
}

// While EDuke32's console takes typing, every hotkey of ours yields: the console reads SDL_KEYDOWN, so a swallowed key
// never reaches it. Not while the panel is open -- it eats every other key, so a yielded panel key could never close
// it.
inline bool hotkey_yields_to_console(bool consoleCapturing, bool panelOpen) {
  return consoleCapturing && !panelOpen;
}

// The ONE test all five ek_WndProc hotkey sites call, so the console term
// cannot reach one key and miss another.
inline bool hotkey_swallows(int pressedVk, int boundVk, bool windowOwned,
                            bool consoleCapturing, bool panelOpen) {
  return !hotkey_yields_to_console(consoleCapturing, panelOpen) &&
         bind_swallows(pressedVk, boundVk, windowOwned);
}

// The name of a keyboard virtual key for bind_label. "Unbound" for exactly the codes vk_to_sdl_scancode rejects:
// input.cpp refuses to bind those, so a hand-edited ini holding e.g. 0x10 VK_SHIFT leaves the row keyless, and a
// key name over a dead key would mislead.
inline const char* freecam_key_label(int vk) {
  switch (vk) {
    case 0x2D: return "Insert";
    case 0x2E: return "Delete";
    case 0x24: return "Home";
    case 0x23: return "End";
    case 0x21: return "Page up";
    case 0x22: return "Page down";
    case 0x09: return "Tab";
    case 0xC0: return "Grave";
    // The two defaults: T is the teleport key's, V the camera toggle's
    // (replacing the old F6 default). The letter run below skips both, since
    // a switch may not name a case twice.
    case 0x54: return "T";
    case 0x56: return "V";
    case 0x70: return "F1";
    case 0x71: return "F2";
    case 0x72: return "F3";
    case 0x73: return "F4";
    case 0x74: return "F5";
    case 0x75: return "F6";
    case 0x76: return "F7";
    case 0x77: return "F8";
    case 0x78: return "F9";
    case 0x79: return "F10";
    case 0x7A: return "F11";
    case 0x7B: return "F12";
    // One short name per code (field is sized once from BIND_LABEL_WIDEST below; suite sweeps every code against that
    // width). Sentence case, like all copy this panel draws. Punctuation keys are named by the US-layout character
    // printed on the key; numeric pad is prefixed "Num " so its 0 and "." are never confused with the main row's.
    case 0x41: return "A";
    case 0x42: return "B";
    case 0x43: return "C";
    case 0x44: return "D";
    case 0x45: return "E";
    case 0x46: return "F";
    case 0x47: return "G";
    case 0x48: return "H";
    case 0x49: return "I";
    case 0x4A: return "J";
    case 0x4B: return "K";
    case 0x4C: return "L";
    case 0x4D: return "M";
    case 0x4E: return "N";
    case 0x4F: return "O";
    case 0x50: return "P";
    case 0x51: return "Q";
    case 0x52: return "R";
    case 0x53: return "S";
    case 0x55: return "U";
    case 0x57: return "W";
    case 0x58: return "X";
    case 0x59: return "Y";
    case 0x5A: return "Z";
    case 0x30: return "0";
    case 0x31: return "1";
    case 0x32: return "2";
    case 0x33: return "3";
    case 0x34: return "4";
    case 0x35: return "5";
    case 0x36: return "6";
    case 0x37: return "7";
    case 0x38: return "8";
    case 0x39: return "9";
    case 0x0D: return "Enter";
    case 0x08: return "Backspace";
    case 0x20: return "Space";
    case 0xBD: return "-";
    case 0xBB: return "=";
    case 0xDB: return "[";
    case 0xDD: return "]";
    case 0xDC: return "\\";
    case 0xBA: return ";";
    case 0xDE: return "'";
    case 0xBC: return ",";
    case 0xBE: return ".";
    case 0xBF: return "/";
    case 0x13: return "Pause";
    case 0x25: return "Left";
    case 0x27: return "Right";
    case 0x26: return "Up";
    case 0x28: return "Down";
    case 0x6F: return "Num /";
    case 0x6A: return "Num *";
    case 0x6D: return "Num -";
    case 0x6B: return "Num +";
    case 0x60: return "Num 0";
    case 0x61: return "Num 1";
    case 0x62: return "Num 2";
    case 0x63: return "Num 3";
    case 0x64: return "Num 4";
    case 0x65: return "Num 5";
    case 0x66: return "Num 6";
    case 0x67: return "Num 7";
    case 0x68: return "Num 8";
    case 0x69: return "Num 9";
    case 0x6E: return "Num .";
    default:   return "Unbound";
  }
}

// What a binding row shows. The mouse half is here; the keyboard half is
// freecam_key_label above. VK_BIND_LBUTTON deliberately falls through to
// "Unbound": mouse 1 is the click that arms a capture, never a binding.
inline const char* bind_label(int vk) {
  switch (vk) {
    case VK_BIND_RBUTTON:  return "Mouse 2";
    case VK_BIND_MBUTTON:  return "Mouse 3";
    case VK_BIND_XBUTTON1: return "Mouse 4";
    case VK_BIND_XBUTTON2: return "Mouse 5";
    default:               return freecam_key_label(vk);
  }
}

// The widest name bind_label can ever return ("Page down", "Backspace" ties it): what a bind row's field must hold.
// Named here rather than measured per draw; the suite sweeps every vk and mouse code against this width. FONT_MONO is
// one advance per character, so strlen is a fair measure.
const char* const BIND_LABEL_WIDEST = "Page down";

// What a bind field says, three states by precedence: ARMED wins over HOVERED (a capture in progress must keep saying
// so wherever the cursor drifts), hovered shows the invitation, otherwise the key's own name. The two strings are
// parameters since this header is pure and menu.h owns the copy.
inline const char* bind_field_text(bool armed, bool hovered, int vk,
                                   const char* prompt, const char* hoverText) {
  if (armed) return prompt;
  if (hovered) return hoverText;
  return bind_label(vk);
}

// --- the capture state machine: one row at a time is "in capture": clicked, waiting for the key or button to bind.
// keycap_step is the whole decision, pure, driven by the suite through every transition (menu.cpp's row helper and
// input.cpp's poll hook cannot both link into the suite).
enum { KEYCAP_IDLE = 0, KEYCAP_ARMED = 1 };
enum { KEYCAP_EV_NONE = 0, KEYCAP_EV_CLICK, KEYCAP_EV_KEY,
       KEYCAP_EV_MOUSE, KEYCAP_EV_ESCAPE, KEYCAP_EV_CLOSE };
struct KeyCapState  { int phase; int row; };
struct KeyCapEvent  { int kind; int row; int vk; };
// `consumed` says whether the PANEL and the game must NOT also act on the same event. It is a statement about the
// event, not the state: a bind and a cancel are consumed (the key must not also reach the toggle, the game, or the
// panel's own close), everything else is not.
struct KeyCapResult { KeyCapState state; int boundRow; int boundVk; bool consumed; };

inline KeyCapResult keycap_result(KeyCapState s, int boundRow, int boundVk, bool consumed) {
  KeyCapResult r;
  r.state = s; r.boundRow = boundRow; r.boundVk = boundVk; r.consumed = consumed;
  return r;
}

// Every arm returns an explicit result, no fallthrough default, so a
// transition added without deciding what it binds is a compile-visible gap.
inline KeyCapResult keycap_step(KeyCapState s, KeyCapEvent e) {
  const KeyCapState idle = { KEYCAP_IDLE, -1 };
  switch (e.kind) {
    case KEYCAP_EV_CLICK: {
      // Out-of-range row refused, not armed (would index off the binding
      // table). Clicking another row while armed MOVES the arm.
      if (e.row < 0 || e.row >= BIND_ROW_COUNT) return keycap_result(s, -1, 0, false);
      const KeyCapState armed = { KEYCAP_ARMED, e.row };
      return keycap_result(armed, -1, 0, false);   // not consumed: the panel's own click
    }
    case KEYCAP_EV_KEY:
    case KEYCAP_EV_MOUSE:
      if (s.phase != KEYCAP_ARMED) return keycap_result(s, -1, 0, false);
      // 0 would silently unbind the row; mouse 1 is the button that armed it.
      // Both leave the capture armed, waiting for a real key.
      if (e.vk == 0 || e.vk == VK_BIND_LBUTTON) return keycap_result(s, -1, 0, false);
      return keycap_result(idle, s.row, e.vk, true);
    case KEYCAP_EV_ESCAPE:
      // Cancels, keeps the old binding. Consumed while armed (or the press
      // would also close the panel); not consumed while idle, where it IS
      // the panel's own close key.
      if (s.phase != KEYCAP_ARMED) return keycap_result(s, -1, 0, false);
      return keycap_result(idle, -1, 0, true);
    case KEYCAP_EV_CLOSE:
      // Disarms whatever was armed. Never consumed: an armed capture left
      // across a close would eat the first key of whatever the player did next.
      return keycap_result(idle, -1, 0, false);
    case KEYCAP_EV_NONE:
    default:
      return keycap_result(s, -1, 0, false);
  }
}
