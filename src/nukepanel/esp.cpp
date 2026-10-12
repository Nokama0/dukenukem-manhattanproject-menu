// Category ESP: enemies (brackets/health), items, keycards, nukes and babes (brackets round the collision box) and
// secrets (AA's cross mark). The draw follows AA's esp.cpp:278-289 (brackets), :508-680 (the actor marker) and
// :2193-2219 (the cross mark), fed from this frame's entity snapshot instead of Build sprites. Arrows/volumes are ours.
#include "esp.h"
#include "esp_layout.h"
#include "arrows.h"
#include "volume.h"
#include "input.h"
#include "ui/menu.h"
#include "entities.h"
#include "aim.h"        // aim_weapon_immune: the "(immune)" mark
#include "view.h"
#include "canvas.h"
#include "font.h"
#include "config.h"
#include "log.h"
#include "puzzle.h"
#include "carryrule.h"
#include <windows.h>
#include <stdio.h>
#include <math.h>

static EkMarker s_markers[EK_MARKER_COLLECT_MAX];
static EdgeArrow s_arrows[ARROW_MAX];

// Over the current 5 s window. Every entry lands in exactly one of skipped, dead, inactive, opened, found, freed,
// broken, dormant, cantTake, offscreen, capped or drawn, so they sum to `entries` and a gap shows. Arrows (arrows ==
// arrowsDrawn + arrowsPanel) and volumes are counted apart.
static unsigned s_frames, s_noview, s_entries, s_drawn, s_capped, s_offscreen, s_dead, s_skipped, s_inactive;
static unsigned s_found;   // found secrets: no marker, arrow or volume
static unsigned s_freed;   // freed babes: no marker or arrow
static unsigned s_broken, s_dormant;   // explodables already broken, or not taking damage yet: no marker
static unsigned s_cantTake;            // items Hide what you can't take hid (pickrule.h says TAKE_NO)
static unsigned s_opened;              // nukes and items of a crate or barrel that broke: the freed pickup shows instead
static unsigned s_takeUnknown;         // items that rule could not judge (pools unread): shown
static unsigned s_arrows_n, s_arrowsDrawn, s_arrowsPanel, s_arrowsCapped, s_volumes, s_volFaces;
static unsigned s_itemsOff;            // items (not keys or nukes) that went offscreen: no marker, arrows are a separate switch
static unsigned s_ambZones;            // ambush zone boxes drawn
static ULONGLONG s_next_log;

// One line per distinct item and reason, the first time: which items the queen fight hides and why. A ring of 48,
// so a long session still logs a late fight's items (a fixed cap filled before the Queen), with
// Duke's ammo pools as the take rule read them.
static void item_note(const EntSnap& e, const char* why, const PlayerPools* pp) {
  static struct { const void* p; char w; } seen[48];
  static int n, next;
  for (int i = 0; i < n; ++i) if (seen[i].p == e.ent && seen[i].w == why[0]) return;
  seen[next].p = e.ent; seen[next].w = why[0];
  next = (next + 1) % 48;
  if (n < 48) ++n;
  np_log("esp: item %p %s kind=%d sub=%d active=%d holder=%d hidden by %s pos (%.0f,%.0f,%.0f) half (%.0f,%.0f,%.0f)"
         " parent %p pos from %s, local (%.0f,%.0f,%.0f) pools %s %d/%.0f %d/%.0f %d/%.0f",
         e.ent, e.name ? e.name : "?", e.kind, e.subtype, (int)e.active, (int)e.holder, why,
         e.pos.x, e.pos.y, e.pos.z, e.hb.half.x, e.hb.half.y, e.hb.half.z, e.parent, att_name(e.att), e.local.x,
         e.local.y, e.local.z, pp && pp->valid ? "read" : "unread",
         pp ? pp->cur[0] : 0, pp ? pp->cap[0] : 0.f, pp ? pp->cur[1] : 0, pp ? pp->cap[1] : 0.f,
         pp ? pp->cur[2] : 0, pp ? pp->cap[2] : 0.f);
}

// Eight rects, two per corner: brackets rather than an outline so markers do not
// merge into a grid in a crowd.
static void draw_brackets(int x, int y, int w, int h, unsigned colour, int thickness = ESP_BRACKET_T) {
  const int cx = esp_bracket_arm(w);
  const int cy = esp_bracket_arm(h);
  const int t  = esp_bracket_thickness(cx, cy, thickness);
  if (t <= 0) return;
  const int x1 = x + w, y1 = y + h;
  canvas_rect(x,       y,       cx, t,  colour);
  canvas_rect(x,       y,       t,  cy, colour);
  canvas_rect(x1 - cx, y,       cx, t,  colour);
  canvas_rect(x1 - t,  y,       t,  cy, colour);
  canvas_rect(x,       y1 - t,  cx, t,  colour);
  canvas_rect(x,       y1 - cy, t,  cy, colour);
  canvas_rect(x1 - cx, y1 - t,  cx, t,  colour);
  canvas_rect(x1 - t,  y1 - cy, t,  cy, colour);
}

static void draw_marker(const EkMarker& m, const EntSnap& e, Vec3 playerPos) {
  const int bx = m.box.x, by = m.box.y, bw = m.box.w, bh = m.box.h;
  // An egg no wire can hatch (or whose wires were not read) is drawn dimmed: destroyable, never targeted.
  const bool idle = e.egg == EGG_INACTIVE || e.egg == EGG_UNREAD;
  const int alpha = idle ? 0x70 : 0xFF;
  // Bounding boxes: off hides the brackets only,
  // keeping the labels and the health bar exactly as they draw with it on.
  if (g_cfg.espBoundingBoxes) draw_brackets(bx, by, bw, bh, canvas_with_alpha(canvas_esp_colour(true), alpha));

  // hashp 0 (Mech Nukem, Wozma, the Crane): +0xB84 is not health there, so no bar
  // and no number.
  const int hp = e.hashp ? esp_hp_int(e.hp) : 0;
  const int maxhp = e.hashp ? esp_hp_int(e.maxhp) : 0;
  if (g_cfg.espHealthBars && hp > 0 && maxhp > 0) {
    const int barX = esp_bar_x(bx);
    const int fill = canvas_bar_fill(bh, hp, maxhp);   // never past bh, even above maxhp
    canvas_rect(barX, by, ESP_BAR_W, bh, canvas_with_alpha(C_WELL, alpha));
    if (fill > 0)
      canvas_rect(barX, by + (bh - fill), ESP_BAR_W, fill, canvas_with_alpha(canvas_health_colour(hp, maxhp), alpha));
  }

  // The variant is always appended to the name on the same line, so it needs Names on.
  // An enemy the held weapon cannot hurt now says so, dimmed, Names on or off.
  const bool immune = aim_weapon_immune(e.ent) != 0;
  char nameBuf[80];
  const char* name = 0;
  if (g_cfg.espNames && e.name) {
    class_name_with_variant(nameBuf, sizeof nameBuf, e.name, e.tier, true);
    if (immune) strcat_s(nameBuf, sizeof nameBuf, " (immune)");
    name = nameBuf;
  } else if (immune) {
    name = "(immune)";
  }
  // A nuke it carries (re/pickups.md section 6): " + Nuke" in the nuke's green after the name, or alone.
  char sfxBuf[24];
  const char* sfx = carry_suffix(e.carry, sfxBuf, sizeof sfxBuf);
  if (!name && sfx[0]) ++sfx;
  char num[ESP_HEALTH_NUM_BUFSIZE];
  const bool haveNum = g_cfg.espHealthNumbers && esp_health_number(num, hp, maxhp);
  if (name || haveNum || sfx[0]) {
    const int baseW = name ? canvas_text_width(name, FONT_SMALL) : 0;
    const int nameW = baseW + (sfx[0] ? canvas_text_width(sfx, FONT_SMALL) : 0);
    const int numW = haveNum ? canvas_text_width(num, FONT_MONO) : 0;
    const EspLabelRuns runs = esp_label_runs(bx, bw, nameW, numW);
    const int nameY = esp_name_label_y(by);
    if (name) esp_draw_label(runs.nameX, nameY, name, idle || immune ? C_BONEDIM : C_BONE, FONT_SMALL);
    if (sfx[0]) esp_draw_label(runs.nameX + baseW, nameY, sfx, C_GREEN, FONT_SMALL);
    if (haveNum)
      esp_draw_label(runs.numX, esp_label_mono_y(nameY, font_height[FONT_SMALL], font_height[FONT_MONO]),
                  num, idle ? C_BONEDIM : C_BONE, FONT_MONO);
  }

  if (g_cfg.espDistance) {
    // Player-relative, not camera-relative: this is the gameplay distance, so it
    // must not change when the camera zooms out or flies.
    // One rounding of the raw distance, not of the unit-rounded dist2d.
    const float dm = esp_dist_m(e.pos.x - playerPos.x, e.pos.z - playerPos.z);
    char buf[24];
    sprintf_s(buf, "%d m", clamp_double_to_int((double)dm + 0.5));
    esp_draw_label(esp_label_x(bx, bw, canvas_text_width(buf, FONT_SMALL)), esp_dist_label_y(by, bh),
                buf, C_BONEDIM, FONT_SMALL);
  }
}

// Items and keycards: the same bracket box as an enemy, always generic blue
// (the box never varies by category); only the
// label takes the card's own colour, and only for a keycard.
static void draw_pickup_marker(const EkMarker& m, const EntSnap& e) {
  const int bx = m.box.x, by = m.box.y, bw = m.box.w, bh = m.box.h;
  draw_brackets(bx, by, bw, bh, C_BLUE);
  const char* label = e.cat == CAT_KEY ? keycard_colour_name(e.keyColour) : e.name;
  if (!label) return;
  esp_draw_label(esp_label_x(bx, bw, canvas_text_width(label, FONT_SMALL)), esp_name_label_y(by),
              label, marker_colour(e.cat, e.keyColour), FONT_SMALL);
}

// Nukes and babes: brackets in the category colour, since the model hides a centre
// cross. The nuke, the level objective, draws heavier and labels in FONT_UI.
// Player-relative distance below, as on every marker.
static void draw_objective_marker(const EkMarker& m, const EntSnap& e, Vec3 playerPos) {
  const int bx = m.box.x, by = m.box.y, bw = m.box.w, bh = m.box.h;
  const unsigned colour = marker_colour(e.cat, e.keyColour);
  const bool nuke = e.cat == CAT_NUKE;
  draw_brackets(bx, by, bw, bh, colour, nuke ? ESP_NUKE_BRACKET_T : ESP_BRACKET_T);
  const int font = nuke ? FONT_UI : FONT_SMALL;
  if (e.name)
    esp_draw_label(esp_label_x(bx, bw, canvas_text_width(e.name, font)), esp_name_label_y(by, font_height[font]),
                e.name, colour, font);
  const float dm = esp_dist_m(e.pos.x - playerPos.x, e.pos.z - playerPos.z);
  char buf[24];
  sprintf_s(buf, "%d m", clamp_double_to_int((double)dm + 0.5));
  esp_draw_label(esp_label_x(bx, bw, canvas_text_width(buf, FONT_SMALL)), esp_dist_label_y(by, bh),
              buf, C_BONEDIM, FONT_SMALL);
}

// A nuke an enemy carries: small heavy brackets in the nuke colour at the centre of the enemy's box, whose own label
// says " + Nuke". With Enemies off there is no such label, so this marker carries one and the distance.
static void draw_carry_marker(const EkMarker& m, const EntSnap& e, Vec3 playerPos) {
  int side = (m.box.w < m.box.h ? m.box.w : m.box.h) / 3;
  side = side < 10 ? 10 : side > 28 ? 28 : side;
  const int x = m.box.x + m.box.w / 2 - side / 2, y = m.box.y + m.box.h / 2 - side / 2;
  draw_brackets(x, y, side, side, C_GREEN, ESP_NUKE_BRACKET_T);
  if (g_cfg.espOn) return;
  char buf[24];
  const char* label = carry_marker_label(e.carry, buf, sizeof buf);
  esp_draw_label(esp_label_x(x, side, canvas_text_width(label, FONT_SMALL)), esp_name_label_y(y), label, C_GREEN,
                 FONT_SMALL);
  const float dm = esp_dist_m(e.pos.x - playerPos.x, e.pos.z - playerPos.z);
  char d[24];
  sprintf_s(d, "%d m", clamp_double_to_int((double)dm + 0.5));
  esp_draw_label(esp_label_x(x, side, canvas_text_width(d, FONT_SMALL)), esp_dist_label_y(y, side), d, C_BONEDIM,
                 FONT_SMALL);
}

// Secrets: AA's cross mark, in the category's colour, name above and 2D
// distance below -- the same two label gaps the bracket markers use.
static void draw_point_marker(const EkMarker& m, const EntSnap& e, Vec3 playerPos) {
  const unsigned colour = marker_colour(e.cat, e.keyColour);
  const int cx = m.p.x, cy = m.p.y;
  canvas_rect(esp_secret_hbar_x(cx), esp_secret_hbar_y(cy), esp_secret_bar_span(), ESP_SECRET_T, colour);
  canvas_rect(esp_secret_vbar_x(cx), esp_secret_vbar_y(cy), ESP_SECRET_T, esp_secret_bar_span(), colour);
  if (e.name)
    esp_draw_label(esp_label_x(cx, 0, canvas_text_width(e.name, FONT_SMALL)), esp_secret_name_label_y(cy),
                e.name, colour, FONT_SMALL);
  const float dm = esp_dist_m(e.pos.x - playerPos.x, e.pos.z - playerPos.z);
  char buf[24];
  sprintf_s(buf, "%d m", clamp_double_to_int((double)dm + 0.5));
  esp_draw_label(esp_label_x(cx, 0, canvas_text_width(buf, FONT_SMALL)), esp_secret_dist_label_y(cy),
              buf, C_BONEDIM, FONT_SMALL);
}

// Explodables: AA's hazard-yellow x with its keyline, the label above and no distance (AA's esp_draw_crack_marker).
// Two 1 px strokes per diagonal give the 2 px weight; the keyline is the same strokes at the 8 outline offsets.
static void draw_explodable_marker(const EkMarker& m, const EntSnap& e) {
  const unsigned colour = marker_colour(e.cat, e.keyColour);
  const EspSaltire s = esp_saltire(m.p.x, m.p.y, ESP_SECRET_ARM);
  int dx[8], dy[8];
  const int n = esp_outline_offsets(1, dx, dy);
  const unsigned kc = esp_keyline_colour(colour);
  for (int pass = 0; pass < 2; ++pass)
    for (int k = 0; k < (pass ? 2 : n); ++k) {
      const int ox = pass ? k : dx[k], oy = pass ? 0 : dy[k];
      const unsigned c = pass ? colour : kc;
      canvas_line(s.x0 + ox, s.y0 + oy, s.x1 + ox, s.y1 + oy, c);
      canvas_line(s.x2 + ox, s.y2 + oy, s.x3 + ox, s.y3 + oy, c);
    }
  if (e.name)
    esp_draw_label(esp_label_x(m.p.x, 0, canvas_text_width(e.name, FONT_SMALL)), esp_secret_name_label_y(m.p.y),
                   e.name, colour, FONT_SMALL);
}

// An ambush: orange brackets round its spawners while it waits, red while a laser can fire it again; the label above
// and the distance below, as a nuke's.
static void draw_ambush_marker(const EkMarker& m, const EntSnap& e, Vec3 playerPos) {
  const int bx = m.box.x, by = m.box.y, bw = m.box.w, bh = m.box.h;
  const unsigned colour = e.kind == AMB_MARK_ARMED ? C_RED : C_AMBUSH;
  draw_brackets(bx, by, bw, bh, colour);
  if (e.name)
    esp_draw_label(esp_label_x(bx, bw, canvas_text_width(e.name, FONT_SMALL)), esp_name_label_y(by), e.name, colour,
                   FONT_SMALL);
  const float dm = esp_dist_m(e.pos.x - playerPos.x, e.pos.z - playerPos.z);
  char buf[24];
  sprintf_s(buf, "%d m", clamp_double_to_int((double)dm + 0.5));
  esp_draw_label(esp_label_x(bx, bw, canvas_text_width(buf, FONT_SMALL)), esp_dist_label_y(by, bh), buf, C_BONEDIM,
                 FONT_SMALL);
}

// w2s_project's own `ok` means only "in front of the near plane" (w2s.h), not
// "inside the screen": on-screen only for markers (Global Constraints), so a
// point category also checks its projected pixel lands in [0,W]x[0,H].
static bool on_screen(float x, float y, int W, int H) {
  return x >= 0.f && x <= (float)W && y >= 0.f && y <= (float)H;
}

// One gate per category, so an entry that fails it is SKIPPED -- never
// counted as offscreen or inactive, which would double up a bucket.
static bool category_wanted(unsigned char cat) {
  switch (cat) {
    case CAT_ENEMY: case CAT_BOSS: return g_cfg.espOn != 0;
    case CAT_ITEM: case CAT_KEY:   return g_cfg.espItems != 0;   // keycards always show with items
    case CAT_NUKE:                 return g_cfg.espNukes != 0;
    case CAT_BABE:                 return g_cfg.espBabes != 0;
    case CAT_SECRET:               return g_cfg.espSecrets != 0;
    case CAT_EXPLODABLE:           return g_cfg.espExplodables != 0;
    case CAT_AMBUSH:               return g_cfg.espOn != 0 && g_cfg.espAmbushes != 0;
    default:                       return false;
  }
}

// The arrow sub-row; its category's own switch has already passed.
static bool arrow_wanted(unsigned char cat) {
  return arrow_wanted_cat(cat, g_cfg.espNukesArrow != 0, g_cfg.espBabesArrow != 0, g_cfg.espSecretsArrow != 0);
}

// Category colour with a 1 px C_BEVELLO outline so it reads on a bright wall;
// the distance is the player's, as on the markers.
static void draw_edge_arrow(const EdgeArrow& a, const EntSnap& e, Vec3 playerPos, bool panelOpen,
                            const UiRect& panel) {
  const float dm = esp_dist_m(e.pos.x - playerPos.x, e.pos.z - playerPos.z);
  char buf[24];
  sprintf_s(buf, "%d m", clamp_double_to_int((double)dm + 0.5));
  const int tw = canvas_text_width(buf, FONT_SMALL), th = font_height[FONT_SMALL];
  if (arrow_suppressed(arrow_bounds(a, tw, th), panelOpen, panel)) { s_arrowsPanel++; return; }
  float px[3], py[3];
  arrow_triangle(a, px, py);
  int ix[3], iy[3];
  for (int k = 0; k < 3; ++k) {
    ix[k] = clamp_double_to_int((double)px[k] + 0.5);
    iy[k] = clamp_double_to_int((double)py[k] + 0.5);
  }
  canvas_tri(ix[0], iy[0], ix[1], iy[1], ix[2], iy[2], marker_colour(e.cat, e.keyColour));
  for (int k = 0; k < 3; ++k) canvas_line(ix[k], iy[k], ix[(k + 1) % 3], iy[(k + 1) % 3], C_BEVELLO);
  int lx, ly;
  arrow_label_pos(a, tw, th, &lx, &ly);
  esp_draw_label(lx, ly, buf, C_BONEDIM, FONT_SMALL);
  s_arrowsDrawn++;
}

// Every area_secret's box, faces then edges, under the markers. On-screen only;
// independent of the Secrets cross, as its row is.
static void draw_secret_volumes(const EntSnapshot& snap, const Mat4& view, const Mat4& proj, float nearZ,
                                int W, int H) {
  const unsigned edge = canvas_with_alpha(C_HAZARD, 0x80);
  for (int i = 0; i < snap.count; ++i) {
    const EntSnap& e = snap.ents[i];
    if (e.cat != CAT_SECRET || ent_secret_found(e)) continue;
    VolumeGeom g;
    // The engine's own volume as its (possibly rotated) box; without it, the old +0x18C box.
    if (e.obb) volume_project_axes(e.pos, e.axes, view, proj, nearZ, W, H, &g);
    else volume_project(e.pos, e.half, view, proj, nearZ, W, H, &g);
    if (!g.onScreen) continue;
    int ix[8], iy[8];
    for (int k = 0; k < 8; ++k) {
      ix[k] = clamp_double_to_int((double)g.c[k].x + 0.5);
      iy[k] = clamp_double_to_int((double)g.c[k].y + 0.5);
    }
    for (int f = 0; f < 6; ++f) {
      if (!g.faceOk[f]) continue;
      const unsigned char* q = VOL_FACE[f];
      canvas_tri(ix[q[0]], iy[q[0]], ix[q[1]], iy[q[1]], ix[q[2]], iy[q[2]], C_SECRET_VOLUME);
      canvas_tri(ix[q[0]], iy[q[0]], ix[q[2]], iy[q[2]], ix[q[3]], iy[q[3]], C_SECRET_VOLUME);
    }
    for (int k = 0; k < 12; ++k)
      if (g.edgeOk[k])
        canvas_line(ix[VOL_EDGE[k][0]], iy[VOL_EDGE[k][0]], ix[VOL_EDGE[k][1]], iy[VOL_EDGE[k][1]], edge);
    s_volumes++;
    s_volFaces += (unsigned)g.faces;
  }
}

// The zones that start a waiting ambush: their boxes in orange, faces then edges, under the markers, as the secret
// volumes draw.
static void draw_ambush_zones(const EntSnapshot& snap, const Mat4& view, const Mat4& proj, float nearZ, int W, int H) {
  const unsigned face = canvas_with_alpha(C_AMBUSH, 0x30), edge = canvas_with_alpha(C_AMBUSH, 0xC0);
  for (int i = 0; i < snap.count; ++i) {
    const EntSnap& e = snap.ents[i];
    if (e.cat != CAT_AMBUSH || e.kind != AMB_MARK_ZONE) continue;
    VolumeGeom g;
    volume_project(e.pos, e.half, view, proj, nearZ, W, H, &g);
    if (!g.onScreen) continue;
    int ix[8], iy[8];
    for (int k = 0; k < 8; ++k) {
      ix[k] = clamp_double_to_int((double)g.c[k].x + 0.5);
      iy[k] = clamp_double_to_int((double)g.c[k].y + 0.5);
    }
    for (int f = 0; f < 6; ++f) {
      if (!g.faceOk[f]) continue;
      const unsigned char* q = VOL_FACE[f];
      canvas_tri(ix[q[0]], iy[q[0]], ix[q[1]], iy[q[1]], ix[q[2]], iy[q[2]], face);
      canvas_tri(ix[q[0]], iy[q[0]], ix[q[2]], iy[q[2]], ix[q[3]], iy[q[3]], face);
    }
    for (int k = 0; k < 12; ++k)
      if (g.edgeOk[k])
        canvas_line(ix[VOL_EDGE[k][0]], iy[VOL_EDGE[k][0]], ix[VOL_EDGE[k][1]], iy[VOL_EDGE[k][1]], edge);
    s_ambZones++;
  }
}

static void esp_draw(int W, int H) {
  const EntSnapshot& snap = entities_snapshot();
  if (!snap.player || snap.count <= 0) return;
  Mat4 view, proj;
  float nearZ;
  if (!view_get(&view, &proj, &nearZ)) { s_noview++; return; }
  s_frames++;
  s_entries += (unsigned)snap.count;

  int count = 0, seq = 0, narrows = 0;
  bool zones = false;   // an ambush zone's box may show while its centre is off screen
  const float margin = arrow_margin(canvas_scale());
  for (int i = 0; i < snap.count && count < EK_MARKER_COLLECT_MAX; ++i) {
    const EntSnap& e = snap.ents[i];
    if (!category_wanted(e.cat)) { s_skipped++; continue; }

    if (e.cat == CAT_ENEMY || e.cat == CAT_BOSS) {
      if (!ent_alive(e)) { s_dead++; continue; }
    } else if (e.cat == CAT_ITEM || e.cat == CAT_KEY || e.cat == CAT_NUKE) {
      if (ent_crate_opened(e)) { s_opened++; continue; }
      if (!e.active) { s_inactive++; continue; }
      // Keys have no refusal (key 0x1005f770), so only items are asked; an unjudged one is shown.
      if (e.cat == CAT_ITEM && g_cfg.espHideTaken) {
        const int take = pickup_can_take(e.kind, e.subtype, &snap.pools);
        if (item_hidden_by_take(g_cfg.espHideTaken, take)) { item_note(e, "cantTake", &snap.pools); s_cantTake++; continue; }
        if (take == TAKE_UNKNOWN) s_takeUnknown++;
      }
    } else if (e.cat == CAT_EXPLODABLE) {
      if (!e.active) { s_broken++; continue; }   // hidden once destroyed
      if (!ent_explodable_shown(e)) { s_dormant++; continue; }
    } else if (ent_secret_found(e)) {
      s_found++;
      continue;
    } else if (ent_babe_freed(e)) {   // no marker and no arrow once freed, as a found secret
      s_freed++;
      continue;
    }

    // Off screen: an arrow instead, for the categories whose sub-row asks for one. Enemies have no arrow switch, so a
    // waiting ambush always takes one while Ambushes is on.
    const bool zone = e.cat == CAT_AMBUSH && e.kind == AMB_MARK_ZONE;
    zones = zones || zone;
    auto offscreen = [&]() {
      s_offscreen++;
      if (e.cat == CAT_ITEM) { s_itemsOff++; item_note(e, "offscreen", &snap.pools); }
      if (!arrow_wanted(e.cat) && !(e.cat == CAT_AMBUSH && e.kind == AMB_MARK_WAIT)) return;
      if (narrows >= ARROW_MAX) s_arrowsCapped++;
      else if (arrow_make(e.pos, view, proj, nearZ, W, H, margin, i, &s_arrows[narrows])) narrows++;
    };
    EkScreenBox box = { 0, 0, 0, 0 };
    int px, py;
    if (e.cat != CAT_SECRET && e.cat != CAT_EXPLODABLE && !zone) {
      int ok = 0;
      box = esp_project_box(e.hb.c, e.hb.half, view, proj, nearZ, W, H, &ok);
      if (!ok) { offscreen(); continue; }
      px = box.x + box.w / 2; py = box.y + box.h / 2;
    } else {
      const ScreenPt sp = w2s_project(e.pos, view, proj, nearZ, W, H);
      if (!sp.ok || !on_screen(sp.x, sp.y, W, H)) { offscreen(); continue; }
      px = clamp_double_to_int((double)sp.x + 0.5);
      py = clamp_double_to_int((double)sp.y + 0.5);
    }
    // No draw-distance test (side scroller): selection is nearest to the
    // camera eye, so this is view-space z from the captured view, not player distance.
    const float wp[4] = { e.pos.x, e.pos.y, e.pos.z, 1.f };
    float v[4];
    w2s_mul(wp, view, v);
    const int dist2d = clamp_double_to_int((double)v[2] + 0.5);
    EkMarker& m = s_markers[count++];
    m.kind = EK_MARKER_ENEMY; m.index = i; m.dist2d = dist2d; m.seq = seq++;
    m.p.x = px; m.p.y = py; m.p.dist2d = dist2d;
    m.box = box;
  }
  // A sub-row of Secret areas (AA's nesting):
  // gated on both, so turning the parent off also turns this off.
  const bool volumes = g_cfg.espSecrets != 0 && g_cfg.espSecretVolumes != 0 && snap.cats[CAT_SECRET] > 0;
  if (count == 0 && narrows == 0 && !volumes && !zones) return;

  // Select, clamp, order, in that order: a clamp first would keep the first
  // collected, not the nearest. The 256-marker cap is shared across every
  // category (Global Constraints), not per kind.
  ekm_keep_nearest(s_markers, count, EK_MARKER_MAX);
  const int capped = count > EK_MARKER_MAX ? count - EK_MARKER_MAX : 0;
  count -= capped;
  s_capped += (unsigned)capped;
  ekm_sort_far_to_near(s_markers, count);

  arrows_fan_out(s_arrows, narrows, W, H, margin);
  s_arrows_n += (unsigned)narrows;

  canvas_begin(W, H);
  if (volumes) draw_secret_volumes(snap, view, proj, nearZ, W, H);
  if (zones) draw_ambush_zones(snap, view, proj, nearZ, W, H);
  for (int i = 0; i < count; ++i) {
    const EntSnap& e = snap.ents[s_markers[i].index];
    if (e.cat == CAT_ENEMY || e.cat == CAT_BOSS) draw_marker(s_markers[i], e, snap.playerPos);
    else if (e.cat == CAT_ITEM || e.cat == CAT_KEY) draw_pickup_marker(s_markers[i], e);
    else if (e.cat == CAT_NUKE && e.carry) draw_carry_marker(s_markers[i], e, snap.playerPos);
    else if (e.cat == CAT_NUKE || e.cat == CAT_BABE) draw_objective_marker(s_markers[i], e, snap.playerPos);
    else if (e.cat == CAT_EXPLODABLE) draw_explodable_marker(s_markers[i], e);
    else if (e.cat == CAT_AMBUSH && e.kind != AMB_MARK_ZONE) draw_ambush_marker(s_markers[i], e, snap.playerPos);
    else draw_point_marker(s_markers[i], e, snap.playerPos);
  }
  if (narrows) {
    // The panel draws after the ESP, closing animation included.
    const bool panelOpen = input_panel_open() || menu_closing_animation_active();
    const UiRect panel = menu_panel_rect(W, H);
    for (int i = 0; i < narrows; ++i)
      draw_edge_arrow(s_arrows[i], snap.ents[s_arrows[i].tag], snap.playerPos, panelOpen, panel);
  }
  canvas_end();
  s_drawn += (unsigned)count;
}

void esp_frame(int W, int H) {
  esp_draw(W, H);
  puzzle_frame(W, H);   // its own canvas pass: esp_draw returns early on a frame with no markers
  const ULONGLONG tick = GetTickCount64();
  if (tick < s_next_log) return;
  s_next_log = tick + 5000;
  if (s_frames || s_noview) {
    np_log("esp: frames=%u noview=%u entries=%u drawn=%u capped=%u offscreen=%u dead=%u skipped=%u inactive=%u opened=%u"
           " found=%u freed=%u broken=%u dormant=%u cantTake=%u takeUnknown=%u arrows=%u arrowsDrawn=%u arrowsPanel=%u arrowsCapped=%u volumes=%u volFaces=%u itemsOff=%u ambZones=%u",
           s_frames, s_noview, s_entries, s_drawn, s_capped, s_offscreen, s_dead, s_skipped, s_inactive, s_opened, s_found,
           s_freed,
           s_broken, s_dormant, s_cantTake, s_takeUnknown, s_arrows_n, s_arrowsDrawn, s_arrowsPanel, s_arrowsCapped, s_volumes, s_volFaces, s_itemsOff, s_ambZones);
  }
  s_itemsOff = 0;
  s_ambZones = 0;
  s_frames = s_noview = s_entries = s_drawn = s_capped = s_offscreen = s_dead = s_skipped = s_inactive = s_found = 0;
  s_freed = s_broken = s_dormant = s_cantTake = s_takeUnknown = s_opened = 0;
  s_arrows_n = s_arrowsDrawn = s_arrowsPanel = s_arrowsCapped = s_volumes = s_volFaces = 0;
}
