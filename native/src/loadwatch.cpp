// Terrain loader wait timing: see loadwatch.h.
#include "loadwatch.h"
#include "log.h"
#include <windows.h>
#include <cstring>
#include <string>
#include "MinHook.h"

namespace {

constexpr uintptr_t kRvaPoll = 0x354ff84;
const unsigned char kPollBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20 };
using PollFn = void(__fastcall*)(void* loaders, uint64_t a2);
PollFn g_orig = nullptr;
long long g_freq = 1, g_first = 0, g_lastPt = 0;
size_t g_startN = 0, g_lastN = ~(size_t)0;
unsigned long long g_polls = 0;
std::string g_curve;

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

size_t Count(void* v) {
	__try { const uintptr_t* p = (const uintptr_t*)v; return (size_t)((p[1] - p[0]) / 8); }
	__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void __fastcall HkPoll(void* loaders, uint64_t a2) {   // main thread only
	const long long a = Now();
	if (!g_first) {
		g_first = a; g_lastPt = a; g_startN = Count(loaders); g_polls = 0; g_curve.clear();
		Log("loadwatch: terrain wait starts, %zu loaders left", g_startN);
	}
	g_orig(loaders, a2);
	g_polls++;
	const size_t n = Count(loaders);
	const long long ms = (a - g_first) * 1000 / g_freq;
	if ((a - g_lastPt) * 4 >= g_freq || n == 0) {
		g_lastPt = a;
		char b[32]; snprintf(b, sizeof b, " %lld:%zu", ms, n); g_curve += b;
	}
	if (n == 0) {
		Log("loadwatch: terrain wait done after %lld ms (%zu loaders, %llu polls); left at ms:%s", ms, g_startN, g_polls,
		    g_curve.c_str());
		g_first = 0;
	}
	g_lastN = n;
}

}  // namespace

void Loadwatch_Install(uintptr_t base) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	void* p = (void*)(base + kRvaPoll);
	if (memcmp(p, kPollBytes, sizeof kPollBytes)) { Log("loadwatch: code check failed - not installed"); return; }
	const bool ok = MH_CreateHook(p, (void*)&HkPoll, (void**)&g_orig) == MH_OK && MH_EnableHook(p) == MH_OK;
	Log("loadwatch: terrain loader wait hook %s", ok ? "on" : "FAILED");
}
