// Parsing XML ahead: see xmlahead.h.
#include "xmlahead.h"
#include "log.h"
#include "mainreads.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "MinHook.h"
#include <psapi.h>

namespace {

constexpr uintptr_t kRvaLoad = 0x979360, kRvaParse = 0x247cecc;
const unsigned char kLoadBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x60,0x0f,0x57,0xc0 };
const unsigned char kParseBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20 };
constexpr ULONGLONG kRecordMs = 10 * 60 * 1000, kKeepMs = 15 * 60 * 1000;
constexpr int kMenuWorkers = 4, kLevelWorkers = 3;
constexpr long long kMenuLoads = 1500;      // the menu has loaded (3,562 XML loads before the 2026-10-23 22:00 load)
constexpr ULONGLONG kMenuQuietMs = 3000;    // ... and no XML load for 3 s: the menu is idle
// Workers stay at most kLead list files ahead of the furthest file the game has asked for (list order = the recorded
// load order), so what they hold ahead is bounded (~kLead trees) and needs no spare RAM: boot 12 (2026-10-24) had 2.1 GB
// free at the menu and the old 2.5 GB floor parsed nothing, leaving the quest job's ~5 s XML phase on the load. They
// pause (not stop) below kMinFreeMb.
constexpr size_t kLead = 3000;
constexpr DWORDLONG kMinFreeMb = 1000;

using LoadFn = void**(__fastcall*)(void* self, void** ret, const char* file, uint64_t a4, char reportErrors);
using ParseFn = void**(__fastcall*)(void* parser, void** ret, const char* file, char cleanPools, char reportErrors);
LoadFn g_origLoad = nullptr;
ParseFn g_origParse = nullptr;
int g_mode = 1;
std::string g_dir;
ULONGLONG g_start = 0;
long long g_freq = 1, g_t0 = 0;
std::atomic<long long> g_parseCalls{0};

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

std::string Norm(const char* f) {
	std::string s;
	if (!f) return s;
	for (const char* p = f; *p; p++) {
		char c = *p == '\\' ? '/' : (char)tolower((unsigned char)*p);
		if (c == '/' && !s.empty() && s.back() == '/') continue;
		s.push_back(c);
	}
	while (s.size() > 2 && s[0] == '.' && s[1] == '/') s.erase(0, 2);
	return s;
}

void ReleaseNode(void* n) {   // XmlNodeRef dtor: vtable +0x10
	if (!n) return;
	__try { (*(void(__fastcall**)(void*))(*(uintptr_t*)n + 0x10))(n); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- recording
struct Rec { uint32_t tMs, tid, us; uint8_t ok, served; std::string file; };
std::mutex g_mu;
std::vector<Rec> g_recs;

// ---- ahead
struct Item {
	std::string path;                 // normalized
	size_t idx = 0;                   // position in the list (the recorded load order)
	std::atomic<int> state{0};        // 0 waiting, 1 parsing, 2 ready, 3 taken / dropped / failed
	void* node = nullptr;
};
std::deque<Item> g_items;
std::unordered_map<std::string, Item*> g_byPath;
std::vector<Item*> g_queue[2];        // 0: files outside levels/ (parsed at the menu), 1: levels/ files (from the load)
std::atomic<size_t> g_next[2];
std::atomic<bool> g_started[2], g_stop{false}, g_lowRam{false};
std::atomic<long long> g_loads{0};
std::atomic<size_t> g_gamePos{0};     // furthest list position the game has asked for
std::atomic<long long> s_leadWaits{0}, s_ramWaits{0};
std::atomic<ULONGLONG> g_lastLoad{0};
std::atomic<bool> g_levelSeen{false};  // the game asked for a levels/ file: a level load has begun
void* g_self = nullptr;
std::atomic<long long> s_ready{0}, s_failed{0}, s_served{0}, s_late{0}, s_parseMs{0}, s_skipped{0}, s_waited{0},
	s_waitTimeout{0}, s_waitUs{0};

bool IsLevel(const std::string& p) { return p.compare(0, 7, "levels/") == 0 || p.compare(0, 12, "data/levels/") == 0; }

size_t PrivateMb() {
	PROCESS_MEMORY_COUNTERS_EX pm{}; pm.cb = sizeof pm;
	return K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pm, sizeof pm) ? pm.PrivateUsage >> 20 : 0;
}
DWORDLONG FreeMb() { MEMORYSTATUSEX m{}; m.dwLength = sizeof m; return GlobalMemoryStatusEx(&m) ? m.ullAvailPhys >> 20 : 0; }
size_t g_privAtStart = 0;

DWORD WINAPI Worker(LPVOID arg) {
	const int q = (int)(intptr_t)arg;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);   // (normal tried in boot 25: no change)
	const size_t n = g_queue[q].size();
	for (size_t k = 0;; k++) {
		if (g_stop) break;
		if ((k & 127) == 0) {
			while (!g_stop && FreeMb() < kMinFreeMb) {   // pause while RAM is short
				if (!g_lowRam.exchange(true)) Log("xmlahead: free RAM below %llu MB - parsing ahead paused", (unsigned long long)kMinFreeMb);
				s_ramWaits++;
				Sleep(100);
			}
			g_lowRam = false;
		}
		const size_t i = g_next[q]++;
		if (i >= n) break;
		Item& it = *g_queue[q][i];
		while (!g_stop && it.idx > g_gamePos.load() + kLead && it.state.load() == 0) { s_leadWaits++; Sleep(2); }
		int w = 0;
		if (!it.state.compare_exchange_strong(w, 1)) continue;   // the game asked first
		void* node = nullptr;
		const long long a = Now();
		g_origLoad(g_self, &node, it.path.c_str(), 0, 0);
		s_parseMs += (Now() - a) * 1000 / g_freq;
		if (!node) { it.state = 3; s_failed++; continue; }
		it.node = node;
		int p = 1;
		if (it.state.compare_exchange_strong(p, 2)) s_ready++;
		else { it.node = nullptr; ReleaseNode(node); s_late++; }   // the game asked meanwhile and parsed it itself
	}
	return 0;
}

void StartQueue(int q, const char* why) {
	bool f = false;
	if (g_queue[q].empty() || !g_started[q].compare_exchange_strong(f, true)) return;
	if (!g_privAtStart) g_privAtStart = PrivateMb();
	const int n = q == 0 ? kMenuWorkers : kLevelWorkers;
	for (int i = 0; i < n; i++) CloseHandle(CreateThread(nullptr, 0, Worker, (LPVOID)(intptr_t)q, 0, nullptr));
	Log("xmlahead: %s - %d workers parsing %zu %s files ahead (%llu MB RAM free, game private %zu MB)", why, n,
	    g_queue[q].size(), q == 0 ? "non-level" : "level", (unsigned long long)FreeMb(), PrivateMb());
}

void** __fastcall HkLoad(void* self, void** ret, const char* file, uint64_t a4, char reportErrors) {
	const ULONGLONG now = GetTickCount64() - g_start;
	const bool record = now <= kRecordMs;
	const bool watch = MainReads_On();   // a mainreads window on this thread (any time, any mode)
	std::string path;
	if (g_mode == 2 || record || watch) path = Norm(file);
	g_loads++;
	g_lastLoad = GetTickCount64();
	if (!g_levelSeen && !path.empty() && IsLevel(path)) g_levelSeen = true;
	if (g_mode == 2 && !g_items.empty() && !g_stop) {
		if (!g_self) g_self = self;
		if (!g_started[1] && IsLevel(path)) { StartQueue(0, "level loading"); StartQueue(1, "level loading"); }
		auto f = g_byPath.find(path);
		if (f != g_byPath.end()) {
			Item& it = *f->second;
			for (size_t pos = g_gamePos.load(); it.idx > pos && !g_gamePos.compare_exchange_weak(pos, it.idx);) {}
			int r = 2;
			if (it.state.compare_exchange_strong(r, 3)) {
				*ret = it.node;          // the worker's reference goes to the caller
				it.node = nullptr;
				s_served++;
				if (watch) MainReads_Add('X', path.c_str(), 0, 0, Now(), 0);
				if (record) {
					Rec rc{ (uint32_t)((Now() - g_t0) * 1000 / g_freq), GetCurrentThreadId(), 0, 1, 1, path };
					std::lock_guard<std::mutex> lk(g_mu); g_recs.push_back(std::move(rc));
				}
				return ret;
			}
			if (r == 0 && it.state.compare_exchange_strong(r, 3)) s_skipped++;   // not started: the game parses it
			else if (r == 1) {
				// a worker is parsing it right now (~1 ms): wait for that tree instead of parsing the file twice;
				// after 100 ms (a lock the worker needs?) the game parses it itself and the worker drops its tree
				const long long a = Now(), lim = a + g_freq / 10;
				while (it.state.load() == 1 && Now() < lim) YieldProcessor();
				s_waitUs += (Now() - a) * 1000000 / g_freq;
				int ready = 2;
				if (it.state.compare_exchange_strong(ready, 3)) {
					*ret = it.node; it.node = nullptr; s_served++; s_waited++;
					if (watch) MainReads_Add('X', path.c_str(), 0, 0, a, Now() - a);
					if (record) {
						Rec rc{ (uint32_t)((Now() - g_t0) * 1000 / g_freq), GetCurrentThreadId(), 0, 1, 2, path };
						std::lock_guard<std::mutex> lk(g_mu); g_recs.push_back(std::move(rc));
					}
					return ret;
				}
				int p = 1;
				if (it.state.compare_exchange_strong(p, 3)) s_waitTimeout++;
			}
		}
	}
	if (!record && !watch) return g_origLoad(self, ret, file, a4, reportErrors);
	const long long a = Now();
	void** out = g_origLoad(self, ret, file, a4, reportErrors);
	const long long b = Now();
	if (watch) MainReads_Add('x', path.c_str(), 0, 0, a, b - a);
	if (!record) return out;
	Rec rc{ (uint32_t)((a - g_t0) * 1000 / g_freq), GetCurrentThreadId(), (uint32_t)((b - a) * 1000000 / g_freq),
	        (uint8_t)(ret && *ret ? 1 : 0), 0, path };
	std::lock_guard<std::mutex> lk(g_mu);
	g_recs.push_back(std::move(rc));
	return out;
}

void** __fastcall HkParse(void* parser, void** ret, const char* file, char cleanPools, char reportErrors) {
	g_parseCalls++;
	return g_origParse(parser, ret, file, cleanPools, reportErrors);
}

void Flush() {
	std::vector<Rec> recs;
	{ std::lock_guard<std::mutex> lk(g_mu); recs = g_recs; }
	const std::string tmp = g_dir + "deagle_xmlahead_trace.tmp", out = g_dir + "deagle_xmlahead_trace.csv";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f) return;
	fprintf(f, "t_ms,tid,ok,served,us,file\n");
	for (const Rec& r : recs) fprintf(f, "%u,%u,%u,%u,%u,%s\n", r.tMs, r.tid, r.ok, r.served, r.us, r.file.c_str());
	fclose(f);
	MoveFileExA(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING);
}

DWORD WINAPI Writer(LPVOID) {
	size_t written = 0;
	long long lastServed = -1, lastReady = -1;
	for (unsigned tick = 1;; tick++) {
		Sleep(500);
		if (!g_items.empty() && !g_started[0] && g_loads >= kMenuLoads && GetTickCount64() - g_lastLoad >= kMenuQuietMs)
			StartQueue(0, "main menu idle");
		if (tick % 10) continue;
		size_t n; { std::lock_guard<std::mutex> lk(g_mu); n = g_recs.size(); }
		if (n != written) { Flush(); written = n; }
		if ((g_started[0] || g_started[1]) && (s_served != lastServed || s_ready != lastReady)) {
			lastServed = s_served; lastReady = s_ready;
			Log("xmlahead: ready %lld of %zu (failed %lld, parse %lld ms), served %lld (%lld after waiting %lld ms, %lld "
			    "waits timed out), game first %lld, dropped %lld; game at %zu, workers at %zu/%zu, lead waits %lld, RAM "
			    "waits %lld; game private %zu MB (+%lld since start)",
			    s_ready.load(), g_items.size(), s_failed.load(), s_parseMs.load(), s_served.load(), s_waited.load(),
			    s_waitUs.load() / 1000, s_waitTimeout.load(), s_skipped.load(), s_late.load(), g_gamePos.load(),
			    g_next[0].load(), g_next[1].load(), s_leadWaits.load(), s_ramWaits.load(), PrivateMb(),
			    (long long)PrivateMb() - (long long)g_privAtStart);
		}
		const ULONGLONG t = GetTickCount64() - g_start;
		if (!g_items.empty() && !g_stop && t > kKeepMs) {
			g_stop = true;
			Sleep(2000);
			size_t freed = 0;
			for (Item& it : g_items) { int r = 2; if (it.state.compare_exchange_strong(r, 3)) { ReleaseNode(it.node); it.node = nullptr; freed++; } }
			Log("xmlahead: closed, %zu trees never asked for, released", freed);
		}
		if (t > kRecordMs + 10000 && (g_items.empty() || g_stop)) {
			Flush();
			Log("xmlahead: recording done, %zu loads; XmlParser::ParseFile calls %lld", n, g_parseCalls.load());
			return 0;
		}
	}
}

bool LoadList() {
	FILE* f = fopen((g_dir + "deagle_xmlahead_list.txt").c_str(), "r");
	if (!f) { Log("xmlahead: no deagle_xmlahead_list.txt - nothing to parse ahead"); return false; }
	char line[1100];
	size_t idx = 0;
	while (fgets(line, sizeof line, f)) {
		if (line[0] == '#') continue;
		size_t l = strlen(line);
		while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
		if (!l) continue;
		const std::string p = Norm(line);
		if (p.compare(0, 6, "%user%") == 0 || g_byPath.count(p)) continue;   // user files can change; once each
		g_items.emplace_back();
		Item& it = g_items.back();
		it.path = p;
		it.idx = idx++;
		g_byPath.emplace(p, &it);
		g_queue[IsLevel(p) ? 1 : 0].push_back(&it);
	}
	fclose(f);
	Log("xmlahead: list %zu files (%zu outside levels/, %zu level)", g_items.size(), g_queue[0].size(), g_queue[1].size());
	return !g_items.empty();
}

}  // namespace

bool Xmlahead_MenuQuiet(unsigned quietMs) {
	return g_loads >= kMenuLoads && !g_levelSeen && GetTickCount64() - g_lastLoad >= quietMs;
}

void Xmlahead_Install(uintptr_t base, void* self) {
	g_start = GetTickCount64();
	LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); g_freq = fq.QuadPart; g_t0 = Now();
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	g_dir = path; g_dir = g_dir.substr(0, g_dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((g_dir + "deagle_xmlahead.txt").c_str(), "r")) {
		int m = 1; if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 2) g_mode = m; fclose(fp);
	}
	if (g_mode == 0) { Log("xmlahead: off"); return; }
	void* ld = (void*)(base + kRvaLoad);
	void* ps = (void*)(base + kRvaParse);
	if (memcmp(ld, kLoadBytes, sizeof kLoadBytes)) { Log("xmlahead: code check failed - not installed (other game version?)"); return; }
	if (g_mode == 2) LoadList();
	g_recs.reserve(20000);
	const bool a = MH_CreateHook(ld, (void*)&HkLoad, (void**)&g_origLoad) == MH_OK && MH_EnableHook(ld) == MH_OK;
	const bool b = !memcmp(ps, kParseBytes, sizeof kParseBytes) &&
	               MH_CreateHook(ps, (void*)&HkParse, (void**)&g_origParse) == MH_OK && MH_EnableHook(ps) == MH_OK;
	Log("xmlahead: mode %d (%s), LoadXmlFromFile hook %s, ParseFile counter %s", g_mode,
	    g_mode == 2 ? "record + parse ahead" : "record", a ? "on" : "FAILED", b ? "on" : "FAILED");
	if (a) CloseHandle(CreateThread(nullptr, 0, Writer, nullptr, 0, nullptr));
}

void Xmlahead_Command(const char* args) {
	(void)args;
	size_t n; { std::lock_guard<std::mutex> lk(g_mu); n = g_recs.size(); }
	Log("xmlahead: mode %d, %zu loads recorded, list %zu, ready %lld, served %lld, failed %lld, game first %lld, "
	    "ParseFile calls %lld, game private %zu MB", g_mode, n, g_items.size(), s_ready.load(), s_served.load(),
	    s_failed.load(), s_skipped.load(), g_parseCalls.load(), PrivateMb());
}
