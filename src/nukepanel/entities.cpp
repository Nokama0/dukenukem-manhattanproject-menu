// The entity walk (entities.md section 1 recipe).
#include "entities.h"
#include "engine.h"
#include "log.h"
#include "dmgnum.h"
#include "bossrule.h"
#include "config.h"   // g_cfg.espExplodables: the t=4 walk runs only for the Explodables row
#include "craterule.h"
#include "carryrule.h"
#include "laserrule.h"
#include "breakrule.h"
#include "areas.h"    // areas_view: the current area's nuke lasers, from its world.scr
#include <windows.h>
#include <stdio.h>
#include <string.h>

static EntSnapshot s_snap;

const EntSnapshot& entities_snapshot(void) { return s_snap; }

// [base, base+size) of duke_base.dll: vtables, their methods and class-name
// strings all live there, so a range test replaces a VirtualQuery per entity.
static const unsigned char* s_duke_base;
static unsigned s_playerParented;   // frames Duke had a parent at +0x60 (unproven that it never happens)
static unsigned s_duke_size;

static int in_duke(const void* p) {
  if (!s_duke_base) {
    const unsigned char* b = (const unsigned char*)GetModuleHandleA("duke_base.dll");
    if (!b || !guarded_readable(b, 0x40)) return 0;
    const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(b + *(const LONG*)(b + 0x3c));
    if (!guarded_readable(nt, sizeof(*nt))) return 0;
    s_duke_size = nt->OptionalHeader.SizeOfImage;
    s_duke_base = b;
  }
  const unsigned char* q = (const unsigned char*)p;
  return q >= s_duke_base && q < s_duke_base + s_duke_size;
}

bool entities_in_duke(const void* p) { return in_duke(p) != 0; }

bool entities_duke_image(const unsigned char** base, unsigned* size) {
  if (!s_duke_base) in_duke(0);   // resolves the bounds; a null pointer is never inside
  *base = s_duke_base;
  *size = s_duke_size;
  return s_duke_base != 0;
}

// thiscall, no stack args, EAX = name (mov eax,[name]; ret, e.g. monst_pig 0x10038040;
// call site duke!0x1002f47e..0x1002f4a0).
typedef const char* (__thiscall* ClassnameFn)(const void* self);

// Every object_e field read below ends by +0x2C8 (height offset +0x2C4, hitboxes.md section 0).
static const unsigned kObjSpan = 0x2C8;
// Monster-base fields end at +0xB90 (max health +0xB8C, entities.md section 3).
static const unsigned kMonsterSpan = 0xB90;
// Set by the monster base ctor (duke!0x1002bd80), which also writes +0xB84. boss_mechnukem
// and boss_wozma skip that base and keep other data at +0xB84 (ctors 0x100011c0, 0x10017180).
static const unsigned kUserMonster = 0x20;
static const int kMaxWalk = 4096;   // a corrupt list must not spin the frame forever
// Ends of the per-category reads below; each is inside its class's alloc size
// (dumps/duke_base.classes.txt: key 0xD18, babe 0xE60, area_secret 0x1078).
static const unsigned kItemSpan = 0x2E1;     // active byte +0x2E0
// key/ammo/ego/gun type dword +0xD14 (pickups.md section 1): each class's alloc reaches past it.
static const unsigned kSubtypeSpan = 0xD18;
static const unsigned kBabeSpan = 0xE14;     // state dword +0xE10 (babe_activate duke!0x10060ac0)
static const unsigned kSecretSpan = 0xAE8;   // armed dword +0xAE4
// breakable_adv (alloc 0xC8C): the damage gate dword +0xC44 that breakable_adv_damage duke!0x100bab30 tests.
static const unsigned kExplSpan = 0xC48;
// area_e+0xA7C: its `avolume` resource item (area_e::get_range/get_bsp p3shared!0x10001580/0x10001650): item +0 ushort
// flags (bit 3 = loaded, else get_range calls the loader), +8 data = { min[3], max[3], bsp }. The local range never
// changes once loaded, so it is read once per secret; only the world matrix is re-read each walk.
static const unsigned kOfsAreaVolume = 0xA7C, kAreaSpan = 0xA80, kOfsWorldMatrix = 0xF4;
struct SecretBox { const void* ent; const void* item; float r[6]; int loaded, loggedLoaded, loggedMissing; };
enum { kSecretBoxMax = 64 };
static SecretBox s_sbox[kSecretBoxMax];
static int s_nsbox;
// Per 5 s window: secretsWalked == boundsRead + boundsUnloaded + boundsUnread.
static unsigned s_secretsWalked, s_boundsRead, s_boundsUnloaded, s_boundsUnread, s_sboxFull;

static SecretBox* secret_box(const void* e, const void* item) {
  for (int i = 0; i < s_nsbox; ++i)
    if (s_sbox[i].ent == e) {
      if (s_sbox[i].item != item) { s_sbox[i].item = item; s_sbox[i].loaded = 0; }   // reused or re-bound
      return &s_sbox[i];
    }
  if (s_nsbox >= kSecretBoxMax) { s_sboxFull++; return 0; }
  SecretBox& b = s_sbox[s_nsbox++];
  memset(&b, 0, sizeof b);
  b.ent = e; b.item = item;
  return &b;
}

// A secret's marker, arrow and box come from the engine's own volume, not +0x18C (gate round 2, item 3).
// kind names the log lines ("secrets", "ladders"): both read the same per-area cache.
static bool area_world_box(const unsigned char* e, ReadCache& rc, const Vec3& origin, Vec3* c, Vec3* h, Vec3* ax,
                           const char* kind) {
  s_secretsWalked++;
  if (!guarded_readable_cached(&rc, e, kAreaSpan)) { s_boundsUnread++; return false; }
  const unsigned char* item;
  memcpy(&item, e + kOfsAreaVolume, sizeof item);
  if (!item) { s_boundsUnread++; return false; }
  SecretBox* b = secret_box(e, item);
  float r[6];
  if (b && b->loaded) {
    memcpy(r, b->r, sizeof r);
  } else {
    if (!guarded_readable(item, 0xC)) { s_boundsUnread++; return false; }
    unsigned short flags;
    memcpy(&flags, item, sizeof flags);
    const unsigned char* data;
    memcpy(&data, item + 8, sizeof data);
    if (!(flags & 8) || !data || !guarded_readable(data, 24)) {
      s_boundsUnloaded++;
      if (b && !b->loggedMissing) {
        b->loggedMissing = 1;
        np_log("%s: %p volume not loaded yet; +0x18C (%.1f,%.1f,%.1f) used", kind, (const void*)e, origin.x,
               origin.y, origin.z);
      }
      return false;
    }
    memcpy(r, data, sizeof r);
    if (b) { memcpy(b->r, r, sizeof r); b->loaded = 1; }
  }
  float m[16];
  memcpy(m, e + kOfsWorldMatrix, sizeof m);   // inside kObjSpan
  area_bounds(r, r + 3, m, c, h);
  area_obb(r, r + 3, m, c, ax);   // the same centre; the drawn box keeps its rotation
  s_boundsRead++;
  if (b && !b->loggedLoaded) {   // also when it loads after the first sighting
    b->loggedLoaded = 1;
    np_log("%s: %p +0x18C (%.1f,%.1f,%.1f); volume centre (%.1f,%.1f,%.1f) half (%.1f,%.1f,%.1f), local "
           "min (%.1f,%.1f,%.1f) max (%.1f,%.1f,%.1f)", kind, (const void*)e, origin.x, origin.y, origin.z, c->x, c->y, c->z,
           h->x, h->y, h->z, r[0], r[1], r[2], r[3], r[4], r[5]);
  }
  return true;
}

// The skmod handle +0xAE4 set by actor_model_ctor duke!0x1008f170, which every monst_* and boss inherits.
static const unsigned kSkelSpan = 0xAE8;

static LARGE_INTEGER s_qpf;
static ULONGLONG s_next_log;
// Over the current 5 s window: the walk's cost and how often the region cache missed.
static double s_walk_us;
static int s_walks;
static unsigned s_misses;
// Walks cut short by the 4096 walk cap or the 1024 snapshot cap, cumulative: never reset.
static unsigned s_capped;

static float rdf(const unsigned char* p) { float f; memcpy(&f, p, sizeof(f)); return f; }
static Vec3 rdv(const unsigned char* p) { Vec3 v; memcpy(&v, p, sizeof(v)); return v; }
static unsigned rdu(const unsigned char* p) { unsigned u; memcpy(&u, p, sizeof(u)); return u; }

// The stamp +0x984 needs the span to 0x988, inside every object_e (axial_t at +0x990 in the ctor p3shared!0x100178a0)
// and the region the walk already validated.
static const unsigned kOfsParent = 0x60, kOfsXformStamp = 0x984, kXformSpan = 0x988;
// An attached entity's +0x18C is local to its parent (+0x60): a pickup in a train car's spawn block read (160,56,-96)
// on e04p03_a02. Its world position is the +0xF4 matrix's translation, refreshed whenever the engine draws it (actor
// draw duke!0x10090260 calls vt+0x15C first) or asks vt+0x160; the walk runs after the world render.
static void ent_world_pos(EntSnap& s, const unsigned char* e, ReadCache& rc) {
  s.local = rdv(e + 0x18C);
  memcpy(&s.parent, e + kOfsParent, sizeof s.parent);   // inside kObjSpan
  unsigned stamp = 0;
  if (s.parent && guarded_readable_cached(&rc, e, kXformSpan)) stamp = rdu(e + kOfsXformStamp);
  float m[16];
  memcpy(m, e + kOfsWorldMatrix, sizeof m);   // inside kObjSpan
  s.att = (unsigned char)att_world_pos(s.parent, stamp, m, s.local, &s.pos);
  s_snap.att[s.att]++;
}

// Each babe and secret logs its state field (entities.md section 4) when first seen in a level, then
// every change, so the gate log confirms the field the ESP hides them by.
enum { kWatchMax = 64, kWatchFields = 2, kWatchLogChanges = 4 };
struct Watch {
  const void* ent;
  unsigned char cat, seen;
  unsigned vals[kWatchFields];
  unsigned char changes[kWatchFields];   // a timer-like field is muted after kWatchLogChanges
};
static Watch s_watch[kWatchMax];
static int s_nwatch;
static const void* s_watch_player;   // a new player pointer is a new level: forget the table
// Cumulative: changes seen, and per-frame watch attempts refused by a full table.
static unsigned s_watch_changes, s_watch_full;

// Watch-only categories: crates and barrels that hold something, enemies that carry a nuke, nuke lasers and the
// spawners they fire.
enum { kWatchHolder = CAT_COUNT, kWatchCarrier, kWatchLaser, kWatchSpawner };

// Babe: +0xE10, 0 bound, 1 freed, then 2/3 (babe_activate 0x10060ac0, babe_handler 0x10060800).
// Secret: +0xAE4, the armed dword area_secret_on_enter duke!0x1009be40 tests.
static int watch_read(const unsigned char* e, unsigned char cat, unsigned* out, unsigned* offs) {
  if (cat == CAT_SECRET) { offs[0] = 0xAE4; out[0] = rdu(e + 0xAE4); return 1; }
  // Explodable: the intact byte and the damage gate the ESP hides it by (re/explodables.md).
  if (cat == CAT_EXPLODABLE) { offs[0] = 0x2E0; out[0] = e[0x2E0]; offs[1] = 0xC44; out[1] = rdu(e + 0xC44); return 2; }
  // Holder: the intact byte the ESP hides its entries by, and the contents count (re/pickups.md section 5).
  if (cat == kWatchHolder) { offs[0] = 0x2E0; out[0] = e[0x2E0]; offs[1] = 0xB80; out[1] = rdu(e + 0xB80); return 2; }
  // Carrier: the drop list's count, which monster_drop_all duke!0x10035450 empties at death (re/pickups.md section 6).
  if (cat == kWatchCarrier) { offs[0] = 0xCA4; out[0] = rdu(e + 0xCA4); return 1; }
  // Laser: its `active` pin value; spawner: the entities it has spawned (re/pickups.md section 7).
  if (cat == kWatchLaser) { offs[0] = 0xB90; out[0] = rdu(e + 0xB90); return 1; }
  if (cat == kWatchSpawner) { offs[0] = 0xAE4; out[0] = rdu(e + 0xAE4); return 1; }
  offs[0] = 0xE10; out[0] = rdu(e + 0xE10);
  return 1;
}

static void watch_update(const unsigned char* e, unsigned char cat, const char* name) {
  unsigned v[kWatchFields] = {}, off[kWatchFields] = {};
  const int n = watch_read(e, cat, v, off);
  Watch* w = 0;
  for (int i = 0; i < s_nwatch; ++i) if (s_watch[i].ent == e && s_watch[i].cat == cat) { w = &s_watch[i]; break; }
  if (!w) {
    if (s_nwatch >= kWatchMax) { s_watch_full++; return; }
    w = &s_watch[s_nwatch++];
    memset(w, 0, sizeof(*w));
    w->ent = e; w->cat = cat;
    memcpy(w->vals, v, sizeof(v));
    char line[400];   // truncation is only a guard
    const Vec3 p = rdv(e + 0x18C);
    int len = _snprintf_s(line, sizeof(line), _TRUNCATE, "watch: %s %p new at (%.0f,%.0f,%.0f)", name, (const void*)e,
                          p.x, p.y, p.z);
    for (int i = 0; i < n && len > 0; ++i) {
      const int k = _snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, " +%X=%X", off[i], v[i]);
      if (k < 0) break;
      len += k;
    }
    np_log("%s", line);
  }
  w->seen = 1;
  for (int i = 0; i < n; ++i) {
    if (v[i] == w->vals[i]) continue;
    s_watch_changes++;
    const unsigned char k = w->changes[i];
    if (k < kWatchLogChanges)
      np_log("watch: %s %p +%X %X -> %X", name, (const void*)e, off[i], w->vals[i], v[i]);
    else if (k == kWatchLogChanges)
      np_log("watch: %s %p +%X muted after %d changes", name, (const void*)e, off[i], kWatchLogChanges);
    if (k <= kWatchLogChanges) w->changes[i]++;
    w->vals[i] = v[i];
  }
}

// After a complete walk: a watched entity the search no longer returns was removed.
static void watch_sweep(void) {
  for (int i = 0; i < s_nwatch;) {
    if (s_watch[i].seen) { s_watch[i].seen = 0; ++i; continue; }
    const unsigned char c = s_watch[i].cat;
    np_log("watch: %s %p gone",
           c == CAT_SECRET ? "Secret" : c == CAT_EXPLODABLE ? "Explodable" : c == kWatchHolder ? "Holder"
           : c == kWatchCarrier ? "Carrier" : c == kWatchLaser ? "Laser" : c == kWatchSpawner ? "Spawner" : "Babe",
           s_watch[i].ent);
    s_watch[i] = s_watch[--s_nwatch];
  }
}

// Ladders (free-move.md section 6): state 3's touch scan duke!0x10086120 grabs an area_ladder (user bit 0x40) when
// Up is held and area_ladder_grab_test duke!0x1009a460 finds Duke's head inside its volume. The same test here tells
// First person to pause and Move anywhere to give Up/Down back.
static Vec3 s_head;
static int s_haveHead, s_lastAtLadder;
static void ladder_test(const unsigned char* e, ReadCache& rc) {
  s_snap.ladders++;
  Vec3 bc, bh, ax[3];
  if (area_world_box(e, rc, rdv(e + 0x18C), &bc, &bh, ax, "ladders") && ent_point_in_obb(s_head, bc, ax))
    s_snap.atLadder = 1;
}

// --- Boss gates (aim.md section 7): each boss's damage handler and tested fields; every span is inside its class's
// alloc (porkchopper 0x1848, child 0xB38, queenroach 0x1A50, mechnukem_head 0x1140, wozma 0xCAC). A boss's span is its
// parts' slots' end (boss_parts_spec); a part's reads (back link, centre bone +0xB2C) stay inside its spec's partSpan.
static const unsigned kQueenSpan = 0x16BC, kHeadSpan = 0x10C4, kWozmaSpan = 0xB4D;
// Cumulative: part slots read, and those refused (unreadable, foreign vtable, no back link, killed, unknown name).
static unsigned s_partSlots, s_partBad;
// Each part's open state by its letter (engine, vulcan, wing), logged when it changes: the fight's own record.
static int s_partOpen[3] = { -1, -1, -1 };
static const void* s_partParent;

static unsigned s_immuneDropped;   // cumulative: immune entries past ENT_IMMUNE_MAX
static void note_immune(const void* e, unsigned char why) {
  if (!why) return;
  if (s_snap.nimmune >= ENT_IMMUNE_MAX) { s_immuneDropped++; return; }
  s_snap.immuneEnt[s_snap.nimmune] = e;
  s_snap.immuneWhy[s_snap.nimmune++] = why;
}

static int part_slot(int letter) { return letter == PORK_PART_ENGINE ? 0 : letter == PORK_PART_VULCAN ? 1 : 2; }

// The parts of a boss with a parts spec, found from the boss's own slots and kept only when they point back
// at it. The helicopter's are boss_porkchopper_child entities whose get_classname answers "actor_mover"
// (vt 0x10127008 slot 1), so the class table never names them. Each is gated by porkchild_think's own test.
static void boss_parts(const unsigned char* e, ReadCache& rc, const BossPartsSpec& ps) {
  PorkFacts p;
  p.health = rdf(e + 0x145C);
  p.active = e[0x1468] != 0;
  memcpy(&p.state, e + 0x146C, sizeof p.state);
  p.dropPigHealth = rdf(e + 0x14B4);
  memcpy(&p.pigsLeft, e + 0x14BC, sizeof p.pigsLeft);
  p.vulcanHealth = rdf(e + 0x1534);
  p.vulcanOut = e[0x1570] != 0;
  if (e != s_partParent) { s_partParent = e; s_partOpen[0] = s_partOpen[1] = s_partOpen[2] = -1; }
  for (int k = 0; k < ps.count; ++k) {
    s_partSlots++;
    const unsigned char* c;
    memcpy(&c, e + ps.firstSlot + 4 * k, sizeof c);
    if (!c || !guarded_readable_cached(&rc, c, ps.partSpan)) { s_partBad++; s_snap.partsUnread++; continue; }
    void* const* vt = *(void* const* const*)c;
    const unsigned char* back;
    memcpy(&back, c + ps.backLink, sizeof back);
    int killed;
    memcpy(&killed, c + 0x90, sizeof killed);
    const int letter = c[4 + PORK_PART_LETTER_AT];   // the byte porkchild_think switches on (+0x10)
    const char* name = pork_part_name(letter);
    if (!in_duke(vt) || back != e || killed || !name || s_snap.nparts >= ENT_PARTS_MAX) {
      s_partBad++; s_snap.partsUnread++;
      continue;
    }
    EntSnap& s = s_snap.parts[s_snap.nparts++];
    memset(&s, 0, sizeof s);
    s.ent = (void*)c;
    s.cat = CAT_BOSS;
    s.name = name;
    s.bossKind = (unsigned char)boss_kind_of_part(letter);
    s.vt = vt;
    float m[16];
    memcpy(m, c + kOfsWorldMatrix, sizeof m);
    s.pos = Vec3{ m[12], m[13], m[14] };   // the world matrix: +0x18C is not proven world for a part
    s.half = rdv(c + 0x2B8);                // the boss's "dimensions", the same for all four (init 0x10006e30)
    s.hb = hitbox_from(s.pos, s.half, rdf(c + 0x2C4));
    s.keyColour = -1; s.subtype = -1; s.active = 1;
    memcpy(&s.skel, c + 0xAE4, sizeof s.skel);
    memcpy(&s.bone, c + 0xB2C, sizeof s.bone);
    const int open = pork_part_open(letter, p);
    s.immune = open ? BOSS_OPEN : BOSS_IMM_PORK_PART;
    note_immune(c, s.immune);
    int& was = s_partOpen[part_slot(letter)];
    if (was != open) {
      np_log("ents: %s %p %s (health %.0f, drop_pig_health %.0f, pigs %d, state %d, gun health %.0f, gun out %d, "
             "active %d)", name, (const void*)c, open ? "takes damage now" : "immune now", p.health, p.dropPigHealth,
             p.pigsLeft, p.state, p.vulcanHealth, p.vulcanOut, p.active);
      was = open;
    }
  }
}

// BOSS_IMM_* when this boss's own vt+0x198 refuses damage now; BOSS_OPEN when it takes it or is unread.
static unsigned char boss_gate(const unsigned char* e, ReadCache& rc, const char* cls) {
  if (!strcmp(cls, "boss_porkchopper")) {
    // The span through the last part slot (0x15C4) also covers every PorkFacts field (+0x145C..+0x1570).
    const BossPartsSpec* ps = boss_parts_spec(cls);
    if (ps && guarded_readable_cached(&rc, e, ps->firstSlot + 4u * (unsigned)ps->count)) boss_parts(e, rc, *ps);
    return pork_core_open() ? BOSS_OPEN : BOSS_IMM_PORK_CORE;
  }
  if (!strcmp(cls, "boss_queenroach"))
    return guarded_readable_cached(&rc, e, kQueenSpan) && !queen_open(rdf(e + 0x1690), e[0x16B8] != 0) ? BOSS_IMM_QUEEN
                                                                                                      : BOSS_OPEN;
  if (!strcmp(cls, "boss_mechnukem_head")) {
    if (!guarded_readable_cached(&rc, e, kHeadSpan)) return BOSS_OPEN;
    int state, mode;
    memcpy(&state, e + 0xF4C, sizeof state);
    memcpy(&mode, e + 0x10C0, sizeof mode);
    return mechhead_open(state, mode) ? BOSS_OPEN : BOSS_IMM_MECHHEAD;
  }
  if (!strcmp(cls, "boss_wozma"))
    return guarded_readable_cached(&rc, e, kWozmaSpan) && !wozma_open(e[0xB4C] != 0) ? BOSS_IMM_WOZMA : BOSS_OPEN;
  return BOSS_OPEN;
}

// --- Roach eggs (eggrule.h; docs/re/entities.md section 8) -------------------------------------------------
// roach_pod (alloc 0xC5C): health +0xC4C, hatched +0xC48, read through +0xC50.
static const char kEggClass[] = "roach_pod";
static const unsigned kEggSpan = 0xC50;
// An egg hatches only when its "hatch" input pin is set, and an input pin keeps no list of its senders: the wires live
// on the senders' output pins (entity_t::pin_io p3shared!0x1000d6d0). So the table of wired eggs comes from one walk of
// every entity's output pins (layout: egg_scan).
enum { kEggWiredMax = 128, kEggScanEntMax = 8192, kEggPinMax = 64, kEggWireMax = 64 };
static char s_eggWired[kEggWiredMax][EGG_NAME_LEN];
static int s_neggWired;
static int s_eggScanOk;               // the table belongs to s_eggLevel
static int s_eggScanDone;             // a scan ran for s_eggLevel (even a failed one: no retry every 2 s)
static EggLevelKey s_eggLevel;        // the level of the table (or of the scan still to come)
static EggVerdictCache s_eggCache;    // each egg's wired verdict against the current table
static int s_eggScanEggs;             // eggs walked when the table was built
static int s_eggsLast;                // eggs walked last frame
static ULONGLONG s_eggScanNext;
static unsigned s_eggScans;           // cumulative

// One walk of every entity (a search with no t= runs the server's whole list, spawn_search's setup
// prism3d!0x40cf30). Once per level, again only when more eggs appear, and at most every 2 s.
static void egg_scan(int sid) {
  if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  ReadCache rc = {};
  int ents = 0, pins = 0, outs = 0, wires = 0, hatch = 0, unread = 0, dropped = 0;
  bool capped = false;
  s_neggWired = 0;
  g_eng.spawn_search("s=%d", sid);
  const unsigned char* e;
  while ((e = (const unsigned char*)g_eng.spawn_search_get()) != 0) {
    if (ents >= kEggScanEntMax) { capped = true; break; }
    ents++;
    if (!guarded_readable_cached(&rc, e, 0x60)) { unread++; continue; }
    // entity +0x5C first pin, pin +0x0C next, +0x04 assign (2 = output, pin_t::assign), +0x64 first wire; wire +0x00
    // target name char[0x40], +0x40 next (regp 0x1000e340, next_pin 0x1000def0, pin_t::connect 0x1001bd40).
    const unsigned char* p;
    memcpy(&p, e + 0x5C, sizeof p);
    for (int k = 0; p && k < kEggPinMax; ++k) {
      if (!guarded_readable_cached(&rc, p, 0x68)) { unread++; break; }
      pins++;
      if (rdu(p + 4) == 2) {
        outs++;
        const unsigned char* w;
        memcpy(&w, p + 0x64, sizeof w);
        for (int j = 0; w && j < kEggWireMax; ++j) {
          if (!guarded_readable_cached(&rc, w, 0x44)) { unread++; break; }
          wires++;
          char wn[0x41];
          memcpy(wn, w, 0x40);
          wn[0x40] = 0;
          char nm[EGG_NAME_LEN];
          if (egg_wire_target(wn, nm, sizeof nm)) {
            hatch++;
            if (!egg_wired(s_eggWired, s_neggWired, nm)) {
              if (s_neggWired < kEggWiredMax) strcpy_s(s_eggWired[s_neggWired++], EGG_NAME_LEN, nm);
              else dropped++;
            }
          }
          memcpy(&w, w + 0x40, sizeof w);
        }
      }
      memcpy(&p, p + 0x0C, sizeof p);
    }
  }
  g_eng.spawn_search_stop();
  QueryPerformanceCounter(&t1);
  // A capped walk or a full table may have missed a wire: the eggs then read unread (never targeted).
  s_eggScanOk = !capped && !dropped;
  s_eggScanDone = 1;
  egg_cache_clear(&s_eggCache);   // the verdicts were against the old table
  s_eggScanEggs = s_eggsLast;
  s_eggScans++;
  np_log("eggs: wire scan #%u: %d entities, %d pins (%d output), %d wires, %d to a hatch pin naming %d egg(s); "
         "unread %d, capped %d, dropped %d, %.0f us; %d egg(s) walked last frame", s_eggScans, ents, pins, outs,
         wires, hatch, s_neggWired, unread, capped ? 1 : 0, dropped,
         (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)s_qpf.QuadPart, s_eggsLast);
}

// Fills an egg's entry: its state, label, health and whether the aim may take it. The walk read +0x2E0 and the
// name (+4, char[0x40]) under kObjSpan.
static void egg_fill(EntSnap& s, const unsigned char* e, ReadCache& rc) {
  EggFacts f = { 0, 1, 1, 0.f, -1 };
  if (!guarded_readable_cached(&rc, e, kEggSpan)) {
    s.egg = EGG_UNREAD;
  } else {
    f.hatched = e[0xC48] != 0;
    f.intact = e[0xAF0] != 0;
    f.solid = e[0x2E0] != 0;
    f.health = rdf(e + 0xC4C);
    // s_eggScanOk is cleared before the walk when the level changed (entities_frame), so the table is this level's.
    if (s_eggScanOk && !egg_cache_get(&s_eggCache, e, &f.wired)) {
      char nm[EGG_NAME_LEN];
      memcpy(nm, e + 4, sizeof nm);
      nm[EGG_NAME_LEN - 1] = 0;
      f.wired = egg_wired(s_eggWired, s_neggWired, nm);
      egg_cache_put(&s_eggCache, e, f.wired);
    }
    s.egg = (unsigned char)egg_state(f);
  }
  s.name = egg_label(s.egg);
  s.hashp = 1;
  s.hp = f.health;
  s.maxhp = EGG_HEALTH_MAX;
  // Not noted as immune: an idle egg still takes a bullet's damage (0x10072770), so it never blocks the ray.
  s.immune = s.egg == EGG_GONE || egg_targetable(s.egg) ? (unsigned char)BOSS_OPEN : (unsigned char)BOSS_IMM_EGG;
  s_snap.eggs[s.egg]++;
}

// --- Crates and barrels that hold a pickup (re/pickups.md section 5): the contents base duke!0x10067d90 (crate 0xC84,
// barrel 0xC8C bytes): +0xB7C int* codes, +0xB80 their count (editor value "contents", registered 0x10067b65), +0xBF8
// crate_type/barrel_type, +0x2E0 intact (cleared by the damage path 0x100685a0, the explode path 0x10068600).
static const unsigned kHolderSpan = 0xBFC;
// The pickups do not exist until the holder breaks: they are spawn scripts handed to a spawner (0x10068230), so the
// walk draws each content at the holder.
static int s_walkHolders;   // Nukes or Items and pickups on
static ReadCache s_codesRc;   // the codes arrays' heap region, apart from the entity cache
// Per frame, logged with the 5 s line: holders walked, holding something, entries by kind, unread, broken.
static int s_holders, s_holdersHeld, s_holderNukes, s_holderItems, s_holdersUnread, s_holdersOpened;

static void holder_entries(const unsigned char* e, ReadCache& rc, void* const* vt, int holder, const char* cls,
                           bool& capped) {
  s_holders++;
  if (!guarded_readable_cached(&rc, e, kHolderSpan)) { s_holdersUnread++; return; }
  int n;
  memcpy(&n, e + 0xB80, sizeof n);
  if (n == 0) return;
  const unsigned char* codes;
  memcpy(&codes, e + 0xB7C, sizeof codes);
  if (n < 0 || n > CRATE_CONTENTS_MAX || !codes || !guarded_readable_cached(&s_codesRc, codes, (size_t)n * 4)) {
    s_holdersUnread++;
    return;
  }
  const int type = (int)rdu(e + 0xBF8);
  const unsigned char active = e[0x2E0] != 0;
  CrateContent done[CRATE_CONTENTS_MAX];
  int ndone = 0;
  for (int k = 0; k < n; ++k) {
    const CrateContent cc = crate_content(holder, type, (int)rdu(codes + 4 * k));
    if (cc.cat == CAT_OTHER) continue;
    bool dup = false;   // one marker per distinct content: demo_crate_barrel's crate3 holds 1,0,1,0,1
    for (int j = 0; j < ndone; ++j)
      dup = dup || (done[j].cat == cc.cat && done[j].kind == cc.kind && done[j].subtype == cc.subtype);
    if (dup) continue;
    done[ndone++] = cc;
    if (s_snap.count >= ENT_SNAPSHOT_MAX) { capped = true; break; }
    EntSnap& s = s_snap.ents[s_snap.count++];
    memset(&s, 0, sizeof s);
    s.ent = (void*)e;
    s.cat = cc.cat; s.kind = cc.kind; s.subtype = cc.subtype;
    s.name = crate_content_label(holder, cc);
    ent_world_pos(s, e, rc);
    s.half = rdv(e + 0x2B8);
    s.hb = hitbox_from(s.pos, s.half, rdf(e + 0x2C4));
    s.keyColour = -1; s.active = active; s.bone = -1;
    s.vt = vt;
    s.holder = (unsigned char)holder;
    if (cc.cat == CAT_NUKE) s_holderNukes++; else s_holderItems++;
  }
  if (!ndone) return;
  s_holdersHeld++;
  if (!active) s_holdersOpened++;
  watch_update(e, kWatchHolder, cls);
}

// --- Enemies that carry a nuke (re/pickups.md section 6): the monster base ctor duke!0x1002bd80 (user bit 0x20) keeps
// the editor value "spawnscript" at +0xCA0 (char[64] entries) and +0xCA4 (count); monster_die 0x1002fdb0 calls
// vt+0x2F4, monster_drop_all 0x10035450, which spawns each "drop-<class>" entry and zeroes the count (alloc > 0xCA8).
static const unsigned kDropsSpan = 0xCA8;
static ReadCache s_dropsRc;   // the drop lists' heap blocks, apart from the entity cache
// Per frame, logged with the 5 s line: living enemies that carry a nuke, the nukes they carry, lists unread.
static int s_carriers, s_carriedNukes, s_carryUnread;

int entities_carried_nukes(const unsigned char* e, ReadCache* rc, ReadCache* listRc) {
  if (!guarded_readable_cached(rc, e, kDropsSpan)) return -1;
  if (!(rdu(e + 0x4C) & kUserMonster)) return 0;
  const int n = (int)rdu(e + 0xCA4);
  if (n == 0) return 0;
  const char* list;
  memcpy(&list, e + 0xCA0, sizeof list);
  if (n < 0 || n > CARRY_DROPS_MAX || !list || !guarded_readable_cached(listRc, list, (size_t)n * CARRY_SCRIPT_LEN))
    return -1;
  return carry_nukes(list, n);
}

// The enemy's own entry gets the count (its label's " + Nuke"); with Nukes on, a CAT_NUKE entry at the enemy draws
// the small nuke marker and takes the Nukes arrow.
static void carry_entries(const unsigned char* e, ReadCache& rc, EntSnap& s, const char* cls, bool& capped) {
  const int k = entities_carried_nukes(e, &rc, &s_dropsRc);
  if (k < 0) { s_carryUnread++; return; }
  s.carry = (unsigned char)k;
  if (!k || !ent_alive(s)) return;
  s_carriers++;
  s_carriedNukes += k;
  watch_update(e, kWatchCarrier, cls);
  if (!g_cfg.espNukes) return;
  if (s_snap.count >= ENT_SNAPSHOT_MAX) { capped = true; return; }
  EntSnap& m = s_snap.ents[s_snap.count++];
  memset(&m, 0, sizeof m);
  m.ent = s.ent;
  m.cat = CAT_NUKE;
  m.name = "Nuke (enemy)";
  m.pos = s.pos; m.half = s.half; m.hb = s.hb;
  m.att = s.att; m.parent = s.parent; m.local = s.local;
  m.keyColour = -1; m.active = 1; m.subtype = -1; m.bone = -1;
  m.vt = s.vt;
  m.carry = s.carry;
}

// --- Blue lasers that spawn a nuke carrier (re/pickups.md section 7): the current area's sources from its world.scr
// (areas.h), each laser and the spawner it fires, found by name. laser (alloc 0xBC0): +0xB90 armed (its `active` pin),
// +0xBB0 the beam's world end; spawner_entity (alloc 0xAEC): +0xAE4 entities spawned. Neither is a dobj.
static const unsigned kLaserSpan = 0xBBC, kSpawnerSpan = 0xAE8;
// Per frame, logged with the 5 s line: nuke lasers here, armed, and lookups that found nothing.
static int s_lasers, s_lasersArmed, s_lasersUnfound, s_spawnersUnfound;
static char s_laserLabel[AR_NUKES_MAX][24];

static const unsigned char* find_named(int sid, const char* name, const char* cls, unsigned span, ReadCache& rc) {
  const unsigned char* hit = 0;
  g_eng.spawn_search("s=%d?n=%s", sid, name);
  const unsigned char* e = (const unsigned char*)g_eng.spawn_search_get();
  if (e && guarded_readable_cached(&rc, e, span)) {
    void* const* vt = *(void* const* const*)e;
    int killed;
    memcpy(&killed, e + 0x90, sizeof killed);
    if (!killed && in_duke(vt) && in_duke(vt + 2) && in_duke(vt[1])) {
      const char* c = ((ClassnameFn)vt[1])(e);
      if (in_duke(c) && strcmp(c, cls) == 0) hit = e;
    }
  }
  g_eng.spawn_search_stop();
  return hit;
}

// A CAT_NUKE entry framing each nuke laser's beam, active while the laser is armed: the not gate it drives switches
// it off when it fires (e08p03_a01's not3/not8/not9/not11), and the carrier then shows " + Nuke" itself.
static void laser_entries(const unsigned char* player, int sid, ReadCache& rc, bool& capped) {
  const ArView* v = areas_view();
  const void* world;
  memcpy(&world, player + 0xE0, sizeof world);   // within probe's kPlayerSpan
  if (!v->ok || !v->part || v->cur < 0 || v->cur >= v->part->nareas || world != v->foundWorld) return;
  const ArArea& a = v->part->areas[v->cur];
  static const void* logged[32];
  static int nlogged;
  for (int k = 0; k < a.nnukes; ++k) {
    const ArNuke& s = a.nukes[k];
    if (s.holder != AR_NK_LASER) continue;
    s_lasers++;
    const unsigned char* S = find_named(sid, s.cls, "spawner_entity", kSpawnerSpan, rc);
    if (S) watch_update(S, kWatchSpawner, s.cls);
    else s_spawnersUnfound++;
    const unsigned char* L = find_named(sid, s.name, "laser", kLaserSpan, rc);
    if (!L) { s_lasersUnfound++; continue; }
    watch_update(L, kWatchLaser, s.name);
    const int armed = rdu(L + 0xB90) != 0;
    s_lasersArmed += armed;
    float m[16];
    memcpy(m, L + kOfsWorldMatrix, sizeof m);
    const float st[3] = { m[12], m[13], m[14] };
    float en[3];
    memcpy(en, L + 0xBB0, sizeof en);
    const int endOk = en[0] == en[0] && en[1] == en[1] && en[2] == en[2] && (en[0] != 0.f || en[1] != 0.f || en[2] != 0.f);
    float c[3], h[3];
    laser_beam_box(st, en, endOk, c, h);
    bool seen = false;
    for (int i = 0; i < nlogged && !seen; ++i) seen = logged[i] == L;
    if (!seen) {
      logged[nlogged++ % 32] = L;
      if (nlogged > 32) nlogged = 32;
      const Vec3 p = rdv(L + 0x18C);
      np_log("lasers: %s %p fires %s %p: +18C (%.0f,%.0f,%.0f) matrix (%.0f,%.0f,%.0f) end (%.0f,%.0f,%.0f), armed %d, "
             "spawned %d", s.name, (const void*)L, s.cls, (const void*)S, p.x, p.y, p.z, st[0], st[1], st[2], en[0], en[1],
             en[2], armed, S ? (int)rdu(S + 0xAE4) : -1);
    }
    if (s_snap.count >= ENT_SNAPSHOT_MAX) { capped = true; return; }
    EntSnap& e = s_snap.ents[s_snap.count++];
    memset(&e, 0, sizeof e);
    e.ent = (void*)L;
    e.cat = CAT_NUKE;
    e.name = laser_marker_label(s.count, s_laserLabel[k], sizeof s_laserLabel[k]);
    e.pos = Vec3{ c[0], c[1], c[2] };
    e.half = Vec3{ h[0], h[1], h[2] };
    e.hb.c = e.pos;
    e.hb.half = e.half;
    e.keyColour = -1; e.active = (unsigned char)armed; e.subtype = -1; e.bone = -1;
    e.vt = *(void* const* const*)L;
  }
}

// --- Breakables that drop a pickup (re/pickups.md section 8): the current area's breakable_adv objects whose `broken`
// pin fires a pickup spawner (areas.h), found by name like the lasers: a CAT_ITEM entry at the breakable, hidden once
// +0x2E0 clears (breakable_break duke!0x100f1600), when the spawned pickup shows as itself. Only areas with one pay.
static int s_breakDrops, s_breakDropsOpen, s_breakDropsUnfound;   // per frame, logged with the 5 s line

static void breakable_drop_entries(const unsigned char* player, int sid, ReadCache& rc, bool& capped) {
  const ArView* v = areas_view();
  const void* world;
  memcpy(&world, player + 0xE0, sizeof world);
  if (!v->ok || !v->part || v->cur < 0 || v->cur >= v->part->nareas || world != v->foundWorld) return;
  const ArArea& a = v->part->areas[v->cur];
  for (int k = 0; k < a.nbreaks; ++k) {
    const ArBreak& b = a.breaks[k];
    s_breakDrops++;
    const unsigned char* e = find_named(sid, b.name, "breakable_adv", kExplSpan, rc);
    if (!e) { s_breakDropsUnfound++; continue; }
    if (!brk_intact(e[0x2E0])) { s_breakDropsOpen++; continue; }
    const char* label = brk_label(b.kind, b.subtype);
    if (!label) continue;
    if (s_snap.count >= ENT_SNAPSHOT_MAX) { capped = true; return; }
    EntSnap& s = s_snap.ents[s_snap.count++];
    memset(&s, 0, sizeof s);
    s.ent = (void*)e;
    s.cat = CAT_ITEM; s.kind = b.kind; s.subtype = b.subtype;
    s.name = label;
    ent_world_pos(s, e, rc);
    s.half = rdv(e + 0x2B8);
    s.hb = hitbox_from(s.pos, s.half, rdf(e + 0x2C4));
    s.keyColour = -1; s.active = 1; s.bone = -1;
    s.vt = *(void* const* const*)e;
  }
}

// --- Ambushes (areas.h; re/kills-and-spawners.md sections 2-3): the current area's ambushes from its world.scr, each
// spawner found by name as the lasers' are: +0xAE4 counts what it spawned, and no spawner keeps a "spent" flag, so 0 is
// "not fired yet".
static int s_ambushes, s_ambWaiting, s_ambArmed, s_ambUnknown, s_ambSpUnfound, s_ambLookups;   // per frame, the 5 s line
// A waiting one draws an orange box round its spawners (the parsed positions: spawners never move) and its zone's
// offline volume; a laser's, while the laser can fire, a red one.
static char s_ambLabel[AR_AMBUSH_MAX][112];
static const void* s_ambWorld;
static signed char s_ambState[AR_AMBUSH_MAX];

// An ambush's trigger (ar_trig_read): 1 gone off, 0 not, -1 unread. area_switch +0xAF4 is its on byte (cutaway.md
// section 3), kept when the area is rebuilt from its .msv; a holder's +0x2E0 its intact byte; a monster's +0xB84 its
// health. Gone counts as gone off.
static int ambush_trigger(int sid, const ArAmbush& m, ReadCache& rc) {
  const int how = ar_trig_read(&m);
  if (how == AR_TRD_NONE) return -1;
  s_ambLookups++;
  if (how == AR_TRD_SWITCH) {
    const unsigned char* s = find_named(sid, m.zone, "area_switch", 0xAF8, rc);
    return s ? s[0xAF4] != 0 : -1;
  }
  const unsigned char* t = find_named(sid, m.trig, m.trigCls, how == AR_TRD_MONSTER ? kMonsterSpan : kItemSpan, rc);
  if (!t) return 1;
  return how == AR_TRD_HOLDER ? t[0x2E0] == 0 : rdf(t + 0xB84) <= 0.f;
}

static void ambush_entries(const unsigned char* player, int sid, ReadCache& rc, bool& capped) {
  const ArView* v = areas_view();
  const void* world;
  memcpy(&world, player + 0xE0, sizeof world);   // within probe's kPlayerSpan
  if (!v->ok || !v->part || v->cur < 0 || v->cur >= v->part->nareas || world != v->foundWorld) return;
  const ArArea& a = v->part->areas[v->cur];
  if (world != s_ambWorld) {
    s_ambWorld = world;
    memset(s_ambState, -1, sizeof s_ambState);
  }
  for (int k = 0; k < a.nambush; ++k) {
    const ArAmbush& m = a.ambush[k];
    s_ambushes++;
    if (s_ambState[k] == AR_AMB_FIRED) continue;   // a fired ambush never waits again in this world: no searches
    int spawned[AR_AMB_SP];
    for (int j = 0; j < m.nsp; ++j) {
      s_ambLookups++;
      const unsigned char* S = find_named(sid, m.sp[j], "spawner_entity", kSpawnerSpan, rc);
      spawned[j] = S ? (int)rdu(S + 0xAE4) : -1;
      if (!S) s_ambSpUnfound++;
    }
    int armed = -1;
    if (m.kind == AR_TRIG_LASER) {
      s_ambLookups++;
      const unsigned char* L = find_named(sid, m.trig, "laser", kLaserSpan, rc);
      if (L) armed = rdu(L + 0xB90) != 0;
    }
    const int trig = ambush_trigger(sid, m, rc);
    int fires;
    const int st = ar_ambush_state(&m, spawned, armed, trig, &fires);
    if (st != s_ambState[k]) {   // each change once, for the log
      np_log("ambush: %s %s (%d spawner(s), first %s spawned %d, trigger %d) %s, fires %d, laser %d", a.map, m.trig,
             m.nsp, m.sp[0], spawned[0], trig, st == AR_AMB_WAITING ? "waiting" : st == AR_AMB_FIRED ? "fired"
                                   : st == AR_AMB_ARMED ? "armed" : st == AR_AMB_IDLE ? "idle" : "unknown", fires, armed);
      s_ambState[k] = (signed char)st;
    }
    s_ambWaiting += st == AR_AMB_WAITING;   // a fired one is skipped above from the next frame on
    s_ambArmed += st == AR_AMB_ARMED;
    s_ambUnknown += st == AR_AMB_UNKNOWN;
    if (st != AR_AMB_WAITING && st != AR_AMB_ARMED) continue;
    float lo[3], hi[3];
    for (int c = 0; c < 3; ++c) {
      lo[c] = hi[c] = m.spPos[0][c];
      for (int j = 1; j < m.nsp; ++j) {
        if (m.spPos[j][c] < lo[c]) lo[c] = m.spPos[j][c];
        if (m.spPos[j][c] > hi[c]) hi[c] = m.spPos[j][c];
      }
    }
    if (s_snap.count >= ENT_SNAPSHOT_MAX) { capped = true; return; }
    ar_ambush_label(&m, fires, s_ambLabel[k], sizeof s_ambLabel[k]);
    EntSnap& e = s_snap.ents[s_snap.count++];
    memset(&e, 0, sizeof e);
    e.ent = (void*)&a.ambush[k];   // no engine entity stands for the group: the aim and the arrows key on nothing else
    e.cat = CAT_AMBUSH;
    e.kind = (unsigned char)(st == AR_AMB_ARMED ? AMB_MARK_ARMED : AMB_MARK_WAIT);
    e.name = s_ambLabel[k];
    e.pos = Vec3{ 0.5f * (lo[0] + hi[0]), 0.5f * (lo[1] + hi[1]), 0.5f * (lo[2] + hi[2]) };
    const float hx = 0.5f * (hi[0] - lo[0]), hy = 0.5f * (hi[1] - lo[1]), hz = 0.5f * (hi[2] - lo[2]);
    e.half = Vec3{ hx > AMB_BOX_MIN ? hx : AMB_BOX_MIN, hy > AMB_BOX_MIN ? hy : AMB_BOX_MIN, hz > AMB_BOX_MIN ? hz : AMB_BOX_MIN };
    e.hb.c = e.pos;
    e.hb.half = e.half;
    e.keyColour = -1; e.active = 1; e.subtype = -1; e.bone = -1;
    if (st != AR_AMB_WAITING || !m.zone[0] || !m.volOk) continue;
    if (s_snap.count >= ENT_SNAPSHOT_MAX) { capped = true; return; }
    EntSnap& z = s_snap.ents[s_snap.count++];
    memset(&z, 0, sizeof z);
    z.ent = (void*)&a.ambush[k];
    z.cat = CAT_AMBUSH;
    z.kind = AMB_MARK_ZONE;
    z.name = "Ambush trigger";
    // The volume is the switch's own, unrotated (no zone switch of the 41 has a rotation key, by census).
    z.pos = Vec3{ m.zonePos[0] + 0.5f * (m.vol[0] + m.vol[3]), m.zonePos[1] + 0.5f * (m.vol[1] + m.vol[4]),
                  m.zonePos[2] + 0.5f * (m.vol[2] + m.vol[5]) };
    z.half = Vec3{ 0.5f * (m.vol[3] - m.vol[0]), 0.5f * (m.vol[4] - m.vol[1]), 0.5f * (m.vol[5] - m.vol[2]) };
    z.hb.c = z.pos;
    z.hb.half = z.half;
    z.keyColour = -1; z.active = 1; z.subtype = -1; z.bone = -1;
  }
}

// Per-entity classification and snapshot fill, shared by the t=2 (dobj) and t=0x20
// (area, incl. area_secret) searches (secrets-walk.md section 5). `capped` is set,
// never cleared, when the snapshot array is full.
static void walk_entity(const unsigned char* e, ReadCache& rc, bool& capped) {
  s_snap.total++;
  if (!guarded_readable_cached(&rc, e, kObjSpan)) { s_snap.skipped++; return; }
  void* const* vt = *(void* const* const*)e;
  if (!in_duke(vt) || !in_duke(vt + 2) || !in_duke(vt[1])) { s_snap.skipped++; return; }
  int killed;
  memcpy(&killed, e + 0x90, sizeof(killed));   // killed flag, nonzero = removed (entities.md section 1)
  if (killed) { s_snap.skipped++; return; }
  const char* cls = ((ClassnameFn)vt[1])(e);
  // actor_player's slot 1 returns a non-pointer (entities.md section 2)
  if (!in_duke(cls)) { s_snap.skipped++; return; }
  const EntClass c = class_lookup(cls);
  s_snap.cats[c.cat]++;
  if (c.cat == CAT_OTHER) {
    // Each unclassified class name once (cap 128), so a pickup the table lacks shows in the log.
    static char seenCls[128][40];
    static int nSeen;
    bool known = false;
    for (int i = 0; i < nSeen && !known; ++i) known = strncmp(seenCls[i], cls, 39) == 0;
    if (!known && nSeen < 128) {
      strncpy_s(seenCls[nSeen], sizeof seenCls[0], cls, _TRUNCATE);
      np_log("ents: unclassified class %s", cls);
      ++nSeen;
    }
    if (s_haveHead && strcmp(cls, "area_ladder") == 0) ladder_test(e, rc);
    else if (s_walkHolders) {
      const int holder = crate_holder_of(cls);
      if (holder) holder_entries(e, rc, vt, holder, cls, capped);
    }
    return;
  }
  const bool egg = c.cat == CAT_ENEMY && strcmp(cls, kEggClass) == 0;
  // The damage hook is armed from a live monster's own vtable, never an offset or a scan. An egg is no monster.
  if ((c.cat == CAT_ENEMY && !egg) || c.cat == CAT_BOSS) dmgnum_arm(vt, cls);
  if (s_snap.count >= ENT_SNAPSHOT_MAX) { capped = true; return; }
  EntSnap& s = s_snap.ents[s_snap.count++];
  s.ent = (void*)e;
  s.cat = c.cat; s.tier = c.tier; s.kind = c.kind; s.name = c.name;
  ent_world_pos(s, e, rc);   // +0x18C (== vt+0x160 for a root entity, measured), or an attached one's matrix
  s.half = rdv(e + 0x2B8);   // collision half-extents (hitboxes.md section 0)
  // Height offset, re-read every frame: monster AI rewrites it (hitboxes.md section 0).
  s.hb = hitbox_from(s.pos, s.half, rdf(e + 0x2C4));
  unsigned user;
  memcpy(&user, e + 0x4C, sizeof(user));   // user type bits (entities.md section 1)
  s.hashp = (user & kUserMonster) && guarded_readable_cached(&rc, e, kMonsterSpan);
  s.hp = s.hashp ? rdf(e + 0xB84) : 0.f;      // current health, dead <= 0 (entities.md section 3)
  s.maxhp = s.hashp ? rdf(e + 0xB8C) : 0.f;   // max health (entities.md section 3)
  s.drawn = s.hashp ? (e[0xAF1] != 0) : -1;   // inside kMonsterSpan, so no second guarded read
  s.keyColour = -1; s.active = 1; s.armed = 0; s.freed = 0; s.subtype = -1;
  s.immune = 0; s.bone = -1; s.holder = HOLDER_NONE; s.egg = EGG_NONE; s.carry = 0;
  if (egg) egg_fill(s, e, rc);
  if ((c.cat == CAT_ENEMY || c.cat == CAT_BOSS) && ent_alive(s)) s_snap.alive++;
  s.bossKind = c.cat == CAT_BOSS ? (unsigned char)boss_kind_of(cls) : (unsigned char)BOSS_K_NONE;
  s.vt = vt;
  s.skel = 0;
  s.obb = 0;
  // Read here, under the walk's region cache, so the aim's candidate loop needs no guarded read. An egg's model
  // has no head bone: it is aimed at its box.
  if (((c.cat == CAT_ENEMY && !egg) || c.cat == CAT_BOSS) && guarded_readable_cached(&rc, e, kSkelSpan))
    memcpy(&s.skel, e + 0xAE4, sizeof(s.skel));
  if (c.cat == CAT_BOSS) {
    s.immune = boss_gate(e, rc, cls);
    note_immune(e, s.immune);
  }
  if (((c.cat == CAT_ENEMY && !egg) || c.cat == CAT_BOSS) && s.hashp && (g_cfg.espOn || g_cfg.espNukes))
    carry_entries(e, rc, s, cls, capped);
  // A nuke is also a powerup class, so its +0x2E0
  // reads and clears exactly like an item/key's; a nuke with the byte clear
  // is collected and is not drawn.
  if (c.cat == CAT_ITEM || c.cat == CAT_KEY || c.cat == CAT_NUKE) {
    const bool typed = c.cat == CAT_KEY || c.kind == ITEM_AMMO || c.kind == ITEM_EGO || c.kind == ITEM_GUN;
    if (guarded_readable_cached(&rc, e, typed ? kSubtypeSpan : kItemSpan)) {
      s.active = e[0x2E0] != 0;   // cleared by powerup_on_picked duke!0x1005d950 (hitboxes.md section 0)
      // key_get_name duke!0x1005f7c0: mov eax,[ecx+0xD14], then 0/1/2 pick the colour.
      if (c.cat == CAT_KEY) s.keyColour = (int)rdu(e + 0xD14);
      // ammo_type/health_type/gun_type, the same +0xD14 dword (pickups.md section 1).
      else if (typed) { s.subtype = (int)rdu(e + 0xD14); s.name = item_label(c.kind, s.subtype, c.name); }
    }
    if (!s.active) s_snap.inactive++;
  } else if (c.cat == CAT_SECRET) {
    if (guarded_readable_cached(&rc, e, kSecretSpan)) {
      s.armed = rdu(e + 0xAE4) != 0;   // area_secret_on_enter duke!0x1009be40 counts only while nonzero
      watch_update(e, c.cat, c.name);
    }
    Vec3 bc, bh, ax[3];
    if (area_world_box(e, rc, s.pos, &bc, &bh, ax, "secrets")) {
      s.pos = bc; s.half = bh; s.hb.c = bc; s.hb.half = bh;
      s.axes[0] = ax[0]; s.axes[1] = ax[1]; s.axes[2] = ax[2];
      s.obb = 1;
    }
  } else if (c.cat == CAT_EXPLODABLE) {
    if (guarded_readable_cached(&rc, e, kExplSpan)) {
      s.active = e[0x2E0] != 0;        // cleared by breakable_break duke!0x100f1600, set again by its reset 0x100f16d0
      s.armed = rdu(e + 0xC44) != 0;
      watch_update(e, c.cat, c.name);
    } else {
      s.active = 0;                    // unread: never drawn
    }
  } else if (c.cat == CAT_BABE) {
    if (guarded_readable_cached(&rc, e, kBabeSpan)) {
      s.freed = rdu(e + 0xE10) != 0;   // set by babe_activate duke!0x10060ac0 (0x10060b56)
      watch_update(e, c.cat, c.name);
    }
  }
}

// One balanced spawn_search: start/get/stop paired on every path (entities.md section 1).
// `areas` counts every hit when non-null (secrets-walk.md section 6); kMaxWalk/total are
// shared with the caller's other searches so a corrupt list still caps the frame.
static void walk_search(int sid, int type_mask, ReadCache& rc, bool& capped, int* areas) {
  g_eng.spawn_search("s=%d?t=%d", sid, type_mask);
  const unsigned char* e;
  while ((e = (const unsigned char*)g_eng.spawn_search_get()) != 0) {
    if (s_snap.total >= kMaxWalk) { capped = true; break; }
    if (areas) ++*areas;
    walk_entity(e, rc, capped);
  }
  g_eng.spawn_search_stop();
}

// Main thread only: each search's iterator is a global stack, so start and stop pair on every path. t=2 is duke dynamic
// objects (dobj_base_ctor); area_secret and other areas never carry that bit and live in the t=0x20 (area_e) list
// instead (secrets-walk.md). watch_sweep() runs only after both walks.
static int s_walkBreakables;
// Cumulative: frames the pools were wanted, and of those the frames they could not be read.
static unsigned s_poolReads, s_poolUnread;

// The stats block's ammo (entities.md PS table; caps at PS+0x64.., ammo_on_touch duke!0x1005e6c0): two guarded reads
// a frame, the player's +0x111C slot (past probe's kPlayerSpan; actor_player is 0x1120 bytes) and PS+0x38..+0x70.
static void read_pools(const unsigned char* player, PlayerPools* pp) {
  pp->valid = 0;
  s_poolReads++;
  const unsigned char* ps = 0;
  if (guarded_readable(player + 0x111C, 4)) memcpy(&ps, player + 0x111C, sizeof ps);
  if (!ps || !guarded_readable(ps + 0x38, 0x70 - 0x38)) { s_poolUnread++; return; }
  for (int t = 0; t < 3; ++t) {
    short cur;
    memcpy(&cur, ps + 0x38 + 2 * t, sizeof cur);
    pp->cur[t] = cur;
    pp->cap[t] = rdf(ps + 0x64 + 4 * t);
  }
  pp->valid = 1;
}

static void walk(const unsigned char* player) {
  ReadCache rc = {};   // fresh each walk: a region freed since last frame is never trusted
  ReadCache fresh = {};
  s_codesRc = fresh;
  s_dropsRc = fresh;
  int sid;
  memcpy(&sid, player + 0x78, sizeof(sid));   // server id, the `s=` key (entities.md section 1)
  bool capped = false;
  // Last frame's eggs ask for the wire table: none for this level yet, or more eggs than it was built for.
  if (s_eggsLast > 0 && (!s_eggScanDone || s_eggsLast > s_eggScanEggs) && GetTickCount64() >= s_eggScanNext) {
    s_eggScanNext = GetTickCount64() + 2000;
    egg_scan(sid);
  }
  walk_search(sid, 2, rc, capped, 0);                    // duke dynamic objects (enemies, items, keys, nukes, babes...)
  walk_search(sid, 0x20, rc, capped, &s_snap.areas);      // areas, incl. area_secret (secrets-walk.md section 5)
  // Breakables: the breakable base ctor duke!0x100f11e0 ORs +0x48 with 4, so they are in the bit-2 list
  // (re/explodables.md). Only while Explodables is on: the list also holds every mover.
  if (s_walkBreakables) walk_search(sid, 4, rc, capped, &s_snap.breakables);
  if (g_cfg.espNukes) laser_entries(player, sid, rc, capped);   // its own name searches, after the walk's
  if (g_cfg.espItems) breakable_drop_entries(player, sid, rc, capped);
  if (g_cfg.espOn && g_cfg.espAmbushes) ambush_entries(player, sid, rc, capped);
  s_misses += rc.misses + s_codesRc.misses + s_dropsRc.misses;
  s_eggsLast = 0;
  for (int k = EGG_ACTIVE; k <= EGG_GONE; ++k) s_eggsLast += s_snap.eggs[k];
  if (capped) s_capped++;
  else watch_sweep();   // a capped walk missed entities: it cannot say one is gone
}

void entities_frame(const unsigned char* player) {
  s_snap.count = 0;
  s_snap.total = 0;
  s_snap.skipped = 0;
  s_snap.alive = 0;
  s_snap.inactive = 0;
  s_snap.areas = 0;
  s_snap.breakables = 0;
  s_snap.ladders = 0;
  s_snap.atLadder = 0;
  s_snap.nparts = 0;
  s_snap.partsUnread = 0;
  s_snap.nimmune = 0;
  for (int k = 0; k <= EGG_GONE; ++k) s_snap.eggs[k] = 0;
  for (int k = 0; k < 4; ++k) s_snap.att[k] = 0;
  s_holders = s_holdersHeld = s_holderNukes = s_holderItems = s_holdersUnread = s_holdersOpened = 0;
  s_carriers = s_carriedNukes = s_carryUnread = 0;
  s_lasers = s_lasersArmed = s_lasersUnfound = s_spawnersUnfound = 0;
  s_breakDrops = s_breakDropsOpen = s_breakDropsUnfound = 0;
  s_ambushes = s_ambWaiting = s_ambArmed = s_ambUnknown = s_ambSpUnfound = s_ambLookups = 0;
  {
    // Nukes and Items both off: no holder walk, so drop their watches with one line, as Explodables does.
    const int want = g_cfg.espNukes != 0 || g_cfg.espItems != 0;
    if (s_walkHolders && !want) {
      int dropped = 0;
      for (int i = 0; i < s_nwatch;) {
        if (s_watch[i].cat == kWatchHolder) { s_watch[i] = s_watch[--s_nwatch]; dropped++; }
        else ++i;
      }
      np_log("watch: Nukes and Items off; %d watched holder(s) dropped", dropped);
    }
    s_walkHolders = want;
  }
  {
    // Explodables switched off: the t=4 walk stops, so drop its watches with one line rather than a "gone" each.
    const int want = g_cfg.espExplodables != 0;
    if (s_walkBreakables && !want) {
      int dropped = 0;
      for (int i = 0; i < s_nwatch;) {
        if (s_watch[i].cat == CAT_EXPLODABLE) { s_watch[i] = s_watch[--s_nwatch]; dropped++; }
        else ++i;
      }
      np_log("watch: Explodables off; %d watched wall(s) dropped", dropped);
    }
    s_walkBreakables = want;
  }
  s_snap.pools.valid = 0;
  for (int i = 0; i < CAT_COUNT; ++i) s_snap.cats[i] = 0;
  // A player being torn down (+0x90 killed/removed) may have no server for its sid,
  // and spawn_search_get would then fault: treat it as no player.
  if (player) {
    int killed;
    memcpy(&killed, player + 0x90, sizeof(killed));   // within probe's kPlayerSpan (0xED0)
    if (killed) player = 0;
  }
  s_snap.player = player;
  if (player != s_watch_player) { s_nwatch = 0; s_watch_player = player; }
  {
    // A new level (player or world) drops the wire table and the verdicts before this frame's walk reads them.
    EggLevelKey lv = { player, 0 };
    if (player) memcpy(&lv.world, player + 0xE0, sizeof lv.world);   // within probe's kPlayerSpan
    if (!egg_level_same(lv, s_eggLevel)) {
      s_eggLevel = lv;
      s_eggScanOk = s_eggScanDone = 0;
      s_eggScanEggs = 0;
      egg_cache_clear(&s_eggCache);
    }
  }
  s_snap.playerPos = player ? rdv(player + 0x18C) : Vec3{ 0.f, 0.f, 0.f };   // entities.md section 2
  // Unproven that Duke never has a parent (a train, a lift): his +0x18C would then be local.
  if (player) { void* par = 0; memcpy(&par, player + 0x60, sizeof par); if (par) s_playerParented++; }
  s_haveHead = player != 0;
  if (player) s_head = ent_ladder_head(s_snap.playerPos, rdv(player + 0x2B8));   // +0x2B8 inside kPlayerSpan
  // No player (menus, loading): no walk, and no log line for the window.
  if (player && g_eng.spawn_search && g_eng.spawn_search_get && g_eng.spawn_search_stop) {
    if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    walk(player);
    if (g_cfg.espItems && g_cfg.espHideTaken) read_pools(player, &s_snap.pools);
    QueryPerformanceCounter(&t1);
    s_walk_us += (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)s_qpf.QuadPart;
    s_walks++;
  }
  if (s_snap.atLadder != s_lastAtLadder) {
    s_lastAtLadder = s_snap.atLadder;
    np_log("ents: Duke's head %s a ladder volume (%d ladder(s) walked), head (%.1f,%.1f,%.1f)",
           s_snap.atLadder ? "entered" : "left", s_snap.ladders, s_head.x, s_head.y, s_head.z);
  }
  const ULONGLONG tick = GetTickCount64();
  if (tick < s_next_log) return;
  s_next_log = tick + 5000;
  if (s_walks) {
    // The pools only from a frame that read them: a stale pair would read as this frame's.
    char ammo[96] = "ammo=unread";
    if (s_snap.pools.valid)
      _snprintf_s(ammo, sizeof ammo, _TRUNCATE, "ammo=%d/%.0f,%d/%.0f,%d/%.0f", s_snap.pools.cur[0], s_snap.pools.cap[0],
                  s_snap.pools.cur[1], s_snap.pools.cap[1], s_snap.pools.cur[2], s_snap.pools.cap[2]);
    np_log("ents: total=%d skipped=%d other=%d enemies=%d bosses=%d alive=%d items=%d keys=%d nukes=%d babes=%d "
           "parts=%d immune=%d immuneDropped=%u partSlots=%u partBad=%u eggs active/inactive/unread/gone=%d/%d/%d/%d "
           "eggScans=%u eggCacheFull=%u "
           "secrets=%d explodables=%d inactive=%d areas=%d ladders=%d breakables=%d watched=%d changes=%u watch_full=%u walk_us=%.1f misses=%u capped=%u "
           "secretsWalked=%u boundsRead=%u boundsUnloaded=%u boundsUnread=%u sboxFull=%u poolReads=%u poolUnread=%u "
           "%s pos root/matrix/unset/bad=%d/%d/%d/%d playerParented=%u",
           s_snap.total, s_snap.skipped, s_snap.cats[CAT_OTHER], s_snap.cats[CAT_ENEMY], s_snap.cats[CAT_BOSS],
           s_snap.alive, s_snap.cats[CAT_ITEM], s_snap.cats[CAT_KEY], s_snap.cats[CAT_NUKE], s_snap.cats[CAT_BABE],
           s_snap.nparts, s_snap.nimmune, s_immuneDropped, s_partSlots, s_partBad, s_snap.eggs[EGG_ACTIVE],
           s_snap.eggs[EGG_INACTIVE], s_snap.eggs[EGG_UNREAD], s_snap.eggs[EGG_GONE], s_eggScans,
           s_eggCache.full,
           s_snap.cats[CAT_SECRET], s_snap.cats[CAT_EXPLODABLE], s_snap.inactive, s_snap.areas, s_snap.ladders, s_snap.breakables, s_nwatch, s_watch_changes, s_watch_full,
           s_walk_us / s_walks, s_misses, s_capped, s_secretsWalked, s_boundsRead, s_boundsUnloaded, s_boundsUnread,
           s_sboxFull, s_poolReads, s_poolUnread, ammo, s_snap.att[ATT_ROOT], s_snap.att[ATT_MATRIX],
           s_snap.att[ATT_UNSET], s_snap.att[ATT_BAD], s_playerParented);
    // The holders from the last frame: entries == nukes + items, each holder's distinct contents.
    if (s_walkHolders)
      np_log("crates: %d with contents (nukes %d, items %d) of %d breakables; unread %d, broken %d", s_holdersHeld,
             s_holderNukes, s_holderItems, s_holders, s_holdersUnread, s_holdersOpened);
    // The carriers from the last frame: living enemies whose drop list names nuke.sc, and the nukes they carry.
    if (g_cfg.espOn || g_cfg.espNukes)
      np_log("carriers: %d enemies carry %d nukes; lists unread %d", s_carriers, s_carriedNukes, s_carryUnread);
    if (g_cfg.espItems && s_breakDrops)
      np_log("breakdrops: %d breakables with a pickup here, %d broken, %d unfound", s_breakDrops, s_breakDropsOpen,
             s_breakDropsUnfound);
    if (g_cfg.espOn && g_cfg.espAmbushes && s_ambushes)
      np_log("ambushes: %d here, %d waiting, %d armed lasers, %d unknown; spawners unfound %d, name searches %d a frame",
             s_ambushes, s_ambWaiting, s_ambArmed, s_ambUnknown, s_ambSpUnfound, s_ambLookups);
    if (g_cfg.espNukes && s_lasers)
      np_log("lasers: %d nuke lasers here, %d armed; lasers unfound %d, spawners unfound %d", s_lasers, s_lasersArmed,
             s_lasersUnfound, s_spawnersUnfound);
  }
  s_walk_us = 0.0; s_walks = 0; s_misses = 0;
  s_secretsWalked = s_boundsRead = s_boundsUnloaded = s_boundsUnread = 0;
}
