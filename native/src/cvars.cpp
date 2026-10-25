// Load-time game settings: see cvars.h.
#include "cvars.h"
#include "log.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "MinHook.h"

namespace {

constexpr uintptr_t kRvaInt = 0x23d8168, kRvaFloat = 0x23d8b0c;
const unsigned char kIntBytes[] = { 0x48,0x83,0xec,0x48,0x48,0x8b,0x44,0x24,0x78 };
const unsigned char kFloatBytes[] = { 0x48,0x83,0xec,0x48,0x4c,0x8b,0xd9,0x33,0xc0 };

using RegIntFn = void(__fastcall*)(const char* name, int* var, int def, int flags, const char* help, void* cb);
using RegFloatFn = void*(__fastcall*)(const char* name, float* var, float def, int flags, const char* help, void* cb);
RegIntFn g_origInt = nullptr;
RegFloatFn g_origFloat = nullptr;
// CXConsole::RegisterInt (console vtable +0x18, FUN_180b95054): settings the console owns (no variable pointer), e.g.
// wh_sys_SkipStartupVideoWhenPossible. Returns the CXConsoleVariableInt, whose value sits at +0x48.
constexpr uintptr_t kRvaConInt = 0xb95054, kRvaConIntVt = 0x3a7f0b8;   // CXConsoleVariableInt::vftable
const unsigned char kConIntBytes[] = { 0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x57,0x41,0x56,0x41 };
using ConIntFn = void*(__fastcall*)(void* con, const char* name, int value, int flags, const char* help, void* cb);
ConIntFn g_origConInt = nullptr;
uintptr_t g_base = 0;

struct Want {
	std::string name;
	double value;
	bool loadOnly;
	void* var = nullptr;     // set at registration
	bool isFloat = false;
	double def = 0;          // the game's default, put back at gameplay start for loadOnly
	bool restored = false;
};
std::vector<Want> g_want;
std::mutex g_mu;

// 2026-10-23 21:36 boot measured the others and dropped them: r_TexturesStreamingMaxRequestedMB 32 / Jobs 128 made
// the save step 20.4 s (15.3 before: more texture data loaded before gameplay starts); r_ShadersAsyncMaxThreads 4
// did nothing (the ShaderCompile threads sat idle; the shader parsing runs on RenderLoadingThread);
// r_PSOCompilationWorkerThreads 6: pre-save step 10.0 s vs 6.9 (not separated from the low-RAM boot).
// The start-up intro (logo videos) off, user 2026-10-24: sys_intromoviesduringinit 0 starts no intro movie (SystemInit,
// "Destroying the splash"); wh_sys_SkipStartupVideoWhenPossible 1 stops one at the end of the game's init instead of
// waiting for it (FUN_1819b6bb0, CIntroMovieRenderer::WaitForCompletion: 5.6-5.9 s on Main in boots 46-77).
const char* kDefaults =
	"sys_SleepIfInactiveWH 0 load\n"
	"sys_intromoviesduringinit 0\n"
	"wh_sys_SkipStartupVideoWhenPossible 1\n";

void Parse(const char* text) {
	const char* p = text;
	while (*p) {
		const char* e = strchr(p, '\n');
		std::string line = e ? std::string(p, e) : std::string(p);
		p = e ? e + 1 : p + strlen(p);
		if (line.empty() || line[0] == '#') continue;
		char name[128] = {}, flag[16] = {};
		double v = 0;
		const int n = sscanf(line.c_str(), "%127s %lf %15s", name, &v, flag);
		if (n < 2) continue;
		Want w;
		w.name = name; w.value = v; w.loadOnly = n == 3 && !_stricmp(flag, "load");
		g_want.push_back(w);
	}
}

Want* Find(const char* name) {
	if (!name) return nullptr;
	for (Want& w : g_want) if (!_stricmp(w.name.c_str(), name)) return &w;
	return nullptr;
}

void __fastcall HkInt(const char* name, int* var, int def, int flags, const char* help, void* cb) {
	g_origInt(name, var, def, flags, help, cb);
	std::lock_guard<std::mutex> lk(g_mu);
	if (Want* w = Find(name)) {
		if (var) {
			w->var = var; w->isFloat = false; w->def = def;
			*var = (int)w->value;
			Log("cvars: %s %d -> %d%s", name, def, *var, w->loadOnly ? " (until gameplay starts)" : "");
		}
	}
}

void* __fastcall HkFloat(const char* name, float* var, float def, int flags, const char* help, void* cb) {
	void* r = g_origFloat(name, var, def, flags, help, cb);
	std::lock_guard<std::mutex> lk(g_mu);
	if (Want* w = Find(name)) {
		if (var) {
			w->var = var; w->isFloat = true; w->def = def;
			*var = (float)w->value;
			Log("cvars: %s %.2f -> %.2f%s", name, def, *var, w->loadOnly ? " (until gameplay starts)" : "");
		}
	}
	return r;
}

// the returned variable, checked: a CXConsoleVariableInt (its vtable), else nullptr
__declspec(noinline) int* ConIntValue(void* v) {
	__try { return v && *(uintptr_t*)v == g_base + kRvaConIntVt ? (int*)((uint8_t*)v + 0x48) : nullptr; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

void* __fastcall HkConInt(void* con, const char* name, int value, int flags, const char* help, void* cb) {
	int use = value;
	bool want = false;
	{
		std::lock_guard<std::mutex> lk(g_mu);
		if (Want* w = Find(name)) { use = (int)w->value; want = true; }
	}
	void* r = g_origConInt(con, name, use, flags, help, cb);
	if (!want) return r;
	std::lock_guard<std::mutex> lk(g_mu);
	if (Want* w = Find(name)) {
		if (int* var = ConIntValue(r)) {
			w->var = var; w->isFloat = false; w->def = value;
			*var = use;
			Log("cvars: %s %d -> %d%s (console-owned)", name, value, *var, w->loadOnly ? " (until gameplay starts)" : "");
		} else Log("cvars: %s registered by the console but not as a plain int: left at its value", name);
	}
	return r;
}

}  // namespace

void Cvars_Install(uintptr_t base, void* self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string file = path;
	file = file.substr(0, file.find_last_of('\\') + 1) + "fastload_cvars.txt";
	std::string text;
	if (FILE* f = fopen(file.c_str(), "r")) {
		char buf[4096]; size_t n;
		while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
		fclose(f);
	} else {
		text = kDefaults;
		if (FILE* o = fopen(file.c_str(), "w")) {
			fprintf(o, "# fastload plugin: game settings set at registration (native/src/cvars.h). <name> <value> [load]\n"
			           "# load = only until gameplay starts. Delete a line to keep the game's own value.\n%s", kDefaults);
			fclose(o);
		}
	}
	Parse(text.c_str());
	if (g_want.empty()) { Log("cvars: nothing to set"); return; }
	void* fi = (void*)(base + kRvaInt);
	void* ff = (void*)(base + kRvaFloat);
	if (memcmp(fi, kIntBytes, sizeof kIntBytes) || memcmp(ff, kFloatBytes, sizeof kFloatBytes)) {
		Log("cvars: code check failed - not installed (other game version?)");
		return;
	}
	g_base = base;
	const bool a = MH_CreateHook(fi, (void*)&HkInt, (void**)&g_origInt) == MH_OK && MH_EnableHook(fi) == MH_OK;
	const bool b = MH_CreateHook(ff, (void*)&HkFloat, (void**)&g_origFloat) == MH_OK && MH_EnableHook(ff) == MH_OK;
	void* fc = (void*)(base + kRvaConInt);
	const bool c = !memcmp(fc, kConIntBytes, sizeof kConIntBytes) &&
	               MH_CreateHook(fc, (void*)&HkConInt, (void**)&g_origConInt) == MH_OK && MH_EnableHook(fc) == MH_OK;
	Log("cvars: %zu settings from %s, hooks int %s float %s console int %s", g_want.size(), file.c_str(), a ? "on" : "FAILED",
	    b ? "on" : "FAILED", c ? "on" : "FAILED");
}

void Cvars_OnLoaded() {
	std::lock_guard<std::mutex> lk(g_mu);
	for (Want& w : g_want) {
		if (!w.loadOnly || w.restored || !w.var) continue;
		if (w.isFloat) *(float*)w.var = (float)w.def; else *(int*)w.var = (int)w.def;
		w.restored = true;
		Log("cvars: gameplay started, %s back to %g", w.name.c_str(), w.def);
	}
}

void Cvars_Command(const char* args) {
	(void)args;
	std::lock_guard<std::mutex> lk(g_mu);
	for (const Want& w : g_want) {
		if (!w.var) { Log("cvars: %s %g - never registered", w.name.c_str(), w.value); continue; }
		const double now = w.isFloat ? *(float*)w.var : *(int*)w.var;
		Log("cvars: %s now %g (game default %g, ours %g%s)", w.name.c_str(), now, w.def, w.value,
		    w.loadOnly ? ", until gameplay starts" : "");
	}
}
