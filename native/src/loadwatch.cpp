// Terrain loader wait timing: see loadwatch.h.
#include "loadwatch.h"
#include "applyfast.h"
#include "questmenu.h"
#include "questfast.h"
#include "questports.h"
#include "questreplay.h"
#include "log.h"
#include <windows.h>
#include <atomic>
#include <cstring>
#include <string>
#include "MinHook.h"

namespace {

constexpr uintptr_t kRvaPoll = 0x354ff84;
const unsigned char kPollBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20 };
using PollFn = void(__fastcall*)(void* loaders, uint64_t a2);
PollFn g_orig = nullptr;
long long g_freq = 1, g_first = 0, g_lastPt = 0;
size_t g_startN = 0, g_lastN = ~(size_t)0;
unsigned long long g_polls = 0;
std::string g_curve;

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

size_t Count(void* v) {
	__try { const uintptr_t* p = (const uintptr_t*)v; return (size_t)((p[1] - p[0]) / 8); }
	__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void __fastcall HkPoll(void* loaders, uint64_t a2) {   // main thread only
	const long long a = Now();
	if (!g_first) {
		g_first = a; g_lastPt = a; g_startN = Count(loaders); g_polls = 0; g_curve.clear();
		Log("loadwatch: terrain wait starts, %zu loaders left", g_startN);
	}
	g_orig(loaders, a2);
	g_polls++;
	const size_t n = Count(loaders);
	const long long ms = (a - g_first) * 1000 / g_freq;
	if ((a - g_lastPt) * 4 >= g_freq || n == 0) {
		g_lastPt = a;
		char b[32]; snprintf(b, sizeof b, " %lld:%zu", ms, n); g_curve += b;
	}
	if (n == 0) {
		Log("loadwatch: terrain wait done after %lld ms (%zu loaders, %llu polls); left at ms:%s", ms, g_startN, g_polls,
		    g_curve.c_str());
		g_first = 0;
	}
	g_lastN = n;
}

// ---- quest load timing (the quest-loader decompile, 2026-10-23: the quest job runs on one job worker, ~12 s, and the
// main thread waits for it; which phase costs what decides whether it can be made shorter). Times every top-level call
// (nested calls of the same function count once); calls of 20 ms or more get a log line, at most 80 lines.
struct QHook { const char* name; uintptr_t rva; unsigned char bytes[10]; void* orig; };
QHook g_q[] = {
	{ "job submit", 0x4da314, { 0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x56 }, nullptr },
	{ "job (ConceptLoadJob body)", 0x1e2b650, { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x54,0x24,0x10 }, nullptr },
	{ "project (3 phases)", 0x51abb4, { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x20 }, nullptr },
	{ "phase 2a node building", 0x51ab28, { 0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x08,0x57,0x48,0x83 }, nullptr },
	{ "phase 2b CreatePorts", 0x6a03d4, { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18 }, nullptr },
	{ "phase 2b CreateEdges", 0x698054, { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18 }, nullptr },
	{ "main thread waits for the job", 0x11414d8, { 0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20 }, nullptr },
};
constexpr int kQ = sizeof g_q / sizeof g_q[0];
constexpr int kQuestWait = 6;   // "main thread waits for the job"
constexpr int kJobSubmit = 0, kJobBody = 1;
std::atomic<int> g_jobState{-1};   // Loadwatch_QuestJob(); -1 until the body hook is on
std::atomic<int> g_qPhase{0};      // the innermost quest hook running (Loadwatch_QuestPhase, for questsample)
std::atomic<long long> g_qCalls[kQ], g_qTicks[kQ];
std::atomic<int> g_qLines{0};
long long g_t0 = 0;
using Fn4 = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t);   // every one takes at most 4 integer args

void QNote(int i, long long a, long long b) {
	g_qCalls[i]++;
	g_qTicks[i] += b - a;
	const double ms = (double)(b - a) * 1000.0 / g_freq;
	if (ms >= 20.0 && g_qLines++ < 80)
		Log("loadwatch: quest %s %.0f ms (thread %lu, started %.2f s after the plugin; %lld calls, %.0f ms in all so far)", g_q[i].name,
		    ms, GetCurrentThreadId(), (double)(a - g_t0) / g_freq, g_qCalls[i].load(), (double)g_qTicks[i].load() * 1000.0 / g_freq);
}

template <int I> uint64_t __fastcall QWrap(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
	thread_local int depth = 0;
	if (I == kQuestWait && depth == 0) Applyfast_BeforeQuestWait();   // its own wait is logged by applyfast
	if (I == kJobSubmit && depth == 0 && g_jobState.load() >= 0) g_jobState = 0;
	if (I == kJobBody && depth == 0 && Questmenu_OnJobBody(a)) {   // the graph built at the main menu is kept: no rebuild
		if (g_jobState.load() >= 0) g_jobState = 1;
		return 0;
	}
	const long long t = Now();
	depth++;
	const int prevPhase = I >= kJobBody && I <= 5 ? g_qPhase.exchange(I) : 0;
	if (I == kJobBody) Questports_JobScope(true);    // the port cache acts on this thread only
	if (I == 3) Questreplay_Phase2a(true);           // phase 2a: the node build record / replay (questreplay.cpp)
	const int rep = I == kJobBody && depth == 1 ? Questmenu_JobReplayBegin(a) : 0;   // the load's own build, from the record
	uint64_t r = ((Fn4)g_q[I].orig)(a, b, c, d);
	if (rep && Questmenu_JobReplayEnd(rep)) r = ((Fn4)g_q[I].orig)(a, b, c, d);      // the replay failed: the game's own build
	if (I == 3) Questreplay_Phase2a(false);
	if (I == kJobBody) Questports_JobScope(false);
	if (I >= kJobBody && I <= 5) g_qPhase = prevPhase;
	depth--;
	if (I == kJobBody && depth == 0 && g_jobState.load() >= 0) g_jobState = 1;
	if (depth == 0) QNote(I, t, Now());
	if (I == kJobBody && depth == 0) Questfast_Report("after the quest job");
	return r;
}
void* const g_qWrap[] = { (void*)&QWrap<0>, (void*)&QWrap<1>, (void*)&QWrap<2>, (void*)&QWrap<3>, (void*)&QWrap<4>,
                          (void*)&QWrap<5>, (void*)&QWrap<6> };
static_assert(sizeof g_qWrap / sizeof g_qWrap[0] == kQ, "one wrapper per hook");

}  // namespace

int Loadwatch_QuestJob() { return g_jobState.load(); }

int Loadwatch_QuestTotals(double ms[7], long long calls[7]) {
	static_assert(kQ == 7, "Loadwatch_QuestTotals' slots");
	for (int i = 0; i < kQ; i++) { ms[i] = g_qTicks[i].load() * 1000.0 / g_freq; calls[i] = g_qCalls[i].load(); }
	return kQ;
}

void Loadwatch_Install(uintptr_t base) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	void* p = (void*)(base + kRvaPoll);
	if (memcmp(p, kPollBytes, sizeof kPollBytes)) { Log("loadwatch: code check failed - not installed"); return; }
	const bool ok = MH_CreateHook(p, (void*)&HkPoll, (void**)&g_orig) == MH_OK && MH_EnableHook(p) == MH_OK;
	Log("loadwatch: terrain loader wait hook %s", ok ? "on" : "FAILED");
	g_t0 = Now();
	int on = 0;
	std::string off;
	for (int i = 0; i < kQ; i++) {
		void* t = (void*)(base + g_q[i].rva);
		if (memcmp(t, g_q[i].bytes, sizeof g_q[i].bytes) ||
		    MH_CreateHook(t, g_qWrap[i], &g_q[i].orig) != MH_OK || MH_EnableHook(t) != MH_OK) { off += off.empty() ? "" : ", "; off += g_q[i].name; continue; }
		on++;
		if (i == kJobBody) g_jobState = 1;   // no job yet = nothing to wait for; a submit sets 0
	}
	Log("loadwatch: quest load timing %d of %d hooks on%s%s", on, kQ, off.empty() ? "" : "; off: ", off.c_str());
}

int Loadwatch_QuestPhase() { return g_qPhase.load(std::memory_order_relaxed); }
