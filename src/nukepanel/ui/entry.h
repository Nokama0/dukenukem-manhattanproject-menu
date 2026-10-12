// AA's ui/entry.h (verbatim from Megaton's), ported 2026-10-01 with two changes and no others: no EK_METRES
// (every slider here already stores what it shows, quick-wins.md section 5), and the key vocabulary is
// Windows virtual keys (our WndProc queue) instead of SDL scancodes.
#pragma once
// clamp_int/clamp_ll_to_int.
#include "common.h"
// _snprintf_s with _TRUNCATE, like esp.cpp's own metre labels: never overruns
// a caller's buffer, fills it as far as it goes.
#include <stdio.h>

// The numeric entry field: right-click a slider row and type the value. Every rule (keystrokes, accepted characters,
// text<->number conversion) lives here so the suite can pin it; menu.cpp draws one instance, input.cpp routes keys in.
// `open`/`commit` are int, not bool: MSVC 19.50 /O2 loses a byte-sized bool member of a struct returned by value.

// What kind of number a row holds: EK_INT (bare integer), EK_MILLI (stored in thousandths, shown as a decimal),
// EK_METRES (world units shown as metres). EK_NONE is a row with no number: it can never be typed into.
enum { EK_NONE = 0, EK_INT = 1, EK_MILLI = 2 };

// "-2147483648", the longest text that can still name an int, sign included.
const int ENTRY_TEXT_CAP = 11;

// Largest whole part any parse carries before the kind's own multiply (x1000 EK_MILLI, x512 EK_METRES), so it cannot
// overflow. Every range here is far smaller, so a part at the cap clamps to the row's maximum. Digits past the cap are
// consumed and discarded, not rejected: a held key gets the maximum, not a refusal.
const long long ENTRY_WHOLE_CAP = 2000000LL;

// What happened to the field. OPEN carries the row (key, kind, bounds, starting text); every other event carries only
// itself. CLICKAWAY is a COMMIT, not a cancel: clicking elsewhere after typing says the number; Escape is the only
// cancel. The ten events after the first six move the caret, select a range and replace/delete/copy/cut/paste it.
enum {
  ENTRY_EV_OPEN = 0,
  ENTRY_EV_CHAR,
  ENTRY_EV_BACKSPACE,
  ENTRY_EV_ENTER,
  ENTRY_EV_ESCAPE,
  ENTRY_EV_CLICKAWAY,
  ENTRY_EV_DELETE,        // the selection, else the character AFTER the caret
  ENTRY_EV_LEFT,          // `ch` is the shift flag: 1 extends, 0 moves
  ENTRY_EV_RIGHT,         // ditto
  ENTRY_EV_HOME,          // ditto
  ENTRY_EV_END,           // ditto
  ENTRY_EV_SELECT_ALL,    // anchor 0, caret len
  ENTRY_EV_SET_CARET,     // `ch` is the INDEX, `rowKey` the shift flag
  ENTRY_EV_PASTE,         // `text` is the string
  ENTRY_EV_COPY,          // fills EntryResult.clip
  ENTRY_EV_CUT            // ... and then deletes the selection
};

// `prefill` decides a commit: a field opens with the row's DISPLAY, not always its value (EK_METRES: 5000 units opens
// as "9", reads back as 4608), so Enter/clickaway commit only when the text DIFFERS from it. Caret/anchor are indices
// into `text`, 0..len; equal means no selection, and which is smaller records drag direction.
struct EntryState {
  int  open;                        // 0/1 -- see the bool-fold note above
  int  rowKey;                      // which row is being edited, -1 when closed
  int  kind;                        // EK_*, the row's own kind
  int  lo, hi;                      // the row's own bounds, applied on commit
  int  len;                         // characters in text, 0..ENTRY_TEXT_CAP
  int  edited;                      // 0/1 -- has a keystroke changed the text?
  int  caret;                       // 0..len, where the next character goes
  int  anchor;                      // 0..len, the other end of the selection
  char text[ENTRY_TEXT_CAP + 1];    // always NUL-terminated
  // Text the field opened with, written once by ENTRY_EV_OPEN, read by ENTER/
  // CLICKAWAY to decide whether there's anything to commit. Kept rather than
  // recomputed from the row, since the row's value can move under an open field.
  char prefill[ENTRY_TEXT_CAP + 1]; // always NUL-terminated
};

struct EntryResult {
  EntryState state;
  int commit;                       // 0/1 -- a value was accepted this step
  int value;                        // meaningful only when commit is 1
  // What the step put on the clipboard (COPY/CUT only). The machine decides
  // WHAT is copied; the Windows clipboard itself is the DLL's business
  // (clipboard.h) and can't be reached from the suite.
  int  hasClip;                     // 0/1
  char clip[ENTRY_TEXT_CAP + 1];    // always NUL-terminated, "" when hasClip is 0
};

inline EntryState entry_begin() {
  EntryState s;
  s.open = 0;
  s.rowKey = -1;
  s.kind = EK_NONE;
  s.lo = 0;
  s.hi = 0;
  s.len = 0;
  s.edited = 0;
  s.caret = 0;
  s.anchor = 0;
  // Whole buffer, not just the terminator: copied by value every step, and an
  // uninitialised tail would make equal states compare unequal byte for byte.
  for (int i = 0; i <= ENTRY_TEXT_CAP; i++) { s.text[i] = 0; s.prefill[i] = 0; }
  return s;
}

// Whether the field still shows what it opened with -- the whole commit rule.
inline bool entry_text_is_prefill(const EntryState& s) {
  for (int i = 0; i <= ENTRY_TEXT_CAP; i++) {
    if (s.text[i] != s.prefill[i]) return false;
    if (s.text[i] == 0) return true;
  }
  return true;
}

// Selection's two ends in order (caret/anchor are kept as the player made
// them; every rule below wants them sorted).
inline int entry_sel_lo(const EntryState& s) { return s.caret < s.anchor ? s.caret : s.anchor; }
inline int entry_sel_hi(const EntryState& s) { return s.caret > s.anchor ? s.caret : s.anchor; }

// Whether a '.' survives outside [a, b): typing a point over a selection that
// contains the field's only point is legal, since the result still holds exactly one.
inline bool entry_has_dot_outside(const char* text, int len, int a, int b) {
  for (int i = 0; i < len; i++) {
    if (i >= a && i < b) continue;
    if (text[i] == '.') return true;
  }
  return false;
}

// Cuts [a, b) out of the text, leaves caret/anchor at a. Returns 1 if anything was removed.
inline int entry_delete_range(EntryState* s, int a, int b) {
  if (!s || a < 0 || b > s->len || a >= b) return 0;
  const int n = b - a;
  for (int i = b; i <= s->len; i++) s->text[i - n] = s->text[i];   // <= len: terminator moves with the tail
  s->len -= n;
  s->caret = a;
  s->anchor = a;
  return 1;
}

// One character in, at the caret, over the selection if there is one (the CHAR rule; ENTRY_EV_PASTE applies it in
// order). Returns 1 when the text changed. A refused character changes nothing, selection included. Every rule is asked
// of the text as it WILL BE (the '-' must end at index 0, the '.' be the only one left, the cap count survivors).
inline int entry_type_char(EntryState* s, int ch) {
  if (!s) return 0;
  const int a = entry_sel_lo(*s), b = entry_sel_hi(*s);
  const int rem = s->len - (b - a);
  bool accept = false;
  if (ch >= '0' && ch <= '9') accept = true;
  // '-' at index 0 and nowhere else, only on a row whose range goes below zero: "-5-"/"--5" are both refused by
  // entry_parse, so typing either would offer a value that is later thrown away. The caret can sit at 0 of a
  // field already holding "5", where "-5" is exactly what is meant.
  else if (ch == '-') accept = (a == 0 && s->lo < 0 && (b >= s->len || s->text[b] != '-'));
  else if (ch == '.') accept = (s->kind == EK_MILLI && !entry_has_dot_outside(s->text, s->len, a, b));
  if (!accept) return 0;
  if (rem >= ENTRY_TEXT_CAP) return 0;
  entry_delete_range(s, a, b);
  for (int i = s->len; i >= a; i--) s->text[i + 1] = s->text[i];
  s->text[a] = (char)ch;
  s->len++;
  s->caret = a + 1;
  s->anchor = s->caret;
  return 1;
}

// Text -> value, in the row's units, clamped to the row's bounds. By hand, no atoi/strtol (atoi is undefined when the
// value does not fit; a player can hold a key down): long long accumulation stopped at ENTRY_WHOLE_CAP. *ok tells
// "field was empty" from "clamped to zero": "", "-" and "." are refusals and keep the row's value.
inline int entry_parse(const char* s, int kind, int lo, int hi, bool* ok) {
  if (ok) *ok = false;
  if (!s || kind == EK_NONE) return 0;

  int i = 0;
  bool neg = false;
  if (s[0] == '-') { neg = true; i = 1; }

  long long whole = 0;
  int digits = 0;
  for (; s[i] >= '0' && s[i] <= '9'; i++) {
    digits++;
    if (whole < ENTRY_WHOLE_CAP) {
      whole = whole * 10 + (long long)(s[i] - '0');
      if (whole > ENTRY_WHOLE_CAP) whole = ENTRY_WHOLE_CAP;
    }
  }

  // Fraction, EK_MILLI only: three digits kept, the rest dropped (not
  // rounded -- a fourth decimal is a digit the row can't hold, and rounding
  // up would move the value while it's still being typed). Short fractions pad.
  long long frac = 0;
  if (kind == EK_MILLI && s[i] == '.') {
    i++;
    int fd = 0;
    for (; s[i] >= '0' && s[i] <= '9'; i++) {
      digits++;
      if (fd < 3) { frac = frac * 10 + (long long)(s[i] - '0'); fd++; }
    }
    while (fd < 3) { frac *= 10; fd++; }
  }

  // Anything left over is a character this kind of field could never have
  // produced: refused outright rather than parsed up to the bad character.
  if (s[i] != 0) return 0;
  if (digits == 0) return 0;        // "", "-", "." and "-." are all refusals

  long long v;
  if (kind == EK_MILLI) v = whole * 1000LL + frac;
  else                  v = whole;
  if (neg) v = -v;

  if (ok) *ok = true;
  return clamp_int(clamp_ll_to_int(v), lo, hi);
}

// Value -> text: what the field opens showing, and what the row shows beside its track (the two must agree). Not
// always entry_parse's inverse: EK_METRES rounds DOWN to the metre (5000 units -> "9" -> 4608); entry_step's prefill
// comparison keeps that from moving a row the player only looked at. EK_INT and EK_MILLI round-trip exactly.
inline void entry_format(char* out, size_t cap, int value, int kind) {
  if (!out || cap == 0) return;
  out[0] = 0;
  if (kind == EK_MILLI) {
    // Sign written separately, magnitude widened first: "%d.%03d" on a
    // negative would print the sign away (-40 thousandths -> "0.040").
    const long long v = (long long)value;
    const long long a = v < 0 ? -v : v;
    _snprintf_s(out, cap, _TRUNCATE, "%s%lld.%03lld", v < 0 ? "-" : "", a / 1000, a % 1000);
  } else if (kind == EK_INT) {
    _snprintf_s(out, cap, _TRUNCATE, "%d", value);
  }
  // EK_NONE writes the empty string: a row with no number has no text.
}

// The field's keys are Windows virtual keys, as the WndProc queues them (input.cpp): Enter (either),
// Escape, Backspace, Delete, the arrows, Home/End, and Ctrl with A, C, X, V.
const int ENTRY_VK_A = 0x41, ENTRY_VK_C = 0x43, ENTRY_VK_V = 0x56, ENTRY_VK_X = 0x58;

// How much of the clipboard a paste reads (input.cpp's stack buffer). The field holds at most ENTRY_TEXT_CAP
// characters, so a longer source only adds digits if the rest is characters the field refuses anyway; 64 delivers them
// and is short enough for a hook running inside the game's own event drain.
const int ENTRY_PASTE_CAP = 64;

// Caret index nearest a pixel, for the mouse. `textX` is the text's left edge, `px` the cursor, `advance` one glyph's
// width (FONT_MONO is monospaced, so no per-prefix measurement). Rounded to the nearest boundary; clamped to
// 0..textLen.
inline int entry_index_at(int textLen, int textX, int px, int advance) {
  if (textLen < 0) textLen = 0;
  if (advance <= 0) return 0;   // would divide by zero; unreachable with a real atlas loaded
  const int d = px - textX;
  if (d <= 0) return 0;
  const int i = (d + advance / 2) / advance;
  return i > textLen ? textLen : i;
}

// Virtual key -> the character the field types from it, 0 for any key the field does not take. The digit
// row and the keypad digits, minus and period from each. Shift is not consulted: Shift+1 is still '1'
// here, as AA's scancode table had it.
inline char entry_char_of_vk(int vk) {
  if (vk >= 0x30 && vk <= 0x39) return (char)('0' + (vk - 0x30));   // '0'..'9'
  if (vk >= 0x60 && vk <= 0x69) return (char)('0' + (vk - 0x60));   // VK_NUMPAD0..9
  if (vk == 0xBD || vk == 0x6D) return '-';                           // VK_OEM_MINUS, VK_SUBTRACT
  if (vk == 0xBE || vk == 0x6E) return '.';                           // VK_OEM_PERIOD, VK_DECIMAL
  return 0;
}

// A queued key-down as the event it is to an open field, or -1 for a key the field does not take. `ch`
// receives the character (CHAR) or the shift flag (the four movement events), as entry_step reads them.
// ENTRY_EV_PASTE is only named here: the caller reads the clipboard and steps with its text.
inline int entry_event_of_vk(int vk, bool shift, bool ctrl, int* ch) {
  *ch = 0;
  // Chords first, so Ctrl+C is never read as a letter the field refuses anyway.
  if (ctrl && vk == ENTRY_VK_A) return ENTRY_EV_SELECT_ALL;
  if (ctrl && vk == ENTRY_VK_C) return ENTRY_EV_COPY;
  if (ctrl && vk == ENTRY_VK_X) return ENTRY_EV_CUT;
  if (ctrl && vk == ENTRY_VK_V) return ENTRY_EV_PASTE;
  switch (vk) {
    case 0x1B: return ENTRY_EV_ESCAPE;      // VK_ESCAPE
    case 0x0D: return ENTRY_EV_ENTER;       // VK_RETURN, the keypad's Enter included
    case 0x08: return ENTRY_EV_BACKSPACE;   // VK_BACK
    case 0x2E: return ENTRY_EV_DELETE;      // VK_DELETE
    case 0x25: *ch = shift ? 1 : 0; return ENTRY_EV_LEFT;
    case 0x27: *ch = shift ? 1 : 0; return ENTRY_EV_RIGHT;
    case 0x24: *ch = shift ? 1 : 0; return ENTRY_EV_HOME;
    case 0x23: *ch = shift ? 1 : 0; return ENTRY_EV_END;
    default: break;
  }
  const char c = entry_char_of_vk(vk);
  if (!c || ctrl) return -1;
  *ch = c;
  return ENTRY_EV_CHAR;
}

// The machine: one step, one event, state in and out. Caret events reuse the eight parameters. LEFT/RIGHT/HOME/END:
// `ch` is the shift flag (1 extends the selection, 0 collapses it). SET_CARET: `ch` is the index (clamped), `rowKey`
// the shift flag (0 = both ends move, the drag's start; 1 = only the caret). PASTE: `text` is the string.
inline EntryResult entry_step(EntryState s, int ev, int ch, int rowKey,
                              int kind, int lo, int hi, const char* text) {
  EntryResult r;
  r.state = s;
  r.commit = 0;
  r.value = 0;
  r.hasClip = 0;
  for (int i = 0; i <= ENTRY_TEXT_CAP; i++) r.clip[i] = 0;   // whole buffer, same reason as entry_begin

  if (ev == ENTRY_EV_OPEN) {
    // A row with no number does not open a field, and closes one that was
    // open: the click opening a field on another row also leaves this one.
    if (kind == EK_NONE) { r.state = entry_begin(); return r; }
    r.state.open = 1;
    r.state.rowKey = rowKey;
    r.state.kind = kind;
    r.state.lo = lo;
    r.state.hi = hi;
    r.state.edited = 0;   // a fresh field is unedited, whatever was handed in
    int n = 0;
    // Index tested before the character (n == ENTRY_TEXT_CAP means the
    // source is at least that long, so text[n] is a valid read either way).
    if (text) while (n < ENTRY_TEXT_CAP && text[n]) { r.state.text[n] = text[n]; n++; }
    r.state.text[n] = 0;
    r.state.len = n;
    // Same bytes again as the prefill this field is compared against on
    // close, copied from what actually landed rather than the caller's
    // string, so a truncated source doesn't look edited from frame one.
    for (int i = 0; i <= n; i++) r.state.prefill[i] = r.state.text[i];
    // Caret at the end, nothing selected: the prefill is a DISPLAY of the
    // row's value, not a proposal, so select-all-by-default would cost the
    // first keystroke of an append for no benefit.
    r.state.caret = n;
    r.state.anchor = n;
    return r;
  }

  if (!s.open) return r;            // every other event needs a field to act on

  if (ev == ENTRY_EV_ESCAPE) { r.state = entry_begin(); return r; }

  const int selLo = entry_sel_lo(s), selHi = entry_sel_hi(s);

  if (ev == ENTRY_EV_BACKSPACE) {
    // Selection if there is one, else the character before the caret.
    const int a = selHi > selLo ? selLo : (selLo > 0 ? selLo - 1 : 0);
    if (entry_delete_range(&r.state, a, selHi > selLo ? selHi : selLo)) r.state.edited = 1;
    return r;
  }

  if (ev == ENTRY_EV_DELETE) {
    // Mirror image: selection, else the character after the caret, nothing at the end.
    const int b = selHi > selLo ? selHi : (selHi < r.state.len ? selHi + 1 : selHi);
    if (entry_delete_range(&r.state, selLo, b)) r.state.edited = 1;
    return r;
  }

  if (ev == ENTRY_EV_CHAR) {
    if (entry_type_char(&r.state, ch)) r.state.edited = 1;
    return r;
  }

  if (ev == ENTRY_EV_PASTE) {
    // Every character through the same rule the keyboard uses, in order, so a copied "FOV: 90" delivers "90"; refused
    // ones are skipped (junk usually precedes the number). Selection goes first regardless: "paste over the selection"
    // is all that was asked. Empty/absent text is not a paste.
    if (!text || !text[0]) return r;
    if (entry_delete_range(&r.state, selLo, selHi)) r.state.edited = 1;
    for (int i = 0; text[i]; i++)
      if (entry_type_char(&r.state, (unsigned char)text[i])) r.state.edited = 1;
    return r;
  }

  if (ev == ENTRY_EV_COPY || ev == ENTRY_EV_CUT) {
    // Selection, or the whole text when nothing is selected (Ctrl+C with no
    // selection meaning "copy the lot" saves the extra Ctrl+A).
    const int a = selHi > selLo ? selLo : 0;
    const int b = selHi > selLo ? selHi : r.state.len;
    int n = 0;
    for (int i = a; i < b && n < ENTRY_TEXT_CAP; i++) r.clip[n++] = r.state.text[i];
    r.clip[n] = 0;
    r.hasClip = n > 0 ? 1 : 0;
    // Cut deletes the selection only; with nothing selected it's a copy --
    // emptying a field the player never selected is a different statement.
    if (ev == ENTRY_EV_CUT && selHi > selLo
        && entry_delete_range(&r.state, selLo, selHi)) r.state.edited = 1;
    return r;
  }

  // Caret/selection movement: none of these is an edit (the text doesn't
  // change), so a field only looked at or selected still shows its prefill
  // and still commits nothing on Enter.
  if (ev == ENTRY_EV_LEFT || ev == ENTRY_EV_RIGHT) {
    const int shift = ch ? 1 : 0;
    if (!shift && selHi > selLo) {
      // A plain move with a selection collapses it to the near/far end
      // rather than also moving, matching every other text field.
      const int at = (ev == ENTRY_EV_LEFT) ? selLo : selHi;
      r.state.caret = at;
      r.state.anchor = at;
      return r;
    }
    int at = r.state.caret + (ev == ENTRY_EV_LEFT ? -1 : 1);
    if (at < 0) at = 0;
    if (at > r.state.len) at = r.state.len;
    r.state.caret = at;
    if (!shift) r.state.anchor = at;
    return r;
  }

  if (ev == ENTRY_EV_HOME || ev == ENTRY_EV_END) {
    const int at = (ev == ENTRY_EV_HOME) ? 0 : r.state.len;
    r.state.caret = at;
    if (!ch) r.state.anchor = at;
    return r;
  }

  if (ev == ENTRY_EV_SELECT_ALL) {
    r.state.anchor = 0;
    r.state.caret = r.state.len;
    return r;
  }

  if (ev == ENTRY_EV_SET_CARET) {
    // Clamped rather than refused: the index comes from a cursor the player
    // can drag off either end, and "as far as it goes" is what's meant.
    int at = ch;
    if (at < 0) at = 0;
    if (at > r.state.len) at = r.state.len;
    r.state.caret = at;
    if (!rowKey) r.state.anchor = at;
    return r;
  }

  if (ev == ENTRY_EV_ENTER || ev == ENTRY_EV_CLICKAWAY) {
    // A field still showing its prefill said nothing: closes, row keeps its
    // exact value rather than being handed the parse of its own display.
    if (entry_text_is_prefill(s)) { r.state = entry_begin(); return r; }
    bool ok = false;
    const int v = entry_parse(s.text, s.kind, s.lo, s.hi, &ok);
    // Closes either way: an emptied field that said nothing has still
    // finished, and leaving it open would trap the player's keys.
    r.state = entry_begin();
    if (ok) { r.commit = 1; r.value = v; }
    return r;
  }

  return r;
}
