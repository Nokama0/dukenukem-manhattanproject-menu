// In-level stats readout glue (levelstats.h holds the rules). Main thread, frame path. Found counts: the live record
// at the player's +0x111C (S+0x223D4, docs/re/levels-and-stats.md section 4). Totals: scripts/episodes.def through the
// engine VFS; its parsed copy sits behind game_globals (0x101869dc) and a lazy loader, so reaching it takes an offset.
#include "levelstats.h"
#include "engine.h"
#include "canvas.h"
#include "esp_layout.h"   // esp_outline_offsets, esp_keyline_colour
#include "font.h"
#include "config.h"
#include "input.h"
#include "shot.h"
#include "ui/menu.h"      // menu_closing_animation_active
#include "gamespeed.h"    // gamespeed_tag: the Speed tag beside the lines
#include "log.h"
#include <windows.h>
#include <string.h>
#include <math.h>

// Live record offsets (levels-and-stats.md section 4) and the session record around it.
static const unsigned kPlayerStats = 0x111C, kWorldName = 0x48;
static const unsigned kLiveInSession = 0x223D4, kSessionDiff = 0x20, kBankBase = 0x2B8, kRecSize = 0x15C;
static const unsigned kNukes = 0x48, kSecrets = 0x4C, kKills = 0x50, kLiveSpan = 0x60;
static const char kDefPath[] = "scripts/episodes.def";
static const ULONGLONG kLogMs = 5000;

static LsDef s_def;
static int s_defOk;
static const unsigned char* s_player;
static const void* s_world;
static const unsigned char* s_live;
static int s_ep, s_part, s_diff = -1, s_haveTotals, s_hardTotal, s_bankOk;
static LsTotals s_totals;
static float s_bankHard[LS_EPISODES * LS_PARTS];
static char s_map[64];

static LsLive s_now;
static int s_liveOk;
static ULONGLONG s_nextLog, s_demandUntil;
static unsigned s_stages, s_defReads, s_unreadable, s_drawn, s_hidShot, s_hidPanel, s_hidMenu;

static bool rd_u32(const void* p, unsigned* out) {
  if (!p || !guarded_readable(p, 4)) return false;
  memcpy(out, p, 4);
  return true;
}
static bool sane(float v) { return v == v && v >= 0.f && v < 100000.f; }

// The whole def through the VFS (the mount priorities pick the patched copy, as for the engine's own load).
static void read_def(void) {
  s_defReads++;
  s_defOk = 0;
  if (!g_eng.vfs_open || !g_eng.vfs_read || !g_eng.vfs_close) return;
  void* f = g_eng.vfs_open(kDefPath, VFS_O_RDONLY, VFS_MODE_RW);
  if (!f) { np_log("stats: %s does not open", kDefPath); return; }
  static char buf[16384];
  int len = 0;
  for (;;) {
    const int n = g_eng.vfs_read(f, buf + len, (unsigned)(sizeof buf - 1 - len));
    if (n <= 0) break;
    len += n;
    if (len >= (int)sizeof buf - 1) break;
  }
  g_eng.vfs_close(f);
  buf[len] = 0;
  s_defOk = ls_parse_def(buf, len, &s_def);
  s_hardTotal = s_defOk ? ls_game_nukes(&s_def) : 0;
  if (!s_defOk) np_log("stats: %s read %d bytes, no episode block", kDefPath, len);
}

// Once per part (a new world or live record): the map's episode and part, the difficulty, the totals and the
// banked Hard records.
static void stage(const void* world) {
  s_stages++;
  s_world = world;
  s_map[0] = 0;
  const char* name = (const char*)world + kWorldName;
  if (world && guarded_readable(name, sizeof s_map) && memchr(name, 0, sizeof s_map)) strcpy_s(s_map, sizeof s_map, name);
  ls_map_episode_part(s_map, &s_ep, &s_part);
  const unsigned char* S = s_live ? s_live - kLiveInSession : 0;
  unsigned d = 0;
  s_diff = S && rd_u32(S + kSessionDiff, &d) && d < LS_DIFFS ? (int)d : -1;
  read_def();
  s_haveTotals = s_defOk && ls_totals(&s_def, s_ep, s_part, s_diff, &s_totals);
  s_bankOk = 0;
  const unsigned char* bank = S ? S + kBankBase + (size_t)ls_record_index(LS_HARD, 0, 0) * kRecSize : 0;
  if (bank && guarded_readable(bank, (size_t)LS_EPISODES * LS_PARTS * kRecSize)) {
    for (int i = 0; i < LS_EPISODES * LS_PARTS; ++i) {
      float v;
      memcpy(&v, bank + (size_t)i * kRecSize + kNukes, sizeof v);
      s_bankHard[i] = sane(v) ? v : 0.f;
    }
    s_bankOk = 1;
  }
  np_log("stats: staged map \"%s\" E%d P%d, difficulty %d, live %p, def %s, totals %s (kills %d secrets %d nukes %d), "
         "game nukes %d, banked Hard %s (stage %u)", s_map, s_ep + 1, s_part + 1, s_diff, (const void*)s_live,
         s_defOk ? "parsed" : "unread", s_haveTotals ? "found" : "none", s_totals.kills, s_totals.secrets,
         s_totals.nukes, s_hardTotal, s_bankOk ? "read" : "unread", s_stages);
}

static void read_live(void) {
  s_liveOk = 0;
  if (!s_live || !guarded_readable(s_live, kLiveSpan)) { s_unreadable++; return; }
  LsLive v;
  memcpy(&v.nukes, s_live + kNukes, 4);
  memcpy(&v.secrets, s_live + kSecrets, 4);
  memcpy(&v.kills, s_live + kKills, 4);
  if (!sane(v.nukes) || !sane(v.secrets) || !sane(v.kills)) { s_unreadable++; return; }
  s_now = v;
  s_liveOk = 1;
}

static int hard_nukes(void) {
  const int cur = s_ep < LS_EPISODES && s_part < LS_PARTS ? s_ep * LS_PARTS + s_part : -1;
  return s_bankOk ? ls_hard_nukes(s_bankHard, LS_EPISODES * LS_PARTS, s_diff == LS_HARD, cur, s_liveOk ? s_now.nukes : 0.f)
                  : (s_diff == LS_HARD && s_liveOk ? (int)s_now.nukes : 0);
}

void levelstats_frame(const unsigned char* player) {
  const ULONGLONG now = GetTickCount64();
  const bool logDue = now >= s_nextLog;
  if (!player) { s_player = 0; s_world = 0; s_live = 0; s_liveOk = 0; }
  else if (g_cfg.levelStats || logDue || now < s_demandUntil) {
    const void* world = 0;
    memcpy(&world, player + 0xE0, sizeof world);   // inside the span probe.cpp validated
    if (player != s_player || world != s_world) {
      // +0x111C lies past that span: read guarded, once per player and world (FUN_100763c0 sets it at spawn).
      unsigned live = 0;
      s_player = player;
      s_live = rd_u32(player + kPlayerStats, &live) ? (const unsigned char*)(size_t)live : 0;
      stage(world);
    }
    read_live();
  }
  if (!logDue) return;
  s_nextLog = now + kLogMs;
  if (!player && !s_drawn) return;
  np_log("stats: E%d P%d difficulty %d, live %s kills %.1f secrets %.1f nukes %.1f; totals %s kills %d secrets %d "
         "nukes %d; Hard nukes %d/%d (banked %s); switch %d, drawn %u, hidden: shot %u panel %u menu %u, "
         "unreadable %u, def reads %u", s_ep + 1, s_part + 1, s_diff, s_liveOk ? "read" : "unread", s_now.kills,
         s_now.secrets, s_now.nukes, s_haveTotals ? "found" : "none", s_totals.kills, s_totals.secrets, s_totals.nukes,
         hard_nukes(), s_hardTotal, s_bankOk ? "read" : "unread", g_cfg.levelStats, s_drawn, s_hidShot,
         s_hidPanel, s_hidMenu, s_unreadable, s_defReads);
  s_drawn = s_hidShot = s_hidPanel = s_hidMenu = s_unreadable = 0;
}

static void corner_line(int x, int y, const char* s, unsigned fill) {
  int dx[8], dy[8];
  const int n = esp_outline_offsets(ESP_LABEL_OUTLINE_R, dx, dy);
  const unsigned key = esp_keyline_colour(fill);
  for (int i = 0; i < n; ++i) canvas_text(x + dx[i], y + dy[i], s, key, FONT_UI);
  canvas_text(x, y, s, fill, FONT_UI);
}

void levelstats_draw(int W, int H) {
  // Game speed's tag shares the corner and its hiding rules; in a level means a player this frame (s_player).
  char tag[16];
  const bool speed = gamespeed_tag(tag, sizeof tag);
  if (!g_cfg.levelStats && !speed) return;
  const int inLevel = s_world && s_liveOk;
  if (!(g_cfg.levelStats && inLevel) && !(speed && s_player)) return;
  const int shot = shot_hides_overlays() ? 1 : 0;
  const int panel = input_panel_open() || menu_closing_animation_active();
  const int menu = input_console_open() || input_ui_window("screen", "menu_main") ||
                   input_ui_window("screen", "menu_help") || input_ui_window("system_game2", "partend");
  if (!ls_visible(1, 1, shot, panel, menu)) {
    if (shot) s_hidShot++; else if (panel) s_hidPanel++; else s_hidMenu++;
    return;
  }
  if (!(g_cfg.levelStats && inLevel)) {
    int x = 0, y = 0;
    ls_corner_top_left(W, H, canvas_text_width(tag, FONT_UI) + 2 * ESP_LABEL_OUTLINE_R, &x, &y);
    canvas_begin(W, H);
    corner_line(x, y, tag, C_HAZARD);
    canvas_end();
    return;
  }
  char lines[LS_LINES][LS_LINE_CAP];
  ls_lines(s_now, s_haveTotals ? &s_totals : 0, hard_nukes(), s_hardTotal, lines);
  // Category colours: kills the enemy red, secrets hazard yellow, nukes (both lines) the nuke green.
  static const unsigned kColour[LS_LINES] = { C_RED, C_HAZARD, C_GREEN, C_GREEN };
  const int pitch = font_height[FONT_UI] + 2 * ESP_LABEL_OUTLINE_R;
  int widest = 0;
  for (int i = 0; i < LS_LINES; ++i) {
    const int w = canvas_text_width(lines[i], FONT_UI);
    if (w > widest) widest = w;
  }
  int x = 0, y = 0;
  ls_corner_top_left(W, H, widest + 2 * ESP_LABEL_OUTLINE_R, &x, &y);
  canvas_begin(W, H);
  if (speed) corner_line(x + widest + 2 * ESP_LABEL_OUTLINE_R + LS_MARGIN, y, tag, C_HAZARD);   // beside Kills
  for (int i = 0; i < LS_LINES; ++i, y += pitch) corner_line(x, y, lines[i], kColour[i]);
  canvas_end();
  s_drawn++;
}

bool levelstats_session(LsSession* out) {
  if (!s_player || !s_live || !s_world) return false;
  out->S = s_live - kLiveInSession;
  out->ep = s_ep;
  out->part = s_part;
  out->diff = s_diff;
  out->liveOk = s_liveOk;
  out->live = s_now;
  return true;
}

void levelstats_demand(void) { s_demandUntil = GetTickCount64() + 1500; }

const LsDef* levelstats_def(void) { return s_defOk ? &s_def : 0; }
