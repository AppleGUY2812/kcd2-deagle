// Long frames, sampled from inside the game. See hitch.h.
#include "hitch.h"
#include "log.h"
#include <psapi.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
std::atomic<long long> g_lastFrame{0};   // QPC at the main thread's last drawn frame
std::atomic<bool> g_on{true};
std::atomic<int> g_thresholdMs{250};
HANDLE g_main = nullptr;
uintptr_t g_stackBase = 0;
uintptr_t g_game = 0, g_gameEnd = 0;
long long g_freq = 1;
std::string g_csvPath;
size_t g_csvBytes = 0;
constexpr size_t kStackBytes = 32 * 1024, kMaxCsvBytes = 32u << 20;
constexpr int kIntervalMs = 20, kMaxSamples = 1500, kKeep = 16;
alignas(16) uint8_t g_stack[kStackBytes];   // the watcher's only buffer while the main thread is suspended

struct Module { uintptr_t base, end; std::string name; };
std::vector<Module> g_mods;   // sorted by base; refreshed at the start of each long frame

struct Sample { uint32_t tMs; uint32_t faults; uint64_t rip; int n; uint64_t ret[kKeep]; };

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

// Guarded helpers, no C++ objects in them (MSVC forbids __try next to unwinding). clang-cl catches a fault in a __try
// only when it happens in a called function, so the copy goes through a pointer the compiler can't inline.
void* (*volatile g_copy)(void*, const void*, size_t) = memcpy;
__declspec(noinline) size_t CopyStack(uint8_t* dst, uintptr_t src, size_t n) {
	__try { g_copy(dst, (const void*)src, n); return n; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
__declspec(noinline) bool CodeBefore(uintptr_t a, uint8_t out[8]) {
	__try { g_copy(out, (const void*)(a - 8), 8); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The bytes before a return address are a call: E8 rel32, FF /2 with a register, [reg], [reg+disp8], [rip/reg+disp32]
// (with or without a SIB byte).
bool AfterCall(uintptr_t a) {
	const Module* m = ModuleOf(a);
	if (!m || a < m->base + 0x1000) return false;   // code never sits in a module's first page (its headers)
	uint8_t c[8];
	if (!CodeBefore(a, c)) return false;   // c[7] = a-1 ... c[0] = a-8
	auto ff2 = [&](int i) { return c[i] == 0xFF && (c[i + 1] & 0x38) == 0x10; };
	return c[3] == 0xE8 || ff2(6) || ff2(5) || ff2(4) || ff2(2) || ff2(1);
}

// Suspend, copy, resume: nothing in between that could wait on something the main thread holds.
bool Grab(Sample* s, uint32_t tMs) {
	if (SuspendThread(g_main) == (DWORD)-1) return false;
	CONTEXT c{};
	c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
	const bool ok = GetThreadContext(g_main, &c) != 0;
	size_t n = 0;
	if (ok && c.Rsp < g_stackBase && g_stackBase - c.Rsp < (8u << 20)) {
		const size_t want = (size_t)(g_stackBase - c.Rsp) < kStackBytes ? (size_t)(g_stackBase - c.Rsp) : kStackBytes;
		n = CopyStack(g_stack, c.Rsp, want);
	}
	ResumeThread(g_main);
	if (!ok) return false;
	s->tMs = tMs;
	s->rip = c.Rip;
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
	std::map<std::string, int> in, from;
	for (const Sample& s : ss) {
		in[Where(s.rip)]++;
		if (s.n > 0) from[Where(s.ret[0])]++;
	}
	const uint32_t df = ss.back().faults - faults0;
	Log("hitch: #%d main frame %.0f ms, %zu samples, page faults +%u (%.0f a second); in: %s; called from: %s", no, frameMs,
	    ss.size(), df, frameMs > 0 ? df * 1000.0 / frameMs : 0.0, Top(in, (int)ss.size(), 6).c_str(),
	    Top(from, (int)ss.size(), 6).c_str());
	if (g_csvPath.empty() || g_csvBytes > kMaxCsvBytes) return;
	FILE* f = fopen(g_csvPath.c_str(), "a");
	if (!f) return;
	for (const Sample& s : ss) {
		std::string st;
		for (int i = 0; i < s.n; i++) st += (i ? " " : "") + Where(s.ret[i]);
		const int w = fprintf(f, "%d,%.0f,%u,%u,%s,%s\n", no, frameMs, s.tMs, s.faults - faults0, Where(s.rip).c_str(), st.c_str());
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
		}
		if ((int)ss.size() >= kMaxSamples) continue;
		Sample s;
		if (Grab(&s, (uint32_t)((now - start) * 1000 / g_freq))) ss.push_back(s);
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
		fprintf(fo, "hitch,frame_ms,t_ms,faults,rip,stack\n");
		fclose(fo);
		g_csvPath = p;
	}
	CloseHandle(CreateThread(nullptr, 0, Watch, nullptr, 0, nullptr));
	Log("hitch: on (main frames over %d ms sampled every %d ms from inside the game; samples in %s)", g_thresholdMs.load(),
	    kIntervalMs, g_csvPath.empty() ? "- (csv not writable)" : g_csvPath.c_str());
}

void Hitch_Frame() {
	if (!g_main) {
		g_main = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE,
		                    GetCurrentThreadId());
		g_stackBase = (uintptr_t)__readgsqword(0x08);   // TEB StackBase of this (the main) thread
	}
	g_lastFrame.store(Now(), std::memory_order_release);
}

void Hitch_Command(const char* args) {
	while (args && *args == ' ') args++;
	if (args && !strcmp(args, "off")) g_on = false;
	else if (args && !strcmp(args, "on")) g_on = true;
	else if (args && *args >= '0' && *args <= '9') {
		const int ms = atoi(args);
		if (ms >= 50 && ms <= 5000) g_thresholdMs = ms;
	}
	Log("hitch: %s, main frames over %d ms  (hitch on|off|<ms>)", g_on ? "on" : "off", g_thresholdMs.load());
}
