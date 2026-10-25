#pragma once
#include <cstdint>
// The D3D12 pipeline library made on a worker (launch -> menu work, 2026-10-25). Right after the device exists the
// renderer reads the user's pipeline cache (Saved Games\kingdomcome2\shaders\cache\d3d12\pipelinecache.dat, 92 MB here)
// and hands it to ID3D12Device1::CreatePipelineLibrary (FUN_180ba9f80 -> FUN_180ba9f34, DX12Device.cpp). The driver
// checks the whole blob first: ~0.9 s of the main thread (boot 110's kcd.log: "shadercache.pak is opened" at 2.14,
// the next line at 3.05; boot 113's samples under FUN_180ba9f34 in the driver's code). Nothing uses the library until
// the first PSO loads, ~10 s later (the start-up's blocking precache at 13.4 s).
// Here FUN_180ba9f34 hands the game a stand-in library at once (S_OK) and a worker makes the real one from the same
// blob; every call on the stand-in waits for the worker, then goes to the real library. If the real one fails with the
// blob, the worker tries an empty one, as the game does; if that fails too the stand-in answers "not in the library"
// (the game then compiles its PSOs, as without a cache) and sets the device's "no library" flag (+0x158).
// fastload_pipelib.txt next to the plugin: 1 on (default), 0 off. "pipelib:" lines in the log.
void Pipelib_Install(uintptr_t gameBase, void* selfModule);   // Init, before the renderer starts (~2.8 s after the plugin)
