#pragma once
// Keeps the panel's cvars from marking the save CHEATED (save-awards.md section 4). cheat_cvar_check duke!0x100e13d0
// runs every game tick and marks the session when a cvar with byte +0x6A set is off its default; around that one call
// the three cvars the panel writes read their defaults. The pure rules; cheatmark.cpp owns the detour.
#include <string.h>

// The flagged cvars the panel writes (First person, Custom FOV and the freecam wheel, Draw the whole map).
// g_p_god is flagged too but never written by the panel: it stays a cheat.
enum { CM_NCVARS = 3 };
static const char* const kCmCvars[CM_NCVARS] = { "g_fps", "g_cam_fov", "g_cam_far" };

// cvar_t offsets (input-console.md section 1): value pointer, default pointer, link, and their buffers.
enum { CM_OFS_DEFBUF = 0x20, CM_OFS_VALBUF = 0x72, CM_OFS_VALUE = 0xB4, CM_OFS_DEFAULT = 0xB8, CM_OFS_LINK = 0xBC,
       CM_CVAR_SIZE = 0xD0, CM_VALUE_CAP = 0x40 };

// A registered, unlinked cvar (cvar_register_variable prism3d!0x406660 sets both pointers into the record):
// the check reads the record's own +0xB4, and cvar_t::set writes there only when +0xBC is null.
inline int cm_layout_ok(const unsigned char* cv, const void* value, const void* def, const void* link) {
  return cv && value == cv + CM_OFS_VALBUF && def == cv + CM_OFS_DEFBUF && !link;
}
// Off its default as the check compares it (strcmp of +0xB4 against +0xB8).
inline int cm_should_restore(int layoutOk, const char* value, const char* def) {
  return layoutOk && value && def && strcmp(value, def) != 0;
}

// One cvar across one check: the value it had, put back after the check.
struct CmSlot { int restored; char saved[CM_VALUE_CAP]; };
// Before the check: 1 when the default must be set (and value is saved). cvar_t::set keeps 0x3F chars.
inline int cm_slot_begin(CmSlot* s, int layoutOk, const char* value, const char* def) {
  s->restored = 0;
  s->saved[0] = 0;
  if (!cm_should_restore(layoutOk, value, def)) return 0;
  strncpy_s(s->saved, sizeof s->saved, value, _TRUNCATE);
  s->restored = 1;
  return 1;
}
// cvar_t::set refused the default: nothing to put back.
inline void cm_slot_failed(CmSlot* s) { s->restored = 0; }
// After the check: the value to set again, or 0.
inline const char* cm_slot_end(CmSlot* s) {
  const int r = s->restored;
  s->restored = 0;
  return r ? s->saved : 0;
}

// cheat_cvar_check's entry through its first test of +0x6A (duke!0x100e13d0-0x100e140a); the cvar_get_list import
// and the game_globals pointer are masked (absolute operands). The head is 5 one-byte pushes, the trampoline's bytes.
enum { CM_FP_LEN = 59, CM_PROLOGUE = 5 };
static const unsigned char kCmCheckFp[CM_FP_LEN] = {
  0x51, 0x53, 0x55, 0x56, 0x57, 0x89, 0x4C, 0x24, 0x10, 0xFF, 0x15, 0xF0, 0x41, 0x18, 0x10, 0x8B, 0xF8, 0x85, 0xFF,
  0x74, 0x7C, 0x8B, 0x15, 0xDC, 0x69, 0x18, 0x10, 0x90, 0x8D, 0x64, 0x24, 0x00, 0x85, 0xD2, 0x74, 0x6D, 0x8B, 0x42,
  0x10, 0x8B, 0x48, 0x20, 0x8D, 0x6C, 0x01, 0x24, 0x80, 0x7D, 0x00, 0x00, 0x75, 0x5D, 0x8A, 0x47, 0x6A, 0x84, 0xC0,
  0x74, 0x4C };
static const unsigned char kCmCheckMask[CM_FP_LEN] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
  1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1 };

// --- cheatmark.cpp (DLL only; read, not run, by the suite) ---------------------------------------
// Frame path, every frame: arms the detour once duke_base and the three cvars are resolved, and writes the
// 5 s counter line.
void cheatmark_frame(void);
