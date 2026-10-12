// The rend_end_frame detour (render.md section 2, candidate 1): runs the
// instruments and the panel on the frame path, one code path for gl and dx8.
#include "probe.h"
#include "engine.h"
#include "hook.h"
#include "log.h"
#include "framestats.h"
#include "view.h"
#include "instruments.h"
#include "jumpfix.h"
#include "panel.h"
#include "config.h"
#include "input.h"
#include "framecap.h"
#include "entities.h"
#include "esp.h"
#include "doorlabel.h"
#include "levelstats.h"
#include "areas.h"
#include "freecam.h"
#include "teleport.h"
#include "nocull.h"
#include "dmgnum.h"
#include "hurtlog.h"
#include "weapons.h"
#include "aim.h"
#include "mouseaim.h"
#include "torso.h"
#include "firstperson.h"
#include "moveanywhere.h"
#include "rescue.h"
#include "aiaware.h"
#include "fpmoves.h"
#include "display.h"
#include "floattext.h"
#include "skipintro.h"
#include "zoom.h"
#include "wheelzoom.h"
#include "shot.h"
#include "cutaway.h"
#include "photo.h"
#include "cheatmark.h"
#include "saveio.h"
#include "gamespeed.h"
#include "stall.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

// The one hardcoded offset: subsys's `call rend_end_frame` (prism3d!0x425c6e, render.md section 2).
// It is verified at install (E8 whose target is the resolved export), so a
// different build installs nothing instead of drawing on the wrong path.
static const unsigned kFrameCallRva = 0x25c6e;

static const unsigned char kEndFrameFp[] = {
  0x8b, 0x0d, 0x38, 0x09, 0x4a, 0x00, 0x8b, 0x01, 0xff, 0xa0, 0xdc, 0x00, 0x00, 0x00 };

static void* s_frame_ret;              // call site + 5, computed at install
static void (*s_end_frame_tramp)(void);

// cdecl, no args: prism3d!0x41fa70 is `mov ecx,[dev]; mov eax,[ecx]; jmp [eax+0xdc]`
static void __declspec(naked) end_frame_detour(void) {
  __asm {
    mov eax, [esp]                      // return address
    cmp eax, dword ptr [s_frame_ret]    // frame path only; rend_restart also ends frames
    jne other
    pushad
    call probe_frame
    popad
    jmp dword ptr [s_end_frame_tramp]
  other:
    pushad
    push eax                            // the caller's return address, for the log
    call view_frame_discard
    add esp, 4
    popad
    jmp dword ptr [s_end_frame_tramp]
  }
}

int probe_install(void) {
  unsigned char* base = (unsigned char*)GetModuleHandleA(NULL);
  if (!g_eng.rend_end_frame) { np_log("probe: rend_end_frame unresolved; no detour"); return 0; }
  unsigned char* site = base + kFrameCallRva;
  if (!guarded_readable(site, 5)) { np_log("probe: call site %p unreadable; no detour", (void*)site); return 0; }
  const int rel = *(const int*)(site + 1);
  unsigned char* dest = site + 5 + rel;
  if (site[0] != 0xE8 || dest != (unsigned char*)g_eng.rend_end_frame) {
    np_log("probe: call site %p is %02X -> %p, expected E8 -> %p; no detour",
           (void*)site, site[0], (void*)dest, (void*)g_eng.rend_end_frame);
    return 0;
  }
  s_frame_ret = site + 5;
  np_log("probe: frame call site %p verified, return address %p", (void*)site, s_frame_ret);
  return hook_install_at((void*)g_eng.rend_end_frame, kEndFrameFp, sizeof(kEndFrameFp), 6,
                         (void*)end_frame_detour, (void**)&s_end_frame_tramp);
}

// wm_get_input(0): the input entity (entities.md section 2). Player bit 0x100 at +0x4C
// gates the instruments.
static const unsigned char* input_entity(const unsigned char** player) {
  *player = 0;
  if (!g_eng.wm_get_input) return 0;
  const unsigned char* in = (const unsigned char*)g_eng.wm_get_input(0);
  if (!in || !guarded_readable(in, 0x80)) return 0;
  unsigned user;
  memcpy(&user, in + 0x4C, sizeof(user));      // user type bits, 0x100 = player (entities.md section 1)
  if ((user & 0x100) && guarded_readable(in, kPlayerSpan)) *player = in;
  return in;
}

static FrameStats s_fs;
static LARGE_INTEGER s_qpf, s_last;
static int s_W, s_H;
static bool s_started;
// fs_reset zeroes last_avg_ms, so the fps hint row (menu.cpp, via
// probe_measured_fps) reads this instead -- the value survives the reset
// that follows each ready window.
static double s_lastAvgMs;
static HitchStats s_hitch;
static LARGE_INTEGER s_bodyT0;   // body entry, for the hitch line's panel share
static double s_tailMs;          // view_frame_end + display_frame of the frame before (framecap sleeps on purpose, excluded)
static unsigned s_msvSeen;
static int s_inLevel;   // a player entity this frame, for display_frame's first-apply gate

double probe_measured_fps(void) { return s_lastAvgMs > 0.0 ? 1000.0 / s_lastAvgMs : 0.0; }

static void probe_frame_body(void) {
  QueryPerformanceCounter(&s_bodyT0);
  aim_lock_clear();   // before any early return: the detour never aims at a stale point
  mouseaim_clear();   // likewise mouse aim's point
  torso_clear();      // and the torso's angle
  if (!s_started) {
    s_started = true;
    QueryPerformanceFrequency(&s_qpf);
    QueryPerformanceCounter(&s_last);
    fs_reset(&s_fs);
    np_log("probe: first frame tid=%lu", GetCurrentThreadId());
    config_load();   // before the instruments read [debug] timing
    floattext_mount();   // before any pickup spawns a popup (actor_popup_alloc loads the script per popup)
    skipintro_mount();   // before game_proc_handler state 4 reads scr_intro.sc (quick-wins.md section 1 ordering)
    saveio_install();    // here, not DllMain: this is the main thread it times (save-io.md)
    // Which graphics modules are resident (GetModuleHandleA/GetModuleFileNameA
    // only -- nothing is loaded here); run 2 settled the frame cap from this.
    static const char* const kMods[] = {
      "d3d8.dll", "d3d9.dll", "dxwrapper.dll", "opengl32.dll",
      "dinput8.dll", "ddraw.dll", "dxgi.dll" };
    for (int i = 0; i < (int)(sizeof(kMods) / sizeof(kMods[0])); ++i) {
      const HMODULE m = GetModuleHandleA(kMods[i]);
      char modPath[MAX_PATH];
      if (m && GetModuleFileNameA(m, modPath, MAX_PATH)) {
        char shown[MAX_PATH];
        np_log("mod: %s = %s", kMods[i], np_log_path(modPath, shown, sizeof shown));
      } else {
        np_log("mod: %s not loaded", kMods[i]);
      }
    }
  }
  // Before any early return: the check runs every game tick, and WideGen's g_cam_fov is off its default from launch.
  cheatmark_frame();
  if (!g_eng.rend_get_params) return;

  int W = 0, H = 0, bpp = 0;
  g_eng.rend_get_params(&W, &H, &bpp);
  if (W != s_W || H != s_H) { np_log("probe: screen %dx%d bpp=%d", W, H, bpp); s_W = W; s_H = H; }
  if (W <= 0 || H <= 0) return;
  photo_capture(W, H);   // before any draw of ours: a photo tile is the world alone

  const unsigned char* player;
  const unsigned char* in = input_entity(&player);
  firstperson_note_input(in);
  s_inLevel = player != 0;
  // The world (prio 10) has ticked this frame and the next tick reads E14 after this returns.
  const int jfEv = jumpfix_frame((unsigned char*)player);
  instruments_frame(player, jfEv);   // after the restore: logs the events, sees the restored E14
  hurtlog_frame(player);   // after the world tick: this frame's sends and Duke's health drop (queen-burst.md)
  entities_frame(player);
  levelstats_frame(player);   // the live stats record, and the part's totals once per part
  areas_frame(player);        // after it: the part's areas and which secrets and nukes each holds still
  doorlabel_frame(player);    // after it: the door label reads the areas' found state
  weapons_frame(player);   // the held weapon's levers, once a frame before the draws
  cutaway_frame(player);   // the faders' state words; their handlers fade them next tick
  firstperson_frame(player);   // before aim_frame: the mode, mouse look and the view point for next tick's shots
  fpmoves_frame(player);       // after it: crouch (and the other first person keys)
  moveanywhere_frame(player, W, H);   // after it: the detours stand down in first person
  rescue_frame(player);               // after both modes: a bottomless fall goes back to safe ground
  aiaware_frame(player);              // after both modes' frames: the gate reads their switches
  aim_frame(player, W, H); // after the walk and the held weapon: this frame's lock, used by next tick's shots
  mouseaim_frame(player, W, H);   // after aim_frame armed the detour: the cursor's point for next tick's shots
  torso_frame(player);            // after mouseaim_frame: the next ticks' bend toward that point
  // Here and nowhere else: a frame that returned above never walked, so its
  // snapshot is last frame's.
  // Screenshot mode hides every overlay of ours (as of last frame's freecam state); the panel still draws.
  const bool shot = shot_hides_overlays();
  if (!shot) {
    doorlabel_draw(W, H);   // under the ESP markers
    esp_frame(W, H);
    dmgnum_draw(W, H);    // over the ESP, under the panel
    aim_draw(W, H);       // the calibration marker, likewise
    mouseaim_draw(W, H);  // the reticle, likewise
    firstperson_draw(W, H);   // first person's centre crosshair, likewise
  }
  levelstats_draw(W, H);   // the corner readout; it hides itself in screenshot mode and counts that
  teleport_frame();     // player_teleport here, outside the draw pass (freecam-teleport.md section 1)
  if (!shot) {
    teleport_draw(W, H);
    freecam_draw(W, H);   // under the panel
  }

  input_frame(W, H);
  wheelzoom_frame();     // before panel_frame, which saves a distance the wheel moved
  skipintro_frame();     // after input_frame: the UI manager check it uses is refreshed there
  freecam_frame();      // this frame's keys and mouse move next frame's pushes
  shot_frame();         // after the freecam toggle settles, before panel_frame saves the g_hud stash
  nocull_frame();       // after the freecam toggle settles; the force applies to next frame's world pass
  panel_frame(W, H);
  gamespeed_frame();      // after panel_frame: this frame's switch, slider and key set the clock's speed
  fov_frame(s_inLevel);  // after panel_frame: this frame's switch and slider (zoom.h)
  photo_frame(W, H);      // after the readback and every draw: its progress line is never in a tile

  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const double dt = (double)(now.QuadPart - s_last.QuadPart) / (double)s_qpf.QuadPart;
  s_last = now;
  fs_add(&s_fs, dt);
  // Taken every frame, so a hitch line's log= and msv= cover the window its dt spans, not the time since the last hitch.
  unsigned lc, lt, lm;
  np_log_take_write_stats(&lc, &lt, &lm);
  const unsigned msv = areas_msv_reads();
  const double sioMs = saveio_take_frame_ms();
  if (fs_hitch_note(&s_hitch, dt * 1000.0)) {
    // The panel share of the window this dt spans: last frame's tail plus this body so far.
    // Single-threaded (frame hook); the log counters are interlocked and cleared by the take.
    const double panelMs = s_tailMs + (double)(now.QuadPart - s_bodyT0.QuadPart) * 1000.0 / (double)s_qpf.QuadPart;
    SYSTEMTIME st; GetLocalTime(&st);
    np_log("hitch: %02u:%02u:%02u.%03u dt=%.0f ms panel=%.1f ms log=%u writes %.1f ms (max %.1f) msv=%u"
           " saveio=%.1f ms stall=%d",
           st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, dt * 1000.0, panelMs,
           lc, lt / 1000.0, lm / 1000.0, msv - s_msvSeen, sioMs, stall_frame_samples());
  }
  s_msvSeen = msv;
  if (fs_ready(&s_fs)) {
    s_lastAvgMs = s_fs.last_avg_ms;
    np_log("frame avg=%.2fms min=%.2f max=%.2f n=%d hitches>100=%u >500=%u", s_fs.last_avg_ms, s_fs.min_ms,
           s_fs.max_ms, s_fs.n, s_hitch.over100, s_hitch.over500);
    fs_reset(&s_fs);
  }
}

void probe_frame(void) {
  probe_frame_body();
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  view_frame_end();
  // After every draw of ours: rend_restart ends this frame itself and begins the next.
  display_frame(s_inLevel);
  QueryPerformanceCounter(&t1);
  s_tailMs = s_qpf.QuadPart ? (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)s_qpf.QuadPart : 0.0;
  // Last, after our own drawing and before the trampoline returns into the
  // engine's EndScene/present: the cap site the plan's Rulings settled on.
  framecap_frame(input_window());
  // After the cap's sleep: the watchdog's frame clock then counts engine time and our next body, not our wait.
  stall_frame_end();
}
