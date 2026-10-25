// Start-up pieces: see startfast.h.
#include "startfast.h"
#include "log.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "MinHook.h"

namespace {

// mov dword ptr [rsp+0x20], 2 (the enumeration kind) right before the call through IGameInput vtable +0x48
constexpr uintptr_t kRvaEnumArg = 0xda21d7;
const unsigned char kEnumBytes[] = { 0xc7,0x44,0x24,0x20,0x02,0x00,0x00,0x00 };
// the callback passed to it (0x183834aa8): a thunk to the game's handler (context, device, current status)
constexpr uintptr_t kRvaCallback = 0x3834aa8;
const unsigned char kCallbackBytes[] = { 0x49,0x8b,0xc0,0x48,0x8b,0xca,0x44,0x8b,0x44,0x24,0x28,0x48,0x8b,0xd0,0xe9 };
// the input init itself, timed
constexpr uintptr_t kRvaInputInit = 0xda1fcc;
const unsigned char kInputInitBytes[] = { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x55,0x48,0x8b,0xec };

using CallbackFn = void(__fastcall*)(uint64_t token, void* ctx, void* device, uint64_t ts, uint32_t cur, uint32_t prev);
using InputInitFn = uint64_t(__fastcall*)(void* self);
CallbackFn g_origCallback = nullptr;
InputInitFn g_origInputInit = nullptr;

struct Event { uint64_t token; void* ctx; void* device; uint64_t ts; uint32_t cur, prev; };
std::mutex g_mu;
std::vector<Event> g_queue;
std::atomic<bool> g_direct{false};   // no frame tick: call the game at once, as the plain async call would
std::atomic<bool> g_pending{false};
std::atomic<long> g_events{0}, g_handed{0};
int g_mode = 1;
bool g_async = false;

inline void AddRef(void* p) { if (p) (*(unsigned long(__stdcall***)(void*))p)[1](p); }
inline void Release(void* p) { if (p) (*(unsigned long(__stdcall***)(void*))p)[2](p); }

void __fastcall HkCallback(uint64_t token, void* ctx, void* device, uint64_t ts, uint32_t cur, uint32_t prev) {
	g_events++;
	if (g_direct) { g_origCallback(token, ctx, device, ts, cur, prev); return; }
	AddRef(device);
	std::lock_guard<std::mutex> lk(g_mu);
	g_queue.push_back({ token, ctx, device, ts, cur, prev });
	g_pending = true;
}

void Drain(const char* where) {
	std::vector<Event> q;
	{ std::lock_guard<std::mutex> lk(g_mu); q.swap(g_queue); g_pending = false; }
	for (const Event& e : q) {
		g_origCallback(e.token, e.ctx, e.device, e.ts, e.cur, e.prev);
		Release(e.device);
		g_handed++;
	}
	if (!q.empty()) Log("startfast: %zu controller event(s) handed to the game on %s (status %u, %ld so far)", q.size(), where,
	                    q.back().cur, g_handed.load());
}

uint64_t __fastcall HkInputInit(void* self) {
	LARGE_INTEGER f, a, b;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&a);
	const uint64_t r = g_origInputInit(self);
	QueryPerformanceCounter(&b);
	Log("startfast: input init %.0f ms on thread %lu (controller list %s)", (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart,
	    GetCurrentThreadId(), g_async ? "async, callbacks to the main thread" : "the game's own: blocking");
	return r;
}

bool Same(uintptr_t at, const unsigned char* bytes, size_t n) {
	__try { return !memcmp((void*)at, bytes, n); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

}   // namespace

void Startfast_Install(uintptr_t base, void* self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	if (FILE* f = fopen((dir + "fastload_startfast.txt").c_str(), "r")) {
		if (fscanf(f, "%d", &g_mode) != 1) g_mode = 1;
		fclose(f);
	}
	void* init = (void*)(base + kRvaInputInit);
	if (Same(base + kRvaInputInit, kInputInitBytes, sizeof kInputInitBytes) &&
	    MH_CreateHook(init, (void*)&HkInputInit, (void**)&g_origInputInit) == MH_OK && MH_EnableHook(init) == MH_OK) {}
	else Log("startfast: input init not timed (other bytes: another game version?)");
	if (g_mode != 1) { Log("startfast: mode %d - the game's own controller list (blocking)", g_mode); return; }
	const uintptr_t arg = base + kRvaEnumArg;
	void* cb = (void*)(base + kRvaCallback);
	if (!Same(arg, kEnumBytes, sizeof kEnumBytes) || !Same(base + kRvaCallback, kCallbackBytes, sizeof kCallbackBytes)) {
		Log("startfast: controller list code differs (another game version?) - left alone");
		return;
	}
	if (MH_CreateHook(cb, (void*)&HkCallback, (void**)&g_origCallback) != MH_OK || MH_EnableHook(cb) != MH_OK) {
		Log("startfast: controller callback hook failed - left alone");
		return;
	}
	DWORD old;
	if (!VirtualProtect((void*)(arg + 4), 1, PAGE_EXECUTE_READWRITE, &old)) {
		MH_DisableHook(cb);
		Log("startfast: VirtualProtect failed - left alone");
		return;
	}
	*(volatile unsigned char*)(arg + 4) = 1;   // GameInputAsyncEnumeration
	VirtualProtect((void*)(arg + 4), 1, old, &old);
	FlushInstructionCache(GetCurrentProcess(), (void*)arg, sizeof kEnumBytes);
	g_async = true;
	Log("startfast: mode 1 - controller list async, the game's controller callbacks handed over on the main thread");
}

void Startfast_TickReady(bool ok) {
	if (ok || !g_async) return;
	g_direct = true;   // no frame tick to hand them over: what is queued goes now, the rest as GameInput sends it
	Drain("the plugin thread (no frame tick)");
}

void Startfast_Frame() {
	if (g_pending.load(std::memory_order_relaxed)) Drain("the main thread");
}
