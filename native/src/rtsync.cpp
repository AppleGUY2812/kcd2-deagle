// The shader-quality settings sent to the render thread without waiting: see rtsync.h.
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

// The shader manager's init (FUN_180da002c): flush and wait, then close %ENGINE%/Shaders.pak and shadercache.pak
constexpr uintptr_t kRvaShaderRet = 0xda0383;  // its return from the flush
constexpr uintptr_t kRvaClose1Ret = 0xda03a0;  // its return from ClosePack("%ENGINE%/Shaders.pak")
constexpr uintptr_t kRvaClose2Ret = 0xda03bd;  // its return from ClosePack("%ENGINE%/shadercache.pak")
constexpr uintptr_t kRvaCryPak = 0x492d850;    // the CryPak object pointer (gEnv's ICryPak)
constexpr size_t kCloseSlot = 0x88 / 8;        // ICryPak::ClosePack
const unsigned char kShaderBytes[] = { 0xe8,0xb5,0x1d,0xa0,0xff, 0x48,0x8b,0x0d,0xc6,0xd4,0xb8,0x03 };   // call flush; mov rcx,[CryPak]
const unsigned char kCallSlotBytes[] = { 0xff,0x90,0x88,0x00,0x00,0x00 };   // call [rax+0x88], before each close's return

using FlushFn = void(__fastcall*)(void* rt);
using CloseFn = bool(__fastcall*)(void* pak, const char* name, uintptr_t a, uintptr_t b);
FlushFn g_orig = nullptr;
uintptr_t g_ret = 0, g_base = 0;
std::atomic<bool> g_on{ false };
std::atomic<long long> g_skipped{ 0 }, g_flushes{ 0 };
bool g_reported = false;
bool g_shaderOn = false;   // mode 1: the shader manager's wait too

// The shader manager's wait, skipped: Main goes on while the render thread loads the shader caches. The two pak closes
// after it are held (the render thread reads those paks) and run on Main right after its next real flush and wait,
// when every command queued before it is done. Main only, so plain variables.
void* g_cryPak = nullptr;
void** g_closeSlot = nullptr;
CloseFn g_closeOrig = nullptr;
const char* g_held[2] = {};
int g_heldN = 0;
DWORD g_shaderTid = 0;
void* g_shaderRt = nullptr;
long long g_skipT = 0, g_freq = 1;
bool g_shaderSkipped = false, g_closesDone = false;
// Main's real waits until the first frame (both modes), to see whether a skipped wait only moves to the next one
DWORD g_mainTid = 0;
long long g_waitTicks = 0, g_waits = 0, g_firstAfterTicks = -1, g_firstAfterAt = 0;
uintptr_t g_firstAfterRet = 0;

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

bool SetSlot(void* fn) {
	DWORD old = 0;
	if (!VirtualProtect(g_closeSlot, sizeof(void*), PAGE_READWRITE, &old)) return false;
	InterlockedExchangePointer(g_closeSlot, fn);
	VirtualProtect(g_closeSlot, sizeof(void*), old, &old);
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
	const bool back = SetSlot((void*)g_closeOrig);
	Log("rtsync: the shader manager's %d pak closes run %.0f ms after its skipped wait (%s)%s", g_heldN,
	    (double)(Now() - g_skipT) * 1000.0 / g_freq, when, back ? "" : "; the ClosePack slot could NOT be put back");
}

void __fastcall HkFlush(void* rt) {
	if (g_on.load(std::memory_order_relaxed)) {
		g_flushes++;
		const uintptr_t r = (uintptr_t)_ReturnAddress();
		if (r == g_ret) { g_skipped++; return; }
		if (r == g_base + kRvaShaderRet && g_shaderOn && !g_shaderSkipped) {
			void* pak = *(void**)(g_base + kRvaCryPak);
			void** vt = pak ? *(void***)pak : nullptr;
			if (vt) {
				g_cryPak = pak;
				g_closeSlot = &vt[kCloseSlot];
				g_closeOrig = (CloseFn)vt[kCloseSlot];
				g_shaderTid = GetCurrentThreadId();
				g_shaderRt = rt;
				if (SetSlot((void*)&HkClose)) {
					g_shaderSkipped = true;
					g_skipT = Now();
					return;   // the closes that follow are held
				}
			}
			Log("rtsync: the shader manager's wait NOT skipped (no CryPak object or its slot is read-only)");
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
	}
	if (g_shaderSkipped && !g_closesDone && GetCurrentThreadId() == g_shaderTid) RunHeldCloses("after Main's next wait");
}

}  // namespace

void Rtsync_Install(uintptr_t base, void* self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	int mode = 2;   // the shader manager's skip (1) is off until a boot pair tests it
	if (FILE* fp = fopen((dir + "fastload_rtsync.txt").c_str(), "r")) {
		if (fscanf(fp, "%d", &mode) != 1) mode = 2;
		fclose(fp);
	}
	g_reported = true;   // until installed
	if (mode != 1 && mode != 2) { Log("rtsync: off (fastload_rtsync.txt)"); return; }
	if (memcmp((void*)(base + kRvaFlush), kFlushBytes, sizeof kFlushBytes) || memcmp((void*)(base + kRvaQualityRet), kRetBytes, sizeof kRetBytes) ||
	    memcmp((void*)(base + kRvaTail), kTailBytes, sizeof kTailBytes)) {
		Log("rtsync: the renderer's sync code differs (another game version?) - off");
		return;
	}
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	g_base = base;
	g_mainTid = Early_MainThread();
	g_ret = base + kRvaQualityRet;
	g_shaderOn = mode == 1 && !memcmp((void*)(base + kRvaShaderRet - 5), kShaderBytes, sizeof kShaderBytes) &&
	             !memcmp((void*)(base + kRvaClose1Ret - 6), kCallSlotBytes, sizeof kCallSlotBytes) &&
	             !memcmp((void*)(base + kRvaClose2Ret - 6), kCallSlotBytes, sizeof kCallSlotBytes);
	void* t = (void*)(base + kRvaFlush);
	if (MH_CreateHook(t, (void*)&HkFlush, (void**)&g_orig) != MH_OK || MH_EnableHook(t) != MH_OK) { Log("rtsync: hook failed - off"); return; }
	g_on = true;
	g_reported = false;
	Log("rtsync: on (until the first frame, the shader-quality settings%s don't wait for the render thread; "
	    "fastload_rtsync.txt %d)", g_shaderOn ? " and the shader manager's init (its 2 pak closes held until Main's next wait)"
	                                         : mode == 1 ? " (the shader manager's code differs: its wait stays)" : "", mode);
}

void Rtsync_Frame() {
	if (g_reported) return;
	g_reported = true;
	g_on = false;
	if (g_shaderSkipped && !g_closesDone) {   // no real flush on Main since the skip: wait now, then close
		g_orig(g_shaderRt);
		RunHeldCloses("at the first frame");
	}
	const double ms = 1000.0 / g_freq;
	Log("rtsync: %lld shader-quality waits for the render thread skipped (of %lld flushes) until the first frame; the shader "
	    "manager's wait %s; Main waited %.0f ms in %lld real flushes (the first after the skip: %.0f ms, %.0f ms after it, from "
	    "+%llx); the game's own waits from now on", g_skipped.load(), g_flushes.load(),
	    g_shaderSkipped ? "skipped" : g_shaderOn ? "never came" : "kept", g_waitTicks * ms, g_waits,
	    g_firstAfterTicks < 0 ? -1.0 : g_firstAfterTicks * ms, g_firstAfterAt * ms, (unsigned long long)g_firstAfterRet);
}
