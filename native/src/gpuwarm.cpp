// The graphics driver loaded on a worker: see gpuwarm.h.
#include "gpuwarm.h"
#include "early.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <intrin.h>
#include <dxgi1_6.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// WHGame 1.5.6
constexpr uintptr_t kRvaQuery = 0x5f98ac;         // char (char* desc, size_t, u32*, u32*, u64* mem, int* level)
const unsigned char kQueryBytes[] = { 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0x80 };
constexpr uintptr_t kRvaBugsplatRet = 0x1dd93b9;  // after FUN_181dd9260's call of it
const unsigned char kBugsplatCall[] = { 0xe8 };   // the call instruction at kRvaBugsplatRet - 5
constexpr uintptr_t kRvaIidFactory1 = 0x3a40838;  // the IIDs FUN_1805f8c78 uses
constexpr uintptr_t kRvaIidFactory6 = 0x3a40878;
constexpr uintptr_t kRvaIidAdapter = 0x3a407f8;
constexpr uintptr_t kRvaIidDevice = 0x3dba500;    // ID3D12Device (FUN_181dd8940)
constexpr uintptr_t kRvaLevels = 0x3dba530;       // its feature levels, 12_1 .. 11_0
const UINT kLevels[] = { 0xc100, 0xc000, 0xb100, 0xb000 };

using QueryFn = char(__fastcall*)(char* desc, size_t n, unsigned* a, unsigned* b, unsigned long long* mem, int* level);
QueryFn g_orig = nullptr;
uintptr_t g_base = 0;
HANDLE g_done = nullptr;
std::atomic<bool> g_started{ false }, g_skipped{ false }, g_waitLogged{ false };
std::atomic<int> g_state{ 0 };   // 0 not installed, 1 on, -1 off by file, -2 code differs, -3 hook failed
long long g_freq = 1, g_t0 = 0, g_t1 = 0;
int g_adapters = 0, g_ok = 0;
char g_note[160] = "";

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long a, long long b) { return (double)(b - a) * 1000.0 / g_freq; }

using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
using D3D12CreateFn = HRESULT(WINAPI*)(IUnknown*, UINT, REFIID, void**);
using D3D11CreateFn = HRESULT(WINAPI*)(IDXGIAdapter*, UINT, HMODULE, UINT, const UINT*, UINT, UINT, void**, UINT*, void**);

// FUN_1805f8c78's D3D calls, without the log lines and with nothing kept.
DWORD WINAPI Warm(LPVOID) {
	g_t0 = Now();
	const IID& iidF1 = *(const IID*)(g_base + kRvaIidFactory1);
	const IID& iidF6 = *(const IID*)(g_base + kRvaIidFactory6);
	const IID& iidA = *(const IID*)(g_base + kRvaIidAdapter);
	const IID& iidD = *(const IID*)(g_base + kRvaIidDevice);
	auto mkF = (CreateFactoryFn)GetProcAddress(LoadLibraryA("dxgi.dll"), "CreateDXGIFactory1");
	auto d3d11 = (D3D11CreateFn)GetProcAddress(LoadLibraryA("d3d11.dll"), "D3D11CreateDevice");
	auto d3d12 = (D3D12CreateFn)GetProcAddress(LoadLibraryA("d3d12.dll"), "D3D12CreateDevice");
	IUnknown* f1 = nullptr;
	if (!mkF || FAILED(mkF(iidF1, (void**)&f1)) || !f1) { snprintf(g_note, sizeof g_note, "no DXGI factory"); }
	else {
		IDXGIFactory6* f6 = nullptr;
		f1->QueryInterface(iidF6, (void**)&f6);
		if (f6 && (d3d11 || d3d12)) {
			for (UINT i = 0;; i++) {
				IDXGIAdapter1* a = nullptr;
				if (f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, iidA, (void**)&a) != S_OK) break;
				if (!a) continue;
				g_adapters++;
				IDXGIOutput* o = nullptr;
				if (SUCCEEDED(a->EnumOutputs(0, &o)) && o) o->Release();
				DXGI_ADAPTER_DESC1 d;
				a->GetDesc1(&d);
				bool ok = false;
				for (UINT lv : kLevels)
					if (d3d12 && SUCCEEDED(d3d12(a, lv, iidD, nullptr))) { ok = true; break; }
				if (!ok && d3d11) {
					const UINT levels[7] = { 0xb100, 0xb000, 0xa100, 0xa000, 0x9300, 0x9200, 0x9100 };
					UINT got = 0;
					ok = SUCCEEDED(d3d11(a, 0, nullptr, 0, levels, 7, 7, nullptr, &got, nullptr));
				}
				g_ok += ok;
				a->Release();
			}
		} else snprintf(g_note, sizeof g_note, "no IDXGIFactory6 / D3D");
		if (f6) f6->Release();
		f1->Release();
	}
	g_t1 = Now();
	SetEvent(g_done);
	return 0;
}

char __fastcall HkQuery(char* desc, size_t n, unsigned* a, unsigned* b, unsigned long long* mem, int* level) {
	if (g_started && (uintptr_t)_ReturnAddress() == g_base + kRvaBugsplatRet && !g_skipped.exchange(true)) {
		if (desc) *desc = 0;   // what the game's wrapper leaves when the query fails
		*a = 0; *b = 0; *mem = 0; *level = 0;
		return 0;
	}
	if (g_started && WaitForSingleObject(g_done, 0) != WAIT_OBJECT_0) {
		const long long w = Now();
		WaitForSingleObject(g_done, 20000);
		const double ms = Ms(w, Now());
		if (!g_waitLogged.exchange(true)) {
			char from[96];
			Early_Where((unsigned long long)_ReturnAddress(), g_base, 0x6000000, from, sizeof from);
			Log("gpuwarm: the GPU query from %s waited %.0f ms for the worker", from, ms);
		}
	}
	return g_orig(desc, n, a, b, mem, level);
}

}  // namespace

bool Gpuwarm_EarlyInstall(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	g_base = base;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	int mode = 1;
	if (FILE* fp = fopen((dir + "fastload_gpuwarm.txt").c_str(), "r")) {
		if (fscanf(fp, "%d", &mode) != 1) mode = 1;
		fclose(fp);
	}
	if (mode != 1) { g_state = -1; return false; }
	if (memcmp((void*)(base + kRvaQuery), kQueryBytes, sizeof kQueryBytes) ||
	    memcmp((void*)(base + kRvaBugsplatRet - 5), kBugsplatCall, sizeof kBugsplatCall) ||
	    (int32_t)(base + kRvaQuery - (base + kRvaBugsplatRet)) != *(const int32_t*)(base + kRvaBugsplatRet - 4) ||
	    memcmp((void*)(base + kRvaLevels), kLevels, sizeof kLevels)) { g_state = -2; return false; }
	void* t = (void*)(base + kRvaQuery);
	if (MH_CreateHook(t, (void*)&HkQuery, (void**)&g_orig) != MH_OK) { g_state = -3; return false; }
	if (MH_EnableHook(t) != MH_OK) { MH_RemoveHook(t); g_state = -3; return false; }
	g_done = CreateEventA(nullptr, TRUE, FALSE, nullptr);
	HANDLE th = g_done ? CreateThread(nullptr, 0, Warm, nullptr, 0, nullptr) : nullptr;   // runs once the loader is done
	if (!th) { MH_DisableHook(t); g_state = -3; return false; }
	CloseHandle(th);
	g_started = true;
	g_state = 1;
	return true;
}

void Gpuwarm_Report() {
	switch (g_state.load()) {
	case 0: Log("gpuwarm: off (the plugin loaded after the game's start-up code: global.ini DontLoadFromDllMain=0)"); return;
	case -1: Log("gpuwarm: off (fastload_gpuwarm.txt)"); return;
	case -2: Log("gpuwarm: the GPU query code differs (another game version?) - off"); return;
	case -3: Log("gpuwarm: hook or thread failed - off"); return;
	}
	Log("gpuwarm: on (the graphics driver loads on a worker; the crash reporter's GPU query is skipped; fastload_gpuwarm.txt)");
}

void Gpuwarm_Frame() {
	static std::atomic<bool> done{ false };
	if (g_state != 1 || done.exchange(true)) return;
	if (WaitForSingleObject(g_done, 0) == WAIT_OBJECT_0)
		Log("gpuwarm: the driver warm-up took %.0f ms on the worker (%d adapters, %d usable%s%s); the crash reporter's query %s",
		    Ms(g_t0, g_t1), g_adapters, g_ok, g_note[0] ? "; " : "", g_note, g_skipped ? "skipped" : "never came");
	else Log("gpuwarm: the driver warm-up is still running at the first frame");
}
