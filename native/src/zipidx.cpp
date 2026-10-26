// Baked archive indexes: see zipidx.h.
#include "zipidx.h"
#include "early.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr uintptr_t kRvaMake = 0x4d5dd4;    // Cache** (factory*, Cache** out, const char* path)
const unsigned char kMakeBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x4c,0x89,0x44,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41 };
constexpr uintptr_t kRvaInit = 0x4d67e8;    // void (Cache*, file struct*, handle, treeSize, flags, total)
const unsigned char kInitBytes[] = { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57 };
constexpr uintptr_t kRvaClose = 0x4d63b0;   // void (factory*): the factory's buffers and file released
const unsigned char kCloseBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48 };
constexpr uintptr_t kRvaAllocFirst = 0x549d370;   // int: CryMalloc set up
constexpr uintptr_t kRvaAllocFn = 0x549d378;      // void* (*)(size_t, int* actual, int)
constexpr uintptr_t kRvaAllocSetup = 0x45aaa0;    // void ()
constexpr uintptr_t kRvaStatActual = 0x51dfb20, kRvaStatAsked = 0x51dfb18, kRvaStatCount = 0x51dfb24;

constexpr size_t kHdrA = 0x78, kHdrAEnd = 0x990, kHdrB = 0x9a0, kHdrBEnd = 0x9a8, kTree = 0x9a8;
constexpr uint32_t kMagic = 0x3258495a;   // "ZIX2": + an FNV-1a hash per entry

using MakeFn = void**(__fastcall*)(char* f, void** out, const char* path);
using InitFn = void(__fastcall*)(char* c, char* file, uint64_t h, uint64_t treeSize, uint32_t flags, uint64_t total);
using CloseFn = void(__fastcall*)(char* f);
using AllocFn = char*(__fastcall*)(size_t n, int* actual, int z);
using SetupFn = void(__fastcall*)();
MakeFn g_orig = nullptr;
InitFn g_init = nullptr;
CloseFn g_close = nullptr;
uintptr_t g_base = 0;
int g_mode = 0;
std::atomic<bool> g_installed{ false };
std::string g_file;
uint32_t g_stamp = 0;

struct Entry {
	std::string path;   // lower case
	uint64_t size = 0, time = 0, tree = 0;
	uint32_t flags = 0;
	uint64_t hash = 0;            // FNV-1a of hdrA, hdrB and the tree
	const char* hdrA = nullptr;   // kHdrAEnd - kHdrA bytes
	const char* hdrB = nullptr;   // kHdrBEnd - kHdrB bytes
	const char* treeBytes = nullptr;
	std::vector<char> own;        // set for entries made this run
};
SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_map<std::string, Entry*> g_map;
std::vector<char> g_loaded;
HANDLE g_loadDone = nullptr;
std::atomic<int> g_dirty{ 0 };
std::atomic<long long> g_lastMiss{ 0 };
long long g_freq = 1;
std::atomic<long long> g_ticksMain{ 0 }, g_ticksAll{ 0 }, g_ticksLoadWait{ 0 };
std::atomic<int> g_eocdMiss{ 0 }, g_calls{ 0 }, g_hits{ 0 }, g_misses{ 0 }, g_noId{ 0 }, g_verifyBad{ 0 }, g_verifyOk{ 0 };
DWORD g_mainTid = 0;

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long t) { return (double)t * 1000.0 / g_freq; }

std::string Lower(const char* s) {
	std::string r(s);
	for (char& c : r) { if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); if (c == '/') c = '\\'; }
	return r;
}

uint64_t Fnv(uint64_t h, const char* p, size_t n) {
	for (size_t i = 0; i < n; i++) { h ^= (unsigned char)p[i]; h *= 0x100000001b3ull; }
	return h;
}
uint64_t EntryHash(const Entry* e) {
	uint64_t h = 0xcbf29ce484222325ull;
	h = Fnv(h, e->hdrA, kHdrAEnd - kHdrA);
	h = Fnv(h, e->hdrB, kHdrBEnd - kHdrB);
	return Fnv(h, e->treeBytes, e->tree);
}

// The zip end record at the file's end (no comment): its directory offset and size must match the saved ones
// (cache +0x78 / +0x7c), so a pak repacked with the same size and time is built by the game again.
bool EocdOk(const char* path, uint64_t size, uint32_t cdOff, uint32_t cdSize) {
	if (size < 22) return false;
	HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	unsigned char b[22] = {};
	LARGE_INTEGER at; at.QuadPart = (LONGLONG)size - 22;
	DWORD got = 0;
	const bool rd = SetFilePointerEx(h, at, nullptr, FILE_BEGIN) && ReadFile(h, b, 22, &got, nullptr) && got == 22;
	CloseHandle(h);
	uint32_t sig, sz, off;
	memcpy(&sig, b, 4); memcpy(&sz, b + 12, 4); memcpy(&off, b + 16, 4);
	return rd && sig == 0x06054b50 && sz == cdSize && off == cdOff;
}

// Tree bytes equal except the 0x48-byte records' bytes the game never writes (FUN_1808b234c: 0x1a-0x1b, 0x2c-0x2f,
// 0x36-0x37, 0x41-0x47 hold whatever memory had; the readers never look at them).
bool TreeSame(const char* a, const char* b, uint64_t n, uint32_t flags) {
	uint64_t base = 0, count = n / 0x48;
	if (!(flags & 0x40)) {
		if (n < 8) return !memcmp(a, b, n);
		uint16_t dirs; uint32_t files;
		memcpy(&dirs, a, 2); memcpy(&files, a + 4, 4);
		base = 8 + 8ull * dirs; count = files;
		if (base + count * 0x48 > n) return !memcmp(a, b, n);
	}
	if (memcmp(a, b, base)) return false;
	static const unsigned char kUsed[0x48] = {
		1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, 1,1,0,0,1,1,1,1,
		1,1,1,1,1,1,1,1, 1,1,1,1,0,0,0,0, 1,1,1,1,1,1,0,0, 1,1,1,1,1,1,1,1,
		1,0,0,0,0,0,0,0 };
	for (uint64_t r = 0; r < count; r++) {
		const char* x = a + base + r * 0x48; const char* y = b + base + r * 0x48;
		for (int i = 0; i < 0x48; i++) if (kUsed[i] && x[i] != y[i]) return false;
	}
	const uint64_t end = base + count * 0x48;
	return !memcmp(a + end, b + end, n - end);
}

bool FileId(const char* path, uint64_t& size, uint64_t& time) {
	WIN32_FILE_ATTRIBUTE_DATA a;
	if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a)) return false;
	size = ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
	time = ((uint64_t)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
	return true;
}

// file: magic, stamp, count, then per entry: u32 pathLen, path, u64 size, time, tree, u32 flags, hdrA, hdrB, tree
DWORD WINAPI LoadThread(LPVOID) {
	HANDLE h = CreateFileA(g_file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	int n = 0, bad = 0;
	if (h != INVALID_HANDLE_VALUE) {
		LARGE_INTEGER sz{};
		GetFileSizeEx(h, &sz);
		g_loaded.resize((size_t)sz.QuadPart);
		size_t got = 0;
		while (got < g_loaded.size()) {
			DWORD r = 0;
			const size_t left = g_loaded.size() - got;
			const DWORD want = (DWORD)(left < (64u << 20) ? left : (64u << 20));
			if (!ReadFile(h, g_loaded.data() + got, want, &r, nullptr) || !r) break;
			got += r;
		}
		CloseHandle(h);
		const char* p = g_loaded.data();
		const char* e = p + got;
		auto take = [&](void* d, size_t k) { if ((size_t)(e - p) < k) return false; memcpy(d, p, k); p += k; return true; };
		uint32_t magic = 0, stamp = 0, count = 0;
		if (got == g_loaded.size() && take(&magic, 4) && take(&stamp, 4) && take(&count, 4) && magic == kMagic && stamp == g_stamp) {
			for (uint32_t i = 0; i < count; i++) {
				uint32_t len = 0;
				Entry* en = new Entry();
				if (!take(&len, 4) || (size_t)(e - p) < len) { delete en; break; }
				en->path.assign(p, len); p += len;
				if (!take(&en->size, 8) || !take(&en->time, 8) || !take(&en->tree, 8) || !take(&en->flags, 4) || !take(&en->hash, 8)) { delete en; break; }
				const size_t need = (kHdrAEnd - kHdrA) + (kHdrBEnd - kHdrB) + en->tree;
				if ((size_t)(e - p) < need) { delete en; break; }
				en->hdrA = p; p += kHdrAEnd - kHdrA;
				en->hdrB = p; p += kHdrBEnd - kHdrB;
				en->treeBytes = p; p += en->tree;
				if (en->tree > en->size || EntryHash(en) != en->hash) { bad++; delete en; continue; }
				g_map[en->path] = en;
				n++;
			}
		} else Log("zipidx: %s is from another game version or damaged: baked again", g_file.c_str());
	}
	Log("zipidx: %d baked archive indexes read (%.1f MB), %d damaged ones dropped", n, g_loaded.size() / 1048576.0, bad);
	SetEvent(g_loadDone);
	return 0;
}

void WaitLoaded() {
	if (WaitForSingleObject(g_loadDone, 0) == WAIT_OBJECT_0) return;
	const long long t0 = Now();
	WaitForSingleObject(g_loadDone, INFINITE);
	g_ticksLoadWait += Now() - t0;
}

void Save() {
	std::vector<Entry*> all;
	AcquireSRWLockShared(&g_lock);
	for (auto& kv : g_map) all.push_back(kv.second);
	ReleaseSRWLockShared(&g_lock);
	const std::string tmp = g_file + ".tmp";
	FILE* fp = fopen(tmp.c_str(), "wb");
	if (!fp) { Log("zipidx: %s not written", tmp.c_str()); return; }
	const uint32_t count = (uint32_t)all.size();
	fwrite(&kMagic, 4, 1, fp); fwrite(&g_stamp, 4, 1, fp); fwrite(&count, 4, 1, fp);
	uint64_t bytes = 0;
	for (Entry* en : all) {
		const uint32_t len = (uint32_t)en->path.size();
		fwrite(&len, 4, 1, fp); fwrite(en->path.data(), 1, len, fp);
		fwrite(&en->size, 8, 1, fp); fwrite(&en->time, 8, 1, fp); fwrite(&en->tree, 8, 1, fp); fwrite(&en->flags, 4, 1, fp); fwrite(&en->hash, 8, 1, fp);
		fwrite(en->hdrA, 1, kHdrAEnd - kHdrA, fp); fwrite(en->hdrB, 1, kHdrBEnd - kHdrB, fp);
		fwrite(en->treeBytes, 1, en->tree, fp);
		bytes += en->tree;
	}
	const bool ok = !ferror(fp);
	fclose(fp);
	if (ok && MoveFileExA(tmp.c_str(), g_file.c_str(), MOVEFILE_REPLACE_EXISTING))
		Log("zipidx: %u archive indexes written (%.1f MB of trees)", count, bytes / 1048576.0);
	else Log("zipidx: %s not replaced (error %lu)", g_file.c_str(), GetLastError());
}

DWORD WINAPI WriterThread(LPVOID) {
	int reported = -1;
	for (;;) {
		Sleep(1000);
		if (g_dirty && Ms(Now() - g_lastMiss) > 3000) { g_dirty = 0; Save(); }
		const int c = g_calls;
		if (c != reported && Ms(Now() - g_lastMiss) > 3000) { reported = c; Zipidx_Report("quiet"); }
	}
}

void Record(const char* path, const std::string& key, uint64_t size, uint64_t time, const char* c) {
	const uint64_t tree = *(const uint64_t*)(c + 0x60);   // [0xc]: the tree's size
	const uint32_t flags = *(const uint32_t*)(c + 0x58);
	if (*(const int*)(c + 0x9a0) != 0 || tree > size) return;   // encrypted paks are not baked
	if (!EocdOk(path, size, *(const uint32_t*)(c + 0x78), *(const uint32_t*)(c + 0x7c))) return;   // a zip comment: not baked
	Entry* en = new Entry();
	en->path = key; en->size = size; en->time = time; en->tree = tree; en->flags = flags;
	en->own.resize((kHdrAEnd - kHdrA) + (kHdrBEnd - kHdrB) + tree);
	char* o = en->own.data();
	memcpy(o, c + kHdrA, kHdrAEnd - kHdrA); en->hdrA = o; o += kHdrAEnd - kHdrA;
	memcpy(o, c + kHdrB, kHdrBEnd - kHdrB); en->hdrB = o; o += kHdrBEnd - kHdrB;
	memcpy(o, c + kTree, tree); en->treeBytes = o;
	en->hash = EntryHash(en);
	AcquireSRWLockExclusive(&g_lock);
	g_map[key] = en;   // an older entry for the path stays allocated (the loaded buffer or its own bytes)
	ReleaseSRWLockExclusive(&g_lock);
	g_dirty = 1;
}

// Where two trees differ: header / directory / record bytes (by offset in the 0x48 record) / names. The first two
// differing paks are also written out (fastload_zipdiff_N_game.bin / _bake.bin) for a byte compare.
void DiffDetail(const char* a, const char* b, uint64_t n, uint32_t flags, const char* path) {
	static std::atomic<int> dumped{ 0 };
	uint64_t base = 0, count = n / 0x48;
	if (!(flags & 0x40) && n >= 8) {
		uint16_t dirs; uint32_t files;
		memcpy(&dirs, a, 2); memcpy(&files, a + 4, 4);
		base = 8 + 8ull * dirs; count = files;
	}
	const uint64_t end = base + count * 0x48 <= n ? base + count * 0x48 : n;
	uint64_t hdr = 0, dir = 0, rec = 0, names = 0, first = ~0ull;
	int byOff[0x48] = {};
	for (uint64_t i = 0; i < n; i++) {
		if (a[i] == b[i]) continue;
		if (first == ~0ull) first = i;
		if (i < 8 && base) hdr++;
		else if (i < base) dir++;
		else if (i < end) { rec++; byOff[(i - base) % 0x48]++; }
		else names++;
	}
	char offs[400] = {}; size_t o = 0;
	for (int k = 0; k < 0x48 && o < sizeof offs - 16; k++)
		if (byOff[k]) o += snprintf(offs + o, sizeof offs - o, " %x:%d", k, byOff[k]);
	Log("zipidx: verify detail %s: %llu bytes, mode %s, dirs+hdr end 0x%llx, records end 0x%llx; differ: header %llu, "
	    "directory %llu, records %llu (by offset in the record:%s), names %llu; first at 0x%llx",
	    path, (unsigned long long)n, (flags & 0x40) ? "sorted" : "tree", (unsigned long long)base, (unsigned long long)end,
	    (unsigned long long)hdr, (unsigned long long)dir, (unsigned long long)rec, offs, (unsigned long long)names,
	    (unsigned long long)first);
	const int k = dumped++;
	if (k >= 2) return;
	char file[MAX_PATH];
	for (int w = 0; w < 2; w++) {
		const std::string dir = g_file.substr(0, g_file.find_last_of('\\') + 1);
		snprintf(file, sizeof file, "%sfastload_zipdiff_%d_%s.bin", dir.c_str(), k, w ? "bake" : "game");
		if (FILE* fp = fopen(file, "wb")) { fwrite(w ? b : a, 1, n, fp); fclose(fp); }
	}
}

void Verify(const Entry* en, const char* c, const char* path) {
	const uint64_t tree = *(const uint64_t*)(c + 0x60);
	const char* what = nullptr;
	if (tree != en->tree) what = "tree size";
	else if (*(const uint32_t*)(c + 0x58) != en->flags) what = "flags";
	else if (!TreeSame(c + kTree, en->treeBytes, tree, en->flags)) what = "tree bytes";
	else if (*(const uint64_t*)(c + 0x50) != en->size) what = "file size (+0x50)";
	else if (memcmp(c + 0x78, en->hdrA, 8)) what = "directory offset/size";
	else if (memcmp(c + kHdrB, en->hdrB, kHdrBEnd - kHdrB)) what = "encryption/signing";
	if (what) { g_verifyBad++; Log("zipidx: verify: %s differs for %s", what, path); }
	if (what && !strcmp(what, "tree bytes")) DiffDetail(c + kTree, en->treeBytes, tree, en->flags, path);
	else g_verifyOk++;
}

void** __fastcall HkMake(char* f, void** out, const char* path) {
	const long long t0 = Now();
	if (!g_mainTid) g_mainTid = Early_MainThread();
	const bool main = GetCurrentThreadId() == g_mainTid;
	g_calls++;
	uint64_t size = 0, time = 0;
	std::string key;
	Entry* en = nullptr;
	bool id = false;
	if (g_mode && path) {
		key = Lower(path);
		id = FileId(path, size, time);
		if (!id) { if (g_noId++ < 3) Log("zipidx: no size/time for '%s': not baked", path); }
		else {
			WaitLoaded();
			AcquireSRWLockShared(&g_lock);
			auto it = g_map.find(key);
			if (it != g_map.end() && it->second->size == size && it->second->time == time &&
			    it->second->flags == *(const uint32_t*)(f + 0x4c)) en = it->second;
			ReleaseSRWLockShared(&g_lock);
			uint32_t off, cds;
			if (en) { memcpy(&off, en->hdrA, 4); memcpy(&cds, en->hdrA + 4, 4); }
			if (en && !EocdOk(path, size, off, cds)) { en = nullptr; g_eocdMiss++; }
		}
	}
	if (en && g_mode == 1) {
		const uint32_t flags = en->flags;
		const size_t nameSz = (flags & 4) ? 1 : strlen(path) + 1;
		const uint64_t total = nameSz + kTree + en->tree;
		if (!*(volatile int*)(g_base + kRvaAllocFirst)) ((SetupFn)(g_base + kRvaAllocSetup))();
		int actual = 0;
		char* c = (*(AllocFn*)(g_base + kRvaAllocFn))(total, &actual, 0);
		if (c) {
			InterlockedAdd((volatile LONG*)(g_base + kRvaStatActual), actual);
			InterlockedAdd((volatile LONG*)(g_base + kRvaStatAsked), (LONG)total);
			InterlockedIncrement((volatile LONG*)(g_base + kRvaStatCount));
			g_init(c, f + 8, *(uint64_t*)(f + 0x70), en->tree, flags, total);
			*(uint64_t*)(c + 0x50) = en->size;   // the pak's size (the game: factory +0x68 from its end-record read)
			memcpy(c + kHdrA, en->hdrA, kHdrAEnd - kHdrA);
			memcpy(c + kHdrB, en->hdrB, kHdrBEnd - kHdrB);
			InterlockedIncrement(*(volatile LONG**)c);
			memcpy(c + kTree, en->treeBytes, en->tree);
			if (flags & 4) c[kTree + en->tree] = 0;
			else memcpy(c + kTree + en->tree, path, nameSz);
			g_close(f);
			*out = c;
			g_hits++;
			const long long d = Now() - t0;
			g_ticksAll += d; if (main) g_ticksMain += d;
			return out;
		}
		Log("zipidx: the game's allocator gave no %llu bytes for %s: built by the game", (unsigned long long)total, path);
	}
	void** r = g_orig(f, out, path);
	const long long d = Now() - t0;
	g_ticksAll += d; if (main) g_ticksMain += d;
	char* c = r ? (char*)*r : nullptr;
	if (c && id) {
		if (en) Verify(en, c, path);
		else { g_misses++; g_lastMiss = Now(); Record(path, key, size, time, c); }
	}
	return r;
}

}  // namespace

void Zipidx_Report(const char* when) {
	if (!g_installed) return;
	Log("zipidx (%s): mode %d, %d archive indexes made: %d from the bake, %d built and baked, %d verified same, %d differ; "
	    "%d end records changed; %.0f ms in all (%.0f ms on Main), %.1f ms waiting for the bake file",
	    when, g_mode, (int)g_calls, (int)g_hits, (int)g_misses, (int)g_verifyOk, (int)g_verifyBad, (int)g_eocdMiss,
	    Ms(g_ticksAll), Ms(g_ticksMain), Ms(g_ticksLoadWait));
}

void Zipidx_Install(uintptr_t base, void* self) {
	if (g_installed.exchange(true)) return;
	LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); g_freq = fq.QuadPart;
	g_base = base;
	g_mainTid = 0;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((dir + "fastload_zipidx.txt").c_str(), "r")) {
		if (fscanf(fp, "%d", &g_mode) != 1) g_mode = 0;
		fclose(fp);
	}
	if (g_mode < 0 || g_mode > 2) g_mode = 0;
	g_file = dir + "fastload_zipidx.bin";
	const auto* nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
	g_stamp = nt->FileHeader.TimeDateStamp;
	if (memcmp((void*)(base + kRvaMake), kMakeBytes, sizeof kMakeBytes) ||
	    memcmp((void*)(base + kRvaInit), kInitBytes, sizeof kInitBytes) ||
	    memcmp((void*)(base + kRvaClose), kCloseBytes, sizeof kCloseBytes)) {
		Log("zipidx: off: ZipDir's cache code differs (another game version?)");
		return;
	}
	g_init = (InitFn)(base + kRvaInit);
	g_close = (CloseFn)(base + kRvaClose);
	g_loadDone = CreateEventA(nullptr, TRUE, FALSE, nullptr);
	if (g_mode) {
		if (HANDLE t = CreateThread(nullptr, 0, LoadThread, nullptr, 0, nullptr)) CloseHandle(t);
		else SetEvent(g_loadDone);
		if (HANDLE t = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr)) CloseHandle(t);
	} else {
		SetEvent(g_loadDone);
		if (HANDLE t = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr)) CloseHandle(t);
	}
	void* p = (void*)(base + kRvaMake);
	if (MH_CreateHook(p, (void*)&HkMake, (void**)&g_orig) != MH_OK || MH_EnableHook(p) != MH_OK) {
		Log("zipidx: off: hook failed");
		return;
	}
	Log("zipidx: mode %d (%s), hook on", g_mode, g_mode == 1 ? "bake + use" : g_mode == 2 ? "verify" : "time only");
}

