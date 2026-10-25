// The D3D12 pipeline library made on a worker: see pipelib.h.
#include "pipelib.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <d3d12.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// WHGame 1.5.6
constexpr uintptr_t kRvaCreate = 0xba9f34;   // HRESULT (CDevice*): device(+0x10)->CreatePipelineLibrary(+0x140 blob, &+0x18)
const unsigned char kCreateBytes[] = { 0x48,0x83,0xec,0x38,0x4c,0x8b,0x89,0x40,0x01,0x00,0x00,0x4c,0x8b,0x81,0x48,0x01 };
constexpr size_t kDev = 0x10, kLib = 0x18, kBlobBegin = 0x140, kBlobEnd = 0x148, kNoLib = 0x158;

using CreateFn = HRESULT(__fastcall*)(void* dev);
CreateFn g_orig = nullptr;
long long g_freq = 1;
inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long a, long long b) { return (double)(b - a) * 1000.0 / g_freq; }
std::atomic<bool> g_waitLogged{ false };

class Proxy final : public ID3D12PipelineLibrary1 {
public:
	ID3D12Device1* dev;
	const void* blob;
	SIZE_T size;
	char* noLibFlag;
	ID3D12PipelineLibrary* real = nullptr;
	ID3D12PipelineLibrary1* real1 = nullptr;
	HANDLE done;
	std::atomic<ULONG> refs{ 1 };
	long long t0 = 0, t1 = 0;
	HRESULT hr = E_FAIL, hrEmpty = S_OK;

	Proxy(ID3D12Device1* d, const void* b, SIZE_T n, char* flag) : dev(d), blob(b), size(n), noLibFlag(flag) {
		dev->AddRef();
		done = CreateEventA(nullptr, TRUE, FALSE, nullptr);
	}
	static DWORD WINAPI Work(LPVOID p) { ((Proxy*)p)->Make(); return 0; }
	void Make() {
		t0 = Now();
		hr = dev->CreatePipelineLibrary(blob, size, __uuidof(ID3D12PipelineLibrary), (void**)&real);
		if (FAILED(hr) && size) {   // the game's fallback: an empty library
			hrEmpty = dev->CreatePipelineLibrary(nullptr, 0, __uuidof(ID3D12PipelineLibrary), (void**)&real);
			if (FAILED(hrEmpty)) real = nullptr;
		}
		if (!real) *noLibFlag = 1;
		else real->QueryInterface(__uuidof(ID3D12PipelineLibrary1), (void**)&real1);
		t1 = Now();
		Log("pipelib: the pipeline library (%.1f MB of cache) made on a worker in %.0f ms (HRESULT 0x%08x%s)", size / 1048576.0,
		    Ms(t0, t1), (unsigned)hr, FAILED(hr) ? (real ? "; an empty one instead, as the game does" : "; none: PSOs compile") : "");
		SetEvent(done);
	}
	void Wait() {
		if (WaitForSingleObject(done, 0) == WAIT_OBJECT_0) return;
		const long long a = Now();
		WaitForSingleObject(done, INFINITE);
		if (!g_waitLogged.exchange(true)) Log("pipelib: the first use of the library waited %.0f ms for the worker", Ms(a, Now()));
	}

	// IUnknown
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
		if (!out) return E_POINTER;
		if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
		    riid == __uuidof(ID3D12PipelineLibrary) || riid == __uuidof(ID3D12PipelineLibrary1)) {
			if (riid == __uuidof(ID3D12PipelineLibrary1)) { Wait(); if (!real1) { *out = nullptr; return E_NOINTERFACE; } }
			AddRef();
			*out = this;
			return S_OK;
		}
		Wait();
		if (!real) { *out = nullptr; return E_NOINTERFACE; }
		return real->QueryInterface(riid, out);
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
	ULONG STDMETHODCALLTYPE Release() override {
		const ULONG n = --refs;
		if (!n) {
			WaitForSingleObject(done, INFINITE);
			if (real1) real1->Release();
			if (real) real->Release();
			dev->Release();
			CloseHandle(done);
			delete this;
		}
		return n;
	}
	// ID3D12Object
	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, UINT* n, void* d) override { Wait(); return real ? real->GetPrivateData(g, n, d) : E_FAIL; }
	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, UINT n, const void* d) override { Wait(); return real ? real->SetPrivateData(g, n, d) : E_FAIL; }
	HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID g, const IUnknown* d) override { Wait(); return real ? real->SetPrivateDataInterface(g, d) : E_FAIL; }
	HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name) override { Wait(); return real ? real->SetName(name) : E_FAIL; }
	// ID3D12DeviceChild
	HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** out) override { return dev->QueryInterface(riid, out); }
	// ID3D12PipelineLibrary
	HRESULT STDMETHODCALLTYPE StorePipeline(LPCWSTR name, ID3D12PipelineState* p) override {
		Wait(); return real ? real->StorePipeline(name, p) : E_FAIL;
	}
	HRESULT STDMETHODCALLTYPE LoadGraphicsPipeline(LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, REFIID riid, void** out) override {
		Wait(); return real ? real->LoadGraphicsPipeline(name, d, riid, out) : E_INVALIDARG;   // E_INVALIDARG: not in the library
	}
	HRESULT STDMETHODCALLTYPE LoadComputePipeline(LPCWSTR name, const D3D12_COMPUTE_PIPELINE_STATE_DESC* d, REFIID riid, void** out) override {
		Wait(); return real ? real->LoadComputePipeline(name, d, riid, out) : E_INVALIDARG;
	}
	SIZE_T STDMETHODCALLTYPE GetSerializedSize() override { Wait(); return real ? real->GetSerializedSize() : 0; }
	HRESULT STDMETHODCALLTYPE Serialize(void* data, SIZE_T n) override { Wait(); return real ? real->Serialize(data, n) : E_FAIL; }
	// ID3D12PipelineLibrary1
	HRESULT STDMETHODCALLTYPE LoadPipeline(LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC* d, REFIID riid, void** out) override {
		Wait(); return real1 ? real1->LoadPipeline(name, d, riid, out) : E_INVALIDARG;
	}
};

HRESULT __fastcall HkCreate(void* devObj) {
	char* o = (char*)devObj;
	ID3D12Device1* dev = nullptr;
	IUnknown* d = *(IUnknown**)(o + kDev);
	if (!d || FAILED(d->QueryInterface(__uuidof(ID3D12Device1), (void**)&dev)) || !dev) return g_orig(devObj);
	const char* b = *(const char**)(o + kBlobBegin);
	const char* e = *(const char**)(o + kBlobEnd);
	Proxy* p = new Proxy(dev, b == e ? nullptr : b, (SIZE_T)(e - b), o + kNoLib);
	dev->Release();   // the proxy holds its own reference
	HANDLE t = p->done ? CreateThread(nullptr, 0, &Proxy::Work, p, 0, nullptr) : nullptr;
	if (!t) { p->Release(); Log("pipelib: no worker thread - the game makes the library"); return g_orig(devObj); }
	CloseHandle(t);
	*(ID3D12PipelineLibrary**)(o + kLib) = p;
	return S_OK;
}

}  // namespace

void Pipelib_Install(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	int mode = 1;
	if (FILE* fp = fopen((dir + "fastload_pipelib.txt").c_str(), "r")) {
		if (fscanf(fp, "%d", &mode) != 1) mode = 1;
		fclose(fp);
	}
	if (mode != 1) { Log("pipelib: off (fastload_pipelib.txt)"); return; }
	void* t = (void*)(base + kRvaCreate);
	if (memcmp(t, kCreateBytes, sizeof kCreateBytes)) { Log("pipelib: the pipeline library code differs (another game version?) - off"); return; }
	if (MH_CreateHook(t, (void*)&HkCreate, (void**)&g_orig) != MH_OK || MH_EnableHook(t) != MH_OK) { Log("pipelib: hook failed - off"); return; }
	Log("pipelib: on (the D3D12 pipeline library is made on a worker; its first use waits for it; fastload_pipelib.txt)");
}
