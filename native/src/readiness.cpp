// Which readiness task holds the loading screen after the save is applied: see readiness.h.
// Addresses: WHGame.dll 1.5.6, checked at start against the installed DLL's bytes.
#include "readiness.h"
#include "log.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uintptr_t kRvaGame = 0x5651d60;       // the C_Game instance; +0xa8 = the level manager
constexpr uintptr_t kRvaLevelMgrVt = 0x4099530, kRvaServiceVt = 0x3dbe8a0, kRvaProgressVt = 0x3a982b8;
constexpr uintptr_t kRvaLock = 0x3d902c, kRvaUnlock = 0x42dc54;   // the game's recursive mutex (as in streamio.cpp)
constexpr uintptr_t kRvaStates = 0xfc3868, kRvaIsReady = 0x8baee0;
const unsigned char kStatesBytes[] = { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x41,0x56,0x41,0x57,0x48 };
const unsigned char kIsReadyBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57 };
constexpr uint32_t kMask = 0x3f93;              // the mask the level manager's observer waits on
// level manager: +0x10 the state (0 idle .. 6 waiting for graphics and scripts, 7 ready, 8/9 video); the state
// machine FUN_180fc3868 switches on it, and its +0x91 is the "video ended" byte (levelmgr +0xa1)
// service: +0x38 its lock, +0x48 / +0x50 the task pointers (begin, end); task vtable +0x10 IsReady(mask),
// +0x18 category; a C_ReadinessProgress task: +0x10 name (char*), +0x1c requests in flight

using LockFn = void(__fastcall*)(void*);
using CatFn = uint32_t(__fastcall*)(void*);
using ReadyFn = bool(__fastcall*)(void*, uint32_t);

uintptr_t g_base = 0;
LockFn g_lock = nullptr, g_unlock = nullptr;
uint8_t* g_service = nullptr;
bool g_off = true;
ULONGLONG g_start = 0;
int g_state = -100;
long long g_freq = 1, g_lastTick = 0, g_waitStart = 0, g_lastLine = 0, g_lastStateCheck = 0;

struct Task {
	void* at;
	uintptr_t vt;
	uint32_t cat;
	std::string name;
	double notReady = 0;      // seconds not ready during this wait
	long long lastBusy = 0;   // QPC time it was last seen not ready
};
std::vector<Task> g_tasks;
uintptr_t g_vecBegin = 0, g_vecEnd = 0;

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Secs(long long t) { return (double)t / (double)g_freq; }

// Guarded helpers, no C++ objects in them (MSVC forbids __try next to unwinding).
bool Rd(uintptr_t a, void* out, size_t n) {
	__try { memcpy(out, (const void*)a, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Ask(void* t, uint32_t* cat, bool* ready) {   // what the game's IsReady (FUN_1808baee0) asks each task
	__try {
		void** v = *(void***)t;
		*cat = ((CatFn)v[0x18 / 8])(t);
		*ready = (*cat & kMask) ? ((ReadyFn)v[0x10 / 8])(t, kMask) : true;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::string NameOf(void* t, uintptr_t vt) {
	uintptr_t p = 0;
	char s[48] = {};
	if (vt == g_base + kRvaProgressVt && Rd((uintptr_t)t + 0x10, &p, 8) && p) {
		int n = 0;
		for (; n < 47; n++) {
			char c;
			if (!Rd(p + n, &c, 1)) { n = -1; break; }
			if (!c) break;
			if (c < 32 || c > 126) { n = -1; break; }
			s[n] = c;
		}
		if (n > 0) return std::string(s, (size_t)n);
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
// Whether +0x28 holds the observer itself or a pointer to it isn't settled, so both ways are tried, and anything near
// it, and a candidate counts only when its vtable is the service's.
uint8_t* FindService(int* where) {
	const uintptr_t lm = LevelMgr();
	if (!lm) return nullptr;
	const uintptr_t want = g_base + kRvaServiceVt;
	for (int off = 0x10; off <= 0x100; off += 8) {
		uintptr_t p = 0, vt = 0, q = 0;
		if (!Rd(lm + off, &p, 8) || !p) continue;
		if (Rd(p, &vt, 8) && vt == want) { *where = off; return (uint8_t*)p; }                  // levelmgr+off -> service
		if (Rd(p + 8, &q, 8) && q && Rd(q, &vt, 8) && vt == want) { *where = 0x1000 + off; return (uint8_t*)q; }   // -> observer -> service
	}
	return nullptr;
}

// The task list, re-read when the vector moves. Called with the service's lock held.
bool ReadTasks() {
	uintptr_t b = 0, e = 0;
	if (!Rd((uintptr_t)g_service + 0x48, &b, 8) || !Rd((uintptr_t)g_service + 0x50, &e, 8) || e < b || (e - b) / 8 > 512)
		return false;
	if (b == g_vecBegin && e == g_vecEnd) return true;
	std::vector<Task> fresh;
	for (uintptr_t p = b; p < e; p += 8) {
		void* t = nullptr;
		uintptr_t vt = 0;
		if (!Rd(p, &t, 8) || !t || !Rd((uintptr_t)t, &vt, 8)) return false;
		Task k{ t, vt, 0, NameOf(t, vt) };
		for (auto& old : g_tasks) if (old.at == t) { k.notReady = old.notReady; k.lastBusy = old.lastBusy; }
		fresh.push_back(k);
	}
	g_tasks.swap(fresh);
	g_vecBegin = b;
	g_vecEnd = e;
	return true;
}

// One look at every task (main thread, service lock held). Adds dt to each masked task that isn't ready.
void Look(double dt, long long now, std::string* busy) {
	for (auto& t : g_tasks) {
		bool ready = true;
		if (!Ask(t.at, &t.cat, &ready) || !(t.cat & kMask) || ready) continue;
		t.notReady += dt;
		t.lastBusy = now;
		if (busy) {
			char one[120];
			const int n = InFlight(t);
			if (n >= 0) snprintf(one, sizeof one, "%s%s (%d in flight)", busy->empty() ? "" : ", ", t.name.c_str(), n);
			else snprintf(one, sizeof one, "%s%s", busy->empty() ? "" : ", ", t.name.c_str());
			*busy += one;
		}
	}
}

void Summary(int to, long long now) {
	std::vector<const Task*> v;
	for (auto& t : g_tasks) if (t.notReady > 0) v.push_back(&t);
	std::sort(v.begin(), v.end(), [](const Task* a, const Task* b) { return a->notReady > b->notReady; });
	std::string s;
	const Task* last = nullptr;
	for (auto* t : v) {
		char one[120];
		snprintf(one, sizeof one, "%s%s %.1f s", s.empty() ? "" : ", ", t->name.c_str(), t->notReady);
		s += one;
		if (!last || t->lastBusy > last->lastBusy) last = t;
	}
	Log("readiness: state 6 -> %d after %.1f s; not ready for: %s; ready last: %s (%.2f s before the end)", to,
	    Secs(now - g_waitStart), s.empty() ? "nothing (every task ready)" : s.c_str(), last ? last->name.c_str() : "-",
	    last ? Secs(now - last->lastBusy) : 0.0);
}

}  // namespace

void Readiness_Install(uintptr_t base) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	const bool ok = !memcmp((void*)(base + kRvaStates), kStatesBytes, sizeof kStatesBytes) &&
	                !memcmp((void*)(base + kRvaIsReady), kIsReadyBytes, sizeof kIsReadyBytes);
	if (!ok) { Log("readiness: code check failed - off (other game version?)"); return; }
	g_base = base;
	g_lock = (LockFn)(base + kRvaLock);
	g_unlock = (LockFn)(base + kRvaUnlock);
	g_start = GetTickCount64();
	g_off = false;
	Log("readiness: on (logs what holds the loading screen after the save is applied, first 20 minutes)");
}

void Readiness_Frame() {
	if (g_off) return;
	if (GetTickCount64() - g_start > 20ull * 60 * 1000) {
		g_off = true;
		Log("readiness: off (20 minutes after start)");
		return;
	}
	const long long now = Now();
	const double dt = g_lastTick ? Secs(now - g_lastTick) : 0.0;
	g_lastTick = now;
	if (g_state != 6 && now - g_lastStateCheck < g_freq / 10) return;   // outside the wait: the state 10 times a second
	g_lastStateCheck = now;
	const int s = LevelState();
	if (s != g_state) {
		if (g_state == 6 && g_service) Summary(s, now);
		if (g_state == -100) Log("readiness: level state %d at the first look (-1 = no level manager yet)", s);
		else Log("readiness: level state %d -> %d", g_state, s);
		if (s == 6) {
			g_waitStart = g_lastLine = now;
			for (auto& t : g_tasks) { t.notReady = 0; t.lastBusy = 0; }
		}
		g_state = s;
	}
	if (s != 6) return;
	if (!g_service) {
		int where = 0;
		g_service = FindService(&where);
		if (!g_service) {
			Log("readiness: no readiness service next to the level manager - off");
			g_off = true;
			return;
		}
		Log("readiness: service found (level manager +0x%x%s)", where & 0xfff, where & 0x1000 ? ", through its observer" : "");
	}
	const bool line = now - g_lastLine >= g_freq;
	std::string busy;
	const size_t before = g_tasks.size();
	g_lock(g_service + 0x38);
	const bool okTasks = ReadTasks();
	if (okTasks) Look(dt, now, line ? &busy : nullptr);
	g_unlock(g_service + 0x38);
	if (!okTasks) { Log("readiness: the task list doesn't read - off"); g_off = true; return; }
	if (g_tasks.size() != before) {
		std::string all;
		for (auto& t : g_tasks) {   // t.cat was filled by Look under the lock
			char one[120];
			snprintf(one, sizeof one, "%s%s (0x%x%s)", all.empty() ? "" : ", ", t.name.c_str(), t.cat,
			         (t.cat & kMask) ? "" : ", not waited on");
			all += one;
		}
		Log("readiness: %zu tasks: %s", g_tasks.size(), all.c_str());
	}
	if (line) {
		g_lastLine = now;
		Log("readiness: waiting %.1f s: not ready: %s", Secs(now - g_waitStart), busy.empty() ? "none" : busy.c_str());
	}
}
