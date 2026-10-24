// The load's main-thread stream finalize, timed and run more often: see terrainfast.h.
#include "terrainfast.h"
#include "loadwatch.h"
#include "log.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include "MinHook.h"

namespace {

struct Target { const char* name; uintptr_t rva; unsigned char bytes[10]; void* orig; };
Target g_t[] = {
	{ "UpdateLoadingScreen", 0x6a0708, { 0x48,0x89,0x5c,0x24,0x08,0x55,0x56,0x57,0x48,0x83 }, nullptr },
	{ "FinalizeIOJobs", 0x489670, { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10 }, nullptr },
	{ "CGF StreamOnComplete", 0x48b5a0, { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10 }, nullptr },
	{ "LOD StreamOnComplete", 0x117e998, { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68 }, nullptr },
};
enum { kTick, kFin, kCgf, kLod, kN };
constexpr uintptr_t kRvaFinStats = 0x48ae28;   // IStreamEngine vt+0x38: the finalize + its stat copies, what the tick calls
const unsigned char kFinStatsBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8 };
constexpr uintptr_t kRvaMainTid = 0x492d9a8;   // the main thread's id, as UpdateLoadingScreen checks it

using TickFn = bool(__fastcall*)(void* sys);
using FinFn = void(__fastcall*)(void* se);
using Fn4 = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t);   // StreamOnComplete(this, stream, error)

std::atomic<int> g_mode{2};
std::atomic<int> g_ms{1};
uintptr_t g_base = 0;
FinFn g_finStats = nullptr;
void* g_se = nullptr;            // the stream engine, from the first main-thread finalize
long long g_freq = 1, g_lastFin = 0;
bool g_inFin = false, g_inPump = false, g_wasWait = false;

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline bool OnMain() { return GetCurrentThreadId() == *(const DWORD*)(g_base + kRvaMainTid); }

// Main-thread counters: [0] the whole load, [1] the terrain wait.
struct Tally {
	long long fin = 0, pumped = 0, finT = 0, finMax = 0, cgf = 0, cgfT = 0, lod = 0, lodT = 0, ticks = 0, tickT = 0;
};
Tally g_s[2];

double Ms(long long t) { return (double)t * 1000.0 / g_freq; }

std::string Line(const Tally& s) {
	char b[400];
	snprintf(b, sizeof b, "finalize %lld calls (%lld ours), %.0f ms, longest %.1f ms; CGF %lld (%.0f ms), LOD %lld (%.0f ms); "
	         "loading-screen ticks %lld (%.0f ms)", s.fin, s.pumped, Ms(s.finT), Ms(s.finMax), s.cgf, Ms(s.cgfT), s.lod,
	         Ms(s.lodT), s.ticks, Ms(s.tickT));
	return b;
}

void __fastcall HkFin(void* se) {
	if (g_inFin || !OnMain()) { ((FinFn)g_t[kFin].orig)(se); return; }   // the game's guard handles a nested call
	if (!g_se) g_se = se;
	g_inFin = true;
	const long long a = Now();
	((FinFn)g_t[kFin].orig)(se);
	const long long d = Now() - a;
	g_inFin = false;
	g_lastFin = a + d;
	const bool w = Loadwatch_InTerrainWait();
	for (int i = 0; i < (w ? 2 : 1); i++) {
		Tally& s = g_s[i];
		s.fin++; s.finT += d; if (d > s.finMax) s.finMax = d;
		if (g_inPump) s.pumped++;
	}
}

template <int I> uint64_t __fastcall HkDone(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {   // I = kCgf or kLod
	if (!g_inFin) return ((Fn4)g_t[I].orig)(a, b, c, d);   // only what the main-thread finalize runs is counted
	const long long t = Now();
	const uint64_t r = ((Fn4)g_t[I].orig)(a, b, c, d);
	const long long e = Now() - t;
	const bool w = Loadwatch_InTerrainWait();
	for (int i = 0; i < (w ? 2 : 1); i++) {
		if (I == kCgf) { g_s[i].cgf++; g_s[i].cgfT += e; } else { g_s[i].lod++; g_s[i].lodT += e; }
	}
	return r;
}

bool __fastcall HkTick(void* sys) {
	const int mode = g_mode.load(std::memory_order_relaxed);
	if (!mode || !OnMain()) return ((TickFn)g_t[kTick].orig)(sys);
	const long long finBefore = g_s[0].fin;
	const long long a = Now();
	const bool r = ((TickFn)g_t[kTick].orig)(sys);
	const long long b = Now();
	const bool w = Loadwatch_InTerrainWait();
	if (g_s[0].fin != finBefore)   // the game's own 20 ms tick ran (its finalize is inside)
		for (int i = 0; i < (w ? 2 : 1); i++) { g_s[i].ticks++; g_s[i].tickT += b - a; }
	if (mode == 2 && g_se && g_finStats && (b - g_lastFin) * 1000 >= (long long)g_ms.load() * g_freq) {
		using LoadingFn = bool(__fastcall*)(void*);
		const LoadingFn loading = (*(LoadingFn**)sys)[0x128 / 8];   // the game's own "is loading" check in this tick
		if (loading(sys)) { g_inPump = true; g_finStats(g_se); g_inPump = false; }
	}
	if (g_wasWait && !w) {
		Log("terrainfast: terrain wait: %s", Line(g_s[1]).c_str());
		g_s[1] = Tally();
	}
	g_wasWait = w;
	return r;
}

void* const g_hk[kN] = { (void*)&HkTick, (void*)&HkFin, (void*)&HkDone<kCgf>, (void*)&HkDone<kLod> };

}  // namespace

void Terrainfast_OnLoaded() {
	if (!g_base) return;
	Log("terrainfast: load (mode %d, every %d ms): %s", g_mode.load(), g_ms.load(), Line(g_s[0]).c_str());
	g_s[0] = Tally();
}

void Terrainfast_Command(const char* args) {
	int m = -1, ms = -1;
	const int n = args ? sscanf_s(args, "%d %d", &m, &ms) : 0;
	if (n >= 1 && m >= 0 && m <= 2) g_mode = m;
	if (n >= 2 && ms >= 0 && ms <= 20) g_ms = ms;
	Log("terrainfast: mode %d (0 off, 1 time only, 2 finalize every %d ms during loads)%s", g_mode.load(), g_ms.load(),
	    g_base ? "" : "; hooks not on");
}

void Terrainfast_Install(uintptr_t base, void* self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string file = path;
	file = file.substr(0, file.find_last_of('\\') + 1) + "fastload_terrainfast.txt";
	if (FILE* fp = fopen(file.c_str(), "r")) {
		int m = -1, ms = -1;
		const int n = fscanf(fp, "%d %d", &m, &ms);
		if (n >= 1 && m >= 0 && m <= 2) g_mode = m;
		if (n >= 2 && ms >= 0 && ms <= 20) g_ms = ms;
		fclose(fp);
	}
	if (!g_mode) { Log("terrainfast: off (fastload_terrainfast.txt)"); return; }
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	if (memcmp((void*)(base + kRvaFinStats), kFinStatsBytes, sizeof kFinStatsBytes)) {
		Log("terrainfast: code check failed (stream finalize) - not installed"); return;
	}
	for (Target& t : g_t)
		if (memcmp((void*)(base + t.rva), t.bytes, sizeof t.bytes)) { Log("terrainfast: code check failed (%s) - not installed", t.name); return; }
	g_base = base;   // read by the hooks (main thread id)
	g_finStats = (FinFn)(base + kRvaFinStats);
	int on = 0;
	for (int i = 0; i < kN; i++) {
		void* p = (void*)(base + g_t[i].rva);
		if (MH_CreateHook(p, g_hk[i], &g_t[i].orig) == MH_OK && MH_EnableHook(p) == MH_OK) { on++; continue; }
		Log("terrainfast: hook %s FAILED", g_t[i].name);
		for (int j = 0; j < i; j++) MH_DisableHook((void*)(base + g_t[j].rva));
		g_base = 0;
		return;
	}
	Log("terrainfast: mode %d (%s), %d hooks on", g_mode.load(),
	    g_mode == 2 ? "the stream finalize runs every 1 ms of loading-screen ticks, not every 20 ms" : "timing only", on);
}
