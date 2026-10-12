// View capture from a rend_camera_push detour. Several pushes per frame (client!0x10002620 recurses over its view
// nodes): every push is counted by caller, only the first from client.dll kept. Zoom out swaps its matrices for pulled-
// back copies (camera-culling.md 1). The freecam replaces every push and beats first person, which beats zoom out.
#include "view.h"
#include "engine.h"
#include "hook.h"
#include "log.h"
#include "config.h"
#include "entities.h"
#include "esp_layout.h"   // ESP_UNITS_PER_METRE
#include "pullback.h"
#include "freecam.h"
#include "nocull.h"      // the live projection while the pause holds
#include "photo.h"       // a photo tile's fov and view shear
#include "firstperson.h"
#include "wheelzoom.h"   // the eased distance
#include "ui/bind.h"
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <string.h>

#pragma intrinsic(_ReturnAddress)

// prism3d!0x4206d0: push esi; mov esi,[esp+8] (5 bytes); push edi; mov edi,[0x4a1278].
static const unsigned char kCamPushFp[] = {
  0x56, 0x8b, 0x74, 0x24, 0x08, 0x57, 0x8b, 0x3d, 0x78, 0x12, 0x4a, 0x00 };

// cdecl (view, inv), plain RET, caller cleans (entry read; call site client!0x1000264a).
static void (__cdecl* s_push_tramp)(const float* view, const float* inv);

static unsigned char* s_client_base;
static unsigned s_client_size;

#define VIEW_MAX_CALLERS 8
struct Caller { void* ret; int n; };

// Main thread only: pushes come from client's prio-99 process, reads from rend_end_frame.
static Caller s_callers[VIEW_MAX_CALLERS];
static int s_ncallers, s_pushes, s_client_pushes, s_have;
static Mat4 s_view, s_proj;
static Mat4 s_stockView;   // the engine's own view for the captured push (zoom-out and freecam replace it)
static int s_stockReplaced, s_stockUnread;   // cumulative: captures whose view was replaced / unreadable
static float s_near, s_far, s_fov;
static int s_frames, s_frames_with_view, s_bad_near, s_discards;
static ULONGLONG s_next_log;

// The thread proof for the plain statics above (two-thread rule):
// pushes on a thread other than the end-frame thread are counted, never assumed away.
static volatile LONG s_endframe_tid, s_first_push_logged, s_foreign_pushes;

#define VIEW_MAX_OTHER 4
static void* s_other_rets[VIEW_MAX_OTHER];   // non-frame rend_end_frame callers, each logged once
static int s_nother;

// The pulled pair handed to the trampoline, static so it outlives the call.
// rend_camera_push copies both 64-byte matrices (rep movsd at 0x4206ea, 0x420704)
// and keeps no pointer to either.
static float s_pullView[16], s_pullInv[16];
static float s_fcView[16], s_fcInv[16];   // the freecam's pair, same lifetime rule
static float s_fpView[16], s_fpInv[16];   // first person's pair, same lifetime rule
static int s_zoomWas;
static int s_zoomWasM;
static int s_pulled;                 // cumulative pushes handed the pulled pair
static ULONGLONG s_nextEyeLog;

static void client_range(void) {
  if (s_client_base) return;
  unsigned char* b = (unsigned char*)GetModuleHandleA("client.dll");
  if (!b || !guarded_readable(b, 0x40)) return;
  const LONG lfanew = *(const LONG*)(b + 0x3c);   // IMAGE_DOS_HEADER.e_lfanew
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(b + lfanew);
  if (lfanew <= 0 || !guarded_readable(nt, sizeof(*nt))) return;
  s_client_size = nt->OptionalHeader.SizeOfImage;
  s_client_base = b;
  np_log("view: client.dll %p size 0x%X", (void*)b, s_client_size);
}

static bool from_client(void* ret) {
  client_range();
  const unsigned char* r = (const unsigned char*)ret;
  return s_client_base && r >= s_client_base && r < s_client_base + s_client_size;
}

// No panel-open gate: the hold works while playing, like zoom.cpp's hold.
static bool zoomout_held(void) {
  const int vk = config_bind_get(BIND_ROW_HOLDZOOMOUT);
  if (!bind_key_is_live(vk)) return false;
  return bind_hold_is_active(vk, (GetAsyncKeyState(vk) & 0x8000) != 0);
}

// World units to pull this push back, 0 for none. Only the frame's first client
// push while a player exists: the menu's 3D scene and loading screens have none
// (the snapshot is last frame's, so a scene change lags by one frame).
static float zoomout_distance(void* ret) {
  if (!from_client(ret) || s_client_pushes != 0) return 0.f;
  // The hold shows the slider's Distance; otherwise wheelzoom.h's view: the wheel's own distance while it is
  // engaged, else the slider's while Zoom out is on, eased in and out. The log follows the slider's metres.
  const float m = zoomout_held() ? wheelzoom_distance_m() : wheelzoom_view_m();
  const bool on = m > 0.f && entities_snapshot().player != 0;
  const float d = on ? m * ESP_UNITS_PER_METRE : 0.f;
  if (on != (s_zoomWas != 0) || (on && g_cfg.zoomOutDist != s_zoomWasM)) {
    if (on) np_log("zoomout: on %d m (d=%.0f units now)", g_cfg.zoomOutDist, d);
    else np_log("zoomout: off");
    s_zoomWas = on; s_zoomWasM = g_cfg.zoomOutDist;
  }
  return d;
}

// The engine's own eye after the push against the one we asked for (camera-culling.md),
// plus id_err (max |view*inv - I| of the engine's ORIGINAL pair, before pulling): the maths
// only holds while that product is the identity, and this line can show a failing case.
static void zoomout_log_eye(const float view[16], const float inv[16]) {
  const ULONGLONG now = GetTickCount64();
  if (now < s_nextEyeLog || !g_eng.rend_get_camera) return;
  s_nextEyeLog = now + 5000;
  float eye[3];
  g_eng.rend_get_camera(eye);
  float id_err = 0.f;
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      float sum = 0.f;
      for (int k = 0; k < 4; ++k) sum += view[r * 4 + k] * inv[k * 4 + c];
      const float want = (r == c) ? 1.f : 0.f;
      const float err = sum - want;
      if (err < 0.f ? -err > id_err : err > id_err) id_err = err < 0.f ? -err : err;
    }
  }
  np_log("zoomout: eye=(%.1f,%.1f,%.1f) want=(%.1f,%.1f,%.1f) pulled=%d id_err=%.2e",
         eye[0], eye[1], eye[2], s_pullInv[12], s_pullInv[13], s_pullInv[14], s_pulled, id_err);
}

// stock: the view the engine asked to push, before any substitution (NULL when unchanged).
static void on_push(void* ret, const float* stock) {
  const DWORD tid = GetCurrentThreadId();
  if (InterlockedCompareExchange(&s_first_push_logged, 1, 0) == 0) np_log("view: first push tid=%lu", tid);
  const LONG ef = s_endframe_tid;
  if (ef && (DWORD)ef != tid) InterlockedIncrement(&s_foreign_pushes);
  s_pushes++;
  int i = 0;
  while (i < s_ncallers && s_callers[i].ret != ret) i++;
  if (i < s_ncallers) s_callers[i].n++;
  else if (s_ncallers < VIEW_MAX_CALLERS) { s_callers[s_ncallers].ret = ret; s_callers[s_ncallers].n = 1; s_ncallers++; }

  if (!from_client(ret)) return;
  s_client_pushes++;
  if (s_have || !g_eng.rend_camera_matrix || !g_eng.rend_get_proj_matrix || !g_eng.rend_projection_get) return;
  const float* top = g_eng.rend_camera_matrix();
  if (!guarded_readable(top, sizeof(s_view.m))) return;
  memcpy(s_view.m, top, sizeof(s_view.m));
  g_eng.rend_get_proj_matrix(s_proj.m);
  g_eng.rend_projection_get(&s_near, &s_far, &s_fov);
  // A near plane <= 0 would let points at or behind the camera project (w2s rejects them too).
  if (!(s_near > 0.f)) { s_bad_near++; return; }
  s_stockView = s_view;
  if (stock) {
    if (guarded_readable(stock, sizeof(s_stockView.m))) { memcpy(s_stockView.m, stock, sizeof(s_stockView.m)); s_stockReplaced++; }
    else s_stockUnread++;   // falls back to the pushed view
  }
  s_have = 1;
}

static void __cdecl camera_push_detour(const float* view, const float* inv) {
  void* ret = _ReturnAddress();
  if (from_client(ret) && freecam_substitute(inv, s_fcView, s_fcInv)) {
    nocull_push_projection();
    photo_push(s_fcView, s_fcInv);   // after nocull: a tile narrows the projection it applied
    s_push_tramp(s_fcView, s_fcInv);
    on_push(ret, view);
    return;
  }
  // First person: the frame's first client push only (the player's view, as zoom-out takes it); it wins over
  // zoom-out, which would pull the eye behind a body the engine does not draw (free-move.md section 2).
  if (from_client(ret) && s_client_pushes == 0 && firstperson_substitute(inv, s_fpView, s_fpInv)) {
    s_push_tramp(s_fpView, s_fpInv);
    on_push(ret, view);
    return;
  }
  const float d = zoomout_distance(ret);
  // The engine's own push reads both unguarded; ours are checked once a frame.
  const bool pull = d > 0.f && guarded_readable(view, sizeof(s_pullView)) && guarded_readable(inv, sizeof(s_pullInv));
  if (pull) {
    pullback(view, inv, d, s_pullView, s_pullInv);
    s_pulled++;
    s_push_tramp(s_pullView, s_pullInv);
  } else {
    s_push_tramp(view, inv);
  }
  on_push(ret, pull ? view : 0);  // the pushed view (pulled or not) is the stack top from here on
  if (pull) zoomout_log_eye(view, inv);
}

int view_install(void) {
  if (!g_eng.rend_camera_push) { np_log("view: rend_camera_push unresolved; no detour"); return 0; }
  return hook_install_at((void*)g_eng.rend_camera_push, kCamPushFp, sizeof(kCamPushFp), 5,
                         (void*)camera_push_detour, (void**)&s_push_tramp);
}

float view_last_fov(void) { return s_fov; }

int view_get(Mat4* view, Mat4* proj, float* nearZ) {
  if (!s_have) return 0;
  *view = s_view; *proj = s_proj; *nearZ = s_near;
  return 1;
}

// Zoom-out moves only the eye (pullback) and the freecam only the view: the projection is the engine's.
int view_get_stock(Mat4* view, Mat4* proj, float* nearZ) {
  if (!s_have) return 0;
  *view = s_stockView; *proj = s_proj; *nearZ = s_near;
  return 1;
}

// "module+0xOFFSET" for a code address; "?+0x<address>" when no module holds it.
static int where(void* addr, char* out, size_t cap) {
  HMODULE m = 0;
  char name[MAX_PATH] = "?";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR)addr, &m) && m)
    GetModuleFileNameA(m, name, MAX_PATH);
  const char* base = strrchr(name, 0x5C);   // backslash
  base = base ? base + 1 : name;
  return _snprintf_s(out, cap, _TRUNCATE, "%s+0x%X", base, (unsigned)((unsigned char*)addr - (unsigned char*)m));
}

static void log_callers(void) {
  char text[512];
  int used = 0;
  text[0] = 0;
  for (int i = 0; i < s_ncallers && used < (int)sizeof(text) - 96; i++) {
    char at[MAX_PATH + 16];
    if (where(s_callers[i].ret, at, sizeof(at)) < 0) break;
    const int n = _snprintf_s(text + used, sizeof(text) - used, _TRUNCATE, "%s%s x%d", i ? " " : "", at, s_callers[i].n);
    if (n < 0) break;
    used += n;
  }
  np_log("view: pushes/frame=%d client=%d callers=[%s] frames=%d with_view=%d",
         s_pushes, s_client_pushes, text, s_frames, s_frames_with_view);
  np_log("view: near=%.3f far=%.3f fov=%.3f (captured=%d) bad_near=%d other_endframes=%d foreign_tid_pushes=%ld "
         "stockReplaced=%d stockUnread=%d", s_near, s_far, s_fov, s_have, s_bad_near, s_discards, s_foreign_pushes,
         s_stockReplaced, s_stockUnread);
}

static void clear_frame(void) { s_have = 0; s_pushes = 0; s_client_pushes = 0; s_ncallers = 0; }

void view_frame_discard(void* ret) {
  s_discards++;
  int i = 0;
  while (i < s_nother && s_other_rets[i] != ret) i++;
  if (i == s_nother && s_nother < VIEW_MAX_OTHER) {
    s_other_rets[s_nother++] = ret;
    char at[MAX_PATH + 16];
    if (where(ret, at, sizeof(at)) >= 0) np_log("view: rend_end_frame from outside the frame path: %s", at);
  }
  clear_frame();
}

void view_frame_end(void) {
  InterlockedExchange(&s_endframe_tid, (LONG)GetCurrentThreadId());
  s_frames++;
  if (s_have) s_frames_with_view++;
  const ULONGLONG now = GetTickCount64();
  if (now >= s_next_log) { s_next_log = now + 5000; log_callers(); }
  clear_frame();
}
