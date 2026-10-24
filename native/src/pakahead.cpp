// Unpacking ahead, step 1: the recorder. See pakahead.h.
#include "pakahead.h"
#include "log.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "MinHook.h"
#include "libdeflate.h"
#include <algorithm>
#include <deque>

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

// ---- step 2: unpack ahead (mode 2). deagle_pakahead_list.txt next to the plugin, made by tools/pakahead_list.py from a
// recording: one line per entry in the order the load needs them: "<pak path>\t<data offset>\t<packed>\t<size>\t<crc32>"
// (entries checked against the pak's own directory). Workers read + unpack them into RAM from game start, at background
// priority; the ReadFile hook copies a finished entry into the game's buffer instead of reading and unpacking it.
constexpr int kWorkers = 6;
constexpr size_t kCapBytes = 2048ull << 20;        // unpacked bytes held at once
constexpr ULONGLONG kKeepMs = 15 * 60 * 1000;     // nothing served after this: everything is freed
struct Item {
	uint32_t pak, dataOff, csize, usize, crc;
	std::atomic<int> state{0};                    // 0 waiting, 1 a worker has it, 2 ready, 3 taken or dropped
	uint8_t* data = nullptr;
};
std::vector<std::string> g_listPaks;               // pak paths of the list, lower case
std::deque<Item> g_items;                          // deque: Items never move (atomics inside)
std::unordered_map<uint64_t, Item*> g_byKey;       // (list pak index << 32) | data offset
std::unordered_map<uintptr_t, int> g_fileToListPak; // the game's FILE* -> list pak index (-1 = not in the list)
std::mutex g_mapMu;
std::atomic<size_t> g_next{0}, g_held{0};
std::atomic<long long> s_served{0}, s_servedBytes{0}, s_missNotReady{0}, s_prefetched{0}, s_bad{0}, s_readMs{0}, s_unpackMs{0};
std::atomic<bool> g_stop{false};

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

void Release(Item& it) {
	if (it.data) { free(it.data); it.data = nullptr; g_held -= it.usize; }
}

// mode 2: a finished entry for this read? copies it into the game's buffer; true = served (the game's read is skipped)
bool TryServe(void* cache, const Rec& r, void* comp, void* uncomp, int64_t off, uint64_t size) {
	if (g_items.empty() || g_stop || !uncomp || off != 0 || (size != ~0ull && size != (uint64_t)r.usize)) return false;
	if (!r.decompress && r.method != 0) return false;           // the caller wants the packed bytes
	if (r.crc == 0) return false;                                // entry already read and checked once: not first use
	uintptr_t file = 0, inMem = 0;
	ReadZipFile(cache, file, inMem);
	if (inMem || !file) return false;
	const int lp = ListPakOf(file);
	if (lp < 0) return false;
	auto f = g_byKey.find(((uint64_t)lp << 32) | r.dataOff);
	if (f == g_byKey.end()) return false;
	Item& it = *f->second;
	if (it.usize != r.usize || it.csize != r.csize || it.crc != r.crc) return false;   // same pak, place, sizes, crc32
	int ready = 2;
	if (!it.state.compare_exchange_strong(ready, 3)) {
		int waiting = 0;
		it.state.compare_exchange_strong(waiting, 3);   // not started: the workers skip it; being unpacked: dropped after
		s_missNotReady++;
		return false;
	}
	memcpy(uncomp, it.data, it.usize);
	Release(it);
	s_served++;
	s_servedBytes += r.usize;
	return true;
}

uint64_t __fastcall HkRead(void* cache, int* entry, void* comp, void* uncomp, char decompress, int64_t off, uint64_t size,
                           char skipCrc) {
	if (!entry) return g_orig(cache, entry, comp, uncomp, decompress, off, size, skipCrc);
	const bool record = g_mode >= 1 && GetTickCount64() - g_start <= kRecordMs;
	Rec r{};
	ReadEntry(entry, r);
	if (g_mode == 2) {
		const long long a = Now();
		if (TryServe(cache, r, comp, uncomp, off, size)) {
			if (!record) return 0;
			r.tMs = (uint32_t)((a - g_t0) * 1000 / g_freq);
			r.us = (uint32_t)((Now() - a) * 1000000 / g_freq);
			r.tid = GetCurrentThreadId();
			r.decompress = 2;                         // 2 = served from the unpack-ahead store
			uintptr_t file = 0, inMem = 0;
			ReadZipFile(cache, file, inMem);
			std::lock_guard<std::mutex> lk(g_mu);
			auto it = g_pakIds.find(file);
			if (it == g_pakIds.end()) { it = g_pakIds.emplace(file, (uint32_t)g_paks.size()).first; g_paks.push_back(PakName(file, 0)); }
			r.pak = it->second;
			g_recs.push_back(r);
			return 0;
		}
	}
	if (!record) return g_orig(cache, entry, comp, uncomp, decompress, off, size, skipCrc);
	r.decompress = decompress ? 1 : 0;
	r.hasComp = comp ? 1 : 0;
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
	g_recs.push_back(r);
	g_calls++;
	return ret;
}

// ---- workers
DWORD WINAPI Worker(LPVOID) {
	SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);   // low CPU and I/O priority: the menu stays smooth
	libdeflate_decompressor* d = libdeflate_alloc_decompressor();
	std::vector<HANDLE> handles(g_listPaks.size(), INVALID_HANDLE_VALUE);
	std::vector<uint8_t> packed;
	const size_t n = g_items.size();
	for (;;) {
		if (g_stop) break;
		const size_t i = g_next++;
		if (i >= n) break;
		Item& it = g_items[i];
		int waiting = 0;
		if (!it.state.compare_exchange_strong(waiting, 1)) continue;   // the game already took it
		while (!g_stop && g_held.load() + it.usize > kCapBytes) Sleep(20);   // full: wait for the load to take some
		if (g_stop) { it.state = 3; break; }
		HANDLE& h = handles[it.pak];
		if (h == INVALID_HANDLE_VALUE)
			h = CreateFileA(g_listPaks[it.pak].c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
			                FILE_ATTRIBUTE_NORMAL, nullptr);
		bool ok = h != INVALID_HANDLE_VALUE;
		uint8_t* out = ok ? (uint8_t*)malloc(it.usize ? it.usize : 1) : nullptr;
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
		if (!ok) { free(out); s_bad++; it.state = 3; continue; }
		g_held += it.usize;
		it.data = out;
		int working = 1;
		if (!it.state.compare_exchange_strong(working, 2)) Release(it);   // the game asked meanwhile and did it itself
		else s_prefetched++;
	}
	for (HANDLE h : handles) if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
	libdeflate_free_decompressor(d);
	return 0;
}

bool LoadList() {
	FILE* f = fopen((g_dir + "deagle_pakahead_list.txt").c_str(), "r");
	if (!f) { Log("pakahead: no deagle_pakahead_list.txt - nothing to unpack ahead"); return false; }
	char line[1200];
	size_t bytes = 0;
	while (fgets(line, sizeof line, f)) {
		char path[1024]; unsigned off, cs, us, crc;
		if (line[0] == '#' || sscanf(line, "%1023[^\t]\t%u\t%u\t%u\t%x", path, &off, &cs, &us, &crc) != 5) continue;
		const std::string p = Lower(path);
		uint32_t idx = 0;
		while (idx < g_listPaks.size() && g_listPaks[idx] != p) idx++;
		if (idx == g_listPaks.size()) g_listPaks.push_back(p);
		g_items.emplace_back();
		Item& it = g_items.back();
		it.pak = idx; it.dataOff = off; it.csize = cs; it.usize = us; it.crc = crc;
		g_byKey.emplace(((uint64_t)idx << 32) | off, &it);
		bytes += us;
	}
	fclose(f);
	Log("pakahead: list %zu entries from %zu paks, %.0f MB unpacked", g_items.size(), g_listPaks.size(), bytes / 1e6);
	return !g_items.empty();
}

void StartAhead() {
	MEMORYSTATUSEX m{ sizeof m };
	GlobalMemoryStatusEx(&m);
	const unsigned long long freeMb = m.ullAvailPhys >> 20;
	if (freeMb < 4096) { Log("pakahead: only %llu MB RAM free - not unpacking ahead", freeMb); g_items.clear(); g_byKey.clear(); return; }
	for (int i = 0; i < kWorkers; i++) CloseHandle(CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr));
	Log("pakahead: %d workers unpacking ahead (%llu MB RAM free, cap %zu MB)", kWorkers, freeMb, kCapBytes >> 20);
}

void Flush() {
	std::vector<Rec> recs; std::vector<std::string> paks;
	{ std::lock_guard<std::mutex> lk(g_mu); recs = g_recs; paks = g_paks; }
	const std::string tmp = g_dir + "deagle_pakahead_trace.tmp", out = g_dir + "deagle_pakahead_trace.csv";
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

DWORD WINAPI Writer(LPVOID) {
	size_t written = 0;
	for (;;) {
		Sleep(5000);
		size_t n; { std::lock_guard<std::mutex> lk(g_mu); n = g_recs.size(); }
		if (n != written) { Flush(); written = n; }
		static long long lastServed = -1, lastPre = -1;
		if (!g_items.empty() && (s_served != lastServed || s_prefetched != lastPre)) {
			lastServed = s_served; lastPre = s_prefetched;
			Log("pakahead: ahead %lld of %zu ready (read %lld ms, unpack %lld ms, bad %lld), served %lld (%.0f MB), game "
			    "came first %lld, held %.0f MB", s_prefetched.load(), g_items.size(), s_readMs.load(), s_unpackMs.load(),
			    s_bad.load(), s_served.load(), s_servedBytes.load() / 1e6, s_missNotReady.load(), g_held.load() / 1e6);
		}
		if (!g_items.empty() && !g_stop && GetTickCount64() - g_start > kKeepMs) {
			g_stop = true;
			Sleep(500);
			size_t freed = 0;
			for (Item& it : g_items) { int r2 = 2; if (it.state.compare_exchange_strong(r2, 3)) { freed += it.usize; Release(it); } }
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
	if (FILE* fp = fopen((g_dir + "deagle_pakahead.txt").c_str(), "r")) {
		int m = 1; if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 2) g_mode = m; fclose(fp);
	}
	if (g_mode == 0) { Log("pakahead: off"); return; }
	if (HMODULE crt = GetModuleHandleA("ucrtbase.dll")) {
		g_fileno = (FilenoFn)GetProcAddress(crt, "_fileno");
		g_osHandle = (OsHandleFn)GetProcAddress(crt, "_get_osfhandle");
	}
	void* rd = (void*)(base + kRvaRead);
	if (memcmp(rd, kReadBytes, sizeof kReadBytes)) { Log("pakahead: code check failed - not installed (other game version?)"); return; }
	g_recs.reserve(150000);
	const bool ok = MH_CreateHook(rd, (void*)&HkRead, (void**)&g_orig) == MH_OK && MH_EnableHook(rd) == MH_OK;
	Log("pakahead: mode %d (%s), ZipDir ReadFile hook %s, pak names %s", g_mode, g_mode == 2 ? "record + unpack ahead" : "record",
	    ok ? "on" : "FAILED", g_fileno && g_osHandle ? "via ucrtbase" : "UNAVAILABLE");
	if (ok && g_mode == 2 && LoadList()) StartAhead();
	if (ok) CloseHandle(CreateThread(nullptr, 0, Writer, nullptr, 0, nullptr));
}

void Pakahead_Command(const char* args) {
	(void)args;
	size_t n, p; { std::lock_guard<std::mutex> lk(g_mu); n = g_recs.size(); p = g_paks.size(); }
	Log("pakahead: mode %d, %zu reads recorded from %zu paks", g_mode, n, p);
}
