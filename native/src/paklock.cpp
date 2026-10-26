// CryPak reads outside the open-files lock: see paklock.h.
#include "paklock.h"
#include "levelprof.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

// The read pattern of the big stored entries (over 16 MB: the level's octree file, terrain.pak's, sound banks), the
// game's own piece reads: where each read starts against where the last one ended (backwards, 0 = straight on, forward
// gaps < 4 KB / < 64 KB / < 1 MB / more), the bytes skipped over by gaps under 1 MB (what a read-ahead would read too),
// and how many reads come from the level-load thread. Logged with the totals.
struct Pat { uintptr_t entry; uint32_t size; int64_t lastEnd, lo, hi; long long n, bytes, ticks, mainN, gapBytes, gap[6]; char name[96]; };
constexpr int kPats = 16;
Pat g_pat[kPats];
SRWLOCK g_patLock = SRWLOCK_INIT;
void PatNote(uintptr_t cached, uintptr_t entry, uint32_t size, int64_t pos, int64_t m, long long ticks) {
	AcquireSRWLockExclusive(&g_patLock);
	Pat* p = nullptr;
	for (Pat& x : g_pat) if (x.entry == entry) { p = &x; break; }
	if (!p) for (Pat& x : g_pat) if (!x.entry) {
		p = &x; *p = Pat{}; p->entry = entry; p->size = size; p->lastEnd = -1; p->lo = pos; p->hi = pos + m;
		const uintptr_t cache = *(uintptr_t*)(cached + 0x18);
		if (cache) snprintf(p->name, sizeof p->name, "%s", (const char*)(cache + 0x9a8 + *(int64_t*)(cache + 0x70)));
		break;
	}
	if (p) {
		if (p->lastEnd >= 0) {
			const int64_t g = pos - p->lastEnd;
			const int k = g < 0 ? 0 : g == 0 ? 1 : g < 4096 ? 2 : g < 65536 ? 3 : g < 1048576 ? 4 : 5;
			p->gap[k]++;
			if (g > 0 && g < 1048576) p->gapBytes += g;
		}
		p->lastEnd = pos + m;
		if (pos < p->lo) p->lo = pos;
		if (pos + m > p->hi) p->hi = pos + m;
		p->n++; p->bytes += m; p->ticks += ticks;
		if (GetCurrentThreadId() == Levelprof_MainTid()) p->mainN++;
	}
	ReleaseSRWLockExclusive(&g_patLock);
}

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

size_t StoredRead(uintptr_t cached, void* buf, int64_t pos, size_t n) {
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
	if (!W || b != 3 || m * 2 > W) {   // windows only on entries over 16 MB: on small ones each fill is one more cold read (boots 141, 143)
		const size_t r = g_origStored(cached, buf, pos, n);
		const long long dt = Now() - t0;
		g_sReads[b][0]++; g_sTicks[b][0] += dt; g_sBytes[b][0] += m;
		if (b == 3) PatNote(cached, entry, size, pos, m, dt);
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

// ---- File-exists lookups in the level load (a measurement): CCryPak::IsFileExist (FUN_180463c64; this, path, location)
// searches every mounted pak from the top (FindPakFileEntry FUN_180462f90: per pak a virtual flags call, the bind root
// compared, the path's folders lowercased with the CRT's tolower and binary-searched). Boot 138: 0.54 s of Main in
// LoadLevel, 0.36 s of it the CGF build's billboard test (FUN_180979bf4: "<mesh>_billbalb.dds" and "_billbnorm.dds" for
// every mesh). Counted on Main while LoadLevel runs: calls, ms and found / not found, billboard names apart; the pak
// count and the game's pak-priority setting at the first call.
constexpr uintptr_t kRvaIsFileExist = 0x463c64;
const unsigned char kIsFileExistBytes[] = { 0x48,0x89,0x5c,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x81,0xec,0x50,0x08,0x00,0x00 };
using IsFileExistFn = bool(__fastcall*)(uintptr_t pak, const char* path, int location);
IsFileExistFn g_origExist = nullptr;
long long g_exN[2][2], g_exT[2][2];   // [billboard name][found], Main in LoadLevel
int g_exPrio = -100;
long long g_exPaks = -1;
bool EndsWithI(const char* s, const char* suf) {
	const size_t n = strlen(s), m = strlen(suf);
	return n >= m && !_stricmp(s + n - m, suf);
}
int ReadPrio(uintptr_t pak) {
	__try { return *(int*)(*(uintptr_t*)(pak + 0x45 * 8) + 0x20); } __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
bool __fastcall HkIsFileExist(uintptr_t pak, const char* path, int location) {
	if (!Levelprof_InLoadLevel() || GetCurrentThreadId() != Levelprof_MainTid() || !path) return g_origExist(pak, path, location);
	if (g_exPrio == -100) { g_exPrio = ReadPrio(pak); g_exPaks = (long long)(*(uintptr_t*)(pak + 0x128) - *(uintptr_t*)(pak + 0x120)); }
	const long long t0 = Now();
	const bool r = g_origExist(pak, path, location);
	const int bb = EndsWithI(path, "_billbalb.dds") || EndsWithI(path, "_billbnorm.dds") ? 1 : 0;
	g_exN[bb][r ? 1 : 0]++;
	g_exT[bb][r ? 1 : 0] += Now() - t0;
	return r;
}

// ---- Level warm: see paklock.h. Main's stored-entry reads in LoadLevel by 1 MB piece of the pak file, recorded for the
// next load; at that load's start a thread reads the recorded pieces through the Windows file cache.
constexpr int kPieceShift = 20;
constexpr int64_t kWarmCap = 512ll << 20;
bool g_warmOn = true;
std::string g_dir, g_root;
char g_level[64] = {};
struct RecPiece { uint32_t order, reads; long long ticks; uint64_t pathHash; int64_t piece; };
std::unordered_map<uint64_t, RecPiece> g_rec;            // Main only, during LoadLevel
std::unordered_map<uint64_t, std::string> g_recPath;     // path hash -> the pak's path as CryPak names it
struct PathHash { uintptr_t cache; uint64_t h; };
PathHash g_ph[64];
int g_nph = 0;
struct Run { std::string path; int64_t off, len; };
std::vector<Run> g_runs;                                 // the list being warmed (built on Main before the thread starts)
std::unordered_map<uint64_t, int> g_warmIdx;             // (pak, piece) -> index into g_warmDone
std::unique_ptr<std::atomic<uint8_t>[]> g_warmDone;
std::atomic<int> g_warmState{0};                         // 0 none, 1 reading, 2 done
std::atomic<long long> g_warmBytes{0}, g_warmTicks{0}, g_warmStartT{0}, g_warmEndT{0};
std::atomic<int> g_warmFailed{0};
long long g_loadStartT = 0;
long long g_mN[3], g_mT[3];   // Main's stored reads in LoadLevel: [0] piece warmed before the read, [1] listed, not yet, [2] not listed

uint64_t PathHashOf(const char* p) {
	uint64_t h = 1469598103934665603ull;
	for (; *p; p++) {
		char c = *p;
		if (c == '/') c = '\\';
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		h = (h ^ (unsigned char)c) * 1099511628211ull;
	}
	return h;
}
inline uint64_t PieceKey(uint64_t h, int64_t piece) { return h ^ ((uint64_t)piece * 0x9E3779B97F4A7C15ull); }
const char* CachePath(uintptr_t cache) { return (const char*)(cache + 0x9a8 + *(int64_t*)(cache + 0x70)); }

void RecNote(uintptr_t cached, int64_t pos, size_t n, long long dt) {   // Main, in LoadLevel, after the game's read
	const uintptr_t entry = *(uintptr_t*)(cached + 0x28);
	const uintptr_t cache = *(uintptr_t*)(cached + 0x18);
	if (!entry || !cache || *(uint16_t*)(entry + 0x18) != 0) return;
	const uint32_t size = *(uint32_t*)(entry + 8), dataOff = ((const uint32_t*)entry)[3];
	if (dataOff == 0xffffffffu || pos < 0 || pos >= (int64_t)size) return;
	int64_t m = (int64_t)n;
	if ((int64_t)size < pos + m) m = (int64_t)size - pos;
	if (m <= 0) return;
	uint64_t h = 0;
	for (int i = 0; i < g_nph; i++) if (g_ph[i].cache == cache) { h = g_ph[i].h; break; }
	if (!h) {
		const char* path = CachePath(cache);
		h = PathHashOf(path);
		if (!g_recPath.count(h)) g_recPath[h] = path;
		if (g_nph < 64) g_ph[g_nph++] = { cache, h };
	}
	const int64_t a = (int64_t)dataOff + pos, p0 = a >> kPieceShift, p1 = (a + m - 1) >> kPieceShift;
	if (g_warmState.load(std::memory_order_relaxed)) {
		auto f = g_warmIdx.find(PieceKey(h, p0));
		const int k = f == g_warmIdx.end() ? 2 : g_warmDone[f->second].load(std::memory_order_acquire) ? 0 : 1;
		g_mN[k]++; g_mT[k] += dt;
	}
	for (int64_t p = p0; p <= p1; p++) {
		auto it = g_rec.try_emplace(PieceKey(h, p), RecPiece{ (uint32_t)g_rec.size(), 0, 0, h, p }).first;
		it->second.reads++;
		it->second.ticks += dt / (p1 - p0 + 1);
	}
}

size_t __fastcall HkStoredRead(uintptr_t cached, void* buf, int64_t pos, size_t n) {
	if (!g_warmOn || !Levelprof_InLoadLevel() || GetCurrentThreadId() != Levelprof_MainTid()) return StoredRead(cached, buf, pos, n);
	const long long t0 = Now();
	const size_t r = StoredRead(cached, buf, pos, n);
	RecNote(cached, pos, n, Now() - t0);
	return r;
}

std::wstring Wide(const std::string& s) {
	std::wstring w(s.size() + 1, L'\0');
	const int k = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], (int)w.size());
	w.resize(k > 0 ? k - 1 : 0);
	return w;
}

DWORD WINAPI WarmThread(void*) {
	g_warmStartT = Now();
	char* buf = (char*)VirtualAlloc(nullptr, 1u << kPieceShift, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	HANDLE h = INVALID_HANDLE_VALUE;
	std::string cur;
	int idx = 0;
	for (const Run& r : g_runs) {
		if (r.path != cur) {
			if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
			cur = r.path;
			const std::string full = (cur.size() > 1 && cur[1] == ':') ? cur : g_root + cur;
			h = CreateFileW(Wide(full).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		}
		for (int64_t o = r.off; o < r.off + r.len; o += 1ll << kPieceShift, idx++) {
			DWORD got = 0;
			bool ok = false;
			if (buf && h != INVALID_HANDLE_VALUE) {
				OVERLAPPED ov{};
				ov.Offset = (DWORD)(o & 0xffffffff);
				ov.OffsetHigh = (DWORD)((uint64_t)o >> 32);
				ok = ReadFile(h, buf, 1u << kPieceShift, &got, &ov) != 0;
			}
			if (ok) g_warmBytes += got; else g_warmFailed++;
			g_warmDone[idx].store(1, std::memory_order_release);
		}
	}
	if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
	if (buf) VirtualFree(buf, 0, MEM_RELEASE);
	g_warmEndT = Now();
	Log("paklock: level warm: %.0f MB read into the file cache in %.0f ms (%d pieces failed), started %.0f ms after LoadLevel began",
	    g_warmBytes.load() / 1048576.0, Ms(g_warmEndT.load() - g_warmStartT.load()), g_warmFailed.load(),
	    Ms(g_warmStartT.load() - g_loadStartT));
	g_warmState = 2;
	return 0;
}

std::string ListPath() { return g_dir + "fastload_levelwarm_" + g_level + ".txt"; }

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

void Paklock_LevelStart(const char* level) {
	if (!g_warmOn) return;
	g_loadStartT = Now();
	g_rec.clear(); g_recPath.clear(); g_nph = 0;
	for (int k = 0; k < 3; k++) g_mN[k] = 0, g_mT[k] = 0;
	size_t j = 0;
	for (const char* p = level; p && *p && j + 1 < sizeof g_level; p++) {   // the level's name as a file-name part
		const char c = *p;
		if (c == '/' || c == '\\') { j = 0; continue; }
		g_level[j++] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ? c : '_';
	}
	g_level[j] = 0;
	if (!j) { g_warmOn = false; Log("paklock: level warm off: LoadLevel's level name is empty"); return; }
	if (g_warmState.load() == 1) { Log("paklock: level warm: the last load's warm still reads, none this time"); return; }
	g_runs.clear(); g_warmIdx.clear(); g_warmState = 0; g_warmBytes = 0; g_warmFailed = 0;
	FILE* f = fopen(ListPath().c_str(), "r");
	if (!f) { Log("paklock: level warm (%s): no list yet; this load writes one", g_level); return; }
	char line[600];
	int64_t total = 0;
	int idx = 0;
	while (fgets(line, sizeof line, f)) {
		if (line[0] == '#') continue;
		char* a = strchr(line, '|');
		char* b = a ? strchr(a + 1, '|') : nullptr;
		if (!b) continue;
		*a = 0;
		const long long off = _strtoi64(a + 1, nullptr, 10), len = _strtoi64(b + 1, nullptr, 10);
		if (off < 0 || len <= 0 || (off & ((1ll << kPieceShift) - 1)) || total + len > kWarmCap) continue;
		total += len;
		g_runs.push_back({ line, off, len });
		const uint64_t h = PathHashOf(line);
		for (int64_t o = off; o < off + len; o += 1ll << kPieceShift) g_warmIdx[PieceKey(h, o >> kPieceShift)] = idx++;
	}
	fclose(f);
	if (!idx) { Log("paklock: level warm (%s): the list is empty", g_level); return; }
	g_warmDone.reset(new std::atomic<uint8_t>[idx]);
	for (int i = 0; i < idx; i++) g_warmDone[i].store(0, std::memory_order_relaxed);
	g_warmState = 1;
	if (HANDLE th = CreateThread(nullptr, 0, &WarmThread, nullptr, 0, nullptr)) {
		SetThreadDescription(th, L"fastload levelwarm");
		CloseHandle(th);
		Log("paklock: level warm (%s): reading %zu runs, %.0f MB, through the file cache", g_level, g_runs.size(), total / 1048576.0);
	} else {
		g_warmState = 0;
		Log("paklock: level warm (%s): thread not started", g_level);
	}
}

void Paklock_LevelEnd() {
	if (g_origExist) {
		Log("paklock: LoadLevel: Main's IsFileExist: billboard names found %lld (%.0f ms), not found %lld (%.0f ms); other names found "
		    "%lld (%.0f ms), not found %lld (%.0f ms); pak priority setting %d, pak list %lld bytes", g_exN[1][1], Ms(g_exT[1][1]),
		    g_exN[1][0], Ms(g_exT[1][0]), g_exN[0][1], Ms(g_exT[0][1]), g_exN[0][0], Ms(g_exT[0][0]), g_exPrio, g_exPaks);
		for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) g_exN[a][b] = 0, g_exT[a][b] = 0;
	}
	if (!g_warmOn || !g_level[0]) return;
	Log("paklock: level warm (%s): Main's stored-entry reads in LoadLevel: piece read ahead first %lld (%.0f ms), listed but not read "
	    "yet %lld (%.0f ms), not listed %lld (%.0f ms); the warm %s", g_level, g_mN[0], Ms(g_mT[0]), g_mN[1], Ms(g_mT[1]), g_mN[2], Ms(g_mT[2]),
	    g_warmState.load() == 2 ? "had ended" : g_warmState.load() == 1 ? "was still reading" : "did not run");
	// The list for the next load: pieces in Main's first-read order that cost Main 0.2 ms or more, or 8 reads; neighbours merged.
	std::vector<const RecPiece*> v;
	for (auto& kv : g_rec) v.push_back(&kv.second);
	std::sort(v.begin(), v.end(), [](const RecPiece* a, const RecPiece* b) { return a->order < b->order; });
	const long long minTicks = g_freq / 5000;
	std::vector<Run> out;
	std::vector<uint64_t> outHash;
	int64_t total = 0;
	long long allTicks = 0, keptTicks = 0;
	for (const RecPiece* p : v) {
		allTicks += p->ticks;
		if (p->ticks < minTicks && p->reads < 8) continue;
		if (total + (1ll << kPieceShift) > kWarmCap) break;
		total += 1ll << kPieceShift;
		keptTicks += p->ticks;
		const int64_t off = p->piece << kPieceShift;
		if (!out.empty() && outHash.back() == p->pathHash && out.back().off + out.back().len == off) out.back().len += 1ll << kPieceShift;
		else { out.push_back({ g_recPath[p->pathHash], off, 1ll << kPieceShift }); outHash.push_back(p->pathHash); }
	}
	if (out.empty()) { Log("paklock: level warm (%s): no stored-entry pieces worth a list (%zu read)", g_level, v.size()); return; }
	const std::string tmp = ListPath() + ".tmp";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f) { Log("paklock: level warm (%s): list not written", g_level); return; }
	fprintf(f, "# fastload level warm list for %s: 1 MB pieces of stored pak entries that LoadLevel's thread read, first-read order\n", g_level);
	fprintf(f, "# pak path (as CryPak names it, under the game folder)|file offset|length\n");
	for (const Run& r : out) fprintf(f, "%s|%lld|%lld\n", r.path.c_str(), (long long)r.off, (long long)r.len);
	const bool ok = fclose(f) == 0;
	if (ok) MoveFileExA(tmp.c_str(), ListPath().c_str(), MOVEFILE_REPLACE_EXISTING);
	Log("paklock: level warm (%s): list for the next load: %zu runs, %lld MB (%zu pieces read, %.0f of %.0f ms of Main's reads in the kept ones)%s",
	    g_level, out.size(), (long long)(total >> 20), v.size(), Ms(keptTicks), Ms(allTicks), ok ? "" : " - NOT WRITTEN");
}

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
	AcquireSRWLockExclusive(&g_patLock);
	for (Pat& p : g_pat) {
		if (p.entry && p.n >= 1000)
			Log("paklock: %s: %u MB entry of %s: %lld reads (%lld from the level-load thread), %.0f MB, %.0f ms; span %.0f-%.0f MB; "
			    "next read backwards %lld, straight on %lld, after a gap <4K %lld, <64K %lld, <1M %lld, more %lld; gaps under 1 MB skip %.0f MB",
			    when, p.size >> 20, p.name, p.n, p.mainN, p.bytes / 1e6, Ms(p.ticks), p.lo / 1e6, p.hi / 1e6, p.gap[0], p.gap[1], p.gap[2],
			    p.gap[3], p.gap[4], p.gap[5], p.gapBytes / 1e6);
		p = Pat{};
	}
	ReleaseSRWLockExclusive(&g_patLock);
}

void Paklock_Install(uintptr_t base, void* self) {
	LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); g_freq = fq.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	g_dir = dir;
	{   // the game folder: the plugin sits in <game>\Bin\Win64MasterMasterSteamPGO\ (CryPak names paks under it)
		std::string r = dir.substr(0, dir.size() - 1);
		r = r.substr(0, r.find_last_of('\\'));
		g_root = r.substr(0, r.find_last_of('\\') + 1);
	}
	if (FILE* fp = fopen((dir + "fastload_paklock.txt").c_str(), "r")) {
		int m = 1, win = 0, warm = 1;
		const int got = fscanf(fp, "%d %d %d", &m, &win, &warm);
		if (got >= 1) g_mode = m == 0 ? 0 : 1;
		if (got >= 2 && win > 0) g_winKb = win < 16 ? 16 : win > 1024 ? 1024 : (unsigned)win;
		if (got >= 3) g_warmOn = warm != 0;
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
	if (!sr) g_winKb = 0, g_warmOn = false;
	Hook(base, kRvaIsFileExist, kIsFileExistBytes, sizeof kIsFileExistBytes, (void*)&HkIsFileExist, (void**)&g_origExist, "IsFileExist (counted in LoadLevel)");
	const bool md = Hook(base, kRvaLockModify, kLockModifyBytes, sizeof kLockModifyBytes, (void*)&HkLockModify, (void**)&g_origModify, "LockModify");
	if (!rd) g_mode = 0;
	Log("paklock: mode %d (%s; fastload_paklock.txt), FReadRaw hook %s, modify-wait timing %s; stored entries over 16 MB read ahead in %u KB "
	    "windows%s; level warm %s (game folder %s)", g_mode, g_mode == 1 ? "pak reads outside the open-files lock" : "the game's own reads",
	    rd ? "on" : "OFF", md ? "on" : "OFF", g_winKb, g_winKb ? "" : " (off: the game's piece-by-piece reads, counted)",
	    g_warmOn ? "on" : "off", g_root.c_str());
}
