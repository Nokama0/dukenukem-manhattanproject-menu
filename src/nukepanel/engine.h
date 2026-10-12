#pragma once
// prism3d.exe exports, resolved by name at attach. Every convention below was
// read from the entry disassembly (prism3d.exe, Enhanced Edition);
// all are cdecl with plain RET unless noted. Addresses are for the log only.
#include <stddef.h>

typedef struct c_item_t c_item_t;   // engine resource handle (shader, font, ...)

// cvar_t layout (input-console.md section 1, 0xD0 bytes). Only the fields
// zoom.cpp's read/write pattern needs; the rest lives in the doc.
enum { CVAR_OFS_PTR_MODE = 0x69, CVAR_OFS_VALUE = 0xB4, CVAR_OFS_DEFAULT = 0xB8, CVAR_OFS_LINK = 0xBC };
typedef struct cvar_t cvar_t;

// vfs_open's flags: the CRT's _O_* values, masked 0x70b before the osfs driver's _open (0x404ab7).
enum { VFS_O_RDONLY = 0x0, VFS_O_WRONLY = 0x1, VFS_O_CREAT = 0x100, VFS_O_TRUNC = 0x200, VFS_MODE_RW = 0x1b6 };

struct Engine {
  // 0x41fa70 ()  tail-jumps device vtable +0xdc (EndScene); detoured, not called
  void (*rend_end_frame)(void);
  // 0x41fa60 ()  `mov ecx,[0x4a0938]; mov eax,[ecx]; jmp [eax+0xd8]` (BeginScene); detoured, not called.
  // Reached by the subsys tail jump 0x425bf0 after inp_reset_impulses and inp_process, before the
  // input handler reads impulses (aim.md section 5); rend_restart 0x41f3dd also calls it.
  void (*rend_begin_frame)(void);
  // 0x406aa0 (int impulse, int channel, float v)  cdecl, plain RET; thiscall 0x4068f0 on slot
  // 0x480d98 + impulse*0x54: adds v to the channel's value, capped at its max (99999.0, not 1).
  void (*inp_impulse_set)(int impulse, int channel, float v);
  // 0x406a50 (int impulse)  cdecl, plain RET; EAX = the slot 0x480d98 + impulse*0x54 (+0x50 bind count,
  // which gates inp_reset_impulses' clear; slot init 0x4068c0).
  unsigned char* (*inp_impulse_get)(int impulse);
  // 0x40d9b0 ()  EAX = the collision-history list 0x497878: +4 first node; node +4 next, +8 record
  // (+0 object, +0x14 hit point). Walk read at proj_bullet_hitscan_ray duke!0x1003ab33-0x1003ac8b.
  void* (*phys_collide_history)(void);
  // 0x4206d0 (const float* view, const float* inv)  pushes both 64-byte matrices
  void (*rend_camera_push)(const float* view, const float* inv);
  // 0x41fb50 (int type)  2 = ortho -1..1 y up, 1 = perspective; device vtable +0xc0
  void (*rend_set_projection_type)(int type);
  // 0x420420 / 0x420580 / 0x4204e0 ()  modelview + inverse stack push / identity / pop
  void (*rend_push)(void);
  void (*rend_identity)(void);
  void (*rend_pop)(void);
  // 0x41f910 (argb, const float a[3], const float b[3])  immediate, device vtable +0x94
  void (*rend_line_plain)(unsigned argb, const float* a, const float* b);
  // 0x41d230 (shader, rect{xl,xr,ytop,ybot}, argb)  queued via rend_sub_bitmap; entry read only, no call site exists
  void (*rend_bitmap)(c_item_t* shader, const float* rect, unsigned argb);
  // 0x41c620 (name)  thiscall 0x413810(name,0,0) on the resource manager; EAX = c_item_t* or 0
  c_item_t* (*mm_get_resource)(const char* name);
  // 0x416000 (float refW, float refH)  reference size; W,H of rend_get_params = pixel-sized glyphs
  void (*pml_set_ref)(float refW, float refH);
  // 0x416020 (const float rect[4])  {xl, xr, ytop, ybot} in the current projection's units
  void (*pml_set_bounds)(const float* rect);
  // 0x416fe0 (const char* markup)  lays text out into the global pml buffer
  void (*pml_render)(const char* markup);
  // 0x4190c0 ()  draws + clears the global buffer (FUN_00417e20); entry read only, no call site exists
  void (*pml_flush)(void);
  // 0x419110 (pml_buffer_t* buf, float x, float y)  draws a caller-owned buffer; unused (no buffer of ours)
  void (*pml_buffer_flush)(void* buf, float x, float y);
  // 0x41cd80 (uint desc[0x14], pool)  cdecl, desc at [esp+0x1c] after sub esp,0xc + 3 pushes; queues one
  // r_cache packet (render.md "2D primitive submission"). Caller FUN_00417e20 @0x418bb2.
  void (*rend_cache_data)(const unsigned* desc, void* pool);
  // 0x41f480 (const char* name)  cdecl, EAX = pool object or 0; caller FUN_00417e20 @0x418b9c ("translucent")
  void* (*rend_get_pool)(const char* name);
  // 0x41dd70 / 0x41dd80 ()  submit queued protos / reset props
  void (*rend_cache_flush)(void);
  void (*rend_props_flush)(void);
  // 0x41ebd0 (int* w, int* h, int* bpp)  each pointer may be NULL
  void (*rend_get_params)(int* w, int* h, int* bpp);
  // 0x420840 ()  returns the top of the view-matrix stack (16 floats)
  float* (*rend_camera_matrix)(void);
  // 0x4200f0 (float out[16])  one argument despite the decompiler ([esp+0x28] after a 0x24 frame)
  void (*rend_get_proj_matrix)(float* out);
  // 0x41f9a0 (void* buf, const float rect[4])  cdecl: thiscall device vtable +0x90; gl's (gl!0x100032b0) is
  // glReadPixels then hbmp_flip, rows top first (photo.md section 1). Call site: the screen capture 0x41fbfc, {0, W, H,
  // 0}, after rend_end_frame 0x425c73. RETURNS ZEROS ON gl (GLDirect over dxwrapper): save thumbnails there are zero.
  void (*rend_get_image)(void* buf, const float* rect);
  // 0x420350 (float* near, float* far, float* fov)  copies the projection-stack top
  void (*rend_projection_get)(float* n, float* f, float* fov);
  // 0x4202d0 (float near, float far, float fov)  cdecl, plain RET via its tail jump: writes the projection-stack top
  // (0x46efe0 +0/+4/+8), tells the device (0x41ec40), then tail-jumps rend_frustum_changed 0x4200e0. Call site
  // actor_camera_build_camera_matrices duke!0x1008c5d0 (push fov, far, near; add esp,0xc).
  void (*rend_projection_change)(float n, float f, float fov);
  // 0x4207a0 (float out[3])  arg at [esp+8] after push ebx, plain RET; the origin through the
  // inverse-stack top, recomputed when rend_camera_push has set the dirty byte 0x4a1a8d
  void (*rend_get_camera)(float* out);
  // 0x4222b0 (const char* fmt, ...)  cdecl varargs, vsnprintf into 1 KB then starts the search
  void (*spawn_search)(const char* fmt, ...);
  // 0x4222f0 ()  thiscall tail on the spawner global; EAX = next entity or 0
  void* (*spawn_search_get)(void);
  // 0x422300 ()  pops the search slot
  void (*spawn_search_stop)(void);
  // 0x423fd0 (int index)  EAX = slot[index].+0x10 when the slot is live, else 0
  void* (*wm_get_input)(int index);
  // 0x423f30 (int index)  cdecl, EAX = slot[index].+0x8 (the view object set_view_entity stores) when live, else 0
  void* (*wm_get_object)(int index);
  // 0x407d60 (int active)  low byte -> 0x43cf71; (un)acquires every device, clears trigger downs. With mouse_state 1 it
  // also runs 0x406e90 (SetCapture, ShowCursor(0)) or 0x406f00 (SetCursorPos, ShowCursor(1)), unguarded. Caller
  // prism_wndproc @0x4263af (push 1). duke_base, p3shared, client and input.dll resolve it; none reads the slot.
  void (*inp_set_active)(int active);
  // 0x406980 ()  clears the 255 impulses (thiscall 0x406930 each); caller engine_input_frame_update @0x425bd3
  void (*inp_reset_impulses)(void);
  // 0x406f30 ()  EAX = mouse_state (0x48ac74), 1 = captured free-camera/pause look; caller @0x4060d6
  int (*inp_get_mouse_state)(void);
  // 0x406510 (const char* name)  cdecl, one arg at [esp+0xc] on entry, caller-cleaned RET; walks the
  // cvar list (0x480d90), EAX = cvar_t* or 0. Call site FUN_00424c90@0x424d54.
  cvar_t* (*cvar_get)(const char* name);
  // 0x41a610 (const char* line)  cdecl, one arg at [esp+0x1c04-ish local frame], caller-cleaned RET
  // (ADD ESP,0x1a04; RET with no imm16), AL = bool. Call site bind_execute@0x40650b.
  bool (*cmd_execute)(const char* line);
  // 0x405fc0 ()  push esi; call 0x405f50 (config.cfg write); signal(1) every process; plain RET. Not called:
  // its first call's target is detoured (canvas_quit_install). Callers: duke_base game_quit_teardown, host_main
  // 0x42510a (the "can't initialize gfx subsystem" exit) -- quit paths only (quit-path.md section 1).
  void (*process_delete_all)(void);
  // 0x41ca60 (c_item_t* item)  cdecl, arg at [esp+8] after push esi, plain RET; dec [item+0x44], at 0 thiscall
  // 0x413080 (frees only an unloaded or flag-0x10 item; else the unreferenced sweep does), EAX = refs left.
  int (*ci_dec_refs)(c_item_t* item);
  // 0x4198e0 (const char* cls, const char* name, int visibleOnly)  cdecl, plain RET; thiscall 0x423770 on
  // the UI manager [0x4a1e48]. visibleOnly scans only the shown list (+0x3e0); cls matches window+0x1e8,
  // name window+4 (the `ui` command 0x422740 passes "console:system_01" as name, cls). EAX = window or 0.
  void* (*ui_wget)(const char* cls, const char* name, int visibleOnly);
  // 0x4198d0 ()  EAX = [[0x4a1e48]+0x41c], the focused top-level window (hide 0x423430 moves it on).
  void* (*ui_wfocused)(void);
  // 0x419850 (void* window)  cdecl, plain RET: null-checked, then thiscall 0x423430 on the UI manager [0x4a1e48].
  // Call site duke!0x100e7d5f (letterbox start: `push game_view; call [ui_whide]`).
  void (*ui_whide)(void* window);
  // 0x419870 (void* window, int flag)  cdecl, plain RET: null-checked, then thiscall 0x423600(window, flag) on the
  // UI manager. Call site duke!0x100e7ddf (letterbox end: `push 0; push game_view; call [ui_wshow]; add esp,8`).
  void (*ui_wshow)(void* window, int flag);
  // 0x40d820 (world, from, to, block, touch)  cdecl, plain RET; 0 when world or world+0x4B8 is null, else the
  // first collision-history record or 0. Caller proj_bullet_hitscan_ray duke!0x1003ab0f (add esp,0x14).
  void* (*phys_ray_test)(void* world, const float* from, const float* to, void* block, void* touch);
  // 0x40d750 (world, from, to, const float half[3], block, touch)  cdecl, plain RET; sweeps a box of half-extents
  // `half` from -> to (phys_sweep_vs_objects 0x40eb00 param 3). Caller game_track_place_camera duke!0x1009705c.
  void* (*phys_bound_test)(void* world, const float* from, const float* to, const float* half, void* block,
                           void* touch);
  // 0x40d8e0 (world, from, to, tmatcher_t* block)  cdecl, plain RET (block at [esp+0x18] after push ebx/esi);
  // AL = 1 when nothing blocked. A NULL world or world+0x4B8 also returns 1, so callers check the world first.
  // (aim.md section 4).
  bool (*phys_ray_visible)(void* world, const float* from, const float* to, void* block);
  // 0x41ff80 (const float c[3], float r)  cdecl, plain RET, AL = 1 visible / 0 culled; detoured, not called.
  // Caller map_draw_vtree_node duke!0x100ee834 (add esp,8; test al).
  bool (*rend_sphere_visible)(const float* c, float r);
  // 0x420080 (c, r, float maxDist)  cdecl, plain RET, AL; tail-calls rend_sphere_visible at 0x4200d0.
  // Caller map_draw_geometry duke!0x100eea58 (add esp,0xc; test al). Detoured, not called.
  bool (*rend_sphere_visible_dist)(const float* c, float r, float maxDist);
  // 0x41ffe0 (const float corners[8][3])  cdecl, plain RET, AL; no duke callers. Detoured, not called.
  bool (*rend_box_visible)(const float* corners);
  // 0x41f200 ()  cdecl, no args (sub esp,0x2c; ends `jmp rend_begin_frame`). Re-reads r_device, r_renderer,
  // r_fullscreen, r_mode; calls rend_end_frame, mode_shutdown/mode_init (apply_window_style), then begins a frame, so
  // the caller must be inside one. Call site duke!0x100a8456 (video menu): `call [0x10183c74]` after cvar_t::set.
  void (*rend_restart)(void);
  // 0x41bdb0 ()  `mov eax,0x49c7a0; ret`: the static render context; +4 (0x49c7a4) is the game window,
  // which hw_set_new_context 0x41bd60 replaces (destroying the old one when it is set to 0).
  void* (*hw_get_context)(void);
  // 0x403b80 (const char* path, unsigned flags, unsigned mode)  cdecl; EAX = handle or 0. THE NAME IS LOOKED UP IN AN
  // IN-MEMORY TREE ONLY, filled once at mount: a file written to disk later is not found (-2) until the next launch.
  // VFS_O_CREAT (0x100) is the one way in; flags are the CRT's _O_*, duke's savegame writer passes 1 then 0x101.
  void* (*vfs_open)(const char* path, unsigned flags, unsigned mode);
  // 0x403ea0 (handle, const void* buf, unsigned n)  cdecl, plain RET; the driver's +0x60 write (osfs 0x404b80,
  // _write), EAX = bytes or -1. Call site config writer 0x407f48 (push n; push buf; push h).
  int (*vfs_write)(void* handle, const void* buf, unsigned n);
  // 0x403e60 (handle, void* buf, unsigned n)  cdecl, plain RET; the driver's +0x5c read, EAX = bytes or -1.
  int (*vfs_read)(void* handle, void* buf, unsigned n);
  // 0x403e20 (handle)  cdecl, plain RET; drops a reference, the driver's +0x58 close at zero; EAX 0 or -1.
  int (*vfs_close)(void* handle);
  // 0x404090 (handle)  cdecl, plain RET; EAX = [[[h+4]+4]+4], the mounted device's name (a static "" for 0).
  const char* (*vfs_get_device_name)(void* handle);
  // 0x407110 (int index)  cdecl, plain RET: `lea eax,[i+i*8]; shl eax,6; add eax,0x43d528`, the trigger record (0x240
  // bytes, key name first). Caller bind_del_impulsealias_rng 0x406336 (inp_trigger(0) as the base).
  unsigned char* (*inp_trigger)(int index);
  // 0x4063d0 (const char* keyName)  cdecl, one arg at [esp+0xc] after push esi/edi, plain RET; EAX = the used
  // 0xa8-byte bind record (0x476590..0x480d90) whose +1 key matches (0x42c3c0), else 0. Call site bind_add_cmd
  // 0x406427 (push edi; call; add esp,4).
  void* (*bind_get)(const char* keyName);
  // 0x407e20 (float* x, float* y)  cdecl, plain RET: copies 0x46155c/0x46179c, the software cursor in NDC (-1..1,
  // y up) that inp_cursor_set normalises from the summed DirectInput deltas (mouse-aim.md section 4). Call site
  // FUN_00423600 @0x4236b9 (add esp,8).
  void (*inp_cursor_info)(float* x, float* y);
  // 0x405e80 / 0x405ea0 (vprocess_t* p)  cdecl, plain RET. sleep: `+0x14 = 1` when 0; wakeup: when 1, `+0x14 = 0`,
  // +0x28 = sys_get_timedelta, +0x38/+0x3C = 0 (no catch-up); main_loop_iteration 0x406001 skips update and think while
  // +0x14 != 0. duke's pause FUN_100e4a20 calls them through pfn 0x101841cc / 0x10183f88 (freecam-pause.md).
  void (*process_sleep)(void* process);
  void (*process_wakeup)(void* process);
  // 0x406180 (int pid)  cdecl, plain RET: walks the live process list 0x47646c, EAX = the process whose +0x20 is
  // pid, else 0. (freecam-pause.md: the liveness check before any sleep or wake).
  void* (*process_pid_get)(int pid);
  // 0x402b60 (const char* name, void* def, int* sizeOut)  cdecl, plain RET: walks the sys var registry 0x476448 by
  // name hash and name, EAX = the entry's pointer, else def (and *sizeOut = -2). duke_base registers its 0xB4-byte
  // game_globals object under "game_globals" (DLL_register 0x100de517: push obj; push name; call).
  void* (*sys_get_var)(const char* name, void* def, int* sizeOut);
};

// p3shared.dll's tmatcher_t exports, all thiscall (ECX = the matcher). The
// camera's own use is game_track_place_camera duke!0x10096f5e-0x10096fec and its dtor at 0x1009710a.
struct P3Shared {
  // 0x100268c0 ??0tmatcher_t@@QAE@XZ  plain RET, writes +0x00..+0x20: 0x24 bytes (the camera's slot is 0x30)
  void (__thiscall* tm_ctor)(void* self);
  // 0x100269f0 ??1tmatcher_t@@QAE@XZ  plain RET; reset_rules(1), which frees the rule nodes
  void (__thiscall* tm_dtor)(void* self);
  // 0x10026a30 ?reset_rules@tmatcher_t@@QAEX_N@Z  RET 4
  void (__thiscall* tm_reset_rules)(void* self, bool b);
  // 0x10026b10 / 0x10026b90 ?accept_superset / ?deny_superset (uint, uint, bool)  RET 0xC
  void (__thiscall* tm_accept_superset)(void* self, unsigned type, unsigned user, bool b);
  void (__thiscall* tm_deny_superset)(void* self, unsigned type, unsigned user, bool b);
  // 0x10026c10 ?deny_ptr (entity_base_t*)  RET 4; appends a pointer rule (the bullet's owner filter, aim.md 4)
  void (__thiscall* tm_deny_ptr)(void* self, const void* ent);
  // 0x1000a7d0 ?set@cvar_t@@QAE_NPBD@Z  RET 4, AL = 0 only for a read-only cvar; follows the +0xBC link, runs the +0x64
  // validator (a refusal still returns 1), copies into +0x72. Call site duke!0x100a8450 (push value; mov ecx,cvar; call
  // [import]), right before its rend_restart. Resolved alone by engine_resolve_cvar_set.
  bool (__thiscall* cvar_set)(cvar_t* self, const char* value);
  // 0x1001beb0 ?set@pin_t@@QAEXXZ  `mov dword [ecx+0x54],1; ret`: raises the pin's flag only. Call site
  // pin_t::receive 0x1001bea5 (ECX = the pin, nothing pushed). Resolved alone by engine_resolve_pin_set.
  // (cutaway.md section 4).
  void (__thiscall* pin_set)(void* self);
};

// skmod.dll's animation clock: weapon anim ends (+0xB2C) and timed events (+0xB98) are on it
// (weapons.md section 1).
struct Skmod {
  // skmod!0x100024b0 ()  cdecl, no args, plain RET, double in ST0 (FLD double). duke_base stores its
  // GetProcAddress in [0x1018418c] (0x100e010b); call site weap_schedule_event 0x10050f6e pushes
  // nothing, pops nothing and FADDs ST0.
  double (*skmod_get_time)(void);
  // skmod!0x100049c0 (handle, const char* name)  cdecl, plain RET; EAX = index or -1, and a console error on a miss
  // (0x10004a32). skmod!0x10004bc0 (handle, int idx, float m[16])  cdecl, plain RET, AL = ok; m is the model-space bone
  // matrix, row-vector, translation m[12..14]. Call site duke!0x10088408-0x1008842d (aim.md section 3).
  int (*skmod_get_bone_index)(void* handle, const char* name);
  bool (*skmod_get_bone_transform)(void* handle, int idx, float* m);
  // The pose API player_spine_bend duke!0x1007c7f0 calls, all cdecl with a plain RET, entries read in skmod.dll
  // (mouse-aim.md): open_pose 0x10004560 (int bones) EAX = pose; set_pose_bone_name 0x100045f0 (pose, int idx, name);
  // copy_pose_bones 0x10004670 (pose, model); add_pose_animation 0x10004810 (model, pose, double) EAX = layer.
  void* (*skmod_open_pose)(int bones);
  void (*skmod_set_pose_bone_name)(void* pose, int idx, const char* name);
  void (*skmod_copy_pose_bones)(void* pose, void* model);
  void* (*skmod_add_pose_animation)(void* model, void* pose, double weight);
  // anim_priority 0x10004310 (model, layer, double: FLD [esp+0x14]); set_pose_rotation 0x10004790 (pose, int idx,
  // const float v[3], int); rmv_pose_animation 0x10004900 (model, layer).
  void (*skmod_anim_priority)(void* model, void* layer, double priority);
  void (*skmod_set_pose_rotation)(void* pose, int idx, const float* v, int flags);
  void (*skmod_rmv_pose_animation)(void* model, void* layer);
};

extern Engine g_eng;
extern P3Shared g_p3;
extern Skmod g_sk;

// Resolves every member; logs each and returns the number missing.
int engine_resolve(void);
// g_p3 from the already-loaded p3shared.dll (never LoadLibrary); main thread, first use. True when all resolved.
bool engine_resolve_p3shared(void);
// g_sk from the already-loaded skmod.dll, by export name (never LoadLibrary); main thread. True when resolved.
bool engine_resolve_skmod(void);
// The two bone exports, apart from the clock so a missing one never stops the weapon levers. Main thread.
bool engine_resolve_skmod_bones(void);
// The seven pose exports (torso.cpp), apart from the others. Main thread. True when all resolved.
bool engine_resolve_skmod_pose(void);
// g_p3.cvar_set only, apart from the matcher exports so neither can stop the other. Main thread.
bool engine_resolve_cvar_set(void);
// g_p3.pin_set only (cutaway.cpp's undo). Main thread.
bool engine_resolve_pin_set(void);

// Committed, readable (not guard / no-access) memory for all n bytes.
bool guarded_readable(const void* p, size_t n);
// Committed, writable (read-write or copy-on-write, no guard) memory for all n bytes.
bool guarded_writable(const void* p, size_t n);

// The last readable region a walk queried. Zero it at the start of each walk so
// a region freed since the last frame is never trusted.
struct ReadCache { const unsigned char* base; size_t size; unsigned misses; };
// guarded_readable, answered without VirtualQuery (~12 us) when [p, p+n) lies in c's region.
bool guarded_readable_cached(ReadCache* c, const void* p, size_t n);
