// nukepanel.ini, after the Alien Armageddon port's config.cpp. Written key by key (WritePrivateProfileStringA), never
// whole, so a rewrite cannot drop the player's comments or keys a newer build wrote. Parsed by hand so garbage or
// out-of-range values fall back to the default instead of becoming whatever strtol makes of them.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "config.h"
#include "ini.h"
#include "ui/bind.h"   // the BIND_ROW_* indexes
#include "esp_layout.h" // ESP_DRAWDIST_MIN_M/MAX_M
#include "aim.h"        // the [aim] ranges and defaults
#include "zoom.h"       // FOV_MAX_DEG, FOV_STOCK
#include "shot.h"       // SHOT_STASH_ODD
#include "mouseaim.h"   // the sensitivity and reticle ranges
#include "ui/ui.h"      // UI_SCALE_MIN/MAX/DEFAULT
#include "photo.h"      // PHOTO_MULT_MIN/MAX/DEF
#include "speedrule.h"  // GS_PCT_MIN/MAX/DEF

Config g_cfg;
int g_cfgDirty = 0;

// What is on disk, so config_write_changed writes only what moved.
static Config s_onDisk;
static char s_path[MAX_PATH];

// One table for reader and writer, with each key's legal range and default.
struct Field { const char* section; const char* key; int Config::* member; int lo, hi, def; };
static const Field kFields[] = {
  { "panel", "key",        &Config::menukey,    1,  254,   0x2D },   // VK_INSERT
  { "panel", "x",          &Config::panelx,    -1,  65535, -1 },
  { "panel", "y",          &Config::panely,    -1,  65535, -1 },
  { "panel", "w",          &Config::panelw,     0,  65535,  0 },
  { "panel", "h",          &Config::panelh,     0,  65535,  0 },
  { "panel", "animations", &Config::animations, 0,  1,      1 },
  { "view",  "zoom",       &Config::zoomOn,     0,  1,      0 },
  { "view",  "zoomlevel",  &Config::zoomLevel,  10, 40,     20 },
  { "view",  "holdzoom",   &Config::holdZoomKey, 0, 254,    0 },
  { "framerate", "cap",       &Config::frameCapOn,  0,  1,   1 },
  { "framerate", "capfps",    &Config::frameCapFps, 0,  240, 0 },   // 0 = follow the monitor
  { "debug", "timing",     &Config::timing,     0,  1,      1 },
  { "esp",   "enemies",       &Config::espOn,            0, 1, 1 },
  { "esp",   "names",         &Config::espNames,         0, 1, 1 },
  { "esp",   "healthbars",    &Config::espHealthBars,    0, 1, 1 },
  { "esp",   "healthnumbers", &Config::espHealthNumbers, 0, 1, 1 },
  { "esp",   "distance",      &Config::espDistance,      0, 1, 1 },
  { "esp",   "drawdist",      &Config::espDrawDist,      ESP_DRAWDIST_MIN_M, ESP_DRAWDIST_MAX_M, ESP_DRAWDIST_MAX_M },
  { "esp",   "nolimit",       &Config::espNoLimit,       0, 1, 0 },
  { "view",  "zoomout",       &Config::zoomOutOn,        0, 1, 0 },
  { "view",  "zoomoutdist",   &Config::zoomOutDist,      ZOOMOUT_MIN_M, ZOOMOUT_MAX_M, ZOOMOUT_DEF_M },
  { "view",  "holdzoomout",   &Config::holdZoomOutKey,   0, 254, 0 },
  { "esp",   "items",         &Config::espItems,         0, 1, 1 },
  { "esp",   "nukes",         &Config::espNukes,         0, 1, 1 },
  { "esp",   "nukesarrow",    &Config::espNukesArrow,    0, 1, 1 },
  { "esp",   "babes",         &Config::espBabes,         0, 1, 1 },
  { "esp",   "babesarrow",    &Config::espBabesArrow,    0, 1, 1 },
  { "esp",   "secrets",       &Config::espSecrets,       0, 1, 1 },
  { "esp",   "secretsarrow",  &Config::espSecretsArrow,  0, 1, 1 },
  { "esp",   "secretvolumes", &Config::espSecretVolumes, 0, 1, 0 },
  { "esp",   "boundingboxes", &Config::espBoundingBoxes, 0, 1, 1 },
  { "freecam", "key",     &Config::freecamKey,    0, 254, 0x56 },   // V: Megaton's camera toggle; T is the teleport's
  { "freecam", "marker",  &Config::freecamMarker, 0, 1, 1 },
  { "freecam", "teleport",       &Config::teleportKey,    0, 254, 0x54 },   // T
  { "freecam", "teleportmarker", &Config::teleportMarker, 0, 1,   1 },
  { "esp",     "dmgnumbers",     &Config::dmgNumbers,     0, 1,   1 },
  { "weapons", "firerate",       &Config::fireRatePct,    100, 400,  100 },
  { "weapons", "reload",         &Config::reloadPct,      100, 1100, 100 },   // 1100 = Max
  { "weapons", "accarifle",      &Config::accArifle,      0, 1,   0 },
  // "target" and "cone"/"silentmax" (Pick target, Cone, Max redirect) dropped 2026-09-29: an old
  // ini value for them is simply ignored by the loop below, which skips any key it does not match.
  { "aim",     "point",          &Config::aimPoint,       AIM_AT_BODY, AIM_AT_HEAD, AIM_AT_BODY },
  { "aim",     "range",          &Config::aimRangeM,      AIM_RANGE_MIN_M, AIM_RANGE_MAX_M, AIM_RANGE_MAX_M },
  { "aim",     "lead",           &Config::aimLead,        0, 1,   1 },
  { "aim",     "offscreen",      &Config::aimOffscreen,   0, 1,   1 },
  { "aim",     "leadamount",     &Config::aimLeadPct,     0, 100, AIM_LEAD_DEF },
  { "aim",     "marker",         &Config::aimMarker,      0, 1,   0 },
  { "aim",     "silent",         &Config::silentOn,       0, 1,   0 },
  { "aim",     "triggerbot",     &Config::tbotOn,         0, 1,   0 },
  { "aim",     "triggerwhen",    &Config::tbotWhen,       TBOT_WHEN_CROSSHAIR, TBOT_WHEN_LOCKED, TBOT_WHEN_CROSSHAIR },
  { "aim",     "triggerdelay",   &Config::tbotDelay,      TBOT_DELAY_MS_MIN, TBOT_DELAY_MS_MAX, TBOT_DELAY_MS_DEF },
  { "display", "borderless",     &Config::borderless,     0, 1,   1 },   // default on
  { "display", "winkeys",        &Config::winKeys,        0, 1,   1 },   // default on
  { "view",    "fov",            &Config::fovOn,          0, 1,   0 },
  { "view",    "fovdeg",         &Config::fovDeg,         0, FOV_MAX_DEG, FOV_STOCK },   // 0 = stock
  { "view",    "wheelzoom",      &Config::wheelZoom,      0, 1,   0 },
  { "view",    "wheelsmooth",    &Config::wheelSmooth,    0, 1,   1 },
  { "freecam", "debug",          &Config::freecamDebug,   0, 1,   0 },
  { "freecam", "shot",           &Config::freecamShot,    0, 1,   0 },
  { "freecam", "copy",           &Config::camCopyKey,     0, 254, 0x42 },   // B
  { "freecam", "paste",          &Config::camPasteKey,    0, 254, 0x4E },   // N
  { "freecam", "hudstash",       &Config::hudStash,      -1, SHOT_STASH_ODD, -1 },
  { "startup", "skipintro",      &Config::skipIntro,      0, 1,   0 },   // default off, as AA
  { "view",    "cutaway",        &Config::cutaway,        0, 1,   0 },
  { "mouseaim", "on",            &Config::mouseAim,       0, 1,   0 },
  { "mouseaim", "reticle",       &Config::mouseReticle,   0, 1,   1 },
  { "mouseaim", "sensitivity",   &Config::mouseSens,      MA_SENS_MIN, MA_SENS_MAX, MA_SENS_DEF },
  { "mouseaim", "colour",        &Config::retColour,      0, MA_RET_COLOURS - 1, MA_RET_COLOUR_DEF },
  { "mouseaim", "size",          &Config::retSize,        MA_RET_SIZE_MIN, MA_RET_SIZE_MAX, MA_RET_SIZE_DEF },
  { "mouseaim", "thickness",     &Config::retThick,       MA_RET_THICK_MIN, MA_RET_THICK_MAX, MA_RET_THICK_DEF },
  { "mouseaim", "dot",           &Config::retDot,         0, 1,   1 },
  { "mouseaim", "outline",       &Config::retOutline,     0, 1,   1 },
  { "freecam", "speedpct",       &Config::freecamSpeedPct, FREECAM_SPEED_MIN_PCT, FREECAM_SPEED_MAX_PCT, FREECAM_SPEED_DEF_PCT },
  { "freecam", "on",             &Config::freecamOn,      0, 1,   1 },
  { "esp",     "explodables",    &Config::espExplodables, 0, 1,   1 },
  { "esp",     "hidecanttake",   &Config::espHideTaken,   0, 1,   0 },
  { "framerate", "doublejump",   &Config::jumpFix,        0, 1,   1 },   // default on
  { "framerate", "queenswipe",   &Config::queenFix,       0, 1,   1 },   // default on
  { "mouseaim", "firstperson",   &Config::firstPerson,    0, 1,   0 },
  { "mouseaim", "moveanywhere",  &Config::moveAnywhere,   0, 1,   0 },
  { "mouseaim", "rescuefalls",   &Config::rescueFalls,    0, 1,   1 },   // default on
  { "mouseaim", "crouchkey",     &Config::crouchKey,      0, 254, 0x43 },   // C
  { "mouseaim", "interactkey",   &Config::interactKey,    0, 254, 0x45 },   // E
  { "mouseaim", "jumpboostpct",  &Config::fpJumpPct,      100, 150, 120 },   // FPJ_PCT_*
  { "panel",   "uiscale",        &Config::uiScale,        UI_SCALE_MIN, UI_SCALE_MAX, UI_SCALE_DEFAULT },   // AA's key
  { "freecam", "pause",          &Config::freecamPause,   0, 1,   0 },   // default off
  { "freecam", "photo",          &Config::photoKey,       0, 254, 0x7B },   // F12: no game bind, free while flying
  { "freecam", "photosize",      &Config::photoMult,      PHOTO_MULT_MIN, PHOTO_MULT_MAX, PHOTO_MULT_DEF },
  { "esp",     "levelstats",     &Config::levelStats,     0, 1,   0 },   // default off
  { "esp",     "puzzles",        &Config::espPuzzles,     0, 1,   1 },   // default on
  { "progress", "folds",         &Config::progFolds,      0, 0x7FFFFFFF, 0 },   // PG_FOLD_* bits, all open
  { "saves",   "fast",           &Config::fastSaves,      0, 1,   1 },   // default on
  { "game",    "speed",          &Config::gameSpeedPct,   GS_PCT_MIN, GS_PCT_MAX, GS_PCT_DEF },
  { "game",    "speedkey",       &Config::gameSpeedKey,   0, 254, 0 },   // unbound
  { "esp",     "ambushes",       &Config::espAmbushes,    0, 1,   1 },   // default on with Enemies
};
// Removed keys (old ini values are skipped by the unknown-key rule; behaviour keeps the former default): the Level,
// Keycards, whole-map, movement-key, Face the cursor and Torso bend rows; "speed" became "speedpct"; [input]
// mouse_nonexclusive (always on); [display] borderless_variant; [esp] secretpath/nukepath (re/paths.md section 7).
static const int kFieldCount = (int)(sizeof(kFields) / sizeof(kFields[0]));

// The string keys: UTF-8 bytes as written, default "". A value too long for its buffer is refused
// and logged, never cut: a cut path is another folder.
struct StrField { const char* section; const char* key; char (Config::*member)[CONFIG_STR_CAP]; };
static const StrField kStrFields[] = {
  { "freecam", "photofolder",    &Config::photoFolder },   // "" = <Pictures>\Nuke Panel (photofolder.h)
};
static const int kStrFieldCount = (int)(sizeof(kStrFields) / sizeof(kStrFields[0]));

void config_defaults(void) {
  for (int i = 0; i < kFieldCount; i++) g_cfg.*(kFields[i].member) = kFields[i].def;
  for (int i = 0; i < kStrFieldCount; i++) (g_cfg.*(kStrFields[i].member))[0] = 0;
}

bool config_key_known(const char* section, const char* key) {
  if (!section || !key) return false;
  for (int i = 0; i < kFieldCount; i++)
    if (_stricmp(section, kFields[i].section) == 0 && _stricmp(key, kFields[i].key) == 0) return true;
  for (int i = 0; i < kStrFieldCount; i++)
    if (_stricmp(section, kStrFields[i].section) == 0 && _stricmp(key, kStrFields[i].key) == 0) return true;
  return false;
}

static bool exe_ini_path(char* out) {
  const DWORD n = GetModuleFileNameA(NULL, out, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return false;
  char* slash = strrchr(out, 0x5C);   // backslash
  if (!slash || (size_t)(slash + 1 - out) + 14 > MAX_PATH) return false;
  strcpy_s(slash + 1, MAX_PATH - (slash + 1 - out), "nukepanel.ini");
  return true;
}

bool config_load_from(const char* path) {
  config_defaults();
  g_cfgDirty = 0;
  strcpy_s(s_path, sizeof s_path, path ? path : "");
  s_onDisk = g_cfg;
  FILE* f = NULL;
  if (!s_path[0] || fopen_s(&f, s_path, "r") != 0 || !f) {
    char shown[MAX_PATH];
    np_log("config: no %s, defaults", s_path[0] ? np_log_path(s_path, shown, sizeof shown) : "path");
    return false;   // no file yet is the normal first run
  }
  char line[INI_LINE_MAX], section[INI_SECTION_MAX] = "";
  int read = 0, refused = 0, lineNo = 0;
  for (;;) {
    line[INI_LINE_MAX - 1] = 1;   // stays 1 unless fgets filled the buffer (a NUL in the text cannot fool it)
    if (!fgets(line, sizeof line, f)) break;
    lineNo++;
    // A full chunk without '\n' is the whole line only when the file or the line ends right there; a longer line is
    // skipped to its end, never cut into a second line whose tail could read as another key (ini_peek_int agrees).
    if (line[INI_LINE_MAX - 1] == 0 && line[INI_LINE_MAX - 2] != '\n') {
      int c = fgetc(f);
      if (c != EOF && c != '\n') {
        while (c != EOF && c != '\n') c = fgetc(f);
        refused++;
        np_log("config: line %d is over %d bytes; skipped", lineNo, INI_LINE_MAX - 1);
        continue;
      }
    }
    const char *key = NULL, *val = NULL;
    if (!ini_line(line, section, sizeof section, &key, &val)) continue;
    // An unknown key is left alone: it may belong to a newer build.
    for (int i = 0; i < kFieldCount; i++) {
      if (_stricmp(section, kFields[i].section) != 0 || _stricmp(key, kFields[i].key) != 0) continue;
      int v;
      if (ini_parse_int(val, kFields[i].lo, kFields[i].hi, &v)) { g_cfg.*(kFields[i].member) = v; read++; }
      else { refused++; np_log("config: [%s] %s=\"%s\" is not %d..%d; default %d", section, key, val,
                                kFields[i].lo, kFields[i].hi, kFields[i].def); }
      break;
    }
    for (int i = 0; i < kStrFieldCount; i++) {
      if (_stricmp(section, kStrFields[i].section) != 0 || _stricmp(key, kStrFields[i].key) != 0) continue;
      if (ini_parse_str(val, g_cfg.*(kStrFields[i].member), CONFIG_STR_CAP)) read++;
      else { refused++; np_log("config: [%s] %s is %u bytes, over %d; default \"\"", section, key,
                                (unsigned)strlen(val), CONFIG_STR_CAP - 1); }
      break;
    }
  }
  fclose(f);
  s_onDisk = g_cfg;
  char shown[MAX_PATH];
  np_log("config: read %d key(s), refused %d, from %s", read, refused, np_log_path(s_path, shown, sizeof shown));
  return true;
}

bool config_load(void) {
  char path[MAX_PATH];
  if (!exe_ini_path(path)) { config_load_from(NULL); return false; }
  return config_load_from(path);
}

int config_peek_borderless(void) {
  const Field* fd = NULL;
  for (int i = 0; i < kFieldCount; i++)
    if (kFields[i].member == &Config::borderless) fd = &kFields[i];
  char path[MAX_PATH];
  if (!fd || !exe_ini_path(path)) return -1;
  const HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    const DWORD e = GetLastError();
    // No file is the first run: the default, as config_load gives it.
    if (e == ERROR_FILE_NOT_FOUND) {
      np_log("config: peek: no nukepanel.ini; [display] borderless=%d", fd->def);
      return fd->def;
    }
    np_log("config: peek: nukepanel.ini not opened (%lu)", e);
    return -1;
  }
  static char buf[65536];
  DWORD got = 0;
  const BOOL ok = ReadFile(h, buf, sizeof buf, &got, NULL);
  const DWORD size = GetFileSize(h, NULL);
  CloseHandle(h);
  if (!ok || size == INVALID_FILE_SIZE || size > got) {
    np_log("config: peek: nukepanel.ini not read whole (%lu of %lu bytes)", got, size);
    return -1;
  }
  const int v = ini_peek_int(buf, (int)got, fd->section, fd->key, fd->lo, fd->hi, fd->def);
  np_log("config: peek: [display] borderless=%d", v);
  return v;
}

void config_write_changed(void) {
  if (!g_cfgDirty) return;
  g_cfgDirty = 0;
  if (!s_path[0]) return;
  int wrote = 0;
  for (int i = 0; i < kFieldCount; i++) {
    const int now = g_cfg.*(kFields[i].member);
    if (now == s_onDisk.*(kFields[i].member)) continue;
    char buf[16];
    _snprintf_s(buf, sizeof buf, _TRUNCATE, "%d", now);
    if (WritePrivateProfileStringA(kFields[i].section, kFields[i].key, buf, s_path)) {
      s_onDisk.*(kFields[i].member) = now;
      wrote++;
    } else {
      // Stays dirty in memory, so the panel still behaves as the player set it.
      np_log("config: could not write [%s] %s (%lu)", kFields[i].section, kFields[i].key, GetLastError());
      g_cfgDirty = 1;
    }
  }
  // The bytes go through the A call unconverted on a single-byte code page, so UTF-8 lands as the loader reads it
  // (tb_0501 on this machine); a double-byte code page is untested (docs/re/photo.md, Open questions).
  for (int i = 0; i < kStrFieldCount; i++) {
    const char* now = g_cfg.*(kStrFields[i].member);
    char* disk = s_onDisk.*(kStrFields[i].member);
    if (strcmp(now, disk) == 0) continue;
    if (WritePrivateProfileStringA(kStrFields[i].section, kStrFields[i].key, now, s_path)) {
      strcpy_s(disk, CONFIG_STR_CAP, now);
      wrote++;
    } else {
      np_log("config: could not write [%s] %s (%lu)", kStrFields[i].section, kStrFields[i].key, GetLastError());
      g_cfgDirty = 1;
    }
  }
  if (wrote) np_log("config: wrote %d changed key(s)", wrote);
}

// Each bind.h row's field, or null for a row with no feature (it reads 0, unbound): the
// movement rows are fixed keys now (freecam.h). Indexed by row, so bind.h's appended-rows rule holds.
static int Config::* const kBindFields[BIND_ROW_COUNT] = {
  &Config::menukey, nullptr /* aim */, &Config::freecamKey, &Config::teleportKey,
  &Config::camCopyKey, &Config::camPasteKey, &Config::holdZoomKey, &Config::holdZoomOutKey,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  &Config::crouchKey, &Config::interactKey, &Config::photoKey, &Config::gameSpeedKey,
};

int config_bind_get(int row) {
  if (row < 0 || row >= BIND_ROW_COUNT || !kBindFields[row]) return 0;
  return g_cfg.*(kBindFields[row]);
}

void config_bind_set(int row, int vk) {
  if (row < 0 || row >= BIND_ROW_COUNT || !kBindFields[row]) return;
  g_cfg.*(kBindFields[row]) = vk;
  g_cfgDirty = 1;
}
