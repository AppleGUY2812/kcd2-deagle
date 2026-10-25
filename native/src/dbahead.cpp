// The SmartObjectAnimations table loaded in the level-start database job: see dbahead.h.
#include "dbahead.h"
#include "early.h"
#include "levelprof.h"
#include "log.h"
#include "readiness.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// ---- WHGame.dll 1.5.6 (decompile 2026-10-24; bytes read from the installed DLL)
constexpr uintptr_t kRvaJob = 0x1e3a140;       // the level-start job's body: types 0xd, then 0xe unless the editor runs
const unsigned char kJobBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xba,0x0d,0x00,0x00,0x00,0x48,0x8b };
constexpr uintptr_t kRvaLoadType = 0xef3afc;   // C_ObjectDatabaseManager: every database of a type (levelprof times it)
const unsigned char kLoadTypeBytes[] = { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89 };
constexpr uintptr_t kRvaSoaInner = 0xd38d8c;   // the table's load, called with the game-profile map filled and its flag set
const unsigned char kSoaInnerBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8 };
constexpr uintptr_t kRvaConv = 0x74cc6c;       // ProfileId: optional<string> -> ushort game profile id (0xffff unknown)
const unsigned char kConvBytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x18,0x48,0x89,0x70,0x20,0x57 };
constexpr uintptr_t kRvaReadA = 0x5d0f48;      // the two readers of the table's index (+0x80 map of the global at 0x185338890)
const unsigned char kReadABytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x44,0x89,0x48,0x20,0x55,0x56,0x57,0x41,0x54 };
constexpr uintptr_t kRvaReadB = 0xf8a1e4;
const unsigned char kReadBBytes[] = { 0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x4c,0x24,0x08,0x55,0x56,0x57,0x41,0x54,0x41 };
// The AI module's level-load step FUN_180d38160 (a virtual called from FUN_180b43f38, right after LoadEntities) fills the
// game profiles in its loop over FUN_180d38e40 (boot 129: 0 -> 879 in 5 ms), then calls FUN_180d364cc (its one caller,
// +0xc5) for the navigation agents, then the navmesh tiles (0.6 s). Boot 130 started the worker when the whole step
// returned: after the navmesh, so Main still waited 1.09 s. Now at FUN_180d364cc's entry: the fill loop is done.
constexpr uintptr_t kRvaProfiles = 0xd364cc;
const unsigned char kProfilesBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57 };
constexpr uintptr_t kRvaSystem = 0x492d8c0;    // ISystem*: vt+0x698 = the editor runs
constexpr uintptr_t kRvaProfileMap = 0x54dc150, kRvaProfileFlag = 0x5495f50;   // the load's name -> id map (size at +0x10), its flag

// Every hook passes 8 integer arguments through (none of these takes a float), so an argument the decompile misses on
// the stack still reaches the game (terrainfast's 5-of-6 async load, boots 52-53).
using Fn8 = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
#define ARGS8 uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h
#define PASS8 a, b, c, d, e, f, g, h
using LoadTypeFn = char(__fastcall*)(uintptr_t mgr, int type);
using BoolFn = char(__fastcall*)(uintptr_t);

uintptr_t g_base = 0;
int g_mode = 3;   // fastload_dbahead.txt: 3 (default since boots 131-133), 1, 0 the game's order
long long g_freq = 1;
Fn8 g_origJob = nullptr, g_origSoa = nullptr, g_origConv = nullptr, g_origReadA = nullptr, g_origReadB = nullptr;
Fn8 g_origProfiles = nullptr;
LoadTypeFn g_loadType = nullptr;

// Mode 3: the table loads on our worker as soon as the profiles are filled; the game's own type loads wait for it.
std::atomic<uintptr_t> g_mgr{0};       // C_ObjectDatabaseManager, from the level-start job
std::atomic<bool> g_armed{false};      // set by the level-start job (once per LoadLevel), taken by the profile step
std::atomic<int> g_worker{0};          // 0 none, 1 loading, 2 done
std::atomic<DWORD> g_workerTid{0};
HANDLE g_done = nullptr;               // manual reset: set when the worker is done
std::atomic<long long> g_wStart{0}, g_wEnd{0}, g_gateTicks{0}, g_gateWaits{0};
std::atomic<uint32_t> g_wUnknown{0};
std::atomic<bool> g_wOk{false};

std::atomic<bool> g_jobLoading{false};
thread_local bool t_in = false;
thread_local uint64_t t_hash = 0;
thread_local uint32_t t_conv = 0, t_unknown = 0;
std::atomic<long long> g_otherConv{0}, g_otherUnknown{0};
std::atomic<long long> g_reads[2][3];        // [reader][0 while the job loads the table, 1 rest of the level load, 2 after]
std::atomic<DWORD> g_readTid[2][3];

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long t) { return (double)t * 1000.0 / g_freq; }

uint64_t __fastcall HkJob(ARGS8) {   // a = the job's lambda: +8 the database manager
	const uintptr_t mgr = *(uintptr_t*)(a + 8);
	const uint64_t r = g_origJob(PASS8);
	if (g_mode != 1 && g_mode != 3) return r;
	const uintptr_t sys = *(uintptr_t*)(g_base + kRvaSystem);
	if (!sys || ((BoolFn)(*(uintptr_t**)sys)[0x698 / 8])(sys)) return r;   // the editor: the game's order
	if (g_mode == 3) {   // armed for this level load; the profile step starts the worker
		if (g_worker.load() == 1) return r;   // a worker from an earlier load still runs (can't happen: the gate waits for it)
		g_mgr = mgr;
		g_worker = 0;
		g_armed = true;
		return r;
	}
	g_jobLoading = true;
	const long long t = Now();
	const char ok = g_loadType(mgr, 0x11);
	g_jobLoading = false;
	Log("dbahead: SmartObjectAnimations (type 0x11) loaded in the level-start job in %.0f ms (thread %lu, %s); level-load end "
	    "finds it loaded", Ms(Now() - t), GetCurrentThreadId(), ok ? "OK" : "FAILED");
	return r;
}

uint64_t __fastcall HkSoaInner(ARGS8) {   // a = the table
	t_in = true; t_hash = 1469598103934665603ull; t_conv = 0; t_unknown = 0;
	const size_t profiles = *(size_t*)(g_base + kRvaProfileMap + 0x10);
	const long long t = Now();
	const uint64_t r = g_origSoa(PASS8);
	// the game clears the map, then drops this flag: a conversion on another thread in between would find no profile.
	// Only when the table loads off Main (mode 1); mode 0 leaves the game's order untouched.
	if (g_jobLoading) *(volatile char*)(g_base + kRvaProfileFlag) = 0;
	t_in = false;
	if (g_jobLoading) g_wUnknown = t_unknown;
	Log("dbahead: SmartObjectAnimations parsed on thread %lu%s in %.0f ms: %zu game profiles, %u ProfileId conversions (%u unknown), "
	    "hash %016llx%s", GetCurrentThreadId(), !g_jobLoading ? "" : g_mode == 3 ? " (our worker)" : " (the level-start job)",
	    Ms(Now() - t), profiles, t_conv, t_unknown, (unsigned long long)t_hash, (uint8_t)r ? ", load FAILED" : "");
	return r;
}

DWORD WINAPI SoaWorker(void*) {   // mode 3: the table's whole type load, as the game's level-end call would do it
	g_workerTid = GetCurrentThreadId();
	Early_NameThread(L"JobWorker_fastload dbahead");
	g_jobLoading = true;
	const char ok = g_loadType(g_mgr.load(), 0x11);
	g_jobLoading = false;
	g_wOk = ok != 0;
	g_wEnd = Now();
	g_worker = 2;
	SetEvent(g_done);
	return 0;
}

void StartWorker();
uint64_t __fastcall HkProfiles(ARGS8) {   // Main, FUN_180d364cc: the profile fill loop just ended
	StartWorker();
	return g_origProfiles(PASS8);
}

void StartWorker() {
	if (g_mode == 3 && g_armed.exchange(false)) {
		const long long n = Levelprof_GameProfiles();
		const int st = Readiness_LevelState();
		if (n <= 0 || !g_mgr.load() || st < 1 || st > 4) {
			Log("dbahead: SmartObjectAnimations not started early: %lld game profiles after the profile step, level state %d - the "
			    "game's order", n, st);
			return;
		}
		ResetEvent(g_done);
		g_wStart = Now();
		g_worker = 1;   // before the thread exists: a type load from now on waits for it
		if (HANDLE th = CreateThread(nullptr, 0, &SoaWorker, nullptr, 0, nullptr)) CloseHandle(th);
		else { g_worker = 0; SetEvent(g_done); Log("dbahead: worker thread not created - the game's order"); }
	}
}

void Gate(uint32_t type) {   // every object-database type load (levelprof's hook): wait while our worker loads the table
	if (g_worker.load() != 1 || GetCurrentThreadId() == g_workerTid.load()) return;
	const long long t = Now();
	const DWORD w = WaitForSingleObject(g_done, 60000);
	const long long dt = Now() - t;
	g_gateTicks += dt; g_gateWaits++;
	Log("dbahead: type 0x%x load on thread %lu waited %.0f ms for our worker%s", type, GetCurrentThreadId(), Ms(dt),
	    w == WAIT_OBJECT_0 ? "" : " - TIMED OUT after 60 s, went on");
}

uint64_t __fastcall HkConv(ARGS8) {   // a = ushort* out, b = optional<CryString>* (frees the string)
	uint16_t* const out = (uint16_t*)a;
	const uintptr_t* const opt = (const uintptr_t*)b;
	const bool has = opt && *(const char*)(opt + 1) != 0;
	if (t_in) {
		uint64_t h = t_hash;
		if (has) for (const char* s = (const char*)opt[0]; s && *s; s++) h = (h ^ (uint8_t)*s) * 1099511628211ull;
		const uint64_t r = g_origConv(PASS8);
		if (has) { h = (h ^ *out) * 1099511628211ull; t_conv++; if (*out == 0xffff) t_unknown++; }
		t_hash = h;
		return r;
	}
	const bool flag = *(volatile const char*)(g_base + kRvaProfileFlag) != 0;
	const uint64_t r = g_origConv(PASS8);
	if (flag && has) { g_otherConv++; if (*out == 0xffff) g_otherUnknown++; }
	return r;
}

void Count(int which) {
	const int s = Readiness_LevelState();
	const int w = g_jobLoading ? 0 : (s >= 1 && s <= 4) ? 1 : 2;
	if (g_reads[which][w]++ == 0) g_readTid[which][w] = GetCurrentThreadId();
}
uint64_t __fastcall HkReadA(ARGS8) { Count(0); return g_origReadA(PASS8); }
uint64_t __fastcall HkReadB(ARGS8) { Count(1); return g_origReadB(PASS8); }

bool Hook(uintptr_t rva, const unsigned char* bytes, size_t n, void* detour, void** orig, const char* what) {
	void* p = (void*)(g_base + rva);
	if (memcmp(p, bytes, n)) { Log("dbahead: %s: code differs - not hooked", what); return false; }
	if (MH_CreateHook(p, detour, orig) != MH_OK || MH_EnableHook(p) != MH_OK) { Log("dbahead: %s: hook FAILED", what); return false; }
	return true;
}

}  // namespace

void Dbahead_OnLoaded() {
	Log("dbahead: readers of the table: FUN_1805d0f48 %lld / %lld / %lld, FUN_180f8a1e4 %lld / %lld / %lld (while the job loads it / "
	    "rest of the level load / after; first threads %lu %lu %lu, %lu %lu %lu); other threads' ProfileId conversions while the "
	    "profile-map flag was set %lld (%lld unknown)", g_reads[0][0].load(), g_reads[0][1].load(), g_reads[0][2].load(),
	    g_reads[1][0].load(), g_reads[1][1].load(), g_reads[1][2].load(), g_readTid[0][0].load(), g_readTid[0][1].load(),
	    g_readTid[0][2].load(), g_readTid[1][0].load(), g_readTid[1][1].load(), g_readTid[1][2].load(), g_otherConv.load(),
	    g_otherUnknown.load());
	for (int i = 0; i < 2; i++) for (int w = 0; w < 3; w++) { g_reads[i][w] = 0; g_readTid[i][w] = 0; }
	g_otherConv = 0; g_otherUnknown = 0;
	if (g_mode == 3 && g_wStart.load()) {
		Log("dbahead: SmartObjectAnimations on our worker: %s in %.0f ms (%u unknown ProfileIds%s); the game's type loads waited %lld x, "
		    "%.0f ms in all", g_worker.load() == 2 ? (g_wOk ? "loaded" : "load FAILED") : "STILL RUNNING", Ms(g_wEnd.load() - g_wStart.load()),
		    g_wUnknown.load(), g_wUnknown.load() ? " - WRONG: the profiles were not all there" : "", g_gateWaits.load(), Ms(g_gateTicks.load()));
		g_wStart = 0; g_gateWaits = 0; g_gateTicks = 0;
	}
}

void Dbahead_Install(uintptr_t base, void* self) {
	g_base = base;
	LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); g_freq = fq.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((dir + "fastload_dbahead.txt").c_str(), "r")) {
		int m = 3;
		if (fscanf(fp, "%d", &m) == 1) g_mode = m == 1 || m == 3 ? m : 0;
		fclose(fp);
	}
	g_done = CreateEventA(nullptr, TRUE, TRUE, nullptr);
	if (!g_done && g_mode == 3) g_mode = 0;
	g_loadType = (LoadTypeFn)(base + kRvaLoadType);
	// levelprof may hook the type loader first: then its bytes are a jump; the call goes through its timing either way
	const bool loadType = !memcmp((void*)(base + kRvaLoadType), kLoadTypeBytes, sizeof kLoadTypeBytes) || *(uint8_t*)(base + kRvaLoadType) == 0xe9;
	const bool soa = Hook(kRvaSoaInner, kSoaInnerBytes, sizeof kSoaInnerBytes, (void*)&HkSoaInner, (void**)&g_origSoa, "the table's load");
	const bool conv = soa && Hook(kRvaConv, kConvBytes, sizeof kConvBytes, (void*)&HkConv, (void**)&g_origConv, "ProfileId converter");
	const bool rd = Hook(kRvaReadA, kReadABytes, sizeof kReadABytes, (void*)&HkReadA, (void**)&g_origReadA, "reader A") &&
	                Hook(kRvaReadB, kReadBBytes, sizeof kReadBBytes, (void*)&HkReadB, (void**)&g_origReadB, "reader B");
	if (!loadType || !soa || !conv || !rd) g_mode = 0;   // no job load without the checks
	// mode 3 needs the gate in levelprof's type-load hook (the game's level-end loads wait for our worker) and the profile step
	if (g_mode == 3 && (!Levelprof_SetDbTypeGate(&Gate) ||
	                    !Hook(kRvaProfiles, kProfilesBytes, sizeof kProfilesBytes, (void*)&HkProfiles, (void**)&g_origProfiles, "profile step"))) {
		Levelprof_SetDbTypeGate(nullptr);
		Log("dbahead: mode 3 needs levelprof's hook (fastload_levelprof.txt 1 or 2) and the profile step - the game's order");
		g_mode = 0;
	}
	const bool job = g_mode && Hook(kRvaJob, kJobBytes, sizeof kJobBytes, (void*)&HkJob, (void**)&g_origJob, "level-start job");
	if (!job) g_mode = 0;
	Log("dbahead: mode %d (%s; fastload_dbahead.txt); checks: table load %s, converter %s, readers %s", g_mode,
	    g_mode == 1 ? "SmartObjectAnimations in the level-start database job" :
	    g_mode == 3 ? "SmartObjectAnimations on a worker once the game profiles are filled" : "the game's order", soa ? "on" : "OFF",
	    conv ? "on" : "OFF", rd ? "on" : "OFF");
}
