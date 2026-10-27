// menutime: times every step of an inventory / map open (fastload.asi, log only).
//
// WHGame 1.5.6, the open frame (2026-10-26 profile, docs/kcd2_map_open.md): the fader callback FUN_1808bb17c runs the
// Apse setup FUN_181f4a860, whose listeners fill the menus: C_UIMap FUN_180562300 -> map fill FUN_180dc95d4,
// C_UIApseInventory FUN_180561f08 -> SetItems FUN_180c49eac, the character panel FUN_180561d68. Every fill talks to
// Flash through FUN_1807f6d94 (a call by name with an argument list) and GFx Invoke FUN_181c2ea20; the new movies then
// run their first frames in GFx Advance FUN_181ca4d90. The freeze sampler only sees 20 ms samples from 50 ms into a
// frame; this logs exact times per open: "menutime: open #N ...". Off with 0 in fastload_menutime.txt (read at start).
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "MinHook.h"
#include "log.h"
#include "hitch.h"

extern bool (*Hitch_MapvdMasking)();   // hitch.cpp, set by mapvd.cpp: true while a menu has the view distance lowered

namespace {
double g_freq = 0;
inline double Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart * 1000.0 / g_freq; }

enum Slot { kFader, kSetup, kMapL, kMapFill, kInvL, kSetItems, kChar, kCall, kInvoke, kAdvance, kN };
const char* kName[kN] = { "fader", "setup", "map listener", "map fill", "inventory listener", "SetItems", "character panel",
                          "calls to Flash", "GFx invokes", "GFx advance (300 ms)" };
struct Acc { double ms; unsigned n; };
Acc g_acc[kN];
DWORD g_mainTid = 0;
bool g_open = false;      // an open is being recorded
double g_openT0 = 0;      // when its fader callback started
unsigned g_opens = 0;
void* g_openMv[16];       // the movies advanced during the open (to match against the outside-menus list)
int g_nOpenMv = 0;
thread_local int t_callDepth = 0, t_invDepth = 0;

void Add(Slot s, double t0) { g_acc[s].ms += Now() - t0; g_acc[s].n++; }

void Flush() {
	if (!g_open) return;
	g_open = false;
	char b[1024]; int k = snprintf(b, sizeof b, "menutime: open #%u:", ++g_opens);
	for (int i = 0; i < kN; i++)
		if (g_acc[i].n) k += snprintf(b + k, sizeof b - k, " %s %.1f ms%s", kName[i], g_acc[i].ms,
		                              i >= kCall ? (" (" + std::to_string(g_acc[i].n) + ")").c_str() : "");
	for (int i = 0; i < g_nOpenMv && k < (int)sizeof b - 24; i++) k += snprintf(b + k, sizeof b - k, "%s%p", i ? " " : "; movies ", g_openMv[i]);
	g_nOpenMv = 0;
	Log("%s", b);
	memset(g_acc, 0, sizeof g_acc);
}

typedef void(__fastcall* V1)(void*);
typedef void(__fastcall* V3)(void*, void*, uint64_t);
typedef void(__fastcall* V5)(void*, void*, void*, void*, void*);
typedef void*(__fastcall* Inv7)(void*, void*, void*, void*, void*, int, char);
typedef float(__fastcall* Adv)(void*, float, int);
V1 o_fader, o_setup, o_mapFill, o_setItems;
V3 o_mapL, o_invL, o_char;
V5 o_call;
Inv7 o_invoke;
Adv o_adv;

// The screen fader's timeline (FUN_18085d9d8 state machine at +0x38): 1 fade out (FUN_1808bb608, timer +0x170 s),
// 2 black: the callback FUN_1808bb17c builds the menu, then it holds until nothing holds the fader (+0x58 / +0xd8),
// 3 fade in (FUN_1808bb290, +0x170 s again), 0 done (FUN_1808bb70c). Logged once per fade: "menutime: fade ...".
double g_fOut = 0, g_fCb = 0, g_fIn = 0;
float g_fDur = 0;
unsigned g_fBlackFrames = 0;
V1 o_fStart, o_fIn, o_fEnd, o_fUpd;
void __fastcall HkfStart(void* a) { o_fStart(a); g_fOut = Now(); g_fCb = g_fIn = 0; g_fBlackFrames = 0; g_fDur = *(float*)((char*)a + 0x170); if (g_fDur < 0.05f) Hitch_MenuWindow(true); }
void __fastcall HkfIn(void* a) { g_fIn = Now(); o_fIn(a); }
void __fastcall HkfUpd(void* a) { if (*(char*)((char*)a + 0x38) == 2) g_fBlackFrames++; o_fUpd(a); }
void __fastcall HkfEnd(void* a) {
	o_fEnd(a);
	Hitch_MenuWindow(false);
	if (!g_fOut || !g_fCb || !g_fIn) { g_fOut = 0; return; }
	const double t = Now();
	Log("menutime: fade: out %.0f ms, black %.0f ms (%u frames; menu build in it), in %.0f ms, total %.0f ms (duration setting %.2f s)",
	    g_fCb - g_fOut, g_fIn - g_fCb, g_fBlackFrames, t - g_fIn, t - g_fOut, g_fDur);
	g_fOut = 0;
}
void __fastcall HkFader(void* a) {
	Flush();   // an earlier open that never got its 300 ms of advance
	g_open = true; g_openT0 = Now(); g_mainTid = GetCurrentThreadId();
	g_fCb = g_openT0;
	double t = Now(); o_fader(a); Add(kFader, t);
}
#define TIMED1(fn, slot) void __fastcall Hk##fn(void* a) { double t = Now(); o_##fn(a); if (g_open) Add(slot, t); }
#define TIMED3(fn, slot) void __fastcall Hk##fn(void* a, void* b, uint64_t c) { double t = Now(); o_##fn(a, b, c); if (g_open) Add(slot, t); }
TIMED1(setup, kSetup)
TIMED1(mapFill, kMapFill)
TIMED1(setItems, kSetItems)
TIMED3(mapL, kMapL)
TIMED3(invL, kInvL)
TIMED3(char, kChar)

// The function name: b is the CryString's text pointer (FUN_180565144 passes *name). Read under SEH.
void CallName(void* b, char* out, size_t n) {
	out[0] = 0;
	__try {
		const char* s = (const char*)b;
		size_t i = 0;
		for (; i + 1 < n && s[i] >= 32 && s[i] < 127; i++) out[i] = s[i];
		out[i] = 0;
		if (i < 2) {   // not text: maybe a CryString* (pointer to the text)
			s = *(const char* const*)b;
			for (i = 0; i + 1 < n && s[i] >= 32 && s[i] < 127; i++) out[i] = s[i];
			out[i] = 0;
		}
	} __except (1) { strcpy_s(out, n, "?"); }
}
void __fastcall HkCall(void* a, void* b, void* c, void* d, void* e) {
	if (!g_open || t_callDepth) { o_call(a, b, c, d, e); return; }
	t_callDepth++; double t = Now(); o_call(a, b, c, d, e); double ms = Now() - t; t_callDepth--;
	g_acc[kCall].ms += ms; g_acc[kCall].n++;
	if (ms >= 3.0) { char nm[64]; CallName(b, nm, sizeof nm); Log("menutime:   call %s %.1f ms", nm, ms); }
}
void* __fastcall HkInvoke(void* a, void* b, void* c, void* d, void* e, int f, char g) {
	if (!g_open || t_invDepth) return o_invoke(a, b, c, d, e, f, g);
	t_invDepth++; double t = Now(); void* r = o_invoke(a, b, c, d, e, f, g); Add(kInvoke, t); t_invDepth--;
	return r;
}
// Outside menus: the menu engine's advance time per minute (HUD and any movie kept loaded), to see what keepload costs.
double g_outMs = 0, g_outT0 = 0;
unsigned g_outCalls = 0;
constexpr int kMaxMv = 24;
struct Mv { void* m; double ms; unsigned n; };
Mv g_mv[kMaxMv];
int g_nMv = 0;
float __fastcall HkAdv(void* m, float dt, int x) {
	if (!g_open) {
		if (Hitch_MapvdMasking && Hitch_MapvdMasking()) return o_adv(m, dt, x);   // a menu is open: not counted
		double t = Now(); float r = o_adv(m, dt, x); double t1 = Now();
		g_outMs += t1 - t; g_outCalls++;
		int i = 0;
		while (i < g_nMv && g_mv[i].m != m) i++;
		if (i == g_nMv && g_nMv < kMaxMv) g_mv[g_nMv++] = { m, 0, 0 };
		if (i < g_nMv) { g_mv[i].ms += t1 - t; g_mv[i].n++; }
		if (g_outT0 == 0) g_outT0 = t1;
		else if (t1 - g_outT0 >= 60000) {
			const double s = (t1 - g_outT0) / 1000;
			char b[700]; int k = 0;
			for (int j = 0; j < g_nMv && k < (int)sizeof b - 48; j++)
				k += snprintf(b + k, sizeof b - k, " %p %.2f/%.0f", g_mv[j].m, g_mv[j].ms / s, g_mv[j].n / s);
			Log("menutime: menu engine outside menus: %.2f ms a second, %.0f advances a second, %d movies (each: ms/advances a second):%s",
			    g_outMs / s, g_outCalls / s, g_nMv, b);
			g_outMs = 0; g_outCalls = 0; g_outT0 = t1; g_nMv = 0;
		}
		return r;
	}
	if (GetCurrentThreadId() != g_mainTid) return o_adv(m, dt, x);
	if (Now() - g_openT0 > 300) { Flush(); return o_adv(m, dt, x); }
	double t = Now(); float r = o_adv(m, dt, x); Add(kAdvance, t);
	int i = 0;
	while (i < g_nOpenMv && g_openMv[i] != m) i++;
	if (i == g_nOpenMv && g_nOpenMv < 16) g_openMv[g_nOpenMv++] = m;
	return r;
}

struct Hook { uintptr_t rva; uint8_t bytes[10]; void* hk; void** orig; };
}  // namespace

void Menutime_Install(uintptr_t base, HMODULE self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = (double)f.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA(self, path, MAX_PATH);
	std::string file = path;
	file = file.substr(0, file.find_last_of('\\') + 1) + "fastload_menutime.txt";
	if (FILE* fp = fopen(file.c_str(), "r")) {
		int m = 1;
		if (fscanf(fp, "%d", &m) == 1 && m == 0) { fclose(fp); Log("menutime: off (fastload_menutime.txt)"); return; }
		fclose(fp);
	}
	const Hook hooks[] = {
		{ 0x8bb17c, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20 }, (void*)&HkFader, (void**)&o_fader },
		{ 0x1f4a860, { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18 }, (void*)&Hksetup, (void**)&o_setup },
		{ 0x562300, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10 }, (void*)&HkmapL, (void**)&o_mapL },
		{ 0xdc95d4, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xe8 }, (void*)&HkmapFill, (void**)&o_mapFill },
		{ 0x561f08, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10 }, (void*)&HkinvL, (void**)&o_invL },
		{ 0xc49eac, { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x7c, 0x24, 0x18 }, (void*)&HksetItems, (void**)&o_setItems },
		{ 0x561d68, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10 }, (void*)&Hkchar, (void**)&o_char },
		{ 0x7f6d94, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10 }, (void*)&HkCall, (void**)&o_call },
		{ 0x1c2ea20, { 0x4c, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10 }, (void*)&HkInvoke, (void**)&o_invoke },
		{ 0x1ca4d90, { 0x40, 0x55, 0x41, 0x55, 0x48, 0x8d, 0x6c, 0x24, 0xd8, 0x48 }, (void*)&HkAdv, (void**)&o_adv },
		{ 0x8bb608, { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18 }, (void*)&HkfStart, (void**)&o_fStart },
		{ 0x8bb290, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20 }, (void*)&HkfIn, (void**)&o_fIn },
		{ 0x8bb70c, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xe8 }, (void*)&HkfEnd, (void**)&o_fEnd },
		{ 0x85d9d8, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x30 }, (void*)&HkfUpd, (void**)&o_fUpd },
	};
	for (const Hook& h : hooks)
		if (memcmp((void*)(base + h.rva), h.bytes, sizeof h.bytes)) {
			Log("menutime: code check failed at +%llx - not installed (other game version?)", (unsigned long long)h.rva);
			return;
		}
	int ok = 0;
	for (const Hook& h : hooks)
		if (MH_CreateHook((void*)(base + h.rva), h.hk, h.orig) == MH_OK && MH_EnableHook((void*)(base + h.rva)) == MH_OK) ok++;
	Log("menutime: %d of %d hooks on (each inventory / map open logs its steps; 0 in fastload_menutime.txt = off)", ok,
	    (int)(sizeof hooks / sizeof hooks[0]));
}
