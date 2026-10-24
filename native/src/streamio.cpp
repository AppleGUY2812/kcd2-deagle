// Bigger streaming reads: see streamio.h. Addresses: WHGame.dll 1.5.6, checked against the installed DLL at start.
#include "streamio.h"
#include "log.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
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

std::mutex g_pakMu;                       // per pak (by its cache object): name + bytes, for the "most read" line
struct Pak { std::string name; long long bytes = 0, calls = 0; };
std::unordered_map<uint8_t*, Pak> g_paks;

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long t) { return 1000.0 * (double)t / (double)g_freq; }

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
	long long lockTicks = 0, readTicks = 0;
	uint32_t r = 0;
	bool verify = mode == 2;
	if (mode == 1 && g_selfCheck.load(std::memory_order_relaxed) > 0) verify = true;
	if (mode == 0) {
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
	n_ticks += Now() - t0;
	return r;
}

DWORD WINAPI Reporter(LPVOID) {
	long long lastCalls = 0, lastBytes = 0, lastTicks = 0, lastLock = 0, lastRead = 0;
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
		Log("streamio: mode %d  +%lld requests %.1f MB in %.1f s = %.0f MB/s, read thread busy %.0f %% (ReadFile %.0f ms, "
		    "lock wait %.0f ms)  total %lld requests %.1f MB, one-read %lld, fallback %lld, verified %lld, mismatches %lld, "
		    "buffered path %lld  sizes: %s", g_mode.load(), c - lastCalls, (b - lastBytes) / 1e6, secs,
		    (b - lastBytes) / 1e6 / (secs > 0 ? secs : 1), 100.0 * Ms(t - lastTicks) / (secs * 1000.0), Ms(rd - lastRead),
		    Ms(lk - lastLock), c, b / 1e6, n_one.load(), n_fallback.load(), n_verified.load(), n_bad.load(),
		    n_buffered.load(), sizes);
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
	const bool h = MH_CreateHook(rd, (void*)&HkRead, (void**)&g_orig) == MH_OK && MH_EnableHook(rd) == MH_OK;
	Log("streamio: mode %d (%s), ReadFileStreaming hook %s, sys_UncachedStreamReads %d", g_mode.load(),
	    g_mode == 0 ? "game reads, counted" : g_mode == 1 ? "one read per request, self-check first" : "verify both",
	    h ? "on" : "FAILED", *g_uncached);
	if (h) CloseHandle(CreateThread(nullptr, 0, Reporter, nullptr, 0, nullptr));
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
