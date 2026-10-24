// Long frames, sampled from inside the game. See hitch.h.
#include "hitch.h"
#include "log.h"
#include <psapi.h>
#include <tlhelp32.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {
std::atomic<long long> g_lastFrame{0};   // QPC at the main thread's last drawn frame
std::atomic<bool> g_on{true};
std::atomic<int> g_thresholdMs{250};
HANDLE g_main = nullptr;
uintptr_t g_mainStack = 0;
uintptr_t g_game = 0, g_gameEnd = 0;
long long g_freq = 1;
std::string g_csvPath;
size_t g_csvBytes = 0;
constexpr size_t kStackBytes = 32 * 1024, kMaxCsvBytes = 48u << 20;
constexpr int kIntervalMs = 20, kMaxSamples = 6000, kKeep = 16;
alignas(16) uint8_t g_stack[kStackBytes];   // the watcher's only buffer while a thread is suspended

// The threads sampled with the main thread, by name (the engine names its threads); `hitch threads A,B`.
std::mutex g_namesMu;
std::vector<std::string> g_names = { "RenderThread", "RenderLoadingThread" };
bool g_namesChanged = true;

struct Target { HANDLE h; uintptr_t stackBase; std::string name; DWORD tid; };
std::vector<Target> g_targets;   // [0] = main; watcher thread only

struct Module { uintptr_t base, end; std::string name; };
std::vector<Module> g_mods;   // sorted by base; refreshed at the start of each long frame

struct Sample { uint32_t tMs; uint32_t faults; uint64_t rip; uint8_t who; int n; uint64_t ret[kKeep]; };

long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

uint32_t Faults() {
	PROCESS_MEMORY_COUNTERS pmc{ sizeof pmc };
	return K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc) ? pmc.PageFaultCount : 0;
}

void RefreshModules() {
	HMODULE hm[1024];
	DWORD need = 0;
	if (!K32EnumProcessModules(GetCurrentProcess(), hm, sizeof hm, &need)) return;
	std::vector<Module> m;
	for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 1024; i++) {
		MODULEINFO mi{};
		char path[MAX_PATH] = {};
		if (!K32GetModuleInformation(GetCurrentProcess(), hm[i], &mi, sizeof mi)) continue;
		GetModuleFileNameA(hm[i], path, MAX_PATH);
		const char* b = strrchr(path, '\\');
		m.push_back({ (uintptr_t)mi.lpBaseOfDll, (uintptr_t)mi.lpBaseOfDll + mi.SizeOfImage, b ? b + 1 : path });
	}
	std::sort(m.begin(), m.end(), [](const Module& a, const Module& b) { return a.base < b.base; });
	g_mods.swap(m);
}

const Module* ModuleOf(uintptr_t a) {
	auto it = std::upper_bound(g_mods.begin(), g_mods.end(), a, [](uintptr_t x, const Module& m) { return x < m.base; });
	if (it == g_mods.begin()) return nullptr;
	--it;
	return a < it->end ? &*it : nullptr;
}

// A thread's stack base from its TEB (+0x08), through NtQueryInformationThread(ThreadBasicInformation).
uintptr_t StackBaseOf(HANDLE h) {
	struct Tbi { LONG status; PVOID teb; PVOID pid, tid; ULONG_PTR affinity; LONG prio, basePrio; } tbi{};
	using Fn = LONG(NTAPI*)(HANDLE, int, PVOID, ULONG, PULONG);
	static Fn q = (Fn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
	if (!q || q(h, 0, &tbi, sizeof tbi, nullptr) < 0 || !tbi.teb) return 0;
	return *(uintptr_t*)((uintptr_t)tbi.teb + 0x08);
}

// Finds the named threads (once each; again after `hitch threads`), at the start of a long frame.
void FindTargets() {
	std::vector<std::string> names;
	{
		std::lock_guard<std::mutex> l(g_namesMu);
		if (!g_namesChanged && g_targets.size() == 1 + g_names.size()) return;
		names = g_names;
		g_namesChanged = false;
	}
	for (size_t i = 1; i < g_targets.size(); i++) CloseHandle(g_targets[i].h);
	g_targets.resize(1);
	using DescFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
	static DescFn desc = (DescFn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadDescription");
	if (!desc) return;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE) return;
	THREADENTRY32 te{ sizeof te };
	const DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
	for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
		if (te.th32OwnerProcessID != pid || te.th32ThreadID == self || te.th32ThreadID == g_targets[0].tid) continue;
		HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
		if (!h) continue;
		PWSTR w = nullptr;
		std::string nm;
		if (SUCCEEDED(desc(h, &w)) && w) {
			char a[128] = {};
			WideCharToMultiByte(CP_UTF8, 0, w, -1, a, sizeof a - 1, nullptr, nullptr);
			nm = a;
			LocalFree(w);
		}
		const bool want = std::find(names.begin(), names.end(), nm) != names.end() &&
		                  std::none_of(g_targets.begin(), g_targets.end(), [&](const Target& t) { return t.name == nm; });
		const uintptr_t sb = want ? StackBaseOf(h) : 0;
		if (want && sb && g_targets.size() < 8) g_targets.push_back({ h, sb, nm, te.th32ThreadID });
		else CloseHandle(h);
	}
	CloseHandle(snap);
}

// Guarded helpers, no C++ objects in them (MSVC forbids __try next to unwinding). The copy goes through a pointer the
// compiler can't inline, so a fault in it is caught however the file is built.
void* (*volatile g_copy)(void*, const void*, size_t) = memcpy;
__declspec(noinline) size_t CopyStack(uint8_t* dst, uintptr_t src, size_t n) {
	__try { g_copy(dst, (const void*)src, n); return n; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
__declspec(noinline) bool CodeBefore(uintptr_t a, uint8_t out[8]) {
	__try { g_copy(out, (const void*)(a - 8), 8); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The bytes before a return address are a call: E8 rel32 (c[3]), or FF /2 as a 2-byte call reg / [reg] (c[6]),
// 3-byte [reg+disp8] (c[5]), 4-byte [sib+disp8] (c[4]), 6-byte [rip/reg+disp32] (c[2]), 7-byte [sib+disp32] (c[1]).
bool AfterCall(uintptr_t a) {
	const Module* m = ModuleOf(a);
	if (!m || a < m->base + 0x1000) return false;   // code never sits in a module's first page (its headers)
	uint8_t c[8];
	if (!CodeBefore(a, c)) return false;   // c[0] = a-8 ... c[7] = a-1
	auto ff2 = [&](int i) { return c[i] == 0xFF && (c[i + 1] & 0x38) == 0x10; };
	return c[3] == 0xE8 || ff2(6) || ff2(5) || ff2(4) || ff2(2) || ff2(1);
}

// Suspend, copy, resume: nothing in between that could wait on something the suspended thread holds.
bool Grab(const Target& t, uint8_t who, Sample* s, uint32_t tMs) {
	if (SuspendThread(t.h) == (DWORD)-1) return false;
	CONTEXT c{};
	c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
	const bool ok = GetThreadContext(t.h, &c) != 0;
	size_t n = 0;
	if (ok && c.Rsp < t.stackBase && t.stackBase - c.Rsp < (8u << 20)) {
		const size_t want = (size_t)(t.stackBase - c.Rsp) < kStackBytes ? (size_t)(t.stackBase - c.Rsp) : kStackBytes;
		n = CopyStack(g_stack, c.Rsp, want);
	}
	ResumeThread(t.h);
	if (!ok) return false;
	s->tMs = tMs;
	s->rip = c.Rip;
	s->who = who;
	s->faults = Faults();
	s->n = 0;
	for (size_t i = 0; i + 8 <= n && s->n < kKeep; i += 8) {
		uint64_t v;
		memcpy(&v, g_stack + i, 8);
		if (AfterCall((uintptr_t)v)) s->ret[s->n++] = v;
	}
	return true;
}

std::string Where(uint64_t a) {
	char s[96];
	if (a >= g_game && a < g_gameEnd) { snprintf(s, sizeof s, "WHGame.dll+%llx", (unsigned long long)(a - g_game)); return s; }
	const Module* m = ModuleOf((uintptr_t)a);
	if (m) snprintf(s, sizeof s, "%s+%llx", m->name.c_str(), (unsigned long long)(a - m->base));
	else snprintf(s, sizeof s, "%llx", (unsigned long long)a);
	return s;
}

std::string Top(const std::map<std::string, int>& c, int total, int k) {
	std::vector<std::pair<int, std::string>> v;
	for (auto& e : c) v.push_back({ e.second, e.first });
	std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
	std::string out;
	char one[160];
	for (int i = 0; i < (int)v.size() && i < k; i++) {
		snprintf(one, sizeof one, "%s%s %d %%", out.empty() ? "" : ", ", v[i].second.c_str(), v[i].first * 100 / (total ? total : 1));
		out += one;
	}
	return out;
}

void Report(int no, double frameMs, const std::vector<Sample>& ss, uint32_t faults0) {
	if (ss.empty()) return;
	const uint32_t df = ss.back().faults - faults0;
	for (size_t who = 0; who < g_targets.size(); who++) {
		std::map<std::string, int> in, from;
		int n = 0;
		for (const Sample& s : ss) {
			if (s.who != who) continue;
			n++;
			in[Where(s.rip)]++;
			if (s.n > 0) from[Where(s.ret[0])]++;
		}
		if (who == 0)
			Log("hitch: #%d main frame %.0f ms, %d samples, page faults +%u (%.0f a second); in: %s; called from: %s", no,
			    frameMs, n, df, frameMs > 0 ? df * 1000.0 / frameMs : 0.0, Top(in, n, 6).c_str(), Top(from, n, 6).c_str());
		else if (n)
			Log("hitch: #%d %s: %d samples; in: %s; called from: %s", no, g_targets[who].name.c_str(), n, Top(in, n, 6).c_str(),
			    Top(from, n, 6).c_str());
	}
	if (g_csvPath.empty() || g_csvBytes > kMaxCsvBytes) return;
	FILE* f = fopen(g_csvPath.c_str(), "a");
	if (!f) return;
	for (const Sample& s : ss) {
		std::string st;
		for (int i = 0; i < s.n; i++) st += (i ? " " : "") + Where(s.ret[i]);
		const char* who = s.who < g_targets.size() ? (s.who ? g_targets[s.who].name.c_str() : "Main") : "?";
		const int w = fprintf(f, "%d,%s,%.0f,%u,%u,%s,%s\n", no, who, frameMs, s.tMs, s.faults - faults0, Where(s.rip).c_str(),
		                      st.c_str());
		if (w > 0) g_csvBytes += (size_t)w;
	}
	fclose(f);
}

DWORD WINAPI Watch(LPVOID) {
	std::vector<Sample> ss;
	ss.reserve(kMaxSamples);
	int no = 0;
	long long start = 0;
	uint32_t faults0 = 0;
	for (;;) {
		Sleep(kIntervalMs);
		const long long last = g_lastFrame.load(std::memory_order_acquire);
		if (!g_main || !last) continue;
		if (g_targets.empty()) g_targets.push_back({ g_main, g_mainStack, "Main", GetThreadId(g_main) });
		const long long now = Now();
		const double gapMs = (now - last) * 1000.0 / (double)g_freq;
		if (!ss.empty() && (last != start || !g_on)) {   // the long frame ended (or sampling was turned off)
			const double frameMs = (last != start ? last - start : now - start) * 1000.0 / (double)g_freq;
			Report(no, frameMs, ss, faults0);
			ss.clear();
		}
		if (!g_on || gapMs < g_thresholdMs.load()) continue;
		if (ss.empty()) {
			if (last == start) continue;   // this long frame already reported (sampling was full)
			start = last;
			no++;
			faults0 = Faults();
			RefreshModules();
			FindTargets();
		}
		const uint32_t tMs = (uint32_t)((now - start) * 1000 / g_freq);
		for (size_t who = 0; who < g_targets.size() && ss.size() < (size_t)kMaxSamples; who++) {
			Sample s;
			if (Grab(g_targets[who], (uint8_t)who, &s, tMs)) ss.push_back(s);
		}
	}
}
}  // namespace

void Hitch_Install(uintptr_t gameBase, size_t gameSize, HMODULE self) {
	g_game = gameBase;
	g_gameEnd = gameBase + gameSize;
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	g_freq = f.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA(self, path, MAX_PATH);
	std::string p(path);
	p = p.substr(0, p.find_last_of('\\') + 1) + "deagle_hitch.csv";
	FILE* fo = fopen(p.c_str(), "w");
	if (fo) {
		fprintf(fo, "hitch,thread,frame_ms,t_ms,faults,rip,stack\n");
		fclose(fo);
		g_csvPath = p;
	}
	CloseHandle(CreateThread(nullptr, 0, Watch, nullptr, 0, nullptr));
	Log("hitch: on (main frames over %d ms sampled every %d ms from inside the game, with RenderThread and "
	    "RenderLoadingThread; samples in %s)", g_thresholdMs.load(), kIntervalMs, g_csvPath.empty() ? "- (csv not writable)" : g_csvPath.c_str());
}

void Hitch_Frame() {
	if (!g_main) {
		g_main = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
		g_mainStack = (uintptr_t)__readgsqword(0x08);   // TEB StackBase of this (the main) thread
	}
	g_lastFrame.store(Now(), std::memory_order_release);
}

void Hitch_Command(const char* args) {
	while (args && *args == ' ') args++;
	if (args && !strcmp(args, "off")) g_on = false;
	else if (args && !strcmp(args, "on")) g_on = true;
	else if (args && !strncmp(args, "threads", 7)) {
		std::vector<std::string> v;
		std::string cur;
		for (const char* p = args + 7; ; p++) {
			if (*p == ',' || !*p) { while (!cur.empty() && cur.back() == ' ') cur.pop_back(); if (!cur.empty()) v.push_back(cur); cur.clear(); if (!*p) break; }
			else if (!(cur.empty() && *p == ' ')) cur += *p;
		}
		std::lock_guard<std::mutex> l(g_namesMu);
		g_names = v;
		g_namesChanged = true;
	}
	else if (args && *args >= '0' && *args <= '9') {
		const int ms = atoi(args);
		if (ms >= 50 && ms <= 5000) g_thresholdMs = ms;
	}
	std::string names;
	{
		std::lock_guard<std::mutex> l(g_namesMu);
		for (auto& n : g_names) names += (names.empty() ? "" : ",") + n;
	}
	Log("hitch: %s, main frames over %d ms, with threads %s  (hitch on|off|<ms>|threads A,B)", g_on ? "on" : "off",
	    g_thresholdMs.load(), names.empty() ? "-" : names.c_str());
}
