// Sampling profile of a quest build thread: see questsample.h.
#include "questsample.h"
#include "loadwatch.h"
#include "log.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr size_t kStackCopy = 128 << 10;
constexpr int kMaxFrames = 128;
struct Sample { int phase; std::vector<uint64_t> f; };   // function starts, innermost first

HANDLE g_thread = nullptr, g_sampler = nullptr;
uintptr_t g_stackBase = 0;
std::atomic<bool> g_run{false};
std::vector<Sample> g_s;
long long g_t0 = 0, g_freq = 1;
std::string g_dir;
std::unordered_map<uint64_t, std::string> g_mod;   // image base -> short module name

uint64_t FuncStart(uint64_t pc, uint64_t* base) {   // the primary function's start (follows chained unwind info)
	DWORD64 ib = 0;
	PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(pc, &ib, nullptr);
	*base = ib;
	if (!fe) return pc | (1ull << 63);   // leaf without unwind data: keep the address itself
	for (int i = 0; i < 32; i++) {
		if (fe->UnwindData & 1) { fe = (PRUNTIME_FUNCTION)(ib + (fe->UnwindData & ~1u)); continue; }
		const uint8_t* ui = (const uint8_t*)(ib + fe->UnwindData);
		if (!((ui[0] >> 3) & UNW_FLAG_CHAININFO)) break;
		const uint8_t n = ui[2];
		fe = (PRUNTIME_FUNCTION)(ui + 4 + 2 * ((n + 1) & ~1));
	}
	return ib + fe->BeginAddress;
}

void Unwind(CONTEXT& c, const uint8_t* copy, uint64_t rsp0, size_t len, std::vector<uint64_t>& out) {
	const uint64_t lo = rsp0, hi = rsp0 + len, d = (uint64_t)copy - rsp0;
	auto rebase = [&](DWORD64& r) { if (r >= lo && r < hi) r += d; };
	c.Rsp += d;
	for (int i = 0; i < kMaxFrames; i++) {
		rebase(c.Rbp); rebase(c.Rbx); rebase(c.Rsi); rebase(c.Rdi); rebase(c.R12); rebase(c.R13); rebase(c.R14); rebase(c.R15);
		uint64_t base = 0;
		out.push_back(FuncStart(c.Rip, &base));
		if (c.Rsp < (uint64_t)copy || c.Rsp + 8 > (uint64_t)copy + len) break;
		DWORD64 ib = 0;
		PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(c.Rip, &ib, nullptr);
		if (!fe) { c.Rip = *(DWORD64*)c.Rsp; c.Rsp += 8; }
		else {
			void* hd = nullptr;
			DWORD64 est = 0;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, ib, c.Rip, fe, &c, &hd, &est, nullptr);
		}
		if (!c.Rip || c.Rsp < (uint64_t)copy || c.Rsp > (uint64_t)copy + len) break;
	}
}

DWORD WINAPI SamplerThread(LPVOID) {
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
	std::vector<uint8_t> copy(kStackCopy);
	while (g_run.load()) {
		Sleep(1);
		const int phase = Loadwatch_QuestPhase();
		if (SuspendThread(g_thread) == (DWORD)-1) break;
		CONTEXT c{};
		c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
		size_t len = 0;
		uint64_t rsp0 = 0;
		const bool got = GetThreadContext(g_thread, &c) != 0;
		if (got) {
			rsp0 = c.Rsp;
			len = rsp0 < g_stackBase ? std::min<size_t>(kStackCopy, g_stackBase - rsp0) : 0;
			if (len) memcpy(copy.data(), (void*)rsp0, len);
		}
		ResumeThread(g_thread);
		if (!got || !len) continue;
		Sample s;
		s.phase = phase;
		s.f.reserve(48);
		Unwind(c, copy.data(), rsp0, len, s.f);
		g_s.push_back(std::move(s));
	}
	return 0;
}

std::string Name(uint64_t f) {
	if (f >> 63) { char b[48]; snprintf(b, sizeof b, "leaf@%llx", (unsigned long long)(f & ~(1ull << 63))); return b; }
	HMODULE m = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)f, &m);
	const uint64_t mb = (uint64_t)m;
	auto it = g_mod.find(mb);
	if (it == g_mod.end()) {
		char p[MAX_PATH] = "?";
		if (m) GetModuleFileNameA(m, p, MAX_PATH);
		std::string s = p;
		s = s.substr(s.find_last_of("\\/") + 1);
		it = g_mod.emplace(mb, s).first;
	}
	char b[96];
	if (!_stricmp(it->second.c_str(), "WHGame.dll")) snprintf(b, sizeof b, "FUN_%llx", (unsigned long long)(f - mb + 0x180000000ull));
	else snprintf(b, sizeof b, "%s+%llx", it->second.c_str(), (unsigned long long)(f - mb));
	return b;
}

const char* PhaseName(int p) {
	switch (p) {
	case 1: return "job (outside projects)";
	case 2: return "project (phase 1, activation)";
	case 3: return "2a nodes";
	case 4: return "2b ports";
	case 5: return "2b edges";
	default: return "not in the job";
	}
}

}  // namespace

void Questsample_Begin() {
	if (g_run.load()) return;
	if (g_dir.empty()) {
		char p[MAX_PATH] = {};
		HMODULE self = nullptr;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&Questsample_Begin, &self);
		GetModuleFileNameA(self, p, MAX_PATH);
		g_dir = p;
		g_dir = g_dir.substr(0, g_dir.find_last_of('\\') + 1);
		LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	}
	g_stackBase = (uintptr_t)((NT_TIB*)NtCurrentTeb())->StackBase;
	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_thread,
	                     THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0)) { g_thread = nullptr; return; }
	g_s.clear();
	g_s.reserve(20000);
	LARGE_INTEGER t; QueryPerformanceCounter(&t); g_t0 = t.QuadPart;
	g_run = true;
	g_sampler = CreateThread(nullptr, 0, SamplerThread, nullptr, 0, nullptr);
	if (!g_sampler) { g_run = false; CloseHandle(g_thread); g_thread = nullptr; }
}

void Questsample_End(const char* tag) {
	if (!g_run.exchange(false)) return;
	WaitForSingleObject(g_sampler, 5000);
	CloseHandle(g_sampler); g_sampler = nullptr;
	CloseHandle(g_thread); g_thread = nullptr;
	LARGE_INTEGER t; QueryPerformanceCounter(&t);
	const double sec = (t.QuadPart - g_t0) * 1.0 / g_freq;
	// per phase: self (innermost frame) and inclusive (anywhere, once per sample)
	struct Agg { long long n = 0; std::unordered_map<uint64_t, long long> self, incl; };
	Agg ph[6];
	for (const Sample& s : g_s) {
		Agg& a = ph[(std::min)((std::max)(s.phase, 0), 5)];
		a.n++;
		if (s.f.empty()) continue;
		a.self[s.f[0]]++;
		std::unordered_set<uint64_t> seen;
		for (uint64_t f : s.f) if (seen.insert(f).second) a.incl[f]++;
	}
	Log("questsample: %s: %zu samples in %.2f s (%.0f a second)", tag, g_s.size(), sec, g_s.size() / (sec > 0 ? sec : 1));
	for (int p = 0; p < 6; p++) {
		if (!ph[p].n) continue;
		const double msPer = sec * 1000.0 / (g_s.empty() ? 1 : g_s.size());
		Log("questsample: %s: phase %s: %lld samples (~%.0f ms)", tag, PhaseName(p), ph[p].n, ph[p].n * msPer);
		for (int k = 0; k < 2; k++) {
			std::vector<std::pair<long long, uint64_t>> v;
			for (auto& e : (k ? ph[p].incl : ph[p].self)) v.emplace_back(e.second, e.first);
			std::sort(v.begin(), v.end(), [](auto& x, auto& y) { return x.first > y.first; });
			std::string line;
			for (size_t i = 0; i < v.size() && i < 30; i++) {
				if (v[i].first * 100 < ph[p].n) break;   // under 1 %
				char b[160];
				snprintf(b, sizeof b, "%s%s %.1f%%", line.empty() ? "" : ", ", Name(v[i].second).c_str(), v[i].first * 100.0 / ph[p].n);
				line += b;
			}
			Log("questsample: %s:   %s: %s", tag, k ? "inclusive" : "self", line.c_str());
		}
	}
	if (FILE* f = fopen((g_dir + "fastload_qsample.csv").c_str(), "a")) {
		for (const Sample& s : g_s) {
			fprintf(f, "%s,%d,", tag, s.phase);
			for (size_t i = 0; i < s.f.size(); i++) fprintf(f, "%s%s", i ? ";" : "", Name(s.f[i]).c_str());
			fputc('\n', f);
		}
		fclose(f);
	}
	std::vector<Sample>().swap(g_s);
}
