// The render thread's start-up work without Main waiting for it: see rtsync.h.
#include "rtsync.h"
#include "log.h"
#include "early.h"
#include "MinHook.h"
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// WHGame 1.5.6
constexpr uintptr_t kRvaFlush = 0x7a2138;      // void (CRenderThread*): flush and wait unless on the render thread
constexpr uintptr_t kRvaQualityRet = 0x24f238b;   // FUN_1824f2370's return from FUN_181de6260 (-> FUN_181de6270, tail jump)
constexpr uintptr_t kRvaTail = 0x1de6367;      // FUN_181de6270's tail jump to the flush
const unsigned char kFlushBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8 };   // push rbx; sub rsp,20; mov rbx,rcx; call
const unsigned char kRetBytes[] = { 0x48,0x8b,0x0d };   // mov rcx, [rip+..] after the call
const unsigned char kTailBytes[] = { 0xe9,0xcc,0xbd,0x9b,0xfe };   // jmp 0x1807a2138

// The hand-over (FUN_1809a6190(rt, 0)): waits until the render thread has finished its last batch, gives it the queued
// commands (swaps the two command buffers) and wakes it. The flush is two of these: the second one waits for the batch.
constexpr uintptr_t kRvaSubmit = 0x9a6190;
constexpr uintptr_t kRvaFlushCall1 = 0x7a2155, kRvaFlushCall2 = 0x7a215f;   // the flush's two calls of it
const unsigned char kSubmitBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x83,0x39,0x00 };

// The shader manager's init (FUN_180da002c): flush and wait, then close %ENGINE%/Shaders.pak and shadercache.pak
constexpr uintptr_t kRvaShaderRet = 0xda0383;  // its return from the flush
constexpr uintptr_t kRvaClose1Ret = 0xda03a0;  // its return from ClosePack("%ENGINE%/Shaders.pak")
constexpr uintptr_t kRvaClose2Ret = 0xda03bd;  // its return from ClosePack("%ENGINE%/shadercache.pak")
constexpr uintptr_t kRvaCryPak = 0x492d850;    // the CryPak object pointer (gEnv's ICryPak)
constexpr size_t kCloseSlot = 0x88 / 8;        // ICryPak::ClosePack
const unsigned char kShaderBytes[] = { 0xe8,0xb5,0x1d,0xa0,0xff, 0x48,0x8b,0x0d,0xc6,0xd4,0xb8,0x03 };   // call flush; mov rcx,[CryPak]
const unsigned char kCallSlotBytes[] = { 0xff,0x90,0x88,0x00,0x00,0x00 };   // call [rax+0x88], before each close's return

// The renderer's system-resource setup (InitSystemResources, renderer vtable slot 0x1a0, FUN_1807a35e8, ~5 s after
// launch): it saves CTexture's precache flag (DAT_18515c3a8) and clears it, runs the Main part, then FUN_181de4890
// queues the render thread's part (a lambda: CreateSystemTargets FUN_180fd2db4 and the rest, FUN_181deb330) and
// flushes and waits (its tail jump to the flush), then puts the flag back. Slot 0x1a8 (FUN_181447938), called right
// after, queues a second lambda (FUN_181683268) and flushes and waits too.
constexpr uintptr_t kRvaIsr = 0x7a35e8;
const unsigned char kIsrBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x80,0xb9,0xf0,0x00,0x00,0x00,0x00 };
constexpr uintptr_t kRvaIsrSave = 0x7a3617;    // mov dil, [flag] (40 8a 3d disp32)
constexpr uintptr_t kRvaIsrRet = 0x7a3654;     // its return from FUN_181de4890's flush: mov [flag], dil (40 88 3d disp32)
constexpr uintptr_t kRvaLam2Ret = 0x144796a;   // FUN_181447938's return from FUN_181de6850's flush
const unsigned char kLam2RetBytes[] = { 0x48,0x83,0xc4,0x20,0x5b,0xc3 };   // add rsp,20; pop rbx; ret
constexpr uintptr_t kRvaLam1Slot = 0x3df0430;  // the setup lambda's std::function vftable (0x3df0420), slot 2 _Do_call
constexpr uintptr_t kRvaLam1Call = 0x1ded7d0;  // jmp FUN_181deb330
constexpr uintptr_t kRvaLam2Slot = 0x3df03f8;  // the second lambda's vftable (0x3df03e8), slot 2
constexpr uintptr_t kRvaLam2Call = 0x1ded870;  // mov rcx,[renderer]; jmp [rax+0xaf0] (FUN_181683268)
constexpr uintptr_t kRvaRendererVt = 0x3dfc008;   // CD3D9Renderer's vtable: 381 slots, the setup is 52, the second lambda's 53
constexpr int kRendererSlots = 381, kIsrSlot = 52, kLam2Slot = 53;
constexpr uintptr_t kRvaForName = 0x9b8ec4;    // CTexture::ForName: reads the flag (postpone the load or load now)
constexpr uintptr_t kRvaForNameRead1 = 0x9b8f0b, kRvaForNameRead2 = 0x9b8f59;   // its reads (8a 05 disp32)
const unsigned char kForNameBytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08 };   // mov rax,rsp; mov [rax+8],rbx

using FlushFn = void(__fastcall*)(void* rt);
using SubmitFn = void(__fastcall*)(void* rt, char frame);
using CloseFn = bool(__fastcall*)(void* pak, const char* name, uintptr_t a, uintptr_t b);
using Fn4 = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
FlushFn g_orig = nullptr;
SubmitFn g_submit = nullptr;
uintptr_t g_ret = 0, g_base = 0;
std::atomic<bool> g_on{ false };
std::atomic<long long> g_skipped{ 0 }, g_flushes{ 0 };
bool g_reported = false;
bool g_shaderOn = false;   // mode 1 or 3: the shader manager's wait too
bool g_isrOn = false;      // mode 3: the system-resource setup's two waits too

// The shader manager's wait: the commands handed to the render thread, Main goes on while it loads the shader caches.
// The two pak closes after it are held (the render thread reads those paks) and run on Main right after its next real
// flush and wait, when every command queued before it is done. Main only, so plain variables.
void* g_cryPak = nullptr;
void** g_closeSlot = nullptr;
CloseFn g_closeOrig = nullptr;
const char* g_held[2] = {};
int g_heldN = 0;
DWORD g_shaderTid = 0;
void* g_shaderRt = nullptr;
long long g_skipT = 0, g_freq = 1;
bool g_shaderSkipped = false, g_closesDone = false;
// Main's real waits until the first frame (all modes), to see whether a skipped wait only moves to the next one
DWORD g_mainTid = 0;
long long g_waitTicks = 0, g_waits = 0, g_firstAfterTicks = -1, g_firstAfterAt = 0, g_submitTicks = 0, g_submits = 0;
uintptr_t g_firstAfterRet = 0;

// The system-resource setup handed over. The flag is cleared before the setup (so the game saves 0 and puts 0 back)
// and put back to its real value by the render thread when its part ends; until then Main waits at its first texture
// load (ForName reads the flag), and other threads see 0 as during the game's own wait.
// From the hand-over until both lambdas have run, Main also waits at its first call of any renderer method (all
// slots of the renderer's vtable but the two running now, through generated thunks that keep every argument).
volatile uint8_t* g_flag = nullptr;
uint8_t g_savedFlag = 0;
void** g_lam1Slot = nullptr;
void** g_lam2Slot = nullptr;
Fn4 g_lam1Orig = nullptr, g_lam2Orig = nullptr, g_isrOrig = nullptr, g_forNameOrig = nullptr;
std::atomic<bool> g_flagHeld{ false };   // the flag stays 0 until the setup's lambda has run
std::atomic<bool> g_gateOn{ false };     // Main waits at texture loads and renderer calls until both lambdas have run
std::atomic<bool> g_lam2Queued{ false };
HANDLE g_setupDone = nullptr;   // manual-reset: both lambdas ran (or the setup ended without them)
SRWLOCK g_vtLock = SRWLOCK_INIT;
void** g_rendVt = nullptr;
void* g_rendOrig[kRendererSlots] = {};
uint8_t* g_thunks = nullptr;
bool g_vtPatched = false;
bool g_isrDone = false, g_inIsr = false, g_isrSkipped = false, g_isrSubmitted = false, g_isrGaveUp = false;
void* g_isrRt = nullptr;
long long g_isrT = 0;   // Main: when the setup's wait would have started
std::atomic<long long> g_lamT0{ 0 }, g_lamT1{ 0 }, g_lam2T1{ 0 };  // render thread: the lambdas' start and ends
long long g_gateTicks = 0, g_gates = 0, g_isrFirstAfterTicks = -1, g_isrFirstAfterAt = 0, g_firstGateAt = 0;
uintptr_t g_isrFirstAfterRet = 0;
char g_gateWhat[64] = "";
constexpr size_t kThunkSize = 0x90;

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline uintptr_t RipTarget(uintptr_t ins, size_t dispAt, size_t len) { return ins + len + (intptr_t)*(const int32_t*)(ins + dispAt); }

bool SetPtr(void** slot, void* fn) {
	DWORD old = 0;
	if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
	InterlockedExchangePointer(slot, fn);
	VirtualProtect(slot, sizeof(void*), old, &old);
	return true;
}

bool __fastcall HkClose(void* pak, const char* name, uintptr_t a, uintptr_t b) {
	const uintptr_t r = (uintptr_t)_ReturnAddress();
	if (pak == g_cryPak && !g_closesDone && g_heldN < 2 && GetCurrentThreadId() == g_shaderTid &&
	    (r == g_base + kRvaClose1Ret || r == g_base + kRvaClose2Ret)) {
		g_held[g_heldN++] = name;   // the game's string literals: they stay valid
		return true;
	}
	return g_closeOrig(pak, name, a, b);
}

// Main, after a real flush (or at the first frame): the held closes, in the game's order, then the slot back
void RunHeldCloses(const char* when) {
	if (!g_shaderSkipped || g_closesDone) return;
	g_closesDone = true;
	for (int i = 0; i < g_heldN; i++) g_closeOrig(g_cryPak, g_held[i], 0, 0);
	const bool back = SetPtr(g_closeSlot, (void*)g_closeOrig);
	Log("rtsync: the shader manager's %d pak closes run %.0f ms after its wait was handed over (%s)%s", g_heldN,
	    (double)(Now() - g_skipT) * 1000.0 / g_freq, when, back ? "" : "; the ClosePack slot could NOT be put back");
}

// Main: hand the queued commands to the render thread without waiting for them (half of a flush)
void Submit(void* rt) {
	const long long t0 = Now();
	g_submit(rt, 0);
	g_submitTicks += Now() - t0;
	g_submits++;
}

// Both lambdas have run (or the setup ended without them): the renderer's slots back, Main let go. Any thread.
void Release() {
	AcquireSRWLockExclusive(&g_vtLock);
	if (g_gateOn.load(std::memory_order_acquire)) {
		if (g_vtPatched) {
			DWORD old = 0;
			if (VirtualProtect(g_rendVt, kRendererSlots * sizeof(void*), PAGE_READWRITE, &old)) {
				for (int i = 0; i < kRendererSlots; i++)   // only the slots still holding our thunk
					if (g_rendVt[i] == g_thunks + 0x40 + i * kThunkSize) InterlockedExchangePointer(&g_rendVt[i], g_rendOrig[i]);
				VirtualProtect(g_rendVt, kRendererSlots * sizeof(void*), old, &old);
			}
			g_vtPatched = false;
		}
		g_gateOn.store(false, std::memory_order_release);
		SetEvent(g_setupDone);
	}
	ReleaseSRWLockExclusive(&g_vtLock);
}

// The render thread: the setup's lambda, then the flag back (and Main let go when no second lambda follows)
uintptr_t __fastcall HkLam1(uintptr_t self, uintptr_t b, uintptr_t c, uintptr_t d) {
	const long long t0 = Now();
	const uintptr_t r = g_lam1Orig(self, b, c, d);
	if (g_flagHeld.load(std::memory_order_acquire)) {
		*g_flag = g_savedFlag;
		g_lamT0 = t0;
		g_lamT1 = Now();
		g_flagHeld.store(false, std::memory_order_release);
		if (!g_lam2Queued.load(std::memory_order_acquire)) Release();
	}
	return r;
}

uintptr_t __fastcall HkLam2(uintptr_t self, uintptr_t b, uintptr_t c, uintptr_t d) {
	const uintptr_t r = g_lam2Orig(self, b, c, d);
	if (g_gateOn.load(std::memory_order_acquire) && !g_flagHeld.load(std::memory_order_acquire)) {
		g_lam2T1 = Now();
		Release();
	}
	return r;
}

// Main: wait until both lambdas have run
void WaitSetup(const char* what, uintptr_t a, uintptr_t b) {
	if (!g_isrSubmitted && g_isrRt) { Submit(g_isrRt); g_isrSubmitted = true; }   // never handed over yet: now
	const long long t0 = Now();
	const DWORD w = WaitForSingleObject(g_setupDone, 2000);
	g_gateTicks += Now() - t0;
	if (g_gates++ == 0) {
		snprintf(g_gateWhat, sizeof g_gateWhat, what, (unsigned long long)a, (unsigned long long)b);
		g_firstGateAt = t0 - g_isrT;
	}
	if (w != WAIT_OBJECT_0 && !g_isrGaveUp) {
		g_isrGaveUp = true;
		char buf[64];
		snprintf(buf, sizeof buf, what, (unsigned long long)a, (unsigned long long)b);
		Log("rtsync: the setup's render-thread part not done after 2 s: Main GAVE UP waiting at %s", buf);
	}
}

// The generated thunks' call: Main waits while the gate is on, every other thread goes straight on
void __fastcall GateSlot(int slot, uintptr_t from) {
	if (g_gateOn.load(std::memory_order_acquire) && GetCurrentThreadId() == g_mainTid && !g_isrGaveUp)
		WaitSetup("renderer slot %llu from +%llx", (uintptr_t)slot, from - g_base);
}

// One thunk per slot: save the 4 argument registers and xmm0-3, GateSlot(slot, caller), restore, jump to the original (stack
// arguments untouched). Registered with unwind data (a 0x88 stack allocation) so stack walks and exceptions pass.
bool BuildThunks() {
	const size_t n = kRendererSlots;
	g_thunks = (uint8_t*)VirtualAlloc(nullptr, 0x40 + n * kThunkSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!g_thunks) return false;
	uint16_t* uw = (uint16_t*)g_thunks;   // UNWIND_INFO: version 1, prolog 7 bytes, 2 codes: UWOP_ALLOC_LARGE 0x88/8
	uw[0] = 0x0701; uw[1] = 0x0002; uw[2] = 0x0107; uw[3] = 0x88 / 8;
	static RUNTIME_FUNCTION rf[kRendererSlots];
	for (size_t i = 0; i < n; i++) {
		uint8_t* p = g_thunks + 0x40 + i * kThunkSize;
		size_t k = 0;
		auto put = [&](std::initializer_list<uint8_t> b) { for (uint8_t x : b) p[k++] = x; };
		auto put64 = [&](uint64_t v) { memcpy(p + k, &v, 8); k += 8; };
		put({ 0x48,0x81,0xec,0x88,0,0,0 });                                   // sub rsp, 0x88
		put({ 0x48,0x89,0x4c,0x24,0x20, 0x48,0x89,0x54,0x24,0x28 });           // mov [rsp+20],rcx; mov [rsp+28],rdx
		put({ 0x4c,0x89,0x44,0x24,0x30, 0x4c,0x89,0x4c,0x24,0x38 });           // mov [rsp+30],r8; mov [rsp+38],r9
		put({ 0xf3,0x0f,0x7f,0x44,0x24,0x40, 0xf3,0x0f,0x7f,0x4c,0x24,0x50 }); // movdqu [rsp+40],xmm0; [rsp+50],xmm1
		put({ 0xf3,0x0f,0x7f,0x54,0x24,0x60, 0xf3,0x0f,0x7f,0x5c,0x24,0x70 }); // movdqu [rsp+60],xmm2; [rsp+70],xmm3
		put({ 0xb9 }); { const uint32_t v = (uint32_t)i; memcpy(p + k, &v, 4); k += 4; }   // mov ecx, slot
		put({ 0x48,0x8b,0x94,0x24,0x88,0,0,0 });                              // mov rdx, [rsp+88] (the caller)
		put({ 0x48,0xb8 }); put64((uint64_t)&GateSlot);                       // mov rax, GateSlot
		put({ 0xff,0xd0 });                                                    // call rax
		put({ 0x48,0x8b,0x4c,0x24,0x20, 0x48,0x8b,0x54,0x24,0x28 });           // mov rcx,[rsp+20]; mov rdx,[rsp+28]
		put({ 0x4c,0x8b,0x44,0x24,0x30, 0x4c,0x8b,0x4c,0x24,0x38 });           // mov r8,[rsp+30]; mov r9,[rsp+38]
		put({ 0xf3,0x0f,0x6f,0x44,0x24,0x40, 0xf3,0x0f,0x6f,0x4c,0x24,0x50 }); // movdqu xmm0,[rsp+40]; xmm1,[rsp+50]
		put({ 0xf3,0x0f,0x6f,0x54,0x24,0x60, 0xf3,0x0f,0x6f,0x5c,0x24,0x70 }); // movdqu xmm2,[rsp+60]; xmm3,[rsp+70]
		put({ 0x48,0x81,0xc4,0x88,0,0,0 });                                   // add rsp, 0x88
		put({ 0x48,0xb8 }); put64((uint64_t)g_rendOrig[i]);                   // mov rax, original
		put({ 0xff,0xe0 });                                                    // jmp rax
		while (k < kThunkSize) p[k++] = 0xcc;
		rf[i].BeginAddress = (DWORD)(0x40 + i * kThunkSize);
		rf[i].EndAddress = (DWORD)(0x40 + i * kThunkSize + kThunkSize);
		rf[i].UnwindData = 0;
	}
	DWORD old = 0;
	if (!VirtualProtect(g_thunks, 0x40 + n * kThunkSize, PAGE_EXECUTE_READ, &old)) return false;
	FlushInstructionCache(GetCurrentProcess(), g_thunks, 0x40 + n * kThunkSize);
	return RtlAddFunctionTable(rf, (DWORD)n, (DWORD64)g_thunks) != FALSE;
}

// Main, at the hand-over: the renderer's slots to the thunks (until Release)
void PatchVtable() {
	AcquireSRWLockExclusive(&g_vtLock);
	if (g_gateOn.load(std::memory_order_acquire) && !g_vtPatched && g_thunks) {
		DWORD old = 0;
		if (VirtualProtect(g_rendVt, kRendererSlots * sizeof(void*), PAGE_READWRITE, &old)) {
			for (int i = 0; i < kRendererSlots; i++)
				if (i != kIsrSlot && i != kLam2Slot && g_rendVt[i] == g_rendOrig[i])
					InterlockedExchangePointer(&g_rendVt[i], g_thunks + 0x40 + i * kThunkSize);
			VirtualProtect(g_rendVt, kRendererSlots * sizeof(void*), old, &old);
			g_vtPatched = true;
		}
	}
	ReleaseSRWLockExclusive(&g_vtLock);
}

uintptr_t __fastcall HkForName(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
	if (g_gateOn.load(std::memory_order_acquire) && GetCurrentThreadId() == g_mainTid && !g_isrGaveUp)
		WaitSetup("a texture load from +%llx", (uintptr_t)_ReturnAddress() - g_base, 0);
	return g_forNameOrig(a, b, c, d);
}

// Main: the setup, with the flag cleared first so that the game's own save and put-back keep it cleared
uintptr_t __fastcall HkIsr(uintptr_t self, uintptr_t b, uintptr_t c, uintptr_t d) {
	if (!g_isrOn || g_isrDone || !g_on.load(std::memory_order_relaxed) || GetCurrentThreadId() != g_mainTid ||
	    *(const uint8_t*)(self + 0xf0) != 0)
		return g_isrOrig(self, b, c, d);
	if (*(void***)self != g_rendVt) {
		g_isrDone = true;
		Log("rtsync: the renderer object's vtable is +%llx, not +%llx: the system-resource setup waits as the game does",
		    (unsigned long long)((uintptr_t)*(void***)self - g_base), (unsigned long long)kRvaRendererVt);
		return g_isrOrig(self, b, c, d);
	}
	g_isrDone = true;
	g_savedFlag = *g_flag;
	*g_flag = 0;
	g_flagHeld.store(true, std::memory_order_release);
	g_inIsr = true;
	const uintptr_t r = g_isrOrig(self, b, c, d);
	g_inIsr = false;
	if (!g_isrSkipped) {   // the render thread's part ran inline (no flush came): as the game
		*g_flag = g_savedFlag;
		g_flagHeld.store(false, std::memory_order_release);
	} else {
		g_gateOn.store(true, std::memory_order_release);   // from here Main's texture loads wait (renderer calls from the hand-over)
	}
	return r;
}

void __fastcall HkFlush(void* rt) {
	if (g_on.load(std::memory_order_relaxed)) {
		g_flushes++;
		const uintptr_t r = (uintptr_t)_ReturnAddress();
		if (r == g_ret) { g_skipped++; return; }
		if (g_isrOn && GetCurrentThreadId() == g_mainTid) {
			if (r == g_base + kRvaIsrRet && g_inIsr && !g_isrSkipped) {   // the setup's lambda stays queued for now
				g_isrSkipped = true;
				g_isrRt = rt;
				g_isrT = Now();
				return;
			}
			if (r == g_base + kRvaLam2Ret && g_isrSkipped && !g_isrSubmitted) {   // both lambdas handed over together
				g_lam2Queued.store(true, std::memory_order_release);
				Submit(rt);
				g_isrSubmitted = true;
				PatchVtable();
				return;
			}
		}
		if (r == g_base + kRvaShaderRet && g_shaderOn && !g_shaderSkipped) {
			void* pak = *(void**)(g_base + kRvaCryPak);
			void** vt = pak ? *(void***)pak : nullptr;
			if (vt) {
				g_cryPak = pak;
				g_closeSlot = &vt[kCloseSlot];
				g_closeOrig = (CloseFn)vt[kCloseSlot];
				g_shaderTid = GetCurrentThreadId();
				g_shaderRt = rt;
				if (SetPtr(g_closeSlot, (void*)&HkClose)) {
					g_shaderSkipped = true;
					g_skipT = Now();
					Submit(rt);   // the render thread starts on the shader caches; the closes that follow are held
					return;
				}
			}
			Log("rtsync: the shader manager's wait NOT handed over (no CryPak object or its slot is read-only)");
		}
	}
	const bool timed = g_on.load(std::memory_order_relaxed) && GetCurrentThreadId() == g_mainTid;
	const long long t0 = timed ? Now() : 0;
	g_orig(rt);
	if (timed) {
		const long long d = Now() - t0;
		g_waitTicks += d;
		g_waits++;
		if (g_shaderSkipped && g_firstAfterTicks < 0) {
			g_firstAfterTicks = d;
			g_firstAfterAt = t0 - g_skipT;
			g_firstAfterRet = (uintptr_t)_ReturnAddress() - g_base;
		}
		if (g_isrSubmitted && g_isrFirstAfterTicks < 0) {
			g_isrFirstAfterTicks = d;
			g_isrFirstAfterAt = t0 - g_isrT;
			g_isrFirstAfterRet = (uintptr_t)_ReturnAddress() - g_base;
		}
	}
	if (g_shaderSkipped && !g_closesDone && GetCurrentThreadId() == g_shaderTid) RunHeldCloses("after Main's next wait");
}

// Mode 3's checks and hooks; false leaves the setup as the game does it
bool InstallIsr(uintptr_t base) {
	const uintptr_t flag = RipTarget(base + kRvaIsrSave, 3, 7);
	if (memcmp((void*)(base + kRvaIsr), kIsrBytes, sizeof kIsrBytes) ||
	    *(const uint8_t*)(base + kRvaIsrSave) != 0x40 || *(const uint8_t*)(base + kRvaIsrSave + 1) != 0x8a ||
	    *(const uint8_t*)(base + kRvaIsrRet - 5) != 0xe8 || *(const uint8_t*)(base + kRvaIsrRet) != 0x40 ||
	    *(const uint8_t*)(base + kRvaIsrRet + 1) != 0x88 || RipTarget(base + kRvaIsrRet, 3, 7) != flag ||
	    *(const uint8_t*)(base + kRvaLam2Ret - 5) != 0xe8 || memcmp((void*)(base + kRvaLam2Ret), kLam2RetBytes, sizeof kLam2RetBytes) ||
	    memcmp((void*)(base + kRvaForName), kForNameBytes, sizeof kForNameBytes) ||
	    RipTarget(base + kRvaForNameRead1, 2, 6) != flag || RipTarget(base + kRvaForNameRead2, 2, 6) != flag ||
	    *(void**)(base + kRvaLam1Slot) != (void*)(base + kRvaLam1Call) ||
	    *(void**)(base + kRvaLam2Slot) != (void*)(base + kRvaLam2Call)) {
		Log("rtsync: the renderer's system-resource setup differs (another game version?) - its waits stay");
		return false;
	}
	// the renderer's vtable: the setup and the second lambda's slot where expected, every slot into the game's code
	g_rendVt = (void**)(base + kRvaRendererVt);
	const auto* dos = (const IMAGE_DOS_HEADER*)base;
	const auto* nt = (const IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
	const uintptr_t end = base + nt->OptionalHeader.SizeOfImage;
	bool vtOk = g_rendVt[kIsrSlot] == (void*)(base + kRvaIsr) && g_rendVt[kLam2Slot] == (void*)(base + 0x1447938);
	for (int i = 0; i < kRendererSlots && vtOk; i++) {
		g_rendOrig[i] = g_rendVt[i];
		vtOk = (uintptr_t)g_rendOrig[i] > base && (uintptr_t)g_rendOrig[i] < end;
	}
	if (!vtOk) {
		Log("rtsync: the renderer's vtable differs (another game version?) - the setup's waits stay");
		return false;
	}
	if (!BuildThunks()) {
		Log("rtsync: the renderer-call gate could not be built - the setup's waits stay");
		return false;
	}
	g_flag = (volatile uint8_t*)flag;
	g_setupDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	g_lam1Slot = (void**)(base + kRvaLam1Slot);
	g_lam1Orig = (Fn4)*g_lam1Slot;
	g_lam2Slot = (void**)(base + kRvaLam2Slot);
	g_lam2Orig = (Fn4)*g_lam2Slot;
	void* isr = (void*)(base + kRvaIsr);
	void* fn = (void*)(base + kRvaForName);
	if (!g_setupDone || MH_CreateHook(isr, (void*)&HkIsr, (void**)&g_isrOrig) != MH_OK ||
	    MH_CreateHook(fn, (void*)&HkForName, (void**)&g_forNameOrig) != MH_OK) {
		Log("rtsync: the system-resource setup's hooks failed - its waits stay");
		return false;
	}
	if (!SetPtr(g_lam1Slot, (void*)&HkLam1) || !SetPtr(g_lam2Slot, (void*)&HkLam2) || MH_EnableHook(fn) != MH_OK ||
	    MH_EnableHook(isr) != MH_OK) {
		Log("rtsync: the system-resource setup's hooks could not be turned on - its waits stay");
		SetPtr(g_lam1Slot, (void*)g_lam1Orig);
		SetPtr(g_lam2Slot, (void*)g_lam2Orig);
		return false;
	}
	return true;
}

}  // namespace

void Rtsync_Install(uintptr_t base, void* self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	int mode = 2;   // the shader manager's hand-over (1) and the setup's (3) are off until a boot pair tests them
	if (FILE* fp = fopen((dir + "fastload_rtsync.txt").c_str(), "r")) {
		if (fscanf(fp, "%d", &mode) != 1) mode = 2;
		fclose(fp);
	}
	g_reported = true;   // until installed
	if (mode < 1 || mode > 3) { Log("rtsync: off (fastload_rtsync.txt)"); return; }
	if (memcmp((void*)(base + kRvaFlush), kFlushBytes, sizeof kFlushBytes) || memcmp((void*)(base + kRvaQualityRet), kRetBytes, sizeof kRetBytes) ||
	    memcmp((void*)(base + kRvaTail), kTailBytes, sizeof kTailBytes)) {
		Log("rtsync: the renderer's sync code differs (another game version?) - off");
		return;
	}
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	g_base = base;
	g_mainTid = Early_MainThread();
	g_ret = base + kRvaQualityRet;
	const bool submitOk = !memcmp((void*)(base + kRvaSubmit), kSubmitBytes, sizeof kSubmitBytes) &&
	                      *(const uint8_t*)(base + kRvaFlushCall1) == 0xe8 && RipTarget(base + kRvaFlushCall1, 1, 5) == base + kRvaSubmit &&
	                      *(const uint8_t*)(base + kRvaFlushCall2) == 0xe8 && RipTarget(base + kRvaFlushCall2, 1, 5) == base + kRvaSubmit;
	g_submit = (SubmitFn)(base + kRvaSubmit);
	g_shaderOn = (mode == 1 || mode == 3) && submitOk && !memcmp((void*)(base + kRvaShaderRet - 5), kShaderBytes, sizeof kShaderBytes) &&
	             !memcmp((void*)(base + kRvaClose1Ret - 6), kCallSlotBytes, sizeof kCallSlotBytes) &&
	             !memcmp((void*)(base + kRvaClose2Ret - 6), kCallSlotBytes, sizeof kCallSlotBytes);
	void* t = (void*)(base + kRvaFlush);
	if (MH_CreateHook(t, (void*)&HkFlush, (void**)&g_orig) != MH_OK || MH_EnableHook(t) != MH_OK) { Log("rtsync: hook failed - off"); return; }
	g_isrOn = mode == 3 && submitOk && InstallIsr(base);
	g_on = true;
	g_reported = false;
	Log("rtsync: on (until the first frame, the shader-quality settings don't wait for the render thread%s%s; "
	    "fastload_rtsync.txt %d)",
	    g_shaderOn ? "; the shader manager's work handed to it (its 2 pak closes held until Main's next wait)"
	               : mode != 2 ? "; the shader manager's code differs: its wait stays" : "",
	    g_isrOn ? "; the renderer's system-resource setup handed to it (Main's texture loads and renderer calls wait for it)" : "", mode);
}

void Rtsync_Frame() {
	if (g_reported) return;
	g_reported = true;
	g_on = false;
	if (g_shaderSkipped && !g_closesDone) {   // no real flush on Main since the hand-over: wait now, then close
		g_orig(g_shaderRt);
		RunHeldCloses("at the first frame");
	}
	if (g_isrOn && (g_flagHeld.load(std::memory_order_acquire) || g_gateOn.load(std::memory_order_acquire)) && g_isrRt) {
		if (!g_isrSubmitted) { Submit(g_isrRt); g_isrSubmitted = true; }   // the setup's lambdas not run yet: run them now
		g_orig(g_isrRt);
		if (g_flagHeld.load(std::memory_order_acquire) || g_gateOn.load(std::memory_order_acquire)) {
			if (g_flagHeld.exchange(false)) *g_flag = g_savedFlag;   // a lambda never went through its slot
			Release();
			Log("rtsync: the setup's render-thread part never reported back; the texture flag and the renderer put back at "
			    "the first frame");
		}
	}
	if (g_lam1Slot) SetPtr(g_lam1Slot, (void*)g_lam1Orig);
	if (g_lam2Slot) SetPtr(g_lam2Slot, (void*)g_lam2Orig);
	const double ms = 1000.0 / g_freq;
	Log("rtsync: %lld shader-quality waits for the render thread skipped (of %lld flushes) until the first frame; the shader "
	    "manager's wait %s; Main waited %.0f ms in %lld real flushes (the first after the hand-over: %.0f ms, %.0f ms after "
	    "it, from +%llx) and %.0f ms in %lld hand-overs; the game's own waits from now on", g_skipped.load(), g_flushes.load(),
	    g_shaderSkipped ? "handed over" : g_shaderOn ? "never came" : "kept", g_waitTicks * ms, g_waits,
	    g_firstAfterTicks < 0 ? -1.0 : g_firstAfterTicks * ms, g_firstAfterAt * ms, (unsigned long long)g_firstAfterRet,
	    g_submitTicks * ms, g_submits);
	if (g_isrOn) {
		const long long l0 = g_lamT0.load(), l1 = g_lamT1.load();
		if (!g_isrDone) Log("rtsync: the renderer's system-resource setup never came before the first frame");
		else if (!g_isrSkipped) Log("rtsync: the renderer's system-resource setup ran inline (no wait to hand over)");
		else
			Log("rtsync: the renderer's system-resource setup handed over: its render-thread part ran %.0f-%.0f ms after Main "
			    "went on (%.0f ms), the second lambda done at %.0f ms (the game waited for both); Main waited %.0f ms for them "
			    "in %lld waits (the first at %s, %.0f ms after it went on)%s; Main's first real flush after it: %.0f ms, %.0f ms "
			    "after it (from +%llx)",
			    l0 ? (l0 - g_isrT) * ms : -1.0, l1 ? (l1 - g_isrT) * ms : -1.0, l0 && l1 ? (l1 - l0) * ms : -1.0,
			    g_lam2T1.load() ? (g_lam2T1.load() - g_isrT) * ms : -1.0, g_gateTicks * ms, g_gates,
			    g_gates ? g_gateWhat : "none", g_firstGateAt * ms, g_isrGaveUp ? " (GAVE UP once after 2 s)" : "",
			    g_isrFirstAfterTicks < 0 ? -1.0 : g_isrFirstAfterTicks * ms, g_isrFirstAfterAt * ms,
			    (unsigned long long)g_isrFirstAfterRet);
	}
}
