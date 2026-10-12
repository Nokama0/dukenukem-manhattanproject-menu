#pragma once
// Blue laser traps that spawn a nuke carrier (re/pickups.md section 7): a `laser` whose `obstructed` pin fires a
// spawner_entity, which hands each monster it spawns its `monst_spawnscr` list (spawner_fire duke!0x100b5f70).
// Pure, so the suite pins it; areas.h parses world.scr, entities.cpp and areas.cpp read the engine.
#include <string.h>
#include <stdio.h>

// laser (alloc 0xBC0): +0xB90 the `active` pin's value (pin +0xB38, value at pin +0x58; think 0x100aee10 tests it and
// vt+0xE0/E4 store it), +0xBB0 the beam's world end (written each think), +0xF4 the world matrix. spawner_entity
// (alloc 0xAEC): +0xAE4 counts spawned entities (ctor 0x100b5770 zeroes it; spawner_fire names "%s-spwn%03d" from it).
static const char LASER_SPAWN_TAG[] = "-spwn";
const float LASER_BOX_MIN = 16.f, LASER_BEAM_MAX = 4096.f;

// "spawner_entity3-spwn000" is an entity spawner_entity3 spawned.
inline bool laser_spawned_by(const char* name, const char* spawner) {
  if (!name || !spawner || !spawner[0]) return false;
  const size_t n = strlen(spawner), t = sizeof LASER_SPAWN_TAG - 1;
  return strncmp(name, spawner, n) == 0 && strncmp(name + n, LASER_SPAWN_TAG, t) == 0;
}

// The fires so far: spawned entities over the entities one fire spawns (its `spawnscript` lines; 0 reads as 1).
inline int laser_fires(int spawned, int perFire) {
  if (spawned <= 0) return 0;
  if (perFire <= 1) return spawned;
  return (spawned + perFire - 1) / perFire;
}

// The marker's label.
inline const char* laser_marker_label(int n, char* buf, int cap) {
  if (n <= 1) _snprintf_s(buf, (size_t)cap, _TRUNCATE, "Nuke (laser)");
  else _snprintf_s(buf, (size_t)cap, _TRUNCATE, "%d Nukes (laser)", n);
  return buf;
}

// The box the marker frames: the beam from the emitter s to its end e, at least LASER_BOX_MIN half-wide on each axis.
// An end not read, or farther than any beam, gives the emitter alone.
inline void laser_beam_box(const float s[3], const float e[3], int endOk, float c[3], float h[3]) {
  float d2 = 0.f;
  for (int k = 0; k < 3; ++k) d2 += (e[k] - s[k]) * (e[k] - s[k]);
  const bool beam = endOk && d2 <= LASER_BEAM_MAX * LASER_BEAM_MAX;
  for (int k = 0; k < 3; ++k) {
    c[k] = beam ? 0.5f * (s[k] + e[k]) : s[k];
    const float half = beam ? 0.5f * (e[k] > s[k] ? e[k] - s[k] : s[k] - e[k]) : 0.f;
    h[k] = half > LASER_BOX_MIN ? half : LASER_BOX_MIN;
  }
}
