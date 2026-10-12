#pragma once
// The Progress tab: every part's banked best of one difficulty, judged against episodes.def.
// Pure for the suite; progress.cpp reads the session, menu.cpp draws.
#include "levelstats.h"
#include <ctype.h>

// partend_commit_stats duke!0x100acef0 (levels-and-stats.md section 4): the record of (diff, ep, part) is at
// S + 0x2B8 + index * 0x15C, its "finished" byte (0 if never) at S + 0x28 + index (index = ls_record_index). The first
// finish copies the live record; stats_merge_best 0x100ea210 keeps the larger nukes +0x48, kills +0x50, secrets +0x4C.
enum { PG_DONE_BASE = 0x28, PG_BANK_BASE = 0x2B8, PG_REC_SIZE = 0x15C,
       PG_REC_NUKES = 0x48, PG_REC_SECRETS = 0x4C, PG_REC_KILLS = 0x50 };
enum { PG_SESSION_SPAN = PG_BANK_BASE + LS_DIFFS * LS_EPISODES * LS_PARTS * PG_REC_SIZE };
inline int pg_record_offset(int diff, int ep, int part) {
  return PG_BANK_BASE + ls_record_index(diff, ep, part) * PG_REC_SIZE;
}
inline int pg_done_offset(int diff, int ep, int part) { return PG_DONE_BASE + ls_record_index(diff, ep, part); }

struct PgCounts { int nukes, secrets, kills; };

// A float field as a count, truncated as the engine's ftol does; false (and 0) for NaN, negative or absurd.
inline bool pg_count(const unsigned char* p, int* out) {
  float v;
  memcpy(&v, p, sizeof v);
  const bool ok = v == v && v >= 0.f && v < 100000.f;
  *out = ok ? (int)v : 0;
  return ok;
}

// rec points at a whole record (PG_REC_SIZE bytes).
inline bool pg_record_decode(const unsigned char* rec, PgCounts* out) {
  const bool n = pg_count(rec + PG_REC_NUKES, &out->nukes);
  const bool s = pg_count(rec + PG_REC_SECRETS, &out->secrets);
  const bool k = pg_count(rec + PG_REC_KILLS, &out->kills);
  return n && s && k;
}

enum { PG_UNPLAYED = 0, PG_INCOMPLETE = 1, PG_COMPLETE = 2 };
struct PgRow { int state, live, haveTotal; PgCounts found, total; };

// One part: `live` non-null makes it the current part, shown with this run's counts and never "not played"; else
// a part with no finished run is unplayed. Complete means every count at its total; with no totals nothing is.
inline PgRow pg_part_row(int done, const PgCounts& banked, const PgCounts* live, const LsTotals* t) {
  PgRow r;
  memset(&r, 0, sizeof r);
  r.haveTotal = t != 0;
  if (t) { r.total.nukes = t->nukes; r.total.secrets = t->secrets; r.total.kills = t->kills; }
  if (!live && !done) { r.state = PG_UNPLAYED; return r; }
  r.live = live != 0;
  r.found = live ? *live : banked;
  const bool all = r.haveTotal && r.found.nukes >= r.total.nukes && r.found.secrets >= r.total.secrets &&
                   r.found.kills >= r.total.kills;
  r.state = all ? PG_COMPLETE : PG_INCOMPLETE;
  return r;
}

// "S 3/5": the denominator max(total, found) as on the part-end screen; "S -/5" not played; "S 3" with no total.
// raw: the total as listed even below the count (the nukes column, as ls_line's nukes).
inline void pg_readout(char* out, int cap, const char* prefix, int found, int total, int haveTotal, int played,
                       bool raw = false) {
  if (!played) _snprintf_s(out, (size_t)cap, _TRUNCATE, haveTotal ? "%s-/%d" : "%s-", prefix, total);
  else if (haveTotal)
    _snprintf_s(out, (size_t)cap, _TRUNCATE, "%s%d/%d", prefix, found, raw || total > found ? total : found);
  else _snprintf_s(out, (size_t)cap, _TRUNCATE, "%s%d", prefix, found);
}

// One readout column of a part or episode row: "" nukes, "S " secrets, "K " kills. Nukes and kills read over the def's
// total, never raised to the count: the Load Game slot raises only the whole game's sums (loadgame_slots_fill
// 0x100a3a4a), so raising per row made them add up to 1157, not the slot's 1136 (save-awards.md section 1).
enum { PG_COL_NUKES, PG_COL_SECRETS, PG_COL_KILLS, PG_COLS };
inline bool pg_column_raw(int col) { return col != PG_COL_SECRETS; }
inline void pg_column_readout(char* out, int cap, int col, const PgCounts& f, const PgCounts& t, int haveTotal,
                              int played) {
  static const char* const kPrefix[PG_COLS] = { "", "S ", "K " };
  const int found[PG_COLS] = { f.nukes, f.secrets, f.kills }, total[PG_COLS] = { t.nukes, t.secrets, t.kills };
  pg_readout(out, cap, kPrefix[col], found[col], total[col], haveTotal, played, pg_column_raw(col));
}

// The tab's whole table for one difficulty.
struct PgView {
  int ok, diff, episodes;
  int parts[LS_EPISODES];
  PgRow rows[LS_EPISODES][LS_PARTS];
  PgCounts epFound[LS_EPISODES], epTotal[LS_EPISODES];   // found: finished parts only; total: every part
  int hardNukes, hardTotal;   // every Hard record's nukes, the part in play as max(banked, live); the def's total
};

// S: PG_SESSION_SPAN readable bytes. Episodes from 0 while the def has them, parts below partcount (as
// ls_game_nukes counts). The current part is live only on the game's own difficulty; the Hard count adds it as the
// corner readout does (ls_hard_nukes) while the game is on Hard, whichever difficulty the tab shows.
inline void pg_build(const unsigned char* S, const LsDef* d, int diff, int curEp, int curPart, int gameDiff,
                     const LsLive* live, PgView* v) {
  memset(v, 0, sizeof *v);
  v->ok = 1;
  v->diff = diff;
  float hard[LS_EPISODES * LS_PARTS];
  for (int i = 0; i < LS_EPISODES * LS_PARTS; ++i) {
    int n = 0;
    pg_count(S + PG_BANK_BASE + (LS_HARD * LS_EPISODES * LS_PARTS + i) * PG_REC_SIZE + PG_REC_NUKES, &n);
    hard[i] = (float)n;
  }
  const int curOk = live && curEp >= 0 && curEp < LS_EPISODES && curPart >= 0 && curPart < LS_PARTS;
  v->hardNukes = ls_hard_nukes(hard, LS_EPISODES * LS_PARTS, curOk && gameDiff == LS_HARD,
                               curOk ? curEp * LS_PARTS + curPart : -1, curOk ? live->nukes : 0.f);
  if (!d) return;
  v->hardTotal = ls_game_nukes(d);
  for (int e = 0; e < LS_EPISODES && d->seen[e]; ++e) {
    v->episodes = e + 1;
    const int parts = d->partcount[e] < LS_PARTS ? d->partcount[e] : LS_PARTS;
    v->parts[e] = parts;
    for (int p = 0; p < parts; ++p) {
      const int done = S[pg_done_offset(diff, e, p)] != 0;
      PgCounts banked;
      pg_record_decode(S + pg_record_offset(diff, e, p), &banked);
      LsTotals t;
      const bool have = ls_totals(d, e, p, diff, &t);
      const bool cur = live && diff == gameDiff && e == curEp && p == curPart;
      PgCounts now = { 0, 0, 0 };
      if (cur) { now.nukes = (int)live->nukes; now.secrets = (int)live->secrets; now.kills = (int)live->kills; }
      v->rows[e][p] = pg_part_row(done, banked, cur ? &now : 0, have ? &t : 0);
      if (done) {
        v->epFound[e].nukes += banked.nukes;
        v->epFound[e].secrets += banked.secrets;
        v->epFound[e].kills += banked.kills;
      }
      if (have) {
        v->epTotal[e].nukes += t.nukes;
        v->epTotal[e].secrets += t.secrets;
        v->epTotal[e].kills += t.kills;
      }
    }
  }
}

// The difficulty selector: the game's difficulty by default, again at each panel session and whenever the game's
// difficulty changes; a pick holds otherwise, across the empty frames of a map change. Normal before any game.
struct PgSel { int picked, gameDiff; };
inline PgSel pg_sel_init() { PgSel s = { -1, -1 }; return s; }
inline int pg_sel_step(PgSel* s, int gameDiff, int newSession) {
  if (gameDiff >= 0 && gameDiff < LS_DIFFS) {
    if (newSession || gameDiff != s->gameDiff || s->picked < 0) s->picked = gameDiff;
    s->gameDiff = gameDiff;
  }
  if (s->picked < 0 || s->picked >= LS_DIFFS) s->picked = 1;
  return s->picked;
}

// --- Names: scripts/local.def, the game's string table (`key:"value"`, one per line) -----------------
inline bool pg_local_value(const char* text, int len, const char* key, char* out, int cap) {
  const int kn = (int)strlen(key);
  for (int i = 0; i < len;) {
    int j = i;
    while (j < len && text[j] != '\n') ++j;
    int a = i;
    while (a < j && (text[a] == ' ' || text[a] == '\t')) ++a;
    const int lineEnd = j;
    i = j + 1;
    if (lineEnd - a <= kn || strncmp(text + a, key, (size_t)kn) != 0) continue;
    int b = a + kn;
    while (b < lineEnd && (text[b] == ' ' || text[b] == '\t')) ++b;
    if (b >= lineEnd || text[b] != ':') continue;
    ++b;
    while (b < lineEnd && (text[b] == ' ' || text[b] == '\t')) ++b;
    int e;
    if (b < lineEnd && text[b] == '"') {
      e = ++b;
      while (e < lineEnd && text[e] != '"') ++e;
    } else {
      e = b;
      while (e < lineEnd && text[e] != '#') ++e;
      while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t' || text[e - 1] == '\r')) --e;
    }
    int n = e - b < cap - 1 ? e - b : cap - 1;
    memcpy(out, text + b, (size_t)n);
    out[n] = 0;
    return n > 0;
  }
  return false;
}

// Printable ASCII only: the panel's fonts hold 0x20..0x7E.
inline bool pg_drawable(const char* s) {
  if (!s || !*s) return false;
  for (; *s; ++s)
    if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7E) return false;
  return true;
}

struct PgNames { char ep[LS_EPISODES][40]; char diff[LS_DIFFS][16]; };

// Episode names from each def title ("@@ep_name_01@@"), difficulty names from i_easy/i_normal/i_hard in sentence
// case. A name missing or not drawable is "" (episodes) or the English default (difficulties).
inline void pg_names(const char* local, int len, const LsDef* d, PgNames* n) {
  memset(n, 0, sizeof *n);
  static const char* const kKey[LS_DIFFS] = { "i_easy", "i_normal", "i_hard" };
  static const char* const kDef[LS_DIFFS] = { "Easy", "Normal", "Hard" };
  for (int i = 0; i < LS_DIFFS; ++i) {
    char b[16];
    if (local && pg_local_value(local, len, kKey[i], b, sizeof b) && pg_drawable(b)) {
      for (int c = 0; b[c]; ++c) b[c] = (char)(c ? tolower((unsigned char)b[c]) : toupper((unsigned char)b[c]));
      strcpy_s(n->diff[i], sizeof n->diff[i], b);
    } else {
      strcpy_s(n->diff[i], sizeof n->diff[i], kDef[i]);
    }
  }
  if (!d || !local) return;
  for (int e = 0; e < LS_EPISODES && d->seen[e]; ++e) {
    const char* t = d->title[e];
    const size_t tl = strlen(t);
    if (tl <= 4 || strncmp(t, "@@", 2) != 0 || strcmp(t + tl - 2, "@@") != 0) continue;
    char key[LS_NAME_CAP];
    _snprintf_s(key, sizeof key, _TRUNCATE, "%.*s", (int)(tl - 4), t + 2);
    char b[40];
    if (pg_local_value(local, len, key, b, sizeof b) && pg_drawable(b)) strcpy_s(n->ep[e], sizeof n->ep[e], b);
  }
}

// A group heading, in the panel's capitals: "EPISODE 1: ROOFTOP REBELLION", or "EPISODE 1" with no name.
inline void pg_episode_heading(int ep, const char* name, char* out, int cap) {
  if (name && *name) _snprintf_s(out, (size_t)cap, _TRUNCATE, "EPISODE %d: %s", ep + 1, name);
  else _snprintf_s(out, (size_t)cap, _TRUNCATE, "EPISODE %d", ep + 1);
  for (char* c = out; *c; ++c) *c = (char)toupper((unsigned char)*c);
}

// --- A row's layout, logical pixels from the row's left edge ----------------------------------------
// From the right: kills, secrets and nukes readouts in fixed columns, then one pip per nuke; from the left: label,
// "Now" tag, map name. What does not fit goes in the order map name, pips, tag; readouts keep N/M. Total: bar at barX.
enum { PG_PIP = 7, PG_PIP_GAP = 2, PG_PIPS_MAX = 16, PG_GAP = 6, PG_COL_GAP = 6, PG_TAG_PAD = 3, PG_BAR_H = 6 };
struct PgLayout { int col[3]; int pipX, tagX, mapX, barX, barW; };
inline int pg_pips_w(int pips) { return pips > 0 ? pips * PG_PIP + (pips - 1) * PG_PIP_GAP : 0; }
inline PgLayout pg_part_layout(int rowW, int margin, int labelEnd, const int colW[3], int pips, int tagW, int mapW) {
  PgLayout L;
  int x = rowW - margin;
  for (int i = 2; i >= 0; --i) { L.col[i] = x - colW[i]; x = L.col[i] - PG_COL_GAP; }
  const int readLeft = L.col[0];
  int after = labelEnd;
  L.tagX = -1;
  if (tagW > 0 && after + PG_GAP + tagW + PG_GAP <= readLeft) { L.tagX = after + PG_GAP; after = L.tagX + tagW; }
  L.pipX = -1;
  if (pips > 0 && pips <= PG_PIPS_MAX) {
    const int px = readLeft - PG_GAP - pg_pips_w(pips);
    if (px >= after + PG_GAP) L.pipX = px;
  }
  const int limit = L.pipX >= 0 ? L.pipX : readLeft;
  L.mapX = mapW > 0 && after + PG_GAP + mapW + PG_GAP <= limit ? after + PG_GAP : -1;
  L.barX = after + PG_GAP;
  L.barW = readLeft - PG_GAP - L.barX;
  if (L.barW < 0) L.barW = 0;
  return L;
}

// --- Save awards (save-awards.md section 2): the Load Game slot's icons as a grid -----------------------
// Session fields beside the records: the save's difficulty (int), the cheated byte per difficulty, the bonus
// floats per difficulty (Mighty Boot, Mighty Duke, X-3000) and Wozma's float (loadgame_awards_stats 0x100a4370).
enum { PG_SAVE_DIFF = 0x20, PG_CHEATED = 0x24, PG_BONUS = 0x1BC, PG_WOZMA = 0x1C8 };
enum { AW_FINISHED, AW_NUKES, AW_SECRETS, AW_KILLS, AW_BONUS, AW_ROWS };
enum { AW_NOT_PLAYED, AW_MISSING, AW_EARNED };
struct AwCell { int state, found, total; };
struct AwView { int ok, parts, saveDiff; AwCell cell[AW_ROWS][LS_DIFFS]; int wozma, cheated[LS_DIFFS]; };

// Mighty Boot on Easy, Mighty Duke on Normal, X-3000 on Hard: the part-end method 0x100a98d0 sets S+0x1BC+4d.
inline const char* aw_bonus_name(int diff) {
  static const char* const kName[LS_DIFFS] = { "Mighty Boot", "Mighty Duke", "X-3000" };
  return kName[diff >= 0 && diff < LS_DIFFS ? diff : 0];
}
// What each bonus does, read 2026-10-03 (save-awards.md section 2): Mighty Boot doubles the kick's 15 damage
// (0x1007dfe0), Mighty Duke halves damage taken (0x10078f10), the X-3000 is granted (weapon 7) at every map load
// on any difficulty of the save (0x100e1fcb; save-awards.md section 5).
inline const char* aw_bonus_effect(int diff) {
  static const char* const kEffect[LS_DIFFS] = { "your kick does double damage (30, not 15)", "you take half damage",
                                                 "the X-3000 joins your weapons" };
  return kEffect[diff >= 0 && diff < LS_DIFFS ? diff : 0];
}

// The slot's own tests (save-awards.md section 1): a float flag is earned while > 0 (NaN is not); Finished is the AND
// of the def's completion bytes (0x100eb6d0); a difficulty is played if it is the save's or any part on it is finished
// (0x100eb790); a count is earned at the def's banked total (0x100eb540), kills as killcount + extrakills of S+0x20.
inline bool aw_flag(const unsigned char* p) {
  float f;
  memcpy(&f, p, sizeof f);
  return f > 0.f;
}
// Shown counts take the part in play as max(banked, live); an award is never earned from them.
inline void aw_build(const unsigned char* S, const LsDef* d, int curEp, int curPart, int gameDiff, const LsLive* live,
                     AwView* v) {
  memset(v, 0, sizeof *v);
  v->wozma = aw_flag(S + PG_WOZMA);
  for (int k = 0; k < LS_DIFFS; ++k) v->cheated[k] = S[PG_CHEATED + k] != 0;
  if (!d) return;
  v->ok = 1;
  int saveDiff;
  memcpy(&saveDiff, S + PG_SAVE_DIFF, sizeof saveDiff);
  v->saveDiff = saveDiff;
  const bool curOk = live && curEp >= 0 && curEp < LS_EPISODES && curPart >= 0 && curPart < LS_PARTS;
  for (int k = 0; k < LS_DIFFS; ++k) {
    LsTotals sum = { 0, 0, 0 };
    unsigned char all = 1, any = 0;
    int parts = 0, done = 0;
    for (int e = 0; e < LS_EPISODES && d->seen[e]; ++e)
      for (int p = 0; p < d->partcount[e] && p < LS_PARTS; ++p) {
        const unsigned char b = S[pg_done_offset(k, e, p)];
        all &= b;
        any |= b;
        parts++;
        done += b != 0;
        LsTotals t;
        if (ls_totals(d, e, p, saveDiff, &t)) { sum.kills += t.kills; sum.secrets += t.secrets; sum.nukes += t.nukes; }
      }
    PgCounts bank = { 0, 0, 0 }, shown;
    for (int i = 0; i < LS_EPISODES * LS_PARTS; ++i) {
      PgCounts c;
      pg_record_decode(S + PG_BANK_BASE + (k * LS_EPISODES * LS_PARTS + i) * PG_REC_SIZE, &c);
      bank.nukes += c.nukes; bank.secrets += c.secrets; bank.kills += c.kills;
    }
    shown = bank;
    if (curOk && k == gameDiff) {
      PgCounts c;
      pg_record_decode(S + pg_record_offset(k, curEp, curPart), &c);
      if ((int)live->nukes > c.nukes) shown.nukes += (int)live->nukes - c.nukes;
      if ((int)live->secrets > c.secrets) shown.secrets += (int)live->secrets - c.secrets;
      if ((int)live->kills > c.kills) shown.kills += (int)live->kills - c.kills;
    }
    v->parts = parts;
    const int played = any || k == saveDiff;
    const int miss = played ? AW_MISSING : AW_NOT_PLAYED;
    const AwCell fin = { parts > 0 && all ? AW_EARNED : miss, done, parts };
    const AwCell nk = { sum.nukes > 0 && bank.nukes >= sum.nukes ? AW_EARNED : miss, shown.nukes, sum.nukes };
    const AwCell sc = { sum.secrets > 0 && bank.secrets >= sum.secrets ? AW_EARNED : miss, shown.secrets, sum.secrets };
    const AwCell kl = { sum.kills > 0 && bank.kills >= sum.kills ? AW_EARNED : miss, shown.kills, sum.kills };
    const AwCell bo = { aw_flag(S + PG_BONUS + 4 * k) ? AW_EARNED : miss, 0, 0 };
    v->cell[AW_FINISHED][k] = fin;
    v->cell[AW_NUKES][k] = nk;
    v->cell[AW_SECRETS][k] = sc;
    v->cell[AW_KILLS][k] = kl;
    v->cell[AW_BONUS][k] = bo;
  }
}

// A slot's text: "not played" (Finished) or "-" never played (the fonts have no dash wider than ASCII); missing,
// "2 / 24 parts", "1133 / 1136" or the bonus's name; earned, the bonus's name and nothing for the glyph slots. A
// count the part in play has brought to its total reads "240 / 240 (at part end)": the game banks it there.
inline void aw_cell_text(const AwCell& c, int row, int diff, char* out, int cap) {
  if (c.state == AW_NOT_PLAYED) strcpy_s(out, (size_t)cap, row == AW_FINISHED ? "not played" : "-");
  else if (row == AW_BONUS) strcpy_s(out, (size_t)cap, aw_bonus_name(diff));
  else if (c.state == AW_EARNED) out[0] = 0;
  else if (row == AW_FINISHED) _snprintf_s(out, (size_t)cap, _TRUNCATE, "%d / %d parts", c.found, c.total);
  else _snprintf_s(out, (size_t)cap, _TRUNCATE, c.total > 0 && c.found >= c.total ? "%d / %d (at part end)" : "%d / %d",
                   c.found, c.total);
}

// The progress bar's fill of w px: found / total, clamped; 0 with no total.
inline int aw_bar_w(const AwCell& c, int w) {
  if (c.total <= 0 || c.found <= 0) return 0;
  if (c.found >= c.total) return w;
  return (int)((long long)w * c.found / c.total);
}

// Slots earned over every slot of the grid, for the folded SAVE AWARDS heading.
inline int aw_earned_count(const AwView& v) {
  int n = 0;
  for (int r = 0; r < AW_ROWS; ++r)
    for (int k = 0; k < LS_DIFFS; ++k) n += v.cell[r][k].state == AW_EARNED;
  return n;
}
inline void aw_summary(char* out, int cap, const AwView& v) {
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "%d of %d earned", aw_earned_count(v), AW_ROWS * LS_DIFFS);
}

// --- Folds: the Progress tab's sections, one bit each in Config::progFolds (set = folded; 0 = all open) ------
// Three sections; the episodes fold as one, so bits a build with per-episode folds saved mean nothing.
enum { PG_FOLD_LEVEL = 0, PG_FOLD_AWARDS = 1, PG_FOLD_RECORDS = 2, PG_FOLD_COUNT = 3 };
static const char PG_HEADING_LEVEL[] = "SECRETS, NUKES AND KILLS";
static const char PG_HEADING_RECORDS[] = "EPISODE RECORDS";
inline bool pg_fold_valid(int sec) { return sec >= 0 && sec < PG_FOLD_COUNT; }
inline bool pg_folded(int mask, int sec) { return pg_fold_valid(sec) && (mask >> sec & 1) != 0; }
inline int pg_fold_toggle(int mask, int sec) { return pg_fold_valid(sec) ? mask ^ (1 << sec) : mask; }

// What a folded heading shows in place of its body's numbers.
inline void pg_summary_episode(char* out, int cap, const PgCounts& f, const PgCounts& t) {
  char n[24], s[24], k[24];
  pg_column_readout(n, sizeof n, PG_COL_NUKES, f, t, 1, 1);
  pg_column_readout(s, sizeof s, PG_COL_SECRETS, f, t, 1, 1);
  pg_column_readout(k, sizeof k, PG_COL_KILLS, f, t, 1, 1);
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "N %s  %s  %s", n, s, k);
}
// The records section: the difficulty shown, then the whole game's sums in the episode format; f/t null when unread.
inline void pg_summary_records(char* out, int cap, const char* diffName, const PgCounts* f, const PgCounts* t) {
  if (!f || !t) { _snprintf_s(out, (size_t)cap, _TRUNCATE, "%s", diffName); return; }
  char e[48];
  pg_summary_episode(e, sizeof e, *f, *t);
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "%s  %s", diffName, e);
}

// What a slot is and how to earn it.
inline void aw_tip(const AwView* v, int row, int diff, const PgNames* n, char* out, int cap) {
  const int k = diff >= 0 && diff < LS_DIFFS ? diff : 0;
  const char* dn = n->diff[k];
  static const char kReplay[] = "Replay a part that is short of its total.";
  switch (row) {
    case AW_FINISHED:
      _snprintf_s(out, (size_t)cap, _TRUNCATE, "Finished on %s: reach the end of all %d parts on %s.", dn, v->parts, dn);
      break;
    case AW_NUKES:
      _snprintf_s(out, (size_t)cap, _TRUNCATE, "All nukes on %s: bank %d across all %d parts. %s", dn,
                  v->cell[AW_NUKES][k].total, v->parts, kReplay);
      break;
    case AW_SECRETS:
      _snprintf_s(out, (size_t)cap, _TRUNCATE, "All secrets on %s: find %d across all %d parts. %s", dn,
                  v->cell[AW_SECRETS][k].total, v->parts, kReplay);
      break;
    case AW_KILLS:
      if (v->saveDiff == k || v->saveDiff < 0 || v->saveDiff >= LS_DIFFS)
        _snprintf_s(out, (size_t)cap, _TRUNCATE, "All kills on %s: kill %d across all %d parts. %s", dn,
                    v->cell[AW_KILLS][k].total, v->parts, kReplay);
      else
        _snprintf_s(out, (size_t)cap, _TRUNCATE, "All kills on %s: kill %d across all %d parts; the game counts the "
                    "extra kills of the save's difficulty (%s) in every column. %s", dn, v->cell[AW_KILLS][k].total,
                    v->parts, n->diff[v->saveDiff], kReplay);
      break;
    default:
      _snprintf_s(out, (size_t)cap, _TRUNCATE, "%s: %s. Earn it: finish every part on %s with all %d nukes; granted "
                  "at a part's end.", aw_bonus_name(k), aw_bonus_effect(k), dn, v->cell[AW_NUKES][k].total);
      break;
  }
}

// "Cheated on Hard", "Cheated on Easy and Hard", "Cheated on Easy, Normal and Hard"; false (and "") when clean.
inline bool aw_cheated_text(const int cheated[LS_DIFFS], const PgNames* n, char* out, int cap) {
  int which[LS_DIFFS], m = 0;
  for (int k = 0; k < LS_DIFFS; ++k)
    if (cheated[k]) which[m++] = k;
  out[0] = 0;
  if (!m) return false;
  int len = _snprintf_s(out, (size_t)cap, _TRUNCATE, "Cheated on %s", n->diff[which[0]]);
  for (int i = 1; i < m && len >= 0; ++i) {
    const int r = _snprintf_s(out + len, (size_t)(cap - len), _TRUNCATE, "%s%s", i == m - 1 ? " and " : ", ",
                              n->diff[which[i]]);
    len = r < 0 ? -1 : len + r;   // truncated: the rest is dropped
  }
  return true;
}

// The footer's tooltips (levels-and-stats.md section 2; save-awards.md section 2).
static const char AW_WOZMA_TIP[] = "The hidden boss: on Hard with all 240 nukes banked, the next nuke pickup opens a "
                                   "down-track in e08p03_a02 to the wozma exit. Beaten once Wozma dies.";
static const char AW_CHEATED_TIP[] = "The game marks the save's difficulty for give, camera or overkill, or for "
                                     "g_p_god, g_fps, g_cam_fov or g_cam_far left off its default.";
static const char AW_UNUSED_TIP[] = "Two icons in the awards font are never earned: All babes (no code shows it) and "
                                    "Double damage (shown for a save flag nothing in the game sets; the Mighty Boot's "
                                    "double kick is a separate flag).";

// --- The glue (progress.cpp), main thread only -------------------------------------------------------
// The view for `diff`, read at most once a second (and at once on a new difficulty); ok 0 with no level.
const PgView* progress_view(int diff);
// The save awards, read with the view (progress_view first); ok 0 with no level or no def.
const AwView* progress_awards(void);
// The names (local.def through the VFS, once); the defaults until it reads.
const PgNames* progress_names(void);
// The selector's readout.
const char* progress_diff_name(int diff);
