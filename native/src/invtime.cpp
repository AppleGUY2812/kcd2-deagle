// Inventory / map screen timing (WHGame 1.5.6), log only. The screen (wh::guimodule::C_UICharacterBase) opens like this
// (decompiled, docs/inventory_load.md):
//   0xc47894  open: fades to black, makes the fader (this+0x3e8)
//   0x1f4a260 every frame until the readiness wait exists: "is the object behind this+0x20 ready" (its vfunc +0x80);
//             only then does the screen ask for the readiness wait. wh_sys_ReadinessObserverEnabled 0, its timeout 0.3,
//             wh_ui_ApseUnloadMode 0 and every fade at 0.05 s changed nothing in game (2026-10-18), so this gate is
//             the suspect
//   0xfc3ec0  the readiness wait is made (mask 0x81: graphics streaming + clothing)
//   0x1f4dc00 the wait is over: fade in (wh_ui_ApseOpenFaderFadeInDuration)
// Each step is logged with its time since the open. The per-frame gate logs only when its answer changes.
// Off by default: "invtime 1" checks every prologue byte for byte (all or none), then hooks; "invtime 0" unhooks.
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "MinHook.h"
#include "log.h"
#include "invtime.h"

namespace {

// every hooked function takes 1-3 integer / pointer arguments and no float (decompiled: open(this), gate(this),
// wait(observer, u32 mask, std::function*), done(lambda*)), so one 4-integer pass-through fits all; the log calls before
// an original may clobber xmm0-3, which none of them reads. Log's own write lands inside the times: ignore under ~5 ms
using Fn4 = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);

struct Site { uint32_t rva; unsigned char bytes[16]; const char* name; void* hook; Fn4* orig; uintptr_t at; };

Fn4 g_open = nullptr, g_gate = nullptr, g_wait = nullptr, g_done = nullptr;
LARGE_INTEGER g_t0 = {}, g_freq = {};
bool g_created = false, g_on = false;
int g_lastGate = -1;   // the gate's last answer this open (-1: none yet)

double Ms() {
	LARGE_INTEGER now; QueryPerformanceCounter(&now);
	return g_t0.QuadPart ? (now.QuadPart - g_t0.QuadPart) * 1000.0 / g_freq.QuadPart : -1.0;
}

uintptr_t __fastcall HkOpen(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
	QueryPerformanceCounter(&g_t0);
	g_lastGate = -1;
	Log("invtime open (fade to black) screen %p", (void*)a);
	uintptr_t r = g_open(a, b, c, d);
	Log("invtime open returned at %.1f ms", Ms());
	return r;
}
uintptr_t __fastcall HkGate(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
	uintptr_t r = g_gate(a, b, c, d);
	int v = (r & 0xFF) != 0;
	if (v != g_lastGate) {   // no work on an unchanged frame
		g_lastGate = v;
		Log("invtime gate (object +0x20 ready) = %d at %.1f ms", v, Ms());
	}
	return r;
}
uintptr_t __fastcall HkWait(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
	if ((b & 0xFFFFFFFF) == 0x81) Log("invtime readiness wait made (mask 0x81) at %.1f ms", Ms());
	return g_wait(a, b, c, d);
}
uintptr_t __fastcall HkDone(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
	Log("invtime readiness done, fade in at %.1f ms", Ms());
	return g_done(a, b, c, d);
}

Site g_sites[] = {
	{ 0xc47894,  { 0x48,0x89,0x5c,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x48,0x8b,0xf9 }, "open", (void*)&HkOpen, &g_open, 0 },
	{ 0x1f4a260, { 0x48,0x83,0xec,0x28,0x48,0x8b,0x49,0x20,0x48,0x85,0xc9,0x74,0x1e,0xe8,0x52,0x24 }, "gate", (void*)&HkGate, &g_gate, 0 },
	{ 0xfc3ec0,  { 0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x10,0x49,0x89,0x73,0x20,0x57,0x48,0x83,0xec,0x70 }, "wait", (void*)&HkWait, &g_wait, 0 },
	{ 0x1f4dc00, { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x41,0x08,0x48,0x8b }, "done", (void*)&HkDone, &g_done, 0 },
};
constexpr int kSites = sizeof(g_sites) / sizeof(g_sites[0]);

bool Create() {
	uintptr_t base = (uintptr_t)GetModuleHandleA("WHGame.dll");
	if (!base) { Log("invtime: no WHGame.dll"); return false; }
	for (auto& s : g_sites) {   // all or none: every byte as read from 1.5.6's WHGame.dll
		s.at = base + s.rva;
		if (memcmp((void*)s.at, s.bytes, sizeof(s.bytes)) != 0) {
			Log("invtime: %s at +0x%x isn't the 1.5.6 code: nothing hooked", s.name, s.rva);
			return false;
		}
	}
	for (int i = 0; i < kSites; i++) {
		if (MH_CreateHook((void*)g_sites[i].at, g_sites[i].hook, (void**)g_sites[i].orig) != MH_OK) {
			Log("invtime: couldn't hook %s: nothing hooked", g_sites[i].name);
			for (int j = 0; j < i; j++) MH_RemoveHook((void*)g_sites[j].at);
			return false;
		}
	}
	QueryPerformanceFrequency(&g_freq);
	return true;
}

}  // namespace

void Invtime_Command(const char* args) {
	bool want = args && atoi(args) != 0;
	if (want == g_on) { Log("invtime already %s", g_on ? "on" : "off"); return; }
	if (want && !g_created && !(g_created = Create())) return;
	// one batch: the threads are suspended once (MH_ApplyQueued)
	for (auto& s : g_sites)
		if ((want ? MH_QueueEnableHook((void*)s.at) : MH_QueueDisableHook((void*)s.at)) != MH_OK) Log("invtime: couldn't queue %s", s.name);
	MH_STATUS st = MH_ApplyQueued();
	if (st != MH_OK) { Log("invtime: %s failed (MinHook %d)", want ? "hooking" : "unhooking", (int)st); return; }
	g_on = want;
	Log("invtime %s", g_on ? "on: open the inventory or map" : "off");
}
