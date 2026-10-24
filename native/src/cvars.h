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
// Values from deagle_cvars.txt next to the plugin, one per line: "<name> <value> [load]"; "load" = only until gameplay
// starts (Lua sends "loaded" from OnGameplayStarted), then the game's default comes back. Missing file: the defaults in cvars.cpp.
void Cvars_Install(uintptr_t gameBase, void* selfModule);   // right after MH_Initialize, before the game registers
void Cvars_OnLoaded();                                       // gameplay started (Lua "loaded")
void Cvars_Command(const char* args);                        // "cvars": log what was set
