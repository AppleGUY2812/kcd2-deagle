// Main's CryPhysics lock waits in the level load: see physlock.h.
#include "physlock.h"
#include "levelprof.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// DestroyPhysicalEntity (world, entity, mode, threadSafe) and CPhysicalEntity::AddGeometry (entity, geometry, params,
// id, threadSafe), integer arguments only; their first 12 bytes as read from the installed WHGame.dll 2026-10-25.
constexpr uintptr_t kDestroyRva = 0x846e10, kAddRva = 0x84b2d4;
const unsigned char kDestroyBytes[12] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x18,0x48 };
const unsigned char kAddBytes[12] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x18,0x48 };
using DestroyFn = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t);
using AddFn = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
DestroyFn g_destroy = nullptr;
AddFn g_add = nullptr;

int g_mode = 1;
long long g_freq = 1;
// mode 2: a quiet window for Main. A non-Main AddGeometry in LoadLevel does not start while Main is inside
// DestroyPhysicalEntity or left it less than g_winMs ago (Main's destroys come in runs; each one that met a running
// insertion waited it out, ~3-4 ms, 165-193 of them per load). Each such call waits at most g_capMs, so the streamer
// is delayed, never stopped.
double g_winMs = 2.0, g_capMs = 20.0;
std::atomic<int> g_dIn{0};               // Main inside DestroyPhysicalEntity now
std::atomic<long long> g_dEnd{0};        // when Main's latest DestroyPhysicalEntity returned
std::atomic<long long> g_hN{0}, g_hT{0}, g_hCap{0};   // AddGeometry calls held, time held, held to the cap

std::atomic<int> g_addIn{0};             // other threads inside AddGeometry now
std::atomic<long long> g_addStart{0};    // when the latest of those calls started
// Main's DestroyPhysicalEntity calls in LoadLevel: all; slow (0.5 ms or more); busy (another thread in AddGeometry at entry)
std::atomic<long long> g_dN{0}, g_dT{0}, g_dMax{0}, g_dSlowN{0}, g_dSlowT{0}, g_dBusyN{0}, g_dBusyT{0};
// Other threads' AddGeometry calls in LoadLevel, and their lengths: under 0.1 / 1 / 10 / 100 ms, then the rest
std::atomic<long long> g_aN{0}, g_aT{0}, g_aMax{0}, g_aB[5];
// world+0x1a0 at each of Main's calls (0, bit 0 set, other non-zero), all calls / slow ones: non-zero = Main locks the
// step lock, bit 0 = it may queue instead when the lock is held
std::atomic<long long> g_fl[3][2];
std::atomic<int> g_flOther{0};
std::atomic<int> g_lines{0};

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long t) { return (double)t * 1000.0 / g_freq; }
void Max(std::atomic<long long>& m, long long v) { long long c = m.load(); while (v > c && !m.compare_exchange_weak(c, v)) {} }

int ReadFlags(uint64_t world) {
	__try { return *(volatile int*)(world + 0x1a0); } __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}

uint64_t __fastcall HkDestroy(uint64_t world, uint64_t ent, uint64_t mode, uint64_t ts) {
	thread_local int depth = 0;
	if (depth || !Levelprof_InLoadLevel() || GetCurrentThreadId() != Levelprof_MainTid()) return g_destroy(world, ent, mode, ts);
	depth++;
	const int fl = ReadFlags(world);
	const int fk = fl == 0 ? 0 : (fl & 1) ? 1 : 2;
	if (fk == 2) g_flOther = fl;
	if (g_mode == 2) g_dIn.store(1);
	const bool busy = g_addIn.load() > 0;
	const long long t = Now();
	const uint64_t r = g_destroy(world, ent, mode, ts);
	const long long dt = Now() - t;
	if (g_mode == 2) { g_dEnd.store(Now()); g_dIn.store(0); }
	depth--;
	g_dN++; g_dT += dt; Max(g_dMax, dt);
	if (busy) { g_dBusyN++; g_dBusyT += dt; }
	g_fl[fk][0]++;
	if (Ms(dt) >= 0.5) { g_dSlowN++; g_dSlowT += dt; g_fl[fk][1]++; }
	if (Ms(dt) >= 10.0 && g_lines++ < 20)
		Log("physlock: Main's DestroyPhysicalEntity took %.1f ms (another thread in AddGeometry at its start: %s)", Ms(dt), busy ? "yes" : "no");
	return r;
}

uint64_t __fastcall HkAdd(uint64_t ent, uint64_t geom, uint64_t params, uint64_t id, uint64_t ts) {
	thread_local int depth = 0;
	if (depth || !Levelprof_InLoadLevel() || GetCurrentThreadId() == Levelprof_MainTid()) return g_add(ent, geom, params, id, ts);
	depth++;
	if (g_mode == 2) {   // Main's quiet window (see g_winMs)
		const long long a = Now(), win = (long long)(g_winMs * g_freq / 1000.0), cap = a + (long long)(g_capMs * g_freq / 1000.0);
		bool held = false;
		for (;;) {
			const long long n = Now();
			if (!(g_dIn.load() || n - g_dEnd.load() < win)) break;
			if (n >= cap) { g_hCap++; break; }
			held = true;
			SwitchToThread();
		}
		if (held) { g_hN++; g_hT += Now() - a; }
	}
	g_addIn++;
	const long long t = Now();
	g_addStart = t;
	const uint64_t r = g_add(ent, geom, params, id, ts);
	const long long dt = Now() - t;
	g_addIn--;
	depth--;
	g_aN++; g_aT += dt; Max(g_aMax, dt);
	const double ms = Ms(dt);
	g_aB[ms < 0.1 ? 0 : ms < 1 ? 1 : ms < 10 ? 2 : ms < 100 ? 3 : 4]++;
	return r;
}

void Report() {   // Main, as LoadLevel ends
	Log("physlock: LoadLevel: Main's DestroyPhysicalEntity %lld calls, %.0f ms (longest %.1f ms; %lld calls of 0.5 ms or more, "
	    "%.0f ms; %lld began while another thread was in AddGeometry, %.0f ms)", g_dN.load(), Ms(g_dT.load()), Ms(g_dMax.load()),
	    g_dSlowN.load(), Ms(g_dSlowT.load()), g_dBusyN.load(), Ms(g_dBusyT.load()));
	Log("physlock: LoadLevel: other threads' AddGeometry %lld calls, %.0f ms (longest %.1f ms; under 0.1 / 1 / 10 / 100 ms / more: "
	    "%lld / %lld / %lld / %lld / %lld)", g_aN.load(), Ms(g_aT.load()), Ms(g_aMax.load()), g_aB[0].load(), g_aB[1].load(),
	    g_aB[2].load(), g_aB[3].load(), g_aB[4].load());
	if (g_mode == 2)
		Log("physlock: LoadLevel: Main's quiet window (%.1f ms, at most %.0f ms a call): %lld AddGeometry calls held, %.0f ms in all, "
		    "%lld to the cap", g_winMs, g_capMs, g_hN.load(), Ms(g_hT.load()), g_hCap.load());
	Log("physlock: LoadLevel: the physics world's flags (world+0x1a0) at Main's calls (all / slow): 0 %lld / %lld, bit 0 set %lld / %lld, "
	    "other %lld / %lld (0x%x)", g_fl[0][0].load(), g_fl[0][1].load(), g_fl[1][0].load(), g_fl[1][1].load(), g_fl[2][0].load(),
	    g_fl[2][1].load(), g_flOther.load());
}

}  // namespace

void Physlock_Install(uintptr_t base, void* self) {
	LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); g_freq = fq.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((dir + "fastload_physlock.txt").c_str(), "r")) {
		int m = 1;
		double w = 0, c = 0;
		const int got = fscanf(fp, "%d %lf %lf", &m, &w, &c);
		if (got >= 1) g_mode = (m == 1 || m == 2) ? m : 0;
		if (got >= 2 && w > 0 && w <= 50) g_winMs = w;
		if (got >= 3 && c > 0 && c <= 200) g_capMs = c;
		fclose(fp);
	}
	if (!g_mode) { Log("physlock: off (fastload_physlock.txt: 1 = timing, the default; 2 [window ms] [cap ms] = Main's quiet window)"); return; }
	if (!Levelprof_SetEndCall(&Report)) { Log("physlock: off: needs levelprof's LoadLevel hook (fastload_levelprof.txt 0 or its hook failed)"); return; }
	void* d = (void*)(base + kDestroyRva);
	void* a = (void*)(base + kAddRva);
	if (memcmp(d, kDestroyBytes, sizeof kDestroyBytes) || memcmp(a, kAddBytes, sizeof kAddBytes)) {
		Log("physlock: off: DestroyPhysicalEntity / AddGeometry bytes differ (another game version?)");
		return;
	}
	if (MH_CreateHook(d, (void*)&HkDestroy, (void**)&g_destroy) != MH_OK || MH_EnableHook(d) != MH_OK) { Log("physlock: off: the DestroyPhysicalEntity hook failed"); return; }
	if (MH_CreateHook(a, (void*)&HkAdd, (void**)&g_add) != MH_OK || MH_EnableHook(a) != MH_OK) {
		MH_DisableHook(d);
		Log("physlock: off: the AddGeometry hook failed");
		return;
	}
	if (g_mode == 2) Log("physlock: mode 2: other threads' AddGeometry waits for Main's quiet window (%.1f ms after its DestroyPhysicalEntity, at most %.0f ms a call) in the level load", g_winMs, g_capMs);
	else Log("physlock: timing DestroyPhysicalEntity (Main) and AddGeometry (other threads) in the level load");
}
