#pragma once
#include <windows.h>
// The start-up before the plugin's Init thread runs (launch -> menu work, 2026-10-24). Boot 94: Init's first line came
// 2.77 s after the process started. WHGame.dll imports dinput8.dll (the ASI loader), so our DllMain runs while WHGame
// is still loading; a normal thread can't start until the loader lock is free, after WHGame's 19,869 static
// initializers. Early_Start (from DllMain) notes the time and starts a sampler thread made with
// THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH, which may run under the loader lock: it samples the thread that ran DllMain
// every 10 ms (instruction pointer + return addresses + its CPU time) into fixed buffers until Early_Report (mode 1) or
// the first frame (mode 2, default since 2026-10-25: the hitch sampler went blind at 0.6-2.1 s after the plugin in boot
// 18:45 while it could not suspend the driver worker). Log only.
// Mode 3 (default since 2026-10-25): also the game's render and audio threads ("RenderThread", "AudioThread(NoCfg)") and our
// GPU workers (gpuwarm, pipelib, ngxearly), found by name every 100 ms; every sampled thread has its own sampler thread
// (one that can't be stopped, e.g. in the kernel, no longer blinds the others), all sample buffers touched before use;
// busy shares from each thread's CPU cycles (QueryThreadCycleTime); per second: while Main waited, how often each other
// thread was busy; for each stretch of 300 ms+ without a Main sample: what the others and the memory probe did then.
// The memory probe: every 10 ms a 10 ms sleep (late = not run), a first touch of a fresh page (the working-set lock) and a
// 64 KB VirtualAlloc + free (the address-space lock), in fastload_early_probe.csv. Why: boots 153-155, Main stopped
// ~1.5 s at 1.3-3.4 s after launch in its D3D set-up (16 ms of CPU), the outside sampler saw no thread at all then; it
// came with streamahead's start-up read in 10 of 11 boots, and once (155hold2) without it.
// fastload_early.csv: one row a sample, then the thread, its busy share since its last sample and whether the frames
// are exact. Since 2026-10-25 late each sample's stack copy is unwound with the images' unwind data (as questsample.cpp)
// after the thread is resumed: up to 48 exact return addresses (the scan before kept 24, stale ones among them, and
// reached CSystem::Init's frame in ~8% of Main's samples); the scan stays as the fallback when the unwind stops early.
void Early_Start(HMODULE self);    // DllMain, DLL_PROCESS_ATTACH
void Early_Report(uintptr_t gameBase, size_t gameSize);   // Init's first lines: the times, the top places, fastload_early.csv
void Early_Frame(uintptr_t gameBase, size_t gameSize);    // the first frame (fastload_early.txt 2, default): Main to here, a line a second
DWORD Early_MainThread();   // the thread that ran DllMain: the game's main thread
void Early_Where(unsigned long long addr, uintptr_t gameBase, size_t gameSize, char* out, size_t n);   // "module+rva"

// Names the calling thread "JobWorker_fastload <what>": hitch's start-up window samples threads named JobWorker_*.
inline void Early_NameThread(const wchar_t* name) {
	using SetDescFn = long(__stdcall*)(void*, const wchar_t*);
	static const SetDescFn f = (SetDescFn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetThreadDescription");
	if (f) f(GetCurrentThread(), name);
}
