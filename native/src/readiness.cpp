// Which readiness task holds a loading or black screen: see readiness.h.
// Addresses: WHGame.dll 1.5.6, checked at start against the installed DLL's bytes.
#include "readiness.h"
#include "readiness_early.h"
#include "log.h"
#include "hitch.h"
#include "mainreads.h"
#include "streamahead.h"
#include "morphpool.h"
#include "early.h"
#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "MinHook.h"

namespace {

constexpr uintptr_t kRvaGame = 0x5651d60;       // the C_Game instance; +0xa8 = the level manager
constexpr uintptr_t kRvaLevelMgrVt = 0x4099530, kRvaServiceVt = 0x3dbe8a0, kRvaProgressVt = 0x3a982b8;
constexpr uintptr_t kRvaLock = 0x3d902c, kRvaUnlock = 0x42dc54;   // the game's recursive mutex (as in streamio.cpp)
constexpr uintptr_t kRvaStart = 0xfc3ec0, kRvaIsReady = 0x8baee0;
const unsigned char kStartBytes[] = { 0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x10,0x49,0x89,0x73,0x20,0x57,0x48,0x83,0xec,0x70 };
const unsigned char kIsReadyBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57 };
// FUN_180fc3ec0(observer, mask, callback): starts a readiness observer (registers it with the service, timeout
// wh_sys_ReadinessObserverTimeoutDuration, a 1-update countdown); 17 call sites, masks 0x3f93 (save load, level
// change), 0x3f91 (new game), 0x3b91, 0x3b97, 0x81. The service fires it once IsReady(mask): every task whose
// category has a bit in the mask says ready.
// level manager: +0x10 the state (0 idle .. 6 waiting for graphics and scripts, 7 ready, 8/9 video); the state
// machine FUN_180fc3868 switches on it, and its +0x91 is the "video ended" byte (levelmgr +0xa1)
// service: +0x38 its lock, +0x48 / +0x50 the task pointers (begin, end); task vtable +0x10 IsReady(mask),
// +0x18 category; a C_ReadinessProgress task: +0x10 name (char*), +0x1c requests in flight
// observer (wh::C_ReadinessObserver, 0x70 bytes; ~/re/work/fast_travel_arrival/report.md): +8 the service, +0x18 the
// mask, +0x20 the expiry (int64, timer units of 10 us; the -1 sentinel 0xfffffffffffe7960 = no timeout), +0x68 armed,
// +0x6c the update countdown. FUN_180fc3f48 arms it; the service fires it once the countdown is used up and either
// FUN_1808bae94 (expiry >= 0 and expiry <= now: the wh_sys_ReadinessObserverTimeoutDuration path) or IsReady(mask).

using LockFn = void(__fastcall*)(void*);
using CatFn = uint32_t(__fastcall*)(void*);
using ReadyFn = bool(__fastcall*)(void*, uint32_t);
using StartFn = uint64_t(__fastcall*)(void*, uint32_t, void*);

uintptr_t g_base = 0;
LockFn g_lock = nullptr, g_unlock = nullptr;
StartFn g_origStart = nullptr;
uint8_t* g_service = nullptr;
bool g_off = true;
int g_state = -100;
std::atomic<int> g_earlyMode{ 0 };   // "let me in early" (readiness_early.h): 0 off, 1 on; fastload_readiness.txt
// "readiness temp <MB>": the stream engine's temp memory budget (sys_streaming_memory_budget, Warhorse's system.cfg
// 20480 KB) raised to this while a save-load or fast-travel gate waits, then put back; 0 = the game's own (default).
// Kept as the second number in fastload_readiness.txt.
std::atomic<int> g_tempMB{ 0 };
int g_tempOrig = -1;   // KB, the value we raised from (-1: not raised)
int g_tempSet = -1;    // KB, the value we wrote (restore only while it is still there)
// "readiness spawn <N>": es_PerInstanceStreamingClassSpawnBudget (how many entities the entity streaming manager spawns
// a frame per streamable class, 2; 4 while a fader is up: FUN_1809b6b20) raised to this while a save-load or
// fast-travel gate waits, then put back; 0 = the game's own (default). Kept as the fourth number in fastload_readiness.txt.
std::atomic<int> g_spawnN{ 0 };
int g_spawnOrig = -1, g_spawnSet = -1;
std::string g_modeFile;
early::Cfg g_earlyCfg;   // its hold: fastload_readiness.txt's third number (s, 0.05-1; 0.3 by default)
long long g_freq = 1, g_lastTick = 0;

struct Task {
	void* at;
	uintptr_t vt;
	uint32_t cat = 0;
	std::string name;
};
std::vector<Task> g_tasks;
uintptr_t g_vecBegin = 0, g_vecEnd = 0;

// The stream engine's HDD reader thread, looked at once a frame while a gate waits (see Reader below).
struct ReaderAcc {
	int n = 0;              // frames looked at
	int parkedBusy = 0;     // reader asleep with requests queued: held by temp memory or a pause, not by an empty queue
	int parkedIdle = 0;     // reader asleep, queue empty: waiting for consumers to ask
	int tempFull = 0;       // temp memory over its budget (a request that needs some can't start)
	int paused = 0;         // a stream type paused (pause mask not 0)
	long long queued = 0;   // sum of the queue length
	int maxQueued = 0;
	uint32_t mask = 0;      // the last pause mask seen (not 0)
};

// the AI frame job's counters (see HkAiWait below)
struct AiAcc {
	long long launches = 0, waits = 0, waited = 0, waitTicks = 0, maxWait = 0, spanTicks = 0;
};
// the AI frame job's phase times at one moment (see AiPhases below)
struct PhaseSnap { std::vector<double> sum; std::vector<long long> n; };

struct Gate {
	uint32_t mask;
	uintptr_t from;           // the call site's RVA
	long long t0 = 0, lastLine = 0;
	std::vector<double> notReady;      // seconds per task (index into g_tasks, by task pointer below)
	std::vector<void*> taskAt;
	std::vector<long long> lastBusy;
	std::vector<double> lowSince;      // early::Straggler: age it went down to its last requests (-1: not)
	std::vector<char> straggled;       // it was counted as ready that way at least once
	void* obs = nullptr;           // the observer (C_ReadinessObserver)
	bool eligible = false;         // a gate "let me in early" may open (save load 0x3f93, fast travel 0x3f91)
	early::State early;
	AiAcc ai0;                     // the AI frame jobs' counters when the gate was first seen
	PhaseSnap ph0;                 // and its phases' times
	double letAt = -1;             // s: let in early at this age (-1: not)
	int frames = 0;                // frames seen while it waited
	double longest = 0;            // s: its longest frame
	int lineFrames = 0;            // the same, since the last "waiting" line
	double lineLongest = 0;
	ReaderAcc rd, lineRd;          // the reader over the whole gate, and since the last "waiting" line
};
std::mutex g_newMu;
std::vector<Gate> g_new;    // started since the last frame (any thread)
std::vector<Gate> g_gates;  // being watched (main thread)

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Secs(long long t) { return (double)t / (double)g_freq; }

// The AI module's frame job (C_XGenAIModule, decoded 2026-10-26, log only): its method FUN_180ec4660 (secondary vtable
// slot 6, this = module+0x10) starts ONE job, "XGenUpdateJob" (the whole XGen AI update; its state at module+0x148),
// and its Update (slot 1, FUN_180fa08c0) ends in FUN_180fa0950, which waits for that job. Boot 171A3's settle: Main
// was in that wait in 54 of 139 samples (39 %, ~1.1 s of 2.9 s). Counted on the main thread only; per gate and for the
// first 10 s of play after a save-load gate, in the "open" line and a "readiness: AI update in play" line.
constexpr uintptr_t kRvaAiLaunch = 0xec4660, kRvaAiWait = 0xfa0950;
const unsigned char kAiLaunchBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x60,0x48,0x8d,0x79,0xf0,0x48,0x8b };
const unsigned char kAiWaitBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x4c,0x8b,0x0d,0xcb,0xcf,0x98,0x03,0x48,0x8d,0x99 };
using AiLaunchFn = void(__fastcall*)(void*);
using AiWaitFn = uint32_t(__fastcall*)(void*);
AiLaunchFn g_aiLaunchOrig = nullptr;
AiWaitFn g_aiWaitOrig = nullptr;
AiAcc g_ai;                  // Main only
long long g_aiLaunchT = 0;   // Main: the last job start
DWORD g_mainTid = 0;
std::atomic<long long> g_aiOther{ 0 };   // calls from other threads (not counted)
AiAcc g_playAi;              // at the last save-load gate's opening
long long g_playT0 = 0;      // when it opened (0: no play window open)

// The job's phases: FUN_1807e2c68 runs the module's phase list (vector at module+0xf8, 0x48-byte entries: +0x38 the
// phase's std::function, +0x40 its flag byte, run when it has a bit of module+0x130) one after another, and notes each
// phase's start (QPC) and length (ms, float) in the timing object *DAT_185493918 (FUN_180fc7020): array *obj, phase i at
// +8 + i*0x18 (start) and +0x10 + i*0x18 (ms). Read on Main after the job is done; a phase counts when its start moved.
constexpr uintptr_t kRvaAiStats = 0x5493918;
constexpr int kMaxPhases = 48;
struct AiPhase { long long lastStart = 0, n = 0; double sumMs = 0, maxMs = 0; uintptr_t fn = 0; int flags = -1; std::string name; };
AiPhase g_phase[kMaxPhases];
int g_nPhase = 0;
uintptr_t g_phaseVecB = 0, g_phaseVecE = 0;
PhaseSnap Snap() {
	PhaseSnap p;
	for (int i = 0; i < g_nPhase; i++) { p.sum.push_back(g_phase[i].sumMs); p.n.push_back(g_phase[i].n); }
	return p;
}
PhaseSnap g_playPh;          // the phases at the last save-load gate's opening
bool Rd(uintptr_t a, void* out, size_t n);   // below
std::string Text(uintptr_t p);
void AiPhases(uintptr_t module) {
	uintptr_t b = 0, e = 0;
	if (!Rd(module + 0xf8, &b, 8) || !Rd(module + 0x100, &e, 8) || e < b || (e - b) % 0x48) return;
	if (b != g_phaseVecB || e != g_phaseVecE) {   // the phase list (once, or again when it changed)
		g_phaseVecB = b;
		g_phaseVecE = e;
		g_nPhase = (int)(std::min)((e - b) / 0x48, (uintptr_t)kMaxPhases);
		for (int i = 0; i < g_nPhase; i++) {
			AiPhase& ph = g_phase[i];
			const uintptr_t en = b + (uintptr_t)i * 0x48;
			uintptr_t impl = 0, vt = 0, fn = 0;
			uint8_t fl = 0;
			ph.fn = Rd(en + 0x38, &impl, 8) && impl && Rd(impl, &vt, 8) && vt && Rd(vt + 0x10, &fn, 8) && fn > g_base ? fn - g_base : 0;
			ph.flags = Rd(en + 0x40, &fl, 1) ? fl : -1;
			ph.name.clear();
			for (int q = 0; q < 7 && ph.name.empty(); q++) {   // a name string among its first fields, if any
				uintptr_t v = 0;
				if (Rd(en + q * 8, &v, 8) && v > 0x10000) { std::string t = Text(v); if (t.size() >= 3) ph.name = t; }
			}
		}
	}
	uintptr_t obj = 0, arr = 0;
	if (!Rd(g_base + kRvaAiStats, &obj, 8) || !obj || !Rd(obj, &arr, 8) || !arr) return;
	for (int i = 0; i < g_nPhase; i++) {
		long long st = 0;
		float ms = 0;
		if (!Rd(arr + 8 + (uintptr_t)i * 0x18, &st, 8) || !Rd(arr + 0x10 + (uintptr_t)i * 0x18, &ms, 4)) return;
		AiPhase& ph = g_phase[i];
		if (!st || st == ph.lastStart) continue;
		ph.lastStart = st;
		if (ms < 0 || ms > 60000) continue;
		ph.n++;
		ph.sumMs += ms;
		if (ms > ph.maxMs) ph.maxMs = ms;
	}
}

// the phases since a snapshot, the costliest first: "phase 3 (+RVA "name" flags 0x..) 812 ms in 220 runs, avg 3.7, max 12"
std::string PhaseLine(const PhaseSnap& a) {
	std::vector<int> ix;
	for (int i = 0; i < g_nPhase; i++) ix.push_back(i);
	auto d = [&](int i) { return g_phase[i].sumMs - (i < (int)a.sum.size() ? a.sum[i] : 0); };
	auto dn = [&](int i) { return g_phase[i].n - (i < (int)a.n.size() ? a.n[i] : 0); };
	std::sort(ix.begin(), ix.end(), [&](int x, int y) { return d(x) > d(y); });
	std::string out;
	double total = 0;
	for (int i : ix) total += d(i);
	char s[200];
	snprintf(s, sizeof s, "; its %d phases %.0f ms in all, the costliest: ", g_nPhase, total);
	out = s;
	int shown = 0;
	for (int i : ix) {
		if (shown == 10 || d(i) <= 0) break;
		const long long n = dn(i);
		snprintf(s, sizeof s, "%sphase %d (+%llx%s%s%s, flags 0x%x) %.0f ms in %lld runs, avg %.2f, max %.1f", shown ? ", " : "", i,
		         (unsigned long long)g_phase[i].fn, g_phase[i].name.empty() ? "" : " \"", g_phase[i].name.c_str(),
		         g_phase[i].name.empty() ? "" : "\"", g_phase[i].flags, d(i), n, n ? d(i) / n : 0.0, g_phase[i].maxMs);
		out += s;
		shown++;
	}
	return out;
}

void __fastcall HkAiLaunch(void* self) {
	if (GetCurrentThreadId() == g_mainTid) { g_aiLaunchT = Now(); g_ai.launches++; }
	else g_aiOther++;
	g_aiLaunchOrig(self);
}

uint32_t __fastcall HkAiWait(void* module) {
	const long long t0 = Now();
	const uint32_t r = g_aiWaitOrig(module);
	if (GetCurrentThreadId() != g_mainTid) { g_aiOther++; return r; }
	const long long t1 = Now(), d = t1 - t0;
	AiPhases((uintptr_t)module);   // the job is done here
	g_ai.waits++;
	g_ai.waitTicks += d;
	if (d > g_ai.maxWait) g_ai.maxWait = d;
	if (d * 5000 > g_freq && g_aiLaunchT && g_aiLaunchT < t0) {   // waited over 0.2 ms: the job ran from its start to now
		g_ai.waited++;
		g_ai.spanTicks += t1 - g_aiLaunchT;
	}
	return r;
}

// "N AI frame jobs, Main waited W ms (X % of T s), longest M ms; in K frames over 0.2 ms each job ran >= J ms"
std::string AiLine(const AiAcc& a, const AiAcc& b, double wallS) {
	const long long w = b.waitTicks - a.waitTicks, k = b.waited - a.waited;
	char s[260];
	snprintf(s, sizeof s, "%lld AI frame jobs (XGenUpdateJob), Main waited for them %.0f ms (%.0f %% of %.1f s), longest "
	         "%.1f ms (the session's), in %lld frames over 0.2 ms, where each job ran >= %.1f ms from its start",
	         b.waits - a.waits, Secs(w) * 1000, wallS > 0 ? Secs(w) * 100 / wallS : 0.0, wallS, Secs(b.maxWait) * 1000, k,
	         k ? Secs(b.spanTicks - a.spanTicks) * 1000 / k : 0.0);
	return s;
}

uint64_t __fastcall HkStart(void* observer, uint32_t mask, void* callback) {
	Gate g;
	g.mask = mask;
	g.from = (uintptr_t)_ReturnAddress() - g_base;
	g.t0 = Now();
	g.obs = observer;
	g.eligible = early::Eligible(mask);
	{
		std::lock_guard<std::mutex> l(g_newMu);
		if (g_new.size() < 16) g_new.push_back(g);
	}
	return g_origStart(observer, mask, callback);
}

// Guarded helpers, no C++ objects in them (MSVC forbids __try next to unwinding).
bool Rd(uintptr_t a, void* out, size_t n) {
	__try { memcpy(out, (const void*)a, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Cat(void* t, uint32_t* cat) {
	__try { *cat = ((CatFn)(*(void***)t)[0x18 / 8])(t); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Ready(void* t, uint32_t mask, bool* ready) {   // what the game's IsReady (FUN_1808baee0) asks each task
	__try { *ready = ((ReadyFn)(*(void***)t)[0x10 / 8])(t, mask); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Lets a waiting observer fire on its next update, through the game's own timeout path (as if
// wh_sys_ReadinessObserverTimeoutDuration ran out): the expiry becomes 0. Only when the observer still looks armed
// for this gate. 0 done, 1 not armed any more, 2 the layout doesn't match, 3 not readable.
int LetIn(void* o, uint32_t mask) {
	__try {
		uint8_t* p = (uint8_t*)o;
		if (p[0x68] != 1) return 1;
		if (*(uint32_t*)(p + 0x18) != mask) return 2;
		const long long e = *(long long*)(p + 0x20);
		if (e < 0 && e != (long long)0xfffffffffffe7960ull) return 2;
		*(volatile long long*)(p + 0x20) = 0;
		return 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return 3; }
}

// a printable C string of 1-47 characters at p, or ""
std::string Text(uintptr_t p) {
	char s[48] = {};
	int n = 0;
	for (; p && n < 47; n++) {
		char c;
		if (!Rd(p + n, &c, 1)) return "";
		if (!c) break;
		if (c < 32 || c > 126) return "";
		s[n] = c;
	}
	return std::string(s, (size_t)n);
}

// the class name in an object's type info (vtable[-1] -> complete object locator -> type descriptor), up to 160 chars
std::string ClassOf(uintptr_t vt) {
	uintptr_t col = 0;
	uint32_t td = 0;
	char raw[161] = {};
	if (!Rd(vt - 8, &col, 8) || !col || !Rd(col + 12, &td, 4) || !td || !Rd(g_base + td + 0x10, raw, sizeof raw - 1)) return "";
	for (char& c : raw) if (c && (c < 32 || c > 126)) c = '?';
	return raw;
}

std::string NameOf(void* t, uintptr_t vt) {
	uintptr_t p = 0;
	char s[48] = {};
	if (vt == g_base + kRvaProgressVt && Rd((uintptr_t)t + 0x10, &p, 8) && p) {
		const std::string n = Text(p);
		if (!n.empty()) return n;
	}
	// Other task classes: the class name from the game's type info (vtable[-1] -> complete object locator ->
	// type descriptor ".?AVC_ClothingSystem@entitymodule@wh@@" -> "ClothingSystem"). The plain C_ReadinessTask class
	// is used by several systems, so it keeps its vtable.
	uintptr_t col = 0;
	uint32_t td = 0;
	char raw[96] = {};
	if (Rd(vt - 8, &col, 8) && col && Rd(col + 12, &td, 4) && td && Rd(g_base + td + 0x10, raw, sizeof raw - 1)) {
		const char* n = raw;
		if (!strncmp(n, ".?AV", 4)) n += 4;
		if (!strncmp(n, "C_", 2)) n += 2;
		size_t len = 0;
		while (n[len] && n[len] != '@' && len < 40) len++;
		if (len > 0 && strncmp(n, "ReadinessTask", len)) return std::string(n, len);
		if (len > 0) {
			// the plain class (FUN_18162c058 = its IsReady: ready while its std::function at +0x58 is empty, else that
			// function's answer): its name string at +0x10 (a CryString, freed by its destructor FUN_1834dac1c) and the
			// function's class (a lambda's type names the code that made it), logged once with the task list
			uintptr_t nm = 0, fn = 0, fvt = 0;
			std::string name = Rd((uintptr_t)t + 0x10, &nm, 8) ? Text(nm) : "";
			std::string what = Rd((uintptr_t)t + 0x58, &fn, 8) && fn && Rd(fn, &fvt, 8) && fvt ? ClassOf(fvt) : "";
			snprintf(s, sizeof s, "ReadinessTask(vt %llx)", (unsigned long long)(vt - g_base));
			std::string out = name.empty() ? s : "ReadinessTask \"" + name + "\"";
			if (!what.empty()) out += " [" + what + "]";
			return out;
		}
	}
	snprintf(s, sizeof s, "task(vt %llx)", (unsigned long long)(vt - g_base));
	return s;
}

int InFlight(const Task& t) {
	int n = -1;
	if (t.vt == g_base + kRvaProgressVt) Rd((uintptr_t)t.at + 0x1c, &n, 4);
	return n;
}

uintptr_t LevelMgr() {
	uintptr_t lm = 0, vt = 0;
	if (!g_base) return 0;   // asked before Readiness_Install (applyprof's reporter starts earlier: boot 70's crash file)
	if (!Rd(g_base + kRvaGame + 0xa8, &lm, 8) || !lm || !Rd(lm, &vt, 8) || vt != g_base + kRvaLevelMgrVt) return 0;
	return lm;
}

int LevelState() {
	const uintptr_t lm = LevelMgr();
	int s = -1;
	if (!lm || !Rd(lm + 0x10, &s, 4) || s < 0 || s > 9) return -1;
	return s;
}

// The service, found by reading only: the level manager's "LevelChanger" observer (levelmgr +0x28) keeps the
// service at its +8 (FUN_180fc3ec0 reads the time through it: service vtable +0x30 = "movss xmm0,[rcx+0xc]").
// Checked in the running game 2026-10-23: levelmgr +0x30. Both layouts are tried, and a candidate counts only when
// its vtable is the service's.
// A field that can't be a heap or image pointer is skipped before it's read through: +0x10 is the level state (an int),
// and reading through "6" was a handled fault in fastload_crash.txt every boot (boots 69-75).
inline bool LooksPtr(uintptr_t p) { return p >= 0x10000 && p < 0x800000000000ull && (p & 7) == 0; }

uint8_t* FindService(int* where) {
	const uintptr_t lm = LevelMgr();
	if (!lm) return nullptr;
	const uintptr_t want = g_base + kRvaServiceVt;
	for (int off = 0x10; off <= 0x100; off += 8) {
		uintptr_t p = 0, vt = 0, q = 0;
		if (!Rd(lm + off, &p, 8) || !LooksPtr(p)) continue;
		if (Rd(p, &vt, 8) && vt == want) { *where = off; return (uint8_t*)p; }
		if (Rd(p + 8, &q, 8) && LooksPtr(q) && Rd(q, &vt, 8) && vt == want) { *where = 0x1000 + off; return (uint8_t*)q; }
	}
	return nullptr;
}

// The task list, re-read when the vector moves. Called with the service's lock held.
bool ReadTasks(bool* changed) {
	uintptr_t b = 0, e = 0;
	if (!Rd((uintptr_t)g_service + 0x48, &b, 8) || !Rd((uintptr_t)g_service + 0x50, &e, 8) || e < b || (e - b) / 8 > 512)
		return false;
	*changed = false;
	if (b == g_vecBegin && e == g_vecEnd) return true;
	std::vector<Task> fresh;
	for (uintptr_t p = b; p < e; p += 8) {
		void* t = nullptr;
		uintptr_t vt = 0;
		if (!Rd(p, &t, 8) || !t || !Rd((uintptr_t)t, &vt, 8)) return false;
		Task k{ t, vt, 0, NameOf(t, vt) };
		Cat(t, &k.cat);
		fresh.push_back(k);
	}
	*changed = fresh.size() != g_tasks.size();
	g_tasks.swap(fresh);
	g_vecBegin = b;
	g_vecEnd = e;
	return true;
}

size_t Slot(Gate& g, void* t) {
	for (size_t i = 0; i < g.taskAt.size(); i++) if (g.taskAt[i] == t) return i;
	g.taskAt.push_back(t);
	g.notReady.push_back(0);
	g.lastBusy.push_back(0);
	g.lowSince.push_back(-1);
	g.straggled.push_back(0);
	return g.taskAt.size() - 1;
}

std::string NameAt(void* t) {
	for (auto& k : g_tasks) if (k.at == t) return k.name;
	return "?";
}

// What feeds the stream reader, read from the game once a "waiting" line (WHGame 1.5.6 addresses, decoded 2026-10-24,
// ~/re/work/settle_feed/report.md on the ProLiant; raw numbers, not yet checked against the game's own debug views):
// the skin loads in progress (checked against wh_cc_MaxSkinLoadingInProgress, 40), the texture scheduler's outstanding
// requests and bytes (capped by r_TexturesStreamingMaxRequestedJobs 32 / ...MB 4), and the stream engine's temp memory.
constexpr uintptr_t kRvaSkinLoading = 0x5324180, kRvaTexOutBytes = 0x55940e0, kRvaTexOutJobs = 0x55940e8,
                    kRvaStreamEngine = 0x547dae8;
std::string Feed(int frames, double longest) {
	int32_t skins = -1, texBytes = -1, texJobs = -1;
	uint32_t pool[3] = {};
	uintptr_t eng = 0;
	Rd(g_base + kRvaSkinLoading, &skins, 4);
	Rd(g_base + kRvaTexOutBytes, &texBytes, 4);
	Rd(g_base + kRvaTexOutJobs, &texJobs, 4);
	const bool poolOk = Rd(g_base + kRvaStreamEngine, &eng, 8) && eng && Rd(eng + 0x48, pool, sizeof pool);
	char s[200];
	snprintf(s, sizeof s, " | %d frames (longest %.0f ms), skins loading %d, texture requests out %d (%.1f MB), stream temp ",
	         frames, longest * 1000, skins, texJobs, texBytes / 1048576.0);
	std::string out = s;
	if (poolOk) snprintf(s, sizeof s, "%.1f MB (peak %.1f, budget %.1f)", pool[0] / 1048576.0, pool[1] / 1048576.0,
	                     pool[2] / 1048576.0);
	else snprintf(s, sizeof s, "?");
	return out + s;
}

// sys_streaming_memory_budget's variable (KB): FUN_18046459c copies it into the pool budget (engine E+0x140, i.e.
// stream engine pointer +0x50) as `<< 10` before each request, so a write takes effect at the next request. Written
// only while it matches the budget the pool holds (checked each time), and put back when the gates are gone.
constexpr uintptr_t kRvaTempBudgetKB = 0x49291f8;
bool Wr(uintptr_t a, const void* in, size_t n) {
	__try { memcpy((void*)a, in, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void TempBudget(bool want) {
	const int mb = g_tempMB.load(std::memory_order_relaxed);
	int32_t kb = 0, pool = 0;
	uintptr_t eng = 0;
	if (!Rd(g_base + kRvaTempBudgetKB, &kb, 4) || !Rd(g_base + kRvaStreamEngine, &eng, 8) || !eng || !Rd(eng + 0x50, &pool, 4))
		return;
	if (want && mb > 0 && g_tempOrig < 0 && kb > 0 && kb < mb * 1024) {
		if ((int64_t)kb << 10 != pool) {   // the variable isn't the budget the pool uses: leave it alone, say so once
			static bool said = false;
			if (!said) Log("readiness: temp budget not raised (variable %d KB, pool budget %d KB: layout differs)", kb, pool >> 10);
			said = true;
			return;
		}
		const int32_t v = mb * 1024;
		if (Wr(g_base + kRvaTempBudgetKB, &v, 4)) {
			g_tempOrig = kb;
			g_tempSet = v;
			Log("readiness: stream temp budget %d -> %d MB while the screen waits", kb / 1024, mb);
		}
	} else if (!want && g_tempOrig >= 0) {
		// Compare with what we wrote, not with g_tempMB: "readiness temp N" during a gate changes g_tempMB, not the variable.
		if (kb == g_tempSet && Wr(g_base + kRvaTempBudgetKB, &g_tempOrig, 4))
			Log("readiness: stream temp budget back to %d MB", g_tempOrig / 1024);
		else
			Log("readiness: stream temp budget left at %d MB (changed by something else since we set %d MB)", kb / 1024, g_tempSet / 1024);
		g_tempOrig = g_tempSet = -1;
	}
}

// es_PerInstanceStreamingClassSpawnBudget's variable (int): FUN_1809b6b20 reads it each frame per streamable class
// (`mov r12d, [rip+...]` at +0x9b6b5d, checked at install), so a write takes effect at the next frame. With the stream
// callback thread freed (morphpool.h), the settle gate waits on EntityStreaming: boots 180B1-4, ~300 entities in flight
// at 1.0 s, ready at 1.3-1.4 s, while the spawns are 2 % of Main (the AI update ~60 %: the frames are long, the count
// per frame small). Same rules as the temp budget: raised only from the game's value, put back if still ours.
// Queue 181 (spawn 32 on the variable only): no change, because while a fader is up (the loading screen) the function
// uses the constant 4 instead: `mov ecx, 4` at +0x9b6b77, `cmovne r12d, ecx` at +0x9b6b7c. So the constant's byte
// (+0x9b6b78) is raised to N too while the gate waits and put back to 4 after (Main writes it, and Main is the only
// thread that runs FUN_1809b6b20). The 4 streamable classes (FUN_180cb31cc: small / environment / reflex lights,
// particles, caps 256 / 64 / 64 / 256) and the despawns share the budget; a full class still stops at its cap.
constexpr uintptr_t kRvaSpawnBudget = 0x492f7a4, kRvaSpawnRead = 0x9b6b5d, kRvaFaderImm = 0x9b6b78;
const unsigned char kSpawnReadBytes[] = { 0x44,0x8b,0x25,0x40,0x8c,0xf7,0x03 };
const unsigned char kFaderBytes[] = { 0xb9,0x04,0x00,0x00,0x00,0x44,0x0f,0x45,0xe1 };   // at +0x9b6b77
bool g_spawnOk = false, g_faderOk = false;
int g_faderSet = -1;   // the constant we wrote (-1: the game's 4 is there)
bool WrCode(uintptr_t a, uint8_t v) {
	DWORD old = 0;
	if (!VirtualProtect((void*)a, 1, PAGE_EXECUTE_READWRITE, &old)) return false;
	*(volatile uint8_t*)a = v;
	VirtualProtect((void*)a, 1, old, &old);
	FlushInstructionCache(GetCurrentProcess(), (void*)a, 1);
	return true;
}
void SpawnBudget(bool want) {
	if (!g_spawnOk) return;
	const int n = g_spawnN.load(std::memory_order_relaxed);
	int32_t v = 0;
	if (!Rd(g_base + kRvaSpawnBudget, &v, 4)) return;
	if (want && n > 0 && g_spawnOrig < 0 && v > 0 && v < n) {
		const int32_t w = n;
		if (Wr(g_base + kRvaSpawnBudget, &w, 4)) {
			g_spawnOrig = v;
			g_spawnSet = w;
			Log("readiness: entity spawns per class a frame %d -> %d while the screen waits", v, w);
		}
	} else if (!want && g_spawnOrig >= 0) {
		if (v == g_spawnSet && Wr(g_base + kRvaSpawnBudget, &g_spawnOrig, 4))
			Log("readiness: entity spawns per class a frame back to %d", g_spawnOrig);
		else
			Log("readiness: entity spawns per class a frame left at %d (changed by something else since we set %d)", v, g_spawnSet);
		g_spawnOrig = g_spawnSet = -1;
	}
	// the constant used while a fader is up
	if (!g_faderOk) return;
	const uintptr_t imm = g_base + kRvaFaderImm;
	const uint8_t cur = *(const volatile uint8_t*)imm;
	if (want && n > 4 && g_faderSet < 0 && cur == 4) {
		if (WrCode(imm, (uint8_t)n)) {
			g_faderSet = n;
			Log("readiness: entity spawns per class a frame under the loading screen's fader 4 -> %d", n);
		}
	} else if (!want && g_faderSet >= 0) {
		if (cur == g_faderSet && WrCode(imm, 4)) Log("readiness: entity spawns under a fader back to 4");
		else Log("readiness: entity spawns under a fader left at %d (changed by something else since we set %d)", cur, g_faderSet);
		g_faderSet = -1;
	}
}

// The HDD reader (the one thread that reads every game pak), from ~/re/work/settle_feed/report.md section 4 (layout
// INFERRED, not yet checked against the game's debug views): engine E = *(base+0x547dae8) - 0xf0; reader T = *(E+0x78):
// sorted queue T+0x70/+0x78 and incoming T+0xa0/+0xa8 (vectors of pointers), T+0xf8 the count of threads parked in
// its wait (FUN_180465878); pause mask E+0xe8; temp pool E+0x138 (used fallback bytes, peak, budget). The reader
// allocates a request's temp memory from a 12 MB heap first, then from the fallback while used + size <= budget; when
// both are full it parks until a free, with the queue still full (report item 4).
struct ReaderNow { int queued; uint32_t parked, mask; int32_t used, budget; };
bool ReadReader(ReaderNow* r) {
	uintptr_t eng = 0, t = 0, q[2] = {}, in[2] = {};
	int32_t pool[3] = {};
	if (!Rd(g_base + kRvaStreamEngine, &eng, 8) || !eng) return false;
	const uintptr_t e = eng - 0xf0;
	if (!Rd(e + 0x78, &t, 8) || !t || !Rd(t + 0x70, q, 16) || !Rd(t + 0xa0, in, 16) || !Rd(t + 0xf8, &r->parked, 4) ||
	    !Rd(e + 0xe8, &r->mask, 4) || !Rd(e + 0x138, pool, 12))
		return false;
	if (q[1] < q[0] || in[1] < in[0] || (q[1] - q[0]) / 8 > 1000000 || (in[1] - in[0]) / 8 > 1000000) return false;
	r->queued = (int)((q[1] - q[0]) / 8 + (in[1] - in[0]) / 8);
	r->used = pool[0];
	r->budget = pool[2];
	return true;
}

void Add(ReaderAcc& a, const ReaderNow& r) {
	a.n++;
	a.queued += r.queued;
	if (r.queued > a.maxQueued) a.maxQueued = r.queued;
	// the wait's counter (FUN_180465878): waiters in the low 31 bits, the top bit set once it is signalled
	const bool asleep = (r.parked & 0x7fffffffu) != 0 && !(r.parked & 0x80000000u);
	if (asleep && r.queued > 0) a.parkedBusy++;
	if (asleep && r.queued == 0) a.parkedIdle++;
	if (r.used > 0 && r.used + (1 << 20) > r.budget) a.tempFull++;   // under 1 MB of the fallback left
	if (r.mask) { a.paused++; a.mask = r.mask; }
}

std::string Reader(const ReaderAcc& a) {
	if (!a.n) return " | reader: ?";
	char s[220];
	const auto pc = [&](int k) { return k * 100 / a.n; };
	snprintf(s, sizeof s, " | reader: queue %lld avg (max %d), asleep with work queued %d %%, asleep with nothing queued %d %%, "
	         "temp memory full %d %%", a.queued / a.n, a.maxQueued, pc(a.parkedBusy), pc(a.parkedIdle), pc(a.tempFull));
	std::string out = s;
	if (a.paused) {
		snprintf(s, sizeof s, ", paused %d %% (mask 0x%x)", pc(a.paused), a.mask);
		out += s;
	}
	return out;
}

void Opened(const Gate& g, long long now, const char* how, std::vector<std::string>* out) {
	const double took = Secs(now - g.t0);
	if (took < 0.25) return;   // quick gates (most in-game checks) stay out of the log
	std::vector<size_t> ix;
	for (size_t i = 0; i < g.taskAt.size(); i++) if (g.notReady[i] > 0) ix.push_back(i);
	std::sort(ix.begin(), ix.end(), [&](size_t a, size_t b) { return g.notReady[a] > g.notReady[b]; });
	std::string s;
	long long lastT = 0;
	std::string last = "-";
	for (size_t i : ix) {
		char one[120];
		snprintf(one, sizeof one, "%s%s %.1f s", s.empty() ? "" : ", ", NameAt(g.taskAt[i]).c_str(), g.notReady[i]);
		s += one;
		if (g.lastBusy[i] > lastT) { lastT = g.lastBusy[i]; last = NameAt(g.taskAt[i]); }
	}
	char head[200];
	snprintf(head, sizeof head, "readiness: gate 0x%x (from +%llx) %s after %.1f s (%d frames, longest %.0f ms); not ready for: ",
	         g.mask, (unsigned long long)g.from, how, took, g.frames, g.longest * 1000);
	std::string tail;
	if (g.letAt >= 0) {
		char e[96];
		snprintf(e, sizeof e, "; let in early at %.1f s (readiness early 1)", g.letAt);
		tail = e;
	} else if (g.eligible && g.early.wouldOpen >= 0) {
		char e[120];
		snprintf(e, sizeof e, "; \"readiness early 1\" would let you in at %.1f s (%.1f s sooner)", g.early.wouldOpen,
		         took - g.early.wouldOpen);
		tail = e;
	}
	out->push_back(head + (s.empty() ? std::string("nothing (every task ready)") : s) + "; ready last: " + last + tail +
	               Reader(g.rd) + " | " + AiLine(g.ai0, g_ai, took) + PhaseLine(g.ph0));
	if (early::Eligible(g.mask)) { g_playAi = g_ai; g_playPh = Snap(); g_playT0 = now; }
}

}  // namespace

int Readiness_LevelState() { return LevelState(); }

// The level state's changes, from fastload's watcher thread every 10 ms: the frame tick (CSystem::Update) doesn't run
// during state 4, so Readiness_Frame never saw it (boot 46); this sees every state, frames or not.
void Readiness_WatchState() {
	if (g_off || !g_base) return;
	const int s = LevelState();
	if (s != g_state) {
		if (g_state == -100) Log("readiness: level state %d at the first look (-1 = no level manager yet)", s);
		else Log("readiness: level state %d -> %d", g_state, s);
		g_state = s;
	}
}

void Readiness_Install(uintptr_t base) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	HMODULE self = nullptr;
	char path[MAX_PATH] = {};
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   (LPCSTR)&Readiness_Install, &self);
	GetModuleFileNameA(self, path, MAX_PATH);
	g_modeFile = path;
	g_modeFile = g_modeFile.substr(0, g_modeFile.find_last_of('\\') + 1) + "fastload_readiness.txt";
	if (FILE* fp = fopen(g_modeFile.c_str(), "r")) {
		int m = 0, t = 0, sp = 0;
		double hold = 0;
		const int n = fscanf(fp, "%d %d %lf %d", &m, &t, &hold, &sp);
		if (n >= 1 && (m == 0 || m == 1)) g_earlyMode = m;
		if (n >= 2 && t >= 0 && t <= 512) g_tempMB = t;
		if (n >= 3 && hold >= 0.05 && hold <= 1.0) g_earlyCfg.hold = hold;
		if (n >= 4 && sp >= 0 && sp <= 64) g_spawnN = sp;
		fclose(fp);
	}
	g_spawnOk = !memcmp((void*)(base + kRvaSpawnRead), kSpawnReadBytes, sizeof kSpawnReadBytes);
	if (!g_spawnOk && g_spawnN) Log("readiness: the entity spawn budget's code differs - spawn %d not used", g_spawnN.load());
	g_faderOk = g_spawnOk && !memcmp((void*)(base + kRvaFaderImm - 1), kFaderBytes, sizeof kFaderBytes);
	if (g_spawnOk && !g_faderOk && g_spawnN) Log("readiness: the fader's spawn constant differs - only the variable is raised");
	void* start = (void*)(base + kRvaStart);
	const bool ok = !memcmp(start, kStartBytes, sizeof kStartBytes) &&
	                !memcmp((void*)(base + kRvaIsReady), kIsReadyBytes, sizeof kIsReadyBytes);
	if (!ok) { Log("readiness: code check failed - off (other game version?)"); return; }
	g_base = base;
	g_lock = (LockFn)(base + kRvaLock);
	g_unlock = (LockFn)(base + kRvaUnlock);
	const bool h = MH_CreateHook(start, (void*)&HkStart, (void**)&g_origStart) == MH_OK && MH_EnableHook(start) == MH_OK;
	g_off = !h;
	g_mainTid = Early_MainThread();
	void* aiL = (void*)(base + kRvaAiLaunch);
	void* aiW = (void*)(base + kRvaAiWait);
	const bool ai = h && g_mainTid && !memcmp(aiL, kAiLaunchBytes, sizeof kAiLaunchBytes) &&
	                !memcmp(aiW, kAiWaitBytes, sizeof kAiWaitBytes) &&
	                MH_CreateHook(aiL, (void*)&HkAiLaunch, (void**)&g_aiLaunchOrig) == MH_OK &&
	                MH_CreateHook(aiW, (void*)&HkAiWait, (void**)&g_aiWaitOrig) == MH_OK &&
	                MH_EnableHook(aiL) == MH_OK && MH_EnableHook(aiW) == MH_OK;
	Log("readiness: %s (logs every readiness gate that holds the screen 0.25 s or more, and which task holds it); "
	    "let me in early %s (once the must-have tasks are ready %.2f s); stream temp budget while it waits: %s; entity "
	    "spawns per class a frame while it waits: %s; the AI frame job timed %s", h ? "on" : "FAILED to hook the observer start",
	    g_earlyMode ? "ON" : "off", g_earlyCfg.hold,
	    g_tempMB ? (std::to_string(g_tempMB.load()) + " MB").c_str() : "the game's own",
	    g_spawnN ? std::to_string(g_spawnN.load()).c_str() : "the game's own",
	    ai ? "(the AI module's job start and Main's wait for it)" : "NOT (its code differs or the hooks failed)");
}

void Readiness_Command(const char* args) {
	while (args && *args == ' ') args++;
	const auto save = [] {
		if (FILE* f = fopen(g_modeFile.c_str(), "w")) {
			if (g_spawnN.load()) fprintf(f, "%d %d %.2f %d\n", g_earlyMode.load(), g_tempMB.load(), g_earlyCfg.hold, g_spawnN.load());
			else if (g_earlyCfg.hold != early::Cfg{}.hold) fprintf(f, "%d %d %.2f\n", g_earlyMode.load(), g_tempMB.load(), g_earlyCfg.hold);
			else fprintf(f, "%d %d\n", g_earlyMode.load(), g_tempMB.load());
			fclose(f);
		}
	};
	if (args && !strncmp(args, "early", 5)) {
		const char* v = args + 5;
		while (*v == ' ') v++;
		if ((*v == '0' || *v == '1') && (v[1] == 0 || v[1] == ' ')) {
			g_earlyMode = *v - '0';
			save();
			Log("readiness: let me in early %s (kept for the next start in %s)", g_earlyMode ? "ON" : "off",
			    g_modeFile.c_str());
			return;
		}
	}
	if (args && !strncmp(args, "hold", 4)) {
		double h = -1;
		if (sscanf(args + 4, "%lf", &h) == 1 && h >= 0.05 && h <= 1.0) {
			g_earlyCfg.hold = h;   // main thread (commands run there, as Readiness_Frame)
			save();
			Log("readiness: let in early once the must-have tasks are ready %.2f s (kept for the next start)", h);
			return;
		}
		Log("readiness: hold takes 0.05-1 (s; 0.3 is the default)");
		return;
	}
	if (args && !strncmp(args, "temp", 4)) {
		int mb = -1;
		if (sscanf(args + 4, "%d", &mb) == 1 && (mb == 0 || (mb >= 32 && mb <= 512))) {
			g_tempMB = mb;
			save();
			Log("readiness: stream temp budget while a load screen waits: %s (kept for the next start)",
			    mb ? (std::to_string(mb) + " MB").c_str() : "the game's own");
			return;
		}
		Log("readiness: temp takes 0 (the game's own) or 32-512 MB");
		return;
	}
	if (args && !strncmp(args, "spawn", 5)) {
		int n = -1;
		if (sscanf(args + 5, "%d", &n) == 1 && (n == 0 || (n >= 4 && n <= 64))) {
			g_spawnN = n;
			save();
			Log("readiness: entity spawns per class a frame while a load screen waits: %s (kept for the next start)",
			    n ? std::to_string(n).c_str() : "the game's own");
			return;
		}
		Log("readiness: spawn takes 0 (the game's own) or 4-64");
		return;
	}
	Log("readiness: %s, let me in early %s (hold %.2f s), temp budget %d MB (0 = the game's), entity spawns %d (0 = the "
	    "game's)  (readiness early 0|1, readiness hold 0.05-1, readiness temp 0|32-512, readiness spawn 0|4-64)",
	    g_off ? "off" : "on", g_earlyMode ? "ON" : "off", g_earlyCfg.hold, g_tempMB.load(), g_spawnN.load());
}

void Readiness_Frame() {
	if (g_off) return;
	const long long now = Now();
	const double dt = g_lastTick ? Secs(now - g_lastTick) : 0.0;
	g_lastTick = now;
	{
		std::lock_guard<std::mutex> l(g_newMu);
		for (auto& g : g_new) { g.ai0 = g_ai; g.ph0 = Snap(); g_gates.push_back(g); }
		g_new.clear();
	}
	if (g_playT0 && now - g_playT0 >= 10 * g_freq) {   // the same numbers in play, for comparison
		Log("readiness: AI update in the first 10 s of play after the gate: %s%s%s", AiLine(g_playAi, g_ai, Secs(now - g_playT0)).c_str(),
		    PhaseLine(g_playPh).c_str(),
		    g_aiOther.load() ? (" (" + std::to_string(g_aiOther.load()) + " calls from other threads not counted)").c_str() : "");
		g_playT0 = 0;
	}
	if (g_gates.empty()) return;
	if (!g_service) {
		int where = 0;
		g_service = FindService(&where);
		if (!g_service) return;   // no level yet: try again next frame
		Log("readiness: service found (level manager +0x%x%s)", where & 0xfff, where & 0x1000 ? ", through its observer" : "");
	}
	bool changed = false, longWait = false;
	std::vector<std::string> lines;
	g_lock(g_service + 0x38);
	const bool okTasks = ReadTasks(&changed);
	ReaderNow rn{};
	const bool rnOk = ReadReader(&rn);
	if (okTasks) {
		for (auto& t : g_tasks) Cat(t.at, &t.cat);   // asked every time, as the game's IsReady does
		for (size_t gi = 0; gi < g_gates.size();) {
			Gate& g = g_gates[gi];
			bool all = true, mustReady = true, detailBusy = false;
			bool line = now - (g.lastLine ? g.lastLine : g.t0) >= g_freq;
			// this frame's time, but not from before the gate started (a gate can start inside a long frame)
			const double gdt = (std::min)(dt, (std::max)(0.0, Secs(now - g.t0)));
			g.frames++;
			if (gdt > g.longest) g.longest = gdt;
			g.lineFrames++;
			if (gdt > g.lineLongest) g.lineLongest = gdt;
			if (rnOk) { Add(g.rd, rn); Add(g.lineRd, rn); }
			std::string busy;
			const double age = Secs(now - g.t0);
			for (auto& t : g_tasks) {
				if (!(t.cat & g.mask)) continue;
				bool r = true;
				if (!Ready(t.at, g.mask, &r) || r) {
					if (early::StragglerKind(t.name.c_str())) g.lowSince[Slot(g, t.at)] = -1;
					continue;
				}
				all = false;
				const size_t i = Slot(g, t.at);
				if (early::Detail(t.name.c_str())) detailBusy = true;
				else if (early::Straggler(t.name.c_str(), InFlight(t), age, g.lowSince[i])) { detailBusy = true; g.straggled[i] = 1; }
				else mustReady = false;
				g.notReady[i] += gdt;
				g.lastBusy[i] = now;
				if (line) {
					char one[120];
					const int n = InFlight(t);
					if (n >= 0) snprintf(one, sizeof one, "%s%s (%d in flight)", busy.empty() ? "" : ", ", t.name.c_str(), n);
					else snprintf(one, sizeof one, "%s%s", busy.empty() ? "" : ", ", t.name.c_str());
					busy += one;
				}
			}
			const double waited = Secs(now - g.t0);
			// and no heads' morph data on morphpool's threads (not in the game's cache yet: morphpool.h)
			if (g.eligible && !all && g.letAt < 0 && early::Step(g.early, g_earlyCfg, waited, mustReady, detailBusy) &&
			    Morphpool_Pending() == 0 &&
			    g_earlyMode.load(std::memory_order_relaxed) == 1) {
				const int r = LetIn(g.obs, g.mask);
				char one[200];
				if (r == 0) {
					g.letAt = waited;
					snprintf(one, sizeof one, "readiness: gate 0x%x (from +%llx) let in early after %.1f s; still loading: ",
					         g.mask, (unsigned long long)g.from, waited);
				} else {
					g.eligible = false;   // once per gate
					snprintf(one, sizeof one, "readiness: gate 0x%x (from +%llx) can't let in early (%s); still loading: ",
					         g.mask, (unsigned long long)g.from, r == 1 ? "observer not armed" : r == 2 ? "observer layout differs" : "observer unreadable");
				}
				std::string still;
				for (auto& t : g_tasks) {
					bool rr = true;
					if (!(t.cat & g.mask) || !Ready(t.at, g.mask, &rr) || rr) continue;
					char n[96];
					const int f = InFlight(t);
					snprintf(n, sizeof n, f >= 0 ? "%s%s (%d in flight)" : "%s%s", still.empty() ? "" : ", ", t.name.c_str(), f);
					still += n;
				}
				for (size_t k = 0; k < g.taskAt.size(); k++)
					if (g.straggled[k]) still += " (" + NameAt(g.taskAt[k]) + " counted as ready: its last requests stuck 1 s)";
				lines.push_back(one + still);
			}
			if (waited >= 0.25) longWait = true;
			if (all || waited > 120) {
				Opened(g, now, all ? "open" : "still shut, stopped watching", &lines);
				g_gates.erase(g_gates.begin() + gi);
				continue;
			}
			if (line) {
				g.lastLine = now;
				char head[96];
				snprintf(head, sizeof head, "readiness: gate 0x%x (from +%llx) waiting %.1f s: not ready: ", g.mask,
				         (unsigned long long)g.from, waited);
				lines.push_back(head + busy + Feed(g.lineFrames, g.lineLongest) + Reader(g.lineRd));
				g.lineFrames = 0;
				g.lineLongest = 0;
				g.lineRd = ReaderAcc{};
			}
			gi++;
		}
	}
	g_unlock(g_service + 0x38);
	{
		bool loadGate = false;
		for (auto& g : g_gates) if (early::Eligible(g.mask)) loadGate = true;
		TempBudget(loadGate);
		SpawnBudget(loadGate);
		Streamahead_SetGate(loadGate);
		Morphpool_SetGate(loadGate);
		Hitch_Window(loadGate);
	}
	// every file the main thread reads while a gate holds the screen, up to 1.5 s after (mainreads.cpp): names the
	// XML read in the frame the fast-travel observer fires
	if (longWait) MainReads_Log(1500);
	if (!okTasks) { Log("readiness: the task list doesn't read - off"); g_off = true; return; }
	if (changed) {
		std::string all;
		for (auto& t : g_tasks) {
			char one[120];
			snprintf(one, sizeof one, "%s%s (0x%x)", all.empty() ? "" : ", ", t.name.c_str(), t.cat);
			all += one;
		}
		Log("readiness: %zu tasks: %s", g_tasks.size(), all.c_str());
	}
	for (auto& l : lines) Log("%s", l.c_str());
}
