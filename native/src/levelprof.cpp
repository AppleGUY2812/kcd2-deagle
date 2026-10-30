// The level load's main-thread work, timed and sampled: see levelprof.h.
#include "levelprof.h"
#include "loadwatch.h"
#include "questsample.h"
#include "readiness.h"
#include "paklock.h"
#include "fastalloc.h"
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

int g_mode = 2;   // timing only unless the file says otherwise
void (*volatile g_dbPre)() = nullptr;   // called first in the object-database hook (Levelprof_SetDbPreCall)
void (*volatile g_dbGate)(uint32_t) = nullptr;   // then this one, with the type (Levelprof_SetDbTypeGate)
long long g_freq = 1;
std::atomic<DWORD> g_mainTid{0};
std::atomic<int> g_phase{0};      // the innermost timed hook on Main (0 = none)
std::atomic<bool> g_inLoad{false};
std::atomic<unsigned> g_loadSeq{0};   // LoadLevel calls started (Levelprof_LoadSeq)
std::atomic<long long> g_calls[kN][2], g_ticks[kN][2];   // [hook][0 Main, 1 other threads]
std::atomic<int> g_lines{0};
void (*volatile g_endCall)() = nullptr;   // runs on Main as LoadLevel ends (Levelprof_SetEndCall)

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long t) { return (double)t * 1000.0 / g_freq; }

int Phase();
const char* PhaseName(int p);

// The game-profile list the SmartObjectAnimations table needs (dbahead.h): C_GameProfileManager, vector of profiles at
// +0x10 / +0x18, reached as *(*(*(WHGame+0x549d388)+0xe0)+0x158) (FUN_180d38cec's FUN_1809155c8 path; its enumerator
// FUN_180d38fec walks that vector). Empty at level-load start (boot 58), 879 by the table's load: the watcher logs when
// it fills, so the table can start as soon as it is full.
uintptr_t g_base = 0;
std::atomic<bool> g_watch{false};
long long ProfileCount() {
	__try {
		const uintptr_t root = *(volatile uintptr_t*)(g_base + 0x549d388);
		if (!root) return -1;
		const uintptr_t sub = *(volatile uintptr_t*)(root + 0xe0);
		if (!sub) return -2;
		const uintptr_t mgr = *(volatile uintptr_t*)(sub + 0x158);
		if (!mgr) return -3;
		const uintptr_t b = *(volatile uintptr_t*)(mgr + 0x10), e = *(volatile uintptr_t*)(mgr + 0x18);
		return e >= b && e - b < (1ull << 24) ? (long long)((e - b) / 8) : -4;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return -5; }
}
DWORD WINAPI ProfileWatch(void*) {   // from LoadLevel's start until 3 s after its end, every 1 ms
	const long long t0 = Now();
	long long last = ProfileCount(), lines = 0, endAt = 0;
	Log("levelprof: game profiles at LoadLevel start: %lld", last);
	while (true) {
		Sleep(1);
		const long long c = ProfileCount();
		if (c != last && lines++ < 40)
			Log("levelprof: game profiles %lld -> %lld at +%.0f ms (Main: %s)", last, c, Ms(Now() - t0), PhaseName(Phase()));
		last = c;
		if (!g_watch.load()) { if (!endAt) endAt = Now(); else if (Ms(Now() - endAt) > 3000) break; }
	}
	Log("levelprof: game profiles 3 s after LoadLevel: %lld (%lld changes)", last, lines);
	return 0;
}

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
	if (I == kDb) {   // dbasync's gate, then dbahead's (this hook holds FUN_180ef3afc's one slot)
		if (void (*pre)() = g_dbPre) pre();
		if (void (*gate)(uint32_t) = g_dbGate) gate((uint32_t)b);
	}
	if (I == kLoadLevel && depth == 0) {
		g_mainTid = GetCurrentThreadId();
		g_loadSeq++;
		Paklock_LevelStart((const char*)b);   // CLevelSystem::LoadLevel(this, level name): the level warm starts first
		g_inLoad = true;
		for (int i = 0; i < kN; i++) for (int k = 0; k < 2; k++) { g_calls[i][k] = 0; g_ticks[i][k] = 0; }
		Paklock_Report("before LoadLevel");   // the counters start over for the load
		if (!g_watch.exchange(true)) {
			if (HANDLE th = CreateThread(nullptr, 0, &ProfileWatch, nullptr, 0, nullptr)) CloseHandle(th);
			else g_watch = false;
		}
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
		if (I == kLoadLevel) {
			g_inLoad = false; g_watch = false; Totals("LoadLevel done"); Paklock_Report("LoadLevel"); Paklock_LevelEnd(); Fastalloc_Report("LoadLevel done");
			if (void (*e)() = g_endCall) e();
		}
	}
	return r;
}
void* const g_wrap[] = { (void*)&Wrap<0>, (void*)&Wrap<1>, (void*)&Wrap<2>, (void*)&Wrap<3>, (void*)&Wrap<4>, (void*)&Wrap<5> };
static_assert(sizeof g_wrap / sizeof g_wrap[0] == kN, "one wrapper per hook");

}  // namespace

void Levelprof_Install(uintptr_t base, void* self) {
	LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); g_freq = fq.QuadPart;
	g_base = base;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((dir + "fastload_levelprof.txt").c_str(), "r")) {
		int m = 2;
		if (fscanf(fp, "%d", &m) == 1) g_mode = m == 1 || m == 2 ? m : 0;
		fclose(fp);
	}
	if (!g_mode) { Log("levelprof: off (fastload_levelprof.txt: 1 = timing + Main sampled, 2 = timing only, the default)"); return; }
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

bool Levelprof_SetDbPreCall(void (*f)()) {
	if (!g_h[kDb].orig) return false;   // levelprof off or its hook failed
	g_dbPre = f;
	return true;
}

bool Levelprof_SetDbTypeGate(void (*f)(uint32_t)) {
	if (!g_h[kDb].orig) return false;
	g_dbGate = f;
	return true;
}

long long Levelprof_GameProfiles() { return g_base ? ProfileCount() : -1; }

unsigned long Levelprof_MainTid() { return g_mainTid.load(std::memory_order_relaxed); }
bool Levelprof_InLoadLevel() { return g_inLoad.load(std::memory_order_relaxed); }
unsigned Levelprof_LoadSeq() { return g_loadSeq.load(); }
bool Levelprof_LoadLevelHooked() { return g_h[kLoadLevel].orig != nullptr; }
bool Levelprof_SetEndCall(void (*f)()) {
	if (!g_h[kLoadLevel].orig) return false;
	g_endCall = f;
	return true;
}
