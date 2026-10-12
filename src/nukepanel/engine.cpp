// Engine functions come from prism3d.exe's export table, never from offsets:
// GetProcAddress on the already-mapped host exe is loader-safe in DllMain.
#include "engine.h"
#include "log.h"
#include <windows.h>

Engine g_eng;

static int resolve_one(HMODULE exe, const char* name, void** slot) {
  *slot = (void*)GetProcAddress(exe, name);
  if (*slot) { np_log("engine: %s = %p", name, *slot); return 0; }
  np_log("engine: MISSING %s", name);
  return 1;
}

#define R(member) missing += resolve_one(exe, #member, (void**)&g_eng.member)

int engine_resolve(void) {
  HMODULE exe = GetModuleHandleA(NULL);
  int missing = 0;
  R(rend_end_frame);   R(rend_begin_frame);  R(inp_impulse_set); R(inp_impulse_get); R(phys_collide_history);
  R(rend_camera_push); R(rend_set_projection_type);
  R(rend_push);        R(rend_identity);    R(rend_pop);
  R(rend_line_plain);  R(rend_bitmap);      R(mm_get_resource);
  R(pml_set_ref);      R(pml_set_bounds);   R(pml_render);
  R(pml_flush);        R(pml_buffer_flush);
  R(rend_cache_flush); R(rend_props_flush); R(rend_get_params);
  R(rend_camera_matrix); R(rend_get_proj_matrix); R(rend_projection_get); R(rend_projection_change);
  R(rend_get_camera);  R(rend_get_image);
  R(spawn_search);     R(spawn_search_get); R(spawn_search_stop);
  R(wm_get_input);     R(wm_get_object);
  R(rend_cache_data); R(rend_get_pool);
  R(inp_set_active);  R(inp_reset_impulses); R(inp_get_mouse_state);
  R(cvar_get);        R(cmd_execute);
  R(process_delete_all); R(ci_dec_refs);
  R(ui_wget);         R(ui_wfocused);      R(ui_whide);         R(ui_wshow);
  R(phys_ray_test);   R(phys_bound_test);  R(phys_ray_visible);
  R(rend_sphere_visible); R(rend_sphere_visible_dist); R(rend_box_visible);
  R(rend_restart);     R(hw_get_context);
  R(vfs_open);        R(vfs_read);         R(vfs_close);        R(vfs_get_device_name);  R(vfs_write);
  R(inp_trigger);     R(bind_get);         R(inp_cursor_info);
  R(process_sleep);   R(process_wakeup);   R(process_pid_get);   R(sys_get_var);
  np_log("engine: %d missing", missing);
  return missing;
}

P3Shared g_p3;

bool engine_resolve_p3shared(void) {
  static int state;   // 0 not tried, 1 resolved, -1 missing (logged once)
  if (state) return state > 0;
  HMODULE p3 = GetModuleHandleA("p3shared.dll");
  if (!p3) return false;   // not mapped yet: try again next time
  int missing = 0;
  missing += resolve_one(p3, "??0tmatcher_t@@QAE@XZ", (void**)&g_p3.tm_ctor);
  missing += resolve_one(p3, "??1tmatcher_t@@QAE@XZ", (void**)&g_p3.tm_dtor);
  missing += resolve_one(p3, "?reset_rules@tmatcher_t@@QAEX_N@Z", (void**)&g_p3.tm_reset_rules);
  missing += resolve_one(p3, "?accept_superset@tmatcher_t@@QAEXII_N@Z", (void**)&g_p3.tm_accept_superset);
  missing += resolve_one(p3, "?deny_superset@tmatcher_t@@QAEXII_N@Z", (void**)&g_p3.tm_deny_superset);
  missing += resolve_one(p3, "?deny_ptr@tmatcher_t@@QAEXPAVentity_base_t@@@Z", (void**)&g_p3.tm_deny_ptr);
  np_log("engine: p3shared %d missing", missing);
  state = missing ? -1 : 1;
  return state > 0;
}

Skmod g_sk;

bool engine_resolve_skmod(void) {
  static int state;   // 0 not tried, 1 resolved, -1 missing (logged once)
  if (state) return state > 0;
  HMODULE sk = GetModuleHandleA("skmod.dll");
  if (!sk) return false;   // not mapped yet: try again next time
  state = resolve_one(sk, "skmod_get_time", (void**)&g_sk.skmod_get_time) ? -1 : 1;
  return state > 0;
}

bool engine_resolve_skmod_bones(void) {
  static int state;   // 0 not tried, 1 resolved, -1 missing (logged once)
  if (state) return state > 0;
  HMODULE sk = GetModuleHandleA("skmod.dll");
  if (!sk) return false;
  int missing = resolve_one(sk, "skmod_get_bone_index", (void**)&g_sk.skmod_get_bone_index);
  missing += resolve_one(sk, "skmod_get_bone_transform", (void**)&g_sk.skmod_get_bone_transform);
  state = missing ? -1 : 1;
  return state > 0;
}

bool engine_resolve_skmod_pose(void) {
  static int state;   // 0 not tried, 1 resolved, -1 missing (logged once)
  if (state) return state > 0;
  HMODULE sk = GetModuleHandleA("skmod.dll");
  if (!sk) return false;
  int missing = resolve_one(sk, "skmod_open_pose", (void**)&g_sk.skmod_open_pose);
  missing += resolve_one(sk, "skmod_set_pose_bone_name", (void**)&g_sk.skmod_set_pose_bone_name);
  missing += resolve_one(sk, "skmod_copy_pose_bones", (void**)&g_sk.skmod_copy_pose_bones);
  missing += resolve_one(sk, "skmod_add_pose_animation", (void**)&g_sk.skmod_add_pose_animation);
  missing += resolve_one(sk, "skmod_anim_priority", (void**)&g_sk.skmod_anim_priority);
  missing += resolve_one(sk, "skmod_set_pose_rotation", (void**)&g_sk.skmod_set_pose_rotation);
  missing += resolve_one(sk, "skmod_rmv_pose_animation", (void**)&g_sk.skmod_rmv_pose_animation);
  state = missing ? -1 : 1;
  return state > 0;
}

bool engine_resolve_cvar_set(void) {
  static int state;   // 0 not tried, 1 resolved, -1 missing (logged once)
  if (state) return state > 0;
  HMODULE p3 = GetModuleHandleA("p3shared.dll");
  if (!p3) return false;
  state = resolve_one(p3, "?set@cvar_t@@QAE_NPBD@Z", (void**)&g_p3.cvar_set) ? -1 : 1;
  return state > 0;
}

bool engine_resolve_pin_set(void) {
  static int state;   // 0 not tried, 1 resolved, -1 missing (logged once)
  if (state) return state > 0;
  HMODULE p3 = GetModuleHandleA("p3shared.dll");
  if (!p3) return false;
  state = resolve_one(p3, "?set@pin_t@@QAEXXZ", (void**)&g_p3.pin_set) ? -1 : 1;
  return state > 0;
}

static bool region_readable(const MEMORY_BASIC_INFORMATION& mbi) {
  const DWORD prot = mbi.Protect & 0xff;
  return mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) && prot != PAGE_NOACCESS && prot != PAGE_EXECUTE;
}

bool guarded_readable(const void* p, size_t n) {
  if (!p || n == 0) return false;
  const unsigned char* cur = (const unsigned char*)p;
  const unsigned char* end = cur + n;
  if (end < cur) return false;
  // A range may span regions with different protections; check each one.
  while (cur < end) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(cur, &mbi, sizeof(mbi))) return false;
    if (!region_readable(mbi)) return false;
    cur = (const unsigned char*)mbi.BaseAddress + mbi.RegionSize;
  }
  return true;
}

bool guarded_writable(const void* p, size_t n) {
  if (!p || n == 0) return false;
  const unsigned char* cur = (const unsigned char*)p;
  const unsigned char* end = cur + n;
  if (end < cur) return false;
  while (cur < end) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(cur, &mbi, sizeof(mbi))) return false;
    const DWORD prot = mbi.Protect & 0xff;
    const bool w = prot == PAGE_READWRITE || prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READWRITE ||
                   prot == PAGE_EXECUTE_WRITECOPY;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || !w) return false;
    cur = (const unsigned char*)mbi.BaseAddress + mbi.RegionSize;
  }
  return true;
}

bool guarded_readable_cached(ReadCache* c, const void* p, size_t n) {
  const unsigned char* q = (const unsigned char*)p;
  if (c->base && q >= c->base && n <= c->size && (size_t)(q - c->base) <= c->size - n) return true;
  c->misses++;
  MEMORY_BASIC_INFORMATION mbi;
  if (!p || n == 0 || !VirtualQuery(p, &mbi, sizeof(mbi))) return false;
  const unsigned char* base = (const unsigned char*)mbi.BaseAddress;
  if (region_readable(mbi) && (size_t)(q - base) + n <= mbi.RegionSize) {
    c->base = base; c->size = mbi.RegionSize;
    return true;
  }
  return guarded_readable(p, n);   // spans two regions (or is unreadable): the full check
}
