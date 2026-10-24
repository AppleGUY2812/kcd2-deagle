// Faster pak unpacking: see fastzip.h. Addresses: WHGame.dll 1.5.6 (docs/PERFORMANCE.md "What the load's threads do").
#include "fastzip.h"
#include "log.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "MinHook.h"
#include "libdeflate.h"

namespace {

// FUN_1807675a8: inflateInit2_(-15, "1.2.11"), then one inflate(Z_SYNC_FLUSH) of the whole entry, or the 16 KB-window
// loop FUN_1803d93c0 when src and dst overlap. Success: *result = 0 (inflateEnd), *outSize = total_out.
constexpr uintptr_t kRvaUnpack = 0x7675a8;
const unsigned char kUnpackBytes[] = { 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24,
                                       0x30,0xc0,0xff,0xff,0xb8,0xd0,0x40,0x00,0x00 };
// FUN_180612300: zlib's crc32 (slice-by-4, ~crc in and out, table 0x18409a500 = the standard 0xEDB88320 one)
constexpr uintptr_t kRvaCrc = 0x612300;
const unsigned char kCrcBytes[] = { 0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x8d,0x35 };

// FUN_1803d9224: the streaming engine's unpack of one block of a file (job FUN_1803d9064 -> here), into dst at the
// file's current output position. z_stream kept across blocks: inflateInit2_(-15) when total_out == 0, then
// inflate(Z_SYNC_FLUSH) of this block; *result = zlib's return (1 = stream end), *totalOut = total_out; inflateEnd on
// stream end or error. Overlap -> the 16 KB-window loop; flag -> FUN_1824857a8 (unpacks through a 16 KB scratch).
constexpr uintptr_t kRvaStream = 0x3d9224;
const unsigned char kStreamBytes[] = { 0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0xb8,0xc8,0x40,
                                       0x00,0x00,0xe8 };
using StreamFn = void(__fastcall*)(int* result, uint8_t* strm, uint8_t* window, uint8_t* dst, uint32_t dstLen, char flag,
                                   const uint8_t* src, uint32_t srcLen, uint32_t* totalOut);
StreamFn g_origStream = nullptr;
// whole = the first block held the whole file (libdeflate did it); split = the game's zlib did it (later blocks too)
std::atomic<long long> s_calls{0}, s_whole{0}, s_split{0}, s_out{0}, s_ticks{0}, s_bad{0}, s_zlibTicks{0}, s_deflTicks{0};
std::atomic<long long> s_noStrm{0}, s_notFirst{0}, s_stateSet{0}, s_flag{0}, s_overlap{0};   // why not tried
std::atomic<int> g_streamMode{0};        // fastload_fastzip_stream.txt: 0 game zlib (counts only), 1 libdeflate, 2 verify
std::atomic<bool> s_off{false};          // too few whole files: stop trying (a failed try costs a partial unpack)

using UnpackFn = void(__fastcall*)(const uint8_t* src, uint8_t* dst, uint32_t srcSize, uint32_t dstSize,
                                   uint32_t* outSize, uint32_t* result, void* heap);
using CrcFn = uint32_t(__fastcall*)(uint32_t crc, const void* buf, size_t len);

UnpackFn g_origUnpack = nullptr;
CrcFn g_origCrc = nullptr;
std::atomic<int> g_mode{1};
std::string g_modeFile;

// counters since start; ticks are QueryPerformanceCounter units
std::atomic<long long> u_calls{0}, u_out{0}, u_ticks{0}, u_fallback{0}, u_overlap{0}, u_bad{0};
std::atomic<long long> u_zlibTicks{0}, u_deflTicks{0};          // verify mode: both on the same calls
std::atomic<long long> c_calls{0}, c_bytes{0}, c_ticks{0}, c_bad{0}, c_zlibTicks{0}, c_deflTicks{0};
long long g_freq = 1;

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long t) { return 1000.0 * (double)t / (double)g_freq; }

libdeflate_decompressor* Decomp() {
	thread_local libdeflate_decompressor* d = libdeflate_alloc_decompressor();   // not shared between threads
	return d;
}

void __fastcall HkUnpack(const uint8_t* src, uint8_t* dst, uint32_t srcSize, uint32_t dstSize, uint32_t* outSize,
                         uint32_t* result, void* heap) {
	const int mode = g_mode.load(std::memory_order_relaxed);
	const long long t0 = Now();
	u_calls++;
	const bool overlap = dst < src + srcSize && src < dst + dstSize;
	libdeflate_decompressor* d = (mode != 0 && !overlap) ? Decomp() : nullptr;
	if (!d) {
		if (overlap) u_overlap++;
		g_origUnpack(src, dst, srcSize, dstSize, outSize, result, heap);
		u_out += *result == 0 ? *outSize : 0;
		u_ticks += Now() - t0;
		return;
	}
	if (mode == 1) {
		size_t in = 0, out = 0;
		if (libdeflate_deflate_decompress_ex(d, src, srcSize, dst, dstSize, &in, &out) == LIBDEFLATE_SUCCESS) {
			*outSize = (uint32_t)out;
			*result = 0;
			u_out += out;
			u_ticks += Now() - t0;
			return;
		}
		u_fallback++;   // bad data, or more output than dstSize (zlib then returns the truncated part): its own code
		g_origUnpack(src, dst, srcSize, dstSize, outSize, result, heap);
		u_out += *result == 0 ? *outSize : 0;
		u_ticks += Now() - t0;
		return;
	}
	// mode 2, verify: libdeflate into a scratch buffer, then the game's zlib into dst (the game keeps zlib's result)
	uint8_t* tmp = (uint8_t*)malloc(dstSize ? dstSize : 1);
	size_t in = 0, out = 0;
	const long long a = Now();
	const libdeflate_result r = tmp ? libdeflate_deflate_decompress_ex(d, src, srcSize, tmp, dstSize, &in, &out)
	                                : LIBDEFLATE_BAD_DATA;
	const long long b = Now();
	g_origUnpack(src, dst, srcSize, dstSize, outSize, result, heap);
	const long long c = Now();
	u_deflTicks += b - a;
	u_zlibTicks += c - b;
	if (r == LIBDEFLATE_SUCCESS) {
		if (*result != 0 || *outSize != out || memcmp(tmp, dst, out) != 0) {
			if (u_bad++ < 20)
				Log("fastzip: MISMATCH unpack src %u -> dst %u: zlib result %u out %u, libdeflate out %zu", srcSize,
				    dstSize, *result, *result == 0 ? *outSize : 0, out);
		}
	} else {
		u_fallback++;   // mode 1 would have handed this one to zlib
	}
	free(tmp);
	u_out += *result == 0 ? *outSize : 0;
	u_ticks += Now() - t0;
}

void __fastcall HkStream(int* result, uint8_t* strm, uint8_t* window, uint8_t* dst, uint32_t dstLen, char flag,
                         const uint8_t* src, uint32_t srcLen, uint32_t* totalOut) {
	const int mode = g_mode.load(std::memory_order_relaxed) == 0 ? 0 : g_streamMode.load(std::memory_order_relaxed);
	const long long t0 = Now();
	s_calls++;
	// first block of a file, stream not set up yet, plain output: try the whole file in one go
	const bool first = strm && *(const uint32_t*)(strm + 0x1c) == 0 && *(void* const*)(strm + 0x28) == nullptr;
	const bool overlap = dst < src + srcLen && src < dst + dstLen;
	if (!strm) s_noStrm++;
	else if (*(const uint32_t*)(strm + 0x1c) != 0) s_notFirst++;
	else if (*(void* const*)(strm + 0x28) != nullptr) s_stateSet++;
	else if (flag) s_flag++;
	else if (overlap) s_overlap++;
	libdeflate_decompressor* d = (mode != 0 && first && !flag && !overlap && !s_off) ? Decomp() : nullptr;
	if (!d) {
		g_origStream(result, strm, window, dst, dstLen, flag, src, srcLen, totalOut);
		s_ticks += Now() - t0;
		return;
	}
	if (mode == 1) {
		size_t in = 0, out = 0;
		if (libdeflate_deflate_decompress_ex(d, src, srcLen, dst, dstLen, &in, &out) == LIBDEFLATE_SUCCESS && in <= srcLen) {
			*result = 1;                                  // Z_STREAM_END; nothing to inflateEnd (never set up)
			// the z_stream as zlib leaves it after the end: next_in/avail_in/total_in, next_out/avail_out/total_out
			*(const uint8_t**)(strm + 0x0) = src + in; *(uint32_t*)(strm + 0x8) = srcLen - (uint32_t)in;
			*(uint32_t*)(strm + 0xc) = (uint32_t)in;
			*(uint8_t**)(strm + 0x10) = dst + out; *(uint32_t*)(strm + 0x18) = dstLen - (uint32_t)out;
			*(uint32_t*)(strm + 0x1c) = (uint32_t)out;
			if (totalOut) *totalOut = (uint32_t)out;
			s_whole++; s_out += out; s_ticks += Now() - t0;
			return;
		}
		// not the whole file (or bad data): the game's zlib redoes this block from the start of dst
		const long long n = ++s_split;
		if (n >= 300 && n * 2 > s_whole.load() * 3) s_off = true;   // > 60 % split: not worth trying
		g_origStream(result, strm, window, dst, dstLen, flag, src, srcLen, totalOut);
		s_ticks += Now() - t0;
		return;
	}
	// verify: libdeflate into scratch, then zlib into dst; compare when zlib also finished the file in this block
	uint8_t* tmp = (uint8_t*)malloc(dstLen ? dstLen : 1);
	size_t in = 0, out = 0;
	const long long a = Now();
	const libdeflate_result r = tmp ? libdeflate_deflate_decompress_ex(d, src, srcLen, tmp, dstLen, &in, &out) : LIBDEFLATE_BAD_DATA;
	const long long b = Now();
	g_origStream(result, strm, window, dst, dstLen, flag, src, srcLen, totalOut);
	const long long c = Now();
	const bool zEnd = *result == 1;
	if (r == LIBDEFLATE_SUCCESS && zEnd) {
		s_whole++; s_deflTicks += b - a; s_zlibTicks += c - b; s_out += out;
		if ((totalOut && *totalOut != out) || *(const uint32_t*)(strm + 0xc) != in || memcmp(tmp, dst, out) != 0)
			if (s_bad++ < 20) Log("fastzip: MISMATCH stream block %u -> %u: zlib out %u, libdeflate out %zu", srcLen, dstLen,
			                      totalOut ? *totalOut : 0, out);
	} else {
		s_split++;
		if ((r == LIBDEFLATE_SUCCESS) != zEnd && s_bad++ < 20)
			Log("fastzip: MISMATCH stream end: zlib result %d, libdeflate %d (block %u -> %u)", *result, (int)r, srcLen, dstLen);
	}
	free(tmp);
	s_ticks += Now() - t0;
}

uint32_t __fastcall HkCrc(uint32_t crc, const void* buf, size_t len) {
	const int mode = g_mode.load(std::memory_order_relaxed);
	const long long t0 = Now();
	uint32_t r;
	if (mode == 0) r = g_origCrc(crc, buf, len);
	else if (mode == 1) r = libdeflate_crc32(crc, buf, len);
	else {
		const uint32_t f = libdeflate_crc32(crc, buf, len);
		const long long a = Now();
		r = g_origCrc(crc, buf, len);
		const long long b = Now();
		c_deflTicks += a - t0;
		c_zlibTicks += b - a;
		if (f != r && c_bad++ < 20) Log("fastzip: MISMATCH crc32 len %zu: zlib %08x libdeflate %08x", len, r, f);
	}
	c_calls++;
	c_bytes += len;
	c_ticks += Now() - t0;
	return r;
}

DWORD WINAPI Reporter(LPVOID) {
	long long seen = -1;
	const ULONGLONG start = GetTickCount64();
	for (;;) {
		Sleep(2000);
		static ULONGLONG last = 0;
		const ULONGLONG now = GetTickCount64();
		if (now - start > 300000 && now - last < 60000) continue;   // every 2 s for the boot, then once a minute
		const long long n = u_calls.load() + s_calls.load();
		if (n == seen) continue;
		seen = n;
		last = now;
		char extra[400] = "";
		if (g_mode.load() == 2)
			snprintf(extra, sizeof extra, " | verify: unpack zlib %.0f ms vs libdeflate %.0f ms, crc32 zlib %.0f ms vs "
			         "libdeflate %.0f ms, stream whole files zlib %.0f ms vs libdeflate %.0f ms, mismatches %lld + %lld + %lld",
			         Ms(u_zlibTicks), Ms(u_deflTicks), Ms(c_zlibTicks), Ms(c_deflTicks), Ms(s_zlibTicks), Ms(s_deflTicks),
			         u_bad.load(), c_bad.load(), s_bad.load());
		Log("fastzip: mode %d  unpack %lld files %.1f MB in %.0f ms (to zlib: %lld in-place, %lld other)  crc32 %lld "
		    "calls %.1f MB in %.0f ms%s", g_mode.load(), u_calls.load(), u_out.load() / 1e6, Ms(u_ticks),
		    u_overlap.load(), u_fallback.load(), c_calls.load(), c_bytes.load() / 1e6, Ms(c_ticks), extra);
		Log("fastzip: stream (mode %d) blocks %lld: whole files by libdeflate %lld (%.1f MB), split -> zlib %lld%s, %.0f ms;"
		    " not tried: no z_stream %lld, later block %lld, z_stream set up %lld, flag %lld, overlap %lld",
		    g_streamMode.load(), s_calls.load(), s_whole.load(), s_out.load() / 1e6, s_split.load(),
		    s_off ? " (stopped trying)" : "", Ms(s_ticks), s_noStrm.load(), s_notFirst.load(), s_stateSet.load(),
		    s_flag.load(), s_overlap.load());
	}
}

void SaveMode(int m) {
	if (FILE* f = fopen(g_modeFile.c_str(), "w")) { fprintf(f, "%d\n", m); fclose(f); }
}

}  // namespace

void Fastzip_Install(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	g_modeFile = path;
	g_modeFile = g_modeFile.substr(0, g_modeFile.find_last_of('\\') + 1) + "fastload_fastzip.txt";
	if (FILE* fp = fopen(g_modeFile.c_str(), "r")) {
		int m = 1;
		if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 2) g_mode = m;
		fclose(fp);
	}
	void* up = (void*)(base + kRvaUnpack);
	void* cr = (void*)(base + kRvaCrc);
	const bool okU = !memcmp(up, kUnpackBytes, sizeof kUnpackBytes), okC = !memcmp(cr, kCrcBytes, sizeof kCrcBytes);
	if (!okU || !okC) { Log("fastzip: code check failed (unpack %d crc32 %d) - not installed (other game version?)", okU, okC); return; }
	if (!Decomp()) { Log("fastzip: libdeflate alloc failed - not installed"); return; }
	const bool hU = MH_CreateHook(up, (void*)&HkUnpack, (void**)&g_origUnpack) == MH_OK && MH_EnableHook(up) == MH_OK;
	const bool hC = MH_CreateHook(cr, (void*)&HkCrc, (void**)&g_origCrc) == MH_OK && MH_EnableHook(cr) == MH_OK;
	if (FILE* fp = fopen((g_modeFile.substr(0, g_modeFile.size() - 4) + "_stream.txt").c_str(), "r")) {
		int m = 0;
		if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 2) g_streamMode = m;
		fclose(fp);
	}
	void* st = (void*)(base + kRvaStream);
	bool hS = false;
	if (memcmp(st, kStreamBytes, sizeof kStreamBytes)) Log("fastzip: stream unpack code check failed - that hook not installed");
	else hS = MH_CreateHook(st, (void*)&HkStream, (void**)&g_origStream) == MH_OK && MH_EnableHook(st) == MH_OK;
	Log("fastzip: mode %d (%s), unpack hook %s, crc32 hook %s, stream unpack hook %s", g_mode.load(),
	    g_mode == 0 ? "game zlib" : g_mode == 1 ? "libdeflate" : "verify both", hU ? "on" : "FAILED", hC ? "on" : "FAILED",
	    hS ? "on" : "FAILED");
	CloseHandle(CreateThread(nullptr, 0, Reporter, nullptr, 0, nullptr));
}

void Fastzip_Command(const char* args) {
	while (args && *args == ' ') args++;
	if (args && *args >= '0' && *args <= '2' && (args[1] == 0 || args[1] == ' ')) {
		g_mode = *args - '0';
		SaveMode(g_mode);
		Log("fastzip: mode %d (kept for the next start in %s)", g_mode.load(), g_modeFile.c_str());
		return;
	}
	Log("fastzip: mode %d  unpack %lld files %.1f MB %.0f ms  crc32 %lld calls %.1f MB %.0f ms  (fastzip 0|1|2)",
	    g_mode.load(), u_calls.load(), u_out.load() / 1e6, Ms(u_ticks), c_calls.load(), c_bytes.load() / 1e6, Ms(c_ticks));
}
