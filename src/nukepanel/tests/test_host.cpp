// The suite. Runs before the DLL links and fails the build on any red check. Only PURE rules are tested; anything
// reading live memory or calling into the real winmm is read, not run.

// CHECK/CHECK_MSG are ported from the AA port's test host: two plainly named macros rather than an argument-count
// dispatch, because MSVC's traditional preprocessor does not expand __VA_ARGS__ into another macro's argument list.
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "../logfmt.h"
#include "ndc.h"
#include "framestats.h"
#include "w2s.h"
#include "jumpwin.h"
#include "../hostgate.h"
#include "../color.h"
#include "../boostrule.h"
#include "../canvas_clip.h"
#include "../ownership.h"
#include "../ui/panel_layout.h"
#include "../ui/bind.h"
#include "../ui/anim.h"
#include "../ui/menu.h"
#include "../config.h"
#include "../ini.h"
#include "../zoom.h"
#include "../framecap.h"
#include "../classes.h"
#include "../espbox.h"
#include "../esp_layout.h"
#include "../entities.h"
#include "../pullback.h"
#include "../canvas.h"   // marker_colour and the palette constants
#include "../arrows.h"
#include "../volume.h"
#include "../freecam.h"
#include "../teleport.h"
#include "../nocull.h"
#include "../dmgnum.h"
#include "../weapons.h"
#include "../aim.h"
#include "../bossrule.h"
#include "../offscreen.h"
#include "../track.h"
#include "../display.h"
#include "../floattext.h"
#include "../winwatch.h"
#include "../hotkeys.h"
#include "../skipintro.h"
#include "../wheelzoom.h"
#include "../campos.h"
#include "../shot.h"
#include "../ui/entry.h"
#include "../gamebind.h"
#include "../mousewatch.h"
#include "../cutaway.h"
#include "../freecamfov.h"
#include "../freecampause.h"
#include "../mouseaim.h"
#include "../torso.h"
#include "../pickrule.h"
#include "../craterule.h"
#include "../carryrule.h"
#include "../jumprule.h"
#include "../firstperson.h"
#include "../moveanywhere.h"
#include "../rescue.h"
#include "../fpmoves.h"
#include "../aiaware.h"
#include "../font.h"
#include "../fontpack.h"
#include "../atlasfile.h"
#include "../photo.h"
#include "../photofolder.h"
#include "../levelstats.h"
#include "../progress.h"
#include "../eggrule.h"
#include "../overkill.h"
#include "../hurtlog.h"
#include "../weapimm.h"
#include "../puzzlerule.h"
#include "../areas.h"
#include "../areamap.h"
#include "../ui/tipwrap.h"
#include "../cheatmark.h"
#include "../saveio_rule.h"
#include "../stallrule.h"
#include "../speedrule.h"
#include "../attachrule.h"
#include <math.h>
#include <stdlib.h>   // atof
#include <string>
#include <windows.h>   // the config tests' temp files only

static int passed = 0, failed = 0;

// config.cpp logs through np_log; the suite has no log file.
void np_log(const char*, ...) {}
const char* np_log_path(const char* path, char* out, int cap) {
  logfmt_redact_profile(path, 0, out, cap);
  return out;
}

// Paths are spelled with '|' for the backslash and '~' for '/': the publish gate reads literal drive paths as PII.
static std::string tb_path(const char* s) {
  std::string r(s);
  for (char& c : r) c = c == '|' ? '\\' : c == '~' ? '/' : c;
  return r;
}

#define CHECK_MSG(cond, what)                                                 \
  do {                                                                        \
    if (cond) { passed++; }                                                   \
    else { failed++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, what); }  \
  } while (0)
#define CHECK(cond) CHECK_MSG(cond, #cond)

static int fmt_into(char* out, int cap, const char* fmt, ...) {
  va_list ap; va_start(ap, fmt); int n = logfmt_line(out, cap, fmt, ap); va_end(ap); return n;
}
static void tb_0001_logfmt_short_line_ends_in_newline() {
  char b[64]; int n = fmt_into(b, sizeof b, "x=%d", 7);
  CHECK(n == 4); CHECK(memcmp(b, "x=7\n", 4) == 0);
}
static void tb_0002_logfmt_overlong_line_truncates_without_nul() {
  char big[2000]; memset(big, 'a', sizeof big - 1); big[sizeof big - 1] = 0;
  char b[1024]; int n = fmt_into(b, sizeof b, "%s", big);
  CHECK(n == 1023); CHECK(b[n - 1] == '\n');
  CHECK(memchr(b, 0, (size_t)n) == NULL);
}
static void tb_0003_logfmt_tiny_cap_still_newline() {
  char b[2]; int n = fmt_into(b, sizeof b, "%s", "hello");
  CHECK(n == 1); CHECK(b[0] == '\n');
}

static void tb_0004_ndc_corners_and_centre() {
  CHECK(ndc_x(0.f, 200) == -1.f); CHECK(ndc_x(200.f, 200) == 1.f); CHECK(ndc_x(100.f, 200) == 0.f);
  CHECK(ndc_y(0.f, 100) == 1.f);  CHECK(ndc_y(100.f, 100) == -1.f); CHECK(ndc_y(50.f, 100) == 0.f);
}
static void tb_0005_framestats_reports_after_one_second() {
  FrameStats fs; fs_reset(&fs);
  for (int i = 0; i < 64; ++i) fs_add(&fs, 0.0057);   // 64 * 5.7 ms = 0.365 s
  CHECK(!fs_ready(&fs));
  fs_reset(&fs);
  for (int i = 0; i < 64; ++i) fs_add(&fs, 1.0 / 60.0);  // 1.067 s
  CHECK(fs_ready(&fs));
  CHECK(fs.last_avg_ms > 16.6 && fs.last_avg_ms < 16.7);
  CHECK(fs.min_ms > 16.6 && fs.max_ms < 16.7);
}

static void tb_0496_hitch_thresholds() {
  HitchStats h = {0, 0};
  CHECK(!fs_hitch_note(&h, 100.0)); CHECK(!fs_hitch_note(&h, 16.7));
  CHECK(fs_hitch_note(&h, 100.5)); CHECK(fs_hitch_note(&h, 500.0)); CHECK(fs_hitch_note(&h, 500.5));
  CHECK(h.over100 == 3 && h.over500 == 1);
}

static Mat4 ident() { Mat4 m = {}; m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.f; return m; }
static Mat4 proj90(float n, float f, int W, int H) {   // render.md section 3, t = tan(45 deg) = 1
  Mat4 p = {}; p.m[0] = 1.f; p.m[5] = (float)W / (float)H; p.m[10] = (f + n) / (f - n);
  p.m[11] = 1.f; p.m[14] = -2.f * f * n / (f - n); return p;
}
static void tb_0006_w2s_centre_edges_and_behind() {
  Mat4 v = ident(), p = proj90(16.f, 8000.f, 100, 100);
  ScreenPt c = w2s_project(Vec3{0, 0, 100}, v, p, 16.f, 100, 100);
  CHECK(c.ok && c.x == 50.f && c.y == 50.f);
  ScreenPt r = w2s_project(Vec3{100, 0, 100}, v, p, 16.f, 100, 100);
  CHECK(r.ok && r.x == 100.f);
  ScreenPt u = w2s_project(Vec3{0, 100, 100}, v, p, 16.f, 100, 100);
  CHECK(u.ok && u.y == 0.f);                         // +y is up on screen
  CHECK(!w2s_project(Vec3{0, 0, -50}, v, p, 16.f, 100, 100).ok);
  CHECK(!w2s_project(Vec3{0, 0, 16}, v, p, 16.f, 100, 100).ok);  // at the near plane
}
static void tb_0007_w2s_view_translation_applies() {
  Mat4 v = ident(); v.m[14] = -50.f;                 // camera 50 units forward
  Mat4 p = proj90(16.f, 8000.f, 100, 100);
  ScreenPt c = w2s_project(Vec3{0, 0, 100}, v, p, 16.f, 100, 100);
  CHECK(c.ok && c.x == 50.f && c.y == 50.f);
  CHECK(!w2s_project(Vec3{0, 0, 60}, v, p, 16.f, 100, 100).ok);  // z_view 10 < near
}
static void tb_0008_jumpwin_edge_opens_and_expires() {
  JumpWin w = {0}; jw_update(&w, 0); CHECK(!jw_active(&w));
  jw_update(&w, 1); CHECK(jw_active(&w));
  for (int i = 0; i < 59; ++i) jw_update(&w, 0);
  CHECK(jw_active(&w)); jw_update(&w, 0); CHECK(!jw_active(&w));
  jw_update(&w, 1); for (int i = 0; i < 30; ++i) jw_update(&w, 0);
  jw_update(&w, 1); CHECK(w.left == 60);             // a new press re-opens the window
}
static void tb_0009_w2s_rejects_behind_when_near_is_not_positive() {
  Mat4 v = ident(), p = proj90(16.f, 8000.f, 100, 100);
  CHECK(!w2s_project(Vec3{0, 0, -50}, v, p, 0.f, 100, 100).ok);    // near 0, point behind
  CHECK(!w2s_project(Vec3{0, 0, -8}, v, p, -16.f, 100, 100).ok);   // garbage near < 0, w = -8
}

static void tb_0010_hostgate_matches_final_path_component_only() {
  CHECK(host_is_game(tb_path("G:|Games|Duke Nukem - Manhattan Project|Enhanced Edition|prism3d.exe").c_str()));
  CHECK(host_is_game("prism3d.exe"));
  CHECK(host_is_game("PRISM3D.EXE"));
  CHECK(host_is_game(tb_path("C:|Games|Prism3D.Exe").c_str()));
  CHECK(!host_is_game("DukeNukemMP.exe"));
  CHECK(!host_is_game("prism3d.exe.bak"));
  CHECK(!host_is_game(""));
}

static void tb_0011_engine_color_swaps_r_and_b() {
  CHECK(engine_color(0xFF2FE0D0u) == 0xFFD0E02Fu);   // measured cyan drew yellow
  CHECK(engine_color(0xFFD8402Fu) == 0xFF2F40D8u);   // measured red cross drew blue
  CHECK(engine_color(0xFFFFFFFFu) == 0xFFFFFFFFu);   // pure white, symmetric under swap
  CHECK(engine_color(0xFF000000u) == 0xFF000000u);   // pure alpha, no colour to swap
}

static void tb_0012_boost_classify_ignores_landing_reset() {
  CHECK(boost_classify(5.f, 5.f, 1.f) == BOOST_NONE);    // no change at all
  CHECK(boost_classify(9.f, 5.f, 1.f) == BOOST_AIR);     // changed to something other than e10: a real boost
  CHECK(boost_classify(1.f, 9.f, 1.f) == BOOST_RESET);   // changed back to e10: the landing reset (run-1 jf: e14==e10)
  CHECK(boost_classify(1.f, 1.f, 1.f) == BOOST_NONE);    // already at e10 and unchanged
}

static bool near_eq(float a, float b) { return fabsf(a - b) < 1e-4f; }
static void tb_0013_clip_rect_inside_is_unchanged() {
  const ClipRect c = { 0.f, 0.f, 200.f, 200.f };
  float x0 = 10, y0 = 20, x1 = 30, y1 = 40, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  CHECK(clip_rect_uv(&c, &x0, &y0, &x1, &y1, &u0, &v0, &u1, &v1) == 1);
  CHECK(x0 == 10.f && y0 == 20.f && x1 == 30.f && y1 == 40.f);
  CHECK(u0 == 0.f && v0 == 0.f && u1 == 1.f && v1 == 1.f);
}
static void tb_0014_clip_rect_fully_outside_returns_zero() {
  const ClipRect c = { 0.f, 0.f, 100.f, 100.f };
  float x0 = 150, y0 = 10, x1 = 180, y1 = 20, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  CHECK(clip_rect_uv(&c, &x0, &y0, &x1, &y1, &u0, &v0, &u1, &v1) == 0);
  float a0 = 10, b0 = -50, a1 = 20, b1 = 0, s0 = 0, t0 = 0, s1 = 1, t1 = 1;   // touching the top edge only
  CHECK(clip_rect_uv(&c, &a0, &b0, &a1, &b1, &s0, &t0, &s1, &t1) == 0);
}
static void tb_0015_clip_rect_half_left_moves_u_halfway() {
  const ClipRect c = { 50.f, 0.f, 300.f, 300.f };
  float x0 = 0, y0 = 0, x1 = 100, y1 = 10, u0 = 0.2f, v0 = 0, u1 = 0.4f, v1 = 1;
  CHECK(clip_rect_uv(&c, &x0, &y0, &x1, &y1, &u0, &v0, &u1, &v1) == 1);
  CHECK(x0 == 50.f && x1 == 100.f);
  CHECK(near_eq(u0, 0.3f) && near_eq(u1, 0.4f));
  CHECK(v0 == 0.f && v1 == 1.f);
}
static void tb_0016_clip_rect_all_four_sides() {
  const ClipRect c = { 10.f, 20.f, 90.f, 60.f };
  float x0 = 0, y0 = 0, x1 = 100, y1 = 100, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  CHECK(clip_rect_uv(&c, &x0, &y0, &x1, &y1, &u0, &v0, &u1, &v1) == 1);
  CHECK(x0 == 10.f && y0 == 20.f && x1 == 90.f && y1 == 60.f);
  CHECK(near_eq(u0, 0.1f) && near_eq(u1, 0.9f) && near_eq(v0, 0.2f) && near_eq(v1, 0.6f));
}
static void tb_0017_clip_rect_zero_area_clip_returns_zero() {
  const ClipRect c = { 50.f, 0.f, 50.f, 100.f };
  float x0 = 0, y0 = 0, x1 = 100, y1 = 100, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  CHECK(clip_rect_uv(&c, &x0, &y0, &x1, &y1, &u0, &v0, &u1, &v1) == 0);
}
static void tb_0018_px_ndc_round_trip_3440x1440() {
  const float xs[5] = { 0.f, 1.f, 1720.f, 3439.f, 3440.f }, ys[5] = { 0.f, 1.f, 720.f, 1439.f, 1440.f };
  for (int i = 0; i < 5; ++i) {
    CHECK(fabsf(ndc_to_px_x(ndc_x(xs[i], 3440), 3440) - xs[i]) < 1e-3f);
    CHECK(fabsf(ndc_to_px_y(ndc_y(ys[i], 1440), 1440) - ys[i]) < 1e-3f);
  }
}
static void tb_0019_clip_poly_triangle_half_clipped() {
  const ClipRect c = { 0.f, 0.f, 50.f, 200.f };
  const ClipVert tri[3] = { { 0, 0, 0, 0 }, { 100, 0, 1, 0 }, { 0, 100, 0, 1 } };
  ClipVert out[CLIP_POLY_MAX];
  const int n = clip_poly(&c, tri, 3, out);
  CHECK(n == 4);
  bool inside = true, mid = false;
  for (int i = 0; i < n; ++i) {
    if (out[i].x > 50.f + 1e-4f) inside = false;
    if (near_eq(out[i].x, 50.f) && near_eq(out[i].y, 0.f) && near_eq(out[i].u, 0.5f)) mid = true;
  }
  CHECK(inside); CHECK(mid);
  const ClipRect away = { 200.f, 200.f, 300.f, 300.f };
  CHECK(clip_poly(&away, tri, 3, out) == 0);
}

static void tb_0020_own_toggle_from_closed_takes_input() {
  OwnState s; own_init(&s);
  CHECK(own_event(&s, EV_TOGGLE) == (ACT_INPUT_OFF | ACT_RESET_IMPULSES));
  CHECK(s.open == 1);
}
static void tb_0021_own_toggle_from_open_gives_input_back() {
  OwnState s; own_init(&s); own_event(&s, EV_TOGGLE);
  CHECK(own_event(&s, EV_TOGGLE) == ACT_INPUT_ON);
  CHECK(s.open == 0);
}
static void tb_0022_own_activate_while_open_reasserts_off() {
  OwnState s; own_init(&s); own_event(&s, EV_TOGGLE);
  CHECK(own_event(&s, EV_ACTIVATE) == ACT_INPUT_OFF);
  CHECK(s.open == 1);
}
static void tb_0023_own_activate_while_closed_does_nothing() {
  OwnState s; own_init(&s);
  CHECK(own_event(&s, EV_ACTIVATE) == 0);
  CHECK(s.open == 0);
}
static void tb_0024_own_frame_while_open_reasserts_off() {
  OwnState s; own_init(&s);
  CHECK(own_event(&s, EV_FRAME) == 0);
  own_event(&s, EV_TOGGLE);
  CHECK(own_event(&s, EV_FRAME) == ACT_INPUT_OFF);
}
static void tb_0025_own_two_toggles_in_one_frame_end_closed() {
  OwnState s; own_init(&s);
  const int a = own_event(&s, EV_TOGGLE);
  const int b = own_event(&s, EV_TOGGLE);
  CHECK(a == (ACT_INPUT_OFF | ACT_RESET_IMPULSES));
  CHECK(b == ACT_INPUT_ON);                          // ON is the last action taken
  CHECK(s.open == 0);
  CHECK(own_event(&s, EV_FRAME) == 0);
}
static void tb_0026_cursor_scale_maps_client_to_render() {
  CHECK(cursor_scale(640, 1280, 3440) == 1720);      // window smaller than the render size
  CHECK(cursor_scale(1720, 3440, 1280) == 640);      // window larger
  CHECK(cursor_scale(-5, 1280, 3440) == 0);
  CHECK(cursor_scale(1280, 1280, 3440) == 3439);
  CHECK(cursor_scale(10, 0, 3440) == 0);             // minimised: no client area
}
static void tb_0027_activation_on_only_for_the_engine_on_branch() {
  CHECK(activation_turns_input_on(0x06, 1));              // WM_ACTIVATE, WA_ACTIVE
  CHECK(activation_turns_input_on(0x06, 0x00010002));     // WA_CLICKACTIVE, minimised flag in the high word
  CHECK(!activation_turns_input_on(0x06, 0x00010000));    // WA_INACTIVE: the engine turned input off
  CHECK(activation_turns_input_on(0x1C, 1));              // WM_ACTIVATEAPP, activating
  CHECK(!activation_turns_input_on(0x1C, 0));
  CHECK(activation_turns_input_on(0x212, 0));             // WM_EXITMENULOOP: always on
  CHECK(!activation_turns_input_on(0x211, 0));            // WM_ENTERMENULOOP: the engine's off
}
static void tb_0028_cursor_restore_steps_undo_drift() {
  CHECK(cursor_restore_steps(-1, -1, 1, 1) == 0);         // balanced open and close
  CHECK(cursor_restore_steps(-1, 3, 1, 1) == -4);         // alt-tab drift upward: four hides
  // In mouse_state 0 the count is dinput8's (its exclusive-mouse latch shows and
  // hides the cursor on unacquire/acquire, dinput8!0x10011f90); undoing its moves left the cursor at -4 for good.
  CHECK_MSG(cursor_restore_steps(0, -2, 0, 0) == 0, "mouse_state 0: never undo dinput8's moves");
  CHECK_MSG(cursor_restore_steps(-2, 0, 0, 0) == 0, "the measured case: open -2, close 0, no hides");
  CHECK(cursor_restore_steps(-1, 3, 1, 0) == 0);          // mouse_state changed while open: leave it
  CHECK(cursor_restore_steps(0, 1000, 1, 1) == -64);      // capped
  CHECK(cursor_restore_steps(0, -1000, 1, 1) == 64);
}

// Ported from AA tb_0012_panel_layout (duke3d-eduke32-aa tests/test_host.cpp:2375).
static void tb_0029_panel_layout() {
  PanelBox unset = {-1, -1, 0, 0};
  const PanelBox d = panel_layout(unset, 3440, 1440);
  CHECK_MSG(d.w >= PANEL_MIN_W && d.h >= PANEL_MIN_H,
            "an unset panel comes back at least the minimum size");
  CHECK_MSG(d.x >= 0 && d.y >= 0, "and on screen");

  PanelBox huge = {0, 0, 99999, 99999};
  const PanelBox c = panel_layout(huge, 1280, 720);
  CHECK_MSG(c.w <= 1280 && c.h <= 720, "a panel larger than the screen is clamped to it");

  PanelBox b = {100, 200, 600, 800};
  const UiRect g = panel_grip_rect(b);
  CHECK_MSG(g.x + g.w == b.x + b.w && g.y + g.h == b.y + b.h,
            "the grip is flush with the bottom-right corner");
  CHECK_MSG(ui_hit(g, b.x + b.w - 1, b.y + b.h - 1), "the corner pixel is in the grip");
  CHECK_MSG(!ui_hit(g, b.x, b.y), "the opposite corner is not");
}

// The two resolutions that matter: the box stays inside the screen.
static void tb_0030_panel_box_clamps_1280x720_and_3440x1440() {
  const PanelBox unset = {-1, -1, 0, 0};
  const PanelBox a = panel_layout(unset, 1280, 720);
  CHECK(a.x == PANEL_INSET && a.w == PANEL_W);
  CHECK(a.h == 720 && a.y == 0);   // the 660 inset box is under AA's 790 floor, which clamps to the screen
  const PanelBox b = panel_layout(unset, 3440, 1440);
  CHECK(b.w == PANEL_W && b.h == PANEL_H && b.y == (1440 - PANEL_H) / 2);
  // A box stored on a big screen and loaded on a small one comes back on it.
  const PanelBox away = {3000, 1300, 620, 900};
  const PanelBox c = panel_layout(away, 1280, 720);
  CHECK(c.x + c.w <= 1280 && c.y + c.h <= 720 && c.x >= 0 && c.y >= 0);
  CHECK(c.h == 720);
  const PanelBox tiny = {10, 10, 50, 50};
  const PanelBox t = panel_layout(tiny, 3440, 1440);
  CHECK(t.w == PANEL_MIN_W && t.h == PANEL_MIN_H);
}

// Ported from AA tb_0317_ui_panel_max_h_for (test_host.cpp:22838).
static void tb_0031_ui_panel_max_h_for() {
  const int screens[] = { 480, 600, 768, 1080, 1200, 1440 };
  const int pcts[]    = { 75, 80, 90, 100, 110, 125, 130, 150, 175, 200 };
  for (unsigned si = 0; si < sizeof(screens) / sizeof(screens[0]); ++si) {
    for (unsigned pi = 0; pi < sizeof(pcts) / sizeof(pcts[0]); ++pi) {
      const int screenH = screens[si], pct = pcts[pi];
      const int h = ui_panel_max_h_for(screenH, pct, PANEL_VINSET);
      if (h == 0x7fffffff) {
        CHECK_MSG(pct <= UI_SCALE_MIN, "only the floor scale has no boundary");
        continue;
      }
      CHECK_MSG(ui_scale_max_for(screenH, h, PANEL_VINSET) >= pct,
                "the answer itself must still clear pct");
      CHECK_MSG(ui_scale_max_for(screenH, h + 1, PANEL_VINSET) < pct,
                "one taller must not");
    }
  }
  const int h1440 = ui_panel_max_h_for(1440, 130, PANEL_VINSET);
  CHECK_MSG(ui_scale_max_for(1440, h1440, PANEL_VINSET) >= 130,
            "1440p at 130%: the reported case must clear its own scale");
  CHECK_MSG(ui_scale_max_for(1440, h1440 + 1, PANEL_VINSET) < 130,
            "and one logical pixel taller must drop it");
  CHECK_MSG(ui_panel_max_h_for(1440, UI_SCALE_MIN, PANEL_VINSET) == 0x7fffffff,
            "at the floor scale nothing needs capping");
}

// Ported from AA tb_0250..0252 (test_host.cpp:18849-18960). The byte sweeps step
// by 4 (64 values): the suite caps loops at 64.
static void tb_0032_bind_key_is_live_matches_label() {
  CHECK_MSG(bind_key_is_live(0x1B) == false, "Escape is not a bindable key");
  CHECK_MSG(bind_key_is_live(0) == false, "0 is the unbound sentinel");
  CHECK(bind_key_is_live(VK_BIND_RBUTTON)  == true);
  CHECK(bind_key_is_live(VK_BIND_MBUTTON)  == true);
  CHECK(bind_key_is_live(VK_BIND_XBUTTON1) == true);
  CHECK(bind_key_is_live(VK_BIND_XBUTTON2) == true);
  CHECK_MSG(bind_key_is_live(0x42) == true, "an ordinary key (B) is live");
  int mismatches = 0;
  for (int vk = 0; vk <= 255; vk += 4) {
    const bool live = bind_key_is_live(vk);
    const bool labelled = strcmp(bind_label(vk), "Unbound") != 0;
    if (live != labelled) mismatches++;
  }
  CHECK_MSG(mismatches == 0, "live iff the label is not Unbound");
}
static void tb_0033_bind_hold_is_active_requires_live_key() {
  CHECK(bind_hold_is_active(0, true) == false);
  CHECK(bind_hold_is_active(0x1B, true) == false);
  CHECK(bind_hold_is_active(0x42, true)  == true);
  CHECK(bind_hold_is_active(0x42, false) == false);
  int mismatches = 0;
  for (int vk = 0; vk <= 255; vk += 4)
    if (!bind_key_is_live(vk) && bind_hold_is_active(vk, true)) mismatches++;
  CHECK_MSG(mismatches == 0, "a non-live vk never reports held");
}
static void tb_0034_bind_menu_key_effective_matches_live() {
  CHECK(bind_menu_key_effective(0x11) == 0x2D);   // VK_CONTROL: unmappable, Insert instead
  CHECK(bind_menu_key_effective(0x10) == 0x2D);   // VK_SHIFT
  CHECK(bind_menu_key_effective(0x42) == 0x42);
  CHECK(bind_menu_key_effective(VK_BIND_RBUTTON)  == VK_BIND_RBUTTON);
  CHECK(bind_menu_key_effective(VK_BIND_MBUTTON)  == VK_BIND_MBUTTON);
  CHECK(bind_menu_key_effective(VK_BIND_XBUTTON1) == VK_BIND_XBUTTON1);
  CHECK(bind_menu_key_effective(VK_BIND_XBUTTON2) == VK_BIND_XBUTTON2);
  int mismatches = 0;
  for (int vk = 0; vk <= 255; vk += 4)
    if (!bind_key_is_live(bind_menu_key_effective(vk))) mismatches++;
  CHECK_MSG(mismatches == 0, "the effective key is always live");
}
static void tb_0035_bind_swallows_requires_live_key_and_window() {
  CHECK(bind_swallows(0x42, 0x42, true) == true);
  CHECK(bind_swallows(0x42, 0x42, false) == false);
  CHECK(bind_swallows(0x1B, 0x1B, true) == false);
  CHECK(bind_swallows(0x42, 0, true) == false);
  CHECK(bind_swallows(0x43, 0x42, true) == false);
}

// anim.h's core, from Megaton's tb_0099 (tests/test_phase8.cpp:570); spring runs
// capped at 64 steps of 16 ms, a second, past both pairs' settling time.
static void tb_0036_anim_core() {
  CHECK(anim_ease_out(0.0) == 0.0 && anim_ease_out(1.0) == 1.0);
  CHECK(anim_ease_out(0.5) > 0.8);
  CHECK(anim_ease_out(-1.0) == 0.0 && anim_ease_out(2.0) == 1.0);
  CHECK(anim_progress(1000u, 1000u, 150) == 0.0);
  CHECK(fabs(anim_progress(1000u, 1075u, 150) - 0.5) < 1e-9);
  CHECK(anim_progress(1000u, 9000u, 150) == 1.0);
  CHECK(anim_progress(0xFFFFFFF0u, 0x0000000Au, 100) > 0.25 && anim_progress(0xFFFFFFF0u, 0x0000000Au, 100) < 0.27);
  CHECK(anim_progress(0u, 5u, 0) == 1.0);
  AnimTween tw = { 10.0, 20.0, 1000u, 100 };
  CHECK(anim_tween_value(tw, 1000u) == 10.0 && anim_tween_value(tw, 1100u) == 20.0);
  CHECK(anim_tween_value(tw, 1050u) > 15.0);
  CHECK(anim_dt_clamped(1000u, 1016u) == 16.0);
  CHECK(anim_dt_clamped(1000u, 3000u) == 50.0);
  CHECK(anim_dt_clamped(1000u,  999u) == 0.0);
  AnimSpring one = { 0.0, 0.0 };
  anim_spring_step(&one, 1.0, 400.0, 40.0, 16.0);
  CHECK(one.x > 0.05);   // semi-implicit Euler moves on the first step
  AnimSpring s = { 0.0, 0.0 }; bool crossed = false;
  for (int i = 0; i < 64; i++) { anim_spring_step(&s, 1.0, 400.0, 40.0, 16.0); if (s.x > 1.0 + 1e-6) crossed = true; }
  CHECK(!crossed && anim_spring_settled(s, 1.0, 1e-3));
  AnimSpring g = { 0.0, 0.0 }; bool over = false;
  for (int i = 0; i < 64; i++) { anim_spring_step(&g, 1.0, 300.0, 22.0, 16.0); if (g.x > 1.02) over = true; }
  CHECK(over && anim_spring_settled(g, 1.0, 1e-3));
  CHECK(anim_blend_colour(0x80102030u, 0xFFFFFFFFu, 0.0) == 0x80102030u);
  CHECK(anim_blend_colour(0x80102030u, 0xFFA0B0C0u, 1.0) == 0x80A0B0C0u);   // alpha kept
}

// Ported from AA tb_0208_menu_scroll_rules (test_host.cpp:14627).
static void tb_0037_menu_scroll_rules() {
  CHECK(menu_scroll_max(800, 825) == 0);
  CHECK(menu_scroll_max(825, 825) == 0);
  CHECK(menu_scroll_max(866, 825) == 41);
  CHECK(menu_scroll_clamp(40, 800, 825) == 0);
  CHECK(menu_scroll_clamp(-5, 866, 825) == 0);
  CHECK(menu_scroll_clamp(999, 866, 825) == 41);
  CHECK(menu_scroll_after_wheel(0, 1.0, 866, 825) == 0);
  CHECK(menu_scroll_after_wheel(41, -9.0, 866, 825) == 41);
  CHECK(menu_scroll_after_wheel(0, -1.0, 4000, 825) == MENU_SCROLL_STEP);
  CHECK(menu_scroll_after_wheel(MENU_SCROLL_STEP, 1.0, 4000, 825) == 0);
  CHECK(menu_scroll_after_wheel(400, 0.0, 866, 825) == 41);
  CHECK(menu_scroll_thumb_h(825, 866, 825) == (825 * 825) / 866);
  CHECK(menu_scroll_thumb_h(825, 100000, 825) == MENU_SCROLLBAR_MIN_THUMB);
  CHECK(menu_scroll_thumb_h(825, 800, 825) == 825);
  const int th = menu_scroll_thumb_h(825, 866, 825);
  CHECK(menu_scroll_thumb_y(825, th, 0, 866, 825) == 0);
  CHECK(menu_scroll_thumb_y(825, th, 41, 866, 825) == 825 - th);
  CHECK(MENU_MOUSE_NOWHERE < PAD - MENU_CONTENT_BUDGET);
}

// Our WndProc toggles on key-downs only, and a DInput-exclusive mouse may send it
// no button messages, so a mouse panel key would leave the panel unreachable.
static void tb_0038_panel_key_effective_is_a_key() {
  CHECK(panel_key_effective(0x2D) == 0x2D);
  CHECK(panel_key_effective(0x70) == 0x70);             // F1
  CHECK(panel_key_effective(VK_BIND_RBUTTON) == 0x2D);
  CHECK(panel_key_effective(VK_BIND_XBUTTON2) == 0x2D);
  CHECK(panel_key_effective(0x10) == 0x2D);             // VK_SHIFT: unmappable
  CHECK(panel_key_effective(0) == 0x2D);
  CHECK(panel_key_effective(-5) == 0x2D && panel_key_effective(4096) == 0x2D);
}

// Temp files for the config tests: written, read back, deleted.
static void temp_ini(char* out, int cap, const char* name) {
  char dir[MAX_PATH];
  const DWORD n = GetTempPathA(MAX_PATH, dir);
  _snprintf_s(out, cap, _TRUNCATE, "%s%s_%lu.ini", (n && n < MAX_PATH) ? dir : ".\\", name,
              GetCurrentProcessId());
}
static void write_text(const char* path, const char* text) {
  FILE* f = NULL;
  if (fopen_s(&f, path, "wb") == 0 && f) { fputs(text, f); fclose(f); }
}
static bool defaults_equal(const Config& c) {
  return c.menukey == 0x2D && c.panelx == -1 && c.panely == -1 && c.panelw == 0 &&
         c.panelh == 0 && c.animations == 1 && c.timing == 1;
}
static void tb_0039_config_missing_file_gives_defaults() {
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_missing");
  DeleteFileA(path);
  g_cfg.menukey = 99; g_cfg.animations = 0;
  CHECK(config_load_from(path) == false);
  CHECK(defaults_equal(g_cfg));
  CHECK(g_cfgDirty == 0);
}
static void tb_0040_config_garbage_value_gives_default() {
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_garbage");
  write_text(path, "[panel]\r\nkey=banana\r\nx=12\r\ny=7q\r\nw= 700 \r\nanimations=7\r\n"
                   "[debug]\r\ntiming=\r\n");
  CHECK(config_load_from(path) == true);
  CHECK(g_cfg.menukey == 0x2D);       // not a number
  CHECK(g_cfg.panelx == 12);
  CHECK(g_cfg.panely == -1);          // trailing junk
  CHECK(g_cfg.panelw == 700);         // spaces around a number are fine
  CHECK(g_cfg.animations == 1);       // out of range
  CHECK(g_cfg.timing == 1);           // empty
  // A key of the right name in the wrong section is not ours.
  write_text(path, "[debug]\r\nkey=70\r\n[panel]\r\ntiming=0\r\n");
  config_load_from(path);
  CHECK(g_cfg.menukey == 0x2D && g_cfg.timing == 1);
  DeleteFileA(path);
}
static void tb_0041_config_unknown_key_survives_write() {
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_unknown");
  write_text(path, "; the player's note\r\n[panel]\r\nfuture=42\r\nkey=112\r\n[other]\r\nkeep=me\r\n");
  config_load_from(path);
  CHECK(g_cfg.menukey == 112);
  g_cfg.animations = 0; g_cfg.panelx = 300; g_cfgDirty = 1;
  config_write_changed();
  CHECK(g_cfgDirty == 0);
  char text[1024] = { 0 };
  FILE* f = NULL;
  if (fopen_s(&f, path, "rb") == 0 && f) { fread(text, 1, sizeof text - 1, f); fclose(f); }
  CHECK(strstr(text, "; the player's note") != NULL);
  CHECK(strstr(text, "future=42") != NULL);
  CHECK(strstr(text, "keep=me") != NULL);
  CHECK(strstr(text, "key=112") != NULL);            // unchanged keys are not rewritten
  CHECK(strstr(text, "animations=0") != NULL);
  CHECK(strstr(text, "x=300") != NULL);
  CHECK(strstr(text, "timing") == NULL);             // unchanged: never written
  config_load_from(path);
  CHECK(g_cfg.animations == 0 && g_cfg.panelx == 300 && g_cfg.menukey == 112);
  DeleteFileA(path);
}

// zoom.h's pure rules (cases verbatim).
static void tb_0042_zoom_fov_level_one_returns_stock() {
  CHECK(fabs(zoom_fov(90.0, 1.0) - 90.0) < 0.01);
  CHECK(fabs(zoom_fov(79.755, 1.0) - 79.755) < 0.01);
}
static void tb_0043_zoom_fov_level_two_at_stock_90() {
  CHECK(fabs(zoom_fov(90.0, 2.0) - 53.13) < 0.01);
}
// 2*atan(tan(79.755/2)/4) is 23.595 deg (double precision, checked against the level-2/stock-90 case too).
static void tb_0044_zoom_fov_level_four_at_stock_79_755() {
  CHECK(fabs(zoom_fov(79.755, 4.0) - 23.595) < 0.05);
}
static void tb_0045_zoom_fov_level_below_one_clamps() {
  CHECK(fabs(zoom_fov(90.0, 0.5) - zoom_fov(90.0, 1.0)) < 1e-9);
  CHECK(fabs(zoom_fov(90.0, -3.0) - zoom_fov(90.0, 1.0)) < 1e-9);
}
static void tb_0046_zoom_fov_stock_outside_open_interval_unchanged() {
  CHECK(zoom_fov(1.0, 2.0) == 1.0);
  CHECK(zoom_fov(0.5, 2.0) == 0.5);
  CHECK(zoom_fov(179.0, 2.0) == 179.0);
  CHECK(zoom_fov(200.0, 2.0) == 200.0);
}
static void tb_0047_zoom_should_write_tolerance() {
  CHECK(zoom_should_write("53.13", 53.13) == 0);
  CHECK(zoom_should_write("53.1", 53.13) == 1);
  CHECK(zoom_should_write("53.1295", 53.13) == 0);   // within 0.001
  CHECK(zoom_should_write("50", 53.13) == 1);
  CHECK(zoom_should_write("53.13", 53.1295) == 0);
}

// A release immediately followed by a re-apply must not let the release frame's stale (pre-write-back) cvar read
// corrupt the remembered stock. Traces the pure decision only (zoom.cpp's glue is outside the suite): frame N applies,
// N+1 releases (writes), N+2 re-applies.
static void tb_0048_zoom_may_recapture_skips_release_and_write_frames() {
  // Steady not-applying, nothing written: the ordinary case, safe to recapture.
  CHECK(zoom_may_recapture(0, 0, 0) == 1);
  // N+1, the release-transition frame: was applying last frame, released and
  // wrote this frame -- cur is still the stale zoomed value. Must refuse.
  CHECK(zoom_may_recapture(0, 1, 1) == 0);
  // N+2, an immediate re-apply: applying now, so recapture never applies (the
  // question is moot, but must never say yes).
  CHECK(zoom_may_recapture(1, 0, 0) == 0);
  CHECK(zoom_may_recapture(1, 1, 1) == 0);
  // Any frame that wrote, even without a transition, must refuse: a write
  // this frame means cur (read before the write) cannot be trusted as stock.
  CHECK(zoom_may_recapture(0, 0, 1) == 0);
  // Back to steady the frame after: safe again.
  CHECK(zoom_may_recapture(0, 0, 0) == 1);
}

// framecap.h's pure rules (cases verbatim).
static void tb_0049_fc_next_deadline_cases() {
  // On schedule: returns prev + period.
  CHECK(fc_next_deadline(1000, 1050, 100) == 1100);
  // now more than a period late (a stall): resyncs to now + period rather
  // than bursting -- 1250 is 150 past next=1100, more than period=100.
  CHECK(fc_next_deadline(1000, 1250, 100) == 1350);
  // Exactly one period late is still "on schedule", not a resync.
  CHECK(fc_next_deadline(1000, 1200, 100) == 1100);
  // period 0 means no cap: always now.
  CHECK(fc_next_deadline(1000, 1234, 0) == 1234);
}
static void tb_0050_fc_wait_ticks_zero_when_past() {
  CHECK(fc_wait_ticks(1100, 1050) == 50);
  CHECK(fc_wait_ticks(1100, 1100) == 0);
  CHECK(fc_wait_ticks(1100, 1150) == 0);
}
// 64 simulated frames at 175 Hz with a 5.8 ms render: every frame's total (render + wait) is within 0.1 ms of 1/175 s
// after the first. Ticks of 1/175,000,000 s make the period (1e6) and the render (1,015,000) exact integers.
static void tb_0051_fc_pacing_64_frames_at_175hz() {
  const long long freq = 175000000;
  const long long period = freq / 175;              // 1,000,000 ticks
  const long long renderTicks = 1015000;             // 5.8 ms
  const long long tolTicks = (long long)(0.0001 * freq);   // 0.1 ms = 17,500 ticks
  long long now = 0;
  long long deadline = now + period;                 // first frame's schedule
  for (int i = 0; i < 64; ++i) {
    const long long frameStart = now;
    now += renderTicks;                              // the render
    const long long wait = fc_wait_ticks(deadline, now);
    now += wait;                                      // the pacing wait
    if (i > 0) {
      const long long total = now - frameStart;
      const long long diff = total > period ? total - period : period - total;
      CHECK(diff <= tolTicks);
    }
    deadline = fc_next_deadline(deadline, now, period);
  }
}

// The sentinel-to-effective rule, pure so a computed default can never leak into g_cfg and be persisted by
// config_write_changed on an unrelated change: framecap.cpp never writes g_cfg.frameCapFps, it only calls this
// against the stored value read fresh each frame.
static void tb_0052_fc_effective_fps_sentinel_rule() {
  CHECK(fc_effective_fps(0, 175) == 175);    // stored 0 (unset): follow the monitor
  CHECK(fc_effective_fps(0, 0) == 60);       // stored 0, monitor not yet known: fallback
  CHECK(fc_effective_fps(120, 175) == 120);  // an explicit player choice always wins
  CHECK(fc_effective_fps(30, 0) == 30);      // explicit, at the slider's floor
  CHECK(fc_effective_fps(240, 60) == 240);   // explicit, at the slider's ceiling
  CHECK(fc_effective_fps(-1, 0) == 60);      // any stale/garbage stored value: fallback too
}

// gl_sync forces vsync at the monitor refresh, so the
// Cap slider's hi and the effective fps must both fall to it.
static void tb_0053_fc_cap_clamp_vsync_cases() {
  FcCapClamp c = fc_cap_clamp(0, 175, 1);
  CHECK(c.sliderHi == 175 && c.effectiveFps == 175);
  c = fc_cap_clamp(200, 175, 1);
  CHECK(c.sliderHi == 175 && c.effectiveFps == 175);
  c = fc_cap_clamp(120, 175, 1);
  CHECK(c.sliderHi == 175 && c.effectiveFps == 120);
  c = fc_cap_clamp(200, 175, 0);
  CHECK(c.sliderHi == 240 && c.effectiveFps == 200);
  c = fc_cap_clamp(0, 0, 1);
  CHECK(c.sliderHi == 240 && c.effectiveFps == 60);
}

// classes.h: the class-name lookup, cases verbatim.
static bool name_is(const EntClass& c, const char* want) { return c.name && strcmp(c.name, want) == 0; }
static void tb_0054_class_lookup_categories_and_tiers() {
  const EntClass a = class_lookup("monst_pigoid1");
  CHECK(a.cat == CAT_ENEMY && name_is(a, "Pig Cop"));
  // The name is bare (2026-09-28 user decision): the tier variant is a
  // separate label (class_variant_label), pinned by tb_0101 below.
  CHECK(name_is(class_lookup("monst_ratoid3"), "Rat Oid"));
  CHECK(name_is(class_lookup("monst_femechB"), "Fem Mech"));
  const EntClass w = class_lookup("boss_wozma");
  CHECK(w.cat == CAT_BOSS && name_is(w, "Wozma"));
  CHECK(class_lookup("monst_talker_mgr").cat == CAT_OTHER);
  CHECK(class_lookup("actor_proj_bullet").cat == CAT_OTHER);
  CHECK(class_lookup(NULL).cat == CAT_OTHER);
  CHECK(name_is(class_lookup("monst_pigoid"), "Pig Cop"));
  CHECK(name_is(class_lookup("monst_pig"), "Pig"));                 // a key that prefixes another key
  CHECK(name_is(class_lookup("boss_porkchopper_child"), "Helicopter Pod"));
}

// espbox.h: 8 projected corners -> one screen box on a 100x100 screen.
static void set_corners(ScreenPt c[8], float x0, float y0, float x1, float y1) {
  for (int i = 0; i < 8; ++i) { c[i].x = (i & 1) ? x1 : x0; c[i].y = (i & 2) ? y1 : y0; c[i].ok = 1; }
}
static void tb_0055_esp_box_from_corners() {
  ScreenPt c[8];
  int ok = 0;
  set_corners(c, 20.f, 30.f, 60.f, 80.f);
  EkScreenBox b = esp_box_from_corners(c, 100, 100, &ok);
  CHECK(ok && b.x == 20 && b.y == 30 && b.w == 40 && b.h == 50);
  // Near corners behind the camera, the rest left of centre and straddling it
  // vertically: left, top and bottom run to the edges, the right side stays.
  set_corners(c, 20.f, 40.f, 40.f, 60.f);
  for (int i = 4; i < 8; ++i) { c[i].x = c[i].y = 0.f; c[i].ok = 0; }
  b = esp_box_from_corners(c, 100, 100, &ok);
  CHECK(ok && b.x == 0 && b.y == 0 && b.w == 40 && b.h == 100);
  set_corners(c, 120.f, 20.f, 150.f, 60.f);                       // entirely right of the screen
  esp_box_from_corners(c, 100, 100, &ok);
  CHECK(ok == 0);
  set_corners(c, 50.f, 50.f, 50.f, 50.f);                         // degenerate: one point
  b = esp_box_from_corners(c, 100, 100, &ok);
  CHECK(ok && b.x == 50 && b.y == 50 && b.w == 1 && b.h == 1);
  for (int i = 0; i < 8; ++i) c[i].ok = 0;                        // nothing in front
  esp_box_from_corners(c, 100, 100, &ok);
  CHECK(ok == 0);
}

// esp_project_box: the centre gate plus the 8 corners through w2s_project.
static void tb_0056_esp_project_box_centre_gate() {
  const Mat4 v = ident(), p = proj90(16.f, 8000.f, 100, 100);
  int ok = 0;
  EkScreenBox b = esp_project_box(Vec3{0, 0, 100}, Vec3{10, 10, 10}, v, p, 16.f, 100, 100, &ok);
  CHECK(ok && b.x == 44 && b.y == 44 && b.w == 12 && b.h == 12);   // 50 -+ 500/90
  esp_project_box(Vec3{0, 0, -50}, Vec3{10, 10, 10}, v, p, 16.f, 100, 100, &ok);
  CHECK(ok == 0);                                                  // centre behind: no box
  b = esp_project_box(Vec3{0, 0, 20}, Vec3{10, 10, 10}, v, p, 16.f, 100, 100, &ok);
  CHECK(ok && b.x == 0 && b.y == 0 && b.w == 100 && b.h == 100);   // near face behind, straddling
}

// esp_layout.h, ported from AA tb_0014 (the dim loop capped at 64 values).
static void tb_0057_esp_markers() {
  // The brackets must not meet: an even edge is the case that would close.
  for (int dim = 2; dim <= 128; dim += 2) {
    const int arm = esp_bracket_arm(dim);
    CHECK_MSG(arm * 2 < dim, "two opposing arms never meet on an even edge");
  }
  CHECK_MSG(esp_bracket_arm(0) == 0, "a zero edge has no arm");
  CHECK_MSG(esp_bracket_arm(-5) == 0, "and neither does a negative one");
  CHECK_MSG(esp_bracket_arm(30) == 10, "the arm is a third of the edge");
  CHECK_MSG(esp_bracket_thickness(0, 10) == 0, "thickness never exceeds the shorter arm");
  CHECK_MSG(esp_bar_x(100) < 100, "the health bar is left of the box");
  CHECK_MSG(esp_bar_x(100) + ESP_BAR_W <= 100, "and does not overlap it");
  CHECK_MSG(esp_label_x(100, 80, 40) == 120, "a label is centred on the box");
  CHECK_MSG(esp_name_label_y(200) < 200, "the name sits above the box");
  CHECK_MSG(esp_dist_label_y(200, 80) > 280, "and the distance below it");
}

// AA tb_0038 without the state label (this game has no state source).
static void tb_0058_esp_health_label() {
  char buf[ESP_HEALTH_NUM_BUFSIZE];
  CHECK_MSG(esp_health_number(buf, 40, 0) && strcmp(buf, "40") == 0, "max 0 is the bare form");
  CHECK_MSG(esp_health_number(buf, 40, 100) && strcmp(buf, "40/100") == 0, "a known max is cur/max");
  CHECK_MSG(esp_health_number(buf, 4600, 4500) && strcmp(buf, "4600/4500") == 0,
            "hp above max prints honestly, not clamped");
  CHECK_MSG(!esp_health_number(buf, 0, 100), "hp 0 draws no number run");
  CHECK_MSG(!esp_health_number(buf, -5, 100), "negative hp does not reach the number run");
  const EspLabelRuns r = esp_label_runs(100, 80, 40, 30);
  CHECK_MSG(r.totalW == 40 + ESP_LABEL_RUN_GAP + 30, "total width is both runs plus the gap");
  CHECK_MSG(r.numX == r.nameX + 40 + ESP_LABEL_RUN_GAP, "the number run sits after the name run");
  const EspLabelRuns n = esp_label_runs(100, 80, 0, 30);
  CHECK_MSG(n.totalW == 30 && n.numX == n.nameX, "no name: no gap, the number alone is centred");
  CHECK_MSG(esp_label_mono_y(200, 17, 12) == 200 + 17 - 12, "mono run bottom-aligns to the name run");
  CHECK(esp_hp_int(0.4f) == 1);    // alive below one still reads as alive
  CHECK(esp_hp_int(37.0f) == 37);
  CHECK(esp_hp_int(-3.0f) == 0);
}

// AA tb_0126: keep-nearest and far-to-near are two different questions.
static void tb_0059_marker_pipeline() {
  EkMarker m[8];
  memset(m, 0, sizeof(m));
  const int d[8] = { 500, 100, 300, 900, 300, 50, 700, 200 };
  for (int i = 0; i < 8; i++) {
    m[i].kind = EK_MARKER_ENEMY; m[i].index = i; m[i].seq = i;
    m[i].dist2d = d[i]; m[i].p.dist2d = d[i];
  }
  EkMarker k[8];
  memcpy(k, m, sizeof(m));
  ekm_keep_nearest(k, 8, 3);
  CHECK(k[0].dist2d == 50 && k[1].dist2d == 100 && k[2].dist2d == 200);
  EkMarker n[8];
  memcpy(n, m, sizeof(m));
  ekm_keep_nearest(n, 8, 8);
  for (int i = 0; i < 8; i++) CHECK(n[i].dist2d == d[i]);
  memcpy(n, m, sizeof(m));
  ekm_keep_nearest(n, 8, 0);
  for (int i = 0; i < 8; i++) CHECK(n[i].dist2d == d[i]);
  ekm_keep_nearest(0, 8, 3);    // null is survivable, not a crash
  EkMarker s[8];
  memcpy(s, m, sizeof(m));
  ekm_sort_far_to_near(s, 8);
  for (int i = 1; i < 8; i++)
    CHECK_MSG(s[i - 1].dist2d >= s[i].dist2d, "far to near is descending");
  CHECK(s[0].dist2d == 900 && s[7].dist2d == 50);
  int firstSeq = -1, secondSeq = -1;
  for (int i = 0; i < 8; i++)
    if (s[i].dist2d == 300) { if (firstSeq < 0) firstSeq = s[i].seq; else secondSeq = s[i].seq; }
  CHECK_MSG(firstSeq == 2 && secondSeq == 4, "equal distances keep collection order");
  // Select then order: the kept three paint far to near.
  memcpy(k, m, sizeof(m));
  ekm_keep_nearest(k, 8, 3);
  ekm_sort_far_to_near(k, 3);
  CHECK(k[0].dist2d == 200 && k[1].dist2d == 100 && k[2].dist2d == 50);
  ekm_sort_far_to_near(s, 1);
  ekm_sort_far_to_near(0, 8);
}

// 2D distance at 52 units a metre, the range cull, and who counts as alive.
static void tb_0060_esp_distance_range_alive() {
  CHECK(fabsf(esp_dist_m(312.f, 416.f) - 10.f) < 1e-4f);   // 3-4-5: 520 units
  CHECK(ekm_within_draw_distance(5200, 5200, 0));
  CHECK(!ekm_within_draw_distance(5201, 5200, 0));
  CHECK(ekm_within_draw_distance(999999, 52, 1));            // no limit ignores the distance
  CHECK(!ekm_within_draw_distance(-1, 5200, 0));
  EntSnap e;
  memset(&e, 0, sizeof(e));
  e.hashp = 0; e.hp = 0.f;
  CHECK_MSG(ent_alive(e), "no monster health (Mech Nukem, Wozma, Crane): always alive");
  e.hashp = 1; e.hp = 0.f;
  CHECK_MSG(!ent_alive(e), "a monster at 0 hp is dead");
  e.hp = 5.f;
  CHECK(ent_alive(e));
}

// No draw distance (2026-09-28): collected only when both facts hold.

// Visuals is a visible tab now, and the ESP keys default on at the maximum range.
static void tb_0061_visuals_tab_and_esp_defaults() {
  CHECK(menu_tab_visible(TAB_VISUALS));
  CHECK(menu_tab_visible(TAB_SETTINGS));
  CHECK(menu_tab_visible(TAB_VIEW));    // View tab is visible because it has rows (zoom out's CAMERA group)
  config_defaults();
  CHECK(g_cfg.espOn == 1 && g_cfg.espNames == 1 && g_cfg.espHealthBars == 1);
  CHECK(g_cfg.espHealthNumbers == 1 && g_cfg.espDistance == 1);
  CHECK(g_cfg.espDrawDist == ESP_DRAWDIST_MAX_M && g_cfg.espNoLimit == 0);
}

// pullback.h: five cases. A camera turned 90 degrees about Y looks
// down world +x: right (0,0,-1), up (0,1,0), forward (1,0,0), eye (5,2,3).
// The view (world->camera) of an orthonormal camera->world matrix, by transpose.
static void view_of_inverse(const float inv[16], float view[16]) {
  memset(view, 0, 16 * sizeof(float));
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) view[i * 4 + j] = inv[j * 4 + i];
  for (int j = 0; j < 3; ++j)
    view[12 + j] = -(inv[12] * inv[j * 4] + inv[13] * inv[j * 4 + 1] + inv[14] * inv[j * 4 + 2]);
  view[15] = 1.f;
}
static void rotated_pair(float view[16], float inv[16]) {
  const float r[16] = { 0, 0, -1, 0,   0, 1, 0, 0,   1, 0, 0, 0,   5, 2, 3, 1 };
  memcpy(inv, r, sizeof(r));
  view_of_inverse(inv, view);
}
static void tb_0062_pullback_zero_is_unchanged() {
  float v[16], i[16], ov[16], oi[16];
  rotated_pair(v, i);
  pullback(v, i, 0.f, ov, oi);
  CHECK(memcmp(v, ov, sizeof(v)) == 0 && memcmp(i, oi, sizeof(i)) == 0);
}
static void tb_0063_pullback_identity_d10() {
  const Mat4 id = ident();
  float ov[16], oi[16];
  pullback(id.m, id.m, 10.f, ov, oi);
  CHECK(oi[14] == -10.f && ov[14] == 10.f);
  int others = 0;
  for (int k = 0; k < 16; ++k) if (k != 14 && (oi[k] != id.m[k] || ov[k] != id.m[k])) others++;
  CHECK_MSG(others == 0, "only the z translation moves");
}
static void tb_0064_pullback_rotated_eye_moves_back_along_forward() {
  float v[16], i[16], ov[16], oi[16];
  rotated_pair(v, i);
  pullback(v, i, 4.f, ov, oi);
  CHECK(near_eq(oi[12], 1.f) && near_eq(oi[13], 2.f) && near_eq(oi[14], 3.f));   // (5,2,3) - 4*(1,0,0)
  CHECK(memcmp(oi, i, 12 * sizeof(float)) == 0);                                  // axes unchanged
}
static void tb_0065_pullback_rotated_stays_inverse() {
  float v[16], i[16], ov[16], oi[16];
  rotated_pair(v, i);
  pullback(v, i, 4.f, ov, oi);
  int off = 0;
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) {
      float s = 0.f;
      for (int k = 0; k < 4; ++k) s += ov[r * 4 + k] * oi[k * 4 + c];
      if (fabsf(s - (r == c ? 1.f : 0.f)) > 1e-4f) off++;
    }
  CHECK_MSG(off == 0, "outView * outInv is the identity");
}
static void tb_0066_pullback_forward_point_depth_grows_by_d() {
  float v[16], i[16], ov[16], oi[16];
  rotated_pair(v, i);
  pullback(v, i, 4.f, ov, oi);
  Mat4 V; memcpy(V.m, ov, sizeof(ov));
  const float p[4] = { 25.f, 2.f, 3.f, 1.f };   // 20 units ahead of the unpulled eye
  float c[4];
  w2s_mul(p, V, c);
  CHECK(near_eq(c[2], 24.f) && near_eq(c[0], 0.f) && near_eq(c[1], 0.f));
}

// Zoom out's keys and bind row: off, 8 m, unbound; the row reads and writes its own key.
static void tb_0067_zoomout_defaults_and_bind_row() {
  config_defaults();
  CHECK(g_cfg.zoomOutOn == 0 && g_cfg.zoomOutDist == ZOOMOUT_DEF_M && g_cfg.holdZoomOutKey == 0);
  config_bind_set(BIND_ROW_HOLDZOOMOUT, 0x5A);
  CHECK(config_bind_get(BIND_ROW_HOLDZOOMOUT) == 0x5A && g_cfg.holdZoomOutKey == 0x5A);
  CHECK(config_bind_get(BIND_ROW_HOLDZOOM) == g_cfg.holdZoomKey);
  config_defaults();
}

// classes.h: the pickup, objective and secret classes.
static void tb_0068_class_lookup_pickups_objectives_secrets() {
  CHECK(class_lookup("nuke").cat == CAT_NUKE);
  CHECK(class_lookup("babe").cat == CAT_BABE);
  CHECK(class_lookup("key").cat == CAT_KEY);
  CHECK(class_lookup("area_secret").cat == CAT_SECRET);
  const char* cls[6] = { "ammo", "life", "ego", "gun", "jetpack", "forcefield" };
  const int kinds[6] = { ITEM_AMMO, ITEM_LIFE, ITEM_EGO, ITEM_GUN, ITEM_JETPACK, ITEM_FORCEFIELD };
  const char* names[6] = { "Ammo", "Double Damage", "Ego", "Weapon", "Jetpack", "Force Field" };
  for (int i = 0; i < 6; ++i) {
    const EntClass c = class_lookup(cls[i]);
    CHECK_MSG(c.cat == CAT_ITEM && c.kind == kinds[i] && name_is(c, names[i]), cls[i]);
  }
  CHECK(class_lookup("key_hole").cat == CAT_OTHER);         // a key prefix, not a key
  CHECK(class_lookup("actor_jetpack").cat == CAT_OTHER);    // the worn jetpack, not the pickup
  CHECK(class_lookup("gun1").cat == CAT_OTHER);             // pickups take no tier suffix
  CHECK(class_lookup("monst_pig").kind == ITEM_NONE);
}
static void tb_0069_keycard_colour_name() {
  CHECK(strcmp(keycard_colour_name(0), "Blue Key") == 0);
  CHECK(strcmp(keycard_colour_name(1), "Yellow Key") == 0);
  CHECK(strcmp(keycard_colour_name(2), "Red Key") == 0);
  CHECK(strcmp(keycard_colour_name(3), "Key") == 0);
  CHECK(strcmp(keycard_colour_name(-1), "Key") == 0);
}

// esp_layout.h: the secret/objective cross mark, ported from AA's tb_0126-area
// case (tests/test_host.cpp:10338-10351), verbatim numbers.
static void tb_0070_esp_cross_mark_geometry() {
  CHECK(esp_secret_bar_span() == ESP_SECRET_ARM * 2 + 1);
  CHECK(esp_secret_hbar_x(100) == 100 - ESP_SECRET_ARM);
  CHECK(esp_secret_vbar_y(100) == 100 - ESP_SECRET_ARM);
  CHECK(esp_secret_hbar_y(100) == 100 - ESP_SECRET_T / 2);
  CHECK(esp_secret_vbar_x(100) == 100 - ESP_SECRET_T / 2);
  CHECK(esp_secret_name_label_y(100) == 100 - ESP_SECRET_ARM - ESP_LABEL_GAP);
  CHECK(esp_secret_dist_label_y(100) == 100 + ESP_SECRET_ARM + 1 + ESP_DIST_GAP);
}

// canvas.h: marker_colour(cat, keyColour). The box stays generic blue
// (world-features.md section 2); this is only the label/cross colour.
static void tb_0071_marker_colour() {
  CHECK(marker_colour(CAT_ITEM, -1) == C_BLUE);
  CHECK(marker_colour(CAT_KEY, 0) == C_BLUE);
  CHECK(marker_colour(CAT_KEY, 1) == C_HAZARD);
  CHECK(marker_colour(CAT_KEY, 2) == C_RED);
  CHECK_MSG(marker_colour(CAT_KEY, 3) == C_BLUE, "an out-of-range card colour reads as a plain key");
  CHECK(marker_colour(CAT_KEY, -1) == C_BLUE);
  CHECK(marker_colour(CAT_NUKE, -1) == C_GREEN);
  CHECK(marker_colour(CAT_BABE, -1) == C_FRIENDLY);
  CHECK(marker_colour(CAT_SECRET, -1) == C_HAZARD);
}

// config.h: every new category switch defaults on except Secret volumes (translucent boxes add clutter).
// The Items tab it once checked is gone (tb_0299).
static void tb_0072_items_tab_and_category_esp_defaults() {
  config_defaults();
  CHECK(g_cfg.espItems == 1);
  CHECK(g_cfg.espNukes == 1 && g_cfg.espNukesArrow == 1);
  CHECK(g_cfg.espBabes == 1 && g_cfg.espBabesArrow == 1);
  CHECK(g_cfg.espSecrets == 1 && g_cfg.espSecretsArrow == 1);
  CHECK(g_cfg.espSecretVolumes == 0);
}

// arrows.h: an off-screen target's arrow sits where the ray from the screen
// centre leaves the 40 px inset rect; behind the camera it follows camera-space
// (x, -y), never the mirrored projection.
static void tb_0073_edge_arrow_direction() {
  const int W = 800, H = 600;
  const float m = arrow_margin(100);
  CHECK(m == 40.f);
  Mat4 v = ident(), p = proj90(16.f, 8000.f, W, H);
  EdgeArrow a;
  CHECK(arrow_make(Vec3{300, 0, 100}, v, p, 16.f, W, H, m, 7, &a));
  CHECK_MSG(a.x == 760.f && a.y == 300.f && a.dx == 1.f && a.dy == 0.f, "right target: right inset edge");
  CHECK(a.tag == 7);
  float px[3], py[3];
  arrow_triangle(a, px, py);
  CHECK_MSG(px[0] == 760.f && px[1] < 760.f && px[2] < 760.f, "tip on the edge, base inward: points right");
  CHECK(fabsf(py[1] - py[2]) == (float)ARROW_BASE && px[0] - px[1] == (float)ARROW_LEN);
  // Projected naively this lands at x = 600, the right half.
  CHECK(arrow_make(Vec3{-50, 0, -100}, v, p, 16.f, W, H, m, 0, &a));
  CHECK_MSG(a.x == 40.f && a.y == 300.f && a.dx < 0.f, "behind-left: left edge, not mirrored right");
  CHECK(arrow_make(Vec3{0, 0, -100}, v, p, 16.f, W, H, m, 0, &a));
  CHECK_MSG(a.x == 400.f && a.y == 560.f && a.dy == 1.f, "straight behind: bottom edge, pointing down");
}

// arrows.h: three targets 2 degrees apart fan out 24 px apart along the edge,
// in angle order, inside the inset rect; a lone arrow keeps its exit point.
static void tb_0074_edge_arrow_fan_out() {
  const int W = 800, H = 600;
  const float m = arrow_margin(100);
  Mat4 v = ident(), p = proj90(16.f, 8000.f, W, H);
  const float deg[3] = { 2.f, -2.f, 0.f };   // shuffled: the fan sorts by angle
  EdgeArrow a[4];
  for (int i = 0; i < 3; ++i) {
    const float t = tanf(deg[i] * 3.14159265f / 180.f);
    CHECK(arrow_make(Vec3{300, -300.f * t, 100}, v, p, 16.f, W, H, m, i, &a[i]));
  }
  CHECK(arrow_make(Vec3{0, 0, -100}, v, p, 16.f, W, H, m, 3, &a[3]));
  arrows_fan_out(a, 4, W, H, m);
  CHECK_MSG(a[0].tag == 1 && a[1].tag == 2 && a[2].tag == 0 && a[3].tag == 3, "kept in angle order");
  for (int i = 0; i < 3; ++i) {
    CHECK_MSG(a[i].x == 760.f, "fanned along the right edge");
    CHECK_MSG(a[i].y >= 40.f && a[i].y <= 560.f, "inside the inset rect");
  }
  CHECK(fabsf(a[1].y - a[0].y - 24.f) < 0.01f && fabsf(a[2].y - a[1].y - 24.f) < 0.01f);
  CHECK(fabsf(a[1].y - 300.f) < 0.01f);
  CHECK_MSG(a[3].x == 400.f && a[3].y == 560.f, "a lone arrow keeps its exit point");
}

// arrows.h: an arrow whose shape or label lands in the open panel is not drawn;
// the label sits toward the screen centre.
static void tb_0075_edge_arrow_panel_and_label() {
  const int W = 800, H = 600;
  Mat4 v = ident(), p = proj90(16.f, 8000.f, W, H);
  EdgeArrow a;
  CHECK(arrow_make(Vec3{300, 0, 100}, v, p, 16.f, W, H, arrow_margin(100), 0, &a));
  int lx, ly;
  arrow_label_pos(a, 30, 17, &lx, &ly);
  CHECK_MSG(lx + 30 < 760 - ARROW_LEN && abs(ly + 8 - 300) <= 1, "label left of a right arrow, centred on it");
  const UiRect b = arrow_bounds(a, 30, 17);
  CHECK(b.x <= lx && b.x + b.w >= 760 && b.y <= 300 - ARROW_BASE / 2 && b.y + b.h >= 300 + ARROW_BASE / 2);
  const UiRect panel = { 600, 200, 200, 200 }, away = { 0, 0, 100, 100 };
  CHECK(arrow_suppressed(b, true, panel));
  CHECK(!arrow_suppressed(b, false, panel));
  CHECK(!arrow_suppressed(b, true, away));
}

// volume.h: a box in front gives all 6 faces; one straddling the near plane
// keeps only the face whose four corners are all in front, never a mirrored one.
static void tb_0076_secret_volume_faces() {
  Mat4 v = ident(), p = proj90(16.f, 8000.f, 800, 600);
  VolumeGeom g;
  volume_project(Vec3{0, 0, 100}, Vec3{0.5f, 0.5f, 0.5f}, v, p, 16.f, 800, 600, &g);
  CHECK(g.faces == 6 && g.edges == 12 && g.onScreen);
  volume_project(Vec3{0, 0, 16}, Vec3{10, 10, 10}, v, p, 16.f, 800, 600, &g);
  CHECK_MSG(g.faces == 1 && g.faceOk[5] && !g.faceOk[4], "only the +z face, corners z = 26");
  CHECK(g.edges == 4 && g.onScreen);
}

// canvas.h: at quit, one ci_dec_refs per held, readable shader whose count still
// includes ours; a count below 1 is left alone (a decrement would underflow), and
// nothing once released (a second quit call or a detach must not decrement again).
static void tb_0077_quit_release_mask() {
  static int objs_[2]; int& a = objs_[0]; int& b = objs_[1];
  const void* both[2] = { &a, &b };
  const void* fontOnly[2] = { &a, 0 };
  const bool rd[2] = { true, true }, rd0[2] = { false, true };
  const int one[2] = { 1, 1 }, zero0[2] = { 0, 1 }, neg1[2] = { 1, -1 };
  CHECK(canvas_release_mask(both, rd, one, false) == 3u);
  CHECK(canvas_release_mask(fontOnly, rd, one, false) == 1u);
  CHECK_MSG(canvas_release_mask(both, rd0, one, false) == 2u, "an unreadable item stays held");
  CHECK_MSG(canvas_release_mask(both, rd, zero0, false) == 2u, "a count of 0 is not decremented");
  CHECK_MSG(canvas_release_mask(both, rd, neg1, false) == 1u, "nor a negative one");
  CHECK_MSG(canvas_release_mask(both, rd, one, true) == 0u, "nothing on a second call");
}

// espbox.h: hitboxes.md section 0's world box. x/z centred on pos with dim's
// half-extents; y centred at pos.y - o with half-height dim.y - |o|, never below 0.
static void tb_0078_hitbox_from() {
  const Vec3 pos = { 100.f, 200.f, 300.f }, dim = { 16.f, 24.f, 20.f };
  Hitbox h = hitbox_from(pos, dim, 0.f);
  CHECK(h.c.x == 100.f && h.c.y == 200.f && h.c.z == 300.f);
  CHECK(h.half.x == 16.f && h.half.y == 24.f && h.half.z == 20.f);
  h = hitbox_from(pos, dim, 8.f);
  CHECK_MSG(h.c.y == 192.f && h.half.y == 16.f, "o > 0: centre down by o, half-height dim.y - o");
  CHECK(h.c.x == 100.f && h.c.z == 300.f && h.half.x == 16.f && h.half.z == 20.f);
  h = hitbox_from(pos, dim, -8.f);
  CHECK_MSG(h.c.y == 208.f && h.half.y == 16.f, "o < 0: centre up by |o|, half-height dim.y - |o|");
  h = hitbox_from(pos, dim, 30.f);
  CHECK_MSG(h.c.y == 170.f && h.half.y == 0.f, "o past dim.y: the half-height clamps at 0");
}

// classes.h: pickup labels by subtype (+0xD14, pickups.md sections 1-2), the
// class name for any value out of range.
static void tb_0079_item_labels_by_subtype() {
  const char* ammo[6] = { "Bullets +10", "Bullets +50", "Pipebomb +1", "Pipebombs +10", "Glopp +25", "Glopp +100" };
  for (int i = 0; i < 6; ++i) CHECK_MSG(strcmp(item_label(ITEM_AMMO, i, "Ammo"), ammo[i]) == 0, ammo[i]);
  CHECK(strcmp(item_label(ITEM_AMMO, 6, "Ammo"), "Ammo") == 0);
  CHECK(strcmp(item_label(ITEM_AMMO, -1, "Ammo"), "Ammo") == 0);
  CHECK(strcmp(item_label(ITEM_EGO, 0, "Ego"), "Ego +10") == 0);
  CHECK(strcmp(item_label(ITEM_EGO, 1, "Ego"), "Ego +100") == 0);
  CHECK(strcmp(item_label(ITEM_EGO, 2, "Ego"), "Ego") == 0);
  // gun_type order is the pickup model table (gun_get_model duke!0x1005f230): 5 is the shotgun.
  const char* guns[6] = { "Golden Eagle", "Assault Rifle", "G.L.O.P.P. Ray", "Pulse Cannon", "PRPG", "Shotgun" };
  for (int i = 0; i < 6; ++i) CHECK_MSG(strcmp(item_label(ITEM_GUN, i, "Weapon"), guns[i]) == 0, guns[i]);
  CHECK(strcmp(item_label(ITEM_GUN, -1, "Weapon"), "Weapon") == 0);
  CHECK(strcmp(item_label(ITEM_GUN, 6, "Weapon"), "Weapon") == 0);
  CHECK(strcmp(item_label(ITEM_LIFE, 0, "Double Damage"), "Double Damage") == 0);
  CHECK(strcmp(item_label(ITEM_JETPACK, 1, "Jetpack"), "Jetpack") == 0);
  CHECK(item_label(ITEM_AMMO, 9, 0) == 0);
}

// esp_layout.h: the nuke's brackets are twice the item thickness, still capped
// by the arm; a label in a taller font sits its own height above the box.
static void tb_0080_nuke_brackets_and_label() {
  CHECK(ESP_NUKE_BRACKET_T == 2 * ESP_BRACKET_T);
  CHECK(esp_bracket_thickness(10, 10, ESP_NUKE_BRACKET_T) == 4);
  CHECK(esp_bracket_thickness(3, 10, ESP_NUKE_BRACKET_T) == 3);
  CHECK(esp_bracket_thickness(10, 10) == ESP_BRACKET_T);
  CHECK(esp_name_label_y(200, 24) == 176);
  CHECK(esp_name_label_y(200) == 200 - ESP_LABEL_GAP);
}

// freecam.h. Cameras are built as inverse rows (right, up, forward, eye) and the
// view is their inverse by transpose (view_of_inverse), independent of freecam.h.
static bool fc_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

// A vector turned about world +Y by `deg`, +z toward +x: a rotation, not freecam.h's closed form.
static void fc_turn_y(float v[3], float deg) {
  const float a = deg * 3.14159265f / 180.f, c = cosf(a), s = sinf(a);
  const float x = v[0] * c + v[2] * s, z = -v[0] * s + v[2] * c;
  v[0] = x; v[2] = z;
}

// Four cameras: rotated_pair's (yaw 90), one pitched 45 degrees down at yaw 0, a
// mirrored one (right negated, determinant -1), and yaw 30 with pitch -20: the
// identity axes tipped about x, then turned about y.
static void tb_0081_freecam_pose_round_trips_the_view() {
  const float s = 0.70710678f;
  const float pitched[16] = { 1, 0, 0, 0,   0, s, s, 0,   0, -s, s, 0,   -7, 40, 12, 1 };
  const float mirrored[16] = { -1, 0, 0, 0,   0, 1, 0, 0,   0, 0, 1, 0,   3, 4, 5, 1 };
  const float p = -20.f * 3.14159265f / 180.f;
  float both[16] = { 1, 0, 0, 0,   0, cosf(p), -sinf(p), 0,   0, sinf(p), cosf(p), 0,   11, -3, 8, 1 };
  for (int row = 0; row < 3; ++row) fc_turn_y(both + row * 4, 30.f);
  float cams[4][16], dummy[16];
  rotated_pair(dummy, cams[0]);
  memcpy(cams[1], pitched, sizeof(pitched));
  memcpy(cams[2], mirrored, sizeof(mirrored));
  memcpy(cams[3], both, sizeof(both));
  const FreecamPose pb = freecam_pose_from_inverse(both);
  CHECK(fc_near(pb.yaw, 30.f, 1e-3f) && fc_near(pb.pitch, -20.f, 1e-3f) && pb.hand == 1.f);
  for (int c = 0; c < 4; ++c) {
    float want[16], v[16], i[16];
    view_of_inverse(cams[c], want);
    const FreecamPose p = freecam_pose_from_inverse(cams[c]);
    freecam_matrices(p, v, i);
    int off = 0;
    for (int k = 0; k < 16; ++k) if (!fc_near(v[k], want[k], 1e-4f) || !fc_near(i[k], cams[c][k], 1e-4f)) off++;
    CHECK_MSG(off == 0, "pose -> view reproduces the captured camera");
  }
}
static void tb_0082_freecam_yaw_90_turns_forward_to_plus_x() {
  FreecamPose p = { { 0, 0, 0 }, 0.f, 0.f, 1.f };
  float r[3], u[3], f[3];
  freecam_axes(p, r, u, f);
  CHECK(fc_near(f[0], 0.f, 1e-5f) && fc_near(f[1], 0.f, 1e-5f) && fc_near(f[2], 1.f, 1e-5f));
  freecam_look(&p, 90.f, 0.f, 1.f);   // 90 counts at 1 degree a count, to the right
  freecam_axes(p, r, u, f);
  CHECK(fc_near(f[0], 1.f, 1e-5f) && fc_near(f[1], 0.f, 1e-5f) && fc_near(f[2], 0.f, 1e-5f));
}
static void tb_0083_freecam_pitch_clamps_at_89() {
  FreecamPose p = { { 0, 0, 0 }, 0.f, 0.f, 1.f };
  freecam_look(&p, 0.f, -1000.f, 1.f);   // mouse up: look up
  CHECK(p.pitch == FREECAM_PITCH_MAX && FREECAM_PITCH_MAX == 89.f);
  freecam_look(&p, 0.f, 2000.f, 1.f);
  CHECK(p.pitch == -FREECAM_PITCH_MAX);
}
static void tb_0084_freecam_forward_is_frame_rate_independent() {
  FreecamPose a = { { 10, 20, 30 }, 30.f, 20.f, 1.f };
  FreecamPose b = a;
  freecam_move(&a, FCK_FWD, 100.f, 1.f);
  for (int k = 0; k < 64; ++k) freecam_move(&b, FCK_FWD, 100.f, 1.f / 64.f);
  float r[3], u[3], f[3];
  freecam_axes(a, r, u, f);
  const float start[3] = { 10, 20, 30 };
  int off = 0;
  for (int k = 0; k < 3; ++k) {
    if (!fc_near(a.eye[k] - start[k], 100.f * f[k], 1e-3f)) off++;
    if (!fc_near(b.eye[k], a.eye[k], 1e-3f)) off++;
  }
  CHECK_MSG(off == 0, "1 s at 100 is 100 units along forward, in one step or 64");
  CHECK(freecam_speed_mult(0) == 1.f && freecam_speed_mult(FCK_FAST) == 4.f);
  CHECK(freecam_speed_mult(FCK_SLOW) == 0.25f && freecam_speed_mult(FCK_FAST | FCK_SLOW) == 1.f);
}
static void tb_0085_freecam_up_is_world_y_at_any_pitch() {
  FreecamPose p = { { 0, 0, 0 }, 40.f, 60.f, 1.f };
  freecam_move(&p, FCK_UP, 100.f, 0.5f);
  CHECK(fc_near(p.eye[0], 0.f, 1e-5f) && fc_near(p.eye[1], 50.f, 1e-5f) && fc_near(p.eye[2], 0.f, 1e-5f));
  freecam_move(&p, FCK_DOWN | FCK_FAST, 100.f, 0.5f);
  CHECK(fc_near(p.eye[1], -150.f, 1e-4f));
}
// ownership.h: the panel over the freecam and back, and the freecam ending under the panel.
static void tb_0086_own_panel_over_freecam_and_back() {
  OwnState s; own_init(&s);
  CHECK(own_event(&s, EV_FREECAM) == (ACT_INPUT_OFF | ACT_RESET_IMPULSES));
  CHECK(s.freecam == 1 && own_owner(&s) == OWNER_FREECAM);
  CHECK(own_event(&s, EV_FRAME) == ACT_INPUT_OFF && own_event(&s, EV_ACTIVATE) == ACT_INPUT_OFF);
  // Hand-overs keep input off and ask the engine for nothing.
  CHECK(own_event(&s, EV_TOGGLE) == 0 && own_owner(&s) == OWNER_PANEL);
  CHECK(own_event(&s, EV_FRAME) == ACT_INPUT_OFF);
  CHECK_MSG(own_event(&s, EV_TOGGLE) == 0, "closing the panel leaves input with the freecam");
  CHECK(own_owner(&s) == OWNER_FREECAM);
  CHECK(own_event(&s, EV_FREECAM) == ACT_INPUT_ON && own_owner(&s) == OWNER_GAME);
  CHECK(own_event(&s, EV_FRAME) == 0);
  // Off while the panel is open: the panel keeps input until it closes.
  own_event(&s, EV_FREECAM);
  own_event(&s, EV_TOGGLE);
  CHECK(own_event(&s, EV_FREECAM) == 0 && own_owner(&s) == OWNER_PANEL);
  CHECK(own_event(&s, EV_TOGGLE) == ACT_INPUT_ON && own_owner(&s) == OWNER_GAME);
}
// config.cpp and bind.h: the freecam keys, appended rows, and the modifiers only its movement rows take.
static void tb_0087_freecam_config_and_bind_rows() {
  CHECK(BIND_ROW_HOLDZOOMOUT == 7 && BIND_ROW_FC_FORWARD == 8 && BIND_ROW_FC_SLOWER == 15 && BIND_ROW_FP_CROUCH == 16 &&
        BIND_ROW_INTERACT == 17 && BIND_ROW_PHOTO == 18 && BIND_ROW_GAMESPEED == 19 && BIND_ROW_COUNT == 20);
  config_defaults();
  CHECK(config_bind_get(BIND_ROW_CAMERA) == 0x56);   // V
  CHECK(g_cfg.freecamMarker == 1);
  CHECK_MSG(!bind_key_is_live(0x10), "the bind rows still refuse modifiers");
}
// ownership.h + bind.h: a hotkey yields while the engine console takes typing, and
// acts in menus (another UI window focused, the console shown behind it or not at all).
static void tb_0088_hotkeys_yield_to_the_console_only() {
  int console = 0, menu = 0;
  CHECK(console_takes_keys(&console, true, &console));
  CHECK_MSG(console_takes_keys(&console, true, 0), "shown with nothing focused still types");
  CHECK_MSG(!console_takes_keys(&console, true, &menu), "a menu focused over it takes the keys");
  CHECK(!console_takes_keys(0, false, &menu));
  CHECK(!console_takes_keys(&console, false, &console));
  CHECK(!hotkey_swallows(0x56, 0x56, true, console_takes_keys(&console, true, &console), false));
  CHECK(hotkey_swallows(0x56, 0x56, true, console_takes_keys(0, false, &menu), false));
  CHECK(hotkey_swallows(0x56, 0x56, true, console_takes_keys(&console, true, &menu), false));
}
// teleport.h: a floor hit stands the box on it, 1 unit clear (dim.y above the hit).
static bool tp_near(Vec3 a, float x, float y, float z) {
  return fabsf(a.x - x) < 1e-3f && fabsf(a.y - y) < 1e-3f && fabsf(a.z - z) < 1e-3f;
}
static void tb_0089_teleport_floor_placement_height() {
  const Vec3 hit = { 10.f, 100.f, 20.f }, up = { 0.f, 1.f, 0.f }, down = { 0.f, -1.f, 0.f };
  const Vec3 dim = { 16.f, 40.f, 16.f };
  TpPlace p = tp_place(&hit, up, down, dim);
  CHECK(p.reason == TP_OK && tp_near(p.pos, 10.f, 141.f, 20.f));
  const Vec3 slope = { 0.66144f, 0.75f, 0.f }, look = { 0.f, -0.7071f, 0.7071f };
  p = tp_place(&hit, slope, look, dim);
  CHECK_MSG(p.reason == TP_OK && tp_near(p.pos, 10.f, 141.f, 20.f), "n.y 0.75 is still a floor");
}
// A wall hit steps back along the normal by the box's support distance + 1; a normal
// facing away from the ray is not trusted.
static void tb_0090_teleport_wall_step_back() {
  const Vec3 hit = { 0.f, 50.f, 0.f }, n = { -1.f, 0.f, 0.f }, dir = { 1.f, 0.f, 0.f };
  const Vec3 dim = { 12.f, 40.f, 20.f };
  TpPlace p = tp_place(&hit, n, dir, dim);
  CHECK(p.reason == TP_OK && p.wall && tp_near(p.pos, -13.f, 50.f, 0.f));   // dim.x*1 + 1
  const Vec3 away = { 1.f, 0.f, 0.f };
  CHECK(tp_place(&hit, away, dir, dim).reason == TP_BAD_NORMAL);
}
static void tb_0091_teleport_steep_normal_refused() {
  const Vec3 hit = { 0.f, 0.f, 0.f }, dim = { 16.f, 40.f, 16.f };
  const Vec3 steep = { 0.8f, 0.6f, 0.f }, dirDown = { -0.7071f, -0.7071f, 0.f };
  CHECK(tp_place(&hit, steep, dirDown, dim).reason == TP_STEEP);
  const Vec3 ceiling = { 0.f, -1.f, 0.f }, dirUp = { 0.f, 1.f, 0.f };
  CHECK_MSG(tp_place(&hit, ceiling, dirUp, dim).reason == TP_STEEP, "a ceiling is refused");
}
static void tb_0092_teleport_null_hit_refused() {
  const Vec3 hit = { 0.f, 0.f, 0.f }, up = { 0.f, 1.f, 0.f }, down = { 0.f, -1.f, 0.f };
  const Vec3 dim = { 16.f, 40.f, 16.f }, zero = { 0.f, 0.f, 0.f };
  CHECK(tp_place(0, up, down, dim).reason == TP_NO_HIT);
  CHECK_MSG(tp_place(&hit, zero, down, dim).reason == TP_BAD_NORMAL, "a zero normal is unknown");
}
// player_is_dead_or_locked duke!0x1007e050, inverted: alive, not in states 0x14-0x16, +0xAF0 set.
static void tb_0093_teleport_engine_gate() {
  CHECK(tp_gate(100, 0, 1));
  CHECK(!tp_gate(0, 0, 1) && !tp_gate(-5, 0, 1));
  CHECK(!tp_gate(100, 0x14, 1) && !tp_gate(100, 0x15, 1) && !tp_gate(100, 0x16, 1));
  CHECK(tp_gate(100, 0x13, 1) && tp_gate(100, 0x17, 1));
  CHECK(!tp_gate(100, 0, 0));
}
// The box never rotates: at 45 degrees its nearest corner is dim.x*0.7071 + dim.z*0.7071 along n.
static void tb_0095_teleport_diagonal_wall_support_distance() {
  const Vec3 hit = { 0.f, 50.f, 0.f }, n = { -0.70711f, 0.f, -0.70711f }, dir = { 0.70711f, 0.f, 0.70711f };
  const Vec3 dim = { 12.f, 40.f, 20.f };
  const TpPlace p = tp_place(&hit, n, dir, dim);
  const float step = 32.f * 0.70711f + 1.f;
  CHECK(p.reason == TP_OK && tp_near(p.pos, -0.70711f * step, 50.f, -0.70711f * step));
}
// A wall placement drops onto the floor found straight below it, or is refused.
static void tb_0096_teleport_wall_drops_to_floor() {
  const Vec3 dim = { 16.f, 40.f, 16.f }, up = { 0.f, 1.f, 0.f }, steep = { 0.8f, 0.6f, 0.f };
  TpPlace wall = { TP_OK, { -17.f, 50.f, 0.f }, 1 };
  const Vec3 floorHit = { -17.f, 0.f, 0.f };
  TpPlace d = tp_drop(wall, &floorHit, up, dim);
  CHECK(d.reason == TP_OK && !d.wall && tp_near(d.pos, -17.f, 41.f, 0.f));
  CHECK_MSG(tp_drop(wall, 0, up, dim).reason == TP_NO_FLOOR, "no floor within the drop is refused");
  CHECK(tp_drop(wall, &floorHit, steep, dim).reason == TP_STEEP);
  TpPlace floor = { TP_OK, { 1.f, 2.f, 3.f }, 0 };
  CHECK_MSG(tp_near(tp_drop(floor, 0, up, dim).pos, 1.f, 2.f, 3.f), "a floor placement is left alone");
  TpPlace refused = { TP_STEEP, { 0.f, 0.f, 0.f }, 0 };
  CHECK(tp_drop(refused, &floorHit, up, dim).reason == TP_STEEP);
}
static void tb_0094_teleport_config_and_bind_row() {
  config_defaults();
  CHECK(config_bind_get(BIND_ROW_TELEPORT) == 0x54 && g_cfg.teleportMarker == 1);   // T
  config_bind_set(BIND_ROW_TELEPORT, 0x47);
  CHECK(g_cfg.teleportKey == 0x47);
  config_defaults();
}

// nocull.h: culling is off only while the freecam flies with the switch set.
static void tb_0097_nocull_active_needs_freecam_and_switch() {
  CHECK(nocull_active(1, 1) == 1);
  CHECK(nocull_active(1, 0) == 0);
  CHECK(nocull_active(0, 1) == 0);
  CHECK(nocull_active(0, 0) == 0);
}
// g_cam_far is written only on transitions, never before a stock is held, and the
// restore frame's stale read (still the raised far) is never recaptured as stock.
static void tb_0098_nocull_far_writes_and_stock_recapture() {
  CHECK(nocull_far_write(1, 0, 0) == NOCULL_NONE);      // active, no stock yet: wait
  CHECK(nocull_far_write(1, 0, 1) == NOCULL_APPLY);
  CHECK(nocull_far_write(1, 1, 1) == NOCULL_NONE);      // held: no write every frame
  CHECK(nocull_far_write(0, 1, 1) == NOCULL_RESTORE);
  CHECK(nocull_far_write(0, 0, 1) == NOCULL_NONE);
  CHECK(nocull_far_write(0, 0, 0) == NOCULL_NONE);
  // Frame trace: applied last frame, restoring now (wrote) -> refuse; next steady frame -> take.
  CHECK(zoom_may_recapture(0, 1, 1) == 0);
  CHECK(zoom_may_recapture(0, 0, 0) == 1);
  CHECK(zoom_may_recapture(1, 0, 1) == 0);              // the apply frame
  CHECK(strcmp(NOCULL_FAR, "1000000") == 0);
}
static void tb_0099_nocull_config_key_defaults_on() {
  // The row is gone (panel clean-up): culling is always off while the freecam flies.
  CHECK(nocull_active(1, 1) == 1 && nocull_active(0, 1) == 0);
}

// AA's panel_layout.h:27 floor, copied rather than re-measured on our shorter
// tabs: a resize must stop where AA's does.
static void tb_0100_panel_min_h_is_aas() {
  CHECK(PANEL_MIN_H == 790);
  const PanelBox tiny = {10, 10, 50, 50};
  CHECK(panel_layout(tiny, 3440, 1440).h == 790);
  CHECK(panel_layout(tiny, 1280, 720).h == 720);   // a screen shorter than the floor still clamps to it
}

// menu.h: every tab is drawn, AA's fixed rail (2026-09-28 decision);
// Combat and Weapons used to read false here.
static void tb_0101_menu_tab_rail_shows_every_tab() {
  for (int t = 0; t < TAB_COUNT; ++t) CHECK(menu_tab_visible(t));
  CHECK(strcmp(MENU_PLANNED_HINT, "Not available yet.") == 0);
}

// classes.h: the tier variant is its own label now, off the bare name
// (2026-09-28 user decision, Enemies' Level sub-row).
static void tb_0102_class_variant_label() {
  CHECK(strcmp(class_variant_label(class_lookup("monst_ratoid3").tier), "3") == 0);
  CHECK(strcmp(class_variant_label(class_lookup("monst_femechB").tier), "Boss") == 0);
  CHECK(strcmp(class_variant_label(class_lookup("monst_pig").tier), "") == 0);          // bare tier 1
  CHECK(strcmp(class_variant_label(class_lookup("boss_wozma").tier), "") == 0);         // TIER_NONE
}

// classes.h: the Level row appends the variant to the name on one line (2026-09-29
// user decision); tier 1 and unclassified stay bare, Level off never suffixes.
static void tb_0104_name_with_variant() {
  char b[64];
  class_name_with_variant(b, sizeof b, "Pig Cop", TIER_1, true);
  CHECK(strcmp(b, "Pig Cop") == 0);
  class_name_with_variant(b, sizeof b, "Pig Cop", TIER_2, true);
  CHECK(strcmp(b, "Pig Cop 2") == 0);
  class_name_with_variant(b, sizeof b, "Pig Cop", TIER_B, true);
  CHECK(strcmp(b, "Pig Cop Boss") == 0);
  class_name_with_variant(b, sizeof b, "Pig Cop", TIER_3, false);
  CHECK(strcmp(b, "Pig Cop") == 0);
  class_name_with_variant(b, 5, "Pig Cop", TIER_2, true);   // truncates, stays terminated
  CHECK(strcmp(b, "Pig ") == 0);
  CHECK(strcmp(class_lookup("monst_securitymech2").name, "Security Mech") == 0);
  CHECK(strcmp(class_lookup("monst_hovermech").name, "Hover Mech") == 0);
}

// config.h: the two enemy sub-rows added with the Level/Bounding boxes rows,
// appended to Config (2026-09-28 user decision), default on like their siblings.
static void tb_0103_config_enemy_subrow_defaults() {
  config_defaults();
  CHECK(g_cfg.espBoundingBoxes == 1);
}

// dmgnum.h: AA's animation constants (world-features.md section 6): 750 ms,
// ease-out rise of one text line, opaque for two thirds then a linear fade.
static void tb_0105_dmg_progress_rise_alpha() {
  CHECK(fabs(dmg_progress(1000u, 1000u) - 0.0) < 1e-9);
  CHECK(fabs(dmg_progress(1000u, 1250u) - 1.0 / 3.0) < 1e-9);
  CHECK(fabs(dmg_progress(1000u, 1500u) - 2.0 / 3.0) < 1e-9);
  CHECK(dmg_progress(1000u, 1750u) == 1.0);
  CHECK(dmg_progress(1000u, 9000u) == 1.0);
  CHECK(fabs(dmg_progress(0xFFFFFF00u, 0xFFFFFF00u + 375u) - 0.5) < 1e-9);   // across the wrap
  CHECK(dmg_rise_px(0.0, 30) == 0);
  CHECK(dmg_rise_px(1.0 / 3.0, 30) == 17);   // 1-(2/3)^2 = 5/9 of 30
  CHECK(dmg_rise_px(2.0 / 3.0, 30) == 27);   // 8/9 of 30
  CHECK(dmg_rise_px(1.0, 30) == 30);
  CHECK(dmg_alpha(0.0) == 255);
  CHECK(dmg_alpha(1.0 / 3.0) == 255);
  CHECK(dmg_alpha(2.0 / 3.0) == 255);
  CHECK(dmg_alpha(0.8) == 153);   // 3 * 0.2 of 255, off the x.5 tie a double cannot hold
  CHECK(dmg_alpha(1.0) == 0);
}
static void tb_0106_dmg_colour() {
  CHECK(dmg_colour(0) == C_HAZARD);
  CHECK(dmg_colour(1) == C_RED);
}
static void tb_0107_dmg_keep_rule_reasons() {
  CHECK(dmg_keep_reason(1, CAT_ENEMY, 0x20u, 50.f, 40.f) == DMG_KEPT);
  CHECK(dmg_keep_reason(1, CAT_BOSS, 0x21u, 50.f, -10.f) == DMG_KEPT);
  CHECK(dmg_keep_reason(0, CAT_ENEMY, 0x20u, 50.f, 40.f) == DMG_REJ_ATTACKER);
  CHECK(dmg_keep_reason(1, CAT_BABE, 0x20u, 50.f, 40.f) == DMG_REJ_CLASS);
  CHECK(dmg_keep_reason(1, CAT_OTHER, 0x20u, 50.f, 40.f) == DMG_REJ_CLASS);
  CHECK(dmg_keep_reason(1, CAT_ENEMY, 0x00u, 50.f, 40.f) == DMG_REJ_DEAD);    // no monster base
  CHECK(dmg_keep_reason(1, CAT_ENEMY, 0x20u, 0.f, -5.f) == DMG_REJ_DEAD);     // already dead
  CHECK(dmg_keep_reason(1, CAT_ENEMY, 0x20u, 50.f, 50.f) == DMG_REJ_NODAMAGE);
  CHECK(dmg_keep_reason(1, CAT_ENEMY, 0x20u, 50.f, 60.f) == DMG_REJ_NODAMAGE);
}
static void tb_0108_dmg_display_and_kill() {
  CHECK(dmg_display(10.4f) == 10);
  CHECK(dmg_display(10.5f) == 11);
  CHECK(dmg_display(0.2f) == 1);
  CHECK(dmg_display(30.f - -70.f) == 100);   // overkill shows in full
  CHECK(dmg_is_kill(30.f, -70.f) == 1);
  CHECK(dmg_is_kill(30.f, 0.f) == 1);
  CHECK(dmg_is_kill(30.f, 1.f) == 0);
  CHECK(dmg_is_kill(0.f, -5.f) == 0);
}
static DmgEvent dmg_ev(const void* victim, int damage, unsigned t0) {
  DmgEvent e = { { 0.f, 0.f, 0.f }, damage, 0, victim, t0, 1 };
  return e;
}
static void tb_0109_dmg_stack_index() {
  static DmgRing r;
  static int objs_[2]; int& a = objs_[0]; int& b = objs_[1];
  dmg_reset(&r);
  dmg_push(&r, dmg_ev(&a, 1, 100u));
  dmg_push(&r, dmg_ev(&a, 2, 100u));
  CHECK(dmg_stack_index(r, 0, 200u) == 0);
  CHECK(dmg_stack_index(r, 1, 200u) == 1);
  dmg_push(&r, dmg_ev(&b, 3, 100u));
  dmg_push(&r, dmg_ev(&a, 4, 100u));
  CHECK(dmg_stack_index(r, 2, 200u) == 0);   // another victim starts its own column
  CHECK(dmg_stack_index(r, 3, 200u) == 2);
  CHECK(dmg_stack_index(r, 3, 100u + DMG_LIFE_MS) == 0);   // expired: not live, no offset
}
static void tb_0110_dmg_ring_overwrites_oldest() {
  static DmgRing r;
  int v = 0;
  dmg_reset(&r);
  for (int i = 0; i <= DMG_MAX; ++i) dmg_push(&r, dmg_ev(&v, i, 0u));
  CHECK(r.e[0].damage == DMG_MAX);
  CHECK(r.e[1].damage == 1);
  CHECK(r.next == 1);
  CHECK(dmg_is_live(r.e[0], 10u) == 1);
  CHECK(dmg_is_live(r.e[0], DMG_LIFE_MS) == 0);
}
static void tb_0111_dmg_fingerprint() {
  unsigned char b[DMG_FP_LEN] = { 0x6A, 0xFF, 0x68, 0xAB, 0xE6, 0x11, 0x10, 0x64, 0xA1, 0, 0, 0, 0, 0x50,
                                  0x64, 0x89, 0x25, 0, 0, 0, 0, 0x83, 0xEC, 0x28 };
  CHECK(dmg_fp_match(b, 0x10000000u, 0x200000u) == 1);
  CHECK(dmg_fp_match(b, 0x20000000u, 0x200000u) == 0);   // handler outside the image
  CHECK(dmg_fp_match(b, 0x10000000u, 0x11E6ABu) == 0);   // one past the end
  CHECK(dmg_fp_match(0, 0x10000000u, 0x200000u) == 0);
  b[23] = 0x2C;
  CHECK(dmg_fp_match(b, 0x10000000u, 0x200000u) == 0);   // one changed byte
}
static void tb_0112_dmg_config_default_on() {
  config_defaults();
  CHECK(g_cfg.dmgNumbers == 1);
}

// Outline geometry (esp_layout.h, ported from AA's esp_outline_offsets/esp_keyline_colour): eight distinct offsets at
// the outline radius, none the fill's own; the outline carries the fill's alpha so a fading label fades its edge too.
static void tb_0113_esp_label_outline_offsets() {
  int dx[8], dy[8];
  CHECK(esp_outline_offsets(ESP_LABEL_OUTLINE_R, dx, dy) == 8);
  bool noCentre = true, ring = true, distinct = true;
  for (int i = 0; i < 8; i++) {
    if (dx[i] == 0 && dy[i] == 0) noCentre = false;
    if ((dx[i] != 0 && dx[i] != ESP_LABEL_OUTLINE_R && dx[i] != -ESP_LABEL_OUTLINE_R) ||
        (dy[i] != 0 && dy[i] != ESP_LABEL_OUTLINE_R && dy[i] != -ESP_LABEL_OUTLINE_R)) ring = false;
    for (int j = 0; j < i; j++)
      if (dx[i] == dx[j] && dy[i] == dy[j]) distinct = false;
  }
  CHECK(noCentre);
  CHECK(ring);
  CHECK(distinct);
  CHECK(esp_outline_offsets(0, dx, dy) == 0);
  CHECK(esp_keyline_colour(0xFFE8E4DAu) == 0xFF000000u);          // opaque fill -> opaque outline
  CHECK(esp_keyline_colour(0x80E8E4DAu) == 0x80000000u);          // half-alpha fill -> half-alpha outline
}

// Weapons (weapons.h; docs/re/weapons.md sections 2-5).
static void tb_0114_weap_levers_m1_unchanged_m2_m4_scaled() {
  // m = 1 leaves every field exactly as read, whatever the other inputs.
  CHECK(weap_geagle_bf8(0.37f, 0.2f, 1.f) == 0.37f);
  CHECK(weap_pcannon_cd(1.98f, 1.f) == 1.98f);
  CHECK(weap_prpg_b18(10.25f, 10.5f, 1.f) == 10.25f);
  CHECK(weap_anim_end(5.7f, 5.f, 1.f) == 5.7f);
  // Golden Eagle: +0xBF8 := +0xC2C / m.
  CHECK(weap_geagle_bf8(0.2f, 0.2f, 2.f) == 0.1f);
  CHECK(weap_geagle_bf8(0.2f, 0.2f, 4.f) == 0.05f);
  // Pulse Cannon: the cooldown the engine just set, / m.
  CHECK(weap_pcannon_cd(2.f, 2.f) == 1.f);
  CHECK(weap_pcannon_cd(2.f, 4.f) == 0.5f);
  // PRPG: +0xB18 := +0xB14 - 0.5 * (1 - 1/m), so now - B18 > 0.5 after 0.5/m.
  CHECK(weap_prpg_b18(10.f, 10.f, 2.f) == 9.75f);
  CHECK(weap_prpg_b18(10.f, 10.f, 4.f) == 9.625f);
  // Anim end: now + (end - now) / m; an end already past is left alone.
  CHECK(weap_anim_end(6.f, 5.f, 2.f) == 5.5f);
  CHECK(weap_anim_end(6.f, 5.f, 4.f) == 5.25f);
  CHECK(weap_anim_end(4.f, 5.f, 4.f) == 4.f);
}
static void tb_0115_weap_pbomb_end_never_before_launch() {
  // The throw ends no earlier than its launch event is due, and never later than stock.
  CHECK(weap_pbomb_end(6.f, 5.f, 4.f, 5.1f) == 5.25f);   // shortened end already after the launch
  CHECK(weap_pbomb_end(6.f, 5.f, 4.f, 5.4f) == 5.4f);    // clamped up to the launch
  CHECK(weap_pbomb_end(6.f, 5.f, 2.f, 5.4f) == 5.5f);
  CHECK(weap_pbomb_end(6.f, 5.f, 4.f, 6.5f) == 6.f);     // a launch past stock end keeps stock
  CHECK(weap_pbomb_end(6.f, 5.f, 1.f, 5.4f) == 6.f);     // m = 1 unchanged
}
static void tb_0116_weap_reload_lever_and_max() {
  CHECK(weap_reload_bf8(0.55f, 0.55f, 100) == 0.55f);    // stock: the value start_reload wrote
  CHECK(weap_reload_bf8(0.8f, 0.8f, 200) == 0.4f);
  CHECK(weap_reload_bf8(0.8f, 0.8f, 400) == 0.2f);
  CHECK(weap_reload_bf8(1.f, 1.f, 1000) == 0.1f);
  CHECK(weap_reload_bf8(0.55f, 0.55f, 1100) == 0.f);     // Max: instant
  CHECK(weap_reload_is_max(1100));
  CHECK(!weap_reload_is_max(1000));
}
static void tb_0117_weap_slider_mapping_and_text() {
  CHECK(weap_fire_pct_clamped(100) == 100);
  CHECK(weap_fire_pct_clamped(0) == 100);
  CHECK(weap_fire_pct_clamped(124) == 100);
  CHECK(weap_fire_pct_clamped(125) == 150);
  CHECK(weap_fire_pct_clamped(400) == 400);
  CHECK(weap_fire_pct_clamped(999) == 400);
  CHECK(weap_reload_pct_clamped(50) == 100);
  CHECK(weap_reload_pct_clamped(1000) == 1000);
  CHECK(weap_reload_pct_clamped(1074) == 1050);
  CHECK(weap_reload_pct_clamped(1075) == 1100);
  CHECK(weap_reload_pct_clamped(5000) == 1100);
  CHECK(weap_mult(100) == 1.f);
  CHECK(weap_mult(250) == 2.5f);
  CHECK(weap_mult(400) == 4.f);
  char b[16];
  weap_fire_text(250, b, sizeof b);    CHECK(strcmp(b, "250%") == 0);
  weap_reload_text(1000, b, sizeof b); CHECK(strcmp(b, "1000%") == 0);
  weap_reload_text(1100, b, sizeof b); CHECK(strcmp(b, "Max") == 0);
  weap_reload_text(100, b, sizeof b);  CHECK(strcmp(b, "100%") == 0);
}
static void tb_0118_weap_edge_detectors() {
  // The first sample only primes: a new weapon's first frame is never a shot.
  WeapWatchF f = {};
  CHECK(!weap_changed(&f, 3.f));
  CHECK(!weap_changed(&f, 3.f));
  CHECK(weap_changed(&f, 3.5f));
  weap_note(&f, 3.25f);                 // our own write is not the next frame's shot
  CHECK(!weap_changed(&f, 3.25f));
  WeapWatchF c = {};
  CHECK(!weap_rose(&c, 2.f));
  CHECK(!weap_rose(&c, 1.9f));
  CHECK(weap_rose(&c, 2.f));
  WeapWatchI s = {};
  CHECK(!weap_entered(&s, 6, 6));       // already attacking when first seen
  CHECK(!weap_entered(&s, 6, 6));
  CHECK(!weap_entered(&s, 1, 6));
  CHECK(weap_entered(&s, 6, 6));
  WeapWatchI r = {};
  CHECK(!weap_rising(&r, 1));
  CHECK(!weap_rising(&r, 0));
  CHECK(weap_rising(&r, 1));
  CHECK(!weap_rising(&r, 1));
  r.primed = 0;                         // a weapon change re-primes
  CHECK(!weap_rising(&r, 1));
}
static void tb_0119_weap_hold_capture_apply_restore() {
  WeapHold h = {};
  float out = -1.f;
  // No stock yet: never applied.
  CHECK(weap_hold_step(&h, 1, 0.f, 0.f, &out) == WH_NONE);
  CHECK(!h.applied);
  weap_hold_capture(&h, 0.f);           // zero is not a stock
  CHECK(!h.captured);
  weap_hold_capture(&h, 10.f);
  CHECK(h.captured && h.stock == 10.f);
  CHECK(weap_hold_step(&h, 0, 10.f, 0.f, &out) == WH_NONE);   // off: nothing written
  CHECK(weap_hold_step(&h, 1, 10.f, 0.f, &out) == WH_WRITE && out == 0.f);
  CHECK(h.applied);
  weap_hold_capture(&h, 0.f);           // our own zero never replaces the stock
  CHECK(h.stock == 10.f);
  CHECK(weap_hold_step(&h, 1, 0.f, 0.f, &out) == WH_NONE);    // held: no rewrite
  CHECK(weap_hold_step(&h, 1, 10.f, 0.f, &out) == WH_WRITE && out == 0.f);   // engine re-parsed: hold again
  CHECK(weap_hold_step(&h, 0, 0.f, 0.f, &out) == WH_WRITE && out == 10.f);   // off: stock back
  CHECK(!h.applied);
  CHECK(weap_hold_step(&h, 0, 10.f, 0.f, &out) == WH_NONE);
  // Applied and already stock when released: nothing to write.
  CHECK(weap_hold_step(&h, 1, 10.f, 10.f, &out) == WH_NONE && h.applied);
  CHECK(weap_hold_step(&h, 0, 10.f, 0.f, &out) == WH_NONE && !h.applied);
}
static void tb_0120_weap_class_kinds() {
  CHECK(weap_kind("actor_weap_geagle") == WK_GEAGLE);
  CHECK(weap_kind("actor_weap_shotgun") == WK_SHOTGUN);
  CHECK(weap_kind("actor_weap_arifle") == WK_ARIFLE);
  CHECK(weap_kind("actor_weap_pbomb") == WK_PBOMB);
  CHECK(weap_kind("actor_weap_pcannon") == WK_PCANNON);
  CHECK(weap_kind("actor_weap_prpg") == WK_PRPG);
  CHECK(weap_kind("actor_weap_gray") == WK_GRAY);
  CHECK(weap_kind("actor_weap_x3000") == WK_X3000);
  CHECK(weap_kind("monst_pig") == WK_OTHER);
  CHECK(weap_kind(0) == WK_NONE);
  CHECK(strcmp(weap_name(WK_ARIFLE), "Assault Rifle") == 0);
  // Each span covers the furthest field the glue reads for that class.
  CHECK(weap_span(WK_GEAGLE) >= 0xC40);
  CHECK(weap_span(WK_ARIFLE) >= 0xD24);
  CHECK(weap_span(WK_PCANNON) >= 0xCCC);
  CHECK(weap_span(WK_PBOMB) >= 0xBCC);
  CHECK(weap_span(WK_SHOTGUN) >= 0xB30);
  CHECK(weap_span(WK_PRPG) >= 0xB1C);
}
static void tb_0121_weap_config_defaults() {
  config_defaults();
  CHECK(g_cfg.fireRatePct == 100);
  CHECK(g_cfg.reloadPct == 100);
  CHECK(g_cfg.accArifle == 0);
}

static bool near_f(float a, float b, float eps) { return fabsf(a - b) <= eps; }

// tb_0122 (cone/max-redirect angle tests) and the pick-mode half of tb_0123 removed 2026-09-29:
// Pick target, Cone and Max redirect are gone (user decision, side-scroller) -- silent aim and
// the triggerbot's locked mode always take the nearest visible enemy in any direction.
static void tb_0123_aim_keep_nearest() {
  // Nearest-first, ties by the walk's order so the pick never flickers between equals.
  AimCand c[3] = { { 0, 100.f, { 0, 0, 0 } }, { 1, 900.f, { 0, 0, 0 } }, { 2, 400.f, { 0, 0, 0 } } };
  const int kept3 = aim_keep_nearest(c, 3, AIM_CAND_MAX);
  CHECK(kept3 == 3 && c[0].idx == 0 && c[1].idx == 2 && c[2].idx == 1);
  AimCand t[2] = { { 7, 50.f, { 0, 0, 0 } }, { 3, 50.f, { 0, 0, 0 } } };
  aim_keep_nearest(t, 2, AIM_CAND_MAX);
  CHECK(t[0].idx == 3);
  // The 64 nearest survive the cap, whatever order they came in.
  AimCand many[70];
  for (int i = 0; i < 70; ++i) { many[i].idx = i; many[i].dist = (float)(70 - i); }
  const int kept = aim_keep_nearest(many, 70, AIM_CAND_MAX);
  CHECK(kept == AIM_CAND_MAX);
  bool farDropped = true;
  for (int i = 0; i < kept; ++i) if (many[i].dist > 64.f) farDropped = false;
  CHECK_MSG(farDropped, "the six farthest are the ones dropped");
  AimCand three[3] = { { 0, 3.f, { 0, 0, 0 } }, { 1, 1.f, { 0, 0, 0 } }, { 2, 2.f, { 0, 0, 0 } } };
  CHECK(aim_keep_nearest(three, 3, AIM_CAND_MAX) == 3);
  CHECK(three[0].idx == 1 && three[1].idx == 2 && three[2].idx == 0);
}
static void tb_0124_aim_lead_point_by_weapon_speed() {
  CHECK(aim_proj_speed(WK_GEAGLE) == 0.f);
  CHECK(aim_proj_speed(WK_SHOTGUN) == 0.f);
  CHECK(aim_proj_speed(WK_ARIFLE) == 0.f);
  CHECK(aim_proj_speed(WK_GRAY) == 0.f);
  CHECK(aim_proj_speed(WK_PBOMB) == 0.f);   // an arc: no straight-line lead
  CHECK(aim_proj_speed(WK_PRPG) == 800.f);
  CHECK(aim_proj_speed(WK_PCANNON) == 400.f);
  const Vec3 p = { 10.f, 20.f, 30.f }, v = { 100.f, 0.f, -50.f };
  Vec3 l = aim_lead_point(p, v, 800.f, 800.f, 100);   // one second of flight
  CHECK(near_f(l.x, 110.f, 0.01f) && near_f(l.y, 20.f, 0.01f) && near_f(l.z, -20.f, 0.01f));
  l = aim_lead_point(p, v, 400.f, 400.f, 70);          // 70 % of one second
  CHECK(near_f(l.x, 80.f, 0.01f) && near_f(l.z, -5.f, 0.01f));
  l = aim_lead_point(p, v, 800.f, 0.f, 100);           // instant: the point itself
  CHECK(l.x == p.x && l.y == p.y && l.z == p.z);
  l = aim_lead_point(p, v, 800.f, 800.f, 0);           // Lead amount 0
  CHECK(l.x == p.x && l.z == p.z);
}
static void tb_0125_aim_redirect_keeps_length() {
  const Vec3 orig = { 0.f, 0.95f * 1.0512f, 0.05f * 1.0512f };   // the look-up case's scaled vector
  const Vec3 from = { 0.f, 0.f, 0.f }, to = { 300.f, 0.f, 400.f };
  Vec3 out = { 0, 0, 0 };
  CHECK(aim_redirect(orig, from, to, &out));
  const float lo = sqrtf(orig.x * orig.x + orig.y * orig.y + orig.z * orig.z);
  const float ln = sqrtf(out.x * out.x + out.y * out.y + out.z * out.z);
  CHECK(near_f(ln, lo, 0.0001f));
  CHECK(near_f(out.x / ln, 0.6f, 0.0001f) && near_f(out.z / ln, 0.8f, 0.0001f));
  CHECK_MSG(!aim_redirect(orig, from, from, &out), "no direction to a point at the origin");
  const Vec3 zero = { 0, 0, 0 };
  CHECK_MSG(!aim_redirect(zero, from, to, &out), "a zero original has no length to keep");
}
static void tb_0126_aim_bone_cache_hit_miss_full() {
  static AimBoneCache c;   // static: zeroed
  int idx = 99;
  const int a = 1, b = 2;
  CHECK(aim_bone_lookup(&c, &a, &idx) == AIM_BONE_ASK);
  aim_bone_put(&c, &a, 5);
  CHECK(aim_bone_lookup(&c, &a, &idx) == AIM_BONE_HIT && idx == 5);
  aim_bone_put(&c, &b, -1);   // a miss is remembered too: never asked again
  CHECK(aim_bone_lookup(&c, &b, &idx) == AIM_BONE_HIT && idx == -1);
  static int keys[AIM_BONE_SLOTS];
  for (int i = 2; i < AIM_BONE_SLOTS; ++i) aim_bone_put(&c, &keys[i], i);
  CHECK(c.n == AIM_BONE_SLOTS);
  const int extra = 3;
  CHECK_MSG(aim_bone_lookup(&c, &extra, &idx) == AIM_BONE_FULL && idx == -1,
            "a full cache answers no head rather than asking every frame");
  aim_bone_put(&c, &extra, 7);
  CHECK(c.n == AIM_BONE_SLOTS && c.full == 1);
}
static void tb_0127_aim_config_defaults_and_labels() {
  config_defaults();
  CHECK(g_cfg.silentOn == 0);
  CHECK(g_cfg.aimPoint == AIM_AT_BODY);
  CHECK(g_cfg.aimRangeM == AIM_RANGE_MAX_M);
  CHECK(g_cfg.aimLead == 1 && g_cfg.aimLeadPct == AIM_LEAD_DEF);
  CHECK(g_cfg.aimMarker == 0);
  CHECK(aim_point_label(0) && strcmp(aim_point_label(0), "Body") == 0);
  CHECK(aim_point_label(1) && strcmp(aim_point_label(1), "Head") == 0);
}
static void tb_0128_aim_fingerprints_tell_dir_from_origin() {
  // duke!0x100508b0 weap_get_fire_dir, read in Ghidra 2026-09-29 (aim.md section 1).
  unsigned char dir[AIM_FIRE_DIR_FP_LEN] = {
    0x56, 0x8B, 0x71, 0x60, 0x85, 0xF6, 0x74, 0x25, 0xF6, 0x46, 0x48, 0x02, 0x74, 0x1F, 0xF6, 0x46,
    0x4C, 0x10, 0x74, 0x19, 0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x90, 0x5C, 0x01, 0x00, 0x00, 0x8B, 0x44,
    0x24, 0x08, 0x8B, 0x16, 0x50, 0x8B, 0xCE, 0xFF, 0x92, 0x1C, 0x02, 0x00, 0x00, 0x5E, 0xC2, 0x04,
    0x00 };
  CHECK(aim_fp_is_fire_dir(dir));
  dir[0x29] = 0x20;   // weap_get_fire_origin 0x100508f0: vt+0x220 instead of +0x21C
  CHECK(!aim_fp_is_fire_dir(dir));
  // duke!0x10078930 player_get_fire_dir (player vt+0x21C) and 0x10078960 player_get_fire_origin (+0x220).
  unsigned char pl[AIM_PLAYER_FP_LEN] = {
    0x56, 0x8B, 0xF1, 0x8B, 0x06, 0xFF, 0x90, 0x5C, 0x01, 0x00, 0x00, 0x8B, 0x8E, 0x7C, 0x0B, 0x00,
    0x00, 0x8B, 0x44, 0x24, 0x08, 0x8B, 0x8C, 0x8E, 0x80, 0x0B, 0x00, 0x00, 0x8B, 0x11, 0x50, 0x56,
    0xFF, 0x52, 0x28, 0x5E, 0xC2, 0x04, 0x00 };
  CHECK(aim_fp_is_player_dir(pl) && !aim_fp_is_player_origin(pl));
  pl[0x22] = 0x2C;   // the state's get_fire_origin slot
  CHECK(aim_fp_is_player_origin(pl) && !aim_fp_is_player_dir(pl));
}

static void tb_0129_tbot_delay_machine() {
  TbotState s = {};
  const int a = 1, b = 2;
  CHECK_MSG(tbot_step(&s, 1, &a, 16, 0), "no delay: fires on the first frame the condition holds");
  s = TbotState();
  bool fired[8];
  for (int f = 0; f < 8; ++f) fired[f] = tbot_step(&s, 1, &a, 16, 100) != 0;
  CHECK_MSG(!fired[0] && !fired[6] && fired[7], "100 ms at 16 ms frames: the eighth frame (112 ms held)");
  CHECK(!tbot_step(&s, 0, &a, 16, 100));        // the condition breaks
  CHECK_MSG(!tbot_step(&s, 1, &a, 16, 100), "and the clock starts again from zero");
  for (int f = 0; f < 7; ++f) tbot_step(&s, 1, &a, 16, 100);
  CHECK(tbot_step(&s, 1, &a, 16, 100));
  CHECK_MSG(!tbot_step(&s, 1, &b, 16, 100), "a new target resets the delay");
  s = TbotState();
  tbot_step(&s, 1, &a, 16, 500);
  CHECK_MSG(!tbot_step(&s, 1, &a, 10000, 500), "a 10 s stall counts as one capped frame, not a held delay");
  s = TbotState();
  tbot_step(&s, 1, &a, 16, 9999);               // a hand-edited delay is held to 500
  for (int f = 0; f < 3; ++f) tbot_step(&s, 1, &a, TBOT_FRAME_MS_MAX, 9999);
  CHECK(!tbot_step(&s, 1, &a, 100, 9999));      // 499 ms held
  CHECK(tbot_step(&s, 1, &a, 1, 9999));         // 500
}
static void tb_0130_tbot_gates_and_when() {
  CHECK(tbot_may_press(1, 0, 0, 0));
  CHECK_MSG(!tbot_may_press(0, 0, 0, 0), "off never presses");
  CHECK_MSG(!tbot_may_press(1, 1, 0, 0), "the panel owns input");
  CHECK_MSG(!tbot_may_press(1, 0, 1, 0), "the freecam owns input");
  CHECK_MSG(!tbot_may_press(1, 0, 0, 1), "the console takes typing");
  CHECK(tbot_when_holds(TBOT_WHEN_CROSSHAIR, 1, 0));
  CHECK(!tbot_when_holds(TBOT_WHEN_CROSSHAIR, 0, 1));
  CHECK(tbot_when_holds(TBOT_WHEN_LOCKED, 0, 1));
  CHECK(!tbot_when_holds(TBOT_WHEN_LOCKED, 1, 0));
  CHECK(tbot_when_label(0) && strcmp(tbot_when_label(0), "Crosshair") == 0);
  CHECK(tbot_when_label(1) && strcmp(tbot_when_label(1), "Locked") == 0);
  config_defaults();
  CHECK(g_cfg.tbotOn == 0);
  CHECK(g_cfg.tbotWhen == TBOT_WHEN_CROSSHAIR);
  CHECK(g_cfg.tbotDelay == TBOT_DELAY_MS_DEF);
}
static void tb_0131_tbot_wm_table_from_the_export() {
  // prism3d!0x423fd0 wm_get_input, read in Ghidra 2026-09-29: the slot table is the imm32 of both
  // the MOV CL and the LEA (0x4a1c70); slot 0's +0x14 is the input channel (aim.md section 5).
  unsigned char b[21] = { 0x8B, 0x44, 0x24, 0x04, 0x8D, 0x04, 0x40, 0x8A, 0x0C, 0xC5, 0x70, 0x1C, 0x4A, 0x00,
                          0x84, 0xC9, 0x8D, 0x04, 0xC5, 0x70, 0x1C };
  unsigned char full[24];
  memcpy(full, b, 21); full[21] = 0x4A; full[22] = 0x00; full[23] = 0x74;
  unsigned t = 0;
  CHECK(aim_wm_table(full, &t) && t == 0x4A1C70u);
  full[19] = 0x71;   // the two immediates disagree
  CHECK(!aim_wm_table(full, &t));
  full[19] = 0x70; full[7] = 0x8B;
  CHECK(!aim_wm_table(full, &t));
}

static void tb_0132_tbot_weapon_channel_and_bind_gates() {
  // Press-to-fire weapons only (AA's tbot.h:131-141 reasoning): a held bit must not throw or discharge.
  CHECK(tbot_weapon_fires_on_press(WK_GEAGLE));
  CHECK(tbot_weapon_fires_on_press(WK_SHOTGUN));
  CHECK(tbot_weapon_fires_on_press(WK_ARIFLE));
  CHECK(tbot_weapon_fires_on_press(WK_PRPG));
  CHECK(tbot_weapon_fires_on_press(WK_GRAY));
  CHECK_MSG(!tbot_weapon_fires_on_press(WK_PBOMB), "the pipebomb fires on release");
  CHECK_MSG(tbot_weapon_fires_on_press(WK_PCANNON), "the pulse cannon is tapped (tbot_pcannon_press)");
  CHECK_MSG(tbot_weapon_fires_on_press(WK_X3000), "the X-3000 is held (weap_x3000_fire_logic fires every held frame)");
  CHECK(!tbot_weapon_fires_on_press(WK_NONE) && !tbot_weapon_fires_on_press(WK_OTHER));
  // Four channels per impulse slot (inp_process loops ch < 4; slot init 0x4068c0).
  CHECK(tbot_channel_ok(0) && tbot_channel_ok(3));
  CHECK(!tbot_channel_ok(4) && !tbot_channel_ok(-1) && !tbot_channel_ok(15));
  // inp_reset_impulses clears only a slot with binds (+0x50 != 0): an unbound attack is never pressed.
  CHECK(tbot_bound(1) && tbot_bound(3));
  CHECK(!tbot_bound(0) && !tbot_bound(-1));
}

// Off-screen hits: the clip is skipped only for the bullet cast, Duke's bullet, a switch on and the
// hook armed; every other combination runs the original (spec A, all 32 rows).
static void tb_0133_offs_bypass_decision_table() {
  int bypass = 0;
  for (int m = 0; m < 32; ++m) {
    const int ret = m & 1, mine = (m >> 1) & 1, sil = (m >> 2) & 1, tb = (m >> 3) & 1, armed = (m >> 4) & 1;
    const int r = offs_reason(ret, mine, sil, tb, armed);
    const int want = ret && mine && (sil || tb) && armed;
    CHECK((r == OFFS_BYPASS) == (want != 0));
    bypass += r == OFFS_BYPASS;
  }
  CHECK_MSG(bypass == 3, "silent only, triggerbot only, both");
  CHECK(offs_reason(1, 1, 1, 1, 0) == OFFS_UNARMED);
  CHECK_MSG(offs_reason(0, 1, 1, 0, 1) == OFFS_OTHER_CALLER, "the voice-line clip passes through");
  CHECK_MSG(offs_reason(1, 1, 0, 0, 1) == OFFS_OFF, "both switches off: stock");
  CHECK_MSG(offs_reason(1, 0, 1, 0, 1) == OFFS_NOT_MINE, "the boss chain-gun's bullets are clipped");
}
// The scan refuses 0 or 2+ matches; the call site's rel32 gives the clip and the return address.
static void tb_0134_offs_scan_and_call_site() {
  unsigned char text[96];
  memset(text, 0x90, sizeof text);
  size_t at = 99;
  CHECK(offs_find(text, sizeof text, kOffsClipFp, kOffsClipMask, OFFS_CLIP_FP_LEN, &at) == 0);
  memcpy(text + 8, kOffsClipFp, OFFS_CLIP_FP_LEN);
  text[8 + 12] = 0x4C; text[8 + 13] = 0x3F; text[8 + 18] = 0x34;   // relocated operands differ
  CHECK(offs_find(text, sizeof text, kOffsClipFp, kOffsClipMask, OFFS_CLIP_FP_LEN, &at) == 1 && at == 8);
  text[8 + 22] = 0x84;   // a masked-in byte differs
  CHECK(offs_find(text, sizeof text, kOffsClipFp, kOffsClipMask, OFFS_CLIP_FP_LEN, &at) == 0);
  text[8 + 22] = 0x83;
  memcpy(text + 50, kOffsClipFp, OFFS_CLIP_FP_LEN);
  CHECK_MSG(offs_find(text, sizeof text, kOffsClipFp, kOffsClipMask, OFFS_CLIP_FP_LEN, &at) == 2, "two matches refuse");
  // duke!0x1003aa92: 8B 0D DC 69 18 10 68 00 40 9C 45 57 53 E8 9C DA 0A 00 -> call 0x100e8540, ret 0x1003aaa4.
  static const unsigned char site[18] = { 0x8B, 0x0D, 0xDC, 0x69, 0x18, 0x10, 0x68, 0x00, 0x40, 0x9C, 0x45, 0x57,
                                          0x53, 0xE8, 0x9C, 0xDA, 0x0A, 0x00 };
  CHECK(offs_find(site, sizeof site, kOffsSiteFp, kOffsSiteMask, OFFS_SITE_FP_LEN, &at) == 1 && at == 0);
  CHECK(offs_site_target(site, 0x1003aa92u) == 0x100e8540u);
  CHECK(offs_site_ret(0x1003aa92u) == 0x1003aaa4u);
}

// Duke's track: the crosshair ray meets the polyline in XZ; y comes from the ray (spec B).
static bool trk_near(float a, float b) { return fabsf(a - b) < 0.01f; }
static void tb_0135_trk_ray_straight_track_y_from_ray() {
  const TrkPt line[2] = { { -500.f, 0.f, 100.f, 0.f }, { 500.f, 0.f, 100.f, 1000.f } };
  const Vec3 o = { 0.f, 50.f, 0.f };
  const Vec3 fwd = { 0.f, 0.f, 1.f };
  TrkHit h = trk_ray_polyline(o, fwd, line, 2, 8192.f);
  CHECK(h.ok && trk_near(h.s, 100.f) && trk_near(h.p.x, 0.f) && trk_near(h.p.y, 50.f) && trk_near(h.p.z, 100.f));
  const Vec3 down = { 0.f, -0.6f, 0.8f };   // s = 125, y = 50 - 75
  h = trk_ray_polyline(o, down, line, 2, 8192.f);
  CHECK_MSG(h.ok && trk_near(h.s, 125.f) && trk_near(h.p.y, -25.f), "y is the ray's at the crossing");
  CHECK_MSG(!trk_ray_polyline(o, down, line, 2, 100.f).ok, "a crossing past the reach is refused");
  const Vec3 straightDown = { 0.f, -1.f, 0.f };
  CHECK(!trk_ray_polyline(o, straightDown, line, 2, 8192.f).ok);
}
static void tb_0136_trk_ray_curved_parallel_behind() {
  const TrkPt arc[5] = { { -200.f, 0.f, 0.f, 0.f }, { -100.f, 0.f, 80.f, 0.f }, { 0.f, 0.f, 100.f, 0.f },
                         { 100.f, 0.f, 80.f, 0.f }, { 200.f, 0.f, 0.f, 0.f } };
  const Vec3 o = { 0.f, 10.f, 0.f };
  const Vec3 diag = { 0.70710678f, 0.f, 0.70710678f };   // x = z meets (0,100)-(100,80) at x = 100/1.2
  TrkHit h = trk_ray_polyline(o, diag, arc, 5, 8192.f);
  CHECK(h.ok && h.seg == 2 && trk_near(h.p.x, 83.333f) && trk_near(h.p.z, 83.333f) && trk_near(h.p.y, 10.f));
  const TrkPt line[2] = { { -500.f, 0.f, 100.f, 0.f }, { 500.f, 0.f, 100.f, 1000.f } };
  const Vec3 along = { 1.f, 0.f, 0.f };
  CHECK_MSG(!trk_ray_polyline(o, along, line, 2, 8192.f).ok, "parallel: no crossing");
  const Vec3 onLine = { -600.f, 0.f, 100.f };
  CHECK_MSG(!trk_ray_polyline(onLine, along, line, 2, 8192.f).ok, "collinear: no crossing");
  const Vec3 past = { 0.f, 0.f, 200.f };
  const Vec3 fwd = { 0.f, 0.f, 1.f };
  CHECK_MSG(!trk_ray_polyline(past, fwd, line, 2, 8192.f).ok, "a crossing behind the camera is refused");
}
static void tb_0137_trk_ray_nearest_crossing_wins() {
  // A U: the far side comes first in the list, the near side last.
  const TrkPt u[4] = { { -100.f, 0.f, 300.f, 0.f }, { 100.f, 0.f, 300.f, 0.f }, { 100.f, 0.f, 100.f, 0.f },
                       { -100.f, 0.f, 100.f, 0.f } };
  const Vec3 o = { 0.f, 0.f, 0.f };
  const Vec3 fwd = { 0.f, 0.f, 1.f };
  const TrkHit h = trk_ray_polyline(o, fwd, u, 4, 8192.f);
  CHECK(h.ok && h.seg == 2 && trk_near(h.s, 100.f) && trk_near(h.p.z, 100.f));
}
// The +0xB2C rows: span i holds counts[i]+1 samples at stride; a non-looped end holds one.
static void tb_0138_trk_flatten_rows() {
  TrkPt s[3 * 4];
  for (int i = 0; i < 12; ++i) { s[i].x = (float)i; s[i].y = 0.f; s[i].z = 0.f; s[i].len = 0.f; }
  const int counts[3] = { 3, 2, 0 };
  TrkPt out[TRK_MAX_PTS];
  int n = trk_flatten(s, 3, 4, counts, 0, out, TRK_MAX_PTS);
  CHECK(n == 4 + 3 + 1 && out[0].x == 0.f && out[3].x == 3.f && out[4].x == 4.f && out[6].x == 6.f && out[7].x == 8.f);
  const int loop[3] = { 3, 2, 1 };
  n = trk_flatten(s, 3, 4, loop, 1, out, TRK_MAX_PTS);
  CHECK_MSG(n == 4 + 3 + 2 && out[8].x == 9.f, "a looped track's last span runs back to point 0");
  const int bad[3] = { 4, 2, 0 };   // 5 samples in a row of 4
  CHECK(trk_flatten(s, 3, 4, bad, 0, out, TRK_MAX_PTS) == -1);
  CHECK_MSG(trk_flatten(s, 3, 4, counts, 0, out, 11) == -1, "n * stride past the cap is refused");
}

// A secret whose +0xAE4 dropped to 0 is found (watch log 2026-09-29): no marker, arrow or volume.
static void tb_0139_secret_found_hides_it() {
  EntSnap e;
  memset(&e, 0, sizeof(e));
  e.cat = CAT_SECRET; e.armed = 1;
  CHECK(!ent_secret_found(e));
  e.armed = 0;
  CHECK(ent_secret_found(e));
  e.cat = CAT_ITEM;   // armed is 0 for every other category
  CHECK_MSG(!ent_secret_found(e), "only a secret can be found");
}

// The Golden Eagle fires once per press (weapons.md): pulse it; hold the automatics.
static void tb_0140_tbot_pulse_semi_auto() {
  CHECK(tbot_weapon_semi_auto(WK_GEAGLE));
  CHECK(!tbot_weapon_semi_auto(WK_ARIFLE) && !tbot_weapon_semi_auto(WK_SHOTGUN) && !tbot_weapon_semi_auto(WK_PRPG));
  int last = 0;
  int got[6];
  for (int i = 0; i < 4; ++i) got[i] = tbot_pulse(1, 1, &last);
  CHECK_MSG(got[0] == 1 && got[1] == 0 && got[2] == 1 && got[3] == 0, "press, release, press, release");
  CHECK(tbot_pulse(0, 1, &last) == 0 && last == 0);
  CHECK_MSG(tbot_pulse(1, 1, &last) == 1, "a new hold presses at once");
  last = 0;
  for (int i = 0; i < 3; ++i) got[i] = tbot_pulse(1, 0, &last);
  CHECK_MSG(got[0] == 1 && got[1] == 1 && got[2] == 1, "automatics hold every frame");
  CHECK(tbot_pulse(0, 0, &last) == 0);
}

// What proj_bullet_damage_hit 0x1003ad90 does to each object on the ray (aim.md section 4b): a wall or an
// undamageable object stops the bullet; what the engine can break lets the bot keep firing.
static void tb_0141_aim_obstacle_kind() {
  AimObsFacts f;
  memset(&f, 0, sizeof f);
  f.type = 0x101;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_BLOCK, "world_e");
  memset(&f, 0, sizeof f);
  f.type = 3; f.user = 0x20; f.actorDamages = 1;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_DESTRUCTIBLE, "a monster takes the damage");
  f.user = 0xA0;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_PASS, "the walk skips a monster with user bit 0x80");
  f.user = 0x20; f.dyingMonster = 1;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_PASS, "a dying monster takes no damage and no budget");
  memset(&f, 0, sizeof f);
  f.type = 3; f.actorDamages = 0;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_BLOCK, "a babe: vt+0x198 is a bare RET, the bullet stops");
  f.actorDamages = 1;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_DESTRUCTIBLE, "a barrel or crate");
  memset(&f, 0, sizeof f);
  f.type = 1; f.handler = AIM_H_BREAKABLE; f.breakArmed = 1;
  CHECK(aim_obstacle_kind(f) == AIM_OBS_DESTRUCTIBLE);
  f.breakArmed = 0;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_BLOCK, "the handler's own +0xC44 gate is off");
  f.breakArmed = 1; f.objClass = AIM_OBJ_BREAKABLE_ADV; f.advArmed = 0;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_BLOCK, "breakable_adv with +0xBE4 == 0 is never called");
  f.advArmed = 1;
  CHECK(aim_obstacle_kind(f) == AIM_OBS_DESTRUCTIBLE);
  f.objClass = AIM_OBJ_BREAKABLE;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_BLOCK, "class breakable is never called");
  memset(&f, 0, sizeof f);
  f.type = 1; f.handler = AIM_H_NOOP;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_BLOCK, "force_field, doors: object_e::damage is a bare RET");
  f.handler = AIM_H_UNKNOWN;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_BLOCK, "an unread handler blocks");
  f.type = 0;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_PASS, "neither bit: the bullet leaves its budget");
}
// The history phys_ray_vs_objects prism!0x40f1b0 builds with block = world-only and touch = everything
// else: every touch hit in gather order (past the wall too), then the single nearest world hit, last.
static void tb_0142_aim_ray_engine_history() {
  int blk = 7;
  AimRayRec a[4] = { { 300.f, 1, AIM_OBS_PASS, 0 }, { 700.f, 0, AIM_OBS_DESTRUCTIBLE, 0 },
                     { 100.f, 0, AIM_OBS_DESTRUCTIBLE, 0 }, { 500.f, 0, AIM_OBS_BLOCK, 1 } };
  int n = aim_ray_sort_cut(a, 4);
  CHECK_MSG(n == 3 && a[0].dist == 100.f && a[1].target && a[2].world, "sorted, cut after the wall");
  CHECK_MSG(aim_ray_first_target(a, n, &blk) == 1 && blk == -1, "a crate in front does not block");
  AimRayRec b[3] = { { 600.f, 1, AIM_OBS_PASS, 0 }, { 100.f, 0, AIM_OBS_DESTRUCTIBLE, 0 }, { 400.f, 0, AIM_OBS_BLOCK, 1 } };
  n = aim_ray_sort_cut(b, 3);
  CHECK(n == 2);
  CHECK_MSG(aim_ray_first_target(b, n, &blk) == -1 && blk == 1, "a target behind the wall is cut off");
  AimRayRec c[3] = { { 300.f, 1, AIM_OBS_PASS, 0 }, { 100.f, 0, AIM_OBS_BLOCK, 0 }, { 900.f, 0, AIM_OBS_BLOCK, 1 } };
  n = aim_ray_sort_cut(c, 3);
  CHECK_MSG(n == 3 && aim_ray_first_target(c, n, &blk) == -1 && blk == 0, "a hostage in front blocks");
  AimRayRec d[2] = { { 200.f, 0, AIM_OBS_DESTRUCTIBLE, 0 }, { 50.f, 1, AIM_OBS_PASS, 0 } };
  n = aim_ray_sort_cut(d, 2);
  CHECK_MSG(n == 2 && aim_ray_first_target(d, n, &blk) == 0, "no wall on the ray: every record counts");
  AimRayRec e[1] = { { 80.f, 0, AIM_OBS_BLOCK, 1 } };
  n = aim_ray_sort_cut(e, 1);
  CHECK(n == 1 && aim_ray_first_target(e, n, &blk) == -1 && blk == 0);
  CHECK(aim_ray_sort_cut(e, 0) == 0);
}
// The engine's handlers, byte for byte (duke 0x1008b630, 0x100bab30, thunk 0x100bb6c0).
static void tb_0143_aim_damage_handler_fingerprints() {
  static const unsigned char dfi[36] = { 0x8B, 0x44, 0x24, 0x0C, 0x8B, 0x11, 0x56, 0x8B, 0x70, 0x1C, 0x56, 0x8B,
    0x70, 0x18, 0x56, 0x50, 0x8B, 0x44, 0x24, 0x18, 0x50, 0x8B, 0x44, 0x24, 0x18, 0x50, 0xFF, 0x92, 0x98, 0x01,
    0x00, 0x00, 0x5E, 0xC2, 0x0C, 0x00 };
  CHECK(aim_fp_is_damage_from_info(dfi));
  static const unsigned char brk[26] = { 0x8B, 0x81, 0x44, 0x0C, 0x00, 0x00, 0x85, 0xC0, 0x74, 0x0D, 0x8B, 0x44,
    0x24, 0x08, 0x89, 0x44, 0x24, 0x08, 0xE9, 0x69, 0x6A, 0x03, 0x00, 0xC2, 0x08, 0x00 };
  CHECK(aim_fp_is_breakable_handler(brk) && !aim_fp_is_breakable_handler(dfi));
  static const unsigned char thunk[13] = { 0x8B, 0x44, 0x24, 0x08, 0x89, 0x44, 0x24, 0x08, 0xE9, 0x63, 0xF4, 0xFF, 0xFF };
  unsigned t = 0;
  CHECK(aim_thunk_target(thunk, 0x100bb6c0u, &t) && t == 0x100bab30u);
  CHECK(!aim_thunk_target(brk, 0x100bab30u, &t));
  static const unsigned char r8[3] = { 0xC2, 0x08, 0x00 }, r14[3] = { 0xC2, 0x14, 0x00 };
  CHECK(aim_is_ret(r8, 8) && !aim_is_ret(r8, 0x14) && aim_is_ret(r14, 0x14));
}

// weap_geagle_fire_logic 0x10055120 shoots on a press only after it has seen a release (+0xC1E): the
// press follows that latch, so a release the weapon missed is repeated rather than lost.
static void tb_0144_tbot_semi_press_follows_the_latch() {
  int last = 0;
  CHECK(tbot_semi_press(1, 1, &last) == 1 && last == 1);
  CHECK_MSG(tbot_semi_press(1, 1, &last) == 1, "held until the weapon fires (the latch stays set)");
  CHECK_MSG(tbot_semi_press(1, 0, &last) == 0, "fired: released until the weapon sees it");
  CHECK_MSG(tbot_semi_press(1, 0, &last) == 0, "a missed release frame is repeated");
  CHECK(tbot_semi_press(1, 1, &last) == 1);
  CHECK(tbot_semi_press(0, 1, &last) == 0 && last == 0);
  last = 0;
  CHECK_MSG(tbot_semi_press(1, -1, &last) == 1 && tbot_semi_press(1, -1, &last) == 0 &&
            tbot_semi_press(1, -1, &last) == 1, "latch unread: alternate frames");
}

// An area's volume is its avolume range (local min/max, area_e::get_range p3shared!0x10001580) through the
// world matrix at +0xF4 (area_e::inside tests the point through its inverse, +0x134): its centre is not +0x18C.
static void tb_0145_area_bounds_through_the_world_matrix() {
  const float mn[3] = { -100.f, 0.f, -20.f }, mx[3] = { 300.f, 200.f, 20.f };
  float m[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  1000.f, -50.f, 128.f, 1 };
  Vec3 c, h;
  area_bounds(mn, mx, m, &c, &h);
  CHECK(c.x == 1100.f && c.y == 50.f && c.z == 128.f);
  CHECK_MSG(h.x == 200.f && h.y == 100.f && h.z == 20.f, "an offset range keeps its size");
  // A quarter turn about y (row vectors: x -> -z, z -> x) swaps the x and z extents.
  float r[16] = { 0, 0, -1, 0,  0, 1, 0, 0,  1, 0, 0, 0,  0, 0, 0, 1 };
  area_bounds(mn, mx, r, &c, &h);
  CHECK(fabsf(c.x) < 1e-3f && fabsf(c.z + 100.f) < 1e-3f && fabsf(h.x - 20.f) < 1e-3f && fabsf(h.z - 200.f) < 1e-3f);
}

// FUN_10094c50 samples a span at most 32 times (ftol of 32.0 at 0x10138684), so the stride
// reaches 33; world.scr tracks have up to 26 points: 858 samples must be readable (gate round 2, item 4).
static void tb_0146_trk_largest_engine_layout_fits() {
  static TrkPt s[26 * 33];
  static TrkPt out[TRK_MAX_PTS];
  int counts[26];
  for (int i = 0; i < 26 * 33; ++i) { s[i].x = (float)i; s[i].y = 0.f; s[i].z = 0.f; s[i].len = 0.f; }
  for (int i = 0; i < 26; ++i) counts[i] = 32;
  CHECK(trk_flatten(s, 26, 33, counts, 0, out, TRK_MAX_PTS) == 25 * 33 + 1);
  CHECK(trk_flatten(s, 26, 33, counts, 1, out, TRK_MAX_PTS) == 26 * 33);
}

// A refused placement keeps its red marker on Duke's line: the track point nearest the ray in XZ, at the
// ray's y where the ray passes closest (gate round 2, item 5).
static void tb_0147_trk_ray_nearest_point() {
  const TrkPt seg[2] = { { 100.f, 0.f, 300.f, 0.f }, { 300.f, 0.f, 300.f, 200.f } };
  const Vec3 o = { 0.f, 40.f, 0.f };
  const Vec3 d = { 0.f, -0.1f, 1.f };   // along +z, x = 0: nearest is the end (100, 300) at s = 300
  TrkHit h = trk_ray_nearest(o, d, seg, 2, 8192.f);
  CHECK(h.ok && trk_near(h.p.x, 100.f) && trk_near(h.p.z, 300.f) && trk_near(h.s, 300.f) && trk_near(h.p.y, 10.f));
  const TrkPt line[2] = { { -500.f, 0.f, 100.f, 0.f }, { 500.f, 0.f, 100.f, 1000.f } };
  const Vec3 fwd = { 0.f, 0.f, 1.f };
  h = trk_ray_nearest(o, fwd, line, 2, 8192.f);
  CHECK_MSG(h.ok && trk_near(h.p.x, 0.f) && trk_near(h.p.z, 100.f) && trk_near(h.p.y, 40.f), "a crossing is its own nearest");
  const Vec3 back = { 0.f, 0.f, -1.f };
  h = trk_ray_nearest(o, back, line, 2, 8192.f);
  CHECK_MSG(h.ok && trk_near(h.s, 0.f) && trk_near(h.p.x, 0.f) && trk_near(h.p.z, 100.f), "behind: nearest to the eye");
  CHECK(!trk_ray_nearest(o, fwd, line, 1, 8192.f).ok);
}

// Off-screen targets: a Combat switch, default on; off, the clip is stock and the
// aim filters want the enemy on the game's own screen.
static void tb_0148_offscreen_targets_toggle() {
  config_defaults();
  CHECK(g_cfg.aimOffscreen == 1);
  CHECK(offs_active(1, 1));
  CHECK_MSG(!offs_active(1, 0), "armed but switched off: stock clip, on-screen filters");
  CHECK(!offs_active(0, 1));
}

// A rotated area is drawn as its own box, the local range's 8 corners through +0xF4.
static bool v3near(Vec3 a, float x, float y, float z) { return fabsf(a.x - x) < 1e-3f && fabsf(a.y - y) < 1e-3f && fabsf(a.z - z) < 1e-3f; }
static void tb_0149_area_obb_corners() {
  const float mn[3] = { -100.f, 0.f, -20.f }, mx[3] = { 300.f, 200.f, 20.f };
  float r[16] = { 0, 0, -1, 0,  0, 1, 0, 0,  1, 0, 0, 0,  0, 0, 0, 1 };   // x -> -z, z -> x
  Vec3 c, ax[3];
  area_obb(mn, mx, r, &c, ax);
  CHECK(v3near(c, 0.f, 100.f, -100.f));
  CHECK(v3near(ax[0], 0.f, 0.f, -200.f) && v3near(ax[1], 0.f, 100.f, 0.f) && v3near(ax[2], 20.f, 0.f, 0.f));
  CHECK_MSG(v3near(volume_corner(c, ax, 0), -20.f, 0.f, 100.f), "corner 0 = c - every axis");
  CHECK(v3near(volume_corner(c, ax, 7), 20.f, 200.f, -300.f));
  const Vec3 aa[3] = { { 5.f, 0.f, 0.f }, { 0.f, 6.f, 0.f }, { 0.f, 0.f, 7.f } };
  const Vec3 o = { 1.f, 2.f, 3.f };
  CHECK_MSG(v3near(volume_corner(o, aa, 5), 6.f, -4.f, 10.f), "bit 0 x, bit 1 y, bit 2 z, as volume_project");
}
// A replayed shot proves the bullet passed an object only when that object lies in front of
// where the victim was hit, and the victim was not killed (a dead or moved victim leaves the ray).
static void tb_0150_offs_shot_verdict() {
  CHECK(offs_shot_verdict(0, 0, 0, 100.f, 0.f) == OFFS_SHOT_NOHIT);
  CHECK(offs_shot_verdict(1, 1, 0, 300.f, 300.f) == OFFS_SHOT_AGREE);
  CHECK(offs_shot_verdict(1, 0, 1, 100.f, 300.f) == OFFS_SHOT_KILLED);
  CHECK(offs_shot_verdict(1, 0, 0, 100.f, 300.f) == OFFS_SHOT_PASSED);
  CHECK_MSG(offs_shot_verdict(1, 0, 0, 290.f, 300.f) == OFFS_SHOT_BEHIND, "within the margin of the victim");
  CHECK_MSG(offs_shot_verdict(1, 0, 0, 500.f, 300.f) == OFFS_SHOT_BEHIND, "the replica stops past the victim");
}
// A Golden Eagle press is a shot only when +0xB18 moved on the normal path, which alone clears the
// latch (weap_geagle_fire_logic 0x10055120); the reload and dry paths move B18 and keep it set.
static void tb_0151_tbot_press_outcome() {
  CHECK(tbot_press_outcome(0, 1, 0) == TBOT_PRESS_UNREAD);
  CHECK(tbot_press_outcome(1, 0, 1) == TBOT_PRESS_NOSHOT);
  CHECK(tbot_press_outcome(1, 1, 0) == TBOT_PRESS_SHOT);
  CHECK(tbot_press_outcome(1, 1, 1) == TBOT_PRESS_RELOAD_DRY);
}

// Gate round 3, item 1: real bullets pass the blocker (clip volume, ctor duke!0x100b9930) and pickups
// (user 0x1000, powerup_base_ctor 0x1005d051) -- the user's sighting. A force field and walls still block,
// and only a blocking world_e cuts the ray as the nearest wall does.
static void tb_0152_aim_blockers_and_pickups_pass() {
  AimObsFacts f;
  memset(&f, 0, sizeof f);
  f.type = 0x200101; f.user = 0x18000; f.objClass = AIM_OBJ_BLOCKER; f.handler = AIM_H_NOOP;
  int k = aim_obstacle_kind(f);
  CHECK_MSG(k == AIM_OBS_PASS, "a blocker does not stop the bullet");
  CHECK_MSG(!aim_obstacle_cuts(f, k), "a passed blocker does not cut the ray like a wall");
  memset(&f, 0, sizeof f);
  f.type = 3; f.user = 0x1017;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_PASS, "an ammo or ego pickup");
  memset(&f, 0, sizeof f);
  f.type = 0x200101; f.user = 0x200; f.handler = AIM_H_NOOP;
  k = aim_obstacle_kind(f);
  CHECK_MSG(k == AIM_OBS_BLOCK && aim_obstacle_cuts(f, k), "a force field still blocks");
  memset(&f, 0, sizeof f);
  f.type = 0x101;
  k = aim_obstacle_kind(f);
  CHECK_MSG(k == AIM_OBS_BLOCK && aim_obstacle_cuts(f, k), "the map");
  memset(&f, 0, sizeof f);
  f.type = 3; f.user = 0x4000017; f.actorDamages = 1;
  k = aim_obstacle_kind(f);
  CHECK_MSG(k == AIM_OBS_DESTRUCTIBLE && !aim_obstacle_cuts(f, k), "a barrel is no wall");
}
// Gate round 3, item 2: babe_activate duke!0x10060ac0 sets +0xE10 from 0 (bound) to 1; the handler moves it on
// to 2 and 3 and never back. A freed babe loses its marker and arrow.
static void tb_0153_babe_freed_hides_it() {
  EntSnap e;
  memset(&e, 0, sizeof(e));
  e.cat = CAT_BABE;
  CHECK(!ent_babe_freed(e));
  e.freed = 1;
  CHECK(ent_babe_freed(e));
  e.cat = CAT_ITEM;
  CHECK_MSG(!ent_babe_freed(e), "only a babe can be freed");
}
// Gate round 3, item 1c: a monster's damage arrives as net message 0x82 after the cast (monster_damage_send
// 0x1002f3f0, applied at 0x10031b17), so a cast is replayed only OFFS_SHOT_DELAY frames later.
static void tb_0154_offs_shot_due() {
  CHECK(!offs_shot_due(10, 10));
  CHECK(!offs_shot_due(10, 10 + OFFS_SHOT_DELAY - 1));
  CHECK(offs_shot_due(10, 10 + OFFS_SHOT_DELAY));
  CHECK_MSG(offs_shot_due(0xFFFFFFFFu, OFFS_SHOT_DELAY - 1), "the frame counter wraps");
}
// The damage's dir is the shot's (end - from) scaled by the frame time: a hit belongs to the oldest open cast
// along that direction; no direction falls back to the newest open cast.
static void tb_0155_offs_hit_slot() {
  static OffsShot r[OFFS_SHOT_RING];
  memset(r, 0, sizeof r);
  const float x[3] = { 1.f, 0.f, 0.f }, y[3] = { 0.f, 1.f, 0.f };
  r[1].seq = 1; r[1].frame = 10; memcpy(r[1].dir, x, sizeof x);
  r[2].seq = 2; r[2].frame = 11; memcpy(r[2].dir, y, sizeof y);
  r[3].seq = 3; r[3].frame = 11; memcpy(r[3].dir, x, sizeof x);
  const float hx[3] = { 0.02f, 0.f, 0.f }, neg[3] = { 0.f, -0.5f, 0.f }, zero[3] = { 0.f, 0.f, 0.f };
  CHECK_MSG(offs_hit_slot(r, 3, 12, hx) == 1, "the oldest open cast along the direction");
  r[1].victim = &r[1];
  CHECK(offs_hit_slot(r, 3, 12, hx) == 3);
  CHECK_MSG(offs_hit_slot(r, 3, 12, neg) == -1, "an opposite direction matches nothing");
  CHECK_MSG(offs_hit_slot(r, 3, 12, zero) == 3, "no direction: the newest open cast");
  r[1].victim = 0;
  CHECK_MSG(offs_hit_slot(r, 3, 13, hx) == 3, "a cast already replayed is closed");
  r[3].seq = 19;
  CHECK_MSG(offs_hit_slot(r, 3, 12, hx) == 1, "a slot reused by a newer cast is not seq 3");
}

// The borderless state machine (display.h). A flip while applied reverts; a failed
// apply is not retried until the switch moves; nothing happens until the window is ready.
static void tb_0156_bl_state_machine() {
  BlState s;
  memset(&s, 0, sizeof s);
  CHECK_MSG(bl_step(&s, 1, 0) == BL_NONE, "no window yet: wait");
  CHECK_MSG(bl_step(&s, 1, 1) == BL_APPLY, "startup: apply once the window exists");
  bl_done_apply(&s, 1);
  CHECK_MSG(bl_step(&s, 1, 1) == BL_NONE, "applied: nothing more per frame");
  CHECK_MSG(bl_step(&s, 0, 1) == BL_REVERT, "switch off mid-session: revert");
  bl_done_revert(&s);
  CHECK(bl_step(&s, 0, 1) == BL_NONE);
  CHECK_MSG(bl_step(&s, 1, 1) == BL_APPLY, "switch on again: apply again");
  bl_done_apply(&s, 0);
  CHECK_MSG(bl_step(&s, 1, 1) == BL_NONE, "a failed apply is not retried every frame");
  CHECK_MSG(bl_step(&s, 0, 1) == BL_NONE, "nothing applied: nothing to revert");
  CHECK_MSG(bl_step(&s, 1, 1) == BL_APPLY, "a switch flip clears the failure");
  bl_done_apply(&s, 1);
  CHECK_MSG(bl_step(&s, 0, 0) == BL_NONE, "not ready (fullscreen): the revert waits");
  CHECK(bl_step(&s, 0, 1) == BL_REVERT);
  memset(&s, 0, sizeof s);
  CHECK_MSG(bl_step(&s, 0, 1) == BL_NONE, "off from the start: stock, nothing to do");
}
// The stock window is captured only while nothing of ours is applied, so a revert restores the
// player's own window and r_mode, never our borderless one.
static void tb_0157_bl_capture_and_mode() {
  BlStock st;
  memset(&st, 0, sizeof st);
  const BlRect rc = { 0, 0, 3446, 1459 }, full = { 0, 0, 3440, 1440 };
  CHECK(bl_capture(&st, 0, 0x02C80000u, 0x300u, rc, "3440x1440x32"));
  CHECK(st.valid && st.style == 0x02C80000u && st.exstyle == 0x300u && st.rect.r == 3446);
  CHECK(strcmp(st.mode, "3440x1440x32") == 0);
  CHECK_MSG(!bl_capture(&st, 1, 0x90000000u, 0u, full, "3440x1440x32"), "applied: keep the stock");
  CHECK(st.style == 0x02C80000u && st.rect.r == 3446);
  CHECK_MSG(bl_capture(&st, 0, 0x02C80000u, 0x300u, rc, "1920x1080x32"), "after a revert: capture again");
  CHECK(strcmp(st.mode, "1920x1080x32") == 0);
  int w = 0, h = 0, bpp = 0;
  CHECK(bl_parse_mode("3440x1440x32", &w, &h, &bpp) && w == 3440 && h == 1440 && bpp == 32);
  CHECK(bl_parse_mode("1280x720", &w, &h, &bpp) && w == 1280 && h == 720 && bpp == 0);
  CHECK(!bl_parse_mode("banana", &w, &h, &bpp));
  CHECK(!bl_parse_mode("0x720x32", &w, &h, &bpp));
  char buf[32];
  CHECK(bl_mode_string(buf, sizeof buf, 3440, 1440, 32) && strcmp(buf, "3440x1440x32") == 0);
  CHECK_MSG(!bl_mode_string(buf, 6, 3440, 1440, 32), "too small a buffer is refused, not truncated");
}
// The monitor holding most of the window; the primary when the window is on none.
static void tb_0158_bl_pick_monitor() {
  const BlRect mons[2] = { { 0, 0, 3440, 1440 }, { 3440, 0, 5360, 1080 } };
  const BlRect onFirst = { 100, 100, 1100, 900 }, mostlySecond = { 3000, 0, 4400, 800 };
  const BlRect straddleFirst = { 2500, 0, 3900, 800 }, nowhere = { -5000, -5000, -4000, -4000 };
  CHECK(bl_pick_monitor(mons, 2, onFirst, 0) == 0);
  CHECK(bl_pick_monitor(mons, 2, mostlySecond, 0) == 1);
  CHECK(bl_pick_monitor(mons, 2, straddleFirst, 1) == 0);
  CHECK_MSG(bl_pick_monitor(mons, 2, nowhere, 1) == 1, "off every monitor: the primary");
  CHECK_MSG(bl_pick_monitor(mons, 2, nowhere, 5) == 0, "no valid primary: the first");
  CHECK(bl_pick_monitor(mons, 0, onFirst, 0) == -1);
}
// The subclass's rewrites: the fullscreen style and the monitor rect while on, the engine's own while off.
static void tb_0159_bl_style_and_pos_filter() {
  CHECK(bl_style_for(1, BL_GWL_STYLE, 0x00C80000u) == 0x90000000u);
  CHECK(bl_style_for(1, BL_GWL_EXSTYLE, 0x300u) == 0u);
  CHECK(bl_style_for(0, BL_GWL_STYLE, 0x00C80000u) == 0x00C80000u);
  CHECK(bl_style_for(0, BL_GWL_EXSTYLE, 0x300u) == 0x300u);
  const BlRect mon = { 0, 0, 3440, 1440 };
  BlPos p = { 10, 20, 3446, 1459 };
  CHECK(!bl_fix_pos(0, 0x40, &p, mon) && p.x == 10 && p.cx == 3446);
  CHECK(bl_fix_pos(1, 0x40, &p, mon) && p.x == 0 && p.y == 0 && p.cx == 3440 && p.cy == 1440);
  BlPos q = { 10, 20, 800, 600 };
  CHECK(bl_fix_pos(1, BL_SWP_NOMOVE, &q, mon) && q.x == 10 && q.y == 20 && q.cx == 3440 && q.cy == 1440);
  BlPos z = { 10, 20, 800, 600 };
  CHECK_MSG(!bl_fix_pos(1, BL_SWP_NOMOVE | BL_SWP_NOSIZE, &z, mon) && z.cx == 800, "z-order only");
  BlPos m = { -32000, -32000, 160, 28 };
  CHECK_MSG(!bl_fix_pos(1, 0x8000, &m, mon) && m.x == -32000 && m.cx == 160, "a minimise is left alone");
  const BlRect second = { 3440, 0, 5360, 1080 };
  BlPos r = { 0, 0, 1, 1 };
  CHECK(bl_fix_pos(1, 0, &r, second) && r.x == 3440 && r.cx == 1920 && r.cy == 1080);
}
// Minimise diagnostics: logged while borderless is on, silent while off, never swallowed.
static void tb_0160_bl_minimise_messages() {
  const unsigned WM_SIZE_ = 0x5, WM_ACTIVATEAPP_ = 0x1C, WM_SYSCOMMAND_ = 0x112;
  CHECK(bl_minimise_msg(1, WM_SYSCOMMAND_, 0xF020) == BL_MSG_LOG);
  CHECK_MSG(bl_minimise_msg(1, WM_SYSCOMMAND_, 0xF022) == BL_MSG_LOG, "low 4 bits are the system's");
  CHECK(bl_minimise_msg(1, WM_SIZE_, 1) == BL_MSG_LOG);
  CHECK(bl_minimise_msg(1, WM_ACTIVATEAPP_, 0) == BL_MSG_LOG);
  CHECK(bl_minimise_msg(1, WM_ACTIVATEAPP_, 1) == BL_MSG_LOG);
  CHECK_MSG(bl_minimise_msg(1, WM_SIZE_, 0) == BL_MSG_PASS, "a restore is not a minimise");
  CHECK_MSG(bl_minimise_msg(1, WM_SYSCOMMAND_, 0xF100) == BL_MSG_PASS, "SC_KEYMENU");
  CHECK(bl_minimise_msg(0, WM_SYSCOMMAND_, 0xF020) == BL_MSG_PASS);
  CHECK(bl_minimise_msg(0, WM_SIZE_, 1) == BL_MSG_PASS);
  CHECK(bl_minimise_msg(0, WM_ACTIVATEAPP_, 0) == BL_MSG_PASS);
}
static void tb_0161_bl_config_default_on() {
  config_defaults();
  CHECK_MSG(g_cfg.borderless == 1, "Borderless window defaults on (user, 2026-09-30)");
}

// The script-kind check behind the "which script wins" log line. Heads are the two
// real files (db_e01 and db_wide, 2026-09-30), cut to their distinguishing lines.
static void tb_0162_ft_script_kind() {
  const char* stock = "spawn ui_window {\r\n\tname:\"actor_text\"\r\n\tcsystem:\"0 0 640 480\"\r\n"
                      "\tcoords:\"0 0 640 480\"\r\n\t\r\n\tspawn ui_text {\r\n\t\tcsystem:\"0 0 640 480\"\r\n";
  const char* wide = "$$_aspect = 3440 / 1440\r\n\r\n# Widescreen (640x480 source)\r\n"
                     "$$_system480_x2 = 480 * $_aspect\r\n";
  CHECK(ft_script_kind(stock) == FT_STOCK43);
  CHECK(ft_script_kind(wide) == FT_WIDE);
  CHECK(ft_script_kind("spawn ui_window {\r\n\tcsystem:\"0 0 1024 768\"\r\n") == FT_OTHER);
  CHECK(ft_script_kind("") == FT_OTHER);
  CHECK(ft_script_kind(0) == FT_OTHER);
}

// A mode the player chose while borderless is applied becomes the stock mode (so the quit
// restore writes it) and borderless re-applies without recapturing the borderless window as stock.
static void tb_0163_bl_mode_drift() {
  BlState s;
  BlStock st;
  memset(&s, 0, sizeof s);
  memset(&st, 0, sizeof st);
  const BlRect rc = { 0, 0, 3446, 1459 }, full = { 0, 0, 3440, 1440 };
  bl_capture(&st, 0, 0x02C80000u, 0x300u, rc, "1920x1080x32");
  CHECK(bl_step(&s, 1, 1) == BL_APPLY);
  bl_done_apply(&s, 1);
  CHECK(bl_mode_drift(&s, &st, "3440x1440x32", "3440x1440x32") == BL_DRIFT_NONE);
  CHECK(strcmp(st.mode, "1920x1080x32") == 0);
  CHECK(bl_mode_drift(&s, &st, "3440x1440x32", "2560x1080x32") == BL_DRIFT_REAPPLY);
  CHECK_MSG(strcmp(st.mode, "2560x1080x32") == 0, "the player's choice is the new stock mode");
  CHECK_MSG(bl_step(&s, 1, 1) == BL_APPLY, "re-applied at the monitor size");
  CHECK_MSG(!bl_capture(&st, s.applied || s.reapply, 0x90000000u, 0u, full, "2560x1080x32"),
            "a re-apply keeps the stock window");
  CHECK(st.style == 0x02C80000u && st.rect.r == 3446);
  bl_done_apply(&s, 1);
  CHECK_MSG(!s.reapply, "done");
  CHECK_MSG(bl_mode_drift(&s, &st, "3440x1440x32", "") == BL_DRIFT_NONE, "an unreadable mode is no drift");
  memset(&s, 0, sizeof s);
  CHECK_MSG(bl_mode_drift(&s, &st, "3440x1440x32", "2560x1080x32") == BL_DRIFT_NONE, "nothing applied");
  s.applied = 1;
  int giveup = 0;
  for (int i = 0; i < BL_DRIFT_MAX + 1; i++) {
    const int d = bl_mode_drift(&s, &st, "3440x1440x32", "2560x1080x32");
    if (d == BL_DRIFT_GIVEUP) { giveup = i; break; }
    bl_done_apply(&s, 1);
  }
  CHECK_MSG(giveup == BL_DRIFT_MAX, "the engine fighting the mode stops after BL_DRIFT_MAX re-applies");
  CHECK(s.failed == 1);
}
// The first apply waits for the main menu or a level; the first seen is named once.
static void tb_0164_bl_scene_gate() {
  BlState s;
  memset(&s, 0, sizeof s);
  CHECK(bl_note_scene(&s, 0, 0) == BL_SCENE_NONE && !s.scene);
  CHECK(bl_note_scene(&s, 1, 0) == BL_SCENE_MENU && s.scene);
  CHECK_MSG(bl_note_scene(&s, 0, 0) == BL_SCENE_NONE && s.scene, "latched, reported once");
  memset(&s, 0, sizeof s);
  CHECK(bl_note_scene(&s, 0, 1) == BL_SCENE_LEVEL);
  memset(&s, 0, sizeof s);
  CHECK_MSG(bl_note_scene(&s, 1, 1) == BL_SCENE_MENU, "both: the menu is named");
}

// gl's rend_restart replaces the game window (window handles 905A6 -> 7C0D0C -> B0114 in one run):
// the subclass follows the engine's window, and adopts one created under our class proc.
static void tb_0165_win_check() {
  static int objs_[2]; int& a = objs_[0]; int& b = objs_[1];
  const void* oldW = &a;
  const void* newW = &b;
  CHECK(win_check(oldW, 1, oldW) == WIN_KEEP);
  CHECK_MSG(win_check(oldW, 0, newW) == WIN_REPLACED, "gl restart: old destroyed, engine has a new one");
  CHECK_MSG(win_check(oldW, 1, newW) == WIN_REPLACED, "a new window while the old one lives");
  CHECK_MSG(win_check(oldW, 0, 0) == WIN_GONE, "destroyed, no engine window yet: search again");
  CHECK_MSG(win_check(oldW, 0, oldW) == WIN_GONE, "the engine still names a dead handle");
  CHECK_MSG(win_check(oldW, 1, 0) == WIN_KEEP, "engine window unknown: keep a live one");
  CHECK_MSG(win_check(0, 0, newW) == WIN_KEEP, "nothing installed: the search path owns it");
  CHECK(win_install_mode(1) == WIN_ADOPT);
  CHECK(win_install_mode(0) == WIN_SUBCLASS);
}

// GLDirect took the monitor-sized popup for fullscreen (its log says "(fullscreen)") and its
// device minimised on focus loss. The window is one row taller than the monitor, the row off-screen.
static void tb_0166_bl_window_rect_fails_gld_fullscreen_test() {
  const BlRect mons[3] = { { 0, 0, 3440, 1440 }, { 3440, 0, 5360, 1080 }, { 0, -1080, 1920, 0 } };
  const BlRect up = bl_window_rect(mons[0], mons, 2);
  CHECK_MSG(up.l == 0 && up.t == -1 && up.r == 3440 && up.b == 1440, "the extra row above the monitor");
  CHECK_MSG(up.b - up.t != 1440, "the client no longer equals the display mode");
  const BlRect down = bl_window_rect(mons[0], mons, 3);
  CHECK_MSG(down.t == 0 && down.b == 1441 && down.r == 3440, "a monitor above: the row goes below");
  const BlRect second = bl_window_rect(mons[1], mons, 3);
  CHECK(second.l == 3440 && second.t == -1 && second.b == 1080);
}

// The Windows key and hotkeys switch (hotkeys.h). The signature is input.dll's own bytes at 0x1000155a
// with g_kbd = 0x100099f0; input.dll's image is 0xBC80 bytes (Ghidra, 2026-10-01).
static void tb_0168_hk_sig_finds_the_keyboard_slot() {
  unsigned char code[64];
  memset(code, 0x90, sizeof code);
  memcpy(code + 10, kHkKbdSig, HK_SIG_LEN);
  const unsigned char slot[4] = { 0xF0, 0x99, 0x00, 0x10 };
  memcpy(code + 10 + HK_SIG_SLOT, slot, 4);
  size_t at = 0;
  CHECK(offs_find(code, sizeof code, kHkKbdSig, kHkKbdMask, HK_SIG_LEN, &at) == 1 && at == 10);
  CHECK_MSG(hk_sig_slot(code + at) == 0x100099f0u, "the imm32 after A1 is &g_kbd");
  CHECK_MSG(kHkKbdSig[HK_SIG_LEVEL] == HK_LEVEL_STOCK, "the push the signature pins is the engine's 0x16");
  CHECK(hk_slot_in_image(0x100099f0u, 0x10000000u, 0xBC80u));
  CHECK_MSG(!hk_slot_in_image(0x1000BC7Eu, 0x10000000u, 0xBC80u), "a slot straddling the image end");
  CHECK_MSG(!hk_slot_in_image(0x0FFFFFFEu, 0x10000000u, 0xBC80u), "below the image");
  CHECK_MSG(!hk_slot_in_image(0xFFFFFFFEu, 0xFFFF0000u, 0x10000u), "no wrap past 4 GB");
  memcpy(code + 40, code + 10, HK_SIG_LEN);
  CHECK_MSG(offs_find(code, sizeof code, kHkKbdSig, kHkKbdMask, HK_SIG_LEN, &at) == 2, "two matches: refuse");
}
static void tb_0169_hk_state_machine() {
  int d1 = 0, d2 = 0, w1 = 0, w2 = 0;
  HkState s;
  memset(&s, 0, sizeof s);
  CHECK_MSG(hk_step(&s, 1, 0, &w1) == HK_NONE, "no keyboard (WndProc input mode): nothing");
  CHECK_MSG(hk_step(&s, 1, &d1, 0) == HK_NONE, "no window yet: wait");
  CHECK_MSG(hk_step(&s, 1, &d1, &w1) == HK_SET_FREE, "on: the engine's device gets level 6");
  hk_done(&s, HK_SET_FREE, &w1, 1);
  CHECK_MSG(s.level == HK_LEVEL_FREE && s.setWnd == &w1, "done records the level and window");
  CHECK_MSG(hk_step(&s, 1, &d1, &w1) == HK_NONE, "applied: nothing per frame");
  CHECK_MSG(hk_step(&s, 1, &d1, &w2) == HK_SET_FREE, "window recreated: set again with the new window");
  hk_done(&s, HK_SET_FREE, &w2, 1);
  CHECK(hk_step(&s, 1, &d1, &w2) == HK_NONE);
  CHECK_MSG(hk_step(&s, 1, &d2, &w2) == HK_SET_FREE, "a new device starts at the engine's level");
  hk_done(&s, HK_SET_FREE, &w2, 1);
  CHECK_MSG(hk_step(&s, 0, &d2, &w2) == HK_SET_STOCK, "off: the device we changed gets 0x16 back");
  hk_done(&s, HK_SET_STOCK, &w2, 1);
  CHECK_MSG(s.level == HK_LEVEL_STOCK, "stock recorded");
  CHECK_MSG(hk_step(&s, 0, &d2, &w2) == HK_NONE, "off and restored: nothing");
  CHECK_MSG(hk_step(&s, 0, &d2, &w1) == HK_NONE, "off: a new window alone never touches the device");
  CHECK_MSG(hk_step(&s, 0, &d1, &w1) == HK_NONE, "off: a device we never changed stays the engine's");
  CHECK(hk_step(&s, 1, &d1, &w1) == HK_SET_FREE);
  hk_done(&s, HK_SET_FREE, &w1, 0);
  CHECK_MSG(hk_step(&s, 1, &d1, &w1) == HK_NONE, "a failed set is not retried every frame");
  CHECK_MSG(hk_step(&s, 1, &d1, &w2) == HK_SET_FREE, "a new window clears the failure");
  hk_done(&s, HK_SET_FREE, &w2, 0);
  CHECK(hk_step(&s, 0, &d1, &w2) == HK_NONE);
  CHECK_MSG(hk_step(&s, 1, &d1, &w2) == HK_SET_FREE, "a switch flip clears the failure");
  memset(&s, 0, sizeof s);
  CHECK_MSG(hk_step(&s, 0, &d1, &w1) == HK_NONE, "off from the start: stock, nothing to do");
}
static void tb_0170_hk_reacquire_and_default() {
  CHECK_MSG(hk_reacquire(0), "DI_OK: it was acquired, acquire again");
  CHECK_MSG(!hk_reacquire(1), "DI_NOEFFECT: released by the engine or the panel, left released");
  CHECK_MSG(!hk_reacquire((long)0x8007001Eu), "an error: never acquire");
  CHECK_MSG(!(HK_LEVEL_FREE & 0x10) && (HK_LEVEL_STOCK & ~0x10) == HK_LEVEL_FREE, "only NOWINKEY differs");
  config_defaults();
  CHECK_MSG(g_cfg.winKeys == 1, "Windows key and hotkeys defaults on");
}

// Windows ships its own SysWOW64\input.dll, so the engine's is taken by its sys\ folder.
static void tb_0171_hk_engine_input_path() {
  CHECK(hk_is_engine_input(tb_path("G:|Games|Duke Nukem - Manhattan Project|Enhanced Edition|sys|input.dll").c_str()));
  CHECK_MSG(hk_is_engine_input(tb_path("G:|Games|DNMP|SYS|Input.DLL").c_str()), "case-insensitive");
  CHECK_MSG(hk_is_engine_input(tb_path("G:~Games~DNMP~sys~input.dll").c_str()), "forward slashes");
  CHECK_MSG(!hk_is_engine_input(tb_path("C:|Windows|SysWOW64|input.dll").c_str()), "Windows' InputSetting DLL");
  CHECK_MSG(!hk_is_engine_input(tb_path("G:|Games|DNMP|xsys|input.dll").c_str()), "a folder merely ending in sys");
  CHECK_MSG(!hk_is_engine_input("sys\\input.dll"), "no folder before sys");
  CHECK(!hk_is_engine_input(tb_path("G:|Games|DNMP|sys|input.dll.bak").c_str()));
  CHECK(!hk_is_engine_input(""));
  CHECK(!hk_is_engine_input(0));
}

// Which scr_intro.sc the engine serves (quick-wins.md section 1). The stock head
// is db_e01's, cut after its first group; ours is the shipped file's first line.
static void tb_0172_si_script_kind() {
  const char* stock = "spawn ui_window {\r\n\tcsystem:\"0 0 640 480\"\r\n\t# background\r\n"
                      "\tspawn ui_group {\r\n\t\tid:1\r\n";
  const char* ours = "# nukepanel skip intro: the stock window and background, no logo groups\r\n"
                     "spawn ui_window {\r\n";
  CHECK(si_script_kind(stock) == SI_STOCK);
  CHECK(si_script_kind(ours) == SI_SKIP);
  CHECK_MSG(si_script_kind("spawn ui_window {\r\n") == SI_OTHER, "a head with no group and no marker");
  CHECK(si_script_kind("") == SI_OTHER);
  CHECK(si_script_kind(0) == SI_OTHER);
}

// The custom field of view (quick-wins.md section 2). 0 stored is the captured stock.
static void tb_0174_fov_want_and_slider() {
  CHECK_MSG(fabs(fov_want_deg(FOV_STOCK, 79.7553) - 79.7553) < 1e-9, "0 is the stock value, exactly");
  CHECK(fov_want_deg(90, 79.7553) == 90.0);
  CHECK_MSG(fov_want_deg(20, 79.7553) == (double)FOV_MIN_DEG, "below the range clamps up");
  CHECK_MSG(fov_want_deg(400, 79.7553) == (double)FOV_MAX_DEG, "above the range clamps down");
  CHECK_MSG(FOV_MIN_DEG < 50 && FOV_MAX_DEG > 120, "well below and above the 79.8 stock");
  CHECK_MSG(fov_slider_value(FOV_STOCK, 79.7553) == 80, "the stock draws as its nearest degree");
  CHECK(fov_slider_value(FOV_STOCK, 0.0) == FOV_MIN_DEG);
  CHECK(fov_slider_value(120, 79.7553) == 120);
  CHECK(fov_slider_value(7, 79.7553) == FOV_MIN_DEG);
}

static void tb_0175_fov_state_machine() {
  FovState s = { 0, 0.0 };
  CHECK_MSG(fov_step(s, 0, 90.0) == FOV_NONE, "off and never applied: nothing");
  CHECK_MSG(fov_step(s, 1, 90.0) == FOV_APPLY, "switched on: capture the stock, then write");
  fov_done(&s, FOV_APPLY, 90.0);
  CHECK(s.applied == 1);
  CHECK_MSG(fov_step(s, 1, 90.0) == FOV_NONE, "held at the same value: no write every frame");
  CHECK_MSG(fov_step(s, 1, 90.0005) == FOV_NONE, "inside the 0.001 degree tolerance");
  CHECK_MSG(fov_step(s, 1, 100.0) == FOV_REWRITE, "a new slider value is written");
  fov_done(&s, FOV_REWRITE, 100.0);
  CHECK(s.wrote == 100.0);
  CHECK_MSG(fov_step(s, 0, 100.0) == FOV_RESTORE, "switched off: the stock string goes back");
  fov_done(&s, FOV_RESTORE, 0.0);
  CHECK(s.applied == 0);
  CHECK(fov_step(s, 0, 100.0) == FOV_NONE);
  config_defaults();
  CHECK_MSG(g_cfg.fovOn == 0 && g_cfg.fovDeg == FOV_STOCK, "Custom field of view off, slider at stock");
}

// Scroll-wheel zoom (quick-wins.md section 3).
static void tb_0176_wz_notch_rule() {
  int res = 0;
  CHECK_MSG(wz_take_notches(&res, 120) == 1 && res == 0, "one WHEEL_DELTA is one notch");
  CHECK_MSG(wz_take_notches(&res, -240) == -2 && res == 0, "two notches toward you");
  CHECK_MSG(wz_take_notches(&res, 60) == 0 && res == 60, "a half notch waits");
  CHECK_MSG(wz_take_notches(&res, 60) == 1 && res == 0, "and completes with the next half");
  CHECK_MSG(wz_take_notches(&res, -30) == 0 && res == -30, "a negative remainder is kept too");
  CHECK_MSG(wz_distance_after(8, 1) == 7, "away from you pulls the camera closer, 1 m a notch");
  CHECK_MSG(wz_distance_after(8, -3) == 11, "toward you pushes it back");
  CHECK_MSG(wz_distance_after(ZOOMOUT_MIN_M, 5) == ZOOMOUT_MIN_M, "clamped at the slider's minimum");
  CHECK_MSG(wz_distance_after(ZOOMOUT_MAX_M - 1, -5) == ZOOMOUT_MAX_M, "clamped at the slider's maximum");
  CHECK_MSG(wz_distance_after(0, 0) == ZOOMOUT_MIN_M, "an out-of-range stored value lands in range");
}

static void tb_0177_wz_smooth_step() {
  CHECK_MSG(wz_smooth_step(8.f, 12.f, 0.016f, 0) == 12.f, "Smooth scrolling off: at the target at once");
  const float a = wz_smooth_step(8.f, 12.f, 0.016f, 1);
  CHECK_MSG(a > 8.f && a < 12.f, "on: part of the way");
  const float b = wz_smooth_step(a, 12.f, 0.016f, 1);
  CHECK_MSG(b > a && b < 12.f, "further on the next frame, never past");
  CHECK_MSG(wz_smooth_step(12.f, 8.f, 0.016f, 1) < 12.f, "eases down as well as up");
  float c = 8.f;
  for (int i = 0; i < 60; ++i) c = wz_smooth_step(c, 12.f, 0.016f, 1);
  CHECK_MSG(c == 12.f, "settles on the target within a second at 60 fps");
  CHECK_MSG(wz_smooth_step(8.f, 12.f, 0.f, 1) == 8.f, "no time, no step");
  CHECK_MSG(wz_smooth_step(8.f, 12.f, 10.f, 1) == 12.f, "a stall lands on the target");
  CHECK_MSG(wz_smooth_step(11.995f, 12.f, 0.001f, 1) == 12.f, "snaps inside 0.01 m");
}

static void tb_0178_wz_source_and_config() {
  CHECK_MSG(wz_pick(1, 240, 120, 0) == 240, "the device read wins when it worked");
  CHECK_MSG(wz_pick(0, 0, 120, 0) == 120, "the messages are the fallback");
  CHECK(wz_pick(0, 0, 0, 0) == 0);
  config_defaults();
  CHECK_MSG(g_cfg.wheelZoom == 0 && g_cfg.wheelSmooth == 1, "Scroll-wheel zoom off, Smooth scrolling on");
  // input.dll mouse init 0x100011ee: the keyboard's shape with the mouse's EXCLUSIVE|FOREGROUND 0x05.
  unsigned char code[64];
  memset(code, 0x90, sizeof code);
  memcpy(code + 7, kHkMouseSig, HK_SIG_LEN);
  const unsigned char slot[4] = { 0xEC, 0x99, 0x00, 0x10 };
  memcpy(code + 7 + HK_SIG_SLOT, slot, 4);
  size_t at = 0;
  CHECK(offs_find(code, sizeof code, kHkMouseSig, kHkKbdMask, HK_SIG_LEN, &at) == 1 && at == 7);
  CHECK_MSG(hk_sig_slot(code + at) == 0x100099ecu, "the imm32 after A1 is the mouse device global");
  CHECK_MSG(kHkMouseSig[HK_SIG_LEVEL] == 0x05, "the mouse's level push");
  CHECK_MSG(offs_find(code, sizeof code, kHkKbdSig, kHkKbdMask, HK_SIG_LEN, &at) == 0, "never the keyboard's");
  CHECK_MSG(WZ_DIMOUSESTATE_SIZE == 16 && WZ_LZ_OFFSET == 8, "c_dfDIMouse at input!0x10006484: 16 bytes, lZ at 8");
}

// Freecam extras (quick-wins.md section 4). The copy/paste line carries our own tag, so a
// Build-game line (Megaton's "cam x= ... ang= pitch=") is refused rather than flown to.
static void tb_0179_campos_round_trip_and_refusal() {
  FreecamPose p = { { 1234.5f, -56.25f, 7890.f }, 135.5f, -20.25f, -1.f };
  char line[128];
  CHECK(campos_format(p, line, sizeof line));
  CHECK_MSG(strncmp(line, "dnmp cam x=", 11) == 0, "the line opens with the tag");
  FreecamPose q = { { 0.f, 0.f, 0.f }, 0.f, 0.f, 1.f };
  CHECK(campos_accept(line, &q));
  CHECK_MSG(fabsf(q.eye[0] - 1234.5f) < 0.05f && fabsf(q.eye[1] + 56.25f) < 0.05f && fabsf(q.eye[2] - 7890.f) < 0.05f,
            "the eye round-trips to the tenth of a unit");
  CHECK_MSG(fabsf(q.yaw - 135.5f) < 0.01f && fabsf(q.pitch + 20.25f) < 0.01f, "yaw and pitch round-trip");
  CHECK_MSG(q.hand == 1.f, "paste keeps the camera's own handedness");
  CHECK_MSG(campos_accept("copied: dnmp cam x=1 y=2 z=3 yaw=4 pitch=5 and more", &q) && q.eye[2] == 3.f,
            "found anywhere in the clipboard text");
  const FreecamPose keep = q;
  CHECK_MSG(!campos_accept("cam x=1 y=2 z=3 ang=4 pitch=5", &q), "Megaton's untagged line is refused");
  CHECK_MSG(!campos_accept("dnmp cam x=1 y=2 z=3 yaw=4", &q), "a field missing");
  CHECK_MSG(!campos_accept("dnmp cam x=1 y=two z=3 yaw=4 pitch=5", &q), "a field that is not a number");
  CHECK_MSG(!campos_accept("dnmp cam x=1 y=2 z=3 yaw=4 pitch=90", &q), "pitch past the freecam's 89");
  CHECK_MSG(!campos_accept("dnmp cam x=1 y=2 z=3 yaw=400 pitch=5", &q), "yaw past a turn");
  CHECK_MSG(!campos_accept("dnmp cam x=1e9 y=2 z=3 yaw=4 pitch=5", &q), "an eye past the far plane");
  CHECK_MSG(!campos_accept("dnmp cam x=nan y=2 z=3 yaw=4 pitch=5", &q), "not finite");
  CHECK(!campos_accept("", &q));
  CHECK(!campos_accept(0, &q));
  CHECK_MSG(memcmp(&q, &keep, sizeof q) == 0, "a refused line leaves the pose alone");
  CHECK_MSG(campos_accept("dnmp cam x=0 y=0 z=0 yaw=-190 pitch=0", &q) && fabsf(q.yaw - 170.f) < 0.01f,
            "yaw is normalised into the pose's range");
  char tiny[8];
  CHECK_MSG(!campos_format(p, tiny, sizeof tiny) && tiny[0] == 0, "half a line is never written");
}

static void tb_0180_freecam_readout() {
  FreecamPose p = { { 100.4f, -2.6f, 3000.f }, -45.26f, 10.f, 1.f };
  char line[96];
  freecam_readout(p, line, sizeof line);
  CHECK_MSG(strcmp(line, "cam x=100 y=-3 z=3000 yaw=-45.3 pitch=10.0") == 0, line);
  config_defaults();
  CHECK_MSG(g_cfg.freecamDebug == 0 && g_cfg.freecamShot == 0, "Debug readout and Screenshot mode default off");
  CHECK_MSG(config_bind_get(BIND_ROW_CAMCOPY) == 0x42 && config_bind_get(BIND_ROW_CAMPASTE) == 0x4E,
            "Copy view B, Paste view N (V is the Camera key)");
  CHECK_MSG(strcmp(bind_label(0x42), "B") == 0 && strcmp(bind_label(0x4E), "N") == 0, "both keys have names");
  CHECK_MSG(g_cfg.hudStash == -1, "no g_hud stash");
}

static void tb_0181_shot_hud_machine() {
  CHECK(shot_active(1, 1));
  CHECK_MSG(!shot_active(1, 0) && !shot_active(0, 1), "flying and switched on, both");
  // The whole HUD screen (game_view) is hidden, as the letterbox hides it.
  CHECK_MSG(shot_hud_act(1, 0, 1, 0) == SHOT_HIDE, "on, the HUD showing: hide it");
  CHECK_MSG(shot_hud_act(1, 1, 1, 0) == SHOT_HIDE, "the game showed it again (a new level): hide again");
  CHECK_MSG(shot_hud_act(1, 1, 0, 0) == SHOT_NONE, "held hidden: nothing every frame");
  CHECK_MSG(shot_hud_act(0, 1, 0, 0) == SHOT_RESTORE, "off (switch, freecam end): shown back");
  CHECK_MSG(shot_hud_act(0, 1, 0, 1) == SHOT_FORGET, "a cutscene's letterbox is up: it shows the HUD back itself");
  CHECK_MSG(shot_hud_act(0, 0, 1, 0) == SHOT_NONE, "never hidden by us: never shown by us");
  CHECK(shot_hud_act(1, 0, 0, 0) == SHOT_NONE);
  CHECK_MSG(shot_recover(1, 0) == 1, "crashed while hidden: the HUD comes back at the next launch");
  CHECK_MSG(shot_recover(2, 0) == 2, "the player's own 2 comes back, not a 1");
  CHECK_MSG(shot_recover(1, 1) == -1, "the HUD already showing: nothing to do");
  CHECK_MSG(shot_recover(0, 0) == -1, "the player had it hidden: left hidden");
  CHECK_MSG(shot_recover(-1, 0) == -1, "no stash: the player's own 0 is left alone");
}

// Slider typing (AA's entry.h, keyed by VK here) and "Also bound in the game".
static void tb_0182_entry_vk_events() {
  int ch = 0;
  CHECK(entry_char_of_vk(0x30) == '0' && entry_char_of_vk(0x39) == '9');
  CHECK_MSG(entry_char_of_vk(0x60) == '0' && entry_char_of_vk(0x69) == '9', "the keypad digits");
  CHECK_MSG(entry_char_of_vk(0xBD) == '-' && entry_char_of_vk(0x6D) == '-', "both minus keys");
  CHECK_MSG(entry_char_of_vk(0x41) == 0 && entry_char_of_vk(0x20) == 0, "letters and space are not typed");
  CHECK(entry_event_of_vk(0x35, false, false, &ch) == ENTRY_EV_CHAR && ch == '5');
  CHECK(entry_event_of_vk(0x0D, false, false, &ch) == ENTRY_EV_ENTER);
  CHECK(entry_event_of_vk(0x1B, false, false, &ch) == ENTRY_EV_ESCAPE);
  CHECK(entry_event_of_vk(0x08, false, false, &ch) == ENTRY_EV_BACKSPACE);
  CHECK(entry_event_of_vk(0x2E, false, false, &ch) == ENTRY_EV_DELETE);
  CHECK_MSG(entry_event_of_vk(0x25, true, false, &ch) == ENTRY_EV_LEFT && ch == 1, "Shift+Left extends");
  CHECK(entry_event_of_vk(0x23, false, false, &ch) == ENTRY_EV_END && ch == 0);
  CHECK(entry_event_of_vk(ENTRY_VK_A, false, true, &ch) == ENTRY_EV_SELECT_ALL);
  CHECK(entry_event_of_vk(ENTRY_VK_C, false, true, &ch) == ENTRY_EV_COPY);
  CHECK(entry_event_of_vk(ENTRY_VK_X, false, true, &ch) == ENTRY_EV_CUT);
  CHECK(entry_event_of_vk(ENTRY_VK_V, false, true, &ch) == ENTRY_EV_PASTE);
  CHECK_MSG(entry_event_of_vk(0x35, false, true, &ch) == -1, "Ctrl+5 is not a digit");
  CHECK_MSG(entry_event_of_vk(0x41, false, false, &ch) == -1, "a key the field does not take");
  CHECK_MSG(strcmp(MENU_GAMECFG_NOTE, "Also bound in the game") == 0, "AA's note text");
}

static EntryState entry_type(EntryState s, const char* keys) {
  for (int i = 0; keys[i]; i++) s = entry_step(s, ENTRY_EV_CHAR, keys[i], 0, EK_NONE, 0, 0, 0).state;
  return s;
}

static void tb_0183_entry_commit_and_clamp() {
  EntryState s = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 4007, EK_INT, 1, 40, "8").state;
  CHECK(s.open == 1 && s.rowKey == 4007 && strcmp(s.text, "8") == 0 && s.caret == 1);
  EntryResult r = entry_step(s, ENTRY_EV_ENTER, 0, 0, EK_NONE, 0, 0, 0);
  CHECK_MSG(r.commit == 0 && r.state.open == 0, "Enter on the untouched prefill closes without a commit");
  s = entry_step(s, ENTRY_EV_BACKSPACE, 0, 0, EK_NONE, 0, 0, 0).state;
  s = entry_type(s, "99");
  r = entry_step(s, ENTRY_EV_ENTER, 0, 0, EK_NONE, 0, 0, 0);
  CHECK_MSG(r.commit == 1 && r.value == 40, "99 commits clamped to the row's 40");
  s = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 1, EK_INT, 1, 40, "8").state;
  s = entry_type(entry_step(s, ENTRY_EV_SELECT_ALL, 0, 0, EK_NONE, 0, 0, 0).state, "0");
  r = entry_step(s, ENTRY_EV_ENTER, 0, 0, EK_NONE, 0, 0, 0);
  CHECK_MSG(r.commit == 1 && r.value == 1, "0 commits clamped up to 1");
  s = entry_type(entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 1, EK_INT, 1, 40, "8").state, "5");
  r = entry_step(s, ENTRY_EV_ESCAPE, 0, 0, EK_NONE, 0, 0, 0);
  CHECK_MSG(r.commit == 0 && r.state.open == 0, "Escape cancels");
  s = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 1, EK_INT, 1, 40, "8").state;
  s = entry_step(s, ENTRY_EV_BACKSPACE, 0, 0, EK_NONE, 0, 0, 0).state;
  r = entry_step(s, ENTRY_EV_ENTER, 0, 0, EK_NONE, 0, 0, 0);
  CHECK_MSG(r.commit == 0 && r.state.open == 0, "an emptied field closes and commits nothing");
  s = entry_type(entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 1, EK_INT, 1, 40, "").state, "-3");
  CHECK_MSG(strcmp(s.text, "3") == 0, "no minus on a row that cannot go below zero");
  s = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 1, EK_INT, 1, 40, "").state;
  s = entry_step(s, ENTRY_EV_PASTE, 0, 0, EK_NONE, 0, 0, "FOV: 12").state;
  r = entry_step(s, ENTRY_EV_CLICKAWAY, 0, 0, EK_NONE, 0, 0, 0);
  CHECK_MSG(r.commit == 1 && r.value == 12, "a pasted \"FOV: 12\" delivers 12, and clicking away commits");
  CHECK_MSG(entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 1, EK_NONE, 0, 0, "x").state.open == 0,
            "a row with no number opens nothing");
  bool ok = true;
  CHECK(entry_parse("", EK_INT, 0, 10, &ok) == 0 && !ok);
}

static void tb_0184_gb_vk_to_trigger() {
  CHECK_MSG(gb_trigger_for_vk(0x57, 0x11) == 17, "W is DIK_W 0x11");
  CHECK_MSG(gb_trigger_for_vk(0x25, 0xE04B) == 0xCB, "Left arrow: E0 4B is DIK_LEFT 0xCB");
  CHECK_MSG(gb_trigger_for_vk(0xA3, 0xE01D) == 0x9D, "Right Ctrl: DIK_RCONTROL");
  CHECK_MSG(gb_trigger_for_vk(0x10, 0x2A) == 42, "generic Shift is the left one, trigger 42 lshift");
  CHECK_MSG(gb_trigger_for_vk(0x13, 0x45) == 0xC5, "VK_PAUSE is DIK_PAUSE");
  CHECK_MSG(gb_trigger_for_vk(0x90, 0xE045) == 0x45, "VK_NUMLOCK is DIK_NUMLOCK");
  CHECK_MSG(gb_trigger_for_vk(0x02, 0) == 263 && gb_trigger_for_vk(0x04, 0) == 264, "right, middle: mouse2, mouse3");
  CHECK(gb_trigger_for_vk(0x05, 0) == 265 && gb_trigger_for_vk(0x06, 0) == 266);
  CHECK_MSG(gb_trigger_for_vk(0x41, 0) == -1, "no scan code: no trigger");
  CHECK_MSG(gb_trigger_for_vk(0x41, 0xE11D) == -1, "an E1 prefix is no DIK");
  CHECK(gb_trigger_for_vk(0, 0x11) == -1);
  CHECK_MSG(gb_trigger_for_vk(0x01, 0) == -1, "mouse 1 is never a bind code of ours");
  const unsigned none[GB_IMPULSE_SLOTS] = { 0, 0, 0, 0 }, one[GB_IMPULSE_SLOTS] = { 0, 0, 0x480e00u, 0 };
  CHECK(!gb_trigger_bound(false, none));
  CHECK_MSG(gb_trigger_bound(true, none), "a command bind");
  CHECK_MSG(gb_trigger_bound(false, one), "an impulse in any slot");
  CHECK_MSG(GB_TRIGGERS * GB_TRIGGER_SIZE == 0x2ed40, "inp_trigger_name's loop bound, 0x407144");
}

// Quick-wins fixes.
static void tb_0185_qw_review_rules() {
  FovState s = { 1, 90.0 };
  CHECK_MSG(fov_needs_reassert(s, 1, 1, 70.0), "a console uset under an applied value is written over");
  CHECK_MSG(!fov_needs_reassert(s, 1, 1, 90.0004), "our own value, inside the tolerance");
  CHECK_MSG(!fov_needs_reassert(s, 0, 1, 70.0), "switch off: the restore edge owns it");
  CHECK_MSG(!fov_needs_reassert(s, 1, 0, 70.0), "an unread cvar is not a change");
  FovState off = { 0, 0.0 };
  CHECK_MSG(!fov_needs_reassert(off, 1, 1, 70.0), "not applied yet: APPLY captures first");
  CHECK_MSG(fov_cfg_from_slider(FOV_STOCK) == FOV_STOCK, "the reset's 0 is kept");
  CHECK_MSG(fov_cfg_from_slider(10) == FOV_MIN_DEG && fov_cfg_from_slider(999) == FOV_MAX_DEG, "saved in range");
  CHECK(fov_cfg_from_slider(95) == 95);
  CHECK_MSG(wz_pick(1, 360, 120, 1) == 0 && wz_pick(0, 0, 120, 1) == 0, "the live edge's read is thrown away");
  CHECK(wz_pick(1, 360, 120, 0) == 360);
  CHECK_MSG(shot_stash_of("1") == 1 && shot_stash_of("2") == 2 && shot_stash_of("0") == 0, "the plain values");
  CHECK_MSG(shot_stash_of("abc") == 0, "atoi's 0, as the HUD handler reads it: hidden, nothing to recover");
  CHECK_MSG(shot_stash_of("-1") == SHOT_STASH_ODD && shot_stash_of("70000") == SHOT_STASH_ODD, "a shown HUD outside the ini range");
  CHECK_MSG(shot_recover(SHOT_STASH_ODD, 0) == 1, "an odd stock comes back as a shown HUD, never lost");
  config_defaults();
  CHECK_MSG(shot_stash_of("9999") == 9999, "the largest plain stash");
}

// The FOV readout fits the reserve every other slider row has, so its track is
// not squeezed (the old "150.0 (stock)" reserve shrank it to TRACK_MIN_W).
static void tb_0186_fov_readout_fits() {
  char b[16];
  fov_value_text(FOV_STOCK, 79.7553, 1, b, sizeof b);
  CHECK_MSG(strcmp(b, "79.8") == 0, b);
  fov_value_text(FOV_STOCK, 106.6, 1, b, sizeof b);
  CHECK_MSG(strcmp(b, "107") == 0, "a stock of 100 or more drops the decimal");
  fov_value_text(101, 79.7553, 1, b, sizeof b);
  CHECK(strcmp(b, "101") == 0);
  fov_value_text(FOV_STOCK, 0.0, 0, b, sizeof b);
  CHECK_MSG(strcmp(b, "--") == 0, "no stock read yet");
  bool fits = true;
  for (int v = FOV_MIN_DEG; v <= FOV_MAX_DEG; v += 7) {
    fov_value_text(v, 0.0, 1, b, sizeof b);
    if (strlen(b) > strlen(FOV_READOUT_RESERVE)) fits = false;
    fov_value_text(FOV_STOCK, (double)v + 0.49, 1, b, sizeof b);
    if (strlen(b) > strlen(FOV_READOUT_RESERVE)) fits = false;
  }
  CHECK_MSG(fits, "every readout fits the 4-character reserve");
  CHECK_MSG(strlen(FOV_READOUT_RESERVE) == strlen("40 m"), "the Distance row's own reserve width");
}

// The wheel engages the zoom-out view with the Zoom out switch off, and a notch
// closer at the Distance slider's minimum lets it go (the rule is in wheelzoom.h).
static void tb_0187_wz_engage_rule() {
  // The wheel has its own distance, so the Zoom out switch no longer matters here.
  CHECK_MSG(wz_engaged_after(0, 1, -1, 8) == 1, "a notch toward you engages it");
  CHECK_MSG(wz_engaged_after(0, 1, 1, 8) == 0, "a notch away with the camera already normal: nothing");
  CHECK_MSG(wz_engaged_after(1, 1, 1, 8) == 1, "engaged: closer is still zoomed out above the minimum");
  CHECK_MSG(wz_engaged_after(1, 1, 1, ZOOMOUT_MIN_M + 1) == 1, "down to the minimum stays engaged");
  CHECK_MSG(wz_engaged_after(1, 1, 1, ZOOMOUT_MIN_M) == 0, "one notch closer at the minimum: normal camera");
  CHECK_MSG(wz_engaged_after(1, 1, 0, 8) == 1, "no notch: unchanged");
  CHECK_MSG(wz_engaged_after(1, 0, 0, 8) == 0, "Scroll-wheel zoom switched off lets it go");
  CHECK_MSG(ZOOMOUT_MIN_M >= 1, "the minimum is above 0, so below it is the only way back");
}

// The wheel's own distance, apart from the Distance slider.
static void tb_0188_wz_own_distance() {
  CHECK_MSG(wz_view_target(1, 12, 1, 5) == 12.f, "engaged: the wheel's own distance, whatever the switch says");
  CHECK_MSG(wz_view_target(0, 12, 1, 5) == 5.f, "released with Zoom out on: the slider's saved Distance");
  CHECK_MSG(wz_view_target(0, 12, 0, 5) == 0.f, "released with Zoom out off: the normal camera");
  CHECK_MSG(WZ_WHEEL_START_M == ZOOMOUT_DEF_M, "each launch starts at the slider's default, never saved");
}

// Which barrier boxes the eye-to-target segment crosses.
static void tb_0189_aim_seg_box() {
  const Vec3 c = { 100.f, 0.f, 0.f }, h = { 5.f, 50.f, 50.f };
  float t = -1.f;
  CHECK_MSG(aim_seg_box_hit(Vec3{ 0.f, 0.f, 0.f }, Vec3{ 200.f, 0.f, 0.f }, c, h, &t) && fabsf(t - 0.475f) < 1e-4f,
            "through the doorway slab, entering at x=95");
  CHECK_MSG(!aim_seg_box_hit(Vec3{ 0.f, 0.f, 0.f }, Vec3{ 90.f, 0.f, 0.f }, c, h, &t), "the target short of it");
  CHECK_MSG(!aim_seg_box_hit(Vec3{ 0.f, 80.f, 0.f }, Vec3{ 200.f, 80.f, 0.f }, c, h, &t), "beside it");
  CHECK_MSG(aim_seg_box_hit(Vec3{ 100.f, 0.f, 0.f }, Vec3{ 300.f, 0.f, 0.f }, c, h, &t) && t == 0.f, "starting inside");
  CHECK_MSG(!aim_seg_box_hit(Vec3{ 0.f, 60.f, 0.f }, Vec3{ 0.f, 60.f, 0.f }, c, h, &t), "a point outside");
}

// Item 3 (instrumentation): a cursor clip narrower than the whole virtual desktop is the stuck-mouse suspect.
static void tb_0190_clip_restricted() {
  const RECT desk = { -1920, 0, 3440, 1440 };
  CHECK(!clip_is_restricted(desk, desk));
  const RECT dinput = { 1700, 700, 1701, 701 };
  CHECK_MSG(clip_is_restricted(dinput, desk), "dinput8's one-pixel clip (dinput8!0x10012485)");
  const RECT wider = { -2000, -10, 3500, 1500 };
  CHECK_MSG(!clip_is_restricted(wider, desk), "a clip covering the desktop is no clip");
}

// The doorway barrier is a `blocker` with neither creatures_only (+0xB0A, user
// 0x10000) nor player_only (+0xB0B, user 0x20000) -- world.scr's blocker2 "invisible:false" -- and it stopped
// Duke's bullets (log: no hit), while a creatures_only one passed them (real shot #4).
static void tb_0191_aim_blocker_kinds() {
  AimObsFacts f;
  memset(&f, 0, sizeof f);
  f.type = 0x200101; f.user = 0x8000; f.objClass = AIM_OBJ_BLOCKER; f.handler = AIM_H_NOOP; f.blockerActive = 1;
  int k = aim_obstacle_kind(f);
  CHECK_MSG(k == AIM_OBS_BLOCK && aim_obstacle_cuts(f, k), "a blocker for everyone stops the bullet, as a wall");
  f.blockerActive = 0;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_PASS, "a lowered one (+0x2E0 clear) lets it through");
  f.blockerActive = 1; f.user = 0x8000 | AIM_USER_BLOCK_CREATURES;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_PASS, "creatures_only: the railing kind, bullets pass (measured)");
  f.user = 0x8000 | AIM_USER_BLOCK_PLAYER;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_PASS, "player_only: blocks Duke, not his bullets (unmeasured)");
}

static void tb_0173_si_config_default_off() {
  config_defaults();
  CHECK_MSG(g_cfg.skipIntro == 0, "Skip intro defaults off, as AA");
}

// cutaway.md section 4: force only a fader above min_trans that is not already fading out.
static void tb_0192_cut_force_decision() {
  CHECK(cut_decide(1.0f, 30, 0, 0) == CUT_FORCE);     // idle at max
  CHECK(cut_decide(1.0f, 30, 1, 0) == CUT_FORCE);     // fading back in
  CHECK(cut_decide(1.0f, 30, 2, 0) == CUT_KEEP);      // already fading out
  CHECK(cut_decide(0.6f, 30, 2, 1) == CUT_KEEP);
  CHECK(cut_decide(cut_min(30), 30, 0, 0) == CUT_KEEP);   // at min: the value the handler clamps to
  CHECK(cut_decide(cut_min(30), 30, 1, 1) == CUT_KEEP);   // not above min yet
  CHECK(cut_decide(0.31f, 30, 1, 1) == CUT_REASSERT); // the engine faded a forced wall back in
  CHECK(cut_decide(0.31f, 30, 0, 1) == CUT_REASSERT);
  CHECK(cut_decide(0.0f, 0, 0, 0) == CUT_KEEP);
  CHECK(cut_decide(1.0f, 100, 0, 0) == CUT_KEEP);     // min_trans 100: never above
  CHECK(cut_decide(0.05f, 0, 0, 0) == CUT_FORCE);
  CHECK(cut_min(30) == 30.f * 0.01f);
}
static void tb_0193_cut_undo_only_forced() {
  const unsigned char forced[4] = { 1, 0, 1, 1 };
  const unsigned char seen[4] = { 1, 1, 0, 1 };
  int out[4] = { -1, -1, -1, -1 };
  CHECK(cut_undo_pick(forced, seen, 4, out) == 2);
  CHECK(out[0] == 0 && out[1] == 3);                 // 1 never forced, 2 not returned by this walk
  CHECK(cut_undo_pick(forced, seen, 0, out) == 0);
}
static void tb_0194_cut_handler_sig() {
  unsigned char h[0x70];
  memset(h, 0xCC, sizeof h);
  memcpy(h + CUT_SIG_A_AT, kCutSigA, sizeof kCutSigA);
  memcpy(h + CUT_SIG_B_AT, kCutSigB, sizeof kCutSigB);
  CHECK(cut_handler_sig_ok(h) == 1);
  CHECK(cut_handler_sig_ok(0) == 0);
  h[CUT_SIG_A_AT + 26] ^= 1;                         // state value 2 -> 3
  CHECK(cut_handler_sig_ok(h) == 0);
  h[CUT_SIG_A_AT + 26] ^= 1;
  h[CUT_SIG_B_AT + sizeof kCutSigB - 4] ^= 4;        // min_trans displacement +0xB0C -> +0xB08
  CHECK(cut_handler_sig_ok(h) == 0);
  CHECK(CUT_SIG_A_AT == 9 && CUT_SIG_B_AT == 0x4D);
}
static void tb_0195_cut_config_default_off() {
  config_defaults();
  CHECK_MSG(g_cfg.cutaway == 0, "Cut-away walls defaults off");
}

static void tb_0196_cut_prune_keeps_seen() {
  CutFader f[4];
  memset(f, 0, sizeof f);
  int a = 1, b = 2, c = 3, d = 4;
  f[0].ent = &a; f[0].seen = 1; f[0].forced = 1;
  f[1].ent = &b;                                     // not returned by the walk: gone
  f[2].ent = &c; f[2].seen = 1;
  f[3].ent = &d; f[3].forced = 1;                    // forced but gone: dropped too
  CHECK(cut_prune(f, 4) == 2);
  CHECK(f[0].ent == &a && f[0].forced == 1 && f[1].ent == &c);
  CHECK(cut_prune(f, 0) == 0);
}

// Freecam wheel FOV.
static void tb_0197_fcf_wheel_fov_rules() {
  CHECK_MSG(fcf_deg_after(80.0, 1) == 78.0, "away from you narrows by 2 degrees a notch (user 2026-10-01)");
  CHECK_MSG(fcf_deg_after(80.0, -3) == 86.0, "toward you widens");
  CHECK_MSG(fcf_deg_after(79.755, 1) > 77.75 && fcf_deg_after(79.755, 1) < 77.76, "a fractional stock keeps its fraction");
  CHECK_MSG(fcf_deg_after(149.0, -5) == FOV_MAX_DEG, "clamped at 150");
  CHECK_MSG(fcf_deg_after(41.0, 5) == FOV_MIN_DEG, "clamped at 40");
  // One owner a frame: wheel > Custom FOV > stock.
  CHECK_MSG(fcf_owner(1, 1) == FCF_OWNER_WHEEL && fcf_owner(1, 0) == FCF_OWNER_WHEEL, "flying: the wheel owns, switch or not");
  CHECK_MSG(fcf_owner(0, 1) == FCF_OWNER_CUSTOM, "not flying, Custom on: Custom FOV");
  CHECK_MSG(fcf_owner(0, 0) == FCF_OWNER_STOCK, "not flying, Custom off: stock");
  // Each flight starts from the gameplay FOV.
  CHECK_MSG(fcf_gameplay_deg(1, 100, 79.755) == 100.0, "Custom on: its value");
  CHECK_MSG(fcf_gameplay_deg(1, FOV_STOCK, 79.755) == 79.755, "Custom on at the reset: the stock");
  CHECK_MSG(fcf_gameplay_deg(0, 100, 79.755) == 79.755, "Custom off: the stock");
  // The plan: what the zoom writer applies, and restore when the flight ends.
  FcfPlan f = fcf_plan(1, 0, 0, 79.755, 90.5);
  CHECK_MSG(f.on == 1 && f.want == 90.5 && !f.stockString, "flight writes the wheel value");
  FcfPlan c = fcf_plan(0, 1, 100, 79.755, 90.5);
  CHECK_MSG(c.on == 1 && c.want == 100.0 && !c.stockString, "flight over, Custom on: back to Custom");
  FcfPlan r = fcf_plan(0, 1, FOV_STOCK, 79.755, 90.5);
  CHECK_MSG(r.on == 1 && r.stockString, "Custom at the reset writes the exact stock string");
  FcfPlan o = fcf_plan(0, 0, 100, 79.755, 90.5);
  CHECK_MSG(o.on == 0, "flight over, Custom off: released, so fov_step restores the stock");
  FovState st = {0, 0.0};
  int act = fov_step(st, f.on, f.want); CHECK_MSG(act == FOV_APPLY, "flight start applies");
  fov_done(&st, act, f.want);
  act = fov_step(st, o.on, o.want); CHECK_MSG(act == FOV_RESTORE, "flight end restores");
  // The ease is the wheel zoom's step.
  CHECK_MSG(wz_smooth_step(80.f, 90.f, 0.016f, 0) == 90.f, "Smooth off: snaps");
  const float e = wz_smooth_step(80.f, 90.f, 0.016f, 1);
  CHECK_MSG(e > 80.f && e < 90.f, "Smooth on: part of the gap");
  char line[128];
  FreecamPose p = { { 1.f, 2.f, 3.f }, 10.f, 5.f, 1.f };
  freecam_readout_fov(p, 90.04, line, sizeof line);
  CHECK_MSG(strcmp(line, "cam x=1 y=2 z=3 yaw=10.0 pitch=5.0 fov=90.0") == 0, line);
}

// Mouse aim: the cursor ray, the Duke plane,
// the precedence and the facing side. The cameras come from freecam.h's matrices and proj90, not mouseaim.h.
static float ma_line_dist(Vec3 o, Vec3 d, Vec3 p) {   // distance from p to the line o + s*d, d unit
  const Vec3 w = { p.x - o.x, p.y - o.y, p.z - o.z };
  const float s = w.x * d.x + w.y * d.y + w.z * d.z;
  const Vec3 q = { w.x - s * d.x, w.y - s * d.y, w.z - s * d.z };
  return sqrtf(q.x * q.x + q.y * q.y + q.z * q.z);
}
static void ma_camera(float yaw, float pitch, float ex, float ey, float ez, Mat4* view) {
  FreecamPose p = { { ex, ey, ez }, yaw, pitch, 1.f };
  float inv[16];
  freecam_matrices(p, view->m, inv);
}
// A world point projected to pixels, back to NDC, then unprojected: the ray starts at the eye and passes
// through the point, for a level, a turned and a pitched camera at 16:9.
static void tb_0198_ma_cursor_ray_round_trips() {
  const int W = 1600, H = 900;
  const Mat4 p = proj90(16.f, 8000.f, W, H);
  const float cams[3][5] = { { 0.f, 0.f, 0.f, 50.f, -500.f }, { 90.f, 0.f, -300.f, 80.f, 20.f },
                             { 30.f, -20.f, 10.f, 400.f, -200.f } };
  const Vec3 pts[3] = { { 120.f, 90.f, 0.f }, { 0.f, 10.f, 60.f }, { 300.f, 50.f, 300.f } };
  for (int c = 0; c < 3; ++c) {
    Mat4 v;
    ma_camera(cams[c][0], cams[c][1], cams[c][2], cams[c][3], cams[c][4], &v);
    const ScreenPt sp = w2s_project(pts[c], v, p, 16.f, W, H);
    CHECK_MSG(sp.ok, "the test point is in front of the camera");
    Vec3 o, d;
    CHECK(ma_cursor_ray(ndc_x(sp.x, W), ndc_y(sp.y, H), v, p, &o, &d) == 1);
    CHECK_MSG(fabsf(o.x - cams[c][2]) < 1e-2f && fabsf(o.y - cams[c][3]) < 1e-2f && fabsf(o.z - cams[c][4]) < 1e-2f,
              "the ray starts at the eye");
    CHECK_MSG(fabsf(sqrtf(d.x * d.x + d.y * d.y + d.z * d.z) - 1.f) < 1e-4f, "unit direction");
    CHECK_MSG(ma_line_dist(o, d, pts[c]) < 0.05f, "the ray passes through the projected point");
    const Vec3 w = { pts[c].x - o.x, pts[c].y - o.y, pts[c].z - o.z };
    CHECK_MSG(w.x * d.x + w.y * d.y + w.z * d.z > 0.f, "toward the point, not away from it");
  }
  Mat4 v;
  ma_camera(0.f, 0.f, 0.f, 0.f, 0.f, &v);
  Mat4 flat = p;
  flat.m[0] = 0.f; flat.m[4] = 0.f;   // x no longer depends on the view: no ray
  Vec3 o, d;
  CHECK_MSG(ma_cursor_ray(0.2f, 0.1f, v, flat, &o, &d) == 0, "a singular projection gives no ray");
  Vec3 f;
  ma_camera(90.f, 0.f, 0.f, 0.f, 0.f, &v);
  CHECK(ma_view_forward(v, &f) == 1 && fabsf(f.x - 1.f) < 1e-4f && fabsf(f.y) < 1e-4f && fabsf(f.z) < 1e-4f);
}

// The plane is vertical, through the fire origin, along the track tangent at Duke.
static void tb_0199_ma_duke_plane_straight_and_curved() {
  const int W = 1600, H = 900;
  const Mat4 p = proj90(16.f, 8000.f, W, H);
  // Straight track along x; the camera looks down +z at it from z = -500.
  const TrkPt straight[2] = { { 0.f, 0.f, 0.f, 0.f }, { 1000.f, 0.f, 0.f, 1000.f } };
  Vec3 t;
  CHECK(ma_track_tangent(straight, 2, Vec3{ 400.f, 50.f, 3.f }, &t) == 1);
  CHECK_MSG(fabsf(fabsf(t.x) - 1.f) < 1e-5f && t.y == 0.f && fabsf(t.z) < 1e-5f, "straight: tangent along x");
  Mat4 v;
  ma_camera(0.f, 0.f, 400.f, 60.f, -500.f, &v);
  const Vec3 want = { 520.f, 130.f, 0.f };
  const ScreenPt sp = w2s_project(want, v, p, 16.f, W, H);
  Vec3 o, d, hit;
  CHECK(ma_cursor_ray(ndc_x(sp.x, W), ndc_y(sp.y, H), v, p, &o, &d) == 1);
  CHECK(ma_plane_point(o, d, Vec3{ 400.f, 50.f, 0.f }, t, &hit) == 1);
  CHECK_MSG(fabsf(hit.x - want.x) < 0.1f && fabsf(hit.y - want.y) < 0.1f && fabsf(hit.z) < 0.1f,
            "straight: the cursor's point on Duke's plane");
  // Curved: x, then a 90 degree turn to +z. Duke on the second leg; the camera looks down -x at it.
  const TrkPt curved[4] = { { 0.f, 0.f, 0.f, 0.f }, { 1000.f, 0.f, 0.f, 1000.f }, { 1000.f, 0.f, 0.f, 1000.f },
                            { 1000.f, 0.f, 1000.f, 2000.f } };   // span joints repeat a point
  CHECK(ma_track_tangent(curved, 4, Vec3{ 1003.f, 50.f, 500.f }, &t) == 1);
  CHECK_MSG(fabsf(t.x) < 1e-5f && fabsf(fabsf(t.z) - 1.f) < 1e-5f, "curved: the tangent of Duke's own leg");
  CHECK(ma_track_tangent(curved, 4, Vec3{ 500.f, 50.f, -4.f }, &t) == 1);
  CHECK_MSG(fabsf(fabsf(t.x) - 1.f) < 1e-5f, "curved: the first leg's tangent near it");
  CHECK(ma_track_tangent(curved, 4, Vec3{ 1003.f, 50.f, 500.f }, &t) == 1);
  ma_camera(-90.f, 0.f, 1500.f, 60.f, 500.f, &v);
  const Vec3 want2 = { 1000.f, 90.f, 620.f };
  const ScreenPt s2 = w2s_project(want2, v, p, 16.f, W, H);
  CHECK(s2.ok && ma_cursor_ray(ndc_x(s2.x, W), ndc_y(s2.y, H), v, p, &o, &d) == 1);
  CHECK(ma_plane_point(o, d, Vec3{ 1000.f, 50.f, 500.f }, t, &hit) == 1);
  CHECK_MSG(fabsf(hit.x - 1000.f) < 0.1f && fabsf(hit.y - 90.f) < 0.1f && fabsf(hit.z - 620.f) < 0.1f,
            "curved: the point lies on the plane of Duke's leg");
  // No crossing: a ray along the plane, or one pointing away from it.
  CHECK(ma_plane_point(Vec3{ 0.f, 0.f, -10.f }, Vec3{ 1.f, 0.f, 0.f }, Vec3{ 0.f, 0.f, 0.f }, Vec3{ 1.f, 0.f, 0.f }, &hit) == 0);
  CHECK(ma_plane_point(Vec3{ 0.f, 0.f, -10.f }, Vec3{ 0.f, 0.f, -1.f }, Vec3{ 0.f, 0.f, 0.f }, Vec3{ 1.f, 0.f, 0.f }, &hit) == 0);
  CHECK_MSG(ma_track_tangent(straight, 1, Vec3{ 0.f, 0.f, 0.f }, &t) == 0, "one point is no track");
  const TrkPt still[2] = { { 5.f, 0.f, 5.f, 0.f }, { 5.f, 0.f, 5.f, 0.f } };
  CHECK_MSG(ma_track_tangent(still, 2, Vec3{ 0.f, 0.f, 0.f }, &t) == 0, "zero-length spans give no tangent");
  // No track: the plane faces the camera, its tangent the horizontal right of the forward.
  CHECK(ma_tangent_from_forward(Vec3{ 0.f, -0.5f, 2.f }, &t) == 1 && fabsf(fabsf(t.x) - 1.f) < 1e-5f && fabsf(t.z) < 1e-5f);
  CHECK_MSG(ma_tangent_from_forward(Vec3{ 0.f, 1.f, 0.f }, &t) == 0, "straight up has no horizontal forward");
}

// Silent aim's lock wins, then the cursor's point, then the stock direction.
static void tb_0200_ma_fire_precedence() {
  CHECK(ma_fire_source(1, 1, 1) == MA_SRC_SILENT);
  CHECK(ma_fire_source(1, 0, 0) == MA_SRC_SILENT);
  CHECK(ma_fire_source(0, 1, 1) == MA_SRC_MOUSE);
  CHECK_MSG(ma_fire_source(0, 1, 0) == MA_SRC_STOCK, "mouse aim with no point this frame: stock");
  CHECK_MSG(ma_fire_source(0, 0, 1) == MA_SRC_STOCK, "a point with mouse aim off: stock");
  CHECK(ma_fire_source(0, 0, 0) == MA_SRC_STOCK);
}

// Facing follows the cursor's side of Duke on screen, with a dead zone that keeps the last side.
static void tb_0201_ma_face_side_and_turn() {
  CHECK(ma_face_side(500.f, 400.f, MA_FACE_DEAD_PX, 0) == 1);
  CHECK(ma_face_side(300.f, 400.f, MA_FACE_DEAD_PX, 1) == -1);
  CHECK_MSG(ma_face_side(410.f, 400.f, MA_FACE_DEAD_PX, -1) == -1, "inside the dead zone the side holds");
  CHECK_MSG(ma_face_side(390.f, 400.f, MA_FACE_DEAD_PX, 1) == 1, "both ways");
  CHECK_MSG(ma_face_side(400.f, 400.f, MA_FACE_DEAD_PX, 0) == 0, "no side yet");
  CHECK(ma_face_side(400.f + MA_FACE_DEAD_PX + 1.f, 400.f, MA_FACE_DEAD_PX, -1) == 1);
  CHECK(ma_face_needs_turn(1, -5.f) == 1 && ma_face_needs_turn(-1, 5.f) == 1);
  CHECK(ma_face_needs_turn(1, 5.f) == 0 && ma_face_needs_turn(-1, -5.f) == 0);
  CHECK_MSG(ma_face_needs_turn(0, -5.f) == 0, "no side wanted");
  CHECK_MSG(ma_face_needs_turn(1, 0.f) == 0, "facing unknown (straight at the camera): no press");
  // One press, watched: moved with no key held refuses the row; a clean miss refuses after four in a row.
  int run = 0;
  CHECK(ma_face_verdict(0, 0.5f, 1, 1) == MA_FACE_INPLACE);
  CHECK(ma_face_verdict(0, MA_FACE_MOVE_EPS + 1.f, 1, 1) == MA_FACE_MOVED);
  CHECK_MSG(ma_face_verdict(1, 50.f, -1, 1) == MA_FACE_UNCLEAN, "a key held: the press proves nothing");
  CHECK(ma_face_verdict(0, 0.f, -1, 1) == MA_FACE_NOTURN);
  CHECK(ma_face_refuse(MA_FACE_MOVED, &run) == 1);
  run = 0;
  CHECK(ma_face_refuse(MA_FACE_NOTURN, &run) == 0);   // the run is MA_FACE_NOTURN_MAX (2, user decision 2026-10-02)
  CHECK_MSG(ma_face_refuse(MA_FACE_UNCLEAN, &run) == 0 && run == 1, "an unclean trial neither counts nor resets");
  CHECK(ma_face_refuse(MA_FACE_INPLACE, &run) == 0 && run == 0);
  for (int i = 0; i < MA_FACE_NOTURN_MAX - 1; ++i) CHECK(ma_face_refuse(MA_FACE_NOTURN, &run) == 0);
  CHECK(ma_face_refuse(MA_FACE_NOTURN, &run) == 1);
}

static void tb_0202_ma_config_defaults() {
  config_defaults();
  CHECK_MSG(g_cfg.mouseAim == 0, "Mouse aim defaults off");
  CHECK_MSG(g_cfg.mouseReticle == 1, "Show reticle defaults on");
}

// Torso bend: the angle toward the aim point, its clamp and the stock ease; the detour's fingerprint.
static void tb_0203_tb_bend_target_and_clamp() {
  int clamped = -1;
  const float up = tb_bend_target(Vec3{ 0.f, 0.f, 0.f }, Vec3{ 100.f, 100.f * tanf(20.f * 3.14159265f / 180.f), 0.f },
                                  &clamped);
  CHECK_MSG(fabsf(up - 20.f) < 1e-3f && clamped == 0, "20 degrees up, positive (the stock look-up is +22)");
  const float down = tb_bend_target(Vec3{ 5.f, 50.f, 5.f }, Vec3{ 5.f, 45.f, -5.f }, &clamped);
  CHECK_MSG(fabsf(down + 26.56505f) < 1e-3f && clamped == 0, "down is negative; the horizontal distance is in XZ whatever the side");
  CHECK_MSG(tb_bend_target(Vec3{ 0.f, 0.f, 0.f }, Vec3{ 10.f, -10.f, 0.f }, &clamped) == TB_BEND_MIN && clamped == 1,
            "clamped at the bottom");
  CHECK_MSG(tb_bend_target(Vec3{ 0.f, 0.f, 0.f }, Vec3{ 0.f, 30.f, 0.f }, &clamped) == TB_BEND_MAX && clamped == 1,
            "straight up clamps");
  CHECK(tb_bend_target(Vec3{ 0.f, 0.f, 0.f }, Vec3{ 0.f, 0.f, 0.f }, &clamped) == 0.f && clamped == 0);
}
static void tb_0204_tb_bend_step_stock_rates() {
  CHECK_MSG(fabsf(tb_bend_step(0.f, 30.f, 0.1f) - 23.f) < 1e-4f, "up at 230 degrees a second (duke!0x10136d80)");
  CHECK_MSG(tb_bend_step(20.f, 22.f, 0.1f) == 22.f, "never past the target");
  CHECK_MSG(fabsf(tb_bend_step(20.f, 0.f, 0.1f) - 10.f) < 1e-4f, "down at 100 degrees a second (0x10126a78)");
  CHECK_MSG(fabsf(tb_bend_step(0.f, -30.f, 0.1f) + 10.f) < 1e-4f, "below zero at the down rate too");
  CHECK_MSG(tb_bend_step(-5.f, -30.f, 1.f) == -30.f, "and stops at the target");
  CHECK(tb_bend_step(-30.f, 10.f, 0.05f) > -30.f);
  CHECK_MSG(tb_bend_step(7.f, 7.f, 0.1f) == 7.f, "at the target it stays");
  CHECK_MSG(tb_bend_step(7.f, 20.f, -1.f) == 7.f && tb_bend_step(7.f, 20.f, 0.f) == 7.f, "no time, no step");
}
static void tb_0205_tb_spine_fingerprint() {
  // player_spine_bend duke!0x1007c7f0, read in Ghidra 2026-10-02, through `cmp eax,3; jnz` at +0x58.
  unsigned char b[TB_FP_LEN] = {
    0x83, 0xEC, 0x10, 0x56, 0x8B, 0xF1, 0x8B, 0x86, 0xE0, 0x00, 0x00, 0x00, 0xDD, 0x40, 0x38, 0x8B, 0x86, 0x10, 0x0C,
    0x00, 0x00, 0x83, 0xF8, 0x03, 0xD9, 0x5C, 0x24, 0x04, 0xD9, 0x05, 0xBC, 0x6C, 0x12, 0x10, 0xBA, 0x02, 0x00, 0x00,
    0x00, 0x75, 0x46, 0x8B, 0x8E, 0xF0, 0x0D, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x25, 0x84, 0x91, 0x08, 0x0B, 0x00, 0x00,
    0x74, 0x1D, 0x8B, 0x8E, 0x1C, 0x11, 0x00, 0x00, 0xDD, 0xD8, 0x66, 0x83, 0x79, 0x02, 0x06, 0x74, 0x08, 0xD9, 0x05,
    0x88, 0x6D, 0x13, 0x10, 0xEB, 0x06, 0xD9, 0x05, 0x84, 0x6D, 0x13, 0x10, 0x83, 0xF8, 0x03, 0x75, 0x12 };
  size_t at = 99;
  CHECK(offs_find(b, sizeof b, kTbSpineFp, kTbSpineMask, TB_FP_LEN, &at) == 1 && at == 0);
  b[0x1E] ^= 0x40; b[0x4C] ^= 0x40; b[0x54] ^= 0x40;   // relocated constants: still a match
  CHECK(offs_find(b, sizeof b, kTbSpineFp, kTbSpineMask, TB_FP_LEN, &at) == 1);
  b[0x10] ^= 1;                                         // +0xC10 is not the state any more
  CHECK(offs_find(b, sizeof b, kTbSpineFp, kTbSpineMask, TB_FP_LEN, &at) == 0);
  for (int i = 0; i < TB_HOOK_FP_LEN; ++i) CHECK_MSG(kTbSpineMask[i] == 1, "the hook's compared head has no relocation");
  CHECK(TB_TRAMP_LEN == 6 && TB_HOOK_FP_LEN >= TB_TRAMP_LEN);
}

// Only clean trials are evidence, and the torso eases out.
static void tb_0206_ma_face_trial_taint() {
  CHECK(ma_face_grounded(1) && ma_face_grounded(2) && ma_face_grounded(3));
  CHECK_MSG(!ma_face_grounded(0) && !ma_face_grounded(4) && !ma_face_grounded(5) && !ma_face_grounded(9),
            "crouch, jumps, ropes, ladders and slides are not grounded idle/walk/run");
  CHECK_MSG(ma_face_trial_tainted(1, 1, 0u, 1) == 0, "idle, still, no key, grounded: clean");
  CHECK_MSG(ma_face_trial_tainted(3, 1, 0u, 1) == 1, "pressed from a run: tainted");
  CHECK_MSG(ma_face_trial_tainted(1, 0, 0u, 1) == 1, "moving at the press: tainted");
  CHECK_MSG(ma_face_trial_tainted(1, 1, 0x2u, 1) == 1, "a move key down on any watched frame: tainted");
  CHECK_MSG(ma_face_trial_tainted(1, 1, 0x20u, 1) == 0, "attack is not a move key");
  CHECK_MSG(ma_face_trial_tainted(1, 1, 0u, 0) == 1, "left the ground during the watch: tainted");
  int run = 0;
  CHECK_MSG(ma_face_refuse(ma_face_verdict(ma_face_trial_tainted(1, 1, 0x1u, 1), 50.f, 1, 1), &run) == 0,
            "a tainted trial that moved never refuses");
}
static void tb_0207_tb_owner_eases_out() {
  CHECK(tb_owner(1, 0, 0.f) == TB_OURS && tb_owner(1, 1, 12.f) == TB_OURS);
  CHECK_MSG(tb_owner(0, 1, -12.f) == TB_EASE_OUT, "our angle gone with a bend left: we ease it to 0");
  CHECK_MSG(tb_owner(0, 1, 0.f) == TB_STOCK, "at 0 the engine takes over");
  CHECK_MSG(tb_owner(0, 0, 12.f) == TB_STOCK, "a bend we never drove is the engine's");
  float b = -20.f;
  int ticks = 0;
  while (b != 0.f && ticks < 64) { b = tb_bend_step(b, 0.f, 0.016f); ++ticks; }
  CHECK_MSG(b == 0.f && ticks > 1, "the ease-out reaches 0 over several ticks, not in one");
}

// Facing by yaw: a press does not turn Duke in place.
static void tb_0208_ma_face_yaw_flip() {
  CHECK(ma_face_flip_yaw(270.f) == 90.f && ma_face_flip_yaw(90.f) == 270.f);
  CHECK(ma_face_flip_yaw(0.f) == 180.f && ma_face_flip_yaw(350.f) == 170.f);
  CHECK_MSG(ma_face_flip_yaw(-90.f) == 90.f && ma_face_flip_yaw(540.f) == 0.f, "any angle comes back in [0, 360)");
  const float y = ma_face_flip_yaw(123.5f), r = y * 3.14159265f / 180.f, r0 = 123.5f * 3.14159265f / 180.f;
  CHECK_MSG(fabsf(sinf(r) + sinf(r0)) < 1e-5f && fabsf(cosf(r) + cosf(r0)) < 1e-5f,
            "the forward (-sin, cos) reverses exactly");
  CHECK_MSG(ma_face_may_write(1, 0u) == 1, "idle with no move key: write");
  CHECK_MSG(ma_face_may_write(1, 0x20u) == 1, "attack held is fine");
  CHECK_MSG(ma_face_may_write(3, 0u) == 0 && ma_face_may_write(2, 0u) == 0, "not while walking or running");
  CHECK_MSG(ma_face_may_write(5, 0u) == 0 && ma_face_may_write(0, 0u) == 0, "not airborne or in another state");
  CHECK_MSG(ma_face_may_write(1, 0x1u) == 0 && ma_face_may_write(1, 0x10u) == 0, "not with a move key down");
  int run = 0;
  CHECK_MSG(ma_face_refuse(MA_FACE_NOTURN, &run) == 0 && ma_face_refuse(MA_FACE_NOTURN, &run) == 1 &&
            MA_FACE_NOTURN_MAX == 2, "two clean writes that do not stick refuse the row");
}

// Sensitivity and the reticle's style.
static void tb_0209_ma_cursor_step_sensitivity() {
  CHECK_MSG(ma_cursor_step(100.f, 10, 100, 1600) == 110.f, "100 %: one pixel a mickey, as the engine's cursor");
  CHECK(ma_cursor_step(100.f, 10, 250, 1600) == 125.f);
  CHECK(ma_cursor_step(100.f, -10, 50, 1600) == 95.f);
  CHECK_MSG(ma_cursor_step(1595.f, 40, 100, 1600) == 1600.f, "clamped to the right edge");
  CHECK_MSG(ma_cursor_step(3.f, -40, 400, 1600) == 0.f, "and the left");
  CHECK_MSG(ma_cursor_step(5.f, 3, 10, 900) > 5.29f && ma_cursor_step(5.f, 3, 10, 900) < 5.31f, "10 % keeps the fraction");
  CHECK_MSG(ma_cursor_step(5.f, 3, 5, 900) == ma_cursor_step(5.f, 3, MA_SENS_MIN, 900), "below the range clamps");
  CHECK_MSG(ma_cursor_step(5.f, 3, 9000, 900) == ma_cursor_step(5.f, 3, MA_SENS_MAX, 900), "above the range clamps");
  CHECK(MA_SENS_MIN == 10 && MA_SENS_MAX == 400 && MA_SENS_DEF == 100);
}
static void tb_0211_ma_swatches() {
  for (int w = 120; w <= 400; w += 7) {
    MaRect sw[3], sep[2];
    ma_swatch_layout(10, 20, w, 62, sw, sep);
    CHECK_MSG(sw[0].x == 10 && sw[2].x + sw[2].w == 10 + w, "swatches span the width");
    CHECK_MSG(sep[0].x == sw[0].x + sw[0].w && sw[1].x == sep[0].x + 1, "separator between 0 and 1");
    CHECK_MSG(sep[1].x == sw[1].x + sw[1].w && sw[2].x == sep[1].x + 1, "separator between 1 and 2");
    CHECK_MSG(sw[0].w == sw[1].w && sw[2].w >= sw[1].w && sw[2].w - sw[1].w <= 2, "equal within the remainder");
    CHECK(sw[0].h == 62 && sep[0].h == 62 && sw[1].y == 20);
  }
  CHECK(ma_swatch_colour(0) == 0xFF3B3F42u && ma_swatch_colour(1) == 0xFF7A5A2Au && ma_swatch_colour(2) == 0xFFCFC8B6u);
}
static void tb_0210_ma_reticle_style() {
  for (int t = MA_RET_THICK_MIN; t <= MA_RET_THICK_MAX; ++t) {
    MaRect r[5];
    CHECK(ma_reticle_rects(400, 300, 8, t, 1, r) == 5);
    CHECK(ma_reticle_rects(400, 300, 8, t, 0, r) == 4);
    ma_reticle_rects(400, 300, 8, t, 1, r);
    // r[0] left, r[1] right, r[2] up, r[3] down, r[4] dot. Twice the centre line of the dot, per axis.
    const int c2x = 2 * r[4].x + r[4].w, c2y = 2 * r[4].y + r[4].h;
    CHECK_MSG(r[0].x + r[1].x + r[1].w == c2x, "horizontal arms symmetric about the dot");
    CHECK_MSG(r[2].y + r[3].y + r[3].h == c2y, "vertical arms symmetric about the dot");
    CHECK_MSG(r[0].w == 8 && r[1].w == 8 && r[2].h == 8 && r[3].h == 8, "arm length is the size");
    CHECK_MSG(r[0].h == t && r[2].w == t && r[4].w == t && r[4].h == t, "thickness everywhere");
    CHECK_MSG(r[0].x + r[0].w < r[4].x && r[1].x > r[4].x + r[4].w - 1, "a gap between the arms and the dot");
    CHECK_MSG(2 * r[0].y + r[0].h == c2y && 2 * r[2].x + r[2].w == c2x, "arms on the dot's centre lines");
  }
  CHECK(ma_reticle_colour(MA_RET_COLOUR_DEF) == C_PLAYER);
  CHECK(ma_reticle_colour(0) == 0xFFFFFFFFu && ma_reticle_colour(-1) == ma_reticle_colour(MA_RET_COLOUR_DEF));
  CHECK(ma_reticle_colour(MA_RET_COLOURS) == ma_reticle_colour(MA_RET_COLOUR_DEF));
  for (int i = 0; i < MA_RET_COLOURS; ++i) {
    CHECK_MSG(ma_reticle_colour_label(i)[0] != 0 && (ma_reticle_colour(i) >> 24) == 0xFF, "named and opaque");
    for (int j = 0; j < i; ++j) CHECK_MSG(ma_reticle_colour(i) != ma_reticle_colour(j), "no repeats");
  }
  config_defaults();
  CHECK(g_cfg.mouseSens == MA_SENS_DEF && g_cfg.retColour == MA_RET_COLOUR_DEF && g_cfg.retSize == MA_RET_SIZE_DEF);
  CHECK(g_cfg.retThick == MA_RET_THICK_DEF && g_cfg.retDot == 1 && g_cfg.retOutline == 1);
  CHECK(MA_RET_SIZE_MIN <= MA_RET_SIZE_DEF && MA_RET_SIZE_DEF <= MA_RET_SIZE_MAX);
}

// Review 2: no facing while the game takes no input or a cutscene runs, nor along a track seen end-on.
static void tb_0211_ma_face_live_and_axis() {
  CHECK_MSG(ma_face_world_live(0x8u, 0, 0) == 1, "world+0xC8 bit 8 set, +0x14 zero, no letterbox: live");
  CHECK_MSG(ma_face_world_live(0x0u, 0, 0) == 0, "bit 8 clear: the input reader returns early (duke!0x1007d27a)");
  CHECK_MSG(ma_face_world_live(0xFFu, 1, 0) == 0, "+0x14 non-zero: likewise (0x1007d287)");
  CHECK_MSG(ma_face_world_live(0x8u, 0, 1) == 0, "a cutscene letterbox is up");
  CHECK(ma_face_axis_ok(MA_FACE_MIN_DX) == 1 && ma_face_axis_ok(-MA_FACE_MIN_DX - 1.f) == 1);
  CHECK_MSG(ma_face_axis_ok(MA_FACE_MIN_DX * 0.5f) == 0 && ma_face_axis_ok(0.f) == 0, "end-on: the side is unknown");
  CHECK_MSG(ma_face_needs_turn(1, -MA_FACE_MIN_DX * 0.5f) == 0, "no turn on an unknown side");
}

// Panel clean-up: a removed row's key is no longer read.
static void tb_0212_cleanup_removed_keys_not_read() {
  static const char* const kGone[][2] = {
    { "esp", "level" }, { "esp", "keycards" }, { "freecam", "nocull" }, { "freecam", "speed" },
    { "freecam", "forward" }, { "freecam", "back" }, { "freecam", "left" }, { "freecam", "right" },
    { "freecam", "up" }, { "freecam", "down" }, { "freecam", "faster" }, { "freecam", "slower" },
    { "mouseaim", "face" }, { "mouseaim", "torso" } };
  int known = 0;
  for (int i = 0; i < (int)(sizeof kGone / sizeof kGone[0]); ++i) known += config_key_known(kGone[i][0], kGone[i][1]);
  CHECK_MSG(known == 0, "removed rows' keys are not read");
  CHECK(config_key_known("freecam", "key") && config_key_known("freecam", "speedpct") && config_key_known("debug", "timing"));
  CHECK(config_key_known("panel", "x") && config_key_known("esp", "items"));
}
// The movement keys are fixed at the old defaults, one per FCK_ bit; their bind rows read unbound.
static void tb_0213_freecam_fixed_move_keys() {
  const int want[8] = { 'W', 'S', 'A', 'D', 0x20, 0x11, 0x10, 0x12 };
  int off = 0;
  for (int k = 0; k < 8; ++k) if (FREECAM_MOVE_VK[k] != want[k]) off++;
  CHECK_MSG(off == 0, "W S A D Space Ctrl Shift Alt");
  CHECK(FCK_FWD == 1 << 0 && FCK_SLOW == 1 << 7);
  config_defaults();
  CHECK(config_bind_get(BIND_ROW_FC_FORWARD) == 0 && config_bind_get(BIND_ROW_FC_SLOWER) == 0);
  config_bind_set(BIND_ROW_FC_FORWARD, 'Z');
  CHECK(config_bind_get(BIND_ROW_FC_FORWARD) == 0);
}
// Flight speed is a percentage: 100 % is the old 8 m/s default.
static void tb_0214_freecam_speed_pct() {
  config_defaults();
  CHECK(g_cfg.freecamSpeedPct == FREECAM_SPEED_DEF_PCT && FREECAM_SPEED_DEF_PCT == 100);
  CHECK(fabs(freecam_speed_mps(100) - 8.0f) < 1e-5f);
  CHECK(fabs(freecam_speed_mps(50) - 4.0f) < 1e-5f);
  CHECK(fabs(freecam_speed_mps(500) - 40.0f) < 1e-4f);
  CHECK(fabs(freecam_speed_mps(0) - freecam_speed_mps(FREECAM_SPEED_MIN_PCT)) < 1e-6f);   // clamped
  CHECK(fabs(freecam_speed_mps(9999) - 40.0f) < 1e-4f);
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_speedpct");
  write_text(path, "[freecam]\r\nspeedpct=250\r\nspeed=3\r\n");
  config_load_from(path);
  CHECK(g_cfg.freecamSpeedPct == 250);
  DeleteFileA(path);
  config_defaults();
}

// The Free camera switch arms the camera hotkeys; it starts no flight, and off ends one (panel clean-up).
static void tb_0215_freecam_switch_arms_hotkeys() {
  config_defaults();
  CHECK_MSG(g_cfg.freecamOn == 1, "armed by default, so the Camera key works as before");
  CHECK(config_key_known("freecam", "on"));
  CHECK(freecam_hotkeys_armed(1) && !freecam_hotkeys_armed(0));
  CHECK(freecam_switch_ends_flight(0, true));
  CHECK(!freecam_switch_ends_flight(0, false) && !freecam_switch_ends_flight(1, true) && !freecam_switch_ends_flight(1, false));
}
// menu.h: a collapsed sub-row block lays out nothing but keeps its row ordinals, so the rows below keep their
// animation slots and row keys whether it shows or not.
static void tb_0216_menu_collapse_keeps_ordinals() {
  const MenuCollapse off = menu_collapse_begin(0, 10, 6);
  CHECK(!off.shown && menu_collapse_end(off, 10) == 16);
  const MenuCollapse on = menu_collapse_begin(1, 10, 6);
  CHECK(on.shown && menu_collapse_end(on, 16) == 16);
  CHECK_MSG(menu_collapse_end(on, 13) == 16, "a block that drew fewer rows still ends at its reserve");
  CHECK_MSG(menu_collapse_end(on, 18) == 18, "never moves an ordinal backwards");
}
// Explodables (panel clean-up): breakable_adv only, AA's hazard-yellow x, hidden once broken (+0x2E0 cleared by
// the breakable base's break, duke!0x100f1600) and while its damage gate +0xC44 is shut.
static void tb_0217_explodables_class_and_shown() {
  CHECK(class_lookup("breakable_adv").cat == CAT_EXPLODABLE);
  CHECK(strcmp(class_lookup("breakable_adv").name, "Explodable") == 0);
  CHECK_MSG(class_lookup("breakable_obstacle").cat == CAT_OTHER, "the respawning track obstacles are not walls");
  CHECK(class_lookup("barrel").cat == CAT_OTHER && class_lookup("crate").cat == CAT_OTHER);
  CHECK(marker_colour(CAT_EXPLODABLE, -1) == C_HAZARD);
  EntSnap e = {};
  e.cat = CAT_EXPLODABLE; e.active = 1; e.armed = 1;
  CHECK(ent_explodable_shown(e));
  e.active = 0;
  CHECK_MSG(!ent_explodable_shown(e), "broken");
  e.active = 1; e.armed = 0;
  CHECK_MSG(!ent_explodable_shown(e), "not yet damageable");
  config_defaults();
  CHECK(g_cfg.espExplodables == 1 && config_key_known("esp", "explodables"));
}
// AA's esp_saltire (its esp_layout.h): the x's two diagonals, `arm` out along each axis.
static void tb_0218_esp_saltire() {
  const EspSaltire s = esp_saltire(100, 50, 5);
  CHECK(s.x0 == 95 && s.y0 == 45 && s.x1 == 105 && s.y1 == 55);
  CHECK(s.x2 == 95 && s.y2 == 55 && s.x3 == 105 && s.y3 == 45);
}
// Hide what you can't take: the engine's own touch tests (re/pickups.md section 4). Ammo refuses at its pool's cap
// (ammo_on_touch duke!0x1005e6c0); ego refuses a type other than 0/1 (ego_on_touch 0x1005eb30); the rest always take.
static void tb_0219_pickup_can_take() {
  PlayerPools pp = {};
  pp.valid = 1;
  pp.cur[0] = 199; pp.cap[0] = 200.f;
  pp.cur[1] = 10;  pp.cap[1] = 10.f;
  pp.cur[2] = 0;   pp.cap[2] = 200.9f;
  CHECK(pickup_can_take(ITEM_AMMO, 0, &pp) == TAKE_YES && pickup_can_take(ITEM_AMMO, 1, &pp) == TAKE_YES);
  CHECK_MSG(pickup_can_take(ITEM_AMMO, 2, &pp) == TAKE_NO && pickup_can_take(ITEM_AMMO, 3, &pp) == TAKE_NO, "C5 at cap");
  CHECK(pickup_can_take(ITEM_AMMO, 4, &pp) == TAKE_YES);
  pp.cur[2] = 200;
  CHECK_MSG(pickup_can_take(ITEM_AMMO, 5, &pp) == TAKE_NO, "the cap truncates, as the game's ftol");
  CHECK_MSG(pickup_can_take(ITEM_AMMO, 6, &pp) == TAKE_NO && pickup_can_take(ITEM_AMMO, 99, &pp) == TAKE_NO,
            "a read type off the jump table is never taken");
  CHECK_MSG(pickup_can_take(ITEM_AMMO, -1, &pp) == TAKE_UNKNOWN && pickup_can_take(ITEM_EGO, -1, &pp) == TAKE_UNKNOWN,
            "a type that could not be read is shown");
  pp.valid = 0;
  CHECK_MSG(pickup_can_take(ITEM_AMMO, 0, &pp) == TAKE_UNKNOWN, "pools unread: shown");
  pp.valid = 1; pp.cap[0] = 3e10f;
  CHECK_MSG(pickup_can_take(ITEM_AMMO, 0, &pp) == TAKE_UNKNOWN, "a cap no int holds: shown");
  CHECK(pickup_can_take(ITEM_EGO, 0, &pp) == TAKE_YES && pickup_can_take(ITEM_EGO, 1, &pp) == TAKE_YES);
  CHECK_MSG(pickup_can_take(ITEM_EGO, 2, &pp) == TAKE_YES && pickup_can_take(ITEM_EGO, -5, &pp) == TAKE_YES,
            "the ego's init clamps health_type into 0..1, so every read type is a grant");
  const int always[4] = { ITEM_GUN, ITEM_LIFE, ITEM_JETPACK, ITEM_FORCEFIELD };
  int yes = 0;
  for (int i = 0; i < 4; ++i) yes += pickup_can_take(always[i], 0, &pp) == TAKE_YES;
  CHECK_MSG(yes == 4, "guns, Double Damage, jetpack and force field are always taken");
  config_defaults();
  CHECK(g_cfg.espHideTaken == 0 && config_key_known("esp", "hidecanttake"));
}
// The camera's four bind rows refuse the fixed movement keys: bound there, one press would also fly the camera.
static void tb_0220_freecam_bind_refuses_move_keys() {
  const int rows[4] = { BIND_ROW_CAMERA, BIND_ROW_TELEPORT, BIND_ROW_CAMCOPY, BIND_ROW_CAMPASTE };
  int refused = 0;
  for (int r = 0; r < 4; ++r)
    for (int k = 0; k < 8; ++k) refused += freecam_bind_refused(rows[r], FREECAM_MOVE_VK[k]);
  CHECK_MSG(refused == 32, "every movement key on every camera row");
  CHECK(!freecam_bind_refused(BIND_ROW_TELEPORT, 'T') && !freecam_bind_refused(BIND_ROW_CAMERA, 'V'));
  CHECK_MSG(!freecam_bind_refused(BIND_ROW_MENU, 'W') && !freecam_bind_refused(BIND_ROW_HOLDZOOMOUT, 'W'),
            "rows outside the camera keep every key");
}
// double-jump.md section 5 lever 1: undo only the contact disarm that lands just after takeoff while rising.
static void tb_0221_jr_takeoff_restore_table() {
  const float e10 = 100.f, old = 90.f;   // old: the landing reset left E14 at the previous jump's E10
  CHECK_MSG(jr_takeoff_disarm(old, e10, e10, 100.005, 585.f) == 1, "takeoff disarm 5 ms after the jump, rising");
  CHECK_MSG(jr_takeoff_disarm(old, e10, e10, 100.019, 585.f) == 1, "still inside 20 ms");
  CHECK_MSG(jr_takeoff_disarm(old, e10, e10, 100.021, 585.f) == 0, "past 20 ms");
  CHECK_MSG(jr_takeoff_disarm(old, e10, e10, 100.8, -300.f) == 0, "a real landing");
  CHECK_MSG(jr_takeoff_disarm(old, e10, e10, 100.15, 0.f) == 0, "a ceiling bump");
  CHECK_MSG(jr_takeoff_disarm(old, e10, e10, 100.01, 0.f) == 0, "a ceiling bump inside 20 ms");
  CHECK_MSG(jr_takeoff_disarm(old, 100.004f, e10, 100.004, 585.f) == 0, "a press stamps E14 with now, not E10");
  CHECK_MSG(jr_takeoff_disarm(100.25f, e10, e10, 100.005, 585.f) == 0, "already disarmed: a press came first");
  CHECK_MSG(jr_takeoff_disarm(e10, e10, e10, 100.005, 585.f) == 0, "no change this frame");
}
// The engine's three air gates in order (A 0x10085b2a..b78), read back from one frame's fields.
static void tb_0222_jr_press_classify() {
  const float e10 = 100.f, e70 = -80.f;
  CHECK(jr_classify_press(0, -10.f, 373.f, 90.f, 100.4f, e10, 100.4, -60.f, e70) == PR_NONE);
  CHECK(jr_classify_press(1, -10.f, 373.f, 90.f, 100.4f, e10, 100.4, -60.f, e70) == PR_BOOST);
  CHECK_MSG(jr_classify_press(1, -10.8f, -21.6f, 90.f, 100.15f, e10, 100.15, -60.f, e70) == PR_REFUSED_TIME,
            "gate 1: 0.2 s after the jump");
  CHECK_MSG(jr_classify_press(1, -10.8f, -21.6f, e10, 100.25f, e10, 100.25, -60.f, e70) == PR_REFUSED_ARMED,
            "gate 2: E14 was not below E10");
  CHECK_MSG(jr_classify_press(1, -10.8f, -21.6f, 90.f, 100.4f, e10, 100.4, -140.f, e70) == PR_REFUSED_HEIGHT,
            "gate 3: more than 50 below the jump start");
  CHECK_MSG(jr_classify_press(1, -10.8f, -21.6f, 90.f, 90.f, e10, 100.4, -60.f, e70) == PR_REFUSED_STATE,
            "no E14 stamp: not the air branch");
  CHECK(jr_classify_press(1, -10.8f, -21.6f, 90.f, 100.4f, e10, 100.4, -60.f, e70) == PR_REFUSED_OTHER);
  CHECK(strcmp(jr_press_tag(PR_NONE), "") == 0);
  CHECK(strcmp(jr_press_tag(PR_BOOST), " BOOST") == 0);
  CHECK(strcmp(jr_press_tag(PR_REFUSED_ARMED), " REFUSED(gate2)") == 0);
  CHECK(strcmp(jr_press_tag(PR_REFUSED_STATE), " REFUSED(state)") == 0);
}
static void tb_0223_jumpfix_config_default_on() {
  config_defaults();
  CHECK(g_cfg.jumpFix == 1);
  CHECK(config_key_known("framerate", "doublejump"));
}

// First person: pitch is ours only, clamped to +-80 degrees;
// mouse up (dy < 0) looks up.
static void tb_0224_fp_pitch_clamp() {
  CHECK(fabsf(fp_pitch_step(0.f, -100, 100) - 10.f) < 1e-4f);
  CHECK(fabsf(fp_pitch_step(0.f, 100, 100) + 10.f) < 1e-4f);
  CHECK(fp_pitch_step(0.f, -100000, 100) == FP_PITCH_MAX);
  CHECK(fp_pitch_step(0.f, 100000, 100) == -FP_PITCH_MAX);
  CHECK(FP_PITCH_MAX == 80.f);
  CHECK(fp_clamp_pitch(85.f) == 80.f && fp_clamp_pitch(-85.f) == -80.f && fp_clamp_pitch(12.f) == 12.f);
}

// The yaw from the device's mickeys times the mouse-aim Sensitivity (0.1 degree a mickey at 100%), wrapped.
static void tb_0225_fp_yaw_from_deltas() {
  CHECK(fabsf(fp_deg_per_mickey(100) - 0.1f) < 1e-6f);
  CHECK(fabsf(fp_deg_per_mickey(200) - 0.2f) < 1e-6f);
  CHECK_MSG(fabsf(fp_deg_per_mickey(1) - 0.01f) < 1e-6f, "below MA_SENS_MIN clamps to 10%");
  CHECK_MSG(fabsf(fp_deg_per_mickey(9999) - 0.4f) < 1e-6f, "above MA_SENS_MAX clamps to 400%");
  CHECK(fabsf(fp_yaw_step(10.f, 100, 100, 1) - 20.f) < 1e-4f);
  CHECK(fabsf(fp_yaw_step(10.f, 100, 100, -1) - 0.f) < 1e-4f);
  CHECK_MSG(fabsf(fp_yaw_step(355.f, 100, 100, 1) - 365.f) < 1e-3f, "not wrapped: the engine never wraps it");
  CHECK_MSG(fabsf(fp_yaw_step(5.f, -100, 100, 1) + 5.f) < 1e-3f, "not wrapped below 0");
  // Engine forward (-sin Y, 0, cos Y): at Y = 0 raising Y swings it toward -x.
  const float rNegX[3] = { -1.f, 0.f, 0.f }, rPosX[3] = { 1.f, 0.f, 0.f };
  CHECK(fp_yaw_sign(0.f, rNegX) == 1);
  CHECK(fp_yaw_sign(0.f, rPosX) == -1);
  const float fwd0[3] = { 0.f, 0.f, 1.f }, fwdBack[3] = { 0.f, 0.f, -1.f };
  CHECK(fp_forward_agree(0.f, fwd0) > 0.99f);
  CHECK(fp_forward_agree(0.f, fwdBack) < -0.99f);
}

// The mode: one write per change, the exit's re-pick, the quit restore, and never g_rpg with g_fps.
static void tb_0226_fp_mode_machine() {
  FpState s = FpState();
  CHECK(fp_step(s, 0, 0, 1) == FP_NONE);
  CHECK(fp_step(s, 1, 0, 1) == FP_APPLY);
  CHECK_MSG(fp_step(s, 1, 1, 1) == FP_REFUSE, "g_rpg on: g_fps is never set");
  CHECK_MSG(fp_step(s, 1, 0, 0) == FP_REFUSE_QUIT, "no quit detour: g_fps could be archived at 1");
  fp_done(&s, FP_APPLY, 0);
  CHECK(s.applied == 1 && s.repick == 0);
  CHECK(fp_step(s, 1, 0, 1) == FP_NONE);
  CHECK_MSG(fp_step(s, 1, 1, 1) == FP_RESTORE, "g_rpg turned on under ours: ours goes");
  CHECK(fp_quit(s) == FP_RESTORE);
  CHECK(fp_step(s, 0, 0, 1) == FP_RESTORE);
  int dukeA = 1, dukeB = 2;
  fp_done(&s, FP_RESTORE, &dukeA);
  CHECK(s.applied == 0 && s.repick == 1 && s.repickPlayer == &dukeA);
  CHECK(fp_quit(s) == FP_NONE);
  CHECK_MSG(fp_repick(s, &dukeA, FP_ENGINE_STATE) == FP_REPICK_WAIT, "still in state 17: wait for the engine's exit");
  CHECK(fp_repick(s, &dukeA, 3) == FP_REPICK_NOW);
  CHECK_MSG(fp_repick(s, &dukeB, 3) == FP_REPICK_DROP, "a new player (level change) starts on a track");
  CHECK(fp_repick(s, 0, 3) == FP_REPICK_DROP);
  fp_done(&s, FP_REPICKED, 0);
  CHECK(s.repick == 0 && s.repickPlayer == 0);
  CHECK(fp_repick(s, &dukeA, 3) == FP_REPICK_NONE);
  CHECK(fp_active(1, FP_ENGINE_STATE) && !fp_active(1, 3) && !fp_active(0, FP_ENGINE_STATE));
  CHECK(FP_ENGINE_STATE == 17);
}

// Shots go along the view's centre ray: to its first world hit, else far along it. Silent aim's lock wins.
static void tb_0227_fp_view_fire_dir() {
  CHECK(fp_fire_source(1, 1, 1, 0, 0) == MA_SRC_SILENT);
  CHECK(fp_fire_source(0, 1, 1, 1, 1) == FP_SRC_VIEW);
  CHECK_MSG(fp_fire_source(0, 1, 0, 1, 1) == MA_SRC_STOCK, "first person with no view this frame: stock");
  CHECK(fp_fire_source(0, 0, 1, 1, 1) == MA_SRC_MOUSE);
  CHECK(fp_fire_source(0, 0, 0, 0, 0) == MA_SRC_STOCK);
  const Vec3 eye = { 0.f, 40.f, 0.f }, fwd = { 0.f, 0.f, 1.f }, hit = { 0.f, 40.f, 300.f };
  const Vec3 a = fp_view_point(eye, fwd, 1, hit, FP_RAY_LEN);
  CHECK(a.z == 300.f && a.y == 40.f);
  const Vec3 b = fp_view_point(eye, fwd, 0, hit, FP_RAY_LEN);
  CHECK(b.z == FP_RAY_LEN && b.x == 0.f);
  // The detour's own rewrite toward that point: the gun's origin below the eye aims up to the centre's hit.
  const Vec3 orig = { 0.f, 0.f, 2.f }, origin = { 0.f, 30.f, 0.f };
  Vec3 out;
  CHECK(aim_redirect(orig, origin, a, &out));
  CHECK(fabsf(aim_len(out) - 2.f) < 1e-4f && out.y > 0.f && out.z > 0.f);
}

static void tb_0228_fp_config_defaults_off() {
  config_defaults();
  CHECK(g_cfg.firstPerson == 0);
  CHECK(g_cfg.moveAnywhere == 0);
  CHECK(config_key_known("mouseaim", "firstperson"));
  CHECK(config_key_known("mouseaim", "moveanywhere"));
}

// Move anywhere: the detours act on the local player only, with the switch on, outside first person.
static void tb_0229_mv_gate() {
  CHECK(mv_acts(1, 1, 0));
  CHECK_MSG(!mv_acts(1, 0, 0), "a monster shares the function: never touched");
  CHECK(!mv_acts(0, 1, 0));
  CHECK_MSG(!mv_acts(1, 1, 1), "first person: state 17 already clears +0xB1C");
  CHECK(mv_step(1, 0) == MV_ON);
  CHECK(mv_step(0, 1) == MV_OFF);
  CHECK(mv_step(1, 1) == MV_NONE && mv_step(0, 0) == MV_NONE);
}

// The snap keeps the track bookkeeping (+0xB58/+0xB5C, the facing) but not its x/z write.
static void tb_0230_mv_snap_keeps_position() {
  const Vec3 before = { 10.f, 5.f, -3.f }, after = { 40.f, 6.f, 7.f };
  const Vec3 k = mv_snap_keep(before, after);
  CHECK(k.x == 10.f && k.z == -3.f);
  CHECK_MSG(k.y == 6.f, "y is the engine's: the snap never writes it");
}

// The two entries the detours fingerprint before arming (duke 0x1008e360, 0x1008e310, read 2026-10-02).
static void tb_0231_mv_fingerprints() {
  static const unsigned char constrain[MV_CONSTRAIN_FP_LEN] = {
    0x83, 0xEC, 0x60, 0x53, 0x56, 0x8B, 0xF1, 0x8A, 0x86, 0x1C, 0x0B, 0x00, 0x00, 0x32, 0xDB, 0x84, 0xC0 };
  static const unsigned char snap[MV_SNAP_FP_LEN] = {
    0x8B, 0xC1, 0x8B, 0x88, 0x20, 0x0B, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x13, 0x8A, 0x90, 0x1C, 0x0B, 0x00, 0x00,
    0x84, 0xD2, 0x74, 0x09, 0x8B, 0x11, 0x50, 0xFF, 0x92, 0xD0, 0x01, 0x00, 0x00, 0xC3 };
  CHECK(mv_fp_is_constrain(constrain));
  CHECK(mv_fp_is_snap(snap));
  CHECK(!mv_fp_is_constrain(snap) && !mv_fp_is_snap(constrain));
  unsigned char bad[MV_SNAP_FP_LEN];
  memcpy(bad, snap, sizeof bad);
  bad[27] = 0xD4;   // another track slot
  CHECK(!mv_fp_is_snap(bad));
  CHECK(MV_CONSTRAIN_PROLOGUE == 5 && MV_SNAP_PROLOGUE == 8);
  CHECK(!mv_fp_is_snap(0) && !mv_fp_is_constrain(0));
}

// Up/Down move in depth while Move anywhere is on: up away from the camera, down toward it.
static void tb_0232_mv_depth_axis_and_away() {
  CHECK(mv_depth_axis(1, 0) == 1 && mv_depth_axis(0, 1) == -1);
  CHECK(mv_depth_axis(1, 1) == 0 && mv_depth_axis(0, 0) == 0);
  // Track along +x, camera looking down +z and a little along the track: away is +z, flat and unit.
  const Vec3 cam = { 0.3f, -0.4f, 0.8f }, tx = { 1.f, 0.f, 0.f };
  Vec3 a;
  CHECK(mv_away(cam, 1, tx, &a));
  CHECK(fabsf(a.x) < 1e-5f && a.y == 0.f && fabsf(a.z - 1.f) < 1e-5f);
  // No track: the camera's flattened forward.
  CHECK(mv_away(cam, 0, tx, &a));
  CHECK(a.y == 0.f && fabsf(a.x * a.x + a.z * a.z - 1.f) < 1e-5f && a.x > 0.f && a.z > 0.f);
  const Vec3 down = { 0.f, -1.f, 0.f };
  CHECK_MSG(!mv_away(down, 0, tx, &a), "a camera looking straight down has no away");
}

static void tb_0233_mv_move_vector() {
  const Vec3 away = { 0.f, 0.f, 1.f }, right = { 1.f, 0.f, 0.f }, zero = { 0.f, 0.f, 0.f };
  Vec3 o = mv_move_vector(right, 1, 0, away);
  CHECK_MSG(o.x == 1.f && o.z == 0.f, "no depth key: the engine's vector as it was");
  o = mv_move_vector(zero, 0, 1, away);
  CHECK(fabsf(o.z - 1.f) < 1e-6f && o.x == 0.f);
  o = mv_move_vector(zero, 0, -1, away);
  CHECK(fabsf(o.z + 1.f) < 1e-6f);
  o = mv_move_vector(right, 1, 1, away);   // a diagonal, unit length: the walk speed
  CHECK(fabsf(o.x - 0.70710678f) < 1e-5f && fabsf(o.z - 0.70710678f) < 1e-5f && o.y == 0.f);
  const Vec3 yawFwd = { 0.f, 0.f, -1.f };
  o = mv_move_vector(yawFwd, 0, 1, away);
  CHECK_MSG(fabsf(o.z - 1.f) < 1e-6f, "no left/right held: the engine's yaw fallback is replaced");
  o = mv_move_vector(zero, 1, 1, away);
  CHECK_MSG(fabsf(o.z - 1.f) < 1e-6f, "the engine gave no walk: depth alone still moves him (review 2 M1)");
}

// The two impulse reads of player_read_input_mask (push 0x67/0x68; call [import]) found by a scan.
static void tb_0234_mv_find_impulse_call() {
  unsigned char code[32];
  memset(code, 0x90, sizeof code);
  const unsigned slot = 0x10183E94u;
  unsigned char call[8] = { 0x6A, 0x67, 0xFF, 0x15, 0x94, 0x3E, 0x18, 0x10 };
  memcpy(code + 5, call, 8);
  call[1] = 0x68;
  memcpy(code + 20, call, 8);
  CHECK(mv_find_impulse_call(code, sizeof code, 0x67, slot) == 5 + 8);
  CHECK(mv_find_impulse_call(code, sizeof code, 0x68, slot) == 20 + 8);
  CHECK(mv_find_impulse_call(code, sizeof code, 0x69, slot) == -1);
  CHECK_MSG(mv_find_impulse_call(code, sizeof code, 0x67, slot + 4) == -1, "another import slot");
  CHECK(mv_find_impulse_call(code, 12, 0x67, slot) == -1);
}

static void tb_0235_mv_new_fingerprints() {
  static const unsigned char mv[MV_MOVEVEC_FP_LEN] = {
    0x83, 0xEC, 0x0C, 0x8B, 0x54, 0x24, 0x14, 0xD9, 0x82, 0x54, 0x0E, 0x00, 0x00, 0x53, 0xD9, 0x82, 0x50, 0x0E, 0x00,
    0x00, 0x56, 0xD9, 0x82, 0x4C, 0x0E, 0x00, 0x00 };
  static const unsigned char rim[MV_READMASK_FP_LEN] = {
    0x51, 0x53, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x14, 0x33, 0xDB, 0x3B, 0xFB, 0x8B, 0xF1 };
  static const unsigned char iv[MV_IMPVAL_FP_LEN] = {
    0x8B, 0x44, 0x24, 0x04, 0x8B, 0x4C, 0x24, 0x08, 0x6B, 0xC0, 0x15, 0x03, 0xC1, 0xD9, 0x04, 0x85 };
  CHECK(mv_fp_is_movevec(mv) && !mv_fp_is_movevec(rim));
  CHECK(mv_fp_is_readmask(rim) && !mv_fp_is_readmask(mv));
  CHECK(mv_fp_is_impval(iv) && !mv_fp_is_impval(mv));
  CHECK(MV_MOVEVEC_PROLOGUE == 7 && MV_IMPVAL_PROLOGUE == 8);
}

// Mouse look turns Duke only while the world takes input (ma_face_world_live); the deltas are drained regardless.
static void tb_0236_fp_look_gate() {
  CHECK(fp_look_applies(1, 0, 0, 0, 1));
  CHECK_MSG(!fp_look_applies(1, 0, 0, 0, 0), "paused, a menu or a cutscene: no turn");
  CHECK(!fp_look_applies(0, 0, 0, 0, 1));
  CHECK(!fp_look_applies(1, 1, 0, 0, 1) && !fp_look_applies(1, 0, 1, 0, 1) && !fp_look_applies(1, 0, 0, 1, 1));
}

// Switching Move anywhere off re-picks the track only with Duke grounded in idle, step or running (PS 1-3).
static void tb_0237_mv_exit_repick_waits() {
  CHECK(mv_repick(0, 1, 1, 0) == MV_REPICK_NONE);
  CHECK(mv_repick(1, 1, 1, 0) == MV_REPICK_NOW);
  CHECK(mv_repick(1, 1, 3, 0) == MV_REPICK_NOW);
  CHECK_MSG(mv_repick(1, 1, 5, 0) == MV_REPICK_WAIT, "mid-jump: wait, player_teleport would cancel the jump");
  CHECK(mv_repick(1, 1, 0, 0) == MV_REPICK_WAIT);
  CHECK_MSG(mv_repick(1, 0, 1, 0) == MV_REPICK_DROP, "another player (level change) starts on a track");
  CHECK_MSG(mv_repick(1, 1, 1, 1) == MV_REPICK_DROP, "first person owns the exit then");
}

// Review 2: the walk branch as the engine decides it (player_track_move_vector 0x10087fd0): a left/right axis that
// does not cancel, no 0x200, +0xB1C and a track.
static void tb_0238_mv_lr_walks() {
  CHECK(mv_lr_walks(0x1, 1, 1) && mv_lr_walks(0x2, 1, 1));
  CHECK_MSG(!mv_lr_walks(0x3, 1, 1), "left and right together cancel");
  CHECK(!mv_lr_walks(0x201, 1, 1) && !mv_lr_walks(0x1, 0, 1));
  CHECK_MSG(!mv_lr_walks(0x1, 1, 0), "no track: no walk along it");
  CHECK(!mv_lr_walks(0x8, 1, 1));
}

// The reader returns before its key reads while the world takes no input, so our held keys clear then.
static void tb_0239_mv_keys_clear_when_not_live() {
  CHECK(mv_key_held(1, 1) == 1);
  CHECK_MSG(mv_key_held(1, 0) == 0, "paused, a menu or a cutscene: no stale depth");
  CHECK(mv_key_held(0, 1) == 0);
}

// The exit's return to the track only when Duke left it.
static void tb_0240_mv_left_line() {
  CHECK(!mv_left_line(0.f) && !mv_left_line(MV_OFFLINE_EPS));
  CHECK(mv_left_line(MV_OFFLINE_EPS + 0.1f));
}

// Mouse lock (docs/re/mouse-lock.md): after each focus loss, one line a second for 60 s whatever it shows.
static void tb_0241_mw_after_focus_loss() {
  CHECK_MSG(!mw_due(5000, 0, 0, 0), "no focus loss seen yet");
  CHECK_MSG(mw_due(5000, 4000, 0, 0), "a second after the loss");
  CHECK_MSG(!mw_due(5000, 4000, 5001, 0), "not before the next second");
  CHECK_MSG(!mw_due(5000, 4000, 0, 1), "our window back in front");
  CHECK_MSG(mw_due(4000 + MW_WINDOW_MS, 4000, 0, 0) && !mw_due(4001 + MW_WINDOW_MS, 4000, 0, 0), "60 s, then stop");
  char k[8];
  mw_keys(0, k);
  CHECK(strcmp(k, "-") == 0);
  mw_keys(MW_K_LBUTTON | MW_K_ALT | MW_K_SHIFT, k);
  CHECK_MSG(strcmp(k, "LAS") == 0, "held keys as letters, fixed order");
}

// FP1 (gate 1): First person stands down while there is no player (loading, a level change, a scripted `camera`
// holding the input) and comes back once the level has taken input for FP_RESUME_NOPLAYER_MS.
static void tb_0242_fp_pause_no_player() {
  FpPause p = {};
  fp_pause_step(&p, 1, fp_pause_reason(1, 0), 1, 1000);
  CHECK_MSG(!p.paused, "a live player: no pause");
  fp_pause_step(&p, 1, fp_pause_reason(0, 0), 0, 2000);
  CHECK(p.paused && p.reason == FP_PAUSE_NO_PLAYER);
  fp_pause_step(&p, 1, fp_pause_reason(1, 0), 0, 3000);
  CHECK_MSG(p.paused, "the new player, but the world takes no input yet (cutscene, loading)");
  fp_pause_step(&p, 1, fp_pause_reason(1, 0), 1, 4000);
  fp_pause_step(&p, 1, fp_pause_reason(1, 0), 1, 4000 + FP_RESUME_NOPLAYER_MS - 1);
  CHECK(p.paused);
  fp_pause_step(&p, 1, fp_pause_reason(1, 0), 0, 4000 + FP_RESUME_NOPLAYER_MS - 1);
  fp_pause_step(&p, 1, fp_pause_reason(1, 0), 1, 4000 + FP_RESUME_NOPLAYER_MS);
  CHECK_MSG(p.paused, "a frame without input restarts the wait");
  fp_pause_step(&p, 1, fp_pause_reason(1, 0), 1, 4000 + 2 * FP_RESUME_NOPLAYER_MS);
  CHECK(!p.paused);
  fp_pause_step(&p, 1, fp_pause_reason(0, 0), 1, 9000);
  fp_pause_step(&p, 0, fp_pause_reason(0, 0), 1, 9001);
  CHECK_MSG(!p.paused && p.reason == FP_PAUSE_NONE, "the switch off forgets the pause");
}

// FP2: a ladder pauses too, and resumes sooner.
static void tb_0243_fp_pause_ladder() {
  FpPause p = {};
  CHECK(fp_pause_reason(1, 1) == FP_PAUSE_LADDER);
  CHECK_MSG(fp_pause_reason(0, 1) == FP_PAUSE_NO_PLAYER, "no player wins");
  fp_pause_step(&p, 1, FP_PAUSE_LADDER, 1, 100);
  CHECK(p.paused && p.reason == FP_PAUSE_LADDER);
  fp_pause_step(&p, 1, FP_PAUSE_NONE, 1, 200);
  fp_pause_step(&p, 1, FP_PAUSE_NONE, 1, 200 + FP_RESUME_LADDER_MS);
  CHECK(!p.paused);
  CHECK(FP_RESUME_LADDER_MS < FP_RESUME_NOPLAYER_MS);
}

// FP2/MA1: Duke's head (the point area_ladder_grab_test duke!0x1009a460 tests: pos + half.y up) inside a volume.
static void tb_0244_ent_point_in_obb() {
  const Vec3 c = { 100.f, 0.f, 0.f };
  const Vec3 ax[3] = { { 10.f, 0.f, 0.f }, { 0.f, 50.f, 0.f }, { 0.f, 0.f, 20.f } };
  CHECK(ent_point_in_obb(Vec3{ 105.f, 40.f, -15.f }, c, ax));
  CHECK(!ent_point_in_obb(Vec3{ 111.f, 0.f, 0.f }, c, ax));
  CHECK(!ent_point_in_obb(Vec3{ 100.f, 0.f, 21.f }, c, ax));
  const Vec3 rot[3] = { { 7.0710678f, 0.f, 7.0710678f }, { 0.f, 50.f, 0.f }, { -14.142136f, 0.f, 14.142136f } };
  CHECK_MSG(ent_point_in_obb(Vec3{ 90.f, 0.f, 10.f }, c, rot), "a turned volume: along its own z axis");
  CHECK(!ent_point_in_obb(Vec3{ 115.f, 0.f, -15.f }, c, rot));
  const Vec3 h = ent_ladder_head(Vec3{ 1.f, 2.f, 3.f }, Vec3{ 10.f, 30.f, 10.f });
  CHECK(h.x == 1.f && h.y == 32.f && h.z == 3.f);
}

// MA1: Up/Down go back to the game on a ladder (state 5), hanging (state 8) or with the head in a ladder volume.
static void tb_0245_mv_keys_to_game() {
  CHECK(!mv_keys_to_game(3, 0));
  CHECK(mv_keys_to_game(MV_STATE_LADDER, 0));
  CHECK(mv_keys_to_game(MV_STATE_HANG, 0));
  CHECK_MSG(mv_keys_to_game(3, 1), "at a ladder: Up grabs it");
}

// MA2: the per-frame camera measurement: yaw differences wrap, a snap is the camera outrunning Duke.
static void tb_0246_mv_camera_snap() {
  CHECK(fabsf(mv_yaw_diff(10.f, 350.f) - 20.f) < 1e-3f);
  CHECK(fabsf(mv_yaw_diff(350.f, 10.f) + 20.f) < 1e-3f);
  CHECK(fabsf(mv_yaw_diff(90.f, -270.f)) < 1e-3f);
  CHECK(!mv_cam_snap(5.f, 4.f));
  CHECK(mv_cam_snap(MV_SNAP_UNITS + 5.f, 4.f));
  CHECK_MSG(!mv_cam_snap(40.f, 40.f), "the camera keeping pace with a fast Duke is no snap");
}

// FP4: Rescue falls: a grounded spot becomes safe after RS_SAFE_MS; a fall of RS_FALL_MS and RS_FALL_DROP brings
// Duke back to it.
static void tb_0247_rescue_rule() {
  RescueState r = {};
  const Vec3 a = { 0.f, 100.f, 0.f }, b = { 50.f, 100.f, 0.f };
  CHECK(rescue_step(&r, 1, 1, a, 0.f, 1000) == RS_NONE && !r.haveSafe);
  CHECK(rescue_step(&r, 1, 1, b, 0.f, 1000 + RS_SAFE_MS) == RS_NONE && r.haveSafe && r.safe.x == 0.f);
  Vec3 f = b;
  unsigned long long t = 1000 + RS_SAFE_MS + 10;
  CHECK(rescue_step(&r, 1, 0, f, -600.f, t) == RS_NONE);
  f.y = 100.f - RS_FALL_DROP - 1.f;
  CHECK_MSG(rescue_step(&r, 1, 0, f, -600.f, t + RS_FALL_MS - 1) == RS_NONE, "not yet long enough");
  CHECK(rescue_step(&r, 1, 0, f, -600.f, t + RS_FALL_MS) == RS_RESCUE);
  CHECK_MSG(rescue_step(&r, 1, 0, f, -600.f, t + RS_FALL_MS + 1) == RS_NONE, "once per fall");
  RescueState q = r;
  q.airSince = 0;
  Vec3 g = { 0.f, 100.f - RS_FALL_DROP + 100.f, 0.f };
  rescue_step(&q, 1, 0, g, -600.f, 100000);
  CHECK_MSG(rescue_step(&q, 1, 0, g, -600.f, 100000 + RS_FALL_MS * 2) == RS_NONE, "long but shallow: no rescue");
  RescueState n = r;
  n.airSince = 0;
  rescue_step(&n, 0, 0, f, -600.f, 200000);
  CHECK_MSG(rescue_step(&n, 0, 0, f, -600.f, 200000 + RS_FALL_MS) == RS_NONE, "ineligible (dying, jetpack): never");
  RescueState z = {};
  rescue_step(&z, 1, 0, f, -600.f, 1);
  CHECK_MSG(rescue_step(&z, 1, 0, f, -600.f, 1 + RS_FALL_MS) == RS_NONE, "no safe spot yet: nothing to return to");
}

static void tb_0248_rescue_config_default_on() {
  config_defaults();
  CHECK(g_cfg.rescueFalls == 1);
  CHECK(config_key_known("mouseaim", "rescuefalls"));
}

// User decisions 2026-10-02 (gate 1): Crouch and Interact keys, appended rows; C, not Ctrl: Ctrl is a fixed freecam
// movement key (and the game's own lctrl jump), which these rows refuse like the camera's keys.
static void tb_0249_fp_bind_rows() {
  config_defaults();
  CHECK(config_bind_get(BIND_ROW_FP_CROUCH) == 'C' && config_bind_get(BIND_ROW_INTERACT) == 'E');
  CHECK(config_key_known("mouseaim", "crouchkey") && config_key_known("mouseaim", "interactkey"));
  int refused = 0;
  for (int k = 0; k < 8; ++k)
    refused += freecam_bind_refused(BIND_ROW_FP_CROUCH, FREECAM_MOVE_VK[k]) +
               freecam_bind_refused(BIND_ROW_INTERACT, FREECAM_MOVE_VK[k]);
  CHECK_MSG(refused == 16, "every fixed movement key, Ctrl included");
  CHECK(!freecam_bind_refused(BIND_ROW_FP_CROUCH, 'C') && !freecam_bind_refused(BIND_ROW_INTERACT, 'E'));
}

// Crouch: the 16/17 pre-tick's stand call (vt+0x1EC with 1.0 at duke!0x1007fbb0-0x1007fbb7) gets the walk state's
// crouch ratio 0.5 (playerstate_move_update 0x100859c6) while the key is held in first person; the eye drops with it.
static void tb_0250_fp_crouch() {
  CHECK(fp_crouch_arg(1, 1, 1, 1, 1.f) == FP_CROUCH_RATIO);
  CHECK_MSG(fp_crouch_arg(0, 1, 1, 1, 1.f) == 1.f, "another actor shares the function");
  CHECK_MSG(fp_crouch_arg(1, 0, 1, 1, 1.f) == 1.f, "another call site: the walk state's own crouch");
  CHECK(fp_crouch_arg(1, 1, 0, 1, 1.f) == 1.f && fp_crouch_arg(1, 1, 1, 0, 1.f) == 1.f);
  CHECK(fp_crouch_eye_drop(1.f, 48.f) == 0.f);
  CHECK(fabsf(fp_crouch_eye_drop(0.5f, 48.f) - 44.f) < 1e-4f);
  CHECK_MSG(fp_crouch_eye_drop(-1.f, 48.f) == fp_crouch_eye_drop(0.f, 48.f), "clamped to 0..1");
  static const unsigned char code[] = { 0x90, 0x8B, 0x16, 0x68, 0x00, 0x00, 0x80, 0x3F, 0x8B, 0xCE, 0xFF, 0x92, 0xEC,
                                        0x01, 0x00, 0x00, 0x8B, 0x07 };
  CHECK_MSG(fp_find_stand_call(code, (int)sizeof code) == 16, "the return address after call [edx+0x1ec]");
  CHECK(fp_find_stand_call(code, 10) == -1);
}

// Move anywhere Interact: one read of Up goes to the mask reader after a press; the game's own key
// value while it has the keys; our depth's 0 otherwise.
static void tb_0251_mv_interact_read() {
  CHECK(mv_up_read(0, 0, 1.f) == 0.f);
  CHECK_MSG(mv_up_read(0, 1, 0.f) == 1.f, "one tick of use, the key itself up");
  CHECK(mv_up_read(1, 0, 1.f) == 1.f && mv_up_read(1, 1, 0.f) == 0.f);
}

// First person Interact: state 3's touch scan run once with the use mask, and the g_fps switch
// held off while the scripted state it starts plays.
static void tb_0252_fp_interact() {
  unsigned m0 = 0, m4 = 0;
  fp_use_masks(0x10u | 0x200u | 0x4u, 0x8u | 0x4u, &m0, &m4);
  CHECK_MSG(m0 == (0x8u | 0x4u) && m4 == 0x4u, "an Up edge, no Down, no 0x200, the rest kept");
  CHECK(fp_hold_scripted(1, 17, 7, 100));
  CHECK(fp_hold_scripted(1, 17, 11, 100) && fp_hold_scripted(1, 17, 5, 100) && fp_hold_scripted(1, 17, 8, 100));
  CHECK_MSG(!fp_hold_scripted(1, 17, 3, 100), "back to walking: first person returns");
  CHECK_MSG(!fp_hold_scripted(0, 17, 7, 100), "only the g_fps switch's call");
  CHECK(!fp_hold_scripted(1, 3, 7, 100));
  CHECK_MSG(!fp_hold_scripted(1, 17, 7, FP_USE_HOLD_MS + 1), "a stuck state lets the switch through");
  static const unsigned char code[] = { 0x90, 0x83, 0xF8, 0x11, 0x74, 0x05, 0x83, 0xF8, 0x10, 0x75, 0x09, 0x6A, 0x03,
                                        0x8B, 0xCE, 0xE8, 0x10, 0x00, 0x00, 0x00, 0x8B };
  int target = 0;
  CHECK(fp_find_switch_call(code, (int)sizeof code, &target) == 20 && target == 20 + 0x10);
  CHECK(fp_find_switch_call(code, 12, &target) == -1);
}

// The teleport marker follows Duke's track only in normal play; First person and Move
// anywhere free him from it, so the marker places freely there.
static void tb_0253_tp_track_lock() {
  CHECK(tp_track_locked(1, 0, 0));
  CHECK(!tp_track_locked(0, 0, 0));
  CHECK_MSG(!tp_track_locked(1, 1, 0) && !tp_track_locked(1, 0, 1), "First person or Move anywhere: free placement");
}

// First person double jump: the walk state's air boost rule (playerstate_move_update
// 0x10085b2a-0x10085d32, double-jump.md section 1), mirrored for state 17, which has none.
static void tb_0254_fp_air_jump() {
  CHECK_MSG(fpj_press(100.03, 100.f, 90.f, 0.f, 0.f) == FPJ_TAKEOFF, "the ground jump's own press: not an air press");
  CHECK(fpj_press(100.3, 100.f, 90.f, 0.f, 0.f) == FPJ_BOOST);
  CHECK_MSG(fpj_press(100.15, 100.f, 90.f, 0.f, 0.f) == FPJ_GATE1, "within 0.2 s of the ground jump");
  CHECK_MSG(fpj_press(100.3, 100.f, 100.f, 0.f, 0.f) == FPJ_GATE2, "disarmed: E14 caught up with E10");
  CHECK_MSG(fpj_press(100.3, 100.f, 90.f, -51.f, 0.f) == FPJ_GATE3, "more than 50 below the jump start");
  CHECK(fpj_press(100.3, 100.f, 90.f, -50.f, 0.f) == FPJ_BOOST);
  // Sweet: |vy| within g_p_boost_sweet, or E64 > 0 while falling: the speed that reaches the jump-plus-boost apex.
  CHECK(fpj_sweet(100.f, 0.f, 150.f) && !fpj_sweet(200.f, 0.f, 150.f) && fpj_sweet(-200.f, 1.f, 150.f));
  CHECK_MSG(!fpj_sweet(200.f, 1.f, 150.f), "E64 only counts while falling");
  CHECK(fpj_velocity(300.f, 0.f, 600.f, 400.f, 150.f, 1800.f, 0.f) == 400.f);
  const float v = fpj_velocity(100.f, 0.f, 600.f, 400.f, 150.f, 1800.f, 50.f);
  CHECK(fabsf(v - sqrtf(600.f * 600.f + 400.f * 400.f - 2.f * 1800.f * 50.f)) < 1e-2f);
  float g = 0.f;
  CHECK(fpj_measure_g(578.4f, 569.4f, 0.005, &g) && fabsf(g - 1800.f) < 1.f);
  CHECK_MSG(!fpj_measure_g(-610.f, -611.f, 0.005, &g), "at terminal speed: no gravity to read");
  CHECK(!fpj_measure_g(100.f, 90.f, 0.0, &g) && !fpj_measure_g(100.f, 300.f, 0.005, &g));
}

// One interact per E press, whatever happens while it is held; a queued Move anywhere use is dropped
// once Up/Down go back to the game.
static void tb_0255_interact_edges() {
  int last = 0;
  CHECK(fp_key_edge(1, &last));
  CHECK_MSG(!fp_key_edge(1, &last), "held through a ladder or use state and back: no second press");
  CHECK(!fp_key_edge(0, &last) && fp_key_edge(1, &last));
  CHECK(mv_use_pending(1, 0) == 1);
  CHECK_MSG(mv_use_pending(1, 1) == 0, "the game has Up now: a late use would fire after the ladder");
}

// Ladders in the first-person view: state 5 walks to the ladder's grab point and state 6 climbs it
// (playerstate_ladder_pretick 0x10083b10 -> player_set_state(6); playerstate_climb_think 0x100842e0); while those or
// the use/grab states play under our pause or hold-off, our view stays.
static void tb_0256_fp_ladder_view() {
  CHECK(fp_ladder_state(5) && fp_ladder_state(6) && !fp_ladder_state(3) && !fp_ladder_state(17));
  CHECK(fp_scripted_state(6) && fp_scripted_state(7) && fp_scripted_state(11) && !fp_scripted_state(3) &&
        !fp_scripted_state(12) && !fp_scripted_state(0));
  CHECK_MSG(mv_keys_to_game(6, 0), "climbing reads Up/Down");
  CHECK_MSG(fp_view_hold(1, 1, 0, 3, 1), "the ladder pause, standing at its foot");
  CHECK_MSG(fp_view_hold(1, 0, 1, 7, 1), "the interact hold-off's use state");
  CHECK(!fp_view_hold(1, 0, 1, 17, 1) && !fp_view_hold(1, 0, 0, 7, 1) && !fp_view_hold(0, 1, 0, 6, 1));
  CHECK_MSG(!fp_view_hold(1, 1, 0, 6, 0), "no player");
  CHECK(fabsf(fp_engine_yaw_for_view(30.f, 330.f) - 330.f) < 1e-3f);
  CHECK(fabsf(fp_engine_yaw_for_view(30.f, -20.f) + 30.f) < 1e-3f);
  CHECK(fabsf(fp_engine_yaw_for_view(-90.f, 800.f) - 810.f) < 1e-3f);
}

// The hold bridges to state 17 (no game-camera frame between), and the hold-off covers the climb.
static void tb_0257_fp_view_bridge() {
  CHECK_MSG(fp_view_bridge(1, 1, 3, 10), "g_fps back on, Duke still in 3 until the tick switches him");
  CHECK_MSG(!fp_view_bridge(1, 1, 17, 10), "in 17: the normal first-person view takes over");
  CHECK(!fp_view_bridge(0, 1, 3, 10) && !fp_view_bridge(1, 0, 3, 10));
  CHECK_MSG(!fp_view_bridge(1, 1, 3, FP_BRIDGE_MS + 1), "a switch that never comes lets the game camera back");
  CHECK_MSG(fp_hold_scripted(1, 17, 6, 100), "the climb state is held off like the rest");
}

// Double jump strength: first person cannot grab ledges, so the boost we apply scales 100-150 %.
static void tb_0258_fp_jump_strength() {
  config_defaults();
  CHECK(g_cfg.fpJumpPct == 120 && config_key_known("mouseaim", "jumpboostpct"));
  CHECK(fabsf(fpj_scaled(400.f, 120) - 480.f) < 1e-3f && fpj_scaled(400.f, 100) == 400.f);
  CHECK_MSG(fpj_scaled(400.f, 300) == 600.f && fpj_scaled(400.f, 10) == 400.f, "clamped to 100..150 %");
}

// Crouch ease: the view's drop follows the engine's instant ratio over about 150 ms.
static void tb_0259_fp_crouch_ease() {
  CHECK(fp_ease_drop(0.f, 44.f, 0.f) == 0.f);
  float d = 0.f;
  for (int i = 0; i < 30; ++i) d = fp_ease_drop(d, 44.f, 5.f);   // 150 ms of 5 ms frames
  CHECK_MSG(d > 0.95f * 44.f && d <= 44.f, "about there after 150 ms");
  CHECK(fp_ease_drop(10.f, 44.f, 10000.f) == 44.f);
  CHECK_MSG(fp_ease_drop(44.f, 0.f, 5.f) < 44.f && fp_ease_drop(44.f, 0.f, 5.f) > 0.f, "standing eases too");
}

// First person through game sequences: while a scripted `camera` holds the input (the FP1 pause),
// our view stays at the player the search finds, facing where he faces plus the mouse's offset.
static void tb_0260_fp_scene_hold() {
  CHECK(fp_scene_hold(1, 1, 1));
  CHECK(!fp_scene_hold(0, 1, 1) && !fp_scene_hold(1, 0, 1));
  CHECK_MSG(!fp_scene_hold(1, 1, 0), "no player found (loading): the game's own screen");
  CHECK(fp_scene_player_ok(0x100, 0, 1));
  CHECK_MSG(!fp_scene_player_ok(0x100, 1, 1), "removed (+0x90)");
  CHECK_MSG(!fp_scene_player_ok(0x20, 0, 1) && !fp_scene_player_ok(0x100, 0, 0), "not a player, or another class");
  CHECK(fabsf(fp_scene_yaw(90.f, 0.f) + 90.f) < 1e-4f && fabsf(fp_scene_yaw(90.f, 15.f) + 75.f) < 1e-4f);
}

// The sequence search runs only on a live camera input of the player's own server, which the
// engine's spawn_get_server still finds; through the resume wait the hold continues as a view-only hold.
static void tb_0261_fp_scene_search_gate() {
  CHECK(fp_scene_search_ok(0x200, 0, 7, 1, 7, 1));
  CHECK_MSG(!fp_scene_search_ok(0x100, 0, 7, 1, 7, 1), "not a camera (menu, loader)");
  CHECK_MSG(!fp_scene_search_ok(0x200, 1, 7, 1, 7, 1), "removed (+0x90)");
  CHECK_MSG(!fp_scene_search_ok(0x200, 0, 8, 1, 7, 1), "another server than the last live player's");
  CHECK_MSG(!fp_scene_search_ok(0x200, 0, 7, 0, 7, 1), "no live player seen in this level");
  CHECK_MSG(!fp_scene_search_ok(0x200, 0, 7, 1, 7, 0), "spawn_get_server finds no server: the search would fault");
  CHECK(fp_wait_hold(1, 1, 1) && !fp_wait_hold(0, 1, 1) && !fp_wait_hold(1, 0, 1) && !fp_wait_hold(1, 1, 0));
}

static AiCand ai_c(const void* e, float d2) { AiCand c; c.ent = e; c.dist2 = d2; return c; }
static void tb_0262_ai_wake_candidates() {
  const float r2 = AI_WAKE_RADIUS * AI_WAKE_RADIUS;
  CHECK(ai_wake_eligible(1, 1, 50.f, 100.f, 0, 0, 0.f, 1));
  CHECK_MSG(!ai_wake_eligible(0, 1, 50.f, 100.f, 0, 0, 0.f, 1), "a boss or non-enemy is never woken");
  CHECK_MSG(!ai_wake_eligible(1, 0, 50.f, 100.f, 0, 0, 0.f, 1), "no monster base: no seen fields");
  CHECK_MSG(!ai_wake_eligible(1, 1, 0.f, 100.f, 0, 0, 0.f, 1), "dead (health 0)");
  CHECK_MSG(!ai_wake_eligible(1, 1, 50.f, r2 + 1.f, 0, 0, 0.f, 1), "outside the radius");
  CHECK(ai_wake_eligible(1, 1, 50.f, r2, 0, 0, 0.f, 1));
  CHECK_MSG(!ai_wake_eligible(1, 1, 50.f, 100.f, AI_FLAG_OFF, 0, 0.f, 1), "+0xB7C 0x200: dead/off");
  CHECK_MSG(!ai_wake_eligible(1, 1, 50.f, 100.f, 0, 1, 0.5f, 1), "seen 0.5 s ago: the draw or we refreshed it");
  CHECK(ai_wake_eligible(1, 1, 50.f, 100.f, 0, 1, AI_REFRESH_S, 1));
  CHECK_MSG(ai_wake_eligible(1, 1, 50.f, 100.f, 0, 1, -3.f, 1), "a clock behind +0xB74 (new level) is stale");
  CHECK_MSG(!ai_wake_eligible(1, 1, 50.f, 100.f, 0, 0, 0.f, 0), "blocked last try, retry not due");
  // Nearest first, the farthest dropped when full.
  AiCand a[3];
  int n = 0;
  int x[5];
  ai_cand_insert(a, &n, 3, ai_c(&x[0], 900.f));
  ai_cand_insert(a, &n, 3, ai_c(&x[1], 100.f));
  ai_cand_insert(a, &n, 3, ai_c(&x[2], 400.f));
  ai_cand_insert(a, &n, 3, ai_c(&x[3], 2500.f));   // farther than all with the list full: dropped
  ai_cand_insert(a, &n, 3, ai_c(&x[4], 50.f));     // nearer: evicts 900
  CHECK(n == 3);
  CHECK(a[0].ent == &x[4] && a[1].ent == &x[1] && a[2].ent == &x[2]);
  CHECK(ai_rays_this_frame(n, 2) == 2 && ai_rays_this_frame(n, 8) == 3 && ai_rays_this_frame(0, 8) == 0);
  CHECK(ai_retry_due(1000, 0) && ai_retry_due(1000, 1000) && !ai_retry_due(999, 1000));
}
static void tb_0263_ai_sight_route() {
  CHECK(ai_see_route(1, 1, 1) == AI_SEE_STOCK);
  CHECK(ai_see_route(1, 0, 0) == AI_SEE_STOCK);
  CHECK_MSG(ai_see_route(0, 0, 1) == AI_SEE_PASS, "modes off: stock answer");
  CHECK_MSG(ai_see_route(0, 1, 0) == AI_SEE_PASS, "another target: stock answer");
  CHECK(ai_see_route(0, 1, 1) == AI_SEE_CONELESS);
  // The original's own tests before its cone, kept: seen byte, 1000 range, and the hidden-player case.
  CHECK(ai_see_pre(1, 1e6f, 0));
  CHECK(!ai_see_pre(0, 10.f, 0) && !ai_see_pre(1, 1e6f + 1.f, 0) && !ai_see_pre(1, 10.f, 1));
  CHECK_MSG(ai_target_hidden(2, 1, 0), "a player (type 2, user 1) with +0xA64 == 0 is not seen");
  CHECK(!ai_target_hidden(2, 1, 5) && !ai_target_hidden(0, 1, 0) && !ai_target_hidden(2, 0, 0));
  float h[3];
  ai_see_heights(100.f, 1.f, 40.f, h);   // standing: centre, then -r*h, then +r*h
  CHECK(h[0] == 100.f && h[1] == 60.f && h[2] == 140.f);
  ai_see_heights(100.f, 0.5f, 40.f, h);  // crouched (< 0.75): starts one step down
  CHECK(h[0] == 80.f && h[1] == 60.f && h[2] == 100.f);
  CHECK(ai_see_hit_ok(1, 0, 0));
  CHECK_MSG(ai_see_hit_ok(0, 2, 0x8000), "the original also accepts a type-2 object with user 0x8000");
  CHECK(!ai_see_hit_ok(0, 2, 0x4000) && !ai_see_hit_ok(0, 0x100, 0x8000));
}
static void tb_0264_ai_reaim_keeps_speed() {
  const Vec3 from = { 0.f, 0.f, 0.f }, to = { 0.f, 300.f, 400.f }, vel = { 2000.f, 0.f, 0.f };
  Vec3 out;
  CHECK(ai_reaim(from, to, vel, &out));
  CHECK(fabsf(out.x) < 1e-3f && fabsf(out.y - 1200.f) < 0.05f && fabsf(out.z - 1600.f) < 0.05f);
  CHECK_MSG(!ai_reaim(from, from, vel, &out), "no direction: the engine's velocity stays");
  const Vec3 still = { 0.f, 0.f, 0.f };
  CHECK_MSG(!ai_reaim(from, to, still, &out), "no speed: nothing to keep");
  const Vec3 pos = { 10.f, 100.f, 20.f }, half = { 16.f, 40.f, 16.f };
  const Vec3 c = ai_chest(pos, half, 4.f);   // box centre y 96, half 36: chest = 96 + 18
  CHECK(c.x == 10.f && c.z == 20.f && fabsf(c.y - 114.f) < 1e-4f);
}
static void tb_0265_ai_gate() {
  CHECK(ai_gate(1, 0, 1) && ai_gate(0, 1, 1) && ai_gate(1, 1, 1));
  CHECK_MSG(!ai_gate(0, 0, 1), "neither mode: the feature writes nothing");
  CHECK_MSG(!ai_gate(1, 1, 0), "no player (menu, loading)");
}
static void tb_0266_ai_fingerprints() {
  // Bytes read in the Ghidra MCP, duke_base 2026-10-02: monster_can_see_target 0x100314b0.
  static const unsigned char see[AI_SEE_FP_LEN] = {
    0x64, 0xA1, 0, 0, 0, 0, 0x6A, 0xFF, 0x68, 0xE9, 0xE6, 0x11, 0x10, 0x50, 0x64, 0x89, 0x25, 0, 0, 0, 0,
    0x83, 0xEC, 0x6C, 0x53, 0x55, 0x8B, 0xAC, 0x24, 0x84, 0, 0, 0, 0x85, 0xED, 0x56, 0x57, 0x8B, 0xF1, 0x0F, 0x84,
    0xF4, 0x03, 0, 0, 0x8A, 0x86, 0x71, 0x0B, 0, 0, 0x84, 0xC0 };
  size_t at = 99;
  CHECK(offs_find(see, sizeof see, kAiSeeFp, kAiSeeMask, AI_SEE_FP_LEN, &at) == 1 && at == 0);
  // The copied prologue ends on an instruction boundary: mov eax,fs:[0] (6).
  CHECK(AI_SEE_PROLOGUE == 6);
}

static void tb_0267_mw_dinput8_version() {
  // A VS_VERSIONINFO head: key text, then the fixed block at a dword boundary (signature 0xFEEF04BD).
  unsigned char res[96];
  memset(res, 0x41, sizeof res);
  const unsigned sig = 0xFEEF04BDu, strucv = 0x00010000u, ms = 0x000A0000u, ls = 0x65F42149u;   // 10.0.26100.8521
  memcpy(res + 40, &sig, 4); memcpy(res + 44, &strucv, 4); memcpy(res + 48, &ms, 4); memcpy(res + 52, &ls, 4);
  unsigned gotMs = 0, gotLs = 0;
  CHECK(mw_fixed_version(res, sizeof res, &gotMs, &gotLs) && gotMs == ms && gotLs == ls);
  CHECK(mw_dn8_known(gotMs, gotLs));
  CHECK_MSG(!mw_dn8_known(0x000A0000u, 0x65F42148u), "any other build: the worker reads stay off");
  CHECK(!mw_dn8_known(0x00060001u, 0x65F42149u));
  CHECK_MSG(!mw_fixed_version(res, 50, &gotMs, &gotLs), "a block cut short is not read");
  unsigned char none[32] = {};
  CHECK(!mw_fixed_version(none, sizeof none, &gotMs, &gotLs));
  // The worker context (dinput8 FUN_100119aa, hook proc 0x10011df0): per type t, count +8+0x10t, exclusive +0xC+0x10t,
  // hook +0x10+0x10t; t 0 keyboard, 1 mouse. RVAs of the raw flag and the context global.
  CHECK(MW_DN8_RAW_RVA == 0x310A4u && MW_DN8_CTX_RVA == 0x3125Cu);
  CHECK(MW_DN8_CTX_MOUSE_COUNT == 0x18 && MW_DN8_CTX_MOUSE_EXCL == 0x1C && MW_DN8_CTX_MOUSE_HOOK == 0x20);
  CHECK(MW_DN8_CTX_KBD_COUNT == 0x8 && MW_DN8_CTX_KBD_EXCL == 0xC && MW_DN8_CTX_KBD_HOOK == 0x10);
}
static void tb_0268_mw_raw_rows() {
  RAWINPUTDEVICE r[3];
  memset(r, 0, sizeof r);
  r[0].usUsagePage = 1; r[0].usUsage = 2; r[0].dwFlags = 0x230;
  r[1].usUsagePage = 1; r[1].usUsage = 6; r[1].dwFlags = 0x100;
  r[2].usUsagePage = 0xC; r[2].usUsage = 1;
  char out[256];
  mw_raw_summary(r, 3, 3, out, sizeof out);
  CHECK(strstr(out, "mouse 0x230") != NULL);
  CHECK(strstr(out, "keyboard 0x100") != NULL);
  CHECK(strstr(out, "other 1") != NULL);
  mw_raw_summary(r, 0, 0, out, sizeof out);
  CHECK(strstr(out, "none") != NULL);
  mw_raw_summary(r, 0, -1, out, sizeof out);
  CHECK(strstr(out, "UNREAD") != NULL);
  mw_raw_summary(r, 2, 5, out, sizeof out);   // more registered than the buffer held: say so
  CHECK(strstr(out, "of 5") != NULL);
}
// Mouse lock (c): the GUI-thread companion line names each thread once and the GUI_* bits by name.
static void tb_0290_mw_guithread_rules() {
  const unsigned long same[4] = { 7, 7, 9, 9 };   // F, U, T, G
  int plan[4];
  mw_gui_plan(same, plan);
  CHECK_MSG(plan[0] == MW_GT_PRINT && plan[1] == 0 && plan[2] == MW_GT_PRINT && plan[3] == 2, "U is F, G is T");
  const unsigned long nofg[4] = { 0, 5, 5, 6 };   // no foreground window
  mw_gui_plan(nofg, plan);
  CHECK_MSG(plan[0] == MW_GT_SKIP && plan[1] == MW_GT_PRINT && plan[2] == 1 && plan[3] == MW_GT_PRINT, "skip, alias");
  const unsigned long all[4] = { 1, 2, 3, 4 };
  mw_gui_plan(all, plan);
  CHECK(plan[0] == MW_GT_PRINT && plan[1] == MW_GT_PRINT && plan[2] == MW_GT_PRINT && plan[3] == MW_GT_PRINT);
  char o[64];
  mw_gui_flags(0, o, sizeof o);
  CHECK(strcmp(o, "-") == 0);
  mw_gui_flags(GUI_INMOVESIZE | GUI_INMENUMODE, o, sizeof o);
  CHECK_MSG(strcmp(o, "MOVESIZE|MENU") == 0, "fixed order");
  mw_gui_flags(GUI_POPUPMENUMODE | GUI_SYSTEMMENUMODE | GUI_CARETBLINKING, o, sizeof o);
  CHECK_MSG(strcmp(o, "CARET|SYSMENU|POPUP") == 0, "caret, sysmenu, popup");
  char k[8];
  mw_keys(MW_K_ALT | MW_K_WIN, k);
  CHECK_MSG(strcmp(k, "AW") == 0, "the Windows key is W");
}
static void tb_0269_mw_menu_near_loss() {
  CHECK(mw_menu_near(10000, 10000) && mw_menu_near(9000, 10000) && mw_menu_near(11000, 10000));
  CHECK(!mw_menu_near(8999, 10000) && !mw_menu_near(11001, 10000));
  CHECK_MSG(!mw_menu_near(0, 10000) && !mw_menu_near(10000, 0), "no transition or no loss yet");
}

// The resume after a ladder applies g_fps at once, not when the 60-frame g_rpg throttle comes round.
static void tb_0270_fp_resume_applies_at_once() {
  unsigned n = 1;   // left mid-cycle by the previous apply
  CHECK_MSG(fp_check_now(0, 1, 0, 1, &n), "the resume frame reads g_rpg and applies");
  n = 1;
  CHECK_MSG(!fp_check_now(0, 1, 0, 0, &n), "a waiting refusal is still throttled");
  CHECK(fp_check_now(1, 0, 0, 0, &n));
  n = 0;
  CHECK_MSG(!fp_check_now(0, 1, 1, 1, &n), "a refused write waits for the switch to move");
}

// Leaving state 17 for a ladder or use lets the next tick snap Duke onto +0xB20, which state 17 never
// re-picks; the guard keeps +0xB1C at 0 while that snap would move him far.
static void tb_0271_fp_track_guard() {
  CHECK_MSG(fp_track_guard(1, 0, 1, 3, 1, 1, 1600.f) == FP_TRK_OFF, "the ladder pause over a far stale track");
  CHECK_MSG(fp_track_guard(1, 0, 1, 5, 1, 0, 0.f) == FP_TRK_OFF, "no verified track: the pick is unknown");
  CHECK_MSG(fp_track_guard(1, 0, 1, 3, 1, 1, 10.f) == FP_TRK_NONE, "a snap of a few units is the engine's own");
  CHECK(fp_track_guard(1, 1, 1, 6, 0, 1, 1600.f) == FP_TRK_NONE);   // already off
  CHECK(fp_track_guard(1, 0, 0, 3, 1, 1, 1600.f) == FP_TRK_NONE);   // not our hold: normal play
  CHECK_MSG(fp_track_guard(1, 1, 1, 17, 0, 1, 1600.f) == FP_TRK_CLEAR, "back in 17, whose pre-tick writes 0");
  CHECK_MSG(fp_track_guard(0, 1, 0, 3, 0, 1, 1600.f) == FP_TRK_RESTORE, "switched off: the track comes back");
  CHECK(fp_track_guard(0, 0, 0, 3, 1, 1, 1600.f) == FP_TRK_NONE);
}

// The ladder trail: the last FP_TRAIL_N frames, oldest first, and a jump in x/z (the snaps were x/z only).
static void tb_0272_fp_trail() {
  static FpTrail t;
  memset(&t, 0, sizeof t);
  for (int i = 0; i < FP_TRAIL_N + 5; ++i) {
    FpTrailRec r;
    memset(&r, 0, sizeof r);
    r.x = (float)i;
    fp_trail_push(&t, r);
  }
  CHECK(t.count == FP_TRAIL_N);
  CHECK(fp_trail_at(&t, 0).x == 5.f && fp_trail_at(&t, FP_TRAIL_N - 1).x == (float)(FP_TRAIL_N + 4));
  const Vec3 a = { 0.f, 0.f, 0.f }, fall = { 0.f, -500.f, 0.f }, snap = { 1600.f, 0.f, 0.f };
  CHECK(!fp_pos_jump(a, fall) && fp_pos_jump(a, snap));
}

// A teleport out of first person keeps Duke at the target; only the exit's re-pick asks for a track.
static void tb_0273_tp_keep_off_track() {
  CHECK_MSG(tp_keep_off_track(1, 0), "freecam teleport or Rescue in first person: stays at the target");
  CHECK_MSG(!tp_keep_off_track(0, 0), "normal play and Move anywhere (its snap detour keeps x/z): the engine's way");
  CHECK_MSG(!tp_keep_off_track(1, 1), "the exit's re-pick wants the track");
}

// The ladder pause starts on the engine's grab condition (head in the volume AND Up), and a state-3
// limbo with +0xB1C held 0 resumes first person after FP_LADDER_LIMBO_MS, latched until Up or the volume lets go.
static void tb_0274_fp_ladder_reason() {
  FpLadderGate g = { 0, 0 };
  CHECK_MSG(!fp_ladder_reason(&g, 1, 0, 0, 0, 0, 1000), "walking past a ladder does not pause");
  CHECK_MSG(fp_ladder_reason(&g, 1, 1, 0, 0, 0, 1000), "head in the volume with Up: the grab");
  CHECK(fp_ladder_reason(&g, 1, 1, 0, 1, 1, 1000 + FP_LADDER_LIMBO_MS));
  CHECK_MSG(!fp_ladder_reason(&g, 1, 1, 0, 1, 1, 1001 + FP_LADDER_LIMBO_MS), "no ladder state: back to 17");
  CHECK_MSG(!fp_ladder_reason(&g, 1, 1, 0, 0, 0, 1100 + FP_LADDER_LIMBO_MS), "latched while Up stays held");
  CHECK(!fp_ladder_reason(&g, 1, 0, 0, 0, 0, 1200 + FP_LADDER_LIMBO_MS));
  CHECK_MSG(fp_ladder_reason(&g, 1, 1, 0, 0, 0, 1300 + FP_LADDER_LIMBO_MS), "a new press grabs again");
  CHECK_MSG(fp_ladder_reason(&g, 0, 0, 6, 1, 0, 5000), "climbing holds whatever the keys");
  CHECK_MSG(fp_ladder_reason(&g, 1, 1, 0, 1, 1, 5000 + FP_LADDER_LIMBO_MS), "the limbo clock runs from the climb");
  CHECK_MSG(fp_ladder_reason(&g, 1, 1, 0, 1, 0, 9000), "+0xB1C 1 in state 3 walks: no limbo");
}

// Switched off mid-climb, +0xB1C goes back to 1 and the re-picks run only once Duke is free.
static void tb_0275_fp_restore_waits_for_state_3() {
  CHECK_MSG(fp_track_guard(0, 1, 0, 6, 0, 1, 0.f) == FP_TRK_NONE, "never 1 in a ladder state");
  CHECK(fp_track_guard(0, 1, 0, 13, 0, 1, 0.f) == FP_TRK_NONE && fp_track_guard(0, 1, 0, 7, 0, 1, 0.f) == FP_TRK_NONE);
  CHECK(fp_track_guard(0, 1, 0, 3, 1, 1, 0.f) == FP_TRK_RESTORE);
  FpState s = { 0, 1, &s };
  CHECK_MSG(fp_repick(s, &s, 6) == FP_REPICK_WAIT && fp_repick(s, &s, 5) == FP_REPICK_WAIT, "off the ladder first");
  CHECK(fp_repick(s, &s, 4) == FP_REPICK_WAIT && fp_repick(s, &s, 13) == FP_REPICK_WAIT);
  CHECK(fp_repick(s, &s, 3) == FP_REPICK_NOW);
}

static void tb_0270_ai_review_rules() {
  // L2's range uses the original's positions (vt+0x160 of monster and target), squared, against 1e6.
  const Vec3 a = { 1.f, 2.f, 3.f }, b = { 4.f, 6.f, 3.f };
  CHECK(ai_dist2(a, b) == 25.f && ai_dist2(b, a) == 25.f && ai_dist2(a, a) == 0.f);
  // An unreadable duke_base is rescanned at most once a second, not every frame.
  CHECK(ai_arm_due(0, 0) && ai_arm_due(5000, 5000) && !ai_arm_due(4999, 5000));
  CHECK(ai_arm_retry_at(1000) == 1000 + AI_ARM_RETRY_MS && AI_ARM_RETRY_MS == 1000);
}

// A hold that starts while Duke is still in 17 (the pause frame) seeds from his yaw, which our yaw
// restore and mouse look have written; the push of 17's first frame predates the restore (log: 84.9 -> 90.0).
static void tb_0276_fp_hold_seed_yaw() {
  CHECK_MSG(fp_hold_seed_yaw(1, 275.1f, 1, 90.f) == -275.1f, "in 17: the engine yaw, not the stale pose");
  CHECK_MSG(fp_hold_seed_yaw(0, 30.f, 1, 12.f) == 12.f, "a use state: the last 17 view");
  CHECK_MSG(fp_hold_seed_yaw(0, 30.f, 0, 12.f) == -30.f, "no 17 view yet: the engine yaw");
}
// After a ladder state, the pause holds while Duke is airborne until he is grounded in 3 or 17.
static void tb_0277_fp_ladder_air() {
  FpLadderAir a = { 0 };
  CHECK_MSG(!fp_ladder_air(&a, 3, 0), "a jump that did not start on a ladder");
  CHECK(fp_ladder_air(&a, 6, 0));
  CHECK_MSG(fp_ladder_air(&a, 3, 0), "jumped off the ladder: airborne in 3");
  CHECK_MSG(fp_ladder_air(&a, 9, 0), "the pull-up");
  CHECK_MSG(!fp_ladder_air(&a, 3, 1), "grounded in 3 ends it");
  CHECK(!fp_ladder_air(&a, 3, 0));
  CHECK(fp_ladder_air(&a, 5, 0));
  CHECK_MSG(!fp_ladder_air(&a, 0, 0) && !fp_ladder_air(&a, 3, 0), "death ends it");
  FpLadderGate g = { 0, 0 };
  CHECK(fp_ladder_air(&a, 6, 0));
  CHECK_MSG(fp_ladder_reason_air(&g, &a, 0, 0, 0, 3, 1, 1, 5000) && g.since == 5000, "airborne: the limbo clock waits");
  CHECK_MSG(!fp_ladder_reason_air(&g, &a, 1, 0, 0, 3, 1, 1, 6000), "grounded, no Up: first person resumes");
}
// Leaving 17 under our hold, a track the snap would move Duke more than FP_SNAP_SAFE onto (or that
// cannot be measured) is forgotten as actor_teleport does: the ladder grab and state 11's enter project their walk
// target onto +0xB20 whatever +0xB1C says (0x1008648e, 0x100873a4).
static void tb_0278_fp_track_forget() {
  CHECK(fp_track_forget(1, 1, 1, 741.3f));
  CHECK_MSG(fp_track_forget(1, 1, 0, 0.f), "unmeasured counts as stale");
  CHECK_MSG(!fp_track_forget(1, 1, 1, 10.f), "on the line: the engine's own small correction");
  CHECK(!fp_track_forget(0, 1, 1, 741.3f) && !fp_track_forget(1, 0, 0, 0.f));
  unsigned char p[0xB60];
  memset(p, 0x5A, sizeof p);
  fp_track_clear_fields(p);
  int v;
  memcpy(&v, p + 0xB20, 4); CHECK(v == 0);
  memcpy(&v, p + 0xB24, 4); CHECK(v == 0);
  memcpy(&v, p + 0xB28, 4); CHECK(v == 0);
  memcpy(&v, p + 0xB58, 4); CHECK(v == -1);
  memcpy(&v, p + 0xB5C, 4); CHECK(v == -1);
  CHECK_MSG(p[0xB1C] == 0x5A && p[0xB2C] == 0x5A && p[0xB54] == 0x5A, "nothing else written");
  CHECK(fp_exit_holding(1, 0) && fp_exit_holding(0, 1) && !fp_exit_holding(0, 0));
}
// The 17 exit sets the view to the first camera its search finds (playerstate_free_exit 0x1007fb50);
// when the view was Duke (17's own), the camera that follows him (+0xB34 == player, actor_teleport's own test) is
// put back instead.
static void tb_0279_fp_exit_view() {
  int pl, cam, other;
  CHECK(fp_exit_view_fix(&pl, &pl, &other, &cam) == &cam);
  CHECK_MSG(fp_exit_view_fix(&pl, &pl, &cam, &cam) == 0, "the exit found the right one");
  CHECK_MSG(fp_exit_view_fix(&other, &pl, &other, &cam) == 0, "a script's camera before the exit is left alone");
  CHECK_MSG(fp_exit_view_fix(&pl, &pl, &other, 0) == 0, "no following camera: nothing to do");
  CHECK(fp_cam_follows(2, 0x200, 0, &pl, &pl));
  CHECK(!fp_cam_follows(2, 0x200, 0, &cam, &pl) && !fp_cam_follows(2, 0x200, 1, &pl, &pl));
  CHECK(!fp_cam_follows(0x100, 0x200, 0, &pl, &pl) && !fp_cam_follows(2, 0x100, 0, &pl, &pl));
  // Bytes read in Ghidra (duke_base): playerstate_free_exit 0x1007fb50 and set_view_entity 0x100e55d0.
  static const unsigned char ex[] = {
    0x56, 0x8B, 0x74, 0x24, 0x08, 0x8B, 0x46, 0x78, 0x57, 0x68, 0x00, 0x02, 0x00, 0x00, 0x6A, 0x02, 0x50, 0x68, 0xA8,
    0xA3, 0x15, 0x10, 0xFF, 0x15, 0xCC, 0x3D, 0x18, 0x10, 0x8B, 0x0D, 0xDC, 0x69, 0x18, 0x10, 0x8B, 0x79, 0x08, 0x83,
    0xC4, 0x10, 0xFF, 0x15, 0x20, 0x40, 0x18, 0x10, 0x50, 0x8B, 0xCF, 0xE8, 0x4A, 0x5A, 0x06, 0x00, 0xFF, 0x15 };
  int target = -1;
  CHECK(fp_find_view_call(ex, sizeof ex, &target) == 54 && 0x1007fb50 + target == 0x100e55d0);
  static const unsigned char sv[] = {
    0x56, 0x8B, 0x74, 0x24, 0x08, 0x8B, 0x86, 0xE0, 0x00, 0x00, 0x00, 0x50, 0xFF, 0x15, 0xD8, 0x40, 0x18, 0x10, 0x8B,
    0x8E, 0xE0, 0x00, 0x00, 0x00, 0x6A, 0x01, 0x51, 0xFF, 0x15, 0x28, 0x3E, 0x18, 0x10, 0x56, 0x6A, 0x00, 0xFF, 0x15,
    0x40, 0x3D, 0x18, 0x10, 0x83, 0xC4, 0x14, 0x5E, 0xC2, 0x04, 0x00 };
  CHECK(fp_fp_is_set_view(sv) && !fp_fp_is_set_view(ex));
}
// Every bullet's first think (proj_bullet_hitscan_ray 0x1003a9c0, +0xBEC set) whose first ignore
// slot (+0xB34 on, +0xB3C) is an enemy is turned at Duke's chest; Duke's own and every later think pass.
static void tb_0280_ai_bullet_route() {
  CHECK(ai_bullet_route(1, 1, 1, 1) == AI_BUL_REAIM);
  CHECK_MSG(ai_bullet_route(0, 1, 1, 1) == AI_BUL_PASS, "fired already");
  CHECK_MSG(ai_bullet_route(1, 0, 1, 1) == AI_BUL_OTHER && ai_bullet_route(1, 1, 0, 1) == AI_BUL_OTHER, "not a monster's");
  CHECK_MSG(ai_bullet_route(1, 1, 1, 0) == AI_BUL_KEPT, "modes off");
  static const unsigned char hit[AI_HIT_FP_LEN] = {
    0x83, 0xEC, 0x18, 0x53, 0x55, 0x56, 0x8B, 0xF1, 0x8A, 0x86, 0xEC, 0x0B, 0x00, 0x00, 0x84, 0xC0, 0x57, 0x0F, 0x84,
    0x86, 0x03, 0x00, 0x00, 0xC6, 0x86, 0xEC, 0x0B, 0x00, 0x00, 0x00, 0xE8, 0x3D, 0xEF, 0xFF, 0xFF };
  size_t at = 99;
  CHECK(offs_find(hit, sizeof hit, kAiHitFp, kAiHitMask, AI_HIT_FP_LEN, &at) == 1 && at == 0);
  CHECK_MSG(AI_HIT_PROLOGUE == 8, "sub esp,0x18; push ebx; push ebp; push esi; mov esi,ecx");
}
// The pipebomb's strong throw is the walk state's Up (+0xC18 = 2 at 0x10085a70, copied to +0xC1C at
// the tick's end 0x1007c3e3, read by weap_read_owner_pose 0x10051584). First person writes it with the bomb held,
// unless crouched, and puts the old value back when it stops.
static void tb_0281_fp_pbomb_throw() {
  CHECK(fp_pbomb_want(1, 1, 0) && !fp_pbomb_want(0, 1, 0) && !fp_pbomb_want(1, 0, 0));
  CHECK_MSG(!fp_pbomb_want(1, 1, 1), "crouched: the engine's own lob");
  CHECK(fp_pbomb_upper(1, 0, 3) == FP_UP_WRITE);
  CHECK(fp_pbomb_upper(1, 1, 2) == FP_UP_NONE);
  CHECK(fp_pbomb_upper(0, 1, 2) == FP_UP_RESTORE);
  CHECK_MSG(fp_pbomb_upper(0, 1, 10) == FP_UP_NONE, "the engine wrote its own since");
  CHECK(fp_pbomb_upper(0, 0, 2) == FP_UP_NONE);
}
// Landing after a ladder, 17's first push is built before our yaw handback lands and showed the
// ladder's facing. The hold's yaw is shown until the engine's own view agrees (mod 360), for at most FP_HANDBACK_MAX.
static void tb_0282_fp_handback() {
  int left = FP_HANDBACK_MAX;
  CHECK_MSG(fp_handback(&left, 84.9f, 90.f) == 84.9f && left == FP_HANDBACK_MAX - 1, "the stale first push");
  CHECK_MSG(fp_handback(&left, 84.9f, 84.9f + 360.f) == 84.9f + 360.f && left == 0, "agrees: the engine's from now");
  CHECK(fp_handback(&left, 84.9f, 90.f) == 90.f);
  left = 1;
  CHECK(fp_handback(&left, -626.2f, 93.8f) == 93.8f && left == 0);   // -626.2 = 93.8 mod 360
  left = 2;
  CHECK(fp_handback(&left, 10.f, 200.f) == 10.f && fp_handback(&left, 10.f, 200.f) == 10.f);
  CHECK_MSG(fp_handback(&left, 10.f, 200.f) == 200.f, "never agreeing: the engine's after the cap");
}
// The handback must not be skipped: 17 is entered after the state's pre-tick, so +0xB1C is still 1 on
// the next tick's actor_track_update_if_moved; our yaw write counts as a turn and its snap re-faces Duke along the
// track (log: hold -440.6, engine view -90.0). The handback writes 17's own +0xB1C = 0 with the yaw.
static void tb_0283_fp_handback_track_off() {
  CHECK(fp_handback_track_off(17, 1));
  CHECK_MSG(!fp_handback_track_off(17, 0), "17's pre-tick already wrote it");
  CHECK_MSG(!fp_handback_track_off(3, 1), "only in 17");
}

// Borderless at launch (display.md section A, "At creation"): the first video init comes up
// borderless when the CBT hook sees the engine's first Prism3D window, before rend_init reads r_mode.
static void tb_0284_bl_fs_cvar_name() {
  char b[32];
  CHECK(bl_fs_cvar_name(b, sizeof b, "gl") && strcmp(b, "gl_fullscreen") == 0);
  CHECK(bl_fs_cvar_name(b, sizeof b, "dx8") && strcmp(b, "dx8_fullscreen") == 0);
  CHECK_MSG(!bl_fs_cvar_name(b, sizeof b, ""), "no device: no name");
  CHECK_MSG(!bl_fs_cvar_name(b, 8, "gl"), "too small a buffer is refused");
  CHECK(!bl_fs_cvar_name(b, sizeof b, 0));
}
static void tb_0285_bl_early_decide() {
  CHECK(bl_early_decide(1, 1, 0, 1) == BL_EARLY_ARM);
  CHECK_MSG(bl_early_decide(0, 1, 0, 1) == BL_EARLY_OFF, "switch off: stock launch");
  CHECK(bl_early_decide(1, 0, 0, 1) == BL_EARLY_NOAPI);
  CHECK_MSG(bl_early_decide(1, 1, 1, 1) == BL_EARLY_FULLSCREEN, "the device cvar says fullscreen");
  CHECK_MSG(bl_early_decide(1, 1, -1, 1) == BL_EARLY_NOAPI, "the device cvar unreadable");
  CHECK(bl_early_decide(1, 1, 0, 0) == BL_EARLY_NOMONITOR);
}
static void tb_0286_bl_early_state() {
  BlState s;
  BlStock st;
  memset(&s, 0, sizeof s);
  memset(&st, 0, sizeof st);
  bl_capture_mode_only(&st, "2560x1080x32");
  CHECK(st.valid && strcmp(st.mode, "2560x1080x32") == 0);
  CHECK_MSG(!bl_stock_has_window(&st), "at launch there is no stock window: a revert keeps the engine's own");
  bl_arm_early(&s);
  CHECK_MSG(bl_step(&s, 1, 1) == BL_NONE, "applied at creation: no restart");
  CHECK_MSG(!s.scene, "a later revert still waits for a scene");
  CHECK(bl_step(&s, 0, 1) == BL_REVERT);
  memset(&s, 0, sizeof s);
  bl_arm_early(&s);
  bl_early_failed(&s);
  CHECK_MSG(bl_step(&s, 1, 1) == BL_APPLY, "refused at creation: the restart path is the fallback");
  const BlRect full = { 0, -1, 3440, 1440 };
  CHECK_MSG(!bl_capture(&st, s.applied || s.reapply, 0x90000000u, 0u, full, "3440x1440x32"),
            "the fallback keeps the launch capture, never our window");
  CHECK(strcmp(st.mode, "2560x1080x32") == 0 && !bl_stock_has_window(&st));
  const BlRect rc = { 0, 0, 3446, 1459 };
  bl_capture(&st, 0, 0x14C80000u, 0x300u, rc, "3440x1440x32");
  CHECK(bl_stock_has_window(&st));
}
static void tb_0287_bl_applied_ok() {
  const BlRect win = { 0, -1, 3440, 1440 };
  BlCheck c = { 3440, 1440, 3440, 1440, 0x94000000u, win, 3440, 1441, 0 };
  CHECK(bl_applied_ok(&c, win));
  c.iconic = 1; CHECK_MSG(!bl_applied_ok(&c, win), "minimised: GLDirect's exclusive device lost focus");
  c.iconic = 0; c.clientH = 1440; CHECK_MSG(!bl_applied_ok(&c, win), "client = display mode: GLDirect's fullscreen test");
  c.clientH = 1427; CHECK_MSG(!bl_applied_ok(&c, win), "clamped client");
  c.clientH = 1441; c.style = 0x14C80000u; CHECK_MSG(!bl_applied_ok(&c, win), "captioned");
  c.style = 0x94000000u; c.modeW = 1920; CHECK_MSG(!bl_applied_ok(&c, win), "the mode is not the monitor's");
  c.modeW = 3440; c.rect.t = 0; CHECK_MSG(!bl_applied_ok(&c, win), "not at the borderless rect");
}
// The DllMain peek of [display] borderless leaves what config_load_from leaves, from the same text.
static void tb_0288_ini_peek_int() {
  static const char* const kTexts[] = {
    "[display]\r\nborderless=0\r\n",
    "[Display]\r\n  BorderLess = 0 \r\n",
    "[display]\r\nborderless=7\r\n",
    "[display]\r\nborderless=0\r\nborderless=x\r\n",
    "[display]\r\nborderless=0\r\nborderless=1\r\n",
    "[panel]\r\nborderless=0\r\n",
    "[display]\r\n; borderless=0\r\n# borderless=0\r\n",
    "[display]\r\n[panel\r\nborderless=0\r\n",
    "[display]\nborderless=0x0",
    "[display]\r\nborderless\r\n=0\r\n",
    "",
  };
  static const int kWant[] = { 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 1 };
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_peek");
  for (int i = 0; i < (int)(sizeof kTexts / sizeof kTexts[0]); i++) {
    const int peek = ini_peek_int(kTexts[i], (int)strlen(kTexts[i]), "display", "borderless", 0, 1, 1);
    write_text(path, kTexts[i]);
    config_load_from(path);
    if (peek != kWant[i] || g_cfg.borderless != kWant[i])
      printf("  text %d: peek %d, config_load_from %d, want %d\n", i, peek, g_cfg.borderless, kWant[i]);
    CHECK_MSG(peek == kWant[i] && g_cfg.borderless == kWant[i], "peek and loader agree");
  }
  DeleteFileA(path);
  const char* cut = "[display]\r\nborderless=0\r\n";
  CHECK_MSG(ini_peek_int(cut, 12, "display", "borderless", 0, 1, 1) == 1, "only n bytes are read");
}
// A failed first frame leaves the monitor r_mode set until the fallback: quit and switch-off still undo it.
static void tb_0289_bl_early_failed_is_ours() {
  BlState s;
  memset(&s, 0, sizeof s);
  CHECK(!bl_mode_ours(&s));
  bl_arm_early(&s);
  CHECK_MSG(bl_mode_ours(&s), "armed at creation: our r_mode is set");
  bl_early_failed(&s);
  CHECK_MSG(bl_mode_ours(&s), "first frame failed: ours until the fallback runs");
  CHECK_MSG(bl_step(&s, 0, 0) == BL_NONE, "switched off before a scene: the revert waits as any revert");
  CHECK_MSG(bl_step(&s, 0, 1) == BL_REVERT, "switched off before the fallback: revert");
  bl_done_revert(&s);
  CHECK(!bl_mode_ours(&s) && !s.reapply);
  CHECK(bl_step(&s, 0, 1) == BL_NONE);
  memset(&s, 0, sizeof s);
  bl_arm_early(&s);
  BlStock st;
  memset(&st, 0, sizeof st);
  CHECK(bl_mode_drift(&s, &st, "3440x1440x32", "2560x1080x32") == BL_DRIFT_REAPPLY);
  CHECK_MSG(bl_mode_ours(&s) && bl_step(&s, 0, 1) == BL_REVERT, "a re-apply pending, switched off: revert");
}

// [display] borderless_variant is gone (2026-10-01, hypothesis (d) refuted): variant 0's rect stays (tb_0287), and an
// old ini value is skipped.
static void tb_0293_config_borderless_variant_gone() {
  config_defaults();
  CHECK_MSG(!config_key_known("display", "borderless_variant"), "the key is no longer read");
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_variant");
  write_text(path, "[display]\r\nborderless_variant=2\r\nborderless=0\r\n");
  config_load_from(path);
  CHECK_MSG(g_cfg.borderless == 0, "the keys after the old one still load");
  DeleteFileA(path);
}

// Mouse lock (e): one state machine for both engine devices; only the stock level differs (keyboard 0x16 with
// NOWINKEY, mouse 5 EXCLUSIVE|FOREGROUND, input.dll 0x100011ee); both are set free at 6 NONEXCLUSIVE|FOREGROUND.
static void tb_0294_hk_level_per_kind() {
  CHECK(hk_level_for(HK_KIND_KEYBOARD, HK_SET_FREE) == 0x06 && hk_level_for(HK_KIND_KEYBOARD, HK_SET_STOCK) == 0x16);
  CHECK(hk_level_for(HK_KIND_MOUSE, HK_SET_FREE) == 0x06 && hk_level_for(HK_KIND_MOUSE, HK_SET_STOCK) == 0x05);
  CHECK(hk_level_for(HK_KIND_MOUSE, HK_NONE) == 0 && hk_level_for(HK_KIND_KEYBOARD, HK_NONE) == 0);
  CHECK(hk_stock_level(HK_KIND_KEYBOARD) == 0x16 && hk_stock_level(HK_KIND_MOUSE) == 0x05);
  int dk = 0, dm = 0, w = 0;
  HkState k, m;
  memset(&k, 0, sizeof k);
  memset(&m, 0, sizeof m);
  CHECK_MSG(hk_step(&m, 0, &dm, &w) == HK_NONE, "mouse switch off (the default): the engine's level 5 is kept");
  CHECK_MSG(hk_step(&k, 1, &dk, &w) == HK_SET_FREE, "the keyboard's switch alone");
  hk_done(&k, HK_SET_FREE, &w, 1, HK_KIND_KEYBOARD);
  CHECK_MSG(hk_step(&m, 0, &dm, &w) == HK_NONE, "the keyboard's set leaves the mouse alone");
  CHECK_MSG(hk_step(&m, 1, &dm, &w) == HK_SET_FREE, "mouse switch on: level 6");
  hk_done(&m, HK_SET_FREE, &w, 1, HK_KIND_MOUSE);
  CHECK(m.level == 0x06 && hk_step(&m, 1, &dm, &w) == HK_NONE);
  CHECK_MSG(hk_step(&m, 0, &dm, &w) == HK_SET_STOCK, "mouse switch off: back to its own level");
  hk_done(&m, HK_SET_STOCK, &w, 1, HK_KIND_MOUSE);
  CHECK_MSG(m.level == 0x05, "the mouse's stock is 5, not the keyboard's 0x16");
  hk_done(&k, HK_SET_STOCK, &w, 1);
  CHECK_MSG(k.level == 0x16, "the keyboard's default kind is unchanged");
}
// Mouse lock (e): the raw-input rows by usage, with dinput8's flags named (hotkeys.md section 2's table).
static void tb_0295_hk_raw_row_mouse() {
  RAWINPUTDEVICE rows[3];
  memset(rows, 0, sizeof rows);
  rows[0].usUsagePage = 1; rows[0].usUsage = 6; rows[0].dwFlags = 0x200;
  rows[1].usUsagePage = 1; rows[1].usUsage = 2; rows[1].dwFlags = 0x230;
  rows[2].usUsagePage = 0xC; rows[2].usUsage = 2; rows[2].dwFlags = 0x100;
  char out[96];
  hk_raw_row(rows, 3, 0, HK_RAW_MOUSE, out, sizeof out);
  CHECK_MSG(strstr(out, "flags 0x230 NOLEGACY|CAPTUREMOUSE target ") == out, out);
  hk_raw_row(rows, 3, 0, HK_RAW_KEYBOARD, out, sizeof out);
  CHECK_MSG(strstr(out, "flags 0x200 NOHOTKEYS target ") == out, out);
  rows[1].dwFlags = 0x100;
  hk_raw_row(rows, 3, 0, HK_RAW_MOUSE, out, sizeof out);
  CHECK_MSG(strstr(out, "flags 0x100 INPUTSINK target ") == out, "the nonexclusive mouse row");
  hk_raw_row(rows + 2, 1, 0, HK_RAW_MOUSE, out, sizeof out);
  CHECK_MSG(strcmp(out, "none registered (1 rows)") == 0, "page 0xC usage 2 is not the mouse");
  hk_raw_row(rows, -1, 122, HK_RAW_MOUSE, out, sizeof out);
  CHECK(strcmp(out, "unread (err 122)") == 0);
  rows[1].dwFlags = 0x2001;
  hk_raw_row(rows, 2, 0, HK_RAW_MOUSE, out, sizeof out);
  CHECK_MSG(strstr(out, "flags 0x2001 REMOVE|DEVNOTIFY target ") == out, out);
}
// [input] mouse_nonexclusive is gone (2026-10-01): the shared mouse is always on, and an old ini value is skipped.
static void tb_0296_config_mouse_nonexclusive_gone() {
  config_defaults();
  CHECK_MSG(!config_key_known("input", "mouse_nonexclusive"), "the key is no longer read");
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_mousenx");
  write_text(path, "[input]\r\nmouse_nonexclusive=0\r\n[display]\r\nwinkeys=0\r\n");
  config_load_from(path);
  CHECK_MSG(g_cfg.winKeys == 0, "the keys after the old one still load");
  DeleteFileA(path);
}

// Mouse lock fix: the OS cursor is hidden and held only while our window is in front, not minimised, and nothing of
// ours or the engine's wants it: the panel, the freecam (its own grab, screenshot mode), the engine's mouse_state 1.
static void tb_0297_cursor_hold_decision() {
  const CursorHoldIn play = { 1, 1, 0, 0, 0, 0 };
  CHECK_MSG(cursor_hold_block(play) == HOLD_OK, "in front, nothing wants the cursor: held");
  CursorHoldIn c = play;
  c.window = 0;     CHECK(cursor_hold_block(c) == HOLD_NO_WINDOW);
  c = play; c.foreground = 0; CHECK_MSG(cursor_hold_block(c) == HOLD_BACKGROUND, "alt-tab, Start, a click elsewhere");
  c = play; c.iconic = 1;     CHECK(cursor_hold_block(c) == HOLD_ICONIC);
  c = play; c.panelOpen = 1;  CHECK_MSG(cursor_hold_block(c) == HOLD_PANEL, "the panel's arrow and crosshair preview");
  c = play; c.freecam = 1;    CHECK_MSG(cursor_hold_block(c) == HOLD_FREECAM, "the freecam's grab and screenshot mode");
  c = play; c.engineCursor = 1; CHECK_MSG(cursor_hold_block(c) == HOLD_ENGINE_CURSOR, "mouse_state 1 (0x406e90)");
  c = play; c.foreground = 0; c.panelOpen = 1;
  CHECK_MSG(cursor_hold_block(c) == HOLD_BACKGROUND, "focus first: the panel over a background window is not held");
  for (int b = HOLD_OK; b <= HOLD_ENGINE_CURSOR; b++)
    CHECK_MSG(cursor_hold_name(b)[0] != '?', "every block has a name for the log");
  CHECK(strcmp(cursor_hold_name(99), "?") == 0);
}
// The hold's point: the centre of the client rect in screen pixels, l + w/2 and t + h/2; none for an empty rect.
static void tb_0298_cursor_hold_centre() {
  long x = 0, y = 0;
  CHECK(cursor_hold_centre(0, 0, 3440, 1440, &x, &y) && x == 1720 && y == 720);
  CHECK_MSG(cursor_hold_centre(0, -1, 3440, 1440, &x, &y) && x == 1720 && y == 719, "borderless: one row above");
  CHECK_MSG(cursor_hold_centre(3440, 0, 5360, 1080, &x, &y) && x == 4400 && y == 540, "a second monitor's window");
  CHECK_MSG(cursor_hold_centre(-1920, -1080, 0, 0, &x, &y) && x == -960 && y == -540, "left of and above primary");
  CHECK_MSG(cursor_hold_centre(10, 20, 13, 25, &x, &y) && x == 11 && y == 22, "odd sizes round down");
  x = y = 7;
  CHECK_MSG(!cursor_hold_centre(0, 0, 0, 1440, &x, &y) && x == 7 && y == 7, "empty: false, outputs untouched");
  CHECK(!cursor_hold_centre(0, 0, 3440, -5, &x, &y));
}

// menu.h: the Items tab is gone; Progress joins before Settings, six again.
static void tb_0299_menu_tab_rail() {
  CHECK(TAB_COUNT == 6);
  CHECK(TAB_VISUALS == 0 && TAB_COMBAT == 1 && TAB_WEAPONS == 2);
  CHECK(TAB_VIEW == 3 && TAB_PROGRESS == 4 && TAB_SETTINGS == 5);
  CHECK(!menu_tab_visible(6));
}

// menu.h: the eased fold. Opens and shuts over MENU_FOLD_MS with ease-out, turns back from
// where it is, pops under animations off, and only an open or opening fold is laid out and hit-tested.
static void tb_0300_menu_fold_ease() {
  const unsigned t = 1000;
  MenuFold f = menu_fold_init(0);
  CHECK(menu_fold_frac(f, t) == 0.0 && !menu_fold_laid_out(f, t) && !menu_fold_animating(f, t));
  f = menu_fold_step(f, 1, t, 1);
  CHECK(f.open == 1 && menu_fold_frac(f, t) == 0.0 && menu_fold_laid_out(f, t) && menu_fold_animating(f, t));
  const double mid = menu_fold_frac(f, t + MENU_FOLD_MS / 2);
  CHECK_MSG(mid > 0.5 && mid < 1.0, "ease-out: past half at half time");
  CHECK(menu_fold_frac(f, t + MENU_FOLD_MS) == 1.0 && !menu_fold_animating(f, t + MENU_FOLD_MS));
  CHECK_MSG(menu_fold_step(f, 1, t + 10, 1).t0 == t, "the same parent value does not restart it");
  MenuFold g = menu_fold_step(f, 0, t + MENU_FOLD_MS / 2, 1);
  CHECK_MSG(menu_fold_frac(g, t + MENU_FOLD_MS / 2) == mid, "turns back from where it was");
  CHECK(menu_fold_laid_out(g, t + MENU_FOLD_MS / 2));
  CHECK(menu_fold_frac(g, t + MENU_FOLD_MS / 2 + MENU_FOLD_MS) == 0.0);
  CHECK(!menu_fold_laid_out(g, t + MENU_FOLD_MS / 2 + MENU_FOLD_MS));
  CHECK_MSG(menu_fold_frac(menu_fold_step(menu_fold_init(0), 1, t, 0), t) == 1.0, "animations off pops open");
  CHECK_MSG(menu_fold_frac(menu_fold_step(menu_fold_init(1), 0, t, 0), t) == 0.0, "and pops shut");
  const MenuFold w = menu_fold_step(menu_fold_init(0), 1, 0xFFFFFFF0u, 1);
  CHECK_MSG(menu_fold_frac(w, 0xFFFFFFF0u + MENU_FOLD_MS) == 1.0, "wrap-safe clock");
  CHECK(menu_fold_height(340, 0.0) == 0 && menu_fold_height(340, 1.0) == 340);
  CHECK(menu_fold_height(340, 0.5) == 170 && menu_fold_height(341, 0.5) == 171);
  CHECK(menu_fold_height(340, 1.5) == 340 && menu_fold_height(340, -1.0) == 0 && menu_fold_height(-5, 0.5) == 0);
  CHECK_MSG(menu_fold_hit_live(f, t + MENU_FOLD_MS, false), "settled open: every row, as before");
  CHECK_MSG(menu_fold_hit_live(f, t + 10, true) && !menu_fold_hit_live(f, t + 10, false),
            "opening: only the part already drawn");
  CHECK_MSG(!menu_fold_hit_live(g, t + MENU_FOLD_MS / 2 + 10, true), "shutting: nothing");
}

// menu.h: the scroll cue. Drawn only on overflow; the thumb is the visible share of the
// track and follows the offset; the bottom fade is full while a fade's height or more lies below, gone at the end.
static void tb_0301_menu_scroll_cue() {
  CHECK(!menu_scroll_cue(600, 0, 600, 600).shown && !menu_scroll_cue(600, 0, 400, 600).shown);
  const MenuScrollCue top = menu_scroll_cue(600, 0, 1200, 600);
  CHECK(top.shown && top.thumbH == 300 && top.thumbY == 0 && top.fade == 1.0);
  const MenuScrollCue end = menu_scroll_cue(600, 600, 1200, 600);
  CHECK(end.thumbY == 300 && end.fade == 0.0);
  CHECK(menu_scroll_cue(600, 300, 1200, 600).thumbY == 150);
  CHECK_MSG(menu_scroll_cue(600, 600 - MENU_SCROLL_FADE_H / 2, 1200, 600).fade == 0.5, "eases out over the end");
  CHECK_MSG(menu_scroll_cue(600, 9999, 1200, 600).thumbY == 300, "an offset past the end is clamped");
  CHECK(menu_scroll_cue(600, 0, 100000, 600).thumbH == MENU_SCROLLBAR_MIN_THUMB);
  CHECK(!menu_scroll_cue(0, 0, 1200, 600).shown);
}

// --- UI scale ---------------------------------------
// AA's tb_0155 packing rules, its slot cases dropped (one panel atlas here), plus the white block this port packs
// last for the flat fills.
static void tb_0330_fontpack_rules() {
  for (int i = 1; i < 6; i++) {
    const long long prev = (long long)FONTPACK_LADDER[i-1].w * FONTPACK_LADDER[i-1].h;
    const long long cur  = (long long)FONTPACK_LADDER[i].w   * FONTPACK_LADDER[i].h;
    CHECK(cur > prev);
  }
  PackBox boxes[4] = { {16,16}, {16,16}, {32,8}, {8,32} };
  PackPos pos[4];
  CHECK(fontpack_shelf(boxes, 4, 512, 256, 1, pos) == true);
  for (int i = 0; i < 4; i++)
    CHECK(pos[i].x >= 0 && pos[i].y >= 0 && pos[i].x + boxes[i].w <= 512 && pos[i].y + boxes[i].h <= 256);
  for (int a = 0; a < 4; a++)
    for (int b = a + 1; b < 4; b++) {
      const int ax0 = pos[a].x, ax1 = pos[a].x + boxes[a].w, ay0 = pos[a].y, ay1 = pos[a].y + boxes[a].h;
      const int bx0 = pos[b].x, bx1 = pos[b].x + boxes[b].w, by0 = pos[b].y, by1 = pos[b].y + boxes[b].h;
      CHECK(ax1 <= bx0 || bx1 <= ax0 || ay1 <= by0 || by1 <= ay0);
    }
  PackBox huge[1] = { {4096, 4096} };
  PackPos hp[1];
  PackBox chosen = { 0, 0 };
  CHECK(fontpack_choose(huge, 1, 1, hp, &chosen) == -1);
  // Every baked glyph doubled plus 4 px each way (GDI's hinting at 2x is not an exact doubling), and the 4x4
  // white block last: 200% still packs into rung 2 (1024x512), well under the 2048 ceiling.
  static PackBox big[FONT_COUNT * 96 + 1];
  static PackPos bp[FONT_COUNT * 96 + 1];
  for (int f = 0; f < FONT_COUNT; f++)
    for (int g = 0; g < 96; g++) {
      const Glyph& gl = font_glyphs[f][g];
      big[f * 96 + g].w = gl.w > 0 ? 2 * gl.w + 4 : 0;
      big[f * 96 + g].h = gl.h > 0 ? 2 * gl.h + 4 : 0;
    }
  big[FONT_COUNT * 96].w = 4; big[FONT_COUNT * 96].h = 4;
  const int rung = fontpack_choose(big, FONT_COUNT * 96 + 1, 1, bp, &chosen);
  CHECK_MSG(rung >= 0 && rung <= 2, "200% and the white block fit rung 2");
  const PackPos wp = bp[FONT_COUNT * 96];
  CHECK(wp.x >= 1 && wp.y >= 1 && wp.x + 4 <= chosen.w && wp.y + 4 <= chosen.h);
}

static void tb_0331_atlas_crc_adler() {
  CHECK(atlas_crc32((const unsigned char*)"123456789", 9) == 0xCBF43926u);
  CHECK(atlas_crc32((const unsigned char*)"IEND", 4) == 0xAE426082u);
  CHECK(atlas_adler32((const unsigned char*)"Wikipedia", 9) == 0x11E60398u);
  CHECK(atlas_adler32((const unsigned char*)"", 0) == 1u);
}

static unsigned be32_at(const unsigned char* p) {
  return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | (unsigned)p[3];
}

// A 2x1 image byte for byte: PNG signature, IHDR (RGBA 8-bit), one IDAT holding a zlib stream of one stored
// block, IEND. White RGB, the coverage as alpha: the baked font.png's own layout (tools/gen_atlas.py).
static void tb_0332_atlas_png_layout() {
  const unsigned char alpha[2] = { 0x00, 0x80 };
  unsigned char out[128];
  CHECK(atlas_png_size(2, 1) == 77);
  const size_t n = atlas_png_encode(alpha, 2, 1, out, sizeof out);
  CHECK(n == 77);
  if (n != 77) return;
  static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
  CHECK(memcmp(out, sig, 8) == 0);
  CHECK(be32_at(out + 8) == 13 && memcmp(out + 12, "IHDR", 4) == 0);
  CHECK(be32_at(out + 16) == 2 && be32_at(out + 20) == 1);
  CHECK(out[24] == 8 && out[25] == 6 && out[26] == 0 && out[27] == 0 && out[28] == 0);
  CHECK(be32_at(out + 29) == atlas_crc32(out + 12, 17));
  CHECK(be32_at(out + 33) == 20 && memcmp(out + 37, "IDAT", 4) == 0);
  CHECK(out[41] == 0x78 && out[42] == 0x01 && ((out[41] << 8) | out[42]) % 31 == 0);
  CHECK(out[43] == 0x01 && out[44] == 9 && out[45] == 0 && out[46] == 0xF6 && out[47] == 0xFF);
  static const unsigned char raw[9] = { 0, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0x80 };
  CHECK(memcmp(out + 48, raw, 9) == 0);
  CHECK(be32_at(out + 57) == atlas_adler32(raw, 9));
  CHECK(be32_at(out + 61) == atlas_crc32(out + 37, 24));
  CHECK(be32_at(out + 65) == 0 && memcmp(out + 69, "IEND", 4) == 0 && be32_at(out + 73) == 0xAE426082u);
  CHECK_MSG(atlas_png_encode(alpha, 2, 1, out, 76) == 0, "one byte short writes nothing");
  CHECK(atlas_png_encode(alpha, 0, 1, out, sizeof out) == 0);
  CHECK(atlas_png_encode(0, 2, 1, out, sizeof out) == 0);
}

// A stored block holds at most 65535 bytes: 200x100 is 80100 raw bytes, two blocks, BFINAL on the second only.
static void tb_0333_atlas_png_two_blocks() {
  static unsigned char alpha[200 * 100];
  static unsigned char raw[100 * 801];
  for (int i = 0; i < 200 * 100; i++) alpha[i] = (unsigned char)(i * 7);
  for (int y = 0; y < 100; y++) {
    raw[y * 801] = 0;
    for (int x = 0; x < 200; x++) {
      unsigned char* p = raw + y * 801 + 1 + x * 4;
      p[0] = p[1] = p[2] = 0xFF;
      p[3] = alpha[y * 200 + x];
    }
  }
  static unsigned char out[80200];
  CHECK(atlas_png_size(200, 100) == 80173);
  const size_t n = atlas_png_encode(alpha, 200, 100, out, sizeof out);
  CHECK(n == 80173);
  if (n != 80173) return;
  CHECK(be32_at(out + 33) == 2 + 80100 + 10 + 4);
  CHECK(out[43] == 0x00 && out[44] == 0xFF && out[45] == 0xFF && out[46] == 0x00 && out[47] == 0x00);
  CHECK(memcmp(out + 48, raw, 65535) == 0);
  const unsigned char* b2 = out + 48 + 65535;
  CHECK(b2[0] == 0x01 && b2[1] == 0xE5 && b2[2] == 0x38 && b2[3] == 0x1A && b2[4] == 0xC7);
  CHECK(memcmp(b2 + 5, raw + 65535, 80100 - 65535) == 0);
  CHECK(be32_at(b2 + 5 + 14565) == atlas_adler32(raw, sizeof raw));
  CHECK(be32_at(out + 37 + 4 + 80116) == atlas_crc32(out + 37, 4 + 80116));
}

// The baked font.shd with only its source renamed: same pool, filters, blend and texenv.
static void tb_0334_atlas_shd_text() {
  char s[1024];
  const int n = atlas_shd_text("font_s130.png", s, sizeof s);
  CHECK(n > 0 && n == (int)strlen(s));
  CHECK(strncmp(s, "common {\n        ortho\n", 23) == 0);
  CHECK(strstr(s, "source          font_s130.png\n") != 0);
  CHECK(strstr(s, "minfilter       nearest\n") && strstr(s, "magfilter       nearest\n"));
  CHECK(strstr(s, "cull            none\n") && strstr(s, "texenv          modulate\n"));
  CHECK(strstr(s, "blendfunc       SRC_ALPHA           ONE_MINUS_SRC_ALPHA\n") && strstr(s, "nomips\n"));
  CHECK(strstr(s, "depthfunc       ALWAYS\n") && strstr(s, "clamp\n"));
  CHECK_MSG(atlas_shd_text("font_s130.png", s, 40) == 0, "a short buffer writes nothing");
  CHECK(atlas_shd_text(0, s, sizeof s) == 0);
  float u = 0.f, v = 0.f;
  atlas_white_uv(508, 252, 512, 256, &u, &v);
  CHECK_MSG(u == 510.f / 512.f && v == 254.f / 256.f, "the baked atlas's own white texel (canvas.cpp)");
}

static void tb_0335_canvas_atlas_step() {
  CHECK(canvas_atlas_step(100, 0, 0) == CANVAS_ATLAS_BAKED);
  CHECK(canvas_atlas_step(100, 130, 130) == CANVAS_ATLAS_BAKED);
  CHECK(canvas_atlas_step(130, 0, 0) == CANVAS_ATLAS_BUILD);
  CHECK(canvas_atlas_step(130, 130, 0) == CANVAS_ATLAS_HELD);
  CHECK(canvas_atlas_step(130, 150, 0) == CANVAS_ATLAS_BUILD);
  CHECK_MSG(canvas_atlas_step(130, 150, 130) == CANVAS_ATLAS_REFUSED, "a refused scale is not retried");
  CHECK(canvas_atlas_step(150, 0, 130) == CANVAS_ATLAS_BUILD);
  CHECK(canvas_atlas_step(250, 200, 0) == CANVAS_ATLAS_HELD);
  CHECK(canvas_atlas_step(60, 0, 0) == CANVAS_ATLAS_BUILD);
  CHECK(canvas_atlas_step(60, 75, 0) == CANVAS_ATLAS_HELD);
}

static void tb_0336_ui_scale_settled_and_px() {
  CHECK(ui_scale_settled(130, 140) == 130);
  CHECK(ui_scale_settled(150, 140) == 140);
  CHECK(ui_scale_settled(300, 200) == 200);
  CHECK(ui_scale_settled(50, 200) == 75);
  CHECK(ui_scale_settled(130, 75) == 75);
  CHECK(ui_scale_settled(100, ui_scale_max_for(1440, PANEL_H, PANEL_VINSET)) == 100);
  // The faces fontgen asks GDI for: Bahnschrift 21/16 and Cascadia Mono 18 at the scale.
  CHECK(ui_scale_px(21, 130) == 27 && ui_scale_px(16, 130) == 21 && ui_scale_px(18, 130) == 23);
  CHECK(ui_scale_px(21, 200) == 42 && ui_scale_px(16, 200) == 32 && ui_scale_px(18, 200) == 36);
  CHECK(ui_scale_px(21, 75) == 16 && ui_scale_px(16, 75) == 12 && ui_scale_px(18, 75) == 14);
  CHECK(ui_unscale_px(ui_scale_px(400, 130), 130) == 400);
  CHECK_MSG(config_key_known("panel", "uiscale"), "AA's key, read and written");
}

// The read-back that proves the VFS serves the atlas as written, and what it reports when not: bytes read and the
// first offset that differs (n when the file runs past what was written).
static void tb_0337_atlas_readback() {
  const unsigned char want[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  AtlasReadBack r;
  atlas_readback_begin(&r);
  atlas_readback_step(&r, want, 8, want, 5);
  atlas_readback_step(&r, want, 8, want + 5, 3);
  CHECK(r.at == 8 && r.firstDiff == -1 && atlas_readback_ok(&r, 8));
  atlas_readback_begin(&r);
  atlas_readback_step(&r, want, 8, want, 5);
  CHECK_MSG(r.at == 5 && r.firstDiff == -1 && !atlas_readback_ok(&r, 8), "a short file is not a match");
  const unsigned char bad[8] = { 1, 2, 3, 4, 5, 9, 7, 9 };
  atlas_readback_begin(&r);
  atlas_readback_step(&r, want, 8, bad, 4);
  atlas_readback_step(&r, want, 8, bad + 4, 4);
  CHECK_MSG(r.at == 8 && r.firstDiff == 5 && !atlas_readback_ok(&r, 8), "the first differing offset, across chunks");
  const unsigned char longer[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 0, 0 };
  atlas_readback_begin(&r);
  atlas_readback_step(&r, want, 8, longer, 10);
  CHECK_MSG(r.at == 10 && r.firstDiff == 8 && !atlas_readback_ok(&r, 8), "a longer file differs at n");
  atlas_readback_begin(&r);
  CHECK_MSG(!atlas_readback_ok(&r, 8), "nothing read is not a match");
}

// The UI scale reaches 200% on every screen at least 766 px tall (it was 674 px before the sixth tab). The ceiling is
// the largest scale at which the least panel fits (chrome PANEL_CHROME_MIN_H tall, PANEL_MIN_W wide). A taller stored
// panel draws capped at the applied scale, its content scrolling; the stored box is not written.
static void tb_0338_ui_scale_ceiling() {
  CHECK_MSG(ui_scale_max_for(1440, 1300, PANEL_VINSET) == 105, "the reported case under the stored-height rule");
  CHECK(PANEL_CHROME_MIN_H == 323);
  CHECK_MSG(PANEL_CHROME_MIN_H - (2 + STRIP_H + STATUS_H) >= TAB_COUNT * MENU_RAIL_ENTRY_H, "the whole rail fits");
  CHECK_MSG((PANEL_CHROME_MIN_H - (2 + STRIP_H + STATUS_H) - 2 * PAD) / (ROW_H + ROW_GAP) >= 3,
            "the body beside the rail holds at least three rows");
  CHECK(ui_scale_ceiling(3440, 1440) == 200);
  CHECK(ui_scale_ceiling(2560, 1440) == 200);
  CHECK(ui_scale_ceiling(1920, 1080) == 200);
  CHECK_MSG(ui_scale_ceiling(1280, 720) == 185, "720 px is now short of 200%");
  CHECK_MSG(ui_scale_ceiling(1920, 766) == 200, "the shortest screen that reaches 200%");
  CHECK(ui_scale_ceiling(1920, 765) == 195);
  CHECK_MSG(ui_scale_ceiling(1024, 768) == 180, "a narrow screen: PANEL_MIN_W 560 decides");
  CHECK_MSG(ui_scale_ceiling(1920, 240) == UI_SCALE_MIN, "a short screen floors at the minimum");
  CHECK(ui_panel_h_cap(1440, 200) == 660);
  CHECK(ui_panel_h_cap(1080, 200) == 480);
  CHECK_MSG(ui_panel_h_cap(600, 200) == PANEL_CHROME_MIN_H, "past the ceiling the chrome floor holds");
  const int pcts[] = { 75, 100, 130, 165, 200 };
  for (int i = 0; i < 5; i++) {
    const int cap = ui_panel_h_cap(1440, pcts[i]);
    CHECK_MSG(cap >= PANEL_CHROME_MIN_H, "the cap never squeezes the panel under the chrome");
    CHECK_MSG(cap == 0x7fffffff || ui_scale_max_for(1440, cap, PANEL_VINSET) >= pcts[i],
              "a panel at its cap fits the screen at that scale");
  }
  // A tall stored panel at 200% on 3440x1440 (1720x720 logical): drawn 660 tall, as near its stored place as fits.
  const PanelBox stored = { 60, 70, 620, 1300 };
  const PanelBox d = menu_panel_drawn(stored, ui_unscale_px(3440, 200), ui_unscale_px(1440, 200),
                                      ui_panel_h_cap(1440, 200));
  CHECK(d.h == 660 && d.y == 60 && d.x == 60 && d.w == 620);
  const PanelBox d1080 = menu_panel_drawn(stored, 960, 540, ui_panel_h_cap(1080, 200));
  CHECK(d1080.h == 480 && d1080.y + d1080.h <= 540);
  // Width: at a narrow screen's ceiling the panel is no wider than the logical screen and no narrower than the floor.
  const int pctN = ui_scale_ceiling(1024, 768);
  const PanelBox unset = { -1, -1, 0, 0 };
  const PanelBox dn = menu_panel_drawn(unset, ui_unscale_px(1024, pctN), ui_unscale_px(768, pctN),
                                       ui_panel_h_cap(768, pctN));
  CHECK(dn.w >= PANEL_MIN_W && dn.x + dn.w <= ui_unscale_px(1024, pctN));
  const PanelBox d100 = menu_panel_drawn(unset, 2560, 1440, ui_panel_h_cap(1440, 100));
  CHECK_MSG(d100.h == PANEL_H && d100.w == PANEL_W, "at 100% the default box is unchanged");
  CHECK(ui_scale_step(137) == 135 && ui_scale_step(138) == 140 && ui_scale_step(199) == 200);
  CHECK(ui_scale_step(76) == 75 && ui_scale_step(300) == 200 && ui_scale_step(-5) == 75);
  CHECK_MSG(ui_scale_settled(137, 200) == 135, "an old stored value settles on a step");
}

// Every slider row in the panel, as menu.cpp draws it: its tab, label, indent, the widest readout it can show and
// whether it has a reset glyph (a named slider has none).
static int tw(const char* s, int font) {
  int w = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++)
    if (*p >= 0x20 && *p <= 0x7F) w += font_glyphs[font][*p - 0x20].adv;
  return w;
}
static const char* widest_label(const char* (*fn)(int), int lo, int hi) {
  const char* best = fn(lo);
  for (int v = lo + 1; v <= hi; v++) if (tw(fn(v), FONT_MONO) > tw(best, FONT_MONO)) best = fn(v);
  return best;
}
static const char* cap_widest() {
  static char a[24], b[24];
  menu_cap_readout(a, sizeof a, 240, 0);
  menu_cap_readout(b, sizeof b, 240, 1);
  return tw(a, FONT_MONO) >= tw(b, FONT_MONO) ? a : b;
}
struct SliderInv { int tab; const char* label; int indent; char reserve[24]; int hasReset; };
static int slider_inventory(SliderInv* out) {
  int n = 0;
  struct Num { int tab; const char* label; int indent; int hi; const char* suffix; };
  static const Num nums[] = {
    { TAB_COMBAT, "Delay", SUB_INDENT, TBOT_DELAY_MS_MAX, " ms" },
    { TAB_COMBAT, "Aim range", 0, AIM_RANGE_MAX_M, " m" },
    { TAB_COMBAT, "Lead amount", SUB_INDENT, 100, "%" },
    { TAB_VIEW, "Flight speed", SUB_INDENT, FREECAM_SPEED_MAX_PCT, "%" },
    { TAB_VIEW, "Distance", 0, ZOOMOUT_MAX_M, " m" },
    { TAB_VIEW, "Sensitivity", SUB_INDENT, MA_SENS_MAX, "%" },
    { TAB_VIEW, "Reticle size", SUB_INDENT, MA_RET_SIZE_MAX, " px" },
    { TAB_VIEW, "Reticle thickness", SUB_INDENT, MA_RET_THICK_MAX, " px" },
    { TAB_VIEW, "Double jump strength", SUB_INDENT, 150, "%" },
    { TAB_VIEW, "Photo size", SUB_INDENT, PHOTO_MULT_MAX, "x" },
  };
  for (unsigned i = 0; i < sizeof nums / sizeof nums[0]; i++) {
    SliderInv& r = out[n++];
    r.tab = nums[i].tab; r.label = nums[i].label; r.indent = nums[i].indent; r.hasReset = 1;
    sprintf_s(r.reserve, "%d%s", nums[i].hi, nums[i].suffix);
  }
  struct Fixed { int tab; const char* label; int indent; const char* reserve; int hasReset; };
  const Fixed fixed[] = {
    { TAB_COMBAT, "Fire when", SUB_INDENT, widest_label(tbot_when_label, TBOT_WHEN_CROSSHAIR, TBOT_WHEN_LOCKED), 0 },
    { TAB_COMBAT, "Aim at", 0, widest_label(aim_point_label, AIM_AT_BODY, AIM_AT_HEAD), 0 },
    { TAB_WEAPONS, "Fire rate", 0, "400%", 1 },
    { TAB_WEAPONS, "Reload speed", 0, "1000%", 1 },
    { TAB_VIEW, "Field of view", SUB_INDENT, FOV_READOUT_RESERVE, 1 },
    { TAB_VIEW, "Reticle colour", SUB_INDENT, widest_label(ma_reticle_colour_label, 0, MA_RET_COLOURS - 1), 0 },
    { TAB_SETTINGS, "Cap", SUB_INDENT, cap_widest(), 1 },
    { TAB_SETTINGS, "UI scale", 0, "200%", 1 },
  };
  for (unsigned i = 0; i < sizeof fixed / sizeof fixed[0]; i++) {
    SliderInv& r = out[n++];
    r.tab = fixed[i].tab; r.label = fixed[i].label; r.indent = fixed[i].indent; r.hasReset = fixed[i].hasReset;
    strncpy_s(r.reserve, sizeof r.reserve, fixed[i].reserve, _TRUNCATE);
  }
  return n;
}

// One track left edge and one track width for every slider row on every tab, sub-rows included, at the default
// panel, the narrowest and a narrow screen's at its ceiling; every label clears the track.
static void tb_0339_slider_geometry_uniform() {
  SliderInv inv[32];
  const int n = slider_inventory(inv);
  CHECK(n == 18);
  const int panelWs[] = { PANEL_W, PANEL_MIN_W, ui_unscale_px(1024, ui_scale_ceiling(1024, 768)) };
  for (int pi = 0; pi < 3; pi++) {
    const int rowW = panelWs[pi] - RAIL_W - 2 * PAD - 2;
    MenuSliderGeom first = { 0, 0, 0, 0, 0 };
    for (int i = 0; i < n; i++) {
      const MenuSliderGeom g = menu_slider_geom(rowW, tw(MENU_READOUT_RESERVE, FONT_MONO));
      if (i == 0) first = g;
      char msg[160];
      sprintf_s(msg, "panel %d, tab %d %s: x=%d w=%d readout=%d+%d (first row x=%d w=%d)", panelWs[pi], inv[i].tab,
                inv[i].label, g.trackX, g.trackW, g.readoutX, g.readoutW, first.trackX, first.trackW);
      CHECK_MSG(g.trackX == first.trackX && g.trackW == first.trackW && g.readoutX == first.readoutX, msg);
      CHECK_MSG(tw(inv[i].reserve, FONT_MONO) <= g.readoutW, msg);
      CHECK_MSG(inv[i].indent + tw(inv[i].label, FONT_UI) + TRACK_GAP <= g.trackX, msg);
    }
  }
  // The default panel: one 100 px track at x=233, the readout column 99 wide (nine mono cells) right after it.
  const MenuSliderGeom d = menu_slider_geom(PANEL_W - RAIL_W - 2 * PAD - 2, tw(MENU_READOUT_RESERVE, FONT_MONO));
  CHECK(d.trackX == 233 && d.trackW == 100 && d.readoutX == 365 && d.readoutW == 99);
  CHECK_MSG(d.glyphX >= d.trackX + d.trackW && d.glyphX + RESET_W <= d.readoutX, "the glyph sits between them");
  // The narrowest panel still gives the minimum track.
  CHECK(menu_slider_geom(PANEL_MIN_W - RAIL_W - 2 * PAD - 2, tw(MENU_READOUT_RESERVE, FONT_MONO)).trackW == TRACK_MIN_W);
  // A wider readout cell (a scaled atlas rounds the mono advance) moves every row's track together.
  CHECK(menu_slider_geom(474, 108).trackX == 233 && menu_slider_geom(474, 108).trackW == 91);
  char cap[24];
  menu_cap_readout(cap, sizeof cap, 144, 1);
  CHECK_MSG(strcmp(cap, "144 fps") == 0, "following the monitor reads fps too (user, 2026-10-01)");
  menu_cap_readout(cap, sizeof cap, 60, 0);
  CHECK(strcmp(cap, "60 fps") == 0);
}

// The capped panel moves, rests and opens where the cap puts it: the drag lays out
// with the cap, so a press and a click that moves nothing leave the box where it is, and an unset box centres.
static void tb_0340_capped_panel_drag() {
  const int W = 1720, H = 720, cap = 660;   // 3440x1440 at 200%
  const PanelBox stored = { 60, 60, 620, 1300 };
  const PanelBox laid = menu_panel_drawn(stored, W, H, cap);
  CHECK(laid.y == 60 && laid.h == 660);
  PanelDrag d = { PDRAG_IDLE, 0, 0, { 0, 0, 0, 0 } };
  PanelBox box; bool ended = false;
  d = panel_drag_step(d, true, true, true, false, 300, 70, laid, W, H, &box, &ended, cap);
  CHECK(d.phase == PDRAG_MOVE && box.y == 60);
  d = panel_drag_step(d, false, true, true, false, 300, 70, laid, W, H, &box, &ended, cap);
  CHECK_MSG(box.x == laid.x && box.y == laid.y && box.w == laid.w && box.h == laid.h,
            "a held press that moves nothing leaves the box as laid, so the click stores nothing");
  d = panel_drag_step(d, false, true, true, false, 300, 40, laid, W, H, &box, &ended, cap);
  CHECK_MSG(box.y == 30 && box.h == 660, "a move at the cap follows the mouse");
  d = panel_drag_step(d, false, false, true, false, 300, 40, laid, W, H, &box, &ended, cap);
  CHECK(ended && d.phase == PDRAG_IDLE);
  // A resize under a cap below PANEL_MIN_H changes the width only, in place.
  d.phase = PDRAG_IDLE;
  const PanelBox g = laid;
  d = panel_drag_step(d, true, true, false, true, g.x + g.w - 1, g.y + g.h - 1, laid, W, H, &box, &ended, cap);
  d = panel_drag_step(d, false, true, false, true, g.x + g.w + 39, g.y + g.h - 101, laid, W, H, &box, &ended, cap);
  CHECK_MSG(box.h == 660 && box.y == 60 && box.w == 660, "a resize at the cap keeps the height and the place");
  // An unset box at the cap is centred.
  const PanelBox unset = { -1, -1, 0, 0 };
  const PanelBox c = menu_panel_drawn(unset, W, H, cap);
  CHECK(c.h == 660 && c.y == 30);
  const PanelBox c1080 = menu_panel_drawn(unset, 960, 540, ui_panel_h_cap(1080, 200));
  CHECK(c1080.h == 480 && c1080.y == 30);
  // The entry field covers the readout column, the glyph slot and the margin, never the track.
  const int fieldW = tw(MENU_ENTRY_WIDEST, FONT_MONO) + 2 * FIELD_PAD;
  const int rowWs[] = { PANEL_W - RAIL_W - 2 * PAD - 2, PANEL_MIN_W - RAIL_W - 2 * PAD - 2 };
  for (int i = 0; i < 2; i++) {
    const MenuSliderGeom s = menu_slider_geom(rowWs[i], tw(MENU_READOUT_RESERVE, FONT_MONO));
    CHECK(rowWs[i] - fieldW >= s.trackX + s.trackW);
  }
}

// Assault Rifle fire rate (weapons.md section 2): the round count is ftol(+0xC28 * elapsed) once
// elapsed >= +0xCE8, so +0xC28 must scale with m and rps * interval must never fall below 1.
static void tb_0341_weap_arifle_rps_and_interval() {
  CHECK(weap_arifle_rps(12, 1.f) == 12);
  CHECK(weap_arifle_rps(12, 1.5f) == 18);
  CHECK(weap_arifle_rps(12, 4.f) == 48);
  CHECK(weap_arifle_rps(7, 1.5f) == 11);           // 10.5 rounds to 11
  CHECK(weap_arifle_rps(1, 0.1f) == 1);            // never below one round a second
  CHECK(weap_arifle_interval_for(12) == 1.f / 12.f);
  for (int pct = WEAP_FIRE_MIN; pct <= WEAP_FIRE_MAX; pct += WEAP_STEP) {
    const int rps = weap_arifle_rps(12, weap_mult(pct));
    CHECK(rps == 12 * pct / 100);
    const float iv = weap_arifle_interval_for(rps);
    CHECK((float)rps * iv >= 1.f);
    CHECK((double)rps * (double)iv >= 1.0);
    CHECK(iv - 1.f / (float)rps < 1e-6f);
  }
}
static void tb_0342_weap_hold_int() {
  WeapHoldI h = {};
  int out = -1;
  CHECK(weap_holdi_step(&h, 1, 0, 18, &out) == WH_NONE && !h.applied);   // no stock yet
  weap_holdi_capture(&h, 0);
  CHECK(!h.captured);
  weap_holdi_capture(&h, 12);
  CHECK(h.captured && h.stock == 12);
  CHECK(weap_holdi_step(&h, 1, 12, 18, &out) == WH_WRITE && out == 18 && h.applied);
  weap_holdi_capture(&h, 18);                       // our own write never becomes the stock
  CHECK(h.stock == 12);
  CHECK(weap_holdi_step(&h, 1, 18, 18, &out) == WH_NONE);
  CHECK(weap_holdi_step(&h, 0, 18, 0, &out) == WH_WRITE && out == 12 && !h.applied);
  CHECK(weap_holdi_step(&h, 0, 12, 0, &out) == WH_NONE);
}

// Engine model of a trigger tap (weap_arifle_fire_logic 0x10052800, weap_arifle_think 0x10052330): a held frame sets
// the burst count +0xC2C = 5; the gate passes while held or +0xC2C > 0; each fire frame fires ftol(rps * elapsed)
// rounds, keeps the remainder in B18; state 6 back-dates B18 by interval + 0.001 (FUN_10052440). Returns rounds fired.
static int arifle_tap_rounds(int rps, float iv, int fps, int held) {
  const float dt = 1.f / (float)fps;
  float now = 1.f, b18 = now - (iv + 0.001f);
  int burst = 0, total = 0;
  for (int f = 0; f < 4 * fps; ++f, now += dt) {
    const int trig = f < held;
    if (trig) burst = 5;
    const float elapsed = now - b18;
    if (elapsed >= iv && (trig || burst > 0)) {
      const int r = (int)((double)rps * (double)elapsed);
      if (r == 0) return -1;              // the no-ammo branch: the weapon switch the user saw
      b18 = now - (elapsed - (float)r * iv);
      burst -= r;
      total += r;
    } else if (!trig && burst < 1) {
      b18 = now;
    }
  }
  return total;
}
static void tb_0343_weap_arifle_burst_same_count_faster() {
  // A tap (one frame) or a release mid-stream leaves the same rounds as stock at every step, as
  // long as the frame rate keeps to one round a frame (fps >= rps).
  const int stockTap = arifle_tap_rounds(12, 1.f / 12.f, 60, 1);
  const int stockHold = arifle_tap_rounds(12, 1.f / 12.f, 60, 30);
  CHECK(stockTap == 5);
  for (int pct = WEAP_FIRE_MIN; pct <= WEAP_FIRE_MAX; pct += WEAP_STEP) {
    const int rps = weap_arifle_rps(12, weap_mult(pct));
    const float iv = weap_arifle_interval_for(rps);
    CHECK(arifle_tap_rounds(rps, iv, 60, 1) == stockTap);
    CHECK(arifle_tap_rounds(rps, iv, 144, 1) == stockTap);
    CHECK(arifle_tap_rounds(rps, iv, 60, 30) >= stockHold);   // more rounds in the same half second
  }
  // The bug: stock rps with a scaled interval fires nothing on the first frame.
  CHECK(arifle_tap_rounds(12, 1.f / 18.f, 60, 1) == -1);
}

// The helicopter's parts take damage only through porkchild_think duke!0x10010ed0's per-part gates; the
// core never does (aim.md section 7, user report 2026-10-02: silent aim shot the core).
static void tb_0344_pork_parts_open() {
  PorkFacts p;
  p.health = 1000.f; p.active = 1; p.state = 1; p.dropPigHealth = 500.f; p.pigsLeft = 4;
  p.vulcanHealth = 200.f; p.vulcanOut = 1;
  CHECK_MSG(pork_part_open(PORK_PART_WING, p) == 1, "the wing first, while health is above drop_pig_health");
  CHECK_MSG(pork_part_open(PORK_PART_ENGINE, p) == 0, "the engine waits for every pig to drop");
  CHECK_MSG(pork_part_open(PORK_PART_VULCAN, p) == 0, "the gun only in state 3");
  p.state = 3;
  CHECK(pork_part_open(PORK_PART_VULCAN, p) == 1);
  p.vulcanOut = 0;
  CHECK_MSG(pork_part_open(PORK_PART_VULCAN, p) == 0, "the gun is not out");
  p.vulcanOut = 1; p.vulcanHealth = 0.f;
  CHECK_MSG(pork_part_open(PORK_PART_VULCAN, p) == 0, "the gun is broken");
  p.health = 500.f;
  CHECK_MSG(pork_part_open(PORK_PART_WING, p) == 0, "at drop_pig_health the wing is done");
  p.pigsLeft = 0;
  CHECK_MSG(pork_part_open(PORK_PART_ENGINE, p) == 1, "no pigs left: the engine");
  p.pigsLeft = -1;
  CHECK(pork_part_open(PORK_PART_ENGINE, p) == 1);
  p.health = 0.f;
  CHECK_MSG(pork_part_open(PORK_PART_ENGINE, p) == 0, "a dead helicopter");
  p.health = 300.f; p.active = 0;
  CHECK_MSG(pork_part_open(PORK_PART_ENGINE, p) == 0, "inactive: nothing takes damage");
  CHECK(pork_part_open(PORK_PART_WING, p) == 0);
  p.active = 1;
  CHECK_MSG(pork_part_open('x', p) == 0, "an unknown part");
  CHECK_MSG(pork_core_open() == 0, "the core's vt+0x198 0x100094a0 takes no health");
  const char* n = pork_part_name(PORK_PART_ENGINE);
  CHECK(n && strcmp(n, "Helicopter Engine") == 0);
  n = pork_part_name(PORK_PART_WING);
  CHECK(n && strcmp(n, "Helicopter Wing") == 0);
  n = pork_part_name(PORK_PART_VULCAN);
  CHECK(n && strcmp(n, "Helicopter Gun") == 0);
  CHECK(pork_part_name('x') == 0);
  CHECK("porkchopper_engine"[PORK_PART_LETTER_AT] == PORK_PART_ENGINE);
  CHECK("porkchopper_vulcan"[PORK_PART_LETTER_AT] == PORK_PART_VULCAN);
  CHECK("porkchopper_wing"[PORK_PART_LETTER_AT] == PORK_PART_WING);
}
// The other bosses' own gates (aim.md section 7).
static void tb_0345_boss_gates_open() {
  CHECK(queen_open(100.f, 1) == 1);
  CHECK_MSG(queen_open(100.f, 0) == 0, "+0x16B8 clear between phases");
  CHECK(queen_open(0.f, 1) == 0);
  CHECK(mechhead_open(1, 0) == 1);
  CHECK(mechhead_open(3, 1) == 1);
  CHECK(mechhead_open(6, 0) == 1);
  const int shut[5] = { 2, 4, 5, 7, 8 };
  for (int i = 0; i < 5; ++i) CHECK_MSG(mechhead_open(shut[i], 0) == 0, "a refused head state");
  CHECK_MSG(mechhead_open(1, 2) == 0, "+0x10C0 == 2");
  CHECK(wozma_open(1) == 1);
  CHECK(wozma_open(0) == 0);
  CHECK(strcmp(boss_imm_label(BOSS_IMM_PORK_CORE), "porkCore") == 0);
  CHECK(strcmp(boss_imm_label(BOSS_IMM_COUNT), "?") == 0);
}
// An actor that cannot take damage now still ends the bullet's budget (proj_bullet_damage_hit 0x1003ad90
// calls vt+0x194, then budget -1): in front of a part, the helicopter core stops the shot.
static void tb_0346_aim_obstacle_immune_blocks() {
  AimObsFacts f;
  memset(&f, 0, sizeof f);
  f.type = 3; f.user = 0x2007; f.actorDamages = 1;
  CHECK(aim_obstacle_kind(f) == AIM_OBS_DESTRUCTIBLE);
  f.immuneNow = 1;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_BLOCK, "an immune boss in front blocks");
  f.type = 1;
  f.handler = AIM_H_BREAKABLE; f.breakArmed = 1;
  CHECK_MSG(aim_obstacle_kind(f) == AIM_OBS_DESTRUCTIBLE, "immuneNow is only an actor's fact");
}
// Only the helicopter keeps its damage in part entities (aim.md section 7 table, read 2026-10-02): the
// queen's init 0x10012aa0 and Wozma's 0x10017330 spawn nothing, the Robo Duke body's 0x100013e0 only its
// ray, the head's 0x10002330 only jet_exhaust; the parts' back link is child+0xB1C (boss init 0x10006e30).
static void tb_0347_boss_parts_spec() {
  const BossPartsSpec* s = boss_parts_spec("boss_porkchopper");
  CHECK(s != 0);
  if (s) {
    CHECK(s->firstSlot == 0x15B8 && s->count == 3 && s->backLink == 0xB1C && s->partSpan == 0xB38);
    CHECK_MSG(s->firstSlot + 4u * (unsigned)s->count == 0x15C4, "the boss span ends after the last slot");
  }
  const char* none[] = { "boss_queenroach", "boss_mechnukem", "boss_mechnukem_head", "mechnukem_crane",
                         "boss_wozma", "boss_porkchopper_child", "monst_pigoid1", "" };
  for (int i = 0; i < 8; ++i) CHECK_MSG(boss_parts_spec(none[i]) == 0, "no parts");
  CHECK(boss_parts_spec(0) == 0);
}
// One kind per boss entity and per helicopter part, for the per-boss counts (as immuneBy is per reason).
static void tb_0348_boss_kinds() {
  CHECK(boss_kind_of("boss_porkchopper") == BOSS_K_PORK_CORE);
  CHECK(boss_kind_of("boss_queenroach") == BOSS_K_QUEEN);
  CHECK(boss_kind_of("boss_mechnukem") == BOSS_K_MECH_BODY);
  CHECK(boss_kind_of("boss_mechnukem_head") == BOSS_K_MECH_HEAD);
  CHECK(boss_kind_of("mechnukem_crane") == BOSS_K_CRANE);
  CHECK(boss_kind_of("boss_wozma") == BOSS_K_WOZMA);
  CHECK_MSG(boss_kind_of("boss_porkchopper_child") == BOSS_K_NONE, "exact names only");
  CHECK(boss_kind_of("monst_pigoid1") == BOSS_K_NONE);
  CHECK(boss_kind_of(0) == BOSS_K_NONE);
  CHECK(boss_kind_of_part(PORK_PART_WING) == BOSS_K_PORK_WING);
  CHECK(boss_kind_of_part(PORK_PART_ENGINE) == BOSS_K_PORK_ENGINE);
  CHECK(boss_kind_of_part(PORK_PART_VULCAN) == BOSS_K_PORK_GUN);
  CHECK(boss_kind_of_part('x') == BOSS_K_NONE);
  CHECK(strcmp(boss_kind_label(BOSS_K_MECH_HEAD), "roboHead") == 0);
  CHECK(strcmp(boss_kind_label(BOSS_K_PORK_GUN), "gun") == 0);
  CHECK(strcmp(boss_kind_label(BOSS_K_COUNT), "?") == 0);
  for (int k = 1; k < BOSS_K_COUNT; ++k) CHECK_MSG(boss_kind_label(k)[0] != '?', "every kind is named");
}
// Silent aim and both Triggerbot modes take only a live enemy or boss whose own gate does not refuse now:
// the Robo Duke body and the crane (vt+0x198 = a bare RET 0x14, 0x10001c40) are refused like an immune boss.
static void tb_0349_aim_target_takes_damage() {
  CHECK(aim_target_ok(1, 1, BOSS_OPEN) == 1);
  CHECK_MSG(aim_target_ok(1, 1, BOSS_IMM_NOHANDLER) == 0, "Robo Duke's body or the crane");
  CHECK_MSG(aim_target_ok(1, 1, BOSS_IMM_MECHHEAD) == 0, "a refused head state");
  CHECK_MSG(aim_target_ok(1, 0, BOSS_OPEN) == 0, "dead");
  CHECK_MSG(aim_target_ok(0, 1, BOSS_OPEN) == 0, "not an enemy or a boss");
}

// freecampause.h: the world freezes while the camera flies and runs again by every path the flight ends.
static void tb_0350_freecam_pause_rules() {
  static int objs_[2]; int& w1 = objs_[0]; int& w2 = objs_[1];
  const void* A = &w1;
  const void* B = &w2;
  FcPause s = {};
  CHECK_MSG(fcp_step(&s, 0, A, 1, 0) == FCP_NONE && !s.held && !s.world, "row or flight off: nothing");
  CHECK_MSG(fcp_step(&s, 1, A, 1, 0) == FCP_SLEEP && s.world == A && s.held, "the flight freezes its world");
  CHECK_MSG(fcp_step(&s, 1, A, 1, 1) == FCP_SLEEP, "every frame, so a world the engine's menu woke freezes again");
  CHECK_MSG(fcp_step(&s, 1, 0, 1, 0) == FCP_SLEEP && s.world == A, "no player this frame: the world stays held");
  CHECK_MSG(fcp_step(&s, 0, A, 1, 0) == FCP_WAKE && s.why == FCP_WHY_ENDED && !s.held && !s.world,
            "the flight ends: woken");
  CHECK_MSG(fcp_step(&s, 0, A, 1, 0) == FCP_NONE, "woken once");
  s = FcPause();
  fcp_step(&s, 1, A, 1, 0);
  CHECK_MSG(fcp_step(&s, 0, A, 1, 1) == FCP_LET_GO && s.why == FCP_WHY_SCREEN && !s.held && !s.world,
            "ends under the engine's menu: let go, the menu's close wakes it");
  s = FcPause();
  fcp_step(&s, 1, A, 1, 0);
  CHECK_MSG(fcp_step(&s, 1, B, 0, 0) == FCP_LET_GO && s.why == FCP_WHY_NEW_WORLD && !s.held && !s.world,
            "a level change: the old world is gone, never touched");
  CHECK_MSG(fcp_step(&s, 1, B, 1, 0) == FCP_NONE && !s.world, "the new level runs for the rest of this flight");
  CHECK(fcp_step(&s, 0, B, 1, 0) == FCP_NONE);
  CHECK_MSG(fcp_step(&s, 1, B, 1, 0) == FCP_SLEEP && s.world == B, "the next flight freezes the new level");
  s = FcPause();
  fcp_step(&s, 1, A, 1, 0);
  CHECK_MSG(fcp_step(&s, 1, B, 1, 0) == FCP_WAKE && s.why == FCP_WHY_NEW_WORLD,
            "a level change with the old world still listed: woken, never left asleep");
  s = FcPause();
  fcp_step(&s, 1, A, 1, 0);
  CHECK_MSG(fcp_step(&s, 1, 0, 0, 0) == FCP_LET_GO && s.why == FCP_WHY_GONE && !s.held, "the world went: let go");
  CHECK_MSG(fcp_step(&s, 1, A, 1, 0) == FCP_NONE, "and not taken again in this flight");
  s = FcPause();
  CHECK_MSG(fcp_step(&s, 1, 0, 0, 0) == FCP_NONE && !s.world, "no world: nothing");
  CHECK_MSG(fcp_step(&s, 1, A, 0, 0) == FCP_NONE && !s.world && !s.held, "not a live process: never frozen");
  CHECK_MSG(fcp_step(&s, 1, A, 1, 0) == FCP_SLEEP, "frozen once it is");
  s = FcPause();
  fcp_step(&s, 0, A, 1, 0);
  CHECK_MSG(fcp_step(&s, 1, A, 1, 0) == FCP_SLEEP, "the row turned on mid-flight freezes at once");
  for (int w = FCP_WHY_ENDED; w <= FCP_WHY_NEW_WORLD; ++w) CHECK(fcp_why_text(w)[0] != 0);
}

// config.cpp: the Pause the game row is stored under [freecam] pause and starts off.
static void tb_0351_freecam_pause_config() {
  config_defaults();
  CHECK_MSG(g_cfg.freecamPause == 0, "Pause the game defaults off");
  CHECK(config_key_known("freecam", "pause"));
}

// nocull.h: while our pause holds the world, the camera's cached near/far/fov are stale (its world tick refreshes
// them), so the push applies the live g_cam_near/g_cam_fov and the raised far; only then, and only on a difference.
static void tb_0352_nocull_proj_push_while_paused() {
  const NcProj stale = { 16.f, 8000.f, 90.f };
  NcProj o;
  CHECK_MSG(nocull_proj_push(1, 1, 1, 16.f, 1, 90.f, stale, &o) == 1 && o.f == NOCULL_FAR_F && o.n == 16.f &&
            o.fov == 90.f, "paused with the stale stock far: the raised far, near and fov kept");
  const NcProj raised = { 16.f, NOCULL_FAR_F, 90.f };
  CHECK_MSG(nocull_proj_push(1, 1, 1, 16.f, 1, 90.f, raised, &o) == 0, "nothing differs: no change call");
  CHECK_MSG(nocull_proj_push(1, 1, 1, 16.f, 1, 60.f, raised, &o) == 1 && o.fov == 60.f && o.f == NOCULL_FAR_F,
            "the wheel's g_cam_fov while frozen");
  CHECK_MSG(nocull_proj_push(1, 1, 1, 8.f, 1, 90.f, raised, &o) == 1 && o.n == 8.f, "g_cam_near while frozen");
  CHECK_MSG(nocull_proj_push(1, 0, 1, 16.f, 1, 60.f, stale, &o) == 1 && o.f == 8000.f && o.fov == 60.f,
            "far not raised: the projection's far kept, the fov still follows");
  CHECK_MSG(nocull_proj_push(0, 1, 1, 16.f, 1, 60.f, stale, &o) == 0, "not paused: the woken camera applies them");
  CHECK_MSG(nocull_proj_push(1, 1, 0, 0.f, 0, 0.f, raised, &o) == 0, "unread cvars: the projection's own kept");
  CHECK_MSG(nocull_proj_push(1, 1, 1, 0.f, 1, 180.f, raised, &o) == 0, "near <= 0 or fov >= 180 refused");
  CHECK(atof(NOCULL_FAR) == (double)NOCULL_FAR_F);
}

// nocull.h: a world with its own far plane (+0xF4 != -1) puts a short far back every frame, paused or not; the
// freecam push raises it again while the raised far is applied and the projection's is below it.
static void tb_0471_nocull_far_override() {
  CHECK_MSG(nocull_far_override(1, 1600.f) == 1, "applied, projection far 1600: override");
  CHECK_MSG(nocull_far_override(1, NOCULL_FAR_F) == 0, "already raised: nothing to do");
  CHECK_MSG(nocull_far_override(0, 1600.f) == 0, "far not applied: the stock far is the player's choice");
  const NcProj map = { 16.f, 1600.f, 90.f };
  NcProj o;
  CHECK_MSG(nocull_proj_push(1, 1, 0, 0.f, 0, 0.f, map, &o) == 1 && o.f == NOCULL_FAR_F && o.n == 16.f &&
            o.fov == 90.f, "unpaused override: far raised, near and fov from the projection");
  CHECK_MSG(nocull_proj_push(0, 1, 0, 0.f, 0, 0.f, map, &o) == 0, "no push wanted: nothing");
}

// Any track: the crosshair's world hit snaps, in x/z, to the nearest point of any track.
static void tb_0353_trk_nearest_xz() {
  const TrkPt line[2] = { { 0.f, 0.f, 100.f, 0.f }, { 200.f, 0.f, 100.f, 200.f } };
  const Vec3 q = { 50.f, 30.f, 40.f };
  TrkHit h = trk_nearest_xz(q, line, 2);
  CHECK_MSG(h.ok && trk_near(h.p.x, 50.f) && trk_near(h.p.z, 100.f) && trk_near(h.p.y, 30.f) && trk_near(h.s, 60.f),
            "the foot on the span, y kept, s the x/z distance");
  const Vec3 past = { -30.f, 0.f, 140.f };
  h = trk_nearest_xz(past, line, 2);
  CHECK_MSG(h.ok && trk_near(h.p.x, 0.f) && trk_near(h.p.z, 100.f) && trk_near(h.s, 50.f), "past the end: the end");
  const TrkPt bend[3] = { { 0.f, 0.f, 0.f, 0.f }, { 100.f, 0.f, 0.f, 100.f }, { 100.f, 0.f, 100.f, 200.f } };
  const Vec3 in = { 90.f, 0.f, 60.f };
  h = trk_nearest_xz(in, bend, 3);
  CHECK_MSG(h.ok && h.seg == 1 && trk_near(h.p.x, 100.f) && trk_near(h.p.z, 60.f), "the nearer of two spans");
  CHECK(!trk_nearest_xz(q, line, 0).ok);
}

// The nearest track within the radius; candidates within `tie` of it go to the one whose floor is nearest the hit's
// height; none within the radius is -1 (red marker). A depth run (state 4) refuses the teleport.
static void tb_0354_trk_choose() {
  const TrkCand three[3] = { { 1, 120.f, 0.f }, { 1, 40.f, 0.f }, { 1, 90.f, 0.f } };
  CHECK(trk_nearest_d(three, 3) == 40.f);
  CHECK_MSG(trk_choose(three, 3, 256.f, 8.f) == 1, "the nearest of several");
  const TrkCand tie[3] = { { 1, 40.f, 300.f }, { 1, 44.f, 2.f }, { 1, 30.f, 999.f } };
  CHECK_MSG(trk_choose(tie, 3, 256.f, 8.f) == 2, "outside the tie band the nearest wins whatever its floor");
  const TrkCand tie2[2] = { { 1, 40.f, 300.f }, { 1, 44.f, 2.f } };
  CHECK_MSG(trk_choose(tie2, 2, 256.f, 8.f) == 1, "a tie goes to the floor nearest the hit");
  const TrkCand far_[2] = { { 1, 300.f, 0.f }, { 0, 10.f, 0.f } };
  CHECK_MSG(trk_choose(far_, 2, 256.f, 8.f) == -1, "nothing within the radius, and an unread track never wins");
  CHECK(trk_nearest_d(far_ + 1, 1) > 1e30f);
  CHECK(trk_choose(three, 0, 256.f, 8.f) == -1);
  CHECK(tp_depth_run(4) && !tp_depth_run(3) && !tp_depth_run(17));
}

// The marker goes red during a depth run (state 4) with its own reason; a placement already refused keeps its reason.
static void tb_0355_tp_depth_run_marker() {
  CHECK(tp_depth_refuse(TP_OK, 4) == TP_DEPTH_RUN);
  CHECK(tp_depth_refuse(TP_OK, 3) == TP_OK);
  CHECK_MSG(tp_depth_refuse(TP_BLOCKED, 4) == TP_BLOCKED, "an earlier refusal keeps its reason");
  CHECK(strcmp(tp_reason_text(TP_DEPTH_RUN), "unknown") != 0);
}

// photo.h: a world point lands on the same pixel of the big image whether the full view is projected at n*W x n*H or
// tile (r, c) at W x H placed at (c*W, r*H), its neighbour's projection agreeing.
static bool photo_px(const float v[16], float m0, float m5, const float p[3], int W, int H, float* x, float* y) {
  float q[3];
  for (int j = 0; j < 3; ++j) q[j] = p[0] * v[j] + p[1] * v[4 + j] + p[2] * v[8 + j] + v[12 + j];
  if (q[2] <= 0.f) return false;
  *x = (m0 * q[0] / q[2] + 1.f) * 0.5f * (float)W;
  *y = (1.f - m5 * q[1] / q[2]) * 0.5f * (float)H;
  return true;
}
static void tb_0356_photo_tile_seams() {
  // The render is W x H; the capture keeps the visible rect, so the big image is n*vis.w x n*vis.h. The borderless
  // window sits at (0,-1) at 3440x1441 (a real screen): its first row is off the monitor.
  const int W = 3440, H = 1441;
  const FreecamPose pose = { { 1200.f, 300.f, -2500.f }, 30.f, -12.f, 1.f };
  float v[16], inv[16];
  freecam_matrices(pose, v, inv);
  float rt[3], up[3], fw[3];
  freecam_axes(pose, rt, up, fw);
  const float fov = 79.755f;
  const float m0 = 1.f / tanf(fov * 0.5f * 0.017453292f), m5 = m0 * (float)W / (float)H;
  PhotoVis full = { 0, 0, W, H, 0, 0 }, cut = { 0, 1, W, H - 1, 0, 0 };
  const int ns[2] = { 2, 10 };
  const PhotoVis* viss[2] = { &full, &cut };
  for (int ni = 0; ni < 2; ++ni) {
    const int n = ns[ni];
    const PhotoVis& vis = *viss[ni];
    const float fovT = photo_tile_fov(fov, n);
    const float m0t = 1.f / tanf(fovT * 0.5f * 0.017453292f), m5t = m0t * (float)W / (float)H;
    CHECK_MSG(fabsf(m0t - (float)n * m0) < 1e-4f * (float)n * m0, "the tile fov magnifies by exactly n");
    // Eight points over the view: depth, then right and up as fractions of the half-frustum at that depth.
    const float pts[8][3] = { { 900.f, 0.f, 0.f }, { 500.f, -0.9f, 0.3f }, { 3000.f, 0.77f, -0.35f },
                              { 120.f, 0.11f, 0.38f }, { 7000.f, -0.5f, -0.4f }, { 2000.f, 0.95f, 0.39f },
                              { 640.f, -0.33f, -0.05f }, { 15000.f, 0.02f, 0.2f } };
    int placed = 0, agreed = 0, seams = 0;
    for (int k = 0; k < 8; ++k) {
      const float d = pts[k][0], a = pts[k][1] * d / m0, b = pts[k][2] * d / m0;
      float p[3];
      for (int j = 0; j < 3; ++j) p[j] = pose.eye[j] + fw[j] * d + rt[j] * a + up[j] * b;
      float xf, yf;
      if (!photo_px(v, m0, m5, p, W, H, &xf, &yf)) continue;
      const float X = (float)n * (xf - (float)vis.vx), Y = (float)n * (yf - (float)vis.vy);   // big-image pixel
      if (X < 0.f || Y < 0.f || X >= (float)(n * vis.w) || Y >= (float)(n * vis.h)) continue;
      placed++;
      const int c = (int)(X / (float)vis.w), r = (int)(Y / (float)vis.h);
      float tv[16], ti[16], x, y, ox, oy;
      photo_tile_offsets(n, r, c, W, H, vis, &ox, &oy);
      photo_tile_matrices(v, inv, m0t, m5t, ox, oy, tv, ti);
      if (photo_px(tv, m0t, m5t, p, W, H, &x, &y) && fabsf(x - (float)vis.vx + (float)(c * vis.w) - X) < 0.05f &&
          fabsf(y - (float)vis.vy + (float)(r * vis.h) - Y) < 0.05f && x >= (float)vis.vx - 0.05f &&
          x < (float)(vis.vx + vis.w) + 0.05f && y >= (float)vis.vy - 0.05f && y < (float)(vis.vy + vis.h) + 0.05f)
        agreed++;
      // A neighbour across the column and across the row projects the same point one tile over.
      const int c2 = c + 1 < n ? c + 1 : c - 1, r2 = r + 1 < n ? r + 1 : r - 1;
      photo_tile_offsets(n, r2, c2, W, H, vis, &ox, &oy);
      photo_tile_matrices(v, inv, m0t, m5t, ox, oy, tv, ti);
      if (photo_px(tv, m0t, m5t, p, W, H, &x, &y) && fabsf(x - (float)vis.vx + (float)(c2 * vis.w) - X) < 0.05f &&
          fabsf(y - (float)vis.vy + (float)(r2 * vis.h) - Y) < 0.05f) seams++;
      if (k == 0) {   // the pushed pair stays a pair: view * inverse is the identity
        float worst = 0.f;
        for (int i = 0; i < 4; ++i)
          for (int j = 0; j < 4; ++j) {
            float sum = 0.f;
            for (int q = 0; q < 4; ++q) sum += tv[i * 4 + q] * ti[q * 4 + j];
            const float e = fabsf(sum - (i == j ? 1.f : 0.f));
            if (e > worst) worst = e;
          }
        // Float error scales with the translation (the eye is ~2500 units out): relative, not absolute.
        CHECK_MSG(worst < 2e-6f * 4096.f, "the sheared view and inverse are each other's inverse");
      }
    }
    CHECK_MSG(placed == 8, "every test point is inside the big image");
    CHECK_MSG(agreed == placed, "each point's tile puts it on its big-image pixel, inside the visible rect");
    CHECK_MSG(seams == placed, "the neighbouring tiles agree on the same point");
  }
  // The strip copy: BGRA rows (the capture's) into BGR at tile column c; it reports whether any pixel is not black.
  const unsigned char tile[2 * 8] = { 1, 2, 3, 0xFF, 4, 5, 6, 0xFF, 7, 8, 9, 0xFF, 10, 11, 12, 0xFF };
  unsigned char strip[2 * 12];
  memset(strip, 0xEE, sizeof strip);
  CHECK(photo_stride(2) == 8 && photo_stride(4) == 12);
  CHECK(photo_copy_bgra(tile, 8, 2, 2, strip, 12, 1));
  const unsigned char want[2 * 12] = { 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 1, 2, 3, 4, 5, 6,
                                       0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 7, 8, 9, 10, 11, 12 };
  CHECK_MSG(memcmp(strip, want, sizeof want) == 0, "tile column 1 lands at x=2, alpha dropped");
  const unsigned char black[2 * 8] = { 0, 0, 0, 0xFF, 0, 0, 0, 0xFF, 0, 0, 0, 0xFF, 0, 0, 0, 0xFF };
  CHECK_MSG(!photo_copy_bgra(black, 8, 2, 2, strip, 12, 0), "an all-black tile (alpha set) is reported black");
  unsigned char one[2 * 8];
  memcpy(one, black, sizeof one);
  one[13] = 1;
  CHECK_MSG(photo_copy_bgra(one, 8, 2, 2, strip, 12, 0), "one dim pixel is not black");
}

// photo.h: a strip as chunks of whole rows (a 226 MB strip was refused in a fragmented address space).
static void tb_0476_photo_strip_chunks() {
  // The failing photo: 16x of 3440x1440, a 226 MB strip. 101 rows of 165120 bytes fit 16 MiB: 15 chunks, last 26 rows.
  const unsigned s16 = photo_stride(3440 * 16);
  const int r16 = photo_chunk_rows(s16, 1440);
  CHECK(s16 == 165120u && r16 == 101);
  CHECK(photo_chunk_count(1440, r16) == 15);
  CHECK(photo_chunk_rows_in(0, 1440, r16) == 101 && photo_chunk_rows_in(14, 1440, r16) == 26);
  CHECK_MSG((unsigned long long)r16 * s16 <= PHOTO_CHUNK_BYTES, "a chunk is at most 16 MiB");
  int sum = 0;
  for (int k = 0; k < photo_chunk_count(1440, r16); ++k) sum += photo_chunk_rows_in(k, 1440, r16);
  CHECK_MSG(sum == 1440, "the chunks hold every row once");
  int k = -1, y = -1;
  photo_chunk_of(0, r16, &k, &y);    CHECK(k == 0 && y == 0);
  photo_chunk_of(100, r16, &k, &y);  CHECK(k == 0 && y == 100);
  photo_chunk_of(101, r16, &k, &y);  CHECK(k == 1 && y == 0);
  photo_chunk_of(1439, r16, &k, &y); CHECK(k == 14 && y == 25);
  // A small strip is one chunk of all its rows.
  CHECK(photo_chunk_rows(photo_stride(1280), 480) == 480 && photo_chunk_count(480, 480) == 1);
  // Rows wider than a chunk: at least one row each, and never more than PHOTO_CHUNKS_MAX chunks.
  const int rw = photo_chunk_rows(20u << 20, 100);
  CHECK(rw >= 1 && photo_chunk_count(100, rw) <= PHOTO_CHUNKS_MAX && photo_chunk_count(100, rw) * rw >= 100);
  CHECK(photo_chunk_rows(1u, 0) == 1 && photo_chunk_count(0, 1) == 0);
  // The copy across chunks: three tile rows into chunks of two rows, tile column 1 of a 2-wide tile.
  const unsigned char t[3 * 8] = { 1, 2, 3, 0xFF, 4, 5, 6, 0xFF, 7, 8, 9, 0xFF, 10, 11, 12, 0xFF,
                                   13, 14, 15, 0xFF, 16, 17, 18, 0xFF };
  unsigned char c0[2 * 12], c1[2 * 12];
  memset(c0, 0xEE, sizeof c0);
  memset(c1, 0xEE, sizeof c1);
  unsigned char* const ch[2] = { c0, c1 };
  CHECK(photo_copy_bgra_chunks(t, 8, 2, 3, ch, 2, 12, 1));
  const unsigned char w0[2 * 12] = { 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 1, 2, 3, 4, 5, 6,
                                     0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 7, 8, 9, 10, 11, 12 };
  const unsigned char w1[12] = { 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 13, 14, 15, 16, 17, 18 };
  CHECK_MSG(memcmp(c0, w0, sizeof w0) == 0 && memcmp(c1, w1, sizeof w1) == 0, "rows 0-1 in chunk 0, row 2 in chunk 1");
  CHECK_MSG(c1[12] == 0xEE, "the short last chunk is not written past its rows");
  const unsigned char blk[3 * 8] = { 0, 0, 0, 0xFF, 0, 0, 0, 0xFF, 0, 0, 0, 0xFF, 0, 0, 0, 0xFF, 0, 0, 0, 0xFF, 0, 0, 0, 0xFF };
  CHECK_MSG(!photo_copy_bgra_chunks(blk, 8, 2, 3, ch, 2, 12, 0), "an all-black tile is black across chunks");
}

// photo.h: the part of the client the monitor shows. The borderless window at (0,-1) 3440x1441 on a 3440x1440
// monitor shows client rows 1..1440; a window fully on screen shows all of it; one off the monitor shows nothing.
static void tb_0360_photo_visible_rect() {
  PhotoVis v;
  CHECK(photo_visible(0, -1, 3440, 1441, 0, 0, 3440, 1440, &v) && v.vx == 0 && v.vy == 1 && v.w == 3440 &&
        v.h == 1440 && v.sx == 0 && v.sy == 0);
  CHECK(photo_visible(100, 50, 1280, 720, 0, 0, 1920, 1080, &v) && v.vx == 0 && v.vy == 0 && v.w == 1280 &&
        v.h == 720 && v.sx == 100 && v.sy == 50);
  CHECK_MSG(photo_visible(1800, 0, 1280, 720, 0, 0, 1920, 1080, &v) && v.w == 120 && v.sx == 1800,
            "clipped on the right");
  CHECK_MSG(photo_visible(-1500, 0, 1280, 720, 1920, 0, 3840, 1080, &v) == false, "off this monitor: nothing");
  char b[48];
  photo_size_text(10, 3440, 1440, b, sizeof b);
  CHECK_MSG(strcmp(b, "10x 34400x14400") == 0, "the readout follows the visible rect");
}

static PhotoIn photo_ok_in() {
  PhotoIn in;
  memset(&in, 0, sizeof in);
  in.cam = in.world = in.focus = in.sameSize = in.frozen = in.hidden = in.bufferFree = in.calibrated = 1;
  return in;
}

// photo.h: tiles row by row, left to right; a strip flushed after each row's last tile; a buffer wait between rows;
// the encoder's end finishes it. A frame whose push did not draw the tile reads nothing and moves nothing.
static void tb_0357_photo_strip_and_tile_order() {
  PhotoSm s = photo_begin(3);
  PhotoIn in = photo_ok_in();
  in.cam = 0; in.press = 1;
  CHECK_MSG(photo_step(&s, in) == PA_NONE && s.phase == PH_IDLE, "no flight: the key does nothing");
  in = photo_ok_in(); in.press = 1;
  CHECK(photo_step(&s, in) == PA_START && s.phase == PH_ARMING);
  in.press = 0;
  CHECK(photo_step(&s, in) == PA_NONE && s.phase == PH_ARMING);
  in.calibrated = 0;
  CHECK_MSG(photo_step(&s, in) == PA_NONE && s.phase == PH_ARMING, "settled but not calibrated: still arming");
  in.calibrated = 1;
  CHECK_MSG(photo_step(&s, in) == PA_NONE && s.phase == PH_TILE && s.r == 0 && s.c == 0,
            "settled (frozen, hidden) and calibrated: the first tile is next");
  char log[160] = "";
  bool missed = false, waited = false;   // one push that missed the tile, one row waiting for a buffer
  for (int f = 0; f < 40 && s.phase != PH_IDLE; ++f) {
    char ev[16] = "";
    in = photo_ok_in();
    if (s.phase == PH_TILE) {
      if (s.r == 1 && !missed) { missed = true; in.tileRead = 0; sprintf_s(ev, "m "); }
      else { in.tileRead = 1; sprintf_s(ev, "t%d%d ", s.r, s.c); }
    } else if (s.phase == PH_WAIT) {
      in.bufferFree = waited ? 1 : 0;
      waited = true;
      sprintf_s(ev, "w ");
    } else if (s.phase == PH_DRAIN) {
      in.encDone = 1;
      sprintf_s(ev, "d ");
    }
    strcat_s(log, ev);
    const int a = photo_step(&s, in);
    if (a == PA_FLUSH) { char fl[8]; sprintf_s(fl, "F%d ", s.flushRow); strcat_s(log, fl); }
    if (a == PA_FINISH) strcat_s(log, "END");
  }
  CHECK_MSG(strcmp(log, "t00 t01 t02 F0 w w m t10 t11 t12 F1 w t20 t21 t22 F2 d END") == 0, log);
  CHECK(s.phase == PH_IDLE);
}

// photo.h: every way a capture ends early, from arming, from the tiles and while the encoder drains.
static void tb_0358_photo_aborts() {
  struct Case { int field; int why; };
  const Case cases[] = { { 0, PHR_CANCELLED }, { 1, PHR_CAMERA }, { 2, PHR_PANEL }, { 3, PHR_LEVEL }, { 4, PHR_SCREEN },
                         { 5, PHR_FOCUS }, { 6, PHR_SIZE } };
  for (int i = 0; i < 7; ++i) {
    for (int phase = 0; phase < 2; ++phase) {
      PhotoSm s = photo_begin(4);
      PhotoIn in = photo_ok_in();
      in.press = 1;
      photo_step(&s, in);
      in.press = 0;
      if (phase) { photo_step(&s, in); photo_step(&s, in); }
      CHECK(s.phase == (phase ? PH_TILE : PH_ARMING));
      switch (cases[i].field) {
        case 0: in.press = 1; break;
        case 1: in.cam = 0; break;
        case 2: in.panel = 1; break;
        case 3: in.world = 0; break;
        case 4: in.screen = 1; break;
        case 5: in.focus = 0; break;
        default: in.sameSize = 0; break;
      }
      in.tileRead = 1;
      char msg[64];
      sprintf_s(msg, "abort case %d from %s", i, phase ? "the tiles" : "arming");
      CHECK_MSG(photo_step(&s, in) == PA_ABORT && s.reason == cases[i].why && s.phase == PH_IDLE, msg);
    }
  }
  PhotoSm s = photo_begin(2);
  PhotoIn in = photo_ok_in();
  in.press = 1; photo_step(&s, in); in.press = 0;
  in.frozen = 0;
  int a = PA_NONE;
  for (int f = 0; f < PHOTO_ARM_TIMEOUT && a == PA_NONE; ++f) a = photo_step(&s, in);
  CHECK_MSG(a == PA_ABORT && s.reason == PHR_NOT_FROZEN, "a world that never freezes ends the arming");
  s = photo_begin(2);
  in = photo_ok_in(); in.press = 1; photo_step(&s, in); in.press = 0; photo_step(&s, in); photo_step(&s, in);
  in.hidden = 0;
  CHECK_MSG(photo_step(&s, in) == PA_ABORT && s.reason == PHR_THAWED, "the HUD or the freeze lost mid-capture");
  s = photo_begin(2);
  in = photo_ok_in(); in.press = 1; photo_step(&s, in); in.press = 0; photo_step(&s, in); photo_step(&s, in);
  a = PA_NONE;
  for (int f = 0; f < PHOTO_TILE_TIMEOUT && a == PA_NONE; ++f) a = photo_step(&s, in);
  CHECK_MSG(a == PA_ABORT && s.reason == PHR_NO_TILE, "pushes that never draw the tile end it");
  // Drain: every pixel is captured, so the camera ending or the panel opening lets the encoder finish.
  s = photo_begin(2);
  s.phase = PH_DRAIN;
  in = photo_ok_in(); in.cam = 0; in.panel = 1; in.focus = 0; in.press = 1;
  CHECK_MSG(photo_step(&s, in) == PA_NONE && s.phase == PH_DRAIN, "draining ignores the camera and the panel");
  in.encFailed = 1;
  CHECK_MSG(photo_step(&s, in) == PA_ABORT && s.reason == PHR_ENCODER, "an encoder failure aborts");
  s = photo_begin(2);
  in = photo_ok_in(); in.press = 1; photo_step(&s, in);
  CHECK_MSG(photo_fail(&s, PHR_START) == PA_ABORT && s.phase == PH_IDLE && s.reason == PHR_START,
            "a start the glue could not make is an abort with its reason");
  CHECK_MSG(photo_fail(&s, PHR_START) == PA_NONE, "nothing to abort when idle");
  for (int w = PHR_CANCELLED; w <= PHR_CALIBRATION; ++w) CHECK(photo_why_text(w)[0] != 0);
}

// photo.h: "<level> <yyyy-mm-dd hh-mm-ss> <W>x<H>.png", the level made safe for a file name; the size readout.
static void tb_0359_photo_file_name() {
  char b[96];
  CHECK(photo_file_name(b, sizeof b, "e01p01_a01", 2026, 10, 2, 9, 5, 7, 34400, 14400) > 0 &&
        strcmp(b, "e01p01_a01 2026-10-02 09-05-07 34400x14400.png") == 0);
  CHECK_MSG(photo_file_name(b, sizeof b, "a:b/c*?", 2026, 1, 31, 23, 59, 58, 6880, 2880) > 0 &&
            strcmp(b, "a_b_c__ 2026-01-31 23-59-58 6880x2880.png") == 0, "characters a file name refuses");
  CHECK(photo_file_name(b, sizeof b, "", 2026, 1, 1, 0, 0, 0, 4, 2) > 0 &&
        strcmp(b, "level 2026-01-01 00-00-00 4x2.png") == 0);
  CHECK(photo_file_name(b, sizeof b, 0, 2026, 1, 1, 0, 0, 0, 4, 2) > 0 && strncmp(b, "level ", 6) == 0);
  CHECK_MSG(photo_file_name(b, 20, "e01p01_a01", 2026, 10, 2, 9, 5, 7, 34400, 14400) == -1 && b[0] == 0,
            "too small a buffer: no name rather than a cut one");
  photo_size_text(10, 3440, 1440, b, sizeof b);
  CHECK(strcmp(b, "10x 34400x14400") == 0);
  CHECK(photo_mult_clamped(1) == PHOTO_MULT_MIN && photo_mult_clamped(17) == PHOTO_MULT_MAX &&
        photo_mult_clamped(7) == 7);
  // [freecam] photo / photosize: the key defaults to F12, the size to 4x; the key refuses the movement keys.
  config_defaults();
  CHECK(g_cfg.photoKey == 0x7B && g_cfg.photoMult == PHOTO_MULT_DEF && config_bind_get(BIND_ROW_PHOTO) == 0x7B);
  CHECK(config_key_known("freecam", "photo") && config_key_known("freecam", "photosize"));
  CHECK(freecam_bind_refused(BIND_ROW_PHOTO, 'W') && !freecam_bind_refused(BIND_ROW_PHOTO, 0x7B));
  CHECK_MSG(freecam_bind_refused(BIND_ROW_GAMESPEED, 0x20) && !freecam_bind_refused(BIND_ROW_GAMESPEED, 'G'),
            "the Speed key runs before the camera's keys, so it refuses a fly key too (review)");
}

// photo.h calibration: 4-px bands 8 px in from each render edge are located on the capture by coverage-weighted
// centroid; 1:1 when the centroids' distance matches the render's within 0.5 px. A 58x38 render lands at (1,2) of a
// 60x40 capture (the borderless window's case); a 1-px mark once read a row up and refused a good 1:1 window.
static void photo_paint_band(unsigned char* img, int iw, int ih, int vertical, int at, unsigned rgb, int stepOn) {
  for (int i = 0; i < (vertical ? ih : iw); ++i) {
    if (stepOn > 1 && i % stepOn) continue;   // a partial row: every stepOn-th pixel only
    const int x = vertical ? at : i, y = vertical ? i : at;
    unsigned char* p = img + (y * iw + x) * 4;
    p[0] = (unsigned char)(rgb & 0xFF); p[1] = (unsigned char)((rgb >> 8) & 0xFF);
    p[2] = (unsigned char)((rgb >> 16) & 0xFF); p[3] = 0xFF;
  }
}
static void tb_0361_photo_calibration() {
  const int iw = 60, ih = 40, W = 58, H = 38, ox = 1, oy = 2;
  CHECK(photo_cal_band_at(H, 0) == 8 && photo_cal_band_at(H, 1) == H - 12);
  CHECK(photo_cal_band_centre(H, 0) == 9.5 && photo_cal_band_centre(H, 1) == H - 10.5);
  static unsigned char img[60 * 40 * 4];
  for (int pass = 0; pass < 2; ++pass) {
    memset(img, 0, sizeof img);
    for (int k = 0; k < PHOTO_CAL_BAND; ++k) {
      photo_paint_band(img, iw, ih, 0, oy + photo_cal_band_at(H, 0) + k, PHOTO_CAL_ROW1, 1);
      photo_paint_band(img, iw, ih, 0, oy + photo_cal_band_at(H, 1) + k, PHOTO_CAL_ROW2, 1);
    }
    if (pass == 1) {
      // The smear: the top band bleeds into the row above at half coverage, and the scene has the band's colour
      // in the screen's top row (outside the band's window).
      photo_paint_band(img, iw, ih, 0, oy + photo_cal_band_at(H, 0) - 1, PHOTO_CAL_ROW1, 2);
      photo_paint_band(img, iw, ih, 0, 0, PHOTO_CAL_ROW1, 2);
    }
    for (int k = 0; k < PHOTO_CAL_BAND; ++k) {
      photo_paint_band(img, iw, ih, 1, ox + photo_cal_band_at(W, 0) + k, PHOTO_CAL_COL1, 1);
      photo_paint_band(img, iw, ih, 1, ox + photo_cal_band_at(W, 1) + k, PHOTO_CAL_COL2, 1);
    }
    const double r1 = photo_band_centroid(img, iw * 4, iw, ih, PHOTO_CAL_ROW1, 0);
    const double r2 = photo_band_centroid(img, iw * 4, iw, ih, PHOTO_CAL_ROW2, 0);
    const double c1 = photo_band_centroid(img, iw * 4, iw, ih, PHOTO_CAL_COL1, 1);
    const double c2 = photo_band_centroid(img, iw * 4, iw, ih, PHOTO_CAL_COL2, 1);
    int org = -99;
    char msg[96];
    sprintf_s(msg, "%s: rows at %.3f, %.3f 1:1 from capture row 2", pass ? "smeared" : "clean", r1, r2);
    CHECK_MSG(photo_cal_axis(photo_cal_band_centre(H, 0), photo_cal_band_centre(H, 1), r1, r2, &org) && org == oy, msg);
    CHECK_MSG(photo_cal_axis(photo_cal_band_centre(W, 0), photo_cal_band_centre(W, 1), c1, c2, &org) && org == ox,
              "columns 1:1, render column 0 at capture column 1");
    if (!pass) CHECK_MSG(fabs(r1 - 11.5) < 1e-9 && fabs(c2 - (ox + W - 10.5)) < 1e-9, "a clean band's centre exactly");
  }
  CHECK_MSG(photo_band_centroid(img, iw * 4, iw, ih, 0x808080u, 0) < 0.0, "a colour nowhere: not found");
  int org = -99;
  // 1440 render rows shown over 1441 screen rows: the bands' distance grows by ~0.99 px. Still refused.
  const double a = photo_cal_band_centre(1440, 0), b = photo_cal_band_centre(1440, 1);
  CHECK_MSG(!photo_cal_axis(a, b, a + 1.0, a + 1.0 + (b - a) * 1441.0 / 1440.0, &org), "a real stretch is refused");
  CHECK_MSG(photo_cal_axis(a, b, a + 1.0, b + 1.4, &org) && org == 1, "0.4 px of measuring noise is 1:1");
  CHECK_MSG(!photo_cal_axis(a, b, -1.0, b, &org), "a band not found: no mapping");
}

// craterule.h: a crate's or barrel's `contents` codes (re/pickups.md section 5). Code 0 is a nuke in every holder;
// the rest follow FUN_100684b0's table by holder (crate +0xBF4 = 1, barrel 0) and crate_type/barrel_type.
static void tb_0362_crate_content_codes() {
  CHECK(crate_holder_of("crate") == HOLDER_CRATE && crate_holder_of("barrel") == HOLDER_BARREL);
  CHECK_MSG(crate_holder_of("crate1") == HOLDER_NONE && crate_holder_of("breakable_adv") == HOLDER_NONE &&
            crate_holder_of(0) == HOLDER_NONE, "exact class names only");
  int nukes = 0;
  for (int t = 0; t < 4; ++t)
    nukes += crate_content(HOLDER_CRATE, t, 0).cat == CAT_NUKE && crate_content(HOLDER_BARREL, t, 0).cat == CAT_NUKE;
  CHECK_MSG(nukes == 4, "code 0 is a nuke whatever the type");
  struct Row { int holder, type, code; unsigned char cat, kind; int sub; };
  static const Row rows[] = {
    { HOLDER_CRATE, 0, 1, CAT_ITEM, ITEM_AMMO, 0 }, { HOLDER_CRATE, 0, 2, CAT_ITEM, ITEM_AMMO, 1 },
    { HOLDER_CRATE, 0, 3, CAT_ITEM, ITEM_EGO, 0 },  { HOLDER_CRATE, 0, 4, CAT_ITEM, ITEM_EGO, 1 },
    { HOLDER_CRATE, 1, 1, CAT_ITEM, ITEM_AMMO, 2 }, { HOLDER_CRATE, 1, 2, CAT_ITEM, ITEM_AMMO, 3 },
    { HOLDER_CRATE, 2, 1, CAT_ITEM, ITEM_AMMO, 4 }, { HOLDER_CRATE, 2, 2, CAT_ITEM, ITEM_AMMO, 5 },
    { HOLDER_BARREL, 0, 1, CAT_ITEM, ITEM_EGO, 0 }, { HOLDER_BARREL, 0, 2, CAT_ITEM, ITEM_EGO, 1 },
    { HOLDER_CRATE, 1, 3, CAT_OTHER, ITEM_NONE, -1 }, { HOLDER_CRATE, 3, 1, CAT_OTHER, ITEM_NONE, -1 },
    { HOLDER_BARREL, 1, 1, CAT_OTHER, ITEM_NONE, -1 }, { HOLDER_BARREL, 0, 5, CAT_OTHER, ITEM_NONE, -1 },
    { HOLDER_CRATE, 0, -1, CAT_OTHER, ITEM_NONE, -1 }, { HOLDER_NONE, 0, 0, CAT_OTHER, ITEM_NONE, -1 },
  };
  int ok = 0;
  for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
    const CrateContent c = crate_content(rows[i].holder, rows[i].type, rows[i].code);
    ok += c.cat == rows[i].cat && c.kind == rows[i].kind && c.subtype == rows[i].sub;
  }
  CHECK_MSG(ok == (int)(sizeof rows / sizeof rows[0]), "the spawn-script table, row by row");
}
// The content's own ESP label with the holder's word, so a crate drawn as a nuke or item says where it is.
static bool crate_label_is(const char* got, const char* want) { return got && !strcmp(got, want); }
static void tb_0363_crate_labels() {
  CHECK(crate_content_label(HOLDER_CRATE, crate_content(HOLDER_CRATE, 0, 0)) != 0);
  CHECK(crate_label_is(crate_content_label(HOLDER_CRATE, crate_content(HOLDER_CRATE, 2, 0)), "Nuke (crate)"));
  CHECK(crate_label_is(crate_content_label(HOLDER_BARREL, crate_content(HOLDER_BARREL, 0, 0)), "Nuke (barrel)"));
  int same = 0, n = 0;
  for (int h = HOLDER_CRATE; h <= HOLDER_BARREL; ++h)
    for (int t = 0; t < 3; ++t)
      for (int code = 1; code <= 4; ++code) {
        const CrateContent c = crate_content(h, t, code);
        if (c.cat != CAT_ITEM) continue;
        char want[64];
        sprintf_s(want, "%s (%s)", item_label(c.kind, c.subtype, c.kind == ITEM_AMMO ? "Ammo" : "Ego"),
                  h == HOLDER_CRATE ? "crate" : "barrel");
        const char* got = crate_content_label(h, c);
        ++n;
        same += crate_label_is(got, want);
      }
  CHECK_MSG(n == 10 && same == n, "every item label is item_label's plus the holder");
  CHECK(crate_label_is(crate_content_label(HOLDER_CRATE, crate_content(HOLDER_CRATE, 0, 4)), "Ego +100 (crate)"));
  CHECK(crate_label_is(crate_content_label(HOLDER_BARREL, crate_content(HOLDER_BARREL, 0, 2)), "Ego +100 (barrel)"));
  CHECK_MSG(pickup_can_take(ITEM_EGO, crate_content(HOLDER_CRATE, 0, 4).subtype, 0) == TAKE_YES,
            "lhealth.sc's health_type 2 is clamped to 1 by the ego's init: a +100 the player takes");
  CHECK_MSG(crate_content_label(HOLDER_CRATE, crate_content(HOLDER_CRATE, 1, 3)) == 0, "no content, no label");
}
// A holder's entry hides once the holder breaks (+0x2E0 cleared); a crate's nuke is a CAT_NUKE entry, so the Nukes
// arrows include it, and an item never gets one.
static void tb_0364_crate_open_and_arrows() {
  EntSnap s = {};
  s.cat = CAT_NUKE; s.holder = HOLDER_CRATE; s.active = 1;
  CHECK(!ent_crate_opened(s));
  s.active = 0;
  CHECK(ent_crate_opened(s));
  s.holder = HOLDER_NONE;
  CHECK_MSG(!ent_crate_opened(s), "a collected nuke is not an opened crate");
  CHECK(arrow_wanted_cat(crate_content(HOLDER_CRATE, 0, 0).cat, true, false, false));
  CHECK(!arrow_wanted_cat(CAT_NUKE, false, true, true));
  CHECK_MSG(!arrow_wanted_cat(crate_content(HOLDER_CRATE, 0, 1).cat, true, true, true), "items have no arrows");
  CHECK(arrow_wanted_cat(CAT_BABE, false, true, false) && arrow_wanted_cat(CAT_SECRET, false, false, true));
  CHECK(!arrow_wanted_cat(CAT_ENEMY, true, true, true) && !arrow_wanted_cat(CAT_EXPLODABLE, true, true, true));
}

// User 2026-10-02: Screenshot mode also hides the letterbox bars and the level-end stats (system windows).
static void tb_0365_shot_letterbox_and_stats() {
  CHECK_MSG(shot_win_act(1, 0, 1, 1) == SHOT_HIDE, "on, the bars or the stats showing: hide them");
  CHECK_MSG(shot_win_act(1, 1, 1, 1) == SHOT_HIDE, "the game showed them again (a new cutscene): hide again");
  CHECK_MSG(shot_win_act(1, 1, 0, 1) == SHOT_NONE, "held hidden: nothing every frame");
  CHECK_MSG(shot_win_act(0, 1, 0, 1) == SHOT_RESTORE, "off, the game still wants them: shown back");
  CHECK_MSG(shot_win_act(0, 1, 0, 0) == SHOT_FORGET, "the game closed them meanwhile: never a stale bar or stats page");
  CHECK_MSG(shot_win_act(0, 0, 1, 1) == SHOT_NONE, "never hidden by us: never shown by us");
  CHECK(shot_win_act(1, 0, 0, 0) == SHOT_NONE);
  // The letterbox handler's state (+0x1d4): 0 idle, 1 held, 2 sliding in, 3 sliding out (hides itself at the end).
  CHECK_MSG(shot_lb_wanted(1) && shot_lb_wanted(2) && shot_lb_wanted(3), "up or still sliding: shown back");
  CHECK_MSG(!shot_lb_wanted(0) && !shot_lb_wanted(-1) && !shot_lb_wanted(4), "idle, unreadable or unknown: left hidden");
  CHECK_MSG(shot_lb_holds_hud(1) && shot_lb_holds_hud(2), "the cutscene's end will show the HUD itself");
  CHECK_MSG(!shot_lb_holds_hud(3) && !shot_lb_holds_hud(0) && !shot_lb_holds_hud(-1),
            "sliding out or idle: its end already ran, the HUD is ours to show");
}

// The G.L.O.P.P. Ray (aim.h): the beam ends at +0xCC8 (290, proj_gray_init_beam 0x100414e0); the bot never
// fires it past that or past the Aim range.
static void tb_0366_glopp_reach() {
  CHECK_MSG(glopp_reach(3328.f, 290.f) == 290.f, "a 64 m Aim range stops at the beam");
  CHECK_MSG(glopp_reach(104.f, 290.f) == 104.f, "a 2 m Aim range is shorter than the beam");
  CHECK_MSG(glopp_reach(3328.f, 0.f) == GLOPP_RANGE_STOCK, "unread beam: the stock 290");
  CHECK_MSG(glopp_reach(3328.f, -1.f) == GLOPP_RANGE_STOCK, "a negative read is no read");
  CHECK_MSG(glopp_reach(3328.f, 400.f) == 400.f, "the live value wins over the stock one");
}
// monster_apply_damage 0x1002f560, damage type 4, in the handler's order.
static void tb_0367_glopp_gate() {
  GloppFacts f = {};
  f.read = 1; f.monster = 1; f.flags = GLOPP_FLAG_MORPHABLE; f.health = 20.f; f.hasMorph = 1;
  CHECK_MSG(glopp_gate(f) == GLOPP_OK, "a live morphable monster turns");
  GloppFacts g = f; g.read = 0;
  CHECK_MSG(glopp_gate(g) == GLOPP_UNREAD && !glopp_refuses(GLOPP_UNREAD), "unread fields: a counted candidate");
  g = f; g.monster = 0;
  CHECK_MSG(glopp_gate(g) == GLOPP_UNREAD_CLASS, "handlers not recognised: a counted candidate");
  g = f; g.boss = 1;
  CHECK_MSG(glopp_gate(g) == GLOPP_IMM_BOSS && glopp_refuses(GLOPP_IMM_BOSS), "a boss's own handler has no morph");
  g = f; g.flags |= GLOPP_FLAG_ANIMAL;
  CHECK_MSG(glopp_gate(g) == GLOPP_IMM_ANIMAL, "already an animal: refused first");
  g.health = 0.f;
  CHECK_MSG(glopp_gate(g) == GLOPP_IMM_ANIMAL, "the animal test comes before health");
  g = f; g.health = 0.f;
  CHECK_MSG(glopp_gate(g) == GLOPP_IMM_DEAD, "dead");
  g = f; g.morphBusy = 1;
  CHECK_MSG(glopp_gate(g) == GLOPP_IMM_MORPH_ANIM, "+0xDC0 +0x71 refuses");
  g = f; g.hyperOn = 1;
  CHECK_MSG(glopp_gate(g) == GLOPP_IMM_HYPER_ON, "+0xDD4 +0x24 refuses");
  g = f; g.flags |= GLOPP_FLAG_HYPER;
  CHECK_MSG(glopp_gate(g) == GLOPP_IMM_HYPER, "0x800: the ray starts hyper mode instead");
  g = f; g.flags = 0;
  CHECK_MSG(glopp_gate(g) == GLOPP_IMM_NO_MORPH, "no morph block: nothing to turn into");
  g = f; g.hasMorph = 0;
  CHECK_MSG(glopp_gate(g) == GLOPP_IMM_NO_MORPH, "flag without its block");
  g = f; g.morphed = 1;
  CHECK_MSG(glopp_gate(g) == GLOPP_IMM_MORPHING, "monster_morph_start already ran");
  for (int r = 0; r < GLOPP_IMM_COUNT; ++r) CHECK_MSG(glopp_imm_label(r)[0] != '?', "every reason is named");
  CHECK(strcmp(glopp_imm_label(GLOPP_IMM_ANIMAL), "animal") == 0);
  CHECK(strcmp(glopp_imm_label(GLOPP_IMM_COUNT), "?") == 0);
}
// An unrecognised class stays a candidate; the apply fingerprint passes dmgnum's detour.
static void tb_0370_glopp_unread_class_and_fp() {
  CHECK_MSG(!glopp_refuses(GLOPP_UNREAD_CLASS), "unreadClass: only a read refusal excludes");
  CHECK_MSG(!glopp_refuses(GLOPP_OK), "ok fires");
  for (int r = GLOPP_IMM_BOSS; r < GLOPP_IMM_COUNT; ++r) CHECK_MSG(glopp_refuses(r), "every read reason refuses");
  CHECK(glopp_verdict(100.f, 290.f, GLOPP_UNREAD_CLASS) == GLOPP_FIRE);
  CHECK(strcmp(glopp_imm_label(GLOPP_UNREAD_CLASS), "unreadClass") == 0);
  unsigned char b[GLOPP_APPLY_SKIP + GLOPP_APPLY_FP_LEN];
  const unsigned char head[7] = { 0x6A, 0xFF, 0x68, 0xAB, 0xE6, 0x11, 0x10 };
  memcpy(b, head, 7);
  memcpy(b + GLOPP_APPLY_SKIP, kGloppApplyFp, GLOPP_APPLY_FP_LEN);
  CHECK_MSG(glopp_fp_monster_apply(b) == 1, "the stock handler");
  const unsigned char jmp[7] = { 0xE9, 0x10, 0x20, 0x30, 0x40, 0x90, 0x90 };
  memcpy(b, jmp, 7);
  CHECK_MSG(glopp_fp_monster_apply(b) == 1, "dmgnum's detour in front (the gate's failure)");
  b[0] = 0x55;
  CHECK_MSG(glopp_fp_monster_apply(b) == 0, "another head");
  b[0] = 0x6A;
  b[GLOPP_APPLY_SKIP + 30] ^= 0xFF;
  CHECK_MSG(glopp_fp_monster_apply(b) == 0, "another body");
  CHECK(glopp_fp_monster_apply(0) == 0);
}
// Once monster_morph_start 0x100347d0 has run ([+0xDC0]+0x70 = 1), the enemy is
// transforming and never a ray target; the morph animation (+0x71, 0x10034930) is named apart.
static void tb_0369_glopp_morphing() {
  GloppFacts f = {};
  f.read = 1; f.monster = 1; f.flags = GLOPP_FLAG_MORPHABLE; f.health = 20.f; f.hasMorph = 1;
  f.morphed = 1;
  CHECK_MSG(strcmp(glopp_imm_label(glopp_gate(f)), "morphing") == 0, "morph started: morphing");
  f.morphBusy = 1;
  CHECK_MSG(strcmp(glopp_imm_label(glopp_gate(f)), "morphAnim") == 0, "+0x71 is tested first (0x1002fd49)");
  f.morphed = 0;
  CHECK_MSG(strcmp(glopp_imm_label(glopp_gate(f)), "morphAnim") == 0, "the animation alone");
}
static void tb_0368_glopp_verdict() {
  CHECK(glopp_verdict(200.f, 290.f, GLOPP_OK) == GLOPP_FIRE);
  CHECK_MSG(glopp_verdict(290.f, 290.f, GLOPP_OK) == GLOPP_FIRE, "the reach itself is in reach");
  CHECK_MSG(glopp_verdict(300.f, 290.f, GLOPP_OK) == GLOPP_REFUSE_RANGE, "past the beam");
  CHECK_MSG(glopp_verdict(300.f, 290.f, GLOPP_IMM_ANIMAL) == GLOPP_REFUSE_RANGE, "range is judged first");
  CHECK_MSG(glopp_verdict(100.f, 290.f, GLOPP_IMM_ANIMAL) == GLOPP_REFUSE_IMMUNE, "an animal in reach");
}

// The Secret path and Nuke path rows are gone, and their keys: an old ini's values are skipped.
static void tb_0379_paths_config() {
  CHECK(!config_key_known("esp", "secretpath") && !config_key_known("esp", "nukepath"));
}

// --- Level stats ---
static void tb_0380_ls_map_episode_part() {
  int e = -1, p = -1;
  ls_map_episode_part("e01_intro", &e, &p);
  CHECK_MSG(e == 0 && p == 0, "e01_intro is E1P1");
  ls_map_episode_part("e01p02_a01", &e, &p);
  CHECK(e == 0 && p == 1);
  ls_map_episode_part("e08p03_a04", &e, &p);
  CHECK(e == 7 && p == 2);
  ls_map_episode_part("e03p03_a03_r01", &e, &p);
  CHECK_MSG(e == 2 && p == 2, "a side room belongs to its part");
  ls_map_episode_part("", &e, &p);
  CHECK(e == 0 && p == 0);
  ls_map_episode_part(0, &e, &p);
  CHECK(e == 0 && p == 0);
}

static const char kLsDef[] =
  "# episode 1\n"
  "def episode00 {\n"
  "\tname:Episode 1\n"
  "\tsecretcount00:5\n"
  "\tsecretcount01:7 \n"
  "\tnukecount00:10\n"
  "\tnukecount01:11\n"
  "\tkillcount00:29\n"
  "\tkillcount01:58\n"
  "\textrakills0100:6\t# 2 digits for part, 2 for difficulty\n"
  "\textrakills0102:12\n"
  "\tpartcount:2\n"
  "\tpart00:e01_intro\n"
  "}\n"
  "\n"
  "def episode01 {\n"
  "    secretcount00:1\n"
  "\tnukecount00:10\n"
  "\tnukecount01:10\n"
  "\tnukecount02:10\n"
  "\tkillcount00:48\n"
  "\tpartcount:1\n"
  "}\n"
  "def episode03 {\n"
  "\tnukecount00:99\n"
  "\tpartcount:1\n"
  "}\n";

static void tb_0381_ls_def_and_totals() {
  static LsDef d;
  CHECK(ls_parse_def(kLsDef, (int)strlen(kLsDef), &d));
  LsTotals t;
  CHECK(ls_totals(&d, 0, 0, 2, &t) && t.kills == 29 && t.secrets == 5 && t.nukes == 10);
  CHECK_MSG(ls_totals(&d, 0, 1, 0, &t) && t.kills == 64 && t.secrets == 7 && t.nukes == 11, "extrakills by difficulty");
  CHECK_MSG(ls_totals(&d, 0, 1, 1, &t) && t.kills == 58, "a missing extrakills reads 0");
  CHECK(ls_totals(&d, 0, 1, 2, &t) && t.kills == 70);
  CHECK_MSG(ls_totals(&d, 1, 0, 2, &t) && t.secrets == 1 && t.kills == 48, "indented with spaces");
  CHECK_MSG(!ls_totals(&d, 2, 0, 2, &t), "an episode the def lacks has no totals");
  CHECK(!ls_totals(&d, 0, 5, 2, &t) && !ls_totals(&d, -1, 0, 2, &t));
  // Episodes from 0 while defined, parts below partcount: 10 + 11, then 10 (episode01 has one part); episode03
  // follows a gap and is not counted, as the engine's find loop stops at the first missing one.
  CHECK_MSG(ls_game_nukes(&d) == 31, "the game's nuke total");
  CHECK_MSG(!ls_parse_def("# nothing\n", 10, &d), "no episode block");
}

static void tb_0382_ls_hard_nukes() {
  float banked[100];
  for (int i = 0; i < 100; ++i) banked[i] = 0.f;
  banked[0] = 10.f; banked[1] = 10.f; banked[5] = 7.f;
  CHECK(ls_record_index(LS_HARD, 0, 0) == 200 && ls_record_index(LS_HARD, 7, 2) == 237);
  CHECK_MSG(ls_hard_nukes(banked, 100, 1, 2, 4.f) == 31, "first run on Hard: banked plus the live part");
  CHECK_MSG(ls_hard_nukes(banked, 100, 1, 5, 4.f) == 27, "a replay counts the part once, at its best");
  CHECK(ls_hard_nukes(banked, 100, 1, 5, 9.f) == 29);
  CHECK_MSG(ls_hard_nukes(banked, 100, 0, 2, 4.f) == 27, "not on Hard: the banked count only");
  CHECK(ls_hard_nukes(banked, 100, 1, -1, 4.f) == 27);
}

static void tb_0383_ls_lines() {
  char out[LS_LINES][LS_LINE_CAP];
  LsLive live = { 3.f, 1.f, 4.9f };
  LsTotals t = { 29, 5, 10 };
  CHECK(ls_lines(live, &t, 57, 240, out) == 4);
  CHECK(strcmp(out[0], "Kills 3/29") == 0 && strcmp(out[1], "Secrets 1/5") == 0);
  CHECK_MSG(strcmp(out[2], "Nukes 4/10") == 0, "floats truncate");
  CHECK(strcmp(out[3], "Hard nukes 57/240") == 0);
  live.kills = 31.f;
  ls_lines(live, &t, 57, 240, out);
  CHECK_MSG(strcmp(out[0], "Kills 31/31") == 0, "the denominator is never below the count");
  ls_lines(live, 0, 57, 0, out);
  CHECK_MSG(strcmp(out[0], "Kills 31") == 0 && strcmp(out[3], "Hard nukes 57") == 0, "no totals: counts only");
}

static void tb_0384_ls_gate_corner_config() {
  CHECK(ls_visible(1, 1, 0, 0, 0));
  CHECK_MSG(!ls_visible(0, 1, 0, 0, 0), "switch off");
  CHECK_MSG(!ls_visible(1, 0, 0, 0, 0), "no level");
  CHECK_MSG(!ls_visible(1, 1, 1, 0, 0), "screenshot mode");
  CHECK_MSG(!ls_visible(1, 1, 0, 1, 0), "panel open");
  CHECK_MSG(!ls_visible(1, 1, 0, 0, 1), "menu up");
  // Top left: at the top edge when the lines clear the HUD's centred 4:3 group, else under
  // the boss icon (y 10..74 of 768) that group puts at its left.
  int x = -1, y = -1;
  ls_corner_top_left(3440, 1440, 150, &x, &y);
  CHECK_MSG(x == 8 && y == 8, "3440x1440: the group starts at 760 px, the corner is free");
  ls_corner_top_left(1920, 1080, 150, &x, &y);
  CHECK(x == 8 && y == 8);
  ls_corner_top_left(1920, 1080, 300, &x, &y);
  CHECK_MSG(y == 113, "lines that reach the group go under the boss icon");
  ls_corner_top_left(1024, 768, 150, &x, &y);
  CHECK_MSG(x == 8 && y == 82, "4:3: the group is the screen");
  ls_corner_top_left(1024, 1024, 150, &x, &y);
  CHECK_MSG(y == 8 + 99, "narrower than 4:3: no group gap either");
  config_defaults();
  CHECK_MSG(g_cfg.levelStats == 0, "default off");
  CHECK(config_key_known("esp", "levelstats"));
}

// The table is keyed on the player and his world; verdicts are cached per egg.
static EggVerdictCache s_eggCache;
static void tb_0403_egg_level_and_cache() {
  int a = 0, b = 0, w1 = 0, w2 = 0;
  const EggLevelKey k1 = { &a, &w1 }, k2 = { &a, &w2 }, k3 = { &b, &w1 }, k4 = { &a, &w1 };
  CHECK(egg_level_same(k1, k4));
  CHECK_MSG(!egg_level_same(k1, k2), "the same player pointer in a new world is a new level");
  CHECK(!egg_level_same(k1, k3));
  egg_cache_clear(&s_eggCache);
  int wired = -1;
  CHECK(!egg_cache_get(&s_eggCache, &a, &wired));
  egg_cache_put(&s_eggCache, &a, 1);
  egg_cache_put(&s_eggCache, &b, 0);
  CHECK(egg_cache_get(&s_eggCache, &a, &wired) && wired == 1);
  CHECK(egg_cache_get(&s_eggCache, &b, &wired) && wired == 0);
  egg_cache_clear(&s_eggCache);
  CHECK_MSG(!egg_cache_get(&s_eggCache, &a, &wired), "cleared with the table");
  static char ents[EGG_CACHE_MAX + 1];
  for (int i = 0; i <= EGG_CACHE_MAX; ++i) egg_cache_put(&s_eggCache, &ents[i], 1);
  CHECK_MSG(s_eggCache.n == EGG_CACHE_MAX && s_eggCache.full == 1 && !egg_cache_get(&s_eggCache, &ents[EGG_CACHE_MAX], &wired),
            "full: the last is not kept, counted");
}

// --- Progress tab ---
static void pg_put_float(unsigned char* p, float v) { memcpy(p, &v, 4); }

static void tb_0390_pg_record_decode() {
  CHECK_MSG(pg_record_offset(0, 0, 0) == 0x2B8 && pg_record_offset(2, 7, 2) == 0x2B8 + 237 * 0x15C,
            "S+0x2B8+((diff*20+ep)*5+part)*0x15C");
  CHECK_MSG(pg_done_offset(0, 0, 0) == 0x28 && pg_done_offset(1, 2, 3) == 0x28 + 100 + 13, "S+0x28+diff*100+ep*5+part");
  CHECK(PG_SESSION_SPAN == 0x2B8 + 300 * 0x15C);
  unsigned char rec[PG_REC_SIZE] = { 0 };
  pg_put_float(rec + 0x48, 9.9f);
  pg_put_float(rec + 0x4C, 3.f);
  pg_put_float(rec + 0x50, 41.f);
  PgCounts c = { -1, -1, -1 };
  CHECK(pg_record_decode(rec, &c));
  CHECK_MSG(c.nukes == 9 && c.secrets == 3 && c.kills == 41, "nukes +0x48, secrets +0x4C, kills +0x50, truncated");
  const unsigned nanBits = 0x7FC00000u;
  memcpy(rec + 0x4C, &nanBits, 4);
  pg_put_float(rec + 0x50, -2.f);
  CHECK_MSG(!pg_record_decode(rec, &c) && c.nukes == 9 && c.secrets == 0 && c.kills == 0, "a bad field reads 0");
}

static void tb_0391_pg_part_row() {
  const PgCounts none = { 0, 0, 0 }, best = { 10, 5, 35 }, part = { 7, 5, 40 };
  const LsTotals t = { 35, 5, 10 };   // kills, secrets, nukes
  PgRow r = pg_part_row(0, best, 0, &t);
  CHECK_MSG(r.state == PG_UNPLAYED && r.found.nukes == 0 && !r.live, "no finished run: not played, whatever the bytes");
  r = pg_part_row(1, best, 0, &t);
  CHECK(r.state == PG_COMPLETE && r.haveTotal && r.total.kills == 35 && r.total.nukes == 10);
  r = pg_part_row(1, part, 0, &t);
  CHECK_MSG(r.state == PG_INCOMPLETE && r.found.kills == 40, "a missing nuke; extra kills do not make up for it");
  const PgCounts live = { 2, 0, 1 };
  r = pg_part_row(0, none, &live, &t);
  CHECK_MSG(r.state == PG_INCOMPLETE && r.live && r.found.nukes == 2, "the current part: live, never not played");
  r = pg_part_row(1, best, &live, &t);
  CHECK_MSG(r.live && r.found.nukes == 2 && r.state == PG_INCOMPLETE, "a replay shows this run");
  r = pg_part_row(1, best, 0, 0);
  CHECK_MSG(r.state == PG_INCOMPLETE && !r.haveTotal, "no totals: nothing to call complete");
  char b[24];
  pg_readout(b, sizeof b, "", 7, 10, 1, 1);
  CHECK(strcmp(b, "7/10") == 0);
  pg_readout(b, sizeof b, "S ", 0, 5, 1, 0);
  CHECK_MSG(strcmp(b, "S -/5") == 0, "not played: a dash, as the part-end screen's uncounted row");
  pg_readout(b, sizeof b, "K ", 40, 35, 1, 1);
  CHECK_MSG(strcmp(b, "K 40/40") == 0, "the denominator is never below the count");
  pg_readout(b, sizeof b, "K ", 3, 0, 0, 1);
  CHECK(strcmp(b, "K 3") == 0);
}

static const char kPgDef[] =
  "def episode00 {\r\n"
  "\tname:Episode 1\r\n"
  "\ttitle:@@ep_name_01@@\r\n"
  "\tsecretcount00:5\r\n"
  "\tsecretcount01:2\r\n"
  "\tnukecount00:10\r\n"
  "\tnukecount01:10\r\n"
  "\tkillcount00:29\r\n"
  "\tkillcount01:58\r\n"
  "\textrakills0002:6\r\n"
  "\tpartcount:2\r\n"
  "\tpart00:e01_intro   # first\r\n"
  "\tpart01:e01p02_a01\r\n"
  "}\r\n"
  "def episode01 {\n"
  " title:@@ep_name_02@@\n"
  " nukecount00:10\n"
  " secretcount00:1\n"
  " killcount00:48\n"
  " partcount:1\n"
  " part00:e02p01_a01\n"
  "}\n";

static void pg_put_rec(unsigned char* S, int diff, int ep, int part, float n, float s, float k, int done) {
  unsigned char* r = S + pg_record_offset(diff, ep, part);
  pg_put_float(r + 0x48, n);
  pg_put_float(r + 0x4C, s);
  pg_put_float(r + 0x50, k);
  S[pg_done_offset(diff, ep, part)] = (unsigned char)done;
}

static void tb_0392_pg_build() {
  static LsDef d;
  CHECK(ls_parse_def(kPgDef, (int)strlen(kPgDef), &d));
  CHECK_MSG(strcmp(d.title[0], "@@ep_name_01@@") == 0 && strcmp(d.partMap[0][0], "e01_intro") == 0 &&
            strcmp(d.partMap[0][1], "e01p02_a01") == 0 && strcmp(d.partMap[1][0], "e02p01_a01") == 0,
            "titles and part maps, trimmed of comments, blanks and CR");
  unsigned char* S = (unsigned char*)calloc(PG_SESSION_SPAN, 1);
  pg_put_rec(S, 2, 0, 0, 10.f, 5.f, 35.f, 1);
  pg_put_rec(S, 2, 0, 1, 7.f, 2.f, 58.f, 1);
  pg_put_rec(S, 2, 1, 0, 3.f, 0.f, 0.f, 0);   // bytes but no finished run
  pg_put_rec(S, 1, 0, 0, 4.f, 1.f, 9.f, 1);
  const LsLive live = { 3.f, 1.f, 4.f };       // kills, secrets, nukes
  static PgView v;
  pg_build(S, &d, LS_HARD, 1, 0, LS_HARD, &live, &v);
  CHECK(v.ok && v.diff == LS_HARD && v.episodes == 2 && v.parts[0] == 2 && v.parts[1] == 1);
  CHECK_MSG(v.rows[0][0].state == PG_COMPLETE && v.rows[0][0].total.kills == 35, "Hard kills add extrakills");
  CHECK(v.rows[0][1].state == PG_INCOMPLETE && v.rows[0][1].found.nukes == 7);
  CHECK_MSG(v.rows[1][0].live && v.rows[1][0].found.nukes == 4 && v.rows[1][0].found.kills == 3, "the current part");
  CHECK_MSG(v.epFound[0].nukes == 17 && v.epTotal[0].nukes == 20 && v.epTotal[0].kills == 93, "an episode's sums");
  CHECK_MSG(v.epFound[1].nukes == 0 && v.epTotal[1].nukes == 10, "the live part is not banked yet");
  CHECK_MSG(v.hardNukes == 21 && v.hardTotal == 30, "every Hard record's nukes, the live part as max(banked, live)");
  pg_build(S, &d, 1, 1, 0, LS_HARD, &live, &v);
  CHECK_MSG(v.rows[1][0].state == PG_UNPLAYED && !v.rows[1][0].live, "live only on the game's own difficulty");
  CHECK(v.rows[0][0].found.nukes == 4 && v.rows[0][0].total.kills == 29 && v.rows[0][1].state == PG_UNPLAYED);
  pg_build(S, 0, 1, 1, 0, LS_HARD, &live, &v);
  CHECK_MSG(v.ok && v.episodes == 0, "no def: no rows");
  free(S);
}

static void tb_0393_pg_difficulty_default() {
  PgSel s = pg_sel_init();
  CHECK_MSG(pg_sel_step(&s, -1, 1) == 1, "no game yet: Normal");
  CHECK_MSG(pg_sel_step(&s, 2, 0) == 2, "a game: its difficulty");
  s.picked = 0;
  CHECK_MSG(pg_sel_step(&s, 2, 0) == 0, "a pick holds while the game's difficulty holds");
  CHECK_MSG(pg_sel_step(&s, -1, 0) == 0 && pg_sel_step(&s, 2, 0) == 0, "a map change's empty frames do not reset it");
  CHECK_MSG(pg_sel_step(&s, 2, 1) == 2, "a new panel session goes back to the game's");
  s.picked = 0;
  CHECK_MSG(pg_sel_step(&s, 1, 0) == 1, "a new game on another difficulty follows it");
  CHECK(pg_sel_step(&s, 7, 1) == 1);
}

static void tb_0394_pg_names_and_layout() {
  const char local[] = "# names\r\nep_name_01:\"ROOFTOP REBELLION\"\r\nep_name_010:\"WRONG\"\r\n"
                       "i_easy:\"EASY\" # comment\r\n  i_hard : HARD\r\nep_name_02:\"PR\xc9SIDENT\"\r\n";
  const int n = (int)strlen(local);
  char b[48];
  CHECK(pg_local_value(local, n, "ep_name_01", b, sizeof b) && strcmp(b, "ROOFTOP REBELLION") == 0);
  CHECK_MSG(pg_local_value(local, n, "i_hard", b, sizeof b) && strcmp(b, "HARD") == 0, "unquoted, spaced");
  CHECK(!pg_local_value(local, n, "i_normal", b, sizeof b));
  static LsDef d;
  ls_parse_def(kPgDef, (int)strlen(kPgDef), &d);
  static PgNames nm;
  pg_names(local, n, &d, &nm);
  CHECK(strcmp(nm.diff[0], "Easy") == 0 && strcmp(nm.diff[1], "Normal") == 0 && strcmp(nm.diff[2], "Hard") == 0);
  CHECK_MSG(strcmp(nm.ep[0], "ROOFTOP REBELLION") == 0 && nm.ep[1][0] == 0, "a name the font cannot draw is dropped");
  pg_episode_heading(0, nm.ep[0], b, sizeof b);
  CHECK(strcmp(b, "EPISODE 1: ROOFTOP REBELLION") == 0);
  pg_episode_heading(1, nm.ep[1], b, sizeof b);
  CHECK(strcmp(b, "EPISODE 2") == 0);
  // The default panel's row (474 px): columns 55/77/99, a sub-row label ending at 74.
  const int cols[3] = { 55, 77, 99 };
  PgLayout L = pg_part_layout(474, 10, 74, cols, 10, 38, 79);
  CHECK(L.col[2] == 365 && L.col[1] == 282 && L.col[0] == 221);
  CHECK_MSG(L.tagX == 80 && L.pipX == 127 && L.mapX < 0, "Now tag and ten pips fit; the map name does not");
  L = pg_part_layout(700, 10, 74, cols, 10, 0, 79);
  CHECK_MSG(L.tagX < 0 && L.mapX == 80 && L.pipX == 353, "a wide panel shows the map name");
  L = pg_part_layout(414, 10, 74, cols, 10, 38, 79);
  CHECK_MSG(L.tagX == 80 && L.pipX < 0, "the narrowest panel drops the pips; the readout keeps N/M");
  L = pg_part_layout(474, 10, 74, cols, PG_PIPS_MAX + 1, 0, 0);
  CHECK_MSG(L.pipX < 0, "more nukes than pips: none");
  L = pg_part_layout(474, 10, 45, cols, 0, 0, 0);
  CHECK_MSG(L.barX == 51 && L.barW == 221 - 6 - 51, "the Total row's bar spans to the readouts");
}

// --- Roach eggs (eggrule.h, docs/re/entities.md section 8) ---
static void tb_0396_egg_class_and_label() {
  const EntClass c = class_lookup("roach_pod");
  CHECK_MSG(c.cat == CAT_ENEMY && name_is(c, "Roach Egg") && c.tier == TIER_NONE, "roach_pod is an enemy, Roach Egg");
  CHECK_MSG(class_lookup("roach_pod1").cat == CAT_OTHER, "the class name is exact; the 1 is an entity name's");
  CHECK(strcmp(egg_label(EGG_ACTIVE), "Roach Egg") == 0);
  CHECK(strcmp(egg_label(EGG_INACTIVE), "Roach Egg (inactive)") == 0);
  CHECK(strcmp(egg_label(EGG_UNREAD), "Roach Egg (unread)") == 0);
  char n[EGG_NAME_LEN];
  CHECK_MSG(egg_wire_target("roach_pod17.hatch", n, sizeof n) && strcmp(n, "roach_pod17") == 0, "a hatch wire");
  CHECK_MSG(!egg_wire_target("roach_pod17.hatched", n, sizeof n), "the output pin is not the input");
  CHECK(!egg_wire_target("timer1.active", n, sizeof n) && !egg_wire_target(".hatch", n, sizeof n));
  CHECK(!egg_wire_target("hatch", n, sizeof n) && !egg_wire_target(0, n, sizeof n));
  char tiny[4];
  CHECK_MSG(!egg_wire_target("roach_pod17.hatch", tiny, sizeof tiny), "a name that does not fit is no match");
  const char names[2][EGG_NAME_LEN] = { "roach_pod17", "roach_pod20" };
  CHECK(egg_wired(names, 2, "roach_pod20") && egg_wired(names, 2, "Roach_Pod17"));
  CHECK(!egg_wired(names, 2, "roach_pod2") && !egg_wired(names, 0, "roach_pod17") && !egg_wired(names, 2, 0));
}

static void tb_0397_egg_state_and_target() {
  EggFacts f = { 0, 1, 1, 25.f, 1 };
  CHECK_MSG(egg_state(f) == EGG_ACTIVE && egg_targetable(EGG_ACTIVE), "an intact wired egg is a target");
  f.wired = 0;
  CHECK_MSG(egg_state(f) == EGG_INACTIVE && !egg_targetable(EGG_INACTIVE), "no wire: it never hatches");
  f.wired = -1;
  CHECK_MSG(egg_state(f) == EGG_UNREAD && !egg_targetable(EGG_UNREAD), "an unread scan targets nothing");
  const EggFacts hatched = { 1, 1, 1, 25.f, 1 }, dead = { 0, 0, 1, 0.f, 1 }, gibbed = { 0, 1, 0, 25.f, 1 },
                 hurt = { 0, 1, 1, -3.f, 1 };
  CHECK(egg_state(hatched) == EGG_GONE && egg_state(dead) == EGG_GONE && egg_state(gibbed) == EGG_GONE &&
        egg_state(hurt) == EGG_GONE);
  EntSnap s;
  memset(&s, 0, sizeof s);
  s.cat = CAT_ENEMY; s.hashp = 1; s.hp = 25.f; s.maxhp = EGG_HEALTH_MAX; s.egg = EGG_GONE;
  CHECK_MSG(!ent_alive(s), "a hatched egg at full health is gone");
  s.egg = EGG_INACTIVE;
  CHECK(ent_alive(s));
  CHECK(strcmp(boss_imm_label(BOSS_IMM_EGG), "eggIdle") == 0 && !aim_target_ok(1, 1, BOSS_IMM_EGG));
  GloppFacts g;
  memset(&g, 0, sizeof g);
  g.egg = 1;
  CHECK_MSG(glopp_gate(g) == GLOPP_IMM_EGG && glopp_refuses(GLOPP_IMM_EGG), "the beam's type 4 never hurts an egg");
  CHECK(strcmp(glopp_imm_label(GLOPP_IMM_EGG), "egg") == 0);
}

static void tb_0404_ovk_doomed_rule() {
  OvkPending p = { 15.f, 0.f, 0.f, 0.f };
  CHECK_MSG(ovk_cause(20.f, p, 1) == OVK_ALIVE, "15 sent at 20 health: one more shot is needed");
  p.dealt = 20.f;
  CHECK_MSG(ovk_cause(20.f, p, 1) == OVK_DEALT, "dealt reaches the health: dead at <= 0");
  p.dealt = 15.f; p.direct = 5.f;
  CHECK_MSG(ovk_cause(20.f, p, 0) == OVK_INFLIGHT, "the in-flight hit completes it");
  const OvkPending s = { 0.f, 0.f, 25.f, 0.f };
  CHECK(ovk_cause(20.f, s, 0) == OVK_SPLASH);
  CHECK_MSG(ovk_cause(0.f, s, 1) == OVK_ALIVE, "already dead is no doom (ent_alive refuses it)");
  CHECK(strcmp(ovk_cause_label(OVK_DEALT), "dealt") == 0 && strcmp(ovk_cause_label(OVK_INFLIGHT), "inFlight") == 0 &&
        strcmp(ovk_cause_label(OVK_SPLASH), "splash") == 0 && strcmp(ovk_cause_label(OVK_BURST), "burst") == 0);
  CHECK(ovk_unapplied(20.f, 15.f, 20.f) == 15.f);
  CHECK_MSG(ovk_unapplied(20.f, 15.f, 5.f) == 0.f, "applied: health fell by the amount sent");
  CHECK(ovk_unapplied(20.f, 30.f, 5.f) == 15.f);
  CHECK_MSG(ovk_unapplied(20.f, 10.f, 25.f) == 10.f, "health that rose applies nothing");
  CHECK(ovk_send_counts(1, 15.f, 0) && !ovk_send_counts(3, 15.f, 0) && !ovk_send_counts(4, 15.f, 0));
  CHECK_MSG(!ovk_send_counts(6, 9.f, 0x200) && ovk_send_counts(6, 9.f, 0) && !ovk_send_counts(1, 0.f, 0),
            "0x200 drops types 6-8; a zero amount takes nothing");
  CHECK(ovk_send_keep(5.f, 100) && !ovk_send_keep(0.f, 100) && !ovk_send_keep(5.f, OVK_SEND_TTL_MS + 1));
  OvkLedger l;
  memset(&l, 0, sizeof l);
  ovk_ledger_note(&l, (void*)0x10, 20.f, 15.f, 0);
  ovk_ledger_note(&l, (void*)0x10, 20.f, 4.f, 10);
  CHECK_MSG(l.n == 1 && l.s[0].hpFirst == 20.f && l.s[0].sent == 19.f && l.s[0].tLast == 10, "one run per monster");
  ovk_ledger_note(&l, (void*)0x20, 7.f, 3.f, 11);
  ovk_ledger_drop(&l, 0);
  CHECK(l.n == 1 && l.s[0].ent == (void*)0x20);
}
static void tb_0405_ovk_pending_direct_and_splash() {
  OvkTable t;
  ovk_reset(&t);
  ovk_add(&t, (void*)1, OVK_INFLIGHT, 30.f);
  ovk_add(&t, (void*)1, OVK_SPLASH, 10.f);
  ovk_add(&t, (void*)2, OVK_SPLASH, 5.f);
  const OvkPending a = ovk_get(t, (void*)1), c = ovk_get(t, (void*)3);
  CHECK(a.direct == 30.f && a.splash == 10.f && a.dealt == 0.f && c.splash == 0.f && t.n == 2);
  for (int i = 0; i < 64; ++i) ovk_add(&t, (void*)(size_t)(100 + i), OVK_SPLASH, 1.f);
  CHECK_MSG(t.n == OVK_MAX && t.full == 2, "a full table counts what it drops");
  const Vec3 o = { 0.f, 0.f, 0.f }, h = { 10.f, 10.f, 10.f };
  const Vec3 in = { 5.f, 0.f, 0.f }, face = { 11.f, 0.f, 0.f }, edge = { 20.f, 20.f, 0.f };
  CHECK(ovk_splash_dist(in, o, h) == 0.f);
  CHECK(fabsf(ovk_splash_dist(face, o, h) - 1.f) < 1e-4f);
  CHECK_MSG(fabsf(ovk_splash_dist(edge, o, h) - 14.1421f) < 1e-3f, "the nearest corner's plane");
  CHECK(ovk_splash_damage(150.f, 0.f, 30.f) == 30.f && ovk_splash_damage(150.f, 75.f, 30.f) == 15.f);
  CHECK_MSG(ovk_splash_damage(150.f, 150.f, 30.f) == 0.f && ovk_splash_damage(150.f, 200.f, 30.f) == 0.f,
            "nothing at or past the radius");
  CHECK(ovk_plasma_direct(70.f, 1.f, 0, 1) == 70.f && ovk_plasma_direct(140.f, 1.f, 0, 1) == 100.f);
  CHECK_MSG(ovk_plasma_direct(140.f, 1.f, 1, 1) == 140.f && ovk_plasma_direct(140.f, .5f, 0, 1) == 140.f &&
            ovk_plasma_direct(140.f, 1.f, 0, 0) == 140.f, "the cap needs a full charge, a monster, no pierce yet");
  CHECK_MSG(ovk_proj_stops(0x100, 0, 0) && ovk_proj_stops(3, 0x20, 0), "walls and monsters stop a rocket");
  CHECK_MSG(!ovk_proj_stops(3, 0x20, 1) && !ovk_proj_stops(3, 0x100, 0) && !ovk_proj_stops(3, 0x1000, 0) &&
            !ovk_proj_stops(3, 0x400, 0) && !ovk_proj_stops(0x100, 0x10000, 0),
            "it flies through a dying monster, Duke, pickups, projectiles, creature blockers");
  CHECK(ovk_proj_kind("actor_proj_bullet") == OVK_P_BULLET && ovk_proj_kind("actor_proj_prpg") == OVK_P_ROCKET &&
        ovk_proj_kind("actor_proj_plasma") == OVK_P_PLASMA && ovk_proj_kind("actor_proj_pbomb") == OVK_P_PBOMB &&
        ovk_proj_kind("actor_proj_gray") == OVK_P_NONE && ovk_proj_kind(0) == OVK_P_NONE);
}
static void tb_0406_ovk_burst() {
  CHECK(ovk_burst_damage(5, 4.f) == 20.f && ovk_burst_damage(0, 4.f) == 0.f && ovk_burst_damage(-2, 4.f) == 0.f);
  const OvkPending p = { 4.f, 0.f, 0.f, ovk_burst_damage(4, 4.f) };
  CHECK_MSG(ovk_cause(20.f, p, 1) == OVK_BURST, "the Triggerbot stops: the burst's 4 rounds finish it");
  CHECK_MSG(ovk_cause(20.f, p, 0) == OVK_ALIVE, "the pick keeps it: those rounds fly at the lock");
  unsigned char b[OVK_SEND_FP_LEN];
  memcpy(b, kOvkSendFp, sizeof b);
  CHECK(ovk_fp_monster_send(b));
  b[0] = 0xE9; b[1] = 0x11; b[2] = 0x22; b[3] = 0x33; b[4] = 0x44; b[5] = 0x90;
  CHECK_MSG(ovk_fp_monster_send(b), "our own detour's head");
  b[10] = 0x01;
  CHECK(!ovk_fp_monster_send(b) && !ovk_fp_monster_send(0));
}

static void tb_0407_ovk_review_fixes() {
  unsigned char b[OVK_SEND_FP_LEN];
  memcpy(b, kOvkSendFp, sizeof b);
  CHECK(ovk_send_slot_kind(b) == OVK_SLOT_STOCK);
  b[0] = 0xE9; b[5] = 0x90;
  CHECK(ovk_send_slot_kind(b) == OVK_SLOT_DETOURED);
  static const unsigned char fly[OVK_SEND_FP_LEN] = { 0x8B, 0x54, 0x24, 0x08, 0x89, 0x54, 0x24, 0x08, 0xE9, 0x73, 0x7B };
  CHECK_MSG(ovk_send_slot_kind(fly) == OVK_SLOT_OTHER && ovk_send_slot_kind(0) == OVK_SLOT_OTHER,
            "monst_fly's wrapper is skipped, not a refusal");
  CHECK(ovk_burst_left(6, 4) == 4 && ovk_burst_left(6, 0) == 0 && ovk_burst_left(6, 500) == 64);
  CHECK_MSG(ovk_burst_left(5, 4) == 0 && ovk_burst_left(1, 5) == 0, "out of the attack state no round fires");
  int prev = 0, n = 0;
  for (int i = 0; i < 4; ++i) n += ovk_withheld_step(1, 1, 1, &prev);
  CHECK_MSG(n == 2, "a semi-automatic presses every other frame");
  prev = 0; n = 0;
  for (int i = 0; i < 4; ++i) n += ovk_withheld_step(1, 1, 0, &prev);
  CHECK_MSG(n == 1, "an automatic's hold is one");
  n = ovk_withheld_step(0, 1, 0, &prev) + ovk_withheld_step(1, 1, 0, &prev) + ovk_withheld_step(1, 0, 0, &prev);
  CHECK_MSG(n == 1, "no fire decision or no doom withholds nothing");
  AimCand c[3] = { { 7, 1.f, { 0, 0, 0 } }, { 3, 2.f, { 0, 0, 0 } }, { 9, 3.f, { 0, 0, 0 } } };
  CHECK(aim_cand_front(c, 3, 9) && c[0].idx == 9 && c[1].idx == 7 && c[2].idx == 3);
  CHECK(!aim_cand_front(c, 3, 42) && c[0].idx == 9);
}

static HurtEv hurt_ev(unsigned t, int loss) {
  HurtEv e = {};
  e.t = t; e.loss = loss; e.sends = 1; e.type = 1; e.amount = (float)loss; e.src = 0;
  return e;
}

static void tb_0408_hurt_burst_window() {
  HurtBurst b;
  hurt_reset(&b);
  hurt_feed(&b, hurt_ev(1000, 20));
  hurt_feed(&b, hurt_ev(1100, 20));
  CHECK_MSG(!b.open, "40 inside a window is no burst");
  hurt_feed(&b, hurt_ev(1400, 20));
  CHECK_MSG(!b.open && b.n == 1, "drops a window old leave the sliding list");
  hurt_reset(&b);
  hurt_feed(&b, hurt_ev(5000, 25));
  hurt_feed(&b, hurt_ev(5249, 25));
  CHECK_MSG(b.open && b.tFirst == 5000 && b.total == 50, "50 inside 250 ms opens a burst");
  CHECK(!hurt_close_due(b, 5498));
  CHECK(hurt_close_due(b, 5499));
  hurt_reset(&b);
  hurt_feed(&b, hurt_ev(0xFFFFFF80u, 30));   // 144 ms before the next
  hurt_feed(&b, hurt_ev(0x00000010u, 30));
  CHECK_MSG(b.open && b.n == 2, "the window is exact across the clock's wrap");
  // The capture: sixteen 25s and an 11 at 175 fps, one list, one total.
  hurt_reset(&b);
  for (int i = 0; i < 16; ++i) hurt_feed(&b, hurt_ev(9000u + 6u * (unsigned)i, 25));
  hurt_feed(&b, hurt_ev(9096, 11));
  CHECK(b.open && b.n == 17 && b.total == 411 && b.tLast == 9096 && b.dropped == 0);
  CHECK(!hurt_close_due(b, 9300) && hurt_close_due(b, 9346));
  hurt_reset(&b);
  for (int i = 0; i < HURT_KEEP + 6; ++i) hurt_feed(&b, hurt_ev(100u + (unsigned)i, 10));
  CHECK_MSG(b.n == HURT_KEEP && b.dropped == 6 && b.total == 10 * (HURT_KEEP + 6), "a full burst counts the rest");
}

static void tb_0409_hurt_quiet_and_tally() {
  HurtQuiet q = {};
  unsigned held = 99;
  CHECK(hurt_quiet_pass(&q, 7, 1000, &held) && held == 0);
  CHECK(!hurt_quiet_pass(&q, 7, 1500, &held));
  CHECK(!hurt_quiet_pass(&q, 7, 1999, &held));
  CHECK_MSG(hurt_quiet_pass(&q, 7, 2000, &held) && held == 2, "a second later the same line prints with its held count");
  CHECK_MSG(hurt_quiet_pass(&q, 8, 2001, &held) && held == 0, "a different line always prints");
  HurtTally t = {};
  hurt_tally_add(&t, 3, 1, 25.f);
  hurt_tally_add(&t, 3, 1, 25.f);
  hurt_tally_add(&t, 3, 2, 4.f);
  CHECK(t.n == 2 && t.count[0] == 2 && t.amount[0] == 50.f && t.type[1] == 2 && t.count[1] == 1);
  for (int i = 0; i < HURT_TALLY_MAX + 3; ++i) hurt_tally_add(&t, 100 + i, 0, 1.f);
  CHECK(t.n == HURT_TALLY_MAX && t.full == 5);
}

static void tb_0410_queen_swipe_gate() {
  QsGate g = {};
  static int objs_[2]; int& q1 = objs_[0]; int& q2 = objs_[1];
  CHECK(qs_gate(&g, 1, 1, &q1, 100) && !qs_gate(&g, 1, 1, &q1, 100) && !qs_gate(&g, 1, 1, &q1, 100));
  CHECK_MSG(qs_gate(&g, 1, 0, &q1, 100), "block 2 and every other send pass");
  CHECK_MSG(qs_gate(&g, 1, 1, &q1, 101) && !qs_gate(&g, 1, 1, &q1, 101), "a new swipe resets the gate");
  CHECK_MSG(qs_gate(&g, 1, 1, &q2, 101), "another Queen is another attack");
  CHECK_MSG(qs_gate(&g, 0, 1, &q2, 101) && qs_gate(&g, 0, 1, &q2, 101), "off: everything passes");
  unsigned char img[200];
  memset(img, 0x90, sizeof img);
  memcpy(img + 50, kQsSite, QS_SITE_LEN);
  memcpy(img + 120, kQsSite, QS_SITE_LEN);
  img[120 + 38] = 0xC2; img[120 + 39] = 0x01;   // block 2: push 0x1C2
  unsigned char mask[QS_SITE_LEN];
  memset(mask, 1, sizeof mask);
  size_t at = 0;
  CHECK(offs_find(img, sizeof img, kQsSite, mask, QS_SITE_LEN, &at) == 1 && at == 50);
  CHECK(img[50 + QS_SITE_RET - 6] == 0xFF && img[50 + QS_SITE_RET - 5] == 0x92);
}
static void tb_0411_queenfix_config_default_on() {
  config_defaults();
  CHECK(g_cfg.queenFix == 1);
  CHECK(config_key_known("framerate", "queenswipe"));
}

// Hide what you can't take decides by the switch first: off never hides, whatever the pools say.
static void tb_0412_hide_cant_take_switch() {
  CHECK(!item_hidden_by_take(0, TAKE_NO) && !item_hidden_by_take(0, TAKE_YES) && !item_hidden_by_take(0, TAKE_UNKNOWN));
  CHECK(item_hidden_by_take(1, TAKE_NO));
  CHECK(!item_hidden_by_take(1, TAKE_YES) && !item_hidden_by_take(1, TAKE_UNKNOWN));
}

// How each weapon's damage enters a monster (aim.md section 11): bullets and splash through vt+0x194, the plasma
// and the X-3000 straight into vt+0x198; the ray is left to its own gate.
static void tb_0413_wimm_weapon_routes() {
  const int bullets[3] = { WK_GEAGLE, WK_SHOTGUN, WK_ARIFLE };
  for (int i = 0; i < 3; ++i) {
    const WimmSpec s = wimm_spec(bullets[i]);
    CHECK(s.known && s.via194 && !s.splash && s.type == 0);
  }
  const WimmSpec r = wimm_spec(WK_PRPG), b = wimm_spec(WK_PBOMB);
  CHECK(r.known && r.via194 && r.splash && r.type == 9);
  CHECK(b.known && b.via194 && b.splash && b.type == 9);
  const WimmSpec p = wimm_spec(WK_PCANNON), x = wimm_spec(WK_X3000);
  CHECK(p.known && !p.via194 && p.type == 10);
  CHECK(x.known && !x.via194 && x.type == 0);
  CHECK_MSG(!wimm_spec(WK_GRAY).known, "the ray has its own gate");
  CHECK(!wimm_spec(WK_NONE).known && !wimm_spec(WK_OTHER).known);
  CHECK(wimm_class(0, 1, 0) == WIMM_CLASS_SMECH);
  CHECK(wimm_class(1, 0, 1) == WIMM_CLASS_SHELL);
  CHECK(wimm_class(1, 0, 0) == WIMM_CLASS_MONSTER);
  CHECK_MSG(wimm_class(0, 0, 1) == WIMM_CLASS_UNREAD, "an unrecognised vt+0x194 is unread");
}

// The gate in the engine's order: vt+0x194 (the SecurityMech's hit-height test), vt+0x198 (the roachoid shell),
// vt+0x2FC (flag 0x200 types). Only a read refusal excludes.
static void tb_0414_wimm_gate_order() {
  WimmFacts m;
  memset(&m, 0, sizeof m);
  m.cls = WIMM_CLASS_SMECH; m.read = 1; m.scale = 1.25f; m.k = 10.f; m.hit = 1; m.hitLocalY = 5.f;
  const WimmSpec gun = wimm_spec(WK_SHOTGUN);
  CHECK_MSG(wimm_gate(gun, m) == WIMM_IMM_LOW_HIT && wimm_refuses(WIMM_IMM_LOW_HIT), "a bullet below scale * k");
  m.hitLocalY = 12.5f;
  CHECK_MSG(wimm_gate(gun, m) == WIMM_IMM_LOW_HIT, "the engine sends only strictly above");
  m.hitLocalY = 12.6f;
  CHECK(wimm_gate(gun, m) == WIMM_OK);
  m.hitLocalY = sqrtf(-1.f);
  CHECK_MSG(wimm_gate(gun, m) == WIMM_IMM_LOW_HIT, "a NaN takes the no-send branch");
  m.hitLocalY = 5.f;
  CHECK_MSG(wimm_gate(wimm_spec(WK_PRPG), m) == WIMM_OK, "a splash info passes the mech's test");
  CHECK_MSG(wimm_gate(wimm_spec(WK_PBOMB), m) == WIMM_OK, "the pipebomb's splash too");
  CHECK_MSG(wimm_gate(wimm_spec(WK_PCANNON), m) == WIMM_OK, "the plasma skips vt+0x194");
  CHECK_MSG(wimm_gate(wimm_spec(WK_X3000), m) == WIMM_OK, "the X-3000 skips vt+0x194");
  CHECK_MSG(wimm_gate(wimm_spec(WK_GRAY), m) == WIMM_OK, "the ray is not judged here");
  m.hit = 0;
  CHECK_MSG(wimm_gate(gun, m) == WIMM_NO_HIT && !wimm_refuses(WIMM_NO_HIT), "no hit point: a candidate");
  WimmFacts s;
  memset(&s, 0, sizeof s);
  s.cls = WIMM_CLASS_SHELL; s.read = 1; s.state = WIMM_SHELL_STATE;
  CHECK(wimm_gate(gun, s) == WIMM_IMM_SHELL && wimm_gate(wimm_spec(WK_PRPG), s) == WIMM_IMM_SHELL);
  CHECK(wimm_gate(wimm_spec(WK_PCANNON), s) == WIMM_IMM_SHELL && wimm_gate(wimm_spec(WK_X3000), s) == WIMM_IMM_SHELL);
  s.state = 3;
  CHECK(wimm_gate(gun, s) == WIMM_OK);
  WimmSpec t7 = { 1, 0, 0, 7 };
  WimmFacts f;
  memset(&f, 0, sizeof f);
  f.cls = WIMM_CLASS_MONSTER; f.read = 1; f.flags = WIMM_FLAG_IGNORE_678;
  CHECK(wimm_gate(t7, f) == WIMM_IMM_TYPE);
  for (int k = WK_GEAGLE; k < WK_OTHER; ++k) CHECK_MSG(wimm_gate(wimm_spec(k), f) == WIMM_OK, "no weapon of Duke's deals 6-8");
  f.flags = 0;
  CHECK(wimm_gate(t7, f) == WIMM_OK);
  f.cls = WIMM_CLASS_UNREAD;
  CHECK(wimm_gate(gun, f) == WIMM_UNREAD_CLASS && !wimm_refuses(WIMM_UNREAD_CLASS));
  f.cls = WIMM_CLASS_MONSTER; f.read = 0;
  CHECK(wimm_gate(gun, f) == WIMM_UNREAD && !wimm_refuses(WIMM_UNREAD));
  CHECK(!strcmp(wimm_label(WIMM_IMM_LOW_HIT), "lowHit") && !strcmp(wimm_label(WIMM_COUNT), "?"));
}

// The SecurityMech's handler and the roachoid wrapper, as read in Ghidra 2026-10-02 (0x1002a9b0, 0x100290b0);
// mult_point_matrix's row-vector product for the local y.
static void tb_0415_wimm_fingerprints_and_local_y() {
  unsigned char sm[WIMM_SMECH_FP_LEN];
  memcpy(sm, kWimmSmechFp, sizeof sm);
  const unsigned char imp[4] = { 0xA0, 0x61, 0x12, 0x10 }, kk[4] = { 0xD4, 0x6C, 0x12, 0x10 };
  memcpy(sm + 25, imp, 4);
  memcpy(sm + WIMM_SMECH_K_AT, kk, 4);
  unsigned ka = 0;
  CHECK(wimm_fp_smech(sm, &ka) && ka == 0x10126CD4u);
  sm[27] = 0x19; sm[28] = 0x0A; sm[59] = 0x19; sm[60] = 0x0A;   // relocated to duke_base 0x0A070000
  CHECK(wimm_fp_smech(sm, &ka) && ka == 0x0A196CD4u);
  sm[49] = 0xD8;
  CHECK_MSG(!wimm_fp_smech(sm, &ka), "the scale load is part of the fingerprint");
  CHECK(!wimm_fp_smech(0, &ka));
  const unsigned char sh[WIMM_SHELL_LEN] = { 0x83, 0xB9, 0x68, 0x0C, 0x00, 0x00, 0x0B, 0x75, 0x08, 0xC7, 0x44, 0x24, 0x08,
                                             0x00, 0x00, 0x00, 0x00, 0x8B, 0x54, 0x24, 0x08, 0x89, 0x54, 0x24, 0x08, 0xE9,
                                             0x22, 0x63, 0x00, 0x00 };
  unsigned tgt = 0;
  CHECK(wimm_fp_shell(sh, 0x100290B0u, &tgt) && tgt == 0x1002F3F0u);
  unsigned char sh2[WIMM_SHELL_LEN];
  memcpy(sh2, sh, sizeof sh2);
  sh2[6] = 0x0C;
  CHECK_MSG(!wimm_fp_shell(sh2, 0x100290B0u, &tgt), "another state is another wrapper");
  float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -20, 0, 1 };
  const Vec3 p = { 1.f, 30.f, 2.f };
  CHECK(wimm_local_y(p, m) == 10.f);
  m[1] = 1.f; m[5] = 0.f;   // x lands on y
  CHECK(wimm_local_y(p, m) == 1.f - 20.f);
}

// The Pulse Cannon tap (weap_pcannon_fire_logic 0x100588a0): press while nothing is charged, release while it
// is (the release discharges); an unread charge alternates frames. The uncharged shot's damage and charge.
static void tb_0416_tbot_pcannon_tap() {
  int last = 0;
  CHECK_MSG(tbot_pcannon_press(1, 1, 0.f, &last) == 1 && last == 1, "nothing charged: press");
  CHECK_MSG(tbot_pcannon_press(1, 1, 0.02f, &last) == 0 && last == 0, "charged: release, the shot leaves");
  CHECK(tbot_pcannon_press(1, 1, 0.f, &last) == 1);
  CHECK_MSG(tbot_pcannon_press(0, 1, 0.02f, &last) == 0, "the condition broke: release too");
  CHECK(tbot_pcannon_press(0, 1, 0.f, &last) == 0);
  last = 0;
  CHECK(tbot_pcannon_press(1, 0, 0.f, &last) == 1 && tbot_pcannon_press(1, 0, 0.f, &last) == 0 &&
        tbot_pcannon_press(1, 0, 0.f, &last) == 1);
  // proj_ammo_rate 50 (weapons.def): 0.02 a unit; a 6 ms press charges 0.0015 by time, so the unit wins.
  CHECK(fabsf(weap_pcannon_first_charge(1.f / 50.f, 0.006f) - 0.02f) < 1e-6f);
  CHECK_MSG(fabsf(weap_pcannon_first_charge(0.02f, 0.4f) - 0.1f) < 1e-6f, "a long first frame charges by time");
  CHECK(weap_pcannon_first_charge(0.02f, 9.f) == 1.f);
  // proj_damage 70, proj_min_damage 10: (70 - 10) * c + 10.
  CHECK(fabsf(ovk_plasma_charge_damage(70.f, 10.f, 0.02f) - 11.2f) < 1e-4f);
  CHECK(ovk_plasma_charge_damage(70.f, 10.f, 1.f) == 70.f && ovk_plasma_charge_damage(70.f, 10.f, 0.f) == 10.f);
}

// The Pulse Cannon's tap cadence: a human rate, at most one release every 125 ms, by
// wall time; the first tap is never held back, and the unsigned clock may wrap.
static void tb_0417_tbot_pcannon_cadence() {
  CHECK(TBOT_PCANNON_GAP_MS == 125u);
  CHECK_MSG(tbot_pcannon_ready(0, 5u, 0u), "no release yet: ready");
  CHECK(!tbot_pcannon_ready(1, 1124u, 1000u));
  CHECK(tbot_pcannon_ready(1, 1125u, 1000u) && tbot_pcannon_ready(1, 2000u, 1000u));
  CHECK_MSG(tbot_pcannon_ready(1, 200u, 0xFFFFFFF0u), "across the clock's wrap");
  CHECK(!tbot_pcannon_ready(1, 10u, 0xFFFFFFF0u));
}

// Puzzle solutions: which maps carry a puzzle, and the gate (near, not solved, row on).
static void tb_0420_pz_map_and_gate() {
  CHECK(pz_map_puzzle("e05p02_a01") == PZ_E05);
  CHECK(pz_map_puzzle("e07p03_a01") == PZ_E07);
  CHECK(pz_map_puzzle("e05p02_a02") == PZ_NONE && pz_map_puzzle("e07p03") == PZ_NONE);
  CHECK(pz_map_puzzle("") == PZ_NONE && pz_map_puzzle(0) == PZ_NONE);
  const Vec3 o = { 0.f, 0.f, 0.f }, in = { 1499.f, 0.f, 0.f }, out = { 1501.f, 0.f, 0.f };
  const Vec3 diag = { 900.f, 900.f, 900.f };
  CHECK(pz_near(in, o) && !pz_near(out, o));
  CHECK(!pz_near(diag, o));   // 3D: a floor above is not near
  const Vec3 two[2] = { { 0.f, 10.f, 20.f }, { 100.f, 30.f, -20.f } };
  const Vec3 c = pz_centre(two, 2);
  CHECK(c.x == 50.f && c.y == 20.f && c.z == 0.f);
  CHECK(pz_show(1, true, false));
  CHECK(!pz_show(0, true, false) && !pz_show(1, false, false) && !pz_show(1, true, true));
}

// e05p02_a01: only Yellow, Blue, Red passes; each flip-flop toggles on its plate and is reset by the earlier ones.
static void tb_0421_pz_e05_rule() {
  PzE05 s = { { 0, 0, 0 }, 0, 0, 0, 0 };
  CHECK(!pz_e05_wrong(s) && !pz_e05_solved(s));
  PzE05 y = { { 1, 0, 0 }, 1, 0, 0, 0 };
  CHECK(!pz_e05_wrong(y));
  PzE05 yb = { { 1, 1, 0 }, 1, 1, 0, 0 };
  CHECK(!pz_e05_wrong(yb) && !pz_e05_solved(yb));
  PzE05 ybr = { { 1, 1, 1 }, 1, 1, 1, 0 };
  CHECK(pz_e05_solved(ybr) && !pz_e05_wrong(ybr));
  PzE05 plat = { { 0, 0, 0 }, 0, 0, 0, 1 };   // after the payoff the plates rise again
  CHECK(pz_e05_solved(plat));
  PzE05 b = { { 0, 1, 0 }, 0, 1, 0, 0 };      // Blue first: F2 toggled, but no Yellow before it
  CHECK(pz_e05_wrong(b));
  PzE05 by = { { 1, 1, 0 }, 1, 0, 0, 0 };     // then Yellow: F2 reset
  CHECK(pz_e05_wrong(by));
  PzE05 r = { { 0, 0, 1 }, 0, 0, 1, 0 };
  CHECK(pz_e05_wrong(r));
  PzE05 yr = { { 1, 0, 1 }, 1, 0, 1, 0 };
  CHECK(pz_e05_wrong(yr));
  PzE05 byr = { { 1, 1, 1 }, 1, 0, 1, 0 };
  CHECK(pz_e05_wrong(byr) && !pz_e05_solved(byr));
}

// e07p03_a01: switch_1, switch_2, switch_3; a cog's target follows its motion, else its opened value.
static void tb_0422_pz_e07_rule() {
  CHECK(pz_cog_target(0, 1) == 1 && pz_cog_target(1, 0) == 0);
  CHECK(pz_cog_target(1, -1) == 1 && pz_cog_target(0, -1) == 0);
  PzE07 s = { { 1, 1, 1 }, { 0, 0, 0 }, { -1, -1, -1 } };
  CHECK(!pz_e07_wrong(s) && !pz_e07_solved(s));
  PzE07 s1 = { { 0, 1, 1 }, { 0, 1, 0 }, { -1, -1, -1 } };
  CHECK(!pz_e07_wrong(s1));
  PzE07 s1turning = { { 0, 1, 1 }, { 0, 0, 0 }, { -1, 1, -1 } };
  CHECK(!pz_e07_wrong(s1turning));
  PzE07 s12 = { { 0, 0, 1 }, { 1, 1, 0 }, { -1, -1, -1 } };
  CHECK(!pz_e07_wrong(s12) && !pz_e07_solved(s12));
  PzE07 s123 = { { 0, 0, 0 }, { 1, 1, 1 }, { -1, -1, -1 } };
  CHECK(pz_e07_solved(s123) && !pz_e07_wrong(s123));
  PzE07 s2 = { { 1, 0, 1 }, { 0, 0, 0 }, { 1, -1, -1 } };
  CHECK(pz_e07_wrong(s2));
  PzE07 s21 = { { 0, 0, 1 }, { 0, 1, 0 }, { -1, -1, -1 } };   // switch_1 closed cog_1 again
  CHECK(pz_e07_wrong(s21));
  PzE07 s3 = { { 1, 1, 0 }, { 0, 0, 1 }, { -1, -1, -1 } };
  CHECK(pz_e07_wrong(s3));
  PzE07 lag = { { 1, 1, 1 }, { 0, 0, 0 }, { -1, 1, -1 } };   // cog_2 turning before switch_1 reads used
  CHECK(pz_e07_wrong(lag));
}

// The view: the next step, the ticks of a correct prefix, and Reset once wrong has held 250 ms.
static void tb_0423_pz_view_and_debounce() {
  const int p100[3] = { 1, 0, 0 }, p010[3] = { 0, 1, 0 }, p111[3] = { 1, 1, 1 };
  PzView v = pz_view(p100, false);
  CHECK(v.next == 1 && v.done[0] && !v.done[1] && !v.done[2]);
  v = pz_view(p010, false);
  CHECK(v.next == 0 && !v.done[0] && !v.done[1]);
  v = pz_view(p111, false);
  CHECK(v.next == -1 && v.done[0] && v.done[1] && v.done[2]);
  v = pz_view(p100, true);
  CHECK(v.next == PZ_RESET && !v.done[0]);
  PzDebounce d = { 0, 0 };
  CHECK(!pz_wrong_shown(&d, true, 1000));
  CHECK(!pz_wrong_shown(&d, true, 1249));
  CHECK(pz_wrong_shown(&d, true, 1250));
  CHECK(!pz_wrong_shown(&d, false, 1300));
  CHECK(!pz_wrong_shown(&d, true, 1400));
  CHECK(pz_wrong_shown(&d, true, 1650));
}

// The pulse stays in [0x50, 0xFF]; the row is on by default.
static void tb_0424_pz_pulse_and_config() {
  CHECK(pz_pulse_alpha(0) == 0x50 && pz_pulse_alpha(500) == 0xFF && pz_pulse_alpha(1000) == 0x50);
  bool inRange = true;
  for (unsigned long long t = 0; t < 2000; t += 50) {
    const int a = pz_pulse_alpha(t);
    inRange = inRange && a >= 0x50 && a <= 0xFF;
  }
  CHECK(inRange);
  config_defaults();
  CHECK(g_cfg.espPuzzles == 1);
  CHECK(config_key_known("esp", "puzzles"));
}

// --- Cross-area secrets (areas.h; re/areas-and-secrets.md) ------------------------------------------------
static const char kArR01[] =
  "spawn map {\r\n  path:maps/e05p02_a01_r01\r\n  name:e05p02_a01_r01\r\n"
  "  spawn area_secret {\r\n    message:\"@@g001@@\"\r\n    position:< -560 952 528 >\r\n    name:\"area_secret1\"\r\n  }\r\n"
  "  spawn map_change {\r\n    change_type:1\r\n    target_dest:\"mover_map_change\"\r\n    target_map:\"e05p02_a01\"\r\n"
  "    position:< 1552 -464 512 >\r\n    name:\"room1\"\r\n  }\r\n"
  "  spawn mover {\r\n    name:\"lift\"\r\n    spawn map_change {\r\n      target_map:\"e05p02_a01\"\r\n"
  "      target_dest:\"nowhere\"\r\n      name:\"inner\"\r\n    }\r\n    position:< 1 2 3 >\r\n  }\r\n"
  "  spawn game_track_auto {\r\n    route_point:< -400 -464 1100 >\r\n    route_point:< 0 0 0 >\r\n"
  "    route_point:< -380 -460 1190 >\r\n    name:\"auto_track1\"\r\n  }\r\n"
  "  spawn map_change {\r\n    change_type:2\r\n    target_dest:\"room2\"\r\n    target_map:\"e05p02_a01\"\r\n"
  "    position:< -416 -464 1184 >\r\n    name:\"room2\"\r\n  }\r\n}\r\n";
static const char kArA01[] =
  "spawn map {\n"
  "  spawn map_change {\n    change_type:2\n    target_dest:\"end\"\n    target_map:\"e05p01_a02\"\n    name:\"start\"\n  }\n"
  "  spawn map_change {\n    change_type:2\n    target_dest:\"start\"\n    target_map:\"e05p02_a02\"\n    name:\"end\"\n  }\n"
  "  spawn map_change {\n    change_type:0\n    target_dest:\"room1\"\n    target_map:\"e05p02_a01_r01\"\n    name:\"room1\"\n  }\n"
  "  spawn map_change {\n    change_type:1\n    target_dest:\"room2\"\n    target_map:\"e05p02_a01_r01\"\n"
  "    position:< 11356 -7680 1036 >\n    name:\"room2\"\n  }\n"
  "  spawn area_secret {\n    name:\"area_secret1\"\n  }\n  spawn area_secret {\n    name:\"area_secret3\"\n  }\n"
  "  spawn area_secret {\n    name:\"area_secret2\"\n  }\n}\n";
static const char kArA02[] =
  "spawn map {\n"
  "  spawn map_change {\n    change_type:2\n    target_dest:\"end\"\n    target_map:\"e05p02_a01\"\n    name:\"start\"\n  }\n"
  "  spawn map_change {\n    change_type:2\n    target_dest:\"start\"\n    target_map:\"e05p03_a01\"\n    name:\"end\"\n  }\n"
  "  spawn area_secret {\n    name:\"area_secret1\"\n  }\n  spawn area_secret {\n    name:\"area_secret2\"\n  }\n"
  "  spawn area_secret {\n    name:\"area_secret3\"\n  }\n}\n";

// e05p02 as the research draws it (section 2), from three small world.scr texts.
static void ar_test_part(ArPart* p) {
  memset(p, 0, sizeof *p);
  strcpy_s(p->prefix, sizeof p->prefix, "e05p02");
  char maps[AR_AREAS_MAX][AR_NAME];
  p->nareas = ar_part_maps("e05p02", maps, AR_AREAS_MAX);
  const char* texts[3] = { kArA01, kArR01, kArA02 };
  for (int i = 0; i < p->nareas && i < 3; ++i) {
    strcpy_s(p->areas[i].map, AR_NAME, maps[i]);
    ar_parse_world(texts[i], (int)strlen(texts[i]), &p->areas[i]);
  }
  ar_link(p);
}

static void tb_0440_ar_part_table_and_world_parse() {
  char pre[8] = "";
  CHECK(ar_map_part("e05p02_a01_r01", pre) && strcmp(pre, "e05p02") == 0);
  CHECK(!ar_map_part("e05_stats", pre));
  CHECK(!ar_map_part("e09p01_a01", pre));
  char maps[AR_AREAS_MAX][AR_NAME];
  CHECK(ar_part_maps("e05p02", maps, AR_AREAS_MAX) == 3);
  CHECK(strcmp(maps[0], "e05p02_a01") == 0 && strcmp(maps[1], "e05p02_a01_r01") == 0 && strcmp(maps[2], "e05p02_a02") == 0);
  CHECK(ar_part_maps("e08p02", maps, AR_AREAS_MAX) == 7);
  ArArea a;
  memset(&a, 0, sizeof a);
  CHECK(ar_parse_world(kArR01, (int)strlen(kArR01), &a));
  CHECK(a.nsecrets == 1 && strcmp(a.secrets[0].name, "area_secret1") == 0);
  CHECK(a.secrets[0].pos[0] == -560.f && a.secrets[0].pos[1] == 952.f && a.secrets[0].pos[2] == 528.f);
  // Doorways in closing order: the nested one closes before its mover; the mover's position is not its child's.
  CHECK(a.ndoors == 3);
  CHECK(strcmp(a.doors[0].name, "room1") == 0 && a.doors[0].changeType == 1 && a.doors[0].hasType);
  CHECK(strcmp(a.doors[0].tmap, "e05p02_a01") == 0 && strcmp(a.doors[0].tdest, "mover_map_change") == 0);
  CHECK(strcmp(a.doors[1].name, "inner") == 0 && !a.doors[1].hasType && a.doors[1].pos[0] == 0.f);
  CHECK(strcmp(a.doors[2].name, "room2") == 0 && a.doors[2].changeType == 2 && a.doors[2].pos[2] == 1184.f);
  CHECK(a.nruns == 1 && strcmp(a.runs[0].name, "auto_track1") == 0);
  CHECK(a.runs[0].a[0] == -400.f && a.runs[0].b[2] == 1190.f);
  unsigned char vb[24];
  const float v[6] = { -56, -64, -160, 48, 72, 128 };
  memcpy(vb, v, sizeof vb);
  float out[6];
  CHECK(ar_volume(vb, 24, out) && out[2] == -160.f && out[5] == 128.f);
  CHECK(!ar_volume(vb, 20, out));
}

static void tb_0441_ar_link_pairs_and_runs() {
  static ArPart p;
  ar_test_part(&p);
  CHECK(p.nareas == 3);
  const ArArea& a01 = p.areas[0];
  const ArArea& r01 = p.areas[1];
  CHECK(a01.doors[0].target == -1);                                   // start leaves the part
  CHECK(a01.doors[1].target == 2 && a01.doors[1].landing == 0 && a01.doors[1].pair);
  CHECK(a01.doors[2].target == 1 && a01.doors[2].landing == 0 && !a01.doors[2].pair);   // r01's room1 is broken
  CHECK(a01.doors[3].target == 1 && a01.doors[3].landing == 2 && a01.doors[3].pair);
  CHECK(r01.doors[0].target == 0 && r01.doors[0].landing == -1);      // mover_map_change does not exist
  CHECK(strcmp(r01.doors[2].run, "auto_track1") == 0 && r01.doors[0].run[0] == 0);
  const ArTotals t = ar_totals(&p);
  CHECK(t.doors == 9 && t.secrets == 7 && t.pairs == 2 && t.edges == 5);
}

static void tb_0442_ar_route_and_text() {
  static ArPart p;
  ar_test_part(&p);
  ArHop hops[AR_HOPS_MAX];
  char b[AR_ROUTE_CAP];
  int n = ar_route(&p, 2, 1, hops, AR_HOPS_MAX);
  CHECK(n == 2);
  ar_route_text(&p, 2, hops, n, b, sizeof b);
  CHECK(strcmp(b, "a02 -> door start -> a01 -> door room1 -> a01_r01") == 0);
  n = ar_route(&p, 1, 2, hops, AR_HOPS_MAX);
  ar_route_text(&p, 1, hops, n, b, sizeof b);
  CHECK(strcmp(b, "a01_r01 -> run auto_track1 -> a01 -> door end -> a02") == 0);
  CHECK(ar_route(&p, 1, 1, hops, AR_HOPS_MAX) == 0);
  ar_route_text(&p, 1, hops, 0, b, sizeof b);
  CHECK(strcmp(b, "here") == 0);
  CHECK(ar_route(&p, 2, 1, hops, 1) == -1);   // longer than the cap
  p.areas[0].doors[2].landing = -1;
  p.areas[0].doors[3].landing = -1;            // no way into r01 now
  CHECK(ar_route(&p, 2, 1, hops, AR_HOPS_MAX) == -1);
  ar_route_text(&p, 2, hops, -1, b, sizeof b);
  CHECK(strcmp(b, "no route") == 0);
}

static int ar_put(unsigned char* b, int at, const void* p, int n) { memcpy(b + at, p, (size_t)n); return at + n; }
static int ar_put_u32(unsigned char* b, int at, unsigned v) { return ar_put(b, at, &v, 4); }
static int ar_put_secret(unsigned char* b, int at, unsigned id, const char* path) {
  at = ar_put_u32(b, at, id);
  at = ar_put_u32(b, at, 0);
  at = ar_put_u32(b, at, 194);
  at = ar_put(b, at, "\x0b" "area_secret", 12);
  at = ar_put(b, at, "\0\0\0 @\0\0\0\0\0", 10);
  const unsigned short pl = (unsigned short)strlen(path);
  at = ar_put(b, at, &pl, 2);
  return ar_put(b, at, path, pl + 1);
}
static int ar_put_state(unsigned char* b, int at, unsigned id, unsigned x, unsigned char y) {
  at = ar_put_u32(b, at, id); at = ar_put_u32(b, at, 1); at = ar_put_u32(b, at, x); at = ar_put_u32(b, at, 1);
  return ar_put(b, at, &y, 1);
}

static void tb_0443_ar_msv_records() {
  static unsigned char b[512];
  memset(b, 0xEE, sizeof b);
  int at = ar_put(b, 0, "\x0a\0e05p02_a01", 12);
  at = ar_put_secret(b, at + 8, 59, "maps/e05p02_a01/area_secret1");
  at = ar_put_secret(b, at + 8, 205, "maps/e05p02_a01/area_secret3");
  at = ar_put_secret(b, at + 8, 301, "maps/e05p02_a01/area_secret2");
  at = ar_put_state(b, at + 8, 59, 1, 0);
  at = ar_put_state(b, at, 205, 0, 1);
  at = ar_put_state(b, at, 301, 2, 1);      // +0xAE4 out of {0, 1}: not a match
  at = ar_put_state(b, at + 4, 301, 0, 1);
  ArMsvRec r[4];
  int dropped = -1;
  CHECK(ar_msv_parse(b, at, r, 4, &dropped) == 3 && dropped == 0);
  CHECK(strcmp(r[0].name, "area_secret1") == 0 && r[0].id == 59 && r[0].ok && r[0].armed == 1 && r[0].bff == 0);
  CHECK(strcmp(r[1].name, "area_secret3") == 0 && r[1].ok && r[1].armed == 0);
  CHECK(strcmp(r[2].name, "area_secret2") == 0 && r[2].ok && r[2].armed == 0 && r[2].matches == 1);
  CHECK(ar_msv_parse(b, at, r, 2, &dropped) == 2 && dropped == 1);
  static ArPart p;
  ar_test_part(&p);
  ArFound f;
  ar_msv_parse(b, at, r, 4, &dropped);
  ar_found_from_msv(&p.areas[0], r, 3, &f);
  CHECK(f.known && f.src == AR_SRC_FILE && f.armed[0] == 1 && f.armed[1] == 0 && f.armed[2] == 0);
  CHECK(ar_found_count(&p.areas[0], &f) == 2);
  ar_found_from_msv(&p.areas[0], r, 2, &f);   // fewer records than secrets
  CHECK(!f.known && f.src == AR_SRC_BAD);
  at = ar_put_state(b, at, 205, 0, 1);         // a second pass-2 match: ambiguous
  ar_msv_parse(b, at, r, 4, &dropped);
  CHECK(!r[1].ok && r[1].matches == 2);
  ar_found_from_msv(&p.areas[0], r, 3, &f);
  CHECK(!f.known);
}

static void tb_0444_ar_tally_and_missing() {
  static ArPart p;
  ar_test_part(&p);
  ArFound f[AR_AREAS_MAX];
  memset(f, 0, sizeof f);
  f[0].known = 1; f[0].armed[0] = 1;            // a01: secret1 missing, 2 found
  ar_found_fresh(&p.areas[1], &f[1]);            // r01: never visited
  CHECK(f[1].known && f[1].src == AR_SRC_FRESH && ar_found_count(&p.areas[1], &f[1]) == 0);
  f[2].known = 1;                                 // a02: all 3 found
  int sum = -1;
  CHECK(ar_tally(&p, f, 5, &sum) == AR_TALLY_OK && sum == 5);
  CHECK(ar_tally(&p, f, 6, &sum) == AR_TALLY_MISMATCH && sum == 5);
  f[2].known = 0;
  CHECK(ar_tally(&p, f, 5, &sum) == AR_TALLY_UNKNOWN);
  char b[64];
  ar_missing_text(&p.areas[0], &f[0], b, sizeof b);
  CHECK(strcmp(b, "secret1") == 0);
  f[0].armed[2] = 1;
  ar_missing_text(&p.areas[0], &f[0], b, sizeof b);
  CHECK(strcmp(b, "secret1, secret2") == 0);
  ar_missing_text(&p.areas[2], &f[2], b, sizeof b);
  CHECK(b[0] == 0);
}

// Cross-area label: the target area by hops, ties by Duke's distance to the exit.
static void tb_0450_ar_cross_target_area() {
  static ArPart p;
  ar_test_part(&p);
  float dist[AR_DOORS_MAX];
  for (int k = 0; k < AR_DOORS_MAX; ++k) dist[k] = 100.f;
  ArCross c;
  // From a02 with r01's secret missing: through a01 (door start), two hops; the next area is a01.
  int missing[AR_AREAS_MAX] = { 0, 1, 0 };
  CHECK(ar_cross_pick(&p, 2, missing, dist, &c));
  CHECK(c.target == 1 && c.door == 0 && c.hops == 2 && c.next == 0);
  // A missing secret one hop away wins over one two hops away.
  missing[0] = 2;
  CHECK(ar_cross_pick(&p, 2, missing, dist, &c) && c.target == 0 && c.hops == 1 && c.next == 0);
  // Unknown (-1) and found-out areas are never targets; the current area's own count is not either.
  missing[0] = -1; missing[1] = 0; missing[2] = 3;
  CHECK(!ar_cross_pick(&p, 2, missing, dist, &c));
}

static void tb_0451_ar_cross_exit_choice() {
  static ArPart p;
  ar_test_part(&p);
  float dist[AR_DOORS_MAX];
  for (int k = 0; k < AR_DOORS_MAX; ++k) dist[k] = 100.f;
  ArCross c;
  int missing[AR_AREAS_MAX] = { 0, 1, 0 };
  // From a01 two doors reach r01 in one hop (room1 = 2, room2 = 3): the door nearer Duke wins.
  dist[2] = 500.f; dist[3] = 300.f;
  CHECK(ar_cross_pick(&p, 0, missing, dist, &c) && c.target == 1 && c.door == 3 && c.hops == 1);
  dist[3] = 500.f;   // as near: file order
  CHECK(ar_cross_pick(&p, 0, missing, dist, &c) && c.door == 2 && c.target == 1);
  // A door leaving the part (start) or landing nowhere is never the exit: from r01 only room2 leads back.
  missing[0] = 1; missing[1] = 0;
  CHECK(ar_cross_pick(&p, 1, missing, dist, &c) && c.door == 2 && c.target == 0);
  // The doorway's volume centre (offline, unrotated); without a volume its position.
  ArDoor d;
  memset(&d, 0, sizeof d);
  d.pos[0] = 100.f; d.pos[1] = 200.f; d.pos[2] = 300.f;
  float q[3], bottom;
  ar_door_centre(&d, q, &bottom);
  CHECK(q[0] == 100.f && q[1] == 200.f && q[2] == 300.f && bottom == 200.f);
  const float v[6] = { -50, -10, -20, 50, 90, 60 };
  memcpy(d.vol, v, sizeof v);
  d.volOk = 1;
  ar_door_centre(&d, q, &bottom);
  CHECK(q[0] == 100.f && q[1] == 240.f && q[2] == 320.f && bottom == 190.f);
}

static void tb_0452_ar_cross_current_first_and_label() {
  static ArPart p;
  ar_test_part(&p);
  float dist[AR_DOORS_MAX];
  for (int k = 0; k < AR_DOORS_MAX; ++k) dist[k] = 100.f;
  ArCross c;
  int missing[AR_AREAS_MAX] = { 0, 1, 0 };
  CHECK(!ar_cross_pick(&p, -1, missing, dist, &c));  // no current area
  // Missing counts from the found state: unknown is -1.
  ArFound f[AR_AREAS_MAX];
  memset(f, 0, sizeof f);
  f[0].known = 1; f[0].armed[0] = 1; f[0].armed[2] = 1;
  ar_found_fresh(&p.areas[1], &f[1]);
  int m[AR_AREAS_MAX];
  ar_missing_counts(&p, f, m);
  CHECK(m[0] == 2 && m[1] == 1 && m[2] == -1);
  char b[96];
  ArWant w;
  memset(&w, 0, sizeof w);
  missing[0] = 0; missing[1] = 2;
  w.secrets[1] = 2;
  CHECK(ar_cross_pick(&p, 0, missing, dist, &c));
  ar_cross_label(&p, &c, &w, b, sizeof b);
  CHECK(strcmp(b, "to a01_r01 (2 secrets)") == 0);
  missing[1] = 1;
  w.secrets[1] = 1;
  CHECK(ar_cross_pick(&p, 2, missing, dist, &c));
  ar_cross_label(&p, &c, &w, b, sizeof b);
  CHECK(strcmp(b, "to a01, then a01_r01 (1 secret)") == 0);
}

// --- The schematic map (areamap.h) ---------------------------------------------------------------------------
static const int kAmLabels[AR_AREAS_MAX] = { 20, 45, 20, 20, 20, 20, 20, 20 };

// Four areas named in reverse (area 0 is a04), links set by hand: area 0 two-way to the other three, and area 3
// (a01) one-way to area 1 (a03): one column, rows 0 and 2.
static void am_test_door(ArPart* p, int from, int to) {
  ArDoor& d = p->areas[from].doors[p->areas[from].ndoors++];
  memset(&d, 0, sizeof d);
  d.target = to;
  d.landing = 0;
}
static void am_test_fan(ArPart* p) {
  memset(p, 0, sizeof *p);
  p->nareas = 4;
  for (int i = 0; i < 4; ++i) sprintf_s(p->areas[i].map, "e09p09_a0%d", 4 - i);   // names reverse the index order
  for (int i = 1; i < 4; ++i) { am_test_door(p, 0, i); am_test_door(p, i, 0); }
  am_test_door(p, 3, 1);
}

static void tb_0460_am_layers() {
  static ArPart p;
  ar_test_part(&p);
  int layer[AR_AREAS_MAX];
  CHECK(am_layers(&p, layer) == 2 && layer[0] == 0 && layer[1] == 1 && layer[2] == 1);
  p.areas[0].doors[3].landing = -1;
  p.areas[1].doors[2].landing = -1;   // a01 -> a01_r01 by the one-way room1 only
  CHECK(am_layers(&p, layer) == 2 && layer[1] == 1);
  p.areas[0].doors[1].landing = -1;
  p.areas[2].doors[0].landing = -1;   // a02 cut off: a column of its own after the last
  CHECK(am_layers(&p, layer) == 3 && layer[2] == 2);
}

static void tb_0461_am_place() {
  static ArPart p;
  ar_test_part(&p);
  static AmMap m;
  am_build(&p, -1, 0, kAmLabels, 400, &m);
  CHECK(m.ok && m.nlayers == 2 && m.boxW == 45 + 2 * AM_PAD_X + AM_TICK + AM_TICK_GAP);
  CHECK(m.box[0].x == 0 && m.box[0].y == 41);                  // centred on the two-box column
  CHECK(m.box[1].x == 101 && m.box[1].y == 10 && m.box[1].row == 0);   // a01_r01 sorts before a02
  CHECK(m.box[2].x == 101 && m.box[2].y == 72 && m.box[2].row == 1);
  CHECK(m.w == 172 && m.h == 128);
  am_build(&p, -1, 0, kAmLabels, 138, &m);                     // shrunk to the minimum label room
  CHECK(m.ok && m.boxW == AM_LABEL_MIN + 2 * AM_PAD_X + AM_TICK + AM_TICK_GAP);
  for (int i = 0; i < m.n; ++i) CHECK(m.box[i].x + m.box[i].w + AM_BEND <= 138);
  am_build(&p, -1, 0, kAmLabels, 137, &m);
  CHECK(!m.ok);
  static ArPart f;
  am_test_fan(&f);
  am_build(&f, -1, 0, kAmLabels + 2, 400, &m);                 // labels narrower than the minimum
  CHECK(m.ok && m.boxW == 48);
  CHECK(m.box[3].row == 0 && m.box[2].row == 1 && m.box[1].row == 2 && m.box[1].y == 134);
}

static void tb_0462_am_link_endpoints() {
  static ArPart p;
  ar_test_part(&p);
  static AmMap m;
  am_build(&p, -1, 0, kAmLabels, 400, &m);
  CHECK(m.nlinks == 2);
  const AmLink& r = m.link[0];   // a01 <-> a01_r01: two-way, its way back is auto_track1
  CHECK(r.from == 0 && r.to == 1 && !r.oneway && r.run);
  CHECK(r.x0 == 71 && r.y0 == 67 && r.x1 == 101 && r.y1 == 36 && r.xm == 86 && r.ym == 51);
  const AmLink& a = m.link[1];
  CHECK(a.from == 0 && a.to == 2 && !a.oneway && !a.run && a.y1 == 98);
  p.areas[0].doors[2].landing = -1;
  p.areas[0].doors[3].landing = -1;   // only the run back from a01_r01 is left: one-way, drawn right to left
  am_build(&p, -1, 0, kAmLabels, 400, &m);
  CHECK(m.link[0].oneway && m.link[0].from == 1 && m.link[0].to == 0 && m.link[0].run);
  CHECK(m.link[0].x0 == 101 && m.link[0].y0 == 36 && m.link[0].x1 == 71 && m.link[0].y1 == 67);
  ar_test_part(&p);
  p.areas[1].doors[0].target = 2;
  p.areas[1].doors[0].landing = 0;    // a01_r01 -> a02, one column, adjacent rows: bottom to top
  am_build(&p, -1, 0, kAmLabels, 400, &m);
  CHECK(m.nlinks == 3 && m.link[2].from == 1 && m.link[2].to == 2 && m.link[2].oneway);
  CHECK(m.link[2].x0 == 136 && m.link[2].y0 == 62 && m.link[2].x1 == 136 && m.link[2].y1 == 72);
  static ArPart f;
  am_test_fan(&f);
  am_build(&f, -1, 0, kAmLabels + 2, 400, &m);
  CHECK(m.nlinks == 4);
  const AmLink& c = m.link[3];        // a01 -> a03, rows 0 and 2: bent out right of the column
  CHECK(c.from == 3 && c.to == 1 && c.oneway && c.x0 == 126 && c.x1 == 126 && c.y0 == 36 && c.y1 == 160);
  CHECK(c.xm == 126 + AM_BEND && c.ym == 98);
}

static void tb_0463_am_route() {
  static ArPart p;
  ar_test_part(&p);
  ArFound f[AR_AREAS_MAX];
  memset(f, 0, sizeof f);
  f[0].known = f[1].known = f[2].known = 1;
  f[1].armed[0] = 1;                  // a01_r01 is missing its secret
  static AmMap m;
  am_build(&p, 2, f, kAmLabels, 400, &m);
  CHECK(m.routeTo == 1 && m.link[0].route && m.link[1].route);
  am_build(&p, 0, f, kAmLabels, 400, &m);
  CHECK(m.routeTo == 1 && m.link[0].route && !m.link[1].route);
  f[2].armed[1] = 1;                  // a02 too, also one hop from a01: file order picks a01_r01
  am_build(&p, 0, f, kAmLabels, 400, &m);
  CHECK(m.routeTo == 1 && !m.link[1].route);
  am_build(&p, 1, f, kAmLabels, 400, &m);   // missing here: no route
  CHECK(m.routeTo == -1 && !m.link[0].route && !m.link[1].route);
  f[1].known = 0;
  f[2].known = 0;                     // unknown areas draw no route
  am_build(&p, 0, f, kAmLabels, 400, &m);
  CHECK(m.routeTo == -1 && !m.link[0].route);
}

// One shared choice: the map's route and the list's route text follow the exit the
// cross pick chose, so the label, the map and the list name the same area.
static void tb_0464_am_shared_cross_choice() {
  static ArPart p;
  ar_test_part(&p);
  ArFound f[AR_AREAS_MAX];
  memset(f, 0, sizeof f);
  f[0].known = f[1].known = f[2].known = 1;
  f[1].armed[0] = 1;
  f[2].armed[1] = 1;                  // a01_r01 and a02 both one hop from a01
  int missing[AR_AREAS_MAX];
  ar_missing_counts(&p, f, missing);
  float dist[AR_DOORS_MAX];
  for (int k = 0; k < AR_DOORS_MAX; ++k) dist[k] = 100.f;
  ArCross c;
  CHECK(ar_cross_pick(&p, 0, missing, dist, &c) && c.target == 2 && c.door == 1);   // door end comes first
  static AmMap m;
  am_build(&p, 0, f, kAmLabels, 400, &m, &c);
  CHECK(m.routeTo == 2 && m.link[1].route && !m.link[0].route);   // not file order's a01_r01
  char t[AR_AREAS_MAX][AR_ROUTE_CAP];
  ar_route_texts(&p, 0, &c, t);
  CHECK(strcmp(t[2], "a01 -> door end -> a02") == 0 && strcmp(t[0], "here") == 0);
  // Only a01_r01 missing, room2 the nearer door: the list's text goes by room2, as the label does.
  f[2].armed[1] = 0;
  ar_missing_counts(&p, f, missing);
  dist[3] = 50.f;
  CHECK(ar_cross_pick(&p, 0, missing, dist, &c) && c.target == 1 && c.door == 3);
  ar_route_texts(&p, 0, 0, t);
  CHECK(strcmp(t[1], "a01 -> door room1 -> a01_r01") == 0);
  ar_route_texts(&p, 0, &c, t);
  CHECK(strcmp(t[1], "a01 -> door room2 -> a01_r01") == 0);
  // Two hops from a02: the chosen door first, then the doorway route on.
  ArHop h[AR_HOPS_MAX];
  CHECK(ar_cross_pick(&p, 2, missing, dist, &c) && ar_cross_hops(&p, 2, &c, h, AR_HOPS_MAX) == 2);
  CHECK(h[0].area == 2 && h[0].door == 0 && h[1].area == 0 && p.areas[0].doors[h[1].door].target == 1);
  // The current area missing a secret: the map follows the shared choice too.
  f[2].armed[1] = 1;
  am_build(&p, 2, f, kAmLabels, 400, &m, &c);
  CHECK(m.routeTo == 1 && m.link[0].route && m.link[1].route);
  am_build(&p, 2, f, kAmLabels, 400, &m);   // no shared choice: as before, none while missing here
  CHECK(m.routeTo == -1);
}

// --- Nukes per area (re/pickups.md section 5, re/areas-and-secrets.md section 4) --------------------
// Two pickups, a crate with a nuke among its codes, one without, a barrel and a crate with two nuke codes; a
// breakable's contents are not a holder's.
static const char kArNk[] =
  "spawn map {\n"
  "  spawn nuke {\n    position:< 100 0 0 >\n    name:\"nuke1\"\n  }\n"
  "  spawn nuke {\n    position:< 900 0 0 >\n    name:\"nuke5\"\n  }\n"
  "  spawn crate {\n    crate_type:0\n    contents:1\n    contents:0\n    position:< 2000 0 0 >\n    name:\"crate11\"\n  }\n"
  "  spawn crate {\n    contents:2\n    position:< 3000 0 0 >\n    name:\"crate12\"\n  }\n"
  "  spawn barrel {\n    contents:0\n    position:< 4000 0 0 >\n    name:\"barrel4\"\n  }\n"
  "  spawn crate {\n    contents:0\n    contents:0\n    position:< 5000 0 0 >\n    name:\"crate13\"\n  }\n"
  "  spawn breakable {\n    contents:0\n    name:\"brk\"\n  }\n}\n";

static void ar_nk_area(ArArea* a) {
  memset(a, 0, sizeof *a);
  strcpy_s(a->map, AR_NAME, "e09p09_a01");
  ar_parse_world(kArNk, (int)strlen(kArNk), a);
}

static void tb_0465_ar_nuke_totals_from_world() {
  ArArea a;
  ar_nk_area(&a);
  CHECK(a.nnukes == 5 && a.nukeTotal == 6);
  CHECK(strcmp(a.nukes[0].name, "nuke1") == 0 && a.nukes[0].holder == AR_NK_PICKUP && a.nukes[0].count == 1);
  CHECK(a.nukes[0].pos[0] == 100.f && strcmp(a.nukes[1].name, "nuke5") == 0);
  CHECK(strcmp(a.nukes[2].name, "crate11") == 0 && a.nukes[2].holder == AR_NK_CRATE && a.nukes[2].count == 1);
  CHECK(a.nukes[2].pos[0] == 2000.f);
  CHECK(strcmp(a.nukes[3].name, "barrel4") == 0 && a.nukes[3].holder == AR_NK_BARREL && a.nukes[3].count == 1);
  CHECK(strcmp(a.nukes[4].name, "crate13") == 0 && a.nukes[4].holder == AR_NK_CRATE && a.nukes[4].count == 2);
  CHECK(a.nsecrets == 0 && a.ndoors == 0);
  static ArPart p;
  ar_test_part(&p);
  CHECK(ar_totals(&p).nukes == 0);
  p.areas[1].nnukes = a.nnukes;
  p.areas[1].nukeTotal = a.nukeTotal;
  memcpy(p.areas[1].nukes, a.nukes, sizeof a.nukes);
  CHECK(ar_totals(&p).nukes == 6 && ar_totals(&p).secrets == 7);
}

static ArLiveNuke ar_ln(const char* name, float x, int holder, int active) {
  ArLiveNuke e;
  memset(&e, 0, sizeof e);
  strcpy_s(e.name, AR_NAME, name);
  e.pos[0] = x;
  e.holder = holder;
  e.active = active;
  return e;
}

static void tb_0466_ar_nukes_live() {
  ArArea a;
  ar_nk_area(&a);
  ArLiveNuke e[8];
  int n = 0;
  e[n++] = ar_ln("nuke1", 100.f, AR_NK_PICKUP, 1);      // still there
  e[n++] = ar_ln("nuke5", 900.f, AR_NK_PICKUP, 0);      // picked this frame (+0x2E0 clear): collected
  e[n++] = ar_ln("crate11", 2000.f, AR_NK_CRATE, 1);    // whole
  e[n++] = ar_ln("barrel4", 4000.f, AR_NK_BARREL, 0);   // broken, its nuke lies beside it
  e[n++] = ar_ln("exploder-spwn000", 4100.f, AR_NK_PICKUP, 1);
  // crate13 is gone and nothing lies near it: both its nukes are collected.
  ArNukeFound f;
  ar_nukes_live(&a, e, n, &f);
  CHECK(f.known && f.src == AR_SRC_LIVE);
  CHECK(!f.got[0] && f.got[1] && !f.got[2] && !f.got[3] && f.got[4]);
  CHECK(f.count == 3);
  e[4].pos[0] = 4000.f + AR_HOLDER_NEAR + 1.f;           // too far from the barrel to be its nuke
  ar_nukes_live(&a, e, n, &f);
  CHECK(f.got[3] && f.count == 4);
  e[4] = ar_ln("nuke5", 4050.f, AR_NK_PICKUP, 1);         // a named pickup beside it is that pickup
  ar_nukes_live(&a, e, n, &f);
  CHECK(!f.got[1] && f.got[3]);
  ar_nukes_fresh(&a, &f);
  CHECK(f.known && f.src == AR_SRC_FRESH && f.count == 0);
}

// A nuke's pass-1 record as e06p01_a02.msv holds it: id, parent, +0x74, "\x04nuke", its keys (floats, then the
// name as u16 length + chars + NUL), its pins and -1.
static int ar_put_nuke(unsigned char* b, int at, unsigned id, const char* name) {
  at = ar_put_u32(b, at, id);
  at = ar_put_u32(b, at, 0);
  at = ar_put_u32(b, at, 198);
  at = ar_put(b, at, "\x04" "nuke", 5);
  static const unsigned char kKeys[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0x3f, 0, 0, 0x30, 0x45,
                                         0, 0, 0x80, 0x41, 0, 0, 0x96, 0x42, 0, 0, 0x80, 0x3f, 0 };
  at = ar_put(b, at, kKeys, sizeof kKeys);
  const unsigned short l = (unsigned short)strlen(name);
  at = ar_put(b, at, &l, 2);
  at = ar_put(b, at, name, l + 1);
  static const unsigned char kZero[24] = { 0 };
  at = ar_put(b, at, kZero, sizeof kZero);
  return ar_put_u32(b, at, 0xFFFFFFFFu);
}

// A crate's or barrel's pass-1 record (its name 95 bytes after the class, as crate1 in e06p01_a01.msv) and its
// pass-2 record (holder_vtE0_store duke!0x10068ac0): +0x2E0 at offset 25.
static int ar_put_holder(unsigned char* b, int at, unsigned id, const char* cls, const char* name) {
  at = ar_put_u32(b, at, id);
  at = ar_put_u32(b, at, 0);
  at = ar_put_u32(b, at, 198);
  at = ar_put(b, at, cls, (int)strlen(cls + 1) + 1);
  static const unsigned char kKeys[95] = { 0 };
  at = ar_put(b, at, kKeys, sizeof kKeys);
  const unsigned short l = (unsigned short)strlen(name);
  at = ar_put(b, at, &l, 2);
  at = ar_put(b, at, name, l + 1);
  return ar_put_u32(b, at, 0xFFFFFFFFu);
}
static int ar_put_holder_state(unsigned char* b, int at, unsigned id, unsigned char whole) {
  static const unsigned char kZero[40] = { 0 };
  at = ar_put_u32(b, at, id);
  at = ar_put_u32(b, at, 1);
  at = ar_put(b, at, kZero, 3 + 4);          // +0xAF0, +0xAF2, +0xAF3, +0xAFC
  at = ar_put_u32(b, at, 1);
  at = ar_put(b, at, kZero, 2 + 4);          // +0xB01, +0xB00, +0xB04
  at = ar_put(b, at, &whole, 1);             // +0x2E0
  at = ar_put(b, at, kZero, 1 + 36 + 4);     // +0x2E3, +0x1C0, +0x18C, +0x1A8, no items
  at = ar_put_u32(b, at, 1);
  at = ar_put(b, at, kZero, 1 + 4 + 4 + 1 + 12);
  at = ar_put_u32(b, at, 1);
  return ar_put(b, at, kZero, 8);            // +0xC14, +0xBFC
}

static void tb_0467_ar_nukes_msv() {
  ArArea a, pk;
  ar_nk_area(&a);
  pk = a;
  pk.nnukes = 2;                                        // the two pickups alone
  pk.nukeTotal = 2;
  static unsigned char b[1024];
  memset(b, 0xEE, sizeof b);
  int at = ar_put(b, 0, "\x0a\0e09p09_a01", 12);
  at = ar_put_nuke(b, at, 145, "nuke5");
  at = ar_put_nuke(b, at, 146, "exploder-spwn000");     // a spawned nuke: no world.scr name
  at = ar_put_nuke(b, at, 147, "nuke10");               // nuke1's name is not a prefix match
  ArNukeFound f;
  int recs = -1, unnamed = -1;
  ar_nukes_from_msv(b, at, &pk, &f, &recs, &unnamed);
  CHECK(f.known && f.src == AR_SRC_FILE && recs == 3 && unnamed == 2);
  CHECK(f.got[0] == 1 && f.got[1] == 0 && f.count == 1);
  ar_nukes_from_msv(b, at, &a, &f, &recs, &unnamed);    // holders gone, and spawned nukes lie somewhere: unknown
  CHECK(!f.known && f.got[0] == 1 && f.got[1] == 0 && f.have[0] && !f.have[2]);
  // A holder's own records: whole, its nuke is still inside.
  ArArea ca = a;
  ca.nnukes = 3;                                        // nuke1, nuke5, crate11
  ca.nukeTotal = 3;
  static unsigned char h[1024];
  int ht = ar_put(h, 0, "\x0a\0e09p09_a01", 12);
  ht = ar_put_holder(h, ht, 40, "\x05" "crate", "crate11");
  ht = ar_put_nuke(h, ht, 145, "nuke5");
  const int pass2 = ht;
  ht = ar_put_holder_state(h, pass2, 40, 1);
  ar_nukes_from_msv(h, ht, &ca, &f, &recs, &unnamed);
  CHECK(f.known && f.have[2] && f.got[2] == 0 && f.count == 1);
  ht = ar_put_holder_state(h, pass2, 40, 0);            // broken and no spawned nuke left: collected
  ar_nukes_from_msv(h, ht, &ca, &f, &recs, &unnamed);
  CHECK(f.known && f.got[2] == 1 && f.count == 2);
  ht = ar_put_nuke(h, ht, 146, "exploder-spwn000");     // a spawned nuke lies somewhere: unknown
  ar_nukes_from_msv(h, ht, &ca, &f, &recs, &unnamed);
  CHECK(!f.known && !f.have[2] && unnamed == 1);
  at = ar_put_nuke(b, at, 148, "nuke5");                // the same pickup twice: not clean
  ar_nukes_from_msv(b, at, &pk, &f, &recs, &unnamed);
  CHECK(!f.known && f.src == AR_SRC_BAD);
}

static void tb_0468_ar_nukes_merge_and_derive() {
  ArArea a;
  ar_nk_area(&a);
  static unsigned char b[512];
  int at = ar_put(b, 0, "\x0a\0e09p09_a01", 12);
  at = ar_put_nuke(b, at, 145, "nuke5");
  at = ar_put_nuke(b, at, 146, "exploder-spwn000");
  ArNukeFound file, rec, out;
  int recs, unnamed;
  ar_nukes_from_msv(b, at, &a, &file, &recs, &unnamed);   // nuke1 collected, nuke5 not; holders unknown
  ArLiveNuke e[4];
  e[0] = ar_ln("nuke1", 100.f, AR_NK_PICKUP, 1);
  e[1] = ar_ln("nuke5", 900.f, AR_NK_PICKUP, 1);
  e[2] = ar_ln("crate11", 2000.f, AR_NK_CRATE, 1);       // barrel4 and crate13 were gone when last seen
  ar_nukes_live(&a, e, 3, &rec);
  ar_nukes_merge(&a, &file, &rec, &out);                  // the file's pickups, the record's holders
  CHECK(out.known && out.src == AR_SRC_RECORD && out.got[0] == 1 && out.got[1] == 0 && out.got[2] == 0);
  CHECK(out.got[3] == 1 && out.got[4] == 1 && out.count == 4);
  ar_nukes_merge(&a, &file, 0, &out);
  CHECK(!out.known);
  memset(&file, 0, sizeof file);
  file.src = AR_SRC_BAD;
  ar_nukes_merge(&a, &file, &rec, &out);                  // no file: the record alone
  CHECK(out.known && out.src == AR_SRC_RECORD && out.got[0] == 0 && out.count == 3);
  ArArea pk = a;
  pk.nnukes = 2;
  pk.nukeTotal = 2;
  ar_nukes_from_msv(b, at, &pk, &file, &recs, &unnamed);
  ar_nukes_merge(&pk, &file, &rec, &out);                 // a clean file wins outright
  CHECK(out.known && out.src == AR_SRC_FILE && out.got[0] == 1);

  // The self-check and the derived area: areas 0 (3 nukes, 2 found) and 1 (2 nukes, unknown); area 2 has none.
  static ArPart p;
  memset(&p, 0, sizeof p);
  p.nareas = 3;
  p.areas[0].nukeTotal = 3;
  p.areas[1].nukeTotal = 2;
  ArNukeFound nf[AR_AREAS_MAX];
  memset(nf, 0, sizeof nf);
  nf[0].known = 1; nf[0].src = AR_SRC_LIVE; nf[0].count = 2;
  int sum = -1, derived = -2;
  // The part's 5 nukes are all in the areas: a live tally of 3 leaves exactly 1 for area 1.
  CHECK(ar_nuke_tally(&p, nf, 3, 1, 5, &sum, &derived) == AR_TALLY_OK && derived == 1 && sum == 3);
  CHECK(nf[1].known && nf[1].src == AR_SRC_DERIVED && nf[1].count == 1);
  memset(&nf[1], 0, sizeof nf[1]);
  // Two of the part's 7 come from enemies or spawners: 3 leaves area 1 at 0 or 1, not derived.
  CHECK(ar_nuke_tally(&p, nf, 3, 1, 7, &sum, &derived) == AR_TALLY_UNKNOWN && derived == -1 && !nf[1].known);
  // 6 of 7: area 1 holds at least 2 and at most its 2.
  CHECK(ar_nuke_tally(&p, nf, 6, 1, 7, &sum, &derived) == AR_TALLY_OK && derived == 1 && sum == 4);
  CHECK(nf[1].count == 2);
  memset(&nf[1], 0, sizeof nf[1]);
  CHECK(ar_nuke_tally(&p, nf, 3, 0, 5, &sum, &derived) == AR_TALLY_UNKNOWN && derived == -1);   // tally unread
  // Every area known (area 1: 1 found, sum 3): the live tally may exceed the sum by the part's other nukes only.
  nf[1].known = 1; nf[1].count = 1;
  CHECK(ar_nuke_tally(&p, nf, 3, 1, 5, &sum, &derived) == AR_TALLY_OK && sum == 3 && derived == -1);
  CHECK(ar_nuke_tally(&p, nf, 4, 1, 5, &sum, &derived) == AR_TALLY_MISMATCH);
  CHECK(ar_nuke_tally(&p, nf, 5, 1, 7, &sum, &derived) == AR_TALLY_OK);
  CHECK(ar_nuke_tally(&p, nf, 6, 1, 7, &sum, &derived) == AR_TALLY_MISMATCH);
  CHECK(ar_nuke_tally(&p, nf, 2, 1, 7, &sum, &derived) == AR_TALLY_MISMATCH);   // below the areas' sum
  CHECK(ar_nuke_tally(&p, nf, 9, 1, -1, &sum, &derived) == AR_TALLY_OK);        // part total unknown
}

// The map's rows: S and N pip rows, capped at 10 and at the room; a finished area.
static void tb_0469_am_pip_rows_and_finished() {
  AmRow r = am_pip_row(0, 0, 1, 10);
  CHECK(r.mode == AM_ROW_NONE);
  CHECK(am_pip_row(3, 1, 0, 10).mode == AM_ROW_UNKNOWN);
  r = am_pip_row(3, 1, 1, 10);
  CHECK(r.mode == AM_ROW_PIPS && r.found == 1 && r.total == 3);
  CHECK(am_pip_row(10, 10, 1, 12).mode == AM_ROW_PIPS);
  CHECK(am_pip_row(12, 4, 1, 12).mode == AM_ROW_TEXT);   // over the cap of 10
  CHECK(am_pip_row(6, 4, 1, 5).mode == AM_ROW_TEXT);     // more than fit
  CHECK(am_pips_fit(8 + AM_PIP_PREFIX_GAP + 10 * AM_PIP + 9 * AM_PIP_GAP, 8) == 10);
  CHECK(am_pips_fit(8 + AM_PIP_PREFIX_GAP + 10 * AM_PIP + 9 * AM_PIP_GAP - 1, 8) == 9);
  CHECK(am_pips_fit(5, 8) == 0);
  ArArea a;
  memset(&a, 0, sizeof a);
  a.nsecrets = 2;
  a.nukeTotal = 3;
  ArFound sf;
  ArNukeFound nf;
  memset(&sf, 0, sizeof sf);
  memset(&nf, 0, sizeof nf);
  sf.known = 1;
  nf.known = 1; nf.count = 3;
  CHECK(am_finished(&a, &sf, &nf));
  nf.count = 2;
  CHECK(!am_finished(&a, &sf, &nf));
  nf.count = 3; nf.known = 0;
  CHECK(!am_finished(&a, &sf, &nf));
  nf.known = 1; sf.armed[1] = 1;
  CHECK(!am_finished(&a, &sf, &nf));
  sf.armed[1] = 0; a.nukeTotal = 0; nf.known = 0;          // secrets only, all found
  CHECK(am_finished(&a, &sf, &nf));
  a.nsecrets = 0;                                           // nothing to find: never "finished"
  CHECK(!am_finished(&a, &sf, &nf));
}

// The door label with nukes: what it counts follows the switches, the door the hops then the distance.
static void tb_0470_ar_cross_label_nukes() {
  static ArPart p;
  ar_test_part(&p);
  p.areas[1].nukeTotal = 2;
  p.areas[2].nukeTotal = 1;
  ArFound f[AR_AREAS_MAX];
  memset(f, 0, sizeof f);
  f[0].known = f[1].known = f[2].known = 1;
  f[1].armed[0] = 1;                                         // a01_r01's secret missing
  ArNukeFound nf[AR_AREAS_MAX];
  memset(nf, 0, sizeof nf);
  for (int i = 0; i < 3; ++i) nf[i].known = 1;
  ArWant w;
  int missing[AR_AREAS_MAX];
  ar_want_counts(&p, f, nf, 1, 1, &w, missing);
  CHECK(missing[0] == 0 && missing[1] == 3 && missing[2] == 1 && w.secrets[1] == 1 && w.nukes[1] == 2);
  float dist[AR_DOORS_MAX];
  for (int k = 0; k < AR_DOORS_MAX; ++k) dist[k] = 100.f;
  ArCross c;
  // From a01 both a02 (door end) and a01_r01 are one hop: equal distances keep file order.
  CHECK(ar_cross_pick(&p, 0, missing, dist, &c) && c.target == 2 && c.door == 1);
  dist[2] = 50.f;                                            // room1 is nearer
  CHECK(ar_cross_pick(&p, 0, missing, dist, &c) && c.target == 1 && c.door == 2 && c.hops == 1);
  char b[96];
  ar_cross_label(&p, &c, &w, b, sizeof b);
  CHECK(strcmp(b, "to a01_r01 (1 secret, 2 nukes)") == 0);
  ar_want_counts(&p, f, nf, 0, 1, &w, missing);              // Nukes on, Secret areas off
  CHECK(missing[1] == 2 && w.secrets[1] == 0);
  ar_cross_label(&p, &c, &w, b, sizeof b);
  CHECK(strcmp(b, "to a01_r01 (2 nukes)") == 0);
  ar_want_counts(&p, f, nf, 1, 0, &w, missing);              // Secret areas on alone: a02 has nothing wanted
  CHECK(missing[2] == 0 && ar_cross_pick(&p, 0, missing, dist, &c) && c.target == 1);
  nf[1].known = 0;
  ar_want_counts(&p, f, nf, 0, 1, &w, missing);              // nukes alone, a01_r01's unknown
  CHECK(missing[1] == -1 && missing[2] == 1);
  nf[1].known = 1;
  nf[1].count = 1;
  ar_want_counts(&p, f, nf, 1, 1, &w, missing);
  CHECK(ar_cross_pick(&p, 2, missing, dist, &c) && c.target == 1 && c.next == 0);   // two hops from a02
  ar_cross_label(&p, &c, &w, b, sizeof b);
  CHECK(strcmp(b, "to a01, then a01_r01 (1 secret, 1 nuke)") == 0);
}

// The part's nukes outside the areas, when the record goes
// stale, a world not yet populated, and a visited area with no file.
static void tb_0472_ar_nukes_review_fixes() {
  int n = -1, miss = -1;
  char b[96];
  // Finding 1: 10 in the part, 8 placed; the tally 7 with 6 placed found: 1 of the 2 others taken.
  CHECK(ar_unplaced(10, 8, 7, 1, 6, 1, &n, &miss) && n == 2 && miss == 1);
  ar_unplaced_text(n, miss, b, sizeof b);
  CHECK(strcmp(b, "2 nukes come from scripts (1 still missing)") == 0);
  CHECK(ar_unplaced(10, 8, 8, 1, 6, 1, &n, &miss) && miss == 0);
  ar_unplaced_text(n, miss, b, sizeof b);
  CHECK(strcmp(b, "2 nukes come from scripts (all taken)") == 0);
  CHECK(ar_unplaced(10, 9, 3, 1, 3, 0, &n, &miss) && n == 1 && miss == -1);   // an area unknown: no count
  ar_unplaced_text(n, miss, b, sizeof b);
  CHECK(strcmp(b, "1 nuke comes from scripts") == 0);
  CHECK(ar_unplaced(10, 0, 4, 0, 0, 1, &n, &miss) && n == 10 && miss == -1);   // e08p02, tally unread
  CHECK(ar_unplaced(10, 10, 5, 1, 4, 1, &n, &miss) == false && n == 0);        // all placed: no line
  CHECK(!ar_unplaced(-1, 6, 5, 1, 4, 1, &n, &miss));                           // part total unknown
  CHECK(ar_unplaced(10, 8, 5, 1, 6, 1, &n, &miss) && miss == 2);              // below the placed sum: clamped
  // Finding 2: the record goes stale on a new session record, a new player in the same world, or an area reached
  // other than through a doorway (a load or a checkpoint respawn names a checkpoint, not a door).
  static ArPart p;
  ar_test_part(&p);
  CHECK(ar_entry_is_door(&p.areas[1], "room2") && !ar_entry_is_door(&p.areas[1], "checkpoint3"));
  CHECK(!ar_entry_is_door(&p.areas[1], ""));
  CHECK(!ar_records_stale(0, 0, 0, 0));
  CHECK(!ar_records_stale(0, 0, 1, 1));   // staged through a doorway
  CHECK(ar_records_stale(0, 0, 1, 0));
  CHECK(ar_records_stale(1, 0, 0, 0) && ar_records_stale(0, 1, 0, 0));
  // Finding 3: no live verdict in the first second after a stage, or from an empty list while sources exist.
  CHECK(ar_live_settled(0, 0, 0));
  CHECK(!ar_live_settled(3, 0, 5000));
  CHECK(!ar_live_settled(3, 40, AR_SETTLE_MS - 1));
  CHECK(ar_live_settled(3, 40, AR_SETTLE_MS));
  // Finding 4: a visited area with no file reads our record before "nothing collected".
  ArArea a;
  ar_nk_area(&a);
  ArNukeFound fresh, rec, out;
  ar_nukes_fresh(&a, &fresh);
  ArLiveNuke e[1];
  e[0] = ar_ln("nuke1", 100.f, AR_NK_PICKUP, 1);
  ar_nukes_live(&a, e, 1, &rec);
  ar_nukes_merge(&a, &fresh, &rec, &out);
  CHECK(out.known && out.src == AR_SRC_RECORD && out.count == rec.count && out.count > 0);
  ar_nukes_merge(&a, &fresh, 0, &out);
  CHECK(out.known && out.src == AR_SRC_FRESH && out.count == 0);
}

// --- Enemies that carry a nuke (re/pickups.md section 6) -------------------------------------------------------
// world.scr as e07p02_a01 and e02p03_a02 hold them: a pig with a bullet and a nuke, the gator boss with three nukes,
// a pig with a bullet only, a spawner whose monster carries a nuke (scripted: not counted) and a nuke spawner.
static const char kArCarry[] =
  "spawn map {\n"
  "  spawn monst_pigoid2 {\n    spawnscript:\"scripts/pwrup/sbullet.sc\"\n    spawnscript:\"scripts/pwrup/nuke.sc\"\n"
  "    position:< 1168 -144 96 >\n    name:\"monst_pigoid22\"\n  }\n"
  "  spawn monst_gatoroidB {\n    spawnscript:\"scripts/pwrup/nuke.sc\"\n    spawnscript:\"scripts/pwrup/nuke.sc\"\n"
  "    spawnscript:\"scripts/pwrup/nuke.sc\"\n    always_awake:true\n    position:< -480 0 448 >\n"
  "    name:\"monst_gatoroidb1\"\n  }\n"
  "  spawn monst_pigoid2 {\n    spawnscript:\"scripts/pwrup/sbullet.sc\"\n    name:\"monst_pigoid21\"\n  }\n"
  "  spawn spawner_entity {\n    monst_spawnscr:\"scripts/pwrup/nuke.sc\"\n    spawnscript:\"scripts/monst/pigoid1.sc\"\n"
  "    name:\"spawner_entity2\"\n  }\n"
  "  spawn spawner_entity {\n    spawnscript:\"scripts/pwrup/nuke.sc\"\n    name:\"spawner_nuke\"\n  }\n"
  "  spawn nuke {\n    position:< 100 0 0 >\n    name:\"nuke1\"\n  }\n}\n";

static void ar_carry_area(ArArea* a) {
  memset(a, 0, sizeof *a);
  strcpy_s(a->map, AR_NAME, "e09p09_a02");
  ar_parse_world(kArCarry, (int)strlen(kArCarry), a);
}

static void tb_0473_carry_detection() {
  CHECK(carry_is_nuke_script("scripts/pwrup/nuke.sc") && carry_is_nuke_script("Scripts/PwrUp/Nuke.sc"));
  CHECK(!carry_is_nuke_script("scripts/pwrup/sbullet.sc") && !carry_is_nuke_script("") && !carry_is_nuke_script(0));
  CHECK(carry_class("monst_pigoid2") && carry_class("boss_queenroach") && !carry_class("spawner_entity"));
  CHECK(!carry_class("nuke") && !carry_class(0));
  // The runtime list: char[64] entries at +0xCA0, the first +0xCA4 of them still held (monster_drop_one pops the last).
  static char list[3][CARRY_SCRIPT_LEN];
  memset(list, 0, sizeof list);
  strcpy_s(list[0], CARRY_SCRIPT_LEN, "scripts/pwrup/sbullet.sc");
  strcpy_s(list[1], CARRY_SCRIPT_LEN, "scripts/pwrup/nuke.sc");
  memset(list[2], 'x', CARRY_SCRIPT_LEN);                  // no NUL in the entry: never read past it
  CHECK(carry_nukes(list[0], 2) == 1 && carry_nukes(list[0], 1) == 0 && carry_nukes(list[0], 0) == 0);
  CHECK(carry_nukes(list[0], 3) == 1 && carry_nukes(0, 2) == 0);
  // world.scr: each nuke an enemy carries is one source of one nuke, by slot; spawners are not enemies.
  ArArea a;
  ar_carry_area(&a);
  CHECK(a.nnukes == 5 && a.nukeTotal == 5);
  CHECK(a.nukes[0].holder == AR_NK_ENEMY && strcmp(a.nukes[0].name, "monst_pigoid22") == 0);
  CHECK(strcmp(a.nukes[0].cls, "monst_pigoid2") == 0 && a.nukes[0].slot == 0 && a.nukes[0].count == 1);
  CHECK(a.nukes[0].pos[0] == 1168.f);
  CHECK(a.nukes[1].holder == AR_NK_ENEMY && a.nukes[1].slot == 0 && a.nukes[3].slot == 2);
  CHECK(strcmp(a.nukes[3].name, "monst_gatoroidb1") == 0 && strcmp(a.nukes[3].cls, "monst_gatoroidB") == 0);
  CHECK(a.nukes[4].holder == AR_NK_PICKUP && strcmp(a.nukes[4].name, "nuke1") == 0);
}

static void tb_0474_carry_label() {
  char b[32];
  CHECK(strcmp(carry_suffix(0, b, sizeof b), "") == 0);
  CHECK(strcmp(carry_suffix(1, b, sizeof b), " + Nuke") == 0);
  CHECK(strcmp(carry_suffix(2, b, sizeof b), " + 2 Nukes") == 0);
  CHECK(strcmp(carry_suffix(-1, b, sizeof b), "") == 0);
  CHECK(strcmp(carry_marker_label(1, b, sizeof b), "Nuke (enemy)") == 0);
  CHECK(strcmp(carry_marker_label(3, b, sizeof b), "3 Nukes (enemy)") == 0);
}

static ArLiveNuke ar_le(const char* name, int held) {
  ArLiveNuke e = ar_ln(name, 0.f, AR_NK_ENEMY, 1);
  e.held = held;
  return e;
}

// A monster's pass-1 record as e07p02_a01.msv holds monst_pigoid22: id, parent, +0x74, the class, floats, its
// spawnscript list (u16 count, then u16 length + chars each), more keys, then its name (u16 length + chars + NUL).
static int ar_put_monster(unsigned char* b, int at, unsigned id, const char* cls, const char* name,
                          const char* const* drops, int ndrops) {
  at = ar_put_u32(b, at, id);
  at = ar_put_u32(b, at, 0);
  at = ar_put_u32(b, at, 190);
  const unsigned char cl = (unsigned char)strlen(cls);
  at = ar_put(b, at, &cl, 1);
  at = ar_put(b, at, cls, cl);
  static const unsigned char kHead[26] = { 2, 0, 0, 0, 0, 0x80, 0x44 };
  at = ar_put(b, at, kHead, sizeof kHead);
  const unsigned short nd = (unsigned short)ndrops;
  at = ar_put(b, at, &nd, 2);
  for (int k = 0; k < ndrops; ++k) {
    const unsigned short l = (unsigned short)strlen(drops[k]);
    at = ar_put(b, at, &l, 2);
    at = ar_put(b, at, drops[k], l);
  }
  static const unsigned char kTail[100] = { 3, 0, 0, 0, 1 };
  at = ar_put(b, at, kTail, sizeof kTail);
  const unsigned short l = (unsigned short)strlen(name);
  at = ar_put(b, at, &l, 2);
  at = ar_put(b, at, name, l + 1);
  return ar_put_u32(b, at, 0xFFFFFFFFu);
}

static void tb_0475_ar_enemy_nukes_found() {
  ArArea a;
  ar_carry_area(&a);
  ArLiveNuke e[8];
  ArNukeFound f;
  // Live: the pig alive holding its nuke, the boss down to one of three, one drop-nuke lying, nuke1 there.
  int n = 0;
  e[n++] = ar_le("monst_pigoid22", 1);
  e[n++] = ar_le("monst_gatoroidb1", 1);
  e[n++] = ar_ln(CARRY_DROP_NAME, 50.f, AR_NK_PICKUP, 1);
  e[n++] = ar_ln("nuke1", 100.f, AR_NK_PICKUP, 1);
  ar_nukes_live(&a, e, n, &f);
  CHECK(f.known && !f.got[0] && !f.got[1] && !f.got[2] && f.got[3] && !f.got[4] && f.count == 1);
  // The pig dead (its list emptied by monster_drop_all): the one drop lying is counted first for it, so the boss's
  // last dropped nuke reads taken.
  e[0] = ar_le("monst_pigoid22", 0);
  ar_nukes_live(&a, e, n, &f);
  CHECK(!f.got[0] && f.got[2] && f.got[3] && f.count == 2);
  ar_nukes_live(&a, e + 1, n - 1, &f);                   // the pig's body removed: the same
  CHECK(!f.got[0] && f.count == 2);
  e[2].active = 0;                                        // the drop picked up this frame
  ar_nukes_live(&a, e, n, &f);
  CHECK(f.got[0] && f.got[2] && f.got[3] && f.count == 3);
  // A drop-nuke is an enemy's, never a broken crate's.
  ArArea c;
  ar_nk_area(&c);
  ArLiveNuke h[2];
  h[0] = ar_ln("barrel4", 4000.f, AR_NK_BARREL, 0);
  h[1] = ar_ln(CARRY_DROP_NAME, 4010.f, AR_NK_PICKUP, 1);
  ar_nukes_live(&c, h, 2, &f);
  CHECK(f.got[3]);
  // The file: the pig's record still lists its nuke, the boss's lists one, one drop-nuke record.
  static unsigned char b[2048];
  memset(b, 0xEE, sizeof b);
  const char* pig[] = { "scripts/pwrup/sbullet.sc", "scripts/pwrup/nuke.sc" };
  const char* boss[] = { "scripts/pwrup/nuke.sc" };
  int at = ar_put(b, 0, "\x0a\0e09p09_a02", 12);
  at = ar_put_monster(b, at, 230, "monst_pigoid2", "monst_pigoid21", pig, 1);
  at = ar_put_monster(b, at, 231, "monst_pigoid2", "monst_pigoid22", pig, 2);
  at = ar_put_monster(b, at, 232, "monst_gatoroidB", "monst_gatoroidb1", boss, 1);
  at = ar_put_nuke(b, at, 300, "nuke1");
  const int base = at;
  at = ar_put_nuke(b, at, 301, CARRY_DROP_NAME);
  int recs, unnamed;
  ar_nukes_from_msv(b, at, &a, &f, &recs, &unnamed);
  CHECK(f.known && f.src == AR_SRC_FILE && unnamed == 1);
  CHECK(!f.got[0] && !f.got[1] && !f.got[2] && f.got[3] && !f.got[4] && f.count == 1);
  ar_nukes_from_msv(b, base, &a, &f, &recs, &unnamed);    // no drop lying: both dropped nukes taken
  CHECK(f.known && f.got[2] && f.got[3] && f.count == 2);
  // The pig's record gone (dead and removed): its nuke dropped and, with no drop left, taken.
  static unsigned char g[2048];
  int gt = ar_put(g, 0, "\x0a\0e09p09_a02", 12);
  gt = ar_put_monster(g, gt, 232, "monst_gatoroidB", "monst_gatoroidb1", boss, 1);
  gt = ar_put_nuke(g, gt, 300, "nuke1");
  ar_nukes_from_msv(g, gt, &a, &f, &recs, &unnamed);
  CHECK(f.known && f.got[0] && !f.got[1] && f.count == 3);
  // A drop-nuke record does not make a broken crate's nuke unknown.
  ArArea ca;
  ar_nk_area(&ca);
  ca.nnukes = 3;
  ca.nukeTotal = 3;
  static unsigned char k[1024];
  int kt = ar_put(k, 0, "\x0a\0e09p09_a01", 12);
  kt = ar_put_holder(k, kt, 40, "\x05" "crate", "crate11");
  kt = ar_put_nuke(k, kt, 145, "nuke5");
  kt = ar_put_nuke(k, kt, 146, CARRY_DROP_NAME);
  kt = ar_put_holder_state(k, kt, 40, 0);
  ar_nukes_from_msv(k, kt, &ca, &f, &recs, &unnamed);
  CHECK(f.known && f.got[2] == 1);
}

// The Progress tab's Hard counter adds the part in play as the corner readout does: max(banked, live) for that part,
// only while the game is on Hard, whichever difficulty the tab shows.
static void tb_0477_pg_hard_live() {
  static LsDef d;
  CHECK(ls_parse_def(kPgDef, (int)strlen(kPgDef), &d));
  unsigned char* S = (unsigned char*)calloc(PG_SESSION_SPAN, 1);
  pg_put_rec(S, 2, 0, 0, 10.f, 0.f, 0.f, 1);
  pg_put_rec(S, 2, 0, 1, 7.f, 0.f, 0.f, 1);
  pg_put_rec(S, 2, 1, 0, 3.f, 0.f, 0.f, 1);
  static PgView v;
  LsLive live = { 0.f, 0.f, 6.f };
  pg_build(S, &d, LS_HARD, 1, 0, LS_HARD, &live, &v);
  CHECK_MSG(v.hardNukes == 23, "the live part's 6 replaces its banked 3");
  live.nukes = 2.f;
  pg_build(S, &d, LS_HARD, 1, 0, LS_HARD, &live, &v);
  CHECK_MSG(v.hardNukes == 20, "a replay below the banked best keeps the best");
  live.nukes = 6.f;
  pg_build(S, &d, 1, 1, 0, LS_HARD, &live, &v);
  CHECK_MSG(v.hardNukes == 23, "the tab on Normal still counts a Hard game's live part");
  pg_build(S, &d, LS_HARD, 1, 0, 1, &live, &v);
  CHECK_MSG(v.hardNukes == 20, "a Normal game's live nukes are not Hard nukes");
  pg_build(S, &d, LS_HARD, 1, 0, LS_HARD, 0, &v);
  CHECK_MSG(v.hardNukes == 20, "no live record: banked only");
  free(S);
}

static int tip_test_measure(const char* s, int n, void* ctx) { (void)s; return n * *(int*)ctx; }

// Hints wrap to a width at spaces; a word too wide is cut.
static void tb_0478_tip_wrap() {
  int px = 6, st[TIP_LINES_MAX], ln[TIP_LINES_MAX], widest = -1;
  const char* s = "aaa bbb ccc";
  int n = tip_wrap(s, 42, tip_test_measure, &px, st, ln, TIP_LINES_MAX, &widest);
  CHECK_MSG(n == 2 && st[0] == 0 && ln[0] == 7 && st[1] == 8 && ln[1] == 3 && widest == 42, "two words fit, one wraps");
  n = tip_wrap(s, 1000, tip_test_measure, &px, st, ln, TIP_LINES_MAX, &widest);
  CHECK_MSG(n == 1 && ln[0] == 11 && widest == 66, "short text stays on one line");
  n = tip_wrap("aa   bb", 24, tip_test_measure, &px, st, ln, TIP_LINES_MAX, &widest);
  CHECK_MSG(n == 2 && ln[0] == 2 && st[1] == 5 && ln[1] == 2, "the spaces at a break are dropped");
  n = tip_wrap("abcdefghij", 30, tip_test_measure, &px, st, ln, TIP_LINES_MAX, &widest);
  CHECK_MSG(n == 2 && ln[0] == 5 && st[1] == 5 && ln[1] == 5, "a word wider than the line is cut");
  n = tip_wrap("a b c d e", 6, tip_test_measure, &px, st, ln, 3, &widest);
  CHECK_MSG(n == 3 && st[2] == 4, "at most cap lines");
  n = tip_wrap("", 100, tip_test_measure, &px, st, ln, TIP_LINES_MAX, &widest);
  CHECK_MSG(n == 0 && widest == 0, "empty: no lines");
}

// The wrapped box's width limit, and the box staying on screen at the right and bottom edges.
static void tb_0479_tip_place() {
  CHECK_MSG(tip_max_w(600, 1920) == TIP_MAX_W, "420 on a wide panel");
  CHECK_MSG(tip_max_w(300, 1920) == 300, "the panel's width when narrower");
  CHECK_MSG(tip_max_w(600, 380) == 380 - 2 * TOOLTIP_PAD, "the screen less the padding");
  CHECK_MSG(tip_max_w(20, 1920) == TIP_MIN_W, "never below the floor");
  int px = 7, st[TIP_LINES_MAX], ln[TIP_LINES_MAX], widest = 0;
  const char* s = "Secrets: every area's, matching the game's tally. Nukes: the game's tally; 7 of them lie in the "
                  "areas' pickups, crates and barrels.";
  const int maxW = tip_max_w(600, 1280);
  const int n = tip_wrap(s, maxW, tip_test_measure, &px, st, ln, TIP_LINES_MAX, &widest);
  CHECK_MSG(n >= 2 && widest <= maxW, "the long hint wraps within the limit");
  const int h = n * 12 + (n - 1) * TIP_LINE_GAP;
  UiRect b = tooltip_box_place(1270, 700, widest, h, 1280, 720);
  CHECK_MSG(b.x >= 0 && b.x + b.w <= 1280 && b.y >= 0 && b.y + b.h <= 720, "near the bottom right: flipped on screen");
  b = tooltip_box_place(10, 10, widest, h, 1280, 720);
  CHECK_MSG(b.x == 10 + TOOLTIP_DX && b.y == 10 + TOOLTIP_DY, "away from the edges: below right of the cursor");
}

// The X-3000 (weap_x3000_fire_logic duke!0x1005c8f0): a plain hold, and only monsters it reaches are targets.
static void tb_0480_tbot_x3000_hold() {
  CHECK_MSG(tbot_weapon_fires_on_press(WK_X3000) && !tbot_weapon_semi_auto(WK_X3000), "held, not pulsed");
  int last = 0;
  CHECK_MSG(tbot_pulse(1, tbot_weapon_semi_auto(WK_X3000), &last) && tbot_pulse(1, 0, &last), "pressed every frame");
  CHECK_MSG(!tbot_pulse(0, 0, &last), "released when the condition breaks");
  CHECK_MSG(x3000_hurts(1, 1, 20.f), "a drawn live monster takes the 1000");
  CHECK_MSG(!x3000_hurts(0, 1, 20.f), "no user bit 0x20 (boss, part, egg): never searched");
  CHECK_MSG(!x3000_hurts(1, 0, 20.f), "+0xAF1 clear (not drawn): skipped");
  CHECK_MSG(!x3000_hurts(1, 1, 0.f), "health <= 0: skipped");
  CHECK_MSG(x3000_hurts(1, -1, 20.f), "+0xAF1 unread: stays a target");
  CHECK_MSG(ovk_send_counts(0, 1000.f, 0), "its send (type 0, 1000) dooms through the dealt ledger");
  {
    // x3000_pick: the first candidate it hurts that overkill does not doom; no line of sight involved.
    const int h1[4] = { 0, 1, 1, 1 }, d1[4] = { 0, 1, 0, 0 };
    CHECK_MSG(x3000_pick(h1, d1, 4) == 2, "skips the unhurtable and the doomed, takes the next");
    const int d2[4] = { 0, 0, 0, 0 };
    CHECK_MSG(x3000_pick(h1, d2, 4) == 1, "the first hurtable");
    const int d3[4] = { 1, 1, 1, 1 };
    CHECK_MSG(x3000_pick(h1, d3, 4) == -1, "all doomed: none");
    const int h0[3] = { 0, 0, 0 };
    CHECK_MSG(x3000_pick(h0, d2, 3) == -1 && x3000_pick(h0, d2, 0) == -1, "none hurtable, or empty: none");
  }
  WimmFacts f = { WIMM_CLASS_SMECH, 1, 0, 0, 1.f, 10.f, 0, 0.f };
  CHECK_MSG(wimm_gate(wimm_spec(WK_X3000), f) == WIMM_OK, "skips the SecurityMech's vt+0x194 line");
  f.cls = WIMM_CLASS_SHELL; f.state = WIMM_SHELL_STATE;
  CHECK_MSG(wimm_gate(wimm_spec(WK_X3000), f) == WIMM_IMM_SHELL, "the roachoid shell zeroes it");
}

// --- Blue laser traps that spawn a nuke carrier (re/pickups.md section 7) ---
// world.scr as e08p03_a01: a red laser wired to damage, a blue one wired to an sc5 carrier's spawner, one on a moving
// pod wired to a nuke carrier's spawner, one wired to three spawners (one a nuke carrier's), after the lasers.
static const char kArLaser[] =
  "spawn map {\n"
  "  spawn laser {\n    world_collide:false\n    target:< 0 0 -304 >\n    shader:\"shaders/e08/laser_red.shd\"\n"
  "    position:< -1402 -172 -50 >\n    name:\"laser15\"\n    connect obstructed:area_damage4.area_active\n  }\n"
  "  spawn laser {\n    shader:\"shaders/e08/laser_blue.shd\"\n    position:< -1678 -172 -50 >\n    name:\"laser16\"\n"
  "    connect obstructed:spawner_entity1.active\n    connect obstructed:not3.input\n  }\n"
  "  spawn mover_adv {\n    position:< 7186 264 -152 >\n    name:\"mover_laserpod13\"\n"
  "    spawn plain_light {\n      position:< 56 -16 -26 >\n      name:\"light215\"\n    }\n"
  "    spawn laser {\n      shader:\"shaders/e08/laser_blue.shd\"\n      position:< 0 -34 -42 >\n      name:\"laser31\"\n"
  "      connect obstructed:mover_laserpod13.active\n      connect obstructed:not9.input\n"
  "      connect obstructed:spawner_entity3.active\n    }\n  }\n"
  "  spawn laser {\n    shader:\"shaders/e08/laser_blue.shd\"\n    position:< 9276 3628 -836 >\n    name:\"laser38\"\n"
  "    connect obstructed:not11.input\n    connect obstructed:spawner_entity7.active\n"
  "    connect obstructed:spawner_entity6.active\n  }\n"
  "  spawn spawner_entity {\n    monst_spawnscr:\"scripts/pwrup/sc5.sc\"\n    spawnscript:\"scripts/monst/securitymech2.sc\"\n"
  "    name:\"spawner_entity1\"\n  }\n"
  "  spawn spawner_entity {\n    monst_spawnscr:\"scripts/pwrup/nuke.sc\"\n    spawnscript:\"scripts/monst/securitymech3.sc\"\n"
  "    name:\"spawner_entity3\"\n  }\n"
  "  spawn spawner_entity {\n    monst_spawnscr:\"scripts/pwrup/sc5.sc\"\n    spawnscript:\"scripts/monst/securitymech3.sc\"\n"
  "    name:\"spawner_entity7\"\n  }\n"
  "  spawn spawner_entity {\n    monst_spawnscr:\"scripts/pwrup/nuke.sc\"\n    spawnscript:\"scripts/monst/securitymech3.sc\"\n"
  "    name:\"spawner_entity6\"\n  }\n"
  "  spawn nuke {\n    position:< 100 0 0 >\n    name:\"nuke1\"\n  }\n}\n";

// --- Breakables that drop a pickup (breakrule.h, areas.h; re/pickups.md section 8) ---
// world.scr as e01p01_a01: a box whose `broken` pin fires a spawner with a pickup `spawnscript`, one firing a
// monster-only spawner, one firing a spawner whose monster carries the pickup (monst_spawnscr), one with no spawner.
static const char kArBrk[] =
  "spawn area_x {\n"
  "  spawn breakable_adv {\n    name:\"spawn_box\"\n    connect broken:spawner_a.active\n    connect broken:area_switch1.signal\n  }\n"
  "  spawn breakable_adv {\n    name:\"mons_box\"\n    connect broken:spawner_b.active\n  }\n"
  "  spawn breakable_adv {\n    name:\"carry_box\"\n    connect broken:spawner_c.active\n  }\n"
  "  spawn breakable_adv {\n    name:\"plain_box\"\n  }\n"
  "  spawn breakable_adv {\n    name:\"ego_box\"\n    connect broken:spawner_d.active\n  }\n"
  "  spawn spawner_entity {\n    spawnscript:\"scripts/pwrup/sbullet.sc\"\n    name:\"spawner_a\"\n  }\n"
  "  spawn spawner_entity {\n    spawnscript:\"scripts/monst/pigoid.sc\"\n    name:\"spawner_b\"\n  }\n"
  "  spawn spawner_entity {\n    monst_spawnscr:\"scripts/pwrup/sbullet.sc\"\n    spawnscript:\"scripts/monst/pigoid.sc\"\n"
  "    name:\"spawner_c\"\n  }\n"
  "  spawn spawner_entity {\n    spawnscript:\"scripts/pwrup/lhealth.sc\"\n    name:\"spawner_d\"\n  }\n"
  "}\n";

static void tb_0497_breakable_drops() {
  ArArea a;
  memset(&a, 0, sizeof a);
  strcpy_s(a.map, AR_NAME, "e09p09_a01");
  ar_parse_world(kArBrk, (int)strlen(kArBrk), &a);
  CHECK_MSG(a.nbreaks == 2, "only the breakables whose spawner's own script is a pickup");
  CHECK(strcmp(a.breaks[0].name, "spawn_box") == 0 && strcmp(a.breaks[0].spawner, "spawner_a") == 0);
  CHECK(a.breaks[0].kind == ITEM_AMMO && a.breaks[0].subtype == 0);
  CHECK(strcmp(a.breaks[1].name, "ego_box") == 0 && a.breaks[1].kind == ITEM_EGO && a.breaks[1].subtype == 1);
  CHECK_MSG(a.nnukes == 0 && a.nukeTotal == 0, "no nuke source");
  unsigned char k; int sub;
  CHECK(brk_script_content("scripts/pwrup/lc5.sc", &k, &sub) && k == ITEM_AMMO && sub == 3);
  CHECK(!brk_script_content("scripts/pwrup/nuke.sc", &k, &sub) && !brk_script_content("scripts/monst/pigoid.sc", &k, &sub) &&
        !brk_script_content(0, &k, &sub));
  CHECK(strcmp(brk_label(ITEM_AMMO, 0), "Bullets +10 (breakable)") == 0);
  CHECK(strcmp(brk_label(ITEM_EGO, 1), "Ego +100 (breakable)") == 0 && brk_label(ITEM_EGO, 2) == 0);
  CHECK(brk_intact(1) && !brk_intact(0));
}

// e01p01_a01's real wiring (world.scr, CRLF): the spawner is the 5th of brk_explodbox's six `broken` pins.
static const char kArBrkReal[] =
  "spawn breakable_adv {\r\n    health:20\r\n    position:< 5752 -956 136 >\r\n    name:\"brk_explodbox\"\r\n"
  "    connect broken:actor_explo2.active\r\n    connect broken:snd_sparks2.active\r\n"
  "    connect broken:e01_sparks11.active\r\n    connect broken:steam_8.active\r\n"
  "    connect broken:spawn_box.active\r\n    connect broken:sound1.active\r\n  }\r\n"
  "  spawn spawner_entity {\r\n    spawnscript:\"scripts/pwrup/sbullet.sc\"\r\n    spawn_type:1\r\n"
  "    position:< 5768 -960 128 >\r\n    name:\"spawn_box\"\r\n  }\r\n";

static void tb_0498_breakable_six_pins() {
  ArArea a;
  memset(&a, 0, sizeof a);
  strcpy_s(a.map, AR_NAME, "e01p01_a01");
  ar_parse_world(kArBrkReal, (int)strlen(kArBrkReal), &a);
  CHECK_MSG(a.nbreaks == 1 && strcmp(a.breaks[0].name, "brk_explodbox") == 0 &&
            strcmp(a.breaks[0].spawner, "spawn_box") == 0, "a spawner past the 4th pin still counts (the game has 6)");
}

static void ar_laser_area(ArArea* a) {
  memset(a, 0, sizeof *a);
  strcpy_s(a->map, AR_NAME, "e09p09_a01");
  ar_parse_world(kArLaser, (int)strlen(kArLaser), a);
}

// The detection: a laser whose `obstructed` pin fires a spawner that hands its monster a nuke is one source per such
// spawner, after the area's other sources; its position adds its parents' (the pod's) positions.
static void tb_0481_laser_world_parse() {
  ArArea a;
  ar_laser_area(&a);
  CHECK_MSG(a.nnukes == 3 && a.nukeTotal == 3, "the pickup and two nuke lasers");
  CHECK(a.nukes[0].holder == AR_NK_PICKUP && strcmp(a.nukes[0].name, "nuke1") == 0);
  CHECK(a.nukes[1].holder == AR_NK_LASER && strcmp(a.nukes[1].name, "laser31") == 0);
  CHECK_MSG(strcmp(a.nukes[1].cls, "spawner_entity3") == 0 && a.nukes[1].count == 1 && a.nukes[1].slot == 1,
            "the spawner it fires, the nukes and the entities per fire");
  CHECK_MSG(a.nukes[1].pos[0] == 7186.f && a.nukes[1].pos[1] == 230.f && a.nukes[1].pos[2] == -194.f,
            "a pod's laser sits at the pod plus its own offset");
  CHECK_MSG(a.nukes[2].holder == AR_NK_LASER && strcmp(a.nukes[2].name, "laser38") == 0 &&
            strcmp(a.nukes[2].cls, "spawner_entity6") == 0, "of three spawners, only the nuke carrier's");
  CHECK_MSG(a.nukes[2].pos[0] == 9276.f, "a top-level laser keeps its own position");
  ArNukeFound f;
  ar_nukes_fresh(&a, &f);
  CHECK(f.known && f.count == 0);
}

static ArLiveNuke ar_lz(const char* name, int armed, int spawned) {
  ArLiveNuke e = ar_ln(name, 0.f, AR_NK_LASER, armed);
  e.held = spawned;
  return e;
}

// The found state: a laser's nuke is missing until its spawner has fired, then while the carrier holds it or its drop
// lies; each fire past the first is a nuke the part's listed total does not count.
static void tb_0482_laser_nukes_found() {
  ArArea a;
  ar_laser_area(&a);
  ArLiveNuke e[8];
  ArNukeFound f;
  int n = 0;
  e[n++] = ar_ln("nuke1", 100.f, AR_NK_PICKUP, 0);
  e[n++] = ar_lz("laser31", 1, 0);
  e[n++] = ar_lz("laser38", 1, 0);
  ar_nukes_live(&a, e, n, &f);
  CHECK_MSG(f.known && f.got[0] && !f.got[1] && !f.got[2] && f.count == 1 && f.extra == 0, "armed: not yet spawned");
  e[1] = ar_lz("laser31", 0, 1);
  ArLiveNuke c = ar_ln("spawner_entity3-spwn000", 0.f, AR_NK_SPAWNED, 1);
  c.held = 1;
  e[n++] = c;
  ar_nukes_live(&a, e, n, &f);
  CHECK_MSG(!f.got[1], "fired: its carrier still holds the nuke");
  e[n - 1].held = 0;
  ar_nukes_live(&a, e, n, &f);
  CHECK_MSG(f.got[1] && f.count == 2, "the carrier dead, nothing lying: taken");
  e[n++] = ar_ln(CARRY_DROP_NAME, 0.f, AR_NK_PICKUP, 1);
  ar_nukes_live(&a, e, n, &f);
  CHECK_MSG(!f.got[1] && f.count == 1, "its drop lying: missing");
  n--;
  e[2] = ar_lz("laser38", 0, 0);
  ar_nukes_live(&a, e, n, &f);
  CHECK_MSG(!f.got[2], "disarmed but its spawner never fired: missing");
  e[1] = ar_lz("laser31", 1, 2);
  ar_nukes_live(&a, e, n, &f);
  CHECK_MSG(f.got[1] && f.extra == 1, "re-armed after two fires: one nuke past the listed one");
  ar_nukes_live(&a, e, 1, &f);
  CHECK_MSG(!f.got[1] && !f.got[2], "a laser the walk did not find: not yet spawned");
  CHECK(laser_fires(0, 1) == 0 && laser_fires(2, 1) == 2 && laser_fires(3, 2) == 2 && laser_fires(5, 0) == 5);
  // The file holds no laser state: those sources come from our record.
  static unsigned char b[512];
  int at = ar_put(b, 0, "\x0a\0e09p09_a01", 12);
  at = ar_put_nuke(b, at, 300, "nuke1");
  int recs, unnamed;
  ar_nukes_from_msv(b, at, &a, &f, &recs, &unnamed);
  CHECK_MSG(!f.known && f.src == AR_SRC_FILE && f.have[0] && !f.have[1] && !f.have[2], "lasers unread in the file");
  // The tally: a laser fired twice takes the part past its listed total.
  ArPart p;
  memset(&p, 0, sizeof p);
  p.nareas = 1;
  p.areas[0] = a;
  ArNukeFound nf[AR_AREAS_MAX];
  memset(nf, 0, sizeof nf);
  nf[0].known = 1; nf[0].src = AR_SRC_LIVE; nf[0].count = 3; nf[0].extra = 1;
  int sum, derived;
  CHECK_MSG(ar_nuke_tally(&p, nf, 4, 1, 3, &sum, &derived) == AR_TALLY_OK && sum == 3, "4 of 3 with one refire: ok");
  nf[0].extra = 0;
  CHECK(ar_nuke_tally(&p, nf, 4, 1, 3, &sum, &derived) == AR_TALLY_MISMATCH);
}

// The marker: its label, the beam it frames, and the spawned carriers' names.
static void tb_0483_laser_marker() {
  char b[32];
  CHECK(strcmp(laser_marker_label(1, b, sizeof b), "Nuke (laser)") == 0);
  CHECK(strcmp(laser_marker_label(2, b, sizeof b), "2 Nukes (laser)") == 0);
  CHECK(laser_spawned_by("spawner_entity3-spwn000", "spawner_entity3"));
  CHECK(!laser_spawned_by("spawner_entity31-spwn000", "spawner_entity3") && !laser_spawned_by("spawner_entity3", "spawner_entity3"));
  CHECK(!laser_spawned_by(0, "spawner_entity3") && !laser_spawned_by("x-spwn000", ""));
  const float s[3] = { 100.f, 0.f, -50.f }, e[3] = { 100.f, 0.f, -354.f };
  float c[3], h[3];
  laser_beam_box(s, e, 1, c, h);
  CHECK_MSG(c[0] == 100.f && c[2] == -202.f && h[2] == 152.f && h[0] == LASER_BOX_MIN, "the beam's middle and length");
  laser_beam_box(s, e, 0, c, h);
  CHECK_MSG(c[2] == -50.f && h[2] == LASER_BOX_MIN, "no beam end read: the emitter");
  const float farEnd[3] = { 100.f, 0.f, 9000.f };
  laser_beam_box(s, farEnd, 1, c, h);
  CHECK_MSG(c[2] == -50.f, "an end past the longest beam is not trusted");
}

// The nukes lines read found over the game's listed total, never raised to the count: a laser fired twice reads
// 11/10, and the Hard sum 242/240. Kills and secrets keep the part-end screen's max.
static void tb_0484_nuke_totals_raw() {
  char out[LS_LINES][LS_LINE_CAP];
  LsLive live = { 31.f, 0.f, 11.f };
  LsTotals t = { 29, 0, 10 };
  ls_lines(live, &t, 242, 240, out);
  CHECK(strcmp(out[2], "Nukes 11/10") == 0 && strcmp(out[3], "Hard nukes 242/240") == 0);
  CHECK_MSG(strcmp(out[0], "Kills 31/31") == 0, "kills unchanged");
  char b[24];
  pg_readout(b, sizeof b, "", 11, 10, 1, 1, true);
  CHECK(strcmp(b, "11/10") == 0);
  pg_readout(b, sizeof b, "", 7, 10, 1, 1, true);
  CHECK(strcmp(b, "7/10") == 0);
}

// The user's complete Hard save: the Load Game slot reads KILLS 1133/1136, the Progress rows added up 1133/1157
// The shipped def (db_p101) on Hard: killcounts 1124 + extrakills0202 12 (E1P3) = 1136. Rows
// whose banked kills pass the def's total (21 over in all) must not lift their denominators.
static void tb_0485_pg_kill_totals_match_slot() {
  static const int kKills[8][3] = { { 29, 58, 28 }, { 48, 48, 28 }, { 84, 54, 41 }, { 82, 76, 16 },
                                    { 57, 70, 22 }, { 85, 53, 13 }, { 55, 40, 29 }, { 29, 63, 16 } };
  static char text[4096];
  int n = 0;
  for (int e = 0; e < 8; ++e) {
    n += sprintf_s(text + n, sizeof text - n, "def episode%02d {\n partcount:3\n", e);
    for (int p = 0; p < 3; ++p) n += sprintf_s(text + n, sizeof text - n, " killcount%02d:%d\n", p, kKills[e][p]);
    if (e == 0) n += sprintf_s(text + n, sizeof text - n, " extrakills0200:6\n extrakills0201:9\n extrakills0202:12\n");
    n += sprintf_s(text + n, sizeof text - n, "}\n");
  }
  static LsDef d;
  CHECK(ls_parse_def(text, n, &d));
  unsigned char* S = (unsigned char*)calloc(PG_SESSION_SPAN, 1);
  for (int e = 0; e < 8; ++e)
    for (int p = 0; p < 3; ++p) {
      float k = (float)(kKills[e][p] + (e == 0 && p == 2 ? 12 : 0));
      if (e == 0 && p == 0) k += 6.f;    // over the total: spawner kills
      if (e == 2 && p == 0) k += 15.f;
      if (e == 1 && p == 0) k -= 24.f;   // short of it
      pg_put_rec(S, LS_HARD, e, p, 0.f, 0.f, k, 1);
    }
  static PgView v;
  pg_build(S, &d, LS_HARD, 7, 2, LS_HARD, 0, &v);
  int found = 0, total = 0, rowDen = 0, epDen = 0;
  for (int e = 0; e < v.episodes; ++e) {
    found += v.epFound[e].kills;
    total += v.epTotal[e].kills;
    char b[24];
    int f = 0, t = 0;
    pg_column_readout(b, sizeof b, PG_COL_KILLS, v.epFound[e], v.epTotal[e], 1, 1);
    if (sscanf_s(b, "K %d/%d", &f, &t) == 2) epDen += t;
    for (int p = 0; p < v.parts[e]; ++p) {
      const PgRow& r = v.rows[e][p];
      pg_column_readout(b, sizeof b, PG_COL_KILLS, r.found, r.total, r.haveTotal, 1);
      if (sscanf_s(b, "K %d/%d", &f, &t) == 2) rowDen += t;
    }
  }
  CHECK_MSG(found == 1133 && total == 1136, "the slot's sums: banked kills over killcount + extrakills[Hard]");
  CHECK_MSG(rowDen == 1136, "the part rows' kill totals add up to the slot's 1136, not 1157");
  CHECK_MSG(epDen == 1136, "the episode totals' kill denominators add up to 1136");
  free(S);
}

// --- Save awards (save-awards.md section 2) ----------------------------------------------------------
// The shipped def's shape: 8 episodes of 3 parts, 10 nukes each (240), 3 secrets in the first 7 parts and 2 in the
// rest (55), the killcounts of tb_0485 (1124) with E1P3's extrakills 6/9/12.
static int aw_def_text(char* text, int cap) {
  static const int kKills[8][3] = { { 29, 58, 28 }, { 48, 48, 28 }, { 84, 54, 41 }, { 82, 76, 16 },
                                    { 57, 70, 22 }, { 85, 53, 13 }, { 55, 40, 29 }, { 29, 63, 16 } };
  int n = 0;
  for (int e = 0; e < 8; ++e) {
    n += sprintf_s(text + n, cap - n, "def episode%02d {\n partcount:3\n", e);
    for (int p = 0; p < 3; ++p)
      n += sprintf_s(text + n, cap - n, " killcount%02d:%d\n nukecount%02d:10\n secretcount%02d:%d\n", p,
                     kKills[e][p], p, p, e * 3 + p < 7 ? 3 : 2);
    if (e == 0) n += sprintf_s(text + n, cap - n, " extrakills0200:6\n extrakills0201:9\n extrakills0202:12\n");
    n += sprintf_s(text + n, cap - n, "}\n");
  }
  return n;
}

static void aw_put_int(unsigned char* p, int v) { memcpy(p, &v, 4); }

static void tb_0486_aw_thresholds() {
  static char text[8192];
  const int n = aw_def_text(text, sizeof text);
  static LsDef d;
  CHECK(ls_parse_def(text, n, &d));
  unsigned char* S = (unsigned char*)calloc(PG_SESSION_SPAN, 1);
  aw_put_int(S + PG_SAVE_DIFF, LS_HARD);
  static AwView v;
  aw_build(S, &d, 0, 0, LS_HARD, 0, &v);
  CHECK(v.ok && v.parts == 24);
  for (int k = 0; k < LS_DIFFS; ++k) {
    CHECK_MSG(v.cell[AW_FINISHED][k].total == 24, "finished: every part of the def");
    CHECK_MSG(v.cell[AW_NUKES][k].total == 240, "all nukes: 240 on each difficulty");
    CHECK_MSG(v.cell[AW_SECRETS][k].total == 55, "all secrets: 55 on each difficulty");
  }
  CHECK_MSG(v.cell[AW_KILLS][0].total == 1136 && v.cell[AW_KILLS][1].total == 1136 &&
            v.cell[AW_KILLS][2].total == 1136, "all kills: every column takes the save's extrakills (Hard)");
  CHECK(v.saveDiff == LS_HARD);
  aw_put_int(S + PG_SAVE_DIFF, 0);
  aw_build(S, &d, 0, 0, 0, 0, &v);
  CHECK_MSG(v.cell[AW_KILLS][0].total == 1130 && v.cell[AW_KILLS][2].total == 1130,
            "all kills on an Easy save: 1130 in every column");
  aw_build(S, 0, 0, 0, LS_HARD, 0, &v);
  CHECK_MSG(!v.ok, "no def, no totals");
  free(S);
}

static void tb_0487_aw_cells() {
  static char text[8192];
  const int n = aw_def_text(text, sizeof text);
  static LsDef d;
  CHECK(ls_parse_def(text, n, &d));
  unsigned char* S = (unsigned char*)calloc(PG_SESSION_SPAN, 1);
  aw_put_int(S + PG_SAVE_DIFF, LS_HARD);
  for (int e = 0; e < 8; ++e)
    for (int p = 0; p < 3; ++p) {
      LsTotals t;
      ls_totals(&d, e, p, LS_HARD, &t);
      pg_put_rec(S, LS_HARD, e, p, 10.f, (float)t.secrets, (float)(t.kills - (e == 1 && p == 0 ? 3 : 0)), 1);
    }
  pg_put_rec(S, 1, 2, 1, 4.f, 1.f, 30.f, 1);   // Normal: one part
  pg_put_float(S + PG_BONUS + 4 * LS_HARD, 1.f);
  pg_put_float(S + PG_BONUS + 4 * 1, -1.f);
  pg_put_float(S + PG_WOZMA, 1.f);
  S[PG_CHEATED + LS_HARD] = 1;
  static AwView v;
  aw_build(S, &d, 0, 0, LS_HARD, 0, &v);
  CHECK(v.cell[AW_FINISHED][2].state == AW_EARNED && v.cell[AW_FINISHED][2].found == 24);
  CHECK(v.cell[AW_NUKES][2].state == AW_EARNED && v.cell[AW_SECRETS][2].state == AW_EARNED);
  CHECK(v.cell[AW_KILLS][2].state == AW_MISSING && v.cell[AW_KILLS][2].found == 1133);
  CHECK(v.cell[AW_BONUS][2].state == AW_EARNED);
  CHECK_MSG(v.cell[AW_FINISHED][1].state == AW_MISSING && v.cell[AW_FINISHED][1].found == 1 &&
            v.cell[AW_NUKES][1].found == 4 && v.cell[AW_BONUS][1].state == AW_MISSING,
            "Normal: a part finished is played; a negative bonus float is not earned");
  for (int r = 0; r < AW_ROWS; ++r) CHECK_MSG(v.cell[r][0].state == AW_NOT_PLAYED, "Easy: never played");
  CHECK(v.wozma == 1 && !v.cheated[0] && !v.cheated[1] && v.cheated[2]);
  // The part in play counts as max(banked, live), but an award is earned only by the banked sum.
  const LsLive live = { 99.f, 0.f, 2.f };   // kills, secrets, nukes: kills past the bank, nukes under it
  aw_build(S, &d, 1, 0, LS_HARD, &live, &v);
  CHECK_MSG(v.cell[AW_KILLS][2].found == 1187 && v.cell[AW_KILLS][2].state == AW_MISSING,
            "live kills shown, the award still missing");
  CHECK_MSG(v.cell[AW_NUKES][2].found == 240, "nukes under the bank keep the bank");
  // The save's own difficulty is played with nothing finished on it (the game's in-progress icon).
  aw_put_int(S + PG_SAVE_DIFF, 0);
  aw_build(S, &d, 0, 0, 0, 0, &v);
  CHECK(v.cell[AW_FINISHED][0].state == AW_MISSING && v.cell[AW_FINISHED][0].found == 0);
  CHECK_MSG(v.cell[AW_KILLS][2].state == AW_EARNED && v.cell[AW_KILLS][2].total == 1130,
            "Hard's 1133 banked kills earn D on an Easy save, as the slot shows it");
  // Finished is the game's AND of the completion bytes over the def's parts.
  S[pg_done_offset(LS_HARD, 7, 2)] = 0;
  aw_build(S, &d, 0, 0, 0, 0, &v);
  CHECK(v.cell[AW_FINISHED][2].state == AW_MISSING && v.cell[AW_FINISHED][2].found == 23);
  free(S);
}

static void tb_0488_aw_text() {
  char b[256];
  AwCell c = { AW_NOT_PLAYED, 0, 24 };
  aw_cell_text(c, AW_FINISHED, LS_HARD, b, sizeof b);
  CHECK(strcmp(b, "not played") == 0);
  aw_cell_text(c, AW_NUKES, LS_HARD, b, sizeof b);
  CHECK(strcmp(b, "-") == 0);
  c.state = AW_MISSING; c.found = 2;
  aw_cell_text(c, AW_FINISHED, LS_HARD, b, sizeof b);
  CHECK(strcmp(b, "2 / 24 parts") == 0);
  c.found = 1133; c.total = 1136;
  aw_cell_text(c, AW_KILLS, LS_HARD, b, sizeof b);
  CHECK(strcmp(b, "1133 / 1136") == 0);
  AwCell at = { AW_MISSING, 240, 240 };
  aw_cell_text(at, AW_NUKES, LS_HARD, b, sizeof b);
  CHECK_MSG(strcmp(b, "240 / 240 (at part end)") == 0, "reached live, banked at the part's end");
  at.found = 1187; at.total = 1136;
  aw_cell_text(at, AW_KILLS, LS_HARD, b, sizeof b);
  CHECK(strcmp(b, "1187 / 1136 (at part end)") == 0);
  CHECK(aw_bar_w(c, 100) == 99);
  c.found = 1200;
  CHECK(aw_bar_w(c, 100) == 100);
  c.total = 0;
  CHECK(aw_bar_w(c, 100) == 0);
  aw_cell_text(c, AW_BONUS, LS_HARD, b, sizeof b);
  CHECK(strcmp(b, "X-3000") == 0);
  c.state = AW_EARNED;
  aw_cell_text(c, AW_BONUS, 0, b, sizeof b);
  CHECK(strcmp(b, "Mighty Boot") == 0);
  aw_cell_text(c, AW_SECRETS, 0, b, sizeof b);
  CHECK_MSG(b[0] == 0, "an earned count draws its glyph, no text");
  CHECK(strcmp(aw_bonus_name(1), "Mighty Duke") == 0);

  static AwView v;
  memset(&v, 0, sizeof v);
  v.ok = 1; v.parts = 24; v.saveDiff = LS_HARD;
  for (int k = 0; k < LS_DIFFS; ++k) {
    v.cell[AW_NUKES][k].total = 240; v.cell[AW_SECRETS][k].total = 55; v.cell[AW_KILLS][k].total = 1136;
  }
  PgNames names;
  pg_names(0, 0, 0, &names);
  aw_tip(&v, AW_KILLS, LS_HARD, &names, b, sizeof b);
  CHECK(strcmp(b, "All kills on Hard: kill 1136 across all 24 parts. Replay a part that is short of its total.") == 0);
  aw_tip(&v, AW_KILLS, 0, &names, b, sizeof b);
  CHECK(strcmp(b, "All kills on Easy: kill 1136 across all 24 parts; the game counts the extra kills of the save's "
                  "difficulty (Hard) in every column. Replay a part that is short of its total.") == 0);
  aw_tip(&v, AW_BONUS, LS_HARD, &names, b, sizeof b);
  CHECK(strcmp(b, "X-3000: the X-3000 joins your weapons. Earn it: finish every part on Hard with all 240 nukes; "
                  "granted at a part's end.") == 0);
  aw_tip(&v, AW_BONUS, 0, &names, b, sizeof b);
  CHECK(strcmp(b, "Mighty Boot: your kick does double damage (30, not 15). Earn it: finish every part on Easy with all "
                  "240 nukes; granted at a part's end.") == 0);
  aw_tip(&v, AW_BONUS, 1, &names, b, sizeof b);
  CHECK(strcmp(b, "Mighty Duke: you take half damage. Earn it: finish every part on Normal with all 240 nukes; "
                  "granted at a part's end.") == 0);
  aw_tip(&v, AW_NUKES, 0, &names, b, sizeof b);
  CHECK(strcmp(b, "All nukes on Easy: bank 240 across all 24 parts. Replay a part that is short of its total.") == 0);
  aw_tip(&v, AW_SECRETS, 1, &names, b, sizeof b);
  CHECK(strcmp(b, "All secrets on Normal: find 55 across all 24 parts. Replay a part that is short of its total.") == 0);
  aw_tip(&v, AW_FINISHED, 1, &names, b, sizeof b);
  CHECK(strcmp(b, "Finished on Normal: reach the end of all 24 parts on Normal.") == 0);

  int ch[LS_DIFFS] = { 0, 0, 1 };
  CHECK(aw_cheated_text(ch, &names, b, sizeof b) && strcmp(b, "Cheated on Hard") == 0);
  ch[0] = 1;
  CHECK(aw_cheated_text(ch, &names, b, sizeof b) && strcmp(b, "Cheated on Easy and Hard") == 0);
  ch[1] = 1;
  CHECK(aw_cheated_text(ch, &names, b, sizeof b) && strcmp(b, "Cheated on Easy, Normal and Hard") == 0);
  ch[0] = ch[1] = ch[2] = 0;
  CHECK_MSG(!aw_cheated_text(ch, &names, b, sizeof b) && b[0] == 0, "clean: nothing");
}

// The panel's flagged cvars (save-awards.md section 4): g_p_god is flagged too but stays a cheat.
static void tb_0490_cm_cvars() {
  CHECK(strcmp(kCmCvars[0], "g_fps") == 0 && strcmp(kCmCvars[1], "g_cam_fov") == 0);
  CHECK(strcmp(kCmCvars[2], "g_cam_far") == 0);
  for (int i = 0; i < CM_NCVARS; ++i) CHECK_MSG(strcmp(kCmCvars[i], "g_p_god") != 0, "g_p_god is never held");
}

// The record's own pointers, as cvar_register_variable sets them; a linked cvar is left to the check.
static void tb_0491_cm_layout_and_restore() {
  static unsigned char cv[CM_CVAR_SIZE];
  const void* value = cv + CM_OFS_VALBUF;
  const void* def = cv + CM_OFS_DEFBUF;
  CHECK(cm_layout_ok(cv, value, def, 0));
  CHECK_MSG(!cm_layout_ok(cv, value, def, cv), "linked");
  CHECK_MSG(!cm_layout_ok(cv, def, def, 0), "value elsewhere");
  CHECK_MSG(!cm_layout_ok(cv, value, value, 0), "default elsewhere");
  CHECK_MSG(!cm_layout_ok(0, value, def, 0), "no record");
  // WideGen's widescreen FOV on the Enhanced launcher, against g_cam_fov's "50" (measured 2026-10-03).
  CHECK(cm_should_restore(1, "79.7553406", "50"));
  CHECK(cm_should_restore(1, "1", "0"));
  CHECK_MSG(!cm_should_restore(1, "50", "50"), "at its default");
  CHECK_MSG(!cm_should_restore(0, "1", "0"), "bad layout");
  CHECK_MSG(!cm_should_restore(1, 0, "0") && !cm_should_restore(1, "1", 0), "null");
}

// Saved before the default goes in, given back once after the check.
static void tb_0492_cm_slot() {
  CmSlot s;
  memset(&s, 0x55, sizeof s);
  CHECK(cm_slot_begin(&s, 1, "1000000", "8000") == 1);
  const char* back = cm_slot_end(&s);
  CHECK(back && strcmp(back, "1000000") == 0);
  CHECK_MSG(cm_slot_end(&s) == 0, "given back once");
  CHECK_MSG(cm_slot_begin(&s, 1, "8000", "8000") == 0 && cm_slot_end(&s) == 0, "at its default");
  CHECK(cm_slot_begin(&s, 1, "85.0000", "50") == 1);
  cm_slot_failed(&s);
  CHECK_MSG(cm_slot_end(&s) == 0, "set refused: nothing to put back");
  char longv[CM_VALUE_CAP + 8];
  memset(longv, '7', sizeof longv - 1);
  longv[sizeof longv - 1] = 0;
  CHECK(cm_slot_begin(&s, 1, longv, "50") == 1);
  back = cm_slot_end(&s);
  CHECK_MSG(back && strlen(back) == CM_VALUE_CAP - 1, "the 0x3F chars cvar_t::set keeps");
}

// duke!0x100e13d0's 59 bytes; a relocated image moves only the two absolute operands.
static void tb_0493_cm_check_fp() {
  unsigned char text[96];
  memset(text, 0xCC, sizeof text);
  memcpy(text + 4, kCmCheckFp, CM_FP_LEN);
  size_t at = 99;
  CHECK(offs_find(text, sizeof text, kCmCheckFp, kCmCheckMask, CM_FP_LEN, &at) == 1 && at == 4);
  text[4 + 13] = 0x28; text[4 + 25] = 0x28;   // base 0x10000000 -> 0x10100000
  CHECK_MSG(offs_find(text, sizeof text, kCmCheckFp, kCmCheckMask, CM_FP_LEN, &at) == 1, "relocated");
  text[4 + 54] = 0x69;   // a test of +0x69 is another function
  CHECK(offs_find(text, sizeof text, kCmCheckFp, kCmCheckMask, CM_FP_LEN, &at) == 0);
  CHECK(CM_PROLOGUE == 5 && kCmCheckFp[4] == 0x57 && kCmCheckFp[5] == 0x89);
}

// Progress folds: the fold mask's bits (one per section, an episode keyed by its index), the ini key, and the
// numbers a folded heading carries.
static void tb_0494_pf_fold_state() {
  config_defaults();
  CHECK_MSG(g_cfg.progFolds == 0, "every section open by default");
  CHECK(config_key_known("progress", "folds"));
  int m = 0;
  for (int s = 0; s < PG_FOLD_COUNT; ++s) CHECK(!pg_folded(m, s));
  m = pg_fold_toggle(m, PG_FOLD_AWARDS);
  CHECK(pg_folded(m, PG_FOLD_AWARDS) && !pg_folded(m, PG_FOLD_LEVEL) && !pg_folded(m, PG_FOLD_RECORDS));
  m = pg_fold_toggle(m, PG_FOLD_RECORDS);
  CHECK(pg_folded(m, PG_FOLD_RECORDS));
  m = pg_fold_toggle(m, PG_FOLD_AWARDS);
  CHECK(!pg_folded(m, PG_FOLD_AWARDS) && pg_folded(m, PG_FOLD_RECORDS));
  CHECK_MSG(pg_fold_toggle(m, -1) == m && pg_fold_toggle(m, PG_FOLD_COUNT) == m, "a section out of range changes nothing");
  CHECK_MSG(!pg_folded(0x7FFFFFF8, PG_FOLD_LEVEL) && !pg_folded(-1, PG_FOLD_COUNT),
            "bits a previous build kept per episode fold nothing now");
  CHECK(strcmp(PG_HEADING_RECORDS, "EPISODE RECORDS") == 0);
}

static void tb_0495_pf_summaries() {
  char b[64];
  const PgCounts f = { 30, 7, 140 }, t = { 30, 7, 141 };
  pg_summary_episode(b, sizeof b, f, t);
  CHECK(strcmp(b, "N 30/30  S 7/7  K 140/141") == 0);
  const PgCounts f2 = { 3, 1, 150 };   // kills above the def's total read over the total, as the row does
  pg_summary_episode(b, sizeof b, f2, t);
  CHECK(strcmp(b, "N 3/30  S 1/7  K 150/141") == 0);
  const PgCounts gf = { 239, 55, 1133 }, gt = { 240, 55, 1136 };
  pg_summary_records(b, sizeof b, "Hard", &gf, &gt);
  CHECK_MSG(strcmp(b, "Hard  N 239/240  S 55/55  K 1133/1136") == 0, "the difficulty shown and the whole game's sums");
  pg_summary_records(b, sizeof b, "Normal", 0, 0);
  CHECK_MSG(strcmp(b, "Normal") == 0, "no records read: the difficulty alone");
  AwView v;
  memset(&v, 0, sizeof v);
  CHECK(aw_earned_count(v) == 0);
  v.cell[AW_FINISHED][0].state = AW_EARNED;
  v.cell[AW_BONUS][LS_HARD].state = AW_EARNED;
  v.cell[AW_KILLS][1].state = AW_MISSING;
  CHECK(aw_earned_count(v) == 2);
  aw_summary(b, sizeof b, v);
  CHECK(strcmp(b, "2 of 15 earned") == 0);
  ar_summary_total(b, sizeof b, AR_TALLY_OK, 3, 7, 1, 2, 4);
  CHECK(strcmp(b, "S 3/7  N 2/4") == 0);
  ar_summary_total(b, sizeof b, AR_TALLY_MISMATCH, 3, 7, 0, 2, 4);
  CHECK(strcmp(b, "S ?/7  N ?/4") == 0);
}

// Photo folder (photofolder.h, user 2026-10-03): the row's shortened path and the default rule.
static int pf_chars(const char* s, void*) { return (int)strlen(s); }
static void tb_0499_photo_folder_shorten() {
  const std::string ps = tb_path("G:|Image|Game Screenshots|Nuke Panel");   // 36 characters
  const char* p = ps.c_str();
  char b[64];
  photo_folder_shorten(p, 40, pf_chars, 0, b, sizeof b);
  CHECK(strcmp(b, p) == 0);
  photo_folder_shorten(p, 31, pf_chars, 0, b, sizeof b);
  CHECK(strcmp(b, "...\\Game Screenshots\\Nuke Panel") == 0);
  photo_folder_shorten(p, 30, pf_chars, 0, b, sizeof b);
  CHECK(strcmp(b, "...\\Nuke Panel") == 0);
  photo_folder_shorten(p, 10, pf_chars, 0, b, sizeof b);   // the last component alone is too wide: its tail
  CHECK(strcmp(b, "...e Panel") == 0);
  photo_folder_shorten(p, 2, pf_chars, 0, b, sizeof b);
  CHECK(b[0] == 0);
  photo_folder_shorten(p, 40, pf_chars, 0, b, 8);           // never past the buffer
  CHECK(strlen(b) < 8);
  photo_folder_display(tb_path("C:|Users|Zo\xC3\xAB|Pics|").c_str(), b, sizeof b);
  CHECK(strcmp(b, tb_path("C:|Users|Zo?|Pics").c_str()) == 0);
  photo_folder_display(tb_path("G:|").c_str(), b, sizeof b);
  CHECK(strcmp(b, "G:") == 0);
}
static void tb_0500_photo_folder_default() {
  CHECK(photo_folder_is_default("") && photo_folder_is_default(NULL));
  CHECK(!photo_folder_is_default(tb_path("G:|Image").c_str()));
  strcpy_s(g_cfg.photoFolder, sizeof g_cfg.photoFolder, "x");
  config_defaults();
  CHECK(g_cfg.photoFolder[0] == 0);
  CHECK(config_key_known("freecam", "photofolder"));
  char b[8] = "keep";
  CHECK(ini_parse_str(tb_path("\"G:|A\"").c_str(), b, sizeof b) && strcmp(b, tb_path("G:|A").c_str()) == 0);
  CHECK(!ini_parse_str(tb_path("G:|Longer").c_str(), b, sizeof b) && strcmp(b, tb_path("G:|A").c_str()) == 0);   // refused, never cut
  CHECK(ini_parse_str("", b, sizeof b) && b[0] == 0);
}
// [freecam] photofolder= round-trips through the loader and config_write_changed, as the int keys do.
static void tb_0501_config_string_roundtrip() {
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_photofolder");
  write_text(path, tb_path("[freecam]\r\nphotofolder=G:|Image|Game Screenshots|Nuke Panel\r\nphoto=0x7A\r\n").c_str());
  config_load_from(path);
  CHECK(strcmp(g_cfg.photoFolder, tb_path("G:|Image|Game Screenshots|Nuke Panel").c_str()) == 0 && g_cfg.photoKey == 0x7A);
  strcpy_s(g_cfg.photoFolder, sizeof g_cfg.photoFolder, tb_path("D:|Zo\xC3\xAB Shots").c_str());   // UTF-8 bytes
  g_cfgDirty = 1;
  config_write_changed();
  char text[1024] = { 0 };
  FILE* f = NULL;
  if (fopen_s(&f, path, "rb") == 0 && f) { fread(text, 1, sizeof text - 1, f); fclose(f); }
  CHECK(strstr(text, tb_path("photofolder=D:|Zo\xC3\xAB Shots").c_str()) != NULL);
  CHECK(strstr(text, "photo=0x7A") != NULL);   // unchanged: not rewritten
  config_load_from(path);
  CHECK(strcmp(g_cfg.photoFolder, tb_path("D:|Zo\xC3\xAB Shots").c_str()) == 0);
  g_cfg.photoFolder[0] = 0;   // back to the default
  g_cfgDirty = 1;
  config_write_changed();
  config_load_from(path);
  CHECK(g_cfg.photoFolder[0] == 0);
  char longv[400];
  memset(longv, 'a', sizeof longv);
  memcpy(longv, "[freecam]\r\nphotofolder=", 23);
  memcpy(longv + sizeof longv - 3, "\r\n", 3);   // a 374-byte value: refused, the default
  write_text(path, longv);
  config_load_from(path);
  CHECK(g_cfg.photoFolder[0] == 0);
  DeleteFileA(path);
}

// The X-3000's draw table: the pass each actor was last drawn in, keyed by its pointer (aim.md section 12).
static void tb_0502_x3000_drawn_table() {
  static X3kDrawn t;
  x3000_drawn_clear(&t);
  unsigned p = 99;
  static int objs_[2]; int& a = objs_[0]; int& b = objs_[1];
  CHECK_MSG(!x3000_drawn_get(&t, &a, &p), "empty: nothing recorded");
  CHECK_MSG(x3000_drawn_put(&t, &a, 7) && x3000_drawn_get(&t, &a, &p) && p == 7, "a draw is recorded with its pass");
  CHECK_MSG(!x3000_drawn_get(&t, &b, &p), "another actor stays unrecorded");
  CHECK_MSG(x3000_drawn_put(&t, &a, 8) && x3000_drawn_get(&t, &a, &p) && p == 8, "a later draw replaces the pass");
  // Keys one table span apart share a chain (never dereferenced): the pass's own entries fill it, older are reused.
  x3000_drawn_clear(&t);
  const void* k[X3K_DRAWN_PROBE + 1];
  for (int i = 0; i <= X3K_DRAWN_PROBE; ++i) k[i] = (const void*)(size_t)(0x100000u + (unsigned)i * 16u * X3K_DRAWN_SLOTS);
  int put = 0;
  for (int i = 0; i < X3K_DRAWN_PROBE; ++i) put += x3000_drawn_put(&t, k[i], 8);
  CHECK_MSG(put == X3K_DRAWN_PROBE && x3000_drawn_get(&t, k[X3K_DRAWN_PROBE - 1], &p) && p == 8, "a full chain stored");
  CHECK_MSG(!x3000_drawn_put(&t, k[X3K_DRAWN_PROBE], 8) && t.full == 1, "one more in the same pass: counted, dropped");
  CHECK_MSG(x3000_drawn_put(&t, k[X3K_DRAWN_PROBE], 9) && x3000_drawn_get(&t, k[X3K_DRAWN_PROBE], &p) && p == 9,
            "the next pass reuses a stale slot");
  CHECK_MSG(!x3000_drawn_get(&t, k[0], &p), "the stale entry it replaced is gone");
  x3000_drawn_clear(&t);
  CHECK_MSG(!x3000_drawn_get(&t, &a, &p) && t.full == 0, "a new level clears the table");
  // FUN_10090260's fingerprint: the stock head or our own JMP, then the 129 read bytes.
  unsigned char code[X3K_DRAW_FP_LEN];
  memcpy(code, kX3kDrawFp, sizeof code);
  CHECK_MSG(x3000_fp_actor_draw(code), "the stock bytes match");
  code[0] = 0xE9; code[1] = 0x11; code[2] = 0x22; code[3] = 0x33; code[4] = 0x44; code[5] = 0x90;
  CHECK_MSG(x3000_fp_actor_draw(code), "a detour's JMP at the head is tolerated");
  code[0] = 0x55;
  CHECK_MSG(!x3000_fp_actor_draw(code), "any other head refuses");
  memcpy(code, kX3kDrawFp, sizeof code);
  code[0x73] ^= 1;   // the +0xAF0 offset
  CHECK_MSG(!x3000_fp_actor_draw(code) && !x3000_fp_actor_draw(0), "a changed body byte or no code refuses");
}

// The X-3000's view gate: drawn in the last world pass (no freecam frame), and its box centre on screen.
static void tb_0503_x3000_view_gate() {
  CHECK_MSG(x3000_fresh(1, 12, 12, 0, 0), "drawn in the pass just completed");
  CHECK_MSG(!x3000_fresh(1, 11, 12, 0, 0), "drawn an earlier pass: stale");
  CHECK_MSG(!x3000_fresh(0, 12, 12, 0, 0), "never drawn: stale");
  CHECK_MSG(!x3000_fresh(1, 12, 12, 1, 0), "a freecam pass is no view of Duke's");
  CHECK_MSG(!x3000_fresh(1, 12, 12, 0, 1), "nor the first frame after the freecam");
  ScreenPt sp = { 320.f, 240.f, 1 };
  CHECK_MSG(x3000_on_screen(sp, 640, 480), "the centre is on screen");
  sp.x = 0.f; sp.y = 480.f;
  CHECK_MSG(x3000_on_screen(sp, 640, 480), "the edges count");
  sp.x = -0.5f;
  CHECK_MSG(!x3000_on_screen(sp, 640, 480), "left of the screen");
  sp.x = 641.f; sp.y = 10.f;
  CHECK_MSG(!x3000_on_screen(sp, 640, 480), "right of the screen");
  sp.x = 10.f; sp.y = 481.f;
  CHECK_MSG(!x3000_on_screen(sp, 640, 480), "below the screen");
  sp.y = 10.f; sp.ok = 0;
  CHECK_MSG(!x3000_on_screen(sp, 640, 480), "behind the near plane");
  CHECK_MSG(x3000_view_gate(1, 1) == X3K_SEEN && x3000_view_gate(0, 1) == X3K_STALE &&
            x3000_view_gate(0, 0) == X3K_STALE && x3000_view_gate(1, 0) == X3K_OFFSCREEN, "stale first, then off screen");
}

// The scan that finds FUN_10090260 in duke_base's code: one match, where it lies; two are counted.
static void tb_0504_x3000_find_actor_draw() {
  static unsigned char code[3 * X3K_DRAW_FP_LEN];
  memset(code, 0xCC, sizeof code);
  size_t at = 0;
  CHECK_MSG(x3000_find_actor_draw(code, sizeof code, &at) == 0, "no copy: no match");
  memcpy(code + 10, kX3kDrawFp, X3K_DRAW_FP_LEN);
  CHECK_MSG(x3000_find_actor_draw(code, sizeof code, &at) == 1 && at == 10, "one copy: found where it lies");
  code[10] = 0xE9;
  CHECK_MSG(x3000_find_actor_draw(code, sizeof code, &at) == 1 && at == 10, "found under a detour's JMP");
  memcpy(code + 2 * X3K_DRAW_FP_LEN, kX3kDrawFp, X3K_DRAW_FP_LEN);
  CHECK_MSG(x3000_find_actor_draw(code, sizeof code, &at) == 2, "two copies: both counted (the caller refuses)");
  CHECK_MSG(x3000_find_actor_draw(code, X3K_DRAW_FP_LEN - 1, &at) == 0, "a range shorter than the bytes: none");
}

static void tb_0506_sio_acc() {
  SioAcc a = {};
  sio_acc_add(&a, 30); sio_acc_add(&a, 70); sio_acc_add(&a, 20);
  CHECK(a.calls == 3 && a.ticks == 120 && a.max == 70);
}
static void tb_0507_sio_table() {
  CHECK(strcmp(sio_basename(tb_path("C:|g|duke~base~state~e04p01_a01.msv").c_str()), "e04p01_a01.msv") == 0);
  CHECK(strcmp(sio_basename("plain.sav"), "plain.sav") == 0);
  SioTable t = {};
  int x[SIO_FILES + 1];
  SioFile* f = sio_open(&t, &x[0], "state/a.msv", 100, 5);
  CHECK_MSG(f && f->t_open == 100 && f->create == 5 && strcmp(f->name, "a.msv") == 0, "an open is tracked");
  CHECK(sio_find(&t, &x[0]) == f && sio_find(&t, &x[1]) == 0);
  CHECK_MSG(sio_open(&t, &x[0], "b.msv", 200, 1) == f && t.stale == 1 && f->t_open == 200 && f->write.calls == 0,
            "a handle reopened unseen replaces its stale entry");
  sio_drop(f);
  CHECK(sio_find(&t, &x[0]) == 0);
  for (int i = 0; i < SIO_FILES; ++i) CHECK(sio_open(&t, &x[i], "f", 0, 0) != 0);
  CHECK_MSG(sio_open(&t, &x[SIO_FILES], "f", 0, 0) == 0 && t.overflow == 1, "a full table counts the open");
  CHECK_MSG(sio_open(&t, 0, "f", 0, 0) == 0, "0 is never tracked");
  char longName[100]; memset(longName, 'n', 99); longName[99] = 0;
  sio_drop(sio_find(&t, &x[0]));
  f = sio_open(&t, &x[0], longName, 0, 0);
  CHECK_MSG(f && strlen(f->name) == SIO_NAME_CAP - 1, "a long name is cut, terminated");
}
static void tb_0508_sio_format() {
  SioTable t = {};
  int h;
  SioFile* f = sio_open(&t, &h, "duke/base/state/e04p01_a01.msv", 1000, 2000);   // 1 tick = 1 us below
  sio_acc_add(&f->write, 100000); sio_acc_add(&f->write, 300000);
  f->bytes = 301234;
  sio_acc_add(&f->seek, 1000);
  sio_acc_add(&f->real, 250000);   // nested in the write/seek/flush/close times above: not subtracted again
  char b[256];
  const int n = sio_format(b, sizeof b, f, 4000, 601000, 1000.0);
  CHECK(n > 0 && n < (int)sizeof b);
  CHECK_MSG(strcmp(b, "saveio: e04p01_a01.msv total=600.0 ms create=2.0 write=2 calls 400.0 ms (max 300.0) "
                      "real=1 calls 250.0 ms bytes=301234 close=4.0 ms seek=1 calls 1.0 ms flush=0 calls 0.0 ms "
                      "engine=193.0 ms") == 0, b);
  CHECK_MSG(sio_format(b, 16, f, 4000, 601000, 1000.0) == 15 && b[15] == 0, "a short buffer is cut, terminated");
  f->flushFails = 2; f->flushErr = 112;
  sio_format(b, sizeof b, f, 4000, 601000, 1000.0);
  CHECK_MSG(strstr(b, "engine=193.0 ms flushfail=2 err=112") != 0, b);
}

static void tb_0509_sio_bufferable() {
  const unsigned long W = SIO_GENERIC_WRITE, R = SIO_GENERIC_READ;
  CHECK(sio_bufferable("duke/base/state/e04p01_a01.msv", W));
  CHECK_MSG(sio_bufferable("SAVE\\slot1.SaV", W) && sio_bufferable("x.MSV", W), "any case");
  CHECK_MSG(!sio_bufferable("state/e04p01_a01.msv", W | R), "a read handle would not see the buffered bytes");
  CHECK(!sio_bufferable("a.msv", W | SIO_GENERIC_ALL) && !sio_bufferable("a.msv", W | SIO_FILE_READ_DATA));
  CHECK_MSG(!sio_bufferable("a.msv", R), "a read-only open");
  CHECK_MSG(!sio_bufferable("prism3d.log", W) && !sio_bufferable("a.msvx", W) && !sio_bufferable("msv", W),
            "any other file passes through");
  CHECK(!sio_bufferable(".ms", W) && !sio_bufferable("", W) && !sio_bufferable(0, W));
  config_defaults();
  CHECK_MSG(g_cfg.fastSaves == 1 && config_key_known("saves", "fast"), "Fast saves defaults on (user, 2026-10-03)");
}

// A file as WriteFile and SetFilePointer see it: bytes at a position. take caps one real write (0: all of it);
// failAt fails that real call and every later one (0: never); stall succeeds while taking nothing.
struct SioSim { unsigned char bytes[256]; unsigned pos, size, calls, take, failAt; int stall; };
static int sio_sim_write(void* ctx, const unsigned char* p, unsigned n, unsigned* wrote) {
  SioSim* s = (SioSim*)ctx;
  s->calls++;
  *wrote = 0;
  if (s->stall) return 1;
  if (s->failAt && s->calls >= s->failAt) return 0;
  const unsigned k = s->take && s->take < n ? s->take : n;
  if (s->pos + k > sizeof s->bytes) return 0;
  memcpy(s->bytes + s->pos, p, k);
  s->pos += k;
  if (s->pos > s->size) s->size = s->pos;
  *wrote = k;
  return 1;
}
// The hook's order of events on one handle: 'w' writes n pattern bytes, 's' seeks to n (~0u: the end), 'f' and 'c'
// flush and close. Buffered (cap > 0), the buffer is flushed before every seek, flush and close, as saveio.cpp does.
struct SioOp { char kind; unsigned n; };
static void sio_sim_run(SioSim* s, const SioOp* ops, int count, unsigned cap) {
  unsigned char store[64];
  SioBuf b = { store, 0, cap };
  unsigned char next = 1;
  for (int i = 0; i < count; ++i) {
    if (ops[i].kind == 'w') {
      unsigned char src[64];
      for (unsigned k = 0; k < ops[i].n; ++k) src[k] = next++;
      const int r = cap ? sio_buf_write(&b, src, ops[i].n, sio_sim_write, s) : SIO_BUF_DIRECT;
      if (r == SIO_BUF_DIRECT) CHECK(sio_write_all(sio_sim_write, s, src, ops[i].n));
      else CHECK(r == SIO_BUF_TAKEN);
    } else {
      if (cap) CHECK(sio_buf_flush(&b, sio_sim_write, s));
      if (ops[i].kind == 's') s->pos = ops[i].n == ~0u ? s->size : ops[i].n;
    }
  }
}
static void tb_0510_sio_buf_same_bytes() {
  // A save's shape: many 1-8 byte fields, a seek back to patch a header, a seek to the end, a block larger than the
  // buffer, a flush, and the close.
  static const SioOp ops[] = {
    {'w', 4}, {'w', 1}, {'w', 2}, {'w', 8}, {'w', 4}, {'w', 1}, {'w', 1}, {'w', 8}, {'w', 2}, {'w', 4},
    {'s', 3}, {'w', 4}, {'s', ~0u}, {'w', 40}, {'w', 1}, {'w', 2}, {'f', 0}, {'w', 3}, {'w', 8}, {'w', 8},
    {'s', 0}, {'w', 1}, {'c', 0},
  };
  const int count = (int)(sizeof ops / sizeof ops[0]);
  SioSim direct = {}, fast = {}, whole = {};
  sio_sim_run(&direct, ops, count, 0);
  fast.take = 5;   // short real writes: the flush loops until every byte is out
  sio_sim_run(&fast, ops, count, 16);
  sio_sim_run(&whole, ops, count, 16);
  CHECK_MSG(fast.size == direct.size && whole.size == direct.size && direct.size == 97, "the same file length");
  CHECK_MSG(memcmp(fast.bytes, direct.bytes, direct.size) == 0 && memcmp(whole.bytes, direct.bytes, direct.size) == 0,
            "the same bytes, in the same places");
  CHECK_MSG(direct.calls == 18 && whole.calls == 9, "9 real writes for the engine's 18");
}
static void tb_0511_sio_buf_edges() {
  SioSim s = {};
  unsigned char store[8], src[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  SioBuf b = { store, 0, sizeof store };
  CHECK_MSG(sio_buf_flush(&b, sio_sim_write, &s) == 1 && s.calls == 0, "an empty flush writes nothing");
  CHECK(sio_buf_write(&b, src, 0, sio_sim_write, &s) == SIO_BUF_TAKEN && b.len == 0 && s.calls == 0);
  CHECK(sio_buf_write(&b, src, 8, sio_sim_write, &s) == SIO_BUF_TAKEN && b.len == 8 && s.calls == 0);
  CHECK_MSG(sio_buf_write(&b, src, 1, sio_sim_write, &s) == SIO_BUF_TAKEN && b.len == 1 && s.calls == 1,
            "full: flushed, then taken");
  CHECK_MSG(sio_buf_write(&b, src, 9, sio_sim_write, &s) == SIO_BUF_DIRECT && b.len == 0 && s.size == 9,
            "larger than the buffer: flushed first, the caller writes it");
  s.take = 3;
  CHECK_MSG(sio_write_all(sio_sim_write, &s, src, 10) == 1 && s.size == 19 && s.calls == 6, "short writes loop");
  s.take = 0; s.failAt = s.calls + 1;
  CHECK(sio_buf_write(&b, src, 4, sio_sim_write, &s) == SIO_BUF_TAKEN);
  CHECK_MSG(sio_buf_write(&b, src, 6, sio_sim_write, &s) == SIO_BUF_FAILED && b.len == 0,
            "a failed flush: reported, the buffer emptied");
  CHECK(sio_buf_write(&b, src, 3, sio_sim_write, &s) == SIO_BUF_TAKEN);
  CHECK(sio_buf_flush(&b, sio_sim_write, &s) == 0 && b.len == 0);
  SioSim stalled = {};
  stalled.stall = 1;
  CHECK_MSG(sio_write_all(sio_sim_write, &stalled, src, 1) == 0 && stalled.calls == 1, "a write that takes nothing stops");
}

// speedrule.h: the scaled clock game callers read (docs/re/game-speed.md section 2).
static void tb_0512_gs_clock_continuity() {
  GsClock c = gs_clock_start();
  CHECK_MSG(gs_scaled(c, 123456u) == 123456u && c.pct == 100, "before any change: the real clock, unchanged");
  const unsigned before = gs_scaled(c, 5000u);
  c = gs_rebase(c, 5000u, 50);
  CHECK_MSG(gs_scaled(c, 5000u) == before && c.pct == 50, "a speed change keeps the clock where it was");
  CHECK_MSG(gs_scaled(c, 7000u) == before + 1000u, "2 s at 50% advance 1 s");
  CHECK_MSG(gs_scaled(c, 5001u) == before && gs_scaled(c, 5002u) == before + 1u, "fractions are kept, not lost");
  const unsigned mid = gs_scaled(c, 9000u);
  c = gs_rebase(c, 9000u, 400);
  CHECK(gs_scaled(c, 9000u) == mid && gs_scaled(c, 9250u) == mid + 1000u);
  c = gs_rebase(c, 9250u, 100);
  CHECK_MSG(gs_scaled(c, 10250u) == mid + 2000u, "off again: real speed, the offset kept, no jump back");
  CHECK(gs_pct_snap(GS_PCT_DEF) == 50 && GS_PCT_MIN == 10 && GS_PCT_MAX == 400 && GS_PCT_STEP == 5);
}
static void tb_0513_gs_clock_monotonic_and_wrap() {
  GsClock c = gs_rebase(gs_clock_start(), 0xFFFFFF00u, 100);
  c = gs_rebase(c, 0xFFFFFF00u, 50);
  CHECK_MSG(gs_scaled(c, 0x00000100u) == 0xFFFFFF00u + 0x100u, "512 ms across the real clock's wrap advance 256");
  unsigned real = 0xFFFFFFC0u, last = gs_scaled(c, real);
  static const int kPct[8] = { 10, 400, 55, 100, 35, 250, 10, 395 };
  bool mono = true;
  for (int i = 0; i < 64; ++i) {
    real += 7u;
    if (i % 8 == 3) c = gs_rebase(c, real, kPct[i / 8]);
    const unsigned now = gs_scaled(c, real);
    if (now - last > 0x7FFFFFFFu) mono = false;   // a step back, read as the engine's unsigned delta would
    last = now;
  }
  CHECK_MSG(mono, "never backwards across speed changes and the wrap");
  GsClock longRun = gs_rebase(gs_clock_start(), 1000u, 400);
  CHECK_MSG(!gs_needs_rebase(longRun, 1000u + 0x3FFFFFFFu) && gs_needs_rebase(longRun, 1000u + 0x40000000u),
            "the base moves on long before the elapsed time could wrap");
}
static void tb_0514_gs_caller_range() {
  const GsRange r = { 0x400000u, 0x4A6000u };
  CHECK(gs_in_range(r, 0x400000u) && gs_in_range(r, 0x402C76u) && gs_in_range(r, 0x4A5FFFu));
  CHECK(!gs_in_range(r, 0x4A6000u) && !gs_in_range(r, 0x3FFFFFu) && !gs_in_range(r, 0x10001000u));
  const GsRange none = { 0u, 0u };
  CHECK_MSG(!gs_in_range(none, 0u) && !gs_in_range(none, 0x402C76u), "an unset range claims no caller");
}
static void tb_0515_gs_settings() {
  CHECK(gs_pct_snap(50) == 50 && gs_pct_snap(52) == 50 && gs_pct_snap(53) == 55 && gs_pct_snap(398) == 400);
  CHECK(gs_pct_snap(3) == 10 && gs_pct_snap(-40) == 10 && gs_pct_snap(999) == 400);
  char b[16];
  gs_tag_text(50, b, sizeof b);
  CHECK(strcmp(b, "Speed 50%") == 0);
  gs_tag_text(400, b, 8);
  CHECK_MSG(strlen(b) == 7, "a short buffer is cut, terminated");
  config_defaults();
  CHECK(g_cfg.gameSpeedPct == 50 && g_cfg.gameSpeedKey == 0 && config_bind_get(BIND_ROW_GAMESPEED) == 0);
  CHECK(config_key_known("game", "speed") && config_key_known("game", "speedkey"));
  CHECK_MSG(!config_key_known("game", "on"), "the switch is not saved: off at every launch");
}
static void tb_0516_att_world_pos() {
  // A pickup in train_car_2 (e04p03_a02 world.scr): local (160,56,-96), the car's world matrix carrying it.
  float m[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  9616, 216, -112, 1 };
  const Vec3 local = { 160.f, 56.f, -96.f };
  int car;
  Vec3 o;
  CHECK_MSG(att_world_pos(0, 5u, m, local, &o) == ATT_ROOT && o.x == 160.f && o.z == -96.f,
            "no parent: +0x18C is the world position");
  CHECK_MSG(att_world_pos(&car, 0u, m, local, &o) == ATT_UNSET && o.x == 160.f,
            "a matrix never computed (stamp 0 since the ctor) is not read");
  CHECK_MSG(att_world_pos(&car, 3u, m, local, &o) == ATT_MATRIX && o.x == 9616.f && o.y == 216.f && o.z == -112.f,
            "attached: the world matrix's translation row");
  m[15] = 2.f;
  CHECK_MSG(att_world_pos(&car, 3u, m, local, &o) == ATT_MATRIX && o.x == 4808.f && o.y == 108.f && o.z == -56.f,
            "w != 1 divides, as mult_zero_matrix does");
  m[15] = 0.f;
  CHECK_MSG(att_world_pos(&car, 3u, m, local, &o) == ATT_MATRIX && o.x == 9616.f, "w == 0 does not divide");
  m[15] = 1.f;
  volatile float zero = 0.f;
  m[13] = zero / zero;
  CHECK_MSG(att_world_pos(&car, 3u, m, local, &o) == ATT_BAD && o.x == 160.f && o.y == 56.f,
            "a non-finite translation keeps +0x18C");
  CHECK(strcmp(att_name(ATT_ROOT), "root") == 0 && strcmp(att_name(ATT_MATRIX), "matrix") == 0);
  CHECK(strcmp(att_name(ATT_UNSET), "unset") == 0 && strcmp(att_name(ATT_BAD), "bad") == 0);
}

// --- Ambushes (areas.h; re/kills-and-spawners.md sections 2-4) ---
// world.scr as e05p02_a02 (CRLF, spawners before their switch) plus a timer, a laser, a breakable whose spawner sits in
// a mover, a projectile and an unwired monster spawner, placed monsters (one nested, a boss), two eggs (one wired).
static const char kArAmb[] =
  "spawn map {\r\n  path:maps/e09p09_a01\r\n  name:e09p09_a01\r\n"
  "  spawn spawner_entity {\r\n    monst_spawnscr:\"scripts/pwrup/sbullet.sc\"\r\n    spawnscript:\"scripts/monst/pigoid1.sc\"\r\n"
  "    position:< 3568 156 -4368 >\r\n    name:\"spawner_ambush02\"\r\n  }\r\n"
  "  spawn spawner_entity {\r\n    monst_spawnscr:\"scripts/pwrup/sbullet.sc\"\r\n    spawnscript:\"scripts/monst/pigoid1.sc\"\r\n"
  "    position:< 3568 156 -4140 >\r\n    name:\"spawner_ambush01\"\r\n  }\r\n"
  "  spawn area_switch {\r\n    switch_type:6\r\n    path:\"maps/e09p09_a01/area_ambush\"\r\n    position:< 3568 156 -4352 >\r\n"
  "    name:\"area_ambush\"\r\n    connect signal:light1.active\r\n    connect signal:spawner_ambush02.active\r\n"
  "    connect signal:spawner_ambush01.active\r\n  }\r\n"
  "  spawn area_switch {\r\n    switch_type:6\r\n    position:< 10 20 30 >\r\n    name:\"area_t\"\r\n"
  "    connect signal:timer1.active\r\n    connect signal:roach_pod1.hatch\r\n  }\r\n"
  "  spawn timer {\r\n    name:\"timer1\"\r\n    connect signal:spawner_t.active\r\n  }\r\n"
  "  spawn spawner_entity {\r\n    spawnscript:\"scripts/monst/ratoid2.sc\"\r\n    name:\"spawner_t\"\r\n  }\r\n"
  "  spawn laser {\r\n    name:\"laser9\"\r\n    connect obstructed:spawner_l.active\r\n  }\r\n"
  "  spawn spawner_entity {\r\n    monst_spawnscr:\"scripts/pwrup/sc5.sc\"\r\n    spawnscript:\"scripts/monst/securitymech3.sc\"\r\n"
  "    name:\"spawner_l\"\r\n  }\r\n"
  "  spawn breakable_adv {\r\n    name:\"brk1\"\r\n    connect broken:spawner_b.active\r\n  }\r\n"
  "  spawn mover_adv {\r\n    position:< 100 0 0 >\r\n    name:\"mover1\"\r\n"
  "    spawn spawner_entity {\r\n      spawnscript:\"scripts/monst/gatoroid2.sc\"\r\n      position:< 1 2 3 >\r\n"
  "      name:\"spawner_b\"\r\n    }\r\n"
  "    spawn monst_pigoid2 {\r\n      name:\"monst_pigoid21\"\r\n    }\r\n  }\r\n"
  "  spawn spawner_entity {\r\n    spawnscript:\"scripts/proj/glopp_perm.sc\"\r\n    name:\"spawner_p\"\r\n  }\r\n"
  "  spawn area_switch {\r\n    name:\"area_glopp\"\r\n    connect signal:spawner_p.active\r\n  }\r\n"
  "  spawn spawner_entity {\r\n    spawnscript:\"scripts/monst/pigoid3.sc\"\r\n    name:\"spawner_u\"\r\n  }\r\n"
  "  spawn monst_pigoid2 {\r\n    name:\"monst_pigoid22\"\r\n  }\r\n"
  "  spawn monst_roach {\r\n    name:\"monst_roach1\"\r\n  }\r\n"
  "  spawn boss_queenroach {\r\n    name:\"boss1\"\r\n  }\r\n"
  "  spawn roach_pod {\r\n    name:\"roach_pod1\"\r\n  }\r\n"
  "  spawn roach_pod {\r\n    name:\"roach_pod2\"\r\n  }\r\n"
  "}\r\n";

static void ar_amb_area(ArArea* a) {
  memset(a, 0, sizeof *a);
  strcpy_s(a->map, AR_NAME, "e09p09_a01");
  ar_parse_world(kArAmb, (int)strlen(kArAmb), a);
}

static void tb_0517_ar_ambush_parse() {
  static ArArea a;
  ar_amb_area(&a);
  CHECK_MSG(a.nambush == 4, "the switch's pair, the timer's, the laser's and the breakable's; not the projectile's");
  const ArAmbush& z = a.ambush[0];
  CHECK(strcmp(z.trig, "area_ambush") == 0 && z.kind == AR_TRIG_ZONE && strcmp(z.zone, "area_ambush") == 0);
  CHECK_MSG(z.zonePos[0] == 3568.f && z.zonePos[1] == 156.f && z.zonePos[2] == -4352.f, "the switch's position");
  CHECK_MSG(z.nsp == 2 && strcmp(z.sp[0], "spawner_ambush02") == 0 && strcmp(z.sp[1], "spawner_ambush01") == 0,
            "both spawners on one trigger, wired after them in the file, past the switch's other pin");
  CHECK(z.spPos[1][2] == -4140.f && z.spScripts[0] == 1 && z.monsters == 2);
  CHECK_MSG(z.nmon == 1 && strcmp(z.mon[0], "monst_pigoid1") == 0 && z.monN[0] == 2, "one class, twice");
  CHECK_MSG(z.ndrop == 1 && strcmp(z.drop[0], "Bullets +10") == 0, "a drop both carry, once");
  const ArAmbush& t = a.ambush[1];
  CHECK_MSG(strcmp(t.trig, "timer1") == 0 && t.kind == AR_TRIG_TIMER && strcmp(t.zone, "area_t") == 0 &&
            t.zonePos[0] == 10.f, "a timer's ambush takes the zone of the switch that starts it");
  CHECK(strcmp(t.mon[0], "monst_ratoid2") == 0 && t.ndrop == 0);
  const ArAmbush& l = a.ambush[2];
  CHECK(strcmp(l.trig, "laser9") == 0 && l.kind == AR_TRIG_LASER && !l.zone[0] && l.ndrop == 1 &&
        strcmp(l.drop[0], "Pipebomb +1") == 0);
  const ArAmbush& b = a.ambush[3];
  CHECK(strcmp(b.trig, "brk1") == 0 && b.kind == AR_TRIG_BREAKABLE && !b.zone[0]);
  CHECK_MSG(b.spPos[0][0] == 101.f && b.spPos[0][1] == 2.f && b.spPos[0][2] == 3.f, "a nested spawner adds its mover");
  CHECK_MSG(a.placed == 3, "two Pig Cops (one in a mover) and a Roach; the boss never counts");
  CHECK_MSG(a.neggs == 1 && strcmp(a.eggs[0], "roach_pod1") == 0, "only the egg a .hatch wire reaches");
  CHECK_MSG(a.spawned == 5, "one fire of each wired monster spawner; not the unwired one");
  CHECK(a.killTotal == 9 && a.dropped == 0);
  CHECK_MSG(a.nnukes == 0 && a.nbreaks == 0, "no nuke source or breakable drop from these");
}

static void tb_0518_ar_ambush_helpers() {
  char c[AR_CLS];
  CHECK(ar_monst_script("scripts/monst/pigoid1.sc", c, sizeof c) && strcmp(c, "monst_pigoid1") == 0);
  CHECK(!ar_monst_script("scripts/proj/glopp_perm.sc", c, sizeof c) && !ar_monst_script("scripts/pwrup/nuke.sc", c, sizeof c));
  CHECK(!ar_monst_script(0, c, sizeof c));
  static const char kGator[] = "// gator\r\nspawn monst_gator {\r\n  health:40\r\n}\r\n";
  CHECK_MSG(ar_script_class(kGator, (int)strlen(kGator), c, sizeof c) && strcmp(c, "monst_gator") == 0,
            "gator2.sc spawns monst_gator");
  CHECK(!ar_script_class("// nothing\r\n", 12, c, sizeof c));
  CHECK(strcmp(ar_drop_label("scripts/pwrup/sbullet.sc"), "Bullets +10") == 0);
  CHECK(strcmp(ar_drop_label("scripts/pwrup/nuke.sc"), "Nuke") == 0 && ar_drop_label("scripts/proj/x.sc") == 0);
  CHECK(ar_trig_kind("area_switch") == AR_TRIG_ZONE && ar_trig_kind("timer") == AR_TRIG_TIMER);
  CHECK(ar_trig_kind("laser") == AR_TRIG_LASER && ar_trig_kind("breakable_adv") == AR_TRIG_BREAKABLE);
  CHECK(ar_trig_kind("key") == AR_TRIG_PICKUP && ar_trig_kind("monst_gatoroid1") == AR_TRIG_KILL);
  CHECK(ar_trig_kind("crate") == AR_TRIG_OTHER && ar_trig_kind("actor_explo") == AR_TRIG_OTHER);
}

static void tb_0519_ar_ambush_state() {
  static ArArea a;
  ar_amb_area(&a);
  int f = -1;
  const int none[2] = { 0, 0 }, one[2] = { 1, 0 }, gone[2] = { -1, -1 }, half[2] = { -1, 0 };
  CHECK(ar_ambush_state(&a.ambush[0], none, -1, -1, &f) == AR_AMB_WAITING && f == 0);
  CHECK_MSG(ar_ambush_state(&a.ambush[0], one, -1, -1, &f) == AR_AMB_FIRED && f == 1, "one spawner spawned: fired");
  CHECK_MSG(ar_ambush_state(&a.ambush[0], gone, -1, -1, &f) == AR_AMB_UNKNOWN, "no spawner found");
  CHECK_MSG(ar_ambush_state(&a.ambush[0], half, -1, -1, &f) == AR_AMB_WAITING, "the found one has not fired");
  const int two[1] = { 2 };
  CHECK_MSG(ar_ambush_state(&a.ambush[2], two, 1, -1, &f) == AR_AMB_ARMED && f == 2, "a laser that can fire again");
  CHECK(ar_ambush_state(&a.ambush[2], two, 0, -1, &f) == AR_AMB_IDLE && f == 2);
  CHECK_MSG(ar_ambush_state(&a.ambush[2], none, -1, -1, &f) == AR_AMB_IDLE && f == 0, "an unread laser draws nothing");
}

static void tb_0520_ar_ambush_label() {
  static ArArea a;
  ar_amb_area(&a);
  char b[128];
  ar_ambush_label(&a.ambush[0], 0, b, sizeof b);
  CHECK_MSG(strcmp(b, "Ambush: 2x Pig Cop + Bullets +10") == 0, b);
  ar_ambush_label(&a.ambush[1], 0, b, sizeof b);
  CHECK_MSG(strcmp(b, "Ambush: 1x Rat Oid 2 (timer)") == 0, b);
  ar_ambush_label(&a.ambush[2], 2, b, sizeof b);
  CHECK_MSG(strcmp(b, "Ambush: 1x Security Mech 3 + Pipebomb +1 (laser, fired 2)") == 0, b);
  ar_ambush_label(&a.ambush[3], 0, b, sizeof b);
  CHECK_MSG(strcmp(b, "Ambush: 1x Gator Oid 2 (breakable)") == 0, b);
  ArAmbush m = a.ambush[0];
  m.nmon = 2;
  strcpy_s(m.mon[0], AR_CLS, "monst_ratoid2");
  strcpy_s(m.mon[1], AR_CLS, "monst_ratoid3");
  m.monN[0] = m.monN[1] = 2;
  m.drop[1] = "Pipebombs +10";
  m.ndrop = 2;
  ar_ambush_label(&m, 0, b, sizeof b);
  CHECK_MSG(strcmp(b, "Ambush: 2x Rat Oid 2, 2x Rat Oid 3 + Bullets +10, Pipebombs +10") == 0, b);
  m.kind = AR_TRIG_OTHER;
  strcpy_s(m.trigCls, AR_NAME, "actor_explo");
  m.nmon = 1;
  m.ndrop = 0;
  ar_ambush_label(&m, 0, b, sizeof b);
  CHECK_MSG(strcmp(b, "Ambush: 2x Rat Oid 2 (explosion)") == 0, b);
  m.kind = AR_TRIG_PICKUP;
  ar_ambush_label(&m, 0, b, sizeof b);
  CHECK_MSG(strcmp(b, "Ambush: 2x Rat Oid 2 (pickup)") == 0, b);
}

static void tb_0521_ambush_config_colour() {
  config_defaults();
  CHECK_MSG(g_cfg.espAmbushes == 1, "on with Enemies by default");
  CHECK(config_key_known("esp", "ambushes"));
  CHECK(marker_colour(CAT_AMBUSH, -1) == C_AMBUSH && C_AMBUSH != C_HAZARD && C_AMBUSH != C_RED);
}

// --- Kills per area (areas.h, areamap.h; re/kills-and-spawners.md section 5) --------------------------------------
// kArAmb's area: 9 kills = 3 placed + 5 from its ambushes (the switch's 2, the timer's, the laser's, the breakable's)
// + 1 wired egg. Found = total - alive - the monsters of ambushes not fired yet.
static void tb_0522_ar_kills_live() {
  static ArArea a;
  ar_amb_area(&a);
  CHECK_MSG(ar_once_ambushes(&a) == 3, "the laser's fires again: no pip");
  int st[4] = { AR_AMB_WAITING, AR_AMB_WAITING, AR_AMB_ARMED, AR_AMB_WAITING }, fires[4] = { 0, 0, 0, 0 };
  ArKills k;
  ar_kills_live(&a, 3, st, fires, &k);
  CHECK_MSG(k.known && k.src == AR_SRC_LIVE && k.found == 1 && k.waiting == 3,
            "9 less 3 alive less the 5 monsters not spawned yet; three ambushes wait");
  st[0] = AR_AMB_FIRED; fires[0] = 1; fires[2] = 1;
  ar_kills_live(&a, 5, st, fires, &k);
  CHECK_MSG(k.known && k.found == 2 && k.waiting == 2, "the pair spawned and the laser fired: 9 - 5 - 2");
  ar_kills_live(&a, 20, st, fires, &k);
  CHECK_MSG(k.known && k.found == 0, "never below 0 (a laser's fires past its first add monsters)");
  st[1] = AR_AMB_UNKNOWN;
  ar_kills_live(&a, 3, st, fires, &k);
  CHECK_MSG(!k.known, "a spawner not found: unknown");
  ar_kills_fresh(&a, &k);
  CHECK(k.known && k.src == AR_SRC_FRESH && k.found == 0 && k.waiting == 3);
}

// An unloaded area's file: only an area with no ambush and no wired egg reads (no spawner record holds +0xAE4: a
// fired and a waiting spawner store the same pass-2 bytes, section 5), as total less its monst_* class records.
static void tb_0523_ar_kills_msv() {
  static ArArea a;
  static const char kPlain[] = "spawn map {\n  spawn monst_pigoid2 {\n    name:\"p1\"\n  }\n  spawn monst_ratoid1 {\n"
                               "    name:\"r1\"\n  }\n  spawn monst_ratoid1 {\n    name:\"r2\"\n  }\n}\n";
  memset(&a, 0, sizeof a);
  ar_parse_world(kPlain, (int)strlen(kPlain), &a);
  CHECK(a.killTotal == 3 && a.nambush == 0);
  unsigned char b[256];
  memset(b, 0, sizeof b);
  int at = 12;
  const char* recs[3] = { "monst_pigoid2", "boss_queenroach", "monst_ratoid1" };
  for (int r = 0; r < 3; ++r) {
    at += 12;   // id, parent, +0x74
    const int l = (int)strlen(recs[r]);
    b[at++] = (unsigned char)l;
    memcpy(b + at, recs[r], (size_t)l);
    at += l;
    b[at++] = 14; b[at++] = 0;   // its name key, u16 length: never a class record
    memcpy(b + at, "monst_pigoid22", 14);
    at += 15;
  }
  ArKills k;
  int n = -1;
  ar_kills_from_msv(b, at, &a, &k, &n);
  CHECK_MSG(n == 2 && k.known && k.src == AR_SRC_FILE && k.found == 1, "3 placed, 2 still stored: 1 killed");
  static ArArea amb;
  ar_amb_area(&amb);
  ar_kills_from_msv(b, at, &amb, &k, &n);
  CHECK_MSG(!k.known && k.src == AR_SRC_FILE, "an area with ambushes does not read from its file");
  ArKills file, rec, out;
  memset(&rec, 0, sizeof rec);
  rec.src = AR_SRC_LIVE; rec.known = 1; rec.found = 4; rec.waiting = 1;
  ar_kills_from_msv(b, at, &amb, &file, &n);
  ar_kills_merge(&file, &rec, &out);
  CHECK_MSG(out.known && out.src == AR_SRC_RECORD && out.found == 4 && out.waiting == 1, "our last view of the area");
  ar_kills_fresh(&amb, &file);
  ar_kills_merge(&file, &rec, &out);
  CHECK_MSG(out.src == AR_SRC_RECORD, "a visited area whose file is missing keeps what we saw");
  ar_kills_merge(&file, 0, &out);
  CHECK(out.src == AR_SRC_FRESH && out.known);
  ar_kills_from_msv(b, at, &amb, &file, &n);
  ar_kills_merge(&file, 0, &out);
  CHECK(!out.known && out.src == AR_SRC_FILE);
  ar_kills_from_msv(b, at, &a, &file, &n);
  ar_kills_merge(&file, &rec, &out);
  CHECK_MSG(out.src == AR_SRC_FILE && out.found == 1, "a clean file first");
}

static void tb_0524_ar_kills_text() {
  char b[32];
  ar_kills_text(b, sizeof b, 3, 8, 1);
  CHECK(strcmp(b, "K 3/8") == 0);
  ar_kills_text(b, sizeof b, 0, 8, 0);
  CHECK(strcmp(b, "K ?/8") == 0);
  ar_kills_game_note(84, 85, b, sizeof b);
  CHECK_MSG(strcmp(b, "game 85 (+1)") == 0, b);
  ar_kills_game_note(91, 84, b, sizeof b);
  CHECK_MSG(strcmp(b, "game 84 (-7)") == 0, b);
  ar_kills_game_note(70, 70, b, sizeof b);
  CHECK(b[0] == 0);
  CHECK(strcmp(PG_HEADING_LEVEL, "SECRETS, NUKES AND KILLS") == 0);
}

// A box with an S, an N and an A row (secrets, nukes, ambushes) is one pip row taller.
static void tb_0525_am_ambush_rows() {
  static ArPart q;
  memset(&q, 0, sizeof q);
  q.nareas = 1;
  ar_amb_area(&q.areas[0]);
  CHECK_MSG(am_box_rows(&q) == 1, "ambushes alone: one row");
  q.areas[0].nsecrets = 1;
  q.areas[0].nukeTotal = 2;
  CHECK(am_box_rows(&q) == 3);
  q.areas[0].nambush = 0;
  CHECK(am_box_rows(&q) == 2);
  static ArPart p;
  ar_test_part(&p);
  static AmMap m;
  am_build(&p, -1, 0, kAmLabels, 400, &m, 0, 0, 3);
  CHECK_MSG(m.ok && m.box[0].h == AM_BOX_H + AM_PIP + 4 && m.box[2].y == 85 && m.h == 154, "three rows");
  am_build(&p, -1, 0, kAmLabels, 400, &m, 0, 0, 2);
  CHECK(m.box[0].h == AM_BOX_H && m.h == 128);
}

// An area rebuilt from its .msv zeroes every spawner's +0xAE4, but its trigger keeps its state.
static void tb_0526_ar_ambush_trigger_state() {
  static ArArea a;
  ar_amb_area(&a);
  int f = -1;
  const int none[2] = { 0, 0 }, gone[2] = { -1, -1 };
  CHECK_MSG(ar_ambush_state(&a.ambush[0], none, -1, 1, &f) == AR_AMB_FIRED, "switch on, spawner count 0: fired");
  CHECK_MSG(ar_ambush_state(&a.ambush[0], gone, -1, 1, &f) == AR_AMB_FIRED, "switch on, no spawner found: fired");
  CHECK_MSG(ar_ambush_state(&a.ambush[0], none, -1, 0, &f) == AR_AMB_WAITING, "switch off: waiting");
  CHECK_MSG(ar_ambush_state(&a.ambush[0], gone, -1, 0, &f) == AR_AMB_UNKNOWN, "switch off and no spawner: unknown");
  const int two[1] = { 2 };
  CHECK_MSG(ar_ambush_state(&a.ambush[2], two, 1, 1, &f) == AR_AMB_ARMED, "a laser keeps its own rule");
  CHECK(ar_trig_read(&a.ambush[0]) == AR_TRD_SWITCH && ar_trig_read(&a.ambush[1]) == AR_TRD_SWITCH);
  CHECK(ar_trig_read(&a.ambush[2]) == AR_TRD_NONE && ar_trig_read(&a.ambush[3]) == AR_TRD_HOLDER);
  ArAmbush m = a.ambush[3];
  m.kind = AR_TRIG_KILL;
  CHECK(ar_trig_read(&m) == AR_TRD_MONSTER);
  m.kind = AR_TRIG_PICKUP;
  CHECK(ar_trig_read(&m) == AR_TRD_HOLDER);
  m.kind = AR_TRIG_OTHER;
  strcpy_s(m.trigCls, AR_NAME, "crate");
  CHECK(ar_trig_read(&m) == AR_TRD_HOLDER);
  strcpy_s(m.trigCls, AR_NAME, "actor_explo");
  CHECK(ar_trig_read(&m) == AR_TRD_NONE);
  m.kind = AR_TRIG_TIMER;
  m.zone[0] = 0;
  CHECK_MSG(ar_trig_read(&m) == AR_TRD_NONE, "a timer no switch starts");
}

static void tb_0527_stall_policy() {
  CHECK(stall_decide(299, 0, 0, 0) == STALL_SKIP); CHECK(stall_decide(300, 0, 0, 0) == STALL_SAMPLE);
  CHECK_MSG(stall_decide(799, 1, 0, 1) == STALL_SKIP, "second sample waits for 800 ms");
  CHECK(stall_decide(800, 1, 0, 1) == STALL_SAMPLE);
  CHECK_MSG(stall_decide(2999, 2, 0, 1) == STALL_SKIP, "two samples per stall");
  CHECK_MSG(stall_decide(300, 0, 0, 39) == STALL_SAMPLE, "stall 40 is sampled");
  CHECK_MSG(stall_decide(300, 0, 0, 40) == STALL_SKIP, "no stall 41");
  CHECK_MSG(stall_decide(800, 1, 0, 40) == STALL_SAMPLE, "stall 40 still gets its second sample");
}

static void tb_0530_stall_long_frames() {
  CHECK(stall_decide(3000, 2, 0, 1) == STALL_SAMPLE_LONG);
  CHECK_MSG(stall_decide(3000, 0, 0, 40) == STALL_SAMPLE_LONG, "a long frame is sampled once past the cap");
  CHECK_MSG(stall_decide(9000, 2, 1, 1) == STALL_SKIP, "only once");
  CHECK_MSG(stall_decide(3000, 1, 1, 1) == STALL_SKIP, "nothing after the long sample");
  CHECK_MSG(stall_count_after(5, STALL_SAMPLE, 0) == 6, "the first sample counts the stall");
  CHECK_MSG(stall_count_after(6, STALL_SAMPLE, 1) == 6, "the second does not");
  CHECK_MSG(stall_count_after(6, STALL_SAMPLE_LONG, 2) == 5, "a long frame gives its count back");
  CHECK_MSG(stall_count_after(6, STALL_SAMPLE_LONG, 0) == 6, "an uncounted long frame gives nothing back");
}

// Fake stack: 32 words from 0x1000; unaligned or outside reads fail.
struct StallMem { unsigned w[32]; };
static int stall_mem_read(unsigned addr, unsigned* out, void* ctx) {
  if (addr < 0x1000 || addr >= 0x1000 + 32 * 4 || (addr & 3)) return 0;
  *out = ((StallMem*)ctx)->w[(addr - 0x1000) / 4];
  return 1;
}
static void stall_mem_set(StallMem* m, unsigned addr, unsigned next, unsigned ret) {
  m->w[(addr - 0x1000) / 4] = next; m->w[(addr - 0x1000) / 4 + 1] = ret;
}
static void tb_0528_stall_walk() {
  StallMem m = {};
  stall_mem_set(&m, 0x1010, 0x1020, 0x401000);
  stall_mem_set(&m, 0x1020, 0x1040, 0x402000);
  stall_mem_set(&m, 0x1040, 0, 0x403000);
  unsigned r[8] = {};
  CHECK(stall_walk(0x1010, 0x1000, 0x1080, stall_mem_read, &m, r, 8) == 3);
  CHECK(r[0] == 0x401000 && r[1] == 0x402000 && r[2] == 0x403000);
  CHECK_MSG(stall_walk(0x1010, 0x1000, 0x1080, stall_mem_read, &m, r, 2) == 2, "max caps the walk");
  CHECK_MSG(stall_walk(0x1010, 0x1000, 0x1030, stall_mem_read, &m, r, 8) == 2, "a caller frame past hi ends it");
  CHECK_MSG(stall_walk(0x1012, 0x1000, 0x1080, stall_mem_read, &m, r, 8) == 0, "unaligned ebp");
  CHECK_MSG(stall_walk(0x1010, 0x1014, 0x1080, stall_mem_read, &m, r, 8) == 0, "ebp below lo");
  CHECK_MSG(stall_walk(0x107C, 0x1000, 0x1080, stall_mem_read, &m, r, 8) == 0, "frame without room for two words");
  stall_mem_set(&m, 0x1020, 0x1020, 0x402000);
  CHECK_MSG(stall_walk(0x1010, 0x1000, 0x1080, stall_mem_read, &m, r, 8) == 2, "a frame pointing at itself ends it");
  stall_mem_set(&m, 0x1020, 0x1010, 0x402000);
  CHECK_MSG(stall_walk(0x1010, 0x1000, 0x1080, stall_mem_read, &m, r, 8) == 2, "a frame pointing down ends it");
  stall_mem_set(&m, 0x1020, 0x1040, 0);
  CHECK_MSG(stall_walk(0x1010, 0x1000, 0x1080, stall_mem_read, &m, r, 8) == 1, "a zero return address ends it");
  stall_mem_set(&m, 0x1020, 0x2000, 0x402000);
  CHECK_MSG(stall_walk(0x1010, 0x1000, 0x3000, stall_mem_read, &m, r, 8) == 2, "an unreadable frame ends it");
}

static void tb_0529_stall_fmt_addr() {
  char b[64];
  CHECK(stall_fmt_addr(b, 64, tb_path("G:|Games|DNMP|prism3d.exe").c_str(), 0x425c6e, 0x400000) == 19);
  CHECK(strcmp(b, "prism3d.exe+0x25c6e") == 0);
  stall_fmt_addr(b, 64, tb_path("C:~Windows~SysWOW64~ntdll.dll").c_str(), 0x77001234, 0x77000000);
  CHECK(strcmp(b, "ntdll.dll+0x1234") == 0);
  CHECK(stall_fmt_addr(b, 64, 0, 0x12abcd, 0) == 10 && strcmp(b, "0x0012abcd") == 0);
  CHECK_MSG(stall_fmt_addr(b, 8, 0, 0x12abcd, 0) == 7 && strcmp(b, "0x0012a") == 0, "truncated, terminated");
}

// photofolder.h: only a drive-letter absolute path is a Photo folder. A UNC path would open an SMB connection (and
// send the user's NTLM hash) when a photo is taken; a \\?\ or \\.\ path reaches devices. Both fall back to the default.
static void tb_0531_photo_folder_path_ok() {
  for (const char* p : { "C:|Shots", "d:~shots", "Z:|", "C:|Games|Pictures|Nuke Panel" })
    CHECK_MSG(photo_folder_path_ok(tb_path(p).c_str()), p);
  for (const char* p : { "||host|share", "~~host~share", "||?|C:|Shots", "||.|C:", "||?|UNC|host|s", "||host@80|x",
                         "Shots", "C:Shots", "C:", "1:|x", "C:|a:b", "", " C:|x" })
    CHECK_MSG(!photo_folder_path_ok(tb_path(p).c_str()), p);
  CHECK(!photo_folder_path_ok(0));
}
// photo.h: a photo never overwrites a file; the next free name gets "-n" before ".png".
static void tb_0532_photo_file_name_n() {
  char out[64];
  CHECK(photo_file_name_n("e1l1 2026-10-07 09-05-03 7680x4320.png", 1, out, sizeof out) > 0 &&
        strcmp(out, "e1l1 2026-10-07 09-05-03 7680x4320.png") == 0);
  CHECK(photo_file_name_n("e1l1 2026-10-07 09-05-03 7680x4320.png", 2, out, sizeof out) > 0 &&
        strcmp(out, "e1l1 2026-10-07 09-05-03 7680x4320-2.png") == 0);
  CHECK(photo_file_name_n("a.png", 9, out, sizeof out) == 7 && strcmp(out, "a-9.png") == 0);
  CHECK_MSG(photo_file_name_n("a.png", 2, out, 7) < 0 && out[0] == 0, "never a cut name");
  CHECK_MSG(photo_file_name_n("a.txt", 2, out, sizeof out) < 0 && out[0] == 0, "only a .png name");
  CHECK(PHOTO_NAME_TRIES >= 2 && PHOTO_NAME_TRIES <= 64);
}
// ini.h / config.cpp: a line over INI_LINE_MAX - 1 bytes is skipped whole, never cut into a second line that could
// be read as another key; the DllMain peek agrees. A line of exactly INI_LINE_MAX - 1 bytes is still read.
static void tb_0533_ini_overlong_line() {
  const std::string tail = "borderless=0\r\n";
  const std::string cut = "[display]\r\n; " + std::string(INI_LINE_MAX - 3, 'a') + tail;   // the tail starts at 511
  const std::string fits = "[display]\r\nborderless=0" + std::string(INI_LINE_MAX - 1 - 12, ' ') + "\r\n";
  const std::string over = "[display]\r\nborderless=0" + std::string(INI_LINE_MAX - 12, ' ') + "\r\n";
  const std::string after = "[display]\r\n; " + std::string(700, 'a') + "\r\nborderless=0\r\n";
  const std::string atEof = "[display]\r\n; " + std::string(INI_LINE_MAX - 3, 'a') + "borderless=0";
  const struct { const std::string* text; int want; } kCases[] = { { &cut, 1 }, { &fits, 0 }, { &over, 1 },
                                                                   { &after, 0 }, { &atEof, 1 } };
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_overlong");
  for (const auto& c : kCases) {
    const int peek = ini_peek_int(c.text->c_str(), (int)c.text->size(), "display", "borderless", 0, 1, 1);
    write_text(path, c.text->c_str());
    config_load_from(path);
    if (peek != c.want || g_cfg.borderless != c.want)
      printf("  %u bytes: peek %d, config_load_from %d, want %d\n", (unsigned)c.text->size(), peek, g_cfg.borderless,
             c.want);
    CHECK_MSG(peek == c.want && g_cfg.borderless == c.want, "an overlong line is skipped whole, in both readers");
  }
  DeleteFileA(path);
}
// logfmt.h: a logged path under the profile folder names it %USERPROFILE%, so a log attached to a public issue does
// not carry the Windows user name. Case and separator style do not matter; a longer name sharing the prefix is not it.
static void tb_0534_log_redact_profile() {
  const std::string prof = tb_path("C:|Users|Player");
  char b[96];
  CHECK(logfmt_redact_profile(tb_path("C:|Users|Player|Pictures|Nuke Panel|a.png").c_str(), prof.c_str(), b, sizeof b));
  CHECK(strcmp(b, tb_path("%USERPROFILE%|Pictures|Nuke Panel|a.png").c_str()) == 0);
  CHECK_MSG(logfmt_redact_profile(tb_path("c:~users~PLAYER~x").c_str(), prof.c_str(), b, sizeof b) &&
            strcmp(b, tb_path("%USERPROFILE%~x").c_str()) == 0, "case and slashes");
  CHECK_MSG(logfmt_redact_profile(prof.c_str(), (prof + tb_path("|")).c_str(), b, sizeof b) &&
            strcmp(b, "%USERPROFILE%") == 0, "the folder itself; a trailing separator on the profile");
  CHECK_MSG(!logfmt_redact_profile(tb_path("C:|Users|Player2|x").c_str(), prof.c_str(), b, sizeof b) &&
            strcmp(b, tb_path("C:|Users|Player2|x").c_str()) == 0, "another user's folder is copied unchanged");
  CHECK_MSG(!logfmt_redact_profile(tb_path("G:|Games|x").c_str(), prof.c_str(), b, sizeof b) &&
            strcmp(b, tb_path("G:|Games|x").c_str()) == 0, "outside the profile");
  CHECK_MSG(!logfmt_redact_profile("x", 0, b, sizeof b) && strcmp(b, "x") == 0, "no profile known");
  CHECK_MSG(!logfmt_redact_profile("x", "", b, sizeof b) && strcmp(b, "x") == 0, "an empty profile matches nothing");
  CHECK_MSG(!logfmt_redact_profile(0, prof.c_str(), b, sizeof b) && b[0] == 0, "no path");
  CHECK_MSG(logfmt_redact_profile(tb_path("C:|Users|Player|abcdef").c_str(), prof.c_str(), b, 16) &&
            strlen(b) == 15, "cut to the buffer, terminated");
}

int main() {
  tb_0001_logfmt_short_line_ends_in_newline();
  tb_0002_logfmt_overlong_line_truncates_without_nul();
  tb_0003_logfmt_tiny_cap_still_newline();
  tb_0004_ndc_corners_and_centre();
  tb_0005_framestats_reports_after_one_second();
  tb_0006_w2s_centre_edges_and_behind();
  tb_0007_w2s_view_translation_applies();
  tb_0008_jumpwin_edge_opens_and_expires();
  tb_0009_w2s_rejects_behind_when_near_is_not_positive();
  tb_0010_hostgate_matches_final_path_component_only();
  tb_0011_engine_color_swaps_r_and_b();
  tb_0012_boost_classify_ignores_landing_reset();
  tb_0013_clip_rect_inside_is_unchanged();
  tb_0014_clip_rect_fully_outside_returns_zero();
  tb_0015_clip_rect_half_left_moves_u_halfway();
  tb_0016_clip_rect_all_four_sides();
  tb_0017_clip_rect_zero_area_clip_returns_zero();
  tb_0018_px_ndc_round_trip_3440x1440();
  tb_0019_clip_poly_triangle_half_clipped();
  tb_0020_own_toggle_from_closed_takes_input();
  tb_0021_own_toggle_from_open_gives_input_back();
  tb_0022_own_activate_while_open_reasserts_off();
  tb_0023_own_activate_while_closed_does_nothing();
  tb_0024_own_frame_while_open_reasserts_off();
  tb_0025_own_two_toggles_in_one_frame_end_closed();
  tb_0026_cursor_scale_maps_client_to_render();
  tb_0027_activation_on_only_for_the_engine_on_branch();
  tb_0028_cursor_restore_steps_undo_drift();
  tb_0029_panel_layout();
  tb_0030_panel_box_clamps_1280x720_and_3440x1440();
  tb_0031_ui_panel_max_h_for();
  tb_0032_bind_key_is_live_matches_label();
  tb_0033_bind_hold_is_active_requires_live_key();
  tb_0034_bind_menu_key_effective_matches_live();
  tb_0035_bind_swallows_requires_live_key_and_window();
  tb_0036_anim_core();
  tb_0037_menu_scroll_rules();
  tb_0038_panel_key_effective_is_a_key();
  tb_0039_config_missing_file_gives_defaults();
  tb_0040_config_garbage_value_gives_default();
  tb_0041_config_unknown_key_survives_write();
  tb_0042_zoom_fov_level_one_returns_stock();
  tb_0043_zoom_fov_level_two_at_stock_90();
  tb_0044_zoom_fov_level_four_at_stock_79_755();
  tb_0045_zoom_fov_level_below_one_clamps();
  tb_0046_zoom_fov_stock_outside_open_interval_unchanged();
  tb_0047_zoom_should_write_tolerance();
  tb_0048_zoom_may_recapture_skips_release_and_write_frames();
  tb_0049_fc_next_deadline_cases();
  tb_0050_fc_wait_ticks_zero_when_past();
  tb_0051_fc_pacing_64_frames_at_175hz();
  tb_0052_fc_effective_fps_sentinel_rule();
  tb_0053_fc_cap_clamp_vsync_cases();
  tb_0054_class_lookup_categories_and_tiers();
  tb_0055_esp_box_from_corners();
  tb_0056_esp_project_box_centre_gate();
  tb_0057_esp_markers();
  tb_0058_esp_health_label();
  tb_0059_marker_pipeline();
  tb_0060_esp_distance_range_alive();
  tb_0061_visuals_tab_and_esp_defaults();
  tb_0062_pullback_zero_is_unchanged();
  tb_0063_pullback_identity_d10();
  tb_0064_pullback_rotated_eye_moves_back_along_forward();
  tb_0065_pullback_rotated_stays_inverse();
  tb_0066_pullback_forward_point_depth_grows_by_d();
  tb_0067_zoomout_defaults_and_bind_row();
  tb_0068_class_lookup_pickups_objectives_secrets();
  tb_0069_keycard_colour_name();
  tb_0070_esp_cross_mark_geometry();
  tb_0071_marker_colour();
  tb_0072_items_tab_and_category_esp_defaults();
  tb_0073_edge_arrow_direction();
  tb_0074_edge_arrow_fan_out();
  tb_0075_edge_arrow_panel_and_label();
  tb_0076_secret_volume_faces();
  tb_0077_quit_release_mask();
  tb_0078_hitbox_from();
  tb_0079_item_labels_by_subtype();
  tb_0080_nuke_brackets_and_label();
  tb_0081_freecam_pose_round_trips_the_view();
  tb_0082_freecam_yaw_90_turns_forward_to_plus_x();
  tb_0083_freecam_pitch_clamps_at_89();
  tb_0084_freecam_forward_is_frame_rate_independent();
  tb_0085_freecam_up_is_world_y_at_any_pitch();
  tb_0086_own_panel_over_freecam_and_back();
  tb_0087_freecam_config_and_bind_rows();
  tb_0088_hotkeys_yield_to_the_console_only();
  tb_0089_teleport_floor_placement_height();
  tb_0090_teleport_wall_step_back();
  tb_0091_teleport_steep_normal_refused();
  tb_0092_teleport_null_hit_refused();
  tb_0093_teleport_engine_gate();
  tb_0094_teleport_config_and_bind_row();
  tb_0095_teleport_diagonal_wall_support_distance();
  tb_0096_teleport_wall_drops_to_floor();
  tb_0097_nocull_active_needs_freecam_and_switch();
  tb_0098_nocull_far_writes_and_stock_recapture();
  tb_0099_nocull_config_key_defaults_on();
  tb_0100_panel_min_h_is_aas();
  tb_0101_menu_tab_rail_shows_every_tab();
  tb_0102_class_variant_label();
  tb_0103_config_enemy_subrow_defaults();
  tb_0104_name_with_variant();
  tb_0105_dmg_progress_rise_alpha();
  tb_0106_dmg_colour();
  tb_0107_dmg_keep_rule_reasons();
  tb_0108_dmg_display_and_kill();
  tb_0109_dmg_stack_index();
  tb_0110_dmg_ring_overwrites_oldest();
  tb_0111_dmg_fingerprint();
  tb_0112_dmg_config_default_on();
  tb_0113_esp_label_outline_offsets();
  tb_0114_weap_levers_m1_unchanged_m2_m4_scaled();
  tb_0115_weap_pbomb_end_never_before_launch();
  tb_0116_weap_reload_lever_and_max();
  tb_0117_weap_slider_mapping_and_text();
  tb_0118_weap_edge_detectors();
  tb_0119_weap_hold_capture_apply_restore();
  tb_0120_weap_class_kinds();
  tb_0121_weap_config_defaults();
  tb_0123_aim_keep_nearest();
  tb_0124_aim_lead_point_by_weapon_speed();
  tb_0125_aim_redirect_keeps_length();
  tb_0126_aim_bone_cache_hit_miss_full();
  tb_0127_aim_config_defaults_and_labels();
  tb_0128_aim_fingerprints_tell_dir_from_origin();
  tb_0129_tbot_delay_machine();
  tb_0130_tbot_gates_and_when();
  tb_0131_tbot_wm_table_from_the_export();
  tb_0132_tbot_weapon_channel_and_bind_gates();
  tb_0133_offs_bypass_decision_table();
  tb_0134_offs_scan_and_call_site();
  tb_0135_trk_ray_straight_track_y_from_ray();
  tb_0136_trk_ray_curved_parallel_behind();
  tb_0137_trk_ray_nearest_crossing_wins();
  tb_0138_trk_flatten_rows();
  tb_0139_secret_found_hides_it();
  tb_0140_tbot_pulse_semi_auto();
  tb_0141_aim_obstacle_kind();
  tb_0142_aim_ray_engine_history();
  tb_0143_aim_damage_handler_fingerprints();
  tb_0144_tbot_semi_press_follows_the_latch();
  tb_0145_area_bounds_through_the_world_matrix();
  tb_0146_trk_largest_engine_layout_fits();
  tb_0147_trk_ray_nearest_point();
  tb_0148_offscreen_targets_toggle();
  tb_0149_area_obb_corners();
  tb_0150_offs_shot_verdict();
  tb_0151_tbot_press_outcome();
  tb_0152_aim_blockers_and_pickups_pass();
  tb_0153_babe_freed_hides_it();
  tb_0154_offs_shot_due();
  tb_0155_offs_hit_slot();
  tb_0156_bl_state_machine();
  tb_0157_bl_capture_and_mode();
  tb_0158_bl_pick_monitor();
  tb_0159_bl_style_and_pos_filter();
  tb_0160_bl_minimise_messages();
  tb_0161_bl_config_default_on();
  tb_0162_ft_script_kind();
  tb_0163_bl_mode_drift();
  tb_0164_bl_scene_gate();
  tb_0165_win_check();
  tb_0166_bl_window_rect_fails_gld_fullscreen_test();
  tb_0168_hk_sig_finds_the_keyboard_slot();
  tb_0169_hk_state_machine();
  tb_0170_hk_reacquire_and_default();
  tb_0171_hk_engine_input_path();
  tb_0172_si_script_kind();
  tb_0173_si_config_default_off();
  tb_0174_fov_want_and_slider();
  tb_0175_fov_state_machine();
  tb_0176_wz_notch_rule();
  tb_0177_wz_smooth_step();
  tb_0178_wz_source_and_config();
  tb_0179_campos_round_trip_and_refusal();
  tb_0180_freecam_readout();
  tb_0181_shot_hud_machine();
  tb_0182_entry_vk_events();
  tb_0183_entry_commit_and_clamp();
  tb_0184_gb_vk_to_trigger();
  tb_0185_qw_review_rules();
  tb_0186_fov_readout_fits();
  tb_0187_wz_engage_rule();
  tb_0188_wz_own_distance();
  tb_0189_aim_seg_box();
  tb_0190_clip_restricted();
  tb_0191_aim_blocker_kinds();
  tb_0192_cut_force_decision();
  tb_0193_cut_undo_only_forced();
  tb_0194_cut_handler_sig();
  tb_0195_cut_config_default_off();
  tb_0196_cut_prune_keeps_seen();
  tb_0197_fcf_wheel_fov_rules();
  tb_0198_ma_cursor_ray_round_trips();
  tb_0199_ma_duke_plane_straight_and_curved();
  tb_0200_ma_fire_precedence();
  tb_0201_ma_face_side_and_turn();
  tb_0202_ma_config_defaults();
  tb_0203_tb_bend_target_and_clamp();
  tb_0204_tb_bend_step_stock_rates();
  tb_0205_tb_spine_fingerprint();
  tb_0206_ma_face_trial_taint();
  tb_0207_tb_owner_eases_out();
  tb_0208_ma_face_yaw_flip();
  tb_0209_ma_cursor_step_sensitivity();
  tb_0210_ma_reticle_style();
  tb_0211_ma_swatches();
  tb_0211_ma_face_live_and_axis();
  tb_0212_cleanup_removed_keys_not_read();
  tb_0213_freecam_fixed_move_keys();
  tb_0214_freecam_speed_pct();
  tb_0215_freecam_switch_arms_hotkeys();
  tb_0216_menu_collapse_keeps_ordinals();
  tb_0217_explodables_class_and_shown();
  tb_0218_esp_saltire();
  tb_0219_pickup_can_take();
  tb_0220_freecam_bind_refuses_move_keys();
  tb_0221_jr_takeoff_restore_table();
  tb_0222_jr_press_classify();
  tb_0223_jumpfix_config_default_on();
  tb_0224_fp_pitch_clamp();
  tb_0225_fp_yaw_from_deltas();
  tb_0226_fp_mode_machine();
  tb_0227_fp_view_fire_dir();
  tb_0228_fp_config_defaults_off();
  tb_0229_mv_gate();
  tb_0230_mv_snap_keeps_position();
  tb_0231_mv_fingerprints();
  tb_0232_mv_depth_axis_and_away();
  tb_0233_mv_move_vector();
  tb_0234_mv_find_impulse_call();
  tb_0235_mv_new_fingerprints();
  tb_0236_fp_look_gate();
  tb_0237_mv_exit_repick_waits();
  tb_0238_mv_lr_walks();
  tb_0239_mv_keys_clear_when_not_live();
  tb_0240_mv_left_line();
  tb_0241_mw_after_focus_loss();
  tb_0242_fp_pause_no_player();
  tb_0243_fp_pause_ladder();
  tb_0244_ent_point_in_obb();
  tb_0245_mv_keys_to_game();
  tb_0246_mv_camera_snap();
  tb_0247_rescue_rule();
  tb_0248_rescue_config_default_on();
  tb_0249_fp_bind_rows();
  tb_0250_fp_crouch();
  tb_0251_mv_interact_read();
  tb_0252_fp_interact();
  tb_0253_tp_track_lock();
  tb_0254_fp_air_jump();
  tb_0255_interact_edges();
  tb_0256_fp_ladder_view();
  tb_0257_fp_view_bridge();
  tb_0258_fp_jump_strength();
  tb_0259_fp_crouch_ease();
  tb_0260_fp_scene_hold();
  tb_0261_fp_scene_search_gate();
  tb_0262_ai_wake_candidates();
  tb_0263_ai_sight_route();
  tb_0264_ai_reaim_keeps_speed();
  tb_0265_ai_gate();
  tb_0266_ai_fingerprints();
  tb_0267_mw_dinput8_version();
  tb_0268_mw_raw_rows();
  tb_0269_mw_menu_near_loss();
  tb_0290_mw_guithread_rules();
  tb_0270_ai_review_rules();
  tb_0270_fp_resume_applies_at_once();
  tb_0271_fp_track_guard();
  tb_0272_fp_trail();
  tb_0273_tp_keep_off_track();
  tb_0274_fp_ladder_reason();
  tb_0275_fp_restore_waits_for_state_3();
  tb_0276_fp_hold_seed_yaw();
  tb_0277_fp_ladder_air();
  tb_0278_fp_track_forget();
  tb_0279_fp_exit_view();
  tb_0280_ai_bullet_route();
  tb_0281_fp_pbomb_throw();
  tb_0282_fp_handback();
  tb_0283_fp_handback_track_off();
  tb_0284_bl_fs_cvar_name();
  tb_0285_bl_early_decide();
  tb_0286_bl_early_state();
  tb_0287_bl_applied_ok();
  tb_0288_ini_peek_int();
  tb_0289_bl_early_failed_is_ours();
  tb_0293_config_borderless_variant_gone();
  tb_0294_hk_level_per_kind();
  tb_0295_hk_raw_row_mouse();
  tb_0296_config_mouse_nonexclusive_gone();
  tb_0297_cursor_hold_decision();
  tb_0298_cursor_hold_centre();
  tb_0299_menu_tab_rail();
  tb_0300_menu_fold_ease();
  tb_0301_menu_scroll_cue();
  tb_0330_fontpack_rules();
  tb_0331_atlas_crc_adler();
  tb_0332_atlas_png_layout();
  tb_0333_atlas_png_two_blocks();
  tb_0334_atlas_shd_text();
  tb_0335_canvas_atlas_step();
  tb_0336_ui_scale_settled_and_px();
  tb_0337_atlas_readback();
  tb_0338_ui_scale_ceiling();
  tb_0339_slider_geometry_uniform();
  tb_0340_capped_panel_drag();
  tb_0341_weap_arifle_rps_and_interval();
  tb_0342_weap_hold_int();
  tb_0343_weap_arifle_burst_same_count_faster();
  tb_0344_pork_parts_open();
  tb_0345_boss_gates_open();
  tb_0346_aim_obstacle_immune_blocks();
  tb_0347_boss_parts_spec();
  tb_0348_boss_kinds();
  tb_0349_aim_target_takes_damage();
  tb_0350_freecam_pause_rules();
  tb_0351_freecam_pause_config();
  tb_0352_nocull_proj_push_while_paused();
  tb_0471_nocull_far_override();
  tb_0353_trk_nearest_xz();
  tb_0354_trk_choose();
  tb_0355_tp_depth_run_marker();
  tb_0356_photo_tile_seams();
  tb_0357_photo_strip_and_tile_order();
  tb_0358_photo_aborts();
  tb_0359_photo_file_name();
  tb_0360_photo_visible_rect();
  tb_0361_photo_calibration();
  tb_0362_crate_content_codes();
  tb_0363_crate_labels();
  tb_0364_crate_open_and_arrows();
  tb_0365_shot_letterbox_and_stats();
  tb_0366_glopp_reach();
  tb_0367_glopp_gate();
  tb_0368_glopp_verdict();
  tb_0369_glopp_morphing();
  tb_0370_glopp_unread_class_and_fp();
  tb_0379_paths_config();
  tb_0380_ls_map_episode_part();
  tb_0381_ls_def_and_totals();
  tb_0382_ls_hard_nukes();
  tb_0383_ls_lines();
  tb_0384_ls_gate_corner_config();
  tb_0390_pg_record_decode();
  tb_0391_pg_part_row();
  tb_0392_pg_build();
  tb_0393_pg_difficulty_default();
  tb_0394_pg_names_and_layout();
  tb_0396_egg_class_and_label();
  tb_0397_egg_state_and_target();
  tb_0403_egg_level_and_cache();
  tb_0404_ovk_doomed_rule();
  tb_0405_ovk_pending_direct_and_splash();
  tb_0406_ovk_burst();
  tb_0407_ovk_review_fixes();
  tb_0408_hurt_burst_window();
  tb_0409_hurt_quiet_and_tally();
  tb_0410_queen_swipe_gate();
  tb_0411_queenfix_config_default_on();
  tb_0412_hide_cant_take_switch();
  tb_0413_wimm_weapon_routes();
  tb_0414_wimm_gate_order();
  tb_0415_wimm_fingerprints_and_local_y();
  tb_0416_tbot_pcannon_tap();
  tb_0417_tbot_pcannon_cadence();
  tb_0420_pz_map_and_gate();
  tb_0421_pz_e05_rule();
  tb_0422_pz_e07_rule();
  tb_0423_pz_view_and_debounce();
  tb_0424_pz_pulse_and_config();
  tb_0440_ar_part_table_and_world_parse();
  tb_0441_ar_link_pairs_and_runs();
  tb_0442_ar_route_and_text();
  tb_0443_ar_msv_records();
  tb_0444_ar_tally_and_missing();
  tb_0450_ar_cross_target_area();
  tb_0451_ar_cross_exit_choice();
  tb_0452_ar_cross_current_first_and_label();
  tb_0460_am_layers();
  tb_0461_am_place();
  tb_0462_am_link_endpoints();
  tb_0463_am_route();
  tb_0464_am_shared_cross_choice();
  tb_0465_ar_nuke_totals_from_world();
  tb_0466_ar_nukes_live();
  tb_0467_ar_nukes_msv();
  tb_0468_ar_nukes_merge_and_derive();
  tb_0469_am_pip_rows_and_finished();
  tb_0470_ar_cross_label_nukes();
  tb_0472_ar_nukes_review_fixes();
  tb_0473_carry_detection();
  tb_0474_carry_label();
  tb_0475_ar_enemy_nukes_found();
  tb_0476_photo_strip_chunks();
  tb_0477_pg_hard_live();
  tb_0478_tip_wrap();
  tb_0479_tip_place();
  tb_0480_tbot_x3000_hold();
  tb_0481_laser_world_parse();
  tb_0482_laser_nukes_found();
  tb_0483_laser_marker();
  tb_0484_nuke_totals_raw();
  tb_0485_pg_kill_totals_match_slot();
  tb_0486_aw_thresholds();
  tb_0487_aw_cells();
  tb_0488_aw_text();
  tb_0490_cm_cvars();
  tb_0491_cm_layout_and_restore();
  tb_0492_cm_slot();
  tb_0493_cm_check_fp();
  tb_0494_pf_fold_state();
  tb_0495_pf_summaries();
  tb_0496_hitch_thresholds();
  tb_0497_breakable_drops();
  tb_0498_breakable_six_pins();
  tb_0499_photo_folder_shorten();
  tb_0500_photo_folder_default();
  tb_0501_config_string_roundtrip();
  tb_0502_x3000_drawn_table();
  tb_0503_x3000_view_gate();
  tb_0504_x3000_find_actor_draw();
  tb_0506_sio_acc();
  tb_0507_sio_table();
  tb_0508_sio_format();
  tb_0509_sio_bufferable();
  tb_0510_sio_buf_same_bytes();
  tb_0511_sio_buf_edges();
  tb_0512_gs_clock_continuity();
  tb_0513_gs_clock_monotonic_and_wrap();
  tb_0514_gs_caller_range();
  tb_0515_gs_settings();
  tb_0516_att_world_pos();
  tb_0517_ar_ambush_parse();
  tb_0518_ar_ambush_helpers();
  tb_0519_ar_ambush_state();
  tb_0520_ar_ambush_label();
  tb_0521_ambush_config_colour();
  tb_0522_ar_kills_live();
  tb_0523_ar_kills_msv();
  tb_0524_ar_kills_text();
  tb_0525_am_ambush_rows();
  tb_0526_ar_ambush_trigger_state();
  tb_0527_stall_policy();
  tb_0528_stall_walk();
  tb_0529_stall_fmt_addr();
  tb_0530_stall_long_frames();
  tb_0531_photo_folder_path_ok();
  tb_0532_photo_file_name_n();
  tb_0533_ini_overlong_line();
  tb_0534_log_redact_profile();
  printf("%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
