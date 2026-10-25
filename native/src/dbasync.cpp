// The dialogue dubbing database loaded on a worker: see dbasync.h.
#include "dbasync.h"
#include "levelprof.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr int kTypeDubbing = 0xf;
constexpr uintptr_t kRvaTypeLoad = 0xef3e9c;   // char (manager, int type, char reload): that type's tables loaded
const unsigned char kTypeLoadBytes[] = { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x54,0x41,0x56 };
constexpr uintptr_t kRvaRegLoad = 0xef3afc;    // char (manager, int type): that type's new tables registered, then loaded
const unsigned char kRegLoadBytes[] = { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x55 };
constexpr uintptr_t kRvaOneLoad = 0xef5668;    // char (manager, table, char): one table loaded
const unsigned char kOneLoadBytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x57,0x48,0x83,0xec,0x40,0x41,0x8a,0xd8,0x48 };
constexpr uintptr_t kRvaFatal = 0x1ab5650;     // void (int code, const char* fmt, ...): the game's error box
const unsigned char kFatalBytes[] = { 0x48,0x89,0x54,0x24,0x10,0x4c,0x89,0x44,0x24,0x18,0x4c,0x89,0x4c,0x24,0x20,0x53 };

using TypeLoadFn = char(__fastcall*)(void* mgr, int type, char reload);
using RegLoadFn = char(__fastcall*)(void* mgr, int type);
using OneLoadFn = char(__fastcall*)(void* mgr, void* table, char a);
using FatalFn = void(__cdecl*)(int code, const char* fmt, ...);
TypeLoadFn g_origType = nullptr;
RegLoadFn g_origReg = nullptr;
OneLoadFn g_origOne = nullptr;
FatalFn g_fatal = nullptr;

DWORD g_mainTid = 0;
std::atomic<bool> g_on{ false }, g_started{ false }, g_reported{ false }, g_framed{ false };
std::atomic<DWORD> g_workerTid{ 0 };
HANDLE g_done = nullptr;
void* g_mgr = nullptr;
volatile char g_result = 1;
long long g_freq = 1, g_t0 = 0, g_t1 = 0;
double g_waitedMs = 0;
const char* g_waitedAt = nullptr;
std::atomic<bool> g_failShown{ false };

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long a, long long b) { return (double)(b - a) * 1000.0 / g_freq; }

char g_reload = 0;
DWORD WINAPI Worker(LPVOID) {
	g_workerTid = GetCurrentThreadId();
	g_result = g_origType(g_mgr, kTypeDubbing, g_reload);
	g_t1 = Now();
	SetEvent(g_done);
	return 0;
}

char __fastcall HkTypeLoad(void* mgr, int type, char reload) {
	// the process's first load of the type, whichever path: at start-up FUN_180ef3afc skips it while g_languageAudio is
	// empty, and the language-audio event (0x100e in FUN_180ef37d8) loads it with reload 1 (boot 106)
	if (type == kTypeDubbing && g_on && GetCurrentThreadId() == g_mainTid && !g_started.exchange(true)) {
		g_mgr = mgr;
		g_reload = reload;
		Log("dbasync: the dubbing database load (reload %d) handed to a worker", (int)reload);
		g_t0 = Now();
		HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
		if (t) {
			CloseHandle(t);
			return 1;   // FUN_180ef3afc goes on as after a good load; a failure is shown at the wait (Dbasync_Gate)
		}
		Log("dbasync: no worker thread - the dubbing database loads on Main");
		SetEvent(g_done);
		g_started = false;
		g_on = false;
	}
	Dbasync_Gate("a database type load");
	return g_origType(mgr, type, reload);
}
void RegGate() { Dbasync_Gate("a database type registration"); }
char __fastcall HkRegLoad(void* mgr, int type) {
	Dbasync_Gate("a database type registration");
	return g_origReg(mgr, type);
}
char __fastcall HkOneLoad(void* mgr, void* table, char a) {
	Dbasync_Gate("a single table load");
	return g_origOne(mgr, table, a);
}

bool Hook(uintptr_t base, uintptr_t rva, const unsigned char* bytes, size_t n, void* hk, void** orig, const char* what) {
	void* t = (void*)(base + rva);
	if (memcmp(t, bytes, n)) { Log("dbasync: %s differs - off", what); return false; }
	MH_STATUS st = MH_CreateHook(t, hk, orig);
	if (st == MH_OK) st = MH_EnableHook(t);
	if (st != MH_OK) { Log("dbasync: %s hook failed (MinHook %d) - off", what, (int)st); return false; }
	return true;
}

}  // namespace

void Dbasync_Gate(const char* where) {
	if (!g_started || GetCurrentThreadId() == g_workerTid.load()) return;
	if (WaitForSingleObject(g_done, 0) != WAIT_OBJECT_0) {
		const long long a = Now();
		while (WaitForSingleObject(g_done, 5000) != WAIT_OBJECT_0)   // a lock the worker needs held by this thread would hang here
			Log("dbasync: still waiting for the worker's dubbing load at %s (%.0f s)", where, Ms(a, Now()) / 1000.0);
		if (!g_waitedAt) { g_waitedMs = Ms(a, Now()); g_waitedAt = where; }
	}
	if (!g_result && !g_failShown.exchange(true)) {
		Log("dbasync: the dubbing database load failed on the worker - the game's error box now");
		if (g_fatal)
			g_fatal(0x103, "Database system error - tables can't be loaded.\nPossibly caused by outdated or corrupted mods.\nSee kcd.log for more details.");
	}
}

void Dbasync_Frame() {
	if (!g_on) return;
	if (!g_framed.exchange(true)) Dbasync_Gate("the first frame");
	if (g_started && !g_reported && WaitForSingleObject(g_done, 0) == WAIT_OBJECT_0 && !g_reported.exchange(true)) {
		if (g_waitedAt)
			Log("dbasync: the dubbing database loaded on a worker in %.0f ms; Main waited %.0f ms for it at %s", Ms(g_t0, g_t1),
			    g_waitedMs, g_waitedAt);
		else
			Log("dbasync: the dubbing database loaded on a worker in %.0f ms; Main never waited for it", Ms(g_t0, g_t1));
	}
}

void Dbasync_Install(uintptr_t base, void* self, unsigned long mainTid, bool listGateOk) {
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	g_freq = f.QuadPart;
	g_mainTid = mainTid;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	int mode = 1;
	if (FILE* fp = fopen((dir + "fastload_dbasync.txt").c_str(), "r")) {
		if (fscanf(fp, "%d", &mode) != 1) mode = 1;
		fclose(fp);
	}
	if (!mode) { Log("dbasync: off (fastload_dbasync.txt 0)"); return; }
	if (!listGateOk) { Log("dbasync: off (dbprof's list-load hook is off, so that gate is missing)"); return; }
	if (!mainTid) { Log("dbasync: off (no main thread id)"); return; }
	if (memcmp((void*)(base + kRvaFatal), kFatalBytes, sizeof kFatalBytes)) { Log("dbasync: the error box function differs - off"); return; }
	g_fatal = (FatalFn)(base + kRvaFatal);
	g_done = CreateEventA(nullptr, TRUE, FALSE, nullptr);
	if (!g_done) return;
	// levelprof times FUN_180ef3afc and holds its hook: then its hook calls our gate first
	if (Levelprof_SetDbPreCall(&RegGate)) Log("dbasync: the type registration gate runs in levelprof's hook");
	else if (!Hook(base, kRvaRegLoad, kRegLoadBytes, sizeof kRegLoadBytes, (void*)&HkRegLoad, (void**)&g_origReg, "the type registration")) return;
	if (!Hook(base, kRvaOneLoad, kOneLoadBytes, sizeof kOneLoadBytes, (void*)&HkOneLoad, (void**)&g_origOne, "the single table load")) return;
	if (!Hook(base, kRvaTypeLoad, kTypeLoadBytes, sizeof kTypeLoadBytes, (void*)&HkTypeLoad, (void**)&g_origType, "the type load")) return;
	g_on = true;
	Log("dbasync: on (the dubbing database, type 0xf, loads on a worker; Main waits for it at its next database load or "
	    "first frame; fastload_dbasync.txt)");
}
