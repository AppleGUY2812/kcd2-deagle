// The level load's main-thread work, timed and sampled: see levelprof.h.
#include "levelprof.h"
#include "loadwatch.h"
#include "questsample.h"
#include "readiness.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// ---- WHGame.dll 1.5.6 (re_stepEF.md, re_stepC.md, re_menu_main.md; bytes read from the installed DLL 2026-10-24)
struct LHook { const char* name; uintptr_t rva; unsigned char bytes[12]; int phase; void* orig; };
LHook g_h[] = {
	{ "LoadLevel", 0x9b17e0, { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x18,0x48,0x89,0x70,0x20,0x57 }, 1, nullptr },   // CLevelSystem::LoadLevel
	{ "LoadEntities", 0xa57aa8, { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89 }, 3, nullptr },
	{ "navmesh tiles", 0x20b4410, { 0x40,0x55,0x57,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24,0x28 }, 4, nullptr },
	{ "object database", 0xef3afc, { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89 }, 5, nullptr },   // (mgr, type)
	{ "XGenAI LoadGameGlobalPass", 0xb4403c, { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x42,0x18,0x48,0x8b }, 8, nullptr },
	{ "material dedupe", 0x45b230, { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x18,0x48,0x89 }, 9, nullptr },
};
constexpr int kN = sizeof g_h / sizeof g_h[0];
constexpr int kLoadLevel = 0, kDb = 3, kDedupe = 5;

int g_mode = 0;
long long g_freq = 1;
std::atomic<DWORD> g_mainTid{0};
std::atomic<int> g_phase{0};      // the innermost timed hook on Main (0 = none)
std::atomic<bool> g_inLoad{false};
std::atomic<long long> g_calls[kN][2], g_ticks[kN][2];   // [hook][0 Main, 1 other threads]
std::atomic<int> g_lines{0};

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long t) { return (double)t * 1000.0 / g_freq; }

int Phase() {   // the sampler thread, each sample
	if (Loadwatch_InTerrainWait()) return 2;
	const int p = g_phase.load(std::memory_order_relaxed);
	if (p) return p;
	if (g_inLoad.load(std::memory_order_relaxed)) return 1;
	const int s = Readiness_LevelState();
	return s >= 0 && s <= 9 ? 10 + s : 20;
}

const char* PhaseName(int p) {
	static const char* const kState[] = { "after LoadLevel, level state 0 (gameplay)", "after LoadLevel, level state 1",
		"after LoadLevel, level state 2", "after LoadLevel, level state 3", "after LoadLevel, level state 4",
		"after LoadLevel, level state 5 (apply save)", "after LoadLevel, level state 6 (Settle gate)",
		"after LoadLevel, level state 7", "after LoadLevel, level state 8", "after LoadLevel, level state 9" };
	switch (p) {
	case 1: return "LoadLevel (outside the timed pieces)";
	case 2: return "terrain wait";
	case 3: return "LoadEntities";
	case 4: return "navmesh tiles";
	case 5: return "object database (other types)";
	case 6: return "object database type 0x10";
	case 7: return "object database type 0x11 (SmartObjectAnimations)";
	case 8: return "XGenAI LoadGameGlobalPass";
	case 9: return "material dedupe";
	case 20: return "after LoadLevel, level state unknown";
	default: return p >= 10 && p <= 19 ? kState[p - 10] : "?";
	}
}

void Totals(const char* when) {
	std::string s;
	for (int i = 0; i < kN; i++) {
		const long long cm = g_calls[i][0].load(), co = g_calls[i][1].load();
		if (!cm && !co) continue;
		char b[200];
		snprintf(b, sizeof b, "%s%s: Main %.0f ms / %lld calls, other threads %.0f ms / %lld calls", s.empty() ? "" : "; ", g_h[i].name,
		         Ms(g_ticks[i][0].load()), cm, Ms(g_ticks[i][1].load()), co);
		s += b;
	}
	Log("levelprof: %s: %s", when, s.c_str());
}

using Fn8 = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

// Passes 8 integer arguments through (LoadEntities takes 5; none of the six takes a float).
template <int I> uint64_t __fastcall Wrap(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	thread_local int depth = 0;
	if (I == kLoadLevel && depth == 0) {
		g_mainTid = GetCurrentThreadId();
		g_inLoad = true;
		for (int i = 0; i < kN; i++) for (int k = 0; k < 2; k++) { g_calls[i][k] = 0; g_ticks[i][k] = 0; }
		if (g_mode == 1) {
			const bool on = Questsample_BeginEx(&Phase, &PhaseName, 30000, "level load", "fastload_lsample.csv");
			Log("levelprof: LoadLevel starts (thread %lu); Main sampled for 30 s: %s", GetCurrentThreadId(), on ? "on" : "NOT STARTED (a profile runs)");
		}
	}
	const bool main = GetCurrentThreadId() == g_mainTid.load(std::memory_order_relaxed);
	int prev = 0;
	if (main && depth == 0) {
		int p = g_h[I].phase;
		if (I == kDb) p = (uint32_t)b == 0x11 ? 7 : (uint32_t)b == 0x10 ? 6 : 5;
		prev = g_phase.exchange(p);
	}
	depth++;
	const long long t = Now();
	const uint64_t r = ((Fn8)g_h[I].orig)(a, b, c, d, e, f, g, h);
	const long long dt = Now() - t;
	depth--;
	if (depth == 0) {
		if (main) g_phase = prev;
		g_calls[I][main ? 0 : 1]++;
		g_ticks[I][main ? 0 : 1] += dt;
		if (I != kDedupe && Ms(dt) >= 20.0 && g_lines++ < 80) {
			if (I == kDb) Log("levelprof: object database type 0x%x %.0f ms (thread %lu%s)", (uint32_t)b, Ms(dt), GetCurrentThreadId(), main ? ", Main" : "");
			else Log("levelprof: %s %.0f ms (thread %lu%s)", g_h[I].name, Ms(dt), GetCurrentThreadId(), main ? ", Main" : "");
		}
		if (I == kLoadLevel) { g_inLoad = false; Totals("LoadLevel done"); }
	}
	return r;
}
void* const g_wrap[] = { (void*)&Wrap<0>, (void*)&Wrap<1>, (void*)&Wrap<2>, (void*)&Wrap<3>, (void*)&Wrap<4>, (void*)&Wrap<5> };
static_assert(sizeof g_wrap / sizeof g_wrap[0] == kN, "one wrapper per hook");

}  // namespace

void Levelprof_Install(uintptr_t base, void* self) {
	LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); g_freq = fq.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((dir + "fastload_levelprof.txt").c_str(), "r")) {
		int m = 0;
		if (fscanf(fp, "%d", &m) == 1) g_mode = m == 1 || m == 2 ? m : 0;
		fclose(fp);
	}
	if (!g_mode) { Log("levelprof: off (fastload_levelprof.txt: 1 = timing + Main sampled, 2 = timing only)"); return; }
	int on = 0;
	std::string off;
	for (int i = 0; i < kN; i++) {
		void* t = (void*)(base + g_h[i].rva);
		if (memcmp(t, g_h[i].bytes, sizeof g_h[i].bytes) || MH_CreateHook(t, g_wrap[i], &g_h[i].orig) != MH_OK || MH_EnableHook(t) != MH_OK) {
			off += off.empty() ? "" : ", "; off += g_h[i].name; continue;
		}
		on++;
	}
	Log("levelprof: mode %d (%s), %d of %d hooks on%s%s", g_mode, g_mode == 1 ? "timing + Main sampled 30 s from LoadLevel" : "timing only",
	    on, kN, off.empty() ? "" : "; FAILED: ", off.c_str());
}
