// The mod's own 2D drawing layer (work/BRIEF.md): Dear ImGui v1.91.5 (third_party/imgui) with its DX12 renderer
// backend draws the commands the screen code fills (Ovl_Begin / Ovl_Rect / Ovl_Text / Ovl_Image / Ovl_End) onto the
// game's back buffer, without Scaleform. The D3D12 side copies the pattern of kcd2-render's probe_encode.cpp: hook
// ID3D12CommandQueue::ExecuteCommandLists (slot 10) and take the first DIRECT queue it sees, and keep every
// engine-facing call inside __try; here the queue is only *found* that way, the submission happens from the Present
// hook.
//
// Hooks (MinHook, installed once from Overlay_Install on the plugin's start-up thread):
//   ID3D12CommandQueue::ExecuteCommandLists  vtable slot 10  (learn the game's graphics queue; pass everything through)
//   IDXGISwapChain::Present                  vtable slot  8  (draw, then the original Present)
//   IDXGISwapChain1::Present1                vtable slot 22  (the same, for the *1 signature)
//   IDXGISwapChain::ResizeBuffers            vtable slot 13  (release our back-buffer refs before the original)
// The vtable addresses come from a throw-away D3D12 device + DIRECT queue + flip-model swap chain on a hidden 100x100
// window, made once at install and released at once (slots per d3d12.h's ID3D12CommandQueueVtbl and dxgi.h's
// IDXGISwapChainVtbl / dxgi1_2.h's IDXGISwapChain1Vtbl; verified at runtime with a probe swap chain: calling slot 8
// moved GetLastPresentCount to 1, slot 22 to 2, slot 13 resized the desc to 50x50, slot 10 accepted an empty
// submission with S_OK).
//
// In Present: the list the main thread filled is taken (two lists under a critical section, swapped once per frame);
// on the first frame that has something to draw the device, one RTV heap, one command allocator per back buffer, one
// command list, a fence and the ImGui DX12 backend are created; then each frame: wait for that back buffer's
// allocator fence, Reset, barrier PRESENT -> RENDER_TARGET, upload any pending picture, render, barrier back, Close,
// ExecuteCommandLists on the captured queue, Signal. Nothing is created until the screen first draws.
// Any exception or failed HRESULT in our own work: log once, turn the overlay off for the session, keep passing
// everything through. The game must never crash because of us.
#include "overlay.h"
#include "log.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <MinHook.h>
#include <cstring>
#include <cstdio>
#include <vector>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")   // imgui_impl_dx12.cpp pragma-links d3dcompiler (D3DCompile of its two shaders)

namespace {

constexpr int kMaxCmds = 4096;            // one frame's commands; past that the extra ones are dropped (logged once)
constexpr int kTextBytes = 240;           // one text command's UTF-8 string, truncated to this
constexpr unsigned kSlots = 256;          // our SRV heap: slot 0 = ImGui's font, 1..255 = pictures (Ovl_LoadImage)
constexpr int kMaxBackBuffers = 8;        // a flip-model swap chain has 2-4; 8 is the DXGI cap
constexpr int kMaxRetire = 256;           // picture upload buffers waiting for their frame's fence
constexpr DWORD kFenceWaitMs = 5000;      // a wait longer than this means the GPU is gone: overlay off, game keeps going

// our own code (no exception was thrown)
constexpr uint32_t kHrFailed = 0xE0000001;   // a D3D12/DXGI call returned a failed HRESULT (logged with it)

enum CmdType { CmdRect = 1, CmdText = 2, CmdImage = 3 };

struct Cmd {                              // POD: the lists are swapped, never copied element by element
	int type;
	float x, y, w, h;                     // w,h: the rectangle / picture; for text: h is size_px
	unsigned rgba, tex;
	char text[kTextBytes];
};

struct FaultRecord { uint32_t code; void* at; };
#define OVL_SEH(f) __except ((f).code = GetExceptionCode(), (f).at = (void*)GetExceptionInformation()->ExceptionRecord->ExceptionAddress, EXCEPTION_EXECUTE_HANDLER)

struct GfxOut { HRESULT hr; const char* what; };

// ---- globals -------------------------------------------------------------------------------------------------
HMODULE g_self = nullptr;
char g_dir[MAX_PATH] = {};                 // the .asi's folder (overlay_test.png lives there)
bool g_installed = false;
volatile LONG g_off = 0;                   // final: an exception / failed call turned the overlay off for the session
char g_lastError[192] = {};                // the first failure, for "overlay stats"

// the hooks (the originals are written only by Overlay_Install, before the hooks are enabled)
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
PresentFn g_origPresent = nullptr;
Present1Fn g_origPresent1 = nullptr;
ResizeFn g_origResize = nullptr;
ExecuteFn g_origExecute = nullptr;
ID3D12CommandQueue* volatile g_queue = nullptr;   // the first DIRECT queue ExecuteCommandLists is called on
IDXGISwapChain* volatile g_sc = nullptr;          // the first swap chain that presents (raw: the game owns it)
volatile LONG g_test = 0, g_testImg = 0, g_testNoImage = 0, g_framesDrawn = 0;
volatile LONGLONG g_cpuTicks = 0;                 // our Present work, in QPC ticks
LONGLONG g_qpcFreq = 0;

// the draw list: the producer fills g_back, End swaps it into g_front, the render thread swaps g_front into g_render
CRITICAL_SECTION g_listCs, g_renderCs;
std::vector<Cmd> g_back, g_front, g_render;      // under g_listCs (g_render: the render thread's)
bool g_hasFrame = false;                         // under g_listCs
bool g_droppedLogged = false;
LARGE_INTEGER g_lastTick = {};                   // render thread only: the previous Present, for io.DeltaTime

// Ovl_LoadImage requests waiting for the render thread (under g_listCs); g_imgWork is the render thread's batch
struct Pend { unsigned slot; char path[MAX_PATH + 1]; };
std::vector<Pend> g_imgQueue, g_imgWork;
unsigned g_imgNext = 1;                          // 1..255

// ---- D3D12 state (render thread only, after Overlay_Install) -------------------------------------------------
ID3D12Device* g_dev = nullptr;
IDXGISwapChain3* g_sc3 = nullptr;                 // raw: for GetCurrentBackBufferIndex
ID3D12DescriptorHeap* g_rtvHeap = nullptr;
UINT g_rtvStride = 0, g_bbCount = 0;
DXGI_FORMAT g_format = DXGI_FORMAT_UNKNOWN;
float g_w = 0, g_h = 0;
ID3D12Resource* g_backBuffers[kMaxBackBuffers] = {};
ID3D12DescriptorHeap* g_srvHeap = nullptr;
UINT g_srvStride = 0;
ID3D12CommandAllocator* g_allocs[kMaxBackBuffers] = {};
UINT g_allocCount = 0;
UINT64 g_bbFence[kMaxBackBuffers] = {};           // the last fence value each back buffer's allocator was used with
ID3D12GraphicsCommandList* g_list = nullptr;
ID3D12Fence* g_fence = nullptr;
UINT64 g_fenceValue = 0;
HANDLE g_fenceEvent = nullptr;
bool g_resizePending = false;
ID3D12Resource* g_imgRes[kSlots] = {};
D3D12_GPU_DESCRIPTOR_HANDLE g_imgGpu[kSlots] = {};
bool g_imgCreated[kSlots] = {};
ID3D12Resource* g_retireRes[kMaxRetire] = {};
UINT64 g_retireFence[kMaxRetire] = {};
int g_nRetire = 0;

// ---- small helpers -------------------------------------------------------------------------------------------
D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
	D3D12_RESOURCE_BARRIER b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = r;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	b.Transition.StateBefore = from;
	b.Transition.StateAfter = to;
	return b;
}

D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle(UINT i) {
	D3D12_CPU_DESCRIPTOR_HANDLE h = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += (SIZE_T)i * g_rtvStride;
	return h;
}
D3D12_CPU_DESCRIPTOR_HANDLE SrvCpu(unsigned i) {
	D3D12_CPU_DESCRIPTOR_HANDLE h = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += (SIZE_T)i * g_srvStride;
	return h;
}
D3D12_GPU_DESCRIPTOR_HANDLE SrvGpu(unsigned i) {
	D3D12_GPU_DESCRIPTOR_HANDLE h = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
	h.ptr += (UINT64)i * g_srvStride;
	return h;
}

// the first failure sticks: it is logged once and is what "overlay stats" reports
void Fail(const char* what, HRESULT hr, uint32_t code, const FaultRecord* f) {
	if (InterlockedExchange(&g_off, 1) == 0) {
		if (code) snprintf(g_lastError, sizeof g_lastError, "exception 0x%08X at %p (%s)", code, f ? f->at : nullptr, what);
		else snprintf(g_lastError, sizeof g_lastError, "hr=0x%08lX (%s)", (unsigned long)hr, what);
		if (code) Log("overlay: %s: EXCEPTION 0x%08X at %p: overlay off for the rest of the session (the game keeps drawing)", what ? what : "?", code, f ? f->at : nullptr);
		else Log("overlay: %s: failed hr=0x%08lX: overlay off for the rest of the session (the game keeps drawing)", what ? what : "?", (unsigned long)hr);
	}
}

// ---- SEH wrappers: every D3D12 / DXGI call of ours is in the plain functions below, called from here -------------
uint32_t SehQueueDesc(ID3D12CommandQueue* q, D3D12_COMMAND_QUEUE_DESC* d, FaultRecord* f) {
	__try { *d = q->GetDesc(); return 0; }
	OVL_SEH(*f) { return f->code; }
}

struct ProbeOut { GfxOut g; void* ecl; void* present; void* present1; void* resize; };
// a throw-away device, queue and flip-model swap chain on a hidden window: the shared vtables, then all released
uint32_t SehProbe(ProbeOut* o, FaultRecord* f) {
	__try {
		o->g.hr = S_OK; o->g.what = "the hidden probe window";
		HWND wnd = CreateWindowExA(0, "STATIC", "deagle_native overlay probe", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, nullptr, nullptr);
		if (!wnd) { o->g.hr = E_FAIL; return kHrFailed; }
		ID3D12Device* dev = nullptr;
		o->g.what = "D3D12CreateDevice (probe)";
		o->g.hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev));
		if (FAILED(o->g.hr) || !dev) { DestroyWindow(wnd); return kHrFailed; }
		D3D12_COMMAND_QUEUE_DESC qd = {}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		ID3D12CommandQueue* q = nullptr;
		o->g.what = "CreateCommandQueue (probe)";
		o->g.hr = dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q));
		IDXGIFactory2* fac = nullptr;
		if (SUCCEEDED(o->g.hr) && q) { o->g.what = "CreateDXGIFactory1 (probe)"; o->g.hr = CreateDXGIFactory1(IID_PPV_ARGS(&fac)); }
		DXGI_SWAP_CHAIN_DESC1 sd = {};
		sd.Width = 100; sd.Height = 100; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.SampleDesc.Count = 1;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		IDXGISwapChain1* sc = nullptr;
		if (SUCCEEDED(o->g.hr) && q && fac) { o->g.what = "CreateSwapChainForHwnd (probe)"; o->g.hr = fac->CreateSwapChainForHwnd(q, wnd, &sd, nullptr, nullptr, &sc); }
		if (FAILED(o->g.hr) || !q || !sc) {
			if (sc) sc->Release();
			if (fac) fac->Release();
			if (q) q->Release();
			dev->Release();
			DestroyWindow(wnd);
			return kHrFailed;
		}
		o->g.what = "the probe vtables";
		o->ecl = (*(void***)q)[10];
		o->present = (*(void***)sc)[8];
		o->present1 = (*(void***)sc)[22];
		o->resize = (*(void***)sc)[13];
		sc->Release(); fac->Release(); q->Release(); dev->Release(); DestroyWindow(wnd);
		return 0;
	}
	OVL_SEH(*f) { return f->code; }
}

// the swap chain's device, format and buffers, our heaps, allocators, list, fence and the ImGui DX12 backend
uint32_t SehInitGfx(IDXGISwapChain* sc, GfxOut* o, FaultRecord* f) {
	__try {
		o->hr = S_OK; o->what = "";
		ID3D12Device* dev = nullptr;
		// D3D12 note: ID3D12Device does not implement IDXGIDevice, so ask GetDevice for the device itself (this is the
		// D3D12 way; IID_IDXGIDevice here returns E_NOINTERFACE, measured in the off-game test)
		o->what = "Present: swap chain GetDevice (ID3D12Device)";
		o->hr = sc->GetDevice(__uuidof(ID3D12Device), (void**)&dev);
		if (FAILED(o->hr) || !dev) return kHrFailed;
		IDXGISwapChain3* sc3 = nullptr;
		o->what = "Present: IDXGISwapChain3 (GetCurrentBackBufferIndex)";
		o->hr = sc->QueryInterface(IID_PPV_ARGS(&sc3));
		if (FAILED(o->hr) || !sc3) { if (sc3) sc3->Release(); dev->Release(); return kHrFailed; }
		DXGI_SWAP_CHAIN_DESC d = {};
		o->what = "Present: swap chain GetDesc";
		o->hr = sc->GetDesc(&d);
		if (FAILED(o->hr)) { sc3->Release(); dev->Release(); return kHrFailed; }
		g_dev = dev; g_sc3 = sc3; g_format = d.BufferDesc.Format;
		g_bbCount = d.BufferCount < 1 ? 1 : (d.BufferCount > kMaxBackBuffers ? kMaxBackBuffers : d.BufferCount);
		g_w = (float)d.BufferDesc.Width; g_h = (float)d.BufferDesc.Height;
		D3D12_DESCRIPTOR_HEAP_DESC hd = {};
		hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors = g_bbCount;
		o->what = "Present: CreateDescriptorHeap (one RTV per back buffer)";
		o->hr = dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_rtvHeap));
		if (FAILED(o->hr) || !g_rtvHeap) return kHrFailed;
		g_rtvStride = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = kSlots; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		o->what = "Present: CreateDescriptorHeap (SRVs: font + 255 pictures)";
		o->hr = dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_srvHeap));
		if (FAILED(o->hr) || !g_srvHeap) return kHrFailed;
		g_srvStride = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		for (UINT i = 0; i < g_bbCount; i++) {
			o->what = "Present: swap chain GetBuffer";
			o->hr = sc->GetBuffer(i, IID_PPV_ARGS(&g_backBuffers[i]));
			if (FAILED(o->hr) || !g_backBuffers[i]) return kHrFailed;
			o->what = "Present: CreateRenderTargetView";
			dev->CreateRenderTargetView(g_backBuffers[i], nullptr, RtvHandle(i));
		}
		for (UINT i = 0; i < g_bbCount; i++) {
			o->what = "Present: CreateCommandAllocator (one per back buffer)";
			o->hr = dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_allocs[i]));
			if (FAILED(o->hr) || !g_allocs[i]) return kHrFailed;
			g_allocCount = i + 1;
		}
		o->what = "Present: CreateCommandList";
		o->hr = dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_allocs[0], nullptr, IID_PPV_ARGS(&g_list));
		if (FAILED(o->hr) || !g_list) return kHrFailed;
		o->what = "Present: command list Close";
		o->hr = g_list->Close();
		if (FAILED(o->hr)) return kHrFailed;
		o->what = "Present: CreateFence";
		o->hr = dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
		if (FAILED(o->hr) || !g_fence) return kHrFailed;
		g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (!g_fenceEvent) { o->hr = E_FAIL; return kHrFailed; }
		// the drawing library: no ini / log file (nothing is ever written next to the game), the default font baked at
		// 32 px (Ovl_Text scales it to size_px), its font SRV at slot 0 of our heap
		o->what = "ImGui::CreateContext";
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.LogFilename = nullptr;
		ImFontConfig fc;
		fc.SizePixels = 32.0f;
		io.Fonts->AddFontDefault(&fc);
		o->what = "ImGui_ImplDX12_Init";
		if (!ImGui_ImplDX12_Init(dev, (int)g_bbCount, g_format, g_srvHeap, SrvCpu(0), SrvGpu(0))) { o->hr = E_FAIL; return kHrFailed; }
		g_resizePending = false;
		return 0;
	}
	OVL_SEH(*f) { return f->code; }
}

// before the original ResizeBuffers: wait our fence (nothing of ours may still be writing into a back buffer we
// release), drop the back-buffer references; they are made again on the next Present
uint32_t SehResizeBefore(GfxOut* o, FaultRecord* f) {
	__try {
		o->hr = S_OK; o->what = "";
		if (g_dev && g_fence && g_queue) {
			o->what = "ResizeBuffers: queue Signal (wait for our lists)";
			o->hr = g_queue->Signal(g_fence, g_fenceValue + 1);
			if (FAILED(o->hr)) return kHrFailed;
			g_fenceValue++;
			o->what = "ResizeBuffers: fence SetEventOnCompletion";
			o->hr = g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent);
			if (FAILED(o->hr)) return kHrFailed;
			o->what = "ResizeBuffers: fence wait";
			if (WaitForSingleObject(g_fenceEvent, kFenceWaitMs) != WAIT_OBJECT_0) { o->hr = E_FAIL; return kHrFailed; }
			for (UINT i = 0; i < kMaxBackBuffers; i++) g_bbFence[i] = g_fenceValue;
		}
		o->what = "ResizeBuffers: release our back buffers";
		for (UINT i = 0; i < kMaxBackBuffers; i++) { if (g_backBuffers[i]) { g_backBuffers[i]->Release(); g_backBuffers[i] = nullptr; } }
		g_resizePending = true;
		return 0;
	}
	OVL_SEH(*f) { return f->code; }
}

// the next Present after a resize: the new buffers, their RTVs, an allocator for each if the count grew
uint32_t SehRecreate(IDXGISwapChain* sc, GfxOut* o, FaultRecord* f) {
	__try {
		o->hr = S_OK; o->what = "";
		DXGI_SWAP_CHAIN_DESC d = {};
		o->what = "resize: swap chain GetDesc";
		o->hr = sc->GetDesc(&d);
		if (FAILED(o->hr)) return kHrFailed;
		UINT bb = d.BufferCount < 1 ? 1 : (d.BufferCount > kMaxBackBuffers ? kMaxBackBuffers : d.BufferCount);
		bool fmtChanged = d.BufferDesc.Format != g_format;
		if (bb != g_bbCount || !g_rtvHeap) {
			o->what = "resize: CreateDescriptorHeap (RTVs)";
			ID3D12DescriptorHeap* heap = nullptr;
			D3D12_DESCRIPTOR_HEAP_DESC hd = {};
			hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors = bb;
			o->hr = g_dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap));
			if (FAILED(o->hr) || !heap) return kHrFailed;
			if (g_rtvHeap) g_rtvHeap->Release();
			g_rtvHeap = heap;
			g_rtvStride = g_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			g_bbCount = bb;
		}
		g_format = d.BufferDesc.Format;
		g_w = (float)d.BufferDesc.Width; g_h = (float)d.BufferDesc.Height;
		for (UINT i = 0; i < g_bbCount; i++) {
			o->what = "resize: swap chain GetBuffer";
			o->hr = sc->GetBuffer(i, IID_PPV_ARGS(&g_backBuffers[i]));
			if (FAILED(o->hr) || !g_backBuffers[i]) return kHrFailed;
			o->what = "resize: CreateRenderTargetView";
			g_dev->CreateRenderTargetView(g_backBuffers[i], nullptr, RtvHandle(i));
			if (!g_allocs[i]) {
				o->what = "resize: CreateCommandAllocator (a back buffer was added)";
				o->hr = g_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_allocs[i]));
				if (FAILED(o->hr) || !g_allocs[i]) return kHrFailed;
				g_allocCount = i + 1;
			}
		}
		if (fmtChanged) ImGui_ImplDX12_InvalidateDeviceObjects();   // new PSO for the new RTV format, next NewFrame
		g_resizePending = false;
		return 0;
	}
	OVL_SEH(*f) { return f->code; }
}

// one picture: stb_image -> R8G8B8A8 texture copied on this list (COPY_DEST -> PIXEL_SHADER_RESOURCE) -> SRV in slot
// 0 = uploaded, 1 = the file is unreadable (skipped, logged), 2 = a D3D12 call failed (o->hr / o->what set; fatal)
int UploadImage(unsigned slot, const char* path, GfxOut* o) {
	if (slot == 0 || slot >= kSlots) return 1;
	if (g_imgCreated[slot]) return 0;
	int w = 0, h = 0, comp = 0;
	unsigned char* px = stbi_load(path, &w, &h, &comp, 4);
	if (!px) { Log("overlay: image %s: stb_image can't read it (%s): slot %u stays empty", path, stbi_failure_reason(), slot); return 1; }
	if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
		Log("overlay: image %s: %dx%d is out of range (1..8192): slot %u stays empty", path, w, h, slot);
		stbi_image_free(px);
		return 1;
	}
	ID3D12Resource* tex = nullptr;
	D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC d = {};
	d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = (UINT64)w; d.Height = (UINT)h; d.DepthOrArraySize = 1; d.MipLevels = 1;
	d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	o->hr = S_OK; o->what = "image: CreateCommittedResource (texture)";
	o->hr = g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex));
	if (FAILED(o->hr) || !tex) { stbi_image_free(px); return 2; }
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {}; UINT rows = 0; UINT64 rowBytes = 0, total = 0;
	o->what = "image: GetCopyableFootprints";
	g_dev->GetCopyableFootprints(&d, 0, 1, 0, &fp, &rows, &rowBytes, &total);
	if (!total || fp.Footprint.RowPitch < (UINT)w * 4) { stbi_image_free(px); tex->Release(); o->hr = E_FAIL; return 2; }
	ID3D12Resource* upload = nullptr;
	D3D12_HEAP_PROPERTIES up = {}; up.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC ud = {};
	ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; ud.Width = total; ud.Height = 1; ud.DepthOrArraySize = 1; ud.MipLevels = 1;
	ud.Format = DXGI_FORMAT_UNKNOWN; ud.SampleDesc.Count = 1; ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	o->what = "image: CreateCommittedResource (upload buffer)";
	o->hr = g_dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));
	if (FAILED(o->hr) || !upload) { stbi_image_free(px); tex->Release(); return 2; }
	void* mapped = nullptr;
	D3D12_RANGE range = { 0, 0 };
	o->what = "image: Map (upload buffer)";
	o->hr = upload->Map(0, &range, &mapped);
	if (FAILED(o->hr) || !mapped) { stbi_image_free(px); upload->Release(); tex->Release(); return 2; }
	for (int y = 0; y < h; y++)
		memcpy((unsigned char*)mapped + fp.Offset + (size_t)y * fp.Footprint.RowPitch, px + (size_t)y * w * 4, (size_t)w * 4);
	upload->Unmap(0, &range);
	stbi_image_free(px);
	D3D12_TEXTURE_COPY_LOCATION src = {}; src.pResource = upload; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp;
	D3D12_TEXTURE_COPY_LOCATION dst = {}; dst.pResource = tex; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = 0;
	o->what = "image: CopyTextureRegion";
	g_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	o->what = "image: ResourceBarrier (COPY_DEST -> PIXEL_SHADER_RESOURCE)";
	D3D12_RESOURCE_BARRIER b = Transition(tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	g_list->ResourceBarrier(1, &b);
	D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
	sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sd.Texture2D.MipLevels = 1;
	o->what = "image: CreateShaderResourceView";
	g_dev->CreateShaderResourceView(tex, &sd, SrvCpu(slot));
	g_imgRes[slot] = tex; g_imgGpu[slot] = SrvGpu(slot); g_imgCreated[slot] = true;
	// the upload buffer is only free once the list we are recording has been executed and finished
	if (g_nRetire < kMaxRetire) { g_retireRes[g_nRetire] = upload; g_retireFence[g_nRetire] = g_fenceValue + 1; g_nRetire++; }
	else Log("overlay: image: retire list full, leaking one upload buffer");
	return 0;
}

void RetireUploads(UINT64 completed) {
	for (int i = 0; i < g_nRetire;) {
		if (g_retireFence[i] <= completed) {
			g_retireRes[i]->Release();
			g_nRetire--;
			g_retireRes[i] = g_retireRes[g_nRetire];
			g_retireFence[i] = g_retireFence[g_nRetire];
		} else i++;
	}
}

// one frame: the back buffer's allocator fence, Reset, PRESENT -> RENDER_TARGET, pictures, ImGui's draw data, back,
// execute on the game's queue, Signal
struct DrawOut { GfxOut g; UINT bb; UINT64 submitted; };
uint32_t SehDrawFrame(const Cmd* cmds, int nCmds, const Pend* imgs, int nImgs, float w, float h, float dt, DrawOut* o, FaultRecord* f) {
	__try {
		o->g.hr = S_OK; o->g.what = ""; o->bb = 0; o->submitted = 0;
		o->g.what = "Present: GetCurrentBackBufferIndex";
		UINT bb = g_sc3->GetCurrentBackBufferIndex();
		if (bb >= g_bbCount) { o->g.hr = E_FAIL; return kHrFailed; }
		o->bb = bb;
		o->g.what = "Present: fence GetCompletedValue";
		UINT64 completed = g_fence->GetCompletedValue();
		RetireUploads(completed);
		if (g_bbFence[bb] > completed) {
			o->g.what = "Present: fence SetEventOnCompletion";
			o->g.hr = g_fence->SetEventOnCompletion(g_bbFence[bb], g_fenceEvent);
			if (FAILED(o->g.hr)) return kHrFailed;
			o->g.what = "Present: fence wait (that back buffer's allocator is still in use)";
			if (WaitForSingleObject(g_fenceEvent, kFenceWaitMs) != WAIT_OBJECT_0) { o->g.hr = E_FAIL; return kHrFailed; }
		}
		o->g.what = "Present: command allocator Reset";
		o->g.hr = g_allocs[bb]->Reset();
		if (FAILED(o->g.hr)) return kHrFailed;
		o->g.what = "Present: command list Reset";
		o->g.hr = g_list->Reset(g_allocs[bb], nullptr);
		if (FAILED(o->g.hr)) return kHrFailed;
		o->g.what = "Present: ResourceBarrier (PRESENT -> RENDER_TARGET)";
		D3D12_RESOURCE_BARRIER b = Transition(g_backBuffers[bb], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		g_list->ResourceBarrier(1, &b);
		for (int i = 0; i < nImgs; i++) {
			if (UploadImage(imgs[i].slot, imgs[i].path, &o->g) == 2) return kHrFailed;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE rtv = RtvHandle(bb);
		g_list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
		ID3D12DescriptorHeap* heaps[1] = { g_srvHeap };
		g_list->SetDescriptorHeaps(1, heaps);
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(w, h);
		io.DeltaTime = dt;
		o->g.what = "ImGui_ImplDX12_NewFrame";
		ImGui_ImplDX12_NewFrame();
		o->g.what = "ImGui::NewFrame";
		ImGui::NewFrame();
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		ImFont* font = ImGui::GetFont();
		for (int i = 0; i < nCmds; i++) {
			const Cmd& c = cmds[i];
			ImU32 col = IM_COL32((c.rgba >> 24) & 0xFF, (c.rgba >> 16) & 0xFF, (c.rgba >> 8) & 0xFF, c.rgba & 0xFF);
			if (c.type == CmdRect) dl->AddRectFilled(ImVec2(c.x, c.y), ImVec2(c.x + c.w, c.y + c.h), col);
			else if (c.type == CmdText) { if (font && c.h > 0 && c.text[0]) dl->AddText(font, c.h, ImVec2(c.x, c.y), col, c.text); }
			else if (c.type == CmdImage && c.tex > 0 && c.tex < kSlots && g_imgCreated[c.tex])
				dl->AddImage((ImTextureID)g_imgGpu[c.tex].ptr, ImVec2(c.x, c.y), ImVec2(c.x + c.w, c.y + c.h));
		}
		o->g.what = "ImGui::Render";
		ImGui::Render();
		o->g.what = "ImGui_ImplDX12_RenderDrawData";
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_list);
		o->g.what = "Present: ResourceBarrier (RENDER_TARGET -> PRESENT)";
		b = Transition(g_backBuffers[bb], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		g_list->ResourceBarrier(1, &b);
		o->g.what = "Present: command list Close";
		o->g.hr = g_list->Close();
		if (FAILED(o->g.hr)) return kHrFailed;
		o->g.what = "Present: queue ExecuteCommandLists";
		g_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_list);
		o->g.what = "Present: queue Signal";
		o->g.hr = g_queue->Signal(g_fence, g_fenceValue + 1);
		if (FAILED(o->g.hr)) return kHrFailed;
		g_fenceValue++;
		g_bbFence[bb] = g_fenceValue;
		o->submitted = g_fenceValue;
		return 0;
	}
	OVL_SEH(*f) { return f->code; }
}

// ---- draw list plumbing --------------------------------------------------------------------------------------
const Cmd* TakeFrame(int* n) {
	EnterCriticalSection(&g_listCs);
	if (!g_hasFrame) { LeaveCriticalSection(&g_listCs); *n = 0; return nullptr; }
	g_hasFrame = false;
	g_render.swap(g_front);
	LeaveCriticalSection(&g_listCs);
	*n = (int)g_render.size();
	return g_render.empty() ? nullptr : g_render.data();
}

bool HasFrame() {
	EnterCriticalSection(&g_listCs);
	bool has = g_hasFrame;
	LeaveCriticalSection(&g_listCs);
	return has;
}

void TakePendingImages() {
	EnterCriticalSection(&g_listCs);
	if (!g_imgQueue.empty()) g_imgWork.swap(g_imgQueue);
	LeaveCriticalSection(&g_listCs);
}

// the test screen: a dark panel 40 % of the width on the left, a title, 30 lines of text, and overlay_test.png if the
// plugin folder has one. Built into its own list and swapped into g_front under the lock, so it never touches g_back:
// a real producer filling the list on the main thread cannot race with it.
void BuildTestScreen() {
	std::vector<Cmd> v;
	Cmd c = {};
	c.type = CmdRect; c.x = 0; c.y = 0; c.w = g_w * 0.4f; c.h = g_h; c.rgba = 0x0C0C10E6;
	v.push_back(c);
	char text[kTextBytes] = {};
	snprintf(text, sizeof text, "deagle_native 2D overlay test - %dx%d", (int)g_w, (int)g_h);
	c = Cmd(); c.type = CmdText; c.x = 16; c.y = 12; c.h = 24; c.rgba = 0xF0E0A0FF;
	strncpy_s(c.text, sizeof c.text, text, _TRUNCATE);
	v.push_back(c);
	const float lineH = 16.0f, top = 52.0f;
	for (int i = 0; i < 30; i++) {
		snprintf(text, sizeof text, "line %d of 30: the quick brown fox jumps over the lazy dog 0123456789", i + 1);
		c = Cmd(); c.type = CmdText; c.x = 16; c.y = top + lineH * i; c.h = 14; c.rgba = 0xC8C8D0FF;
		strncpy_s(c.text, sizeof c.text, text, _TRUNCATE);
		v.push_back(c);
	}
	LONG img = g_testImg;
	if (img > 0) {
		c = Cmd(); c.type = CmdImage; c.x = 16; c.y = top + lineH * 30 + 10; c.w = 128; c.h = 128; c.tex = (unsigned)img;
		v.push_back(c);
	}
	EnterCriticalSection(&g_listCs);
	g_front.swap(v);
	g_hasFrame = true;
	LeaveCriticalSection(&g_listCs);
}

// ---- the render thread's per-frame entry (the Present / Present1 hooks) ---------------------------------------
void OverlayTick(IDXGISwapChain* sc) {
	if (!g_installed || g_off) return;
	EnterCriticalSection(&g_renderCs);
	if (g_off) { LeaveCriticalSection(&g_renderCs); return; }
	if (!g_sc) InterlockedCompareExchangePointer((PVOID volatile*)&g_sc, sc, nullptr);
	if (sc != g_sc) { LeaveCriticalSection(&g_renderCs); return; }   // another window's swap chain: ignored
	if (!g_queue) { LeaveCriticalSection(&g_renderCs); return; }     // nothing submitted on the game's queue yet
	if (g_test == 0 && !HasFrame()) { LeaveCriticalSection(&g_renderCs); return; }   // nothing to draw
	if (!g_dev) {
		GfxOut o = {}; FaultRecord f = {};
		uint32_t c = SehInitGfx(sc, &o, &f);
		if (c) { Fail(o.what, o.hr, c == kHrFailed ? 0 : c, &f); LeaveCriticalSection(&g_renderCs); return; }
		Log("overlay: device objects ready (device %p, %u back buffers, format %d, %dx%d)", (void*)g_dev, g_bbCount, (int)g_format, (int)g_w, (int)g_h);
	}
	if (g_resizePending) {
		GfxOut o = {}; FaultRecord f = {};
		uint32_t c = SehRecreate(sc, &o, &f);
		if (c) { Fail(o.what, o.hr, c == kHrFailed ? 0 : c, &f); LeaveCriticalSection(&g_renderCs); return; }
	}
	if (g_test && !HasFrame()) BuildTestScreen();
	int n = 0;
	const Cmd* cmds = TakeFrame(&n);
	if (!n || !cmds) { LeaveCriticalSection(&g_renderCs); return; }
	TakePendingImages();
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	float dt = 1.0f / 60.0f;
	if (g_lastTick.QuadPart && g_qpcFreq > 0) {
		double gap = (double)(now.QuadPart - g_lastTick.QuadPart) / (double)g_qpcFreq;
		if (gap > 0.0005 && gap < 0.25) dt = (float)gap;
	}
	g_lastTick = now;
	DrawOut o = {}; FaultRecord f = {};
	uint32_t c = SehDrawFrame(cmds, n, g_imgWork.empty() ? nullptr : g_imgWork.data(), (int)g_imgWork.size(), g_w, g_h, dt, &o, &f);
	LARGE_INTEGER cpu;
	QueryPerformanceCounter(&cpu);
	if (c) { Fail(o.g.what, o.g.hr, c == kHrFailed ? 0 : c, &f); LeaveCriticalSection(&g_renderCs); return; }
	InterlockedIncrement(&g_framesDrawn);
	InterlockedAdd64(&g_cpuTicks, cpu.QuadPart - now.QuadPart);
	g_imgWork.clear();
	LeaveCriticalSection(&g_renderCs);
}

// ---- hooks --------------------------------------------------------------------------------------------------
void STDMETHODCALLTYPE HkExecute(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists) {
	g_origExecute(q, n, lists);
	if (g_off || g_queue) return;   // the queue is learned once, on the first call (no GetDesc on later submissions)
	D3D12_COMMAND_QUEUE_DESC d = {};
	FaultRecord f = {};
	uint32_t c = SehQueueDesc(q, &d, &f);
	if (c) { Fail("ExecuteCommandLists: ID3D12CommandQueue::GetDesc", S_OK, c, &f); return; }
	if (d.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return;
	if (InterlockedCompareExchangePointer((PVOID volatile*)&g_queue, q, nullptr) == nullptr)
		Log("overlay: the game's DIRECT queue is %p (ExecuteCommandLists slot 10, thread %lu)", (void*)q, GetCurrentThreadId());
}

HRESULT STDMETHODCALLTYPE HkPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
	OverlayTick(sc);
	return g_origPresent(sc, sync, flags);
}

HRESULT STDMETHODCALLTYPE HkPresent1(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* params) {
	OverlayTick(sc);
	return g_origPresent1(sc, sync, flags, params);
}

HRESULT STDMETHODCALLTYPE HkResize(IDXGISwapChain* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags) {
	if (g_installed && !g_off && (g_sc == nullptr || sc == g_sc)) {
		EnterCriticalSection(&g_renderCs);
		GfxOut o = {}; FaultRecord f = {};
		uint32_t c = SehResizeBefore(&o, &f);
		LeaveCriticalSection(&g_renderCs);
		if (c) Fail(o.what, o.hr, c == kHrFailed ? 0 : c, &f);
	}
	return g_origResize(sc, count, w, h, fmt, flags);
}

// ---- commands ------------------------------------------------------------------------------------------------
void LoadTestImage() {
	char path[MAX_PATH] = {};
	snprintf(path, sizeof path, "%s\\overlay_test.png", g_dir);
	if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
		Log("overlay: test: no %s: the screen is drawn without a picture", path);
		InterlockedExchange(&g_testNoImage, 1);
		return;
	}
	unsigned id = Ovl_LoadImage(path);
	InterlockedExchange(&g_testImg, (LONG)id);
	Log("overlay: test: picture %s -> texId %u", path, id);
}

} // namespace

// ---- public API ----------------------------------------------------------------------------------------------
void Overlay_Install(HMODULE self) {
	if (g_installed) return;
	g_self = self;
	if (self) {
		GetModuleFileNameA(self, g_dir, MAX_PATH);
		char* slash = strrchr(g_dir, '\\');      // the .asi's folder, not the file name
		if (slash) *slash = 0;
	}
	InitializeCriticalSection(&g_listCs);
	InitializeCriticalSection(&g_renderCs);
	LARGE_INTEGER freq;
	if (QueryPerformanceFrequency(&freq)) g_qpcFreq = freq.QuadPart;
	ProbeOut p = {};
	FaultRecord f = {};
	uint32_t c = SehProbe(&p, &f);
	if (c == 0 && (!p.ecl || !p.present || !p.present1 || !p.resize)) c = kHrFailed;
	if (c) {
		if (c == kHrFailed) Log("overlay: install: %s failed hr=0x%08lX: not installed (the game is untouched)", p.g.what, (unsigned long)p.g.hr);
		else Log("overlay: install: %s EXCEPTION 0x%08X at %p: not installed (the game is untouched)", p.g.what, c, f.at);
		return;
	}
	MH_STATUS st = MH_Initialize();
	if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) { Log("overlay: MH_Initialize = %d: not installed", (int)st); return; }
	struct Hook { void* target; void* detour; void** orig; const char* name; HMODULE owner; char ownerName[MAX_PATH]; };
	Hook h[4] = {
		{ p.ecl, (void*)&HkExecute, (void**)&g_origExecute, "ExecuteCommandLists (slot 10)" },
		{ p.present, (void*)&HkPresent, (void**)&g_origPresent, "Present (slot 8)" },
		{ p.present1, (void*)&HkPresent1, (void**)&g_origPresent1, "Present1 (slot 22)" },
		{ p.resize, (void*)&HkResize, (void**)&g_origResize, "ResizeBuffers (slot 13)" },
	};
	for (int i = 0; i < 4; i++) {
		h[i].ownerName[0] = 0;
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)h[i].target, &h[i].owner))
			GetModuleFileNameA(h[i].owner, h[i].ownerName, MAX_PATH);
	}
	int made = 0;
	for (int i = 0; i < 4; i++) {
		st = MH_CreateHook(h[i].target, h[i].detour, h[i].orig);
		if (st != MH_OK) {
			Log("overlay: MH_CreateHook(%s at %p in %s) = %d: not installed", h[i].name, h[i].target, h[i].ownerName, (int)st);
			for (int j = 0; j < made; j++) MH_RemoveHook(h[j].target);
			return;
		}
		made++;
	}
	for (int i = 0; i < 4; i++) {
		st = MH_EnableHook(h[i].target);
		if (st != MH_OK) {
			Log("overlay: MH_EnableHook(%s) = %d: not installed", h[i].name, (int)st);
			for (int j = 0; j < made; j++) MH_RemoveHook(h[j].target);
			return;
		}
	}
	g_installed = true;
	for (int i = 0; i < 4; i++)
		Log("overlay: hooked %s at %p in %s (original %p)", h[i].name, h[i].target, h[i].ownerName, *h[i].orig);
	Log("overlay: ready; nothing is drawn (and no D3D12 objects are made) until \"overlay test 1\" or a frame is filled");
}

void Overlay_Command(const char* rest) {
	if (!rest) return;
	while (*rest == ' ') rest++;
	if (!strncmp(rest, "test", 4) && (rest[4] == 0 || rest[4] == ' ')) {
		int on = atoi(rest + 4) != 0;
		if (on && g_installed && !g_testImg && !g_testNoImage) LoadTestImage();
		InterlockedExchange(&g_test, on ? 1 : 0);
		Log("overlay: test screen %s (%s), back buffer %dx%d", on ? "on" : "off", g_installed ? "installed" : "NOT installed", (int)g_w, (int)g_h);
		return;
	}
	if (!strcmp(rest, "stats")) {
		LONG frames = InterlockedCompareExchange(&g_framesDrawn, 0, 0);
		LONGLONG ticks = InterlockedCompareExchange64(&g_cpuTicks, 0, 0);
		double ms = frames > 0 && g_qpcFreq > 0 ? (double)ticks * 1000.0 / (double)g_qpcFreq / (double)frames : 0.0;
		Log("overlay: stats: %ld frames drawn, average Present work %.3f ms, last error: %s", frames, ms, g_lastError[0] ? g_lastError : "none");
		Log("overlay: stats: %s, test screen %s, back buffer %dx%d, %u allocators, queue %p, swap chain %p",
			g_off ? "off (see the error)" : (g_dev ? "drawing" : "idle (no frame drawn yet)"), g_test ? "on" : "off", (int)g_w, (int)g_h, g_allocCount, (void*)g_queue, (void*)g_sc);
		return;
	}
	Log("overlay: unknown command \"%s\" (overlay test 1|0, overlay stats)", rest);
}

void Ovl_Begin() {
	if (!g_installed || g_off) return;
	g_back.clear();
}

void Ovl_Rect(float x, float y, float w, float h, unsigned rgba) {
	if (!g_installed || g_off) return;
	if ((int)g_back.size() >= kMaxCmds) {
		if (!g_droppedLogged && (g_droppedLogged = true)) Log("overlay: more than %d commands in a frame: the extra ones are dropped", kMaxCmds);
		return;
	}
	Cmd c = {};
	c.type = CmdRect; c.x = x; c.y = y; c.w = w; c.h = h; c.rgba = rgba;
	g_back.push_back(c);
}

void Ovl_Text(float x, float y, float size_px, unsigned rgba, const char* utf8) {
	if (!g_installed || g_off) return;
	if (!utf8 || !utf8[0]) return;
	if ((int)g_back.size() >= kMaxCmds) return;
	Cmd c = {};
	c.type = CmdText; c.x = x; c.y = y; c.h = size_px; c.rgba = rgba;
	strncpy_s(c.text, sizeof c.text, utf8, _TRUNCATE);
	g_back.push_back(c);
}

void Ovl_Image(float x, float y, float w, float h, unsigned texId) {
	if (!g_installed || g_off) return;
	if ((int)g_back.size() >= kMaxCmds) return;
	Cmd c = {};
	c.type = CmdImage; c.x = x; c.y = y; c.w = w; c.h = h; c.tex = texId;
	g_back.push_back(c);
}

void Ovl_End() {
	if (!g_installed || g_off) return;
	EnterCriticalSection(&g_listCs);
	g_front.swap(g_back);
	g_hasFrame = true;
	LeaveCriticalSection(&g_listCs);
	g_back.clear();   // after the swap g_back holds the list that was drawn two frames ago
}

unsigned Ovl_LoadImage(const char* path) {
	if (!g_installed || g_off || !path || !path[0]) return 0;
	unsigned slot = 0;
	EnterCriticalSection(&g_listCs);
	if (g_imgNext < kSlots) {
		slot = g_imgNext++;
		Pend p = {};
		p.slot = slot;
		strncpy_s(p.path, sizeof p.path, path, _TRUNCATE);
		g_imgQueue.push_back(p);
	}
	LeaveCriticalSection(&g_listCs);
	if (!slot) Log("overlay: Ovl_LoadImage(\"%s\"): no slot left (255 pictures max)", path);
	return slot;
}
