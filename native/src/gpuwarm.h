#pragma once
#include <cstdint>
// The graphics driver loaded on a worker (launch -> menu work, 2026-10-25). The first thing the main thread does after
// the game's start-up code is BugSplat's crash-report set-up (FUN_181dd9260), which asks for the GPU (FUN_1805f98ac ->
// FUN_1805f8c78: a DXGI factory, every adapter by GPU preference, D3D12CreateDevice(adapter, level, ID3D12Device, NULL)
// support checks). Being the process's first D3D call, it loads the NVIDIA driver, whose loader checks the driver
// DLLs' signatures (wintrust / imagehlp in boot 112-113's samples): ~1.1 s on the main thread (boot 110: 1095 ms), the
// later identical queries ~0.4 s.
// Here a worker started from DllMain makes the same calls (the same IIDs, read from the game, the same feature levels,
// no device created), and the crash reporter's two queries return "no GPU" at once: its system info (FUN_180dcfe0c,
// ISystem +0x548, from 0x180dcfef2) and its GPU fields (from 0x181dd93b9), each the first time (its report then has no
// GPU name / memory). Boot 116 skipped only the second: the first waited 689 ms for the worker, so nothing was saved.
// The renderer's query and the later ones run as before, after the worker is done (they wait for it).
// gpumemo (answering later queries from the first) is a different thing: it made boot 110's PSO precache 28 s. This
// keeps every query the renderer and spec detection see real; a test boot checks the precache time.
// devahead (2026-10-25): the renderer's own device creation (FUN_181df7bc0, D3D12CreateDevice on the first
// high-performance adapter with a display) took 1.3-1.7 s of main in boots 116-119 (the driver's DLLs loaded and their
// signatures checked: imagehlp, nvwgf2umx, nvgpucomp64 in the outside sampler at 3.9-5.6 s from process start). The
// same worker now makes that device right after the support checks; D3D12 keeps one device per adapter in a process,
// so the renderer's call gets it back. Its hook waits for the worker, lets the worker's device go first when the debug
// layer or DRED is on (enabling those with a device alive removes it / misses it), and drops the worker's reference
// after. NVIDIA Aftermath (on in this install) makes GPU crash dumps only for devices made after its set-up: this one
// is made before, so the game's GPU crash dumps are lost. "devahead:" lines. fastload_devahead.txt: 1 on (default), 0 off.
// nvcamera (2026-10-25): inside that device creation the NVIDIA driver loads NvCamera64.dll (photo mode, game filters,
// RTX HDR; they need the NVIDIA overlay, off on this PC). It loads nvapi and nvcuda, makes its own D3D11 device and checks
// signatures: 432 of the worker's 969 ms in boot 165e3's exact stacks, and Main waits for the worker (580-680 ms with
// zipidx 1). fastload_nvcamera.txt 1: ntdll's LdrLoadDll refuses NvCamera64.dll in this process (as if the driver had no
// NvCamera folder), so those NVIDIA features are off in this game; 0 (default) the driver's own. "nvcamera:" lines.
// nvcachewarm (2026-10-25): the render thread's first root signature (ResourceLayoutBinCache.txt, FUN_180b1afd0, at
// 3.0 s) spent 287 ms inside NvMemMapStoragex.dll (boot 165e3): the NVIDIA driver setting up its shader disk cache
// (DXCache, 3.9 GB here), once per process, on the chain Main's slot-5 flush and spec gate wait for. With
// fastload_nvcachewarm.txt 1 the worker makes one empty root signature on its device (the renderer's, see devahead)
// after the support checks, so that set-up runs ~1.3 s earlier beside Main. Skipped when the debug layer or DRED is on
// (the worker holds a device reference meanwhile). "nvcachewarm:" lines. 0 (default) off.
// fastload_gpuwarm.txt next to the plugin: 1 on (default), 0 off. Needs the plugin loaded before WHGame's static
// initializers (global.ini DontLoadFromDllMain=0). "gpuwarm:" lines in the log.
bool Gpuwarm_EarlyInstall(uintptr_t gameBase, void* selfModule);   // DllMain, MinHook initialized
void Gpuwarm_Report();   // Init: the state
void Gpuwarm_Frame();    // the frame tick: the worker's time, once
