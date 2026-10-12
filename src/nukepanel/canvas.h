// Ported from the Alien Armageddon port's ekmod/ui/canvas.h: the palette and declarations are its, so menu.cpp ports
// unchanged. The NO_GL recording API (canvas_record_*) is dropped. The backend is prim.cpp on prism3d's ortho pool.
#pragma once
#include "classes.h"   // CAT_* for marker_colour below
#include "ui/ui.h"     // ui_scale_clamped, UI_SCALE_DEFAULT for canvas_atlas_step
// Colours are 0xAARRGGBB.
const unsigned C_PANEL   = 0xF014171A;
const unsigned C_RECESS  = 0xF01C2127;
const unsigned C_RAISED  = 0xFF333C46;
const unsigned C_BEVELHI = 0xFF48535F;
const unsigned C_BEVELLO = 0xFF0A0C0E;
const unsigned C_HAZARD  = 0xFFF2B01E;
const unsigned C_RED     = 0xFFD8402F;
const unsigned C_BLUE    = 0xFF3C9BD6;
const unsigned C_GREEN   = 0xFF6FBF5B;
const unsigned C_BONE    = 0xFFE8E4DA;
const unsigned C_BONEDIM = 0xFF9AA1A8;
const unsigned C_BONEFNT = 0xFF666E77;
const unsigned C_WELL      = 0xFF0E1114;  // darkest sunken steel: switch/slider wells, tab rail
const unsigned C_HAZARDDIM = 0xFF8A6512;  // dimmed hazard, the slider's filled track
const unsigned C_AIM       = 0xFFC061E8;  // aim calibration marker: violet, unused elsewhere so a 4th ESP colour can't be mistaken for enemy/item/secret
const unsigned C_PLAYER    = 0xFF2FE0D0;  // free camera's own-body marker: cyan, the one hue distinct from enemy/item/secret/aim
const unsigned C_SETTINGS  = 0xFFB8A9E0;  // Settings tab rail accent: muted lavender, distinct from the other tab colours and from C_AIM/C_PLAYER
const unsigned C_PROGRESS  = 0xFF4FC9B0;  // Progress tab rail accent: teal, between the View green and the Visuals blue
const unsigned C_KEYLINE   = 0xFF000000;  // world-overlay text outline (AA's C_KEYLINE/C_CORNER_OUTLINE), always on

// Friendlies take hue 325, the one unoccupied region (red 15 enemies, yellow 43 secrets, green 108 View/free camera,
// cyan 175 camera body and landing column, blue 203 items/switches, violet 280 aim, lavender Settings), in C_AIM's
// saturation family. Blue is wrong: every pickup's brackets are C_BLUE (esp_items.h), a blue friendly reads as an item.
const unsigned C_FRIENDLY  = 0xFFE85FA8;  // friendlies: magenta, hue 325
const unsigned C_AMBUSH    = 0xFFF57A1F;  // ambushes not fired yet: orange, between the hazard yellow and enemy red

// Combination tint: a VOLUME drawn in-world around the lock's model, so not C_GREEN/C_RED (made for the panel's dark
// ground); pushed toward full saturation to survive a dark wall. Over the switch body only one layer blends: the box
// encloses a depth-writing model under GL_LEQUAL, so its far face is rejected. Alpha 0x76: 0x40 read too faint.
const unsigned C_TINT_BOX_FLIP  = 0x7660FF60;  // combination volume, flip: the tint green
const unsigned C_TINT_BOX_LEAVE = 0x76FF5050;  // combination volume, leave: the tint red

// Secret volume outline: C_HAZARD's hue (matches the "Secret" ESP marker) at
// a lower alpha (0x40) than the tint above, since 2-3 room panels can overlap
// along one sightline and would otherwise wash out the texture behind them.
const unsigned C_SECRET_VOLUME = 0x40F2B01E;  // secret volume: the secret yellow, barely there

// Teleport landing marker: C_PLAYER's cyan at C_SECRET_VOLUME's alpha, so the
// column reads as "you land here" rather than as another secret room, and
// stacks over the secret panels without washing out what is behind them.
const unsigned C_TELEPORT_MARK = 0x402FE0D0;  // landing column: the camera's own cyan, barely there

// Shot tracers: two lines told apart at a glance and not mistaken for a marker (which own six hues). The predicted
// ray is achromatic near-white ("exact"); the actual impact is rose at about 340, the widest gap left between the
// violet aim marker and the enemy red.
const unsigned C_TRACE_PRED = 0xFFF2F4FF;  // predicted ray: cool near-white, ours and exact
const unsigned C_TRACE_REAL = 0xFFFF3E8C;  // actual impact: hot rose, the engine's own answer

// Glass style's four colours (menuanim == 2 only; unused under Off/Simple),
// the only translucent-surface entries in this palette -- what a surface is
// seen through, not a surface itself.
const unsigned C_GLASS_TINT = 0x99141C26;  // slab ground over the blurred backdrop capture; cool/dark since Duke levels read brown/orange, alpha doubles as "how much scene shows through"
const unsigned C_GLASS_RIM  = 0xB0A8C8E0;  // one-pixel rim under the slab; brighter than C_BEVELHI since Glass has no bevel to share the job
const unsigned C_GLASS_TOP  = 0x40D8E8F8;  // top-edge sheen gradient, not a second rim -- stops the slab reading as a flat hole
const unsigned C_GLASS_GLOW = 0x66A8D8FF;  // additive bloom (open sweep, switch flare, hover glow); one colour since additive RGB adds to what's under it

// True when canvas_hazard's stripe geometry is well formed: a non-positive period never advances the stripe loop
// (glBegin left open forever) and a non-positive w/h gives a degenerate scissor box. Header-only for test_host.
inline bool canvas_hazard_params_ok(int w, int h, int period) {
  return w > 0 && h > 0 && period > 0;
}

// ESP marker colour: red enemy, blue pickup; colour alone separates them (same box shape). Keycards colour the NAME,
// not the box, so a red box always reads as "enemy".
inline unsigned canvas_esp_colour(bool enemy) { return enemy ? C_RED : C_BLUE; }

// Secret-area marker colour: hazard yellow, distinct from the enemy/item
// pair above.
inline unsigned canvas_esp_secret_colour() { return C_HAZARD; }

// Teleport failure notice colour: the same warning yellow used everywhere
// else on this overlay (hazard strip, slider knob, secret markers).
inline unsigned canvas_notice_colour() { return C_HAZARD; }

// Aim calibration marker colour: distinct from enemy/item/secret.
inline unsigned canvas_aim_colour() { return C_AIM; }

// Free camera's own-body marker colour: distinct from all four other
// marker colours.
inline unsigned canvas_player_colour() { return C_PLAYER; }

// Shot tracer colours: two functions, not one with a flag, because the predicted line is exact and the actual one a
// best-effort pairing; a caller that must name which it draws cannot colour a guess as a certainty.
inline unsigned canvas_tracer_predicted_colour() { return C_TRACE_PRED; }
inline unsigned canvas_tracer_actual_colour() { return C_TRACE_REAL; }

// Explodables marker colour (crack sprite and breakable wall): shares C_HAZARD with the secret colour on purpose, the
// label separates the categories; kept as its own function in case that changes.
inline unsigned canvas_esp_explodable_colour() { return C_HAZARD; }

// Switch/lock marker colour: pickup blue, safe to reuse since a switch's
// cross shape never shares a silhouette with a pickup's box.
inline unsigned canvas_esp_switch_colour() { return C_BLUE; }

// Cash-container marker colour: green, and above all not C_HAZARD. This row marks the SUBSET of breakables that pay,
// and the Explodables/Breakables rows draw the same cross in hazard yellow, so a shared colour would hide the
// distinction. C_GREEN is also the View accent and health band, but no other ESP marker uses it.
inline unsigned canvas_esp_cash_colour() { return C_GREEN; }

// Category markers: fixed per category, except a keycard whose LABEL (never its generic-blue box) takes the card's
// colour. An out-of-range card colour reads as a plain key.
inline unsigned marker_colour(int cat, int keyColour) {
  if (cat == CAT_NUKE) return C_GREEN;
  if (cat == CAT_BABE) return C_FRIENDLY;
  if (cat == CAT_SECRET) return C_HAZARD;
  if (cat == CAT_AMBUSH) return C_AMBUSH;
  if (cat == CAT_EXPLODABLE) return C_HAZARD;   // AA's canvas_esp_explodable_colour; the x tells it from a secret
  if (cat == CAT_KEY) {
    if (keyColour == 0) return C_BLUE;
    if (keyColour == 1) return C_HAZARD;
    if (keyColour == 2) return C_RED;
    return C_BLUE;
  }
  return C_BLUE;   // CAT_ITEM and anything else: the generic pickup blue
}

// Health bar colour at hp/maxhp.
inline unsigned canvas_health_colour(int hp, int maxhp) {
  // Non-positive maxhp would divide by zero; non-positive hp has no band.
  if (maxhp <= 0 || hp <= 0) return C_RED;
  // Widened before the multiply: hp is a short from the sprite's `extra`
  // field and the caller may not have bounded it.
  const long long pct = (long long)hp * 100 / maxhp;
  if (pct > 50) return C_GREEN;
  if (pct > 25) return C_HAZARD;
  return C_RED;
}

// Filled length of a bar `total` pixels long at hp/maxhp; never exceeds
// `total` even if hp reads above its learned maximum.
inline int canvas_bar_fill(int total, int hp, int maxhp) {
  if (total <= 0 || maxhp <= 0 || hp <= 0) return 0;
  // Taken before the multiply so the widened product can't exceed `total`.
  if (hp >= maxhp) return total;
  return (int)((long long)total * hp / maxhp);
}

// Alpha of tap `i` of a `taps`-tap blur whose passes must sum to exactly aTint (a heavier sum makes the slab too
// opaque). Cumulative rounding, not aTint/taps plus a remainder: that goes negative on the last tap (aTint=5, taps=9)
// and a negative alpha wraps to a wrong colour byte. Each tap is a difference of rounded running totals.
inline int canvas_tap_alpha(int aTint, int taps, int i) {
  if (taps <= 0 || i < 0 || i >= taps) return 0;
  if (aTint < 0) aTint = 0;
  if (aTint > 255) aTint = 255;
  const int upto = (int)((double)aTint * (double)(i + 1) / (double)taps + 0.5);
  const int prev = (int)((double)aTint * (double)i / (double)taps + 0.5);
  return upto - prev;
}

// A colour (0xAARRGGBB) with its alpha byte replaced and clamped; the colour channels survive bit for bit. Shared by
// the damage-number fade, the panel's open alpha and tab crossfade, and the Glass grounds/glows.
inline unsigned canvas_with_alpha(unsigned rgba, int alpha) {
  if (alpha < 0) alpha = 0;
  if (alpha > 255) alpha = 255;
  return (rgba & 0x00FFFFFFu) | ((unsigned)alpha << 24);
}

// Canvas-wide open-motion transform: moves every drawn pixel while the LOGICAL layout hit testing uses stays put.
// Applied to device coordinates after the UI scale: d = ((p - c) * scale + c) + offset. Identity is exact and the
// default (short circuit), so Off and the earlier ESP draw are unchanged. canvas_begin resets it; menu_draw sets it.
struct CanvasTransform { int dx, dy, cx, cy; double scale; };
void canvas_set_transform(int dx, int dy, double scale, int cx, int cy);

// Bit i set: the quit release drops held[i]'s reference. Only a held, readable item
// whose count (+0x44) still includes ours: below 1 someone else already dropped it and a
// decrement would underflow, so the item would never be freed. None once released.
inline unsigned canvas_release_mask(const void* const held[2], const bool readable[2], const int refs[2],
                                    bool alreadyReleased) {
  unsigned m = 0;
  if (alreadyReleased) return 0;
  for (int i = 0; i < 2; i++)
    if (held[i] && readable[i] && refs[i] >= 1) m |= 1u << i;
  return m;
}

// Detours the engine's quit teardown so the canvas drops its cache references
// before the main loop waits for the process list to empty. Returns 1 when installed.
int canvas_quit_install(void);
// The config-write detour is in: the archived cvars we change go back before config.cfg is written.
int canvas_quit_installed(void);
// Drops them now and stops the canvas for good (no draw, no re-acquire). Main thread.
void canvas_release_for_quit(void);

// Which atlas a scale draws from (canvas_set_scale): 100 is baked; another scale needs its own rasterised atlas
// (fontgen.h): held if it is that scale's, else built, else refused (100% again) once a build failed, so one warning
// and never a rebuild per frame. pct is clamped; heldPct and failedPct are 0 for none.
enum { CANVAS_ATLAS_BAKED = 0, CANVAS_ATLAS_HELD = 1, CANVAS_ATLAS_BUILD = 2, CANVAS_ATLAS_REFUSED = 3 };
inline int canvas_atlas_step(int pct, int heldPct, int failedPct) {
  const int p = ui_scale_clamped(pct);
  if (p == UI_SCALE_DEFAULT) return CANVAS_ATLAS_BAKED;
  if (p == heldPct) return CANVAS_ATLAS_HELD;
  if (p == failedPct) return CANVAS_ATLAS_REFUSED;
  return CANVAS_ATLAS_BUILD;
}

// Primitive coordinates are LOGICAL; the canvas multiplies by this factor. menu_draw sets it and restores
// UI_SCALE_DEFAULT before returning (the ESP shares the primitives); canvas_text_width stays LOGICAL. If the atlas
// cannot be built the setter applies 100 and warns once: read the applied scale from canvas_scale().
void canvas_set_scale(int pct);
int  canvas_scale();

void canvas_begin(int w, int h);
void canvas_end();
void canvas_rect(int x, int y, int w, int h, unsigned rgba);
void canvas_bevel(int x, int y, int w, int h, unsigned fill, unsigned hi, unsigned lo);
void canvas_line(int x0, int y0, int x1, int y1, unsigned rgba);
// A flat triangle, any winding (reordered to the engine quad's); a degenerate
// one draws nothing. The arrowheads and the secret volumes' faces.
void canvas_tri(int x0, int y0, int x1, int y1, int x2, int y2, unsigned rgba);
void canvas_hazard(int x, int y, int w, int h, unsigned stripe, unsigned ground, int period);
int canvas_text(int x, int y, const char* s, unsigned rgba, int font);
int canvas_text_width(const char* s, int font);

void canvas_round_rect(int x, int y, int w, int h, int r, unsigned rgba);
// A rounded rectangle's outline: a ring `thickness` logical pixels wide just inside the rect. Its own primitive, as
// a filled round rect under the slab only reads as an edge under an opaque slab (Glass's translucent one washed the
// face light blue). `thickness` floors at one device pixel.
void canvas_round_rect_outline(int x, int y, int w, int h, int r, int thickness,
                               unsigned rgba);
// A clip rect over everything drawn until the matching pop, in LOGICAL coordinates (scale and transform applied
// here); shapes and glyphs share one draw list. Used by canvas_hazard and the panel's scrolled tab body. Pushed
// without intersecting, like the scissor box it stands in for; every push must be popped in the same frame.
void canvas_push_clip(int x, int y, int w, int h);
void canvas_pop_clip();

void canvas_gradient_rect(int x, int y, int w, int h, unsigned top, unsigned bottom);
void canvas_glow_rect(int x, int y, int w, int h, unsigned rgba);
bool canvas_backdrop_capture(int x, int y, int w, int h);
void canvas_backdrop_draw(int x, int y, int w, int h, int r, unsigned tint,
                          double wobbleAmp, double wobblePhase, int blurTaps, int blurRadius);
void canvas_backdrop_release();

// The atlas texel a flat fill samples, for a caller queueing its own triangles through the batcher. Selects the
// frame's atlas as the primitives do, so the triangles share the batch. False when no atlas could be built.
bool canvas_white_texel(float* u, float* v);
