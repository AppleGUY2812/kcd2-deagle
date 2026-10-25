#pragma once
#include <cstdint>
// The stream engine's main-thread finalize during a load, timed and (mode 2) run more often. Decoded 2026-10-24
// (5a scratchpad re_terrain_loaders.md, WHGame.dll 1.5.6): every finished streaming read (the terrain step's 14,153
// brush CGFs and their LOD files among them) is finished on the main thread by CStreamEngine::MainThread_FinalizeIOJobs
// (FUN_180489670: CGF parse, CStatObj build, LOD reads issued). During a load the only caller is the loading-screen tick
// CSystem::UpdateLoadingScreen (FUN_1806a0708), which runs it at most once per 20 ms. Boot 46: the terrain wait was
// 1430 ms with the reads already served from memory (streamahead) and the main thread spinning 227k polls in between,
// so the wait is set by that 20 ms cadence and the finalize's own CPU, not by the disk.
// Boot 48 (mode 2): the wait fell 1430 -> 156 ms but started 1.4 s later, total unchanged: ~1.6 s of CGF StreamOnComplete
// is the main thread's own CPU whatever the tick rate, so the default is mode 1 (timing only).
// Mode 2: at every UpdateLoadingScreen call on the main thread while the game says it is loading (its own
// vt+0x128 check), the finalize also runs when the last one is at least `ms` (1) old. Same call sites, same thread, the
// game's own re-entry guard (DAT_18547dae5); only the 20 ms throttle is shorter. The loading screen itself still draws
// every 20 ms. Mode 1 (default) times only, mode 0 off. fastload_terrainfast.txt: "mode [ms]".
// Log: "terrainfast: terrain wait ..." when the terrain wait ends (loadwatch.cpp), and "terrainfast: load ..." when
// gameplay starts: finalize calls (the game's / ours), their main-thread ms, CGF and LOD completions and their ms, and the
// loading-screen ticks' ms.
// Menu preload (the third number in fastload_terrainfast.txt, 1 = on by default; re_terrain_menu.md, 2026-10-24): every
// load records the global mesh names (not levels/<level>/... or %level%/...) it hands the async CGF load (FUN_18052d91c)
// to fastload_terrainahead_<level>.txt. At the next start, once the newest save's level is known (questmenu's save scan)
// and before the session's first load, the main thread asks for those meshes at the menu, 16 a frame, at most 128
// unfinished, while the RAM rule allows, with no callback (as the game's own brushlist preloader does). Nothing frees
// CObjManager's objects between the menu and the terrain step on a first load (UnLoadLevel is a no-op with no level;
// FreeNotUsedCGFs runs at LEVEL_LOAD_END, after the terrain step has taken its references), so the terrain step finds
// them finished and skips their read and build. A mesh that fails to load is not registered (FUN_18052c6dc), so the
// level loads it itself. Only global meshes: 75 % of Trosecko's 14,153 live in level paks mounted after Continue.
// "terrainfast: menu preload ..." lines say how many were asked and finished at the menu, and when the load came.
void Terrainfast_Frame();   // main thread, every frame (fastload_main.cpp)
bool Terrainfast_MenuPreloadDone();   // the menu preload has asked for all its meshes (or has nothing to do): readymenu
void Terrainfast_Install(uintptr_t gameBase, void* self);   // after MH_Initialize
void Terrainfast_OnLoaded();                // gameplay started (fastload_main.cpp): the load's summary line
void Terrainfast_Command(const char* args);   // "terrainfast [0|1|2] [ms] [ahead 0|1]"
