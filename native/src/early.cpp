// The start-up before the plugin's Init thread runs: see early.h.
#include "early.h"
#include "log.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr int kMax = 3000;     // 30 s at 10 ms
constexpr int kRet = 24;       // return addresses kept per sample
struct Sample { uint32_t tMs; uint8_t n; uint64_t rip; uint64_t ret[kRet]; };
Sample g_s[kMax];              // static: the sampler never allocates (the heap lock may be held by the suspended thread)
volatile LONG g_n = 0;
volatile LONG g_stop = 0;
uint8_t g_stack[64 * 1024];
long long g_freq = 1, g_dllMain = 0, g_samplerStart = 0;
FILETIME g_dllMainFt{};
DWORD g_mainTid = 0;
HANDLE g_main = nullptr;
HMODULE g_self = nullptr;
uintptr_t g_stackBase = 0;
bool g_whgameAtDllMain = false;

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

void* (*volatile g_copy)(void*, const void*, size_t) = memcpy;
__declspec(noinline) size_t Copy(void* dst, uintptr_t src, size_t n) {
	__try { g_copy(dst, (const void*)src, n); return n; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// a return address: the bytes before it are a call (E8 rel32, or FF /2 in its 2..7-byte forms)
bool AfterCall(uint64_t a) {
	if (a < 0x10000 || a > 0x7fffffffffffull) return false;
	uint8_t c[8];
	if (Copy(c, (uintptr_t)a - 8, 8) != 8) return false;
	auto ff2 = [&](int i) { return c[i] == 0xFF && (c[i + 1] & 0x38) == 0x10; };
	return c[3] == 0xE8 || ff2(6) || ff2(5) || ff2(4) || ff2(2) || ff2(1);
}

DWORD WINAPI Sampler(LPVOID) {
	g_samplerStart = Now();
	while (!g_stop && g_n < kMax) {
		if (SuspendThread(g_main) != (DWORD)-1) {
			CONTEXT c{};
			c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
			const bool ok = GetThreadContext(g_main, &c) != 0;
			size_t n = 0;
			if (ok && g_stackBase && c.Rsp < g_stackBase && g_stackBase - c.Rsp < (8u << 20)) {
				const size_t want = std::min<size_t>(g_stackBase - c.Rsp, sizeof g_stack);
				n = Copy(g_stack, c.Rsp, want);
			}
			ResumeThread(g_main);
			if (ok) {
				Sample& s = g_s[g_n];
				s.tMs = (uint32_t)((Now() - g_dllMain) * 1000 / g_freq);
				s.rip = c.Rip;
				s.n = 0;
				for (size_t i = 0; i + 8 <= n && s.n < kRet; i += 8) {
					uint64_t v;
					memcpy(&v, g_stack + i, 8);
					if (AfterCall(v)) s.ret[s.n++] = v;
				}
				InterlockedIncrement(&g_n);
			}
		}
		Sleep(10);
	}
	return 0;
}

using NtCreateThreadExFn = LONG(NTAPI*)(PHANDLE, ACCESS_MASK, PVOID, HANDLE, PVOID, PVOID, ULONG, SIZE_T, SIZE_T, SIZE_T, PVOID);

std::string ModName(uint64_t a, uintptr_t game, size_t gameSize, uint64_t* rva) {
	if (a >= game && a < game + gameSize) { *rva = a - game; return "WHGame.dll"; }
	HMODULE m = nullptr;
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) && m) {
		char p[MAX_PATH] = {};
		GetModuleFileNameA(m, p, MAX_PATH);
		const char* s = strrchr(p, '\\');
		*rva = a - (uintptr_t)m;
		return s ? s + 1 : p;
	}
	*rva = a;
	return "?";
}

}  // namespace

void Early_Start(HMODULE self) {
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	g_freq = f.QuadPart;
	g_dllMain = Now();
	GetSystemTimePreciseAsFileTime(&g_dllMainFt);
	g_self = self;
	g_mainTid = GetCurrentThreadId();
	g_whgameAtDllMain = GetModuleHandleA("WHGame.dll") != nullptr;
	ULONG_PTR lo = 0, hi = 0;
	GetCurrentThreadStackLimits(&lo, &hi);
	g_stackBase = hi;
	g_main = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_mainTid);
	char path[MAX_PATH] = {};
	GetModuleFileNameA(self, path, MAX_PATH);
	if (char* sl = strrchr(path, '\\')) strcpy_s(sl + 1, MAX_PATH - (sl + 1 - path), "fastload_early.txt");
	if (FILE* fp = fopen(path, "r")) {   // fastload_early.txt next to the plugin: 0 = no early sampler
		int m = 1;
		if (fscanf(fp, "%d", &m) == 1 && m == 0) { fclose(fp); return; }
		fclose(fp);
	}
	auto create = (NtCreateThreadExFn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCreateThreadEx");
	if (!g_main || !create) return;
	HANDLE t = nullptr;
	// 0x2 = THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH: no DllMain thread notifications, so no wait for the loader lock there
	if (create(&t, THREAD_ALL_ACCESS, nullptr, GetCurrentProcess(), (PVOID)&Sampler, nullptr, 0x2, 0, 0, 0, nullptr) >= 0 && t)
		CloseHandle(t);
}

void Early_Report(uintptr_t game, size_t gameSize) {
	const long long initAt = Now();
	g_stop = 1;
	Sleep(30);
	FILETIME create{}, a{}, b{}, c{};
	GetProcessTimes(GetCurrentProcess(), &create, &a, &b, &c);
	auto ft = [](const FILETIME& x) { return ((unsigned long long)x.dwHighDateTime << 32) | x.dwLowDateTime; };
	const double procToDllMain = ((long long)(ft(g_dllMainFt) - ft(create))) / 10000.0;
	const double toInit = (initAt - g_dllMain) * 1000.0 / g_freq;
	const double toSampler = g_samplerStart ? (g_samplerStart - g_dllMain) * 1000.0 / g_freq : -1;
	const int n = (int)g_n;
	Log("early: DllMain %.0f ms after the process started (WHGame.dll %s then; thread %lu); the Init thread %.0f ms after DllMain; "
	    "the early sampler %s (%d samples)", procToDllMain, g_whgameAtDllMain ? "loaded" : "not loaded", g_mainTid, toInit,
	    g_samplerStart ? (toSampler < toInit - 50 ? "ran under the loader lock" : "only started with the Init thread") : "never started",
	    n);
	if (!n) return;
	// the leaf and every frame, counted once per sample (inclusive), by module+rva
	std::map<std::string, int> leaf, incl;
	std::string dir;
	{
		char p[MAX_PATH] = {};
		GetModuleFileNameA(g_self, p, MAX_PATH);
		dir = p;
		dir = dir.substr(0, dir.find_last_of('\\') + 1);
	}
	FILE* csv = fopen((dir + "fastload_early.csv").c_str(), "w");
	if (csv) fprintf(csv, "t_ms,rip,frames\n");
	for (int i = 0; i < n; i++) {
		const Sample& s = g_s[i];
		uint64_t rva = 0;
		char k[160];
		std::string m = ModName(s.rip, game, gameSize, &rva);
		snprintf(k, sizeof k, "%s+%llx", m.c_str(), (unsigned long long)rva);
		leaf[m]++;
		std::map<std::string, int> seen;
		if (csv) fprintf(csv, "%u,%s", s.tMs, k);
		std::string fr;
		for (int j = 0; j < s.n; j++) {
			uint64_t r2 = 0;
			std::string m2 = ModName(s.ret[j], game, gameSize, &r2);
			char k2[160];
			snprintf(k2, sizeof k2, "%s+%llx", m2.c_str(), (unsigned long long)r2);
			if (csv) fprintf(csv, " %s", k2);
			if (!seen[k2]++) incl[k2]++;
		}
		if (csv) fprintf(csv, "\n");
	}
	if (csv) fclose(csv);
	auto top = [&](const std::map<std::string, int>& c, int k) {
		std::vector<std::pair<int, std::string>> v;
		for (auto& e : c) v.push_back({ e.second, e.first });
		std::sort(v.begin(), v.end(), [](auto& x, auto& y) { return x.first > y.first; });
		std::string out;
		for (int i = 0; i < (int)v.size() && i < k; i++) {
			char one[200];
			snprintf(one, sizeof one, "%s%s %d", i ? ", " : "", v[i].second.c_str(), v[i].first);
			out += one;
		}
		return out;
	};
	Log("early: %d samples of the DllMain thread from %u to %u ms after DllMain; by module (leaf): %s", n, g_s[0].tMs, g_s[n - 1].tMs,
	    top(leaf, 8).c_str());
	Log("early: most-sampled frames (samples, of %d): %s", n, top(incl, 30).c_str());
	Log("early: every sample in fastload_early.csv");
}
