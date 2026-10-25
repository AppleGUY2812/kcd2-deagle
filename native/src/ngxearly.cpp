// NVIDIA NGX started early: see ngxearly.h.
#include "ngxearly.h"
#include "log.h"
#include <windows.h>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include "MinHook.h"

namespace {

// FUN_181e0b7c0 up to its call of FUN_181dffa40 (the RIP-relative offsets in it only match 1.5.6)
constexpr uintptr_t kRvaInit = 0x1e0b7c0;
const unsigned char kInitBytes[] = {
	0x40,0x53,0x48,0x83,0xec,0x30,0xf7,0x05,0x10,0xf2,0x4a,0x03,0x00,0x00,0x04,0x00,0x48,0x8b,0xd9,0x74,0x58,0x80,0xb9,
	0xd4,0x08,0x00,0x00,0x00,0x75,0x4f,0x48,0x8d,0x81,0xd8,0x08,0x00,0x00,0x48,0x89,0x44,0x24,0x20,0x8b,0x81,0xd0,0x08,
	0x00,0x00,0x48,0x8b,0x0d,0x69,0x23,0x67,0x03,0x89,0x44,0x24,0x28,0x48,0x8b,0x01,0xff,0x90,0x80,0x00,0x00,0x00,0x48,
	0x8d,0x54,0x24,0x20,0xe8,0x32,0x42,0xff,0xff,0x88,0x83,0xd4,0x08,0x00,0x00,0x84,0xc0,0x75,0x1c,0x48,0x8b,0xcb,0xe8 };
// FUN_181e11170 (NGX DestroyParameters + Shutdown1) up to its Shutdown1 call
constexpr uintptr_t kRvaTeardown = 0x1e11170;
const unsigned char kTeardownBytes[] = {
	0x40,0x53,0x48,0x83,0xec,0x20,0x80,0xb9,0xd4,0x08,0x00,0x00,0x00,0x48,0x8b,0xd9,0x74,0x4b,0x48,0x8b,0x0d,0xd7,0xc9,
	0x66,0x03,0x48,0x8b,0x01,0xff,0x90,0x80,0x00,0x00,0x00,0x48,0x83,0xbb,0xe0,0x08,0x00,0x00,0x00,0x74,0x08,0x48,0x8b,
	0xcb,0xe8,0xbc,0xf9,0xff,0xff,0x48,0x8b,0x8b,0xd8,0x08,0x00,0x00,0xe8,0x10,0x37,0xdd,0xff,0x48,0x8b,0x0d,0xc1,0x93,
	0x4a,0x03,0x48,0x8b,0x49,0x10,0xe8,0xf0,0x3b,0xdd,0xff };
// in FUN_181dffa40: mov rax, [the device global] (its +0x10 is the ID3D12Device passed to NVSDK_NGX_D3D12_Init)
constexpr uintptr_t kRvaDevLoad = 0x1dffae7;
const unsigned char kDevLoadBytes[] = { 0x48,0x8b,0x05 };
// in the DLSS plugin's constructor FUN_181e06ec0: mov dword ptr [rsi+0x8d0], <app id>
constexpr uintptr_t kRvaAppId = 0x1e07163;
const unsigned char kAppIdBytes[] = { 0xc7,0x86,0xd0,0x08,0x00,0x00 };

constexpr uint32_t kNvidia = 0x40000;      // the flags global: "D3D Detected: NVIDIA video card" (FUN_1805fa29c)
constexpr uint32_t kDeviceDone = 0x80000;  // the same function's last flag, after the device global is set

using InitFn = uint64_t(__fastcall*)(char* self);
using StartFn = uint32_t(__fastcall*)(void* unused, void* args);   // FUN_181dffa40, the result in AL
using NgxFn = int(__fastcall*)(void* p);                             // DestroyParameters / Shutdown1

struct StartArgs { void** params; uint32_t appId; uint32_t pad; };   // the stack block FUN_181e0b7c0 passes in RDX

InitFn g_origInit = nullptr;
StartFn g_start = nullptr;
NgxFn g_destroy = nullptr, g_shutdown = nullptr;
volatile uint32_t* g_flags = nullptr;
uintptr_t g_devGlobal = 0, g_devGlobal2 = 0;
uint32_t g_appId = 0;
uintptr_t g_target = 0;   // FUN_181e0b7c0: the starter waits for our hook's JMP there
std::string g_usedPath;

enum State { kIdle, kRunning, kReady, kAdopted, kReleased, kGame };
std::mutex g_mu;
std::condition_variable g_cv;
int g_state = kIdle;
void* g_params = nullptr;
bool g_ok = false;
char* g_adopted = nullptr;   // the DLSS plugin that took the early start: its +0x8e0 is the DLSS feature once created
LARGE_INTEGER g_freq, g_t0;

double Ms(const LARGE_INTEGER& a) {
	LARGE_INTEGER n;
	QueryPerformanceCounter(&n);
	return (n.QuadPart - a.QuadPart) * 1000.0 / g_freq.QuadPart;
}

bool Same(uintptr_t at, const unsigned char* bytes, size_t n) {
	__try { return !memcmp((void*)at, bytes, n); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uintptr_t Rel(uintptr_t at, uintptr_t next) { return next + *(const int32_t*)at; }   // a rel32 operand -> its target
void* ReadPtr(uintptr_t at) {
	__try { return at ? *(void* volatile*)at : nullptr; } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
void* Device(uintptr_t global) {
	char* wrap = (char*)ReadPtr(global);
	return wrap ? ReadPtr((uintptr_t)(wrap + 0x10)) : nullptr;
}

void WriteUsed(int used) {
	if (FILE* f = fopen(g_usedPath.c_str(), "w")) { fprintf(f, "%d\n", used); fclose(f); }
}

// Under g_mu. What the game's own teardown does for a good start; a failed one may still hold NGX (Init worked, the
// capability query didn't), so Shutdown1 too.
void Release() {
	if (g_params) g_destroy(g_params);
	g_params = nullptr;
	if (void* dev = Device(g_devGlobal2)) g_shutdown(dev);
	g_state = kReleased;
}

uint64_t __fastcall HkInit(char* self) {
	if (!(*g_flags & kNvidia) || self[0x8d4]) return g_origInit(self);
	LARGE_INTEGER a;
	QueryPerformanceCounter(&a);
	std::unique_lock<std::mutex> lk(g_mu);
	if (g_state == kIdle) {   // not started (off, skipped, or no device yet): the game's own, and the next launch starts it
		g_state = kGame;
		lk.unlock();
		WriteUsed(1);
		const uint64_t r = g_origInit(self);
		Log("ngxearly: the game's DLSS init ran its own NGX start on thread %lu: %.0f ms (result %d)", GetCurrentThreadId(),
		    Ms(a), (int)(r & 0xff));
		return r;
	}
	const bool waited = g_state == kRunning;
	g_cv.wait(lk, [] { return g_state != kRunning; });
	if (g_state == kReady) {
		g_state = kAdopted;
		*(void**)(self + 0x8d8) = g_params;
		self[0x8d4] = 1;
		g_adopted = self;
		g_cv.notify_all();
		Log("ngxearly: the game's DLSS init (thread %lu) took the early NGX start%s (%.0f ms in it)", GetCurrentThreadId(),
		    waited ? ", after waiting for it" : "", Ms(a));
		return 1;
	}
	lk.unlock();   // released (failed or unused): the game's own
	const uint64_t r = g_origInit(self);
	Log("ngxearly: the game's DLSS init ran its own NGX start on thread %lu (the early one was shut down): %.0f ms "
	    "(result %d)", GetCurrentThreadId(), Ms(a), (int)(r & 0xff));
	return r;
}

DWORD WINAPI Starter(LPVOID) {
	using SetDesc = HRESULT(WINAPI*)(HANDLE, PCWSTR);
	if (auto f = (SetDesc)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetThreadDescription"))
		f(GetCurrentThread(), L"fastload ngxearly");
	const ULONGLONG t0 = GetTickCount64();
	for (;;) {   // the device, and our hook on (a JMP at its start), or nothing
		const uint32_t fl = *g_flags;
		if ((fl & kDeviceDone) && Device(g_devGlobal) && *(volatile unsigned char*)g_target == 0xe9) break;
		if (GetTickCount64() - t0 > 180000) {
			Log("ngxearly: no D3D device or hook after 180 s (flags %08x) - not started", fl);
			return 0;
		}
		Sleep(2);
	}
	if (!(*g_flags & kNvidia)) { Log("ngxearly: not an NVIDIA card - not started"); return 0; }
	{
		std::lock_guard<std::mutex> lk(g_mu);
		if (g_state != kIdle) return 0;   // the game got there first
		g_state = kRunning;
	}
	LARGE_INTEGER a;
	QueryPerformanceCounter(&a);
	const double at = Ms(g_t0);
	StartArgs args = { &g_params, g_appId, 0 };
	const bool ok = (g_start(nullptr, &args) & 0xff) != 0;
	const double took = Ms(a);
	std::unique_lock<std::mutex> lk(g_mu);
	g_ok = ok;
	if (ok) g_state = kReady;
	else Release();
	g_cv.notify_all();
	Log("ngxearly: NVIDIA NGX started on thread %lu %.1f s after the install, %.0f ms (%s)", GetCurrentThreadId(),
	    at / 1000, took, ok ? "ok, waiting for the game's DLSS init" : "failed: shut down, the game runs its own");
	if (!ok) return 0;
	if (!g_cv.wait_for(lk, std::chrono::seconds(120), [] { return g_state != kReady; })) {
		Release();
		lk.unlock();
		WriteUsed(0);
		Log("ngxearly: the game didn't start DLSS within 120 s (DLSS off?): NGX shut down; the next launch skips it");
		return 0;
	}
	char* plugin = g_adopted;
	lk.unlock();
	WriteUsed(1);
	// Check (log only): the game creates its DLSS feature (FUN_181e11220, handle in +0x8e0) with the NGX it was handed
	const ULONGLONG t1 = GetTickCount64();
	while (GetTickCount64() - t1 < 300000 && !ReadPtr((uintptr_t)(plugin + 0x8e0))) Sleep(50);
	if (ReadPtr((uintptr_t)(plugin + 0x8e0)))
		Log("ngxearly: the game created its DLSS feature %.1f s after the hand-over", (GetTickCount64() - t1) / 1000.0);
	else Log("ngxearly: no DLSS feature 300 s after the hand-over (not created yet, or DLSS isn't running)");
	return 0;
}

}  // namespace

void Ngxearly_Install(uintptr_t base, void* self) {
	QueryPerformanceFrequency(&g_freq);
	QueryPerformanceCounter(&g_t0);
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	int mode = 1, used = 1;
	if (FILE* f = fopen((dir + "fastload_ngxearly.txt").c_str(), "r")) {
		if (fscanf(f, "%d", &mode) != 1) mode = 1;
		fclose(f);
	}
	g_usedPath = dir + "fastload_ngxearly_used.txt";
	if (FILE* f = fopen(g_usedPath.c_str(), "r")) {
		if (fscanf(f, "%d", &used) != 1) used = 1;
		fclose(f);
	}
	if (!Same(base + kRvaInit, kInitBytes, sizeof kInitBytes) || !Same(base + kRvaTeardown, kTeardownBytes, sizeof kTeardownBytes) ||
	    !Same(base + kRvaDevLoad, kDevLoadBytes, sizeof kDevLoadBytes) || !Same(base + kRvaAppId, kAppIdBytes, sizeof kAppIdBytes)) {
		Log("ngxearly: the DLSS init code differs (another game version?) - left alone");
		return;
	}
	const uintptr_t init = base + kRvaInit;
	g_flags = (volatile uint32_t*)Rel(init + 0x08, init + 0x10);
	g_start = (StartFn)Rel(init + 0x4a, init + 0x4e);
	const uintptr_t td = base + kRvaTeardown;
	g_destroy = (NgxFn)Rel(td + 0x3c, td + 0x40);
	g_devGlobal2 = Rel(td + 0x43, td + 0x47);
	g_shutdown = (NgxFn)Rel(td + 0x4c, td + 0x50);
	g_devGlobal = Rel(base + kRvaDevLoad + 3, base + kRvaDevLoad + 7);
	g_appId = *(const uint32_t*)(base + kRvaAppId + 6);
	g_target = init;
	if (mode != 1) { Log("ngxearly: off (fastload_ngxearly.txt %d) - the game's own NGX start", mode); return; }
	if (MH_CreateHook((void*)init, (void*)&HkInit, (void**)&g_origInit) != MH_OK || MH_EnableHook((void*)init) != MH_OK) {
		Log("ngxearly: hook failed - left alone");
		return;
	}
	if (!used) { Log("ngxearly: the game didn't start DLSS last launch - no early NGX start this time"); return; }
	if (HANDLE h = CreateThread(nullptr, 0, Starter, nullptr, 0, nullptr)) CloseHandle(h);
	Log("ngxearly: on - NVIDIA NGX starts as soon as the D3D device exists (app id %08x), handed to the game's DLSS init",
	    g_appId);
}
