// Which readiness task holds a loading or black screen: see readiness.h.
// Addresses: WHGame.dll 1.5.6, checked at start against the installed DLL's bytes.
#include "readiness.h"
#include "log.h"
#include <windows.h>
#include <intrin.h>
#include <algorithm>
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
long long g_freq = 1, g_lastTick = 0, g_lastStateCheck = 0;

struct Task {
	void* at;
	uintptr_t vt;
	uint32_t cat = 0;
	std::string name;
};
std::vector<Task> g_tasks;
uintptr_t g_vecBegin = 0, g_vecEnd = 0;

struct Gate {
	uint32_t mask;
	uintptr_t from;           // the call site's RVA
	long long t0 = 0, lastLine = 0;
	std::vector<double> notReady;      // seconds per task (index into g_tasks, by task pointer below)
	std::vector<void*> taskAt;
	std::vector<long long> lastBusy;
};
std::mutex g_newMu;
std::vector<Gate> g_new;    // started since the last frame (any thread)
std::vector<Gate> g_gates;  // being watched (main thread)

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Secs(long long t) { return (double)t / (double)g_freq; }

uint64_t __fastcall HkStart(void* observer, uint32_t mask, void* callback) {
	Gate g;
	g.mask = mask;
	g.from = (uintptr_t)_ReturnAddress() - g_base;
	g.t0 = Now();
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
// Checked in the running game 2026-10-23: levelmgr +0x30. Both layouts are tried, and a candidate counts only when
// its vtable is the service's.
uint8_t* FindService(int* where) {
	const uintptr_t lm = LevelMgr();
	if (!lm) return nullptr;
	const uintptr_t want = g_base + kRvaServiceVt;
	for (int off = 0x10; off <= 0x100; off += 8) {
		uintptr_t p = 0, vt = 0, q = 0;
		if (!Rd(lm + off, &p, 8) || !p) continue;
		if (Rd(p, &vt, 8) && vt == want) { *where = off; return (uint8_t*)p; }
		if (Rd(p + 8, &q, 8) && q && Rd(q, &vt, 8) && vt == want) { *where = 0x1000 + off; return (uint8_t*)q; }
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
	return g.taskAt.size() - 1;
}

std::string NameAt(void* t) {
	for (auto& k : g_tasks) if (k.at == t) return k.name;
	return "?";
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
	char head[160];
	snprintf(head, sizeof head, "readiness: gate 0x%x (from +%llx) %s after %.1f s; not ready for: ", g.mask,
	         (unsigned long long)g.from, how, took);
	out->push_back(head + (s.empty() ? std::string("nothing (every task ready)") : s) + "; ready last: " + last);
}

}  // namespace

void Readiness_Install(uintptr_t base) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	void* start = (void*)(base + kRvaStart);
	const bool ok = !memcmp(start, kStartBytes, sizeof kStartBytes) &&
	                !memcmp((void*)(base + kRvaIsReady), kIsReadyBytes, sizeof kIsReadyBytes);
	if (!ok) { Log("readiness: code check failed - off (other game version?)"); return; }
	g_base = base;
	g_lock = (LockFn)(base + kRvaLock);
	g_unlock = (LockFn)(base + kRvaUnlock);
	const bool h = MH_CreateHook(start, (void*)&HkStart, (void**)&g_origStart) == MH_OK && MH_EnableHook(start) == MH_OK;
	g_off = !h;
	Log("readiness: %s (logs every readiness gate that holds the screen 0.25 s or more, and which task holds it)",
	    h ? "on" : "FAILED to hook the observer start");
}

void Readiness_Frame() {
	if (g_off) return;
	const long long now = Now();
	const double dt = g_lastTick ? Secs(now - g_lastTick) : 0.0;
	g_lastTick = now;
	if (now - g_lastStateCheck >= g_freq / 10) {   // the level manager's state, 10 times a second
		g_lastStateCheck = now;
		const int s = LevelState();
		if (s != g_state) {
			if (g_state == -100) Log("readiness: level state %d at the first look (-1 = no level manager yet)", s);
			else Log("readiness: level state %d -> %d", g_state, s);
			g_state = s;
		}
	}
	{
		std::lock_guard<std::mutex> l(g_newMu);
		for (auto& g : g_new) g_gates.push_back(g);
		g_new.clear();
	}
	if (g_gates.empty()) return;
	if (!g_service) {
		int where = 0;
		g_service = FindService(&where);
		if (!g_service) return;   // no level yet: try again next frame
		Log("readiness: service found (level manager +0x%x%s)", where & 0xfff, where & 0x1000 ? ", through its observer" : "");
	}
	bool changed = false;
	std::vector<std::string> lines;
	g_lock(g_service + 0x38);
	const bool okTasks = ReadTasks(&changed);
	if (okTasks) {
		for (auto& t : g_tasks) Cat(t.at, &t.cat);   // asked every time, as the game's IsReady does
		for (size_t gi = 0; gi < g_gates.size();) {
			Gate& g = g_gates[gi];
			bool all = true, line = now - (g.lastLine ? g.lastLine : g.t0) >= g_freq;
			std::string busy;
			for (auto& t : g_tasks) {
				if (!(t.cat & g.mask)) continue;
				bool r = true;
				if (!Ready(t.at, g.mask, &r) || r) continue;
				all = false;
				const size_t i = Slot(g, t.at);
				g.notReady[i] += dt;
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
				lines.push_back(head + busy);
			}
			gi++;
		}
	}
	g_unlock(g_service + 0x38);
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
