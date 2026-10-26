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
struct Sample { uint32_t tMs, cpuUs, grabUs; uint8_t n; uint64_t rip; uint64_t ret[kRet]; };   // cpuUs: Main's user + kernel time
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
int g_mode = 2;   // fastload_early.txt: 0 off, 1 until Init, 2 until the first frame
double g_procToDllMain = 0;

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
		FILETIME cr{}, ex{}, kt{}, ut{};
		const bool times = GetThreadTimes(g_main, &cr, &ex, &kt, &ut) != 0;
		const long long g0 = Now();
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
				const long long now = Now();
				s.tMs = (uint32_t)((now - g_dllMain) * 1000 / g_freq);
				s.grabUs = (uint32_t)((now - g0) * 1000000 / g_freq);
				auto t100 = [](const FILETIME& x) { return ((unsigned long long)x.dwHighDateTime << 32) | x.dwLowDateTime; };
				s.cpuUs = times ? (uint32_t)((t100(kt) + t100(ut)) / 10) : 0;
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
	if (FILE* fp = fopen(path, "r")) {   // fastload_early.txt next to the plugin: 0 off, 1 until Init, 2 (default) the first frame
		int m = 2;
		if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 2) g_mode = m;
		fclose(fp);
	}
	if (!g_mode) return;
	auto create = (NtCreateThreadExFn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCreateThreadEx");
	if (!g_main || !create) return;
	HANDLE t = nullptr;
	// 0x2 = THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH: no DllMain thread notifications, so no wait for the loader lock there
	if (create(&t, THREAD_ALL_ACCESS, nullptr, GetCurrentProcess(), (PVOID)&Sampler, nullptr, 0x2, 0, 0, 0, nullptr) >= 0 && t)
		CloseHandle(t);
}

DWORD Early_MainThread() { return g_mainTid; }

void Early_Where(uint64_t a, uintptr_t game, size_t gameSize, char* out, size_t n) {
	uint64_t rva = 0;
	const std::string m = ModName(a, game, gameSize, &rva);
	snprintf(out, n, "%s+%llx", m.c_str(), (unsigned long long)rva);
}

namespace {
std::string g_dir;

std::string Top(const std::map<std::string, int>& c, int k) {
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
}

std::string Key(uint64_t a, uintptr_t game, size_t gameSize) {
	uint64_t rva = 0;
	const std::string m = ModName(a, game, gameSize, &rva);
	char k[160];
	snprintf(k, sizeof k, "%s+%llx", m.c_str(), (unsigned long long)rva);
	return k;
}

// samples [0, n): fastload_early.csv, and the leaf modules + most-sampled frames over [from, n)
void Dump(int from, int n, uintptr_t game, size_t gameSize, std::map<std::string, int>* leafOut, std::map<std::string, int>* inclOut) {
	FILE* csv = fopen((g_dir + "fastload_early.csv").c_str(), "w");
	if (csv) fprintf(csv, "t_ms,cpu_ms,grab_us,rip,frames\n");
	for (int i = 0; i < n; i++) {
		const Sample& s = g_s[i];
		const std::string k = Key(s.rip, game, gameSize);
		if (i >= from && leafOut) (*leafOut)[k.substr(0, k.find('+'))]++;
		if (csv) fprintf(csv, "%u,%.1f,%u,%s", s.tMs, s.cpuUs / 1000.0, s.grabUs, k.c_str());
		std::map<std::string, int> seen;
		for (int j = 0; j < s.n; j++) {
			const std::string k2 = Key(s.ret[j], game, gameSize);
			if (csv) fprintf(csv, " %s", k2.c_str());
			if (i >= from && inclOut && !seen[k2]++) (*inclOut)[k2]++;
		}
		if (csv) fprintf(csv, "\n");
	}
	if (csv) fclose(csv);
}
}  // namespace

void Early_Report(uintptr_t game, size_t gameSize) {
	const long long initAt = Now();
	if (g_mode == 1) {
		g_stop = 1;
		Sleep(30);
	}
	FILETIME create{}, a{}, b{}, c{};
	GetProcessTimes(GetCurrentProcess(), &create, &a, &b, &c);
	auto ft = [](const FILETIME& x) { return ((unsigned long long)x.dwHighDateTime << 32) | x.dwLowDateTime; };
	g_procToDllMain = ((long long)(ft(g_dllMainFt) - ft(create))) / 10000.0;
	const double toInit = (initAt - g_dllMain) * 1000.0 / g_freq;
	const double toSampler = g_samplerStart ? (g_samplerStart - g_dllMain) * 1000.0 / g_freq : -1;
	const int n = (int)g_n;
	Log("early: DllMain %.0f ms after the process started (WHGame.dll %s then; thread %lu); the Init thread %.0f ms after DllMain; "
	    "the early sampler started %.0f ms after DllMain (%d samples)%s", g_procToDllMain, g_whgameAtDllMain ? "loaded" : "not loaded",
	    g_mainTid, toInit, toSampler, n, g_mode == 2 ? "; it goes on until the first frame (fastload_early.txt 2)" : "");
	if (!n) return;
	{
		char p[MAX_PATH] = {};
		GetModuleFileNameA(g_self, p, MAX_PATH);
		g_dir = p;
		g_dir = g_dir.substr(0, g_dir.find_last_of('\\') + 1);
	}
	std::map<std::string, int> leaf, incl;
	Dump(0, n, game, gameSize, &leaf, &incl);
	Log("early: %d samples of the DllMain thread from %u to %u ms after DllMain; by module (leaf): %s", n, g_s[0].tMs, g_s[n - 1].tMs,
	    Top(leaf, 8).c_str());
	Log("early: most-sampled frames (samples, of %d): %s", n, Top(incl, 30).c_str());
	Log("early: every sample in fastload_early.csv");
}

// The first frame (mode 2): Main from DllMain to here, one line a second (its CPU use from the thread's own times, the
// longest stretch without a sample: Main could not be suspended then, e.g. in the kernel), and every sample in the csv.
namespace {
uintptr_t g_rGame = 0;
size_t g_rSize = 0;
DWORD WINAPI FrameReport(LPVOID) {
	const uintptr_t game = g_rGame;
	const size_t gameSize = g_rSize;
	Sleep(20);   // the sampler's last sample
	const int n = (int)g_n;
	if (n < 2 || g_dir.empty()) return 0;
	Dump(0, n, game, gameSize, nullptr, nullptr);
	const double t0 = g_procToDllMain;
	Log("early: Main from DllMain to the first frame, %d samples (times below from the process start: DllMain + %.0f ms); "
	    "every sample in fastload_early.csv", n, t0);
	int i = 0;
	while (i < n) {
		const uint32_t sec = g_s[i].tMs / 1000;
		int j = i;
		uint32_t maxGap = 0, gapAt = 0;
		std::map<std::string, int> leaf, fr;
		while (j < n && g_s[j].tMs / 1000 == sec) {
			if (j > 0 && g_s[j].tMs - g_s[j - 1].tMs > maxGap) { maxGap = g_s[j].tMs - g_s[j - 1].tMs; gapAt = g_s[j - 1].tMs; }
			const std::string k = Key(g_s[j].rip, game, gameSize);
			leaf[k.substr(0, k.find('+'))]++;
			// the deepest game frames say the phase: the 3rd WHGame frame from the top
			int seenGame = 0;
			for (int r = 0; r < g_s[j].n; r++) {
				const uint64_t a = g_s[j].ret[r];
				if (a >= game && a < game + gameSize && ++seenGame == 3) { fr[Key(a, game, gameSize)]++; break; }
			}
			j++;
		}
		const Sample& a = g_s[i];
		const Sample& b = g_s[j - 1];
		const double wall = (double)b.tMs - a.tMs;
		const double cpu = (b.cpuUs - a.cpuUs) / 1000.0;
		Log("early: Main %.1f-%.1f s: %d samples, busy %.0f%% (CPU %.0f ms of %.0f), longest gap %u ms at %.2f s; leaf %s; game frames %s",
		    (sec * 1000 + t0) / 1000.0, ((sec + 1) * 1000 + t0) / 1000.0, j - i, wall > 0 ? 100.0 * cpu / wall : 0.0, cpu, wall, maxGap,
		    (gapAt + t0) / 1000.0, Top(leaf, 4).c_str(), Top(fr, 3).c_str());
		i = j;
	}
	return 0;
}
}  // namespace

void Early_Frame(uintptr_t game, size_t gameSize) {
	static bool done = false;
	if (done || g_mode != 2) return;
	done = true;
	g_stop = 1;
	g_rGame = game;
	g_rSize = gameSize;
	if (HANDLE t = CreateThread(nullptr, 0, &FrameReport, nullptr, 0, nullptr)) CloseHandle(t);   // off Main: ~1200 samples to name
}
