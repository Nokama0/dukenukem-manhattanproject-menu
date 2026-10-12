// Cross-area secrets and nukes glue (areas.h holds the rules). Main thread, frame path. Static data per part: each
// area's world.scr and avolume files via the VFS. Found state: loaded world from memory (area_secrets +0xAE4), else
// state/<map>.msv, else fresh; nukes also fall back on our last live view (docs/re/areas-and-secrets.md section 4).
#include "areas.h"
#include "engine.h"
#include "entities.h"     // entities_in_duke
#include "levelstats.h"   // levelstats_session, levelstats_def
#include "log.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

typedef const char* (__thiscall* ClassnameFn)(const void* self);

// World fields: +0x48 map name, +0xD4 server id (world_find_or_load; area_changemap_place_player's spawn_search).
static const unsigned kWorldName = 0x48, kWorldSid = 0xD4, kWorldSpan = 0xD8;
// game: +0xEC current world, +0xF8 first node of the loaded-world list; node +4 next, +8 world.
static const unsigned kGameOfs = 0x8, kGameCurWorld = 0xEC, kGameFirstNode = 0xF8, kGameSpan = 0xFC;
// The respawn record area_changemap_place_player duke!0x1009ad00 writes on a doorway: S+0x2268C target map,
// S+0x226AC target_dest, char[32] each (section 5).
static const unsigned kEntryMap = 0x2268C, kEntryDest = 0x226AC, kEntrySpan = 0x40;
// area_secret: +0x90 killed, +4 name char[0x40], +0xAE4 armed (entities.cpp's walk).
static const unsigned kSecretSpan = 0xAE8;
// nuke, crate, barrel: +4 name, +0x18C position, +0x2E0 active/whole (entities.cpp's item and holder reads).
static const unsigned kNukeSpan = 0x2E1;
// The player's +0xC10: 20-22 are the deaths (rescue.cpp); the game then reloads its checkpoint.
static const unsigned kPlayerPs = 0xC10;
static const int kWalkMax = 4096, kListMax = 32, kLiveNukesMax = 64;
static const ULONGLONG kRefreshMs = 1000, kDemandMs = 1500, kRecordMs = 1000;
// A file written before the part's first staging is from another play. The slack covers a save load, which
// unpacks the save's state files just before the player spawns (FUN_100eac50 0x100eaec7).
static const ULONGLONG kStaleSlack100ns = 60ull * 10000000ull;
static const int kFileMax = 8 << 20;

static ArPart s_part;
static int s_partOk, s_partNukes = -1;   // s_partNukes: episodes.def's nukecount for the part, -1 unknown
static char s_prefix[8];
static ULONGLONG s_partStart;   // FILETIME units
static const void* s_world;
static ArView s_view;
static ULONGLONG s_nextRefresh, s_demandUntil, s_nextRecord;
// Frames until the forced refresh: 2 at an area change, so this frame's levelstats_frame (which ran before the stage,
// without demand) is followed by one that reads the live tally.
static int s_refreshDue;
static char s_stateDir[MAX_PATH];
// Our own record of each area's nukes: the last live view this part. Dropped with the part, on Duke's death (the
// game reloads its checkpoint and forgets what he took since) and when the live nuke tally falls.
static ArNukeFound s_rec[AR_AREAS_MAX];
static ArKills s_krec[AR_AREAS_MAX];   // the same record for the kills, dropped with it
static int s_dead, s_lastNukeLive = -1;
// What makes the record stale besides a death (ar_records_stale): the session record and the player entity seen last,
// and when the current world was staged (a live verdict waits AR_SETTLE_MS after it).
static const unsigned char* s_sessionS;
static const unsigned char* s_player;
static const void* s_lastWorld;   // unlike s_world, kept through menus (no player): a re-stage of it is no arrival
static ULONGLONG s_stageMs;

// The cached result of each area's file: refreshed only when its time or size changes.
struct MsvCache {
  ULONGLONG time;
  DWORD size;
  int valid, nukeRecs, nukeUnnamed, monRecs;
  ArFound found;
  ArNukeFound nukes;
  ArKills kills;
};
static MsvCache s_msv[AR_AREAS_MAX];

// Cumulative counters, in every refresh log line.
static unsigned s_parts, s_scrFail, s_volRead, s_volFail, s_refreshes, s_walks, s_walkCapped, s_msvReads, s_msvVfs,
    s_msvWin32, s_msvStale, s_msvBad, s_listBad, s_mismatches, s_nukeWalks, s_nukeWalkBad, s_nukeMsvBad, s_recTicks,
    s_recDrops, s_nukeMismatches, s_derived, s_nukeSettling, s_carryUnread, s_laserLookups, s_laserUnfound,
    s_spawnerUnfound, s_killWalks, s_killWalkBad, s_killSettling, s_ambUnfound;
static char s_lastLog[1024];

static unsigned char* s_buf;
static int s_bufCap;

static bool grow(int need) {
  if (need <= s_bufCap) return true;
  int cap = s_bufCap ? s_bufCap : 256 << 10;
  while (cap < need) cap *= 2;
  unsigned char* b = (unsigned char*)realloc(s_buf, (size_t)cap);
  if (!b) return false;
  s_buf = b;
  s_bufCap = cap;
  return true;
}

// The whole file through the VFS into s_buf (NUL-terminated); the length, or -1 when it does not open.
static int vfs_read_all(const char* path) {
  if (!g_eng.vfs_open || !g_eng.vfs_read || !g_eng.vfs_close) return -1;
  void* f = g_eng.vfs_open(path, VFS_O_RDONLY, VFS_MODE_RW);
  if (!f) return -1;
  int len = 0;
  for (;;) {
    if (!grow(len + (64 << 10) + 1)) break;
    const int n = g_eng.vfs_read(f, s_buf + len, (unsigned)(s_bufCap - 1 - len));
    if (n <= 0) break;
    len += n;
    if (len >= kFileMax) break;
  }
  g_eng.vfs_close(f);
  if (s_buf) s_buf[len] = 0;
  return len;
}

// The same file from disk, for a .msv the VFS tree does not hold (a name it never registered).
static int win32_read_all(const char* path) {
  HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, 0);
  if (h == INVALID_HANDLE_VALUE) return -1;
  const DWORD size = GetFileSize(h, 0);
  int len = -1;
  if (size != INVALID_FILE_SIZE && (int)size < kFileMax && grow((int)size + 1)) {
    DWORD got = 0;
    if (ReadFile(h, s_buf, size, &got, 0)) { len = (int)got; s_buf[len] = 0; }
  }
  CloseHandle(h);
  return len;
}

static void read_volume(const char* map, const char* name, float out[6], int* ok) {
  char path[128];
  _snprintf_s(path, sizeof path, _TRUNCATE, "maps/%s/%s/avolume", map, name);
  const int n = vfs_read_all(path);
  *ok = n >= 24 && ar_volume(s_buf, n, out);
  if (*ok) s_volRead++;
  else s_volFail++;
}

// The class a monster script spawns ("scripts/monst/gator2.sc" spawns monst_gator); the name's guess stays when it
// does not read.
static unsigned s_clsRead, s_clsFail;
static void resolve_class(char cls[AR_CLS]) {
  char path[96];
  _snprintf_s(path, sizeof path, _TRUNCATE, "scripts/monst/%s.sc", cls + 6);
  char got[AR_CLS];
  const int n = vfs_read_all(path);
  if (n > 0 && ar_script_class((const char*)s_buf, n, got, sizeof got) && carry_class(got)) {
    memcpy(cls, got, AR_CLS);
    s_clsRead++;
  } else {
    s_clsFail++;
  }
}

static void drop_records(const char* why) {
  int n = 0;
  for (int i = 0; i < AR_AREAS_MAX; ++i) n += s_rec[i].known || s_krec[i].known;
  memset(s_rec, 0, sizeof s_rec);
  memset(s_krec, 0, sizeof s_krec);
  if (n) {
    s_recDrops++;
    np_log("areas: nuke records dropped (%d areas): %s", n, why);
  }
}

static void load_part(const char* prefix) {
  s_parts++;
  memset(&s_part, 0, sizeof s_part);
  memset(s_msv, 0, sizeof s_msv);
  memset(s_rec, 0, sizeof s_rec);
  memset(s_krec, 0, sizeof s_krec);
  s_lastNukeLive = -1;
  strcpy_s(s_part.prefix, sizeof s_part.prefix, prefix);
  char maps[AR_AREAS_MAX][AR_NAME];
  s_part.nareas = ar_part_maps(prefix, maps, AR_AREAS_MAX);
  int read = 0;
  for (int i = 0; i < s_part.nareas; ++i) {
    ArArea& a = s_part.areas[i];
    strcpy_s(a.map, sizeof a.map, maps[i]);
    char path[96];
    _snprintf_s(path, sizeof path, _TRUNCATE, "maps/%s/world.scr", a.map);
    const int n = vfs_read_all(path);
    if (n <= 0 || !ar_parse_world((const char*)s_buf, n, &a)) {
      s_scrFail++;
      np_log("areas: %s does not read (%d bytes)", path, n);
      continue;
    }
    read++;
    for (int k = 0; k < a.ndoors; ++k) read_volume(a.map, a.doors[k].name, a.doors[k].vol, &a.doors[k].volOk);
    for (int k = 0; k < a.nsecrets; ++k) read_volume(a.map, a.secrets[k].name, a.secrets[k].vol, &a.secrets[k].volOk);
    for (int k = 0; k < a.nambush; ++k) {
      ArAmbush& m = a.ambush[k];
      if (m.zone[0]) read_volume(a.map, m.zone, m.vol, &m.volOk);
      for (int c = 0; c < m.nmon; ++c) resolve_class(m.mon[c]);
    }
  }
  ar_link(&s_part);
  s_partOk = read == s_part.nareas && s_part.nareas > 0;
  const ArTotals t = ar_totals(&s_part);
  char per[200];
  int len = 0;
  per[0] = 0;
  for (int i = 0; i < s_part.nareas && len >= 0; ++i) {
    const ArArea& a = s_part.areas[i];
    const int k = _snprintf_s(per + len, sizeof per - len, _TRUNCATE, "%s%s %d/%d", i ? ", " : "", ar_label(&a),
                              a.nsecrets, a.nukeTotal);
    if (k < 0) break;
    len += k;
  }
  int carried = 0, carriers = 0, lasered = 0, lasers = 0;
  for (int i = 0; i < s_part.nareas; ++i)
    for (int k = 0; k < s_part.areas[i].nnukes; ++k)
      if (s_part.areas[i].nukes[k].holder == AR_NK_ENEMY) {
        carried++;
        carriers += s_part.areas[i].nukes[k].slot == 0;
      } else if (s_part.areas[i].nukes[k].holder == AR_NK_LASER) {
        lasered += s_part.areas[i].nukes[k].count;
        lasers++;
      }
  int ep = 0, pt = 0, def = -1;
  s_partNukes = -1;
  ls_map_episode_part(prefix, &ep, &pt);
  const LsDef* d = levelstats_def();
  if (d && ep < LS_EPISODES && pt < LS_PARTS) {
    def = d->secrets[ep][pt];
    s_partNukes = d->nukes[ep][pt];
  }
  np_log("areas: part %s %s: %d areas (%d read), %d doorways (%d edges in the part, %d two-way pairs), %d depth runs, "
         "secrets/nukes %s = %d/%d (%d nukes on %d enemies, %d from %d lasers; episodes.def %d/%d: the rest of the "
         "nukes come from spawner scripts), volumes read %u failed %u, dropped %d", prefix,
         s_partOk ? "parsed" : "INCOMPLETE", s_part.nareas, read, t.doors, t.edges, t.pairs, t.runs, per, t.secrets,
         t.nukes, carried, carriers, lasered, lasers, def, s_partNukes, s_volRead, s_volFail, t.dropped);
  for (int i = 0; i < s_part.nareas; ++i)
    for (int k = 0; k < s_part.areas[i].ndoors; ++k) {
      const ArDoor& dr = s_part.areas[i].doors[k];
      np_log("areas:   %s door %s type %d%s -> %s:%s (%s)%s%s", s_part.areas[i].map, dr.name, dr.changeType,
             dr.hasType ? "" : " (no key)", dr.tmap[0] ? dr.tmap : "-", dr.tdest[0] ? dr.tdest : "-",
             dr.landing >= 0 ? (dr.pair ? "two-way" : "one-way") : dr.target >= 0 ? "LANDING MISSING"
                                                                                  : dr.tmap[0] ? "leaves the part"
                                                                                               : "landing only",
             dr.run[0] ? ", run " : "", dr.run);
    }
  for (int i = 0; i < s_part.nareas; ++i) {
    const ArArea& a = s_part.areas[i];
    np_log("areas:   %s kills %d = %d placed + %d from %d ambush(es) + %d wired egg(s)", a.map, a.killTotal, a.placed,
           a.spawned, a.nambush, a.neggs);
    for (int k = 0; k < a.nambush; ++k) {
      const ArAmbush& m = a.ambush[k];
      char label[112];
      ar_ambush_label(&m, 0, label, sizeof label);
      np_log("areas:   %s ambush %s (%s) %d spawner(s) from %s at (%.0f,%.0f,%.0f), zone %s%s: %s", a.map, m.trig,
             m.trigCls, m.nsp, m.sp[0], m.spPos[0][0], m.spPos[0][1], m.spPos[0][2], m.zone[0] ? m.zone : "-",
             m.zone[0] ? (m.volOk ? " (volume read)" : " (VOLUME UNREAD)") : "", label);
    }
  }
  np_log("areas: monster scripts read %u, unread %u (the class is then the script's name)", s_clsRead, s_clsFail);
  for (int i = 0; i < s_part.nareas; ++i)
    for (int k = 0; k < s_part.areas[i].nnukes; ++k) {
      const ArNuke& nk = s_part.areas[i].nukes[k];
      static const char* const kKind[] = { "pickup", "crate", "barrel", "enemy", "laser" };
      const bool named = nk.holder == AR_NK_ENEMY || nk.holder == AR_NK_LASER;
      np_log("areas:   %s nuke source %s (%s, %d%s%s) at (%.0f,%.0f,%.0f)", s_part.areas[i].map, nk.name,
             kKind[nk.holder >= 0 && nk.holder <= AR_NK_LASER ? nk.holder : 0], nk.count,
             nk.holder == AR_NK_LASER ? ", fires " : named ? ", " : "", named ? nk.cls : "", nk.pos[0], nk.pos[1],
             nk.pos[2]);
    }
}

// laser +0xB90 armed, spawner_entity +0xAE4 entities spawned (re/pickups.md section 7; laserrule.h).
static const unsigned kLaserSpan = 0xB94, kSpawnerSpan = 0xAE8;

static bool entity_class(const unsigned char* e, ReadCache* rc, unsigned span, const char** cls) {
  if (!guarded_readable_cached(rc, e, span)) return false;
  void* const* vt = *(void* const* const*)e;
  if (!entities_in_duke(vt) || !entities_in_duke(vt + 2) || !entities_in_duke(vt[1])) return false;
  int killed;
  memcpy(&killed, e + 0x90, sizeof killed);
  if (killed) return false;
  *cls = ((ClassnameFn)vt[1])(e);
  return entities_in_duke(*cls);
}

// The area_secrets of one loaded world, name-matched against the area's list.
static void walk_world(int sid, const ArArea* a, ArFound* f) {
  s_walks++;
  char names[AR_SECRETS_MAX + 1][AR_NAME];
  int armed[AR_SECRETS_MAX + 1];
  int n = 0, total = 0;
  bool capped = false;
  ReadCache rc = {};
  g_eng.spawn_search("s=%d?t=%d", sid, 0x20);
  const unsigned char* e;
  while ((e = (const unsigned char*)g_eng.spawn_search_get()) != 0) {
    if (++total > kWalkMax) { capped = true; break; }
    const char* cls;
    if (!entity_class(e, &rc, kSecretSpan, &cls) || strcmp(cls, "area_secret") != 0) continue;
    if (n > AR_SECRETS_MAX) { n++; continue; }   // more than any area holds: the match below refuses
    _snprintf_s(names[n], AR_NAME, _TRUNCATE, "%.*s", 0x3F, (const char*)e + 4);
    unsigned v;
    memcpy(&v, e + 0xAE4, sizeof v);
    armed[n++] = v != 0;
  }
  g_eng.spawn_search_stop();
  if (capped) { s_walkCapped++; memset(f, 0, sizeof *f); f->src = AR_SRC_BAD; return; }
  ar_found_match(a, names, armed, n, AR_SRC_LIVE, f);
}

// The nukes, crates, barrels and carriers of one loaded world (the t=2 dobj search, as entities.cpp walks them). An
// enemy's drop list that does not read, or a capped one, makes the walk bad (unknown). msSinceStage: since the world
// was staged; a world just staged, or with an empty list while the area has sources, is SETTLING (ar_live_settled).
static void walk_nukes(int sid, const ArArea* a, ArNukeFound* f, ULONGLONG msSinceStage) {
  s_nukeWalks++;
  static ArLiveNuke live[kLiveNukesMax];
  int n = 0, total = 0;
  bool bad = false;
  ReadCache rc = {}, listRc = {};
  g_eng.spawn_search("s=%d?t=%d", sid, 2);
  const unsigned char* e;
  while ((e = (const unsigned char*)g_eng.spawn_search_get()) != 0) {
    if (++total > kWalkMax) { bad = true; break; }
    const char* cls;
    if (!entity_class(e, &rc, kNukeSpan, &cls)) continue;
    const int holder = !strcmp(cls, "nuke") ? AR_NK_PICKUP : !strcmp(cls, "crate") ? AR_NK_CRATE
                     : !strcmp(cls, "barrel") ? AR_NK_BARREL : carry_class(cls) ? AR_NK_ENEMY : -1;
    if (holder < 0) continue;
    char name[AR_NAME];
    _snprintf_s(name, sizeof name, _TRUNCATE, "%.*s", 0x3F, (const char*)e + 4);
    int kind = holder;
    if (holder != AR_NK_PICKUP) {
      bool named = false;
      for (int k = 0; k < a->nnukes && !named; ++k) named = a->nukes[k].holder == holder && !strcmp(a->nukes[k].name, name);
      if (!named && holder == AR_NK_ENEMY && ar_laser_spawned(a, name)) kind = AR_NK_SPAWNED;
      else if (!named) continue;
    }
    if (n >= kLiveNukesMax) { bad = true; continue; }
    ArLiveNuke& o = live[n++];
    memcpy(o.name, name, sizeof o.name);
    memcpy(o.pos, e + 0x18C, sizeof o.pos);
    o.holder = kind;
    o.active = e[0x2E0] != 0;
    o.held = 0;
    if (holder == AR_NK_ENEMY && (o.held = entities_carried_nukes(e, &rc, &listRc)) < 0) {
      s_carryUnread++;
      bad = true;
    }
  }
  g_eng.spawn_search_stop();
  // Each nuke laser and the spawner it fires, by name (neither is a dobj): armed, and the entities spawned so far.
  for (int k = 0; k < a->nnukes && !bad; ++k) {
    const ArNuke& s = a->nukes[k];
    if (s.holder != AR_NK_LASER) continue;
    s_laserLookups++;
    const unsigned char* L = 0;
    const unsigned char* S = 0;
    for (int w = 0; w < 2; ++w) {
      g_eng.spawn_search("s=%d?n=%s", sid, w ? s.cls : s.name);
      const unsigned char* x = (const unsigned char*)g_eng.spawn_search_get();
      const char* cls;
      if (x && entity_class(x, &rc, w ? kSpawnerSpan : kLaserSpan, &cls) && !strcmp(cls, w ? "spawner_entity" : "laser"))
        (w ? S : L) = x;
      g_eng.spawn_search_stop();
    }
    if (!L) s_laserUnfound++;
    if (!S) s_spawnerUnfound++;
    if (!L && !S) continue;   // neither found: not fired (ar_nukes_live)
    if (n >= kLiveNukesMax) { bad = true; break; }
    ArLiveNuke& o = live[n++];
    memset(&o, 0, sizeof o);
    strcpy_s(o.name, sizeof o.name, s.name);
    o.holder = AR_NK_LASER;
    unsigned u = 0;
    if (L) { memcpy(&u, L + 0xB90, sizeof u); o.active = u != 0; }
    if (S) { memcpy(&u, S + 0xAE4, sizeof u); o.held = (int)u; }
  }
  if (bad) { s_nukeWalkBad++; memset(f, 0, sizeof *f); f->src = AR_SRC_BAD; return; }
  if (!ar_live_settled(a->nnukes, total, msSinceStage)) {
    s_nukeSettling++;
    memset(f, 0, sizeof *f);
    f->src = AR_SRC_SETTLING;
    return;
  }
  ar_nukes_live(a, live, n, f);
}

// One entity by name (spawners and lasers are no dobjs: the type searches never return them), or 0.
static const unsigned char* named(int sid, const char* name, const char* cls, unsigned span, ReadCache* rc) {
  g_eng.spawn_search("s=%d?n=%s", sid, name);
  const unsigned char* x = (const unsigned char*)g_eng.spawn_search_get();
  const char* c;
  const unsigned char* hit = x && entity_class(x, rc, span, &c) && !strcmp(c, cls) ? x : 0;
  g_eng.spawn_search_stop();
  return hit;
}

// An ambush's trigger (ar_trig_read): 1 gone off, 0 not, -1 unread. area_switch +0xAF4 is its on byte (cutaway.md
// section 3); a holder's +0x2E0 its intact byte; a monster's +0xB84 its health. Gone counts as gone off.
static const unsigned kSwitchSpan = 0xAF8, kHolderSpan = 0x2E1, kMonsterSpan = 0xB90, kEggSpan = 0xC50;
static int trig_done(int sid, const ArAmbush& m, ReadCache* rc) {
  const int how = ar_trig_read(&m);
  if (how == AR_TRD_SWITCH) {
    const unsigned char* s = named(sid, m.zone, "area_switch", kSwitchSpan, rc);
    return s ? s[0xAF4] != 0 : -1;
  }
  if (how == AR_TRD_NONE) return -1;
  const unsigned char* t = named(sid, m.trig, m.trigCls, how == AR_TRD_MONSTER ? kMonsterSpan : kHolderSpan, rc);
  if (!t) return 1;
  if (how == AR_TRD_HOLDER) return t[0x2E0] == 0;
  float hp;
  memcpy(&hp, t + 0xB84, sizeof hp);
  return hp <= 0.f;
}

// The kills of one loaded world (ar_kills_live): its monsters alive (the monster base's user bit +0x4C 0x20 and
// health +0xB84 > 0, as entities.cpp's walk reads them), its wired eggs not hatched (roach_pod +0xC48 hatched, +0xAF0
// intact, eggrule.h), and each ambush's spawners (+0xAE4) and laser (+0xB90) by name. Once a second at most.
static void walk_kills(int sid, const ArArea* a, ArKills* k, ULONGLONG msSinceStage) {
  s_killWalks++;
  memset(k, 0, sizeof *k);
  int alive = 0, total = 0;
  bool bad = false;
  ReadCache rc = {};
  g_eng.spawn_search("s=%d?t=%d", sid, 2);
  const unsigned char* e;
  while ((e = (const unsigned char*)g_eng.spawn_search_get()) != 0) {
    if (++total > kWalkMax) { bad = true; break; }
    const char* cls;
    if (!entity_class(e, &rc, kNukeSpan, &cls)) continue;
    if (!strncmp(cls, "monst_", 6)) {
      if (!guarded_readable_cached(&rc, e, kMonsterSpan)) { bad = true; continue; }
      unsigned user;
      float hp;
      memcpy(&user, e + 0x4C, sizeof user);
      memcpy(&hp, e + 0xB84, sizeof hp);
      alive += (user & 0x20) && hp > 0.f;
    } else if (a->neggs && !strcmp(cls, "roach_pod")) {
      char name[AR_NAME];
      _snprintf_s(name, sizeof name, _TRUNCATE, "%.*s", 0x3F, (const char*)e + 4);
      bool wired = false;
      for (int j = 0; j < a->neggs && j < AR_EGGS_MAX && !wired; ++j) wired = !strcmp(a->eggs[j], name);
      if (!wired) continue;
      if (!guarded_readable_cached(&rc, e, kEggSpan)) { bad = true; continue; }
      alive += !e[0xC48] && e[0xAF0];
    }
  }
  g_eng.spawn_search_stop();
  int st[AR_AMBUSH_MAX], fires[AR_AMBUSH_MAX];
  for (int m = 0; m < a->nambush && !bad; ++m) {
    const ArAmbush& am = a->ambush[m];
    int spawned[AR_AMB_SP];
    for (int j = 0; j < am.nsp; ++j) {
      const unsigned char* S = named(sid, am.sp[j], "spawner_entity", kSpawnerSpan, &rc);
      if (!S) s_ambUnfound++;
      unsigned u = 0;
      if (S) memcpy(&u, S + 0xAE4, sizeof u);
      spawned[j] = S ? (int)u : -1;
    }
    int armed = -1;
    if (am.kind == AR_TRIG_LASER) {
      const unsigned char* L = named(sid, am.trig, "laser", kLaserSpan, &rc);
      unsigned u = 0;
      if (L) { memcpy(&u, L + 0xB90, sizeof u); armed = u != 0; }
    }
    st[m] = ar_ambush_state(&am, spawned, armed, trig_done(sid, am, &rc), &fires[m]);
  }
  if (bad) { s_killWalkBad++; k->src = AR_SRC_BAD; return; }
  if (!ar_live_settled(a->killTotal, total, msSinceStage)) { s_killSettling++; k->src = AR_SRC_SETTLING; return; }
  ar_kills_live(a, alive, st, fires, k);
}

static ULONGLONG ft64(const FILETIME& t) { return ((ULONGLONG)t.dwHighDateTime << 32) | t.dwLowDateTime; }

static void from_file(int i, ArFound* f, ArNukeFound* nf, ArKills* kf) {
  const ArArea& a = s_part.areas[i];
  char lower[AR_NAME];
  strcpy_s(lower, sizeof lower, a.map);
  _strlwr_s(lower, sizeof lower);
  char disk[MAX_PATH + AR_NAME];
  _snprintf_s(disk, sizeof disk, _TRUNCATE, "%s%s.msv", s_stateDir, lower);
  WIN32_FILE_ATTRIBUTE_DATA fa;
  if (!s_stateDir[0] || !GetFileAttributesExA(disk, GetFileExInfoStandard, &fa)) {
    ar_found_fresh(&a, f);   // never unloaded this play: it loads fresh from world.scr
    ar_nukes_fresh(&a, nf);
    ar_kills_fresh(&a, kf);
    return;
  }
  const ULONGLONG t = ft64(fa.ftLastWriteTime);
  if (t + kStaleSlack100ns < s_partStart) {
    s_msvStale++;
    memset(f, 0, sizeof *f);
    f->src = AR_SRC_STALE;
    memset(nf, 0, sizeof *nf);
    nf->src = AR_SRC_STALE;
    memset(kf, 0, sizeof *kf);
    kf->src = AR_SRC_STALE;
    return;
  }
  MsvCache& c = s_msv[i];
  if (c.valid && c.time == t && c.size == fa.nFileSizeLow) { *f = c.found; *nf = c.nukes; *kf = c.kills; return; }
  s_msvReads++;
  char vpath[64];
  _snprintf_s(vpath, sizeof vpath, _TRUNCATE, "state/%s.msv", lower);
  int n = vfs_read_all(vpath);
  if (n > 0) s_msvVfs++;
  else if ((n = win32_read_all(disk)) > 0) s_msvWin32++;
  ArMsvRec recs[AR_SECRETS_MAX + 1];
  int dropped = 0;
  const int nr = n > 0 ? ar_msv_parse(s_buf, n, recs, AR_SECRETS_MAX + 1, &dropped) : 0;
  c.nukeRecs = c.nukeUnnamed = c.monRecs = 0;
  if (n > 0) {
    ar_found_from_msv(&a, recs, nr + dropped, f);
    ar_nukes_from_msv(s_buf, n, &a, nf, &c.nukeRecs, &c.nukeUnnamed);
    ar_kills_from_msv(s_buf, n, &a, kf, &c.monRecs);
  } else {
    memset(f, 0, sizeof *f);
    f->src = AR_SRC_BAD;
    memset(nf, 0, sizeof *nf);
    nf->src = AR_SRC_BAD;
    memset(kf, 0, sizeof *kf);
    kf->src = AR_SRC_BAD;
  }
  if (!f->known) {
    s_msvBad++;
    np_log("areas: %s (%d bytes) did not read cleanly: %d area_secret records (%d dropped) for %d secrets", vpath, n, nr,
           dropped, a.nsecrets);
  }
  if (nf->src == AR_SRC_BAD) s_nukeMsvBad++;
  int have = 0;
  for (int k = 0; k < a.nnukes; ++k) have += nf->have[k];
  np_log("areas: %s nukes: %d nuke records (%d with no pickup's name), sources read %d of %d, %s, %d collected of %d",
         vpath, c.nukeRecs, c.nukeUnnamed, have, a.nnukes, nf->src == AR_SRC_BAD ? "NOT CLEAN" : nf->known ? "known"
                                                                                                 : "partial",
         nf->count, a.nukeTotal);
  np_log("areas: %s kills: %d monst_* records, %s (%d ambush(es), %d wired egg(s): their state is not stored)", vpath,
         c.monRecs, kf->known ? "known" : "unknown", a.nambush, a.neggs);
  c.time = t;
  c.size = fa.nFileSizeLow;
  c.found = *f;
  c.nukes = *nf;
  c.kills = *kf;
  c.valid = 1;
}

static const char* src_name(int src) {
  static const char* const k[] = { "none", "live", "file", "fresh", "stale", "bad", "record", "derived", "settling" };
  return src >= 0 && src <= AR_SRC_SETTLING ? k[src] : "?";
}

// The loaded worlds through the game's list; false when the list does not check out (its current world must be
// the player's), and then only the player's own world is walked.
static int loaded_worlds(const unsigned char** out, int cap) {
  int n = 0;
  const unsigned char* G = g_eng.sys_get_var ? (const unsigned char*)g_eng.sys_get_var("game_globals", 0, 0) : 0;
  const unsigned char* game = 0;
  if (G && guarded_readable(G + kGameOfs, 4)) memcpy(&game, G + kGameOfs, sizeof game);
  const void* cur = 0;
  if (game && guarded_readable(game, kGameSpan)) memcpy(&cur, game + kGameCurWorld, sizeof cur);
  if (!game || cur != s_world) {
    if (!s_listBad++) np_log("areas: world list unread (game_globals %p, game %p, current %p, player's %p)", G, game, cur,
                             s_world);
    out[n++] = (const unsigned char*)s_world;
    return n;
  }
  const unsigned char* node = 0;
  memcpy(&node, game + kGameFirstNode, sizeof node);
  for (int k = 0; node && k < kListMax && n < cap; ++k) {
    if (!guarded_readable(node, 0xC)) break;
    const unsigned char* w = 0;
    memcpy(&w, node + 8, sizeof w);
    if (w) out[n++] = w;
    memcpy(&node, node + 4, sizeof node);
  }
  return n;
}

static void refresh(void) {
  s_refreshes++;
  ArView& v = s_view;
  LsSession ses;
  const bool haveSes = levelstats_session(&ses);
  v.liveOk = haveSes && ses.liveOk;
  v.live = v.liveOk ? (int)ses.live.secrets : 0;
  v.nukeLive = v.liveOk ? (int)ses.live.nukes : 0;
  // A falling nuke tally means the game reloaded a checkpoint and forgot nukes: our record may hold them.
  if (v.liveOk && s_lastNukeLive >= 0 && v.nukeLive < s_lastNukeLive) drop_records("the live nuke tally fell");
  if (v.liveOk) s_lastNukeLive = v.nukeLive;
  for (int i = 0; i < AR_AREAS_MAX; ++i) {
    memset(&v.found[i], 0, sizeof v.found[i]);
    memset(&v.nfound[i], 0, sizeof v.nfound[i]);
    memset(&v.kills[i], 0, sizeof v.kills[i]);
  }
  const unsigned char* worlds[kListMax];
  const int nw = loaded_worlds(worlds, kListMax);
  int loaded = 0;
  for (int k = 0; k < nw; ++k) {
    const unsigned char* w = worlds[k];
    if (!guarded_readable(w, kWorldSpan) || !memchr(w + kWorldName, 0, 0x40)) continue;
    const int i = ar_area_index(&s_part, (const char*)w + kWorldName);
    if (i < 0 || v.found[i].src != AR_SRC_NONE) continue;
    int sid;
    memcpy(&sid, w + kWorldSid, sizeof sid);
    walk_world(sid, &s_part.areas[i], &v.found[i]);
    const ULONGLONG since = w == s_world ? GetTickCount64() - s_stageMs : ~0ull;
    walk_nukes(sid, &s_part.areas[i], &v.nfound[i], since);
    if (v.nfound[i].known && !s_dead) s_rec[i] = v.nfound[i];
    else if (v.nfound[i].src == AR_SRC_BAD && s_rec[i].known) {   // the walk failed: our last view of it
      v.nfound[i] = s_rec[i];
      v.nfound[i].src = AR_SRC_RECORD;
    }
    walk_kills(sid, &s_part.areas[i], &v.kills[i], since);
    if (v.kills[i].known && !s_dead) s_krec[i] = v.kills[i];
    else if (v.kills[i].src == AR_SRC_BAD && s_krec[i].known) {
      v.kills[i] = s_krec[i];
      v.kills[i].src = AR_SRC_RECORD;
    }
    loaded++;
  }
  for (int i = 0; i < s_part.nareas; ++i) {
    if (v.found[i].src != AR_SRC_NONE) continue;
    ArNukeFound file;
    ArKills kfile;
    from_file(i, &v.found[i], &file, &kfile);
    ar_nukes_merge(&s_part.areas[i], &file, s_rec[i].known ? &s_rec[i] : 0, &v.nfound[i]);
    ar_kills_merge(&kfile, s_krec[i].known ? &s_krec[i] : 0, &v.kills[i]);
  }
  // Kills: the areas' total against the part's listed kills (the corner readout's), and the known areas' sum.
  v.killLive = v.liveOk ? (int)ses.live.kills : 0;
  v.killTotal = v.killSum = 0;
  for (int i = 0; i < s_part.nareas; ++i) {
    v.killTotal += s_part.areas[i].killTotal;
    v.killSum += v.kills[i].known ? v.kills[i].found : 0;
  }
  v.killGame = -1;
  {
    int ep = 0, pt = 0;
    LsTotals lt;
    const LsDef* d = levelstats_def();
    ls_map_episode_part(s_part.prefix, &ep, &pt);
    if (haveSes && d && ep < LS_EPISODES && pt < LS_PARTS && ses.diff >= 0 && ses.diff < LS_DIFFS &&
        ls_totals(d, ep, pt, ses.diff, &lt))
      v.killGame = lt.kills;
    static char said[8];
    static int saidTotal = -1, saidGame = -1;
    if (v.killGame >= 0 && (strcmp(said, s_part.prefix) || saidTotal != v.killTotal || saidGame != v.killGame)) {
      strcpy_s(said, sizeof said, s_part.prefix);
      saidTotal = v.killTotal;
      saidGame = v.killGame;
      const int extra = d->extra[ep][pt][ses.diff];
      np_log("areas: kills of %s: the areas place %d, the game lists %d (killcount %d + extrakills %d)%s", s_part.prefix,
             v.killTotal, v.killGame, d->kills[ep][pt], extra,
             v.killGame == v.killTotal ? ""
             : extra > 0 && v.killGame > v.killTotal ? ": extrakills are the boss fight's spawns, which no world.scr places"
             : v.killGame > v.killTotal ? ": the part lists more than its maps hold (kills-and-spawners.md section 4)"
                                        : ": the maps place more than the part lists (a surplus, section 4)");
    }
  }
  ar_route_texts(&s_part, v.cur, v.crossOk ? &v.cross : 0, v.route);
  v.tally = v.liveOk ? ar_tally(&s_part, v.found, v.live, &v.foundSum) : AR_TALLY_UNKNOWN;
  if (!v.liveOk) ar_tally(&s_part, v.found, 0, &v.foundSum);
  const ArTotals t = ar_totals(&s_part);
  v.secretTotal = t.secrets;
  v.nukeTotal = t.nukes;
  v.nukePart = s_partNukes;
  v.nukeTally = ar_nuke_tally(&s_part, v.nfound, v.nukeLive, v.liveOk, s_partNukes, &v.nukeSum, &v.nukeDerived);
  v.foundWorld = s_world;
  char line[1024];
  int len = _snprintf_s(line, sizeof line, _TRUNCATE, "areas: found in %s:", s_part.prefix);
  for (int i = 0; i < s_part.nareas && len >= 0; ++i) {
    const ArArea& a = s_part.areas[i];
    char missing[96];
    ar_missing_text(&a, &v.found[i], missing, sizeof missing);
    // Enemy-carried and laser nukes apart: E taken/carried by the area's enemies, L taken/listed (+ fired again).
    int carried = 0, taken = 0, lz = 0, lzTaken = 0;
    for (int k = 0; k < a.nnukes; ++k)
      if (a.nukes[k].holder == AR_NK_ENEMY) {
        carried++;
        taken += v.nfound[i].known && v.nfound[i].got[k];
      } else if (a.nukes[k].holder == AR_NK_LASER) {
        lz += a.nukes[k].count;
        lzTaken += v.nfound[i].known && v.nfound[i].got[k] ? a.nukes[k].count : 0;
      }
    char ek[48] = "";
    int ekLen = carried ? _snprintf_s(ek, sizeof ek, _TRUNCATE, ", E %d/%d", taken, carried) : 0;
    if (lz && ekLen >= 0)
      _snprintf_s(ek + ekLen, sizeof ek - ekLen, _TRUNCATE, ", L %d/%d (+%d)", lzTaken, lz, v.nfound[i].extra);
    char nk[96];
    if (v.nfound[i].known)
      _snprintf_s(nk, sizeof nk, _TRUNCATE, "N %d/%d %s%s", v.nfound[i].count, a.nukeTotal, src_name(v.nfound[i].src),
                  ek);
    else
      _snprintf_s(nk, sizeof nk, _TRUNCATE, "N ?/%d %s%s", a.nukeTotal, src_name(v.nfound[i].src), carried ? ", E ?" : "");
    char kk[48];
    ar_kills_text(kk, sizeof kk, v.kills[i].found, a.killTotal, v.kills[i].known);
    const int kn = (int)strlen(kk);
    _snprintf_s(kk + kn, sizeof kk - kn, _TRUNCATE, " %s, %d waiting", src_name(v.kills[i].src), v.kills[i].waiting);
    const int k = v.found[i].known
                      ? _snprintf_s(line + len, sizeof line - len, _TRUNCATE, " %s %d/%d (%s%s%s; %s; %s)", ar_label(&a),
                                    ar_found_count(&a, &v.found[i]), a.nsecrets, src_name(v.found[i].src),
                                    missing[0] ? "; missing " : "", missing, nk, kk)
                      : _snprintf_s(line + len, sizeof line - len, _TRUNCATE, " %s ?/%d (%s; %s; %s)", ar_label(&a),
                                    a.nsecrets, src_name(v.found[i].src), nk, kk);
    if (k < 0) break;
    len += k;
  }
  static const char* const kTally[] = { "ok", "MISMATCH", "unknown" };
  if (len >= 0)
    _snprintf_s(line + len, sizeof line - len, _TRUNCATE, "; sum %d, live tally %d%s: %s; nukes sum %d of %d in the "
                "areas, live tally %d of the part's %d: %s%s; kills sum %d of %d in the areas, live tally %d of the "
                "game's %d", v.foundSum, v.live, v.liveOk ? "" : " (unread)",
                kTally[v.tally], v.nukeSum, v.nukeTotal, v.nukeLive, s_partNukes, kTally[v.nukeTally],
                v.nukeDerived >= 0 ? " (one area derived)" : "", v.killSum, v.killTotal, v.killLive, v.killGame);
  if (strcmp(line, s_lastLog) != 0) {
    strcpy_s(s_lastLog, sizeof s_lastLog, line);
    if (v.tally == AR_TALLY_MISMATCH) s_mismatches++;
    if (v.nukeTally == AR_TALLY_MISMATCH) s_nukeMismatches++;
    if (v.nukeDerived >= 0) s_derived++;
    np_log("%s", line);
    np_log("areas: %d loaded world(s) walked of %d listed; refreshes %u, walks %u (capped %u), msv reads %u (vfs %u, "
           "win32 %u), stale %u, bad %u, list unread %u, mismatches %u; nuke walks %u (bad %u), msv not clean %u, "
           "record ticks %u, record drops %u, nuke mismatches %u, derived %u, settling %u, carrier lists unread %u, "
           "laser lookups %u (laser unfound %u, spawner unfound %u); kill walks %u (bad %u, settling %u), ambush "
           "spawners unfound %u",
           loaded, nw, s_refreshes, s_walks, s_walkCapped, s_msvReads, s_msvVfs, s_msvWin32, s_msvStale, s_msvBad,
           s_listBad, s_mismatches, s_nukeWalks, s_nukeWalkBad, s_nukeMsvBad, s_recTicks, s_recDrops, s_nukeMismatches,
           s_derived, s_nukeSettling, s_carryUnread, s_laserLookups, s_laserUnfound, s_spawnerUnfound, s_killWalks,
           s_killWalkBad, s_killSettling, s_ambUnfound);
  }
}

static void state_dir(void) {
  char exe[MAX_PATH];
  const DWORD n = GetModuleFileNameA(NULL, exe, MAX_PATH);
  if (!n || n >= MAX_PATH) return;
  char* slash = strrchr(exe, '\\');
  if (!slash) return;
  slash[1] = 0;
  _snprintf_s(s_stateDir, sizeof s_stateDir, _TRUNCATE, "%sduke\\base\\state\\", exe);
}

// A new world: its part (parsed once per part), its index and the doorway Duke came in by.
static void stage(const unsigned char* world) {
  s_world = world;
  s_stageMs = GetTickCount64();
  if (!s_stateDir[0]) state_dir();
  char name[64] = "";
  if (guarded_readable(world + kWorldName, sizeof name) && memchr(world + kWorldName, 0, sizeof name))
    strcpy_s(name, sizeof name, (const char*)world + kWorldName);
  char prefix[8] = "";
  if (!ar_map_part(name, prefix)) {
    s_prefix[0] = 0;   // a stats screen or a sky: the next part (even this one again) starts anew
    s_partOk = 0;
    s_view.ok = 0;
    return;
  }
  if (strcmp(prefix, s_prefix) != 0) {
    strcpy_s(s_prefix, sizeof s_prefix, prefix);
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    s_partStart = ft64(now);
    load_part(prefix);
  }
  ArView& v = s_view;
  v.part = &s_part;
  v.ok = s_partOk;
  v.cur = ar_area_index(&s_part, name);
  v.entry[0] = 0;
  v.crossOk = 0;   // the door label chooses again once the found state is for this area
  LsSession ses;
  const bool haveSes = levelstats_session(&ses);
  const bool sessionChanged = haveSes && s_sessionS && ses.S != s_sessionS;
  if (haveSes && ses.S) s_sessionS = ses.S;
  if (haveSes && ses.S && guarded_readable(ses.S + kEntryMap, kEntrySpan)) {
    char m[33], d[33];
    memcpy(m, ses.S + kEntryMap, 32); m[32] = 0;
    memcpy(d, ses.S + kEntryDest, 32); d[32] = 0;
    if (_stricmp(m, name) == 0) strcpy_s(v.entry, sizeof v.entry, d);
    np_log("areas: area %s (%d of %d), entered by %s (respawn record %s:%s)", name, v.cur + 1, s_part.nareas,
           v.entry[0] ? v.entry : "-", m, d);
  } else {
    np_log("areas: area %s (%d of %d), respawn record unread", name, v.cur + 1, s_part.nareas);
  }
  const bool byDoor = v.cur >= 0 && ar_entry_is_door(&s_part.areas[v.cur], v.entry);
  const bool arrived = world != s_lastWorld;
  s_lastWorld = world;
  if (ar_records_stale(sessionChanged, 0, arrived, byDoor))
    drop_records(sessionChanged ? "a new session record" : "Duke arrived without a doorway (a load or a respawn)");
  levelstats_demand();
  s_refreshDue = 2;
}

// The current area's nukes into our record once a second, whether or not the Progress tab is open: an area left
// before the next refresh keeps what Duke took there (up to a second's worth may be missed).
static void record_tick(const unsigned char* world) {
  const int cur = s_view.cur;
  if (cur < 0 || cur >= s_part.nareas) return;
  int sid;
  memcpy(&sid, world + kWorldSid, sizeof sid);   // the stage read the world's name through the same span
  if (s_part.areas[cur].killTotal) {
    ArKills k;
    walk_kills(sid, &s_part.areas[cur], &k, GetTickCount64() - s_stageMs);
    if (k.known) s_krec[cur] = k;
  }
  if (!s_part.areas[cur].nnukes) return;
  ArNukeFound f;
  walk_nukes(sid, &s_part.areas[cur], &f, GetTickCount64() - s_stageMs);
  if (!f.known) return;
  s_rec[cur] = f;
  s_recTicks++;
}

void areas_frame(const unsigned char* player) {
  if (!player) { s_world = 0; return; }
  int killed, ps;
  memcpy(&killed, player + 0x90, sizeof killed);   // inside probe's validated span, as entities_frame reads it
  if (killed) return;
  memcpy(&ps, player + kPlayerPs, sizeof ps);
  const int dead = ps >= 20 && ps <= 22;
  if (dead && !s_dead) drop_records("Duke died; the game reloads its checkpoint");
  s_dead = dead;
  const unsigned char* world = 0;
  memcpy(&world, player + 0xE0, sizeof world);
  if (!world) return;
  if (ar_records_stale(0, s_player && player != s_player && world == s_lastWorld, 0, 0))
    drop_records("a new player entity in the same world (a respawn in place)");
  s_player = player;
  if (world != s_world) stage(world);
  if (!s_partOk || !g_eng.spawn_search || !g_eng.spawn_search_get || !g_eng.spawn_search_stop) return;
  const ULONGLONG now = GetTickCount64();
  if (s_refreshDue > 1) {
    s_refreshDue--;
  } else if (s_refreshDue || (now < s_demandUntil && now >= s_nextRefresh)) {
    s_refreshDue = 0;
    s_nextRefresh = now + kRefreshMs;
    s_nextRecord = now + kRecordMs;   // the refresh recorded every loaded area
    refresh();
  } else if (now >= s_nextRecord && !s_dead && guarded_readable(world, kWorldSpan)) {
    s_nextRecord = now + kRecordMs;
    record_tick(world);
  }
}

void areas_set_cross(const void* world, const ArCross* c) {
  ArView& v = s_view;
  const int ok = c && v.ok && world && world == s_world && world == v.foundWorld;
  if (ok == v.crossOk && (!ok || memcmp(c, &v.cross, sizeof v.cross) == 0)) return;
  v.crossOk = ok;
  if (ok) v.cross = *c;
  ar_route_texts(&s_part, v.cur, ok ? &v.cross : 0, v.route);
}

void areas_demand(void) { s_demandUntil = GetTickCount64() + kDemandMs; }

const ArView* areas_view(void) { return &s_view; }

unsigned areas_msv_reads(void) { return s_msvReads; }
