#pragma once
#include <cstdint>
// The graphics driver loaded on a worker (launch -> menu work, 2026-10-25). The first thing the main thread does after
// the game's start-up code is BugSplat's crash-report set-up (FUN_181dd9260), which asks for the GPU (FUN_1805f98ac ->
// FUN_1805f8c78: a DXGI factory, every adapter by GPU preference, D3D12CreateDevice(adapter, level, ID3D12Device, NULL)
// support checks). Being the process's first D3D call, it loads the NVIDIA driver, whose loader checks the driver
// DLLs' signatures (wintrust / imagehlp in boot 112-113's samples): ~1.1 s on the main thread (boot 110: 1095 ms), the
// later identical queries ~0.4 s.
// Here a worker started from DllMain makes the same calls (the same IIDs, read from the game, the same feature levels,
// no device created), and the crash reporter's query returns "no GPU" at once (its report then has no GPU name / memory).
// The renderer's query and the later ones run as before, after the worker is done (they wait for it).
// gpumemo (answering later queries from the first) is a different thing: it made boot 110's PSO precache 28 s. This
// keeps every query the renderer and spec detection see real; a test boot checks the precache time.
// fastload_gpuwarm.txt next to the plugin: 1 on (default), 0 off. Needs the plugin loaded before WHGame's static
// initializers (global.ini DontLoadFromDllMain=0). "gpuwarm:" lines in the log.
bool Gpuwarm_EarlyInstall(uintptr_t gameBase, void* selfModule);   // DllMain, MinHook initialized
void Gpuwarm_Report();   // Init: the state
void Gpuwarm_Frame();    // the frame tick: the worker's time, once
