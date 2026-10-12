<div align="center">

# Duke Nukem: Manhattan Project Mod Menu

**Outlines, silent aim, a free camera and fixes for the game's rough edges, one key away.**

A mod menu for Duke Nukem: Manhattan Project (2002).

![Game](https://img.shields.io/badge/Game-Manhattan%20Project-FFB000?labelColor=0E0F13&style=flat)
![Platform](https://img.shields.io/badge/Platform-Windows-FFB000?labelColor=0E0F13&style=flat)
![Release](https://img.shields.io/github/v/release/Nokama0/dukenukem-manhattanproject-menu?label=Release&color=FFB000&labelColor=0E0F13&style=flat)
![Build](https://img.shields.io/github/actions/workflow/status/Nokama0/dukenukem-manhattanproject-menu/build.yml?branch=main&label=Build&labelColor=0E0F13&style=flat)
![License](https://img.shields.io/badge/License-Unlicense-FFB000?labelColor=0E0F13&style=flat)

</div>

---

A mod menu for **Duke Nukem: Manhattan Project** (2002). Press **Insert** in game to open it.

It loads as a `winmm.dll` in the game folder, with a few small asset files under `duke\base\nukepanel`. It does not
change any game file.

Made and tested on the ZOOM Platform Enhanced Edition. The engine binaries are the same in the Classic edition and
the Steam release, so it should work there too, but that is untested. The Enhanced Edition is tested on the `gl`
renderer only.

## What it does

The menu has six tabs.

**Visuals**
- Enemy outlines with bounding boxes, health bars, names and distance; spawner ambushes
- Markers for items and pickups, nukes, babes, secret areas, explodables and puzzle solutions, with off-screen arrows
- Damage numbers; level stats for kills, secrets and nukes

**Combat**
- Triggerbot: crosshair or locked target, with a delay
- Silent aim: aim point, range, off-screen targets, lead for moving targets
- An aim calibration marker

**Weapons**
- Perfect accuracy for the Assault Rifle
- Fire rate and reload speed sliders

**View**
- Custom field of view
- A free camera: flight speed, pause the game, teleport, copy and paste a view, screenshot mode, with photos larger
  than the screen
- Camera zoom-out, hold and scroll-wheel zoom; cut-away walls
- Mouse aim with a configurable reticle; first person; move anywhere; rescue falls

**Progress**
- Secrets, nukes and kills for the current part, episode records per difficulty, and save awards

**Settings**
- Panel key, frame cap with double-jump and Queen swipe fixes, game speed
- Fast saves, skip intro, borderless window, Windows key and hotkeys
- Panel scale and animations, debug log

Cheat-style features (combat, weapons, view, game speed) start off; display and quality-of-life options start on.
Hover a row for a short explanation.

## Get it

You need Duke Nukem: Manhattan Project, ZOOM Platform Enhanced Edition. Windows, 32-bit game.

1. **Download** `dukenukem-manhattanproject-menu-<version>.zip` from the
   [latest release](https://github.com/Nokama0/dukenukem-manhattanproject-menu/releases/latest).
2. **Extract** it into the game folder, the one that holds `prism3d.exe`. It adds `winmm.dll` and
   `duke\base\nukepanel\`.
3. **Start** the game and press **Insert**.

To remove it, delete `winmm.dll` and the `duke\base\nukepanel` folder. If you want the menu's own files gone too,
delete `nukepanel.ini`, `nukepanel.log` and `nukepanel.log.prev` from the game folder.

## Good to know

- **Where files go.** Settings are saved in `nukepanel.ini` and the log in `nukepanel.log`, both next to
  `prism3d.exe`. Photos go to `<Pictures>\Nuke Panel` by default, or to the folder you choose under View > Free
  camera > Photo folder. The photo folder must be a path that starts with a drive letter: network
  (`\\server\...`) paths are not used.
- **Frame cap above 60.** The Enhanced Edition's `dxwrapper.ini` (next to `prism3d.exe`) limits the game to 60 FPS
  with `LimitPerFrameFPS = 60`. Set it to `0` for the menu's frame cap in Settings to go higher.
- **Fast saves.** Settings > Fast saves gathers the game's many tiny save writes into a few large ones, which fixes
  the short freeze when the game saves. Turn it off to let the game write saves as it does by itself.
- **Intro.** Skip intro takes effect the next time the game starts.

---

## Build it

You need Windows with Visual Studio 2022 (the MSVC x86 32-bit toolset) and a Windows 10 SDK. The scripts find them
with `vswhere`.

```powershell
powershell -File src/nukepanel/build.ps1
powershell -File src/nukepanel/deploy.ps1 -GameDir "<Enhanced Edition folder>"
```

### Tests

`build.ps1` runs a unit-test host of the pure rules first, then compiles `build\winmm.dll`. A red test fails the
build; a good run prints a line ending in `N passed, 0 failed`.

### Deploy

`deploy.ps1` copies the DLL and the files under `assets/nukepanel` into the game. `-GameDir` can be left out if the
`NUKEPANEL_GAME_DIR` environment variable holds the same path. Deploy refuses unless that folder contains
`prism3d.exe` and `duke\base`, and refuses while the game is running. It never overwrites a file it did not write
itself: the hashes of what it wrote are kept in `duke\base\nukepanel\deployed.txt`, and any other `winmm.dll` (another
mod's, for example) stops the deploy before anything is copied. Move such a file away yourself if it should be
replaced. The `aspect43` and `skipintro` folders are mirrored from the repo, so files that are no longer in
`assets/` are deleted and listed. Deploy prints `MATCH` once every copied file has been hash-checked.

The wrapper's own frame limiter fights the menu's frame cap. When `dxwrapper.ini` has a `LimitPerFrameFPS` other
than 0, deploy prints a hint. Add `-SetDxwrapperFps0` to have deploy set it to 0; a byte-exact
`dxwrapper.ini.nukepanel-bak` backup is kept.

Design notes and reverse-engineering findings are in [`docs/`](docs/README.md).

## Other menus

The same menu, ported to other Duke Nukem games.

| Game | Repository |
|---|---|
| Duke Nukem Forever | [dukenukem-forever-menu](https://github.com/Nokama0/dukenukem-forever-menu) |
| Alien Armageddon on EDuke32 | [eduke32-aa-menu](https://github.com/Nokama0/eduke32-aa-menu) |
| Duke Nukem 3D on EDuke32 with the High Resolution Pack | [eduke32-hrp-menu](https://github.com/Nokama0/eduke32-hrp-menu) |
| Duke Nukem 3D: 20th Anniversary World Tour | [duke3d-worldtour-menu](https://github.com/Nokama0/duke3d-worldtour-menu) |
| Duke Nukem 3D: Megaton Edition | [duke3d-megaton-menu](https://github.com/Nokama0/duke3d-megaton-menu) |

## License

[The Unlicense](LICENSE): public domain, no warranty.
