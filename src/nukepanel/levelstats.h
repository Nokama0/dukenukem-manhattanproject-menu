#pragma once
// In-level stats readout (evidence re/levels-and-stats.md section 4):
// "Kills N/M", "Secrets N/M", "Nukes N/M" for the current part and "Hard nukes N/240", top left.
// The rules are pure for the suite; levelstats.cpp reads the engine and draws.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LS_EPISODES = 20, LS_PARTS = 5, LS_DIFFS = 3, LS_HARD = 2, LS_LINES = 4, LS_LINE_CAP = 40 };

// map_name_parse_epa duke!0x100e9270 [A]: the first 10 chars, cut at 10, 6 and 3; episode = atoi(chars 1-2) - 1,
// part = atoi(chars 4-5) - 1, each clamped at 0. So "e01_intro" is E1P1 and "e03p03_a03_r01" is E3P3.
inline void ls_map_episode_part(const char* name, int* ep, int* part) {
  char b[11] = { 0 };
  if (name) strncpy_s(b, sizeof b, name, _TRUNCATE);
  const size_t n = strlen(b);
  int p = -1, e = -1;
  if (n >= 6) { b[6] = 0; p = atoi(b + 4) - 1; }
  if (n >= 3) { b[3] = 0; e = atoi(b + 1) - 1; }
  if (ep) *ep = e < 0 ? 0 : e;
  if (part) *part = p < 0 ? 0 : p;
}

// scripts/episodes.def as the engine reads it (partend_stats_text 0x100a9000, session_stats_sum 0x100eb540):
// `def episodeNN { key:value ... }` with killcountPP, extrakillsPPDD (part, difficulty), secretcountPP, nukecountPP,
// partcount, title (a local.def key) and partPP (first map). A missing key reads 0, as def_obj_t::get_int does.
enum { LS_NAME_CAP = 24 };
struct LsDef {
  int seen[LS_EPISODES];
  int partcount[LS_EPISODES];
  int kills[LS_EPISODES][LS_PARTS];
  int extra[LS_EPISODES][LS_PARTS][LS_DIFFS];
  int secrets[LS_EPISODES][LS_PARTS];
  int nukes[LS_EPISODES][LS_PARTS];
  char title[LS_EPISODES][LS_NAME_CAP];
  char partMap[LS_EPISODES][LS_PARTS][LS_NAME_CAP];
};

// "killcount01" with prefix "killcount": true and the digits after it (2 or 4 of them).
inline bool ls_key_digits(const char* key, const char* prefix, int digits, int* a, int* b) {
  const size_t n = strlen(prefix);
  if (strncmp(key, prefix, n) != 0 || strlen(key) != n + (size_t)digits) return false;
  for (int i = 0; i < digits; ++i)
    if (key[n + i] < '0' || key[n + i] > '9') return false;
  *a = (key[n] - '0') * 10 + (key[n + 1] - '0');
  if (digits == 4) *b = (key[n + 2] - '0') * 10 + (key[n + 3] - '0');
  return true;
}

inline void ls_def_key(LsDef* d, int ep, const char* key, int v) {
  int p = 0, q = 0;
  if (strcmp(key, "partcount") == 0) d->partcount[ep] = v;
  else if (ls_key_digits(key, "killcount", 2, &p, &q)) { if (p < LS_PARTS) d->kills[ep][p] = v; }
  else if (ls_key_digits(key, "secretcount", 2, &p, &q)) { if (p < LS_PARTS) d->secrets[ep][p] = v; }
  else if (ls_key_digits(key, "nukecount", 2, &p, &q)) { if (p < LS_PARTS) d->nukes[ep][p] = v; }
  else if (ls_key_digits(key, "extrakills", 4, &p, &q)) { if (p < LS_PARTS && q < LS_DIFFS) d->extra[ep][p][q] = v; }
}

// The string keys: the value cut at a comment, its blanks and CR trimmed.
inline void ls_def_str(LsDef* d, int ep, const char* key, const char* v) {
  int p = 0, q = 0;
  char* out = 0;
  if (strcmp(key, "title") == 0) out = d->title[ep];
  else if (ls_key_digits(key, "part", 2, &p, &q) && p < LS_PARTS) out = d->partMap[ep][p];
  if (!out) return;
  while (*v == ' ' || *v == '\t') ++v;
  int n = 0;
  while (v[n] && v[n] != '#' && n < LS_NAME_CAP - 1) { out[n] = v[n]; ++n; }
  while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t' || out[n - 1] == '\r')) --n;
  out[n] = 0;
}

// The episode blocks of the text; false when it holds none.
inline bool ls_parse_def(const char* text, int len, LsDef* d) {
  memset(d, 0, sizeof *d);
  int ep = -1, found = 0;
  for (int i = 0; i < len;) {
    int j = i;
    while (j < len && text[j] != '\n') ++j;
    char line[160];
    int n = j - i < (int)sizeof line - 1 ? j - i : (int)sizeof line - 1;
    memcpy(line, text + i, (size_t)n);
    line[n] = 0;
    i = j + 1;
    char* hash = strchr(line, '#');
    if (hash) *hash = 0;
    char* s = line;
    while (*s == ' ' || *s == '\t') ++s;
    if (strncmp(s, "def ", 4) == 0) {
      s += 4;
      while (*s == ' ' || *s == '\t') ++s;
      int e = -1;
      if (strncmp(s, "episode", 7) == 0 && s[7] >= '0' && s[7] <= '9') e = atoi(s + 7);
      ep = e >= 0 && e < LS_EPISODES ? e : -1;
      if (ep >= 0 && !d->seen[ep]) { d->seen[ep] = 1; found++; }
      continue;
    }
    if (*s == '}') { ep = -1; continue; }
    char* colon = strchr(s, ':');
    if (ep < 0 || !colon) continue;
    *colon = 0;
    char* k = colon;
    while (k > s && (k[-1] == ' ' || k[-1] == '\t')) *--k = 0;
    ls_def_key(d, ep, s, atoi(colon + 1));
    ls_def_str(d, ep, s, colon + 1);
  }
  return found > 0;
}

// The part-end screen's totals for (ep, part, difficulty): kills = killcount + extrakills[difficulty].
struct LsTotals { int kills, secrets, nukes; };
inline bool ls_totals(const LsDef* d, int ep, int part, int diff, LsTotals* t) {
  if (!d || ep < 0 || ep >= LS_EPISODES || part < 0 || part >= LS_PARTS || !d->seen[ep]) return false;
  t->kills = d->kills[ep][part] + (diff >= 0 && diff < LS_DIFFS ? d->extra[ep][part][diff] : 0);
  t->secrets = d->secrets[ep][part];
  t->nukes = d->nukes[ep][part];
  return true;
}

// Every part's nukecount, episodes from 0 while defined, parts below partcount: session_stats_sum's total, the
// 240 player_add_nukes compares the banked Hard nukes against.
inline int ls_game_nukes(const LsDef* d) {
  int sum = 0;
  for (int e = 0; e < LS_EPISODES && d->seen[e]; ++e)
    for (int p = 0; p < d->partcount[e] && p < LS_PARTS; ++p) sum += d->nukes[e][p];
  return sum;
}

// The banked record of (diff, ep, part): S + 0x2B8 + index * 0x15C (levels-and-stats.md section 4).
inline int ls_record_index(int diff, int ep, int part) { return (diff * LS_EPISODES + ep) * LS_PARTS + part; }

// Hard nukes toward the Wozma path: the 100 banked Hard records' nukes, with the current part's record counted as
// max(banked, live) while on Hard. A part's record keeps the best of each field (stats_merge_best), so on a first
// run this adds the live count and on a replay it never counts the part twice. Floats truncated as ftol does.
inline int ls_hard_nukes(const float* bankedHard, int n, int onHard, int cur, float live) {
  double sum = 0.0;
  for (int i = 0; i < n; ++i) {
    float v = bankedHard[i];
    if (onHard && i == cur && live > v) v = live;
    sum += v;
  }
  return (int)sum;
}

// The live values the readout shows (the live record +0x48 nukes, +0x4C secrets, +0x50 kills).
struct LsLive { float kills, secrets, nukes; };

// One line: "Kills 3/29". The denominator is max(total, found) as on the part-end screen, except for nukes (raw):
// those read over the game's listed total, so a laser that fired again shows as 11/10. No total, no "/M".
inline void ls_line(char* out, int cap, const char* label, int found, int total, int haveTotal, bool raw = false) {
  if (haveTotal)
    _snprintf_s(out, (size_t)cap, _TRUNCATE, "%s %d/%d", label, found, raw || total > found ? total : found);
  else _snprintf_s(out, (size_t)cap, _TRUNCATE, "%s %d", label, found);
}

// The four lines in order: kills, secrets, nukes, Hard nukes. hardTotal 0 = unknown.
inline int ls_lines(const LsLive& live, const LsTotals* t, int hardNukes, int hardTotal, char out[LS_LINES][LS_LINE_CAP]) {
  const int have = t != 0;
  ls_line(out[0], LS_LINE_CAP, "Kills", (int)live.kills, have ? t->kills : 0, have);
  ls_line(out[1], LS_LINE_CAP, "Secrets", (int)live.secrets, have ? t->secrets : 0, have);
  ls_line(out[2], LS_LINE_CAP, "Nukes", (int)live.nukes, have ? t->nukes : 0, have, true);
  ls_line(out[3], LS_LINE_CAP, "Hard nukes", hardNukes, hardTotal, hardTotal > 0, true);
  return LS_LINES;
}

// Shown only with the switch on, in a level, outside screenshot mode, with the panel shut (its closing slide
// included) and no menu, console or level-end screen up.
inline bool ls_visible(int switchOn, int inLevel, int shot, int panel, int menu) {
  return switchOn && inLevel && !shot && !panel && !menu;
}

// Top left, left-aligned LS_MARGIN from the edge. The widescreen HUD (db_wide's scr_hud.sc, 768 tall) centres a
// 1024x768 group whose top band holds the messages (y 2..~50), boss bar (y 10..50) and boss icon (x 32..96, y 10..74).
// Lines ending LS_MARGIN short of that group start at the top edge, else under the icon; drawn over the arrows' corner.
enum { LS_MARGIN = 8, LS_HUD_ICON_BOTTOM = 74, LS_HUD_REF_H = 768 };
inline void ls_corner_top_left(int W, int H, int textW, int* x, int* y) {
  const int groupLeft = 3 * W > 4 * H ? (3 * W - 4 * H) / 6 : 0;   // (W - H * 1024 / 768) / 2
  *x = LS_MARGIN;
  *y = LS_MARGIN + textW + LS_MARGIN <= groupLeft
         ? LS_MARGIN
         : LS_MARGIN + (H * LS_HUD_ICON_BOTTOM + LS_HUD_REF_H - 1) / LS_HUD_REF_H;
}

// --- The glue (levelstats.cpp), main thread only -------------------------------------------------------
// Frame path, after entities_frame: the live record every frame while the switch is on or the Progress tab asks
// (every 5 s for the log otherwise); the part's totals, difficulty and banked Hard nukes once per part.
void levelstats_frame(const unsigned char* player);
// The corner lines, over the ESP and under the panel; hidden by ls_visible (screenshot mode included).
void levelstats_draw(int W, int H);

// For the Progress tab (progress.cpp): the session S (the live record's base), the current part and difficulty
// and the live counts; false with no player. levelstats_demand keeps the frame path reading them for 1.5 s while
// the switch is off.
struct LsSession { const unsigned char* S; int ep, part, diff, liveOk; LsLive live; };
bool levelstats_session(LsSession* out);
void levelstats_demand(void);
// The parsed episodes.def, or null until it has been read.
const LsDef* levelstats_def(void);
