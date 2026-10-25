// The object database's tables timed one by one: see dbprof.h.
#include "dbprof.h"
#include "dbasync.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <algorithm>
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

struct Rec { uintptr_t table; char name[64]; double load, link; };
thread_local std::vector<Rec>* t_recs = nullptr;   // the list being loaded on this thread

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long a, long long b) { return (double)(b - a) * 1000.0 / g_freq; }

bool InImage(uintptr_t p) { return p >= g_base && p < g_end; }

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

uint64_t __fastcall HkLoad(void* table) {
	const Orig o = Find(table);
	if (!o.load) return 0;   // not reached: the slot only points here for a vtable in the map
	const long long a = Now();
	const uint64_t r = o.load(table);
	if (Rec* rec = RecOf(table)) rec->load += Ms(a, Now());
	return r;
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

bool Dbprof_Install(uintptr_t base) {
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
	return true;
}
