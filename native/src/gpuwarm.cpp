// The graphics driver loaded on a worker: see gpuwarm.h.
#include "gpuwarm.h"
#include "early.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <intrin.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// WHGame 1.5.6
constexpr uintptr_t kRvaQuery = 0x5f98ac;         // char (char* desc, size_t, u32*, u32*, u64* mem, int* level)
const unsigned char kQueryBytes[] = { 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0x80 };
constexpr uintptr_t kRvaBugsplatRet = 0x1dd93b9;  // after FUN_181dd9260's call of it (BugSplat's "GPU Info")
constexpr uintptr_t kRvaSysinfoRet = 0xdcfef2;    // after FUN_180dcfe0c's (ISystem +0x548, the system info BugSplat asks
                                                  // for first: "CPU Info"); boot 116: this one waited 689 ms for the worker
constexpr uintptr_t kRvaIidFactory1 = 0x3a40838;  // the IIDs FUN_1805f8c78 uses
constexpr uintptr_t kRvaIidFactory6 = 0x3a40878;
constexpr uintptr_t kRvaIidAdapter = 0x3a407f8;
constexpr uintptr_t kRvaIidDevice = 0x3dba500;    // ID3D12Device (FUN_181dd8940)
constexpr uintptr_t kRvaLevels = 0x3dba530;       // its feature levels, 12_1 .. 11_0
const UINT kLevels[] = { 0xc100, 0xc000, 0xb100, 0xb000 };
// devahead: the renderer's device (FUN_181df7bc0: CDevice*, adapter wrapper (+0x18 IDXGIAdapter), out level) made ahead
constexpr uintptr_t kRvaDevCreate = 0x1df7bc0;
const unsigned char kDevCreateBytes[] = { 0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x41,0x56,0x48,0x8d,0x6c,0x24,0xb9,0x48,0x81 };
constexpr uintptr_t kRvaDebugLayer = 0x55712bc;   // r_EnableDebugLayer: enabling it with a device alive removes the device
constexpr uintptr_t kRvaDred = 0x55712a4;         // r_D3D12EnableDRED: its settings apply to devices made after them
constexpr uintptr_t kRvaAftermath = 0x50dc264;    // r_EnableNvidiaAftermath: crash dumps only for devices made after
using DevCreateFn = char(__fastcall*)(void* dev, void* adapter, unsigned* level);
DevCreateFn g_origDev = nullptr;
ID3D12Device* g_dev = nullptr;   // ours, until the renderer's own creation has its reference
UINT g_devLevel = 0;
LUID g_devLuid{};
long long g_dt0 = 0, g_dt1 = 0;
HRESULT g_devHr = S_FALSE;
bool g_devahead = false;

using QueryFn = char(__fastcall*)(char* desc, size_t n, unsigned* a, unsigned* b, unsigned long long* mem, int* level);
QueryFn g_orig = nullptr;
uintptr_t g_base = 0;
HANDLE g_done = nullptr, g_devDone = nullptr;   // the support checks done; the device made (or not)
std::atomic<bool> g_started{ false }, g_skipped{ false }, g_skippedSys{ false }, g_waitLogged{ false };
std::atomic<int> g_state{ 0 };   // 0 not installed, 1 on, -1 off by file, -2 code differs, -3 hook failed
long long g_freq = 1, g_t0 = 0, g_t1 = 0, g_tInstall = 0;   // g_tInstall: DllMain (the worker made)
int g_adapters = 0, g_ok = 0, g_devMode = 1;
char g_note[160] = "";

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long a, long long b) { return (double)(b - a) * 1000.0 / g_freq; }

// nvcamera (gpuwarm.h): the refusal is gone; a 1 in fastload_nvcamera.txt is only reported
int g_camMode = 0;
int g_cacheWarm = 0;   // nvcachewarm: 1 on
HRESULT g_cacheHr = S_FALSE;
double g_cacheMs = -1;
using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
using D3D12CreateFn = HRESULT(WINAPI*)(IUnknown*, UINT, REFIID, void**);
using D3D11CreateFn = HRESULT(WINAPI*)(IDXGIAdapter*, UINT, HMODULE, UINT, const UINT*, UINT, UINT, void**, UINT*, void**);

// FUN_1805f8c78's D3D calls, without the log lines and with nothing kept.
DWORD WINAPI Warm(LPVOID) {
	Early_NameThread(L"JobWorker_fastload gpuwarm");
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
			// devahead: the device the renderer will ask for (FUN_180c19664: the first adapter by high-performance
			// preference with a display output and a D3D12 device, levels 12_1 .. 11_0). D3D12 devices are one per
			// adapter in a process: the renderer's D3D12CreateDevice then gets this one back. First: with a device alive the
			// driver stays loaded, so the support checks below and the game's own GPU queries don't load it again (boot 120:
			// main's query at 3.4 s re-checked the driver's signatures, imagehlp, after the worker's checks had let it go).
			g_dt0 = Now();
			for (UINT i = 0; g_devahead && d3d12 && !g_dev; i++) {
				IDXGIAdapter1* a = nullptr;
				if (f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, iidA, (void**)&a) != S_OK) break;
				if (!a) continue;
				IDXGIOutput* o = nullptr;
				const bool out = SUCCEEDED(a->EnumOutputs(0, &o)) && o;
				if (o) o->Release();
				if (out)
					for (UINT lv : kLevels)
						if (SUCCEEDED(g_devHr = d3d12(a, lv, iidD, (void**)&g_dev)) && g_dev) { g_devLevel = lv; break; }
				if (g_dev) { DXGI_ADAPTER_DESC1 d; a->GetDesc1(&d); g_devLuid = d.AdapterLuid; }
				a->Release();
			}
			g_dt1 = Now();
			ID3D12Device* warmDev = g_cacheWarm == 1 ? g_dev : nullptr;   // nvcachewarm, below: our own reference
			if (warmDev) warmDev->AddRef();
			SetEvent(g_devDone);
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
			SetEvent(g_done);
			if (warmDev) {   // nvcachewarm (gpuwarm.h): the driver's shader disk cache set up by one root signature
				const long long c0 = Now();
				auto ser = (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)GetProcAddress(GetModuleHandleA("d3d12.dll"), "D3D12SerializeRootSignature");
				if (*(volatile int*)(g_base + kRvaDebugLayer) || *(volatile int*)(g_base + kRvaDred)) g_cacheHr = E_ABORT;   // keep their rule
				else if (ser) {
					// not empty: boot 170C1's empty one took 0 ms and left the cache set-up to the render thread
					D3D12_DESCRIPTOR_RANGE ranges[2] = {};
					ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[0].NumDescriptors = 8;
					ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
					ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[1].NumDescriptors = 2;
					ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
					D3D12_ROOT_PARAMETER params[3] = {};
					params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
					params[0].Constants.Num32BitValues = 4;
					params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
					params[1].Descriptor.ShaderRegister = 1;
					params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
					params[2].DescriptorTable.NumDescriptorRanges = 2;
					params[2].DescriptorTable.pDescriptorRanges = ranges;
					D3D12_STATIC_SAMPLER_DESC smp = {};
					smp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
					smp.AddressU = smp.AddressV = smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
					smp.MaxLOD = D3D12_FLOAT32_MAX;
					smp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
					D3D12_ROOT_SIGNATURE_DESC rd{};
					rd.NumParameters = 3; rd.pParameters = params;
					rd.NumStaticSamplers = 1; rd.pStaticSamplers = &smp;
					rd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
					ID3DBlob* blob = nullptr;
					ID3DBlob* err = nullptr;
					g_cacheHr = ser(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
					if (SUCCEEDED(g_cacheHr) && blob) {
						ID3D12RootSignature* rs = nullptr;
						g_cacheHr = warmDev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&rs);
						if (rs) rs->Release();
					}
					if (blob) blob->Release();
					if (err) err->Release();
				}
				g_cacheMs = Ms(c0, Now());
				warmDev->Release();
			}
		} else snprintf(g_note, sizeof g_note, "no IDXGIFactory6 / D3D");
		if (f6) f6->Release();
		f1->Release();
	}
	g_t1 = Now();
	SetEvent(g_done);
	SetEvent(g_devDone);
	return 0;
}

char __fastcall HkQuery(char* desc, size_t n, unsigned* a, unsigned* b, unsigned long long* mem, int* level) {
	const uintptr_t ret = (uintptr_t)_ReturnAddress();
	if (g_started && ((ret == g_base + kRvaBugsplatRet && !g_skipped.exchange(true)) ||
	                  (ret == g_base + kRvaSysinfoRet && !g_skippedSys.exchange(true)))) {   // each the first time
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

// The renderer's device creation: waits for the worker, then runs as before; D3D12 hands it the worker's device.
char __fastcall HkDevCreate(void* devObj, void* adapter, unsigned* level) {
	const long long w = Now();
	if (g_started) WaitForSingleObject(g_devDone, 30000);
	const double waited = Ms(w, Now());
	ID3D12Device* mine = g_dev;
	g_dev = nullptr;
	const char* note = "";
	if (mine && (*(volatile int*)(g_base + kRvaDebugLayer) || *(volatile int*)(g_base + kRvaDred))) {
		mine->Release();   // the last reference: gone before the game's debug layer / DRED set-up
		mine = nullptr;
		note = " (debug layer or DRED on: the worker's device let go first, the game made its own)";
	}
	const long long t0 = Now();
	const char r = g_origDev(devObj, adapter, level);
	const long long t1 = Now();
	bool same = false;
	if (mine && r) {
		IUnknown* theirs = *(IUnknown**)((char*)devObj + 0x10);   // the device interface FUN_181df7bc0 keeps
		IUnknown *u1 = nullptr, *u2 = nullptr;
		if (theirs) theirs->QueryInterface(__uuidof(IUnknown), (void**)&u1);
		mine->QueryInterface(__uuidof(IUnknown), (void**)&u2);
		same = u1 && u1 == u2;
		if (u1) u1->Release();
		if (u2) u2->Release();
	}
	if (mine) mine->Release();   // the game holds its own reference now
	static std::atomic<bool> logged{ false };
	if (!logged.exchange(true))
		Log("devahead: the renderer's device creation took %.0f ms (waited %.0f ms for the worker; the worker's device: %s, %.0f ms, "
		    "level %x, HRESULT 0x%08x; %s%s%s)", Ms(t0, t1), waited, g_devahead ? (g_devLevel ? "made" : "not made") : "off",
		    Ms(g_dt0, g_dt1), g_devLevel, (unsigned)g_devHr, same ? "the same device came back" : "not the same device",
		    *(volatile int*)(g_base + kRvaAftermath) ? "; Aftermath on: its GPU crash dumps miss this device" : "", note);
	return r;
}

}  // namespace

bool Gpuwarm_EarlyInstall(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	g_tInstall = Now();
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
	if (FILE* fp = fopen((dir + "fastload_nvcamera.txt").c_str(), "r")) {
		if (fscanf(fp, "%d", &g_camMode) != 1) g_camMode = 0;
		fclose(fp);
	}
	if (FILE* fp = fopen((dir + "fastload_nvcachewarm.txt").c_str(), "r")) {
		if (fscanf(fp, "%d", &g_cacheWarm) != 1) g_cacheWarm = 0;
		fclose(fp);
	}
	if (mode != 1) { g_state = -1; return false; }
	int dmode = 1;
	if (FILE* fp = fopen((dir + "fastload_devahead.txt").c_str(), "r")) {
		if (fscanf(fp, "%d", &dmode) != 1) dmode = 1;
		fclose(fp);
	}
	auto callsQuery = [&](uintptr_t ret) {   // a call rel32 to the query just before the return address
		return *(const unsigned char*)(base + ret - 5) == 0xe8 && *(const int32_t*)(base + ret - 4) == (int32_t)(kRvaQuery - ret);
	};
	if (memcmp((void*)(base + kRvaQuery), kQueryBytes, sizeof kQueryBytes) || !callsQuery(kRvaBugsplatRet) || !callsQuery(kRvaSysinfoRet) ||
	    memcmp((void*)(base + kRvaLevels), kLevels, sizeof kLevels)) { g_state = -2; return false; }
	void* t = (void*)(base + kRvaQuery);
	if (MH_CreateHook(t, (void*)&HkQuery, (void**)&g_orig) != MH_OK) { g_state = -3; return false; }
	if (MH_EnableHook(t) != MH_OK) { MH_RemoveHook(t); g_state = -3; return false; }
	void* dc = (void*)(base + kRvaDevCreate);
	g_devMode = dmode;
	g_devahead = dmode == 1 && !memcmp(dc, kDevCreateBytes, sizeof kDevCreateBytes) &&
	             MH_CreateHook(dc, (void*)&HkDevCreate, (void**)&g_origDev) == MH_OK && MH_EnableHook(dc) == MH_OK;
	g_done = CreateEventA(nullptr, TRUE, FALSE, nullptr);
	g_devDone = CreateEventA(nullptr, TRUE, FALSE, nullptr);
	HANDLE th = g_done && g_devDone ? CreateThread(nullptr, 0, Warm, nullptr, 0, nullptr) : nullptr;   // runs once the loader is done
	if (!th) { MH_DisableHook(t); g_state = -3; return false; }
	CloseHandle(th);
	g_started = true;
	g_state = 1;
	return true;
}

void Gpuwarm_Report() {
	if (g_camMode == 1)
		Log("nvcamera: fastload_nvcamera.txt 1 ignored - the NVIDIA driver fails the D3D12 device without NvCamera64.dll (boot "
		    "170N1: no usable GPU, the game hung before the menu); the driver's own");
	switch (g_state.load()) {
	case 0: Log("gpuwarm: off (the plugin loaded after the game's start-up code: global.ini DontLoadFromDllMain=0)"); return;
	case -1: Log("gpuwarm: off (fastload_gpuwarm.txt)"); return;
	case -2: Log("gpuwarm: the GPU query code differs (another game version?) - off"); return;
	case -3: Log("gpuwarm: hook or thread failed - off"); return;
	}
	Log("gpuwarm: on (the graphics driver loads on a worker; the crash reporter's GPU query is skipped; fastload_gpuwarm.txt)");
	Log("devahead: %s", g_devahead ? "on (the renderer's D3D12 device made on the same worker; fastload_devahead.txt)"
	                               : g_devMode != 1 ? "off (fastload_devahead.txt)" : "the device creation code differs or its hook failed - off");
}

void Gpuwarm_Frame() {
	static std::atomic<bool> done{ false };
	if (g_state != 1 || done.exchange(true)) return;
	if (WaitForSingleObject(g_done, 0) == WAIT_OBJECT_0)
		Log("gpuwarm: the driver warm-up took %.0f ms on the worker, from %.0f ms after the plugin's DllMain (%d adapters, %d usable%s%s); "
		    "the crash reporter's two queries: %s, %s",
		    Ms(g_t0, g_t1), Ms(g_tInstall, g_t0), g_adapters, g_ok, g_note[0] ? "; " : "", g_note,
		    g_skippedSys ? "system info skipped" : "system info never came",
		    g_skipped ? "GPU info skipped" : "GPU info never came");
	else Log("gpuwarm: the driver warm-up is still running at the first frame");
	if (g_cacheWarm == 1)
		Log("nvcachewarm: %s", g_cacheMs < 0 ? "not run (no worker device)" : g_cacheHr == E_ABORT ? "skipped (debug layer or DRED on)" : "");
	if (g_cacheWarm == 1 && g_cacheMs >= 0 && g_cacheHr != E_ABORT)
		Log("nvcachewarm: one root signature on the worker's device: %.0f ms (HRESULT 0x%08x); the driver's shader cache was set up "
		    "before the render thread's first one", g_cacheMs, (unsigned)g_cacheHr);
}
