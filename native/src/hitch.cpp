// Long frames, sampled from inside the game. See hitch.h.
#include "hitch.h"
#include "log.h"
#include "MinHook.h"
#include <winternl.h>
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
// Start-up window: plugin start -> the first frame tick sampled as long frame #0 (launch-to-menu, 2026-10-24). The main
// thread is guessed at install as the process's first thread; the first frame tick checks the guess.
bool g_startup = false;
std::atomic<HANDLE> g_fixMain{nullptr};   // the frame tick's thread when the guess was wrong; the watcher swaps it in
uintptr_t g_fixStack = 0;
uintptr_t g_game = 0, g_gameEnd = 0;
long long g_freq = 1;
std::string g_csvPath;
size_t g_csvBytes = 0;
constexpr size_t kStackBytes = 128 * 1024, kMaxCsvBytes = 48u << 20;   // 128 KB, 64 kept: a 5 s load frame
// (the save-load apply step, 2026-10-24) needs the outer phases, which 32 KB / 16 return addresses cut off
constexpr int kIntervalMs = 20, kMaxSamples = 6000, kKeep = 64;
alignas(16) uint8_t g_stack[kStackBytes];   // the watcher's only buffer while a thread is suspended

// The threads sampled with the main thread, by name (the engine names its threads); `hitch threads A,B`.
std::mutex g_namesMu;
std::vector<std::string> g_names = { "RenderThread", "RenderLoadingThread" };
bool g_namesChanged = true;
// Settle window (readiness.cpp, 2026-10-24): while a save-load gate holds the screen, frames are short, so nothing above
// samples it. Hitch_Window(true) samples Main, the threads above and the stream engine's two serial stages (the one pak
// reader and the one callback thread) until Hitch_Window(false), reported as one "window", with each thread's CPU use.
std::atomic<bool> g_window{false};
const char* const kWindowNames[] = { "Streaming File IO HDD", "Streaming AsyncCallback" };

struct Target { HANDLE h; uintptr_t stackBase; std::string name; DWORD tid; };
std::vector<Target> g_targets;   // [0] = main; watcher thread only

struct Module { uintptr_t base, end; std::string name; };
std::vector<Module> g_mods;   // sorted by base; refreshed at the start of each long frame

struct Sample { uint32_t tMs; uint32_t faults; uint64_t rip; uint8_t who; int n; uint64_t ret[kKeep]; };

long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

// ---- Files opened and DLLs loaded, by any thread: a ring the long-frame report reads back by time. ----
// Written from inside NtCreateFile / NtOpenFile (every file open in the process, the DLL loader's too) and the loader's
// DLL notification (under the loader lock): atomics and plain copies only, no locks, no allocation, no file calls.
constexpr uint32_t kEvRing = 8192;
constexpr size_t kEvPath = 176;
struct Ev { std::atomic<uint32_t> seq; long long t0; uint32_t us, tid, extra; char kind; char path[kEvPath]; };
Ev g_ev[kEvRing];
std::atomic<uint32_t> g_evNext{0};
std::atomic<bool> g_evOn{false};
bool g_watchOk = false;   // the file hooks are in

// UTF-16 name -> ASCII ('?' for the rest), keeping the end (the file name), guarded: the caller's memory.
__declspec(noinline) void CopyName(const UNICODE_STRING* us, char* out, size_t cap) {
	out[0] = 0;
	__try {
		if (!us || !us->Buffer) return;
		const size_t n = us->Length / 2, from = n > cap - 1 ? n - (cap - 1) : 0;
		size_t j = 0;
		for (size_t i = from; i < n; i++) { const wchar_t c = us->Buffer[i]; out[j++] = c >= 32 && c < 127 ? (char)c : '?'; }
		out[j] = 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}
__declspec(noinline) const UNICODE_STRING* NameOf(const OBJECT_ATTRIBUTES* oa) {
	__try { return oa ? oa->ObjectName : nullptr; } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// kind 'f' file open (extra = NTSTATUS), 'L' DLL loaded, 'U' DLL unloaded (extra = image size)
void Note(char kind, const UNICODE_STRING* name, long long t0, long long t1, uint32_t extra) {
	const uint32_t v = g_evNext.fetch_add(1, std::memory_order_relaxed);
	Ev& e = g_ev[v % kEvRing];
	e.seq.store(0, std::memory_order_release);   // being written
	std::atomic_thread_fence(std::memory_order_release);
	e.t0 = t0;
	e.us = (uint32_t)((t1 - t0) * 1000000 / g_freq);
	e.tid = GetCurrentThreadId();
	e.extra = extra;
	e.kind = kind;
	CopyName(name, e.path, kEvPath);
	e.seq.store(v + 1, std::memory_order_release);
}

using NtCreateFileFn = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG,
                                        ULONG, ULONG, ULONG, PVOID, ULONG);
using NtOpenFileFn = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, ULONG, ULONG);
NtCreateFileFn g_origCreate = nullptr;
NtOpenFileFn g_origOpen = nullptr;

NTSTATUS NTAPI HkCreate(PHANDLE h, ACCESS_MASK a, POBJECT_ATTRIBUTES oa, PIO_STATUS_BLOCK io, PLARGE_INTEGER size,
                        ULONG attr, ULONG share, ULONG disp, ULONG opt, PVOID ea, ULONG eaLen) {
	if (!g_evOn.load(std::memory_order_relaxed)) return g_origCreate(h, a, oa, io, size, attr, share, disp, opt, ea, eaLen);
	const long long t0 = Now();
	const NTSTATUS s = g_origCreate(h, a, oa, io, size, attr, share, disp, opt, ea, eaLen);
	Note('f', NameOf(oa), t0, Now(), (uint32_t)s);
	return s;
}
NTSTATUS NTAPI HkOpen(PHANDLE h, ACCESS_MASK a, POBJECT_ATTRIBUTES oa, PIO_STATUS_BLOCK io, ULONG share, ULONG opt) {
	if (!g_evOn.load(std::memory_order_relaxed)) return g_origOpen(h, a, oa, io, share, opt);
	const long long t0 = Now();
	const NTSTATUS s = g_origOpen(h, a, oa, io, share, opt);
	Note('f', NameOf(oa), t0, Now(), (uint32_t)s);
	return s;
}

struct DllNoteData { ULONG flags; const UNICODE_STRING* full; const UNICODE_STRING* base; PVOID dllBase; ULONG size; };
VOID CALLBACK OnDll(ULONG reason, const void* data, PVOID) {
	if (!g_evOn.load(std::memory_order_relaxed) || !data) return;
	const DllNoteData* d = (const DllNoteData*)data;
	const long long t = Now();
	Note(reason == 1 ? 'L' : 'U', d->full, t, t, d->size);
}

bool InstallFileWatch() {
	HMODULE nt = GetModuleHandleA("ntdll.dll");
	void* c = nt ? (void*)GetProcAddress(nt, "NtCreateFile") : nullptr;
	void* o = nt ? (void*)GetProcAddress(nt, "NtOpenFile") : nullptr;
	using RegFn = NTSTATUS(NTAPI*)(ULONG, PVOID, PVOID, PVOID*);
	RegFn reg = nt ? (RegFn)GetProcAddress(nt, "LdrRegisterDllNotification") : nullptr;
	const bool hc = c && MH_CreateHook(c, (void*)&HkCreate, (void**)&g_origCreate) == MH_OK && MH_EnableHook(c) == MH_OK;
	const bool ho = o && MH_CreateHook(o, (void*)&HkOpen, (void**)&g_origOpen) == MH_OK && MH_EnableHook(o) == MH_OK;
	static PVOID cookie = nullptr;
	const bool hd = reg && reg(0, (PVOID)&OnDll, nullptr, &cookie) >= 0;
	if (!hc || !ho || !hd)
		Log("hitch: file watch partly off (NtCreateFile %s, NtOpenFile %s, DLL notifications %s)", hc ? "ok" : "FAILED",
		    ho ? "ok" : "FAILED", hd ? "ok" : "FAILED");
	return hc || ho || hd;
}

struct EvCopy { long long t0; uint32_t us, tid, extra; char kind; char path[kEvPath]; };

std::vector<EvCopy> EventsIn(long long a, long long b) {
	std::vector<EvCopy> out;
	const uint32_t n = g_evNext.load(std::memory_order_acquire);
	for (uint32_t v = n > kEvRing ? n - kEvRing : 0; v < n; v++) {
		Ev& e = g_ev[v % kEvRing];
		if (e.seq.load(std::memory_order_acquire) != v + 1) continue;
		EvCopy c;
		c.t0 = e.t0; c.us = e.us; c.tid = e.tid; c.extra = e.extra; c.kind = e.kind;
		memcpy(c.path, e.path, kEvPath);
		c.path[kEvPath - 1] = 0;
		std::atomic_thread_fence(std::memory_order_acquire);
		if (e.seq.load(std::memory_order_acquire) != v + 1) continue;   // rewritten while copied
		if (c.t0 >= a && c.t0 <= b) out.push_back(c);
	}
	return out;
}

std::map<DWORD, std::string> g_tidNames;   // watcher thread only
std::string ThreadName(DWORD tid) {
	if (!g_targets.empty() && tid == g_targets[0].tid) return "Main";
	auto it = g_tidNames.find(tid);
	if (it != g_tidNames.end()) return it->second;
	std::string nm;
	using DescFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
	static DescFn desc = (DescFn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadDescription");
	if (HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid)) {
		PWSTR w = nullptr;
		if (desc && SUCCEEDED(desc(h, &w)) && w) {
			char a[128] = {};
			WideCharToMultiByte(CP_UTF8, 0, w, -1, a, sizeof a - 1, nullptr, nullptr);
			nm = a;
			LocalFree(w);
		}
		CloseHandle(h);
	}
	if (nm.empty()) nm = "tid " + std::to_string(tid);
	if (g_tidNames.size() < 4096) g_tidNames[tid] = nm;
	return nm;
}

uint32_t Faults() {
	PROCESS_MEMORY_COUNTERS pmc{ sizeof pmc };
	return K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc) ? pmc.PageFaultCount : 0;
}

// System-wide page counters (SYSTEM_PERFORMANCE_INFORMATION, NtQuerySystemInformation class 2): which kind of fault a
// long frame's page faults were. Pages read from disk = hard faults (paging in what Windows had moved out); transition =
// soft (back from the standby / modified lists); demand-zero = fresh memory. Whole system, but a load dominates it.
struct SysPf { bool ok = false; uint32_t avail = 0, trans = 0, dz = 0, reads = 0, readIos = 0, dirty = 0; };
SysPf SysFaults() {
	using Fn = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
	static const Fn f = (Fn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation");
	uint64_t buf[128] = {};   // 1 KB, 8-byte aligned (the struct is ~0x158 bytes)
	const unsigned char* b = (const unsigned char*)buf;
	ULONG len = 0;
	SysPf r;
	if (!f) return r;
	LONG st = f(2, buf, sizeof buf, &len);
	if (st < 0 && len >= 100 && len < sizeof buf) st = f(2, buf, len, &len);   // a class that wants its exact size
	if (st < 0 || len < 100) return r;
	r.ok = true;
	r.avail = *(uint32_t*)(b + 44);   // AvailablePages (checks the layout against free RAM in the log line)
	r.trans = *(uint32_t*)(b + 68);   // TransitionCount
	r.dz = *(uint32_t*)(b + 76);      // DemandZeroCount
	r.reads = *(uint32_t*)(b + 80);   // PageReadCount
	r.readIos = *(uint32_t*)(b + 84); // PageReadIoCount
	r.dirty = *(uint32_t*)(b + 96);   // DirtyPagesWriteCount (written out to the page file)
	return r;
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
void FindTargets(bool window, bool appendOnly = false) {
	std::vector<std::string> names;
	{
		std::lock_guard<std::mutex> l(g_namesMu);
		names = g_names;
		g_namesChanged = false;
	}
	if (window) for (const char* n : kWindowNames) if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
	{   // the same threads as now: nothing to do
		std::vector<std::string> have;
		for (size_t i = 1; i < g_targets.size(); i++) have.push_back(g_targets[i].name);
		std::vector<std::string> want = names;
		std::sort(have.begin(), have.end());
		std::sort(want.begin(), want.end());
		if (have == want) return;
	}
	if (!appendOnly) {   // appendOnly: keep the threads found so far (their samples' index stays the same)
		for (size_t i = 1; i < g_targets.size(); i++) CloseHandle(g_targets[i].h);
		g_targets.resize(1);
	}
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

std::string g_filesCsvPath;
size_t g_filesCsvBytes = 0;

// The files opened and DLLs loaded during the long frame [a, b] (QPC): one log line, every event in the files csv.
void ReportFiles(int no, long long a, long long b) {
	if (!g_evOn) return;
	const std::vector<EvCopy> ev = EventsIn(a, b);
	if (ev.empty()) { Log("hitch: #%d files: none opened, no DLL loaded", no); return; }
	int opens = 0, failed = 0;
	double openMs = 0;
	std::string dlls;
	std::map<std::string, std::pair<int, double>> byFile;   // "thread file" -> count, ms
	for (const EvCopy& e : ev) {
		const std::string who = ThreadName(e.tid);
		if (e.kind == 'f') {
			opens++;
			openMs += e.us / 1000.0;
			if ((int32_t)e.extra < 0) failed++;
			const char* slash = strrchr(e.path, '\\');
			auto& f = byFile[who + " " + (slash ? slash + 1 : e.path)];
			f.first++;
			f.second += e.us / 1000.0;
		} else if (dlls.size() < 900) {
			const char* slash = strrchr(e.path, '\\');
			char one[256];
			char size[24];
			if (e.extra >= 1048576) snprintf(size, sizeof size, "%.1f MB", e.extra / 1048576.0);
			else snprintf(size, sizeof size, "%u KB", e.extra / 1024);
			snprintf(one, sizeof one, "%s%s %s %s at %.0f ms (%s)", dlls.empty() ? "" : ", ", e.kind == 'L' ? "loaded" : "unloaded",
			         slash ? slash + 1 : e.path, size, (e.t0 - a) * 1000.0 / (double)g_freq, who.c_str());
			dlls += one;
		}
	}
	std::vector<std::pair<double, std::string>> top;
	for (auto& f : byFile) {
		char one[256];
		snprintf(one, sizeof one, "%s x%d %.1f ms", f.first.c_str(), f.second.first, f.second.second);
		top.push_back({ f.second.second, one });
	}
	std::sort(top.begin(), top.end(), [](auto& x, auto& y) { return x.first > y.first; });
	std::string most;
	for (size_t i = 0; i < top.size() && i < 10; i++) most += (i ? ", " : "") + top[i].second;
	Log("hitch: #%d files: %d opens (%d failed, %.0f ms inside the opens, %zu files); DLLs: %s; slowest: %s", no, opens, failed,
	    openMs, byFile.size(), dlls.empty() ? "-" : dlls.c_str(), most.empty() ? "-" : most.c_str());
	if (g_filesCsvPath.empty() || g_filesCsvBytes > kMaxCsvBytes) return;
	FILE* f = fopen(g_filesCsvPath.c_str(), "a");
	if (!f) return;
	for (const EvCopy& e : ev) {
		const int w = fprintf(f, "%d,%s,%.1f,%c,%u,%08x,%s\n", no, ThreadName(e.tid).c_str(), (e.t0 - a) * 1000.0 / (double)g_freq,
		                      e.kind, e.us, e.extra, e.path);
		if (w > 0) g_filesCsvBytes += (size_t)w;
	}
	fclose(f);
}

uint64_t CpuUs(HANDLE h) {   // the thread's user + kernel time so far, in microseconds
	FILETIME c, e, k, u;
	if (!GetThreadTimes(h, &c, &e, &k, &u)) return 0;
	return ((((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime) + (((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime)) / 10;
}

void Report(int no, double frameMs, const std::vector<Sample>& ss, uint32_t faults0, const SysPf& sys0,
            const std::vector<uint64_t>* cpu0 = nullptr) {
	if (ss.empty()) return;
	const uint32_t df = ss.back().faults - faults0;
	const SysPf sys1 = SysFaults();
	if (sys0.ok && sys1.ok) {
		MEMORYSTATUSEX m{ sizeof m };
		GlobalMemoryStatusEx(&m);
		Log("hitch: #%d system pages: read from disk %u (%.0f MB in %u reads), back from standby %u, fresh %u, written to the "
		    "page file %u; free %u MB (Windows says %llu)", no, sys1.reads - sys0.reads, (sys1.reads - sys0.reads) / 256.0,
		    sys1.readIos - sys0.readIos, sys1.trans - sys0.trans, sys1.dz - sys0.dz, sys1.dirty - sys0.dirty, sys1.avail / 256,
		    m.ullAvailPhys >> 20);
	}
	for (size_t who = 0; who < g_targets.size(); who++) {
		std::map<std::string, int> in, from;
		int n = 0;
		for (const Sample& s : ss) {
			if (s.who != who) continue;
			n++;
			in[Where(s.rip)]++;
			if (s.n > 0) from[Where(s.ret[0])]++;
		}
		char cpu[48] = "";
		if (cpu0 && who < cpu0->size() && frameMs > 0)
			snprintf(cpu, sizeof cpu, ", CPU %.0f %%", (CpuUs(g_targets[who].h) - (*cpu0)[who]) / (frameMs * 10.0));
		if (who == 0)
			Log("hitch: #%d %s %.0f ms, %d samples%s, page faults +%u (%.0f a second); in: %s; called from: %s", no,
			    cpu0 ? "settle window: Main," : "main frame", frameMs, n, cpu, df, frameMs > 0 ? df * 1000.0 / frameMs : 0.0,
			    Top(in, n, 6).c_str(), Top(from, n, 6).c_str());
		else if (n)
			Log("hitch: #%d %s: %d samples%s; in: %s; called from: %s", no, g_targets[who].name.c_str(), n, cpu,
			    Top(in, n, 6).c_str(), Top(from, n, 6).c_str());
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

// The process's first-created thread (the game's main thread) and its stack base (TEB +8), or nullptr.
HANDLE FirstThread(uintptr_t* stackBase) {
	using NtQitFn = LONG(NTAPI*)(HANDLE, int, PVOID, ULONG, PULONG);
	struct Tbi { LONG exitStatus; PVOID teb; CLIENT_ID cid; ULONG_PTR affinity; LONG prio, basePrio; };
	const auto qit = (NtQitFn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (!qit || snap == INVALID_HANDLE_VALUE) return nullptr;
	const DWORD pid = GetCurrentProcessId();
	HANDLE best = nullptr;
	unsigned long long bestT = ~0ull;
	THREADENTRY32 te{ sizeof te };
	for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
		if (te.th32OwnerProcessID != pid) continue;
		HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
		if (!h) continue;
		FILETIME cr, ex, k, u;
		const unsigned long long t = GetThreadTimes(h, &cr, &ex, &k, &u) ? ((unsigned long long)cr.dwHighDateTime << 32) | cr.dwLowDateTime : ~0ull;
		if (t < bestT) { if (best) CloseHandle(best); best = h; bestT = t; } else CloseHandle(h);
	}
	CloseHandle(snap);
	Tbi tbi{};
	if (!best || qit(best, 0, &tbi, sizeof tbi, nullptr) < 0 || !tbi.teb) { if (best) CloseHandle(best); return nullptr; }
	*stackBase = *(uintptr_t*)((uint8_t*)tbi.teb + 8);   // NT_TIB StackBase
	return best;
}

DWORD WINAPI Watch(LPVOID) {
	std::vector<Sample> ss;
	ss.reserve(kMaxSamples);
	int no = g_startup ? -1 : 0;   // the start-up window is #0
	long long start = 0;
	uint32_t faults0 = 0;
	SysPf sys0;
	bool inWin = false;
	long long winStart = 0;
	std::vector<uint64_t> cpu0;
	for (;;) {
		Sleep(kIntervalMs);
		if (HANDLE fix = g_fixMain.exchange(nullptr)) {   // the start-up guess was not the main thread
			ss.clear();
			g_main = fix; g_mainStack = g_fixStack;
			g_targets.clear();
		}
		const long long last = g_lastFrame.load(std::memory_order_acquire);
		if (!g_main || !last) continue;
		if (g_targets.empty()) g_targets.push_back({ g_main, g_mainStack, "Main", GetThreadId(g_main) });
		const long long now = Now();
		const double gapMs = (now - last) * 1000.0 / (double)g_freq;
		const bool win = g_window.load(std::memory_order_acquire) && g_on;
		if (win != inWin) {
			if (!ss.empty()) {   // whatever was being sampled ends here
				const double ms = (now - (inWin ? winStart : start)) * 1000.0 / (double)g_freq;
				if (inWin) Log("hitch: #%d below is the settle window: a save-load gate holding the screen", no);
				Report(no, ms, ss, faults0, sys0, inWin ? &cpu0 : nullptr);
				ReportFiles(no, inWin ? winStart : start, now);
				ss.clear();
			}
			inWin = win;
			start = last;   // a long frame running now is not sampled again as a new one
			if (win) {
				winStart = now;
				no++;
				faults0 = Faults();
				sys0 = SysFaults();
				RefreshModules();
				FindTargets(true);
				cpu0.clear();
				for (const Target& t : g_targets) cpu0.push_back(CpuUs(t.h));
				std::string found;
				for (size_t i = 1; i < g_targets.size(); i++) found += (i > 1 ? ", " : "") + g_targets[i].name;
				Log("hitch: #%d settle window on (a save-load gate holds the screen): sampling Main%s%s", no, found.empty() ? "" : ", ",
				    found.c_str());
			} else FindTargets(false);
		}
		if (inWin) {
			const uint32_t tMs = (uint32_t)((now - winStart) * 1000 / g_freq);
			for (size_t who = 0; who < g_targets.size() && ss.size() < (size_t)kMaxSamples; who++) {
				Sample s;
				if (Grab(g_targets[who], (uint8_t)who, &s, tMs)) ss.push_back(s);
			}
			continue;
		}
		if (!ss.empty() && (last != start || !g_on)) {   // the long frame ended (or sampling was turned off)
			const double frameMs = (last != start ? last - start : now - start) * 1000.0 / (double)g_freq;
			if (no == 0 && g_startup) Log("hitch: #0 below is the start-up: plugin start -> the first frame tick (the main menu)");
			Report(no, frameMs, ss, faults0, sys0);
			ReportFiles(no, start, last != start ? last : now);
			ss.clear();
		}
		if (!g_on || gapMs < g_thresholdMs.load()) continue;
		if (ss.empty()) {
			if (last == start) continue;   // this long frame already reported (sampling was full)
			start = last;
			no++;
			faults0 = Faults();
			sys0 = SysFaults();
			RefreshModules();
			FindTargets(false);
		}
		const uint32_t tMs = (uint32_t)((now - start) * 1000 / g_freq);
		// the start-up window begins before the render threads exist: look for them again every second
		static unsigned ticks = 0;
		if (no == 0 && g_startup && ++ticks % 50 == 0) FindTargets(false, true);
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
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	if (FILE* sw = fopen((dir + "fastload_hitch.txt").c_str(), "r")) {   // "0": nothing at all (no thread, no hooks)
		int m = 1;
		const bool off = fscanf(sw, "%d", &m) == 1 && m == 0;
		fclose(sw);
		if (off) { g_on = false; Log("hitch: off (fastload_hitch.txt says 0: no sampler, no file watch)"); return; }
	}
	const std::string p = dir + "fastload_hitch.csv";
	if (FILE* fo = fopen(p.c_str(), "w")) {
		fprintf(fo, "hitch,thread,frame_ms,t_ms,faults,rip,stack\n");
		fclose(fo);
		g_csvPath = p;
	}
	const std::string pf = dir + "fastload_hitch_files.csv";
	if (FILE* fo = fopen(pf.c_str(), "w")) {
		fprintf(fo, "hitch,thread,t_ms,kind,us,status_or_size,path\n");
		fclose(fo);
		g_filesCsvPath = pf;
	}
	g_watchOk = InstallFileWatch();
	g_evOn = g_watchOk;
	uintptr_t sb = 0;
	if (HANDLE m = FirstThread(&sb)) {   // start-up window: sampled from now until the first frame tick
		g_main = m; g_mainStack = sb; g_startup = true;
		g_lastFrame.store(Now(), std::memory_order_release);
		Log("hitch: start-up window on: main thread guessed as %lu (the process's first), sampled until the first frame tick",
		    GetThreadId(m));
	}
	CloseHandle(CreateThread(nullptr, 0, Watch, nullptr, 0, nullptr));
	Log("hitch: on (main frames over %d ms sampled every %d ms from inside the game, with RenderThread and "
	    "RenderLoadingThread; files opened and DLLs loaded %s; samples in %s)", g_thresholdMs.load(), kIntervalMs,
	    g_evOn ? "listed per long frame" : "NOT watched", g_csvPath.empty() ? "- (csv not writable)" : g_csvPath.c_str());
}

void Hitch_Frame() {
	static bool checked = false;
	if (!g_main) {
		g_main = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
		g_mainStack = (uintptr_t)__readgsqword(0x08);   // TEB StackBase of this (the main) thread
		checked = true;
	} else if (!checked) {
		checked = true;
		if (GetThreadId(g_main) != GetCurrentThreadId()) {
			Log("hitch: the start-up window sampled thread %lu, but frames tick on %lu: its #0 is not the main thread",
			    GetThreadId(g_main), GetCurrentThreadId());
			g_fixStack = (uintptr_t)__readgsqword(0x08);
			g_fixMain = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
		}
	}
	g_lastFrame.store(Now(), std::memory_order_release);
}

void Hitch_Window(bool on) { g_window.store(on, std::memory_order_release); }

void Hitch_Command(const char* args) {
	while (args && *args == ' ') args++;
	if (args && !strcmp(args, "off")) { g_on = false; g_evOn = false; }
	else if (args && !strcmp(args, "on")) { g_on = true; g_evOn = g_watchOk; }
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
