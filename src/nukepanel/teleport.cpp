// The freecam teleport (freecam-teleport.md sections 1-2): each frame a world-only ray from the freecam eye along the
// crosshair, the placement rules (teleport.h) and a clearance sweep; the Teleport key calls player_teleport. In normal
// play the target is the hit snapped to the nearest track of the level (section 5). Main thread, frame path only.
#include "teleport.h"
#include "track.h"
#include "freecam.h"
#include "input.h"
#include "engine.h"
#include "entities.h"
#include "espbox.h"   // hitbox_from
#include "ui/common.h"   // clamp_double_to_int
#include "volume.h"
#include "view.h"
#include "canvas.h"
#include "config.h"
#include "moveanywhere.h"   // moveanywhere_refused: a refused switch leaves Duke on his track
#include "firstperson.h"    // firstperson_view_held, firstperson_hold_track_off: a teleport out of first person
#include "log.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

// player_teleport is actor_player's vtable slot +0x218 (duke!0x1007d830), thiscall (pos, angles),
// RET 8. The one hardcoded constant, justified by the engine's own three callers: area_teleport
// 0x1009d8ed, the map change 0x1009ad00 and the launcher 0x100afa50 (freecam-teleport.md section 1).
static const unsigned kVtTeleport = 0x218;
// Its entry (push esi; push 1; mov esi,ecx; call): another build or class is refused, not called.
static const unsigned char kTeleportEntry[6] = { 0x56, 0x6A, 0x01, 0x8B, 0xF1, 0xE8 };
typedef void (__thiscall* TeleportFn)(void* self, const float* pos, const float* angles);

static const float kRayLen = 8192.f;   // world units, about 160 m
// The clearance sweep comes down onto the target from this high: a zero-length sweep returns early
// (phys_sweep_vs_objects), and a longer one would refuse spots with little headroom.
static const float kClearLift = 2.f;
// Engine type bit 0x100 = world_e; the denies are the camera's own (game_track_place_camera).
static const unsigned kDenyUser[4] = { 0x8000, 0x10000, 0x20000, 0x400 };
// The ray record read at once: +0x04 normal, +0x14 hit point, ..., +0x50 distance.
static const unsigned kRecSpan = 0x54;

// Duke's current game_track (B1) and its fields (B2, game_track_tessellate 0x100945c0): +0xAFC route
// point count, +0xB24 looped byte, +0xB28 int samples-per-span array, +0xB2C sample rows, +0xB30 stride.
static const unsigned kOfsTrack = 0xB20;
static const unsigned kTrkN = 0xAFC, kTrkLooped = 0xB24, kTrkCounts = 0xB28, kTrkSamples = 0xB2C,
                      kTrkStride = 0xB30, kTrkSpan = 0xB34;
// track vt+0x1EC = game_track_project_point (B4): the slot is the engine's own, called through
// `FF 92 EC 01 00 00` at 16 sites; the entry is fingerprinted (track.h) before any call.
static const unsigned kVtProject = 0x1EC;
typedef void (__thiscall* ProjectFn)(void* track, float* inout);

// +0xAE4: the track's area volume; actor_pick_track skips a track without one in both passes.
static const unsigned kTrkVolume = 0xAE4;
typedef const char* (__thiscall* ClassnameFn)(const void* self);

static TrkPt s_trkRaw[TRK_MAX_PTS], s_trkPts[TRK_MAX_PTS];
static int s_trkCounts[TRK_MAX_PTS];
static const void* s_lastTrack;
static const void* s_projChecked;   // the vt+0x1EC entry last fingerprinted, and its verdict
static int s_projOk;
static int s_trackFloorLogged;
static unsigned s_trkFree, s_trkChosen, s_trkNone, s_trkUnread, s_trkTies;

struct Plan { int have, reason, hasHit; Vec3 hit, n, pos, dim; float o; const unsigned char* track; };
static Plan s_plan;
static unsigned s_casts, s_byReason[TP_REASON_COUNT], s_presses, s_teleports, s_gateRefused, s_entryRefused;
static int s_normalLogged;
static ULONGLONG s_nextLog;

static Vec3 rdv(const unsigned char* p) { Vec3 v; memcpy(&v, p, sizeof v); return v; }
static float rdf(const unsigned char* p) { float f; memcpy(&f, p, sizeof f); return f; }
static unsigned rdu(const unsigned char* p) { unsigned u; memcpy(&u, p, sizeof u); return u; }

// Sized past the ctor's 0x24 bytes; the camera gives it a 0x30 stack slot.
struct Matcher { __declspec(align(8)) unsigned char b[0x40]; };

static void matcher_build(Matcher* m) {
  g_p3.tm_ctor(m->b);
  g_p3.tm_reset_rules(m->b, false);
  g_p3.tm_accept_superset(m->b, 0x100, 0, true);
  // Separate calls: "superset" needs every listed user bit present (tmatcher_t::validate).
  for (int i = 0; i < 4; ++i) g_p3.tm_deny_superset(m->b, 0x100, kDenyUser[i], false);
}

// One world-only ray; copies the record's normal and hit point at once, since the next
// physics query overwrites it. False when it hit nothing.
static bool ray(void* world, const float* from, const float* to, Matcher* m, Vec3* n, Vec3* hit) {
  const unsigned char* rec = (const unsigned char*)g_eng.phys_ray_test(world, from, to, m->b, m->b);
  if (!rec || !guarded_readable(rec, kRecSpan)) return false;
  *n = rdv(rec + 0x04);
  *hit = rdv(rec + 0x14);
  return true;
}

// The verified vt+0x1EC of a track whose header (kTrkSpan bytes) is already known readable, or 0 with *why set.
static ProjectFn project_fn(const unsigned char* t, const char** why) {
  const void* const* vt;
  memcpy(&vt, t, sizeof vt);
  const void* const* slot = vt + kVtProject / 4;
  if (!entities_in_duke(slot) || !entities_in_duke((const unsigned char*)(slot + 1) - 1)) {
    *why = "vtable outside duke_base";
    return 0;
  }
  const unsigned char* f = (const unsigned char*)*slot;
  if (f != s_projChecked) {
    s_projChecked = f;
    s_projOk = entities_in_duke(f) && guarded_readable(f, TRK_PROJECT_FP_LEN) && trk_fp_is_project(f);
    np_log("teleport: track vt+0x1EC -> %p %s", (const void*)f,
           s_projOk ? "is game_track_project_point" : "fingerprint refused");
  }
  if (!s_projOk) { *why = "vt+0x1EC is not game_track_project_point"; return 0; }
  return (ProjectFn)f;
}

// The track's polyline (one guarded read each of the header, counts and rows) and its verified
// vt+0x1EC. Returns the flattened point count, or -1 with *why set.
static int track_read(const unsigned char* t, ProjectFn* fn, const char** why) {
  static char s_why[160];
  if (!guarded_readable(t, kTrkSpan)) { *why = "header: track object unreadable (0xB34 bytes)"; return -1; }
  if (!(*fn = project_fn(t, why))) return -1;
  const int n = (int)rdu(t + kTrkN), stride = (int)rdu(t + kTrkStride);
  const unsigned char* counts = (const unsigned char*)(size_t)rdu(t + kTrkCounts);
  const unsigned char* rows = (const unsigned char*)(size_t)rdu(t + kTrkSamples);
  if (n < 2 || stride < 1 || n > TRK_MAX_PTS || stride > TRK_MAX_PTS || n * stride > TRK_MAX_PTS) {
    sprintf_s(s_why, "header: n=%d stride=%d (n*stride cap %d)", n, stride, (int)TRK_MAX_PTS);
    *why = s_why;
    return -1;
  }
  if (!counts || !guarded_readable(counts, (size_t)n * 4)) {
    sprintf_s(s_why, "counts: %p unreadable for %d ints (n=%d stride=%d)", (const void*)counts, n, n, stride);
    *why = s_why;
    return -1;
  }
  if (!rows || !guarded_readable(rows, (size_t)n * stride * sizeof(TrkPt))) {
    sprintf_s(s_why, "rows: %p unreadable for %d samples (n=%d stride=%d)", (const void*)rows, n * stride, n, stride);
    *why = s_why;
    return -1;
  }
  memcpy(s_trkCounts, counts, (size_t)n * 4);
  memcpy(s_trkRaw, rows, (size_t)n * stride * sizeof(TrkPt));
  const int m = trk_flatten(s_trkRaw, n, stride, s_trkCounts, t[kTrkLooped] != 0, s_trkPts, TRK_MAX_PTS);
  if (m < 2) {
    int bad = 0;
    while (bad < n && s_trkCounts[bad] >= 0 && s_trkCounts[bad] + 1 <= stride) ++bad;
    sprintf_s(s_why, "counts: counts[%d]=%d against stride %d (n=%d looped=%u)", bad, bad < n ? s_trkCounts[bad] : -1,
              stride, n, t[kTrkLooped]);
    *why = s_why;
    return -1;
  }
  return m;
}

// Once per track: the layout and the engine's snap of Duke's own position.
static void track_changed(const unsigned char* player, const unsigned char* t) {
  s_lastTrack = t;
  if (!t) { np_log("teleport: track 0 (free placement)"); return; }
  ProjectFn fn = 0;
  const char* why = 0;
  const int m = track_read(t, &fn, &why);
  if (m < 0) { np_log("teleport: track %p unread: %s", (const void*)t, why); return; }
  const Vec3 me = rdv(player + 0x18C);
  float q[3] = { me.x, me.y, me.z };
  fn((void*)t, q);
  np_log("teleport: track %p n=%u stride=%u looped=%u counts[0]=%d points=%d first (%.1f,%.1f,%.1f,%.1f) "
         "(%.1f,%.1f,%.1f,%.1f); Duke (%.1f,%.1f,%.1f) snaps to (%.1f,%.1f,%.1f)", (const void*)t, rdu(t + kTrkN),
         rdu(t + kTrkStride), t[kTrkLooped], s_trkCounts[0], m, s_trkRaw[0].x, s_trkRaw[0].y, s_trkRaw[0].z,
         s_trkRaw[0].len, s_trkRaw[1].x, s_trkRaw[1].y, s_trkRaw[1].z, s_trkRaw[1].len, me.x, me.y, me.z, q[0], q[1],
         q[2]);
}

// Every walkable track of the level, read once per freecam flight (and again when the player or his world changes):
// the game_track areas of actor_pick_track's own query, with a volume, flattened into one pool. game_track_auto is
// left out: it is the depth run's connector (state 4), and the pick never leaves one it holds.
enum { TRK_CACHE_MAX = 32, TRK_POOL_PTS = 16384, TRK_AREAS_MAX = 1024 };
struct TrkEntry { const unsigned char* t; int first, n; };
static TrkEntry s_trks[TRK_CACHE_MAX];
static TrkPt s_pool[TRK_POOL_PTS];
static TrkCand s_cand[TRK_CACHE_MAX];
static int s_ntrks, s_trksValid, s_trksWanted = 1;   // wanted: set at each freecam start
static const unsigned char* s_trksPlayer;
static const void* s_trksWorld;
static unsigned s_enums;

static void tracks_enumerate(const unsigned char* player) {
  s_ntrks = 0;
  s_trksValid = 1;
  s_trksWanted = 0;
  s_trksPlayer = player;
  memcpy(&s_trksWorld, player + 0xE0, sizeof s_trksWorld);
  s_enums++;
  if (!g_eng.spawn_search || !g_eng.spawn_search_get || !g_eng.spawn_search_stop) {
    np_log("teleport: tracks: spawn_search unresolved, none read");
    return;
  }
  int areas = 0, tracks = 0, autos = 0, novol = 0, unread = 0, over = 0, used = 0;
  ReadCache rc = {};
  g_eng.spawn_search("s=%d?t=%d?Y=%d", (int)rdu(player + 0x78), 0x20, 1);   // actor_pick_track 0x1008e10b's query
  const unsigned char* e;
  while ((e = (const unsigned char*)g_eng.spawn_search_get()) != 0 && areas < TRK_AREAS_MAX) {
    areas++;
    if (!guarded_readable_cached(&rc, e, 4)) continue;
    void* const* vt;
    memcpy(&vt, e, sizeof vt);
    if (!entities_in_duke(vt) || !entities_in_duke(vt + 2) || !entities_in_duke(vt[1])) continue;
    const char* cls = ((ClassnameFn)vt[1])(e);
    if (!entities_in_duke(cls)) continue;
    if (!strcmp(cls, "game_track_auto")) { autos++; continue; }
    if (strcmp(cls, "game_track")) continue;
    tracks++;
    if (!guarded_readable_cached(&rc, e, kTrkSpan)) { unread++; continue; }
    if (!rdu(e + kTrkVolume)) { novol++; continue; }
    ProjectFn fn = 0;
    const char* why = 0;
    const int m = track_read(e, &fn, &why);
    if (m < 2) { unread++; np_log("teleport: tracks: %p unread: %s", (const void*)e, why ? why : "?"); continue; }
    if (s_ntrks >= TRK_CACHE_MAX || used + m > TRK_POOL_PTS) { over++; continue; }
    memcpy(s_pool + used, s_trkPts, (size_t)m * sizeof(TrkPt));
    TrkEntry& te = s_trks[s_ntrks++];
    te.t = e; te.first = used; te.n = m;
    used += m;
  }
  g_eng.spawn_search_stop();   // start/stop paired on every path
  np_log("teleport: tracks: %d cached (%d points) of %d game_track: no volume %d, unread %d, over the cap %d; "
         "game_track_auto skipped %d; areas walked %d (enumeration %u)", s_ntrks, used, tracks, novol, unread, over,
         autos, areas, s_enums);
  for (int k = 0; k < s_ntrks; ++k) {
    const TrkPt* p = s_pool + s_trks[k].first;
    const int n = s_trks[k].n;
    np_log("teleport: tracks: [%d] %p %d points, first (%.1f,%.1f) last (%.1f,%.1f)", k, (const void*)s_trks[k].t, n,
           p[0].x, p[0].z, p[n - 1].x, p[n - 1].z);
  }
}

// The crosshair's world hit snapped, in x/z, to the nearest cached track (ties: the floor nearest the hit's height),
// then onto the engine's own line (vt+0x1EC) and dropped to the floor below. None within the radius is red at the hit.
static TpPlace anytrack_place(void* world, Matcher* m, Plan* p) {
  TpPlace pl = { TP_NO_HIT, { 0.f, 0.f, 0.f }, 0 };
  if (!p->hasHit) { s_trkNone++; return pl; }
  if (!s_trksValid || !s_ntrks) { pl.reason = TP_TRACK_UNREAD; s_trkUnread++; return pl; }
  for (int k = 0; k < s_ntrks; ++k) {
    const TrkHit h = trk_nearest_xz(p->hit, s_pool + s_trks[k].first, s_trks[k].n);
    s_cand[k].ok = h.ok;
    s_cand[k].d = h.s;
    s_cand[k].dy = 0.f;
  }
  const float best = trk_nearest_d(s_cand, s_ntrks);
  int band = 0;
  for (int k = 0; k < s_ntrks; ++k)
    if (s_cand[k].ok && s_cand[k].d <= TP_ANYTRACK_RADIUS && s_cand[k].d <= best + TP_TRACK_TIE) band++;
  if (band > 1) {   // a tie: each one's floor below its nearest point, from the hit's height
    s_trkTies++;
    for (int k = 0; k < s_ntrks; ++k) {
      if (!s_cand[k].ok || s_cand[k].d > TP_ANYTRACK_RADIUS || s_cand[k].d > best + TP_TRACK_TIE) continue;
      const TrkHit h = trk_nearest_xz(p->hit, s_pool + s_trks[k].first, s_trks[k].n);
      const float from[3] = { h.p.x, p->hit.y + p->dim.y, h.p.z }, below[3] = { h.p.x, p->hit.y - TP_DROP_LEN, h.p.z };
      Vec3 dn, dh;
      s_cand[k].dy = ray(world, from, below, m, &dn, &dh) ? fabsf(dh.y - p->hit.y) : 3.4e38f;
    }
  }
  const int w = trk_choose(s_cand, s_ntrks, TP_ANYTRACK_RADIUS, TP_TRACK_TIE);
  if (w < 0) { pl.reason = TP_OFF_TRACK; s_trkNone++; return pl; }   // red footprint at the world hit
  const unsigned char* t = s_trks[w].t;
  ProjectFn fn = 0;
  const char* why = 0;
  if (!guarded_readable(t, kTrkSpan) || !(fn = project_fn(t, &why))) {
    // Freed or reused since the enumeration: the cache is dropped and read again on the next cast.
    np_log("teleport: tracks: cached track %p no longer reads as a track (%s); cache dropped", (const void*)t,
           why ? why : "header unreadable");
    s_trksValid = 0;
    s_ntrks = 0;
    pl.reason = TP_TRACK_UNREAD;
    s_trkUnread++;
    return pl;
  }
  s_trkChosen++;
  float q[3] = { p->hit.x, p->hit.y, p->hit.z };
  fn((void*)t, q);   // x/z onto the engine's own line; y kept
  // Down from Duke's half height above the hit, so a hit just under a floor's edge still finds it.
  const float from[3] = { q[0], q[1] + p->dim.y, q[2] };
  const float below[3] = { q[0], q[1] - TP_DROP_LEN, q[2] };
  Vec3 dn = { 0.f, 0.f, 0.f }, dh = dn;
  const bool found = ray(world, from, below, m, &dn, &dh);
  const TpPlace spot = { TP_OK, { q[0], q[1], q[2] }, 1 };
  if (found && !s_trackFloorLogged) {
    s_trackFloorLogged = 1;
    np_log("teleport: first track floor: track %p point (%.1f,%.1f,%.1f) floor (%.1f,%.1f,%.1f) n=(%.3f,%.3f,%.3f)",
           (const void*)t, q[0], q[1], q[2], dh.x, dh.y, dh.z, dn.x, dn.y, dn.z);
  }
  pl = tp_drop(spot, found ? &dh : 0, dn, p->dim);
  p->track = t;
  p->hit = spot.pos;   // a refused spot draws its red footprint on the chosen line
  if (found) p->n = dn;
  return pl;
}

// Cast, place, drop a wall spot to its floor and check the clearance; fills s_plan. player is
// the snapshot's validated player, readable for kPlayerSpan (0xED0) bytes. Returns why nothing
// was cast, or 0.
static const char* cast(const unsigned char* player, const float eye[3], const float fwd[3]) {
  void* world;
  memcpy(&world, player + 0xE0, sizeof world);   // the object's world (proj_bullet_hitscan_ray reads it so)
  if (!world) return "the player's world pointer (+0xE0) is null";
  if (!guarded_readable((const unsigned char*)world + 0x4B8, 4)) return "the world's physics slot (+0x4B8) is unreadable";
  const float to[3] = { eye[0] + fwd[0] * kRayLen, eye[1] + fwd[1] * kRayLen, eye[2] + fwd[2] * kRayLen };
  Matcher m;
  matcher_build(&m);
  Plan p;
  memset(&p, 0, sizeof p);
  p.have = 1;
  p.hasHit = ray(world, eye, to, &m, &p.n, &p.hit) ? 1 : 0;
  p.dim = rdv(player + 0x2B8);
  p.o = rdf(player + 0x2C4);
  const Vec3 dir = { fwd[0], fwd[1], fwd[2] };
  const unsigned char* track = (const unsigned char*)(size_t)rdu(player + kOfsTrack);
  if (track != s_lastTrack) track_changed(player, track);
  TpPlace pl;
  const bool locked = tp_track_locked(track != 0, g_cfg.firstPerson, g_cfg.moveAnywhere && !moveanywhere_refused());
  if (locked) {
    // The level's tracks once per flight, again when the player or his world changed (a level change), and on the
    // frame after a cached track stopped reading as one (the cache was dropped).
    if (s_trksWanted || !s_trksValid || s_trksPlayer != player || s_trksWorld != world) tracks_enumerate(player);
    pl = anytrack_place(world, &m, &p);
  } else {
    s_trkFree++;
    pl = tp_place(p.hasHit ? &p.hit : 0, p.n, dir, p.dim);
    if (pl.reason == TP_OK && pl.wall) {
      const float below[3] = { pl.pos.x, pl.pos.y - TP_DROP_LEN, pl.pos.z };
      Vec3 dn = { 0.f, 0.f, 0.f }, dh = dn;
      const bool found = ray(world, &pl.pos.x, below, &m, &dn, &dh);
      pl = tp_drop(pl, found ? &dh : 0, dn, p.dim);
    }
  }
  p.reason = tp_depth_refuse(pl.reason, rdu(player + 0xB7C));
  p.pos = pl.pos;
  if (!locked && p.hasHit && !s_normalLogged && fabsf(p.n.y) > TP_FLOOR_NY) {
    s_normalLogged = 1;   // settles freecam-teleport.md Open questions 3: a floor reads (0,1,0)
    np_log("teleport: first floor-like hit n=(%.3f,%.3f,%.3f) at (%.1f,%.1f,%.1f) dir=(%.3f,%.3f,%.3f)",
           p.n.x, p.n.y, p.n.z, p.hit.x, p.hit.y, p.hit.z, dir.x, dir.y, dir.z);
  }
  if (p.reason == TP_OK) {
    const Hitbox hb = hitbox_from(p.pos, p.dim, p.o);
    const float from[3] = { hb.c.x, hb.c.y + kClearLift, hb.c.z };
    if (g_eng.phys_bound_test(world, from, &hb.c.x, &hb.half.x, m.b, m.b)) p.reason = TP_BLOCKED;
  }
  g_p3.tm_dtor(m.b);
  s_plan = p;
  return 0;
}

// player_is_dead_or_locked's reads and the vt+0x218 entry check: player_teleport's verified entry, or 0 with the
// refusal logged under `who`.
static const unsigned char* teleport_entry(const unsigned char* player, const char* who) {
  // [+0x111C] is past kPlayerSpan, so checked here; +0xC10 and +0xAF0 are inside it.
  const unsigned char* stats = 0;
  short health = 0;
  if (guarded_readable(player + 0x111C, 4)) memcpy(&stats, player + 0x111C, sizeof stats);
  if (stats && guarded_readable(stats, 2)) memcpy(&health, stats, sizeof health);
  const unsigned state = rdu(player + 0xC10);
  const unsigned char unlocked = player[0xAF0];
  if (!stats || !tp_gate(health, state, unlocked)) {
    s_gateRefused++;
    np_log("%s: refused by the engine's gate: health=%d state=0x%x +0xAF0=%u", who, health, state, unlocked);
    return 0;
  }
  void* const* vt;
  memcpy(&vt, player, sizeof vt);
  const unsigned char* fn = 0;
  if (vt && guarded_readable(vt + kVtTeleport / 4, sizeof(void*))) fn = (const unsigned char*)vt[kVtTeleport / 4];
  if (!fn || !guarded_readable(fn, sizeof kTeleportEntry) || memcmp(fn, kTeleportEntry, sizeof kTeleportEntry)) {
    s_entryRefused++;
    np_log("%s: refused, vt+0x218 %p is not player_teleport's entry", who, (const void*)fn);
    return 0;
  }
  return fn;
}

// First person holds Duke now: in state 17, or out of it under our ladder or use hold.
static int fp_engaged(const unsigned char* player) {
  return rdu(player + 0xB7C) == (unsigned)FP_ENGINE_STATE || firstperson_view_held() ? 1 : 0;
}

// The freecam teleport's check a frame later (after one tick): how far Duke stands from the asked target.
static const unsigned char* s_checkPlayer;
static Vec3 s_checkAt;
static unsigned s_checkTrack;   // the track the marker chose, 0 in free placement
static unsigned s_checks, s_checksOff, s_trackKept, s_trackLost;

static void check_frame(const unsigned char* player) {
  if (!s_checkPlayer) return;
  const unsigned char* p = s_checkPlayer;
  s_checkPlayer = 0;
  if (p != player) return;
  const Vec3 now = rdv(player + 0x18C);
  const float dx = now.x - s_checkAt.x, dz = now.z - s_checkAt.z, d = sqrtf(dx * dx + dz * dz);
  s_checks++;
  if (d > 1.f) s_checksOff++;
  const unsigned trk = rdu(player + kOfsTrack);
  const char* verdict = "free placement";
  if (s_checkTrack) {
    const int kept = trk == s_checkTrack;
    if (kept) s_trackKept++; else s_trackLost++;
    verdict = kept ? "the chosen track" : "NOT the chosen track";
  }
  np_log("teleport: #%u a frame later at (%.1f,%.1f,%.1f), %.1f from the target in x/z, state %u +0xB1C %u +0xB20 0x%x "
         "(%s 0x%x; kept %u, lost %u; checks %u, off %u)", s_teleports, now.x, now.y, now.z, d, rdu(player + 0xB7C),
         player[0xB1C], trk, verdict, s_checkTrack, s_trackKept, s_trackLost, s_checks, s_checksOff);
}

// Any track: player_teleport's re-pick (actor_teleport -> vt+0x230) keeps the LAST track whose volume holds Duke, so
// overlapping volumes could snap him elsewhere and fire triggers (offscreen-and-track.md B3). So +0xB1C is held at 0
// across the call (duke!0x1007d830) and the chosen track written as the engine would (0x1008e0f0), then restored.
static int chosen_track_ok(const unsigned char* player, const unsigned char* chosen, const char** why) {
  *why = 0;
  if (!player[0xB1C]) { *why = "+0xB1C is 0 (Duke does not follow tracks now)"; return 0; }
  if (!guarded_readable(chosen, kTrkSpan)) { *why = "the track is unreadable"; return 0; }
  return project_fn(chosen, why) != 0;
}

static void set_chosen_track(const unsigned char* player, const unsigned char* chosen, unsigned char follows) {
  unsigned char* w = (unsigned char*)player;
  const unsigned zero = 0, chosenU = (unsigned)(size_t)chosen, minus3 = 0xFFFFFFFDu;
  memcpy(w + 0xB24, &zero, 4);
  memcpy(w + 0xB58, &minus3, 4);
  memcpy(w + 0xB5C, &minus3, 4);
  memcpy(w + kOfsTrack, &chosenU, 4);
  w[0xB1C] = follows;
}

static void teleport_now(const unsigned char* player) {
  if (s_plan.reason != TP_OK) {
    np_log("teleport: refused, %s", tp_reason_text(s_plan.reason));
    return;
  }
  const unsigned state = rdu(player + 0xB7C);
  if (tp_depth_run(state)) {
    np_log("teleport: refused, Duke is in a depth run (state %u)", state);
    return;
  }
  const unsigned char* fn = teleport_entry(player, "teleport");
  if (!fn) return;
  const Vec3 before = rdv(player + 0x18C), ang = rdv(player + 0x1A8);
  const unsigned trackBefore = rdu(player + 0xB20), visBefore = rdu(player + 0x930);
  const int fpEngaged = fp_engaged(player);
  const unsigned char follows = player[0xB1C];
  const char* why = 0;
  const int hold = s_plan.track && !fpEngaged && chosen_track_ok(player, s_plan.track, &why);
  if (s_plan.track && !fpEngaged && !hold)
    np_log("teleport: the chosen track %p is not set, the engine picks: %s", (const void*)s_plan.track,
           why ? why : "?");
  if (hold) ((unsigned char*)player)[0xB1C] = 0;   // no pick or snap inside the call
  ((TeleportFn)fn)((void*)player, &s_plan.pos.x, &ang.x);   // keeps Duke's facing
  s_teleports++;
  const unsigned picked = rdu(player + kOfsTrack);   // 0 when held: actor_teleport cleared it and nothing picked
  if (hold) set_chosen_track(player, s_plan.track, follows);
  if (tp_keep_off_track(fpEngaged, 0)) firstperson_hold_track_off(player, "teleport");
  s_checkPlayer = player;   // where he is a frame later: the next tick's snap would show there
  s_checkAt = s_plan.pos;
  s_checkTrack = (unsigned)(size_t)s_plan.track;
  const Vec3 after = rdv(player + 0x18C);
  // +0xB20 (game_track) and +0x930 (vis sectors) settle freecam-teleport.md Open questions 1 and 2.
  np_log("teleport: #%u (%.1f,%.1f,%.1f) -> (%.1f,%.1f,%.1f) asked (%.1f,%.1f,%.1f) track 0x%x->0x%x (engine's pick "
         "0x%x, chosen 0x%x, %s) vis 0x%x->0x%x", s_teleports, before.x, before.y, before.z, after.x, after.y, after.z,
         s_plan.pos.x, s_plan.pos.y, s_plan.pos.z, trackBefore, rdu(player + kOfsTrack), picked,
         (unsigned)(size_t)s_plan.track, hold ? "set with +0xB1C held" : "not set", visBefore, rdu(player + 0x930));
}

static int teleport_call(const unsigned char* player, const float pos[3], const char* who, int repick) {
  if (!player) return 0;
  const unsigned char* fn = teleport_entry(player, who);
  if (!fn) return 0;
  const Vec3 before = rdv(player + 0x18C), ang = rdv(player + 0x1A8);
  const unsigned trackBefore = rdu(player + 0xB20), b1c = player[0xB1C];
  const int fpEngaged = fp_engaged(player);
  ((TeleportFn)fn)((void*)player, pos, &ang.x);   // his facing kept; the engine re-picks the track
  if (tp_keep_off_track(fpEngaged, repick)) firstperson_hold_track_off(player, who);
  const Vec3 after = rdv(player + 0x18C);
  // F7 (free-move.md): where the re-pick put him, and on which track.
  np_log("%s: player_teleport from (%.1f,%.1f,%.1f) -> (%.1f,%.1f,%.1f) moved %.1f, track 0x%x->0x%x, "
         "+0xB1C %u->%u, state %u", who, before.x, before.y, before.z, after.x, after.y, after.z,
         sqrtf((after.x - before.x) * (after.x - before.x) + (after.y - before.y) * (after.y - before.y) +
               (after.z - before.z) * (after.z - before.z)),
         trackBefore, rdu(player + 0xB20), b1c, player[0xB1C], rdu(player + 0xB7C));
  return 1;
}

int teleport_repick_here(const unsigned char* player, const char* who) {
  if (!player) return 0;
  const Vec3 here = rdv(player + 0x18C);
  return teleport_call(player, &here.x, who, 1);   // his own spot and facing: the engine re-picks the track
}

int teleport_player_to(const unsigned char* player, const float pos[3], const char* who) {
  return teleport_call(player, pos, who, 0);
}

void* teleport_ray_world(const unsigned char* player) {
  if (!player || !g_eng.phys_ray_test || !engine_resolve_p3shared()) return 0;
  void* world;
  memcpy(&world, player + 0xE0, sizeof world);
  if (!world || !guarded_readable((const unsigned char*)world + 0x4B8, 4)) return 0;
  return world;
}

bool teleport_world_hit(const unsigned char* player, const float from[3], const float to[3], float hit[3]) {
  return teleport_world_hit_in(teleport_ray_world(player), from, to, hit);
}

// The record is the first entry of the engine's global collision history (phys_ray_test prism3d!0x40d820 returns
// DAT_0049787c + 8), so the same address comes back each time: it is validated once, not per ray.
static const unsigned char* s_recChecked;

bool teleport_world_hit_in(void* world, const float from[3], const float to[3], float hit[3], float* ny) {
  if (!world) return false;
  Matcher m;
  matcher_build(&m);
  const unsigned char* rec = (const unsigned char*)g_eng.phys_ray_test(world, from, to, m.b, m.b);
  bool found = false;
  if (rec && (rec == s_recChecked || guarded_readable(rec, kRecSpan))) {
    s_recChecked = rec;
    memcpy(hit, rec + 0x14, 12);   // the hit point; copied at once, the next query overwrites it
    if (ny) memcpy(ny, rec + 0x08, 4);   // the normal's y (+0x04 x, +0x08 y, +0x0C z)
    found = true;
  }
  g_p3.tm_dtor(m.b);
  return found;
}

int teleport_track_snap(const unsigned char* player, const void** track, float* dist) {
  const unsigned char* t = (const unsigned char*)(size_t)rdu(player + kOfsTrack);
  *track = t;
  const char* why = 0;
  if (!t || !guarded_readable(t, kTrkSpan)) return 0;
  const ProjectFn fn = project_fn(t, &why);
  if (!fn) return 0;
  const Vec3 me = rdv(player + 0x18C);
  float q[3] = { me.x, me.y, me.z };
  fn((void*)t, q);   // game_track_project_point on a float[3]: x/z where the engine's snap would put him
  *dist = sqrtf((q[0] - me.x) * (q[0] - me.x) + (q[2] - me.z) * (q[2] - me.z));
  return 1;
}

int teleport_track_polyline(const unsigned char* player, const TrkPt** pts) {
  const unsigned char* track = (const unsigned char*)(size_t)rdu(player + kOfsTrack);
  if (!track) return 0;
  ProjectFn fn = 0;
  const char* why = 0;
  const int n = track_read(track, &fn, &why);
  if (n < 2) return -1;
  *pts = s_trkPts;
  return n;
}

// Once per freecam flight: which placement the marker uses, and Duke's track as the engine holds it (in first
// person +0xB20 can name a track at the far end of the level).
static int s_flying;
static void placement_log(const unsigned char* player) {
  const int fp = g_cfg.firstPerson != 0, mv = g_cfg.moveAnywhere != 0 && !moveanywhere_refused();
  const unsigned char* track = (const unsigned char*)(size_t)rdu(player + kOfsTrack);
  const bool locked = tp_track_locked(track != 0, fp, mv);
  const Vec3 me = rdv(player + 0x18C);
  char pts[160] = "no polyline";
  if (track) {
    ProjectFn fn = 0;
    const char* why = 0;
    const int n = track_read(track, &fn, &why);
    if (n >= 2)
      sprintf_s(pts, "%d points, first (%.1f,%.1f,%.1f) last (%.1f,%.1f,%.1f)", n, s_trkPts[0].x, s_trkPts[0].y,
                s_trkPts[0].z, s_trkPts[n - 1].x, s_trkPts[n - 1].y, s_trkPts[n - 1].z);
    else
      sprintf_s(pts, "unread: %s", why ? why : "?");
  }
  np_log("teleport: freecam start: placement %s (first person %d, move anywhere %d); Duke (%.1f,%.1f,%.1f) state %u "
         "+0xB1C %u, +0xB20 %p: %s", locked ? "any-track" : "free", fp, mv, me.x, me.y, me.z, rdu(player + 0xB7C),
         player[0xB1C], (const void*)track, pts);
}

void teleport_frame(void) {
  const int presses = input_teleport_take();
  s_plan.have = 0;
  float eye[3], fwd[3];
  const unsigned char* player = (const unsigned char*)entities_snapshot().player;
  check_frame(player);
  const bool flying = freecam_ray(eye, fwd);
  if (flying && !s_flying && player) {
    placement_log(player);
    s_trksWanted = 1;   // the level's tracks are read again at each flight's first cast
  }
  s_flying = flying && player ? 1 : 0;
  // Every exit before a cast names itself when it drops a press (Review Focus 3).
  const char* why = 0;
  if (!flying) why = "the freecam has no pose yet";
  else if (!player) { why = tp_reason_text(TP_NO_PLAYER); s_byReason[TP_NO_PLAYER]++; }
  else if (!g_eng.phys_ray_test || !g_eng.phys_bound_test) why = "phys_ray_test/phys_bound_test unresolved";
  else if (!engine_resolve_p3shared()) why = "p3shared.dll's tmatcher_t exports unresolved";
  else why = cast(player, eye, fwd);
  if (why) {
    if (presses) np_log("teleport: refused, %s", why);
    return;
  }
  s_casts++;
  s_byReason[s_plan.reason]++;
  if (presses) { s_presses += (unsigned)presses; teleport_now(player); }

  const ULONGLONG t = GetTickCount64();
  if (t >= s_nextLog) {
    s_nextLog = t + 5000;
    // casts == ok + nohit + badnormal + steep + blocked + nofloor + offtrack + trackunread + depthrun;
    // casts == free + chosen + none + unread (none: no hit, or no track within the radius)
    np_log("teleport: casts=%u ok=%u nohit=%u badnormal=%u steep=%u blocked=%u nofloor=%u offtrack=%u "
           "trackunread=%u depthrun=%u noplayer=%u | free=%u chosen=%u none=%u unread=%u ties=%u tracks=%d | presses=%u "
           "teleports=%u gate=%u entry=%u", s_casts, s_byReason[TP_OK], s_byReason[TP_NO_HIT], s_byReason[TP_BAD_NORMAL],
           s_byReason[TP_STEEP], s_byReason[TP_BLOCKED], s_byReason[TP_NO_FLOOR], s_byReason[TP_OFF_TRACK],
           s_byReason[TP_TRACK_UNREAD], s_byReason[TP_DEPTH_RUN], s_byReason[TP_NO_PLAYER], s_trkFree, s_trkChosen, s_trkNone, s_trkUnread,
           s_trkTies, s_ntrks, s_presses, s_teleports, s_gateRefused, s_entryRefused);
  }
}

static void quad(const VolumeGeom& g, int f, unsigned colour) {
  if (!g.faceOk[f]) return;
  const unsigned char* q = VOL_FACE[f];
  int ix[4], iy[4];
  for (int k = 0; k < 4; ++k) {
    ix[k] = clamp_double_to_int((double)g.c[q[k]].x + 0.5);
    iy[k] = clamp_double_to_int((double)g.c[q[k]].y + 0.5);
  }
  canvas_tri(ix[0], iy[0], ix[1], iy[1], ix[2], iy[2], colour);
  canvas_tri(ix[0], iy[0], ix[2], iy[2], ix[3], iy[3], colour);
}

void teleport_draw(int W, int H) {
  if (!s_plan.have || !g_cfg.teleportMarker) return;
  Mat4 view, proj;
  float nearZ;
  if (!view_get(&view, &proj, &nearZ)) return;
  VolumeGeom g;
  canvas_begin(W, H);
  if (s_plan.reason == TP_OK) {
    // Duke's box where he would land: four sides and the cap (-x, +x, -z, +z, +y).
    const Hitbox hb = hitbox_from(s_plan.pos, s_plan.dim, s_plan.o);
    volume_project(hb.c, hb.half, view, proj, nearZ, W, H, &g);
    static const int kFaces[5] = { 0, 1, 4, 5, 3 };
    for (int i = 0; i < 5; ++i) quad(g, kFaces[i], C_TELEPORT_MARK);
  } else if (s_plan.hasHit) {
    // Refused: the footprint flat at the hit, no column.
    const Vec3 half = { s_plan.dim.x, 0.f, s_plan.dim.z };
    volume_project(s_plan.hit, half, view, proj, nearZ, W, H, &g);
    quad(g, 3, canvas_with_alpha(C_RED, 0x80));
  }
  canvas_end();
}
