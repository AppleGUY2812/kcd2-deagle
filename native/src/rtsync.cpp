// The shader-quality settings sent to the render thread without waiting: see rtsync.h.
#include "rtsync.h"
#include "log.h"
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
const unsigned char kFlushBytes[] = { 0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8 };   // push rbx; sub rsp,20; mov rbx,rcx; call
const unsigned char kRetBytes[] = { 0x48,0x8b,0x0d };   // mov rcx, [rip+..] after the call
const unsigned char kTailBytes[] = { 0xe9,0xcc,0xbd,0x9b,0xfe };   // jmp 0x1807a2138

using FlushFn = void(__fastcall*)(void* rt);
FlushFn g_orig = nullptr;
uintptr_t g_ret = 0;
std::atomic<bool> g_on{ false };
std::atomic<long long> g_skipped{ 0 }, g_flushes{ 0 };
bool g_reported = false;

void __fastcall HkFlush(void* rt) {
	if (g_on.load(std::memory_order_relaxed)) {
		g_flushes++;
		if ((uintptr_t)_ReturnAddress() == g_ret) { g_skipped++; return; }
	}
	g_orig(rt);
}

}  // namespace

void Rtsync_Install(uintptr_t base, void* self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	int mode = 1;
	if (FILE* fp = fopen((dir + "fastload_rtsync.txt").c_str(), "r")) {
		if (fscanf(fp, "%d", &mode) != 1) mode = 1;
		fclose(fp);
	}
	g_reported = true;   // until installed
	if (mode != 1) { Log("rtsync: off (fastload_rtsync.txt)"); return; }
	if (memcmp((void*)(base + kRvaFlush), kFlushBytes, sizeof kFlushBytes) || memcmp((void*)(base + kRvaQualityRet), kRetBytes, sizeof kRetBytes) ||
	    memcmp((void*)(base + kRvaTail), kTailBytes, sizeof kTailBytes)) {
		Log("rtsync: the renderer's sync code differs (another game version?) - off");
		return;
	}
	g_ret = base + kRvaQualityRet;
	void* t = (void*)(base + kRvaFlush);
	if (MH_CreateHook(t, (void*)&HkFlush, (void**)&g_orig) != MH_OK || MH_EnableHook(t) != MH_OK) { Log("rtsync: hook failed - off"); return; }
	g_on = true;
	g_reported = false;
	Log("rtsync: on (the shader-quality settings don't wait for the render thread until the first frame; fastload_rtsync.txt)");
}

void Rtsync_Frame() {
	if (g_reported) return;
	g_reported = true;
	g_on = false;
	Log("rtsync: %lld shader-quality waits for the render thread skipped (of %lld flushes) until the first frame; the game's own "
	    "waits from now on", g_skipped.load(), g_flushes.load());
}
