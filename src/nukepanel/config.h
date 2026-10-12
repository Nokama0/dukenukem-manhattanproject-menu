#pragma once
// nukepanel.ini beside prism3d.exe. The shape of AA's config.h (plain int fields, one field table for reader and
// writer), reduced to our keys; the few string fields have a table of their own (config.cpp's kStrFields).

struct Config {
  // [panel]
  int menukey;     // key: virtual-key code of the panel key, default VK_INSERT
  // x, y, w, h: stored panel box, logical pixels. -1/-1/0/0 is "unset" and
  // panel_layout picks the default box for the screen (0,0 is a real corner).
  int panelx, panely, panelw, panelh;
  int animations;  // 0/1, the panel's springs and fades
  // [view] Kept, unread (append-only rule): the old zoom-in, replaced by Custom field of view.
  // Stale in users' ini files, harmless.
  int zoomOn;      // 0/1, the Zoom switch (View > FIELD OF VIEW)
  int zoomLevel;   // 10..40, the multiplier x10 (1.0x-4.0x in 0.1 steps), default 20 (2.0x)
  int holdZoomKey; // BIND_ROW_HOLDZOOM's binding, a VK (bind.h); 0 default (unbound)
  // [framerate]
  int frameCapOn;  // 0/1, the Frame cap switch (Settings > FRAME RATE), default 1
  // 30..240 is the player's explicit choice; 0 is the sentinel "follow the
  // monitor's refresh rate". Never write a computed default here -- config
  // diffs every field on any change and would leak it into the ini.
  int frameCapFps;
  // [debug]
  int timing;      // 0/1, Debug log: the jump and position lines in nukepanel.log (instruments.h)
  // [esp] (Visuals tab)
  int espOn;            // 0/1, Enemies: brackets on enemies and bosses
  int espNames;         // 0/1, the name above the box
  int espHealthBars;    // 0/1, the bar left of the box
  int espHealthNumbers; // 0/1, cur/max beside the name
  int espDistance;      // 0/1, metres below the box
  // Kept, unread (append-only rule): no draw-distance limit (side scroller).
  // Stale in users' ini files, harmless.
  int espDrawDist;      // metres, ESP_DRAWDIST_MIN_M..MAX_M (esp_layout.h), default the maximum
  int espNoLimit;       // 0/1, ignores espDrawDist
  // [view] zoom out (View > CAMERA): the camera pulled back along its forward row
  int zoomOutOn;        // 0/1, the Zoom out switch, default 0
  int zoomOutDist;      // metres, ZOOMOUT_MIN_M..ZOOMOUT_MAX_M, default ZOOMOUT_DEF_M
  int holdZoomOutKey;   // BIND_ROW_HOLDZOOMOUT's binding, a VK (bind.h); 0 default (unbound)
  // [esp] category switches. The off-screen arrow and secret volumes are switches only here.
  int espItems;          // 0/1, Items and pickups: item and keycard brackets, default on
  int espNukes;          // 0/1, Visuals OBJECTIVES: nuke cross marks, default on
  int espNukesArrow;     // 0/1, sub-row: nukes' off-screen arrow, default on
  int espBabes;          // 0/1, Visuals OBJECTIVES: babe cross marks, default on
  int espBabesArrow;     // 0/1, sub-row: babes' off-screen arrow, default on
  int espSecrets;        // 0/1, Visuals OBJECTIVES: secret cross marks, default on
  int espSecretsArrow;   // 0/1, sub-row: secrets' off-screen arrow, default on
  int espSecretVolumes;  // 0/1, Visuals OBJECTIVES: secret trigger volumes, default off (clutter)
  // Enemies sub-row (AA's Bounding boxes).
  int espBoundingBoxes;  // 0/1, the corner brackets; off keeps the labels and health bars. Default on.
  // [freecam] (View > FREE CAMERA). Every key is a VK. The movement keys are fixed (freecam.h).
  int freecamKey;        // BIND_ROW_CAMERA: the toggle, default V
  int freecamMarker;     // 0/1, the Show player row, default on
  int teleportKey;       // BIND_ROW_TELEPORT: moves Duke to the freecam crosshair, default T
  int teleportMarker;    // 0/1, Teleport marker: the landing column, default on
  int dmgNumbers;        // 0/1, Visuals WORLD: numbers over enemies Duke hits (dmgnum.h), default on
  // [weapons] (Weapons tab, weapons.h). Stored as the slider shows them; weapons.h rounds to the 50 stops.
  int fireRatePct;       // 100..400 %, default 100 (stock)
  int reloadPct;         // 100..1000 %, 1100 = Max (instant); default 100 (stock)
  int accArifle;         // 0/1, PERFECT ACCURACY Assault Rifle, default off
  // [aim] (Combat TARGETING, aim.h). AA's rows and defaults (its config.cpp:223-251), no Aimbot.
  // No Pick target/Cone/Max redirect (side-scroller): silent aim and
  // the triggerbot's locked mode always take the nearest visible enemy in any direction.
  int aimPoint;          // AIM_AT_BODY (0, AA's default) or AIM_AT_HEAD (1)
  int aimRangeM;         // metres at 52 units, 1..64 (AA's 512..32768 Build units), default the maximum
  int aimLead;           // 0/1, Lead moving targets, default on (AA's)
  int aimOffscreen;      // 0/1, Off-screen targets, default on: the bullet-clip bypass and no on-screen filter
  int aimLeadPct;        // 0..100 %, Lead amount, default 70
  int aimMarker;         // 0/1, Aim calibration marker, default off
  int silentOn;          // 0/1, Silent aim, default off: the detour is not even armed until it is on
  // [aim] Triggerbot. Off by default: the only feature that presses a key.
  int tbotOn;            // 0/1; the rend_begin_frame detour is not armed until it is on
  int tbotWhen;          // TBOT_WHEN_CROSSHAIR (0, AA's default) or TBOT_WHEN_LOCKED (1)
  int tbotDelay;         // 0..500 ms the condition must hold first, default 0 (AA's)
  // [display] (Settings > DISPLAY, display.h)
  int borderless;        // 0/1, Borderless window: windowed at the monitor's size with no frame, default on
  int winKeys;           // 0/1, Windows key and hotkeys: the keyboard without DISCL_NOWINKEY (hotkeys.h), default on
  // [view] Custom field of view (View > FIELD OF VIEW, zoom.h)
  int fovOn;             // 0/1, the Custom field of view switch, default off
  int fovDeg;            // FOV_MIN_DEG..FOV_MAX_DEG horizontal degrees; 0 = the captured stock (the reset), default 0
  // [view] Scroll-wheel zoom (View > CAMERA, wheelzoom.h)
  int wheelZoom;         // 0/1, the wheel moves the saved zoom-out distance, default off
  int wheelSmooth;       // 0/1, Smooth scrolling: the camera eases to the new distance, default on
  // [freecam] extras (View > FREE CAMERA, quick-wins.md section 4)
  int freecamDebug;      // 0/1, Debug readout: the pose top-left while flying, default off
  int freecamShot;       // 0/1, Screenshot mode: no HUD and no overlays of ours while flying (shot.h), default off
  int camCopyKey;        // BIND_ROW_CAMCOPY: the view line to the clipboard, default B (V is the Camera key)
  int camPasteKey;       // BIND_ROW_CAMPASTE: flies to a clipboard view line, default N
  int hudStash;          // -1; only read to clean up the first quick-wins build's g_hud stash (shot.h), never set now
  // [startup] (Settings > STARTUP)
  int skipIntro;         // 0/1, Skip intro: mounts the logo-less scr_intro.sc at the next launch (skipintro.h), default off
  // [view] Cut-away walls (View > CAMERA, cutaway.h)
  int cutaway;           // 0/1, every fader_entity held in its cut-away state, default off
  // [mouseaim] (View > MOUSE LOOK, mouseaim.h)
  int mouseAim;          // 0/1, Mouse aim: shots to the cursor's point on Duke's plane, default off
  int mouseReticle;      // 0/1, Show reticle, default on
  int mouseSens;         // MA_SENS_MIN..MAX %, Sensitivity: our cursor from the device's mickeys, default 100
  int retColour;         // 0..MA_RET_COLOURS-1, Reticle colour (ma_reticle_colour), default cyan
  int retSize;           // MA_RET_SIZE_MIN..MAX px, the arm length, default 8
  int retThick;          // MA_RET_THICK_MIN..MAX px, default 2
  int retDot;            // 0/1, Centre dot, default on
  int retOutline;        // 0/1, Outline: a black keyline around every part, default on
  // [freecam] Flight speed as a percentage of FREECAM_SPEED_BASE_M.
  int freecamSpeedPct;   // FREECAM_SPEED_MIN_PCT..MAX_PCT, default FREECAM_SPEED_DEF_PCT
  // The Free camera switch: arms the Camera, Teleport, Copy view and Paste view keys; it starts no flight.
  int freecamOn;         // 0/1, default on, so the Camera key works as it did before the switch armed it
  int espExplodables;    // 0/1, Visuals WORLD Explodables: breakable_adv walls, AA's yellow x, default on
  int espHideTaken;      // 0/1, Items and pickups > Hide what you can't take (pickrule.h), default off
  // [framerate] Fix double jump (Settings > FRAME RATE, jumpfix.h)
  int jumpFix;           // 0/1, undoes the takeoff disarm of the air boost, default on
  int queenFix;          // 0/1, one hit per Queen running swipe (hurtlog.h, queen-burst.md section 5), default on
  // [mouseaim] View > MOUSE LOOK
  int firstPerson;       // 0/1, First person: g_fps 1 plus our mouse look (firstperson.h), default off
  int moveAnywhere;      // 0/1, Move anywhere: the track no longer holds Duke, default off
  int crouchKey;         // BIND_ROW_FP_CROUCH: hold to crouch in first person, default C (Ctrl is a freecam key)
  int interactKey;       // BIND_ROW_INTERACT: use/interact in first person and Move anywhere, default E
  int fpJumpPct;         // FPJ_PCT_MIN..MAX %, Double jump strength in first person (fpmoves.h), default 120
  int rescueFalls;       // 0/1, Rescue falls (rescue.h): a bottomless fall returns Duke to safe ground, default on
  // [panel] Settings > PANEL UI scale (ui.h): the stored setting; menu.cpp caps what it applies per screen height
  int uiScale;           // UI_SCALE_MIN..MAX %, default UI_SCALE_DEFAULT (100)
  // [freecam] Pause the game (View > FREE CAMERA, freecampause.h)
  int freecamPause;      // 0/1, the world frozen while the camera flies, default off
  // [freecam] Photo (View > FREE CAMERA, photo.h)
  int photoKey;          // BIND_ROW_PHOTO: renders the high-resolution photo while flying, default F12
  int photoMult;         // PHOTO_MULT_MIN..MAX, Photo size: the screen times this on each side, default 4
  // [esp] Visuals WORLD Level stats (levelstats.h)
  int levelStats;        // 0/1, the part's kills, secrets and nukes top right, default off
  // [esp] Visuals WORLD Puzzle solutions (puzzlerule.h; re/puzzle-e05p02.md, re/puzzle-e07p03.md)
  int espPuzzles;        // 0/1, the next step of the e05p02/e07p03 puzzles near Duke, default on
  // [progress] folded sections of the Progress tab, a bit mask (progress.h's PG_FOLD_*), default 0: all open
  int progFolds;
  // [saves] Fast saves (Settings > SAVES, saveio.h; re/save-io.md section 6)
  int fastSaves;         // 0/1, the game's tiny save writes coalesced into a few large ones, default on
  // [game] Game speed (Settings > GAME SPEED, gamespeed.h). The switch itself is not saved: off at every launch.
  int gameSpeedPct;      // GS_PCT_MIN..MAX %, the Speed slider (speedrule.h snaps it to 5 % steps), default 50
  int gameSpeedKey;      // BIND_ROW_GAMESPEED: toggles the Game speed switch, default 0 (unbound)
  // [esp] Enemies > Ambushes (re/kills-and-spawners.md): spawners not fired yet and their zones
  int espAmbushes;       // 0/1, default on
  // String fields (config.cpp's kStrFields): UTF-8, default "", a longer value than CONFIG_STR_CAP - 1 is refused.
  // [freecam] Photo folder (photofolder.h): "" is the default, <Pictures>\Nuke Panel
  char photoFolder[260];
};

// MAX_PATH bytes with the terminator; config.h stays free of windows.h.
enum { CONFIG_STR_CAP = (int)sizeof(((Config*)0)->photoFolder) };

enum { ZOOMOUT_MIN_M = 1, ZOOMOUT_MAX_M = 40, ZOOMOUT_DEF_M = 8 };
// AA's percent range (its freecam.h:21-23); 100 % is the old 8 m/s default.
enum { FREECAM_SPEED_MIN_PCT = 1, FREECAM_SPEED_MAX_PCT = 500, FREECAM_SPEED_DEF_PCT = 100, FREECAM_SPEED_BASE_M = 8 };

extern Config g_cfg;

// Set by every row that changes a value; config_write_changed writes only the
// keys that moved since the last load or write.
extern int g_cfgDirty;

// bind.h addresses binds by row (config.cpp's kBindFields): the panel key, the
// zoom holds, the freecam toggle (BIND_ROW_CAMERA), the teleport and the view keys.
// Every other row (the retired movement rows included) reads 0 (unbound).
int  config_bind_get(int row);
void config_bind_set(int row, int vk);

void config_defaults(void);
// Whether the loader reads [section] key; a removed row's key is not read (old ini values are ignored).
bool config_key_known(const char* section, const char* key);
// Loads nukepanel.ini beside the host exe. False when there is no file (the
// normal first run): g_cfg then holds the defaults.
bool config_load(void);
// The same from an explicit path; the suite's entry point.
bool config_load_from(const char* path);
// [display] borderless as config_load would read it, through kernel32 file I/O only (CreateFileA, ReadFile): DllMain
// runs it under the loader lock, where the CRT's fopen is not proven safe. g_cfg is untouched. 0/1 (a missing file
// gives the default), or -1 when not readable whole (path, open or read failure, over 64 KB): the hook then decides.
int config_peek_borderless(void);
// Writes each changed key with WritePrivateProfileStringA, never the whole file,
// so comments and keys this build does not know survive. No-op unless dirty.
void config_write_changed(void);
