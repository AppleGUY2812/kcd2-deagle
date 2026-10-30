// The game's small memory blocks from mimalloc: see fastalloc.h.
#include "fastalloc.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <psapi.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include "mimalloc.h"

namespace {

// ---- WHGame.dll 1.5.6 (decompile 2026-10-25; bytes read from the installed DLL)
constexpr uintptr_t kRvaAlloc = 0x4f7610;     // CryMalloc(size, size_t* allocated, align) -> block
const unsigned char kAllocBytes[] = { 0x40,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0xb8,0x68,0x12,0x00,0x00 };
constexpr uintptr_t kRvaFree = 0x4fd8d0;      // CryFree(block, align) -> the block's size
const unsigned char kFreeBytes[] = { 0x40,0x57,0x48,0x83,0xec,0x20,0x33,0xc0,0x48,0x8b,0xf9,0x48,0x85,0xc9 };
constexpr uintptr_t kRvaRealloc = 0x490cfc;   // CryRealloc(block, size, size_t* allocated, size_t* oldSize, align)
const unsigned char kReallocBytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48 };
constexpr uintptr_t kRvaBucketOff = 0x56651e8;   // byte, nonzero: the bucket allocator is off (small blocks from the CRT)
constexpr uintptr_t kRvaAllocated = 0x547d8e8;   // int32: bytes allocated; CryMalloc adds *allocated, CryFree subtracts
constexpr size_t kArenaBytes = 32ull << 30;

using AllocFn = void*(__fastcall*)(size_t, size_t*, size_t);
using FreeFn = size_t(__fastcall*)(void*, size_t);
using ReallocFn = void*(__fastcall*)(void*, size_t, size_t*, size_t*, size_t);
AllocFn g_alloc = nullptr;
FreeFn g_free = nullptr;
ReallocFn g_realloc = nullptr;
volatile unsigned char* g_bucketOff = nullptr;
volatile long* g_allocated = nullptr;
mi_arena_id_t g_arena{};
uintptr_t g_lo = ~(uintptr_t)0, g_size = 0;   // the arena: [g_lo, g_lo + g_size)
bool g_on = false;
std::atomic<long long> g_fallbacks{0}, g_heaps{0}, g_heapFails{0};
thread_local mi_heap_t* t_heap = nullptr;
thread_local bool t_done = false;   // the thread is ending: mimalloc deletes its heaps next, so it gets the game's blocks

// Mode 2: the C runtime heap too (ucrtbase's malloc family, every module). Its blocks live in a second arena, so CryFree's
// hook never sees them: CryMalloc's aligned blocks (malloc + an offset) come back to CryFree as inner pointers, and the
// game's own CryFree takes the offset off before it calls free / _msize, which land here with the block's start.
mi_arena_id_t g_arenaC{};
uintptr_t g_loC = ~(uintptr_t)0, g_sizeC = 0;
bool g_crtOn = false;
std::atomic<long long> g_crtFallbacks{0}, g_crtHeaps{0};
thread_local mi_heap_t* t_heapC = nullptr;
using CMallocFn = void*(__cdecl*)(size_t);
using CCallocFn = void*(__cdecl*)(size_t, size_t);
using CReallocFn = void*(__cdecl*)(void*, size_t);
using CFreeFn = void(__cdecl*)(void*);
using CMsizeFn = size_t(__cdecl*)(void*);
using CExpandFn = void*(__cdecl*)(void*, size_t);
using CRecallocFn = void*(__cdecl*)(void*, size_t, size_t);
CMallocFn o_malloc = nullptr;
CCallocFn o_calloc = nullptr;
CReallocFn o_realloc = nullptr;
CFreeFn o_free = nullptr;
CMsizeFn o_msize = nullptr;
CExpandFn o_expand = nullptr;
CRecallocFn o_recalloc = nullptr;

// Thread exit: this module's TLS callbacks run in their section order, ours (.CRT$XLC) before mimalloc's detach
// (.CRT$XLY), which deletes the thread's heaps. A later allocation on this thread (another module's thread-exit code)
// must not use the deleted heap.
volatile LONG g_tlsSeen = 0;   // TLS callbacks run so far: 0 at install = this load path skips them (see Fastalloc_Install)
void NTAPI TlsCallback(PVOID, DWORD reason, PVOID) {
	InterlockedIncrement(&g_tlsSeen);
	if (reason == DLL_THREAD_DETACH) { t_heap = nullptr; t_heapC = nullptr; t_done = true; }
}

inline bool Ours(const void* p) { return (uintptr_t)p - g_lo < g_size; }
inline bool OursC(const void* p) { return (uintptr_t)p - g_loC < g_sizeC; }

mi_heap_t* HeapC() {
	mi_heap_t* h = t_heapC;
	if (h || t_done) return h;
	h = mi_heap_new_in_arena(g_arenaC);
	if (h) { t_heapC = h; g_crtHeaps++; }
	return h;
}

// ---- the C runtime heap (mode 2). Blocks made before the hooks (or after a thread's heaps are gone) stay the CRT's.
void* __cdecl HkCMalloc(size_t n) {
	if (mi_heap_t* h = HeapC()) {
		if (void* p = mi_heap_malloc(h, n)) return p;
		g_crtFallbacks++;
	}
	return o_malloc(n);
}

void* __cdecl HkCCalloc(size_t c, size_t n) {
	if (mi_heap_t* h = HeapC()) {
		if (void* p = mi_heap_calloc(h, c, n)) return p;
		g_crtFallbacks++;
	}
	return o_calloc(c, n);
}

void __cdecl HkCFree(void* p) {
	if (OursC(p)) mi_free(p);
	else o_free(p);
}

size_t __cdecl HkCMsize(void* p) { return OursC(p) ? mi_usable_size(p) : o_msize(p); }

void* __cdecl HkCExpand(void* p, size_t n) {
	if (!OursC(p)) return o_expand(p, n);
	return n && mi_expand(p, n) ? p : nullptr;   // in place only, like the CRT's
}

// Our block into a new one of n bytes: in our arena when this thread has a heap, else from the CRT (thread ending).
void* MoveC(void* p, size_t n) {
	void* q = nullptr;
	if (mi_heap_t* h = HeapC()) q = mi_heap_realloc(h, p, n);   // frees p itself when it moves
	if (q) return q;
	q = o_malloc(n);
	if (!q) return nullptr;   // the CRT's failure: NULL, the old block kept
	const size_t old = mi_usable_size(p);
	memcpy(q, p, old < n ? old : n);
	mi_free(p);
	g_crtFallbacks++;
	return q;
}

void* __cdecl HkCRealloc(void* p, size_t n) {
	if (!p) return HkCMalloc(n);
	if (!OursC(p)) return o_realloc(p, n);   // the CRT's block stays in the CRT heap
	if (!n) { mi_free(p); return nullptr; }   // realloc(p, 0) frees p and returns NULL (UCRT)
	return MoveC(p, n);
}

void* __cdecl HkCRecalloc(void* p, size_t c, size_t n) {
	if (!p) return HkCCalloc(c, n);
	if (!OursC(p)) return o_recalloc(p, c, n);
	if (n && c > SIZE_MAX / n) return nullptr;
	const size_t total = c * n, old = mi_usable_size(p);
	if (!total) { mi_free(p); return nullptr; }
	void* q = MoveC(p, total);
	if (q && total > old) memset((char*)q + old, 0, total - old);   // the CRT zeroes past the old size
	return q;
}

// CryMalloc's own test for its bucket path (blocks of up to 512 bytes from the shared lists); everything else keeps
// the game's path. `align` may be garbage (one caller passes two arguments): the game tests it the same way.
inline bool BucketPath(size_t n, size_t align) {
	if (n == 0 || n > 0x200) return false;
	if (align == 0) return true;
	const bool fits = (0x400 % n == 0 && n % align == 0) || ((n & 0xf) == 0 && align <= 16) || ((n & 7) == 0 && align <= 8);
	return fits && 0x400 % align == 0;
}

mi_heap_t* Heap() {
	mi_heap_t* h = t_heap;
	if (h || t_done) return h;
	h = mi_heap_new_in_arena(g_arena);
	if (h) { t_heap = h; g_heaps++; } else g_heapFails++;
	return h;
}

void* __fastcall HkAlloc(size_t n, size_t* allocated, size_t align) {
	if (!BucketPath(n, align) || *g_bucketOff) {
		void* p = g_alloc(n, allocated, align);
		// Mode 2: the big path's malloc block is ours; CryMalloc counted n, CryFree takes off _msize = our usable size
		if (align == 0 && n > 0x200 && OursC(p)) {
			const size_t got = mi_usable_size(p);
			if (got > *allocated) { _InterlockedExchangeAdd(g_allocated, (long)(got - *allocated)); *allocated = got; }
		}
		return p;
	}
	mi_heap_t* h = Heap();
	const size_t n16 = (n + 15) & ~(size_t)15;
	size_t a = n16 & (0 - n16);   // the game's buckets: a block of 16 * k bytes is aligned to k's lowest set bit * 16
	if (a > 64) a = 64;
	if (align > a) a = align;
	void* p = h ? mi_heap_malloc_aligned(h, n16, a) : nullptr;
	if (!p) { g_fallbacks++; return g_alloc(n, allocated, align); }
	const size_t got = mi_usable_size(p);
	*allocated = got;
	_InterlockedExchangeAdd(g_allocated, (long)got);
	return p;
}

size_t __fastcall HkFree(void* p, size_t align) {
	if (!Ours(p)) return g_free(p, align);
	const size_t got = mi_usable_size(p);
	mi_free(p);
	_InterlockedExchangeAdd(g_allocated, -(long)got);
	return got;
}

void* __fastcall HkRealloc(void* p, size_t n, size_t* allocated, size_t* oldSize, size_t align) {
	if (!Ours(p)) return g_realloc(p, n, allocated, oldSize, align);   // its new block comes through HkAlloc
	const size_t old = mi_usable_size(p);
	void* q = HkAlloc(n, allocated, align);
	*oldSize = old;
	if (!q && n) return nullptr;   // the game's failure: NULL, the old block kept
	if (q) memcpy(q, p, old < n ? old : n);
	HkFree(p, align);
	return q;
}

bool Hook(uintptr_t base, uintptr_t rva, const unsigned char* bytes, size_t n, void* detour, void** orig) {
	void* p = (void*)(base + rva);
	return !memcmp(p, bytes, n) && MH_CreateHook(p, detour, orig) == MH_OK;
}

// ucrtbase's export, or the function its first instruction jumps to (malloc, _msize, ... are `jmp <inner>`; hooking the
// inner one also catches ucrtbase's own calls, e.g. _aligned_malloc and _strdup call malloc, _aligned_free calls free)
void* CrtFn(HMODULE m, const char* name) {
	unsigned char* p = (unsigned char*)GetProcAddress(m, name);
	if (p && p[0] == 0xe9) p = p + 5 + *(int32_t*)(p + 1);
	return p;
}

// Mode 2's hooks, turned on in this order: the free-like ones (free, _msize, _expand) before any that can make an arena
// block (realloc, _recalloc, calloc, malloc), so any state part-way through is consistent.
bool InstallCrt() {
	HMODULE m = GetModuleHandleA("ucrtbase.dll");
	if (!m) { Log("fastalloc: C runtime heap not hooked: ucrtbase.dll is not loaded"); return false; }
	struct H { const char* name; void* detour; void** orig; void* at; };
	H hs[] = {
		{ "_free_base", (void*)&HkCFree, (void**)&o_free, nullptr },
		{ "_msize", (void*)&HkCMsize, (void**)&o_msize, nullptr },
		{ "_expand", (void*)&HkCExpand, (void**)&o_expand, nullptr },
		{ "_realloc_base", (void*)&HkCRealloc, (void**)&o_realloc, nullptr },
		{ "_recalloc", (void*)&HkCRecalloc, (void**)&o_recalloc, nullptr },
		{ "_calloc_base", (void*)&HkCCalloc, (void**)&o_calloc, nullptr },
		{ "_malloc_base", (void*)&HkCMalloc, (void**)&o_malloc, nullptr },
	};
	for (H& h : hs) {
		h.at = CrtFn(m, h.name);
		bool dup = false;
		for (H& o : hs) if (&o != &h && o.at == h.at) dup = true;
		if (!h.at || dup || MH_CreateHook(h.at, h.detour, h.orig) != MH_OK) {
			Log("fastalloc: C runtime heap not hooked: %s %s", h.name, !h.at ? "not found" : dup ? "shares its code" : "hook failed");
			for (H& o : hs) if (o.at && &o != &h) MH_RemoveHook(o.at);
			return false;
		}
	}
	g_crtOn = true;
	for (H& h : hs)
		if (MH_EnableHook(h.at) != MH_OK) {   // the ones before it stay on: they handle every arena block made so far
			Log("fastalloc: C runtime heap: enabling %s FAILED (the hooks before it stay on)", h.name);
			return false;
		}
	return true;
}

}  // namespace

extern "C" {
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:fastalloc_tls_callback")
#pragma const_seg(".CRT$XLC")
extern const PIMAGE_TLS_CALLBACK fastalloc_tls_callback;
const PIMAGE_TLS_CALLBACK fastalloc_tls_callback = &TlsCallback;
#pragma const_seg()
}

void Fastalloc_Install(uintptr_t base, void* self) {
	if (g_on) return;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	int mode = 0, purgeMs = -1;   // second number: ms before freed pages go back to Windows; -1 = never (2026-10-25: with 2000
	                              // (x10 for arenas) the user saw 0.3-0.7 s gameplay hitches, 10-45k fresh page faults each; suspected)
	if (FILE* fp = fopen((dir + "fastload_fastalloc.txt").c_str(), "r")) {
		if (fscanf(fp, "%d %d", &mode, &purgeMs) < 1) mode = 0;
		fclose(fp);
	}
	if (mode != 1 && mode != 2) { Log("fastalloc: off (fastload_fastalloc.txt: 1 = the game's small blocks from mimalloc, 2 = also the C runtime heap)"); return; }
	const bool started = *(volatile unsigned char*)(base + 0x549d370) != 0;   // CryMalloc's first-use flag
	// mimalloc starts itself from a TLS callback at our DLL's attach. Loaded by Ultimate ASI Loader as version.dll (before
	// WHGame.dll, 2026-10-30) that never ran: the first heap call seeded its random state inside the read-only empty heap
	// and crashed every launch (fastload.asi+e6047, write to .rdata). Start it here; a second call does nothing.
	// With no TLS callbacks at all, thread exits don't hand their pages back either (mimalloc's detach is one too).
	Log("fastalloc: TLS callbacks seen before install: %ld%s", (long)g_tlsSeen,
	    g_tlsSeen ? "" : " (none: this load path skips them; mimalloc started by hand, exited threads keep their pages)");
	mi_process_init();
	mi_option_set(mi_option_purge_delay, purgeMs);   // -1: freed pages stay committed and are reused (no re-fault bursts)
	// Commit per page as used. mimalloc's default commits each thread's second and later 32 MB segment whole: with ~77
	// game threads that held ~1.4 GB more private memory at the level load's start (boot 146: 7788 MB, boot 145: 6347).
	mi_option_set(mi_option_eager_commit, 0);
	mi_option_set(mi_option_show_errors, 0);
	mi_option_set(mi_option_verbose, 0);
	if (mi_reserve_os_memory_ex(kArenaBytes, false, false, true, &g_arena) != 0) { Log("fastalloc: off: the arena's address space was not reserved"); return; }
	size_t sz = 0;
	void* lo = mi_arena_area(g_arena, &sz);
	if (!lo || !sz) { Log("fastalloc: off: the arena has no area"); return; }
	g_bucketOff = (volatile unsigned char*)(base + kRvaBucketOff);
	g_allocated = (volatile long*)(base + kRvaAllocated);
	if (!Hook(base, kRvaFree, kFreeBytes, sizeof kFreeBytes, (void*)&HkFree, (void**)&g_free) ||
	    !Hook(base, kRvaRealloc, kReallocBytes, sizeof kReallocBytes, (void*)&HkRealloc, (void**)&g_realloc) ||
	    !Hook(base, kRvaAlloc, kAllocBytes, sizeof kAllocBytes, (void*)&HkAlloc, (void**)&g_alloc)) {
		MH_RemoveHook((void*)(base + kRvaFree));
		MH_RemoveHook((void*)(base + kRvaRealloc));
		MH_RemoveHook((void*)(base + kRvaAlloc));
		Log("fastalloc: off: CryMalloc / CryFree / CryRealloc differ (another game version?) or a hook failed");
		return;
	}
	g_lo = (uintptr_t)lo;
	g_size = sz;   // from here CryFree / CryRealloc know the arena's blocks; CryMalloc's hook goes on last
	// free and realloc first, so no arena block exists before they know it (with deferred enables all three go on together)
	const bool ok = MH_EnableHook((void*)(base + kRvaFree)) == MH_OK && MH_EnableHook((void*)(base + kRvaRealloc)) == MH_OK &&
	                MH_EnableHook((void*)(base + kRvaAlloc)) == MH_OK;
	g_on = ok;
	Log("fastalloc: %s: CryMalloc's blocks of up to 512 bytes from mimalloc %d (per-thread heaps in a %zu GB arena at %p); %s",
	    ok ? "on" : "FAILED to enable", mi_version(), sz >> 30, lo,
	    started ? "the game had already allocated (its earlier blocks stay its own)" : "before the game's first allocation");
	if (!ok || mode != 2) return;
	if (mi_reserve_os_memory_ex(kArenaBytes, false, false, true, &g_arenaC) != 0) { Log("fastalloc: C runtime heap not hooked: no address space for its arena"); return; }
	size_t szC = 0;
	void* loC = mi_arena_area(g_arenaC, &szC);
	if (!loC || !szC) { Log("fastalloc: C runtime heap not hooked: its arena has no area"); return; }
	g_loC = (uintptr_t)loC;
	g_sizeC = szC;
	const bool crt = InstallCrt();
	Log("fastalloc: C runtime heap %s: malloc / calloc / realloc / _recalloc / free / _msize / _expand of every module from "
	    "mimalloc (per-thread heaps in a %zu GB arena at %p; blocks made before stay the CRT's)", crt ? "on" : "NOT fully on", szC >> 30, loC);
}

void Fastalloc_Report(const char* when) {
	PROCESS_MEMORY_COUNTERS_EX pm{};
	GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pm, sizeof pm);
	if (!g_on) {   // the same memory numbers with the game's allocator, to compare
		Log("fastalloc: %s (off): process commit %.0f MB (peak %.0f MB), working set %.0f MB", when, pm.PrivateUsage / 1048576.0,
		    pm.PeakPagefileUsage / 1048576.0, pm.WorkingSetSize / 1048576.0);
		return;
	}
	size_t elapsed = 0, user = 0, sys = 0, rss = 0, peakRss = 0, commit = 0, peakCommit = 0, faults = 0;
	mi_process_info(&elapsed, &user, &sys, &rss, &peakRss, &commit, &peakCommit, &faults);
	Log("fastalloc: %s: %lld thread heaps (%lld failed), %lld blocks went to the game's allocator (arena full); C runtime heap "
	    "%s (%lld thread heaps, %lld blocks to the CRT); process commit %.0f MB (peak %.0f MB), working set %.0f MB; game's "
	    "allocated-bytes counter %ld", when, g_heaps.load(), g_heapFails.load(), g_fallbacks.load(), g_crtOn ? "on" : "off",
	    g_crtHeaps.load(), g_crtFallbacks.load(), pm.PrivateUsage / 1048576.0, peakCommit / 1048576.0,
	    pm.WorkingSetSize / 1048576.0, *g_allocated);
}
