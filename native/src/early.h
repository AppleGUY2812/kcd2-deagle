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
