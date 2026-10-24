#pragma once
#include <cstddef>
#include <cstdint>
#include <windows.h>
// Long frames, sampled from inside the game. When the main thread hasn't finished a drawn frame for 250 ms, a watcher
// thread samples where it is every 20 ms: it suspends the main thread, copies its registers and the top 32 KB of its
// stack into a fixed buffer, and resumes it (no allocation, no lock, nothing else while it is suspended). After resuming
// it keeps the return addresses (stack values right after a call instruction in a loaded module).
// Why inside: during the 4 s first gameplay frame after a save load the outside sampler (tools/threadsample.ps1) got
// 1 sample in 5 s while Windows paged in 16-58k pages a second (boot 16, 2026-10-24); this one doesn't read the game's
// memory from another process.
// Per long frame, in fastload.log:
//   "hitch: #N main frame X ms, K samples, page faults +F; in: <module+offset or WHGame RVA> P %, ...; called from: ..."
// and every sample in fastload_hitch.csv next to the plugin (hitch,frame_ms,t_ms,faults,rip,stack), for
// symbolizing offline (WHGame.dll+offset). On by default; `hitch off`, `hitch on`, `hitch <ms>` (threshold, 50-5000).
void Hitch_Install(uintptr_t gameBase, size_t gameSize, HMODULE self);   // starts the watcher
void Hitch_Frame();                       // main thread, once a drawn frame (fastload_main.cpp)
void Hitch_Command(const char* args);     // "hitch [on|off|<ms>]"
