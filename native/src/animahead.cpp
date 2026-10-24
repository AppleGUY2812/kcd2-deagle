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

const char* const kCdf[] = { "objects/characters/humans/female/skeleton/female.cdf",
                             "objects/characters/humans/male/skeleton/male.cdf" };

uintptr_t g_base = 0;
int g_mode = 1;
long long g_freq = 1;
std::atomic<int> g_lines{0};
std::atomic<bool> g_busy{false};           // our preload is inside the character manager now
std::atomic<bool> g_levelStarted{false};   // "Loading level" ran: no preload may start after it
std::atomic<DWORD> g_preloadTid{0};
HANDLE g_done = nullptr;   // manual reset, set while no preload runs
using Fn3 = uint64_t(__fastcall*)(void*, const char*, uint64_t);
using Fn4 = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t);
Fn3 g_chr = nullptr, g_skel = nullptr;
Fn4 g_loadLevel = nullptr;

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

uint64_t __fastcall HkChr(void* skel, const char* path, uint64_t flag) {
	const long long a = Now();
	const uint64_t r = g_chr(skel, path, flag);
	const long long b = Now();
	char p[200]; Copy(p, sizeof p, path);
	if (g_lines++ < 80)
		Log("animahead: chrparams %s in %.0f ms (thread %lu%s)", p, Ms(a, b), GetCurrentThreadId(), When());
	return r;
}

uint64_t __fastcall HkSkel(void* cm, const char* path, uint64_t flags) {
	char p[200]; Copy(p, sizeof p, path);
	const bool human = strstr(p, "humans/male/skeleton") || strstr(p, "humans/female/skeleton");
	const long long a = Now();
	const uint64_t r = g_skel(cm, path, flags);
	if (human && g_lines++ < 80)
		Log("animahead: skeleton %s flags 0x%llx in %.0f ms (thread %lu%s)", p, (unsigned long long)(uint32_t)flags, Ms(a, Now()),
		    GetCurrentThreadId(), When());
	return r;
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
}

DWORD WINAPI Preload(LPVOID arg) {
	const Job job = *(Job*)arg;
	delete (Job*)arg;
	if (job.waitFirst) {   // the quest graph's menu build first: nothing else may load animations next to ours
		const long long t = Now();
		const DWORD w = WaitForSingleObject(job.waitFirst, 120000);
		Log("animahead: waited %.0f ms for the quest graph's menu build%s", Ms(t, Now()), w == WAIT_OBJECT_0 ? "" : " (gave up)");
		if (w != WAIT_OBJECT_0) return 0;
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
		Log("animahead: %s %s in %.0f ms at the main menu (free RAM %lld -> %lld MB)", cdf, ok ? "preloaded" : "FAILED", Ms(a, Now()),
		    freeBefore, ramgov::AvailMb());
	}
	Log("animahead: preload done in %.0f ms", Ms(t, Now()));
	Finish();
	return 0;
}

uint64_t __fastcall HkLoadLevel(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
	g_levelStarted = true;
	if (g_busy) {
		const long long t = Now();
		const DWORD w = WaitForSingleObject(g_done, 20000);
		Log("animahead: the level load waited %.0f ms for the preload%s", Ms(t, Now()), w == WAIT_OBJECT_0 ? "" : " (gave up after 20 s)");
	}
	g_lines = 0;   // a fresh line budget per load
	return g_loadLevel(a, b, c, d);
}

}  // namespace

void Animahead_AtMenu(void* waitFirst) {
	if (g_mode != 1 || !g_loadLevel) return;
	const char* why = nullptr;
	void* cm = ReadPtr(g_base + kRvaCharMgr);
	void** vt = cm ? (void**)ReadPtr((uintptr_t)cm) : nullptr;
	void* fn = vt ? ReadPtr((uintptr_t)(vt + 0x70 / 8)) : nullptr;
	if (!fn || (uintptr_t)fn != g_base + kRvaLoadLock || memcmp(fn, kLoadLockBytes, sizeof kLoadLockBytes))
		why = "the character manager's preload isn't where 1.5.6 has it";
	else if (g_levelStarted || Xmlahead_LevelSeen()) why = "a level load has already begun";
	else if (ramgov::AvailMb() < 1500) why = "less than 1500 MB RAM free";
	if (why) { Log("animahead: no preload at the menu: %s", why); return; }
	HANDLE h = CreateThread(nullptr, 1 << 20, Preload, new Job{ fn, (HANDLE)waitFirst }, 0, nullptr);
	if (h) CloseHandle(h);
	else Log("animahead: could not start the preload thread");
}

void Animahead_Install(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	g_base = base;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((dir + "deagle_animahead.txt").c_str(), "r")) {
		int m = 1;
		if (fscanf(fp, "%d", &m) == 1) g_mode = m == 1 ? 1 : 0;
		fclose(fp);
	}
	g_done = CreateEventA(nullptr, TRUE, TRUE, nullptr);
	const bool lvl = g_mode == 1 && Hook(kRvaLoadLevel, kLoadLevelBytes, sizeof kLoadLevelBytes, (void*)&HkLoadLevel, (void**)&g_loadLevel,
	                                     "Loading level");
	const bool chr = Hook(kRvaChrparams, kChrparamsBytes, sizeof kChrparamsBytes, (void*)&HkChr, (void**)&g_chr, "LoadCHRPARAMS");
	const bool skel = Hook(kRvaModelSkel, kModelSkelBytes, sizeof kModelSkelBytes, (void*)&HkSkel, (void**)&g_skel, "LoadModelSKEL");
	Log("animahead: mode %d (%s; deagle_animahead.txt), level-load gate %s, chrparams log %s, skeleton log %s", g_mode,
	    g_mode ? "human animation sets at the main menu" : "off", lvl ? "on" : "OFF", chr ? "on" : "OFF", skel ? "on" : "OFF");
}
