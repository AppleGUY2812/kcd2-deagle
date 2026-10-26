// Save writers during the load: see savewatch.h. WHGame 1.5.6 (re/out/settlectr.md): CPlatformOS_PC vt+0x88
// SaveGetWriter FUN_182440d40(this, shared_ptr* out, const char* file, int user) counts DAT_185570258 up; the writer's
// destructors FUN_182435040 / FUN_182435928 count it down; FUN_1817ea370 is the readiness poll ("a writer is alive").
#include "savewatch.h"
#include "log.h"
#include <windows.h>
#include <intrin.h>
#include <MinHook.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace {
uintptr_t g_base = 0;
LARGE_INTEGER g_freq{};
double NowMs() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart * 1000.0 / (double)g_freq.QuadPart; }

using FactoryFn = uintptr_t(__fastcall*)(uintptr_t self, uintptr_t out, uintptr_t file, uintptr_t user);
using DtorFn = uintptr_t(__fastcall*)(uintptr_t w, uintptr_t flags);
using BusyFn = uintptr_t(__fastcall*)();
FactoryFn oFactory = nullptr; DtorFn oDtor = nullptr, oDelDtor = nullptr; BusyFn oBusy = nullptr;

struct Live { uintptr_t w; double t; char file[160]; };
Live g_live[32]; int g_nLive = 0; std::mutex g_mu;
std::atomic<long> g_polls{0};
double g_busySince = -1; long g_busyPolls = 0;

void CopyName(uintptr_t p, char* out, size_t n) {
	out[0] = 0;
	__try { const char* s = (const char*)p; size_t i = 0; for (; i + 1 < n && s[i]; i++) out[i] = (s[i] >= 32 && s[i] < 127) ? s[i] : '?'; out[i] = 0; }
	__except (EXCEPTION_EXECUTE_HANDLER) { strcpy_s(out, n, "(unreadable)"); }
}
uintptr_t ReadOut(uintptr_t out) { uintptr_t w = 0; __try { w = *(uintptr_t*)out; } __except (EXCEPTION_EXECUTE_HANDLER) {} return w; }
uintptr_t Rel(void* ra) { uintptr_t a = (uintptr_t)ra; return a >= g_base ? a - g_base : a; }

uintptr_t __fastcall Factory(uintptr_t self, uintptr_t out, uintptr_t file, uintptr_t user) {
	void* ra = _ReturnAddress();
	uintptr_t r = oFactory(self, out, file, user);
	Live l{}; l.w = ReadOut(out); l.t = NowMs(); CopyName(file, l.file, sizeof l.file);
	{ std::lock_guard<std::mutex> lk(g_mu); if (g_nLive < 32) g_live[g_nLive++] = l; }
	Log("savewatch: writer %p for '%s' (user %d, thread %lu, from WHGame+%llx)", (void*)l.w, l.file, (int)user, GetCurrentThreadId(),
	    (unsigned long long)Rel(ra));
	return r;
}
void Died(uintptr_t w, const char* how) {
	double now = NowMs();
	std::lock_guard<std::mutex> lk(g_mu);
	for (int i = 0; i < g_nLive; i++) if (g_live[i].w == w) {
		Log("savewatch: writer %p for '%s' gone after %.0f ms (%s, thread %lu)", (void*)w, g_live[i].file, now - g_live[i].t, how, GetCurrentThreadId());
		g_live[i] = g_live[--g_nLive];
		return;
	}
	Log("savewatch: writer %p gone (%s; not seen made)", (void*)w, how);
}
uintptr_t __fastcall Dtor(uintptr_t w, uintptr_t f) { Died(w, "dtor"); return oDtor(w, f); }
uintptr_t __fastcall DelDtor(uintptr_t w, uintptr_t f) { Died(w, "deleting dtor"); return oDelDtor(w, f); }
uintptr_t __fastcall Busy() {
	void* ra = _ReturnAddress();
	uintptr_t r = oBusy();
	bool busy = (r & 0xff) != 0;
	g_polls++;
	double now = NowMs();
	if (busy && g_busySince < 0) { g_busySince = now; g_busyPolls = 0; Log("savewatch: the readiness poll says a writer is alive (from WHGame+%llx)", (unsigned long long)Rel(ra)); }
	if (busy) g_busyPolls++;
	if (!busy && g_busySince >= 0) {
		Log("savewatch: writers done: the poll said busy for %.0f ms (%ld polls, last from WHGame+%llx)", now - g_busySince, g_busyPolls, (unsigned long long)Rel(ra));
		g_busySince = -1;
	}
	return r;
}

struct H { const char* name; uintptr_t rva; unsigned char bytes[8]; void* wrap; void** orig; };
}

void Savewatch_Install(uintptr_t base, void* self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path; dir = dir.substr(0, dir.find_last_of('\\') + 1);
	int mode = 0;
	if (FILE* fp = fopen((dir + "fastload_savewatch.txt").c_str(), "r")) { if (fscanf(fp, "%d", &mode) != 1) mode = 0; fclose(fp); }
	if (mode != 1) { Log("savewatch: off (fastload_savewatch.txt 1 logs the save writers during the load)"); return; }
	g_base = base; QueryPerformanceFrequency(&g_freq);
	H h[] = {
		{ "SaveGetWriter", 0x2440d40, { 0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48 }, (void*)&Factory, (void**)&oFactory },
		{ "writer dtor", 0x2435040, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b }, (void*)&Dtor, (void**)&oDtor },
		{ "writer deleting dtor", 0x2435928, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8d }, (void*)&DelDtor, (void**)&oDelDtor },
		{ "readiness poll", 0x17ea370, { 0x48, 0x83, 0xec, 0x28, 0x48, 0x8b, 0x0d, 0x45 }, (void*)&Busy, (void**)&oBusy },
	};
	int on = 0; std::string off;
	for (auto& x : h) {
		void* t = (void*)(base + x.rva);
		if (memcmp(t, x.bytes, sizeof x.bytes) || MH_CreateHook(t, x.wrap, x.orig) != MH_OK || MH_EnableHook(t) != MH_OK) { off += off.empty() ? "" : ", "; off += x.name; continue; }
		on++;
	}
	Log("savewatch: %d of 4 hooks on%s%s", on, off.empty() ? "" : "; FAILED: ", off.c_str());
}
