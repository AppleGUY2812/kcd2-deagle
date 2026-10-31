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
void Mapvd_Install(uintptr_t base, HMODULE self);   // mapvd.cpp
void Mapvd_Command(const char* args);
void Perkcopy_Install(uintptr_t base, HMODULE self);   // perkcopy.cpp
void Perkcopy_Command(const char* args);
void Gfxlist_Install(uintptr_t base, HMODULE self);   // gfxlist.cpp
void Menutime_Install(uintptr_t base, HMODULE self);  // menutime.cpp
#include "framelog.h"
void Keepload_Install(uintptr_t base, HMODULE self);  // keepload.cpp
void Keepload_Command(const char* args);
void Keepload_Frame();   // quest log events from other threads, run on the main thread
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
#include "terrainfast.h"
#include "questmenu.h"
#include "animahead.h"
#include "questprof.h"
#include "questdump.h"
#include "questreplay.h"
#include "rttrname.h"
#include "gpuwarm.h"
#include "pipelib.h"
#include "rtsync.h"
#include "audiofast.h"
#include "questfast.h"
#include "levelprof.h"
#include "savewatch.h"
#include "physlock.h"
#include "applyprof.h"
#include "paklock.h"
#include "dbahead.h"
#include "samelevel.h"
#include "readymenu.h"
#include "smartobj.h"
#include "dllinit.h"
#include "startfast.h"
#include "gpumemo.h"
#include "dbprof.h"
#include "tblcache.h"
#include "early.h"
#include "dbasync.h"
#include "zipidx.h"
#include "ngxearly.h"
#include "memsite.h"
#include "crashlog.h"
#include "fastalloc.h"
#include "morphpool.h"

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
	Terrainfast_OnLoaded();
	Fastalloc_Report("gameplay start");
	Zipidx_Report("gameplay start");
	Paklock_Report("LoadLevel end to gameplay");
	Dbahead_OnLoaded();
	Xmlahead_OnLoaded();   // the XML read-ahead list for the next start, from this run (xmlahead.cpp)
}

void Tick() {
	const int s = Readiness_LevelState();
	if (s >= 4) g_world = true;
	Startfast_Frame();   // controller callbacks queued off the main thread (startfast.cpp)
	Cvars_Frame();       // settings the registration hooks never saw, set through the console (cvars.cpp)
	if (s >= 1) { if (!g_loading) { g_loading = true; g_firedThisLoad = false; } }
	else if (s == 0 && g_loading) { g_loading = false; GameplayStarted("level state 0"); }
	Terrainfast_Frame();   // the terrain meshes asked for at the menu (terrainfast.cpp)
	Readymenu_Frame(s);    // the newest save loaded at the menu, then the pause menu (readymenu.cpp)
	Applyprof_SetMainThread(GetCurrentThreadId());   // applyprof counts the main thread apart
	Tblcache_Frame(s);   // new table records written when loading is quiet (tblcache.cpp)
	Dbasync_Frame();     // the first frame waits for the dubbing database's worker (dbasync.cpp)
	Gpuwarm_Frame();     // the driver warm-up's time, once (gpuwarm.cpp)
	Rtsync_Frame();      // the shader-quality settings wait for the render thread again (rtsync.cpp)
	Audiofast_Frame();   // the sound system's start-up requests finished before the game runs on (audiofast.cpp)
	Early_Frame(g_game.base, g_game.size);   // Main from DllMain to the first frame (early.cpp)
	Streamahead_FirstFrame();   // held read-ahead readers start at the first frame (streamahead.cpp)
	Keepload_Frame();   // quest log events NPCs made on the AI thread, played on the main thread (keepload.cpp)
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
	if (want == 0 || want == me) { g_ticks++; const long long ft = Framelog_TickStart(); Tick(); Framelog_TickEnd(ft, Readiness_LevelState()); }
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

// Start-up cost of our own installs (2026-10-24, launch -> menu): each one's time and how long MinHook kept the game's
// threads suspended for it (Freeze suspends every other thread while it patches), logged when >= 20 ms.
extern "C" void WINAPI MH_FreezeStats(ULONGLONG* us, ULONGLONG* count, ULONGLONG* longestUs, ULONG* maxThreads);
// While deferred, MH_EnableHook only queues; MH_ApplyQueued turns the queue on in one freeze (minhook_shared/hook.c).
// Boot 94: 94 single enables kept the game's threads suspended 5.9 s of its start-up.
extern "C" void WINAPI MH_DeferEnables(BOOL defer);
// The freeze stops main last and resumes it first (boots 96-97: main held 1.25 s while the freeze waited for another
// thread to stop); the longest freeze's parts for the log (minhook_shared/hook.c).
extern "C" void WINAPI MH_SetFreezeLast(DWORD tid);
extern "C" void WINAPI MH_FreezeParts(ULONGLONG* enumUs, ULONGLONG* askUs, ULONGLONG* askSlowUs, DWORD* askSlowTid,
                                          ULONGLONG* askSlowStart, ULONGLONG* waitUs);
extern "C" void WINAPI MH_FreezeSlowest(ULONGLONG* totalUs, ULONGLONG* mainHeldUs, ULONGLONG* mainWaitUs, ULONGLONG* patchUs,
                                        ULONGLONG* slowUs, DWORD* slowTid, ULONGLONG* slowRip, ULONGLONG* slowStart);
bool ApplyHooks(const char* which) {
	const MH_STATUS st = MH_ApplyQueued();
	if (st != MH_OK) Log("fastload: turning on the %s hooks failed (MinHook %d)", which, (int)st);
	return st == MH_OK;
}
long long g_initStart = 0;
double MsSince(long long t) {
	LARGE_INTEGER n, f;
	QueryPerformanceCounter(&n);
	QueryPerformanceFrequency(&f);
	return (n.QuadPart - t) * 1000.0 / f.QuadPart;
}
void StepLog(const char* what, long long t0, ULONGLONG us0, ULONGLONG n0) {
	ULONGLONG us = 0, n = 0;
	MH_FreezeStats(&us, &n, nullptr, nullptr);
	const double ms = MsSince(t0);
	if (ms >= 20 || us - us0 >= 20000) {
		char name[64] = {};
		size_t i = 0;
		for (; what[i] && what[i] != '(' && i + 1 < sizeof name; i++) name[i] = what[i];
		Log("fastload: install %s %.0f ms (game threads suspended %.0f ms, %llu times)", name, ms, (us - us0) / 1000.0, n - n0);
	}
}
#define STEP(call) do { LARGE_INTEGER t0_; QueryPerformanceCounter(&t0_); ULONGLONG us0_ = 0, n0_ = 0; \
	MH_FreezeStats(&us0_, &n0_, nullptr, nullptr); call; StepLog(#call, t0_.QuadPart, us0_, n0_); } while (0)

// fastload.asi's DllMain before WHGame.dll's static initializers (DllMain): MinHook up, questfast's sort hooks on
constexpr uintptr_t kRvaAllocInit = 0x549d370;   // WHGame 1.5.6: the allocator's "set up" flag, 0 until its first use
bool g_mhEarly = false;
int g_earlyHooks = -1;   // -1: DllMain ran after the game's start-up code, -2: before WHGame.dll was loaded

DWORD WINAPI Init(LPVOID) {
	{ LARGE_INTEGER t; QueryPerformanceCounter(&t); g_initStart = t.QuadPart; }
	Log("fastload v1 loaded");
	Crashlog_Install(g_self);   // crash-type exceptions to fastload_crash.txt (crashlog.cpp)
	Dllinit_WaitStatic();   // after WHGame's static initializers, as when the loader ran them (dllinit.cpp)
	for (int i = 0; i < 600 && !GetModuleHandleA("WHGame.dll"); i++) Sleep(100);
	g_game = GetModule("WHGame.dll");
	if (!g_game.base) { Log("WHGame.dll not found"); return 0; }
	Log("WHGame.dll at %p size %zu", (void*)g_game.base, g_game.size);
	Early_Report(g_game.base, g_game.size);   // what ran before this thread could start (early.cpp)
	if (!g_mhEarly && MH_Initialize() != MH_OK) { Log("MinHook init failed"); return 0; }
	if (g_earlyHooks >= 0)
		Log("fastload: DllMain ran before WHGame.dll's start-up code (ASI loader DontLoadFromDllMain=0): %d of 2 rttr sort hooks "
		    "on from there%s", g_earlyHooks, g_mhEarly ? "" : " (MinHook init failed there)");
	else if (g_earlyHooks == -2) Log("fastload: DllMain ran before WHGame.dll was loaded: no early sort hooks");
	else Log("fastload: DllMain ran after WHGame.dll's start-up code (the ASI loader's default); global.ini DontLoadFromDllMain=0 "
	         "next to dinput8.dll loads it before");
	Rttrname_Report();
	Gpuwarm_Report();
	MH_SetFreezeLast(Early_MainThread());
	MH_DeferEnables(TRUE);   // the hooks below go on in two freezes, not one each
	// first, so the start-up window samples the game from here (it began after all the installs, ~6.5 s later)
	STEP(Fastalloc_Install(g_game.base, g_self));   // from here if DllMain could not (does nothing when it did; fastalloc.cpp)
	STEP(Zipidx_Install(g_game.base, g_self));   // baked archive indexes, if DllMain could not (zipidx.cpp)
	STEP(Hitch_Install(g_game.base, g_game.size, g_self));   // samples the main thread in frames over 250 ms (hitch.cpp)
	STEP(Memsite_Install(g_self));   // who holds the memory, a diagnostic, off by default (memsite.cpp)
	STEP(Cvars_Install(g_game.base, g_self));      // load-time game settings at registration (cvars.cpp)
	STEP(Startfast_Install(g_game.base, g_self));   // the controller list asked for without waiting (startfast.cpp)
	STEP(Gpumemo_Install(g_game.base, g_game.size, g_self));   // the GPU adapter query asked once, not 3 times (gpumemo.cpp)
	STEP(Ngxearly_Install(g_game.base, g_self));   // NVIDIA NGX (DLSS) started once the device exists, off the render thread (ngxearly.cpp)
	STEP(Pipelib_Install(g_game.base, g_self));   // the D3D12 pipeline library made on a worker (pipelib.cpp)
	STEP(Rtsync_Install(g_game.base, g_self));   // the shader-quality settings don't wait for the render thread (rtsync.cpp)
	STEP(Audiofast_Install(g_game.base, g_self));   // the sound system's start-up requests without Main waiting (audiofast.cpp)
	STEP(Smartobj_Install(g_game.base, g_self));   // the AI smart objects' animation lookups spread over cores (smartobj.cpp)
	STEP(ApplyHooks("early"));   // the settings registered early and the input init come soon: on now
	STEP(Loadwatch_Install(g_game.base));   // exact terrain-step timing (loadwatch.cpp)
	STEP(Terrainfast_Install(g_game.base, g_self));   // the load's stream finalize every 1 ms, not 20 (terrainfast.cpp)
	STEP(Applyfast_Install(g_game.base, g_self));   // faster pieces of the save load's apply step (applyfast.cpp)
	STEP(Conceptbfs_Install(g_game.base, g_self));   // the concept graph's name lookup without its deque (conceptbfs.cpp)
	STEP(Stormmemo_Install(g_game.base, g_self));   // Storm's named condition sets remembered per character (stormmemo.cpp)
	STEP(Xmlahead_Install(g_game.base, g_self));   // parses the load's XML files ahead on idle threads (xmlahead.cpp)
	STEP(Questmenu_Install(g_game.base, g_self));   // the quest graph built at the main menu, kept by the first load (questmenu.cpp)
	STEP(Questfast_Install(g_game.base, g_self));   // the quest graph build made faster (questfast.cpp)
	STEP(Questreplay_Install(g_game.base));   // phase 2a record / replay: the menu build, questbench N rec (questreplay.cpp); before questprof
	STEP(Questprof_Install(g_game.base));   // the quest build's inner profile for questbench N prof (questprof.cpp)
	STEP(Questdump_Install(g_game.base));   // the quest graph dump for questbench N dump (questdump.cpp)
	STEP(Animahead_Install(g_game.base, g_self));   // human animation sets during the terrain wait (animahead.cpp)
	STEP(Levelprof_Install(g_game.base, g_self));   // the level load's main-thread work timed / sampled, mode file (levelprof.cpp)
	STEP(Savewatch_Install(g_game.base, g_self));   // the save writers the load's settle waits for, logged (savewatch.cpp)
	STEP(Physlock_Install(g_game.base, g_self));   // Main's CryPhysics lock spin in LoadEntities, after levelprof (physlock.cpp)
	STEP(Applyprof_Install(g_game.base, g_self));   // the apply step's pieces timed, log only (applyprof.cpp)
	STEP(Paklock_Install(g_game.base, g_self));   // CryPak reads outside the open-files lock, mode file (paklock.cpp)
	STEP(Dbahead_Install(g_game.base, g_self));   // SmartObjectAnimations in the level-start database job (dbahead.cpp)
	STEP(Samelevel_Install(g_game.base));   // save loads onto the loaded level: logged, `samelevel load` replays (samelevel.cpp)
	STEP(Readymenu_Install(g_game.base, g_self));   // the newest save loaded by itself at the menu, off by default (readymenu.cpp)
	STEP(Pakahead_Install(g_game.base, g_self));   // records which pak entries the load reads, in order (pakahead.cpp)
	STEP(Fastzip_Install(g_game.base, g_self));   // libdeflate for the pak unpack + crc32 (fastzip.cpp): before the level load
	STEP(Streamio_Install(g_game.base, g_self));  // one read per streaming request instead of 128 KB pieces (streamio.cpp)
	STEP(Fttex_Install(g_self));       // fast-travel arrival texture caps, mode file (ftpreload.cpp)
	STEP(Ftinstant_Install(g_self));   // fast-travel arrival let in early, mode file (ftpreload.cpp)
	STEP(Ftspeed_Install(g_game.base, g_self));   // fast-travel piece speed independent of the frame rate (ftpreload.cpp)
	STEP(Ftplaces_Install(g_game.base, g_self));   // fast-travel arrival places loaded ahead, mode file (ftplaces.cpp)
	STEP(Mapvd_Install(g_game.base, g_self));      // map / inventory: no octree rebuild (mapvd.cpp)
	STEP(Perkcopy_Install(g_game.base, g_self));   // map / inventory: the perk copy without per-perk re-checks (perkcopy.cpp)
	STEP(Gfxlist_Install(g_game.base, g_self));    // map / inventory: Scaleform clip stops without the list walk (gfxlist.cpp)
	{   // frame rate in play (framelog.cpp)
		char p[MAX_PATH] = {};
		GetModuleFileNameA(g_self, p, MAX_PATH);
		std::string d = p;
		d = d.substr(0, d.find_last_of('\\') + 1);
		STEP(Framelog_Install(d.c_str()));
	}
	STEP(Menutime_Install(g_game.base, g_self));   // map / inventory: each open's steps timed, log only (menutime.cpp)
	STEP(Keepload_Install(g_game.base, g_self));   // map: stays loaded after the menu closes (keepload.cpp)
	bool dbprofOn = false;
	STEP(dbprofOn = Dbprof_Install(g_game.base, g_self));   // the object database tables timed one by one; dbpool (dbprof.cpp)
	STEP(Dbasync_Install(g_game.base, g_self, Early_MainThread(), dbprofOn));   // the dubbing database on a worker (dbasync.cpp)
	STEP(Tblcache_Install(g_game.base, g_self));   // the XML tables recorded once and replayed (tblcache.cpp)
	STEP(Morphpool_Install(g_game.base, g_self));   // the heads' facial morph data parsed on a pool, mode file (morphpool.cpp)
	STEP(Readiness_Install(g_game.base));   // logs which streaming task holds a loading / black screen: save load, fast travel (readiness.cpp)
	STEP(ApplyHooks("start-up"));
	MH_DeferEnables(FALSE);   // hooks made later (commands, ftpre) go on at once again
	STEP(Startfast_TickReady(PatchUpdate()));   // startfast hands controller callbacks over at the frame tick
	CreateThread(nullptr, 0, StateWatch, nullptr, 0, nullptr);
	{
		ULONGLONG us = 0, n = 0, longest = 0;
		ULONG threads = 0;
		MH_FreezeStats(&us, &n, &longest, &threads);
		Log("fastload: start-up installs done in %.0f ms; MinHook suspended the game's threads %llu times, %.0f ms in all "
		    "(longest %.1f ms, up to %lu threads)", MsSince(g_initStart), n, us / 1000.0, longest / 1000.0, threads);
		ULONGLONG tot = 0, held = 0, mwait = 0, patch = 0, slow = 0, rip = 0, start = 0;
		DWORD tid = 0;
		MH_FreezeSlowest(&tot, &held, &mwait, &patch, &slow, &tid, &rip, &start);
		char at[96] = "-", from[96] = "-";
		if (rip) Early_Where(rip, g_game.base, g_game.size, at, sizeof at);
		if (start) Early_Where(start, g_game.base, g_game.size, from, sizeof from);
		Log("fastload: the longest freeze %.1f ms: main (thread %lu) held %.1f ms of it (%.1f ms to stop), patching %.1f ms; "
		    "the slowest other thread to stop was %lu, %.1f ms (at %s, started at %s)", tot / 1000.0, Early_MainThread(),
		    held / 1000.0, mwait / 1000.0, patch / 1000.0, tid, slow / 1000.0, at, from);
		ULONGLONG en = 0, ask = 0, askSlow = 0, askStart = 0, wt = 0;
		DWORD askTid = 0;
		MH_FreezeParts(&en, &ask, &askSlow, &askTid, &askStart, &wt);
		char af[96] = "-";
		if (askStart) Early_Where(askStart, g_game.base, g_game.size, af, sizeof af);
		Log("fastload: that freeze's parts: the thread list %.1f ms, the stop requests %.1f ms (the slowest %.1f ms, thread %lu "
		    "started at %s), waiting for them to stop %.1f ms", en / 1000.0, ask / 1000.0, askSlow / 1000.0, askTid, af, wt / 1000.0);
	}
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
	else if (!strcmp(cmd, "fttex")) Fttex_Command(rest);
	else if (!strcmp(cmd, "ftinstant")) Ftinstant_Command(rest);
	else if (!strcmp(cmd, "ftspeed")) Ftspeed_Command(rest);
	else if (!strcmp(cmd, "mapvd")) Mapvd_Command(rest);
	else if (!strcmp(cmd, "perkcopy")) Perkcopy_Command(rest);
	else if (!strcmp(cmd, "keepload")) Keepload_Command(rest);
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
	else if (!strcmp(cmd, "terrainfast")) Terrainfast_Command(rest);
	else if (!strcmp(cmd, "samelevel")) Samelevel_Command(rest);
	else if (!strcmp(cmd, "readymenu")) Readymenu_Command(rest);
	else if (!strcmp(cmd, "memsite")) Memsite_Command(rest);
	else return 0;
	return 1;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		g_self = h;
		DisableThreadLibraryCalls(h);
		LogInit(h);
		Early_Start(h);   // the time, and a sampler of this thread until Init runs (early.cpp)
		// Loaded inside the ASI loader's DllMain (global.ini DontLoadFromDllMain=0)? Then WHGame.dll is mapped but its
		// static initializers haven't run: its allocator's first-use flag (DAT_18549d370, set by the first allocation) is 0.
		HMODULE wh = GetModuleHandleA("WHGame.dll");
		if (!wh) g_earlyHooks = -2;
		else {
			const uintptr_t b = (uintptr_t)wh;
			const auto* nt = (const IMAGE_NT_HEADERS*)(b + ((const IMAGE_DOS_HEADER*)b)->e_lfanew);
			if (nt->OptionalHeader.SizeOfImage > kRvaAllocInit && !*(volatile unsigned char*)(b + kRvaAllocInit)) {
				g_mhEarly = MH_Initialize() == MH_OK;
				if (g_mhEarly) Fastalloc_Install(b, h);   // CryMalloc's small blocks from mimalloc, before the game's first one (fastalloc.cpp)
				if (g_mhEarly) Zipidx_Install(b, h);   // baked archive indexes, before the first pak opens (zipidx.cpp)
				g_earlyHooks = g_mhEarly ? Questfast_EarlyInstall(b, h) : 0;
				if (g_mhEarly) Rttrname_EarlyInstall(b, h);   // rttr's type naming without the no-op re-derivations (rttrname.cpp)
				if (g_mhEarly) Gpuwarm_EarlyInstall(b, h);   // the graphics driver loaded on a worker (gpuwarm.cpp)
				if (g_mhEarly) Cvars_Install(b, h);   // the settings hooks on before the game registers any (cvars.cpp; boot 121)
				if (g_mhEarly) Dllinit_EarlyInstall(b, h);   // WHGame's static initializers outside the loader lock, off by default (dllinit.cpp)
			}
			Startfast_EarlyInstall(b, h);   // CPU detection pins the main thread, not the whole process (startfast.cpp; ~0.35 s)
		}
		CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
	}
	return TRUE;
}
