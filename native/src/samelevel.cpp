// The save load onto the level already loaded: see samelevel.h.
#include "samelevel.h"
#include "log.h"
#include "readiness.h"
#include "MinHook.h"
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstring>

namespace {

// ---- WHGame.dll 1.5.6 (decompile 2026-10-24; bytes read from the installed DLL)
constexpr uintptr_t kRvaLoadSave = 0x25bdc7c;   // SaveGameManager: load (user, save); the level compare
const unsigned char kLoadSaveBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x44,0x89,0x44,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41 };
constexpr uintptr_t kRvaSameLevel = 0x25bda54;  // the same-level path: read into the loaded world, PostLoadGame, gate
const unsigned char kSameLevelBytes[] = { 0x48,0x89,0x5c,0x24,0x10,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24 };
constexpr uintptr_t kRvaLevelMgr = 0xfc3868;    // the level manager's state machine, every game update
const unsigned char kLevelMgrBytes[] = { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x41,0x56,0x41,0x57,0x48 };

// 8 integer arguments passed through (no float arguments in these three)
using Fn8 = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
#define ARGS8 uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h
#define PASS8 a, b, c, d, e, f, g, h

uintptr_t g_base = 0;
long long g_freq = 1;
Fn8 g_origLoad = nullptr, g_origSame = nullptr, g_origMgr = nullptr;
std::atomic<uint64_t> g_mgr{0};
std::atomic<uint32_t> g_user{0}, g_save{0};
std::atomic<bool> g_pending{false};
std::atomic<unsigned> g_loadSeq{0};
thread_local int t_depth = 0;
thread_local bool t_same = false;
long long g_replayAt = 0;

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long t) { return (double)t * 1000.0 / g_freq; }

uint64_t __fastcall HkLoad(ARGS8) {   // a = the manager, b = user, c = save
	g_loadSeq++;
	const uintptr_t from = (uintptr_t)_ReturnAddress() - g_base;
	g_mgr = a; g_user = (uint32_t)b; g_save = (uint32_t)c;
	const bool outer = t_depth++ == 0;
	if (outer) t_same = false;
	const long long t = Now();
	const uint64_t r = g_origLoad(PASS8);
	t_depth--;
	Log("samelevel: load save (user %u, save %u) from +%llx: %s, %.0f ms on the main thread, result %u (level state %d)",
	    (uint32_t)b, (uint32_t)c, (unsigned long long)from, t_same ? "SAME LEVEL (no map, no LoadLevel)" :
	    "the level manager's load (map + LoadLevel)", Ms(Now() - t), (uint32_t)(uint8_t)r, Readiness_LevelState());
	return r;
}

uint64_t __fastcall HkSame(ARGS8) {
	t_same = true;
	const long long t = Now();
	const uint64_t r = g_origSame(PASS8);
	Log("samelevel: same-level path: save read into the loaded world + PostLoadGame in %.0f ms, result %u%s", Ms(Now() - t),
	    (uint32_t)(uint8_t)r, g_replayAt ? " (the replay; the readiness gate's open line ends it)" : "");
	return r;
}

uint64_t __fastcall HkMgr(ARGS8) {
	const uint64_t r = g_origMgr(PASS8);
	if (g_pending && Readiness_LevelState() == 0 && g_mgr) {
		g_pending = false;
		Log("samelevel: replaying the last load (user %u, save %u) on the level that is loaded", g_user.load(), g_save.load());
		g_replayAt = Now();
		g_origLoad(g_mgr, g_user, g_save, 0, 0, 0, 0, 0);   // through the original: the hook's log is for the game's calls
		Log("samelevel: replay call returned after %.0f ms", Ms(Now() - g_replayAt));
	}
	return r;
}

bool Hook(uintptr_t rva, const unsigned char* bytes, size_t n, void* detour, void** orig, const char* what) {
	void* p = (void*)(g_base + rva);
	if (memcmp(p, bytes, n)) { Log("samelevel: %s: code differs - not hooked", what); return false; }
	if (MH_CreateHook(p, detour, orig) != MH_OK || MH_EnableHook(p) != MH_OK) { Log("samelevel: %s: hook FAILED", what); return false; }
	return true;
}

bool g_on = false;

}  // namespace

void Samelevel_Install(uintptr_t base) {
	g_base = base;
	LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); g_freq = fq.QuadPart;
	g_on = Hook(kRvaLoadSave, kLoadSaveBytes, sizeof kLoadSaveBytes, (void*)&HkLoad, (void**)&g_origLoad, "save load") &&
	       Hook(kRvaSameLevel, kSameLevelBytes, sizeof kSameLevelBytes, (void*)&HkSame, (void**)&g_origSame, "same-level path") &&
	       Hook(kRvaLevelMgr, kLevelMgrBytes, sizeof kLevelMgrBytes, (void*)&HkMgr, (void**)&g_origMgr, "level manager");
	Log("samelevel: %s", g_on ? "save loads logged (`samelevel load` replays the last one)" : "OFF");
}

unsigned Samelevel_LoadSeq() { return g_loadSeq.load(); }

void Samelevel_Command(const char* args) {
	if (!g_on) { Log("samelevel: off (hooks not set)"); return; }
	if (!strncmp(args, "load", 4)) {
		if (!g_mgr) { Log("samelevel: no save load seen yet"); return; }
		g_pending = true;
		Log("samelevel: replay of the last load (user %u, save %u) asked; it runs on the next level-manager update in game",
		    g_user.load(), g_save.load());
		return;
	}
	Log("samelevel: last load user %u, save %u%s", g_user.load(), g_save.load(), g_pending ? " (replay pending)" : "");
}
