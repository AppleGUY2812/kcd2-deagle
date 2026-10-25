// The GPU adapter query remembered: see gpumemo.h.
#include "gpumemo.h"
#include "early.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <intrin.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr uintptr_t kRvaQuery = 0x5f8c78;   // bool (void* out, int* featureLevel)
const unsigned char kQueryBytes[] = { 0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24 };
constexpr uintptr_t kRvaConsole = 0x492d8a8;   // the console the query reads its two settings from (vtable +0xb8 GetCVar)
constexpr size_t kOut = 0x138;

using QueryFn = bool(__fastcall*)(void* out, int* level);
QueryFn g_orig = nullptr;
uintptr_t g_base = 0;
size_t g_size = 0;
long long g_freq = 1;

struct Entry { int headless, adapter; int level; double ms; unsigned char out[kOut]; };
std::mutex g_mu;
std::vector<Entry> g_kept;
int g_served = 0;
double g_saved = 0;

int CVarInt(const char* name, int def) {
	void* con = *(void**)(g_base + kRvaConsole);
	if (!con) return def;
	using GetCVarFn = void*(__fastcall*)(void*, const char*);
	void* cv = (*(GetCVarFn**)con)[0xb8 / 8](con, name);
	if (!cv) return def;
	using GetIValFn = int(__fastcall*)(void*);
	return (*(GetIValFn**)cv)[0x10 / 8](cv);
}

bool __fastcall HkQuery(void* out, int* level) {
	const int headless = CVarInt("r_HeadlessStartup", 0) != 0, adapter = CVarInt("r_overrideDXGIAdapter", -1);
	char from[96];
	Early_Where((unsigned long long)_ReturnAddress(), g_base, g_size, from, sizeof from);
	{
		std::lock_guard<std::mutex> l(g_mu);
		for (const Entry& e : g_kept) {
			if (e.headless != headless || e.adapter != adapter) continue;
			memcpy(out, e.out, kOut);
			*level = e.level;
			g_served++;
			g_saved += e.ms;
			Log("gpumemo: the GPU adapter query from %s answered from the first run (feature level %x; ~%.0f ms saved, %d calls, "
			    "%.0f ms in all)", from, e.level, e.ms, g_served, g_saved);
			return true;
		}
	}
	LARGE_INTEGER a, b;
	QueryPerformanceCounter(&a);
	const bool ok = g_orig(out, level);
	QueryPerformanceCounter(&b);
	const double ms = (double)(b.QuadPart - a.QuadPart) * 1000.0 / g_freq;
	if (ok) {
		Entry e{ headless, adapter, *level, ms, {} };
		memcpy(e.out, out, kOut);
		std::lock_guard<std::mutex> l(g_mu);
		g_kept.push_back(e);
	}
	Log("gpumemo: the GPU adapter query ran in %.0f ms (from %s, headless %d, adapter override %d, feature level %x%s)", ms, from,
	    headless, adapter, ok ? *level : 0, ok ? "; later calls with these settings get this answer" : ", failed: not kept");
	return ok;
}

}  // namespace

void Gpumemo_Install(uintptr_t base, size_t size, void* self) {
	g_base = base;
	g_size = size;
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	g_freq = f.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	int mode = 1;
	if (FILE* fp = fopen((dir + "fastload_gpumemo.txt").c_str(), "r")) {
		if (fscanf(fp, "%d", &mode) != 1) mode = 1;
		fclose(fp);
	}
	if (!mode) { Log("gpumemo: off (fastload_gpumemo.txt 0)"); return; }
	void* t = (void*)(base + kRvaQuery);
	if (memcmp(t, kQueryBytes, sizeof kQueryBytes)) { Log("gpumemo: the GPU adapter query differs (another game version?) - off"); return; }
	MH_STATUS st = MH_CreateHook(t, (void*)&HkQuery, (void**)&g_orig);
	if (st == MH_OK) st = MH_EnableHook(t);
	if (st != MH_OK) { Log("gpumemo: hook failed (MinHook %d) - off", (int)st); return; }
	Log("gpumemo: on (the GPU adapter query's first answer kept for later calls with the same settings; fastload_gpumemo.txt)");
}
