// The save load's apply step (level state 5) timed by piece: see applyprof.h.
#include "applyprof.h"
#include "readiness.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

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
};
constexpr int kN = sizeof g_h / sizeof g_h[0];
constexpr int kBroadcast = 6;
// the broadcast is counted per state: slot kN = state 5, kN + 1 = state 2, the hook's own slot = any other state
constexpr int kSlots = kN + 2;

long long g_freq = 1;
std::atomic<long long> g_calls[kSlots][2], g_ticks[kSlots][2];   // [piece][0 Main, 1 other threads]
std::atomic<DWORD> g_mainTid{0};

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long t) { return (double)t * 1000.0 / g_freq; }

using Fn8 = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

// Timed only while the level manager is in state 5 (the save being applied); otherwise straight to the game's.
// Inclusive times: a piece's time contains the pieces it calls. A piece that calls itself counts once.
template <int I> uint64_t __fastcall Wrap(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) {
	thread_local int depth = 0;
	if (depth == 0 && Readiness_LevelState() != 5) return ((Fn8)g_h[I].orig)(a, b, c, d, e, f, g, h);
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
	}
	return r;
}
void* const g_wrap[] = { (void*)&Wrap<0>, (void*)&Wrap<1>, (void*)&Wrap<2>, (void*)&Wrap<3>, (void*)&Wrap<4>,
                         (void*)&Wrap<5>, (void*)&Wrap<6>, (void*)&Wrap<7>, (void*)&Wrap<8>, (void*)&Wrap<9>,
                         (void*)&Wrap<10>, (void*)&Wrap<11> };
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
		Sleep(100);
		const int s = Readiness_LevelState();
		if (prev == 5 && s != 5 && applies < 20) {
			applies++;
			std::string line;
			for (int i = 0; i < kSlots; i++) {
				const long long cm = g_calls[i][0].exchange(0), co = g_calls[i][1].exchange(0);
				const long long tm = g_ticks[i][0].exchange(0), to = g_ticks[i][1].exchange(0);
				if (!cm && !co) continue;
				char b[256];
				snprintf(b, sizeof b, "%s%s %.0f ms / %lld%s", line.empty() ? "" : "; ", SlotName(i), Ms(tm), cm,
				         co ? (" (other threads " + std::to_string((long long)Ms(to)) + " ms / " + std::to_string(co) + ")").c_str() : "");
				line += b;
			}
			Log("applyprof: apply step %d done (level state 5 -> %d), Main, inclusive: %s", applies, s,
			    line.empty() ? "nothing timed" : line.c_str());
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
