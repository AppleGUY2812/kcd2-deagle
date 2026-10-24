// Outside-the-game test of hitch.cpp (the long-frame sampler), a Windows exe: the "main thread" draws 10 ms frames,
// then stalls 700 ms in Busy() (CPU) and 600 ms in Sleep() (a wait), then frames again. The sampler must report two
// long frames, sample the first inside Busy and the second in a wait, and leave short frames alone.
// Run: hitch_test.exe <dir>   (writes <dir>\hitch_test.log and <dir>\deagle_hitch.csv)
#include "../src/hitch.h"
#include "../src/log.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" IMAGE_DOS_HEADER __ImageBase;
static volatile unsigned long long g_sink;

__declspec(noinline) void Busy(int ms) {
	const ULONGLONG end = GetTickCount64() + ms;
	unsigned long long x = 1;
	while (GetTickCount64() < end) for (int i = 0; i < 100000; i++) x = x * 6364136223846793005ull + 1442695040888963407ull;
	g_sink = x;
}

static DWORD WINAPI Render(LPVOID) {   // stands in for the game's render thread: sampled by name
	using Fn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
	if (Fn set = (Fn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetThreadDescription")) set(GetCurrentThread(), L"RenderThread");
	for (;;) Busy(5);
}

__declspec(noinline) void Frames(int n) {
	for (int i = 0; i < n; i++) { Hitch_Frame(); Sleep(10); }
}

static LONG CALLBACK Crash(EXCEPTION_POINTERS* e) {
	if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
	const uintptr_t a = (uintptr_t)e->ExceptionRecord->ExceptionAddress;
	printf("crash: access violation at exe+%llx (reading %llx), thread %lu\n", (unsigned long long)(a - (uintptr_t)&__ImageBase),
	       (unsigned long long)e->ExceptionRecord->ExceptionInformation[1], GetCurrentThreadId());
	fflush(stdout);
	return EXCEPTION_CONTINUE_SEARCH;
}

int main() {
	AddVectoredExceptionHandler(0, Crash);   // last, so the sampler's own guarded reads handle theirs first
	LogInit(&__ImageBase);   // hitch_test.log next to the exe
	HMODULE self = (HMODULE)&__ImageBase;
	const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)((const char*)self + __ImageBase.e_lfanew);
	Hitch_Install((uintptr_t)self, nt->OptionalHeader.SizeOfImage, self);   // the exe stands in for WHGame.dll
	CloseHandle(CreateThread(nullptr, 0, Render, nullptr, 0, nullptr));
	Frames(30);
	Busy(700);
	Frames(30);
	Sleep(600);
	Frames(60);
	Sleep(200);   // the watcher reports at its next look
	char path[MAX_PATH];
	GetModuleFileNameA(self, path, MAX_PATH);
	std::string log(path);
	log = log.substr(0, log.find_last_of('.')) + ".log";
	FILE* f = fopen(log.c_str(), "r");
	if (!f) { printf("FAIL: no log\n"); return 1; }
	char line[4096];
	int hitches = 0, busyOk = 0, waitOk = 0, renderOk = 0;
	while (fgets(line, sizeof line, f)) {
		if (!strstr(line, "hitch: #")) continue;
		printf("%s", line);
		if (strstr(line, " RenderThread: ") && strstr(line, "in: WHGame.dll+")) { renderOk++; continue; }
		hitches++;
		int ms = 0, n = 0;
		const char* p = strstr(line, "main frame ");
		if (p) sscanf(p, "main frame %d ms, %d samples", &ms, &n);
		if (hitches == 1 && ms >= 650 && ms <= 900 && n >= 8 && strstr(line, "in: WHGame.dll+")) busyOk = 1;
		if (hitches == 2 && ms >= 550 && ms <= 800 && n >= 6 && (strstr(line, "in: ntdll.dll+") || strstr(line, "in: KERNELBASE")))
			waitOk = 1;
	}
	fclose(f);
	if (hitches != 2 || !busyOk || !waitOk || renderOk != 2) {
		printf("FAIL: %d long frames (want 2), busy %d, wait %d, render thread sampled in %d (want 2)\n", hitches, busyOk,
		       waitOk, renderOk);
		return 1;
	}
	printf("hitch_test: all passed\n");
	return 0;
}
