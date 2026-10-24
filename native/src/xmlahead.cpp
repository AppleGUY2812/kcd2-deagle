// Parsing XML ahead: see xmlahead.h.
#include "xmlahead.h"
#include "log.h"
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

namespace {

constexpr uintptr_t kRvaLoad = 0x979360, kRvaParse = 0x247cecc;
const unsigned char kLoadBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x60,0x0f,0x57,0xc0 };
const unsigned char kParseBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20 };
constexpr ULONGLONG kRecordMs = 10 * 60 * 1000, kKeepMs = 15 * 60 * 1000;
constexpr int kWorkers = 4;

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
	std::atomic<int> state{0};        // 0 waiting, 1 parsing, 2 ready, 3 taken / dropped / failed
	void* node = nullptr;
};
std::deque<Item> g_items;
std::unordered_map<std::string, Item*> g_byPath;
std::atomic<size_t> g_next{0};
std::atomic<bool> g_started{false}, g_stop{false};
void* g_self = nullptr;
std::atomic<long long> s_ready{0}, s_failed{0}, s_served{0}, s_late{0}, s_parseMs{0};

DWORD WINAPI Worker(LPVOID) {
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
	const size_t n = g_items.size();
	for (;;) {
		if (g_stop) break;
		const size_t i = g_next++;
		if (i >= n) break;
		Item& it = g_items[i];
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

void StartAhead() {
	bool f = false;
	if (!g_started.compare_exchange_strong(f, true)) return;
	for (int i = 0; i < kWorkers; i++) CloseHandle(CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr));
	Log("xmlahead: level loading - %d workers parsing %zu files ahead", kWorkers, g_items.size());
}

void** __fastcall HkLoad(void* self, void** ret, const char* file, uint64_t a4, char reportErrors) {
	const ULONGLONG now = GetTickCount64() - g_start;
	const bool record = now <= kRecordMs;
	std::string path;
	if (g_mode == 2 || record) path = Norm(file);
	if (g_mode == 2 && !g_items.empty() && !g_stop) {
		if (!g_self) g_self = self;
		if (!g_started && path.compare(0, 7, "levels/") == 0) StartAhead();
		auto f = g_byPath.find(path);
		if (f != g_byPath.end()) {
			Item& it = *f->second;
			int r = 2;
			if (it.state.compare_exchange_strong(r, 3)) {
				*ret = it.node;          // the worker's reference goes to the caller
				it.node = nullptr;
				s_served++;
				if (record) {
					Rec rc{ (uint32_t)((Now() - g_t0) * 1000 / g_freq), GetCurrentThreadId(), 0, 1, 1, path };
					std::lock_guard<std::mutex> lk(g_mu); g_recs.push_back(std::move(rc));
				}
				return ret;
			}
			int w = 0;
			it.state.compare_exchange_strong(w, 3);   // not started: skip it; parsing: the worker drops its tree
			if (r == 1) it.state.compare_exchange_strong(r, 3);
		}
	}
	if (!record) return g_origLoad(self, ret, file, a4, reportErrors);
	const long long a = Now();
	void** out = g_origLoad(self, ret, file, a4, reportErrors);
	const long long b = Now();
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
	for (;;) {
		Sleep(5000);
		size_t n; { std::lock_guard<std::mutex> lk(g_mu); n = g_recs.size(); }
		if (n != written) { Flush(); written = n; }
		if (g_started && (s_served != lastServed || s_ready != lastReady)) {
			lastServed = s_served; lastReady = s_ready;
			Log("xmlahead: ready %lld of %zu (failed %lld, parse %lld ms), served %lld, game came first + dropped %lld",
			    s_ready.load(), g_items.size(), s_failed.load(), s_parseMs.load(), s_served.load(), s_late.load());
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
		g_byPath.emplace(p, &it);
	}
	fclose(f);
	Log("xmlahead: list %zu files", g_items.size());
	return !g_items.empty();
}

}  // namespace

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
	Log("xmlahead: mode %d, %zu loads recorded, list %zu, ready %lld, served %lld, failed %lld, ParseFile calls %lld",
	    g_mode, n, g_items.size(), s_ready.load(), s_served.load(), s_failed.load(), g_parseCalls.load());
}
