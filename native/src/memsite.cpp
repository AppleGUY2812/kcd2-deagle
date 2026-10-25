// Who holds the game's memory: see memsite.h.
#include "memsite.h"
#include "log.h"
#include "readiness.h"
#include "MinHook.h"
#include <windows.h>
#include <dxgi1_4.h>
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <new>
#include <string>

namespace {

using NtAllocFn = LONG(NTAPI*)(HANDLE, PVOID*, ULONG_PTR, PSIZE_T, ULONG, ULONG);
using NtAllocExFn = LONG(NTAPI*)(HANDLE, PVOID*, PSIZE_T, ULONG, ULONG, void*, ULONG);
using NtFreeFn = LONG(NTAPI*)(HANDLE, PVOID*, PSIZE_T, ULONG);
NtAllocFn g_origAlloc = nullptr;
NtAllocExFn g_origAllocEx = nullptr;
NtFreeFn g_origFree = nullptr;

constexpr int kFrames = 6;
struct Range { uintptr_t lo = 0, hi = 0; };
Range g_skip[4];   // ntdll, KernelBase, kernel32, ucrtbase: leading frames there are the allocation path itself
std::string g_dir;
std::atomic<bool> g_on{false};
// Re-entry guard: a TLS slot below 64 lives in the TEB itself. Not thread_local: a thread's implicit TLS block is taken
// from the heap while the thread starts, and a commit made for it would read the block before it exists (boot 83's crash).
DWORD g_tls = TLS_OUT_OF_INDEXES;
inline bool Inside() { return TlsGetValue(g_tls) != nullptr; }   // TlsGetValue clears the last error: callers keep a LastError
inline void SetInside(bool v) { TlsSetValue(g_tls, v ? (void*)1 : nullptr); }
SRWLOCK g_lock = SRWLOCK_INIT, g_reportLock = SRWLOCK_INIT;

struct LastError {   // the hooks leave the thread's last error as they found it
	DWORD e = GetLastError();
	~LastError() { SetLastError(e); }
};

// ---- the map's nodes come from pages taken with the original NtAllocateVirtualMemory: no heap (see memsite.h)
constexpr size_t kArenaReserve = 1ull << 30, kArenaStep = 8ull << 20;
uint8_t* g_arena = nullptr;
size_t g_arenaUsed = 0, g_arenaCommitted = 0, g_nodeSize = 0;
void* g_freeList = nullptr;
void* ArenaGet(size_t n) {   // under g_lock (or before the hooks are on)
	n = (n + 15) & ~size_t(15);
	if (!g_nodeSize) g_nodeSize = n;
	if (n == g_nodeSize && g_freeList) { void* p = g_freeList; g_freeList = *(void**)p; return p; }
	if (g_arenaUsed + n > g_arenaCommitted) {
		if (g_arenaCommitted + kArenaStep > kArenaReserve) return nullptr;
		void* a = g_arena + g_arenaCommitted;
		SIZE_T sz = kArenaStep;
		if (g_origAlloc ? g_origAlloc(GetCurrentProcess(), &a, 0, &sz, MEM_COMMIT, PAGE_READWRITE) < 0
		                : !VirtualAlloc(a, sz, MEM_COMMIT, PAGE_READWRITE))
			return nullptr;
		g_arenaCommitted += kArenaStep;
	}
	void* p = g_arena + g_arenaUsed;
	g_arenaUsed += n;
	return p;
}
void ArenaPut(void* p, size_t n) {
	n = (n + 15) & ~size_t(15);
	if (n != g_nodeSize) return;
	*(void**)p = g_freeList;
	g_freeList = p;
}
template <class T> struct ArenaAlloc {
	using value_type = T;
	ArenaAlloc() = default;
	template <class U> ArenaAlloc(const ArenaAlloc<U>&) {}
	T* allocate(size_t n) {
		void* p = ArenaGet(n * sizeof(T));
		if (!p) throw std::bad_alloc();
		return (T*)p;
	}
	void deallocate(T* p, size_t n) { ArenaPut(p, n * sizeof(T)); }
	template <class U> bool operator==(const ArenaAlloc<U>&) const { return true; }
	template <class U> bool operator!=(const ArenaAlloc<U>&) const { return false; }
};

struct Rec { size_t size; uint32_t site; };
using RecMap = std::map<uintptr_t, Rec, std::less<uintptr_t>, ArenaAlloc<std::pair<const uintptr_t, Rec>>>;
alignas(RecMap) unsigned char g_mapBuf[sizeof(RecMap)];
RecMap* g_map = nullptr;

// The tables are taken at install, only when the census is on (no commit charge otherwise).
constexpr uint32_t kMaxSites = 1u << 17, kHash = 1u << 18;
struct Site { uintptr_t f[kFrames]; long long live, commits, res; };
Site* g_sites = nullptr;
Site* g_snap = nullptr;   // a report's copy
uint32_t* g_hash = nullptr;   // site index, 0 = empty
uint32_t* g_idx = nullptr;
uint32_t g_nSites = 1;   // site 0: the overflow bucket (no frames, or the table full)
long long g_live = 0, g_errors = 0;

struct Span { uintptr_t b; size_t s; uint32_t site; };
constexpr size_t kMaxSpans = 1u << 20, kWsBatch = 1u << 16;
Span* g_spans = nullptr;
PSAPI_WORKING_SET_EX_INFORMATION* g_ws = nullptr;
uint32_t* g_wsSite = nullptr;

uint32_t SiteOf(const uintptr_t f[kFrames]) {   // under g_lock
	if (!f[0]) return 0;
	uint64_t h = 1469598103934665603ull;
	for (int i = 0; i < kFrames; i++) h = (h ^ f[i]) * 1099511628211ull;
	for (uint32_t i = (uint32_t)(h & (kHash - 1)), k = 0; k < 64; i = (i + 1) & (kHash - 1), k++) {
		const uint32_t s = g_hash[i];
		if (!s) {
			if (g_nSites >= kMaxSites) return 0;
			const uint32_t n = g_nSites++;
			memcpy(g_sites[n].f, f, sizeof g_sites[n].f);
			g_hash[i] = n;
			return n;
		}
		if (!memcmp(g_sites[s].f, f, sizeof g_sites[s].f)) return s;
	}
	return 0;
}

void Take(uint32_t site, long long n) { g_sites[site].live -= n; g_live -= n; }

void Remove(uintptr_t lo, uintptr_t hi) {   // under g_lock: the tracked commits inside [lo, hi) are gone
	auto it = g_map->lower_bound(lo);
	if (it != g_map->begin()) {
		auto p = std::prev(it);
		const uintptr_t pend = p->first + p->second.size;
		if (pend > lo) {   // a record that starts before lo reaches into the range
			Take(p->second.site, (long long)((pend < hi ? pend : hi) - lo));
			const uint32_t site = p->second.site;
			p->second.size = lo - p->first;
			if (pend > hi) g_map->emplace(hi, Rec{ pend - hi, site });
		}
	}
	while (it != g_map->end() && it->first < hi) {
		const uintptr_t e = it->first + it->second.size;
		if (e <= hi) {
			Take(it->second.site, (long long)it->second.size);
			it = g_map->erase(it);
		} else {
			Take(it->second.site, (long long)(hi - it->first));
			const Rec r{ e - hi, it->second.site };
			g_map->erase(it);
			g_map->emplace(hi, r);
			break;
		}
	}
}

void Add(uintptr_t b, size_t s, const uintptr_t f[kFrames]) {
	AcquireSRWLockExclusive(&g_lock);
	try {
		Remove(b, b + s);   // pages committed again: counted once
		const uint32_t site = SiteOf(f);
		g_map->emplace(b, Rec{ s, site });
		g_sites[site].live += (long long)s;
		g_sites[site].commits++;
		g_live += (long long)s;
	} catch (...) { g_errors++; }
	ReleaseSRWLockExclusive(&g_lock);
}

void Drop(uintptr_t lo, uintptr_t hi) {
	AcquireSRWLockExclusive(&g_lock);
	try { Remove(lo, hi); } catch (...) { g_errors++; }
	ReleaseSRWLockExclusive(&g_lock);
}

inline bool Skipped(uintptr_t a) {
	for (const Range& r : g_skip) if (a >= r.lo && a < r.hi) return true;
	return false;
}

// The caller's frames: frame 0 is this function, 1 the hook; then the allocation path (ntdll's heap, KernelBase's
// VirtualAlloc, ucrtbase's malloc) is skipped and the next kFrames return addresses are the site.
__declspec(noinline) void Frames(uintptr_t f[kFrames]) {
	void* st[48];
	const USHORT n = RtlCaptureStackBackTrace(2, 48, st, nullptr);
	int k = 0;
	USHORT i = 0;
	while (i < n && Skipped((uintptr_t)st[i])) i++;
	for (; i < n && k < kFrames; i++) f[k++] = (uintptr_t)st[i];
	for (; k < kFrames; k++) f[k] = 0;
}

inline bool Self(HANDLE h) { return h == GetCurrentProcess(); }

LONG NTAPI HkAlloc(HANDLE h, PVOID* base, ULONG_PTR zb, PSIZE_T size, ULONG type, ULONG prot) {
	if (!g_on.load(std::memory_order_relaxed) || !(type & MEM_COMMIT) || !Self(h)) return g_origAlloc(h, base, zb, size, type, prot);
	LastError le;
	if (Inside()) return g_origAlloc(h, base, zb, size, type, prot);
	SetInside(true);
	uintptr_t f[kFrames];
	Frames(f);
	const LONG r = g_origAlloc(h, base, zb, size, type, prot);
	if (r >= 0 && base && *base && size && *size) Add((uintptr_t)*base, *size, f);
	SetInside(false);
	return r;
}

LONG NTAPI HkAllocEx(HANDLE h, PVOID* base, PSIZE_T size, ULONG type, ULONG prot, void* ext, ULONG n) {
	if (!g_on.load(std::memory_order_relaxed) || !(type & MEM_COMMIT) || !Self(h)) return g_origAllocEx(h, base, size, type, prot, ext, n);
	LastError le;
	if (Inside()) return g_origAllocEx(h, base, size, type, prot, ext, n);
	SetInside(true);
	uintptr_t f[kFrames];
	Frames(f);
	const LONG r = g_origAllocEx(h, base, size, type, prot, ext, n);
	if (r >= 0 && base && *base && size && *size) Add((uintptr_t)*base, *size, f);
	SetInside(false);
	return r;
}

uintptr_t ReservationEnd(uintptr_t b) {   // the end of the reservation that starts at b (b itself if it isn't a start)
	MEMORY_BASIC_INFORMATION mi;
	uintptr_t p = b;
	while (VirtualQuery((void*)p, &mi, sizeof mi) == sizeof mi && (uintptr_t)mi.AllocationBase == b && mi.State != MEM_FREE)
		p = (uintptr_t)mi.BaseAddress + mi.RegionSize;
	return p;
}

LONG NTAPI HkFree(HANDLE h, PVOID* base, PSIZE_T size, ULONG type) {
	if (!g_on.load(std::memory_order_relaxed) || !Self(h) || !base || !*base) return g_origFree(h, base, size, type);
	LastError le;
	if (Inside()) return g_origFree(h, base, size, type);
	SetInside(true);
	const uintptr_t b = (uintptr_t)*base;
	const size_t s = size ? *size : 0;
	const uintptr_t lo = b & ~uintptr_t(0xfff);
	const uintptr_t hi = ((type & MEM_RELEASE) || s == 0) ? ReservationEnd(b) : (b + s + 0xfff) & ~uintptr_t(0xfff);
	const LONG r = g_origFree(h, base, size, type);
	if (r >= 0 && hi > lo) Drop(lo, hi);
	SetInside(false);
	return r;
}

// ---- reports
const char* ModName(uintptr_t a, uintptr_t* off) {
	void* b = nullptr;
	*off = a;
	if (!a || !RtlPcToFileHeader((void*)a, &b) || !b) return "?";
	*off = a - (uintptr_t)b;
	static char name[MAX_PATH];   // reports only, under g_reportLock
	if (!GetModuleFileNameA((HMODULE)b, name, MAX_PATH)) return "?";
	const char* s = strrchr(name, '\\');
	return s ? s + 1 : name;
}

std::string Frame(uintptr_t a) {
	if (!a) return "-";
	uintptr_t off = 0;
	const char* m = ModName(a, &off);
	char buf[MAX_PATH + 32];
	snprintf(buf, sizeof buf, "%s+%llx", m, (unsigned long long)off);
	return buf;
}

std::string Frames6(const Site& s, const char* sep) {
	std::string r;
	for (int i = 0; i < kFrames; i++) { if (i) r += sep; r += Frame(s.f[i]); }
	return r;
}

std::atomic<int> g_reportN{0};

// Resident = in this process's working set now (QueryWorkingSetEx, page by page): the RAM a site really holds.
void Resident(size_t nSpans, long long& resTotal) {
	size_t k = 0;
	auto flush = [&]() {
		if (!k) return;
		if (QueryWorkingSetEx(GetCurrentProcess(), g_ws, (DWORD)(k * sizeof *g_ws)))
			for (size_t j = 0; j < k; j++)
				if (g_ws[j].VirtualAttributes.Valid) { g_snap[g_wsSite[j]].res += 4096; resTotal += 4096; }
		k = 0;
	};
	for (size_t i = 0; i < nSpans; i++)
		for (uintptr_t p = g_spans[i].b; p < g_spans[i].b + g_spans[i].s; p += 4096) {
			g_ws[k].VirtualAddress = (void*)p;
			g_wsSite[k++] = g_spans[i].site;
			if (k == kWsBatch) flush();
		}
	flush();
}

void Report(const char* why) {
	AcquireSRWLockExclusive(&g_reportLock);
	const int n = ++g_reportN;
	const bool was = Inside();
	SetInside(true);   // this thread's own allocations (log strings, the CSV) are not tracked
	const ULONGLONG t0 = GetTickCount64();
	uint32_t ns;
	long long live, errors;
	size_t nSpans = 0, nMap;
	AcquireSRWLockExclusive(&g_lock);
	ns = g_nSites;
	memcpy(g_snap, g_sites, sizeof(Site) * ns);
	live = g_live;
	errors = g_errors;
	nMap = g_map->size();
	for (const auto& e : *g_map) {
		if (nSpans == kMaxSpans) break;
		g_spans[nSpans++] = Span{ e.first, e.second.size, e.second.site };
	}
	ReleaseSRWLockExclusive(&g_lock);
	for (uint32_t i = 0; i < ns; i++) g_snap[i].res = 0;
	long long res = 0;
	Resident(nSpans, res);
	// the process's memory by kind; private memory the hook never saw, by page protection and the largest regions (boot
	// 85: 1.7 GB at the load's start, 5.3 GB 20 s in game: commits from before the hook, or made by the kernel for the
	// process, such as the graphics driver's CPU-visible GPU memory)
	unsigned long long priv = 0, mapped = 0, image = 0;
	struct Prot { DWORD p; unsigned long long b; };
	Prot prots[24] = {};
	int np = 0;
	struct Big { uintptr_t base, alloc; unsigned long long size; DWORD prot; };
	Big bigs[8] = {};
	struct Wc { uintptr_t a; SIZE_T n; };
	static Wc wcs[1024];   // the report lock is held
	int nwc = 0;
	auto trackedIn = [&](uintptr_t a, uintptr_t b) {   // g_spans: sorted by base, no overlaps
		size_t lo = 0, hi = nSpans;
		while (lo < hi) { const size_t m = (lo + hi) / 2; if (g_spans[m].b + g_spans[m].s <= a) lo = m + 1; else hi = m; }
		unsigned long long t = 0;
		for (size_t i = lo; i < nSpans && g_spans[i].b < b; i++) {
			const uintptr_t x = g_spans[i].b > a ? g_spans[i].b : a, y = g_spans[i].b + g_spans[i].s < b ? g_spans[i].b + g_spans[i].s : b;
			if (y > x) t += y - x;
		}
		return t;
	};
	MEMORY_BASIC_INFORMATION mi;
	for (uintptr_t p = 0x10000; VirtualQuery((void*)p, &mi, sizeof mi) == sizeof mi; p = (uintptr_t)mi.BaseAddress + mi.RegionSize) {
		if (mi.State == MEM_COMMIT) (mi.Type == MEM_PRIVATE ? priv : mi.Type == MEM_MAPPED ? mapped : image) += mi.RegionSize;
		if (mi.State == MEM_COMMIT && mi.Type == MEM_PRIVATE) {
			const uintptr_t a = (uintptr_t)mi.BaseAddress, b = a + mi.RegionSize;
			const unsigned long long un = mi.RegionSize - trackedIn(a, b);
			if (un) {
				int k = 0;
				while (k < np && prots[k].p != mi.Protect) k++;
				if (k == np && np < 24) prots[np++] = Prot{ mi.Protect, 0 };
				if (k < np) prots[k].b += un;
				int m = 0;
				for (int j = 1; j < 8; j++) if (bigs[j].size < bigs[m].size) m = j;
				if (un > bigs[m].size) bigs[m] = Big{ a, (uintptr_t)mi.AllocationBase, un, mi.Protect };
				if ((mi.Protect & PAGE_WRITECOMBINE) && nwc < 1024) wcs[nwc++] = Wc{ a, mi.RegionSize };
			}
		}
		if ((uintptr_t)mi.BaseAddress + mi.RegionSize <= p) break;
	}
	PROCESS_MEMORY_COUNTERS_EX pc{};
	pc.cb = sizeof pc;
	GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pc, sizeof pc);
	MEMORYSTATUSEX ms{ sizeof ms };
	GlobalMemoryStatusEx(&ms);
	constexpr double MB = 1048576.0;
	// write-combined memory (PAGE_WRITECOMBINE: what the graphics driver gives D3D12 upload heaps and GPU data kept in
	// system RAM): how much of it is in RAM, page by page
	unsigned long long wcAll = 0, wcRes = 0;
	for (int r = 0; r < nwc; r++) {
		wcAll += wcs[r].n;
		for (SIZE_T o = 0; o < wcs[r].n;) {
			size_t k = 0;
			for (; k < kWsBatch && o < wcs[r].n; k++, o += 4096) g_ws[k].VirtualAddress = (void*)(wcs[r].a + o);
			if (QueryWorkingSetEx(GetCurrentProcess(), g_ws, (DWORD)(k * sizeof *g_ws)))
				for (size_t j = 0; j < k; j++) if (g_ws[j].VirtualAttributes.Valid) wcRes += 4096;
		}
	}
	// the GPU's own count for this process (DXGI): video memory used and budget, and system memory used for GPU data
	char gpu[160] = "no DXGI adapter";
	if (HMODULE dx = GetModuleHandleA("dxgi.dll")) {
		using Fn = HRESULT(WINAPI*)(REFIID, void**);
		IDXGIFactory1* f = nullptr;
		if (auto cf = (Fn)GetProcAddress(dx, "CreateDXGIFactory1"); cf && SUCCEEDED(cf(__uuidof(IDXGIFactory1), (void**)&f))) {
			IDXGIAdapter1* best = nullptr;
			SIZE_T bestV = 0;
			IDXGIAdapter1* ad = nullptr;
			for (UINT i = 0; f->EnumAdapters1(i, &ad) != DXGI_ERROR_NOT_FOUND; i++) {
				DXGI_ADAPTER_DESC1 d{};
				ad->GetDesc1(&d);
				if (d.DedicatedVideoMemory > bestV) { if (best) best->Release(); best = ad; bestV = d.DedicatedVideoMemory; }
				else ad->Release();
			}
			IDXGIAdapter3* a3 = nullptr;
			if (best && SUCCEEDED(best->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&a3))) {
				DXGI_QUERY_VIDEO_MEMORY_INFO loc{}, sys{};
				a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &loc);
				a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &sys);
				snprintf(gpu, sizeof gpu, "video memory %.0f of %.0f MB budget (card %.0f MB), system RAM for GPU data %.0f MB (budget %.0f)",
				         loc.CurrentUsage / MB, loc.Budget / MB, bestV / MB, sys.CurrentUsage / MB, sys.Budget / MB);
				a3->Release();
			}
			if (best) best->Release();
			f->Release();
		}
	}
	Log("memsite: report %d (%s): commit charge %.0f MB, working set %.0f MB (peak %.0f), RAM free %.0f MB; committed: private "
	    "%.0f MB, mapped %.0f MB, image %.0f MB; tracked %.0f MB, %.0f MB of it in RAM, %zu ranges%s from %u sites (private not "
	    "tracked: %.0f MB, from before the hook and the census's own %.0f MB); %llu ms%s",
	    n, why, pc.PrivateUsage / MB, pc.WorkingSetSize / MB, pc.PeakWorkingSetSize / MB, ms.ullAvailPhys / MB, priv / MB, mapped / MB,
	    image / MB, live / MB, res / MB, nMap, nSpans < nMap ? " (resident counted on the first 1M)" : "", ns,
	    ((double)priv - (double)live) / MB, g_arenaCommitted / MB, GetTickCount64() - t0, errors ? " (tracker errors!)" : "");
	// by module of the first frame
	struct Mod { uintptr_t base; long long live, res; };
	Mod mods[64] = {};
	int nm = 0;
	for (uint32_t i = 0; i < ns; i++) {
		if (g_snap[i].live <= 0) continue;
		void* b = nullptr;
		const uintptr_t mb = g_snap[i].f[0] && RtlPcToFileHeader((void*)g_snap[i].f[0], &b) ? (uintptr_t)b : 0;
		int k = 0;
		while (k < nm && mods[k].base != mb) k++;
		if (k == nm) { if (nm == 64) continue; mods[nm++] = Mod{ mb, 0, 0 }; }
		mods[k].live += g_snap[i].live;
		mods[k].res += g_snap[i].res;
	}
	std::sort(mods, mods + nm, [](const Mod& a, const Mod& b) { return a.res > b.res; });
	std::string line;
	for (int k = 0; k < nm && k < 14; k++) {
		uintptr_t off = 0;
		char buf[MAX_PATH + 48];
		snprintf(buf, sizeof buf, "%s%s %.0f/%.0f", k ? ", " : "", mods[k].base ? ModName(mods[k].base, &off) : "(no frame)",
		         mods[k].res / MB, mods[k].live / MB);
		line += buf;
	}
	Log("memsite: report %d by module (MB in RAM / committed): %s", n, line.c_str());
	std::sort(prots, prots + np, [](const Prot& x, const Prot& y) { return x.b > y.b; });
	std::sort(bigs, bigs + 8, [](const Big& x, const Big& y) { return x.size > y.size; });
	std::string un, bg;
	for (int k = 0; k < np && k < 8; k++) {
		char buf[64];
		snprintf(buf, sizeof buf, "%sprotect 0x%lx %.0f", k ? ", " : "", prots[k].p, prots[k].b / MB);
		un += buf;
	}
	for (const Big& b : bigs) {
		if (!b.size) continue;
		char buf[96];
		snprintf(buf, sizeof buf, "%s%.0f MB at %p (reservation %p, 0x%lx)", bg.empty() ? "" : ", ", b.size / MB, (void*)b.base, (void*)b.alloc, b.prot);
		bg += buf;
	}
	Log("memsite: report %d private memory the hook never saw, by protection (MB): %s; largest regions: %s", n, un.c_str(), bg.c_str());
	Log("memsite: report %d graphics: write-combined memory %.0f MB in %d regions, %.0f MB of it in RAM; %s", n, wcAll / MB, nwc,
	    wcRes / MB, gpu);
	uint32_t ni = 0;
	for (uint32_t i = 0; i < ns; i++) if (g_snap[i].live >= (1 << 20) || g_snap[i].res >= (1 << 20)) g_idx[ni++] = i;
	std::sort(g_idx, g_idx + ni, [](uint32_t a, uint32_t b) { return g_snap[a].res > g_snap[b].res; });
	for (uint32_t k = 0; k < ni && k < 30; k++) {
		const Site& s = g_snap[g_idx[k]];
		Log("memsite: report %d #%u %.1f MB in RAM of %.1f committed (%lld commits): %s", n, k + 1, s.res / MB, s.live / MB, s.commits,
		    Frames6(s, " < ").c_str());
	}
	char path[40];
	snprintf(path, sizeof path, "fastload_memsite_%d.csv", n);
	if (FILE* fp = fopen((g_dir + path).c_str(), "w")) {
		fprintf(fp, "# %s\nres_mb,live_mb,commits,f0,f1,f2,f3,f4,f5\n", why);
		for (uint32_t k = 0; k < ni; k++) {
			const Site& s = g_snap[g_idx[k]];
			fprintf(fp, "%.2f,%.2f,%lld,%s\n", s.res / MB, s.live / MB, s.commits, Frames6(s, ",").c_str());
		}
		fclose(fp);
	}
	SetInside(was);
	ReleaseSRWLockExclusive(&g_reportLock);
}

DWORD WINAPI Watch(LPVOID) {   // the reports' moments, from the level state
	bool loadSeen = false, s5 = false, s6 = false, inGame = false;
	ULONGLONG gameAt = 0;
	for (;;) {
		Sleep(100);
		const int s = Readiness_LevelState();
		if (!loadSeen && s >= 1) { loadSeen = true; Report("the level load starts"); }
		if (loadSeen && !s5 && s == 5) { s5 = true; Report("level state 5: the save is applied"); }
		if (loadSeen && !s6 && s == 6) { s6 = true; Report("level state 6: Settle"); }
		if (loadSeen && !inGame && s == 0) { inGame = true; gameAt = GetTickCount64(); Report("gameplay starts"); }
		if (inGame && gameAt && GetTickCount64() - gameAt > 20000) { Report("20 s after gameplay starts"); return 0; }
	}
}

void ModRange(Range& r, const char* mod) {
	HMODULE m = GetModuleHandleA(mod);
	MODULEINFO mi{};
	if (m && GetModuleInformation(GetCurrentProcess(), m, &mi, sizeof mi)) { r.lo = (uintptr_t)mi.lpBaseOfDll; r.hi = r.lo + mi.SizeOfImage; }
}

template <class T> T* Pages(size_t n) { return (T*)VirtualAlloc(nullptr, n * sizeof(T), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE); }

}  // namespace

void Memsite_Install(void* self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	g_dir = path;
	g_dir = g_dir.substr(0, g_dir.find_last_of('\\') + 1);
	int mode = 0;
	if (FILE* f = fopen((g_dir + "fastload_memsite.txt").c_str(), "r")) { if (fscanf_s(f, "%d", &mode) != 1) mode = 0; fclose(f); }
	if (mode != 1) { Log("memsite: off (fastload_memsite.txt 1 turns the memory census on)"); return; }
	g_tls = TlsAlloc();
	if (g_tls == TLS_OUT_OF_INDEXES || g_tls >= 64) { Log("memsite: no TLS slot below 64 (%lu) - off", g_tls); return; }
	g_arena = (uint8_t*)VirtualAlloc(nullptr, kArenaReserve, MEM_RESERVE, PAGE_READWRITE);
	g_sites = Pages<Site>(kMaxSites);
	g_snap = Pages<Site>(kMaxSites);
	g_hash = Pages<uint32_t>(kHash);
	g_idx = Pages<uint32_t>(kMaxSites);
	g_spans = Pages<Span>(kMaxSpans);
	g_ws = Pages<PSAPI_WORKING_SET_EX_INFORMATION>(kWsBatch);
	g_wsSite = Pages<uint32_t>(kWsBatch);
	if (!g_arena || !g_sites || !g_snap || !g_hash || !g_idx || !g_spans || !g_ws || !g_wsSite) { Log("memsite: no memory for the tables - off"); return; }
	g_map = new (g_mapBuf) RecMap();
	ModRange(g_skip[0], "ntdll.dll");
	ModRange(g_skip[1], "KernelBase.dll");
	ModRange(g_skip[2], "kernel32.dll");
	ModRange(g_skip[3], "ucrtbase.dll");
	HMODULE nt = GetModuleHandleA("ntdll.dll");
	void* a = (void*)GetProcAddress(nt, "NtAllocateVirtualMemory");
	void* ax = (void*)GetProcAddress(nt, "NtAllocateVirtualMemoryEx");
	void* fr = (void*)GetProcAddress(nt, "NtFreeVirtualMemory");
	const bool ok = a && fr && MH_CreateHook(a, (void*)&HkAlloc, (void**)&g_origAlloc) == MH_OK &&
	                MH_CreateHook(fr, (void*)&HkFree, (void**)&g_origFree) == MH_OK;
	const bool okx = ok && ax && MH_CreateHook(ax, (void*)&HkAllocEx, (void**)&g_origAllocEx) == MH_OK;
	if (!ok) { Log("memsite: hooks FAILED - off"); return; }
	g_on = true;
	MH_EnableHook(fr);
	MH_EnableHook(a);
	if (okx) MH_EnableHook(ax);
	CloseHandle(CreateThread(nullptr, 0, Watch, nullptr, 0, nullptr));
	Log("memsite: on - every commit kept with its caller (NtAllocateVirtualMemoryEx %s); reports at the level load's start, "
	    "states 5 and 6, gameplay start and 20 s later", okx ? "too" : "not hooked");
}

void Memsite_Command(const char*) {
	if (!g_on) { Log("memsite: off"); return; }
	Report("asked");
}
