// Unpacking ahead, step 1: the recorder. See pakahead.h.
#include "pakahead.h"
#include "log.h"
#include "mainreads.h"
#include "ramgov.h"
#include "readiness.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
extern "C" void WINAPI MH_ExemptThread(DWORD tid);   // minhook_shared/hook.c
#include <string>
#include <unordered_map>
#include <vector>
#include "MinHook.h"
#include "libdeflate.h"
#include <algorithm>
#include <deque>
#include <psapi.h>

namespace {

constexpr uintptr_t kRvaRead = 0x4617fc;
const unsigned char kReadBytes[] = { 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,
                                     0x24,0x38,0xfe,0xff,0xff,0x48,0x81,0xec,0xc8,0x02,0x00,0x00 };
constexpr ULONGLONG kRecordMs = 10 * 60 * 1000;

using ReadFn = uint64_t(__fastcall*)(void* cache, int* entry, void* comp, void* uncomp, char decompress, int64_t off,
                                     uint64_t size, char skipCrc);
using FilenoFn = int(__cdecl*)(void*);
using OsHandleFn = intptr_t(__cdecl*)(int);

ReadFn g_orig = nullptr;
FilenoFn g_fileno = nullptr;
OsHandleFn g_osHandle = nullptr;
int g_mode = 1;
std::string g_dir;
ULONGLONG g_start = 0;
long long g_freq = 1, g_t0 = 0;

struct Rec { uint32_t tMs, tid, pak, dataOff, csize, usize, us, crc; uint16_t method; uint8_t decompress, part, hasComp; };
std::mutex g_mu;
std::vector<Rec> g_recs;
std::unordered_map<uintptr_t, uint32_t> g_pakIds;      // FILE* or in-memory pak -> index into g_paks
std::vector<std::string> g_paks;
std::atomic<long long> g_calls{0};

// ---- step 2: unpack ahead (mode 2). fastload_pakahead_list.txt next to the plugin: one line per entry in the order the
// load needs them: "<pak path>\t<data offset>\t<packed>\t<size>\t<crc32>[\t<reads>]". The plugin writes it itself at the
// end of each load from that load's reads (WriteNextList; tools/pakahead_list.py makes one from a recording). Workers
// read + unpack them into RAM from game start, at background priority; the ReadFile hook copies a finished entry into
// the game's buffer instead of reading and unpacking it. <reads> > 1: the load reads that entry again (boot 121: the
// blood mod's 16.8 MB textures 4 times each, 0.9 GB unpacked again on the render loading thread while Main waited), so
// it stays after a serve until its last read or kKeptIdleMs without one.
constexpr int kWorkers = 6;
size_t kCapBytes = 2048ull << 20;                  // unpacked bytes held at once: set from free RAM at start
bool g_pin = true;                                 // VirtualLock the big ones (only with RAM to spare)
constexpr ULONGLONG kKeepMs = 15 * 60 * 1000;     // nothing served after this: everything is freed
struct Item {
	uint32_t pak, dataOff, csize, usize, crc, idx;
	std::atomic<int> state{0};                    // 0 waiting, 1 a worker has it, 2 ready, 3 taken or dropped, 4 being touched,
	                                              // 5 being freed for RAM (back to 0 right after), 6 ready but offered to
	                                              // Windows (OfferVirtualMemory: may be dropped, see kWindow), 8 an
	                                              // in-memory pak's entry being copied to the game (back to 2 after)
	uint8_t* data = nullptr;
	bool freedOnce = false;                       // freed for RAM once: the next time it is dropped for good (no re-read loop)
	bool mem = false;                             // from a pak the game holds in memory (see g_listMem): kept after a serve
	uint8_t left = 0;                             // reads still to come after the next (the list's <reads> - 1); changed
	                                              // only by the thread that holds it in state 3
	bool kept = false;                            // served and kept for those reads (in g_kept)
	std::atomic<bool> keptDone{false};            // its last read took it (the sweep drops it from g_kept)
	std::atomic<ULONGLONG> lastServe{0};
};
std::vector<uint64_t> g_prefix;                    // unpacked bytes before item i (list order)
std::atomic<size_t> g_servedHi{0};                 // 1 + the highest list index the game asked for so far (served or not)
// RAM (boot 85 census: at the level load's start pakahead held 976 MB of the game's 4.3 GB working set, with 1.9 GB
// free): only the next kWindow bytes of the list (from where the load is) stay plain RAM. A big entry unpacked beyond
// that is offered to Windows (OfferVirtualMemory): it counts as free RAM and Windows drops it before it trims the game's
// own pages. Boot 87, lowest priority: Windows dropped 768 of 777 MB offered while the load grew (the game unpacked those
// itself, load 25.0 s); now normal priority, so other programs' file cache goes before it. When the window reaches it, it is reclaimed (ReclaimVirtualMemory); if Windows
// dropped it, a worker unpacks it again. Not with pinned buffers (lots of RAM free: nothing to gain).
constexpr uint64_t kWindow = 256ull << 20;
std::atomic<long long> s_offered{0}, s_offeredNow{0}, s_reclaimed{0}, s_lost{0}, s_lostBytes{0}, s_lostAtServe{0};
// diagnostics + the toucher (22:00 boot: the loader thread copied 3.4 GB at 0.8 GB/s, its memcpy stalled on loads
// from our buffers, unpinned with 6.8 GB free)
std::atomic<long long> s_copyTicks{0}, s_copyBytes{0}, s_resChecked{0}, s_srcOut{0}, s_dstOut{0},
	s_touchTicks{0}, s_touchBytes{0}, s_touchChecked{0}, s_touchOut{0}, s_touchWaits{0};
std::vector<std::string> g_listPaks;               // pak paths of the list, lower case
// Paks the game holds in memory (Engine/Engine.pak, ShaderCache.pak, Shaders.pak; list lines "mem|<path>"): their reads
// come with no FILE*, only the in-memory image, and the game unpacks an entry again on every read (boot 93: Engine.pak
// 758 first reads, 274 MB, 608 ms, plus 1,115 repeats, 449 MB, 1,079 ms, nearly all on one start-up thread at +3-7 s).
// Workers unpack them from the pak file on disk; a read of an in-memory pak is matched to its list pak by the entry
// (data offset, sizes, crc32) and that image is remembered. Such an entry stays after a serve (state 8 while copied),
// so the repeats (crc32 already cleared by the game) are served too; all of them are freed once no in-memory read has
// come for 3 s. Never offered or shed.
std::vector<bool> g_listMem;
bool g_anyMem = false;
std::unordered_map<uintptr_t, int> g_memToListPak; // the game's in-memory image -> list pak index (under g_mapMu)
std::unordered_map<uintptr_t, int> g_memMisses;
std::atomic<long long> s_memFirst{0}, s_memFirstBytes{0}, s_memRepeat{0}, s_memRepeatBytes{0}, s_memFreedBytes{0};
std::atomic<ULONGLONG> g_memLastServe{0};
std::atomic<bool> g_memFreed{false};
std::deque<Item> g_items;                          // deque: Items never move (atomics inside)
std::unordered_map<uint64_t, Item*> g_byKey;       // (list pak index << 32) | data offset
std::unordered_map<uintptr_t, int> g_fileToListPak; // the game's FILE* -> list pak index (-1 = not in the list)
std::mutex g_mapMu;
std::atomic<size_t> g_held{0};
std::mutex g_pickMu;                               // workers pick their next entry under this
size_t g_cursor = 0;                               // no waiting entry below this (moved back when entries are freed)
std::atomic<long long> s_shedBytes{0}, s_shedTimes{0}, s_ramWaits{0}, s_shedDropped{0};
std::atomic<long long> s_served{0}, s_servedBytes{0}, s_missNotReady{0}, s_prefetched{0}, s_bad{0}, s_readMs{0}, s_unpackMs{0};
std::atomic<bool> g_stop{false};
// entries read more than once (the list's <reads>): kept after a serve, freed at their last read or after kKeptIdleMs
// without one (the sweep in Writer)
constexpr ULONGLONG kKeptIdleMs = 4000;            // boot 121: the blood textures' repeats came 10 ms - 3.3 s apart
bool g_anyKeep = false;                            // set by LoadList before the hook goes on
std::mutex g_keptMu;
std::vector<uint32_t> g_kept;                      // list indices of kept entries (under g_keptMu)
std::atomic<long long> s_repServed{0}, s_repBytes{0}, s_keptIdle{0}, s_keptIdleBytes{0}, s_keptPeak{0}, s_keptNow{0};
// the list for the next start (WriteNextList): off with 0 in fastload_pakahead_autolist.txt
bool g_autoList = true;
std::vector<std::pair<uint32_t, std::string>> g_oldMemLines;   // this start's "mem|" lines (list pak, line), carried over
                                                   // for an in-memory pak this run didn't match (its reads are recorded as
                                                   // mem:<address> only, so this run alone can't name its file)
size_t g_oldListCount = 0;

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

std::string PakName(uintptr_t file, uintptr_t inMem) {
	if (inMem) { char b[40]; snprintf(b, sizeof b, "mem:%llx", (unsigned long long)inMem); return b; }
	if (!file || !g_fileno || !g_osHandle) return "?";
	const int fd = g_fileno((void*)file);
	if (fd < 0) return "?";
	const HANDLE h = (HANDLE)g_osHandle(fd);
	if (h == INVALID_HANDLE_VALUE || !h) return "?";
	char path[MAX_PATH * 2] = {};
	const DWORD n = GetFinalPathNameByHandleA(h, path, sizeof path, FILE_NAME_NORMALIZED);
	if (!n || n >= sizeof path) return "?";
	return strncmp(path, "\\\\?\\", 4) ? path : path + 4;
}

// SEH reads in their own functions (MSVC: no __try where C++ objects need unwinding)
void ReadEntry(const int* entry, Rec& r) {
	__try {
		r.crc = (uint32_t)entry[0]; r.csize = (uint32_t)entry[1]; r.usize = (uint32_t)entry[2]; r.dataOff = (uint32_t)entry[3];
		r.method = *(const uint16_t*)((const char*)entry + 0x18);
	} __except (EXCEPTION_EXECUTE_HANDLER) {}
}
void ReadZipFile(const void* cache, uintptr_t& file, uintptr_t& inMem) {
	__try { const uintptr_t* zf = (const uintptr_t*)((const char*)cache + 8); file = zf[0]; inMem = zf[7]; }
	__except (EXCEPTION_EXECUTE_HANDLER) {}
}

std::string Lower(std::string x) { for (char& c : x) c = (char)tolower((unsigned char)c); return x; }

int ListPakOf(uintptr_t file) {
	std::lock_guard<std::mutex> lk(g_mapMu);
	auto it = g_fileToListPak.find(file);
	if (it != g_fileToListPak.end()) return it->second;
	const std::string name = Lower(PakName(file, 0));
	int idx = -1;
	for (size_t i = 0; i < g_listPaks.size(); i++) if (g_listPaks[i] == name) { idx = (int)i; break; }
	g_fileToListPak.emplace(file, idx);
	return idx;
}

// an in-memory pak's list index, from the first read of it that matches a listed entry exactly (crc32 included)
int MemListPak(uintptr_t inMem, const Rec& r) {
	std::lock_guard<std::mutex> lk(g_mapMu);
	auto it = g_memToListPak.find(inMem);
	if (it != g_memToListPak.end()) return it->second;
	if (r.crc == 0) return -1;
	int found = -1, n = 0;
	for (size_t lp = 0; lp < g_listPaks.size(); lp++) {
		if (!g_listMem[lp]) continue;
		auto f = g_byKey.find(((uint64_t)lp << 32) | r.dataOff);
		if (f == g_byKey.end()) continue;
		const Item& e = *f->second;
		if (e.csize == r.csize && e.usize == r.usize && e.crc == r.crc) { found = (int)lp; n++; }
	}
	if (n == 1) {
		g_memToListPak.emplace(inMem, found);
		Log("pakahead: in-memory pak %p is %s", (void*)inMem, g_listPaks[found].c_str());
		return found;
	}
	if (++g_memMisses[inMem] >= 256) g_memToListPak.emplace(inMem, -1);   // not one of ours
	return -1;
}

// Ready entries of 64 KB and more live in VirtualAlloc'd, VirtualLock'd memory: the 2026-10-23 boot sampler showed
// the game's loader thread in our memcpy 23 % of the terrain step, stalled reading these buffers (16 GB PC, the game
// grows to ~6.5 GB during the load: Windows had paged the held buffers out). Smaller ones use malloc.
constexpr uint32_t kBig = 64 * 1024;
std::atomic<long long> s_locked{0}, s_lockFail{0};
uint8_t* AllocData(uint32_t n) {
	if (n < kBig) return (uint8_t*)malloc(n ? n : 1);
	void* p = VirtualAlloc(nullptr, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (p && g_pin && VirtualLock(p, n)) s_locked++; else if (p) s_lockFail++;
	return (uint8_t*)p;
}
void FreeData(uint8_t* p, uint32_t n) {
	if (!p) return;
	if (n < kBig) { free(p); return; }
	VirtualUnlock(p, n);   // fails harmlessly when the lock had failed
	VirtualFree(p, 0, MEM_RELEASE);
}

void Release(Item& it) {
	if (it.data) { FreeData(it.data, it.usize); it.data = nullptr; g_held -= it.usize; }
}

// pages of [p, p+n) not in the working set, out of up to 16 sampled evenly; adds to checked / out
void CountOut(const void* p, size_t n, std::atomic<long long>& checked, std::atomic<long long>& out) {
	if (!p || n < 4096) return;
	PSAPI_WORKING_SET_EX_INFORMATION w[16];
	const size_t pages = n / 4096, k = pages < 16 ? pages : 16;
	for (size_t i = 0; i < k; i++) w[i].VirtualAddress = (void*)(((uintptr_t)p & ~(uintptr_t)4095) + (i * pages / k) * 4096);
	if (!K32QueryWorkingSetEx(GetCurrentProcess(), w, (DWORD)(k * sizeof w[0]))) return;
	long long o = 0;
	for (size_t i = 0; i < k; i++) if (!w[i].VirtualAttributes.Valid) o++;
	checked += (long long)k; out += o;
}

// Behind the load's place it is due now and stays. Before 2026-10-25 the unsigned difference wrapped for those and lent
// them all to Windows: boot 121, one thread read a face mod's entries listed near the end at 9.7 s (the list was a day
// old), the place jumped to 3.1 of 3.7 GB, and Windows dropped 723 MB the game then unpacked itself.
inline bool Beyond(const Item& it) {
	const size_t hi = g_servedHi.load() < g_items.size() ? g_servedHi.load() : g_items.size();
	return it.idx > hi && g_prefix[it.idx] - g_prefix[hi] >= kWindow;
}
inline size_t PageUp(uint32_t n) { return ((size_t)n + 4095) & ~(size_t)4095; }

// state 2 -> 6 when it lies beyond the window (the caller holds it in state 4)
bool Offer(Item& it) {
	if (g_pin || it.mem || it.usize < kBig || !it.data || !Beyond(it)) return false;
	if (OfferVirtualMemory(it.data, PageUp(it.usize), VmOfferPriorityNormal) != ERROR_SUCCESS) return false;
	s_offered += it.usize;
	s_offeredNow += it.usize;
	return true;
}

// an offered entry back (the caller holds it in state 4): true = intact; false = Windows dropped it (the caller frees it)
bool Reclaim(Item& it) {
	const DWORD r = ReclaimVirtualMemory(it.data, PageUp(it.usize));
	s_offeredNow -= it.usize;
	if (r == ERROR_SUCCESS) { s_reclaimed++; return true; }
	s_lost++;
	s_lostBytes += it.usize;
	return false;
}

// takes a ready entry for the game; waits up to 50 ms while the toucher is on it
bool TakeReady(Item& it) {
	const long long lim = Now() + g_freq / 20;
	bool waited = false;
	for (;;) {
		int s = 2;
		if (it.state.compare_exchange_strong(s, 3)) { if (waited) s_touchWaits++; return true; }
		if (s == 5) { YieldProcessor(); continue; }   // being freed for RAM: back at 0 in a moment, then the game reads it
		if (s == 6) {                                 // offered and not reclaimed yet: take it back here
			if (!it.state.compare_exchange_strong(s, 4)) continue;
			if (Reclaim(it)) { it.state.store(3); return true; }
			Release(it);
			it.state.store(3);
			s_lostAtServe++;
			return false;
		}
		if (s != 4 || Now() > lim) {
			// not started (0): the workers skip it; being unpacked (1): the worker's 1 -> 2 then fails and it frees the
			// buffer (before 2026-10-24 it stayed held as "ready", never served, until the 15-min close)
			if (s == 0 || s == 1) { int w = s; it.state.compare_exchange_strong(w, 3); }
			return false;
		}
		waited = true;
		YieldProcessor();
	}
}

// mode 2: a finished entry for this read? copies it into the game's buffer; true = served (the game's read is skipped)
bool TryServe(void* cache, const Rec& r, void* comp, void* uncomp, int64_t off, uint64_t size) {
	if (g_items.empty() || g_stop || !uncomp || off != 0 || (size != ~0ull && size != (uint64_t)r.usize)) return false;
	if (!r.decompress && r.method != 0) return false;           // the caller wants the packed bytes
	uintptr_t file = 0, inMem = 0;
	ReadZipFile(cache, file, inMem);
	if (!inMem && r.crc == 0 && !g_anyKeep) return false;        // entry already read and checked once: not first use
	int lp = -1;
	if (inMem) lp = g_memFreed.load() ? -1 : MemListPak(inMem, r);
	else if (file) lp = ListPakOf(file);
	if (lp < 0) return false;
	auto f = g_byKey.find(((uint64_t)lp << 32) | r.dataOff);
	if (f == g_byKey.end()) return false;
	Item& it = *f->second;
	if (it.usize != r.usize || it.csize != r.csize) return false;   // same pak, place, sizes
	// and crc32; a repeat has it cleared by the game's first read (served for in-memory paks and kept entries: same pak,
	// place and sizes are the same bytes)
	if (r.crc != 0 ? it.crc != r.crc : !(it.mem || it.kept)) return false;
	for (size_t hi = g_servedHi.load(); hi < it.idx + 1 && !g_servedHi.compare_exchange_weak(hi, it.idx + 1);) {}   // the window moves on
	if (it.mem) {   // kept for the repeats: copied under state 8
		const long long lim = Now() + g_freq / 20;
		for (;;) {
			int s = 2;
			if (it.state.compare_exchange_strong(s, 8)) break;
			if ((s == 4 || s == 8) && Now() < lim) { YieldProcessor(); continue; }   // the toucher / another copy on it
			if (s == 0 || s == 1) s_missNotReady++;
			return false;
		}
		memcpy(uncomp, it.data, it.usize);
		it.state.store(2);
		g_memLastServe = GetTickCount64();
		if (r.crc != 0) { s_memFirst++; s_memFirstBytes += it.usize; }
		else { s_memRepeat++; s_memRepeatBytes += it.usize; }
		s_served++;
		s_servedBytes += r.usize;
		return true;
	}
	if (!TakeReady(it)) { s_missNotReady++; return false; }
	static std::atomic<unsigned> sample{0};
	if (it.usize >= kBig && (sample++ & 7) == 0) {   // every 8th big one: are the pages in RAM?
		CountOut(it.data, it.usize, s_resChecked, s_srcOut);
		std::atomic<long long> dummy{0};
		CountOut(uncomp, it.usize, dummy, s_dstOut);
	}
	const long long c0 = Now();
	memcpy(uncomp, it.data, it.usize);
	if (it.usize >= kBig) { s_copyTicks += Now() - c0; s_copyBytes += it.usize; }
	if (r.crc == 0) { s_repServed++; s_repBytes += it.usize; }
	if (it.left > 0) {   // read again later: kept (state 2) until then
		it.left--;
		it.lastServe = GetTickCount64();
		const bool first = !it.kept;
		it.kept = true;
		it.state.store(2);
		if (first) {
			std::lock_guard<std::mutex> lk(g_keptMu);
			g_kept.push_back(it.idx);
			const long long now = s_keptNow += it.usize;
			if (now > s_keptPeak) s_keptPeak = now;
		}
	} else {
		Release(it);
		if (it.kept) { s_keptNow -= it.usize; it.keptDone = true; }
	}
	s_served++;
	s_servedBytes += r.usize;
	return true;
}

uint64_t __fastcall HkRead(void* cache, int* entry, void* comp, void* uncomp, char decompress, int64_t off, uint64_t size,
                           char skipCrc) {
	if (!entry) return g_orig(cache, entry, comp, uncomp, decompress, off, size, skipCrc);
	const bool record = g_mode >= 1 && GetTickCount64() - g_start <= kRecordMs;
	const bool watch = MainReads_On();   // a mainreads window on this thread (any time, any mode)
	Rec r{};
	ReadEntry(entry, r);
	r.decompress = decompress ? 1 : 0;   // before TryServe: it needs this (2026-10-23 bug: every packed entry missed)
	r.hasComp = comp ? 1 : 0;
	if (g_mode == 2) {
		const long long a = Now();
		if (TryServe(cache, r, comp, uncomp, off, size)) {
			if (watch) {
				uintptr_t file = 0, inMem = 0;
				ReadZipFile(cache, file, inMem);
				MainReads_Add('P', PakName(file, 0).c_str(), r.dataOff, r.usize, a, Now() - a);
			}
			if (!record) return 0;
			r.tMs = (uint32_t)((a - g_t0) * 1000 / g_freq);
			r.us = (uint32_t)((Now() - a) * 1000000 / g_freq);
			r.tid = GetCurrentThreadId();
			r.decompress = 2;                         // 2 = served from the unpack-ahead store
			uintptr_t file = 0, inMem = 0;
			ReadZipFile(cache, file, inMem);
			const uintptr_t key = inMem ? inMem : file;   // an in-memory pak stays "mem:" in the recording, as unserved
			std::lock_guard<std::mutex> lk(g_mu);
			auto it = g_pakIds.find(key);
			if (it == g_pakIds.end()) { it = g_pakIds.emplace(key, (uint32_t)g_paks.size()).first; g_paks.push_back(PakName(file, inMem)); }
			r.pak = it->second;
			g_recs.push_back(r);
			return 0;
		}
	}
	if (!record && !watch) return g_orig(cache, entry, comp, uncomp, decompress, off, size, skipCrc);
	r.part = (off != 0 || (size != ~0ull && size != (uint64_t)r.usize)) ? 1 : 0;
	uintptr_t file = 0, inMem = 0;
	ReadZipFile(cache, file, inMem);
	const long long a = Now();
	const uint64_t ret = g_orig(cache, entry, comp, uncomp, decompress, off, size, skipCrc);
	const long long b = Now();
	r.tMs = (uint32_t)((a - g_t0) * 1000 / g_freq);
	r.us = (uint32_t)((b - a) * 1000000 / g_freq);
	r.tid = GetCurrentThreadId();
	const uintptr_t key = inMem ? inMem : file;
	std::lock_guard<std::mutex> lk(g_mu);
	auto it = g_pakIds.find(key);
	if (it == g_pakIds.end()) {
		it = g_pakIds.emplace(key, (uint32_t)g_paks.size()).first;
		g_paks.push_back(PakName(file, inMem));   // once per pak; the game's FILE* is still open here
	}
	r.pak = it->second;
	// names are cached per FILE*: a pak closed and another opened at the same address later would show the old name
	if (watch) MainReads_Add('p', g_paks[r.pak].c_str(), r.dataOff, r.usize, a, b - a);
	if (!record) return ret;
	g_recs.push_back(r);
	g_calls++;
	return ret;
}

// ---- workers
// the lowest waiting entry, now the caller's (state 0 -> 1); g_items.size() = none left
size_t Pick() {
	std::lock_guard<std::mutex> lk(g_pickMu);
	const size_t n = g_items.size();
	for (; g_cursor < n; g_cursor++) {
		int w = 0;
		if (g_items[g_cursor].state.compare_exchange_strong(w, 1)) return g_cursor++;
	}
	return n;
}

// RAM is short: frees ready entries from the far end (list order) until `want` bytes are freed; they wait again
void Shed(long long want) {
	size_t top;
	{ std::lock_guard<std::mutex> lk(g_pickMu); top = g_cursor; }
	const size_t floorIdx = g_servedHi.load();
	long long got = 0;
	size_t lowest = top;
	for (size_t i = top; i-- > floorIdx && got < want;) {
		Item& it = g_items[i];
		if (it.mem) continue;   // needed in the start-up's first seconds, freed after them
		int r = 2;
		if (!it.state.compare_exchange_strong(r, 5)) continue;
		got += it.usize;
		Release(it);
		if (it.freedOnce) { it.state.store(3); s_shedDropped++; continue; }   // boot 15/16: re-reading looped (12 goes)
		it.freedOnce = true;
		it.state.store(0);
		lowest = i;
	}
	if (!got) return;
	{ std::lock_guard<std::mutex> lk(g_pickMu); if (lowest < g_cursor) g_cursor = lowest; }
	s_shedBytes += got;
	if (s_shedTimes++ == 0)
		Log("pakahead: free RAM %lld MB, below %lld: freeing the farthest-ahead entries (%.0f MB now; read again when there "
		    "is room)", ramgov::AvailMb(), ramgov::ShedLevel().load(), got / 1e6);
}

DWORD WINAPI Worker(LPVOID) {
	MH_ExemptThread(GetCurrentThreadId());   // own code only (files, libdeflate): MinHook's freezes don't wait for it (hook.c)
	SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);   // low CPU and I/O priority: the menu stays smooth
	libdeflate_decompressor* d = libdeflate_alloc_decompressor();
	std::vector<HANDLE> handles(g_listPaks.size(), INVALID_HANDLE_VALUE);
	std::vector<uint8_t> packed;
	const size_t n = g_items.size();
	for (;;) {
		if (g_stop) break;
		for (bool counted = false; !g_stop && !ramgov::CanTake(8 << 20); Sleep(20))   // RAM first: the game comes first
			if (!counted) { counted = true; s_ramWaits++; }
		const size_t i = Pick();
		if (i >= n) {   // wait at the end: entries freed for RAM or dropped by Windows come back to 0 (before boot 87 the
		                // workers exited here and those were never unpacked again); the read buffer goes meanwhile (boot 88:
		                // 6 waiting workers kept 120 MB)
			if (packed.capacity()) std::vector<uint8_t>().swap(packed);
			Sleep(50);
			continue;
		}
		Item& it = g_items[i];
		while (!g_stop && g_held.load() + it.usize > kCapBytes && it.state.load() == 1) Sleep(20);   // full: wait for the load to take some
		if (g_stop) { it.state = 3; break; }
		if (it.state.load() != 1) continue;                                   // the game read it itself meanwhile
		HANDLE& h = handles[it.pak];
		if (h == INVALID_HANDLE_VALUE)
			h = CreateFileA(g_listPaks[it.pak].c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
			                FILE_ATTRIBUTE_NORMAL, nullptr);
		bool ok = h != INVALID_HANDLE_VALUE;
		uint8_t* out = ok ? AllocData(it.usize) : nullptr;
		ok = out != nullptr;
		const long long t0 = Now();
		if (ok) {
			packed.resize(it.csize);
			LARGE_INTEGER pos; pos.QuadPart = it.dataOff;
			DWORD got = 0;
			ok = SetFilePointerEx(h, pos, nullptr, FILE_BEGIN) && ReadFile(h, packed.data(), it.csize, &got, nullptr) && got == it.csize;
		}
		const long long t1 = Now();
		if (ok) {
			if (it.csize == it.usize) memcpy(out, packed.data(), it.usize);   // stored
			else {
				size_t got = 0;
				ok = libdeflate_deflate_decompress(d, packed.data(), it.csize, out, it.usize, &got) == LIBDEFLATE_SUCCESS && got == it.usize;
			}
			ok = ok && libdeflate_crc32(0, out, it.usize) == it.crc;
		}
		s_readMs += (t1 - t0) * 1000 / g_freq;
		s_unpackMs += (Now() - t1) * 1000 / g_freq;
		if (!ok) { FreeData(out, it.usize); s_bad++; it.state = 3; continue; }
		g_held += it.usize;
		it.data = out;
		int working = 1;
		if (!it.state.compare_exchange_strong(working, 4)) { Release(it); continue; }   // the game asked meanwhile and did it itself
		s_prefetched++;
		it.state.store(Offer(it) ? 6 : 2);   // far ahead of the load: offered to Windows
	}
	for (HANDLE h : handles) if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
	libdeflate_free_decompressor(d);
	return 0;
}

// The toucher: reads one byte per page of the ready entries up to 256 MB ahead of the game (list order), so pages
// Windows moved out of the game's working set come back on this thread instead of the game's loader thread.
constexpr uint64_t kTouchAhead = kWindow;
DWORD WINAPI Toucher(LPVOID) {
	const size_t n = g_items.size();
	size_t t = 0;
	ULONGLONG lastShed = 0;
	while (!g_stop && t < n) {
		const ULONGLONG now = GetTickCount64();
		if (now - lastShed >= 50) {   // RAM short: free from the far end (also while the workers wait)
			lastShed = now;
			if (const long long want = ramgov::ShedBytes()) Shed(want);
		}
		const size_t hi = g_servedHi.load();
		if (t < hi) t = hi;                                        // fell behind the game: catch up
		if (t >= n) break;
		if (g_prefix[t] - g_prefix[hi] >= kTouchAhead) { Sleep(1); continue; }
		Item& it = g_items[t];
		int s6 = 6;
		if (it.state.compare_exchange_strong(s6, 4) && !Reclaim(it)) {   // the window reached an offered entry Windows dropped
			Release(it);
			it.state.store(0);   // a worker unpacks it again
			{ std::lock_guard<std::mutex> lk(g_pickMu); if (t < g_cursor) g_cursor = t; }
			t++;
			continue;
		}
		int s = s6 == 6 ? 4 : 2;   // reclaimed: already ours (4)
		if (s == 4 || it.state.compare_exchange_strong(s, 4)) {
			const long long a = Now();
			CountOut(it.data, it.usize, s_touchChecked, s_touchOut);
			volatile uint8_t sink = 0;
			for (size_t o = 0; o < it.usize; o += 4096) sink += it.data[o];
			if (it.usize) sink += it.data[it.usize - 1];
			s_touchTicks += Now() - a;
			s_touchBytes += it.usize;
			it.state.store(2);
		}
		t++;   // not unpacked yet: its worker writes it fresh; taken: nothing to do
	}
	return 0;
}

bool LoadList() {
	FILE* f = fopen((g_dir + "fastload_pakahead_list.txt").c_str(), "r");
	if (!f) { Log("pakahead: no fastload_pakahead_list.txt - nothing to unpack ahead"); return false; }
	char line[1200];
	size_t bytes = 0;
	while (fgets(line, sizeof line, f)) {
		char path[1024]; unsigned off, cs, us, crc, reads = 1;
		if (line[0] == '#' || sscanf(line, "%1023[^\t]\t%u\t%u\t%u\t%x\t%u", path, &off, &cs, &us, &crc, &reads) < 5) continue;
		const bool mem = !strncmp(path, "mem|", 4);
		const std::string p = Lower(path + (mem ? 4 : 0));
		uint32_t idx = 0;
		while (idx < g_listPaks.size() && g_listPaks[idx] != p) idx++;
		if (idx == g_listPaks.size()) { g_listPaks.push_back(p); g_listMem.push_back(mem); }
		g_items.emplace_back();
		Item& it = g_items.back();
		it.pak = idx; it.dataOff = off; it.csize = cs; it.usize = us; it.crc = crc; it.idx = (uint32_t)(g_items.size() - 1);
		it.mem = g_listMem[idx];
		if (it.mem) { g_anyMem = true; g_oldMemLines.emplace_back(idx, line); }
		if (!it.mem && reads > 1) { it.left = (uint8_t)(reads > 255 ? 254 : reads - 1); g_anyKeep = true; }
		g_byKey.emplace(((uint64_t)idx << 32) | off, &it);
		bytes += us;
	}
	fclose(f);
	g_prefix.assign(g_items.size() + 1, 0);
	for (size_t i = 0; i < g_items.size(); i++) g_prefix[i + 1] = g_prefix[i] + g_items[i].usize;
	g_oldListCount = g_items.size();
	size_t rep = 0; uint64_t repBytes = 0;
	for (const Item& it : g_items) if (it.left) { rep++; repBytes += (uint64_t)it.left * it.usize; }
	Log("pakahead: list %zu entries from %zu paks, %.0f MB unpacked; %zu read more than once (kept for %.0f MB of repeats)",
	    g_items.size(), g_listPaks.size(), bytes / 1e6, rep, repBytes / 1e6);
	return !g_items.empty();
}

void StartAhead() {
	MEMORYSTATUSEX m{ sizeof m };
	GlobalMemoryStatusEx(&m);
	const unsigned long long freeMb = m.ullAvailPhys >> 20;
	// At most 2 GB held; free RAM decides the rest as it goes (ramgov.h: read while >= 1.5 GB free, free the farthest
	// entries below 1.2 GB). The old rule fixed the cap at start from free RAM - 3.5 GB: boot 14 started with 6.6 GB free,
	// held 2 GB, and the game's growth then paged it out. Pin only with lots to spare.
	kCapBytes = 2048ull << 20;
	g_pin = freeMb >= 7500;
	// room to lock the held buffers: the minimum working set bounds what VirtualLock may pin
	SIZE_T mn = 0, mx = 0;
	DWORD fl = 0;
	const bool ws = g_pin && GetProcessWorkingSetSizeEx(GetCurrentProcess(), &mn, &mx, &fl) &&
	                SetProcessWorkingSetSizeEx(GetCurrentProcess(), mn + kCapBytes + (128ull << 20),
	                                           (mx > mn + kCapBytes + (256ull << 20) ? mx : mn + kCapBytes + (256ull << 20)),
	                                           QUOTA_LIMITS_HARDWS_MIN_DISABLE | QUOTA_LIMITS_HARDWS_MAX_DISABLE);
	Log("pakahead: working set minimum %s (%zu MB -> +%zu MB)", ws ? "raised" : "NOT raised", mn >> 20, (kCapBytes >> 20) + 128);
	for (int i = 0; i < kWorkers; i++) CloseHandle(CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr));
	CloseHandle(CreateThread(nullptr, 0, Toucher, nullptr, 0, nullptr));
	Log("pakahead: %d workers unpacking ahead (%llu MB RAM free, cap %zu MB, reading while >= %lld MB free, %s)", kWorkers,
	    freeMb, kCapBytes >> 20, ramgov::TakeLevel().load(), g_pin ? "pinned" : "not pinned: little RAM");
}

void Flush() {
	std::vector<Rec> recs; std::vector<std::string> paks;
	{ std::lock_guard<std::mutex> lk(g_mu); recs = g_recs; paks = g_paks; }
	const std::string tmp = g_dir + "fastload_pakahead_trace.tmp", out = g_dir + "fastload_pakahead_trace.csv";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f) return;
	fprintf(f, "# paks\n");
	for (size_t i = 0; i < paks.size(); i++) fprintf(f, "#%zu,%s\n", i, paks[i].c_str());
	fprintf(f, "t_ms,tid,pak,data_off,csize,usize,crc,method,decompress,part,has_comp,us\n");
	for (const Rec& r : recs)
		fprintf(f, "%u,%u,%u,%u,%u,%u,%08x,%u,%u,%u,%u,%u\n", r.tMs, r.tid, r.pak, r.dataOff, r.csize, r.usize, r.crc, r.method,
		        r.decompress, r.part, r.hasComp, r.us);
	fclose(f);
	MoveFileExA(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// the in-memory paks' entries, once no read of them has come for 3 s (or at the store's close)
void FreeMem(const char* why) {
	if (g_memFreed.exchange(true)) return;
	long long freed = 0;
	for (Item& it : g_items) {
		if (!it.mem) continue;
		for (int k = 0; k < 1000; k++) {   // a copy under way (8) ends in microseconds
			int s2 = 2;
			if (it.state.compare_exchange_strong(s2, 3)) { freed += it.usize; Release(it); break; }
			if (s2 != 8 && s2 != 4) break;
			Sleep(0);
		}
	}
	s_memFreedBytes = freed;
	Log("pakahead: in-memory paks (%s): served %lld first reads (%.0f MB) and %lld repeats (%.0f MB); %.0f MB freed", why,
	    s_memFirst.load(), s_memFirstBytes.load() / 1e6, s_memRepeat.load(), s_memRepeatBytes.load() / 1e6, freed / 1e6);
}

// kept entries: dropped from g_kept once their last read took them; freed after kKeptIdleMs without a read
void SweepKept() {
	std::lock_guard<std::mutex> lk(g_keptMu);
	const ULONGLONG now = GetTickCount64();
	for (size_t k = 0; k < g_kept.size();) {
		Item& it = g_items[g_kept[k]];
		bool gone = it.keptDone.load() || g_stop.load();
		if (!gone && now - it.lastServe.load() > kKeptIdleMs) {
			int s2 = 2;
			if (it.state.compare_exchange_strong(s2, 3)) {
				s_keptIdle++;
				s_keptIdleBytes += it.usize;
				s_keptNow -= it.usize;
				Release(it);
				gone = true;
			}
		}
		if (gone) { g_kept[k] = g_kept.back(); g_kept.pop_back(); } else k++;
	}
}

// The list for the next start, from this run's reads up to gameplay start. The user's newest save changes as they play,
// and a mod update moves entries: the list made by hand on 23 Oct still had 2.1 GB of a face mod the game no longer read
// in boot 121 (its pak changed on the 24th). First whole reads in time order; <reads> counts every whole read of the
// entry until then (the next start keeps those entries for the repeats).
void WriteNextList(uint32_t untilMs) {
	std::vector<Rec> recs; std::vector<std::string> paks; std::vector<std::pair<uintptr_t, uint32_t>> ids;
	{
		std::lock_guard<std::mutex> lk(g_mu);
		recs = g_recs;
		paks = g_paks;
		ids.assign(g_pakIds.begin(), g_pakIds.end());
	}
	// recorded pak -> its path in the list ("" = left out); an in-memory pak by the list pak its image matched this run
	std::vector<std::string> path(paks.size());
	std::vector<bool> memSeen(g_listPaks.size(), false);
	{
		std::lock_guard<std::mutex> lk(g_mapMu);
		for (const auto& [key, i] : ids) {
			if (i >= paks.size()) continue;
			if (!paks[i].compare(0, 4, "mem:")) {
				auto m = g_memToListPak.find(key);
				if (m != g_memToListPak.end() && m->second >= 0) { path[i] = "mem|" + g_listPaks[m->second]; memSeen[m->second] = true; }
			} else if (paks[i] != "?") path[i] = paks[i];
		}
	}
	std::stable_sort(recs.begin(), recs.end(), [](const Rec& a, const Rec& b) { return a.tMs < b.tMs; });
	struct E { uint32_t path, off, cs, us, crc, reads; };
	std::unordered_map<std::string, uint32_t> pathId;
	std::vector<std::string> paths;
	std::unordered_map<uint64_t, size_t> at;
	std::vector<E> out;
	for (const Rec& r : recs) {
		if (r.tMs > untilMs) break;
		if (r.part || r.pak >= path.size() || path[r.pak].empty()) continue;
		if (r.decompress == 0 && r.method != 0) continue;   // the packed bytes wanted
		auto p = pathId.emplace(Lower(path[r.pak]), (uint32_t)paths.size());
		if (p.second) paths.push_back(path[r.pak]);
		const uint64_t k = ((uint64_t)p.first->second << 32) | r.dataOff;
		auto f = at.find(k);
		if (f != at.end()) { if (out[f->second].reads < 255) out[f->second].reads++; continue; }
		if (r.crc == 0) continue;   // its first whole read came before the recording
		at.emplace(k, out.size());
		out.push_back({p.first->second, r.dataOff, r.csize, r.usize, r.crc, 1});
	}
	const std::string list = g_dir + "fastload_pakahead_list.txt", tmp = list + ".tmp", prev = g_dir + "fastload_pakahead_list.prev.txt";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f) { Log("pakahead: the list for the next start NOT written (can't open %s)", tmp.c_str()); return; }
	SYSTEMTIME st; GetLocalTime(&st);
	fprintf(f, "# written by fastload (pakahead.cpp) at gameplay start, %04d-%02d-%02d %02d:%02d:%02d, %.1f s after the plugin: "
	        "the load's whole reads in time order, <reads> = whole reads until then\n",
	        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, untilMs / 1000.0);
	size_t carried = 0;
	for (const auto& [lp, line] : g_oldMemLines)   // an in-memory pak this run didn't match: its old lines, first
		if (lp < memSeen.size() && !memSeen[lp]) { fputs(line.c_str(), f); carried++; }
	uint64_t bytes = 0, repBytes = 0;
	size_t rep = 0;
	for (const E& e : out) {
		fprintf(f, "%s\t%u\t%u\t%u\t%08x\t%u\n", paths[e.path].c_str(), e.off, e.cs, e.us, e.crc, e.reads);
		bytes += e.us;
		if (e.reads > 1) { rep++; repBytes += (uint64_t)(e.reads - 1) * e.us; }
	}
	if (fclose(f) != 0) { Log("pakahead: the list for the next start NOT written (write failed)"); return; }
	MoveFileExA(list.c_str(), prev.c_str(), MOVEFILE_REPLACE_EXISTING);
	const bool ok = MoveFileExA(tmp.c_str(), list.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
	Log("pakahead: list for the next start %s: %zu entries (%.0f MB) from %zu paks, %zu read more than once (%.0f MB of repeats), "
	    "%zu in-memory lines carried over; this start's list had %zu (kept as fastload_pakahead_list.prev.txt)",
	    ok ? "written" : "NOT moved into place", out.size(), bytes / 1e6, paths.size(), rep, repBytes / 1e6, carried, g_oldListCount);
}

DWORD WINAPI Writer(LPVOID) {
	size_t written = 0;
	bool sawLoad = false, listDone = false;
	for (;;) {
		for (int k = 0; k < 10; k++) {   // 5 s, checking the in-memory paks' quiet every 0.5 s
			Sleep(500);
			if (g_anyKeep) SweepKept();
			if (!listDone) {   // the first load's end: the list for the next start
				const int ls = Readiness_LevelState();
				if (ls >= 1) sawLoad = true;
				else if (ls == 0 && sawLoad) {
					listDone = true;
					const uint32_t ms = (uint32_t)((Now() - g_t0) * 1000 / g_freq);
					if (g_mode != 2 || !g_autoList) Log("pakahead: no list for the next start (%s)", g_mode != 2 ? "mode 1" : "fastload_pakahead_autolist.txt 0");
					else if (ms > kRecordMs) Log("pakahead: no list for the next start (the load ended after the recording's 10 min)");
					else WriteNextList(ms);
				}
			}
			const ULONGLONG last = g_memLastServe.load();
			if (g_memFreed.load() || !g_anyMem) continue;
			if (last && GetTickCount64() - last > 3000) FreeMem("no read of them for 3 s");
			else if (!last && GetTickCount64() - g_start > 60000) FreeMem("none asked for in the first 60 s");
		}
		size_t n; { std::lock_guard<std::mutex> lk(g_mu); n = g_recs.size(); }
		if (n != written) { Flush(); written = n; }
		static long long lastServed = -1, lastPre = -1;
		if (!g_items.empty() && (s_served != lastServed || s_prefetched != lastPre)) {
			lastServed = s_served; lastPre = s_prefetched;
			Log("pakahead: ahead %lld of %zu ready (read %lld ms, unpack %lld ms, bad %lld), served %lld (%.0f MB), game "
			    "came first %lld, held %.0f MB, pinned %lld (not %lld); RAM: waited %lld x, freed %.0f MB in %lld goes (%lld dropped for good), %lld MB free",
			    s_prefetched.load(), g_items.size(), s_readMs.load(), s_unpackMs.load(), s_bad.load(), s_served.load(),
			    s_servedBytes.load() / 1e6, s_missNotReady.load(), g_held.load() / 1e6, s_locked.load(), s_lockFail.load(),
			    s_ramWaits.load(), s_shedBytes.load() / 1e6, s_shedTimes.load(), s_shedDropped.load(), ramgov::AvailMb());
			const double ct = (double)s_copyTicks.load() / g_freq, tt = (double)s_touchTicks.load() / g_freq;
			Log("pakahead: offered to Windows %.0f MB now (%.0f MB in all), reclaimed %lld, dropped by Windows %lld (%.0f MB, "
			    "unpacked again; %lld at the game's read)", s_offeredNow.load() / 1e6, s_offered.load() / 1e6, s_reclaimed.load(),
			    s_lost.load(), s_lostBytes.load() / 1e6, s_lostAtServe.load());
			Log("pakahead: copies into the game %.0f MB in %.2f s (%.2f GB/s); sampled pages not in RAM: ours %lld of %lld, "
			    "game's buffer %lld; toucher %.0f MB in %.2f s, %lld of %lld sampled pages were out, game waited on it %lld x",
			    s_copyBytes.load() / 1e6, ct, ct > 0 ? s_copyBytes.load() / 1e9 / ct : 0.0, s_srcOut.load(), s_resChecked.load(),
			    s_dstOut.load(), s_touchBytes.load() / 1e6, tt, s_touchOut.load(), s_touchChecked.load(), s_touchWaits.load());
			if (g_anyKeep)
				Log("pakahead: repeat reads served %lld (%.0f MB); kept for repeats now %.0f MB, at most %.0f MB; freed after %.0f s "
				    "with no read %lld (%.0f MB)", s_repServed.load(), s_repBytes.load() / 1e6, s_keptNow.load() / 1e6,
				    s_keptPeak.load() / 1e6, kKeptIdleMs / 1000.0, s_keptIdle.load(), s_keptIdleBytes.load() / 1e6);
		}
		if (!g_items.empty() && !g_stop && GetTickCount64() - g_start > kKeepMs) {
			FreeMem("the store closes");
			g_stop = true;
			Sleep(500);
			size_t freed = 0;
			Sleep(100);   // the toucher stops at g_stop; an entry it held is back at 2 by now
		for (Item& it : g_items) {
				int r2 = 2, r6 = 6;
				if (it.state.compare_exchange_strong(r2, 3) || it.state.compare_exchange_strong(r6, 3)) {
					if (r6 == 6) s_offeredNow -= it.usize;
					freed += it.usize;
					Release(it);
				}
			}
			Log("pakahead: unpack-ahead store closed, %.0f MB never asked for, freed", freed / 1e6);
		}
		if (GetTickCount64() - g_start > kRecordMs + 10000 && (g_items.empty() || g_stop)) {
			Flush();
			Log("pakahead: recording done, %zu reads from %zu paks", n, g_paks.size());
			return 0;
		}
	}
}

}  // namespace

void Pakahead_Install(uintptr_t base, void* self) {
	g_start = GetTickCount64();
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart; g_t0 = Now();
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	g_dir = path; g_dir = g_dir.substr(0, g_dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((g_dir + "fastload_pakahead.txt").c_str(), "r")) {
		int m = 1; if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 2) g_mode = m; fclose(fp);
	}
	if (FILE* fp = fopen((g_dir + "fastload_pakahead_autolist.txt").c_str(), "r")) {
		int a = 1; if (fscanf(fp, "%d", &a) == 1) g_autoList = a != 0; fclose(fp);
	}
	if (g_mode == 0) { Log("pakahead: off"); return; }
	if (HMODULE crt = GetModuleHandleA("ucrtbase.dll")) {
		g_fileno = (FilenoFn)GetProcAddress(crt, "_fileno");
		g_osHandle = (OsHandleFn)GetProcAddress(crt, "_get_osfhandle");
	}
	void* rd = (void*)(base + kRvaRead);
	if (memcmp(rd, kReadBytes, sizeof kReadBytes)) { Log("pakahead: code check failed - not installed (other game version?)"); return; }
	g_recs.reserve(150000);
	// the list first: the hook's TryServe reads g_items / g_byKey / g_listPaks with no lock, so they must be complete before
	// any loader thread can enter it (review 2026-10-24: the hook was on while LoadList still inserted and rehashed)
	const bool listed = g_mode == 2 && LoadList();
	const bool ok = MH_CreateHook(rd, (void*)&HkRead, (void**)&g_orig) == MH_OK && MH_EnableHook(rd) == MH_OK;
	Log("pakahead: mode %d (%s), ZipDir ReadFile hook %s, pak names %s", g_mode, g_mode == 2 ? "record + unpack ahead" : "record",
	    ok ? "on" : "FAILED", g_fileno && g_osHandle ? "via ucrtbase" : "UNAVAILABLE");
	if (ok && listed) StartAhead();
	if (ok) CloseHandle(CreateThread(nullptr, 0, Writer, nullptr, 0, nullptr));
}

void Pakahead_Command(const char* args) {
	(void)args;
	size_t n, p; { std::lock_guard<std::mutex> lk(g_mu); n = g_recs.size(); p = g_paks.size(); }
	Log("pakahead: mode %d, %zu reads recorded from %zu paks", g_mode, n, p);
}
