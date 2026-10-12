// The Progress tab's reads (progress.h holds the rules, menu.cpp draws). Main thread, from the panel's draw. The
// session comes from levelstats; the banked span is checked with one guarded read, at most once a second while the tab
// draws. Names: scripts/local.def through the engine VFS, which picks the language's copy as the engine does.
#include "progress.h"
#include "engine.h"
#include "log.h"
#include <windows.h>
#include <string.h>

static const char kLocalPath[] = "scripts/local.def";
static const ULONGLONG kReadMs = 1000, kLogMs = 5000, kLocalRetryMs = 5000;

static PgView s_view;
static AwView s_awards;
static int s_viewDiff = -1;
static ULONGLONG s_nextRead, s_nextLog, s_nextLocal;
static PgNames s_names;
static int s_namesInit, s_localOk;
static unsigned s_reads, s_noSession, s_unreadable, s_localReads;

// The whole file; false when it does not open or reads empty.
static bool read_local(void) {
  s_localReads++;
  if (!g_eng.vfs_open || !g_eng.vfs_read || !g_eng.vfs_close) return false;
  void* f = g_eng.vfs_open(kLocalPath, VFS_O_RDONLY, VFS_MODE_RW);
  if (!f) {
    static unsigned said = 0;   // logged once a session, not every retry
    if (!said++) np_log("progress: %s does not open", kLocalPath);
    return false;
  }
  static char buf[65536];
  int len = 0;
  for (;;) {
    const int n = g_eng.vfs_read(f, buf + len, (unsigned)(sizeof buf - 1 - len));
    if (n <= 0) break;
    len += n;
    if (len >= (int)sizeof buf - 1) break;
  }
  g_eng.vfs_close(f);
  buf[len] = 0;
  const LsDef* d = levelstats_def();
  pg_names(buf, len, d, &s_names);
  np_log("progress: %s read %d bytes; difficulties %s/%s/%s; episode 1 \"%s\" (def %s)", kLocalPath, len,
         s_names.diff[0], s_names.diff[1], s_names.diff[2], s_names.ep[0], d ? "parsed" : "unread");
  return len > 0 && d;   // episode names need the def's titles: retried until both are there
}

static void names_step(ULONGLONG now) {
  if (!s_namesInit) { pg_names(0, 0, 0, &s_names); s_namesInit = 1; }
  if (s_localOk || now < s_nextLocal) return;
  s_nextLocal = now + kLocalRetryMs;
  s_localOk = read_local();
}

const PgNames* progress_names(void) {
  names_step(GetTickCount64());
  return &s_names;
}

const char* progress_diff_name(int diff) {
  const PgNames* n = progress_names();
  return n->diff[diff >= 0 && diff < LS_DIFFS ? diff : 1];
}

const PgView* progress_view(int diff) {
  levelstats_demand();
  const ULONGLONG now = GetTickCount64();
  names_step(now);
  if (diff != s_viewDiff || now >= s_nextRead) {
    s_nextRead = now + kReadMs;
    s_viewDiff = diff;
    s_reads++;
    LsSession ses;
    if (!levelstats_session(&ses) || !ses.S) {
      s_noSession++;
      s_view.ok = 0;
      s_awards.ok = 0;
    } else if (!guarded_readable(ses.S, PG_SESSION_SPAN)) {
      s_unreadable++;
      s_view.ok = 0;
      s_awards.ok = 0;
    } else {
      pg_build(ses.S, levelstats_def(), diff, ses.ep, ses.part, ses.diff, ses.liveOk ? &ses.live : 0, &s_view);
      aw_build(ses.S, levelstats_def(), ses.ep, ses.part, ses.diff, ses.liveOk ? &ses.live : 0, &s_awards);
    }
  }
  if (now >= s_nextLog) {
    s_nextLog = now + kLogMs;
    int played = 0, complete = 0, rows = 0;
    for (int e = 0; e < s_view.episodes; ++e)
      for (int p = 0; p < s_view.parts[e]; ++p, ++rows) {
        played += s_view.rows[e][p].state != PG_UNPLAYED;
        complete += s_view.rows[e][p].state == PG_COMPLETE;
      }
    np_log("progress: difficulty %d, view %s, %d episodes %d parts (%d played, %d complete), Hard nukes %d/%d; "
           "reads %u, no session %u, unreadable %u, local.def reads %u", diff, s_view.ok ? "read" : "empty",
           s_view.episodes, rows, played, complete, s_view.hardNukes, s_view.hardTotal, s_reads, s_noSession,
           s_unreadable, s_localReads);
    if (s_awards.ok) {
      char e[LS_DIFFS * AW_ROWS + LS_DIFFS];   // per difficulty: F N S K B as E earned, m missing, - not played
      int n = 0;
      for (int k = 0; k < LS_DIFFS; ++k) {
        for (int r = 0; r < AW_ROWS; ++r) {
          const int st = s_awards.cell[r][k].state;
          e[n++] = st == AW_EARNED ? 'E' : st == AW_MISSING ? 'm' : '-';
        }
        e[n++] = k + 1 < LS_DIFFS ? '/' : 0;
      }
      np_log("progress: awards %s (easy/normal/hard: finished nukes secrets kills bonus), parts %d, Hard kills %d/%d, "
             "wozma %d, cheated %d%d%d", e, s_awards.parts, s_awards.cell[AW_KILLS][LS_HARD].found,
             s_awards.cell[AW_KILLS][LS_HARD].total, s_awards.wozma, s_awards.cheated[0], s_awards.cheated[1],
             s_awards.cheated[2]);
    }
  }
  return &s_view;
}

const AwView* progress_awards(void) { return &s_awards; }
