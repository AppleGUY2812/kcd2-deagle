// Bigger streaming reads: see streamio.h. Addresses: WHGame.dll 1.5.6, checked against the installed DLL at start.
#include "streamio.h"
#include "log.h"
#include "streamahead.h"
#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "MinHook.h"

namespace {

// FUN_180464928(cache, fileEntry, out, dataOffset, readSize) -> 0 ok, 1 IO error, 0x10 no entry.
//   fileEntry: +4 compressed size (the size when readSize is 0), +8 uncompressed size, +0xc data offset in the pak.
//   cache: +8 the zip file (+0x38 != 0: held in memory), +0x10 the unbuffered HANDLE, +0x18 sector size,
//          +0x20 the 128 KB read buffer, +0x998 -> cache data, whose +8 is the IO lock (a recursive SRW mutex).
//   The unbuffered branch runs when the zip is not in memory, sys_UncachedStreamReads != 0 and the handle is valid;
//   otherwise it calls the buffered ZipDir ReadFile FUN_1804617fc (pakahead.cpp hooks that one).
constexpr uintptr_t kRvaRead = 0x464928, kRvaLock = 0x3d902c, kRvaUnlock = 0x42dc54, kRvaUncached = 0x49292c8;
const unsigned char kReadBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x4c,0x89,0x44,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,
                                     0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec };
const unsigned char kLockBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xff,0x15 };
const unsigned char kUnlockBytes[] = { 0x8b,0x41,0x08,0x85,0xc0,0x74,0x06,0xff,0xc8,0x89,0x41,0x08,0xc3 };

using ReadFn = uint32_t(__fastcall*)(uint8_t* cache, uint8_t* entry, uint8_t* out, int64_t offset, int64_t size);
using LockFn = void(__fastcall*)(void* mutex);
ReadFn g_orig = nullptr;
LockFn g_lock = nullptr, g_unlock = nullptr;
const int* g_uncached = nullptr;          // sys_UncachedStreamReads

std::atomic<int> g_mode{0};   // 0 until mode 1 is proven in game (self-check + a timed load)
std::atomic<long long> g_selfCheck{300};  // mode 1: requests still to verify before trusting the one-read path
std::string g_modeFile;
long long g_freq = 1;

// counters since start; ticks are QueryPerformanceCounter units
std::atomic<long long> n_calls{0}, n_bytes{0}, n_ticks{0}, n_lockTicks{0}, n_readTicks{0}, n_buffered{0};
std::atomic<long long> n_one{0}, n_fallback{0}, n_verified{0}, n_bad{0};
std::atomic<long long> n_size[6];         // <=32K, <=128K, <=512K, <=1M, <=4M, more
const char* const kSizeName[6] = { "32K", "128K", "512K", "1M", "4M", "more" };

// Repeats: the same exact piece (pak, data offset, offset, size) read again, by the time since its last read. Boots
// 34-37 (2026-10-24): ~4,000 of the Settle wait's requests were such repeats; under 1 s apart means requests cancelled
// and asked again, seconds apart means textures dropped from the pool and streamed again.
std::mutex g_repMu;
std::unordered_map<uint64_t, ULONGLONG> g_lastRead;   // piece -> GetTickCount64 of its last read
constexpr size_t kRepMax = 400000;                    // ~25 MB at most; new pieces past that aren't tracked
const ULONGLONG kRepEdge[3] = { 100, 1000, 5000 };
const char* const kRepName[4] = { "<0.1 s", "<1 s", "<5 s", "later" };
std::atomic<long long> n_rep[4], n_repBytes{0};

void NoteRepeat(uint8_t* cache, uint8_t* entry, int64_t offset, int64_t want) {
	uint64_t k = (uint64_t)(uintptr_t)cache * 0x9e3779b97f4a7c15ull;
	k ^= ((uint64_t)*(uint32_t*)(entry + 0xc) << 1) + 0x632be59bd9b4e019ull + (k << 6) + (k >> 2);
	k ^= (uint64_t)offset * 0xff51afd7ed558ccdull + (k << 6) + (k >> 2);
	k ^= (uint64_t)want * 0xc4ceb9fe1a85ec53ull + (k << 6) + (k >> 2);
	const ULONGLONG now = GetTickCount64();
	std::lock_guard<std::mutex> g(g_repMu);
	auto f = g_lastRead.find(k);
	if (f == g_lastRead.end()) {
		if (g_lastRead.size() < kRepMax) g_lastRead.emplace(k, now);
		return;
	}
	const ULONGLONG d = now - f->second;
	f->second = now;
	n_rep[d < kRepEdge[0] ? 0 : d < kRepEdge[1] ? 1 : d < kRepEdge[2] ? 2 : 3]++;
	n_repBytes += want;
}

// Texture mips dropped from the streaming pool: FUN_18252c168(texture, keep from mip) frees a texture's top mips and
// returns the bytes freed. Every drop goes through it: the streaming update FUN_18085f70c (over budget / idle trim),
// the pool trims FUN_182518104 and FUN_182519698 (called by FUN_182515560 when a new piece finds no room, and inline
// in FUN_182237906), FUN_18251954c and FUN_1819d1f48. Counted by caller, to set against the repeats above.
constexpr uintptr_t kRvaDropMips = 0x252c168;
const unsigned char kDropBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,
                                     0x48,0x83,0xec,0x40,0xf6,0x81,0x9e,0x00 };
const uintptr_t kDropCaller[5] = { 0x85f70c, 0x19d1f48, 0x2518104, 0x251954c, 0x2519698 };   // sorted function starts
const char* const kDropName[6] = { "update", "19d1f48", "trim 18104", "1954c", "trim 19698", "other" };
using DropFn = int(__fastcall*)(uint8_t* tex, uint64_t mip);
DropFn g_dropOrig = nullptr;
uintptr_t g_base = 0;
std::atomic<long long> n_drop[6], n_dropBytes[6];

int __fastcall HkDropMips(uint8_t* tex, uint64_t mip) {
	const int r = g_dropOrig(tex, mip);
	if (r > 0) {
		const uintptr_t ret = (uintptr_t)_ReturnAddress() - g_base;
		int k = 5;
		for (int i = 4; i >= 0; i--) if (ret >= kDropCaller[i] && ret - kDropCaller[i] < 0x8000) { k = i; break; }
		n_drop[k]++;
		n_dropBytes[k] += r;
	}
	return r;
}

// The texture pool's size: FUN_180c18130 returns it in MB, asked by the streaming code every frame. With
// r_TexturesStreamDynamicPool 1 (the default) it is r_TexturesStreamDynamicPoolTargetVramUsage (80) % of the card's
// DXGI budget minus what isn't the pool, kept within r_TexturesStreamDynamicPoolMinSize (2048) and ...MaxSizeWH (8192);
// r_TexturesStreamPoolSize (5120 here) is only the fallback. *(int64*)DAT_18515c3a0 = bytes the pool holds now.
constexpr uintptr_t kRvaPoolMb = 0xc18130, kRvaPoolObj = 0x515c3a0;
const unsigned char kPoolBytes[] = { 0x40,0x55,0x48,0x8b,0xec,0x48,0x83,0xec,0x30,0x83,0x3d,0x98,0x41,0x4c,0x04,0x00 };
using PoolFn = long long(__fastcall*)();
PoolFn g_poolOrig = nullptr;
std::atomic<long long> g_poolMin{LLONG_MAX}, g_poolMax{0}, g_poolHeld{0};

long long __fastcall HkPoolMb() {
	const long long r = g_poolOrig();
	for (long long m = g_poolMin.load(); r < m && !g_poolMin.compare_exchange_weak(m, r);) {}
	for (long long m = g_poolMax.load(); r > m && !g_poolMax.compare_exchange_weak(m, r);) {}
	if (const long long* obj = *(long long**)(g_base + kRvaPoolObj)) g_poolHeld.store(*obj, std::memory_order_relaxed);
	return r;
}

std::mutex g_pakMu;                       // per pak (by its cache object): name + bytes, for the "most read" line
struct Pak { std::string name; long long bytes = 0, calls = 0; };
std::unordered_map<uint8_t*, Pak> g_paks;

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long t) { return 1000.0 * (double)t / (double)g_freq; }

// ---- Lock probe: who holds the pak IO locks, and whether a thread is queued behind them (the step-2 session's note,
// 2026-10-23: this lock function is the top frame in 29 % of Main's step-5 samples). The lock is the game's recursive
// mutex (FUN_1803d902c / FUN_18042dc54): +0 SRWLOCK (bit 1 set = a thread is queued on it), +8 recursion count,
// +0xc owner thread id, written right after the acquire and zeroed right before the release. A thread looks at every
// pak lock the stream reads have used, about once a millisecond while reads come in. It only reads.
constexpr int kMaxLocks = 64;
std::atomic<uint8_t*> g_lockAt[kMaxLocks];
uint8_t* g_lockCache[kMaxLocks];          // the cache (pak) each lock belongs to, for its name
std::atomic<int> g_nLocks{0};
std::mutex g_lockRegMu;
std::atomic<ULONGLONG> g_lastReq{0};      // GetTickCount64 of the latest stream read

void NoteLock(uint8_t* cache) {
	uint8_t* m = *(uint8_t**)(cache + 0x998) + 8;
	const int n = g_nLocks.load(std::memory_order_acquire);
	for (int i = 0; i < n; i++) if (g_lockAt[i].load(std::memory_order_relaxed) == m) return;
	std::lock_guard<std::mutex> g(g_lockRegMu);
	const int k = g_nLocks.load(std::memory_order_relaxed);
	for (int i = 0; i < k; i++) if (g_lockAt[i].load(std::memory_order_relaxed) == m) return;
	if (k >= kMaxLocks) return;
	g_lockCache[k] = cache;
	g_lockAt[k].store(m, std::memory_order_relaxed);
	g_nLocks.store(k + 1, std::memory_order_release);
}

// Guarded: a pak closed since its lock was noted can't crash the game. No C++ objects here (MSVC forbids __try next
// to unwinding).
bool PeekLock(const uint8_t* m, uintptr_t* word, uint32_t* owner) {
	__try { *word = *(volatile const uintptr_t*)m; *owner = *(volatile const uint32_t*)(m + 0xc); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::string ThreadName(DWORD tid) {
	using GetDesc = HRESULT(WINAPI*)(HANDLE, PWSTR*);
	static const GetDesc get = (GetDesc)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription");
	char out[96];
	snprintf(out, sizeof out, "thread %lu", tid);
	if (!get) return out;
	const HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
	if (!h) return out;
	PWSTR w = nullptr;
	if (SUCCEEDED(get(h, &w)) && w) {
		if (*w) WideCharToMultiByte(CP_UTF8, 0, w, -1, out, sizeof out, nullptr, nullptr);
		LocalFree(w);
	}
	CloseHandle(h);
	return out;
}

std::string PakName(uint8_t* cache);

// Every 2 s with reads: "streamio locks:" = share of looks where a pak lock was held, by holder thread, and where a
// thread was queued behind it, by holder; the busiest pak. Runs for the first 15 minutes after start.
DWORD WINAPI LockProbe(LPVOID) {
	struct Who { DWORD tid; long long held, queued; };
	constexpr int kWho = 12;                      // the last slot gathers everyone else
	Who who[kWho] = {};
	long long looks = 0, anyHeld = 0, anyQueued = 0, perLock[kMaxLocks] = {};
	std::unordered_map<DWORD, std::string> names;
	const ULONGLONG start = GetTickCount64();
	ULONGLONG first = 0;
	while (GetTickCount64() - start < 15ull * 60 * 1000) {
		const ULONGLONG now = GetTickCount64();
		if (now - g_lastReq.load(std::memory_order_relaxed) > 3000) {
			if (!looks) { Sleep(100); continue; }   // idle: flush what was gathered below, then wait
		} else {
			Sleep(1);
			if (!first) first = now;
			const int n = g_nLocks.load(std::memory_order_acquire);
			bool held = false, queued = false;
			for (int i = 0; i < n; i++) {
				uintptr_t w = 0;
				uint32_t owner = 0;
				if (!PeekLock(g_lockAt[i].load(std::memory_order_relaxed), &w, &owner) || !owner) continue;
				held = true;
				perLock[i]++;
				int s = 0;
				while (s < kWho - 1 && who[s].tid && who[s].tid != owner) s++;
				if (s < kWho - 1 && !who[s].tid) who[s].tid = owner;
				who[s].held++;
				if (w & 2) { who[s].queued++; queued = true; }
			}
			looks++;
			anyHeld += held;
			anyQueued += queued;
			if (now - first < 2000) continue;
		}
		const double secs = (GetTickCount64() - first) / 1000.0;
		std::string h, q;
		for (int s = 0; s < kWho; s++) {
			if (!who[s].held) continue;
			std::string nm = "others";
			if (s < kWho - 1) {
				auto it = names.find(who[s].tid);
				if (it == names.end()) it = names.emplace(who[s].tid, ThreadName(who[s].tid)).first;
				nm = it->second;
			}
			char one[160];
			snprintf(one, sizeof one, "%s%s %.0f %%", h.empty() ? "" : ", ", nm.c_str(), 100.0 * who[s].held / looks);
			h += one;
			if (who[s].queued) {
				snprintf(one, sizeof one, "%s%s %.0f %%", q.empty() ? "" : ", ", nm.c_str(), 100.0 * who[s].queued / looks);
				q += one;
			}
		}
		int top = -1;
		const int n = g_nLocks.load(std::memory_order_acquire);
		for (int i = 0; i < n; i++) if (perLock[i] && (top < 0 || perLock[i] > perLock[top])) top = i;
		Log("streamio locks: %lld looks in %.1f s, a pak lock held %.0f %% [%s], a thread queued behind it %.0f %% [behind "
		    "%s]; busiest %s %.0f %% (%d locks)", looks, secs, 100.0 * anyHeld / looks, h.empty() ? "-" : h.c_str(),
		    100.0 * anyQueued / looks, q.empty() ? "-" : q.c_str(), top >= 0 ? PakName(g_lockCache[top]).c_str() : "-",
		    top >= 0 ? 100.0 * perLock[top] / looks : 0.0, n);
		for (auto& x : who) x.held = x.queued = 0;   // slots keep their thread
		looks = anyHeld = anyQueued = 0;
		for (auto& x : perLock) x = 0;
		first = 0;
	}
	Log("streamio locks: probe off (15 minutes after start)");
	return 0;
}

void CountPak(uint8_t* cache, HANDLE h, long long bytes) {
	std::lock_guard<std::mutex> g(g_pakMu);
	Pak& p = g_paks[cache];
	if (p.name.empty()) {
		wchar_t w[MAX_PATH] = {};
		const DWORD n = h != INVALID_HANDLE_VALUE ? GetFinalPathNameByHandleW(h, w, MAX_PATH, 0) : 0;
		if (n > 0 && n < MAX_PATH) {
			const wchar_t* base = wcsrchr(w, L'\\');
			char a[MAX_PATH] = {};
			WideCharToMultiByte(CP_UTF8, 0, base ? base + 1 : w, -1, a, MAX_PATH, nullptr, nullptr);
			p.name = a;
		} else {
			p.name = "?";
		}
	}
	p.bytes += bytes;
	p.calls++;
}

std::string PakName(uint8_t* cache) {
	std::lock_guard<std::mutex> g(g_pakMu);
	auto it = g_paks.find(cache);
	return it != g_paks.end() ? it->second.name : "?";
}

// One positional read of the whole request (sector-aligned) under the pak's lock, into a per-thread buffer.
// Returns false (nothing written to out) when it can't, so the caller runs the game's own read instead.
bool OneRead(uint8_t* cache, uint8_t* entry, uint8_t* out, int64_t offset, int64_t size, long long& lockTicks,
             long long& readTicks) {
	thread_local uint8_t* buf = nullptr;
	thread_local size_t cap = 0;
	const HANDLE h = *(HANDLE*)(cache + 0x10);
	const int64_t sector = *(int64_t*)(cache + 0x18);
	if (sector <= 0 || (sector & (sector - 1))) return false;
	const int64_t abs = offset + *(uint32_t*)(entry + 0xc);
	const int64_t start = abs & ~(sector - 1);
	const int64_t pad = abs - start;
	const int64_t total = (pad + size + sector - 1) & ~(sector - 1);
	constexpr int64_t kMax = 64ll << 20;
	if (total <= 0 || total > kMax) return false;
	if ((size_t)total > cap) {
		if (buf) VirtualFree(buf, 0, MEM_RELEASE);
		cap = (size_t)((total + (1 << 20) - 1) & ~((1ll << 20) - 1));
		buf = (uint8_t*)VirtualAlloc(nullptr, cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!buf) { cap = 0; return false; }
	}
	void* mutex = *(uint8_t**)(cache + 0x998) + 8;
	const long long t0 = Now();
	g_lock(mutex);
	const long long t1 = Now();
	OVERLAPPED ov = {};
	ov.Offset = (DWORD)(start & 0xffffffff);
	ov.OffsetHigh = (DWORD)((uint64_t)start >> 32);
	DWORD got = 0;
	const BOOL ok = ReadFile(h, buf, (DWORD)total, &got, &ov);   // synchronous handle: waits, then moves the pointer
	const long long t2 = Now();
	g_unlock(mutex);
	lockTicks += t1 - t0;
	readTicks += t2 - t1;
	if (!ok || (int64_t)got < pad + size) return false;
	memcpy(out, buf + pad, (size_t)size);
	return true;
}

uint32_t __fastcall HkRead(uint8_t* cache, uint8_t* entry, uint8_t* out, int64_t offset, int64_t size) {
	const long long t0 = Now();
	const bool unbuffered = entry && *(uint32_t*)(entry + 8) != 0 && *(int64_t*)(cache + 8 + 0x38) == 0 &&
	                        *g_uncached != 0 && *(HANDLE*)(cache + 0x10) != INVALID_HANDLE_VALUE;
	const int mode = g_mode.load(std::memory_order_relaxed);
	if (!unbuffered) {
		n_buffered++;
		return g_orig(cache, entry, out, offset, size);
	}
	const int64_t want = size ? size : (int64_t)*(uint32_t*)(entry + 4);
	g_lastReq.store(GetTickCount64(), std::memory_order_relaxed);
	NoteLock(cache);
	long long lockTicks = 0, readTicks = 0;
	uint32_t r = 0;
	bool verify = mode == 2;
	if (mode == 1 && g_selfCheck.load(std::memory_order_relaxed) > 0) verify = true;
	const int served = Streamahead_Serve(cache, entry, out, offset, size, want, g_orig);   // read ahead (streamahead.cpp)
	if (served >= 0) {
		r = (uint32_t)served;
	} else if (mode == 0) {
		const long long a = Now();
		r = g_orig(cache, entry, out, offset, size);
		readTicks += Now() - a;   // lock wait included: the game's function takes the lock itself
	} else if (verify) {
		r = g_orig(cache, entry, out, offset, size);
		if (r == 0) {
			thread_local std::vector<uint8_t> mine;
			mine.resize((size_t)want);
			if (OneRead(cache, entry, mine.data(), offset, want, lockTicks, readTicks)) {
				n_verified++;
				if (memcmp(mine.data(), out, (size_t)want)) {
					if (n_bad++ < 20) Log("streamio: MISMATCH request %lld bytes at %lld (entry offset %u)", (long long)want,
					                     (long long)offset, *(uint32_t*)(entry + 0xc));
					if (g_mode == 1) { g_mode = 0; Log("streamio: one-read path off for this session (mismatch in self-check)"); }
				}
			} else {
				n_fallback++;
			}
			if (g_mode == 1 && g_selfCheck.fetch_sub(1) == 1 && n_bad == 0)
				Log("streamio: self-check done (%lld requests matched), one read per request from now on", n_verified.load());
		}
	} else if (OneRead(cache, entry, out, offset, want, lockTicks, readTicks)) {
		n_one++;
		r = 0;
	} else {
		n_fallback++;
		const long long a = Now();
		r = g_orig(cache, entry, out, offset, size);
		readTicks += Now() - a;
	}
	n_calls++;
	n_bytes += want;
	n_lockTicks += lockTicks;
	n_readTicks += readTicks;
	n_size[want <= (32 << 10) ? 0 : want <= (128 << 10) ? 1 : want <= (512 << 10) ? 2 : want <= (1 << 20) ? 3
	       : want <= (4 << 20) ? 4 : 5]++;
	CountPak(cache, *(HANDLE*)(cache + 0x10), want);
	NoteRepeat(cache, entry, offset, want);
	Streamahead_Record(cache, entry, offset, want, Now() - t0);
	n_ticks += Now() - t0;
	return r;
}

DWORD WINAPI Reporter(LPVOID) {
	long long lastCalls = 0, lastBytes = 0, lastTicks = 0, lastLock = 0, lastRead = 0, lastRep[4] = {}, lastRepB = 0;
	ULONGLONG lastPaks = 0;
	const ULONGLONG start = GetTickCount64();
	ULONGLONG lastLine = 0, lastT = GetTickCount64();
	for (;;) {
		Sleep(2000);
		const ULONGLONG now = GetTickCount64();
		const long long c = n_calls.load();
		if (c == lastCalls) { lastT = now; continue; }
		if (now - start > 300000 && now - lastLine < 60000) continue;   // every 2 s for the boot, then once a minute
		const double secs = (now - lastT) / 1000.0;
		const long long b = n_bytes.load(), t = n_ticks.load(), lk = n_lockTicks.load(), rd = n_readTicks.load();
		char sizes[200] = "";
		int o = 0;
		for (int i = 0; i < 6; i++) o += snprintf(sizes + o, sizeof sizes - o, "%s%s %lld", i ? ", " : "", kSizeName[i], n_size[i].load());
		char reps[200];
		long long repN = 0, rp[4];
		for (int i = 0; i < 4; i++) repN += (rp[i] = n_rep[i].load()) - lastRep[i];
		const long long repB = n_repBytes.load();
		o = snprintf(reps, sizeof reps, "+%lld (%.0f MB), since the last read", repN, (repB - lastRepB) / 1e6);
		for (int i = 0; i < 4; i++) o += snprintf(reps + o, sizeof reps - o, "%s %s %lld", i ? "," : ":", kRepName[i], rp[i] - lastRep[i]);
		for (int i = 0; i < 4; i++) lastRep[i] = rp[i];
		lastRepB = repB;
		static long long lastDrop[6], lastDropB[6];
		char drops[300];
		long long dN = 0, dB = 0;
		for (int i = 0; i < 6; i++) dN += n_drop[i].load() - lastDrop[i], dB += n_dropBytes[i].load() - lastDropB[i];
		o = snprintf(drops, sizeof drops, "+%lld (%.0f MB)", dN, dB / 1e6);
		for (int i = 0; i < 6; i++) {
			const long long a = n_drop[i].load(), bb = n_dropBytes[i].load();
			if (a != lastDrop[i]) o += snprintf(drops + o, sizeof drops - o, ", %s %lld (%.0f MB)", kDropName[i], a - lastDrop[i], (bb - lastDropB[i]) / 1e6);
			lastDrop[i] = a; lastDropB[i] = bb;
		}
		const long long pMin = g_poolMin.exchange(LLONG_MAX), pMax = g_poolMax.exchange(0);
		char pool[120] = "not asked";
		if (pMax) snprintf(pool, sizeof pool, "%lld-%lld MB, holding %.0f MB", pMin, pMax, g_poolHeld.load() / 1048576.0);
		long long aServed = 0, aFirst = 0, aNot = 0;
		Streamahead_Stats(&aServed, &aFirst, &aNot);
		Log("streamio: mode %d  +%lld requests %.1f MB in %.1f s = %.0f MB/s, read thread busy %.0f %% (ReadFile %.0f ms, "
		    "lock wait %.0f ms)  total %lld requests %.1f MB, one-read %lld, fallback %lld, verified %lld, mismatches %lld, "
		    "buffered path %lld, read ahead: served %lld, game first %lld, not listed %lld  sizes: %s  repeats %s  texture mips dropped %s  texture pool %s", g_mode.load(),
		    c - lastCalls, (b - lastBytes) / 1e6, secs,
		    (b - lastBytes) / 1e6 / (secs > 0 ? secs : 1), 100.0 * Ms(t - lastTicks) / (secs * 1000.0), Ms(rd - lastRead),
		    Ms(lk - lastLock), c, b / 1e6, n_one.load(), n_fallback.load(), n_verified.load(), n_bad.load(),
		    n_buffered.load(), aServed, aFirst, aNot, sizes, reps, drops, pool);
		lastCalls = c; lastBytes = b; lastTicks = t; lastLock = lk; lastRead = rd; lastT = now; lastLine = now;
		if (now - lastPaks >= 30000) {
			lastPaks = now;
			std::vector<std::pair<long long, std::string>> top;
			{
				std::lock_guard<std::mutex> g(g_pakMu);
				for (auto& kv : g_paks) top.push_back({ kv.second.bytes, kv.second.name });
			}
			std::sort(top.begin(), top.end(), [](auto& a, auto& x) { return a.first > x.first; });
			std::string s;
			for (size_t i = 0; i < top.size() && i < 10; i++) {
				char one[200];
				snprintf(one, sizeof one, "%s%s %.0f MB", i ? ", " : "", top[i].second.c_str(), top[i].first / 1e6);
				s += one;
			}
			Log("streamio: most read (%zu paks): %s", top.size(), s.c_str());
		}
	}
}

void SaveMode(int m) {
	if (FILE* f = fopen(g_modeFile.c_str(), "w")) { fprintf(f, "%d\n", m); fclose(f); }
}

}  // namespace

void Streamio_Install(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	g_modeFile = path;
	g_modeFile = g_modeFile.substr(0, g_modeFile.find_last_of('\\') + 1) + "deagle_streamio.txt";
	if (FILE* fp = fopen(g_modeFile.c_str(), "r")) {
		int m = 0;
		if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 2) g_mode = m;
		fclose(fp);
	}
	void* rd = (void*)(base + kRvaRead);
	void* lk = (void*)(base + kRvaLock);
	void* ul = (void*)(base + kRvaUnlock);
	const bool okR = !memcmp(rd, kReadBytes, sizeof kReadBytes), okL = !memcmp(lk, kLockBytes, sizeof kLockBytes),
	           okU = !memcmp(ul, kUnlockBytes, sizeof kUnlockBytes);
	if (!okR || !okL || !okU) {
		Log("streamio: code check failed (read %d lock %d unlock %d) - not installed (other game version?)", okR, okL, okU);
		return;
	}
	g_lock = (LockFn)lk;
	g_unlock = (LockFn)ul;
	g_uncached = (const int*)(base + kRvaUncached);
	Streamahead_Install(self);   // before the hook goes on: it serves and records from the hook (streamahead.cpp)
	const bool h = MH_CreateHook(rd, (void*)&HkRead, (void**)&g_orig) == MH_OK && MH_EnableHook(rd) == MH_OK;
	g_base = base;
	void* dm = (void*)(base + kRvaDropMips);
	const bool okD = !memcmp(dm, kDropBytes, sizeof kDropBytes);
	const bool hd = okD && MH_CreateHook(dm, (void*)&HkDropMips, (void**)&g_dropOrig) == MH_OK && MH_EnableHook(dm) == MH_OK;
	Log("streamio: texture mip-drop counter %s", hd ? "on" : okD ? "FAILED" : "off (code check failed)");
	void* pm = (void*)(base + kRvaPoolMb);
	const bool okP = !memcmp(pm, kPoolBytes, sizeof kPoolBytes);
	const bool hp = okP && MH_CreateHook(pm, (void*)&HkPoolMb, (void**)&g_poolOrig) == MH_OK && MH_EnableHook(pm) == MH_OK;
	Log("streamio: texture pool size log %s", hp ? "on" : okP ? "FAILED" : "off (code check failed)");
	Log("streamio: mode %d (%s), ReadFileStreaming hook %s, sys_UncachedStreamReads %d", g_mode.load(),
	    g_mode == 0 ? "game reads, counted" : g_mode == 1 ? "one read per request, self-check first" : "verify both",
	    h ? "on" : "FAILED", *g_uncached);
	if (h) {
		CloseHandle(CreateThread(nullptr, 0, Reporter, nullptr, 0, nullptr));
		CloseHandle(CreateThread(nullptr, 0, LockProbe, nullptr, 0, nullptr));
	}
}

void Streamio_Command(const char* args) {
	while (args && *args == ' ') args++;
	if (args && *args >= '0' && *args <= '2' && (args[1] == 0 || args[1] == ' ')) {
		g_mode = *args - '0';
		SaveMode(g_mode);
		Log("streamio: mode %d (kept for the next start in %s)", g_mode.load(), g_modeFile.c_str());
		return;
	}
	Log("streamio: mode %d  %lld requests %.1f MB, inside %.0f ms (ReadFile %.0f ms, lock wait %.0f ms), one-read %lld, "
	    "fallback %lld, verified %lld, mismatches %lld  (streamio 0|1|2)", g_mode.load(), n_calls.load(),
	    n_bytes.load() / 1e6, Ms(n_ticks), Ms(n_readTicks), Ms(n_lockTicks), n_one.load(), n_fallback.load(),
	    n_verified.load(), n_bad.load());
}
