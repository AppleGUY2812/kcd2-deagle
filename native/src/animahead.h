#pragma once
#include <cstdint>
// Human animation sets loaded at the main menu (re_menu_main.md #1, 2026-10-24).
// The level load builds the male and female skeletons with their animation sets (.chrparams: persistent DBAs of 170 MB,
// ~1.9 s on the main thread) after the terrain step. Mode 1 (fastload_animahead.txt next to the plugin, the default): at
// boot event 0x24 (save list read, the main menu is up; questmenu's start point), after questmenu's quest build if one
// runs, a plugin thread calls the game's own preload, ICharacterManager::LoadAndLockResources (vtable +0x70), for female.cdf and male.cdf; nothing purges them
// before the first load, whose spawns then find both in the character manager's cache. 0 = off.
// Mode 1 (since 2026-10-24) starts the preload at 0x24 beside questmenu's build, not after it: with the auto-load
// (readymenu) the load began before the build ended and the preload never ran (boot 86: the load built both sets on
// Main, ~1.9 s). The global header array's only add (FUN_180b14d68) is hooked: while the preload runs, an add from any
// other thread waits for it (10 s cap, "animahead: header add ... held" lines). Boots 76-86: no animation loads during
// the quest build. Mode 2 = the old rule (after the quest build).
// The animation header list takes no lock: boot 39 called the preload on Main during the terrain wait and crashed 1.1 s
// in (dump: FUN_18083f874, an entry of the global header array CM+0x280 read after the array moved), while the
// terrain's own loaders (JobWorker_05) added hen/book/tongs animations to it. So the preload never starts once a level
// load began, and while it runs another thread's LoadModelSKEL / LoadCHRPARAMS (and the header add) waits for it: gate 1
// (the second number in fastload_animahead.txt, the default since 2026-10-25). Those entries hold no character-manager
// lock (LoadModelSKEL's cache lookup unlocks CM+0xa8 before it returns). Boots 135/136: the level load starts at once,
// the preload ends 1.6 s into it, one terrain loader's hen_v2.chr waited 9-10 ms; to LoadLevel 1.4 -> 0.3 s, launch ->
// control 31.0 / 31.8 s (boots 131-134: 32.5-33.7). Gate 0 = "Loading level" (FUN_180b8f748) waits on Main (the old rule).
// Mode 1 since 2026-10-25: the preload starts at start-up, right after CharacterManager::OnFacialsPakChanged (Main, the
// language's facial paks just opened, ~11 s after the install, ~4 s before the menu), so it is done before the auto-load
// begins (boot 102: started at 0x24, the load began 0.5 s later and "Loading level" waited 3.2 s on Main). If it can't
// start there it starts at 0x24 as before. Mode 3 = at 0x24 beside the quest build (mode 1 before 2026-10-25).
// Every LoadCHRPARAMS call is logged ("animahead: chrparams": path, thread, ms; a call from another thread during the
// preload is marked), and every human skeleton load with its flags, so a test boot shows the load doesn't build those
// two again (flags bit 2 = the cache bypassed).
void Animahead_Install(uintptr_t gameBase, void* self);
// questmenu's boot event 0x24 hook (Main, once): starts the preload thread; waitFirst = an event to wait for first
// (questmenu's build: the two never run at the same time), or null.
void Animahead_AtMenu(void* waitFirst);
// Boot event 0x24 has come, and neither the preload nor questmenu's build (or a questbench) is running or still to come:
// the menu loads no animations now. terrainfast's menu preload waits for this (its meshes may add animation headers).
bool Animahead_MenuSettled();
