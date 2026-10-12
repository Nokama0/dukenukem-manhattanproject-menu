#pragma once
// "Also bound in the game" (docs/re/quick-wins.md section 5): whether a key row's VK is bound in the engine's own live
// tables. Pure mapping and verdict here; the reads are gamebind.cpp's.

// Triggers: 333 records of 0x240 bytes, inp_trigger(i) = 0x43d528 + i*0x240 (prism3d!0x407110), the key name first
// (inp_trigger_name 0x407120). A command bind: bind_get(name) 0x4063d0 answers its record; bind_del 0x406260 clears its
// used byte (bind_get skips it) and trigger+0x40. Impulse: bind_add_imp 0x406490 stores it at trigger+0x44+slot*4.
enum { GB_TRIGGERS = 333, GB_TRIGGER_SIZE = 0x240, GB_NAME_MAX = 0x40, GB_OFS_IMPULSE = 0x44, GB_IMPULSE_SLOTS = 4 };

// A VK and MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX) -> the trigger index (a DIK code for keys), -1 for none. Mouse codes
// are triggers mouse2..mouse5 (263-266; DInput mode reports right as mouse2, middle as mouse3). An E0 prefix is DIK's
// 0x80 bit. Pause and NumLock swap (Windows 0x45 / E0 45, DirectInput 0xC5 / 0x45); generic Shift/Ctrl/Alt map left.
inline int gb_trigger_for_vk(int vk, unsigned vscEx) {
  switch (vk) {
    case 0x02: return 263;   // VK_BIND_RBUTTON -> mouse2
    case 0x04: return 264;   // VK_BIND_MBUTTON -> mouse3
    case 0x05: return 265;   // VK_BIND_XBUTTON1 -> mouse4
    case 0x06: return 266;   // VK_BIND_XBUTTON2 -> mouse5
    case 0x13: return 0xC5;  // VK_PAUSE -> DIK_PAUSE
    case 0x90: return 0x45;  // VK_NUMLOCK -> DIK_NUMLOCK
    default: break;
  }
  if (vk <= 0 || vk > 0xFE) return -1;
  const unsigned sc = vscEx & 0xFFu, prefix = vscEx >> 8;
  if (sc == 0 || sc > 0x7F) return -1;
  if (prefix == 0xE0) return (int)(sc | 0x80u);
  return prefix == 0 ? (int)sc : -1;
}

// Bound when a command is bound to it or any of its four impulse slots holds an impulse.
inline bool gb_trigger_bound(bool commandBound, const unsigned impulses[GB_IMPULSE_SLOTS]) {
  if (commandBound) return true;
  for (int i = 0; i < GB_IMPULSE_SLOTS; ++i) if (impulses[i]) return true;
  return false;
}

// --- The glue (gamebind.cpp), main thread only -----------------------------------------------------
// At each panel open: forgets the last answers and checks the trigger table once (one guarded range).
void gamebind_begin_session(void);
// Whether the game also binds this VK, answered once per panel session. False when the tables are unread.
bool gamebind_vk_bound(int vk);
