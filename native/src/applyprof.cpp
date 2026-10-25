// The save load's apply step (level state 5) timed by piece: see applyprof.h.
#include "applyprof.h"
#include "readiness.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

// ---- WHGame.dll 1.5.6 (re_apply_xgen_profiles.md, 2026-10-24; bytes read from the installed DLL). All take integer /
// pointer arguments only (decompile signatures), so the wrapper passes 8 integer registers/slots through.
// Left out: PostLoadGame FUN_1825bda54 (samelevel.cpp hooks it), FUN_18051a8e0 (ftplaces.cpp hooks it).
struct AHook { const char* name; uintptr_t rva; unsigned char bytes[12]; void* orig; };
AHook g_h[] = {
	{ "CryEngine save data (FUN_1825bd8d8)", 0x25bd8d8, { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48 }, nullptr },
	{ "SoulList::LoadGameSouls (FUN_1811903dc)", 0x11903dc, { 0x48,0x89,0x5c,0x24,0x10,0x55,0x56,0x57,0x48,0x8d,0x6c,0x24 }, nullptr },
	{ "XGenAI linkables pass (FUN_180c3496c)", 0xc3496c, { 0x48,0x89,0x5c,0x24,0x10,0x55,0x48,0x8d,0x6c,0x24,0xc0,0x48 }, nullptr },
	{ "XGenAI dynamic linkables (FUN_180c34830)", 0xc34830, { 0x48,0x89,0x5c,0x24,0x08,0x55,0x48,0x8d,0x6c,0x24,0xa9,0x48 }, nullptr },
	{ "XGenAI FUN_180c34908", 0xc34908, { 0x4c,0x8b,0xdc,0x48,0x83,0xec,0x78,0x48,0x8b,0x05,0xaa,0xa6 }, nullptr },
	{ "concept sync chunk (FUN_181e2a0f0)", 0x1e2a0f0, { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0xb8,0xf2 }, nullptr },
	{ "state broadcast (FUN_1808d727c)", 0x8d727c, { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48 }, nullptr },
	{ "ConceptState sub-chunks (FUN_1819ecd64)", 0x19ecd64, { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48 }, nullptr },
	{ "ConceptState reader (FUN_181e2a280)", 0x1e2a280, { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x60,0x44,0x0f }, nullptr },
	{ "ConceptState node set (FUN_181141980)", 0x1141980, { 0x48,0x89,0x5c,0x24,0x08,0x55,0x56,0x57,0x48,0x8b,0xec,0x48 }, nullptr },
	{ "NodeManager register (FUN_181141624)", 0x1141624, { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b }, nullptr },
	{ "GameProfileManager::SynchronizeProfiles (FUN_18096b128)", 0x96b128, { 0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x48 }, nullptr },
	// The node set (FUN_181141980) asks C_RandomEventConceptStreaming (concept manager +0xb0) to load every place graph
	// the save had loaded, sync=1, one by one: the sync request (S, Place** slot, u8 notify), which runs the streaming
	// worker's job body on Main (under the lock at S+0x220) and then the finish (node registration).
	{ "concept graph sync load (FUN_182735b34)", 0x2735b34, { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x55,0x57 }, nullptr },
	{ "concept streaming job (FUN_18051a52c)", 0x51a52c, { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89 }, nullptr },
	{ "concept streaming finish (FUN_18051a794)", 0x51a794, { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89 }, nullptr },
};
constexpr int kN = sizeof g_h / sizeof g_h[0];
constexpr int kBroadcast = 6;
constexpr int kSyncLoad = 12;
// the broadcast is counted per state: slot kN = state 5, kN + 1 = state 2, the hook's own slot = any other state
constexpr int kSlots = kN + 2;

long long g_freq = 1;
std::atomic<long long> g_calls[kSlots][2], g_ticks[kSlots][2];   // [piece][0 Main, 1 other threads]
std::atomic<DWORD> g_mainTid{0};
std::atomic<long long> g_first[kSlots], g_state5At{0};   // QPC of a piece's first Main call; of the state 5 start

// Per graph, the sync loads on Main: (ms, place name), for the slowest-first list in the report.
std::mutex g_loadsMu;
std::vector<std::pair<double, std::string>> g_loads;

// The place's name (node +0x10, a char*), copied before the call (the request consumes the reference). Guarded.
void PlaceName(uint64_t slot, char* out, size_t n) {
	out[0] = 0;
	__try {
		const uintptr_t place = slot ? *(uintptr_t*)slot : 0;
		const char* name = place ? *(const char**)(place + 0x10) : nullptr;
		if (!name) return;
		size_t i = 0;
		for (; i + 1 < n && name[i]; i++) out[i] = (name[i] >= 0x20 && name[i] < 0x7f) ? name[i] : '?';
		out[i] = 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long t) { return (double)t * 1000.0 / g_freq; }

using Fn8 = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

// Timed only while the level manager is in state 5 (the save being applied); otherwise straight to the game's.
// Inclusive times: a piece's time contains the pieces it calls. A piece that calls itself counts once.
template <int I> uint64_t __fastcall Wrap(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	thread_local int depth = 0;
	if (depth == 0 && Readiness_LevelState() != 5) return ((Fn8)g_h[I].orig)(a, b, c, d, e, f, g, h);
	char name[80] = {};
	if (I == kSyncLoad && depth == 0) PlaceName(b, name, sizeof name);
	depth++;
	const long long t = Now();
	const uint64_t r = ((Fn8)g_h[I].orig)(a, b, c, d, e, f, g, h);
	const long long dt = Now() - t;
	depth--;
	if (depth == 0) {
		const int k = GetCurrentThreadId() == g_mainTid.load(std::memory_order_relaxed) ? 0 : 1;
		int slot = I;
		if (I == kBroadcast) slot = (uint32_t)b == 5 ? kN : (uint32_t)b == 2 ? kN + 1 : I;
		g_calls[slot][k]++;
		g_ticks[slot][k] += dt;
		long long none = 0;
		if (k == 0) g_first[slot].compare_exchange_strong(none, t);
		if (I == kSyncLoad && k == 0) {
			std::lock_guard<std::mutex> lk(g_loadsMu);
			if (g_loads.size() < 4096) g_loads.emplace_back(Ms(dt), name[0] ? name : "?");
		}
	}
	return r;
}
void* const g_wrap[] = { (void*)&Wrap<0>, (void*)&Wrap<1>, (void*)&Wrap<2>, (void*)&Wrap<3>, (void*)&Wrap<4>,
                         (void*)&Wrap<5>, (void*)&Wrap<6>, (void*)&Wrap<7>, (void*)&Wrap<8>, (void*)&Wrap<9>,
                         (void*)&Wrap<10>, (void*)&Wrap<11>, (void*)&Wrap<12>, (void*)&Wrap<13>, (void*)&Wrap<14> };
static_assert(sizeof g_wrap / sizeof g_wrap[0] == kN, "one wrapper per hook");

const char* SlotName(int i) {
	if (i == kN) return "state broadcast 5 (FUN_1808d727c)";
	if (i == kN + 1) return "state broadcast 2 (FUN_1808d727c)";
	if (i == kBroadcast) return "state broadcast, other states (FUN_1808d727c)";
	return g_h[i].name;
}

// Each time the level state leaves 5: one line with every piece's Main time and calls, then the counters start over.
DWORD WINAPI Reporter(LPVOID) {
	int prev = -1, applies = 0;
	for (;;) {
		Sleep(10);
		const int s = Readiness_LevelState();
		if (s == 5 && prev != 5) g_state5At = Now();   // within 10 ms of the real start (this thread polls)
		if (prev == 5 && s != 5 && applies < 20) {
			applies++;
			std::string line;
			for (int i = 0; i < kSlots; i++) {
				const long long cm = g_calls[i][0].exchange(0), co = g_calls[i][1].exchange(0);
				const long long tm = g_ticks[i][0].exchange(0), to = g_ticks[i][1].exchange(0);
				if (!cm && !co) continue;
				char b[256];
				const long long first = g_first[i].exchange(0), at = g_state5At.load();
				char when[48] = "";
				if (first && at) snprintf(when, sizeof when, " from +%.0f", Ms(first - at));
				snprintf(b, sizeof b, "%s%s %.0f ms / %lld%s%s", line.empty() ? "" : "; ", SlotName(i), Ms(tm), cm, when,
				         co ? (" (other threads " + std::to_string((long long)Ms(to)) + " ms / " + std::to_string(co) + ")").c_str() : "");
				line += b;
			}
			Log("applyprof: apply step %d done (level state 5 -> %d), Main, inclusive: %s", applies, s,
			    line.empty() ? "nothing timed" : line.c_str());
			// the place graphs loaded one by one: count, sizes, the slowest 12 by name
			std::vector<std::pair<double, std::string>> loads;
			{ std::lock_guard<std::mutex> lk(g_loadsMu); loads.swap(g_loads); }
			if (!loads.empty()) {
				double total = 0;
				int over[4] = {};   // >= 1, 5, 20, 50 ms
				for (auto& l : loads) {
					total += l.first;
					over[0] += l.first >= 1; over[1] += l.first >= 5; over[2] += l.first >= 20; over[3] += l.first >= 50;
				}
				std::sort(loads.begin(), loads.end(), [](auto& x, auto& y) { return x.first > y.first; });
				std::string top;
				for (size_t i = 0; i < loads.size() && i < 12; i++) {
					char b[160];
					snprintf(b, sizeof b, "%s%s %.1f", i ? ", " : "", loads[i].second.c_str(), loads[i].first);
					top += b;
				}
				Log("applyprof: apply step %d: %zu place graphs loaded sync on Main, %.0f ms (>=1 ms %d, >=5 %d, >=20 %d, "
				    ">=50 %d); slowest (ms): %s", applies, loads.size(), total, over[0], over[1], over[2], over[3], top.c_str());
			}
		}
		prev = s;
	}
	return 0;
}

}  // namespace

void Applyprof_SetMainThread(unsigned long tid) { g_mainTid = (DWORD)tid; }

void Applyprof_Install(uintptr_t base, void* self) {
	LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); g_freq = fq.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	int mode = 1;
	if (FILE* fp = fopen((dir + "fastload_applyprof.txt").c_str(), "r")) {
		int m = 1;
		if (fscanf(fp, "%d", &m) == 1) mode = m ? 1 : 0;
		fclose(fp);
	}
	if (!mode) { Log("applyprof: off (fastload_applyprof.txt)"); return; }
	int on = 0;
	std::string off;
	for (int i = 0; i < kN; i++) {
		void* t = (void*)(base + g_h[i].rva);
		if (memcmp(t, g_h[i].bytes, sizeof g_h[i].bytes) || MH_CreateHook(t, g_wrap[i], &g_h[i].orig) != MH_OK ||
		    MH_EnableHook(t) != MH_OK) {
			off += off.empty() ? "" : ", ";
			off += g_h[i].name;
			continue;
		}
		on++;
	}
	Log("applyprof: the apply step's pieces timed (level state 5 only), %d of %d hooks on%s%s", on, kN,
	    off.empty() ? "" : "; FAILED: ", off.c_str());
	if (on) CloseHandle(CreateThread(nullptr, 0, Reporter, nullptr, 0, nullptr));
}
