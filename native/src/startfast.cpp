// Start-up pieces: see startfast.h.
#include "startfast.h"
#include "log.h"
#include <windows.h>
#include <intrin.h>
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

// inputahead (startfast.h): GameInput's first GameInputCreate made on a worker, through the game's own WhGdk.dll
const unsigned char kGetWrapperBytes[] = { 0x48,0x8d,0x05 };   // GetWhGdkWrapper: lea rax, [static wrapper]; ret
const unsigned char kCreateBytes[] = { 0x48,0x8b,0xca,0xe9,0x00,0x00,0x00,0x00,0xff,0x25 };   // slot 0: GameInputCreate(out)
int g_ahead = 1;   // 1 by default since boot queue 171
std::atomic<int> g_aheadState{ 0 };   // 0 not started, 1 running, 2 done, -1 skipped
double g_aheadMs = -1, g_aheadStartMs = -1;
long g_aheadHr = 0;
const char* g_aheadWhy = "";
long long g_installTicks = 0;
void* g_gameInput = nullptr;   // kept for the process (GameInput: create once, keep the reference)

DWORD WINAPI InputAhead(LPVOID) {
	LARGE_INTEGER f, a, b;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&a);
	g_aheadStartMs = (a.QuadPart - g_installTicks) * 1000.0 / f.QuadPart;
	wchar_t exe[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring dll(exe);
	dll = dll.substr(0, dll.find_last_of(L"\\/") + 1) + L"WhGdk.dll";   // where the game's LoadLibraryA("WhGdk") finds it
	HMODULE m = LoadLibraryW(dll.c_str());
	using GetFn = void*(*)();
	using CreateFn = long(__fastcall*)(void* self, void** out);
	GetFn get = m ? (GetFn)GetProcAddress(m, "GetWhGdkWrapper") : nullptr;
	if (!get) { g_aheadWhy = "WhGdk.dll or GetWhGdkWrapper not found"; g_aheadState = -1; return 0; }
	if (memcmp((void*)get, kGetWrapperBytes, sizeof kGetWrapperBytes)) { g_aheadWhy = "GetWhGdkWrapper differs"; g_aheadState = -1; return 0; }
	void* w = get();
	CreateFn create = w ? *(CreateFn*)*(void**)w : nullptr;
	if (!create || memcmp((void*)create, kCreateBytes, sizeof kCreateBytes)) { g_aheadWhy = "the wrapper's create differs"; g_aheadState = -1; return 0; }
	g_aheadHr = create(w, &g_gameInput);
	QueryPerformanceCounter(&b);
	g_aheadMs = (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart;
	g_aheadState = 2;
	return 0;
}

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
	const int ahead = g_aheadState.load();
	const uint64_t r = g_origInputInit(self);
	QueryPerformanceCounter(&b);
	Log("startfast: input init %.0f ms on thread %lu (controller list %s)", (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart,
	    GetCurrentThreadId(), g_async ? "async, callbacks to the main thread" : "the game's own: blocking");
	if (g_ahead == 1) {
		if (ahead == 2)
			Log("inputahead: GameInput was made on a worker before the input init: %.0f ms, from %.0f ms after the plugin's start-up "
			    "pieces (HRESULT 0x%08lx)", g_aheadMs, g_aheadStartMs, (unsigned long)g_aheadHr);
		else if (ahead == -1) Log("inputahead: skipped (%s)", g_aheadWhy);
		else Log("inputahead: the worker was still making GameInput when the input init came (the game's call waited for it)");
	}
	return r;
}

bool Same(uintptr_t at, const unsigned char* bytes, size_t n) {
	__try { return !memcmp((void*)at, bytes, n); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// CPU detection (startfast.h): the two `call [SetProcessAffinityMask]` in FUN_18136739c and the import slot they use
constexpr uintptr_t kRvaProbeLoopCall = 0x136743a;      // in the per-core loop; returns to +1367440
constexpr uintptr_t kRvaProbeRestoreCall = 0x1367462;   // the restore after it; returns to +1367468
constexpr uintptr_t kRvaSpamSlot = 0x3a037d8;           // WHGame.dll's import slot for kernel32!SetProcessAffinityMask
const unsigned char kProbeLoopBytes[] = { 0xff,0x15,0x98,0xc3,0x69,0x02 };
const unsigned char kProbeRestoreBytes[] = { 0xff,0x15,0x70,0xc3,0x69,0x02 };

bool SlotHolds(void** slot, void* fn) {
	__try { return *slot == fn; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

using SpamFn = BOOL(WINAPI*)(HANDLE, DWORD_PTR);
SpamFn g_realSpam = nullptr;
uintptr_t g_raLoop = 0, g_raRestore = 0;
DWORD_PTR g_savedThreadMask = 0;   // the main thread's own mask before the first core
int g_probeCores = 0, g_probeRuns = 0;
bool g_probeFellBack = false;   // a core went the game's own way: the process mask needs the real restore too
LARGE_INTEGER g_probeT0{};

BOOL WINAPI HkSetProcessAffinityMask(HANDLE proc, DWORD_PTR mask) {
	const uintptr_t ra = (uintptr_t)_ReturnAddress();
	if (ra == g_raLoop && proc == GetCurrentProcess()) {
		const DWORD_PTR prev = SetThreadAffinityMask(GetCurrentThread(), mask);
		if (!prev) {   // refused: the game's own way for this core
			g_probeFellBack = true;
			return g_realSpam(proc, mask);
		}
		if (!g_savedThreadMask) {
			g_savedThreadMask = prev;
			QueryPerformanceCounter(&g_probeT0);
			g_probeCores = 0;
		}
		g_probeCores++;
		return TRUE;
	}
	if (ra == g_raRestore && proc == GetCurrentProcess() && g_savedThreadMask) {
		SetThreadAffinityMask(GetCurrentThread(), g_savedThreadMask);
		g_savedThreadMask = 0;
		if (g_probeFellBack) {   // the process mask was changed for some core: restore it as the game does
			g_probeFellBack = false;
			g_realSpam(proc, mask);
		}
		LARGE_INTEGER t, f;
		QueryPerformanceCounter(&t);
		QueryPerformanceFrequency(&f);
		Log("startfast: CPU detection %d: the main thread (not the process) on each of %d cores, %.1f ms", ++g_probeRuns,
		    g_probeCores, (t.QuadPart - g_probeT0.QuadPart) * 1000.0 / f.QuadPart);
		return TRUE;
	}
	return g_realSpam(proc, mask);
}

}   // namespace

void Startfast_EarlyInstall(uintptr_t base, void* self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	int mode = 0;
	if (FILE* f = fopen((dir + "fastload_cpuprobe.txt").c_str(), "r")) {
		if (fscanf(f, "%d", &mode) != 1) mode = 0;
		fclose(f);
	}
	if (mode != 1) {
		Log("startfast: CPU detection mode %d - the game's own (the whole process on each core; fastload_cpuprobe.txt 1 = only "
		    "the main thread)", mode);
		return;
	}
	const auto real = (SpamFn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetProcessAffinityMask");
	void** slot = (void**)(base + kRvaSpamSlot);
	const bool slotOk = real && SlotHolds(slot, (void*)real);
	if (!Same(base + kRvaProbeLoopCall, kProbeLoopBytes, sizeof kProbeLoopBytes) ||
	    !Same(base + kRvaProbeRestoreCall, kProbeRestoreBytes, sizeof kProbeRestoreBytes) || !slotOk) {
		Log("startfast: CPU detection code or its import differs (another game version? imports not bound yet?) - left alone");
		return;
	}
	g_realSpam = real;
	g_raLoop = base + kRvaProbeLoopCall + 6;
	g_raRestore = base + kRvaProbeRestoreCall + 6;
	DWORD old;
	if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
		Log("startfast: CPU detection: VirtualProtect failed - left alone");
		return;
	}
	InterlockedExchangePointer(slot, (void*)&HkSetProcessAffinityMask);
	VirtualProtect(slot, sizeof(void*), old, &old);
	Log("startfast: CPU detection mode 1 - its per-core probe pins the main thread, not the whole process");
}

void Startfast_Install(uintptr_t base, void* self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	if (FILE* f = fopen((dir + "fastload_startfast.txt").c_str(), "r")) {
		if (fscanf(f, "%d", &g_mode) != 1) g_mode = 1;
		fclose(f);
	}
	if (FILE* f = fopen((dir + "fastload_inputahead.txt").c_str(), "r")) {
		if (fscanf(f, "%d", &g_ahead) != 1) g_ahead = 1;
		fclose(f);
	}
	if (g_ahead == 1) {
		LARGE_INTEGER t; QueryPerformanceCounter(&t); g_installTicks = t.QuadPart;
		g_aheadState = 1;
		if (HANDLE h = CreateThread(nullptr, 0, InputAhead, nullptr, 0, nullptr)) { CloseHandle(h); Log("inputahead: on - GameInput made on a worker now"); }
		else { g_aheadState = -1; g_aheadWhy = "no thread"; }
	} else Log("inputahead: off (fastload_inputahead.txt 1: GameInput made on a worker at start-up)");
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
