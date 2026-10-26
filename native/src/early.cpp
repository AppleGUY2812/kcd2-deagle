// The start-up before the plugin's Init thread runs: see early.h.
#include "early.h"
#include "log.h"
#include <tlhelp32.h>
#include <intrin.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr int kMax = 14000;    // 30 s at 10 ms for Main, plus the other threads until the first frame
constexpr int kRet = 24;       // return addresses kept per sample
// cpuUs: the thread's user + kernel time (GetThreadTimes: 15.6 ms steps); cyc: its exact CPU cycles (QueryThreadCycleTime)
struct Sample { uint32_t tMs, cpuUs, grabUs; uint8_t n, th, ok; uint64_t rip, cyc; uint64_t ret[kRet]; };   // ok: written
Sample g_s[kMax];              // static and touched before use: a sampler never allocates or faults while its thread is stopped
volatile LONG g_n = 0;         // slots taken (a slot's ok is set once it is written)
volatile LONG g_stop = 0;
long long g_freq = 1, g_dllMain = 0, g_samplerStart = 0;
unsigned long long g_tsc0 = 0;   // the time-stamp counter at DllMain: cycles per ms for the busy shares
FILETIME g_dllMainFt{};
DWORD g_mainTid = 0;
HANDLE g_main = nullptr;
HMODULE g_self = nullptr;
uintptr_t g_stackBase = 0;
bool g_whgameAtDllMain = false;
int g_mode = 3;   // fastload_early.txt: 0 off, 1 until Init, 2 Main until the first frame, 3 also other threads + the memory probe
double g_procToDllMain = 0;
// the sampled threads: Main, and (mode 3) the game's render and audio threads and our GPU workers, found by name; each has
// its own sampler thread, so one that can't be stopped (in the kernel) doesn't blind the others
struct Target { HANDLE h; uintptr_t stackBase; DWORD tid; long long foundAt; const wchar_t* name; const char* label; };
Target g_t[] = { { nullptr, 0, 0, 0, L"", "Main" },
                 { nullptr, 0, 0, 0, L"RenderThread", "RenderThread" },
                 { nullptr, 0, 0, 0, L"AudioThread", "AudioThread" },            // "AudioThread(NoCfg)"
                 { nullptr, 0, 0, 0, L"JobWorker_fastload gpuwarm", "gpuwarm" },
                 { nullptr, 0, 0, 0, L"JobWorker_fastload pipelib", "pipelib" },
                 { nullptr, 0, 0, 0, L"fastload ngxearly", "ngxearly" } };
constexpr int kTargets = sizeof g_t / sizeof g_t[0];
uint8_t g_stacks[kTargets][64 * 1024];

// The memory probe (mode 3): every 10 ms, how late a 10 ms sleep wakes (the thread not run), one first touch of a fresh
// page (a page fault: the process's working-set lock) and a 64 KB VirtualAlloc + touch + free (the address-space lock).
struct Probe { uint32_t tMs, sleepUs, faultUs, vaUs; };
constexpr int kProbes = 4000;
Probe g_p[kProbes];
volatile LONG g_np = 0;

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline uint32_t MsNow() { return (uint32_t)((Now() - g_dllMain) * 1000 / g_freq); }
inline uint32_t Us(long long a, long long b) { return (uint32_t)((b - a) * 1000000 / g_freq); }

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

// One sample of thread ti: stopped only for the context and the stack copy, nothing allocated meanwhile.
// false: the thread is gone.
bool Grab(int ti) {
	Target& t = g_t[ti];
	FILETIME cr{}, ex{}, kt{}, ut{};
	const bool times = GetThreadTimes(t.h, &cr, &ex, &kt, &ut) != 0;
	if (times && (ex.dwLowDateTime || ex.dwHighDateTime)) return false;   // it exited
	ULONG64 cyc = 0;
	QueryThreadCycleTime(t.h, &cyc);
	const long long g0 = Now();
	if (SuspendThread(t.h) == (DWORD)-1) return false;
	CONTEXT c{};
	c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
	const bool ok = GetThreadContext(t.h, &c) != 0;
	size_t n = 0;
	uint8_t* stack = g_stacks[ti];
	if (ok && t.stackBase && c.Rsp < t.stackBase && t.stackBase - c.Rsp < (8u << 20)) {
		const size_t want = std::min<size_t>(t.stackBase - c.Rsp, sizeof g_stacks[ti]);
		n = Copy(stack, c.Rsp, want);
	}
	ResumeThread(t.h);
	if (!ok) return true;
	const long long now = Now();
	const LONG idx = InterlockedIncrement(&g_n) - 1;
	if (idx >= kMax) return true;
	Sample& s = g_s[idx];
	s.tMs = (uint32_t)((now - g_dllMain) * 1000 / g_freq);
	s.grabUs = Us(g0, now);
	auto t100 = [](const FILETIME& x) { return ((unsigned long long)x.dwHighDateTime << 32) | x.dwLowDateTime; };
	s.cpuUs = times ? (uint32_t)((t100(kt) + t100(ut)) / 10) : 0;
	s.cyc = cyc;
	s.th = (uint8_t)ti;
	s.rip = c.Rip;
	s.n = 0;
	for (size_t i = 0; i + 8 <= n && s.n < kRet; i += 8) {
		uint64_t v;
		memcpy(&v, stack + i, 8);
		if (AfterCall(v)) s.ret[s.n++] = v;
	}
	MemoryBarrier();
	*(volatile uint8_t*)&s.ok = 1;
	return true;
}

using NtCreateThreadExFn = LONG(NTAPI*)(PHANDLE, ACCESS_MASK, PVOID, HANDLE, PVOID, PVOID, ULONG, SIZE_T, SIZE_T, SIZE_T, PVOID);
using DescFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
using NtQitFn = LONG(NTAPI*)(HANDLE, int, PVOID, ULONG, PULONG);
// Resolved in Early_Start on Main. The sampler threads are made with THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH, which also
// leaves them without the DLL's TLS block: no function-local statics (MSVC's guard reads a TLS variable), no
// thread_local, nothing that needs either may run on them (b529e1f did: the game died at 0.3 s in every boot of 157).
NtCreateThreadExFn g_create = nullptr;
DescFn g_desc = nullptr;
NtQitFn g_qit = nullptr;
DWORD g_named[2048];   // threads already named something else
int g_nNamed = 0;

// 0x2 = THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH: no DllMain thread notifications, so no wait for the loader lock there
bool Spawn(LPTHREAD_START_ROUTINE fn, void* arg) {
	const NtCreateThreadExFn create = g_create;
	HANDLE t = nullptr;
	if (!create || create(&t, THREAD_ALL_ACCESS, nullptr, GetCurrentProcess(), (PVOID)fn, arg, 0x2, 0, 0, 0, nullptr) < 0 || !t)
		return false;
	SetThreadPriority(t, THREAD_PRIORITY_HIGHEST);   // so a busy machine delays our samples less than the game's threads
	CloseHandle(t);
	return true;
}

DWORD WINAPI TargetSampler(LPVOID p) {
	const int ti = (int)(intptr_t)p;
	while (!g_stop && g_n < kMax && Grab(ti)) Sleep(10);
	return 0;
}

struct Tbi { LONG exitStatus; PVOID teb; HANDLE pid, tid; ULONG_PTR affinity; LONG prio, basePrio; };

// Mode 3: the other threads by their names (set as they start); each found one gets its own sampler. Runs on Main's
// sampler with no thread stopped (it allocates).
void FindThreads() {
	const DescFn desc = g_desc;
	const NtQitFn qit = g_qit;
	DWORD* const named = g_named;
	int& nNamed = g_nNamed;
	if (!desc || !qit) return;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE) return;
	const DWORD pid = GetCurrentProcessId();
	THREADENTRY32 te{};
	te.dwSize = sizeof te;
	for (BOOL more = Thread32First(snap, &te); more; more = Thread32Next(snap, &te)) {
		const DWORD tid = te.th32ThreadID;
		if (te.th32OwnerProcessID != pid || tid == g_mainTid) continue;
		bool skip = false;
		for (int i = 0; i < kTargets && !skip; i++) skip = g_t[i].tid == tid;
		for (int i = 0; i < nNamed && !skip; i++) skip = named[i] == tid;
		if (skip) continue;
		HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
		if (!h) continue;
		PWSTR w = nullptr;
		int hit = -1;
		if (SUCCEEDED(desc(h, &w)) && w) {
			for (int i = 1; i < kTargets; i++)
				if (!g_t[i].h && !wcsncmp(w, g_t[i].name, wcslen(g_t[i].name))) hit = i;
			if (hit < 0 && *w && nNamed < 2048) named[nNamed++] = tid;
			LocalFree(w);
		}
		Tbi tbi{};
		uintptr_t base = 0;
		if (hit > 0 && qit(h, 0, &tbi, sizeof tbi, nullptr) >= 0 && tbi.teb)
			Copy(&base, (uintptr_t)tbi.teb + 8, sizeof base);   // NT_TIB.StackBase
		if (hit > 0 && base) {
			g_t[hit].stackBase = base;
			g_t[hit].tid = tid;
			g_t[hit].foundAt = Now();
			g_t[hit].h = h;
			if (!Spawn(&TargetSampler, (void*)(intptr_t)hit)) { g_t[hit].h = nullptr; CloseHandle(h); }
		} else CloseHandle(h);
	}
	CloseHandle(snap);
}

DWORD WINAPI MemProbe(LPVOID) {
	memset(g_p, 0, sizeof g_p);   // touched here, not during a probe
	constexpr size_t kRegion = 64u << 20;
	uint8_t* region = (uint8_t*)VirtualAlloc(nullptr, kRegion, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	size_t page = 0;
	while (!g_stop && g_np < kProbes) {
		const long long a = Now();
		Sleep(10);
		const long long b = Now();
		if (region && page < kRegion / 4096) *(volatile uint8_t*)(region + (page++) * 4096) = 1;
		const long long c = Now();
		if (uint8_t* v = (uint8_t*)VirtualAlloc(nullptr, 64 << 10, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
			*(volatile uint8_t*)v = 1;
			VirtualFree(v, 0, MEM_RELEASE);
		}
		const long long d = Now();
		Probe& p = g_p[g_np];
		p.tMs = (uint32_t)((d - g_dllMain) * 1000 / g_freq);
		p.sleepUs = Us(a, b);
		p.faultUs = region ? Us(b, c) : 0;
		p.vaUs = Us(c, d);
		InterlockedIncrement(&g_np);
	}
	if (region) VirtualFree(region, 0, MEM_RELEASE);
	return 0;
}

DWORD WINAPI Sampler(LPVOID) {
	memset(g_s, 0, sizeof g_s);   // every page touched before the first sample
	memset(g_stacks, 0, sizeof g_stacks);
	g_samplerStart = Now();
	for (int k = 0; !g_stop && g_n < kMax; k++) {
		Grab(0);
		if (g_mode == 3 && k % 10 == 0) {
			bool missing = false;
			for (int i = 1; i < kTargets; i++) missing |= !g_t[i].h;
			if (missing) FindThreads();
		}
		Sleep(10);
	}
	return 0;
}

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
	g_tsc0 = __rdtsc();
	GetSystemTimePreciseAsFileTime(&g_dllMainFt);
	g_self = self;
	g_mainTid = GetCurrentThreadId();
	g_whgameAtDllMain = GetModuleHandleA("WHGame.dll") != nullptr;
	ULONG_PTR lo = 0, hi = 0;
	GetCurrentThreadStackLimits(&lo, &hi);
	g_stackBase = hi;
	g_main = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_mainTid);
	g_create = (NtCreateThreadExFn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCreateThreadEx");
	g_qit = (NtQitFn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
	g_desc = (DescFn)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "GetThreadDescription");   // kernel32's is a forwarder
	if (!g_desc) g_desc = (DescFn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadDescription");
	g_t[0].h = g_main;
	g_t[0].stackBase = g_stackBase;
	g_t[0].tid = g_mainTid;
	char path[MAX_PATH] = {};
	GetModuleFileNameA(self, path, MAX_PATH);
	if (char* sl = strrchr(path, '\\')) strcpy_s(sl + 1, MAX_PATH - (sl + 1 - path), "fastload_early.txt");
	if (FILE* fp = fopen(path, "r")) {   // fastload_early.txt next to the plugin: 0 off, 1 until Init, 2 the first frame, 3 (default) + threads
		int m = 3;
		if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 3) g_mode = m;
		fclose(fp);
	}
	if (!g_mode || !g_main) return;
	Spawn(&Sampler, nullptr);
	if (g_mode == 3) Spawn(&MemProbe, nullptr);
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

// a thread's busy share (0-100) from its CPU cycles over ms of wall time; the TSC rate measured since DllMain
double g_tscPerMs = 0;
double Busy(uint64_t cycles, uint32_t ms) {
	if (g_tscPerMs <= 0) g_tscPerMs = (double)(__rdtsc() - g_tsc0) / ((Now() - g_dllMain) * 1000.0 / g_freq);
	return ms ? (std::min)(100.0, 100.0 * (double)cycles / (g_tscPerMs * ms)) : 0;
}

// the written samples so far, in time order (the samplers run on their own threads)
std::vector<Sample> Snapshot() {
	const int n = (std::min)((int)g_n, kMax);
	std::vector<Sample> v;
	v.reserve(n);
	for (int i = 0; i < n; i++)
		if (*(volatile uint8_t*)&g_s[i].ok) v.push_back(g_s[i]);
	std::stable_sort(v.begin(), v.end(), [](const Sample& a, const Sample& b) { return a.tMs < b.tMs; });
	return v;
}

// fastload_early.csv, and Main's leaf modules + most-sampled frames
void Dump(const std::vector<Sample>& v, uintptr_t game, size_t gameSize, std::map<std::string, int>* leafOut,
          std::map<std::string, int>* inclOut) {
	FILE* csv = fopen((g_dir + "fastload_early.csv").c_str(), "w");
	if (csv) fprintf(csv, "t_ms,cpu_ms,grab_us,rip,frames,thread,busy\n");   // busy: % of the time since its last sample (cycles)
	uint64_t lastCyc[kTargets] = {};
	uint32_t lastT[kTargets] = {};
	bool have[kTargets] = {};
	for (const Sample& s : v) {
		const bool mine = s.th == 0;
		const std::string k = Key(s.rip, game, gameSize);
		if (mine && leafOut) (*leafOut)[k.substr(0, k.find('+'))]++;
		if (csv) fprintf(csv, "%u,%.1f,%u,%s", s.tMs, s.cpuUs / 1000.0, s.grabUs, k.c_str());
		std::map<std::string, int> seen;
		for (int j = 0; j < s.n; j++) {
			const std::string k2 = Key(s.ret[j], game, gameSize);
			if (csv) fprintf(csv, " %s", k2.c_str());
			if (mine && inclOut && !seen[k2]++) (*inclOut)[k2]++;
		}
		const double busy = have[s.th] ? Busy(s.cyc - lastCyc[s.th], s.tMs - lastT[s.th]) : -1;
		lastCyc[s.th] = s.cyc;
		lastT[s.th] = s.tMs;
		have[s.th] = true;
		if (csv) fprintf(csv, ",%s,%.0f\n", g_t[s.th].label, busy);
	}
	if (csv) fclose(csv);
	if (FILE* p = fopen((g_dir + "fastload_early_probe.csv").c_str(), "w")) {
		fprintf(p, "t_ms,sleep_us,fault_us,valloc_us\n");
		const int np = (std::min)((int)g_np, kProbes);
		for (int i = 0; i < np; i++) fprintf(p, "%u,%u,%u,%u\n", g_p[i].tMs, g_p[i].sleepUs, g_p[i].faultUs, g_p[i].vaUs);
		fclose(p);
	}
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
	const std::vector<Sample> v = Snapshot();
	int nMain = 0;
	uint32_t first = 0, last = 0;
	for (const Sample& s : v)
		if (s.th == 0) { if (!nMain++) first = s.tMs; last = s.tMs; }
	Log("early: DllMain %.0f ms after the process started (WHGame.dll %s then; thread %lu); the Init thread %.0f ms after DllMain; "
	    "the early sampler started %.0f ms after DllMain (%d samples)%s", g_procToDllMain, g_whgameAtDllMain ? "loaded" : "not loaded",
	    g_mainTid, toInit, toSampler, nMain, g_mode >= 2 ? "; it goes on until the first frame (fastload_early.txt 2 or 3)" : "");
	if (!nMain) return;
	{
		char p[MAX_PATH] = {};
		GetModuleFileNameA(g_self, p, MAX_PATH);
		g_dir = p;
		g_dir = g_dir.substr(0, g_dir.find_last_of('\\') + 1);
	}
	std::map<std::string, int> leaf, incl;
	Dump(v, game, gameSize, &leaf, &incl);
	Log("early: %d samples of the DllMain thread from %u to %u ms after DllMain; by module (leaf): %s", nMain, first, last,
	    Top(leaf, 8).c_str());
	Log("early: most-sampled frames (samples, of %d): %s", nMain, Top(incl, 30).c_str());
	Log("early: every sample in fastload_early.csv");
}

// The first frame (mode 2 and 3): each thread from DllMain (or when it was found) to here, one line a second (its CPU use,
// the longest stretch without a sample: it could not be stopped then, e.g. in the kernel), and every sample in the csv.
namespace {
uintptr_t g_rGame = 0;
size_t g_rSize = 0;

// one line a second for thread ti
void ThreadLines(const std::vector<Sample>& v, int ti, uintptr_t game, size_t gameSize, double t0) {
	const int n = (int)v.size();
	int i = 0;
	while (i < n && v[i].th != ti) i++;
	while (i < n) {
		const uint32_t sec = v[i].tMs / 1000;
		int j = i, cnt = 0, last = i;
		uint32_t maxGap = 0, gapAt = 0, prevT = 0;
		bool havePrev = false;
		std::map<std::string, int> leaf, fr;
		for (; j < n && v[j].tMs / 1000 == sec; j++) {
			const Sample& x = v[j];
			if (x.th != ti) continue;
			cnt++;
			last = j;
			if (havePrev && x.tMs - prevT > maxGap) { maxGap = x.tMs - prevT; gapAt = prevT; }
			prevT = x.tMs;
			havePrev = true;
			const std::string k = Key(x.rip, game, gameSize);
			leaf[k.substr(0, k.find('+'))]++;
			// the deepest game frames say the phase: the 3rd WHGame frame from the top
			int seenGame = 0;
			for (int r = 0; r < x.n; r++) {
				const uint64_t ad = x.ret[r];
				if (ad >= game && ad < game + gameSize && ++seenGame == 3) { fr[Key(ad, game, gameSize)]++; break; }
			}
		}
		const Sample& a = v[i];
		const Sample& b = v[last];
		const double wall = (double)b.tMs - a.tMs;
		const double cpu = (b.cpuUs - a.cpuUs) / 1000.0;
		const double busy = ti == 0 ? (wall > 0 ? 100.0 * cpu / wall : 0.0) : Busy(b.cyc - a.cyc, b.tMs - a.tMs);
		Log("early: %s %.1f-%.1f s: %d samples, busy %.0f%% (CPU %.0f ms of %.0f), longest gap %u ms at %.2f s; leaf %s; game frames %s",
		    g_t[ti].label, (sec * 1000 + t0) / 1000.0, ((sec + 1) * 1000 + t0) / 1000.0, cnt, busy,
		    ti == 0 ? cpu : busy * wall / 100.0, wall, maxGap, (gapAt + t0) / 1000.0, Top(leaf, 4).c_str(), Top(fr, 3).c_str());
		i = j;
		while (i < n && v[i].th != ti) i++;
	}
}

// per-sample busy share (cycles since the thread's previous sample), -1 for a thread's first
std::vector<double> SampleBusy(const std::vector<Sample>& v) {
	std::vector<double> out(v.size(), -1);
	uint64_t lastCyc[kTargets] = {};
	uint32_t lastT[kTargets] = {};
	bool have[kTargets] = {};
	for (size_t i = 0; i < v.size(); i++) {
		const Sample& x = v[i];
		if (have[x.th]) out[i] = Busy(x.cyc - lastCyc[x.th], x.tMs - lastT[x.th]);
		lastCyc[x.th] = x.cyc;
		lastT[x.th] = x.tMs;
		have[x.th] = true;
	}
	return out;
}

// While Main waited (under 20% busy since its last sample), were the other threads busy (over 50%) within 6 ms of it?
void WaitLines(const std::vector<Sample>& v, double t0) {
	const std::vector<double> busy = SampleBusy(v);
	struct Sec { int mainN = 0, waitN = 0, busyN[kTargets] = {}, seenN[kTargets] = {}; };
	std::map<uint32_t, Sec> secs;
	const int n = (int)v.size();
	for (int i = 0; i < n; i++) {
		if (v[i].th != 0) continue;
		Sec& c = secs[v[i].tMs / 1000];
		c.mainN++;
		if (busy[i] < 0 || busy[i] >= 20) continue;
		c.waitN++;
		bool seen[kTargets] = {};
		for (int d = -24; d <= 24; d++) {   // the nearest sample of each other thread
			const int j = i + d;
			if (j < 0 || j >= n || v[j].th == 0 || seen[v[j].th] || busy[j] < 0) continue;
			if ((int)v[j].tMs - (int)v[i].tMs > 6 || (int)v[i].tMs - (int)v[j].tMs > 6) continue;
			seen[v[j].th] = true;
			c.seenN[v[j].th]++;
			if (busy[j] > 50) c.busyN[v[j].th]++;
		}
	}
	for (auto& e : secs) {
		const Sec& c = e.second;
		if (!c.waitN) continue;
		char other[400] = "";
		size_t at = 0;
		for (int t = 1; t < kTargets; t++)
			if (c.seenN[t] && at < sizeof other)
				at += snprintf(other + at, sizeof other - at, "%s %s busy in %d of %d", at ? "," : "", g_t[t].label, c.busyN[t], c.seenN[t]);
		Log("early: %.1f-%.1f s: Main waited in %d of %d samples;%s", (e.first * 1000 + t0) / 1000.0, ((e.first + 1) * 1000 + t0) / 1000.0,
		    c.waitN, c.mainN, at ? other : " no other thread sampled then");
	}
}

// Main's stretches without a sample over 300 ms: what the other threads and the memory probe did meanwhile
void GapLines(const std::vector<Sample>& v, uintptr_t game, size_t gameSize, double t0) {
	const std::vector<double> busy = SampleBusy(v);
	const int np = (std::min)((int)g_np, kProbes);
	int prev = -1;
	for (int i = 0; i < (int)v.size(); i++) {
		if (v[i].th != 0) continue;
		if (prev >= 0 && v[i].tMs - v[prev].tMs > 300) {
			const uint32_t a = v[prev].tMs, b = v[i].tMs;
			std::string other;
			for (int t = 1; t < kTargets; t++) {
				int cnt = 0, first = -1, last = -1;
				std::map<std::string, int> leaf;
				for (int j = prev; j < i; j++)
					if (v[j].th == t) {
						cnt++;
						if (first < 0) first = j;
						last = j;
						const std::string k = Key(v[j].rip, game, gameSize);
						leaf[k.substr(0, k.find('+'))]++;
					}
				if (!cnt) continue;
				char one[300];
				snprintf(one, sizeof one, "%s%s %d samples, busy %.0f%%, leaf %s", other.empty() ? "" : "; ", g_t[t].label, cnt,
				         last > first ? Busy(v[last].cyc - v[first].cyc, v[last].tMs - v[first].tMs) : 0.0, Top(leaf, 3).c_str());
				other += one;
			}
			uint32_t sl = 0, fa = 0, va = 0, cntP = 0;
			for (int k = 0; k < np; k++)
				if (g_p[k].tMs > a && g_p[k].tMs <= b) {
					cntP++;
					sl = (std::max)(sl, g_p[k].sleepUs);
					fa = (std::max)(fa, g_p[k].faultUs);
					va = (std::max)(va, g_p[k].vaUs);
				}
			Log("early: Main not sampled %.2f-%.2f s (%u ms): %s; memory probe %u ticks, the slowest 10 ms sleep %.0f ms, page fault %.1f ms, "
			    "VirtualAlloc %.1f ms", (a + t0) / 1000.0, (b + t0) / 1000.0, b - a, other.empty() ? "no other thread sampled" : other.c_str(),
			    cntP, sl / 1000.0, fa / 1000.0, va / 1000.0);
		}
		prev = i;
	}
}

// the memory probe, one line a second where anything was slow (a sleep over 30 ms, a fault or VirtualAlloc over 5 ms)
void ProbeLines(double t0) {
	const int np = (std::min)((int)g_np, kProbes);
	std::map<uint32_t, Probe> worst;
	std::map<uint32_t, int> slowN;
	for (int k = 0; k < np; k++) {
		const Probe& p = g_p[k];
		Probe& w = worst[p.tMs / 1000];
		w.sleepUs = (std::max)(w.sleepUs, p.sleepUs);
		w.faultUs = (std::max)(w.faultUs, p.faultUs);
		w.vaUs = (std::max)(w.vaUs, p.vaUs);
		if (p.sleepUs > 30000 || p.faultUs > 5000 || p.vaUs > 5000) slowN[p.tMs / 1000]++;
	}
	int lines = 0;
	for (auto& e : worst)
		if (slowN[e.first] && lines++ < 30)
			Log("early: memory probe %.1f-%.1f s: %d slow ticks; the slowest 10 ms sleep %.0f ms, page fault %.1f ms, VirtualAlloc %.1f ms",
			    (e.first * 1000 + t0) / 1000.0, ((e.first + 1) * 1000 + t0) / 1000.0, slowN[e.first], e.second.sleepUs / 1000.0,
			    e.second.faultUs / 1000.0, e.second.vaUs / 1000.0);
	if (!lines) Log("early: memory probe: %d ticks, none slow (sleeps under 30 ms, faults and VirtualAlloc under 5 ms)", np);
}

DWORD WINAPI FrameReport(LPVOID) {
	const uintptr_t game = g_rGame;
	const size_t gameSize = g_rSize;
	Sleep(30);   // the samplers' last samples
	const std::vector<Sample> v = Snapshot();
	if (v.size() < 2 || g_dir.empty()) return 0;
	Dump(v, game, gameSize, nullptr, nullptr);
	const double t0 = g_procToDllMain;
	int per[kTargets] = {};
	for (const Sample& s : v) per[s.th]++;
	std::string found;
	if (g_mode == 3)
		for (int t = 1; t < kTargets; t++) {
			char one[120];
			if (g_t[t].h)
				snprintf(one, sizeof one, "%s%s from %.1f s (%d samples)", found.empty() ? "" : ", ", g_t[t].label,
				         ((g_t[t].foundAt - g_dllMain) * 1000.0 / g_freq + t0) / 1000.0, per[t]);
			else snprintf(one, sizeof one, "%s%s not found", found.empty() ? "" : ", ", g_t[t].label);
			found += one;
		}
	Log("early: Main from DllMain to the first frame, %d samples (times below from the process start: DllMain + %.0f ms)%s%s; "
	    "every sample in fastload_early.csv%s", per[0], t0, found.empty() ? "" : "; also ", found.c_str(),
	    g_mode == 3 ? ", the memory probe in fastload_early_probe.csv" : "");
	for (int t = 0; t < kTargets; t++)
		if (per[t]) ThreadLines(v, t, game, gameSize, t0);
	if (g_mode == 3) {
		WaitLines(v, t0);
		GapLines(v, game, gameSize, t0);
		ProbeLines(t0);
	}
	return 0;
}
}  // namespace

void Early_Frame(uintptr_t game, size_t gameSize) {
	static bool done = false;
	if (done || g_mode < 2) return;
	done = true;
	g_stop = 1;
	g_rGame = game;
	g_rSize = gameSize;
	if (HANDLE t = CreateThread(nullptr, 0, &FrameReport, nullptr, 0, nullptr)) CloseHandle(t);   // off Main: thousands of samples to name
}
