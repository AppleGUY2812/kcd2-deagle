// The quest graph built at the main menu and kept by the first load: see questmenu.h.
#include "questmenu.h"
#include "animahead.h"
#include "questprof.h"
#include "questdump.h"
#include "questreplay.h"
#include "log.h"
#include "xmlahead.h"
#include "loadwatch.h"
#include "questfast.h"
#include "MinHook.h"
#include <windows.h>
#include <shlobj.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

uintptr_t g_base = 0;
std::string g_dir;
long long g_freq = 1;
int g_mode = 1;
bool g_conceptHook = false;   // applyfast's concept OnMessage hook is on (the disarm / wait checks need it)

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
double SecSince(long long t) { return (Now() - t) * 1.0 / g_freq; }
unsigned long long FreeMb() { MEMORYSTATUSEX m{ sizeof m }; GlobalMemoryStatusEx(&m); return m.ullAvailPhys >> 20; }

// ---- game addresses (WHGame.dll 1.5.6)
constexpr uintptr_t kRvaConceptModule = 0x5480280;   // DAT_185480280: C_ConceptModule*; manager at +0x18, cvar block *(+0x28)
constexpr uintptr_t kRvaRoot = 0x549d388;            // DAT_18549d388 (FUN_1809155c8): quest manager *(*(root+0x118)+0x10)
constexpr uintptr_t kRvaJobBody = 0x1e2b650;         // the ConceptLoadJob lambda's body (hooked by loadwatch)
constexpr uintptr_t kRvaNormalise = 0x1141428;       // FUN_181141428(mgr): what msg 0x18 does with mgr+0x2e0 after a job
constexpr uintptr_t kRvaStrCtor = 0x4f66cc, kRvaStrRelease = 0x4fc624;   // CryString(const char*) / release
constexpr uintptr_t kRvaEvt24 = 0xfc4de8;            // boot system event 0x24: UpdateSaveGameDescriptions + playline
const unsigned char kNormaliseBytes[] = { 0x48,0x8d,0x81,0xf0,0x02,0x00,0x00,0x48,0x89,0x81,0xe8,0x02,0x00,0x00 };
const unsigned char kStrCtorBytes[] = { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18 };
const unsigned char kStrReleaseBytes[] = { 0x48,0x83,0xec,0x28,0x48,0x8b,0x09,0x48,0x83,0xc1,0xf4 };
const unsigned char kJobBodyBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x54,0x24,0x10,0x57,0x48,0x83,0xec,0x20 };
const unsigned char kEvt24Bytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0x49,0x68,0x48,0x8b,0x49,0x48 };

struct Inputs {
	std::string level;                 // M[0]
	std::vector<std::string> M, P;
	int cv20 = -1, cv24 = -1;          // wh_concept_RandomEventStreaming, wh_concept_MemorySavingDoNotLoadOtherLevel
	int dg[4] = { -1, -1, -1, -1 };    // quest manager delegate counts (+8, +0x18, +0x28, +0x38)
	unsigned dllStamp = 0, dllSize = 0;
};
bool SameInputs(const Inputs& a, const Inputs& b) { return a.M == b.M && a.P == b.P && a.cv20 == b.cv20 && a.cv24 == b.cv24; }

Inputs g_cache;
bool g_cacheOk = false;
std::string g_saveLevel, g_saveName;
long long g_saveTime = 0;
HANDLE g_scanDone = nullptr;

std::atomic<int> g_phase{0};       // 0 idle, 1 building, 2 built, 3 build failed, 4 kept, 5 not kept
std::atomic<bool> g_armed{false}, g_disarmed{false};
HANDLE g_built = nullptr;
uintptr_t g_mgr = 0;
std::vector<uintptr_t> g_snap;     // mgr+0x48 project pointers right after the build
size_t g_snapN = 0;
thread_local bool t_build = false;
long long g_buildT0 = 0, g_buildT1 = 0;
std::atomic<unsigned long long> g_typesSeen[2];   // concept message types (< 128) logged once each

// ---- raw reads (no C++ objects in __try functions)
uintptr_t Module() { return *(uintptr_t*)(g_base + kRvaConceptModule); }
bool ReadStrVec(uintptr_t vec, const char** out, int maxN, int* n) {
	__try {
		const uintptr_t b = *(uintptr_t*)vec, e = *(uintptr_t*)(vec + 8);
		if (e < b || (e - b) % 8 || (e - b) / 8 > (uintptr_t)maxN) return false;
		*n = (int)((e - b) / 8);
		for (int i = 0; i < *n; i++) { out[i] = *(const char**)(b + 8 * i); if (!out[i]) return false; }
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadState(int* cv20, int* cv24, int dg[4]) {
	__try {
		const uintptr_t c = *(uintptr_t*)(Module() + 0x28);
		*cv20 = *(int*)(c + 0x20);
		*cv24 = *(int*)(c + 0x24);
		const uintptr_t root = *(uintptr_t*)(g_base + kRvaRoot);
		const uintptr_t qm = *(uintptr_t*)(*(uintptr_t*)(root + 0x118) + 0x10);
		const int offs[4] = { 0x8, 0x18, 0x28, 0x38 };
		for (int i = 0; i < 4; i++) {
			const uintptr_t arr = *(uintptr_t*)(qm + offs[i]);
			dg[i] = arr ? *(short*)(arr - 4) : 0;
		}
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// empty manager, no job, nothing running the graph yet: what a first load starts from
const char* ManagerNotEmpty(uintptr_t* mgrOut) {
	__try {
		const uintptr_t mod = Module();
		if (!mod) return "no concept module";
		const uintptr_t mgr = *(uintptr_t*)(mod + 0x18);
		if (!mgr) return "no concept manager";
		*mgrOut = mgr;
		if (*(uintptr_t*)(mgr + 8)) return "a quest job is already set";
		if (*(uintptr_t*)(mgr + 0x48) != *(uintptr_t*)(mgr + 0x50)) return "projects already loaded";
		if (*(uintptr_t*)(mgr + 0x98) != *(uintptr_t*)(mgr + 0xa0)) return "deserializers present";
		if (*(uint32_t*)(mod + 0x30)) return "module flags +0x30 set";
		const uintptr_t root = *(uintptr_t*)(g_base + kRvaRoot);
		if (*(char*)(*(uintptr_t*)root + 200)) return "the graph would start at once (root flag)";
		return nullptr;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return "exception reading the manager"; }
}
bool Snapshot(uintptr_t mgr, uintptr_t* out, size_t maxN, size_t* n) {
	__try {
		const uintptr_t b = *(uintptr_t*)(mgr + 0x48), e = *(uintptr_t*)(mgr + 0x50);
		if (e < b || (e - b) / 8 > maxN) return false;
		*n = (e - b) / 8;
		for (size_t i = 0; i < *n; i++) out[i] = *(uintptr_t*)(b + 8 * i);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void DllStamp(unsigned* stamp, unsigned* size) {
	const uintptr_t nt = g_base + *(int*)(g_base + 0x3c);
	*stamp = *(unsigned*)(nt + 8);
	*size = *(unsigned*)(nt + 0x50);
}

bool ReadInputs(uint64_t impl, Inputs& in) {   // the job lambda: +0x08 M, +0x20 manager, +0x28 P
	const char* m[16]; const char* p[64]; int nm = 0, np = 0;
	if (!ReadStrVec(impl + 0x08, m, 16, &nm) || !ReadStrVec(impl + 0x28, p, 64, &np) || nm < 1) return false;
	for (int i = 0; i < nm; i++) in.M.emplace_back(m[i]);
	for (int i = 0; i < np; i++) in.P.emplace_back(p[i]);
	in.level = in.M[0];
	ReadState(&in.cv20, &in.cv24, in.dg);
	DllStamp(&in.dllStamp, &in.dllSize);
	return true;
}

// ---- the cache file
void WriteCache(const Inputs& in) {
	const std::string out = g_dir + "deagle_questcache.txt", tmp = out + ".tmp";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f) return;
	fprintf(f, "# inputs of the last load's quest job (questmenu.cpp); delete to stop the main-menu pre-build until the next load\n");
	fprintf(f, "dll %u %u\nlevel %s\ncvars %d %d\ndelegates %d %d %d %d\n", in.dllStamp, in.dllSize, in.level.c_str(), in.cv20,
	        in.cv24, in.dg[0], in.dg[1], in.dg[2], in.dg[3]);
	for (const std::string& s : in.M) fprintf(f, "M %s\n", s.c_str());
	for (const std::string& s : in.P) fprintf(f, "P %s\n", s.c_str());
	fclose(f);
	MoveFileExA(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING);
}
bool ReadCache(Inputs& in) {
	FILE* f = fopen((g_dir + "deagle_questcache.txt").c_str(), "r");
	if (!f) return false;
	char line[1200];
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = 0;
		char s[1100];
		if (sscanf(line, "dll %u %u", &in.dllStamp, &in.dllSize) == 2) continue;
		if (sscanf(line, "cvars %d %d", &in.cv20, &in.cv24) == 2) continue;
		if (sscanf(line, "delegates %d %d %d %d", &in.dg[0], &in.dg[1], &in.dg[2], &in.dg[3]) == 4) continue;
		if (sscanf(line, "level %1099s", s) == 1) { in.level = s; continue; }
		if (!strncmp(line, "M ", 2)) in.M.emplace_back(line + 2);
		else if (!strncmp(line, "P ", 2)) in.P.emplace_back(line + 2);
	}
	fclose(f);
	return !in.M.empty() && !in.P.empty() && in.level == in.M[0];
}

// ---- which level Continue loads: the newest save over playline0-4 (re_questmenu_when.md section 3)
DWORD WINAPI ScanSaves(LPVOID) {
	PWSTR sg = nullptr;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &sg)) && sg) {
		for (int pl = 0; pl <= 4; pl++) {
			wchar_t dir[MAX_PATH], pat[MAX_PATH];
			swprintf(dir, MAX_PATH, L"%s\\KingdomCome2\\saves\\playline%d\\", sg, pl);
			swprintf(pat, MAX_PATH, L"%s*.whs", dir);
			WIN32_FIND_DATAW fd;
			HANDLE h = FindFirstFileW(pat, &fd);
			if (h == INVALID_HANDLE_VALUE) continue;
			do {
				if (wcschr(fd.cFileName, L' ')) continue;   // the game skips these (FUN_18070f21c)
				wchar_t path[MAX_PATH];
				swprintf(path, MAX_PATH, L"%s%s", dir, fd.cFileName);
				FILE* f = _wfopen(path, L"rb");
				if (!f) continue;
				char buf[8192];
				const size_t n = fread(buf, 1, sizeof buf - 1, f);
				fclose(f);
				buf[n] = 0;
				if (n < 16 || *(const uint32_t*)buf != 0xffffffffu) continue;
				for (size_t i = 8; i < n; i++) if (!buf[i]) buf[i] = ' ';
				const char* t = strstr(buf + 8, "SaveTime=\"");
				const char* l = strstr(buf + 8, "LevelName=\"");
				if (!t || !l) continue;
				const long long st = _atoi64(t + 10);
				char lv[64] = {};
				sscanf(l + 11, "%63[^\"]", lv);
				if (st > g_saveTime && lv[0]) {
					g_saveTime = st;
					g_saveLevel = lv;
					char nm[MAX_PATH] = {};
					WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, nm, sizeof nm, nullptr, nullptr);
					g_saveName = "playline" + std::to_string(pl) + "\\" + nm;
				}
			} while (FindNextFileW(h, &fd));
			FindClose(h);
		}
		CoTaskMemFree(sg);
	}
	SetEvent(g_scanDone);
	return 0;
}

// ---- the build on a plugin thread
using StrFn = void(__fastcall*)(uintptr_t*, const char*);
using RelFn = void(__fastcall*)(uintptr_t*);
bool CallBody(uint8_t* impl, uintptr_t mgr) {
	__try {
		reinterpret_cast<void(__fastcall*)(void*, uint64_t)>(g_base + kRvaJobBody)(impl, 0);
		reinterpret_cast<void(__fastcall*)(uintptr_t)>(g_base + kRvaNormalise)(mgr);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
DWORD WINAPI Build(LPVOID) {
	t_build = true;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
	std::vector<uintptr_t> m(g_cache.M.size()), p(g_cache.P.size());
	for (size_t i = 0; i < m.size(); i++) reinterpret_cast<StrFn>(g_base + kRvaStrCtor)(&m[i], g_cache.M[i].c_str());
	for (size_t i = 0; i < p.size(); i++) reinterpret_cast<StrFn>(g_base + kRvaStrCtor)(&p[i], g_cache.P[i].c_str());
	alignas(16) uint8_t impl[0x60] = {};
	*(uintptr_t*)(impl + 0x08) = (uintptr_t)m.data();
	*(uintptr_t*)(impl + 0x10) = *(uintptr_t*)(impl + 0x18) = (uintptr_t)(m.data() + m.size());
	*(uintptr_t*)(impl + 0x20) = g_mgr;
	*(uintptr_t*)(impl + 0x28) = (uintptr_t)p.data();
	*(uintptr_t*)(impl + 0x30) = *(uintptr_t*)(impl + 0x38) = (uintptr_t)(p.data() + p.size());
	const unsigned long long free0 = FreeMb();
	g_buildT0 = Now();
	const bool ok = CallBody(impl, g_mgr);
	g_buildT1 = Now();
	uintptr_t snap[64];
	size_t n = 0;
	const bool snapped = ok && Snapshot(g_mgr, snap, 64, &n);
	if (snapped) g_snap.assign(snap, snap + n);
	for (uintptr_t& s : m) reinterpret_cast<RelFn>(g_base + kRvaStrRelease)(&s);
	for (uintptr_t& s : p) reinterpret_cast<RelFn>(g_base + kRvaStrRelease)(&s);
	g_phase = ok && snapped && n ? 2 : 3;
	Log("questmenu: quest graph built at the main menu %s in %.2f s (%zu projects; free RAM %llu -> %llu MB)",
	    g_phase == 2 ? "OK" : "FAILED (exception or no projects), the load builds its own", (g_buildT1 - g_buildT0) * 1.0 / g_freq,
	    n, free0, FreeMb());
	SetEvent(g_built);
	return 0;
}

// ---- start: after boot event 0x24, on Main, once
const char* WhyNot(Inputs& now) {
	if (!g_conceptHook) return "the concept message hook is off (deagle_questearly.txt 0 or code differs)";
	if (Xmlahead_LevelSeen()) return "a level load has already begun";
	if (!g_cacheOk) return "no deagle_questcache.txt yet (written by the next load)";
	if (WaitForSingleObject(g_scanDone, 2000) != WAIT_OBJECT_0) return "the save scan did not finish";
	if (g_saveLevel.empty()) return "no save found";
	if (g_saveLevel != g_cache.level) return "the newest save is on another level than the cache";
	unsigned st = 0, sz = 0;
	DllStamp(&st, &sz);
	if (st != g_cache.dllStamp || sz != g_cache.dllSize) return "WHGame.dll changed since the cache";
	if (const char* e = ManagerNotEmpty(&g_mgr)) return e;
	if (!ReadState(&now.cv20, &now.cv24, now.dg)) return "could not read the concept cvars / quest manager";
	if (now.cv20 != g_cache.cv20 || now.cv24 != g_cache.cv24) return "concept cvars differ from the cache";
	for (int i = 0; i < 4; i++)
		if (now.dg[i] != g_cache.dg[i]) return "quest manager subscribers differ from a load's (the quest log may not be listening yet)";
	if (g_cache.dg[0] <= 0) return "the quest manager had no subscriber in the cached load";
	// the build needs what the load's own build would need, only earlier; below this the menu read-ahead is shedding too
	if (FreeMb() < 1200) return "less than 1200 MB RAM free";
	return nullptr;
}

using Evt24Fn = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t);
Evt24Fn g_evt24 = nullptr;
uint64_t __fastcall HkEvt24(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
	const uint64_t r = g_evt24(a, b, c, d);
	static bool once = false;
	if (once) return r;
	once = true;
	Inputs now;
	const char* why = WhyNot(now);
	Log("questmenu: boot event 0x24 (save list read): newest save %s (level %s, SaveTime %lld); cache level %s, %zu projects; "
	    "quest manager subscribers %d %d %d %d (cache %d %d %d %d); %s%s", g_saveName.empty() ? "-" : g_saveName.c_str(),
	    g_saveLevel.empty() ? "-" : g_saveLevel.c_str(), g_saveTime, g_cacheOk ? g_cache.level.c_str() : "-", g_cache.P.size(),
	    now.dg[0], now.dg[1], now.dg[2], now.dg[3], g_cache.dg[0], g_cache.dg[1], g_cache.dg[2], g_cache.dg[3],
	    why ? "no pre-build: " : "building the quest graph now", why ? why : "");
	if (why) { Animahead_AtMenu(nullptr); return r; }   // the human animation sets on a plugin thread (animahead.h)
	g_phase = 1;
	g_armed = true;
	Xmlahead_StartMenuNow("the quest graph is built at the menu");
	HANDLE t = CreateThread(nullptr, 8 << 20, Build, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
	if (t) CloseHandle(t);
	else { g_phase = 3; g_armed = false; SetEvent(g_built); Log("questmenu: could not start the build thread"); }
	Animahead_AtMenu(g_built);   // after the build: the animation sets must not load next to anything else
	return r;
}

// ---- questbench (questmenu.h)
std::atomic<bool> g_benching{false};
struct BenchArgs { int n; bool ab, prof, dump, rec; };
uintptr_t ReadMgr() {
	__try { const uintptr_t mod = Module(); return mod ? *(uintptr_t*)(mod + 0x18) : 0; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
DWORD WINAPI Bench(LPVOID arg) {
	const BenchArgs a = *(BenchArgs*)arg;
	delete (BenchArgs*)arg;
	t_build = true;
	std::vector<uintptr_t> m(g_cache.M.size()), p(g_cache.P.size());
	for (size_t i = 0; i < m.size(); i++) reinterpret_cast<StrFn>(g_base + kRvaStrCtor)(&m[i], g_cache.M[i].c_str());
	for (size_t i = 0; i < p.size(); i++) reinterpret_cast<StrFn>(g_base + kRvaStrCtor)(&p[i], g_cache.P[i].c_str());
	alignas(16) uint8_t impl[0x60] = {};
	*(uintptr_t*)(impl + 0x08) = (uintptr_t)m.data();
	*(uintptr_t*)(impl + 0x10) = *(uintptr_t*)(impl + 0x18) = (uintptr_t)(m.data() + m.size());
	*(uintptr_t*)(impl + 0x20) = g_mgr;
	*(uintptr_t*)(impl + 0x28) = (uintptr_t)p.data();
	*(uintptr_t*)(impl + 0x30) = *(uintptr_t*)(impl + 0x38) = (uintptr_t)(p.data() + p.size());
	bool ok = false;
	int done = 0;
	for (int k = 1; k <= a.n; k++) {
		if (Xmlahead_LevelSeen()) { Log("questbench: a level load began: stopped after %d builds", done); break; }
		if (FreeMb() < 1200) { Log("questbench: less than 1200 MB RAM free: stopped after %d builds", done); break; }
		const bool fast = !a.ab || (k % 2 == 1);
		if (a.ab) Questfast_SetOn(fast);
		double ms0[7] = {}, ms1[7] = {}, d[7];
		long long c0[7] = {}, c1[7] = {};
		Loadwatch_QuestTotals(ms0, c0);
		const unsigned long long free0 = FreeMb();
		const bool prof = a.prof && Questprof_Begin();
		std::string dumpPath;
		if (a.dump) {   // one file per build (deagle_questdump_<k>.txt): tools/questdump_diff.py compares two
			dumpPath = g_dir + "deagle_questdump_" + std::to_string(k) + ".txt";
			DeleteFileA(dumpPath.c_str());
			Questdump_Arm(dumpPath.c_str());
		}
		const char* rtag = "";
		if (a.rec) {   // build 1 records phase 2a, the others replay it (questreplay.cpp)
			const int rm = k == 1 ? 1 : 2;
			rtag = Questreplay_Arm(rm) ? (rm == 1 ? " [record]" : " [replay]") : " [record / replay could not arm]";
		}
		const long long t0 = Now();
		ok = CallBody(impl, g_mgr);
		const double s = SecSince(t0);
		if (a.rec) { Questreplay_Disarm(); if (k > 1 && Questreplay_LastFailed()) rtag = " [replay FAILED]"; }
		if (a.dump) Questdump_Disarm();
		if (prof) Questprof_End(k);
		Loadwatch_QuestTotals(ms1, c1);
		for (int i = 0; i < 7; i++) d[i] = (ms1[i] - ms0[i]) / 1000.0;
		done++;
		Log("questbench: build %d/%d %s in %.2f s: XML + definitions %.2f, 2a nodes %.2f, ports %.2f, edges %.2f, after the "
		    "projects %.2f s%s%s; free RAM %llu -> %llu MB", k, a.n, ok ? "OK" : "FAILED (exception)", s, d[2] - d[3] - d[4] - d[5],
		    d[3], d[4], d[5], d[1] - d[2], a.ab ? (fast ? " [questfast on]" : " [questfast off]") : "", rtag, free0, FreeMb());
		if (!ok) break;
	}
	if (a.ab) Questfast_SetOn(true);
	if (a.prof) Questprof_Stop();
	uintptr_t snap[64];
	size_t n = 0;
	const bool snapped = ok && Snapshot(g_mgr, snap, 64, &n) && n;
	if (snapped) g_snap.assign(snap, snap + n);
	for (uintptr_t& s : m) reinterpret_cast<RelFn>(g_base + kRvaStrRelease)(&s);
	for (uintptr_t& s : p) reinterpret_cast<RelFn>(g_base + kRvaStrRelease)(&s);
	g_buildT1 = Now();
	g_phase = snapped ? 2 : 3;
	Log("questbench: done, %d builds; %s", done, !g_armed.load() ? "no main-menu build was pending, the load builds its own" :
	    snapped ? "the first load keeps the last build" : "the last build failed: the load builds its own");
	SetEvent(g_built);
	g_benching = false;
	return 0;
}

bool InList(int type, const int* l, size_t n) { for (size_t i = 0; i < n; i++) if (l[i] == type) return true; return false; }
const int kWaitTypes[] = { 2, 5, 6, 9, 0xa, 0x18, 0x22, 0x33, 0x3c, 0x41, 0x47, 0x48, 0x4d, 0x4f };   // touch the projects
const int kDisarmTypes[] = { 2, 5, 6, 9, 0xa, 0x22, 0x33, 0x3c, 0x41, 0x47, 0x4d, 0x4f };

}  // namespace

void Questmenu_OnConceptMsg(int type) {
	const int ph = g_phase.load();
	if (ph != 1 && ph != 2) return;
	if (type >= 0 && type < 128) {   // log each type once while a pre-build is pending (which messages come at the menu)
		const unsigned long long bit = 1ull << (type & 63);
		if (!(g_typesSeen[type >> 6].fetch_or(bit) & bit))
			Log("questmenu: concept message 0x%x while the pre-build is %s", type, ph == 1 ? "running" : "waiting for the load");
	}
	if (ph == 1 && InList(type, kWaitTypes, sizeof kWaitTypes / sizeof *kWaitTypes)) {
		const long long t0 = Now();
		const DWORD w = WaitForSingleObject(g_built, 60000);
		Log("questmenu: concept message 0x%x waited %.0f ms for the pre-build%s", type, SecSince(t0) * 1000,
		    w == WAIT_OBJECT_0 ? "" : " (60 s, gave up)");
	}
	if (g_phase.load() == 2 && g_armed.load() && InList(type, kDisarmTypes, sizeof kDisarmTypes / sizeof *kDisarmTypes) &&
	    !g_disarmed.exchange(true))
		Log("questmenu: concept message 0x%x touched the projects before the load: the load will build its own graph", type);
}

bool Questmenu_OnJobBody(uint64_t impl) {
	if (t_build) return false;   // our own build: run the game's body
	Inputs real;
	const bool read = ReadInputs(impl, real);
	if (read && g_mode) WriteCache(real);
	if (!g_armed.exchange(false)) return false;
	const long long t0 = Now();
	while (WaitForSingleObject(g_built, 5000) == WAIT_TIMEOUT)
		Log("questmenu: the load's quest job waits for the main-menu build (%.0f s so far)", SecSince(t0));
	const double waitedMs = SecSince(t0) * 1000;
	const char* why = nullptr;
	uintptr_t snap[64];
	size_t n = 0;
	if (g_phase.load() != 2) why = "the pre-build failed";
	else if (g_disarmed.load()) why = "a concept message touched the projects after the build";
	else if (!read) why = "could not read the load's job inputs";
	else if (!SameInputs(real, g_cache)) why = "the load's inputs differ (another level, mods or cvars changed)";
	else if (*(uintptr_t*)(impl + 0x20) != g_mgr) why = "another concept manager";
	else if (!Snapshot(g_mgr, snap, 64, &n) || n != g_snap.size() || memcmp(snap, g_snap.data(), n * 8)) why = "the project list changed";
	if (why) {
		g_phase = 5;
		Log("questmenu: main-menu graph NOT kept (%s; waited %.0f ms): the load builds its own", why, waitedMs);
		return false;
	}
	*(uint8_t*)(g_mgr + 0x378) = 0;   // what the body's reset leaves (Serialize set it to IncludeModGraphs)
	g_phase = 4;
	Log("questmenu: quest graph KEPT from the main menu (%zu projects, built %.1f s before this load in %.2f s; the load waited "
	    "%.0f ms for it)", n, (t0 - g_buildT1) * 1.0 / g_freq, (g_buildT1 - g_buildT0) * 1.0 / g_freq, waitedMs);
	return true;
}

void Questmenu_SetConceptHook(bool on) { g_conceptHook = on; }

void Questmenu_Bench(const char* args) {
	int n = 0;
	char mode[8] = {};
	const int got = args ? sscanf(args, "%d %7s", &n, mode) : 0;
	const char* why = nullptr;
	if (got < 1) why = "usage: questbench N [ab|prof|dump|abdump|rec|recdump] (N 1-20)";
	else if (n < 1 || n > 20) why = "N must be 1-20";
	else if (!g_mode) why = "questmenu is off (deagle_questmenu.txt 0)";
	else if (!g_cacheOk) why = "no deagle_questcache.txt yet (a load writes it)";
	else if (Xmlahead_LevelSeen()) why = "only at the main menu, before the first load";
	else if (g_phase.load() == 1) why = "a build is running";
	else if (g_benching.load()) why = "a bench is running";
	else if (!(g_mgr = g_mgr ? g_mgr : ReadMgr())) why = "no concept manager";
	else if (FreeMb() < 1200) why = "less than 1200 MB RAM free";
	if (why) {
		Log("questbench: %s (state: phase %d, cache %s, last build %.2f s)", why, g_phase.load(), g_cacheOk ? g_cache.level.c_str() : "-",
		    (g_buildT1 - g_buildT0) * 1.0 / g_freq);
		return;
	}
	g_benching = true;
	ResetEvent(g_built);   // a load or a project-touching concept message waits for the bench, as for the pre-build
	g_phase = 1;
	const bool ab = !strcmp(mode, "ab") || !strcmp(mode, "abdump"), prof = !strcmp(mode, "prof"),
	           dump = !strcmp(mode, "dump") || !strcmp(mode, "abdump") || !strcmp(mode, "recdump"),
	           rec = !strcmp(mode, "rec") || !strcmp(mode, "recdump");
	HANDLE t = CreateThread(nullptr, 8 << 20, Bench, new BenchArgs{ n, ab, prof, dump, rec }, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
	if (t) { CloseHandle(t); Log("questbench: %d builds started%s%s%s", n, ab ? " (questfast on / off alternating)" : prof ? " (with the inner profile, questprof)" : "",
	                            dump ? " (graph dump per build: deagle_questdump_<k>.txt)" : "", rec ? " (build 1 records phase 2a, the others replay it)" : ""); }
	else { g_phase = 3; g_benching = false; SetEvent(g_built); Log("questbench: could not start the thread"); }
}

void Questmenu_Install(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	g_base = base;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	g_dir = path;
	g_dir = g_dir.substr(0, g_dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((g_dir + "deagle_questmenu.txt").c_str(), "r")) {
		int m = 1;
		if (fscanf(fp, "%d", &m) == 1) g_mode = m != 0;
		fclose(fp);
	}
	g_built = CreateEventA(nullptr, TRUE, FALSE, nullptr);
	g_scanDone = CreateEventA(nullptr, TRUE, FALSE, nullptr);
	if (!g_mode) { Log("questmenu: off (deagle_questmenu.txt); the load's inputs are not recorded"); return; }
	const bool code = !memcmp((void*)(base + kRvaNormalise), kNormaliseBytes, sizeof kNormaliseBytes) &&
	                  !memcmp((void*)(base + kRvaStrCtor), kStrCtorBytes, sizeof kStrCtorBytes) &&
	                  !memcmp((void*)(base + kRvaStrRelease), kStrReleaseBytes, sizeof kStrReleaseBytes) &&
	                  !memcmp((void*)(base + kRvaEvt24), kEvt24Bytes, sizeof kEvt24Bytes);
	// the job body is hooked by loadwatch (its first bytes are a jmp now); checked there
	g_cacheOk = ReadCache(g_cache);
	bool hooked = false;
	if (code) {
		void* t = (void*)(base + kRvaEvt24);
		hooked = MH_CreateHook(t, (void*)&HkEvt24, (void**)&g_evt24) == MH_OK && MH_EnableHook(t) == MH_OK;
	}
	if (hooked) { HANDLE h = CreateThread(nullptr, 0, ScanSaves, nullptr, 0, nullptr); if (h) CloseHandle(h); }
	else SetEvent(g_scanDone);
	Log("questmenu: %s; cache %s", hooked ? "on (quest graph built at the main menu when the inputs are known)" :
	    "OFF (code differs or hook failed)", g_cacheOk ? ("level " + g_cache.level + ", " + std::to_string(g_cache.P.size()) +
	    " projects").c_str() : "none yet");
	(void)kJobBodyBytes;
}
