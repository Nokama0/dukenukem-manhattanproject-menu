// Puzzle solutions: finds the two puzzles' entities by name once per map (spawn_search "n="), validates them once
// a frame, reads their state fields and draws the next step. Evidence: docs/re/puzzle-e05p02.md section 5 and
// docs/re/puzzle-e07p03.md section 3; the rules are puzzlerule.h's.
#include "puzzle.h"
#include "puzzlerule.h"
#include "entities.h"
#include "engine.h"
#include "view.h"
#include "canvas.h"
#include "esp_layout.h"   // esp_draw_label
#include "ui/common.h"    // clamp_double_to_int
#include "volume.h"
#include "font.h"
#include "config.h"
#include "log.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef const char* (__thiscall* ClassnameFn)(const void* self);

// The world's inline map name (levelstats.cpp, photo.cpp) and the object fields every entity here shares.
static const unsigned kWorldName = 0x48, kOfsKilled = 0x90, kOfsPos = 0x18C, kObjSpan = 0x198;

struct PzEnt { const char* name; const char* cls; unsigned span; int pos; const unsigned char* e; const void* vt; };

// e05p02_a01: mover_adv (alloc 0xF94) point index +0xE4C; flip-flop (alloc 0x21C) bit +0x20C, no position.
enum { E05_Y, E05_B, E05_R, E05_RESET, E05_F1, E05_F2, E05_F3, E05_PLATFORM, E05_N };
static PzEnt s_e05[E05_N] = {
  { "mover_yellow_button", "mover_adv", 0xE50, 1 }, { "mover_blue_button", "mover_adv", 0xE50, 1 },
  { "mover_red_button", "mover_adv", 0xE50, 1 },    { "mover_reset_button", "mover_adv", 0xE50, 1 },
  { "flipflop1", "flip-flop", 0x210, 0 },           { "flipflop2", "flip-flop", 0x210, 0 },
  { "flipflop3", "flip-flop", 0x210, 0 },           { "mover_spike_platform", "mover_adv", 0xE50, 1 },
};
// e07p03_a01: area_switch (alloc 0xC9C) area_active value +0xAE4; hinged_door (alloc 0xF74) opened +0xCAC,
// motion +0xF4C.
enum { E07_S1, E07_S2, E07_S3, E07_RESET, E07_C1, E07_C2, E07_C3, E07_N };
static PzEnt s_e07[E07_N] = {
  { "switch_1", "area_switch", 0xAE8, 1 }, { "switch_2", "area_switch", 0xAE8, 1 },
  { "switch_3", "area_switch", 0xAE8, 1 }, { "switch_reset", "area_switch", 0xAE8, 1 },
  { "cog_1", "hinged_door", 0xF50, 1 },    { "cog_2", "hinged_door", 0xF50, 1 },
  { "cog_3", "hinged_door", 0xF50, 1 },
};

// Where each target draws, from map data (unverified until seen): a plate's centre top is its mover origin + (28, 8, 26)
// (puzzle-e05p02.md section 1); a button's model stands at its switch + (20, -16, -20), the reset button's at +
// (56, 32, 0) (puzzle-e07p03.md section 1). The box outlines the plate or button; the label floats above it.
struct PzLook { Vec3 base, boxOff, half; float labelUp; };
static const PzLook kPlate = { { 28.f, 8.f, 26.f }, { 0.f, -4.f, 0.f }, { 30.f, 6.f, 28.f }, 56.f };
static const PzLook kButton = { { 20.f, -16.f, -20.f }, { 0.f, 28.f, 0.f }, { 18.f, 28.f, 18.f }, 80.f };
static const PzLook kResetButton = { { 56.f, 32.f, 0.f }, { 0.f, 28.f, 0.f }, { 18.f, 28.f, 18.f }, 80.f };

static const unsigned char* s_player;
static const void* s_world;
static int s_puzzle;                 // PZ_* of the current map
static int s_found;                  // every entity found and of its class
static ULONGLONG s_findNext;
static unsigned s_finds, s_lost;     // cumulative per map: find attempts, entities that failed validation
static PzDebounce s_deb;
static char s_lastState[200];
static int s_lines;                  // state lines this map, capped
static const int kLinesMax = 400;

static int rdi(const unsigned char* p) { int v; memcpy(&v, p, sizeof v); return v; }
static Vec3 rdv(const unsigned char* p) { Vec3 v; memcpy(&v, p, sizeof v); return v; }

static PzEnt* table(int* n) {
  if (s_puzzle == PZ_E05) { *n = E05_N; return s_e05; }
  if (s_puzzle == PZ_E07) { *n = E07_N; return s_e07; }
  *n = 0;
  return 0;
}

// A new player or world: read the map name once and pick the puzzle.
static void stage(const unsigned char* player, const void* world) {
  s_player = player;
  s_world = world;
  s_found = 0;
  s_findNext = 0;
  s_finds = s_lost = 0;
  s_deb.raw = 0;
  s_lastState[0] = 0;
  s_lines = 0;
  char map[64] = "";
  const char* name = (const char*)world + kWorldName;
  if (world && guarded_readable(name, sizeof map) && memchr(name, 0, sizeof map)) strcpy_s(map, sizeof map, name);
  s_puzzle = pz_map_puzzle(map);
  if (s_puzzle)
    np_log("puzzle: %s recognised (%s)", map, s_puzzle == PZ_E05 ? "floor plates Yellow, Blue, Red"
                                                                 : "cog buttons switch_1, switch_2, switch_3");
}

// Each entity by name: the first live match of class `cls` (vt[1], as the entity walk identifies them).
static bool find_one(int sid, PzEnt& t) {
  t.e = 0; t.vt = 0;
  g_eng.spawn_search("s=%d?n=%s", sid, t.name);
  const unsigned char* e = (const unsigned char*)g_eng.spawn_search_get();
  if (e && guarded_readable(e, t.span > kObjSpan ? t.span : kObjSpan)) {
    void* const* vt = *(void* const* const*)e;
    if (entities_in_duke(vt) && entities_in_duke(vt + 2) && entities_in_duke(vt[1])) {
      const char* cls = ((ClassnameFn)vt[1])(e);
      if (entities_in_duke(cls) && !strcmp(cls, t.cls)) { t.e = e; t.vt = vt; }
    }
  }
  g_eng.spawn_search_stop();
  return t.e != 0;
}

static void find_all(const unsigned char* player) {
  int n;
  PzEnt* t = table(&n);
  if (!t || !g_eng.spawn_search || !g_eng.spawn_search_get || !g_eng.spawn_search_stop) return;
  const int sid = rdi(player + 0x78);   // server id, the s= key (entities.md section 1), inside probe's span
  s_finds++;
  int got = 0;
  char missing[200] = "";
  for (int i = 0; i < n; ++i) {
    if (find_one(sid, t[i])) { got++; continue; }
    strncat_s(missing, sizeof missing, " ", _TRUNCATE);
    strncat_s(missing, sizeof missing, t[i].name, _TRUNCATE);
  }
  s_found = got == n;
  if (s_found) {
    np_log("puzzle: found %d/%d entities (attempt %u)", got, n, s_finds);
    for (int i = 0; i < n; ++i) {
      if (!t[i].pos) { np_log("puzzle:   %s %p", t[i].name, (const void*)t[i].e); continue; }
      const Vec3 p = rdv(t[i].e + kOfsPos);
      np_log("puzzle:   %s %p at (%.0f,%.0f,%.0f)", t[i].name, (const void*)t[i].e, p.x, p.y, p.z);
    }
  } else if (s_finds == 1 || s_finds % 30 == 0) {
    np_log("puzzle: found %d/%d (attempt %u); missing:%s", got, n, s_finds, missing);
  }
}

// Once a frame: every entity still readable, its vtable unchanged and not removed. One cache for the lot.
static bool validate(PzEnt* t, int n) {
  ReadCache rc = {};
  for (int i = 0; i < n; ++i) {
    const unsigned span = t[i].span > kObjSpan ? t[i].span : kObjSpan;
    if (!t[i].e || !guarded_readable_cached(&rc, t[i].e, span) || *(const void* const*)t[i].e != t[i].vt ||
        rdi(t[i].e + kOfsKilled) != 0) {
      s_lost++;
      np_log("puzzle: %s %p no longer valid (lost %u); finding again", t[i].name, (const void*)t[i].e, s_lost);
      s_found = 0;
      return false;
    }
  }
  return true;
}

// The current state of the map's puzzle, as the rule's inputs, one log line per change.
struct PzNow { int pressed[PZ_STEPS]; bool wrong, solved; Vec3 base[PZ_STEPS + 1]; const PzLook* look[PZ_STEPS + 1]; };

static void read_now(PzNow* s, char* line, int cap) {
  if (s_puzzle == PZ_E05) {
    PzE05 r;
    for (int i = 0; i < PZ_STEPS; ++i) r.down[i] = rdi(s_e05[E05_Y + i].e + 0xE4C) == 1;
    r.f1 = rdi(s_e05[E05_F1].e + 0x20C) != 0;
    r.f2 = rdi(s_e05[E05_F2].e + 0x20C) != 0;
    r.f3 = rdi(s_e05[E05_F3].e + 0x20C) != 0;
    r.platform = rdi(s_e05[E05_PLATFORM].e + 0xE4C);
    for (int i = 0; i < PZ_STEPS; ++i) s->pressed[i] = r.down[i];
    s->wrong = pz_e05_wrong(r);
    s->solved = pz_e05_solved(r);
    for (int i = 0; i <= PZ_STEPS; ++i) { s->base[i] = rdv(s_e05[E05_Y + i].e + kOfsPos); s->look[i] = &kPlate; }
    _snprintf_s(line, cap, _TRUNCATE, "plates Y%d B%d R%d reset%d, flip-flops %d%d%d, platform %d", r.down[0],
                r.down[1], r.down[2], rdi(s_e05[E05_RESET].e + 0xE4C), r.f1, r.f2, r.f3, r.platform);
  } else {
    PzE07 r;
    for (int i = 0; i < PZ_STEPS; ++i) {
      r.armed[i] = rdi(s_e07[E07_S1 + i].e + 0xAE4) != 0;
      r.opened[i] = rdi(s_e07[E07_C1 + i].e + 0xCAC) != 0;
      r.motion[i] = rdi(s_e07[E07_C1 + i].e + 0xF4C);
      s->pressed[i] = !r.armed[i];
    }
    s->wrong = pz_e07_wrong(r);
    s->solved = pz_e07_solved(r);
    for (int i = 0; i <= PZ_STEPS; ++i) {
      s->base[i] = rdv(s_e07[E07_S1 + i].e + kOfsPos);
      s->look[i] = i == PZ_STEPS ? &kResetButton : &kButton;
    }
    _snprintf_s(line, cap, _TRUNCATE, "buttons armed %d%d%d reset %d, cogs opened %d%d%d motion %d,%d,%d",
                r.armed[0], r.armed[1], r.armed[2], rdi(s_e07[E07_RESET].e + 0xAE4) != 0, r.opened[0], r.opened[1],
                r.opened[2], r.motion[0], r.motion[1], r.motion[2]);
  }
  for (int i = 0; i <= PZ_STEPS; ++i) {
    const PzLook& k = *s->look[i];
    s->base[i].x += k.base.x; s->base[i].y += k.base.y; s->base[i].z += k.base.z;
  }
}

// A tick right of a label: two strokes, keyline first, 2 px.
static void draw_tick(int x, int y, int h, unsigned colour) {
  const int x0 = x, y0 = y + h / 2, x1 = x + h / 4, y1 = y + h * 3 / 4, x2 = x + h / 2 + 2, y2 = y + h / 5;
  int dx[8], dy[8];
  const int n = esp_outline_offsets(1, dx, dy);
  const unsigned kc = esp_keyline_colour(colour);
  for (int pass = 0; pass < 2; ++pass)
    for (int k = 0; k < (pass ? 2 : n); ++k) {
      const int ox = pass ? k : dx[k], oy = pass ? 0 : dy[k];
      const unsigned c = pass ? colour : kc;
      canvas_line(x0 + ox, y0 + oy, x1 + ox, y1 + oy, c);
      canvas_line(x1 + ox, y1 + oy, x2 + ox, y2 + oy, c);
    }
}

// The next step's box, its 12 edges at the pulse's alpha, 2 px.
static void draw_box(Vec3 c, Vec3 half, unsigned colour, const Mat4& view, const Mat4& proj, float nearZ, int W,
                     int H) {
  VolumeGeom g;
  volume_project(c, half, view, proj, nearZ, W, H, &g);
  if (!g.onScreen) return;
  int ix[8], iy[8];
  for (int k = 0; k < 8; ++k) {
    ix[k] = clamp_double_to_int((double)g.c[k].x + 0.5);
    iy[k] = clamp_double_to_int((double)g.c[k].y + 0.5);
  }
  for (int k = 0; k < 12; ++k) {
    if (!g.edgeOk[k]) continue;
    const int a = VOL_EDGE[k][0], b = VOL_EDGE[k][1];
    canvas_line(ix[a], iy[a], ix[b], iy[b], colour);
    canvas_line(ix[a] + 1, iy[a], ix[b] + 1, iy[b], colour);
  }
}

static void draw(const PzNow& s, const PzView& v, int W, int H) {
  Mat4 view, proj;
  float nearZ;
  if (!view_get(&view, &proj, &nearZ)) return;
  // The plates' own colours on e05; the buttons have none, so the panel's accent on e07. Reset is bone on both.
  static const unsigned kE05[PZ_STEPS] = { C_HAZARD, C_BLUE, C_RED };
  const int alpha = pz_pulse_alpha(GetTickCount64());
  canvas_begin(W, H);
  for (int i = 0; i <= PZ_STEPS; ++i) {
    const bool reset = i == PZ_STEPS;
    if (reset && v.next != PZ_RESET) continue;   // "Reset" only after a wrong step
    const unsigned colour = reset ? C_BONE : s_puzzle == PZ_E05 ? kE05[i] : C_HAZARD;
    const PzLook& k = *s.look[i];
    if (v.next == i) {
      const Vec3 bc = { s.base[i].x + k.boxOff.x, s.base[i].y + k.boxOff.y, s.base[i].z + k.boxOff.z };
      draw_box(bc, k.half, canvas_with_alpha(colour, alpha), view, proj, nearZ, W, H);
    }
    const Vec3 lp = { s.base[i].x, s.base[i].y + k.labelUp, s.base[i].z };
    const ScreenPt sp = w2s_project(lp, view, proj, nearZ, W, H);
    if (!sp.ok || sp.x < 0.f || sp.y < 0.f || sp.x > (float)W || sp.y > (float)H) continue;
    char text[8];
    if (reset) strcpy_s(text, sizeof text, "Reset");
    else sprintf_s(text, "%d", i + 1);
    const int fh = font_height[FONT_UI];
    const int tw = canvas_text_width(text, FONT_UI);
    const int x = clamp_double_to_int((double)sp.x + 0.5) - tw / 2;
    const int y = clamp_double_to_int((double)sp.y + 0.5) - fh / 2;
    esp_draw_label(x, y, text, colour, FONT_UI);
    if (!reset && v.done[i]) draw_tick(x + tw + 4, y, fh, C_GREEN);
  }
  canvas_end();
}

void puzzle_frame(int W, int H) {
  const EntSnapshot& snap = entities_snapshot();
  const unsigned char* player = (const unsigned char*)snap.player;
  if (!player) { s_player = 0; return; }   // a reload at reused addresses must stage again
  const void* world;
  memcpy(&world, player + 0xE0, sizeof world);   // inside probe's kPlayerSpan (levelstats.cpp reads it the same way)
  if (player != s_player || world != s_world) stage(player, world);
  if (s_puzzle == PZ_NONE || !g_cfg.espPuzzles) return;
  int n;
  PzEnt* t = table(&n);
  if (!s_found) {
    const ULONGLONG now = GetTickCount64();
    if (now < s_findNext) return;
    s_findNext = now + 2000;
    find_all(player);
    if (!s_found) return;
  }
  if (!validate(t, n)) return;
  PzNow s;
  char line[160];
  read_now(&s, line, sizeof line);
  const bool shown = pz_wrong_shown(&s_deb, s.wrong, GetTickCount64());
  const PzView v = pz_view(s.pressed, shown);
  const Vec3 centre = pz_centre(s.base, PZ_STEPS);
  const bool nearby = pz_near(snap.playerPos, centre);
  char full[200];
  _snprintf_s(full, sizeof full, _TRUNCATE, "%s; wrong %d shown %d, next %d, solved %d, near %d", line, (int)s.wrong,
              (int)shown, v.next, (int)s.solved, (int)nearby);
  if (strcmp(full, s_lastState) != 0) {
    strcpy_s(s_lastState, sizeof s_lastState, full);
    if (s_lines < kLinesMax) np_log("puzzle: %s", full);
    else if (s_lines == kLinesMax) np_log("puzzle: %d state lines this map; muted", kLinesMax);
    s_lines++;
  }
  if (!pz_show(g_cfg.espPuzzles, nearby, s.solved)) return;
  draw(s, v, W, H);
}
