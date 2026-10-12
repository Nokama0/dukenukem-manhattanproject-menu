// Damage numbers: the monster_apply_damage detour, its arming and the draw. dmgnum.h owns every rule; this file owns
// the engine reads and the canvas. Main thread only: the hook runs inside the game tick and the draw on the frame path
// of the same cooperative loop (render.md section 1), so the ring needs no locking.
#include "dmgnum.h"
#include "entities.h"
#include "engine.h"
#include "hook.h"
#include "offscreen.h"   // offscreen_note_kill: the first off-screen kill
#include "view.h"
#include "espbox.h"   // hitbox_from: the anchor is the ESP's own box centre
#include "esp_layout.h"   // esp_draw_label (outlined labels always on)
#include "canvas.h"
#include "font.h"
#include "config.h"
#include "log.h"
#include "ui/wallclock.h"
#include "ui/common.h"   // clamp_double_to_int
#include <windows.h>
#include <stdio.h>
#include <string.h>

// duke!0x1002f560: thiscall (type, float amount, float* dir, uint flags, object* attacker), callee pops 5 stack args
// (single exit `ret 0x14` at 0x1002fda9; entry reads type at [esp+0x48] after a 0x44-byte frame). Its return value is
// incidental (EAX from a virtual call on one path), so none is passed through.
typedef void (__thiscall* ApplyDamageFn)(void* self, int type, float amount, float* dir, unsigned flags,
                                         void* attacker);
// Slot 1, get_classname: the walk's own call (entities.cpp).
typedef const char* (__thiscall* ClassnameFn)(const void* self);

static const unsigned kSlotApplyDamage = 0x2FC / 4;   // monster vt+0x2FC (entities.md section 6)
static const unsigned kMonsterSpan = 0xB90;           // monster-base fields end here (entities.cpp)

static DmgRing s_ring;   // static zero-init: every slot not live
static ApplyDamageFn s_tramp;
static void* s_target;   // the armed slot target; set once and never cleared
static const char* s_armClass;
static int s_installFailed;   // a refused install is not retried every frame
static unsigned s_entered, s_kept, s_rej[DMG_REASON_COUNT], s_drawn;
static int s_loggedFirst;
static unsigned s_loggedEntered = 0xFFFFFFFFu;
static unsigned s_loggedDrawn = 0xFFFFFFFFu;
static ULONGLONG s_nextLog;

// Vtables already logged (armed, differing or refused), so each class logs once.
enum { kSeenMax = 64 };
static const void* s_seen[kSeenMax];
static int s_nseen;
static unsigned s_seenFull;

int dmgnum_armed(void) { return s_target != 0; }
unsigned dmgnum_entered(void) { return s_entered; }
unsigned dmgnum_kept(void) { return s_kept; }
unsigned dmgnum_rejected(int reason) { return reason > 0 && reason < DMG_REASON_COUNT ? s_rej[reason] : 0u; }
unsigned dmgnum_drawn(void) { return s_drawn; }

static float rdf(const unsigned char* p) { float f; memcpy(&f, p, sizeof(f)); return f; }
static Vec3 rdv(const unsigned char* p) { Vec3 v; memcpy(&v, p, sizeof(v)); return v; }

struct DmgPre { float before; Vec3 at; const char* name; float dir[3]; };

static int reject(int reason) { s_rej[reason]++; return reason; }

// Before the original: every read is guarded, and nothing is kept past the call
// except these copies, because the call may kill and free the victim.
static int dmg_pre(const unsigned char* e, const void* attacker, const float* dir, DmgPre* out) {
  s_entered++;
  // The Damage numbers switch gates only the ring below (dmg_post): the hook also feeds the shot measurement and the
  // off-screen kill log (offscreen.cpp), which must not depend on it. Kills are credited against get_local_player
  // (duke!0x100e7060); wm_get_input(0) is the same entity in normal play, and when they differ the hit is rejected.
  const void* player = g_eng.wm_get_input ? g_eng.wm_get_input(0) : 0;
  const int mine = attacker != 0 && attacker == player;
  if (!mine) return reject(DMG_REJ_ATTACKER);
  if (!e || !guarded_readable(e, kMonsterSpan)) return reject(DMG_REJ_UNREAD);
  void* const* vt = *(void* const* const*)e;
  if (!entities_in_duke(vt) || !entities_in_duke(vt + 2) || !entities_in_duke(vt[1]))
    return reject(DMG_REJ_UNREAD);
  const char* cls = ((ClassnameFn)vt[1])(e);
  // actor_player's slot 1 returns a non-pointer (entities.md section 2): not a monster.
  if (!entities_in_duke(cls)) return reject(DMG_REJ_CLASS);
  const EntClass c = class_lookup(cls);
  unsigned user;
  memcpy(&user, e + 0x4C, sizeof(user));   // user type bits (entities.md section 1)
  const float before = rdf(e + 0xB84);     // current health (entities.md section 3)
  const int r = dmg_pre_reason(mine, c.cat, user, before);
  if (r != DMG_KEPT) return reject(r);
  out->before = before;
  out->at = hitbox_from(rdv(e + 0x18C), rdv(e + 0x2B8), rdf(e + 0x2C4)).c;   // hitboxes.md section 0
  out->name = c.name;
  // The shot's direction times the frame time (proj_bullet_damage_hit 0x1003ad90): matches the hit to its cast.
  if (dir && guarded_readable(dir, sizeof out->dir)) memcpy(out->dir, dir, sizeof out->dir);
  return DMG_KEPT;
}

// After the original: the victim is only re-read through a guarded read.
static void dmg_post(const unsigned char* e, const DmgPre& pre) {
  if (!guarded_readable(e + 0xB84, sizeof(float))) { reject(DMG_REJ_UNREAD); return; }
  const float after = rdf(e + 0xB84);
  const int r = dmg_post_reason(pre.before, after);
  if (r != DMG_KEPT) { reject(r); return; }
  DmgEvent ev;
  ev.at = pre.at;
  ev.damage = dmg_display(pre.before - after);
  ev.kill = dmg_is_kill(pre.before, after);
  offscreen_note_hit(e, &pre.at.x, ev.kill, pre.dir);
  if (ev.kill) offscreen_note_kill(pre.name, &pre.at.x);
  // Every hit of Duke's under Debug log, beside aim's "shot" lines: how many hits each kill took.
  if (g_cfg.timing)
    np_log("dmgnum: hit t=%u %s %p health %.2f -> %.2f (damage %.2f), kill=%d", wallclock_ms(),
           pre.name ? pre.name : "?", (const void*)e, pre.before, after, pre.before - after, ev.kill);
  if (!g_cfg.dmgNumbers) { reject(DMG_REJ_OFF); return; }
  ev.victim = e;
  ev.t0 = wallclock_ms();
  ev.live = 1;
  dmg_push(&s_ring, ev);
  s_kept++;
  if (!s_loggedFirst) {
    s_loggedFirst = 1;
    np_log("dmgnum: first hit kept -- %s %p, damage %d, health %.2f -> %.2f, kill=%d",
           pre.name ? pre.name : "?", (const void*)e, ev.damage, pre.before, after, ev.kill);
  }
}

// thiscall seen as fastcall: ECX = this, EDX unused, the 5 stack args and the callee's
// ret 0x14 are the same in both, and the float travels in its stack slot unchanged.
static void __fastcall dmg_detour(void* self, void* /*edx*/, int type, float amount, float* dir,
                                  unsigned flags, void* attacker) {
  DmgPre pre = {};
  const int reason = dmg_pre((const unsigned char*)self, attacker, dir, &pre);
  s_tramp(self, type, amount, dir, flags, attacker);
  if (reason == DMG_KEPT) dmg_post((const unsigned char*)self, pre);
}

static bool seen_once(const void* vt) {
  for (int i = 0; i < s_nseen; ++i) if (s_seen[i] == vt) return true;
  if (s_nseen >= kSeenMax) { s_seenFull++; return true; }
  s_seen[s_nseen++] = vt;
  return false;
}

void dmgnum_arm(const void* const* vt, const char* className) {
  if (!vt) return;
  const void* const* slot = vt + kSlotApplyDamage;
  // The vtable is in duke_base's image (the walk checked vt); so must its slot be.
  if (!entities_in_duke(slot) || !entities_in_duke((const unsigned char*)(slot + 1) - 1)) return;
  void* const target = (void*)*slot;
  if (s_target && target == s_target) return;   // the common case, every frame
  if (seen_once(vt)) return;
  if (s_target) {
    np_log("dmgnum: %s vt %p slot +0x2FC -> %p, not the armed %p; its hits show only if it calls through",
           className, (const void*)vt, target, s_target);
    return;
  }
  if (s_installFailed) return;
  const unsigned char* base = 0;
  unsigned size = 0;
  if (!entities_duke_image(&base, &size)) { np_log("dmgnum: duke_base image unknown; not arming"); return; }
  if (!entities_in_duke(target) || !guarded_readable(target, DMG_FP_LEN)) {
    np_log("dmgnum: %s slot +0x2FC -> %p is outside duke_base or unreadable; not arming from it",
           className, target);
    return;
  }
  unsigned char live[DMG_FP_LEN];
  memcpy(live, target, DMG_FP_LEN);
  if (!dmg_fp_match(live, (unsigned)(size_t)base, size)) {
    char hex[DMG_FP_LEN * 3 + 1];
    for (int i = 0; i < DMG_FP_LEN; ++i) sprintf_s(hex + 3 * i, sizeof(hex) - 3 * i, "%02X ", live[i]);
    np_log("dmgnum: %s slot +0x2FC -> %p fingerprint refused (image %p+%X): %s", className, target,
           (const void*)base, size, hex);
    return;
  }
  // The live bytes, already matched above, are the fingerprint hook_install_at re-checks.
  if (!hook_install_at(target, live, DMG_FP_LEN, DMG_TRAMP_LEN, (void*)dmg_detour, (void**)&s_tramp)) {
    s_installFailed = 1;
    np_log("dmgnum: install at %p refused; the hook stays unarmed", target);
    return;
  }
  s_target = target;
  s_armClass = className;
  np_log("dmgnum: armed at %p (duke_base+%X) from %s vt %p, fingerprint matched, handler %02X%02X%02X%02X",
         target, (unsigned)((const unsigned char*)target - base), className, (const void*)vt,
         live[6], live[5], live[4], live[3]);
}

static void dmg_log(void) {
  const ULONGLONG tick = GetTickCount64();
  if (tick < s_nextLog) return;
  s_nextLog = tick + 5000;
  // Cumulative: an unchanged line says nothing. drawn keeps rising after the last hit, so it counts too.
  if (!s_target || (s_entered == s_loggedEntered && s_drawn == s_loggedDrawn)) return;
  s_loggedEntered = s_entered;
  s_loggedDrawn = s_drawn;
  np_log("dmgnum: entered=%u kept=%u off=%u attacker=%u unread=%u class=%u dead=%u nodamage=%u drawn=%u "
         "seenFull=%u armedBy=%s",
         s_entered, s_kept, s_rej[DMG_REJ_OFF], s_rej[DMG_REJ_ATTACKER], s_rej[DMG_REJ_UNREAD],
         s_rej[DMG_REJ_CLASS], s_rej[DMG_REJ_DEAD], s_rej[DMG_REJ_NODAMAGE], s_drawn, s_seenFull, s_armClass ? s_armClass : "?");
}

// Each number is projected at its hit-time anchor, never the victim's current place:
// the entity may have been freed since. On screen only, no draw distance (as the ESP).
void dmgnum_draw(int W, int H) {
  dmg_log();
  if (!g_cfg.dmgNumbers || !s_target) return;
  const unsigned now = wallclock_ms();
  int live = 0;
  for (int k = 0; k < DMG_MAX; ++k) live += dmg_is_live(s_ring.e[k], now);
  if (!live) return;
  Mat4 view, proj;
  float nearZ;
  if (!view_get(&view, &proj, &nearZ)) return;
  const int textH = font_height[FONT_UI];
  bool begun = false;
  for (int k = 0; k < DMG_MAX; ++k) {
    const DmgEvent& e = s_ring.e[k];
    if (!dmg_is_live(e, now)) continue;
    const ScreenPt sp = w2s_project(e.at, view, proj, nearZ, W, H);
    if (!sp.ok || sp.x < 0.f || sp.x > (float)W || sp.y < 0.f || sp.y > (float)H) continue;
    char s[16];
    sprintf_s(s, "%d", e.damage);
    const double prog = dmg_progress(e.t0, now);
    const int rise = dmg_rise_px(prog, textH) + dmg_stack_index(s_ring, k, now) * textH;
    const unsigned col = canvas_with_alpha(dmg_colour(e.kill), dmg_alpha(prog));
    if (!begun) { canvas_begin(W, H); begun = true; }
    esp_draw_label(clamp_double_to_int((double)sp.x + 0.5) - canvas_text_width(s, FONT_UI) / 2,
                clamp_double_to_int((double)sp.y + 0.5) - textH / 2 - rise, s, col, FONT_UI);
    s_drawn++;
  }
  if (begun) canvas_end();
}
