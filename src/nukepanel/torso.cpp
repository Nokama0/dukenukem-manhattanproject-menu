// Torso bend: detours player_spine_bend duke!0x1007c7f0 and, while mouse aim has an angle, replays its body with that
// angle in place of the hard-coded 0/11/22 target. torso.h owns the rules. Main thread only: the detour runs in
// player_tick, the frame path in the same loop.
#include "torso.h"
#include "mouseaim.h"   // mouseaim_point
#include "aim.h"        // aim_row_xform, aim_fire_origin
#include "offscreen.h"  // offs_scan_duke
#include "engine.h"
#include "entities.h"    // entities_duke_image
#include "hook.h"
#include "config.h"
#include "log.h"
#include <windows.h>
#include <string.h>

typedef void (__fastcall* BendFn)(unsigned char* player);

// The fields the original reads and writes (mouse-aim.md section 1a): +0xE0 world (+0x38 the tick's dt as a
// double), +0xAE4 the skmod model, +0xDE4 the pose, +0xDE8 its animation layer, +0xDEC the bend in degrees.
static const unsigned kOfsWorld = 0xE0, kOfsModel = 0xAE4, kOfsPose = 0xDE4, kOfsLayer = 0xDE8, kOfsBend = 0xDEC;
static const unsigned kOfsWorldMatrix = 0xF4;   // row-vector world matrix (aim.cpp)
static const char kSpine[] = "Bip01 Spine1";
// The original's two pushed doubles: FLT_MAX widened (duke!0x10136500, 0x47EFFFFFE0000000) and 1000.0.
static const unsigned long long kLayerWeightBits = 0x47EFFFFFE0000000ull;
static const double kLayerPriority = 1000.0;

static BendFn s_tramp;
static void* s_target;
static int s_refused;
static const unsigned char* s_player;   // the frame path's validated player the angle is for
static int s_haveAngle;
static float s_angle;
static const void* s_boneVt;            // the player class whose Spine1 index is cached (as aim.cpp's head cache)
static int s_boneIdx;
static int s_owned;                     // the last tick's bend was ours and is not 0 yet (tb_owner)
static const unsigned char* s_ownedPlayer;
static unsigned s_calls, s_ours, s_easeOut, s_pass, s_unread, s_poses, s_layers, s_removed;
static unsigned s_frames, s_noPoint, s_chestBone, s_chestEye, s_clampHits;
static float s_lastBend;
static ULONGLONG s_nextLog;
static unsigned s_loggedSum = 0xFFFFFFFFu;

int torso_refused(void) { return s_refused; }
void torso_clear(void) { s_haveAngle = 0; }

template <class T> static T rd(const unsigned char* p) { T v; memcpy(&v, p, sizeof v); return v; }
template <class T> static void wr(unsigned char* p, T v) { memcpy(p, &v, sizeof v); }

// The original's body (read whole, 0x1007c7f0-0x1007c9df) with our target: the same fields, the same pose calls
// in the same order. Only the target and the ease (both ways, never snapping) differ.
static void bend_ours(unsigned char* p, float dt, float target) {
  const float v = tb_bend_step(rd<float>(p + kOfsBend), target, dt);
  wr<float>(p + kOfsBend, v);
  s_lastBend = v;
  if (v != 0.f) {
    if (!rd<void*>(p + kOfsPose)) {
      void* pose = g_sk.skmod_open_pose(1);
      wr<void*>(p + kOfsPose, pose);
      s_poses++;
      if (pose) {
        g_sk.skmod_set_pose_bone_name(pose, 0, kSpine);
        g_sk.skmod_copy_pose_bones(rd<void*>(p + kOfsPose), rd<void*>(p + kOfsModel));
      }
    }
    if (!rd<void*>(p + kOfsLayer) && rd<void*>(p + kOfsPose)) {
      double weight;
      memcpy(&weight, &kLayerWeightBits, sizeof weight);
      void* layer = g_sk.skmod_add_pose_animation(rd<void*>(p + kOfsModel), rd<void*>(p + kOfsPose), weight);
      wr<void*>(p + kOfsLayer, layer);   // stored before the priority call, as the original does (0x1007c96f)
      g_sk.skmod_anim_priority(rd<void*>(p + kOfsModel), layer, kLayerPriority);
      s_layers++;
    }
    const float rot[3] = { 0.f, rd<float>(p + kOfsBend), 0.f };
    g_sk.skmod_set_pose_rotation(rd<void*>(p + kOfsPose), 0, rot, 0);
    return;
  }
  if (rd<void*>(p + kOfsLayer)) {
    g_sk.skmod_rmv_pose_animation(rd<void*>(p + kOfsModel), rd<void*>(p + kOfsLayer));
    wr<void*>(p + kOfsLayer, (void*)0);
    s_removed++;
  }
}

// fastcall(ECX = player), plain RET: the original's own convention (mouse-aim.md section 1d).
static void __fastcall bend_detour(unsigned char* p) {
  s_calls++;
  const int have = mouseaim_on() && s_haveAngle && p == s_player;
  const int owned = s_owned && p == s_ownedPlayer;
  const int who = tb_owner(have, owned, owned ? rd<float>(p + kOfsBend) : 0.f);
  if (who == TB_STOCK) { s_owned = 0; s_pass++; s_tramp(p); return; }
  // The engine reads these unguarded; a null model or an unreadable world gets the engine's own code.
  const unsigned char* world = rd<const unsigned char*>(p + kOfsWorld);
  if (!rd<void*>(p + kOfsModel) || !world || !guarded_readable(world + 0x38, sizeof(double))) {
    s_owned = 0;
    s_unread++;
    s_tramp(p);
    return;
  }
  if (who == TB_OURS) s_ours++; else s_easeOut++;
  bend_ours(p, (float)rd<double>(world + 0x38), who == TB_OURS ? s_angle : 0.f);
  s_owned = rd<float>(p + kOfsBend) != 0.f;
  s_ownedPlayer = p;
}

// Armed the first time Mouse aim and Torso bend are both on: off, the tick is stock. A refusal is final.
static void arm(void) {
  if (s_target || s_refused) return;
  const unsigned char* hit = 0;
  const int n = offs_scan_duke(kTbSpineFp, kTbSpineMask, TB_FP_LEN, &hit);
  if (n < 0) return;   // the image is not readable yet: next frame
  if (!engine_resolve_skmod_pose()) {
    if (!GetModuleHandleA("skmod.dll")) return;
    s_refused = 1;
    np_log("torso: refused: skmod.dll's pose exports are missing");
    return;
  }
  s_refused = 1;
  if (n != 1) { np_log("torso: refused: player_spine_bend's fingerprint matched %d time(s), need 1", n); return; }
  unsigned char live[TB_HOOK_FP_LEN];
  memcpy(live, hit, sizeof live);   // relocation-free, matched by the scan
  if (!hook_install_at((void*)hit, live, TB_HOOK_FP_LEN, TB_TRAMP_LEN, (void*)bend_detour, (void**)&s_tramp)) {
    np_log("torso: install at %p refused", (const void*)hit);
    return;
  }
  s_refused = 0;
  s_target = (void*)hit;
  const unsigned char* base = 0;
  unsigned size = 0;
  entities_duke_image(&base, &size);
  np_log("torso: armed at %p (duke_base+%X)", (const void*)hit, (unsigned)(hit - base));
}

// "Bip01 Spine1" through the world matrix, as aim.cpp's head point; else the fire origin.
static bool chest_point(const unsigned char* player, Vec3* out) {
  void* skel = rd<void*>(player + kOfsModel);
  const void* vt = rd<const void*>(player);
  if (skel && engine_resolve_skmod_bones()) {
    if (vt != s_boneVt) {   // one rig per class: a model freed and reallocated keeps its class's index
      s_boneVt = vt;
      s_boneIdx = g_sk.skmod_get_bone_index(skel, kSpine);
      np_log("torso: %s of player vt %p = %d%s", kSpine, vt, s_boneIdx, s_boneIdx < 0 ? ", the fire origin stands in" : "");
    }
    float m[16];
    if (s_boneIdx >= 0 && g_sk.skmod_get_bone_transform(skel, s_boneIdx, m)) {
      float world[16];
      memcpy(world, player + kOfsWorldMatrix, sizeof world);   // inside the validated player span
      *out = aim_row_xform(Vec3{ m[12], m[13], m[14] }, world);
      s_chestBone++;
      return true;
    }
  }
  if (!aim_fire_origin(player, out)) return false;
  s_chestEye++;
  return true;
}

static void torso_log(void) {
  const ULONGLONG tick = GetTickCount64();
  if (tick < s_nextLog) return;
  s_nextLog = tick + 5000;
  const unsigned sum = s_calls + s_frames + s_noPoint;
  if (sum == s_loggedSum) return;
  s_loggedSum = sum;
  // calls == ours + easeOut + pass + unread; frames (an angle set) == chestBone + chestEye.
  np_log("torso: armed=%d calls=%u ours=%u easeOut=%u pass=%u unread=%u poses=%u layers=%u removed=%u | frames=%u noPoint=%u "
         "chestBone=%u chestEye=%u clampHits=%u target=%.1f bend=%.1f", s_target != 0, s_calls, s_ours, s_easeOut,
         s_pass, s_unread, s_poses, s_layers, s_removed, s_frames, s_noPoint, s_chestBone, s_chestEye, s_clampHits, s_angle,
         s_lastBend);
}

void torso_frame(const unsigned char* player) {
  torso_log();
  s_haveAngle = 0;
  if (!mouseaim_on() || !player) return;
  arm();
  if (!s_target) return;
  Vec3 aim, chest;
  if (!mouseaim_point(&aim)) { s_noPoint++; return; }
  if (!chest_point(player, &chest)) { s_noPoint++; return; }
  int clamped = 0;
  s_angle = tb_bend_target(chest, aim, &clamped);
  if (clamped) s_clampHits++;
  s_player = player;
  s_haveAngle = 1;
  s_frames++;
}
