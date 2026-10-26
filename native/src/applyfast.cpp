// Faster pieces of the save load's "apply save" step. See applyfast.h.
#include "applyfast.h"
#include "loadwatch.h"
#include "questmenu.h"
#include "log.h"
#include "readiness.h"
#include "xmlahead.h"
#include "MinHook.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>
#include <intrin.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>

namespace {

int g_mode = 2;
long long g_freq = 1;
inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

bool Hook(uintptr_t base, uintptr_t rva, const unsigned char* bytes, size_t n, void* detour, void** orig, const char* what) {
	void* p = (void*)(base + rva);
	if (memcmp(p, bytes, n)) { Log("applyfast: %s: code check failed - not hooked (other game version?)", what); return false; }
	if (MH_CreateHook(p, detour, orig) != MH_OK || MH_EnableHook(p) != MH_OK) { Log("applyfast: %s: hook FAILED", what); return false; }
	return true;
}

// ---- 1. map clouds vs fog of war
// FUN_180b59660(fog, poly): rax is junk when the fog has no root (its callers read al); the hook passes rax through.
// FUN_180b59c54(node, fog, rect {x0, y0, x1, y1}, poly): one 64 m node; leaf cells are tested at their corners.
constexpr uintptr_t kRvaFogAny = 0xb59660, kRvaFogNode = 0xb59c54;
const unsigned char kFogAnyBytes[] = { 0x48,0x8b,0xc4,0x48,0x83,0xec,0x38,0x48,0x83,0x79,0x68,0x00 };
const unsigned char kFogNodeBytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x68,0x18,0x56 };
using FogAnyFn = uint64_t(__fastcall*)(uint8_t* fog, uint8_t* poly);
using FogNodeFn = uint64_t(__fastcall*)(void* node, uint8_t* fog, float* rect, uint8_t* poly);
FogAnyFn g_fogAny = nullptr;
FogNodeFn g_fogNode = nullptr;

struct Box {
	const uint8_t* poly = nullptr;   // the cloud being asked about (null: no culling)
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	long long nodes = 0, culled = 0, checked = 0, bad = 0;
};
thread_local Box t_box;
std::atomic<long long> s_clouds{0}, s_ticks{0}, s_nodes{0}, s_culled{0}, s_checked{0}, s_bad{0}, s_yes{0};

uint64_t __fastcall HkFogAny(uint8_t* fog, uint8_t* poly) {
	const long long t0 = Now();
	const Box saved = t_box;   // nested asks (none seen) keep their own box
	t_box = Box{};
	if (g_mode && poly) {
		const float* p = *(const float**)poly;
		const float* e = *(const float**)(poly + 8);
		float x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
		bool ok = p && e && p <= e;
		for (; ok && p < e; p += 2) {
			if (!std::isfinite(p[0]) || !std::isfinite(p[1])) { ok = false; break; }   // the game's answer, the slow way
			if (p[0] < x0) x0 = p[0];
			if (p[0] > x1) x1 = p[0];
			if (p[1] < y0) y0 = p[1];
			if (p[1] > y1) y1 = p[1];
		}
		if (ok) { t_box.poly = poly; t_box.x0 = x0 - 1.f; t_box.y0 = y0 - 1.f; t_box.x1 = x1 + 1.f; t_box.y1 = y1 + 1.f; }
	}
	const uint64_t r = g_fogAny(fog, poly);
	s_nodes += t_box.nodes; s_culled += t_box.culled; s_checked += t_box.checked; s_bad += t_box.bad;
	if ((uint8_t)r && fog && *(void**)(fog + 0x68)) s_yes++;
	t_box = saved;
	s_clouds++;
	s_ticks += Now() - t0;
	return r;
}

uint64_t __fastcall HkFogNode(void* node, uint8_t* fog, float* rect, uint8_t* poly) {
	Box& b = t_box;
	if (b.poly && b.poly == poly && rect && fog) {
		b.nodes++;
		const uint32_t cellShift = *(uint32_t*)(fog + 0x48) & 31, nodeShift = *(uint32_t*)(fog + 0x4c) & 31;
		const int cells = *(int*)(fog + 0x18);
		float ext = (float)(1u << nodeShift);
		const float leafExt = (float)cells * (float)(1u << cellShift);   // the corners tested lie below x0 + cells * cell
		if (leafExt > ext) ext = leafExt;
		const float x0 = rect[0], y0 = rect[1];
		const float hx = (std::max)(x0 + ext, rect[2]), hy = (std::max)(y0 + ext, rect[3]);
		if (hx < b.x0 || x0 > b.x1 || hy < b.y0 || y0 > b.y1) {
			b.culled++;
			if (g_mode == 2 && (b.culled & 63) == 0) {
				b.checked++;
				if ((uint8_t)g_fogNode(node, fog, rect, poly)) {
					if (b.bad++ == 0 && s_bad.load() == 0)
						Log("applyfast: fog MISMATCH: node (%.1f, %.1f) outside the cloud's box (%.1f..%.1f, %.1f..%.1f) said yes "
						    "- the cull is off for this session", x0, y0, b.x0, b.x1, b.y0, b.y1);
					g_mode = 0;
					return 1;
				}
			}
			return 0;
		}
	}
	return g_fogNode(node, fog, rect, poly);
}

std::atomic<long long> s_lastClouds{0}, s_lastTicks{0};

// ---- 2. the facial DBA list, sorted before the game inserts it (objects step; RE report re_stepC.md)
// male/female.chrparams: `$TracksDatabase path="Animations\humans\facials\*.dba"` makes the file finder
// FUN_18041d194 (recursive) return 19,800 paths into the vector at its 5th argument. The caller (return address
// 0x1803c5e35) inserts each into a sorted array (FUN_1803c6c60: '\' -> '/', ASCII lower case; lower_bound
// FUN_1803c6b0c compares unsigned bytes). The paths come unsorted, so every insert shifted the tail: 0.66 s in boot 13.
// Sorted by the same key first, every insert lands at the end. The set holds the same paths in any order.
constexpr uintptr_t kRvaFind = 0x41d194, kRvaFindChrparams = 0x3c5e35;
const unsigned char kFindBytes[] = { 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48 };
const unsigned char kFindCallBytes[] = { 0x48,0x8d,0x4d,0x90 };   // at the return address: lea rcx,[rbp-70h]
using FindFn = void(__fastcall*)(void* a1, void* a2, void* a3, void* a4, uint64_t* out);
FindFn g_find = nullptr;
uintptr_t g_base = 0;
std::atomic<long long> s_sorts{0}, s_sorted{0}, s_moved{0}, s_sortTicks{0};

inline unsigned char DbaKey(char c) {
	const unsigned char u = (unsigned char)c;
	return u == '\\' ? '/' : ((unsigned)(u - 'A') < 26u ? (unsigned char)(u + 32) : u);
}
bool DbaLess(uint64_t a, uint64_t b) {
	const char* x = (const char*)a;
	const char* y = (const char*)b;
	if (!x || !y) return x < y;
	for (;; x++, y++) {
		const unsigned char p = DbaKey(*x), q = DbaKey(*y);
		if (p != q) return p < q;
		if (!p) return false;
	}
}

void __fastcall HkFind(void* a1, void* a2, void* a3, void* a4, uint64_t* out) {
	const uintptr_t ret = (uintptr_t)_ReturnAddress();
	g_find(a1, a2, a3, a4, out);
	if (g_mode && out && ret == g_base + kRvaFindChrparams) {
		const long long t0 = Now();
		uint64_t* b = (uint64_t*)out[0];
		uint64_t* e = (uint64_t*)out[1];
		if (b && e && b < e) {
			const size_t n = (size_t)(e - b);
			size_t out_of_order = 0;
			for (size_t i = 1; i < n; i++) out_of_order += DbaLess(b[i], b[i - 1]);
			if (out_of_order) std::sort(b, e, DbaLess);
			s_sorted += (long long)n;
			s_moved += (long long)out_of_order;
		}
		s_sorts++;
		s_sortTicks += Now() - t0;
	}
}

// ---- 3. PSO precache at the main menu
// FUN_181de4180 (renderer vt+0x3e0 via the thunk at 0x5f88a8; no arguments) queues the mode-2 PSO precache lambda on
// the render thread (FUN_181dfaf80(gRenDev->m_pRT): on any thread but the render threads it writes into the main
// thread's command buffer, so it must be called on the main thread). PostLoadLevel calls it and the main thread
// then waits ~2.4 s for it in state 4. The PSO list comes from the pipeline cache file, not the level (re_stepEF.md),
// so the menu call builds the same PSOs and the load's own call finds them made.
constexpr uintptr_t kRvaPsoQueue = 0x1de4180;
const unsigned char kPsoQueueBytes[] = { 0x48,0x83,0xec,0x48,0x48,0x8b,0x15 };   // sub rsp,48h; mov rdx,[rip+..]
// FUN_1809a6190 SyncMainWithRender(m_pRT, flag): the main thread hands its command buffer to the render thread, every
// frame, at the menu too (boot 21 samples; ISystem::Render and CSystem vt+0x50 don't run at the menu).
constexpr uintptr_t kRvaSync = 0x9a6190;
const unsigned char kSyncBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57 };
using SyncFn = uint64_t(__fastcall*)(void* rt, uint64_t a2, uint64_t a3, uint64_t a4);
SyncFn g_sync = nullptr;
constexpr unsigned kPsoQuietMs = 1500;   // the menu: no XML load for this long
// fastload_psoahead.txt: 5 (default since boot 133) on a plugin thread from the main menu, as soon as the pipeline cache
// is built (start-up, ~7 s before the load; piece 5's one-parse-per-frame is skipped: the precache parses that set; boot
// 133: 8.65 s there, the load parsed 20 shaders instead of 363, LoadLevel's end -> state 5 3.0 -> 0.9 s, the animation
// preload 0.5 s slower); 3 the same from the quest-wait point (3c; with dbahead 3 Main waited ~1.8 s for it, boot 131);
// 4 the same from the load's start (right after its Blocking precache; boot 132: it overlapped the render loading thread's
// shader backlog and the level's lookups, 7.7 s, 8357 parses in the load, LoadLevel +1.1 s);
// 2 handed to the render loading thread during the load (3b), 1 at the main menu on Main (the menu freezes ~6 s), 0 off
int g_psoMode = 3;
bool g_psoEarly = false;   // fastload_psoahead.txt 4: g_psoMode 3, started after the Blocking precache
bool g_psoMenu = true;     // fastload_psoahead.txt 5 (default): g_psoMode 3, started when the pipeline cache is built
// The load's Blocking precache (mode 1, FUN_181dfd500 on the render thread at LoadLevel's start, 91 PSOs, ~0.02 s) while
// ours still runs: 1 (the second number, default since 2026-10-25) = it returns at once and our thread runs it right after
// ours (boots 137/139: load 17.9 / 17.9 s, LoadLevel 10.43 / 10.32 s); 0 = it waits for ours (boots 135/136: the render
// thread 0.9 s and Main 0.93 s in its render sync; load 18.2 / 18.5 s, LoadLevel 10.65 / 10.79 s). Left either way:
// LoadLevel's PSO pause (FUN_181de4290 -> FUN_180babcf8: flag +0x138, spin until the async PSO queue's count +0x13c is 0)
// waits 0.3-0.5 s for our precache's last jobs, the same 3 shaders DXC compiles in every boot.
bool g_psoDefer = true;
std::atomic<int> g_psoState{0};          // 0 not yet, 1 queued, -1 not possible
std::atomic<long long> s_psoFrames{0};
std::atomic<long long> s_psoQueuedAt{0};

void PsoMainFrame() {
	if (g_psoMode != 1 || g_psoState.load(std::memory_order_relaxed) != 0) return;
	const long long n = ++s_psoFrames;
	if (!Xmlahead_MenuQuiet(kPsoQuietMs)) return;
	g_psoState = 1;
	s_psoQueuedAt = Now();
	Log("applyfast: PSO precache queued at the main menu (render sync %lld, thread %lu); the load's own precache should "
	    "find them made (kcd.log: \"Precached N Graphics PSOs\")", n, GetCurrentThreadId());
	reinterpret_cast<void(__fastcall*)()>(g_base + kRvaPsoQueue)();
}

void ShaderMenuFrame();   // piece 5, below
std::atomic<long long> s_syncs{0};
long long g_lastSyncAt = 0;
// m_pRT+0x140 = the render thread's video (loading screen) mode: 0 off, 1 asked, 2 running (StartLoadtimePlayback
// FUN_1807a1f98 waits for 2), 4 stopping. Logged when it changes, so a hang at the loading screen's start shows in the
// log (boot 107: the render thread waited forever in the menu video's Bink frame, FUN_180892e30, 20 ms after the load
// started; once in ~70 auto-loads, not seen again; MODLOG).
void VideoModeWatch(void* rt) {
	static int last = 0;
	__try {
		const int m = *(volatile int*)((uintptr_t)rt + 0x140);
		if (m != last) {
			Log("applyfast: render sync %lld: the render thread's video mode %d -> %d (level state %d)", s_syncs.load(), last,
			    m, Readiness_LevelState());
			last = m;
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {}
}
uint64_t __fastcall HkSync(void* rt, uint64_t a2, uint64_t a3, uint64_t a4) {
	{   // when the main thread syncs during start-up (measurement: can the precache run before the menu shows?)
		const long long n = ++s_syncs, now = Now();
		if (n <= 3 || (n <= 400 && (now - g_lastSyncAt) * 1000 / g_freq >= 1000) || n == 100 || n == 400)
			Log("applyfast: render sync %lld on thread %lu (%lld ms after the one before)", n, GetCurrentThreadId(),
			    g_lastSyncAt ? (now - g_lastSyncAt) * 1000 / g_freq : -1);
		g_lastSyncAt = now;
	}
	VideoModeWatch(rt);
	PsoMainFrame();     // before the hand-over: the queued precache goes with this frame's commands
	ShaderMenuFrame();  // piece 5: one shader parse queued per menu frame
	return g_sync(rt, a2, a3, a4);
}

// ---- 3b. PSO precache during the load (fastload_psoahead.txt 2), no menu freeze
// The render loading thread finishes its texture precache around the time the main thread starts waiting for the quest
// job, then sits idle until the main thread's first frame sync after "finish level", and only then gets the precache
// (boot 27: idle 13.9-15.7 s, boot 26: 14.8-15.8 s; precache 2.4 s after that). So: on the main thread, just before
// the quest wait (it would wait there anyway), wait until the loading thread's batch is done, then queue the game's
// own precache and hand it over at once, so it runs while the main thread finishes the level. Done = the flush byte
// m_pRT+0x50 is 0 (SyncMainWithRender sets it, the loading thread clears it in FUN_180fbbdb8); the engine's own texture
// precache request syncs the same way, only when that byte is 0 (FUN_1809b165c), so the hand-over never blocks.
// The load's own precache (PostLoadLevel) then finds the PSOs made.
// Never longer than the game would wait (boot 31, 2026-10-24: with little free RAM the loading thread stayed busy and a
// fixed 5 s wait cost the main thread 5 s, 40 s load; kcd2-deagle-66's review had flagged it). So: before the quest wait
// it waits only while the quest job still runs (the main thread would wait there anyway); if the loading thread is
// still busy then, the hand-over stays pending and is tried again, without waiting, at three points while the main
// thread finishes the level (PostLoadLevel; see HkPostLoadLevel). Not handed over by then = the game's own precache at
// its first frame sync, as without the plugin.
constexpr uintptr_t kRvaRenDev = 0x547db60;   // DAT_18547db60: gRenDev; m_pRT at +0xf8
constexpr unsigned kPsoWaitCapMs = 5000;      // safety only: the quest job normally ends the wait first
std::atomic<long long> s_psoWaitMs{-1};
uintptr_t g_psoRt = 0;                        // m_pRT, set when the hand-over goes pending (state 2)
std::atomic<bool> g_gamePsoQueued{false};     // PostLoadLevel has queued the game's own precache (no need for ours)
long long g_psoPendingAt = 0;
using Fn4 = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t);
Fn4 g_postLoadLevel = nullptr;
constexpr uintptr_t kRvaPostLoadLevel = 0x5f8740;
const unsigned char kPostLoadLevelBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x41,0x56 };

bool OnMainThread() {   // the engine names it "Main" (thread samples); m_pRT+0xf8 is not it (FUN_1809b16f8)
	using DescFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
	static const DescFn f = (DescFn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadDescription");
	PWSTR d = nullptr;
	const bool ok = f && SUCCEEDED(f(GetCurrentThread(), &d)) && d && !wcscmp(d, L"Main");
	if (d) LocalFree(d);
	return ok;
}

}  // namespace

namespace {
// main thread, the loading thread idle (flush byte 0): queue the game's precache (unless PostLoadLevel already did) and
// hand it over; the sync does not block because the byte is 0
void PsoHandOver(const char* where, long long waitedMs) {
	const bool queueOurs = !g_gamePsoQueued.load();
	if (queueOurs) reinterpret_cast<void(__fastcall*)()>(g_base + kRvaPsoQueue)();
	reinterpret_cast<void(__fastcall*)(uintptr_t, char)>(g_base + kRvaSync)(g_psoRt, 0);
	g_psoState = 1;
	s_psoWaitMs = waitedMs;
	Log("applyfast: PSO precache during the load: handed to the render loading thread %s (waited %lld ms, %.2f s after it "
	    "went pending; %s)", where, waitedMs, g_psoPendingAt ? (Now() - g_psoPendingAt) * 1.0 / g_freq : 0.0,
	    queueOurs ? "queued by us" : "the game's own, queued by PostLoadLevel");
}

// a later point on the main thread: hand over if pending and the loading thread is idle, never wait
void PsoRetry(const char* where) {
	if (g_psoState.load(std::memory_order_relaxed) != 2) return;
	__try {
		if (*(volatile char*)(g_psoRt + 0x50) || !OnMainThread()) return;
		PsoHandOver(where, 0);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_psoState = -1;
		Log("applyfast: PSO precache during the load: exception at %s, left to the game", where);
	}
}

// The one later point: PostLoadLevel queues the game's own precache, and the game's next frame sync runs it (state 4).
// Boots 32-33 showed the level-load-end event (0x1c) and the XGenAI global pass (0x7306) come after that sync, so a
// hand-over there was only an extra sync in the frame loop; they are no longer used. Still busy here = left to the game.
uint64_t __fastcall HkPostLoadLevel(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
	PsoRetry("as the level finishes (PostLoadLevel)");
	const uint64_t r = g_postLoadLevel(a, b, c, d);
	int pending = 2;
	if (g_psoState.compare_exchange_strong(pending, -1))
		Log("applyfast: PSO precache during the load: the render loading thread was still busy at PostLoadLevel, left to "
		    "the game (%.2f s pending)", (Now() - g_psoPendingAt) * 1.0 / g_freq);
	return r;
}

// ---- 3c. PSO precache on a plugin thread (fastload_psoahead.txt 3, the default; re_pso_parallel.md R2, 2026-10-25)
// 3b's hand-over found the render loading thread still busy (its texture precache) in every boot since 88, so the game ran
// the precache itself at LoadLevel's end: boot 113, Main's FlushAndWait there 3.0 s = that thread's shader parses ~1.0 s
// + its wait for the creation jobs ~1.7 s ("Precached 15989 Graphics PSOs" 1.9-2.0 s). Now a plugin thread runs the
// game's own precache, FUN_181dfbac0 (graphics FUN_180babff8(mgr, 2), then compute FUN_180bab8e0), from the quest-wait
// point; the PSO creation stays on the game's job workers. What makes it safe off the loading thread:
// - the code's three render-thread helpers (FUN_181e12810 Sleep(1); FUN_181e12860 / FUN_181e127a0 job waits) release and
//   retake the loading thread's lock (m_pRT+0x20) while a loading screen is up (m_pRT+0x140 != 0): on our thread they get
//   a zeroed stand-in render-thread object, so they only wait (the Sleep(1) between shaders is skipped);
// - the loading thread's own call (PostLoadLevel's command, FUN_180babff8) waits for ours at its start, releasing its lock
//   the way the game's helper there does, then finds every PSO made ("Precached 0 ...", 0.12-0.15 s in boots 22-31). If
//   the game's own call comes first, ours never starts;
// - shader parses (FUN_181390424) test their "parsed" flag (+0x94 & 0x100) without a lock: one lock now covers every
//   parse on every thread, so a second parse of a shader waits, then returns on the flag;
// - our precache makes its new shader instances first and parses them after (FUN_180bac67c). Meanwhile the game's
//   lookup (FUN_1807e9088, 3 callers) would answer "exists" for one of them and queue no parse; for those it answers
//   "new", so the caller queues a parse (mfForName FUN_1807e8fe4 only adds a reference and queues it).
constexpr uintptr_t kRvaPsoLambda = 0x1dfbac0, kRvaPsoGate = 0xbabff8, kRvaRtSleep = 0x1e12810, kRvaRtWaitAux = 0x1e12860,
                    kRvaRtWaitJobs = 0x1e127a0, kRvaShaderCore = 0x7e9088, kRvaRtLock = 0x860c58;
const unsigned char kPsoLambdaBytes[] = { 0x48,0x83,0xec,0x28,0xe8,0xab,0x03,0xdb,0xfe,0x84,0xc0,0x75,0x21,0xba,0x02,0x00 };
const unsigned char kPsoGateBytes[] = { 0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x10,0x49,0x89,0x6b,0x18,0x56,0x57,0x41,0x56,0x48 };
const unsigned char kRtSleepBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0x81,0x40,0x01,0x00,0x00,0x48,0x8b,0xd9,0x90 };
const unsigned char kRtWaitBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x8b };
const unsigned char kShaderCoreBytes[] = { 0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57 };
const unsigned char kRtLockBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xff,0x15,0xa9,0x28,0x1a,0x03,0x48 };
constexpr DWORD kGateCapMs = 30000;          // safety only: our precache took ~2 s
thread_local bool t_psoOurs = false;
std::atomic<int> g_deferState{0};   // the load's Blocking precache handed to our thread: 0 none, 1 owed, 2 taken
std::atomic<uintptr_t> g_deferMgr{0};
std::atomic<uint32_t> g_deferMode{0};
std::atomic<bool> g_deferRunning{false};   // our thread runs the handed-over one: another game call waits for it
long long g_deferAt = 0;
alignas(16) unsigned char g_rtStandIn[0x200] = {};   // +0x140 == 0: no loading screen, no lock juggling
std::atomic<int> g_coord{0};                 // 0 not started, 1 running, 2 done, -1 the game's own came first / off
HANDLE g_coordDone = nullptr;
long long g_coordStart = 0;
SRWLOCK g_parseLock = SRWLOCK_INIT;
bool g_parseLockOn = false;
std::mutex g_unparsedMu;
std::unordered_set<uintptr_t> g_unparsed;    // shaders our precache made and has not parsed yet
std::atomic<long long> s_barrier{0}, s_parseLockWaits{0}, s_parseLockWaitTicks{0}, s_ourParses{0}, s_gateWaits{0};
using RtFn1 = void(__fastcall*)(uintptr_t);
using RtFn2 = void(__fastcall*)(uintptr_t, uintptr_t);
RtFn1 g_rtSleep = nullptr;
RtFn2 g_rtWaitAux = nullptr, g_rtWaitJobs = nullptr;
void __fastcall HkRtSleep(uintptr_t rt) { if (!t_psoOurs) g_rtSleep(rt); }
void __fastcall HkRtWaitAux(uintptr_t rt, uintptr_t p) { g_rtWaitAux(t_psoOurs ? (uintptr_t)g_rtStandIn : rt, p); }
void __fastcall HkRtWaitJobs(uintptr_t rt, uintptr_t p) { g_rtWaitJobs(t_psoOurs ? (uintptr_t)g_rtStandIn : rt, p); }

DWORD WINAPI PsoEarlyStart(LPVOID);   // below: mode 4's start
using GateFn = void(__fastcall*)(uintptr_t, uint32_t);
GateFn g_psoGate = nullptr;
void __fastcall HkPsoGate(uintptr_t mgr, uint32_t mode) {
	// mode 2 = the NonBlocking precache (ours, and the loading thread's at LoadLevel's end). The load's start also runs a
	// Blocking one here (91 PSOs, 0.02 s; boot 117: it switched ours off): only a mode-2 call before ours keeps ours off.
	int z = 0;
	const bool first = mode == 2 && !t_psoOurs && g_coord.compare_exchange_strong(z, -1);
	if (!first && !t_psoOurs && mode == 1 && g_psoDefer && g_coord.load() == 1 && g_deferState.load() == 0) {
		// ours is running: hand this Blocking one to our thread (Dekker with PsoCoordinator's end, seq_cst)
		g_deferMgr = mgr;
		g_deferMode = mode;
		g_deferAt = Now();
		g_deferState = 1;
		if (g_coord.load() == 1) {
			Log("applyfast: PSO precache on our thread: the load's Blocking precache (thread %lu, mode %u) handed to our thread, "
			    "it runs right after ours", GetCurrentThreadId(), mode);
			return;
		}
		int one = 1;   // ours ended meanwhile: run it here if our thread didn't take it
		if (!g_deferState.compare_exchange_strong(one, 2)) {
			Log("applyfast: PSO precache on our thread: the load's Blocking precache (mode %u) taken by our thread as ours ended", mode);
			return;
		}
	}
	if (!first && !t_psoOurs && ((z = g_coord.load()) == 1 || g_deferRunning.load())) {   // ours is running: wait for it
		uintptr_t rt = 0;
		bool release = false;
		__try {
			const uintptr_t rd = *(uintptr_t*)(g_base + kRvaRenDev);
			rt = rd ? *(uintptr_t*)(rd + 0xf8) : 0;
			const DWORD me = GetCurrentThreadId();   // the loading or render thread holds that lock while a loading
			release = rt && *(volatile int*)(rt + 0x140) != 0 &&   // screen is up (as FUN_181e12860 assumes here)
			          (me == *(DWORD*)(rt + 0xf4) || me == *(DWORD*)(rt + 0xf0));
		} __except (EXCEPTION_EXECUTE_HANDLER) { release = false; }
		const long long t0 = Now();
		if (release) ReleaseSRWLockExclusive((PSRWLOCK)(rt + 0x20));
		const DWORD w = WaitForSingleObject(g_coordDone, kGateCapMs);
		if (release) reinterpret_cast<void(__fastcall*)(uintptr_t)>(g_base + kRvaRtLock)(rt + 0x18);
		s_gateWaits++;
		Log("applyfast: PSO precache on our thread: the game's own (thread %lu, mode %u) waited %.0f ms for it%s",
		    GetCurrentThreadId(), mode, (Now() - t0) * 1000.0 / g_freq, w == WAIT_OBJECT_0 ? "" : " - GAVE UP after 30 s, both run");
	}
	g_psoGate(mgr, mode);
	if (g_psoEarly && mode != 2 && !t_psoOurs && g_coord.load() == 0) {   // the load's Blocking precache just ended
		static std::atomic<bool> once{false};
		if (!once.exchange(true)) {
			Log("applyfast: PSO precache on our thread: the load's Blocking precache (mode %u) ended on thread %lu, ours starts",
			    mode, GetCurrentThreadId());
			if (HANDLE h = CreateThread(nullptr, 0, PsoEarlyStart, nullptr, 0, nullptr)) CloseHandle(h);
		}
	}
}

void UnparsedAdd(uintptr_t sh) { std::lock_guard<std::mutex> lk(g_unparsedMu); g_unparsed.insert(sh); }
bool UnparsedHas(uintptr_t sh) { std::lock_guard<std::mutex> lk(g_unparsedMu); return g_unparsed.count(sh) != 0; }
size_t UnparsedClear() { std::lock_guard<std::mutex> lk(g_unparsedMu); const size_t n = g_unparsed.size(); g_unparsed.clear(); return n; }
bool RunPsoGate(uintptr_t mgr, uint32_t mode) {
	__try { g_psoGate(mgr, mode); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool RunPsoLambda() {
	__try { reinterpret_cast<void(__fastcall*)()>(g_base + kRvaPsoLambda)(); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
using CoreFn = uint64_t(__fastcall*)(uint64_t, uint64_t*, const char*, uint32_t, uint64_t, uint64_t*);
CoreFn g_shaderCore = nullptr;
uint64_t __fastcall HkShaderCore(uint64_t sm, uint64_t* out, const char* name, uint32_t flags, uint64_t a5, uint64_t* a6) {
	const uint64_t r = g_shaderCore(sm, out, name, flags, a5, a6);
	if (g_coord.load(std::memory_order_relaxed) != 1 || !out) return r;
	__try {
		const uintptr_t sh = (uintptr_t)out[0];
		uint8_t* isNew = (uint8_t*)out + 0x10;
		if (!sh) return r;
		if (t_psoOurs) {
			if (*isNew) UnparsedAdd(sh);
		} else if (!*isNew && !(*(volatile uint32_t*)(sh + 0x94) & 0x100) && UnparsedHas(sh)) {
			*isNew = 1;
			s_barrier++;
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {}
	return r;
}

DWORD WINAPI PsoCoordinator(LPVOID) {
	{   // hitch's sampler takes threads named "JobWorker_*"
		using SetDescFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
		static const SetDescFn f = (SetDescFn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetThreadDescription");
		if (f) f(GetCurrentThread(), L"JobWorker_fastload psoahead");
	}
	t_psoOurs = true;
	const bool ok = RunPsoLambda();
	const size_t left = UnparsedClear();
	g_coord = 2;
	int one = 1;
	if (g_deferState.load() == 1 && g_deferState.compare_exchange_strong(one, 2)) {   // the load's Blocking one, owed
		const long long a = Now();
		bool dok = true;
		g_deferRunning = true;
		dok = RunPsoGate(g_deferMgr.load(), g_deferMode.load());
		g_deferRunning = false;
		Log("applyfast: PSO precache on our thread: the load's Blocking precache (mode %u) %s here in %.0f ms, %.0f ms after "
		    "it was handed over", g_deferMode.load(), dok ? "ran" : "EXCEPTION", (Now() - a) * 1000.0 / g_freq,
		    (Now() - g_deferAt) * 1000.0 / g_freq);
	}
	t_psoOurs = false;
	SetEvent(g_coordDone);
	Log("applyfast: PSO precache on our thread: %s in %.2f s (%lld shader parses here, %lld parse-lock waits %.0f ms on all "
	    "threads, %lld lookups answered \"new\", %zu made but not parsed); kcd.log has its \"Precached N Graphics PSOs\" line",
	    ok ? "done" : "EXCEPTION", (Now() - g_coordStart) * 1.0 / g_freq, s_ourParses.load(), s_parseLockWaits.load(),
	    s_parseLockWaitTicks.load() * 1000.0 / g_freq, s_barrier.load(), left);
	return 0;
}

void PsoCoordinatorStart(const char* where = "the quest-wait point");
DWORD WINAPI PsoEarlyStart(LPVOID) {   // mode 4: after the load's Blocking precache, once its "running" flag is clear
	const long long t0 = Now();
	bool clear = false;
	while (!clear && (Now() - t0) * 1000 / g_freq < 3000) {
		__try {
			const uintptr_t rd = *(uintptr_t*)(g_base + kRvaRenDev);
			clear = rd && !*(volatile uint8_t*)(rd + 0x2ec0);
		} __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
		if (!clear) Sleep(1);
	}
	PsoCoordinatorStart("the load's start (after its Blocking precache)");
	return 0;
}

void PsoCoordinatorStart(const char* where) {   // the quest-wait point (main thread) or mode 4's early start; runs once per game start
	static bool told = false;
	if (const int c = g_coord.load()) {
		if (!told && c < 0) Log("applyfast: PSO precache on our thread: not started (the game's own NonBlocking precache came first)");
		told = true;
		return;
	}
	__try {
		const uintptr_t rd = *(uintptr_t*)(g_base + kRvaRenDev);
		if (!rd || *(volatile uint8_t*)(rd + 0x2ec0)) {   // a precache is running (the game's)
			int z = 0;
			g_coord.compare_exchange_strong(z, -1);
			Log("applyfast: PSO precache on our thread: not started (a precache is already running), left to the game");
			return;
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) { return; }
	g_coordStart = Now();
	int z = 0;
	if (!g_coord.compare_exchange_strong(z, 1)) {
		Log("applyfast: PSO precache on our thread: not started (the game's own came first), left to the game");
		return;
	}
	HANDLE h = CreateThread(nullptr, 0, PsoCoordinator, nullptr, 0, nullptr);
	if (!h) { g_coord = -1; SetEvent(g_coordDone); Log("applyfast: PSO precache on our thread: thread not created"); return; }
	CloseHandle(h);
	Log("applyfast: PSO precache on our thread: started at %s (thread named \"JobWorker_fastload psoahead\")", where);
}
}  // namespace

void Applyfast_BeforeQuestWait() {   // main thread, from loadwatch's hook of FUN_1811414d8 (msg 0x18)
	if (g_psoMode == 3 && g_base && g_coordDone) { PsoCoordinatorStart(); return; }
	if (g_psoMode != 2 || g_psoState.load() != 0 || !g_base) return;
	g_psoState = -1;   // once per game start: the PSOs stay made
	__try {
		const uintptr_t rd = *(uintptr_t*)(g_base + kRvaRenDev);
		const uintptr_t rt = rd ? *(uintptr_t*)(rd + 0xf8) : 0;
		const bool main = OnMainThread();
		if (!rt || !*(uintptr_t*)rt || !*(DWORD*)(rt + 0xf4) || !main) {
			Log("applyfast: PSO precache during the load: not done (render thread object %p, loading thread %lu, render "
			    "thread %lu, this thread %lu named Main: %s)", (void*)rt, rt ? *(DWORD*)(rt + 0xf4) : 0,
			    rt ? *(DWORD*)(rt + 0xf0) : 0, GetCurrentThreadId(), main ? "yes" : "no");
			return;
		}
		g_psoRt = rt;
		const long long t0 = Now();
		long long ms = 0;
		int job = Loadwatch_QuestJob();   // -1 unknown (hook off), 0 running, 1 done
		while (*(volatile char*)(rt + 0x50) && (job = Loadwatch_QuestJob()) != 1 &&
		       (ms = (Now() - t0) * 1000 / g_freq) < (job < 0 ? 2000u : kPsoWaitCapMs))
			Sleep(1);
		ms = (Now() - t0) * 1000 / g_freq;
		if (!*(volatile char*)(rt + 0x50)) { PsoHandOver("before the quest wait", ms); return; }
		g_psoPendingAt = Now();
		g_psoState = 2;
		Log("applyfast: PSO precache during the load: pending after %lld ms (quest job %s, the render loading thread still "
		    "busy); tried again while the level finishes, without waiting", ms, job == 1 ? "done" : job == 0 ? "still running" : "unknown");
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		Log("applyfast: PSO precache during the load: exception, left to the game");
	}
}

namespace {

// ---- 4. Quests first in the level data message (re_questearly.md, 2026-10-24)
// The LevelLoadJob thread reads levels/<level>/whdata_0 and sends module message 0x1e (C_ModuleMessageSerializer).
// C_ModulesManager::ProcessMessage (FUN_180479bec) hands it to every module's OnMessage (vt+8) in registration order;
// the concept module (C_ConceptModule, DAT_185480280, vtable 0x183E233C8, OnMessage FUN_1811411cc) comes after the
// character and AI-tree loaders, so its handler FUN_18114148c (reads "*m_ConceptManager", submits the ConceptLoadJob)
// runs ~3 s after the data exists, and the main thread later waits 1-3.4 s for that job. Here the concept handler runs
// first in the module loop (after the pre-step, as in the game), with the same message on the same thread, and its own
// turn in the loop is skipped. The serializer finds groups
// by name, so the other modules read the same data.
constexpr uintptr_t kRvaProcessMsg = 0x479bec, kRvaConceptOnMsg = 0x11411cc, kRvaConceptLevelData = 0x114148c;
constexpr uintptr_t kRvaConceptModule = 0x5480280, kRvaConceptModuleVt = 0x3E233C8, kRvaSerializerMsgVt = 0x4095570;
const unsigned char kProcessMsgBytes[] = { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20 };
const unsigned char kConceptOnMsgBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x44,0x8b,0x42,0x08 };
const unsigned char kConceptLevelDataBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x5a,0x18 };
using MsgFn = void(__fastcall*)(void* self, void* msg);
MsgFn g_processMsg = nullptr, g_conceptOnMsg = nullptr, g_preModules = nullptr;
constexpr uintptr_t kRvaPreModules = 0x47a1e4;
const unsigned char kPreModulesBytes[] = { 0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x48,0x83,0xec,0x70 };
thread_local long long t_earlyAt = 0;
bool g_questsFirst = true;                 // fastload_questearly.txt: 0 off, 1 on
thread_local void* t_earlyMsg = nullptr;   // the message whose concept turn already ran
std::atomic<long long> s_qeEarly{0}, s_qeSkipped{0};

// FUN_18047a1e4(*mgr, msg): ProcessMessage's step before the module loop (only caller); for 0x1e it calls one handler
// (*mgr + 0x70, vt+0x20). The quests' turn goes right after it, so that order stays as in the game.
void __fastcall HkPreModules(void* game, void* msg) {
	g_preModules(game, msg);
	uintptr_t mod = 0;
	if (msg && !t_earlyMsg && *(uintptr_t*)msg == g_base + kRvaSerializerMsgVt && *(int*)((char*)msg + 8) == 0x1e &&
	    (mod = *(uintptr_t*)(g_base + kRvaConceptModule)) != 0 && *(uintptr_t*)mod == g_base + kRvaConceptModuleVt) {
		const long long t0 = Now();
		reinterpret_cast<MsgFn>(g_base + kRvaConceptLevelData)((void*)mod, msg);
		t_earlyAt = Now();
		t_earlyMsg = msg;
		s_qeEarly++;
		Log("applyfast: level data message: quests went first (%.0f ms, thread %lu)", (t_earlyAt - t0) * 1000.0 / g_freq,
		    GetCurrentThreadId());
	}
}

// Log only (2026-10-24, boot 75): the apply step (level state 5) is a series of module messages; the hitch sampler's
// stacks are cut at 64 frames, so 74 of its 190 apply samples had no known piece. Every message dispatch in state 5
// that takes 10 ms or more is logged with its id, the save chunk's type for id 9 (msg +0x18 -> +8), the nesting depth
// and the thread. Lines end when the call ends; start = line time - ms.
int ChunkType(void* msg) {
	__try {
		const uintptr_t ch = *(uintptr_t*)((char*)msg + 0x18);
		return ch ? *(unsigned short*)(ch + 8) : -1;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}

void __fastcall HkProcessMsg(void* mgr, void* msg) {
	thread_local int depth = 0;
	if (msg && Readiness_LevelState() == 5) {
		const int id = *(const int*)((const char*)msg + 8);
		const int chunk = id == 9 ? ChunkType(msg) : 0;
		depth++;
		const long long t0 = Now();
		g_processMsg(mgr, msg);
		const double ms = (Now() - t0) * 1000.0 / g_freq;
		depth--;
		if (ms >= 10) {
			char what[32] = "";
			if (id == 9) snprintf(what, sizeof what, " chunk 0x%x", chunk);
			Log("applyfast: apply message 0x%x%s took %.0f ms (depth %d, thread %lu)", id, what, ms, depth,
			    GetCurrentThreadId());
		}
		if (t_earlyMsg == msg) t_earlyMsg = nullptr;
		return;
	}
	g_processMsg(mgr, msg);
	if (t_earlyMsg == msg) t_earlyMsg = nullptr;   // this call's message is done: never match a later one at the same
	                                               // address (a nested message's call leaves the outer one's mark)
}

// Log only (2026-10-24, boot 71): when the concept module's messages come, with the level state and the place-graph
// streaming's state (concept manager +0xb0, its +0xb8: 0 no worker yet, 1 reset, 2 idle / pop, 3 load, 4 unload).
// 0x34 starts the streaming (creates the worker the first time), 0x4d resets it: a load ahead of the save's apply
// needs both times. The first 120 messages of the session from level state 1 on, consecutive repeats of one id folded.
std::mutex s_cmMu;   // messages come from more than one thread (0x1e from the level-load job)
int s_cmLines = 0, s_cmLastId = -1, s_cmRepeat = 0;
int StreamState(void* mod) {
	__try {
		const uintptr_t mgr = *(uintptr_t*)((char*)mod + 0x18);
		return mgr ? *(int*)(mgr + 0xb0 + 0xb8) : -1;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
void LogConceptMsg(void* mod, int id, const char* when) {
	if (Readiness_LevelState() < 1) return;   // the menu sends 120+ (0xc / 0x24 pairs, boot 72): loads only
	std::lock_guard<std::mutex> lk(s_cmMu);
	if (id == s_cmLastId) { s_cmRepeat++; return; }
	if (s_cmLines >= 120) return;
	s_cmLines++;
	if (s_cmRepeat) Log("applyfast: concept message 0x%x repeated %d more times", s_cmLastId, s_cmRepeat);
	s_cmLastId = id;
	s_cmRepeat = 0;
	Log("applyfast: concept message 0x%x %s, thread %lu, level state %d, place streaming state %d", id, when,
	    GetCurrentThreadId(), Readiness_LevelState(), StreamState(mod));
}

void __fastcall HkConceptOnMsg(void* mod, void* msg) {
	if (msg) LogConceptMsg(mod, *(const int*)((const char*)msg + 8), "in");
	if (msg && msg == t_earlyMsg) {   // ran first already (HkPreModules)
		s_qeSkipped++;
		Log("applyfast: level data message: the quests' own turn came %.0f ms later (the quest job's head start)",
		    (Now() - t_earlyAt) * 1000.0 / g_freq);
		return;
	}
	if (msg) Questmenu_OnConceptMsg(*(const int*)((const char*)msg + 8));   // may wait for / disarm the main-menu quest graph
	g_conceptOnMsg(mod, msg);
}

}  // namespace

namespace {
// ---- 5. Shaders parsed at the main menu (re_menu_render.md plan A, 2026-10-24)
// In the load the render loading thread first parses a backlog of ~416 shaders (4.3-8.7 s; the PSO precache another 71,
// ~1.1 s) before its texture precache, so the main thread later waits for it. The set comes from the pipeline-state
// cache, not the level (FUN_180bad1c4 lists its entries; FUN_180bad714 creates an instance and keeps only new ones).
// Here, once per game start: on the first menu frame where that cache is built and its job idle, the list is walked
// with FUN_180bad714 diverted to record the keys (nothing created). Then each menu frame (main thread, render sync,
// no loading screen) calls the game's own CShaderMan::mfForName (FUN_1807e8fe4) for keys until one is new: creating it
// queues its parse (RC_ParseShader) with this frame's commands, so the render thread parses one shader per frame
// (~10-11 ms, boot 22) and the menu keeps drawing. Parsed shaders survive into the first level (UnLoadLevel does
// nothing with no level; the renderer's events flush no shaders). Continue mid-way leaves the rest to the load, as
// without the plugin. Boots 22-23 (the same parse in one go, menu frozen 6 s): the load's backlog went from 150-170 to
// 9-13 samples and the texture precache started at 6.2-6.6 s instead of 8.7-9.3 s. The list call reserves its vector
// (~3 MB, the game's allocator) and returns it empty; it is left allocated, once.
constexpr uintptr_t kRvaPsoMgr = 0x52ba440, kRvaKeyList = 0xbad1c4, kRvaKeyOne = 0xbad714, kRvaShaderForName = 0x7e8fe4,
                    kRvaParse = 0x1390424;
const unsigned char kKeyListBytes[] = { 0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x48,0x8d,0x6c,0x24,0xb9,0x48,0x81,0xec };
const unsigned char kKeyOneBytes[] = { 0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x08,0x49,0x89,0x73,0x10,0x49,0x89,0x7b,0x18,0x55 };
const unsigned char kForNameBytes[] = { 0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x08,0x49,0x89,0x73,0x10,0x49,0x89,0x7b,0x18,0x55 };
const unsigned char kParseBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x54,0x41,0x56 };
bool g_menuShaders = true;
int g_shState = 0;   // 0 waiting for the cache, 1 pacing, 2 done or stopped, -1 off
struct ShaderKey { char* name; uint32_t flags; uint64_t mask; };
std::vector<ShaderKey> g_keys;
size_t g_keyNext = 0;
thread_local bool t_collect = false;
long long s_shStart = 0, s_shFrames = 0, s_shCalls = 0, s_shQueued = 0, s_shReady = 0, s_shMaxGapMs = 0, s_shLastSync = 0;
using KeyOneFn = uint64_t*(__fastcall*)(uint64_t, uint64_t*, const char*, uint32_t, uint64_t);
KeyOneFn g_keyOne = nullptr;
uint64_t* __fastcall HkKeyOne(uint64_t sm, uint64_t* out, const char* name, uint32_t flags, uint64_t mask) {
	if (!t_collect) return g_keyOne(sm, out, name, flags, mask);
	if (name) g_keys.push_back({ _strdup(name), flags, mask });
	out[0] = 0; out[1] = 0; *(uint32_t*)(out + 2) = 0;   // "not new": the list stays empty
	return out;
}
Fn4 g_parse = nullptr;
std::atomic<long long> s_parseN[2], s_parseTicks[2], s_parseMax[2];   // [0] before the level load, [1] after
// RT_ParseShader (sm, shader, mask, flags), any thread: timing; with 3c also one lock for every parse (see 3c)
// SRW locks are not fair: our thread releasing and re-taking the lock for each of its ~450 menu parses kept winning, and
// the render thread waited 4.5-4.7 s for one parse (boots 170I2/I3: no menu frames, found by kcd2-deagle-1f). A game
// thread that waits is counted, and ours lets it go first.
std::atomic<int> g_gameParseWaiting{0};
uint64_t __fastcall HkParse(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
	if (g_parseLockOn && t_psoOurs) {
		for (int spins = 0; g_gameParseWaiting.load(std::memory_order_acquire) > 0; spins++) {
			if (spins < 64) SwitchToThread(); else Sleep(1);
		}
	}
	if (g_parseLockOn && !TryAcquireSRWLockExclusive(&g_parseLock)) {
		const long long w = Now();
		if (!t_psoOurs) g_gameParseWaiting.fetch_add(1, std::memory_order_acq_rel);
		AcquireSRWLockExclusive(&g_parseLock);
		if (!t_psoOurs) g_gameParseWaiting.fetch_sub(1, std::memory_order_acq_rel);
		s_parseLockWaits++;
		s_parseLockWaitTicks += Now() - w;
	}
	const long long t0 = Now();
	const uint64_t r = g_parse(a, b, c, d);
	const long long dt = Now() - t0;
	if (g_parseLockOn) {
		ReleaseSRWLockExclusive(&g_parseLock);
		if (t_psoOurs) s_ourParses++;
		if (g_coord.load(std::memory_order_relaxed) == 1) { std::lock_guard<std::mutex> lk(g_unparsedMu); g_unparsed.erase((uintptr_t)b); }
	}
	const int i = Xmlahead_LevelSeen() ? 1 : 0;
	s_parseN[i]++;
	s_parseTicks[i] += dt;
	for (long long m = s_parseMax[i].load(); dt > m && !s_parseMax[i].compare_exchange_weak(m, dt);) {}
	return r;
}

void ShaderSummary(const char* why) {
	Log("applyfast: shaders at the main menu %s: %zu keys, %lld looked up, %lld parses queued, %lld already parsed, in %lld "
	    "menu frames over %.1f s (longest gap between frames %lld ms); parses so far %lld at the menu (%.0f ms, longest "
	    "%.1f ms)", why, g_keys.size(), s_shCalls, s_shQueued, s_shReady, s_shFrames, (Now() - s_shStart) * 1.0 / g_freq,
	    s_shMaxGapMs, s_parseN[0].load(), s_parseTicks[0].load() * 1000.0 / g_freq, s_parseMax[0].load() * 1000.0 / g_freq);
}

void ShaderMenuFrameInner() {
	if (Xmlahead_LevelSeen()) {
		const bool wasPacing = g_shState == 1;
		g_shState = 2;
		if (wasPacing) ShaderSummary("stopped by the level load");
		return;
	}
	const uintptr_t rd = *(uintptr_t*)(g_base + kRvaRenDev);
	const uintptr_t rt = rd ? *(uintptr_t*)(rd + 0xf8) : 0;
	if (!rt || *(volatile int*)(rt + 0x140) != 0) return;   // a loading screen or video thread is up
	if (g_shState == 0) {
		const uintptr_t mgr = g_base + kRvaPsoMgr;
		const uintptr_t js = *(uintptr_t*)(mgr + 0x100);
		if (!js || *(volatile int*)(js + 0x10) != 0 || *(uint64_t*)(mgr + 0xe0) == 0) return;   // cache not built / job running
		if (g_psoMenu && g_psoMode == 3 && g_coordDone) {   // mode 5: the precache makes and parses this set itself
			g_shState = 2;
			Log("applyfast: shaders at the main menu: the pipeline cache is built (%llu entries); the PSO precache parses its "
			    "shaders on our thread (no parse per menu frame)", (unsigned long long)*(uint64_t*)(mgr + 0xe0));
			PsoCoordinatorStart("the main menu (pipeline cache built)");
			return;
		}
		uint64_t vec[3] = {};
		const long long t0 = Now();
		t_collect = true;
		reinterpret_cast<void(__fastcall*)(uint64_t*, uintptr_t, int)>(g_base + kRvaKeyList)(vec, mgr + 0xd8, 2);
		t_collect = false;
		g_shState = 1;
		s_shStart = s_shLastSync = Now();
		Log("applyfast: shaders at the main menu: %zu keys from the pipeline cache (%llu entries) in %.0f ms; one parse per menu "
		    "frame from now", g_keys.size(), (unsigned long long)*(uint64_t*)(mgr + 0xe0), (s_shStart - t0) * 1000.0 / g_freq);
		return;
	}
	const long long now = Now(), gap = (now - s_shLastSync) * 1000 / g_freq;
	if (gap > s_shMaxGapMs) s_shMaxGapMs = gap;
	s_shLastSync = now;
	s_shFrames++;
	const uintptr_t sm = rd + 0xf00;
	using ForNameFn = uintptr_t(__fastcall*)(uintptr_t, const char*, uint32_t, uint64_t, uint64_t);
	for (int checks = 0; g_keyNext < g_keys.size() && checks < 64; checks++) {
		const ShaderKey& k = g_keys[g_keyNext++];
		const uintptr_t sh = reinterpret_cast<ForNameFn>(g_base + kRvaShaderForName)(sm, k.name, k.flags, 0, k.mask);
		s_shCalls++;   // the reference it returns is kept: the level uses these shaders
		if (sh && (*(volatile uint32_t*)(sh + 0x94) & 0x100)) { s_shReady++; continue; }
		s_shQueued++;
		break;   // one new shader per frame: its parse runs on the render thread next frame (boot 109: up to 24 per start-up
		         // sync made the menu 1.2-1.6 s later, Main waiting for the render thread, and the load no faster)
	}
	if (g_keyNext >= g_keys.size()) {
		g_shState = 2;
		ShaderSummary("done");
	}
}

void ShaderMenuFrame() {   // main thread, every render sync
	if (g_shState != 0 && g_shState != 1) return;
	__try { ShaderMenuFrameInner(); }
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_shState = -1;
		Log("applyfast: shaders at the main menu: exception, stopped (the load parses the rest, as without the plugin)");
	}
}
}  // namespace

void Applyfast_OnLoaded() {
	if (s_qeEarly.load())
		Log("applyfast: quests first: %lld level data messages, concept turn skipped %lld times", s_qeEarly.load(),
		    s_qeSkipped.load());
	if (g_psoState.load() == 2) {   // never handed over: the game's own precache ran at its first frame sync
		g_psoState = -1;
		Log("applyfast: PSO precache during the load: still pending when the level was done, left to the game (%.2f s pending)",
		    (Now() - g_psoPendingAt) * 1.0 / g_freq);
	}
	static bool shTold = false;
	if (!shTold && g_shState != -1 && (s_parseN[0].load() || s_parseN[1].load())) {
		shTold = true;
		Log("applyfast: shader parses: at the menu %lld (%.0f ms, longest %.1f ms), in the load %lld (%.0f ms, longest %.1f ms)",
		    s_parseN[0].load(), s_parseTicks[0].load() * 1000.0 / g_freq, s_parseMax[0].load() * 1000.0 / g_freq,
		    s_parseN[1].load(), s_parseTicks[1].load() * 1000.0 / g_freq, s_parseMax[1].load() * 1000.0 / g_freq);
	}
	static bool psoTold = false;
	if (!psoTold && g_psoState.load() >= 0) {
		psoTold = true;
		if (g_psoMode == 1)
			Log("applyfast: PSO precache at the main menu: %s (after %lld render syncs)",
			    g_psoState.load() == 1 ? "queued" : "NOT queued (the menu never went quiet, or xmlahead mode 0)", s_psoFrames.load());
		// render syncs counted only until it was queued
	}
	const long long c = s_clouds.load(), t = s_ticks.load();
	const long long dc = c - s_lastClouds.exchange(c), dt = t - s_lastTicks.exchange(t);
	if (!dc && !s_sorts.load()) return;
	if (s_sorts.load())
		Log("applyfast: facial DBA lists sorted before the game inserts them: %lld lists, %lld paths (%lld out of order), "
		    "%.1f ms", s_sorts.load(), s_sorted.load(), s_moved.load(), s_sortTicks.load() * 1000.0 / g_freq);
	Log("applyfast: fog: %lld cloud asks in %.0f ms (%lld said yes); grid nodes %lld, skipped %lld (outside the cloud's box), "
	    "checked the game's way %lld, mismatches %lld; mode %d", dc, dt * 1000.0 / g_freq, s_yes.load(), s_nodes.load(),
	    s_culled.load(), s_checked.load(), s_bad.load(), g_mode);
}

void Applyfast_Install(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((dir + "fastload_applyfast.txt").c_str(), "r")) {
		int m = 2;
		if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 2) g_mode = m;
		fclose(fp);
	}
	if (!g_mode) { g_psoState = -1; Log("applyfast: off (fastload_applyfast.txt)"); return; }
	const bool a = Hook(base, kRvaFogAny, kFogAnyBytes, sizeof kFogAnyBytes, (void*)&HkFogAny, (void**)&g_fogAny, "fog ask");
	const bool b = a && Hook(base, kRvaFogNode, kFogNodeBytes, sizeof kFogNodeBytes, (void*)&HkFogNode, (void**)&g_fogNode,
	                         "fog node");
	if (a && !b) MH_DisableHook((void*)(base + kRvaFogAny));
	g_base = base;
	const bool c = !memcmp((void*)(base + kRvaFindChrparams), kFindCallBytes, sizeof kFindCallBytes) &&
	               Hook(base, kRvaFind, kFindBytes, sizeof kFindBytes, (void*)&HkFind, (void**)&g_find, "facial DBA list");
	if (FILE* fp = fopen((dir + "fastload_psoahead.txt").c_str(), "r")) {
		int m = 3, d = 1;
		if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 5) { g_psoEarly = m == 4; g_psoMenu = m == 5; g_psoMode = m >= 4 ? 3 : m; }
		if (fscanf(fp, "%d", &d) == 1) g_psoDefer = d != 0;
		fclose(fp);
	}
	if (FILE* fp = fopen((dir + "fastload_questearly.txt").c_str(), "r")) {
		int m = 1;
		if (fscanf(fp, "%d", &m) == 1) g_questsFirst = m != 0;
		fclose(fp);
	}
	bool e = false;
	if (g_questsFirst && !memcmp((void*)(base + kRvaConceptLevelData), kConceptLevelDataBytes, sizeof kConceptLevelDataBytes) &&
	    *(uintptr_t*)(base + kRvaConceptModuleVt + 8) == base + kRvaConceptOnMsg &&
	    Hook(base, kRvaConceptOnMsg, kConceptOnMsgBytes, sizeof kConceptOnMsgBytes, (void*)&HkConceptOnMsg,
	         (void**)&g_conceptOnMsg, "concept OnMessage")) {
		e = Hook(base, kRvaProcessMsg, kProcessMsgBytes, sizeof kProcessMsgBytes, (void*)&HkProcessMsg, (void**)&g_processMsg,
		         "ProcessMessage");
		if (e && !Hook(base, kRvaPreModules, kPreModulesBytes, sizeof kPreModulesBytes, (void*)&HkPreModules,
		               (void**)&g_preModules, "ProcessMessage pre-step")) {
			MH_DisableHook((void*)(base + kRvaProcessMsg));
			e = false;
		}
		if (!e) MH_DisableHook((void*)(base + kRvaConceptOnMsg));
	}
	Questmenu_SetConceptHook(e);
	const bool psoCode = !memcmp((void*)(base + kRvaPsoQueue), kPsoQueueBytes, sizeof kPsoQueueBytes) &&
	                     !memcmp((void*)(base + kRvaSync), kSyncBytes, sizeof kSyncBytes);
	bool coord = false;   // 3c: every hook or none
	if (g_psoMode == 3 && psoCode && !memcmp((void*)(base + kRvaPsoLambda), kPsoLambdaBytes, sizeof kPsoLambdaBytes) &&
	    !memcmp((void*)(base + kRvaRtLock), kRtLockBytes, sizeof kRtLockBytes) &&
	    !memcmp((void*)(base + kRvaRtWaitAux), kRtWaitBytes, sizeof kRtWaitBytes) &&
	    !memcmp((void*)(base + kRvaRtWaitJobs), kRtWaitBytes, sizeof kRtWaitBytes)) {
		g_coordDone = CreateEventA(nullptr, TRUE, FALSE, nullptr);
		struct H { uintptr_t rva; const unsigned char* b; size_t n; void* hk; void** orig; const char* name; } hs[] = {
			{ kRvaParse, kParseBytes, sizeof kParseBytes, (void*)&HkParse, (void**)&g_parse, "shader parse (lock + timing)" },
			{ kRvaRtSleep, kRtSleepBytes, sizeof kRtSleepBytes, (void*)&HkRtSleep, (void**)&g_rtSleep, "render-thread Sleep(1)" },
			{ kRvaRtWaitAux, kRtWaitBytes, sizeof kRtWaitBytes, (void*)&HkRtWaitAux, (void**)&g_rtWaitAux, "render-thread aux wait" },
			{ kRvaRtWaitJobs, kRtWaitBytes, sizeof kRtWaitBytes, (void*)&HkRtWaitJobs, (void**)&g_rtWaitJobs, "render-thread job wait" },
			{ kRvaShaderCore, kShaderCoreBytes, sizeof kShaderCoreBytes, (void*)&HkShaderCore, (void**)&g_shaderCore, "shader lookup" },
			{ kRvaPsoGate, kPsoGateBytes, sizeof kPsoGateBytes, (void*)&HkPsoGate, (void**)&g_psoGate, "PSO precache gate" },
		};
		int made = 0;
		for (const H& h : hs) { if (!g_coordDone || !Hook(base, h.rva, h.b, h.n, h.hk, h.orig, h.name)) break; made++; }
		coord = made == (int)(sizeof hs / sizeof hs[0]);
		if (!coord) {
			for (int i = 0; i < made; i++) MH_DisableHook((void*)(base + hs[i].rva));
			g_parse = nullptr;   // piece 5 may hook it again below
		}
		g_parseLockOn = coord;
	}
	if (g_psoMode == 3 && !coord) g_coord = -1;
	const bool d = g_psoMode && psoCode &&
	               (g_psoMode == 2 || coord ||
	                (g_psoMode == 1 && Hook(base, kRvaSync, kSyncBytes, sizeof kSyncBytes, (void*)&HkSync, (void**)&g_sync, "render sync")));
	if (!d || g_psoMode == 3) g_psoState = -1;
	const bool retry = d && g_psoMode == 2 &&   // the later hand-over point (3b); without it, only before the quest wait
	                   Hook(base, kRvaPostLoadLevel, kPostLoadLevelBytes, sizeof kPostLoadLevelBytes, (void*)&HkPostLoadLevel,
	                        (void**)&g_postLoadLevel, "PostLoadLevel");
	// piece 5: shaders parsed at the main menu, one per frame (fastload_menushaders.txt: 1 on, default; 0 off)
	if (FILE* fp = fopen((dir + "fastload_menushaders.txt").c_str(), "r")) {
		int m = 1;
		if (fscanf(fp, "%d", &m) == 1) g_menuShaders = m != 0;
		fclose(fp);
	}
	bool sh = g_menuShaders && psoCode &&
	          !memcmp((void*)(base + kRvaKeyList), kKeyListBytes, sizeof kKeyListBytes) &&
	          !memcmp((void*)(base + kRvaShaderForName), kForNameBytes, sizeof kForNameBytes) &&
	          Hook(base, kRvaKeyOne, kKeyOneBytes, sizeof kKeyOneBytes, (void*)&HkKeyOne, (void**)&g_keyOne, "shader key");
	if (sh && !g_sync && !Hook(base, kRvaSync, kSyncBytes, sizeof kSyncBytes, (void*)&HkSync, (void**)&g_sync, "render sync")) {
		MH_DisableHook((void*)(base + kRvaKeyOne));
		sh = false;
	}
	if (sh && !g_parse) Hook(base, kRvaParse, kParseBytes, sizeof kParseBytes, (void*)&HkParse, (void**)&g_parse, "shader parse (timing)");
	else if (!sh) g_shState = -1;
	Log("applyfast: mode %d (%s); map clouds vs fog of war culled by the cloud's box: %s; facial DBA list sorted: %s; "
	    "PSO precache: %s; quests first in the level data message: %s", g_mode, g_mode == 2 ? "on + spot check" : "on", a && b ? "on" : "OFF",
	    c ? "on" : "OFF", d ? (g_psoMode == 3 ? (g_psoMenu ? "on a plugin thread from the main menu, once the pipeline cache is built (5)" : g_psoEarly ? "on a plugin thread from the load's start, after its Blocking precache (4)" : "on a plugin thread from the quest-wait point (3c)") : g_psoMode == 2 ? (retry ? "during the load (+ PostLoadLevel)" : "during the load (PostLoadLevel point OFF)") : "at the main menu") : g_psoMode ? "OFF (code differs or a hook failed: the game's own)" : "the game's own (fastload_psoahead.txt 0)",
	    e ? "on" : g_questsFirst ? "OFF (code differs)" : "off (fastload_questearly.txt)");
	Log("applyfast: shaders parsed at the main menu: %s", sh ? "on (one per menu frame)" :
	    g_menuShaders ? "OFF (code differs or hook failed)" : "off (fastload_menushaders.txt)");
}
