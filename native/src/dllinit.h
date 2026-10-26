#pragma once
#include <cstdint>
// WHGame.dll's static initializers outside the loader lock (launch -> menu work, 2026-10-26). The exe loads WHGame with
// LoadLibraryA (KingdomCome.exe+3378) and Windows runs WHGame's C runtime start-up inside that call (its security cookie,
// then ~20k static constructors: 0.18-0.64 s after the process start in boot 170b3, 0.25 s less with rttrname 3), holding
// the loader lock. A thread made meanwhile waits for that lock before it runs, so the gpuwarm worker (gpuwarm.h), made in
// this plugin's DllMain at 0.17 s, starts at 0.64 s, and Main's GPU query at ~1 s waits ~1.0 s for its driver load
// (boots 170*: the wait's end moved with the static initializers' end, rttrname 3 -0.2 s).
// With fastload_dllinit.txt 1 WHGame's entry point returns at once for the process attach, and the real one runs on Main
// when the exe calls CreateGameStartup (KingdomCome.exe+353b, right after GetProcAddress): the same work on the same
// thread before any other WHGame code runs, but outside the lock, so the driver worker loads beside it. Thread
// notifications to WHGame's entry point until then are dropped: its C runtime is the DLL one, its thread attach / detach
// handlers return 1 and do nothing (WHGame+41a6a0 = mov al,1; ret). Its two TLS callbacks run as before (a
// thread-local set to 0, its destructor registered, no C runtime needed). Nothing the 19,741 static initializers reach by
// direct calls starts a thread (_beginthreadex / _beginthread / Bink / RegisterWaitForSingleObject: 0 of 51,081
// reachable functions, 13 such calls elsewhere; calls through pointers not traced), so no game thread starts earlier.
// The fastload Init thread waits for the static initializers, as it did. The other plugins' start-up threads
// (deagle_native, kcd2_render) start earlier: they wait for WHGame and gEnv by polling. If the static initializers fail,
// CreateGameStartup returns null (the loader would have failed LoadLibraryA).
// fastload_dllinit.txt next to the plugin: 1 on, 0 off (default). "dllinit:" lines.
bool Dllinit_EarlyInstall(uintptr_t gameBase, void* selfModule);   // DllMain, MinHook initialized, before WHGame's entry runs
void Dllinit_WaitStatic();   // Init: returns once WHGame's static initializers ran (at once when they weren't held)
