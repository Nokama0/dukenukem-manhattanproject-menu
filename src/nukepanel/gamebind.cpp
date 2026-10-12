// "Also bound in the game" glue (gamebind.h). Main thread only: the panel draws on the frame path. The trigger
// table is prism3d's static .data, checked with one VirtualQuery over its whole range per panel open (the 12 us
// rule); bind_get is the engine's own walk of its bind table.
#include "gamebind.h"
#include "engine.h"
#include "log.h"
#include <windows.h>
#include <string.h>

static const unsigned char* s_base;   // inp_trigger(0) once this session's check passed
static signed char s_answer[256];     // per VK this session: -1 not asked yet, 0 free, 1 bound
static int s_loggedOff, s_bound, s_asked, s_sessions;

void gamebind_begin_session(void) {
  memset(s_answer, -1, sizeof s_answer);
  if (s_asked) np_log("gamebind: last session asked %d key(s), %d also bound in the game", s_asked, s_bound);
  s_asked = s_bound = 0;
  s_sessions++;
  s_base = NULL;
  if (!g_eng.inp_trigger || !g_eng.bind_get) {
    if (!s_loggedOff++) np_log("gamebind: inp_trigger/bind_get unresolved; no \"Also bound in the game\" notes");
    return;
  }
  const unsigned char* base = g_eng.inp_trigger(0);
  const unsigned char* last = g_eng.inp_trigger(GB_TRIGGERS - 1);
  if (last != base + (GB_TRIGGERS - 1) * GB_TRIGGER_SIZE || !guarded_readable(base, GB_TRIGGERS * GB_TRIGGER_SIZE)) {
    if (!s_loggedOff++) np_log("gamebind: trigger table %p..%p not as read (or unreadable); no notes", base, last);
    return;
  }
  s_base = base;
}

bool gamebind_vk_bound(int vk) {
  if (!s_base || vk <= 0 || vk > 255) return false;
  if (s_answer[vk] >= 0) return s_answer[vk] != 0;
  const unsigned vsc = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC_EX);
  const int idx = gb_trigger_for_vk(vk, vsc);
  bool bound = false;
  char name[GB_NAME_MAX] = "";
  if (idx >= 0 && idx < GB_TRIGGERS) {
    const unsigned char* rec = s_base + idx * GB_TRIGGER_SIZE;
    memcpy(name, rec, sizeof name - 1);
    name[sizeof name - 1] = 0;
    unsigned imp[GB_IMPULSE_SLOTS];
    memcpy(imp, rec + GB_OFS_IMPULSE, sizeof imp);
    // An unnamed trigger has no bind by name; its impulse slots still answer.
    bound = gb_trigger_bound(name[0] && g_eng.bind_get(name) != NULL, imp);
  }
  s_answer[vk] = bound ? 1 : 0;
  s_asked++;
  if (bound) s_bound++;
  // Each key once per launch (the first panel open); later sessions log only their totals.
  if (s_sessions == 1)
    np_log("gamebind: vk 0x%02x -> scan 0x%x trigger %d \"%s\": %s", vk, vsc, idx, name, bound ? "BOUND in the game" : "free");
  return bound;
}
