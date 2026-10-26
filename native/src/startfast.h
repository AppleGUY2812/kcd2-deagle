#pragma once
#include <cstdint>
// Start-up pieces (launch -> main menu), user 2026-10-24: "just put me in the game".
// Controller list: the game's input init (FUN_180da1fcc, "Initializing DirectInput") registers its gamepad callback with
// IGameInput::RegisterDeviceCallback(..., GameInputBlockingEnumeration, ...): Main waited 1.3 s in it on boot 81 (inside
// the Steam overlay's hook), with the render thread idle. Mode 1 asks for GameInputAsyncEnumeration instead (the same
// callbacks, from GameInput's own thread). The game's callback adds / removes the device in the input system with no
// lock, so every callback is queued and handed to the game on the main thread at the next frame tick (also hot plugs).
// fastload_startfast.txt next to the plugin: 1 (default) on, 0 the game's own. "startfast:" lines in the log.
// CPU detection (2026-10-25): the game's per-core probe (FUN_18136739c, run twice at start-up, "CPU Information" in the
// game log) pins the WHOLE PROCESS to each of the 12 logical cores in turn (SetProcessAffinityMask) to read CPUID there.
// Every thread of the process is squeezed onto that one core each time; Main sat in the kernel 74-988 ms (mean 346 ms,
// 19 boots of queues 158-159, 0.64-1.6 s after launch) for what is ~1 ms of CPUID. Mode 1 pins only the calling thread
// (SetThreadAffinityMask: the same cores, the same CPUID answers) through the game's import slot for
// SetProcessAffinityMask, only for the two calls in that function (by return address); every other caller gets the real one.
// fastload_cpuprobe.txt next to the plugin: 1 on, 0 the game's own (default until a boot pair tests it). Installed from
// DllMain (Init is too late).
void Startfast_EarlyInstall(uintptr_t gameBase, void* selfModule);   // DllMain, WHGame.dll mapped
void Startfast_Install(uintptr_t gameBase, void* selfModule);   // before the game's input init (~8 s after the plugin)
void Startfast_TickReady(bool ok);   // fastload_main.cpp: the frame tick is patched (true) or failed (false)
void Startfast_Frame();              // the main thread, once a frame
