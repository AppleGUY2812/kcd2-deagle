// CryPak reads outside the open-files lock: see paklock.h.
#include "paklock.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// ---- WHGame.dll 1.5.6 (decompile 2026-10-24; bytes read from the installed DLL)
constexpr uintptr_t kRvaFReadRaw = 0x4610a4;     // CCryPak::FReadRaw(pak, buf, size, count, handle)
const unsigned char kFReadRawBytes[] = { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56 };
constexpr uintptr_t kRvaLockModify = 0xc35288;   // CryReadModifyLock::LockModify(lock): mutex, modifyCount++, wait for readCount 0
const unsigned char kLockModifyBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20 };
constexpr uintptr_t kRvaLockRead = 0x23c9b0c;    // AutoReadLock ctor(tok, lock): mutex, readCount++, mutex released
const unsigned char kLockReadBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x48,0x89,0x11 };
constexpr uintptr_t kRvaUnlockRead = 0x506d34;   // UnlockRead(lock): readCount--, wakes a waiting modifier
const unsigned char kUnlockReadBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57 };
constexpr uintptr_t kRvaZipFRead = 0x461654;     // CZipPseudoFile::FRead(slot, buf, size, count): reads at slot.pos, advances it
constexpr uintptr_t kRvaOsRead = 0x4d7854;       // the non-pak path: (buf, size, count, handle)
constexpr uintptr_t kRvaStatBegin = 0x461170;    // the pak's read-time statistic (guard, pak) / (guard)
const unsigned char kStatBeginBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x89,0x11,0x48,0x8b,0xd9 };
constexpr uintptr_t kRvaStatEnd = 0x46119c;
const unsigned char kStatEndBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x48,0x8b,0xd9 };
constexpr uintptr_t kOpenLock = 0x10, kSlotsBegin = 0x40, kSlotsEnd = 0x48;   // in CCryPak
constexpr uintptr_t kRvaStoredRead = 0x461704;   // CCachedFileData read(cached, buf, pos, n): stored entries straight from disk
const unsigned char kStoredReadBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x40 };
constexpr uintptr_t kRvaZipRead = 0x4617fc;      // ZipDir::Cache::ReadFile(cache, entry, 0, buf, 0, pos, n, 1): seek + fread under the pak's mutex
const unsigned char kZipReadBytes[] = { 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xac };
constexpr uintptr_t kRvaDtor = 0x463634;         // ~CCachedFileData (body; the deleting destructor 0x46073c calls it)
const unsigned char kDtorBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8d,0x05 };
constexpr uintptr_t kRvaMutexLock = 0x3d902c, kRvaMutexUnlock = 0x42dc54;   // the cached data's own read lock at +0x38
// CCachedFileData: +0x10 the whole entry if GetData ran (compressed / text reads), +0x18 the pak's ZipDir cache (its path
// at cache + 0x9a8 + *(cache+0x70)), +0x28 the FileEntry: +8 uncompressed size, +0x18 method (0 = stored).

struct Slot { uint32_t pos, pad; void* cached; uint32_t flags, pad2; };   // CZipPseudoFile in m_arrOpenFiles
static_assert(sizeof(Slot) == 0x18, "slot stride");

using FReadRawFn = size_t(__fastcall*)(uintptr_t pak, void* buf, size_t size, size_t count, intptr_t handle);
using LockModifyFn = uint64_t(__fastcall*)(void* lock);
using LockReadFn = void*(__fastcall*)(void** tok, void* lock);
using UnlockReadFn = void(__fastcall*)(void* lock);
using ZipFReadFn = size_t(__fastcall*)(Slot* slot, void* buf, size_t size, size_t count);
using OsReadFn = size_t(__fastcall*)(void* buf, size_t size, size_t count, intptr_t handle);
using StatBeginFn = void*(__fastcall*)(void* guard, uintptr_t pak);
using StatEndFn = void(__fastcall*)(void* guard);
using RefFn = void(__fastcall*)(void* obj);
using StoredReadFn = size_t(__fastcall*)(uintptr_t cached, void* buf, int64_t pos, size_t n);
using ZipReadFn = int(__fastcall*)(uintptr_t cache, uintptr_t entry, void* cbuf, void* buf, char unpack, int64_t pos, int64_t n, char check);
using ObjFn = void(__fastcall*)(uintptr_t obj);

FReadRawFn g_origRead = nullptr;
LockModifyFn g_origModify = nullptr;
LockReadFn g_lockRead = nullptr;
UnlockReadFn g_unlockRead = nullptr;
ZipFReadFn g_zipRead = nullptr;
OsReadFn g_osRead = nullptr;
StatBeginFn g_statBegin = nullptr;
StatEndFn g_statEnd = nullptr;
StoredReadFn g_origStored = nullptr;
ZipReadFn g_zipReadFile = nullptr;
ObjFn g_origDtor = nullptr, g_mutexLock = nullptr, g_mutexUnlock = nullptr;

int g_mode = 1;
unsigned g_winKb = 0;   // read-ahead window per stored entry read in pieces (0 = off: the game's reads, counted)
// stored-entry reads by entry size: [bucket][0 disk (the game's read), 1 from a window, 2 window fills]
constexpr int kBuckets = 4;   // <= 64 KB, <= 1 MB, <= 16 MB, bigger
std::atomic<long long> g_sReads[kBuckets][3], g_sTicks[kBuckets][3], g_sBytes[kBuckets][3];
std::atomic<int> g_bigLines{0};

// The windows: at most kWins (64 x 256 KB = 16 MB), least recently used goes first. A slot remembers where the last read
// of its file ended; the next read that continues there (or skips ahead less than a window) fills a window from its
// position, every read inside it is then a copy. A file read once or at random keeps the game's reads.
struct Win { uintptr_t cached, entry, cache; int64_t start, lastEnd; uint32_t len; char* buf; std::atomic<uint64_t> used; };
constexpr int kWins = 64;
Win g_win[kWins];
SRWLOCK g_winLock = SRWLOCK_INIT;
char* g_free[kWins];
int g_nFree = 0, g_nBufs = 0;
std::atomic<uint64_t> g_clock{0};
long long g_freq = 1;
std::atomic<uintptr_t> g_openLock{0};   // the pak's m_csOpenFiles, from the first FReadRaw
std::atomic<long long> g_outside{0}, g_outsideTicks{0}, g_inside{0}, g_writeBackMissed{0};
std::atomic<long long> g_waits{0}, g_waitTicks{0}, g_waitMax{0};

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long t) { return (double)t * 1000.0 / g_freq; }
inline void AddRef(void* o) { ((RefFn)(*(void***)o)[1])(o); }    // CCachedFileData vtable +8: atomic ++refs
inline void Release(void* o) { ((RefFn)(*(void***)o)[2])(o); }   // +0x10: --refs, frees under its owner's lock at 0

size_t __fastcall HkFReadRaw(uintptr_t pak, void* buf, size_t size, size_t count, intptr_t handle) {
	if (g_mode != 1) { g_openLock = pak + kOpenLock; return g_origRead(pak, buf, size, count, handle); }
	alignas(8) unsigned char guard[16];
	g_statBegin(guard, pak);
	void* lock = (void*)(pak + kOpenLock);
	g_openLock = (uintptr_t)lock;
	void* tok = nullptr;
	g_lockRead(&tok, lock);
	const size_t i = (size_t)handle - 1;
	size_t r;
	uintptr_t b = *(uintptr_t*)(pak + kSlotsBegin), e = *(uintptr_t*)(pak + kSlotsEnd);
	if (i < (e - b) / sizeof(Slot)) {
		Slot s = *(Slot*)(b + i * sizeof(Slot));
		if (!s.cached) {   // FRead returns 0 at once: nothing to take out of the lock
			r = g_zipRead((Slot*)(b + i * sizeof(Slot)), buf, size, count);
			g_unlockRead(lock);
			g_inside++;
		} else {
			AddRef(s.cached);
			g_unlockRead(lock);
			const long long t = Now();
			r = g_zipRead(&s, buf, size, count);
			g_outsideTicks += Now() - t;
			g_outside++;
			g_lockRead(&tok, lock);
			b = *(uintptr_t*)(pak + kSlotsBegin); e = *(uintptr_t*)(pak + kSlotsEnd);
			Slot* now = i < (e - b) / sizeof(Slot) ? (Slot*)(b + i * sizeof(Slot)) : nullptr;
			if (now && now->cached == s.cached) now->pos = s.pos;
			else g_writeBackMissed++;   // closed (and maybe reopened) while we read: the game's lock would have made FClose wait
			g_unlockRead(lock);
			Release(s.cached);
		}
	} else {
		g_unlockRead(lock);
		r = g_osRead(buf, size, count, handle);
	}
	g_statEnd(guard);
	return r;
}

int Bucket(uint32_t size) { return size <= 65536 ? 0 : size <= 1048576 ? 1 : size <= 16777216 ? 2 : 3; }

// A stored entry read in pieces (the octree's object file: two small reads per node, 153,000 reads of ~1.4 KB in level
// loading) costs a seek + a CRT fread under the pak's mutex each time (~6 us). Read ahead into a window instead.
Win* FindLocked(uintptr_t cached) { for (Win& w : g_win) if (w.cached == cached) return &w; return nullptr; }
Win* SlotLocked(uintptr_t cached, uintptr_t entry, uintptr_t cache) {   // exclusive lock held: the file's slot, new if none
	Win* w = FindLocked(cached);
	if (w && (w->entry != entry || w->cache != cache)) w->cached = 0, w = nullptr;   // freed without the destructor seen
	if (w) return w;
	Win* old = &g_win[0];
	for (Win& x : g_win) { if (!x.cached) { old = &x; break; } if (x.used < old->used) old = &x; }
	if (old->buf) g_free[g_nFree++] = old->buf, old->buf = nullptr;
	old->cached = cached; old->entry = entry; old->cache = cache; old->start = 0; old->len = 0; old->lastEnd = -1;
	return old;
}

size_t __fastcall HkStoredRead(uintptr_t cached, void* buf, int64_t pos, size_t n) {
	const uintptr_t entry = *(uintptr_t*)(cached + 0x28);
	if (!entry || *(uint16_t*)(entry + 0x18) != 0) return g_origStored(cached, buf, pos, n);   // compressed: unpacked once by the game
	const uint32_t size = *(uint32_t*)(entry + 8);
	int64_t m = (int64_t)n;
	if ((int64_t)size < pos + m) m = (int64_t)size - pos;
	if (m <= 0 || pos < 0) return g_origStored(cached, buf, pos, n);
	const int b = Bucket(size);
	const long long t0 = Now();
	const int64_t W = (int64_t)g_winKb * 1024;
	const uintptr_t whole = W ? *(volatile uintptr_t*)(cached + 0x10) : 0;
	if (whole) {   // the game already holds the whole entry (a text-mode read ran GetData)
		memcpy(buf, (const void*)(whole + pos), (size_t)m);
		g_sReads[b][1]++; g_sTicks[b][1] += Now() - t0; g_sBytes[b][1] += m;
		return (size_t)m;
	}
	if (!W || m * 2 > W) {
		const size_t r = g_origStored(cached, buf, pos, n);
		g_sReads[b][0]++; g_sTicks[b][0] += Now() - t0; g_sBytes[b][0] += m;
		return r;
	}
	AcquireSRWLockShared(&g_winLock);
	Win* w = FindLocked(cached);
	if (w && w->buf && w->entry == entry && pos >= w->start && pos + m <= w->start + w->len) {
		memcpy(buf, w->buf + (pos - w->start), (size_t)m);
		w->lastEnd = pos + m;   // only this file's reader writes it (the game reads one file object from one thread at a time)
		w->used = ++g_clock;
		ReleaseSRWLockShared(&g_winLock);
		g_sReads[b][1]++; g_sTicks[b][1] += Now() - t0; g_sBytes[b][1] += m;
		return (size_t)m;
	}
	const bool seq = w && w->entry == entry && w->lastEnd >= 0 && pos >= w->lastEnd && pos < w->lastEnd + W;
	ReleaseSRWLockShared(&g_winLock);
	const uintptr_t cache = *(uintptr_t*)(cached + 0x18);
	if (!seq) {   // first read of this file, or a jump: the game's read, then remember where it ended
		const size_t r = g_origStored(cached, buf, pos, n);
		AcquireSRWLockExclusive(&g_winLock);
		Win* s = SlotLocked(cached, entry, cache);
		s->lastEnd = pos + m; s->used = ++g_clock;
		ReleaseSRWLockExclusive(&g_winLock);
		g_sReads[b][0]++; g_sTicks[b][0] += Now() - t0; g_sBytes[b][0] += m;
		return r;
	}
	// continues where the last read ended: fill a window from here (the game's own read call, under the same locks)
	const int64_t len = (int64_t)size - pos < W ? (int64_t)size - pos : W;
	AcquireSRWLockExclusive(&g_winLock);
	char* nb = g_nFree ? g_free[--g_nFree] : nullptr;
	if (!nb && g_nBufs < kWins) { nb = (char*)VirtualAlloc(nullptr, (size_t)g_winKb * 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); if (nb) g_nBufs++; }
	ReleaseSRWLockExclusive(&g_winLock);
	if (!nb) return g_origStored(cached, buf, pos, n);
	g_mutexLock(cached + 0x38);
	const int e = g_zipReadFile(cache, entry, nullptr, nb, 0, pos, len, 1);
	g_mutexUnlock(cached + 0x38);
	AcquireSRWLockExclusive(&g_winLock);
	if (e != 0) {   // the game's read failed: give the buffer back, let the game's own read report it
		g_free[g_nFree++] = nb;
		ReleaseSRWLockExclusive(&g_winLock);
		return g_origStored(cached, buf, pos, n);
	}
	Win* s = SlotLocked(cached, entry, cache);
	if (s->buf) g_free[g_nFree++] = s->buf;
	s->buf = nb; s->start = pos; s->len = (uint32_t)len; s->lastEnd = pos + m; s->used = ++g_clock;
	memcpy(buf, nb, (size_t)m);
	ReleaseSRWLockExclusive(&g_winLock);
	g_sReads[b][2]++; g_sTicks[b][2] += Now() - t0; g_sBytes[b][2] += len;
	if (size > 16777216 && g_bigLines++ < 8 && cache)
		Log("paklock: window on a %u MB stored entry of %s", size >> 20, (const char*)(cache + 0x9a8 + *(int64_t*)(cache + 0x70)));
	return (size_t)m;
}

void __fastcall HkDtor(uintptr_t obj) {   // its window goes with it (a new object at the same address must not see it)
	AcquireSRWLockExclusive(&g_winLock);
	if (Win* w = FindLocked(obj)) { if (w->buf) g_free[g_nFree++] = w->buf; w->buf = nullptr; w->cached = 0; w->len = 0; }
	ReleaseSRWLockExclusive(&g_winLock);
	g_origDtor(obj);
}

uint64_t __fastcall HkLockModify(void* lock) {
	if ((uintptr_t)lock != g_openLock.load(std::memory_order_relaxed)) return g_origModify(lock);
	const long long t = Now();
	const uint64_t r = g_origModify(lock);
	const long long dt = Now() - t;
	g_waits++;
	g_waitTicks += dt;
	long long m = g_waitMax.load();
	while (dt > m && !g_waitMax.compare_exchange_weak(m, dt)) {}
	return r;
}

bool Hook(uintptr_t base, uintptr_t rva, const unsigned char* bytes, size_t n, void* detour, void** orig, const char* what) {
	void* p = (void*)(base + rva);
	if (memcmp(p, bytes, n)) { Log("paklock: %s: code differs - not hooked", what); return false; }
	if (MH_CreateHook(p, detour, orig) != MH_OK || MH_EnableHook(p) != MH_OK) { Log("paklock: %s: hook FAILED", what); return false; }
	return true;
}

}  // namespace

void Paklock_Report(const char* when) {
	Log("paklock: %s: reads outside the lock %lld (%.0f ms of reading), inside %lld, position not written back %lld; "
	    "open-files modify waits %lld, %.0f ms, longest %.1f ms (all threads)", when, g_outside.load(), Ms(g_outsideTicks.load()),
	    g_inside.load(), g_writeBackMissed.load(), g_waits.load(), Ms(g_waitTicks.load()), Ms(g_waitMax.load()));
	g_outside = 0; g_outsideTicks = 0; g_inside = 0; g_writeBackMissed = 0; g_waits = 0; g_waitTicks = 0; g_waitMax = 0;
	static const char* const kName[kBuckets] = { "<=64K", "<=1M", "<=16M", ">16M" };
	std::string s;
	for (int b = 0; b < kBuckets; b++) {
		if (!g_sReads[b][0] && !g_sReads[b][1]) continue;
		char x[200];
		snprintf(x, sizeof x, " %s: game's reads %lld (%.0f ms, %lld KB), from memory %lld (%.0f ms), window fills %lld (%.0f ms, %lld KB);",
		         kName[b], g_sReads[b][0].load(), Ms(g_sTicks[b][0].load()), g_sBytes[b][0].load() / 1024, g_sReads[b][1].load(),
		         Ms(g_sTicks[b][1].load()), g_sReads[b][2].load(), Ms(g_sTicks[b][2].load()), g_sBytes[b][2].load() / 1024);
		s += x;
		for (int k = 0; k < 3; k++) { g_sReads[b][k] = 0; g_sTicks[b][k] = 0; g_sBytes[b][k] = 0; }
	}
	Log("paklock: %s: stored entries read in pieces (window %u KB, %d buffers):%s", when, g_winKb, g_nBufs, s.empty() ? " none" : s.c_str());
}

void Paklock_Install(uintptr_t base, void* self) {
	LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); g_freq = fq.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((dir + "fastload_paklock.txt").c_str(), "r")) {
		int m = 1, win = 0;
		const int got = fscanf(fp, "%d %d", &m, &win);
		if (got >= 1) g_mode = m == 0 ? 0 : 1;
		if (got >= 2 && win > 0) g_winKb = win < 16 ? 16 : win > 1024 ? 1024 : (unsigned)win;
		fclose(fp);
	}
	// the functions FReadRaw calls must be the ones read in the decompile, or the replacement is off
	const bool calls = !memcmp((void*)(base + kRvaLockRead), kLockReadBytes, sizeof kLockReadBytes) &&
	                   !memcmp((void*)(base + kRvaUnlockRead), kUnlockReadBytes, sizeof kUnlockReadBytes) &&
	                   !memcmp((void*)(base + kRvaStatBegin), kStatBeginBytes, sizeof kStatBeginBytes) &&
	                   !memcmp((void*)(base + kRvaStatEnd), kStatEndBytes, sizeof kStatEndBytes);
	if (!calls && g_mode == 1) { g_mode = 0; Log("paklock: the lock / statistic functions differ: replacement off"); }
	g_lockRead = (LockReadFn)(base + kRvaLockRead);
	g_unlockRead = (UnlockReadFn)(base + kRvaUnlockRead);
	g_zipRead = (ZipFReadFn)(base + kRvaZipFRead);
	g_osRead = (OsReadFn)(base + kRvaOsRead);
	g_statBegin = (StatBeginFn)(base + kRvaStatBegin);
	g_statEnd = (StatEndFn)(base + kRvaStatEnd);
	const bool rd = Hook(base, kRvaFReadRaw, kFReadRawBytes, sizeof kFReadRawBytes, (void*)&HkFReadRaw, (void**)&g_origRead, "FReadRaw");
	g_zipReadFile = (ZipReadFn)(base + kRvaZipRead);
	g_mutexLock = (ObjFn)(base + kRvaMutexLock);
	g_mutexUnlock = (ObjFn)(base + kRvaMutexUnlock);
	// pakahead hooks ReadFile too: compare its bytes before any hook is placed (Paklock_Install runs first)
	if (memcmp((void*)(base + kRvaZipRead), kZipReadBytes, sizeof kZipReadBytes)) { g_winKb = 0; Log("paklock: ZipDir ReadFile differs: windows off"); }
	const bool dt = g_winKb && Hook(base, kRvaDtor, kDtorBytes, sizeof kDtorBytes, (void*)&HkDtor, (void**)&g_origDtor, "CCachedFileData destructor");
	if (!dt) g_winKb = 0;
	const bool sr = Hook(base, kRvaStoredRead, kStoredReadBytes, sizeof kStoredReadBytes, (void*)&HkStoredRead, (void**)&g_origStored, "stored read");
	if (!sr) g_winKb = 0;
	const bool md = Hook(base, kRvaLockModify, kLockModifyBytes, sizeof kLockModifyBytes, (void*)&HkLockModify, (void**)&g_origModify, "LockModify");
	if (!rd) g_mode = 0;
	Log("paklock: mode %d (%s; fastload_paklock.txt), FReadRaw hook %s, modify-wait timing %s; stored entries read ahead in %u KB windows%s",
	    g_mode, g_mode == 1 ? "pak reads outside the open-files lock" : "the game's own reads", rd ? "on" : "OFF", md ? "on" : "OFF",
	    g_winKb, g_winKb ? "" : " (off: the game's piece-by-piece reads, counted)");
}
