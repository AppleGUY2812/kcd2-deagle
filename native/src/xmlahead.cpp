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
#include <algorithm>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include "MinHook.h"
#include <psapi.h>

namespace {

constexpr uintptr_t kRvaLoad = 0x979360, kRvaParse = 0x247cecc;
// CCryPak::OpenPack (CryPak vtable 0x183a97568, FUN_1804d5320: "Cannot open Pak file %s"): counted, for the start-up start
constexpr uintptr_t kRvaOpenPack = 0x4d5320;
const unsigned char kOpenPackBytes[] = { 0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57 };
// Start-up (2026-10-24, boot 82): the game's init loads the list's first 3,562 files on Main (2.6 s) before the menu.
// The base, engine and mod data paks mount in one run (~1.1 s after the hitch window opens), the localization and
// facial paks ~7.5 s later, the level paks with the load. The workers start at the first XML load once >= kStartPaks
// paks are mounted and none for kStartQuietMs, and parse only list files before the first localization/ one (list
// order = the recorded load order, so the game read each of those before the localization paks mounted); the rest
// from the game's first localization/ load on (or the menu / level triggers). The engine paks and the first XML loads
// (engine config, the profile) come seconds before the data paks: the start waits until every mod data pak on disk
// (Mods/*/Data/*.pak, the Workshop's */Data/*.pak) has mounted, or with no mods, a Data pak. A disabled mod never
// mounts: then the start is the menu's, as before.
constexpr long kStartPaks = 10;
constexpr ULONGLONG kStartQuietMs = 300, kLocQuietMs = 50;
const unsigned char kLoadBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x60,0x0f,0x57,0xc0 };
const unsigned char kParseBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20 };
constexpr ULONGLONG kRecordMs = 10 * 60 * 1000, kKeepMs = 15 * 60 * 1000;
constexpr int kMenuWorkers = 4, kLevelWorkers = 3;
// The quest job at the menu asks for ~21,800 quests/ files in list order (4.1 s of parsing): 4 workers left it waiting
// 1.1 s of its 1.46 s XML phase (questsample, 2026-10-24). Extra workers help while the list is in quests/ and stop at
// the first other file.
constexpr int kQuestBoost = 6;
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
using OpenPackFn = void*(__fastcall*)(void* pak, const char* path, unsigned flags, void* a4);
OpenPackFn g_origOpenPack = nullptr;
std::atomic<long> g_pakOpens{0}, g_modPaks{0}, g_dataPaks{0};
long g_modPaksOnDisk = 0;
std::atomic<ULONGLONG> g_lastPak{0};
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

// XmlNodeRef release: vtable +0x20 (FUN_1804fb998, as the game's own release FUN_1804fb980 calls it). Until 2026-10-24
// this called +0x10, which is createNode (FUN_182486658): every call added a node to that tree, wrote its pointer to
// whatever rdx held, and AddRef'd it, so late and unused pre-parsed trees were never freed (re_quest_dump_binary.md).
void ReleaseNode(void* n) {
	if (!n) return;
	__try { (*(void(__fastcall**)(void*))(*(uintptr_t*)n + 0x20))(n); } __except (EXCEPTION_EXECUTE_HANDLER) {}
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
std::vector<Item*> g_quests;          // the quests/ files of queue 0, in list order: the boost workers' own queue
size_t g_firstLevel = SIZE_MAX;       // list index of the first levels/ file: the level load starts there
std::atomic<size_t> g_nextQuest{0};
std::atomic<bool> g_started[2], g_stop{false}, g_lowRam{false}, g_boosted{false};
std::atomic<size_t> g_cap{SIZE_MAX};   // start-up: list positions from here wait for the localization paks
size_t g_firstLoc = SIZE_MAX;          // list position of the first localization/ file
std::atomic<long long> s_capWaits{0};
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
	const int q = (int)(intptr_t)arg & 0xff;
	const bool boost = ((intptr_t)arg >> 8) & 1;   // a quest boost worker: only the quests/ files, then it stops
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);   // (normal tried in boot 25: no change)
	const size_t n = boost ? g_quests.size() : g_queue[q].size();
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
		const size_t i = boost ? g_nextQuest++ : g_next[q]++;
		if (i >= n) break;
		Item& it = boost ? *g_quests[i] : *g_queue[q][i];
		while (!g_stop && it.idx > g_gamePos.load() + kLead && it.state.load() == 0) { s_leadWaits++; Sleep(2); }
		while (!g_stop && it.idx >= g_cap.load() && it.state.load() == 0) { s_capWaits++; Sleep(5); }
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

void LiftCap(const char* why) {
	if (g_cap.exchange(SIZE_MAX) != SIZE_MAX)
		Log("xmlahead: %s - the whole list from now (%lld served so far, game first %lld, %lld cap waits)", why,
		    s_served.load(), s_skipped.load(), s_capWaits.load());
}

// queue 0 at the start-up: no quest boost (that is for the quest job at the menu), the cap on
void StartEarly() {
	bool f = false;
	if (g_queue[0].empty() || g_started[1] || !g_started[0].compare_exchange_strong(f, true)) return;
	g_privAtStart = PrivateMb();
	g_cap = g_firstLoc;
	for (int i = 0; i < kMenuWorkers; i++) CloseHandle(CreateThread(nullptr, 0, Worker, (LPVOID)(intptr_t)0, 0, nullptr));
	Log("xmlahead: start-up (%ld paks mounted, %ld mod data paks of %ld on disk) - %d workers parsing ahead the %zu files listed before the first "
	    "localization/ one, the rest from the localization paks on (%llu MB RAM free, game private %zu MB)",
	    g_pakOpens.load(), g_modPaks.load(), g_modPaksOnDisk, kMenuWorkers, g_firstLoc == SIZE_MAX ? g_items.size() : g_firstLoc,
	    (unsigned long long)FreeMb(), PrivateMb());
}

void StartQueue(int q, const char* why) {
	if (q == 0) LiftCap(why);
	if (q == 0 && g_origOpenPack && !g_started[0])
		Log("xmlahead: start-up start not reached (%ld pak mounts, %ld mod data paks of %ld on disk)", g_pakOpens.load(),
		    g_modPaks.load(), g_modPaksOnDisk);
	bool b = false;
	if (q == 0 && !g_quests.empty() && g_nextQuest.load() < g_quests.size() && g_started[0] && g_boosted.compare_exchange_strong(b, true)) {
		for (int i = 0; i < kQuestBoost; i++) CloseHandle(CreateThread(nullptr, 0, Worker, (LPVOID)(intptr_t)0x100, 0, nullptr));
		Log("xmlahead: %s - %d more workers for the quests/ part", why, kQuestBoost);
	}
	bool f = false;
	if (g_queue[q].empty() || !g_started[q].compare_exchange_strong(f, true)) return;
	if (!g_privAtStart) g_privAtStart = PrivateMb();
	const int n = q == 0 ? kMenuWorkers : kLevelWorkers;
	b = false;
	const int boost = q == 0 && !g_quests.empty() && g_boosted.compare_exchange_strong(b, true) ? kQuestBoost : 0;
	for (int i = 0; i < n + boost; i++)
		CloseHandle(CreateThread(nullptr, 0, Worker, (LPVOID)(intptr_t)(q | (i >= n ? 0x100 : 0)), 0, nullptr));
	Log("xmlahead: %s - %d workers parsing %zu %s files ahead%s (%llu MB RAM free, game private %zu MB)", why, n,
	    g_queue[q].size(), q == 0 ? "non-level" : "level", boost ? (" + " + std::to_string(boost) + " more for the quests/ part").c_str() : "",
	    (unsigned long long)FreeMb(), PrivateMb());
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
		if (g_origOpenPack) {
			const ULONGLONG quiet = GetTickCount64() - g_lastPak.load();
			if (!g_started[0] && g_pakOpens >= kStartPaks && quiet >= kStartQuietMs && g_dataPaks > 0 &&
			    g_modPaks >= g_modPaksOnDisk) StartEarly();
			if (g_cap.load() != SIZE_MAX && path.compare(0, 13, "localization/") == 0 && quiet >= kLocQuietMs)
				LiftCap("the game's first localization/ load");
		}
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

void* __fastcall HkOpenPack(void* pak, const char* path, unsigned flags, void* a4) {
	void* r = g_origOpenPack(pak, path, flags, a4);
	std::string p = path ? path : "";
	for (char& ch : p) ch = ch == '/' ? '\\' : (char)tolower((unsigned char)ch);
	if (p.find("\\data\\") != std::string::npos || p.compare(0, 5, "data\\") == 0) {
		g_dataPaks++;
		if (p.find("mods\\") != std::string::npos || p.find("workshop\\content\\") != std::string::npos) g_modPaks++;
	}
	g_pakOpens++;
	g_lastPak = GetTickCount64();
	return r;
}

void** __fastcall HkParse(void* parser, void** ret, const char* file, char cleanPools, char reportErrors) {
	g_parseCalls++;
	return g_origParse(parser, ret, file, cleanPools, reportErrors);
}

void Flush() {
	std::vector<Rec> recs;
	{ std::lock_guard<std::mutex> lk(g_mu); recs = g_recs; }
	const std::string tmp = g_dir + "fastload_xmlahead_trace.tmp", out = g_dir + "fastload_xmlahead_trace.csv";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f) return;
	fprintf(f, "t_ms,tid,ok,served,us,file\n");
	for (const Rec& r : recs) fprintf(f, "%u,%u,%u,%u,%u,%s\n", r.tMs, r.tid, r.ok, r.served, r.us, r.file.c_str());
	fclose(f);
	MoveFileExA(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// The list for the next start, from this run's recording (the same filter as tools/xmlahead_list.py: loads that
// returned a tree, first load of each path, in start-time order, %user% files left out). Written once, 20 s after the
// first load reaches gameplay, when the recording holds a level load. A list recorded before a change to the load
// went stale and cost the apply 0.5 s (boot 72: questreplay had removed 19,400 menu files from the load).
std::atomic<ULONGLONG> g_loadedAt{0};
bool g_listWritten = false;
void WriteList() {
	std::vector<Rec> recs;
	{ std::lock_guard<std::mutex> lk(g_mu); recs = g_recs; }
	std::stable_sort(recs.begin(), recs.end(), [](const Rec& a, const Rec& b) { return a.tMs < b.tMs; });
	std::unordered_set<std::string> seen;
	std::vector<const std::string*> keep;
	size_t level = 0;
	for (const Rec& r : recs) {
		if (!r.ok || r.file.empty() || r.file.compare(0, 6, "%user%") == 0 || !seen.insert(r.file).second) continue;
		keep.push_back(&r.file);
		if (IsLevel(r.file)) level++;
	}
	if (keep.size() < 5000 || !level) {
		Log("xmlahead: list not rewritten (%zu files, %zu level files: no full load recorded)", keep.size(), level);
		return;
	}
	const std::string tmp = g_dir + "fastload_xmlahead_list.tmp", out = g_dir + "fastload_xmlahead_list.txt";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f) return;
	fprintf(f, "# written by the plugin from this run's recording (xmlahead.cpp WriteList)\n");
	for (const std::string* p : keep) fprintf(f, "%s\n", p->c_str());
	const bool ok = fclose(f) == 0 && MoveFileExA(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING);
	Log("xmlahead: list for the next start %s: %zu files (%zu level), was %zu", ok ? "written" : "NOT written", keep.size(),
	    level, g_items.size());
}

DWORD WINAPI Writer(LPVOID) {
	size_t written = 0;
	long long lastServed = -1, lastReady = -1;
	for (unsigned tick = 1;; tick++) {
		Sleep(500);
		if (!g_items.empty() && (!g_started[0] || g_cap.load() != SIZE_MAX) && g_loads >= kMenuLoads &&
		    GetTickCount64() - g_lastLoad >= kMenuQuietMs)
			StartQueue(0, "main menu idle");
		if (g_mode == 2 && !g_listWritten && g_loadedAt && GetTickCount64() - g_loadedAt > 20000) {
			g_listWritten = true;
			WriteList();
		}
		if (tick % 10) continue;
		size_t n; { std::lock_guard<std::mutex> lk(g_mu); n = g_recs.size(); }
		if (n != written) { Flush(); written = n; }
		if ((g_started[0] || g_started[1]) && (s_served != lastServed || s_ready != lastReady)) {
			lastServed = s_served; lastReady = s_ready;
			Log("xmlahead: ready %lld of %zu (failed %lld, parse %lld ms), served %lld (%lld after waiting %lld ms, %lld "
			    "waits timed out), game first %lld, dropped %lld; game at %zu, workers at %zu/%zu, lead waits %lld, cap waits %lld, RAM "
			    "waits %lld; game private %zu MB (+%lld since start)",
			    s_ready.load(), g_items.size(), s_failed.load(), s_parseMs.load(), s_served.load(), s_waited.load(),
			    s_waitUs.load() / 1000, s_waitTimeout.load(), s_skipped.load(), s_late.load(), g_gamePos.load(),
			    g_next[0].load(), g_next[1].load(), s_leadWaits.load(), s_capWaits.load(), s_ramWaits.load(), PrivateMb(),
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
	FILE* f = fopen((g_dir + "fastload_xmlahead_list.txt").c_str(), "r");
	if (!f) { Log("xmlahead: no fastload_xmlahead_list.txt - nothing to parse ahead"); return false; }
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
		if (g_firstLevel == SIZE_MAX && IsLevel(p)) g_firstLevel = it.idx;
		if (p.compare(0, 7, "quests/") == 0) g_quests.push_back(&it);
		if (g_firstLoc == SIZE_MAX && p.compare(0, 13, "localization/") == 0) g_firstLoc = it.idx;
	}
	fclose(f);
	Log("xmlahead: list %zu files (%zu outside levels/, %zu level, %zu quests/)", g_items.size(), g_queue[0].size(), g_queue[1].size(),
	    g_quests.size());
	return !g_items.empty();
}

long CountPaks(const std::string& root) {   // root\*\Data\*.pak, without the -partN files
	long n = 0;
	WIN32_FIND_DATAA d;
	HANDLE h = FindFirstFileA((root + "*").c_str(), &d);
	if (h == INVALID_HANDLE_VALUE) return 0;
	do {
		if (!(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || d.cFileName[0] == '.') continue;
		WIN32_FIND_DATAA e;
		HANDLE g = FindFirstFileA((root + d.cFileName + "\\Data\\*.pak").c_str(), &e);
		if (g == INVALID_HANDLE_VALUE) continue;
		do if (!strstr(e.cFileName, "-part")) n++; while (FindNextFileA(g, &e));   // X-part1.pak goes with X.pak
		FindClose(g);
	} while (FindNextFileA(h, &d));
	FindClose(h);
	return n;
}

}  // namespace

bool Xmlahead_LevelSeen() { return g_levelSeen.load(); }

void Xmlahead_OnLoaded() { ULONGLONG z = 0; g_loadedAt.compare_exchange_strong(z, GetTickCount64()); }

void Xmlahead_StartMenuNow(const char* why) { if (!g_items.empty() && !g_levelSeen) StartQueue(0, why); }

void Xmlahead_SkipQuests(const char* why) {
	// Only the quest build's own files: those listed before the level load starts. The save load's place graphs read
	// quests/ files too (boot 72: 2,398 on Main in the apply step, ~0.5 s), listed after the first levels/ file: kept,
	// the queue-0 workers parse them ahead as any other file.
	size_t n = 0, kept = 0;
	g_nextQuest = g_quests.size();   // the boost workers have nothing left
	for (Item* it : g_quests) {
		if (it->idx > g_firstLevel) { kept++; continue; }
		int w = 0;
		if (it->state.compare_exchange_strong(w, 3)) { n++; continue; }
		int r = 2;
		if (it->state.compare_exchange_strong(r, 3)) { ReleaseNode(it->node); it->node = nullptr; n++; }
	}
	Log("xmlahead: %zu quests/ files not parsed ahead (%s); %zu listed after the level load starts kept (the save "
	    "load's place graphs)", n, why, kept);
}

bool Xmlahead_MenuQuiet(unsigned quietMs) {
	return g_loads >= kMenuLoads && !g_levelSeen && GetTickCount64() - g_lastLoad >= quietMs;
}

void Xmlahead_Install(uintptr_t base, void* self) {
	g_start = GetTickCount64();
	LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); g_freq = fq.QuadPart; g_t0 = Now();
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	g_dir = path; g_dir = g_dir.substr(0, g_dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((g_dir + "fastload_xmlahead.txt").c_str(), "r")) {
		int m = 1; if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 2) g_mode = m; fclose(fp);
	}
	if (g_mode == 0) { Log("xmlahead: off"); return; }
	const std::string root = g_dir + "..\\..\\";   // Bin\Win64MasterMasterSteamPGO\ -> the game folder
	g_modPaksOnDisk = CountPaks(root + "Mods\\") + CountPaks(root + "..\\..\\workshop\\content\\1771300\\");
	void* ld = (void*)(base + kRvaLoad);
	void* ps = (void*)(base + kRvaParse);
	if (memcmp(ld, kLoadBytes, sizeof kLoadBytes)) { Log("xmlahead: code check failed - not installed (other game version?)"); return; }
	if (g_mode == 2) LoadList();
	g_recs.reserve(20000);
	const bool a = MH_CreateHook(ld, (void*)&HkLoad, (void**)&g_origLoad) == MH_OK && MH_EnableHook(ld) == MH_OK;
	const bool b = !memcmp(ps, kParseBytes, sizeof kParseBytes) &&
	               MH_CreateHook(ps, (void*)&HkParse, (void**)&g_origParse) == MH_OK && MH_EnableHook(ps) == MH_OK;
	void* op = (void*)(base + kRvaOpenPack);
	const bool c = g_mode == 2 && a && !memcmp(op, kOpenPackBytes, sizeof kOpenPackBytes) &&
	               MH_CreateHook(op, (void*)&HkOpenPack, (void**)&g_origOpenPack) == MH_OK && MH_EnableHook(op) == MH_OK;
	if (!c) g_origOpenPack = nullptr;
	Log("xmlahead: mode %d (%s), LoadXmlFromFile hook %s, ParseFile counter %s, start-up start %s", g_mode,
	    g_mode == 2 ? "record + parse ahead" : "record", a ? "on" : "FAILED", b ? "on" : "FAILED",
	    c ? ("on (after " + std::to_string(g_modPaksOnDisk) + " mod data paks)").c_str() : g_mode == 2 ? "FAILED (from the menu)" : "off");
	if (a) CloseHandle(CreateThread(nullptr, 0, Writer, nullptr, 0, nullptr));
}

void Xmlahead_Command(const char* args) {
	(void)args;
	size_t n; { std::lock_guard<std::mutex> lk(g_mu); n = g_recs.size(); }
	Log("xmlahead: mode %d, %zu loads recorded, list %zu, ready %lld, served %lld, failed %lld, game first %lld, "
	    "ParseFile calls %lld, game private %zu MB", g_mode, n, g_items.size(), s_ready.load(), s_served.load(),
	    s_failed.load(), s_skipped.load(), g_parseCalls.load(), PrivateMb());
}
