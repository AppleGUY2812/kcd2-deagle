#pragma once
#include <cstdint>
// Load-time game settings (user 2026-10-23: "why cant we just use all the threads"). The 2026-10-23 boot sampler
// (docs/PERFORMANCE.md "What the 47 s load does") showed settings capping the load:
//   sys_SleepIfInactiveWH 1: "when the game window is inactive, sleep the main thread for 25ms every frame"; the save
//     step runs frame by frame, so tabbing out during a load stretched it (main thread 67 % of its samples in Sleep).
//   r_ShadersAsyncMaxThreads 1: one of the two terrain-step threads at 100 % parses shaders.
//   r_PSOCompilationWorkerThreads 2, r_TexturesStreamingMaxRequestedMB 4 / ...Jobs 32: the save step's texture
//     workers sat ~25 % busy.
// The plugin hooks the game's setting registration (FUN_1823d8168 int, FUN_1823d8b0c float: name, variable pointer,
// default, flags, help) and writes its own value into the variable right after. user.cfg, read later, still wins.
// Settings the console owns (registered by value through CXConsole::RegisterInt, console vtable +0x18) are set by a hook
// there: the value passed in is replaced (e.g. wh_sys_SkipStartupVideoWhenPossible, "cvars: ... (console-owned)").
// Names no hook saw (CryPak's sys_* settings register through FUN_180e40078 -> the console; queue 177 dropped
// sys_UncachedStreamReads 0 silently) are set through the console from the first frame on Main ("cvars: X a -> b
// through the console"); still missing at frame 600: "cvars: X ... not found". Such a value wins over user.cfg.
// Values from fastload_cvars.txt next to the plugin, one per line: "<name> <value> [load]"; "load" = only until gameplay
// starts (Lua sends "loaded" from OnGameplayStarted), then the game's default comes back. Missing file: the defaults in cvars.cpp.
// Boot 121 (2026-10-25): from Init the hooks went on at ApplyHooks("early"), DllMain +1.09 s, after the game had
// registered sys_intromoviesduringinit and wh_sys_SkipStartupVideoWhenPossible: the intro played, the menu 7.6 s late
// (1 of 12 boots). Now DllMain installs them when it runs before WHGame's start-up code (global.ini), enabled at once.
void Cvars_Install(uintptr_t gameBase, void* selfModule);   // DllMain (early) or Init, before the game registers; once
void Cvars_Frame();                                          // every frame, Main: names the hooks never saw, set through the console
void Cvars_OnLoaded();                                       // gameplay started (fastload_main.cpp: the level state back to 0)
void Cvars_Command(const char* args);                        // "cvars": log what was set
