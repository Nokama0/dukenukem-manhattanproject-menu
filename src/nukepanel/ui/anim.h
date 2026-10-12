// Verbatim from Megaton's src/duke3dmod/anim.h; kept identical on purpose -- do not edit here.
#pragma once
#include <math.h>

// Pure animation layer for the Simple and Glass presentation styles; menu.cpp is the only caller. Wall clock
// (wallclock.h), never the tic clock: the panel is mostly open while the game is paused. Progress/dt use unsigned
// millisecond differences, wrap-safe like dmgnum.h's clock arithmetic.

// menuanim's three values (config.h). ANIM_GLASS draws exactly like
// ANIM_SIMPLE for now (menu.cpp's presentation switch).
enum { ANIM_OFF = 0, ANIM_SIMPLE = 1, ANIM_GLASS = 2 };

// Nothing over 200 ms; hover rises faster than it falls so it reads as a
// nudge rather than a toggle.
const unsigned ANIM_OPEN_MS      = 150;
const unsigned ANIM_TAB_MS       = 120;
const unsigned ANIM_CTRL_MS      = 120;
const unsigned ANIM_HOVER_IN_MS  = 80;
const unsigned ANIM_HOVER_OUT_MS = 160;

// Dt clamp ceiling: a hitch (alt-tab, breakpoint, slow frame) must not fling
// a spring at the velocity a multi-second "dt" would imply.
const double ANIM_DT_MAX_MS = 50.0;

// How long menu_draw may go uncalled before the open spring reads as settled. The spring only advances while menu_draw
// runs (overlay.cpp skips it on a non-GL rendmode or zero-sized frame), and every animated duration here is under
// 650 ms, so past a second nothing could still be moving.
const unsigned ANIM_STALE_MS = 1000;

// A damped spring's two parameters, named as a pair so a style's feel is one
// constant to pass around.
struct AnimSpringParams { double stiffness, damping; };

// Critical damping at 400 stiffness is 2*sqrt(400) = 40: settles onto target
// without crossing it (pinned in tb_0099: semi-implicit Euler at 16 ms steps
// stays within 1e-15 of 1.0, never above).
const AnimSpringParams ANIM_SPRING_CRITICAL = { 400.0, 40.0 };   // settles, never crosses
// Under-damped relative to critical (2*sqrt(300) ~= 34.6 > 22): one visible
// overshoot before settling, the "glass" feel.
const AnimSpringParams ANIM_SPRING_GLASS    = { 300.0, 22.0 };   // one visible overshoot

// Position and velocity only; target/parameters are passed to
// anim_spring_step each call since different springs never share a target.
struct AnimSpring { double x, v; };

// One-shot value tween: eases from `from` to `to` over `dur` ms starting at
// `t0`. Used where a spring is overkill -- one destination, not redirectable
// mid-flight like a knob under a drag.
struct AnimTween { double from, to; unsigned t0, dur; };

// Cubic ease-out, clamped exactly (not approximately) at both ends so a
// caller testing `== from`/`== to` at the tween's boundaries sees it.
inline double anim_ease_out(double p) {
  if (p <= 0.0) return 0.0;
  if (p >= 1.0) return 1.0;
  const double q = 1.0 - p;
  return 1.0 - q * q * q;
}

// Fraction of a `durMs` span elapsed since `t0`, clamped to [0, 1]. Unsigned
// difference, wrap-safe like dmgnum.h's dmgnum_progress. Zero duration is
// instant (1.0) rather than a division by zero.
inline double anim_progress(unsigned t0, unsigned now, unsigned durMs) {
  if (durMs == 0) return 1.0;
  const unsigned age = now - t0;   // wrap-safe: exact for any age under 2^32 ms
  return age >= durMs ? 1.0 : (double)age / (double)durMs;
}

// Linear interpolation over eased progress: starts exactly at `from`, ends
// exactly at `to`, faster early than late.
inline double anim_tween_value(const AnimTween& t, unsigned now) {
  return t.from + (t.to - t.from) * anim_ease_out(anim_progress(t.t0, now, t.dur));
}

// Never negative (a `now` before `last` reads as no time passed, not time
// running backwards) and never over ANIM_DT_MAX_MS.
inline double anim_dt_clamped(unsigned last, unsigned now) {
  const unsigned d = now - last;
  if (d > 0x7FFFFFFFu) return 0.0;   // `now` is "before" `last`: clock went backwards, no step
  return d > (unsigned)ANIM_DT_MAX_MS ? ANIM_DT_MAX_MS : (double)d;
}

// Semi-implicit Euler is stable only on omega*dt. ANIM_SPRING_CRITICAL's omega is 20/s, so at the 50 ms dt-clamp
// omega*dt hits 1.0, where the pair oscillates forever instead of settling. 4 ms keeps it at 0.08 for the stiffest
// pair used here.
const double ANIM_SPRING_SUBSTEP_MS = 4.0;

// Semi-implicit (symplectic) Euler: velocity advances from the current position, then position from the new velocity,
// which keeps the critical pair from crossing its target at a real 16 ms dt (explicit Euler overshoots). `dtMs` is a
// span to cover, cut into ceil(dtMs/ANIM_SPRING_SUBSTEP_MS) equal substeps so the result ignores call frequency.
inline void anim_spring_step(AnimSpring* s, double target, double stiffness, double damping, double dtMs) {
  if (!s) return;
  if (!(dtMs > 0.0)) return;
  int n = (int)ceil(dtMs / ANIM_SPRING_SUBSTEP_MS);
  if (n < 1) n = 1;
  const double dt = dtMs / (double)n / 1000.0;
  for (int i = 0; i < n; i++) {
    const double a = stiffness * (target - s->x) - damping * s->v;
    s->v += a * dt;
    s->x += s->v * dt;
  }
}

// Within `eps` of target AND within 10x `eps` of rest: a spring can sit on
// target for an instant while still moving fast, and menu_closing_animation_
// active (menu.h) needs "has it come to rest", not "is it there right now".
inline bool anim_spring_settled(const AnimSpring& s, double target, double eps) {
  return fabs(s.x - target) < eps && fabs(s.v) < eps * 10.0;
}

// Clamps a hand-edited ini value to the nearest legal style rather than
// corrupting it, same discipline as esp_draw_distance_clamped etc.
inline int anim_style_clamped(int v) { return v < ANIM_OFF ? ANIM_OFF : v > ANIM_GLASS ? ANIM_GLASS : v; }

// Off writes ANIM_OFF; on writes ANIM_SIMPLE from off but keeps a stored
// ANIM_GLASS (ini-only, `menuanim=2`) so a click meaning "on" cannot lose it.
inline int anim_switch_apply(int stored, int on) {
  if (!on) return ANIM_OFF;
  return stored != ANIM_OFF ? stored : ANIM_SIMPLE;
}

// Blends one channel at a time toward `target`, the one place hover tinting is computed (menu.cpp). Takes a target
// rather than assuming C_BONE: lightening a hotkey field's well, rim and text toward one bone made the text vanish.
// `c`'s alpha is kept; `frac <= 0` returns `c`, `>= 1` target's RGB, and `!(frac > 0.0)` catches NaN as "not hovered".
inline unsigned anim_blend_colour(unsigned c, unsigned target, double frac) {
  if (!(frac > 0.0)) return c;
  if (frac >= 1.0) return (c & 0xFF000000u) | (target & 0x00FFFFFFu);
  const double br = (double)((c >> 16) & 0xFFu);
  const double bg = (double)((c >>  8) & 0xFFu);
  const double bb = (double)( c        & 0xFFu);
  const double tr = (double)((target >> 16) & 0xFFu);
  const double tg = (double)((target >>  8) & 0xFFu);
  const double tb = (double)( target        & 0xFFu);
  const unsigned r = (unsigned)(br + (tr - br) * frac);
  const unsigned g = (unsigned)(bg + (tg - bg) * frac);
  const unsigned b = (unsigned)(bb + (tb - bb) * frac);
  return (c & 0xFF000000u) | (r << 16) | (g << 8) | b;
}

// Glass style's geometry rules. Header-only since canvas.cpp's GL bodies cannot link into test_host, and the corner
// tessellation must come out the same in the rounded rect and the backdrop or the tint would not sit on its ground.

// Squash and stretch about the pill's own centre, in proportion to velocity.
struct AnimPill { double x, w; };
inline AnimPill anim_pill_stretch(double x, double w, double v,
                                  double stretchPerVelocity, double maxStretch) {
  // Sign of velocity dropped (same stretch moving either way); capped since
  // velocity is otherwise unbounded (a tab click during another switch).
  double e = fabs(v) * stretchPerVelocity;
  if (e > maxStretch) e = maxStretch;
  const double s = 1.0 + e;
  AnimPill p;
  p.w = w * s;
  p.x = x - (w * s - w) / 2.0;
  return p;
}

// Three full cycles over the span, damped linearly to nothing, exactly zero
// at both ends so the capture starts and finishes undisplaced.
inline double anim_wobble(double t01, double amp) {
  if (t01 <= 0.0 || t01 >= 1.0) return 0.0;
  const double pi = 3.14159265358979323846;
  return amp * sin(t01 * 6.0 * pi) * (1.0 - t01);
}

// Grows with radius so a big corner is not faceted; floored at 3 (degenerates
// cleanly to a plain rect at radius zero) and ceilinged at 12 against a
// pathological radius flooding immediate-mode GL with vertices.
inline int anim_round_rect_segments(int r) {
  const int s = 3 + (r > 0 ? r / 2 : 0);
  return s < 3 ? 3 : s > 12 ? 12 : s;
}
