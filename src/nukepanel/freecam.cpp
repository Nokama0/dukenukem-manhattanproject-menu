// The free camera's glue. The pose (freecam.h) replaces every client.dll camera push while the freecam is on, in
// levels, menus and mission-complete screens alike (freecam-teleport.md section 4). Main thread only: the WndProc,
// the pushes and the frame path share one thread (input.h).
#include "freecam.h"
#include "freecamfov.h"
#include "freecampause.h"
#include "input.h"
#include "view.h"
#include "engine.h"       // guarded_readable
#include "entities.h"
#include "esp_layout.h"   // ESP_UNITS_PER_METRE, ESP_LABEL_GAP, esp_label_x
#include "canvas.h"
#include "font.h"
#include "config.h"
#include "log.h"
#include "campos.h"
#include "clipboard.h"
#include "photo.h"
#include <windows.h>
#include <string.h>

static FreecamPose s_pose;
static int s_seeded, s_wasOn;
static unsigned s_pushes, s_totalPushes;   // substituted pushes: this 5 s window, since the seed
static LARGE_INTEGER s_qpf, s_lastQpc;
static ULONGLONG s_nextLog;
static long long s_mouseDx, s_mouseDy;     // every count taken since load, steering or not

static void log_pose(const char* what) {
  np_log("freecam: %s eye=(%.1f,%.1f,%.1f) yaw=%.1f pitch=%.1f hand=%+.0f pushes=%u", what,
         s_pose.eye[0], s_pose.eye[1], s_pose.eye[2], s_pose.yaw, s_pose.pitch, s_pose.hand, s_totalPushes);
}

bool freecam_substitute(const float* inv, float outView[16], float outInv[16]) {
  if (!input_freecam_on()) { s_seeded = 0; return false; }
  if (!s_seeded) {
    // The engine's own push reads it unguarded; ours is checked once per seed.
    if (!guarded_readable(inv, 16 * sizeof(float))) return false;
    s_pose = freecam_pose_from_inverse(inv);
    s_seeded = 1;
    s_totalPushes = 0;
    // The rows as captured: a roll or a scale here would not survive the pose.
    np_log("freecam: engine inverse r=(%.3f,%.3f,%.3f) u=(%.3f,%.3f,%.3f) f=(%.3f,%.3f,%.3f)",
           inv[0], inv[1], inv[2], inv[4], inv[5], inv[6], inv[8], inv[9], inv[10]);
    log_pose("on, seeded from the engine camera:");
  }
  freecam_matrices(s_pose, outView, outInv);
  s_pushes++;
  s_totalPushes++;
  return true;
}

static int read_keys(void) {
  int keys = 0;
  for (int i = 0; i < 8; ++i)
    if (GetAsyncKeyState(FREECAM_MOVE_VK[i]) & 0x8000) keys |= 1 << i;
  return keys;
}

// Copy view / Paste view: presses the WndProc took while the camera steered (input.cpp), acted on here on the
// main thread, which owns the window the clipboard calls name. A refused paste leaves the camera where it is.
static void view_keys(void) {
  const int copies = input_camcopy_take(), pastes = input_campaste_take();
  if (!s_seeded || photo_capturing()) return;
  if (copies) {
    char line[128];
    const bool ok = campos_format(s_pose, line, sizeof line) && clipboard_write_text(line);
    np_log("freecam: copy view %s (%d bytes)", ok ? "on the clipboard" : "FAILED", (int)strlen(line));
  }
  if (pastes) {
    char text[256];
    const bool read = clipboard_read_text(text, sizeof text);
    FreecamPose p = s_pose;
    if (read && campos_accept(text, &p)) {
      s_pose = p;
      log_pose("paste view, flown to");
    } else {
      // Never the text itself: the clipboard may hold anything the player copied (review qw-b 1).
      np_log("freecam: paste view refused: %d byte(s) of text, dnmp tag %s", read ? (int)strlen(text) : 0,
             read && strstr(text, "dnmp cam x=") ? "present (a field missing or out of range)" : "absent");
    }
  }
}

// --- Pause the game (freecampause.h, docs/re/freecam-pause.md) ---
// The engine's own pause without its flag or its `camera camera`: process_sleep / process_wakeup on the player's
// world, a live process checked by pid through process_pid_get before every call.
static FcPause s_fp;
// While our pause holds the world, Duke's fall clock is frozen too (rescue.cpp).
bool freecam_pause_holding() { return s_fp.world != 0 && s_fp.held != 0; }
static int s_fpPid;
static int s_fpLevers;   // 0 unchecked, 1 the three entries match the read build, -1 refused (logged once)
static unsigned s_fpHolds, s_fpRefreezes, s_fpWakes, s_fpLetGo;
// The entries as read 2026-10-02. The process offsets below appear in these bytes, so a build with another
// layout is refused instead of read.
static const unsigned char kSleepEntry[19] = { 0x8B, 0x44, 0x24, 0x04, 0x8B, 0x48, 0x14, 0x85, 0xC9, 0x75, 0x07,
                                               0xC7, 0x40, 0x14, 0x01, 0x00, 0x00, 0x00, 0xC3 };
static const unsigned char kWakeEntry[9] = { 0x56, 0x8B, 0x74, 0x24, 0x08, 0x83, 0x7E, 0x14, 0x01 };
static const unsigned char kPidGetLoop[6] = { 0x8B, 0x50, 0x08, 0x3B, 0x4A, 0x20 };   // at +0x10: cmp ecx,[edx+0x20]
static const unsigned kProcState = 0x14, kProcPid = 0x20;   // 0 running, 1 asleep; the pid process_pid_get matches

static bool pause_levers(void) {
  if (s_fpLevers) return s_fpLevers > 0;
  const unsigned char* sl = (const unsigned char*)g_eng.process_sleep;
  const unsigned char* wk = (const unsigned char*)g_eng.process_wakeup;
  const unsigned char* pg = (const unsigned char*)g_eng.process_pid_get;
  const bool ok = sl && wk && pg && guarded_readable(sl, sizeof kSleepEntry) && guarded_readable(wk, sizeof kWakeEntry) &&
                  guarded_readable(pg + 0x10, sizeof kPidGetLoop) && !memcmp(sl, kSleepEntry, sizeof kSleepEntry) &&
                  !memcmp(wk, kWakeEntry, sizeof kWakeEntry) && !memcmp(pg + 0x10, kPidGetLoop, sizeof kPidGetLoop);
  s_fpLevers = ok ? 1 : -1;
  np_log("freecam: pause levers %s (process_sleep %p, process_wakeup %p, process_pid_get %p)",
         ok ? "verified" : "REFUSED: an entry differs from the read build; Pause the game does nothing", (const void*)sl,
         (const void*)wk, (const void*)pg);
  return ok;
}

// The world's run state, 0 or 1, or -1 unreadable. Only for a world process_pid_get just listed.
static int world_state(const void* w) {
  const unsigned char* p = (const unsigned char*)w + kProcState;
  if (!guarded_readable(p, 4)) return -1;
  int v;
  memcpy(&v, p, sizeof v);
  return v;
}

static void pause_frame(int on) {
  // A photo freezes the world for its capture even with the row off (docs/re/photo.md).
  const int armed = on && (g_cfg.freecamPause || photo_busy());
  // Nothing held and nothing to take: no reads this frame.
  if (!s_fp.world && (!armed || s_fp.blocked)) { fcp_step(&s_fp, armed, 0, 0, 0); return; }
  const unsigned char* player = (const unsigned char*)entities_snapshot().player;
  const void* cur = 0;
  if (player) memcpy(&cur, player + 0xE0, sizeof cur);   // the object's world, as teleport.cpp's cast reads it
  int alive = 0, pid = s_fpPid;
  if (pause_levers()) {
    if (s_fp.world) {
      alive = g_eng.process_pid_get(s_fpPid) == s_fp.world;
    } else if (cur && guarded_readable((const unsigned char*)cur + kProcPid, 4)) {
      memcpy(&pid, (const unsigned char*)cur + kProcPid, sizeof pid);
      alive = g_eng.process_pid_get(pid) == cur;
    }
  }
  const int screen =
      s_fp.world && (input_ui_window("screen", "menu_main") || input_ui_window("screen", "menu_help")) ? 1 : 0;
  const void* before = s_fp.world;
  const int act = fcp_step(&s_fp, armed, cur, alive, screen);
  if (act == FCP_SLEEP) {
    void* w = (void*)s_fp.world;
    const int state = world_state(w);
    if (!before) {
      s_fpPid = pid;
      s_fpHolds++;
      np_log("freecam: pause holds world %p (pid %d), it was %s (holds %u)", w, pid,
             state == 0 ? "running" : state == 1 ? "already asleep (an engine pause)" : "unreadable", s_fpHolds);
    } else if (state == 0) {
      // Capped: a state that wakes the world every frame (demo playback) must not log every frame.
      if (++s_fpRefreezes <= 32) np_log("freecam: pause: world %p was woken under the flight (the engine's menu closed?); frozen again (%u)", w,
             s_fpRefreezes);
    }
    g_eng.process_sleep(w);
  } else if (act == FCP_WAKE) {
    g_eng.process_wakeup((void*)before);
    s_fpWakes++;
    np_log("freecam: pause ends, world %p woken: %s (wakes %u)", before, fcp_why_text(s_fp.why), s_fpWakes);
  } else if (act == FCP_LET_GO) {
    s_fpLetGo++;
    np_log("freecam: pause ends, world %p let go unwoken: %s (let go %u)", before, fcp_why_text(s_fp.why), s_fpLetGo);
  }
}

void freecam_frame(void) {
  if (!s_qpf.QuadPart) { QueryPerformanceFrequency(&s_qpf); QueryPerformanceCounter(&s_lastQpc); }
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  double dt = (double)(now.QuadPart - s_lastQpc.QuadPart) / (double)s_qpf.QuadPart;
  s_lastQpc = now;
  // A stall (a load, a breakpoint) must not fling the camera across the level.
  if (dt > 0.25) dt = 0.25;
  if (dt < 0.0) dt = 0.0;

  const int on = input_freecam_on() ? 1 : 0;
  if (on != s_wasOn) {
    if (on) np_log("freecam: on, waiting for a camera push to seed from");
    else { log_pose("off, camera handed back:"); s_seeded = 0; }
    s_wasOn = on;
    s_pushes = 0;
    s_nextLog = GetTickCount64() + 5000;
  }
  int dx, dy;
  input_freecam_mouse_take(&dx, &dy);   // drained even when unused, so no stale turn lands later
  s_mouseDx += dx;
  s_mouseDy += dy;
  pause_frame(on);   // before the early return: every way the flight ends lets the world run again
  if (!on) return;
  if (s_seeded && input_freecam_steers() && !photo_capturing()) {   // every tile from one pose
    freecam_look(&s_pose, (float)dx, (float)dy, FREECAM_SENS_DEG);
    freecam_move(&s_pose, read_keys(), freecam_speed_mps(g_cfg.freecamSpeedPct) * ESP_UNITS_PER_METRE, (float)dt);
  }
  view_keys();
  // Pushes per screen settle where the freecam can apply (the main-menu question).
  const ULONGLONG t = GetTickCount64();
  if (t >= s_nextLog) {
    s_nextLog = t + 5000;
    np_log("freecam: pushes=%u in 5 s, seeded=%d screen=%s eye=(%.1f,%.1f,%.1f) mouse=(%lld,%lld) grab=%d | pause "
           "row=%d held=%d blocked=%d holds=%u refreezes=%u wakes=%u letgo=%u",
           s_pushes, s_seeded, entities_snapshot().player ? "level" : "menu", s_pose.eye[0], s_pose.eye[1],
           s_pose.eye[2], s_mouseDx, s_mouseDy, input_freecam_steers() ? 1 : 0, g_cfg.freecamPause, s_fp.held,
           s_fp.blocked, s_fpHolds, s_fpRefreezes, s_fpWakes, s_fpLetGo);
    s_pushes = 0;
  }
}

// A rect with a 1 px C_BEVELLO keyline, so the cyan reads on a bright wall.
static void keyed_rect(int x, int y, int w, int h, unsigned colour) {
  canvas_rect(x - 1, y - 1, w + 2, h + 2, C_BEVELLO);
  canvas_rect(x, y, w, h, colour);
}

static void draw_crosshair(int cx, int cy) {
  const int a = FREECAM_CROSS_ARM, g = FREECAM_CROSS_GAP, t = FREECAM_CROSS_T;
  keyed_rect(cx - g - a, cy - t / 2, a, t, C_PLAYER);
  keyed_rect(cx + g, cy - t / 2, a, t, C_PLAYER);
  keyed_rect(cx - t / 2, cy - g - a, t, a, C_PLAYER);
  keyed_rect(cx - t / 2, cy + g, t, a, C_PLAYER);
}

// Duke's +0x18C, the box centre, through this frame's view (the freecam's own).
static void draw_duke_marker(int W, int H) {
  const EntSnapshot& snap = entities_snapshot();
  if (!snap.player) return;
  Mat4 view, proj;
  float nearZ;
  if (!view_get(&view, &proj, &nearZ)) return;
  const ScreenPt sp = w2s_project(snap.playerPos, view, proj, nearZ, W, H);
  if (!sp.ok || sp.x < 0.f || sp.x > (float)W || sp.y < 0.f || sp.y > (float)H) return;
  const int cx = clamp_double_to_int((double)sp.x + 0.5), cy = clamp_double_to_int((double)sp.y + 0.5);
  const int rows = FREECAM_MARKER_HALF * 2 + 1;
  for (int r = 0; r < rows; ++r) {
    const int hw = freecam_marker_row_halfwidth(r);
    canvas_rect(cx - hw - 1, cy - FREECAM_MARKER_HALF + r - 1, hw * 2 + 3, 3, C_BEVELLO);
  }
  for (int r = 0; r < rows; ++r) {
    const int hw = freecam_marker_row_halfwidth(r);
    canvas_rect(cx - hw, cy - FREECAM_MARKER_HALF + r, hw * 2 + 1, 1, C_PLAYER);
  }
  esp_draw_label(esp_label_x(cx, 0, canvas_text_width(FREECAM_MARKER_LABEL, FONT_SMALL)),
              cy - FREECAM_MARKER_HALF - ESP_LABEL_GAP, FREECAM_MARKER_LABEL, C_PLAYER, FONT_SMALL);
}

void freecam_draw(int W, int H) {
  if (!input_freecam_on() || !s_seeded) return;
  canvas_begin(W, H);
  draw_crosshair(W / 2, H / 2);
  if (g_cfg.freecamMarker) draw_duke_marker(W, H);
  if (g_cfg.freecamDebug) {
    // Top-left: our ESP draws no corner lines for it to stack under (quick-wins.md section 4).
    char line[96];
    double fov;
    if (fov_flight_deg(&fov)) freecam_readout_fov(s_pose, fov, line, sizeof line);
    else freecam_readout(s_pose, line, sizeof line);
    esp_draw_label(FREECAM_READOUT_MARGIN, FREECAM_READOUT_MARGIN, line, FREECAM_READOUT_RGBA, FONT_SMALL);
  }
  canvas_end();
}

bool freecam_ray(float eye[3], float fwd[3]) {
  if (!input_freecam_on() || !s_seeded) return false;
  float r[3], u[3];
  freecam_axes(s_pose, r, u, fwd);
  for (int k = 0; k < 3; ++k) eye[k] = s_pose.eye[k];
  return true;
}
