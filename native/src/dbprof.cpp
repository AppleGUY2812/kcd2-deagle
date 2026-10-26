// The object database's tables timed one by one: see dbprof.h.
#include "dbprof.h"
#include "dbasync.h"
#include "early.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr uintptr_t kRvaLoadList = 0xef4048;   // byte (manager, std::vector<table*>* list, char, char)
const unsigned char kLoadListBytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18,0x4c };

using LoadListFn = uint8_t(__fastcall*)(void* mgr, uintptr_t* list, char a, char b);
using MethodFn = uint64_t(__fastcall*)(void* table);
LoadListFn g_origList = nullptr;
uintptr_t g_base = 0, g_end = 0;
long long g_freq = 1;

struct Orig { MethodFn load, link; };
std::mutex g_mu;
std::unordered_map<uintptr_t, Orig> g_vts;   // table vtable -> its own load / link

// ---- dbpool (fastload_dbpool.txt, see dbprof.h) ----
// The three tables, their load (vt+0x48), "is loaded" (vt+0x30: movzx eax, byte [rcx+0x60]) and post-load (vt+0x88).
struct Known { const char* name; uintptr_t vt, load, post; };
const Known kKnown[] = {
	{ "CharacterComponent", 0x3e8b648, 0x1364f20, 0x3b6e80 },
	{ "Item", 0x3bb0df8, 0x16a366c, 0x3b6e80 },
	{ "InventoryPreset", 0x3eb6490, 0x1364f20, 0x1364f50 },
};
constexpr uintptr_t kRvaIsLoaded = 0x1a72e80;
const unsigned char kIsLoadedBytes[] = { 0x0f,0xb6,0x41,0x60,0xc3 };
// first bytes of the two load functions (both: the reader (FUN_1816a369c / FUN_1806e3bf0), then on success vt+0x88,
// byte +0x60 = 1 and eax 0; on failure the rows cleared and eax 1)
const unsigned char kLoadItemBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0x22,0x00,0x00,0x00,0x84,0xc0 };
const unsigned char kLoadCompBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0xc2,0xec,0x37,0xff,0x84,0xc0 };
// FUN_18096d914 (every one of these loads lists the table's extra files through it) sets up a magic static on its first
// call (FUN_1819bca74: registers the cvar wh_sys_ExcludedTableParts, fills a global set). That must happen on Main, as
// in the game, not on a pool thread beside Main's own cvar work: the pool starts only once its guard says done. MSVC's
// guard: 0 never run, -1 running, else the (negative, from INT_MIN up) epoch it finished in. Queue 183 (fastload_184)
// tested "> 0", which is never true: nothing was pooled.
constexpr uintptr_t kRvaPartsGuard = 0x55d48e0;
inline bool PartsSetUp() { const int g = *(volatile const int*)(g_base + kRvaPartsGuard); return g != 0 && g != -1; }
int g_poolMode = 0;   // 0 timed only (rows checked), 1 the three tables loaded ahead on threads
std::atomic<bool> g_poolWant{false};
uintptr_t g_poolMgr = 0;
DWORD g_poolTid = 0;
bool g_poolCodeOk = false;
std::atomic<bool> g_poolStarted{false};
MethodFn g_origIsLoaded = nullptr;
struct Pre {
	uintptr_t table = 0;
	MethodFn load = nullptr;
	const char* name = "";
	HANDLE done = nullptr;
	std::atomic<int> state{0};   // 0 none, 1 loading on its thread, 2 loaded (rc kept), 3 handed to the game
	uint64_t rc = 0;
	long long t0 = 0, t1 = 0;
	DWORD tid = 0;
};
Pre g_pre[3];
std::atomic<int> g_preCount{0};
Pre* PreOf(uintptr_t table) {
	const int n = g_preCount.load(std::memory_order_acquire);
	for (int i = 0; i < n; i++) if (g_pre[i].table == table) return &g_pre[i];
	return nullptr;
}
const Known* KnownOf(uintptr_t vt) {
	for (const Known& k : kKnown) if (vt == g_base + k.vt) return &k;
	return nullptr;
}

struct Rec { uintptr_t table; char name[64]; double load, link; };
thread_local std::vector<Rec>* t_recs = nullptr;   // the list being loaded on this thread

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long a, long long b) { return (double)(b - a) * 1000.0 / g_freq; }

bool InImage(uintptr_t p) { return p >= g_base && p < g_end; }
bool Rd8(uintptr_t a, uintptr_t* v) {
	__try { *v = *(const uintptr_t*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool RdB(uintptr_t a, uint8_t* v) {
	__try { *v = *(const uint8_t*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// guarded reads (no C++ objects here: MSVC forbids __try next to unwinding)
bool ReadVt(uintptr_t obj, uintptr_t* vt) {
	__try { *vt = *(uintptr_t*)obj; return InImage(*vt); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void NameOf(void* table, uintptr_t vt, char* out, size_t n) {   // vt+0x10: the table's name (a C string)
	out[0] = 0;
	__try {
		const char* s = ((const char* (__fastcall*)(void*)) * (uintptr_t*)(vt + 0x10))(table);
		size_t i = 0;
		for (; s && i + 1 < n && s[i] >= 32 && s[i] < 127; i++) out[i] = s[i];
		out[i] = 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}
int TypeOf(void* table, uintptr_t vt) {   // vt+8: the table's database type
	__try { return ((int(__fastcall*)(void*)) * (uintptr_t*)(vt + 8))(table); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

Orig Find(void* table) {
	uintptr_t vt = 0;
	ReadVt((uintptr_t)table, &vt);
	std::lock_guard<std::mutex> l(g_mu);
	auto it = g_vts.find(vt);
	return it != g_vts.end() ? it->second : Orig{ nullptr, nullptr };
}

Rec* RecOf(void* table) {
	if (!t_recs) return nullptr;
	for (Rec& r : *t_recs) if (r.table == (uintptr_t)table) return &r;
	return nullptr;
}

// The rows of a table of the template the three share: a vector of 16-byte entries at +0x30 / +0x38, the row first,
// its name at row+0x20 (a C string: InventoryPreset's index FUN_181364f50 keys its map by it, hashed by FUN_1803f6ca8).
// Row count + a hash of the names in row order, so the game-order load (dbpool 0) and the pooled one (1) can be compared
// across boots.
bool RowsCheck(uintptr_t table, size_t* n, uint64_t* h) {
	__try {
		const uintptr_t* b = *(const uintptr_t**)(table + 0x30);
		const uintptr_t* e = *(const uintptr_t**)(table + 0x38);
		uint64_t x = 1469598103934665603ull;
		size_t c = 0;
		for (const uintptr_t* it = b; it && it < e && c < 1000000; it += 2, c++) {
			const char* key = *(const char* const*)(it[0] + 0x20);
			for (int i = 0; key && i < 256 && key[i]; i++) x = (x ^ (uint8_t)key[i]) * 1099511628211ull;
			x = (x ^ 0xff) * 1099511628211ull;
		}
		*n = c; *h = x;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Item: +0x20 is not its name (queue 183: the hash changed between game-order boots, the row count 5606 did not), so
// the raw 8 bytes at a few row offsets and the entry's second word are hashed too; the ones that stay the same across
// game-order boots are the check.
bool RowsRaw(uintptr_t table, uint64_t* h /* [7] */) {
	static const int kOff[6] = { 0x08, 0x10, 0x18, 0x20, 0x28, 0x30 };
	__try {
		const uintptr_t* b = *(const uintptr_t**)(table + 0x30);
		const uintptr_t* e = *(const uintptr_t**)(table + 0x38);
		for (int k = 0; k < 7; k++) h[k] = 1469598103934665603ull;
		size_t c = 0;
		for (const uintptr_t* it = b; it && it < e && c < 1000000; it += 2, c++) {
			for (int k = 0; k < 6; k++) h[k] = (h[k] ^ *(const uint64_t*)(it[0] + kOff[k])) * 1099511628211ull;
			h[6] = (h[6] ^ it[1]) * 1099511628211ull;
		}
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void LogRows(const char* name, uintptr_t table, const char* how) {
	size_t n = 0; uint64_t h = 0;
	if (RowsCheck(table, &n, &h)) Log("dbpool: %s %s: %zu rows, key hash %016llx", name, how, n, (unsigned long long)h);
	else Log("dbpool: %s %s: rows not readable", name, how);
	uint64_t r[7];
	if (RowsRaw(table, r))
		Log("dbpool: %s %s: raw row words +08 %016llx +10 %016llx +18 %016llx +20 %016llx +28 %016llx +30 %016llx, entry+8 %016llx",
		    name, how, (unsigned long long)r[0], (unsigned long long)r[1], (unsigned long long)r[2], (unsigned long long)r[3],
		    (unsigned long long)r[4], (unsigned long long)r[5], (unsigned long long)r[6]);
}

void TryStartPool(const char* when);
uint64_t __fastcall HkLoad(void* table) {
	const Orig o = Find(table);
	if (!o.load) return 0;   // not reached: the slot only points here for a vtable in the map
	const long long a = Now();
	if (Pre* p = PreOf((uintptr_t)table)) {
		int s = p->state.load();
		if (s == 1 || s == 2) {   // loaded ahead (or still loading): hand its result to the game here, in the game's order
			if (WaitForSingleObject(p->done, 5000) != WAIT_OBJECT_0) {
				Log("dbpool: %s still loading on its thread after 5 s - waiting on", p->name);
				WaitForSingleObject(p->done, INFINITE);
			}
			p->state = 3;
			if (p->rc == 0) *(volatile uint8_t*)((uintptr_t)table + 0x60) = 1;   // as its load does on success
			const long long b = Now();
			if (Rec* rec = RecOf(table)) rec->load += Ms(a, b);
			Log("dbpool: %s handed over on thread %lu: loaded on thread %lu in %.0f ms (started %.0f ms before the game asked); "
			    "the game waited %.0f ms%s", p->name, GetCurrentThreadId(), p->tid, Ms(p->t0, p->t1), Ms(p->t0, a), Ms(a, b),
			    p->rc ? " - its load FAILED (the game's error path follows)" : "");
			LogRows(p->name, (uintptr_t)table, "pooled");
			return p->rc;
		}
	}
	const uint64_t r = o.load(table);
	if (Rec* rec = RecOf(table)) rec->load += Ms(a, Now());
	if (g_poolWant.load()) TryStartPool("after a table load");
	uintptr_t vt = 0;
	if (ReadVt((uintptr_t)table, &vt))
		if (const Known* k = KnownOf(vt)) LogRows(k->name, (uintptr_t)table, "game's order");
	return r;
}
uint64_t __fastcall HkIsLoaded(void* table) {   // vt+0x30 of the three: "not loaded" until its pooled load is handed over
	if (Pre* p = PreOf((uintptr_t)table)) {
		const int s = p->state.load();
		if (s == 1 || s == 2) return 0;
	}
	return g_origIsLoaded(table);
}

DWORD WINAPI PreRun(void* arg) {
	Pre* p = (Pre*)arg;
	p->tid = GetCurrentThreadId();
	Early_NameThread(L"JobWorker_fastload dbpool");
	p->t0 = Now();
	const uint64_t rc = p->load((void*)p->table);
	if (rc == 0) *(volatile uint8_t*)(p->table + 0x60) = 0;   // the game marks it loaded when it reaches it (HkLoad)
	p->rc = rc;
	p->t1 = Now();
	p->state = 2;
	SetEvent(p->done);
	return 0;
}

void Patch(uintptr_t vt);
// Main, the first table list load (type 1, the start of the game init's database step): the three tables are still
// pending (not yet activated: FUN_180ef3afc(4) does that ~0.25 s later), so each gets its own thread now.
void StartPool(uintptr_t mgr) {
	if (g_poolMode != 1 || !g_poolCodeOk || g_poolStarted.exchange(true)) return;
	const uintptr_t* b = nullptr; const uintptr_t* e = nullptr;
	if (!Rd8(mgr + 0x48, (uintptr_t*)&b) || !Rd8(mgr + 0x50, (uintptr_t*)&e) || !b || e < b || e - b > 100000) {
		Log("dbpool: the database manager's pending list not readable - the game's order");
		return;
	}
	int n = 0;
	std::string names;
	for (const uintptr_t* it = b; it < e && n < 3; ++it) {
		uintptr_t t = 0, vt = 0;
		if (!Rd8((uintptr_t)it, &t) || !t || !ReadVt(t, &vt)) continue;
		const Known* k = KnownOf(vt);
		if (!k) continue;
		uintptr_t s30 = 0, s48 = 0, s88 = 0;
		uint8_t loaded = 1;
		Rd8(vt + 0x30, &s30); Rd8(vt + 0x48, &s48); Rd8(vt + 0x88, &s88);
		if (!RdB(t + 0x60, &loaded) || s30 != g_base + kRvaIsLoaded || s48 != g_base + k->load || s88 != g_base + k->post || loaded) {
			Log("dbpool: %s not as expected (slots %llx %llx %llx, loaded %d) - left to the game", k->name, (unsigned long long)(s30 - g_base),
			    (unsigned long long)(s48 - g_base), (unsigned long long)(s88 - g_base), loaded);
			continue;
		}
		Pre& p = g_pre[n];
		p.table = t; p.name = k->name;
		p.load = (MethodFn)s48;
		p.done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!p.done) continue;
		n++;
		names += names.empty() ? "" : ", ";
		names += k->name;
	}
	if (!n) { Log("dbpool: none of the three tables pending - the game's order"); return; }
	g_preCount.store(n, std::memory_order_release);
	// the slots: +0x48 / +0x50 through dbprof's wrappers (Patch), +0x30 through HkIsLoaded
	for (int i = 0; i < n; i++) {
		uintptr_t vt = 0;
		ReadVt(g_pre[i].table, &vt);
		Patch(vt);
		DWORD old = 0;
		if (VirtualProtect((void*)(vt + 0x30), 8, PAGE_READWRITE, &old)) {
			*(volatile uintptr_t*)(vt + 0x30) = (uintptr_t)&HkIsLoaded;
			VirtualProtect((void*)(vt + 0x30), 8, old, &old);
		}
	}
	for (int i = 0; i < n; i++) {
		g_pre[i].state = 1;
		HANDLE th = CreateThread(nullptr, 0, &PreRun, &g_pre[i], 0, nullptr);
		if (th) CloseHandle(th);
		else { g_pre[i].rc = 0; g_pre[i].state = 0; Log("dbpool: no thread for %s - the game's order", g_pre[i].name); }
	}
	Log("dbpool: %d tables loading ahead on their own threads (%s)", n, names.c_str());
}
// Main only: start once the extra-files static is set up (by one of Main's own table loads).
void TryStartPool(const char* when) {
	if (!g_poolWant.load() || GetCurrentThreadId() != g_poolTid) return;
	if (!PartsSetUp()) return;
	g_poolWant = false;
	Log("dbpool: the table-parts setup done on Main (%s) - starting", when);
	StartPool(g_poolMgr);
}
uint64_t __fastcall HkLink(void* table) {
	const Orig o = Find(table);
	if (!o.link) return 0;
	const long long a = Now();
	const uint64_t r = o.link(table);
	if (Rec* rec = RecOf(table)) rec->link += Ms(a, Now());
	return r;
}

// Points one vtable's +0x48 / +0x50 at the wrappers (once per vtable).
void Patch(uintptr_t vt) {
	{
		std::lock_guard<std::mutex> l(g_mu);
		if (g_vts.count(vt)) return;
		g_vts[vt] = Orig{ *(MethodFn*)(vt + 0x48), *(MethodFn*)(vt + 0x50) };
	}
	DWORD old = 0;
	if (!VirtualProtect((void*)(vt + 0x48), 16, PAGE_READWRITE, &old)) return;
	*(volatile uintptr_t*)(vt + 0x48) = (uintptr_t)&HkLoad;
	*(volatile uintptr_t*)(vt + 0x50) = (uintptr_t)&HkLink;
	VirtualProtect((void*)(vt + 0x48), 16, old, &old);
}

uint8_t __fastcall HkLoadList(void* mgr, uintptr_t* list, char a, char b) {
	Dbasync_Gate("a database list load");   // the dubbing load on its worker done first (dbasync.cpp)
	std::vector<Rec> recs;
	int type = -1;
	if (list) {
		const uintptr_t* it = (const uintptr_t*)list[0];
		const uintptr_t* end = (const uintptr_t*)list[1];
		for (; it && it != end && recs.size() < 4096; ++it) {
			uintptr_t vt = 0;
			if (!ReadVt(*it, &vt)) continue;
			Patch(vt);
			Rec r{ *it, {}, 0, 0 };
			NameOf((void*)*it, vt, r.name, sizeof r.name);
			if (type < 0) type = TypeOf((void*)*it, vt);
			recs.push_back(r);
		}
	}
	// dbpool: armed at the game init's first database list load (type 1), started once the parts setup is done on Main;
	// given up when type 4 itself starts without it
	if (type == 1 && g_poolMode == 1 && g_poolCodeOk && !g_poolStarted.load() && !g_poolWant.load()) {
		g_poolMgr = (uintptr_t)mgr;
		g_poolTid = GetCurrentThreadId();
		g_poolWant = true;
		Log("dbpool: armed at the type 0x1 load (table-parts setup %s)", PartsSetUp() ? "done" : "not done yet");
	}
	if (g_poolWant.load()) {
		TryStartPool("at a list load");
		if (type == 4 && g_poolWant.exchange(false)) {
			g_poolStarted = true;   // given up for this run: not armed again by a later type 0x1 load
			Log("dbpool: type 0x4 reached before the table-parts setup ran on Main - the game's order");
		}
	}
	std::vector<Rec>* prev = t_recs;
	t_recs = &recs;
	const long long t0 = Now();
	const uint8_t r = g_origList(mgr, list, a, b);
	const double all = Ms(t0, Now());
	t_recs = prev;
	double load = 0, link = 0;
	for (const Rec& x : recs) { load += x.load; link += x.link; }
	std::sort(recs.begin(), recs.end(), [](const Rec& x, const Rec& y) { return x.load + x.link > y.load + y.link; });
	std::string top;
	for (size_t i = 0; i < recs.size() && i < 10; i++) {
		char one[128];
		snprintf(one, sizeof one, "%s%s %.0f (%.0f / %.0f)", i ? ", " : "", recs[i].name[0] ? recs[i].name : "?",
		         recs[i].load + recs[i].link, recs[i].load, recs[i].link);
		top += one;
	}
	Log("dbprof: type 0x%x on thread %lu: %zu tables, %.0f ms (load %.0f, link %.0f, the rest %.0f); slowest (ms, load / link): %s",
	    type, GetCurrentThreadId(), recs.size(), all, load, link, all - load - link, top.c_str());
	return r;
}

}  // namespace

bool Dbprof_Install(uintptr_t base, void* self) {
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	g_freq = f.QuadPart;
	g_base = base;
	const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
	g_end = base + nt->OptionalHeader.SizeOfImage;
	void* t = (void*)(base + kRvaLoadList);
	if (memcmp(t, kLoadListBytes, sizeof kLoadListBytes)) { Log("dbprof: the database list loader differs - off"); return false; }
	if (MH_CreateHook(t, (void*)&HkLoadList, (void**)&g_origList) != MH_OK || MH_EnableHook(t) != MH_OK) {
		Log("dbprof: hook failed - off");
		return false;
	}
	Log("dbprof: on (each object database table's load and link timed; log only)");
	// dbpool
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	if (FILE* fp = fopen((dir + "fastload_dbpool.txt").c_str(), "r")) {
		if (fscanf(fp, "%d", &g_poolMode) < 1) g_poolMode = 0;
		fclose(fp);
	}
	if (g_poolMode < 0 || g_poolMode > 1) g_poolMode = 0;
	g_poolCodeOk = !memcmp((void*)(base + kRvaIsLoaded), kIsLoadedBytes, sizeof kIsLoadedBytes) &&
	               !memcmp((void*)(base + kKnown[1].load), kLoadItemBytes, sizeof kLoadItemBytes) &&
	               !memcmp((void*)(base + kKnown[0].load), kLoadCompBytes, sizeof kLoadCompBytes);
	g_origIsLoaded = (MethodFn)(base + kRvaIsLoaded);
	Log("dbpool: mode %d (%s)%s", g_poolMode,
	    g_poolMode == 1 ? "CharacterComponent, Item and InventoryPreset loaded on their own threads from the game init's first "
	                      "database load, handed to the game in its order"
	                    : "the game's order; their rows logged for comparison",
	    g_poolCodeOk ? "" : " - the tables' code differs: the game's order");
	return true;
}
