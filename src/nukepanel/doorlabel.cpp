// The cross-area door label (doorlabel.h; the rules are areas.h's ar_want_counts, ar_cross_pick, ar_cross_label).
// Main thread, frame path. Reads only Duke's position (inside probe's validated player span) and the areas view.
#include "doorlabel.h"
#include "areas.h"
#include "config.h"
#include "entities.h"     // entities_snapshot: the player at draw time
#include "esp_layout.h"   // esp_draw_label, esp_label_x
#include "view.h"
#include "canvas.h"
#include "font.h"
#include "ui/common.h"    // clamp_double_to_int
#include "log.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static const ULONGLONG kChooseMs = 250, kLogMs = 5000;

struct DoorLabel {
  int active, secrets;   // secrets: the label counts secrets (drawn in secret yellow, else nuke green)
  const void* world;     // the world it was chosen in: drawn only while Duke is still there
  ArCross c;
  Vec3 at;               // the doorway's volume centre (offline, unrotated)
  char text[96];
};
static DoorLabel s_l;
static ULONGLONG s_nextChoose, s_nextLog, s_logMs;
static int s_logKey = -2;
// Cumulative: choices made, labels drawn, no area with anything wanted missing in reach, the areas' found state not
// ready for this world (an area change in progress).
static unsigned s_chooses, s_drawn, s_noTarget, s_notReady;

static void choice_log(int key, const char* what) {
  const ULONGLONG now = GetTickCount64();
  if (key == s_logKey || now < s_logMs + 1000) return;   // a flicker shows in the 5 s line's counts
  s_logKey = key;
  s_logMs = now;
  np_log("doorlabel: %s; chooses %u, drawn %u, no target %u, not ready %u", what, s_chooses, s_drawn, s_noTarget,
         s_notReady);
}

static void choose(const unsigned char* player, const void* world, int wantS, int wantN) {
  s_l.active = 0;
  s_chooses++;
  const ArView* v = areas_view();
  if (!v->ok || !v->part || v->foundWorld != world || v->cur < 0 || v->cur >= v->part->nareas) {
    s_notReady++;
    choice_log(-1, "none (the areas' found state is not ready for this area)");
    return;
  }
  const ArPart* P = v->part;
  const ArArea& a = P->areas[v->cur];
  ArWant w;
  int missing[AR_AREAS_MAX];
  ar_want_counts(P, v->found, v->nfound, wantS, wantN, &w, missing);
  Vec3 me;
  memcpy(&me, player + 0x18C, sizeof me);
  float dist[AR_DOORS_MAX];
  for (int k = 0; k < a.ndoors; ++k) {
    float q[3], bottom;
    ar_door_centre(&a.doors[k], q, &bottom);
    const float dx = q[0] - me.x, dy = q[1] - me.y, dz = q[2] - me.z;
    dist[k] = sqrtf(dx * dx + dy * dy + dz * dz);
  }
  ArCross c;
  if (!ar_cross_pick(P, v->cur, missing, dist, &c)) {
    s_noTarget++;
    choice_log(-1, "none (no other area with anything wanted missing in reach)");
    return;
  }
  const ArDoor& d = a.doors[c.door];
  float q[3], bottom;
  ar_door_centre(&d, q, &bottom);
  s_l.active = 1;
  s_l.world = world;
  s_l.c = c;
  s_l.at.x = q[0]; s_l.at.y = q[1]; s_l.at.z = q[2];
  s_l.secrets = w.secrets[c.target] > 0;
  ar_cross_label(P, &c, &w, s_l.text, sizeof s_l.text);
  char what[200];
  _snprintf_s(what, sizeof what, _TRUNCATE, "%s via %s (hops %d, %.0f away), \"%s\"%s%s", P->areas[c.target].map,
              d.name, c.hops, dist[c.door], s_l.text, d.run[0] ? ", depth run " : "", d.run);
  choice_log(c.target * AR_DOORS_MAX + c.door, what);
}

void doorlabel_frame(const unsigned char* player) {
  const int wantS = g_cfg.espSecrets != 0, wantN = g_cfg.espNukes != 0;
  if (!player || (!wantS && !wantN)) {   // menus, loading, or both switches off: no label, no shared choice
    s_l.active = 0;
    areas_set_cross(0, 0);
    return;
  }
  const void* world;
  memcpy(&world, player + 0xE0, sizeof world);
  const ULONGLONG now = GetTickCount64();
  if (now >= s_nextChoose) {
    s_nextChoose = now + kChooseMs;
    choose(player, world, wantS, wantN);
  }
  // The Progress tab's map and list follow the label's choice (one shared choice, review of areas2, finding 3).
  areas_set_cross(world, s_l.active && s_l.world == world ? &s_l.c : 0);
  if (now >= s_nextLog) {
    s_nextLog = now + kLogMs;
    np_log("doorlabel: %s%s%s; chooses %u, drawn %u, no target %u, not ready %u", s_l.active ? "\"" : "off",
           s_l.active ? s_l.text : "", s_l.active ? "\"" : "", s_chooses, s_drawn, s_noTarget, s_notReady);
  }
}

void doorlabel_draw(int W, int H) {
  if (!s_l.active || (!g_cfg.espSecrets && !g_cfg.espNukes)) return;
  const unsigned char* player = (const unsigned char*)entities_snapshot().player;
  if (!player) return;
  const void* world;
  memcpy(&world, player + 0xE0, sizeof world);
  if (world != s_l.world) return;
  Mat4 view, proj;
  float nearZ;
  if (!view_get(&view, &proj, &nearZ)) return;
  const ScreenPt s = w2s_project(s_l.at, view, proj, nearZ, W, H);
  if (!s.ok || s.x < 0.f || s.y < 0.f || s.x > (float)W || s.y > (float)H) return;   // nothing while off-screen
  const unsigned colour = marker_colour(s_l.secrets ? CAT_SECRET : CAT_NUKE, -1);
  const int x = clamp_double_to_int((double)s.x + 0.5), y = clamp_double_to_int((double)s.y + 0.5);
  canvas_begin(W, H);
  esp_draw_label(esp_label_x(x, 0, canvas_text_width(s_l.text, FONT_SMALL)), y, s_l.text, colour, FONT_SMALL);
  canvas_end();
  s_drawn++;
}
