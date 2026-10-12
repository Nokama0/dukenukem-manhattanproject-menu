#pragma once
// Skip intro (docs/re/quick-wins.md section 1): our logo-less ui/scripts/scr_intro.sc, mounted at p61 above
// db_zoom_wide (p52) on the first frame. ui_hdl_intro then finds no group 1, hides itself on its first update
// and starts the main menu. Data and one mount only: removing winmm.dll restores the stock intro.
#include <string.h>

// Which scr_intro.sc a file head is: ours (its marker line), a stock one (a ui_group), or neither.
enum { SI_OTHER = 0, SI_STOCK = 1, SI_SKIP = 2 };
inline int si_script_kind(const char* head) {
  if (!head) return SI_OTHER;
  if (strstr(head, "nukepanel skip intro")) return SI_SKIP;
  if (strstr(head, "ui_group")) return SI_STOCK;
  return SI_OTHER;
}

// First frame, after config_load: the mount while the switch is on, then which script the engine serves.
void skipintro_mount(void);
// Every frame until the main menu appears (or 120 s): when the intro screen showed and went, for the log.
void skipintro_frame(void);
