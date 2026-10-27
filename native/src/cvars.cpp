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
	void* con = nullptr;     // set through the console instead (Cvars_Frame): its ICVar
	std::string conOld;      // its value before, as text (put back at gameplay start for loadOnly)
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

// The console fallback (2026-10-26, queue 177: sys_UncachedStreamReads is registered through CryPak's own registrar
// FUN_180e40078, which goes through the console, so the hooks above never saw it and the line was dropped silently).
// From the first frame, on Main: every wanted name the hooks did not see is looked up in the console (gEnv's console
// at WHGame+492d8a8, GetCVar = vtable +0xb8) and set through the ICVar (WHGame's layout: +0x10 GetIVal, +0x28
// GetString, +0x38 Set(int), +0x40 Set(float), +0x48 Set(const char*); FUN_180e40078 and the CXConsoleVariableIntRef
// vtable +3dd0020). A name still missing after 600 frames is logged as not found. user.cfg runs before the first
// frame, so a value set here wins over user.cfg.
constexpr uintptr_t kRvaConsole = 0x492d8a8;
using GetCVarFn = void*(__fastcall*)(void* con, const char* name);
using GetIFn = int(__fastcall*)(void* v);
using GetSFn = const char*(__fastcall*)(void* v);
using SetIFn = void(__fastcall*)(void* v, int i);
using SetFFn = void(__fastcall*)(void* v, float f);
using SetSFn = void(__fastcall*)(void* v, const char* s);
inline void* Slot(void* obj, int off) { return *(void**)(*(uintptr_t*)obj + off); }

__declspec(noinline) void* ConFind(const char* name) {
	__try {
		void* con = *(void**)(g_base + kRvaConsole);
		return con ? ((GetCVarFn)Slot(con, 0xb8))(con, name) : nullptr;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
__declspec(noinline) bool ConText(void* v, char* out, size_t n) {
	__try {
		const char* t = ((GetSFn)Slot(v, 0x28))(v);
		if (!t) return false;
		strncpy(out, t, n - 1);
		out[n - 1] = 0;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
__declspec(noinline) bool ConSet(void* v, double value) {
	__try {
		if (value == (double)(int)value) ((SetIFn)Slot(v, 0x38))(v, (int)value);
		else ((SetFFn)Slot(v, 0x40))(v, (float)value);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
__declspec(noinline) bool ConSetText(void* v, const char* t) {
	__try { ((SetSFn)Slot(v, 0x48))(v, t); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
int g_frames = 0;
bool g_conDone = false;

}  // namespace

void Cvars_Frame() {
	if (g_conDone || !g_base) return;
	const int f = ++g_frames;
	if (f != 1 && f % 60) return;
	std::lock_guard<std::mutex> lk(g_mu);
	bool missing = false;
	for (Want& w : g_want) {
		if (w.var || w.con) continue;
		void* v = ConFind(w.name.c_str());
		if (!v) { missing = true; continue; }
		char before[64] = "?", after[64] = "?";
		ConText(v, before, sizeof before);
		if (!ConSet(v, w.value)) { Log("cvars: %s found in the console but setting it failed", w.name.c_str()); w.con = v; continue; }
		ConText(v, after, sizeof after);
		w.con = v;
		w.conOld = before;
		Log("cvars: %s %s -> %s through the console (frame %d; the registration hooks never saw it)%s%s", w.name.c_str(),
		    before, after, f, w.loadOnly ? " (until gameplay starts)" : "",
		    strtod(after, nullptr) != w.value ? " - the console kept another value (refused?)" : "");
	}
	if (!missing) { g_conDone = true; return; }
	if (f >= 600) {
		for (const Want& w : g_want)
			if (!w.var && !w.con) Log("cvars: %s %g - not found (no such setting registered by frame 600): not set", w.name.c_str(), w.value);
		g_conDone = true;
	}
}

void Cvars_Install(uintptr_t base, void* self) {
	static bool done = false;   // DllMain's early call, then Init's: once
	if (done) { Log("cvars: hooks on since DllMain (%zu settings)", g_want.size()); return; }
	done = true;
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
	g_base = base;   // the console fallback works even when the hooks can't go in
	void* fi = (void*)(base + kRvaInt);
	void* ff = (void*)(base + kRvaFloat);
	if (memcmp(fi, kIntBytes, sizeof kIntBytes) || memcmp(ff, kFloatBytes, sizeof kFloatBytes)) {
		Log("cvars: code check failed - not installed (other game version?)");
		return;
	}
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
		if (!w.loadOnly || w.restored) continue;
		if (w.var) {
			if (w.isFloat) *(float*)w.var = (float)w.def; else *(int*)w.var = (int)w.def;
			// and through the console: a raw write skips the game's change handling (2026-10-27: sys_SleepIfInactiveWH
			// read 1 after the write, yet the game drew at full speed in the background, 100 % GPU, Brave at 1 fps,
			// until "sys_SleepIfInactiveWH 1" was typed in the console)
			void* v = ConFind(w.name.c_str());
			const bool con = v && ConSet(v, w.def);
			w.restored = true;
			Log("cvars: gameplay started, %s back to %g%s", w.name.c_str(), w.def, con ? " (through the console)" : " (raw write only: not in the console)");
		} else if (w.con) {
			w.restored = ConSetText(w.con, w.conOld.c_str());
			Log("cvars: gameplay started, %s back to %s through the console%s", w.name.c_str(), w.conOld.c_str(),
			    w.restored ? "" : " - FAILED");
		}
	}
}

void Cvars_Command(const char* args) {
	(void)args;
	std::lock_guard<std::mutex> lk(g_mu);
	for (const Want& w : g_want) {
		if (!w.var && w.con) {
			char now[64] = "?";
			ConText(w.con, now, sizeof now);
			Log("cvars: %s now %s (set through the console; before %s, ours %g%s)", w.name.c_str(), now, w.conOld.c_str(),
			    w.value, w.loadOnly ? ", until gameplay starts" : "");
			continue;
		}
		if (!w.var) { Log("cvars: %s %g - never registered", w.name.c_str(), w.value); continue; }
		const double now = w.isFloat ? *(float*)w.var : *(int*)w.var;
		Log("cvars: %s now %g (game default %g, ours %g%s)", w.name.c_str(), now, w.def, w.value,
		    w.loadOnly ? ", until gameplay starts" : "");
	}
}
