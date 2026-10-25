#pragma once
#include <windows.h>
// The start-up before the plugin's Init thread runs (launch -> menu work, 2026-10-24). Boot 94: Init's first line came
// 2.77 s after the process started. WHGame.dll imports dinput8.dll (the ASI loader), so our DllMain runs while WHGame
// is still loading; a normal thread can't start until the loader lock is free, after WHGame's 19,869 static
// initializers. Early_Start (from DllMain) notes the time and starts a sampler thread made with
// THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH, which may run under the loader lock: it samples the thread that ran DllMain
// every 10 ms (instruction pointer + return addresses) into fixed buffers until Early_Report. Log only.
void Early_Start(HMODULE self);    // DllMain, DLL_PROCESS_ATTACH
void Early_Report(uintptr_t gameBase, size_t gameSize);   // Init's first lines: the times, the top places, fastload_early.csv
