#pragma once
// What the carried UI layer needs, and nothing else. NOT A CARRY, DELIBERATELY: Megaton's and World Tour's common.h
// is 2261 lines of engine internals, including a table of Megaton's addresses that is wrong for every other port;
// this port takes only the symbols the UI uses (clamp_int, clamp_ll_to_int in ui.h, menu.h; wallclock_ms in menu.h).

inline int clamp_int(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Saturate rather than wrap when narrowing 64-bit to int: callers widen to
// long long specifically so a hostile value cannot signed-overflow before this
// narrows it back.
inline int clamp_ll_to_int(long long v) {
  if (v >  2147483647LL)       return  2147483647;
  if (v < -2147483647LL - 1LL) return -2147483647 - 1;
  return (int)v;
}

// Truncate a double to int without UB: an out-of-range or NaN (int) cast is undefined, and canvas.cpp's transform
// runs this every vertex of every frame. `!(v > MIN)` catches NaN too. Verbatim from the other two ports, so there is
// one body and not two copies that drift apart.
inline int clamp_double_to_int(double v) {
  if (!(v > -2147483648.0)) return -2147483647 - 1;
  if (v > 2147483647.0)     return  2147483647;
  return (int)v;
}

// VK to SDL scancode, verbatim from the other two ports (EDuke32 is SDL too): it lets bind.h refuse a key it cannot
// express, so a bind row shows what the game will see. Pure for test_host. Absent on purpose: ESCAPE (cancel/close),
// modifiers, lock keys, PRINT SCREEN, numpad ENTER. SDL2 scancodes: A=4..Z=29, '1'=30..'9'=38, '0'=39.
inline bool vk_to_sdl_scancode(int vk, int* scancodeOut) {
  switch (vk) {
    case 0x2D: *scancodeOut = 73; return true;  // VK_INSERT           -> SDL_SCANCODE_INSERT
    case 0x2E: *scancodeOut = 76; return true;  // VK_DELETE           -> SDL_SCANCODE_DELETE
    case 0x24: *scancodeOut = 74; return true;  // VK_HOME             -> SDL_SCANCODE_HOME
    case 0x23: *scancodeOut = 77; return true;  // VK_END              -> SDL_SCANCODE_END
    case 0x21: *scancodeOut = 75; return true;  // VK_PRIOR (PageUp)   -> SDL_SCANCODE_PAGEUP
    case 0x22: *scancodeOut = 78; return true;  // VK_NEXT  (PageDown) -> SDL_SCANCODE_PAGEDOWN
    case 0x09: *scancodeOut = 43; return true;  // VK_TAB              -> SDL_SCANCODE_TAB
    case 0xC0: *scancodeOut = 53; return true;  // VK_OEM_3 (grave)    -> SDL_SCANCODE_GRAVE
    // T is the teleport default. V is the toggle default: the old F6 collided with Megaton's Quick_Save binding, so the
    // toggle also fired the game's save. V sits next to WASD and is unbound in both key tables. Kept out of the letter
    // run below since a switch may not name a case twice.
    case 0x54: *scancodeOut = 23; return true;  // 'T'                 -> SDL_SCANCODE_T
    case 0x56: *scancodeOut = 25; return true;  // 'V'                 -> SDL_SCANCODE_V
    case 0x70: *scancodeOut = 58; return true;  // VK_F1  -> SDL_SCANCODE_F1
    case 0x71: *scancodeOut = 59; return true;  // VK_F2  -> SDL_SCANCODE_F2
    case 0x72: *scancodeOut = 60; return true;  // VK_F3  -> SDL_SCANCODE_F3
    case 0x73: *scancodeOut = 61; return true;  // VK_F4  -> SDL_SCANCODE_F4
    case 0x74: *scancodeOut = 62; return true;  // VK_F5  -> SDL_SCANCODE_F5
    case 0x75: *scancodeOut = 63; return true;  // VK_F6  -> SDL_SCANCODE_F6
    case 0x76: *scancodeOut = 64; return true;  // VK_F7  -> SDL_SCANCODE_F7
    case 0x77: *scancodeOut = 65; return true;  // VK_F8  -> SDL_SCANCODE_F8
    case 0x78: *scancodeOut = 66; return true;  // VK_F9  -> SDL_SCANCODE_F9
    case 0x79: *scancodeOut = 67; return true;  // VK_F10 -> SDL_SCANCODE_F10
    case 0x7A: *scancodeOut = 68; return true;  // VK_F11 -> SDL_SCANCODE_F11
    case 0x7B: *scancodeOut = 69; return true;  // VK_F12 -> SDL_SCANCODE_F12
    // The letters, alphabetical in both spaces (SDL = VK - 0x41 + 4),
    // written case by case to match the rest of this table.
    case 0x41: *scancodeOut =  4; return true;  // 'A'                 -> SDL_SCANCODE_A
    case 0x42: *scancodeOut =  5; return true;  // 'B'                 -> SDL_SCANCODE_B
    case 0x43: *scancodeOut =  6; return true;  // 'C'                 -> SDL_SCANCODE_C
    case 0x44: *scancodeOut =  7; return true;  // 'D'                 -> SDL_SCANCODE_D
    case 0x45: *scancodeOut =  8; return true;  // 'E'                 -> SDL_SCANCODE_E
    case 0x46: *scancodeOut =  9; return true;  // 'F'                 -> SDL_SCANCODE_F
    case 0x47: *scancodeOut = 10; return true;  // 'G'                 -> SDL_SCANCODE_G
    case 0x48: *scancodeOut = 11; return true;  // 'H'                 -> SDL_SCANCODE_H
    case 0x49: *scancodeOut = 12; return true;  // 'I'                 -> SDL_SCANCODE_I
    case 0x4A: *scancodeOut = 13; return true;  // 'J'                 -> SDL_SCANCODE_J
    case 0x4B: *scancodeOut = 14; return true;  // 'K'                 -> SDL_SCANCODE_K
    case 0x4C: *scancodeOut = 15; return true;  // 'L'                 -> SDL_SCANCODE_L
    case 0x4D: *scancodeOut = 16; return true;  // 'M'                 -> SDL_SCANCODE_M
    case 0x4E: *scancodeOut = 17; return true;  // 'N'                 -> SDL_SCANCODE_N
    case 0x4F: *scancodeOut = 18; return true;  // 'O'                 -> SDL_SCANCODE_O
    case 0x50: *scancodeOut = 19; return true;  // 'P'                 -> SDL_SCANCODE_P
    case 0x51: *scancodeOut = 20; return true;  // 'Q'                 -> SDL_SCANCODE_Q
    case 0x52: *scancodeOut = 21; return true;  // 'R'                 -> SDL_SCANCODE_R
    case 0x53: *scancodeOut = 22; return true;  // 'S'                 -> SDL_SCANCODE_S
    case 0x55: *scancodeOut = 24; return true;  // 'U'                 -> SDL_SCANCODE_U
    case 0x57: *scancodeOut = 26; return true;  // 'W'                 -> SDL_SCANCODE_W
    case 0x58: *scancodeOut = 27; return true;  // 'X'                 -> SDL_SCANCODE_X
    case 0x59: *scancodeOut = 28; return true;  // 'Y'                 -> SDL_SCANCODE_Y
    case 0x5A: *scancodeOut = 29; return true;  // 'Z'                 -> SDL_SCANCODE_Z
    // THE DIGIT ROW, and its one trap: SDL numbers '1' FIRST and puts '0' after
    // '9', where the VK codes have 0x30 before 0x31.
    case 0x31: *scancodeOut = 30; return true;  // '1'                 -> SDL_SCANCODE_1
    case 0x32: *scancodeOut = 31; return true;  // '2'                 -> SDL_SCANCODE_2
    case 0x33: *scancodeOut = 32; return true;  // '3'                 -> SDL_SCANCODE_3
    case 0x34: *scancodeOut = 33; return true;  // '4'                 -> SDL_SCANCODE_4
    case 0x35: *scancodeOut = 34; return true;  // '5'                 -> SDL_SCANCODE_5
    case 0x36: *scancodeOut = 35; return true;  // '6'                 -> SDL_SCANCODE_6
    case 0x37: *scancodeOut = 36; return true;  // '7'                 -> SDL_SCANCODE_7
    case 0x38: *scancodeOut = 37; return true;  // '8'                 -> SDL_SCANCODE_8
    case 0x39: *scancodeOut = 38; return true;  // '9'                 -> SDL_SCANCODE_9
    case 0x30: *scancodeOut = 39; return true;  // '0'                 -> SDL_SCANCODE_0 (AFTER '9')
    // The three big keys of the main block.
    case 0x0D: *scancodeOut = 40; return true;  // VK_RETURN           -> SDL_SCANCODE_RETURN
    case 0x08: *scancodeOut = 42; return true;  // VK_BACK             -> SDL_SCANCODE_BACKSPACE
    case 0x20: *scancodeOut = 44; return true;  // VK_SPACE            -> SDL_SCANCODE_SPACE
    // The punctuation keys, in scancode order. These are the US layout's own
    // OEM codes, which is what Windows reports whatever the layout is: the VK
    // is the PHYSICAL key here, exactly as an SDL scancode is.
    case 0xBD: *scancodeOut = 45; return true;  // VK_OEM_MINUS  (-) -> SDL_SCANCODE_MINUS
    case 0xBB: *scancodeOut = 46; return true;  // VK_OEM_PLUS   (=) -> SDL_SCANCODE_EQUALS
    case 0xDB: *scancodeOut = 47; return true;  // VK_OEM_4      ([) -> SDL_SCANCODE_LEFTBRACKET
    case 0xDD: *scancodeOut = 48; return true;  // VK_OEM_6      (]) -> SDL_SCANCODE_RIGHTBRACKET
    case 0xDC: *scancodeOut = 49; return true;  // VK_OEM_5      (\) -> SDL_SCANCODE_BACKSLASH
    case 0xBA: *scancodeOut = 51; return true;  // VK_OEM_1      (;) -> SDL_SCANCODE_SEMICOLON
    case 0xDE: *scancodeOut = 52; return true;  // VK_OEM_7      (') -> SDL_SCANCODE_APOSTROPHE
    case 0xBC: *scancodeOut = 54; return true;  // VK_OEM_COMMA  (,) -> SDL_SCANCODE_COMMA
    case 0xBE: *scancodeOut = 55; return true;  // VK_OEM_PERIOD (.) -> SDL_SCANCODE_PERIOD
    case 0xBF: *scancodeOut = 56; return true;  // VK_OEM_2      (/) -> SDL_SCANCODE_SLASH
    case 0x13: *scancodeOut = 72; return true;  // VK_PAUSE            -> SDL_SCANCODE_PAUSE
    // The arrows, which are NOT in reading order in SDL: right comes first.
    case 0x27: *scancodeOut = 79; return true;  // VK_RIGHT -> SDL_SCANCODE_RIGHT
    case 0x25: *scancodeOut = 80; return true;  // VK_LEFT  -> SDL_SCANCODE_LEFT
    case 0x28: *scancodeOut = 81; return true;  // VK_DOWN  -> SDL_SCANCODE_DOWN
    case 0x26: *scancodeOut = 82; return true;  // VK_UP    -> SDL_SCANCODE_UP
    // The numeric pad. Its digits carry the same trap the main row does -- 1
    // first, 0 after 9 -- and its ENTER is absent because Windows has no
    // virtual key for it (it reports VK_RETURN, already mapped above).
    case 0x6F: *scancodeOut = 84; return true;  // VK_DIVIDE   -> SDL_SCANCODE_KP_DIVIDE
    case 0x6A: *scancodeOut = 85; return true;  // VK_MULTIPLY -> SDL_SCANCODE_KP_MULTIPLY
    case 0x6D: *scancodeOut = 86; return true;  // VK_SUBTRACT -> SDL_SCANCODE_KP_MINUS
    case 0x6B: *scancodeOut = 87; return true;  // VK_ADD      -> SDL_SCANCODE_KP_PLUS
    case 0x61: *scancodeOut = 89; return true;  // VK_NUMPAD1  -> SDL_SCANCODE_KP_1
    case 0x62: *scancodeOut = 90; return true;  // VK_NUMPAD2  -> SDL_SCANCODE_KP_2
    case 0x63: *scancodeOut = 91; return true;  // VK_NUMPAD3  -> SDL_SCANCODE_KP_3
    case 0x64: *scancodeOut = 92; return true;  // VK_NUMPAD4  -> SDL_SCANCODE_KP_4
    case 0x65: *scancodeOut = 93; return true;  // VK_NUMPAD5  -> SDL_SCANCODE_KP_5
    case 0x66: *scancodeOut = 94; return true;  // VK_NUMPAD6  -> SDL_SCANCODE_KP_6
    case 0x67: *scancodeOut = 95; return true;  // VK_NUMPAD7  -> SDL_SCANCODE_KP_7
    case 0x68: *scancodeOut = 96; return true;  // VK_NUMPAD8  -> SDL_SCANCODE_KP_8
    case 0x69: *scancodeOut = 97; return true;  // VK_NUMPAD9  -> SDL_SCANCODE_KP_9
    case 0x60: *scancodeOut = 98; return true;  // VK_NUMPAD0  -> SDL_SCANCODE_KP_0 (AFTER KP_9)
    case 0x6E: *scancodeOut = 99; return true;  // VK_DECIMAL  -> SDL_SCANCODE_KP_PERIOD
    default: return false;
  }
}

// The typed numeric field's wrapper (entry.h) for input.cpp's key router. vk_to_sdl_scancode excludes VK_ESCAPE (a
// different meaning for a bind row), but entry.h's machine needs SDL_SCANCODE_ESCAPE (41) to cancel an open field, so
// this adds the one case back. 0x1B is VK_ESCAPE as a literal: this file stays windows.h-free so the suite can include it.
inline bool vk_to_entry_scancode(int vk, int* scancodeOut) {
  if (vk == 0x1B) { if (scancodeOut) *scancodeOut = 41; return true; }  // VK_ESCAPE -> SDL_SCANCODE_ESCAPE
  return vk_to_sdl_scancode(vk, scancodeOut);
}

// Packs a row key and value into one 64-bit word so menu.cpp's s_rowCommit publishes/claims it with one Interlocked64
// and never separates them; rowKey == -1 packed means empty. `long long`, not LONG64, keeps this windows.h-free.
// Values widen through unsigned (sign extension would smear 1s into the other half) and the shift is unsigned too.
inline long long row_commit_pack(int rowKey, int value) {
  return (long long)(((unsigned long long)(unsigned int)rowKey << 32) |
                     (unsigned long long)(unsigned int)value);
}
inline int row_commit_key(long long w) {
  return (int)(unsigned int)((unsigned long long)w >> 32);
}
inline int row_commit_value(long long w) {
  return (int)(unsigned int)(w & 0xFFFFFFFFLL);
}

// wallclock_ms lives in wallclock.h, carried verbatim, because the panel
// includes that name directly. Declared in one place only.
