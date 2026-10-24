#pragma once
#include <cstdint>
// Human animation sets loaded while the main thread waits for the ground (re_menu_main.md #2, 2026-10-24).
// The level load builds the male and female skeletons with their animation sets (.chrparams: persistent DBAs of 170 MB,
// ~1.9 s on the main thread) right after the terrain step, and in the terrain step the main thread only spins in a poll
// loop for 1-2.5 s (CTerrain::Load_T, loadwatch.h). Mode 1 (deagle_animahead.txt next to the plugin, the default): at
// the first poll of every terrain wait, the game's own preload ICharacterManager::LoadAndLockResources (vtable +0x70) runs
// for female.cdf and male.cdf; the spawns later find both in the character manager's cache. 0 = off.
// Every LoadCHRPARAMS call is logged ("animahead: chrparams" lines: path, thread, ms, whether it was our preload), and
// every human skeleton load with its flags, so a test boot shows the load doesn't build those two again.
void Animahead_Install(uintptr_t gameBase, void* self);
void Animahead_OnTerrainWait();   // main thread, first poll of each terrain wait
