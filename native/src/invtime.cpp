// Inventory / map screen timing (WHGame 1.5.6), log only. The screen (wh::guimodule::C_UICharacterBase) opens like this
// (decompiled, inventory/docs/inventory_load.md):
//   0xc47894  open: fades to black, makes the fader (this+0x3e8)
//   0x1f4a260 every frame until the readiness wait exists: "is the object behind this+0x20 ready" (its vfunc +0x80);
//             only then does the screen ask for the readiness wait. wh_sys_ReadinessObserverEnabled 0, its timeout 0.3,
//             wh_ui_ApseUnloadMode 0 and every fade at 0.05 s changed nothing in game (2026-10-18), so this gate is
//             the suspect
//   0xfc3ec0  the readiness wait is made (mask 0x81: graphics streaming + clothing)
//   0x1f4dc00 the wait is over: fade in (wh_ui_ApseOpenFaderFadeInDuration)
// Each step is logged with its time since the open. The per-frame gate logs only when its answer changes.
// Decompiled since: the object is the actor's clothing attachment manager (+0x688, wh::entitymodule::clothing::
// C_ComponentAttachmentManager, made by C_ClothingAttachmentManagerFactory); its vfunc +0x80 (0x181e96650) is
// "state +0x38 == 1", reached once every clothing part in its list has loaded. "invtime 2" (skip): see HkGate.
// Off by default: "invtime 1" checks every prologue byte for byte (all or none), then hooks; "invtime 0" unhooks.
// "invtime 2": on, and the inventory gate is skipped (HkGate); "invtime 1" again turns the skip off.
// "invtime 3": on, gate skipped, and the Apse/shop readiness wait (mask 0x81) passed as 0: the room pops in at once.
// "invtime 4": 2 + the readiness service itself is told "ready" for mask 0x81 while the screen opens (HkReady), so the
// observer fires on its next update. 3 measured no better than 2 (mask 0 did not make the observer fire: in game the fade
// still came ~460 ms after the wait was made), so 4 asks IsReady instead of the observer's argument.
#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "MinHook.h"
#include "log.h"
#include "scan.h"
#include "invtime.h"

namespace {

// every hooked function takes 1-3 integer / pointer arguments and no float (decompiled: open(this), gate(this),
// wait(observer, u32 mask, std::function*), done(lambda*)), so one 4-integer pass-through fits all; the log calls before
// an original may clobber xmm0-3, which none of them reads. Log's own write lands inside the times: ignore under ~5 ms
using Fn4 = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);

struct Site { uint32_t rva; unsigned char bytes[16]; const char* name; void* hook; Fn4* orig; uintptr_t at; };

Fn4 g_open = nullptr, g_gate = nullptr, g_wait = nullptr, g_done = nullptr, g_ready = nullptr;
LARGE_INTEGER g_t0 = {}, g_freq = {};
bool g_created = false, g_on = false;
int g_lastGate = -1;   // the gate's last answer this open (-1: none yet)
bool g_forceReadyMode = false;   // "invtime 4": tell the readiness service "ready" while the screen opens
bool g_forceReady = false;       // armed by HkOpen, cleared by HkDone
bool g_loggedReady = false;
double g_forceUntil = 0;

double Ms() {
	LARGE_INTEGER now; QueryPerformanceCounter(&now);
	return g_t0.QuadPart ? (now.QuadPart - g_t0.QuadPart) * 1000.0 / g_freq.QuadPart : -1.0;
}

uintptr_t __fastcall HkOpen(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
	QueryPerformanceCounter(&g_t0);
	g_lastGate = -1;
	if (g_forceReadyMode) {   // arm "the service is ready" for the whole open, cleared by HkDone (or after 3 s)
		g_forceReady = true;
		g_forceUntil = Ms() + 3000.0;
		g_loggedReady = false;
	}
	Log("invtime open (fade to black) screen %p", (void*)a);
	uintptr_t r = g_open(a, b, c, d);
	Log("invtime open returned at %.1f ms", Ms());
	return r;
}
// what the gate asks (0x1f4a260: RCX = [screen+0x20]; 0x18068c6c4 makes / returns its +0x688; then that object's
// vfunc +0x80): the pointers and vtables, read under SEH (no C++ objects in here)
struct GateObjs { uintptr_t obj = 0, vt = 0, inner = 0, innerVt = 0, fn80 = 0; bool fault = false; };
GateObjs ReadGate(uintptr_t screen) {
	GateObjs g;
	__try {
		g.obj = *(uintptr_t*)(screen + 0x20);
		if (g.obj) { g.vt = *(uintptr_t*)g.obj; g.inner = *(uintptr_t*)(g.obj + 0x688); }
		if (g.inner) { g.innerVt = *(uintptr_t*)g.inner; g.fn80 = *(uintptr_t*)(g.innerVt + 0x80); }
	} __except (1) { g.fault = true; }
	return g;
}
void LogGate(uintptr_t screen) {
	static Module game;
	if (!game.base) game = GetModule("WHGame.dll");
	GateObjs g = ReadGate(screen);
	if (g.fault) { Log("invtime gate object: unreadable"); return; }
	auto rva = [&](uintptr_t x) { return x >= game.base && x < game.base + game.size ? (unsigned long long)(x - game.base) : 0ull; };
	Log("invtime gate object %p %s (vt +0x%llx); its +0x688 %p %s (vt +0x%llx), asked vfunc +0x80 = +0x%llx",
		(void*)g.obj, g.vt ? RttiName(game, g.vt).c_str() : "-", rva(g.vt), (void*)g.inner,
		g.innerVt ? RttiName(game, g.innerVt).c_str() : "-", rva(g.innerVt), rva(g.fn80));
}

// "invtime 2" = on + skip (an A/B test, not a fix): the gate still runs and its real answer is logged, but the screen is told
// "ready" (1) at once, so it makes its readiness wait (mask 0x81, clothing included) at the open. Only for the inventory
// screen's call (C_UICharacterBase update 0x180567d70: its call at 0x567e4b returns to 0x567e50; the gate's only caller in
// 1.5.6), every other caller passes through untouched. With skip on the screen stops asking once it has 1, so the
// gate's real ready time isn't logged: compare open -> fade in, skip off vs on, over 5+ opens each
constexpr uintptr_t kGateCallRet = 0x567e50;
bool g_skip = false;
bool g_fastWait = false;   // "invtime 3": also pass the Apse/shop wait mask 0x81 as 0 (no readiness wait)
uintptr_t g_base = 0;

// 0x8baee0 C_ReadinessService::IsReady(this, u32 mask) -> bool (AL): mov r15d,edx confirms the mask is arg 2.
// "invtime 4" answers 1 for the inventory's mask (0x81: graphics streaming | clothing system) while the screen is opening,
// so C_ReadinessObserver fires on its next update instead of when the room's textures and the dummy's clothes are loaded.
// Scoped by mask + the open window + a 3 s cap, so every other waiter (AI, saves, fast travel) passes through untouched.
uintptr_t __fastcall HkReady(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
	if (g_forceReady && (uint32_t)b == 0x81u) {
		if (Ms() > g_forceUntil) g_forceReady = false;   // never armed forever if HkDone is missed
		else {
			if (!g_loggedReady) { g_loggedReady = true; Log("invtime fast: IsReady(0x81) -> 1 at %.1f ms", Ms()); }
			return 1;
		}
	}
	return g_ready(a, b, c, d);
}

uintptr_t __fastcall HkGate(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
	uintptr_t ret = (uintptr_t)_ReturnAddress();
	uintptr_t r = g_gate(a, b, c, d);
	int v = (r & 0xFF) != 0;
	if (v != g_lastGate) {   // no work on an unchanged frame
		if (g_lastGate == -1) LogGate(a);   // once an open: which object it waits on
		g_lastGate = v;
		Log("invtime gate (object +0x20 ready) = %d at %.1f ms", v, Ms());
	}
	if (g_skip && !v && ret == g_base + kGateCallRet) {
		Log("invtime skip: gate told 1 (its real answer 0) at %.1f ms; its real ready time isn't logged with skip on, compare open -> fade in", Ms());
		return (r & ~(uintptr_t)0xFF) | 1;
	}
	return r;
}
uintptr_t __fastcall HkWait(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
	uint32_t mask = (uint32_t)b;
	if (mask == 0x81) {
		Log("invtime readiness wait made (mask 0x81) at %.1f ms", Ms());
		if (g_fastWait) {   // no wait: the room fades in while the dummy / textures are still streaming
			Log("invtime fast: readiness mask 0x81 -> 0 (no wait) at %.1f ms", Ms());
			b = 0;
		}
	}
	return g_wait(a, b, c, d);
}
uintptr_t __fastcall HkDone(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
	Log("invtime readiness done, fade in at %.1f ms", Ms());
	if (g_forceReady) { g_forceReady = false; Log("invtime fast: readiness forcing off at %.1f ms", Ms()); }
	return g_done(a, b, c, d);
}

Site g_sites[] = {
	{ 0xc47894,  { 0x48,0x89,0x5c,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x48,0x8b,0xf9 }, "open", (void*)&HkOpen, &g_open, 0 },
	{ 0x1f4a260, { 0x48,0x83,0xec,0x28,0x48,0x8b,0x49,0x20,0x48,0x85,0xc9,0x74,0x1e,0xe8,0x52,0x24 }, "gate", (void*)&HkGate, &g_gate, 0 },
	{ 0xfc3ec0,  { 0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x10,0x49,0x89,0x73,0x20,0x57,0x48,0x83,0xec,0x70 }, "wait", (void*)&HkWait, &g_wait, 0 },
	{ 0x1f4dc00, { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x41,0x08,0x48,0x8b }, "done", (void*)&HkDone, &g_done, 0 },
	{ 0x8baee0,  { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57 }, "ready", (void*)&HkReady, &g_ready, 0 },
};
constexpr int kSites = sizeof(g_sites) / sizeof(g_sites[0]);

bool Create() {
	uintptr_t base = (uintptr_t)GetModuleHandleA("WHGame.dll");
	if (!base) { Log("invtime: no WHGame.dll"); return false; }
	g_base = base;
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
	int n = args ? atoi(args) : 0;   // 0 off, 1 on, 2 on + skip (HkGate), 3 on + skip + mask 0x81 passed as 0, 4 = 2 + IsReady forced
	bool want = n != 0;
	bool skip = n >= 2;
	bool fastWait = n == 3;
	bool forceReady = n >= 4;
	if (want && (skip != g_skip || fastWait != g_fastWait || forceReady != g_forceReadyMode)) {
		g_skip = skip;
		g_fastWait = fastWait;
		g_forceReadyMode = forceReady;
		if (!g_forceReadyMode) g_forceReady = false;
		Log("invtime skip %s", g_skip ? "on: the inventory gate is told ready at once (a test: save first)" : "off");
		if (g_fastWait) Log("invtime fast: the readiness wait (mask 0x81) is skipped too, so the room pops in while it streams");
		if (g_forceReadyMode) Log("invtime fast: the readiness service answers ready (mask 0x81) while the screen opens");
	}
	if (want == g_on) { Log("invtime already %s", g_on ? "on" : "off"); return; }
	if (want && !g_created && !(g_created = Create())) return;
	// one batch: the threads are suspended once (MH_ApplyQueued)
	bool queued = true;
	for (auto& s : g_sites)
		if ((want ? MH_QueueEnableHook((void*)s.at) : MH_QueueDisableHook((void*)s.at)) != MH_OK) { Log("invtime: couldn't queue %s", s.name); queued = false; }
	if (!queued) {   // all or none: take the queued ones back, apply nothing
		for (auto& s : g_sites) g_on ? MH_QueueEnableHook((void*)s.at) : MH_QueueDisableHook((void*)s.at);
		Log("invtime: nothing changed, still %s", g_on ? "on" : "off");
		return;
	}
	MH_STATUS st = MH_ApplyQueued();
	if (st != MH_OK) { Log("invtime: %s failed (MinHook %d)", want ? "hooking" : "unhooking", (int)st); return; }
	g_on = want;
	Log("invtime %s", g_on ? "on: open the inventory or map" : "off");
}
