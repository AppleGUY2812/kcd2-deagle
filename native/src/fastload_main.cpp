// fastload: KCD2 load-time speed-ups (save load, quest build, streaming, fast travel), a plugin of its own:
// Bin/Win64MasterMasterSteamPGO/fastload.asi, loaded by the ASI loader (dinput8.dll) with or without the Deagle mod.
// Its settings and records are fastload_*.txt / .bin / .csv next to it, its log fastload.log (docs/LOADTIME.md).
// What it needs from the game besides the modules' own hooks:
//   - a main-thread tick once a frame: CSystem::Update (vtable slot +0x28, FUN_1806678c4, no direct callers) patched in
//     the vtable, so it shares no inline hook with deagle_native.asi or kcd2_render.asi (RenderEnd, slot +0x58);
//   - "gameplay started": the level manager's state back to 0 after a load (0.2-0.5 s before Lua's OnGameplayStarted
//     on boots 41/42), or the "loaded" command, whichever comes first, once a load;
//   - commands: Fastload_Command (exported). deagle_native.asi passes the load commands from its Lua channel
//     (deagle_questbench, deagle_hitch ...) here; without the Deagle mod there are none (settings files only).
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "MinHook.h"
#include "log.h"
#include "scan.h"
#include "ftpreload.h"
#include "fastzip.h"
#include "streamio.h"
#include "streamahead.h"
#include "readiness.h"
#include "hitch.h"
#include "ftplaces.h"
#include "pakahead.h"
#include "cvars.h"
#include "xmlahead.h"
#include "loadwatch.h"
#include "applyfast.h"
#include "conceptbfs.h"
#include "stormmemo.h"
#include "questmenu.h"
#include "animahead.h"
#include "questprof.h"
#include "questdump.h"
#include "questreplay.h"
#include "questfast.h"

namespace {

HMODULE g_self = nullptr;
Module g_game;

// CSystem's vtable and its Update (WHGame 1.5.6): the slot must hold the function and the function must start as below.
constexpr uintptr_t kRvaSystemVt = 0x3dc2858, kSlotUpdate = 0x28, kRvaUpdate = 0x6678c4;
const unsigned char kUpdateBytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x55 };
using UpdateFn = uintptr_t(__fastcall*)(void* sys, unsigned flags, int pause);
UpdateFn g_origUpdate = nullptr;
std::atomic<DWORD> g_mainTid{0};
std::atomic<unsigned long long> g_ticks{0};

// Tick state, main thread only.
bool g_world = false;           // the level manager has reached state 4 once: the modules' frame calls start there,
                                // where deagle_native's camera hook used to start them (the first player view)
bool g_loading = false;         // a level load runs (state 1-9 seen since the last 0)
std::atomic<bool> g_firedThisLoad{false};   // "gameplay started" sent for the current load (Lua alone when the state is unreadable)

bool ReadPtr(uintptr_t a, uintptr_t& out) {
	__try { out = *(const uintptr_t*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadBytes(uintptr_t a, void* out, size_t n) {
	__try { memcpy(out, (const void*)a, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void GameplayStarted(const char* why) {
	if (g_firedThisLoad.exchange(true)) return;
	Log("fastload: gameplay started (%s)", why);
	Cvars_OnLoaded();
	Streamahead_OnLoaded();
	Applyfast_OnLoaded();
}

void Tick() {
	const int s = Readiness_LevelState();
	if (s >= 4) g_world = true;
	if (s >= 1) { if (!g_loading) { g_loading = true; g_firedThisLoad = false; } }
	else if (s == 0 && g_loading) { g_loading = false; GameplayStarted("level state 0"); }
	if (!g_world) return;
	Hitch_Frame();      // a frame: the long-frame sampler measures from here (hitch.cpp)
	Ftplaces_Frame();   // the fast-travel arrival's random-event places, loaded ahead (ftplaces.cpp)
	Ftpre_Frame();      // fast-travel preload (ftpreload.cpp): the destination streams in while the map piece walks
	Readiness_Frame();  // what holds a loading / black screen, and "let me in early" (readiness.cpp)
}

uintptr_t __fastcall HkUpdate(void* sys, unsigned flags, int pause) {
	const uintptr_t r = g_origUpdate(sys, flags, pause);
	DWORD want = 0;
	const DWORD me = GetCurrentThreadId();
	if (g_mainTid.compare_exchange_strong(want, me)) Log("fastload: frame tick on thread %lu", me);
	if (want == 0 || want == me) { g_ticks++; Tick(); }
	return r;
}

bool PatchUpdate() {
	void** slot = (void**)(g_game.base + kRvaSystemVt + kSlotUpdate);
	uintptr_t cur = 0;
	unsigned char head[sizeof(kUpdateBytes)] = {};
	if (!ReadPtr((uintptr_t)slot, cur)) { Log("fastload: CSystem vtable unreadable: no frame tick"); return false; }
	const bool ours = cur == g_game.base + kRvaUpdate;
	const bool foreign = cur && (cur < g_game.base || cur >= g_game.base + g_game.size);   // another mod's patch: chain it
	if (!ours && !foreign) { Log("fastload: CSystem::Update slot holds %p, not the expected function: no frame tick", (void*)cur); return false; }
	if (ours && (!ReadBytes(cur, head, sizeof(head)) || memcmp(head, kUpdateBytes, sizeof(head)))) {
		Log("fastload: CSystem::Update has other bytes (another game version?): no frame tick");
		return false;
	}
	DWORD old = 0;
	if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) { Log("fastload: VirtualProtect failed: no frame tick"); return false; }
	g_origUpdate = (UpdateFn)cur;
	InterlockedExchangePointer(slot, (void*)&HkUpdate);
	VirtualProtect(slot, sizeof(void*), old, &old);
	Log("fastload: frame tick on CSystem::Update (vtable +0x%llx%s)", (unsigned long long)kSlotUpdate,
	    foreign ? ", chained to another mod's patch" : "");
	return true;
}

// gEnv, as deagle_native finds it (main.cpp FindEnv): two RIP-relative patterns, the one whose +0x108 renderer has the
// D3D renderer's RTTI. Only the fast-travel preload needs it.
bool ValidEnv(uintptr_t env) {
	uintptr_t rnd = 0, vt = 0;
	if (!ReadPtr(env + 0x108, rnd) || !rnd || !ReadPtr(rnd, vt)) return false;
	return RttiName(g_game, vt).find("Renderer") != std::string::npos;
}

uintptr_t FindEnv() {
	uintptr_t cands[2] = {};
	if (uintptr_t a = FindUnique(g_game, "48 8D 05 ?? ?? ?? ?? 48 89 0F 4C 8D 67 28 48 8D 0D ?? ?? ?? ?? 48 89 47 20 48 89 4F 08"))
		cands[0] = RipTarget(a, 3, 7);
	if (uintptr_t b = FindUnique(g_game, "48 8B 51 08 48 8B C8 FF D2 48 8B 0D ?? ?? ?? ?? 48 85 C9"))
		cands[1] = RipTarget(b + 9, 3, 7);
	for (uintptr_t c : cands) {
		if (!c) continue;
		if (ValidEnv(c)) return c;
		uintptr_t d = 0;
		if (ReadPtr(c, d) && d && ValidEnv(d)) return d;
	}
	return 0;
}

DWORD WINAPI StateWatch(LPVOID) {   // the level state's changes every 10 ms, frames or not (readiness.cpp)
	for (;;) { Readiness_WatchState(); Sleep(10); }
}

DWORD WINAPI Init(LPVOID) {
	Log("fastload v1 loaded");
	for (int i = 0; i < 600 && !GetModuleHandleA("WHGame.dll"); i++) Sleep(100);
	g_game = GetModule("WHGame.dll");
	if (!g_game.base) { Log("WHGame.dll not found"); return 0; }
	Log("WHGame.dll at %p size %zu", (void*)g_game.base, g_game.size);
	if (MH_Initialize() != MH_OK) { Log("MinHook init failed"); return 0; }
	Cvars_Install(g_game.base, g_self);      // load-time game settings at registration (cvars.cpp)
	Loadwatch_Install(g_game.base);   // exact terrain-step timing (loadwatch.cpp)
	Applyfast_Install(g_game.base, g_self);   // faster pieces of the save load's apply step (applyfast.cpp)
	Conceptbfs_Install(g_game.base, g_self);   // the concept graph's name lookup without its deque (conceptbfs.cpp)
	Stormmemo_Install(g_game.base, g_self);   // Storm's named condition sets remembered per character (stormmemo.cpp)
	Xmlahead_Install(g_game.base, g_self);   // parses the load's XML files ahead on idle threads (xmlahead.cpp)
	Questmenu_Install(g_game.base, g_self);   // the quest graph built at the main menu, kept by the first load (questmenu.cpp)
	Questfast_Install(g_game.base, g_self);   // the quest graph build made faster (questfast.cpp)
	Questreplay_Install(g_game.base);   // phase 2a record / replay: the menu build, questbench N rec (questreplay.cpp); before questprof
	Questprof_Install(g_game.base);   // the quest build's inner profile for questbench N prof (questprof.cpp)
	Questdump_Install(g_game.base);   // the quest graph dump for questbench N dump (questdump.cpp)
	Animahead_Install(g_game.base, g_self);   // human animation sets during the terrain wait (animahead.cpp)
	Pakahead_Install(g_game.base, g_self);   // records which pak entries the load reads, in order (pakahead.cpp)
	Fastzip_Install(g_game.base, g_self);   // libdeflate for the pak unpack + crc32 (fastzip.cpp): before the level load
	Streamio_Install(g_game.base, g_self);  // one read per streaming request instead of 128 KB pieces (streamio.cpp)
	Ftplaces_Install(g_game.base, g_self);   // fast-travel arrival places loaded ahead, mode file (ftplaces.cpp)
	Hitch_Install(g_game.base, g_game.size, g_self);   // samples the main thread in frames over 250 ms (hitch.cpp)
	Readiness_Install(g_game.base);   // logs which streaming task holds a loading / black screen: save load, fast travel (readiness.cpp)
	PatchUpdate();
	CreateThread(nullptr, 0, StateWatch, nullptr, 0, nullptr);
	uintptr_t env = 0;
	for (int i = 0; i < 240 && !env; i++) { env = FindEnv(); if (!env) Sleep(500); }
	Log("gEnv %p", (void*)env);
	if (env) Ftpre_Install(env, g_game.base);   // fast-travel preload (ftpreload.cpp)
	Sleep(60000);
	if (!g_ticks.load()) Log("fastload: no frame tick in the first minutes (CSystem::Update never called through its slot): "
	                         "hitch, readiness, ftplaces and ftpre are idle this session");
	return 0;
}

}  // namespace

// One command line ("questbench 3", "hitch off" ...): 1 if it is a fastload command, 0 if not. Main thread (Lua).
extern "C" __declspec(dllexport) int Fastload_Command(const char* line) {
	char cmd[32] = {};
	if (!line || sscanf_s(line, "%31s", cmd, (unsigned)sizeof(cmd)) != 1) return 0;
	const char* rest = line + strlen(cmd);
	while (*rest == ' ') rest++;
	if (!strcmp(cmd, "ftpre")) Ftpre_Command(rest);
	else if (!strcmp(cmd, "fastzip")) Fastzip_Command(rest);
	else if (!strcmp(cmd, "streamio")) Streamio_Command(rest);
	else if (!strcmp(cmd, "readiness")) Readiness_Command(rest);
	else if (!strcmp(cmd, "hitch")) Hitch_Command(rest);
	else if (!strcmp(cmd, "ftplaces")) Ftplaces_Command(rest);
	else if (!strcmp(cmd, "pakahead")) Pakahead_Command(rest);
	else if (!strcmp(cmd, "cvars")) Cvars_Command(rest);
	else if (!strcmp(cmd, "xmlahead")) Xmlahead_Command(rest);
	else if (!strcmp(cmd, "questbench")) Questmenu_Bench(rest);   // rebuild the quest graph N times at the menu (questmenu.h)
	else if (!strcmp(cmd, "loaded")) GameplayStarted("Lua loaded");   // a no-op when the level state already said so
	else if (!strcmp(cmd, "streamahead")) Streamahead_Command(rest);
	else return 0;
	return 1;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		g_self = h;
		DisableThreadLibraryCalls(h);
		LogInit(h);
		CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
	}
	return TRUE;
}
