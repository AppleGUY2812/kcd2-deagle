// The AI smart objects' animation lookups spread over cores: see smartobj.h.
#define NOMINMAX
#include "smartobj.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

// WHGame 1.5.6
constexpr uintptr_t kRvaAll = 0x1096ec8;   // void (mgr*): if mgr+0x48, for each node of the map at mgr+0x10: FUN_181096fdc(node[3])
constexpr uintptr_t kRvaOne = 0x1096fdc;   // void (type*): its fragments' stance ids per actor class into type+0x78
constexpr uintptr_t kRvaCallOne = 0x1096ee8;   // the call of kRvaOne inside kRvaAll
const unsigned char kAllBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x80,0x79,0x48,0x00,0x74,0x1a };
const unsigned char kOneBytes[] = { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x54,0x41,0x56 };
// Each lookup (FUN_180946ec8) first gets its animation database by file name: gEnv-like object (0x18492d890) +0x80 ->
// +0xb0 -> +0x18 -> +0x38 (name), CryEngine's framework -> Mannequin -> database manager -> Load(filename), which loads
// and caches a database the first time. That is the one shared write on the path (the tag checks, the filter and the
// scoring only read the database; the results go to the type's own list), so it runs under a lock while spread.
constexpr uintptr_t kRvaEnv = 0x492d890;
constexpr uintptr_t kRvaChain = 0x946ef4;   // the getter chain inside FUN_180946ec8
const unsigned char kChainBytes[] = { 0x48,0x8b,0x0d,0x95,0x69,0xfe,0x03,0x48,0x8b,0xda,0x48,0x8b,0x01,0xff,0x90,0x80,0x00,0x00,
	0x00,0x48,0x8b,0x08,0x48,0x8b,0x91,0xb0,0x00,0x00,0x00,0x48,0x8b,0xc8,0xff,0xd2,0x48,0x8b,0x08,0x48,0x8b,0x51,0x18,0x48,0x8b,
	0xc8,0xff,0xd2,0x49,0x8b,0x56,0x18,0x48,0x8b,0x08,0x4c,0x8b,0x41,0x38,0x48,0x8b,0xc8,0x41,0xff,0xd0 };

using AllFn = void(__fastcall*)(char* mgr);
using OneFn = void(__fastcall*)(void* type);
AllFn g_origAll = nullptr;
OneFn g_one = nullptr;
uintptr_t g_base = 0;
using GetFn = void*(__fastcall*)(void*);
using LoadFn = void*(__fastcall*)(void*, const char*, uintptr_t, uintptr_t);
LoadFn g_origLoad = nullptr;
CRITICAL_SECTION g_loadCs;   // re-entrant: a database load may load another
std::atomic<bool> g_spread{ false };
std::atomic<long> g_loadsSpread{ 0 };
void* __fastcall HkLoad(void* mgr, const char* name, uintptr_t a, uintptr_t b) {
	if (!g_spread.load(std::memory_order_acquire)) return g_origLoad(mgr, name, a, b);
	g_loadsSpread++;
	EnterCriticalSection(&g_loadCs);
	void* r = g_origLoad(mgr, name, a, b);
	LeaveCriticalSection(&g_loadCs);
	return r;
}
// Main, after the first type ran: the database manager's Load hooked with the lock; false = not spread this start
const char* LockLoad() {
	void* env = *(void**)(g_base + kRvaEnv);
	if (!env) return "no framework object";
	void* fw = ((GetFn)(*(void***)env)[0x80 / 8])(env);
	void* mann = fw ? ((GetFn)(*(void***)fw)[0xb0 / 8])(fw) : nullptr;
	void* dbm = mann ? ((GetFn)(*(void***)mann)[0x18 / 8])(mann) : nullptr;
	if (!dbm) return "no database manager";
	void* load = (*(void***)dbm)[0x38 / 8];
	unsigned char before[5]; memcpy(before, load, 5);
	if (MH_CreateHook(load, (void*)&HkLoad, (void**)&g_origLoad) != MH_OK || MH_EnableHook(load) != MH_OK) return "hook failed";
	if (*(const unsigned char*)load != 0xe9 || !memcmp(before, load, 5)) return "hook not on yet";
	return nullptr;
}
int g_mode = 1, g_threads = 0;   // 1 by default since boot queue 171 (menu -0.5 s)
long long g_freq = 1;
std::atomic<int> g_calls{ 0 }, g_work{ 0 };   // all calls; calls that do the work (the first has the flag at +0x48 off)

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long a, long long b) { return (double)(b - a) * 1000.0 / g_freq; }

void __fastcall HkAll(char* mgr) {
	const int call = g_calls++;
	if (!mgr[0x48]) { g_origAll(mgr); return; }
	const int work = g_work++;
	std::vector<void*> types;
	void** head = *(void***)(mgr + 0x10);
	for (void** n = (void**)*head; n != (void**)head; n = (void**)*n) types.push_back(n[3]);
	const long long t0 = Now();
	std::vector<double> ms(types.size(), 0.0);
	int used = 1;
	const bool spread = g_mode == 1 && work == 0 && types.size() > 2;   // the start-up one only
	if (spread) {
		const long long a = Now();
		g_one(types[0]);   // alone first: what is built on first use gets built here
		ms[0] = Ms(a, Now());
		static const char* why = LockLoad();
		if (why) {
			Log("smartobj: the database lock could not be set (%s): the game's way", why);
			for (size_t i = 1; i < types.size(); i++) { const long long c = Now(); g_one(types[i]); ms[i] = Ms(c, Now()); }
			goto done;
		}
		g_spread = true;
		{
		const int hw = (int)std::max(1u, std::thread::hardware_concurrency());
		const int want = g_threads > 0 ? g_threads : std::min(8, hw - 1);
		const int workers = std::max(0, std::min(want, (int)types.size() - 1) - 1);   // Main is one of them
		std::atomic<size_t> next{ 1 };
		auto run = [&]() {
			for (size_t i; (i = next.fetch_add(1)) < types.size();) {
				const long long b = Now();
				g_one(types[i]);
				ms[i] = Ms(b, Now());
			}
		};
		std::vector<std::thread> pool;
		for (int k = 0; k < workers; k++) pool.emplace_back(run);
		run();
		for (std::thread& th : pool) th.join();
		used = workers + 1;
		}
		g_spread = false;
	} else {
		for (size_t i = 0; i < types.size(); i++) {   // the game's order
			const long long b = Now();
			g_one(types[i]);
			ms[i] = Ms(b, Now());
		}
	}
done:
	const double wall = Ms(t0, Now());
	if (call < 3) {
		double sum = 0, top = 0;
		for (double v : ms) { sum += v; top = std::max(top, v); }
		Log("smartobj: call %d: %zu smart-object types in %.0f ms on %d thread(s)%s (one after another: %.0f ms; the first %.0f ms, "
		    "the slowest %.0f ms; %ld database loads under the lock)", call + 1, types.size(), wall, used,
		    spread ? "" : " (the game's way)", sum, ms.empty() ? 0.0 : ms[0], top, g_loadsSpread.load());
	}
}

}  // namespace

void Smartobj_Install(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	if (FILE* fp = fopen((dir + "fastload_smartobj.txt").c_str(), "r")) {
		if (fscanf(fp, "%d %d", &g_mode, &g_threads) < 1) g_mode = 1;
		fclose(fp);
	}
	if (g_mode != 1 && g_mode != 2) { Log("smartobj: off (fastload_smartobj.txt: 1 spread over cores, 2 timed)"); return; }
	const bool callOk = *(const unsigned char*)(base + kRvaCallOne) == 0xe8 &&
	                    *(const int32_t*)(base + kRvaCallOne + 1) == (int32_t)(kRvaOne - (kRvaCallOne + 5));
	if (memcmp((void*)(base + kRvaAll), kAllBytes, sizeof kAllBytes) || memcmp((void*)(base + kRvaOne), kOneBytes, sizeof kOneBytes) || !callOk) {
		Log("smartobj: the smart-object code differs (another game version?) - off");
		return;
	}
	if (memcmp((void*)(base + kRvaChain), kChainBytes, sizeof kChainBytes)) {
		Log("smartobj: the database lookup differs (another game version?) - off");
		return;
	}
	InitializeCriticalSection(&g_loadCs);
	g_base = base;
	g_one = (OneFn)(base + kRvaOne);
	void* t = (void*)(base + kRvaAll);
	if (MH_CreateHook(t, (void*)&HkAll, (void**)&g_origAll) != MH_OK || MH_EnableHook(t) != MH_OK) {
		Log("smartobj: hook failed - off");
		return;
	}
	Log("smartobj: mode %d (%s)", g_mode, g_mode == 1 ? "the smart objects' animation lookups spread over cores at start-up" : "timed only");
}
