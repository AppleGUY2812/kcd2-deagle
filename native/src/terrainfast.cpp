// The load's main-thread stream finalize, timed and run more often: see terrainfast.h.
#include "terrainfast.h"
#include "animahead.h"
#include "loadwatch.h"
#include "log.h"
#include "questmenu.h"
#include "ramgov.h"
#include "readiness.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>
#include "MinHook.h"

namespace {

struct Target { const char* name; uintptr_t rva; unsigned char bytes[10]; void* orig; };
Target g_t[] = {
	{ "UpdateLoadingScreen", 0x6a0708, { 0x48,0x89,0x5c,0x24,0x08,0x55,0x56,0x57,0x48,0x83 }, nullptr },
	{ "FinalizeIOJobs", 0x489670, { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10 }, nullptr },
	{ "CGF StreamOnComplete", 0x48b5a0, { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10 }, nullptr },
	{ "LOD StreamOnComplete", 0x117e998, { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68 }, nullptr },
};
enum { kTick, kFin, kCgf, kLod, kN };
constexpr uintptr_t kRvaFinStats = 0x48ae28;   // IStreamEngine vt+0x38: the finalize + its stat copies, what the tick calls
const unsigned char kFinStatsBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8 };
constexpr uintptr_t kRvaMainTid = 0x492d9a8;   // the main thread's id, as UpdateLoadingScreen checks it

using TickFn = bool(__fastcall*)(void* sys);
using FinFn = void(__fastcall*)(void* se);
using Fn4 = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t);   // StreamOnComplete(this, stream, error)

std::atomic<int> g_mode{1};   // 2 measured no gain (boot 48): the finalize is Main CPU
std::atomic<int> g_ms{1};
uintptr_t g_base = 0;
FinFn g_finStats = nullptr;
void* g_se = nullptr;            // the stream engine, from the first main-thread finalize
long long g_freq = 1, g_lastFin = 0;
bool g_inFin = false, g_inPump = false, g_wasWait = false;

inline double Ms(long long t) { return (double)t * 1000.0 / g_freq; }
inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline bool OnMain() { return GetCurrentThreadId() == *(const DWORD*)(g_base + kRvaMainTid); }

// ---- terrain meshes ahead (terrainfast.h "Menu preload")
constexpr uintptr_t kRvaAsync = 0x52d91c;    // CObjManager async CGF load (objMgr, name, -, -, -, callback): the callback is
// argument 6 ([rsp+0x1b8] after 0x188 of pushes + frame = entry rsp+0x30; the callers store it at their [rsp+0x28]).
// Boot 53 crashed with it forwarded as argument 5: every brush load got a garbage callback.
const unsigned char kAsyncBytes[] = { 0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20 };
constexpr uintptr_t kRvaObjMgr = 0x5496fb0;  // CObjManager*
using AsyncFn = void(__fastcall*)(void* objMgr, const char* name, uint64_t, uint64_t, uint64_t, void* callback);
AsyncFn g_origAsync = nullptr;
std::atomic<int> g_ahead{1};
std::string g_dir;
constexpr int kPerFrame = 16, kInFlight = 128;
// record (main thread, during a load): the global names, in the order the load asks for them
std::vector<std::string> g_rec;
std::unordered_set<std::string> g_recSet;
std::string g_recLevel;
bool g_ourCall = false;
// preload (main thread, at the menu, before the session's first load)
enum class Ahead { Idle, Run, Done };
Ahead g_aState = Ahead::Idle;
bool g_loadSeen = false;
std::vector<std::string> g_list;
std::string g_listLevel;
size_t g_next = 0;
long long g_menuCgf = 0, g_menuCgfT = 0, g_menuFinMax = 0, g_aT0 = 0, g_lastIssue = 0, g_ramWaits = 0, g_aEnd = 0, g_animWaits = 0;

// "levels/<name>/..." or "%level%/..." -> a level's own file (from its level paks, mounted only after Continue)
bool LevelName(const char* name, std::string* level) {
	std::string n;
	for (const char* c = name; *c; c++) n += *c == '\\' ? '/' : (char)tolower((unsigned char)*c);
	if (!n.compare(0, 5, "data/")) n.erase(0, 5);
	if (!n.compare(0, 7, "%level%")) return true;
	if (n.compare(0, 7, "levels/")) return false;
	const size_t e = n.find('/', 7);
	if (level && level->empty() && e != std::string::npos) *level = n.substr(7, e - 7);
	return true;
}

void __fastcall HkAsync(void* objMgr, const char* name, uint64_t a3, uint64_t a4, uint64_t a5, void* cb) {
	if (!g_ourCall && name && *name && OnMain() && Readiness_LevelState() >= 1 && g_rec.size() < 100000 &&
	    !LevelName(name, &g_recLevel) && g_recSet.insert(name).second)
		g_rec.push_back(name);
	g_origAsync(objMgr, name, a3, a4, a5, cb);
}

std::string AheadFile(const std::string& level) { return g_dir + "fastload_terrainahead_" + level + ".txt"; }

void AheadSummary(const char* why) {
	Log("terrainfast: menu preload (%s level %s): %zu of %zu meshes asked, %lld finished at the menu (%.0f ms on Main, "
	    "longest finalize %.1f ms), %.1f s, %lld frames waited for RAM, %lld for animation loads", why, g_listLevel.c_str(), g_next, g_list.size(), g_menuCgf,
	    Ms(g_menuCgfT), Ms(g_menuFinMax), (double)((g_aEnd ? g_aEnd : Now()) - g_aT0) / g_freq, g_ramWaits, g_animWaits);
}

// Main-thread counters: [0] the whole load, [1] the terrain wait.
struct Tally {
	long long fin = 0, pumped = 0, finT = 0, finMax = 0, cgf = 0, cgfT = 0, lod = 0, lodT = 0, ticks = 0, tickT = 0;
};
Tally g_s[2];


std::string Line(const Tally& s) {
	char b[400];
	snprintf(b, sizeof b, "finalize %lld calls (%lld ours), %.0f ms, longest %.1f ms; CGF %lld (%.0f ms), LOD %lld (%.0f ms); "
	         "loading-screen ticks %lld (%.0f ms)", s.fin, s.pumped, Ms(s.finT), Ms(s.finMax), s.cgf, Ms(s.cgfT), s.lod,
	         Ms(s.lodT), s.ticks, Ms(s.tickT));
	return b;
}

void __fastcall HkFin(void* se) {
	if (g_inFin || !OnMain()) { ((FinFn)g_t[kFin].orig)(se); return; }   // the game's guard handles a nested call
	if (!g_se) g_se = se;
	g_inFin = true;
	const long long a = Now();
	((FinFn)g_t[kFin].orig)(se);
	const long long d = Now() - a;
	g_inFin = false;
	g_lastFin = a + d;
	if (Readiness_LevelState() < 1 && d > g_menuFinMax) g_menuFinMax = d;
	const bool w = Loadwatch_InTerrainWait();
	for (int i = 0; i < (w ? 2 : 1); i++) {
		Tally& s = g_s[i];
		s.fin++; s.finT += d; if (d > s.finMax) s.finMax = d;
		if (g_inPump) s.pumped++;
	}
}

template <int I> uint64_t __fastcall HkDone(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {   // I = kCgf or kLod
	if (!g_inFin) return ((Fn4)g_t[I].orig)(a, b, c, d);   // only what the main-thread finalize runs is counted
	const long long t = Now();
	const uint64_t r = ((Fn4)g_t[I].orig)(a, b, c, d);
	const long long e = Now() - t;
	if (I == kCgf && Readiness_LevelState() < 1) { g_menuCgf++; g_menuCgfT += e; }
	const bool w = Loadwatch_InTerrainWait();
	for (int i = 0; i < (w ? 2 : 1); i++) {
		if (I == kCgf) { g_s[i].cgf++; g_s[i].cgfT += e; } else { g_s[i].lod++; g_s[i].lodT += e; }
	}
	return r;
}

bool __fastcall HkTick(void* sys) {
	const int mode = g_mode.load(std::memory_order_relaxed);
	if (!mode || !OnMain()) return ((TickFn)g_t[kTick].orig)(sys);
	const long long finBefore = g_s[0].fin;
	const long long a = Now();
	const bool r = ((TickFn)g_t[kTick].orig)(sys);
	const long long b = Now();
	const bool w = Loadwatch_InTerrainWait();
	if (g_s[0].fin != finBefore)   // the game's own 20 ms tick ran (its finalize is inside)
		for (int i = 0; i < (w ? 2 : 1); i++) { g_s[i].ticks++; g_s[i].tickT += b - a; }
	if (mode == 2 && g_se && g_finStats && (b - g_lastFin) * 1000 >= (long long)g_ms.load() * g_freq) {
		using LoadingFn = bool(__fastcall*)(void*);
		const LoadingFn loading = (*(LoadingFn**)sys)[0x128 / 8];   // the game's own "is loading" check in this tick
		if (loading(sys)) { g_inPump = true; g_finStats(g_se); g_inPump = false; }
	}
	if (g_wasWait && !w) {
		Log("terrainfast: terrain wait: %s", Line(g_s[1]).c_str());
		g_s[1] = Tally();
	}
	g_wasWait = w;
	return r;
}

void* const g_hk[kN] = { (void*)&HkTick, (void*)&HkFin, (void*)&HkDone<kCgf>, (void*)&HkDone<kLod> };

}  // namespace

void Terrainfast_Frame() {   // main thread, every frame (fastload_main.cpp Tick)
	if (!g_origAsync) return;
	if (Readiness_LevelState() >= 1) {
		if (!g_loadSeen) {
			g_loadSeen = true;
			if (g_aState == Ahead::Run) { g_aState = Ahead::Done; AheadSummary("stopped by the load"); }
			else if (g_aState == Ahead::Done && !g_list.empty()) AheadSummary("done before the load");
		}
		return;
	}
	if (g_loadSeen || !g_ahead.load(std::memory_order_relaxed) || g_aState == Ahead::Done) return;
	void* objMgr = *(void**)(g_base + kRvaObjMgr);
	if (!objMgr) return;
	// The animation header list takes no lock (animahead.h, boot 39's crash), and some meshes add animations: never next to
	// animahead's preload or questmenu's build.
	if (!Animahead_MenuSettled()) { if (g_aState == Ahead::Run) g_animWaits++; return; }
	if (g_aState == Ahead::Idle) {
		const char* lv = Questmenu_SaveLevel();
		if (!lv) return;
		g_listLevel = lv;
		for (char& c : g_listLevel) c = (char)tolower((unsigned char)c);
		g_aState = Ahead::Done;
		FILE* f = fopen(AheadFile(g_listLevel).c_str(), "r");
		if (!f) { Log("terrainfast: no menu preload list for level %s (the next load records one)", g_listLevel.c_str()); return; }
		char line[1024];
		while (fgets(line, sizeof line, f)) {
			line[strcspn(line, "\r\n")] = 0;
			if (line[0] && !LevelName(line, nullptr)) g_list.emplace_back(line);
		}
		fclose(f);
		if (g_list.empty()) return;
		g_aState = Ahead::Run;
		g_aT0 = Now();
		Log("terrainfast: menu preload of %zu terrain meshes for level %s starts (%d a frame, RAM rule)", g_list.size(),
		    g_listLevel.c_str(), kPerFrame);
	}
	if (!ramgov::CanTake(64ll << 20)) { g_ramWaits++; return; }
	const long long now = Now();
	if ((long long)g_next - g_menuCgf >= kInFlight && now - g_lastIssue < g_freq) return;   // 1 s: some come back without a completion
	g_ourCall = true;
	for (int i = 0; i < kPerFrame && g_next < g_list.size(); i++) g_origAsync(objMgr, g_list[g_next++].c_str(), 0, 0, 0, nullptr);
	g_ourCall = false;
	g_lastIssue = now;
	if (g_next == g_list.size()) { g_aState = Ahead::Done; g_aEnd = Now(); AheadSummary("all asked"); }
}

void Terrainfast_OnLoaded() {
	if (!g_base) return;
	if (g_rec.size() >= 100 && !g_recLevel.empty()) {
		if (FILE* f = fopen(AheadFile(g_recLevel).c_str(), "w")) {
			for (auto& n : g_rec) fprintf(f, "%s\n", n.c_str());
			fclose(f);
			Log("terrainfast: %zu global meshes of level %s recorded for the next start's menu preload", g_rec.size(), g_recLevel.c_str());
		}
	}
	g_rec.clear(); g_recSet.clear(); g_recLevel.clear();
	Log("terrainfast: load (mode %d, every %d ms): %s", g_mode.load(), g_ms.load(), Line(g_s[0]).c_str());
	g_s[0] = Tally();
}

void Terrainfast_Command(const char* args) {
	int m = -1, ms = -1, ah = -1;
	const int n = args ? sscanf_s(args, "%d %d %d", &m, &ms, &ah) : 0;
	if (n >= 1 && m >= 0 && m <= 2) g_mode = m;
	if (n >= 2 && ms >= 0 && ms <= 20) g_ms = ms;
	if (n >= 3 && ah >= 0 && ah <= 1) g_ahead = ah;
	Log("terrainfast: mode %d (0 off, 1 time only, 2 finalize every %d ms during loads), menu preload %d%s", g_mode.load(),
	    g_ms.load(), g_ahead.load(), g_base ? "" : "; hooks not on");
	if (g_aState != Ahead::Idle && !g_list.empty()) AheadSummary("so far");
}

void Terrainfast_Install(uintptr_t base, void* self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string file = path;
	g_dir = file.substr(0, file.find_last_of('\\') + 1);
	file = g_dir + "fastload_terrainfast.txt";
	if (FILE* fp = fopen(file.c_str(), "r")) {
		int m = -1, ms = -1, ah = -1;
		const int n = fscanf(fp, "%d %d %d", &m, &ms, &ah);
		if (n >= 1 && m >= 0 && m <= 2) g_mode = m;
		if (n >= 2 && ms >= 0 && ms <= 20) g_ms = ms;
		if (n >= 3 && ah >= 0 && ah <= 1) g_ahead = ah;
		fclose(fp);
	}
	if (!g_mode) { Log("terrainfast: off (fastload_terrainfast.txt)"); return; }
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	if (memcmp((void*)(base + kRvaFinStats), kFinStatsBytes, sizeof kFinStatsBytes)) {
		Log("terrainfast: code check failed (stream finalize) - not installed"); return;
	}
	for (Target& t : g_t)
		if (memcmp((void*)(base + t.rva), t.bytes, sizeof t.bytes)) { Log("terrainfast: code check failed (%s) - not installed", t.name); return; }
	g_base = base;   // read by the hooks (main thread id)
	g_finStats = (FinFn)(base + kRvaFinStats);
	int on = 0;
	for (int i = 0; i < kN; i++) {
		void* p = (void*)(base + g_t[i].rva);
		if (MH_CreateHook(p, g_hk[i], &g_t[i].orig) == MH_OK && MH_EnableHook(p) == MH_OK) { on++; continue; }
		Log("terrainfast: hook %s FAILED", g_t[i].name);
		for (int j = 0; j < i; j++) MH_DisableHook((void*)(base + g_t[j].rva));
		g_base = 0;
		return;
	}
	Log("terrainfast: mode %d (%s), %d hooks on", g_mode.load(),
	    g_mode == 2 ? "the stream finalize runs every 1 ms of loading-screen ticks, not every 20 ms" : "timing only", on);
	void* p = (void*)(base + kRvaAsync);
	if (memcmp(p, kAsyncBytes, sizeof kAsyncBytes)) { Log("terrainfast: code check failed (async CGF load): no menu preload"); return; }
	if (MH_CreateHook(p, (void*)&HkAsync, (void**)&g_origAsync) != MH_OK || MH_EnableHook(p) != MH_OK) {
		g_origAsync = nullptr; Log("terrainfast: async CGF load hook FAILED: no menu preload"); return;
	}
	Log("terrainfast: menu preload %s; the load's global meshes are recorded (fastload_terrainahead_<level>.txt)",
	    g_ahead.load() ? "on" : "off (third number in fastload_terrainfast.txt)");
}
