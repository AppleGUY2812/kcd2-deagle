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

struct Rec { uint32_t tMs, tid, pak, dataOff, csize, usize, us; uint16_t method; uint8_t decompress, part; };
std::mutex g_mu;
std::vector<Rec> g_recs;
std::unordered_map<uintptr_t, uint32_t> g_pakIds;      // FILE* or in-memory pak -> index into g_paks
std::vector<std::string> g_paks;
std::atomic<long long> g_calls{0};

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
		r.csize = (uint32_t)entry[1]; r.usize = (uint32_t)entry[2]; r.dataOff = (uint32_t)entry[3];
		r.method = *(const uint16_t*)((const char*)entry + 0x18);
	} __except (EXCEPTION_EXECUTE_HANDLER) {}
}
void ReadZipFile(const void* cache, uintptr_t& file, uintptr_t& inMem) {
	__try { const uintptr_t* zf = (const uintptr_t*)((const char*)cache + 8); file = zf[0]; inMem = zf[7]; }
	__except (EXCEPTION_EXECUTE_HANDLER) {}
}

uint64_t __fastcall HkRead(void* cache, int* entry, void* comp, void* uncomp, char decompress, int64_t off, uint64_t size,
                           char skipCrc) {
	if (g_mode != 1 || !entry || GetTickCount64() - g_start > kRecordMs) return g_orig(cache, entry, comp, uncomp, decompress, off, size, skipCrc);
	Rec r{};
	ReadEntry(entry, r);
	r.decompress = decompress ? 1 : 0;
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

void Flush() {
	std::vector<Rec> recs; std::vector<std::string> paks;
	{ std::lock_guard<std::mutex> lk(g_mu); recs = g_recs; paks = g_paks; }
	const std::string tmp = g_dir + "deagle_pakahead_trace.tmp", out = g_dir + "deagle_pakahead_trace.csv";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f) return;
	fprintf(f, "# paks\n");
	for (size_t i = 0; i < paks.size(); i++) fprintf(f, "#%zu,%s\n", i, paks[i].c_str());
	fprintf(f, "t_ms,tid,pak,data_off,csize,usize,method,decompress,part,us\n");
	for (const Rec& r : recs)
		fprintf(f, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", r.tMs, r.tid, r.pak, r.dataOff, r.csize, r.usize, r.method, r.decompress,
		        r.part, r.us);
	fclose(f);
	MoveFileExA(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING);
}

DWORD WINAPI Writer(LPVOID) {
	size_t written = 0;
	for (;;) {
		Sleep(5000);
		size_t n; { std::lock_guard<std::mutex> lk(g_mu); n = g_recs.size(); }
		if (n != written) { Flush(); written = n; }
		if (GetTickCount64() - g_start > kRecordMs + 10000) {
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
		int m = 1; if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 1) g_mode = m; fclose(fp);
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
	Log("pakahead: mode %d (record), ZipDir ReadFile hook %s, pak names %s", g_mode, ok ? "on" : "FAILED",
	    g_fileno && g_osHandle ? "via ucrtbase" : "UNAVAILABLE");
	if (ok) CloseHandle(CreateThread(nullptr, 0, Writer, nullptr, 0, nullptr));
}

void Pakahead_Command(const char* args) {
	(void)args;
	size_t n, p; { std::lock_guard<std::mutex> lk(g_mu); n = g_recs.size(); p = g_paks.size(); }
	Log("pakahead: mode %d, %zu reads recorded from %zu paks", g_mode, n, p);
}
