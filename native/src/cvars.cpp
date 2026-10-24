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
const char* kDefaults =
	"sys_SleepIfInactiveWH 0 load\n";

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
	const bool a = MH_CreateHook(fi, (void*)&HkInt, (void**)&g_origInt) == MH_OK && MH_EnableHook(fi) == MH_OK;
	const bool b = MH_CreateHook(ff, (void*)&HkFloat, (void**)&g_origFloat) == MH_OK && MH_EnableHook(ff) == MH_OK;
	Log("cvars: %zu settings from %s, hooks int %s float %s", g_want.size(), file.c_str(), a ? "on" : "FAILED",
	    b ? "on" : "FAILED");
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
