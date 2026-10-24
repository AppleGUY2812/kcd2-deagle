// Faster pieces of the save load's "apply save" step. See applyfast.h.
#include "applyfast.h"
#include "loadwatch.h"
#include "log.h"
#include "xmlahead.h"
#include "MinHook.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <vector>
#include <intrin.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

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
// deagle_psoahead.txt: 2 during the load (default), 1 at the main menu (the menu freezes ~6 s), 0 off
int g_psoMode = 2;
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
uint64_t __fastcall HkSync(void* rt, uint64_t a2, uint64_t a3, uint64_t a4) {
	{   // when the main thread syncs during start-up (measurement: can the precache run before the menu shows?)
		const long long n = ++s_syncs, now = Now();
		if (n <= 3 || (n <= 400 && (now - g_lastSyncAt) * 1000 / g_freq >= 1000) || n == 100 || n == 400)
			Log("applyfast: render sync %lld on thread %lu (%lld ms after the one before)", n, GetCurrentThreadId(),
			    g_lastSyncAt ? (now - g_lastSyncAt) * 1000 / g_freq : -1);
		g_lastSyncAt = now;
	}
	PsoMainFrame();     // before the hand-over: the queued precache goes with this frame's commands
	ShaderMenuFrame();  // piece 5: one shader parse queued per menu frame
	return g_sync(rt, a2, a3, a4);
}

// ---- 3b. PSO precache during the load (deagle_psoahead.txt 2), no menu freeze
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
}  // namespace

void Applyfast_BeforeQuestWait() {   // main thread, from loadwatch's hook of FUN_1811414d8 (msg 0x18)
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
bool g_questsFirst = true;                 // deagle_questearly.txt: 0 off, 1 on
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

void __fastcall HkProcessMsg(void* mgr, void* msg) {
	g_processMsg(mgr, msg);
	if (t_earlyMsg == msg) t_earlyMsg = nullptr;   // this call's message is done: never match a later one at the same
	                                               // address (a nested message's call leaves the outer one's mark)
}

void __fastcall HkConceptOnMsg(void* mod, void* msg) {
	if (msg && msg == t_earlyMsg) {   // ran first already (HkPreModules)
		s_qeSkipped++;
		Log("applyfast: level data message: the quests' own turn came %.0f ms later (the quest job's head start)",
		    (Now() - t_earlyAt) * 1000.0 / g_freq);
		return;
	}
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
uint64_t __fastcall HkParse(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {   // RT_ParseShader, any thread: timing only
	const long long t0 = Now();
	const uint64_t r = g_parse(a, b, c, d);
	const long long dt = Now() - t0;
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
		break;   // one new shader per frame: its parse runs on the render thread next frame
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
	if (FILE* fp = fopen((dir + "deagle_applyfast.txt").c_str(), "r")) {
		int m = 2;
		if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 2) g_mode = m;
		fclose(fp);
	}
	if (!g_mode) { g_psoState = -1; Log("applyfast: off (deagle_applyfast.txt)"); return; }
	const bool a = Hook(base, kRvaFogAny, kFogAnyBytes, sizeof kFogAnyBytes, (void*)&HkFogAny, (void**)&g_fogAny, "fog ask");
	const bool b = a && Hook(base, kRvaFogNode, kFogNodeBytes, sizeof kFogNodeBytes, (void*)&HkFogNode, (void**)&g_fogNode,
	                         "fog node");
	if (a && !b) MH_DisableHook((void*)(base + kRvaFogAny));
	g_base = base;
	const bool c = !memcmp((void*)(base + kRvaFindChrparams), kFindCallBytes, sizeof kFindCallBytes) &&
	               Hook(base, kRvaFind, kFindBytes, sizeof kFindBytes, (void*)&HkFind, (void**)&g_find, "facial DBA list");
	if (FILE* fp = fopen((dir + "deagle_psoahead.txt").c_str(), "r")) {
		int m = 2;
		if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 2) g_psoMode = m;
		fclose(fp);
	}
	if (FILE* fp = fopen((dir + "deagle_questearly.txt").c_str(), "r")) {
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
	const bool psoCode = !memcmp((void*)(base + kRvaPsoQueue), kPsoQueueBytes, sizeof kPsoQueueBytes) &&
	                     !memcmp((void*)(base + kRvaSync), kSyncBytes, sizeof kSyncBytes);
	const bool d = g_psoMode && psoCode &&
	               (g_psoMode == 2 || Hook(base, kRvaSync, kSyncBytes, sizeof kSyncBytes, (void*)&HkSync, (void**)&g_sync, "render sync"));
	if (!d) g_psoState = -1;
	const bool retry = d && g_psoMode == 2 &&   // the later hand-over point (3b); without it, only before the quest wait
	                   Hook(base, kRvaPostLoadLevel, kPostLoadLevelBytes, sizeof kPostLoadLevelBytes, (void*)&HkPostLoadLevel,
	                        (void**)&g_postLoadLevel, "PostLoadLevel");
	// piece 5: shaders parsed at the main menu, one per frame (deagle_menushaders.txt: 1 on, default; 0 off)
	if (FILE* fp = fopen((dir + "deagle_menushaders.txt").c_str(), "r")) {
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
	if (sh) Hook(base, kRvaParse, kParseBytes, sizeof kParseBytes, (void*)&HkParse, (void**)&g_parse, "shader parse (timing)");
	else g_shState = -1;
	Log("applyfast: mode %d (%s); map clouds vs fog of war culled by the cloud's box: %s; facial DBA list sorted: %s; "
	    "PSO precache: %s; quests first in the level data message: %s", g_mode, g_mode == 2 ? "on + spot check" : "on", a && b ? "on" : "OFF",
	    c ? "on" : "OFF", d ? (g_psoMode == 2 ? (retry ? "during the load (+ PostLoadLevel)" : "during the load (PostLoadLevel point OFF)") : "at the main menu") : g_psoMode ? "OFF (code differs)" : "the game's own (deagle_psoahead.txt 0)",
	    e ? "on" : g_questsFirst ? "OFF (code differs)" : "off (deagle_questearly.txt)");
	Log("applyfast: shaders parsed at the main menu: %s", sh ? "on (one per menu frame)" :
	    g_menuShaders ? "OFF (code differs or hook failed)" : "off (deagle_menushaders.txt)");
}
