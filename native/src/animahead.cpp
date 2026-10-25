// Human animation sets loaded during the terrain wait: see animahead.h.
#include "animahead.h"
#include "log.h"
#include "ramgov.h"
#include "xmlahead.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// ---- WHGame.dll 1.5.6 (re_menu_main.md 1a / 5)
constexpr uintptr_t kRvaCharMgr = 0x492d8d0;   // gEnv->pCharacterManager
constexpr uintptr_t kRvaLoadLock = 0x97870c;   // CharacterManager::LoadAndLockResources(cm, path, flags), vtable +0x70
const unsigned char kLoadLockBytes[] = { 0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57 };
constexpr uintptr_t kRvaChrparams = 0x20f4c30;   // LoadCHRPARAMS(skeleton, path, flag): "Loaded CHRPARAMS file '%s'"
const unsigned char kChrparamsBytes[] = { 0x48,0x89,0x6c,0x24,0x10,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x30 };   // +0xd is rip-relative
constexpr uintptr_t kRvaLoadLevel = 0xb8f748;   // "============ Loading level %s ============" (Main, the level load's start)
const unsigned char kLoadLevelBytes[] = { 0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x10,0x49,0x89,0x73,0x18,0x57,0x41,0x54,0x41,0x55 };
constexpr uintptr_t kRvaModelSkel = 0xaa8278;   // LoadModelSKEL(cm, path, flags): bit 2 skips the cache lookup
const unsigned char kModelSkelBytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x57 };
// The global animation header array's add (FUN_180b14d68(db + 0x1c8, &name): find, else grow the array at +0xb8 and
// index the name; callers 180766210, 18083f874 = AnimationSet add, 180b14364). The only writer of that array: while our
// preload runs, a call from any other thread waits for it to finish (boot 39's crash: the array moved under a reader).
constexpr uintptr_t kRvaHeaderAdd = 0xb14d68;
const unsigned char kHeaderAddBytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48 };
constexpr DWORD kHeaderWaitMs = 10000;
constexpr DWORD kGateWaitMs = 20000;
// CharacterManager::OnFacialsPakChanged (Main, start-up): logs "CharacterManager::OnFacialsPakChanged()
// m_LanguageGahBaseIndexStart" right after the language's facial paks open. After it the animation system has every pak
// and the language's facial header range; the game itself loads no character until the level (boots 76-103: no
// chrparams or skeleton load before "Loading level").
constexpr uintptr_t kRvaFacials = 0x20f35e0;
const unsigned char kFacialsBytes[] = { 0x40,0x55,0x56,0x48,0x8d,0x6c,0x24,0x98,0x48,0x81,0xec,0x68,0x01,0x00,0x00 };

const char* const kCdf[] = { "objects/characters/humans/female/skeleton/female.cdf",
                             "objects/characters/humans/male/skeleton/male.cdf" };

uintptr_t g_base = 0;
int g_mode = 1;
// Where a level load meets a running preload (the second number in fastload_animahead.txt). 1 (default): the load starts
// at once and only another thread's skeleton / chrparams load waits for the preload (the header add waits as before);
// 0: "Loading level" waits for it on Main (the rule before 2026-10-25; boots 132-134 waited 1.1-1.3 s there).
int g_gate = 1;
std::atomic<long> g_gateHolds{0};
long long g_levelAt = 0;
long long g_freq = 1;
std::atomic<int> g_lines{0};
std::atomic<bool> g_busy{false};           // our preload is inside the character manager now
std::atomic<bool> g_levelStarted{false};   // "Loading level" ran: no preload may start after it
std::atomic<DWORD> g_preloadTid{0};
HANDLE g_done = nullptr;   // manual reset, set while no preload runs
std::atomic<int> g_menuState{0};   // Animahead_MenuSettled: 0 boot event 0x24 not yet, 1 preload pending/running, 2 settled
std::atomic<HANDLE> g_waitFirst{nullptr};
std::atomic<bool> g_early{false};          // mode 1: the preload was started at start-up (after the facial paks)
std::atomic<bool> g_atMenu{false};         // Animahead_AtMenu has run (boot event 0x24)
long long g_installAt = 0;
using Fn3 = uint64_t(__fastcall*)(void*, const char*, uint64_t);
using Fn4 = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t);
Fn3 g_chr = nullptr, g_skel = nullptr;
Fn4 g_loadLevel = nullptr;
using HeaderAddFn = int(__fastcall*)(void* db, void* name);
HeaderAddFn g_headerAdd = nullptr;
std::atomic<long> g_heldAdds{0};

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long a, long long b) { return (double)(b - a) * 1000.0 / g_freq; }

void Copy(char* out, size_t n, const char* s) {
	out[0] = 0;
	if (!s) return;
	__try {
		size_t i = 0;
		for (; i + 1 < n && s[i]; i++) out[i] = s[i];
		out[i] = 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}

void* ReadPtr(uintptr_t at) {
	__try { return *(void**)at; } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

const char* When() {
	if (GetCurrentThreadId() == g_preloadTid.load()) return ", our preload";
	return g_busy ? ", ANOTHER THREAD DURING OUR PRELOAD" : "";
}

// gate 1: another thread's character load waits here while our preload runs (it holds no character-manager lock at
// these entries: LoadModelSKEL's cache lookup unlocks CM+0xa8 before it returns)
void WaitPreload(const char* what, const char* path) {
	if (!g_gate || !g_busy.load() || GetCurrentThreadId() == g_preloadTid.load()) return;
	const long long a = Now();
	const DWORD w = WaitForSingleObject(g_done, kGateWaitMs);
	if (g_gateHolds++ < 30)
		Log("animahead: %s %s on thread %lu held %.0f ms until our preload was done%s", what, path, GetCurrentThreadId(), Ms(a, Now()),
		    w == WAIT_OBJECT_0 ? "" : " (GAVE UP after 20 s)");
}

uint64_t __fastcall HkChr(void* skel, const char* path, uint64_t flag) {
	char p[200]; Copy(p, sizeof p, path);
	WaitPreload("chrparams", p);
	const long long a = Now();
	const uint64_t r = g_chr(skel, path, flag);
	const long long b = Now();
	if (g_lines++ < 80)
		Log("animahead: chrparams %s in %.0f ms (thread %lu%s)", p, Ms(a, b), GetCurrentThreadId(), When());
	return r;
}

uint64_t __fastcall HkSkel(void* cm, const char* path, uint64_t flags) {
	char p[200]; Copy(p, sizeof p, path);
	const bool human = strstr(p, "humans/male/skeleton") || strstr(p, "humans/female/skeleton");
	WaitPreload("skeleton", p);
	const long long a = Now();
	const uint64_t r = g_skel(cm, path, flags);
	if (human && g_lines++ < 80)
		Log("animahead: skeleton %s flags 0x%llx in %.0f ms (thread %lu%s)", p, (unsigned long long)(uint32_t)flags, Ms(a, Now()),
		    GetCurrentThreadId(), When());
	return r;
}

int __fastcall HkHeaderAdd(void* db, void* name) {
	if (g_busy.load() && GetCurrentThreadId() != g_preloadTid.load()) {
		const long long a = Now();
		const DWORD w = WaitForSingleObject(g_done, kHeaderWaitMs);
		if (g_heldAdds++ < 20)
			Log("animahead: header add from thread %lu held %.0f ms until our preload was done%s", GetCurrentThreadId(), Ms(a, Now()),
			    w == WAIT_OBJECT_0 ? "" : " (GAVE UP after 10 s)");
	}
	return g_headerAdd(db, name);
}

bool Hook(uintptr_t rva, const unsigned char* bytes, size_t n, void* hk, void** orig, const char* name) {
	void* t = (void*)(g_base + rva);
	if (memcmp(t, bytes, n)) { Log("animahead: %s code differs - not hooked", name); return false; }
	if (MH_CreateHook(t, hk, orig) != MH_OK || MH_EnableHook(t) != MH_OK) { Log("animahead: %s hook failed", name); return false; }
	return true;
}

}  // namespace

namespace {

struct Job { void* fn; HANDLE waitFirst; };

void Finish() {
	g_preloadTid = 0;
	g_busy = false;
	SetEvent(g_done);
	g_menuState = 2;
}

DWORD WINAPI Preload(LPVOID arg) {
	const Job job = *(Job*)arg;
	delete (Job*)arg;
	{   // hitch's start-up window samples threads named "JobWorker_*": so this one shows up in fastload_hitch.csv
		using SetDescFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
		static const SetDescFn f = (SetDescFn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetThreadDescription");
		if (f) f(GetCurrentThread(), L"JobWorker_fastload animahead");
	}
	if (job.waitFirst && g_mode == 2) {   // mode 2: the quest graph's menu build first (the rule before 2026-10-24)
		const long long t = Now();
		const DWORD w = WaitForSingleObject(job.waitFirst, 120000);
		Log("animahead: waited %.0f ms for the quest graph's menu build%s", Ms(t, Now()), w == WAIT_OBJECT_0 ? "" : " (gave up)");
		if (w != WAIT_OBJECT_0) { g_menuState = 2; return 0; }
	}
	// Dekker with "Loading level" (seq_cst): either we see its flag, or it sees us busy and waits for g_done
	g_preloadTid = GetCurrentThreadId();
	ResetEvent(g_done);
	g_busy = true;
	if (g_levelStarted || Xmlahead_LevelSeen()) {
		Log("animahead: a level load began first: no preload");
		Finish();
		return 0;
	}
	void* cm = ReadPtr(g_base + kRvaCharMgr);
	using LoadLockFn = bool(__fastcall*)(void*, const char*, uint32_t);
	const long long t = Now();
	for (const char* cdf : kCdf) {
		const long long freeBefore = ramgov::AvailMb(), a = Now();
		const bool ok = ((LoadLockFn)job.fn)(cm, cdf, 0);
		Log("animahead: %s %s in %.0f ms %s (free RAM %lld -> %lld MB)", cdf, ok ? "preloaded" : "FAILED", Ms(a, Now()),
		    g_early ? "at start-up" : "at the main menu", freeBefore, ramgov::AvailMb());
	}
	const long long lvl = g_levelAt;
	char after[96] = "";
	if (lvl) snprintf(after, sizeof after, ", %.0f ms after the level load began", Ms(lvl, Now()));
	Log("animahead: preload done in %.0f ms (%.1f s after the install%s)", Ms(t, Now()), Ms(g_installAt, Now()) / 1000.0, after);
	Finish();
	return 0;
}

uint64_t __fastcall HkLoadLevel(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
	g_levelAt = Now();
	g_levelStarted = true;
	if (g_busy && g_gate) {
		Log("animahead: the level load starts with the preload still running: other threads' skeleton / chrparams loads wait for it");
	} else if (g_busy) {
		const long long t = Now();
		const DWORD w = WaitForSingleObject(g_done, 20000);
		Log("animahead: the level load waited %.0f ms for the preload%s", Ms(t, Now()), w == WAIT_OBJECT_0 ? "" : " (gave up after 20 s)");
	}
	g_lines = 0;   // a fresh line budget per load
	return g_loadLevel(a, b, c, d);
}

}  // namespace

namespace {
// the game's preload, or null and why not
void* PreloadFn(const char** why) {
	*why = nullptr;
	void* cm = ReadPtr(g_base + kRvaCharMgr);
	void** vt = cm ? (void**)ReadPtr((uintptr_t)cm) : nullptr;
	void* fn = vt ? ReadPtr((uintptr_t)(vt + 0x70 / 8)) : nullptr;
	if (!fn || (uintptr_t)fn != g_base + kRvaLoadLock || memcmp(fn, kLoadLockBytes, sizeof kLoadLockBytes))
		*why = "the character manager's preload isn't where 1.5.6 has it";
	else if (g_levelStarted || Xmlahead_LevelSeen()) *why = "a level load has already begun";
	else if (ramgov::AvailMb() < 1500) *why = "less than 1500 MB RAM free";
	return *why ? nullptr : fn;
}

// mode 1: start-up, right after the language's facial paks (Main, once). Boot 102: the preload started at the menu
// (event 0x24), the auto-load 0.5 s later, and "Loading level" waited 3.2 s for it on Main. From here it has ~4 s
// before the menu.
Fn4 g_facials = nullptr;
uint64_t __fastcall HkFacials(uint64_t cm, uint64_t a2, uint64_t a3, uint64_t a4) {
	const uint64_t r = g_facials(cm, a2, a3, a4);
	static std::atomic<bool> once{false};
	if (once.exchange(true)) return r;
	const char* why = nullptr;
	void* fn = PreloadFn(&why);
	if (!fn) { Log("animahead: no preload at start-up (%s): at the menu instead", why); return r; }
	g_early = true;
	g_menuState = 1;
	Log("animahead: preload starts at start-up, after the facial paks (%.1f s after the install, thread %lu)",
	    Ms(g_installAt, Now()) / 1000.0, GetCurrentThreadId());
	HANDLE h = CreateThread(nullptr, 1 << 20, Preload, new Job{ fn, nullptr }, 0, nullptr);
	if (h) CloseHandle(h);
	else { g_early = false; g_menuState = 0; Log("animahead: could not start the preload thread: at the menu instead"); }
	return r;
}
}  // namespace

void Animahead_AtMenu(void* waitFirst) {
	g_waitFirst = (HANDLE)waitFirst;
	g_atMenu = true;
	if (g_early) {   // started at start-up: running (state 1) or done (2); the load waits for it as before
		Log("animahead: at the menu: the start-up preload is %s", g_menuState.load() == 2 ? "done" : "still running");
		return;
	}
	g_menuState = 2;   // set back to 1 below when a preload thread starts
	if (g_mode != 1 && g_mode != 2 && g_mode != 3) return;
	if (!g_loadLevel) return;
	if (g_mode != 2 && !g_headerAdd) { Log("animahead: header add not guarded - after the quest build (mode 2's rule)"); g_mode = 2; }
	const char* why = nullptr;
	void* fn = PreloadFn(&why);
	if (!fn) { Log("animahead: no preload at the menu: %s", why); return; }
	g_menuState = 1;
	HANDLE h = CreateThread(nullptr, 1 << 20, Preload, new Job{ fn, (HANDLE)waitFirst }, 0, nullptr);
	if (h) CloseHandle(h);
	else { g_menuState = 2; Log("animahead: could not start the preload thread"); }
}

void Animahead_Install(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	g_installAt = Now();
	g_base = base;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((dir + "fastload_animahead.txt").c_str(), "r")) {
		int m = 1, g = 1;
		if (fscanf(fp, "%d", &m) == 1) g_mode = m >= 1 && m <= 3 ? m : 0;
		if (fscanf(fp, "%d", &g) == 1) g_gate = g == 0 ? 0 : 1;
		fclose(fp);
	}
	g_done = CreateEventA(nullptr, TRUE, TRUE, nullptr);
	const bool lvl = g_mode != 0 && Hook(kRvaLoadLevel, kLoadLevelBytes, sizeof kLoadLevelBytes, (void*)&HkLoadLevel, (void**)&g_loadLevel,
	                                     "Loading level");
	const bool chr = Hook(kRvaChrparams, kChrparamsBytes, sizeof kChrparamsBytes, (void*)&HkChr, (void**)&g_chr, "LoadCHRPARAMS");
	const bool skel = Hook(kRvaModelSkel, kModelSkelBytes, sizeof kModelSkelBytes, (void*)&HkSkel, (void**)&g_skel, "LoadModelSKEL");
	const bool hdr = (g_mode == 1 || g_mode == 3) && Hook(kRvaHeaderAdd, kHeaderAddBytes, sizeof kHeaderAddBytes, (void*)&HkHeaderAdd,
	                                     (void**)&g_headerAdd, "animation header add");
	if (g_gate && !(chr && skel && hdr)) g_gate = 0;   // gate 1 needs all three guards; else "Loading level" waits
	// mode 1 starts at start-up only with the level-load gate and the header-add guard (else at the menu, as mode 3)
	const bool fac = g_mode == 1 && lvl && hdr && Hook(kRvaFacials, kFacialsBytes, sizeof kFacialsBytes, (void*)&HkFacials,
	                                                   (void**)&g_facials, "OnFacialsPakChanged");
	Log("animahead: mode %d (%s; fastload_animahead.txt), a running preload holds %s, level-load hook %s, chrparams log %s, skeleton log %s, header-add "
	    "guard %s, start-up trigger %s", g_mode,
	    g_mode == 1 ? "human animation sets at start-up, after the facial paks" :
	    g_mode == 3 ? "human animation sets at the main menu, beside the quest build" :
	    g_mode == 2 ? "human animation sets at the main menu, after the quest build" : "off",
	    g_gate ? "only other threads' skeleton / chrparams loads (gate 1)" : "the level load's start (gate 0)", lvl ? "on" : "OFF",
	    chr ? "on" : "OFF", skel ? "on" : "OFF", hdr ? "on" : g_mode != 2 && g_mode ? "OFF" : "off",
	    fac ? "on" : g_mode == 1 ? "OFF (at the menu instead)" : "off");
}

bool Animahead_MenuSettled() {   // terrainfast's menu preload: nothing loads animations at the menu now (any thread)
	if (!g_atMenu.load() || g_menuState.load() != 2) return false;
	const HANDLE w = g_waitFirst.load();
	return !w || WaitForSingleObject(w, 0) == WAIT_OBJECT_0;   // questmenu's build (or a questbench) has finished
}
